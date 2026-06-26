#include "uatu/engine/attach_engine.h"
#include "uatu/dwarf/symbol_finder.h"
#include "uatu/ebpf/uprobe_loader.h"
#include "uatu/platform/arch.h"
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <array>
#include <elf.h>
#include <fcntl.h>
#include <map>
#include <optional>
#include <unordered_map>

namespace uatu {

namespace {

std::string resolve_exe(int pid) {
    char link[64];
    char buf[512];
    snprintf(link, sizeof(link), "/proc/%d/exe", pid);
    ssize_t n = readlink(link, buf, sizeof(buf) - 1);
    if (n <= 0) throw std::runtime_error("cannot resolve /proc/" + std::to_string(pid) + "/exe");
    buf[n] = '\0';
    return buf;
}

// Strip leading const/volatile qualifiers for type classification.
static std::string strip_cv(const std::string& t) {
    std::string s = t;
    for (;;) {
        if (s.size() >= 6 && s.substr(0, 6) == "const ")    { s = s.substr(6); continue; }
        if (s.size() >= 9 && s.substr(0, 9) == "volatile ") { s = s.substr(9); continue; }
        break;
    }
    return s;
}

// Returns true for types passed in XMM registers (not captured by our BPF program).
static bool is_fp_param(const std::string& type) {
    const std::string s = strip_cv(type);
    return s == "float" || s == "double" || s == "long double";
}

// Format a single 64-bit register value according to its DWARF type name.
static std::string format_param_val(uint64_t v, const std::string& type) {
    const std::string s = strip_cv(type);

    if (s == "bool" || s == "_Bool")
        return v ? "true" : "false";

    // Signed integer types
    if (s == "int"   || s == "signed"     || s == "signed int")
        return std::to_string(static_cast<int32_t>(static_cast<uint32_t>(v)));
    if (s == "short" || s == "short int"  || s == "signed short" || s == "signed short int")
        return std::to_string(static_cast<int16_t>(static_cast<uint16_t>(v)));
    if (s == "char"  || s == "signed char")
        return std::to_string(static_cast<int8_t>(static_cast<uint8_t>(v)));
    if (s == "long"       || s == "long int"       ||
        s == "signed long"|| s == "signed long int" ||
        s == "long long"  || s == "long long int")
        return std::to_string(static_cast<int64_t>(v));

    // Pointer types: show as hex; char* gets a string hint
    if (!s.empty() && s.back() == '*') {
        if (v == 0) return "nullptr";
        std::ostringstream oss;
        if (s.find("char") != std::string::npos)
            oss << "\"<0x" << std::hex << v << ">\"";
        else
            oss << "0x" << std::hex << v;
        return oss.str();
    }

    // Reference types
    if (!s.empty() && s.back() == '&') {
        std::ostringstream oss;
        oss << "&0x" << std::hex << v;
        return oss.str();
    }

    // Default: unsigned decimal
    return std::to_string(v);
}

// Apply the x86_64 SysV ABI to produce formatted param strings.
// Integer/pointer args use RDI/RSI/RDX/RCX/R8/R9 (args[0..5]).
// Floating-point args use XMM0..7 — not captured; shown as "<xmmN>".
static std::vector<std::string> format_params(
        const std::vector<std::string>& param_types,
        const uint64_t args[6],
        bool has_this) {
    std::vector<std::string> result;
    result.reserve(param_types.size());
    // Member functions pass 'this' in args[0] even though it's not in param_types;
    // skip that slot so explicit params map to the correct registers.
    int int_reg = has_this ? 1 : 0;
    int fp_reg  = 0;
    for (const auto& t : param_types) {
        if (is_fp_param(t)) {
            result.push_back("<xmm" + std::to_string(fp_reg++) + ">");
        } else if (int_reg < 6) {
            result.push_back(format_param_val(args[int_reg++], t));
        } else {
            result.push_back("?");  // stack-passed args not captured
        }
    }
    return result;
}

std::string format_int_val(uint64_t v, const std::string& type_name) {
    return format_param_val(v, type_name);
}

#ifndef BPF_OBJ_PATH
#define BPF_OBJ_PATH "/tmp/watch.bpf.o"
#endif

} // namespace

// Forward declarations for file-scope helpers used by Impl::get_aslr_slide().
static uint64_t elf_min_vaddr(const std::string& path);
static uint64_t get_load_base(int pid, const std::string& exe_path);

struct AttachEngine::Impl {
    int         pid;
    std::string exe_path;

    // Lazily-initialised caches — valid for the lifetime of the engine.
    std::unique_ptr<dwarf::SymbolFinder> finder_;
    std::optional<uint64_t>              cached_elf_min_vaddr_;
    std::optional<uint64_t>              cached_maps_base_;

    // Returns the shared SymbolFinder, constructing it on first call.
    // Throws if the binary has no DWARF info (stripped).
    dwarf::SymbolFinder& get_finder() {
        if (!finder_)
            finder_ = std::make_unique<dwarf::SymbolFinder>(exe_path);
        return *finder_;
    }

    // Returns (maps_base - elf_min_vaddr): the ASLR slide to add to DWARF
    // link-time addresses to get runtime addresses.  Both components are
    // cached after the first call.
    uint64_t get_aslr_slide() {
        if (!cached_elf_min_vaddr_)
            cached_elf_min_vaddr_ = elf_min_vaddr(exe_path);
        if (!cached_maps_base_)
            cached_maps_base_ = get_load_base(pid, exe_path);
        return *cached_maps_base_ - *cached_elf_min_vaddr_;
    }
};

AttachEngine::AttachEngine(int pid)
    : impl_(std::make_unique<Impl>()) {
    impl_->pid      = pid;
    impl_->exe_path = resolve_exe(pid);
}

AttachEngine::~AttachEngine() = default;

tl::expected<std::vector<WatchEvent>, EngineError>
AttachEngine::watch_checked(const std::string& func_name,
                            int max_events, int timeout_ms) {
    // Step 1: DWARF lookup — must succeed before touching BPF
    FuncInfo info;
    try {
        auto opt = impl_->get_finder().find(func_name);
        if (!opt)
            return tl::unexpected(EngineError{"function not found in DWARF: " + func_name});
        info = *opt;
    } catch (const std::exception& ex) {
        // get_finder() throws "no DWARF debug info in: ..." for stripped binaries
        std::string msg = ex.what();
        if (msg.find("DWARF") == std::string::npos)
            msg = "no DWARF debug info: " + msg;
        return tl::unexpected(EngineError{msg});
    }

    if (info.address == 0)
        return tl::unexpected(EngineError{"function has no address (inlined?): " + func_name});

    // Step 2: ptrace fallback helper — used when .bpf.o is absent or eBPF load fails.
    auto ptrace_watch = [&]() -> tl::expected<std::vector<WatchEvent>, EngineError> {
        auto nodes = trace(func_name, max_events, timeout_ms);
        std::vector<WatchEvent> evs;
        for (auto& node : nodes) {
            WatchEvent ev;
            ev.func_name   = node.func_name;
            ev.duration_ns = node.duration_ns;
            ev.ret_value   = format_int_val(node.ret_raw, info.return_type);
            evs.push_back(std::move(ev));
        }
        return evs;
    };

    // If the compiled eBPF object is absent, use ptrace directly.
    if (access(BPF_OBJ_PATH, R_OK) != 0)
        return ptrace_watch();

    // eBPF path: any exception (e.g. no CAP_BPF) degrades gracefully to ptrace.
    std::vector<WatchEvent> events;
    events.reserve(max_events);
    // Cache entry-event args by TID: the BPF program clears args[] in exit events,
    // so we must save them when the entry event arrives and look them up on exit.
    std::unordered_map<uint32_t, std::array<uint64_t, 6>> entry_args;
    try {
        ebpf::UprobeLoader loader(impl_->pid, BPF_OBJ_PATH);
        loader.attach(info.address);

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (static_cast<int>(events.size()) < max_events) {
            auto now       = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            int remaining = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());

            loader.poll(remaining, [&](const ebpf::RawEvent& raw) {
                if (!raw.is_exit) {
                    // Entry event: save args[0..5] keyed by TID for later.
                    std::array<uint64_t, 6> a;
                    std::copy(std::begin(raw.args), std::end(raw.args), a.begin());
                    entry_args[raw.tid] = a;
                    return;
                }
                if (static_cast<int>(events.size()) >= max_events) return;

                WatchEvent ev;
                ev.timestamp_ns = raw.timestamp_ns;
                ev.duration_ns  = raw.duration_ns;
                ev.func_name    = func_name;
                ev.ret_value    = format_int_val(raw.ret_val, info.return_type);

                auto it = entry_args.find(raw.tid);
                if (it != entry_args.end()) {
                    ev.params = format_params(info.param_types, it->second.data(),
                                              info.has_this);
                    entry_args.erase(it);
                }

                events.push_back(std::move(ev));
            });
        }

        loader.detach_all();
        return events;
    } catch (const std::exception&) {
        return ptrace_watch();
    }
}

std::vector<WatchEvent> AttachEngine::watch(const std::string& func_name,
                                            int max_events, int timeout_ms) {
    auto result = watch_checked(func_name, max_events, timeout_ms);
    if (!result)
        throw std::runtime_error("watch error: " + result.error().message);
    return *result;
}

// ---------------------------------------------------------------------------
// trace() — collect call-tree nodes using ptrace software breakpoints.
//
// Strategy:
//   1. Resolve function virtual address via DWARF + /proc/<pid>/maps (ASLR).
//   2. PTRACE_ATTACH, insert INT3 (0xCC) at function entry.
//   3. PTRACE_CONT; on SIGTRAP read the return address from the stack (RSP),
//      record entry timestamp, insert a second INT3 at the return address.
//   4. On next SIGTRAP (at return address) record exit timestamp, remove
//      return-address breakpoint, restore original byte, emit TraceNode.
//   5. Re-arm entry breakpoint and repeat until count reached or timeout.
//   6. Restore all patched bytes and PTRACE_DETACH.
//
// Note: ASLR means the mapped address = base (from /proc/<pid>/maps) +
//       (symbol_address − file_load_bias).  For PIE executables the
//       file_load_bias is the lowest PT_LOAD p_vaddr (usually 0).
// ---------------------------------------------------------------------------

// Return the lowest PT_LOAD p_vaddr from the ELF file (the link-time base).
// For PIE this is 0; for non-PIE it is the fixed load address (e.g. 0x400000).
// Used together with get_load_base() to compute the true ASLR slide:
//   slide = get_load_base(pid) - elf_min_vaddr(exe)
//   runtime_addr = dwarf_addr + slide
static uint64_t elf_min_vaddr(const std::string& path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return 0;

    Elf64_Ehdr ehdr{};
    if (pread(fd, &ehdr, sizeof(ehdr), 0) != static_cast<ssize_t>(sizeof(ehdr)) ||
        memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd);
        return 0;
    }

    uint64_t min_vaddr = 0;
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr phdr{};
        off_t off = static_cast<off_t>(ehdr.e_phoff) +
                    static_cast<off_t>(i) * ehdr.e_phentsize;
        if (pread(fd, &phdr, sizeof(phdr), off) != static_cast<ssize_t>(sizeof(phdr)))
            break;
        if (phdr.p_type == PT_LOAD && phdr.p_offset == 0) {
            min_vaddr = phdr.p_vaddr;
            break;
        }
    }
    close(fd);
    return min_vaddr;
}

// Return the runtime load address for our executable in the target process.
// For a PIE, this is the start address of the first LOAD segment (offset 0).
static uint64_t get_load_base(int pid, const std::string& exe_path) {
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream f(maps_path);
    std::string line;
    while (std::getline(f, line)) {
        // Format: start-end perms offset dev ino [path]
        // ASLR base = start address of the FIRST mapping (offset==0) for our exe.
        // Note: on modern Linux, the first mapping is r--p (not r-xp), so we
        // must NOT filter on 'x' here — just find offset==0.
        if (line.find(exe_path) == std::string::npos) continue;
        uint64_t start = 0, end = 0, offset = 0;
        char perms[8]{};
        sscanf(line.c_str(), "%lx-%lx %4s %lx", &start, &end, perms, &offset);
        if (offset == 0) return start;  // this is the ASLR base
    }
    return 0;
}

// Read 8 bytes from target at addr via PTRACE_PEEKDATA.
// Throws on failure (errno check required: -1 is also a valid data value).
static uint64_t ptrace_read8(pid_t pid, uint64_t addr) {
    errno = 0;
    long val = ptrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(addr), nullptr);
    if (errno != 0)
        throw std::runtime_error("PTRACE_PEEKDATA failed at addr 0x" +
                                 [addr]{ std::ostringstream s; s << std::hex << addr; return s.str(); }());
    return static_cast<uint64_t>(val);
}

// Write 8 bytes to target at addr via PTRACE_POKEDATA.
// Throws on failure so callers don't silently corrupt breakpoint state.
static void ptrace_write8(pid_t pid, uint64_t addr, uint64_t val) {
    if (ptrace(PTRACE_POKEDATA, pid, reinterpret_cast<void*>(addr),
               reinterpret_cast<void*>(val)) < 0)
        throw std::runtime_error("PTRACE_POKEDATA failed at addr 0x" +
                                 [addr]{ std::ostringstream s; s << std::hex << addr; return s.str(); }());
}

// Insert a software breakpoint at addr; returns the original 8-byte word.
// Uses platform-specific instruction (INT3 on x86_64, BRK on aarch64, EBREAK on riscv64).
static uint64_t insert_breakpoint(pid_t pid, uint64_t addr) {
    uint64_t orig = ptrace_read8(pid, addr);
    ptrace_write8(pid, addr, make_patched_word(orig));
    return orig;
}

// Restore the original word saved by insert_breakpoint.
static void restore_word(pid_t pid, uint64_t addr, uint64_t orig) {
    ptrace_write8(pid, addr, orig);
}

// waitpid with WNOHANG polling loop; writes result into wstatus.
// Returns true on success, false on timeout or error.
static bool waitpid_timeout(pid_t pid, int& wstatus, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int ws = 0;
        int ret = waitpid(pid, &ws, WNOHANG);
        if (ret > 0) { wstatus = ws; return true; }
        if (ret < 0) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::vector<TraceNode> AttachEngine::trace(const std::string& func_name,
                                           int count, int timeout_ms) {
    // 1. DWARF lookup (SymbolFinder is cached in Impl after first call)
    auto opt = impl_->get_finder().find(func_name);
    if (!opt)
        throw std::runtime_error("trace: function not found in DWARF: " + func_name);
    const FuncInfo& info = *opt;
    if (info.address == 0)
        throw std::runtime_error("trace: function has no address (inlined?): " + func_name);

    // 2. Compute runtime address using cached ASLR slide.
    uint64_t entry_vaddr = info.address + impl_->get_aslr_slide();

    // 3. Attach
    if (ptrace(PTRACE_ATTACH, impl_->pid, nullptr, nullptr) < 0)
        throw std::runtime_error("trace: PTRACE_ATTACH failed: " +
                                 std::string(strerror(errno)));

    int wstatus = 0;

    auto wait_with_timeout = [&](int ms) {
        return waitpid_timeout(impl_->pid, wstatus, ms);
    };

    if (!wait_with_timeout(5000)) {
        ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);
        throw std::runtime_error("trace: timeout waiting for process to stop after PTRACE_ATTACH");
    }

    std::vector<TraceNode> results;
    results.reserve(count);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    // Install entry breakpoint
    uint64_t orig_entry = insert_breakpoint(impl_->pid, entry_vaddr);

    // RAII guard: on any exit path, restore the entry breakpoint byte and detach.
    // Set active=false once we have successfully completed cleanup ourselves.
    struct PtraceGuard {
        pid_t    pid;
        uint64_t addr;
        uint64_t orig_word;
        bool     active{true};

        ~PtraceGuard() {
            if (!active) return;
            ptrace(PTRACE_POKEDATA, pid, reinterpret_cast<void*>(addr),
                   reinterpret_cast<void*>(orig_word));
            ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        }
    };
    PtraceGuard guard{impl_->pid, entry_vaddr, orig_entry};

    // State: are we waiting for entry or exit?
    bool   waiting_for_exit = false;
    uint64_t ret_vaddr      = 0;
    uint64_t orig_ret       = 0;
    uint64_t entry_ts       = 0;

    // Helper: current monotonic time in ns
    auto now_ns = []() -> uint64_t {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    };

    // need_cont tracks whether PTRACE_CONT must be called at the top of the next
    // iteration. Set false when we already called PTRACE_CONT (e.g. signal forward)
    // to avoid double-continuing a running tracee.
    bool need_cont = true;
    while (static_cast<int>(results.size()) < count) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;

        if (need_cont)
            ptrace(PTRACE_CONT, impl_->pid, nullptr, nullptr);
        need_cont = true;

        // Wait for the process to stop (SIGTRAP or other), with timeout.
        // wait_with_timeout stores the result into wstatus.
        auto now2 = std::chrono::steady_clock::now();
        if (now2 >= deadline) break;
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now2).count());
        if (!wait_with_timeout(remaining_ms)) break;
        if (!WIFSTOPPED(wstatus)) break;

        int sig = WSTOPSIG(wstatus);
        if (sig != SIGTRAP) {
            // Deliver non-SIGTRAP signals transparently; process is now running,
            // so skip the PTRACE_CONT at the top of the next iteration.
            ptrace(PTRACE_CONT, impl_->pid, nullptr,
                   reinterpret_cast<void*>(static_cast<uintptr_t>(sig)));
            need_cont = false;
            continue;
        }

        // Read registers via platform-abstracted get_regs()
        NativeRegs regs = get_regs(impl_->pid);
        // PC points one past the breakpoint instruction on x86_64; unchanged on others
        uint64_t hit_addr = get_ip(regs) - PC_ADJUST_AFTER_TRAP;

        if (!waiting_for_exit && hit_addr == entry_vaddr) {
            // --- entry breakpoint hit ---
            entry_ts = now_ns();

            // Restore entry bytes and rewind PC
            restore_word(impl_->pid, entry_vaddr, orig_entry);
            set_ip(regs, entry_vaddr);

            // Read return address from top of stack (SP points to it)
            uint64_t sp = get_sp(regs);
            ret_vaddr = ptrace_read8(impl_->pid, sp);
            orig_ret  = insert_breakpoint(impl_->pid, ret_vaddr);

            set_regs(impl_->pid, regs);

            waiting_for_exit = true;

        } else if (waiting_for_exit && hit_addr == ret_vaddr) {
            // --- return breakpoint hit ---
            uint64_t exit_ts = now_ns();

            TraceNode node;
            node.func_name   = func_name;
            node.duration_ns = exit_ts - entry_ts;
            node.ret_raw     = get_ret(regs);
            results.push_back(std::move(node));

            // Restore return-address bytes and rewind PC
            restore_word(impl_->pid, ret_vaddr, orig_ret);
            set_ip(regs, ret_vaddr);
            set_regs(impl_->pid, regs);

            // Re-arm entry breakpoint for next call
            orig_entry       = insert_breakpoint(impl_->pid, entry_vaddr);
            guard.orig_word  = orig_entry;  // keep guard in sync with current INT3 state
            waiting_for_exit = false;

        } else {
            // Unexpected SIGTRAP (e.g. from another thread or library).
            // Rewind RIP if we happened to hit an INT3 we placed.
            // Safe to just continue; the process will re-execute.
        }
    }

    // Cleanup: remove any installed breakpoints and detach.
    // Disable the RAII guard so it does not double-cleanup.
    guard.active = false;
    if (waiting_for_exit && ret_vaddr != 0)
        restore_word(impl_->pid, ret_vaddr, orig_ret);
    restore_word(impl_->pid, entry_vaddr, orig_entry);

    ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);
    return results;
}

// ---------------------------------------------------------------------------
// stack() — capture call stacks by inserting a breakpoint at the target
// function's entry and walking frames each time the breakpoint fires.
//
// At function entry (before the prologue executes):
//   RSP → return address (pushed by CALL; identifies the caller)
//   RBP = caller's saved frame pointer (prologue has not yet run)
//
// Frame chain built per sample:
//   [0] = func_name  (the function whose entry fired the breakpoint)
//   [1] = sym(*RSP)  (the instruction to return to ≈ the caller)
//   [2+] walked via RBP chain (caller's caller, etc.)
// ---------------------------------------------------------------------------
std::vector<StackEvent> AttachEngine::stack(const std::string& func_name,
                                             int count, int timeout_ms) {
    auto& finder = impl_->get_finder();
    auto  opt    = finder.find(func_name);
    if (!opt)
        throw std::runtime_error("stack: function not found in DWARF: " + func_name);
    if (opt->address == 0)
        throw std::runtime_error("stack: function has no address (inlined?): " + func_name);

    uint64_t aslr_slide  = impl_->get_aslr_slide();
    uint64_t entry_vaddr = opt->address + aslr_slide;

    if (ptrace(PTRACE_ATTACH, impl_->pid, nullptr, nullptr) < 0)
        throw std::runtime_error("stack: PTRACE_ATTACH failed: " +
                                 std::string(strerror(errno)));

    int wstatus = 0;
    auto wait_with_timeout = [&](int ms) {
        return waitpid_timeout(impl_->pid, wstatus, ms);
    };

    if (!wait_with_timeout(5000)) {
        ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);
        throw std::runtime_error("stack: timeout after PTRACE_ATTACH");
    }

    std::vector<StackEvent> results;
    results.reserve(count);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    // sym_for: runtime address → demangled name or "0x…" hex
    auto sym_for = [&](uint64_t runtime_addr) -> std::string {
        uint64_t link_addr = (runtime_addr >= aslr_slide)
                             ? (runtime_addr - aslr_slide) : runtime_addr;
        auto sym = finder.lookup_by_addr(link_addr);
        if (sym) return *sym;
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%016lx", runtime_addr);
        return buf;
    };

    // peek_word: read one 64-bit word from the target process
    auto peek_word = [&](uint64_t addr) -> std::optional<uint64_t> {
        errno = 0;
        long v = ptrace(PTRACE_PEEKDATA, impl_->pid,
                        reinterpret_cast<void*>(addr), nullptr);
        if (errno != 0) return std::nullopt;
        return static_cast<uint64_t>(v);
    };

    uint64_t orig_entry     = insert_breakpoint(impl_->pid, entry_vaddr);
    bool     entry_armed    = true;
    bool     waiting_for_exit = false;
    uint64_t ret_vaddr      = 0;
    uint64_t orig_ret       = 0;

    // RAII guard restores whatever breakpoints remain if we exit early.
    struct PtraceGuard {
        pid_t    pid;
        uint64_t entry_addr, &orig_entry_ref;
        bool&    entry_armed_ref;
        bool&    waiting_for_exit_ref;
        uint64_t& ret_vaddr_ref, &orig_ret_ref;
        bool active{true};
        ~PtraceGuard() {
            if (!active) return;
            if (waiting_for_exit_ref && ret_vaddr_ref)
                ptrace(PTRACE_POKEDATA, pid,
                       reinterpret_cast<void*>(ret_vaddr_ref),
                       reinterpret_cast<void*>(orig_ret_ref));
            if (entry_armed_ref)
                ptrace(PTRACE_POKEDATA, pid,
                       reinterpret_cast<void*>(entry_addr),
                       reinterpret_cast<void*>(orig_entry_ref));
            ptrace(PTRACE_DETACH, pid, nullptr, nullptr);
        }
    };
    PtraceGuard guard{impl_->pid,
                      entry_vaddr, orig_entry, entry_armed,
                      waiting_for_exit, ret_vaddr, orig_ret};

    bool need_cont = true;
    while (static_cast<int>(results.size()) < count) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;

        if (need_cont)
            ptrace(PTRACE_CONT, impl_->pid, nullptr, nullptr);
        need_cont = true;

        auto now2 = std::chrono::steady_clock::now();
        if (now2 >= deadline) break;
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now2).count());
        if (!wait_with_timeout(remaining_ms)) break;
        if (!WIFSTOPPED(wstatus)) break;

        int sig = WSTOPSIG(wstatus);
        if (sig != SIGTRAP) {
            ptrace(PTRACE_CONT, impl_->pid, nullptr,
                   reinterpret_cast<void*>(static_cast<uintptr_t>(sig)));
            need_cont = false;
            continue;
        }

        NativeRegs regs = get_regs(impl_->pid);
        uint64_t hit_addr = get_ip(regs) - PC_ADJUST_AFTER_TRAP;

        if (!waiting_for_exit && hit_addr == entry_vaddr) {
            // --- Entry breakpoint fired ---
            // Restore the original instruction and rewind PC.
            restore_word(impl_->pid, entry_vaddr, orig_entry);
            entry_armed = false;
            set_ip(regs, entry_vaddr);
            set_regs(impl_->pid, regs);

            // Walk the call stack.
            // At entry (before prologue): RSP → return addr, RBP = caller's frame.
            StackEvent ev;
            ev.func_name = func_name;
            ev.timestamp_ns = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            ev.frames.push_back(func_name);

            uint64_t rsp = get_sp(regs);
            uint64_t rbp = get_fp(regs);

            if (auto ra = peek_word(rsp)) {
                ev.frames.push_back(sym_for(*ra));   // direct caller
                for (int i = 0; i < 14 && rbp && rbp % 8 == 0; ++i) {
                    auto saved = peek_word(rbp);
                    auto ret   = peek_word(rbp + 8);
                    if (!ret || *ret == 0) break;
                    ev.frames.push_back(sym_for(*ret));
                    if (!saved || *saved == 0 || *saved <= rbp) break;
                    rbp = *saved;
                }
            }

            results.push_back(std::move(ev));

            if (static_cast<int>(results.size()) >= count) {
                // Have all samples — no need to re-arm; function will run freely.
                break;
            }

            // Need more samples: install return-address breakpoint so we can
            // re-arm the entry breakpoint after this call completes.
            if (auto ra = peek_word(rsp)) {
                ret_vaddr = *ra;
                orig_ret  = insert_breakpoint(impl_->pid, ret_vaddr);
                waiting_for_exit = true;
            } else {
                // Cannot read return address — re-arm entry immediately (best effort).
                orig_entry  = insert_breakpoint(impl_->pid, entry_vaddr);
                entry_armed = true;
            }

        } else if (waiting_for_exit && hit_addr == ret_vaddr) {
            // --- Return-address breakpoint fired: restore and re-arm entry ---
            restore_word(impl_->pid, ret_vaddr, orig_ret);
            set_ip(regs, ret_vaddr);
            set_regs(impl_->pid, regs);
            waiting_for_exit = false;

            orig_entry  = insert_breakpoint(impl_->pid, entry_vaddr);
            entry_armed = true;
            guard.orig_entry_ref = orig_entry;  // keep guard in sync
        }
        // else: unexpected SIGTRAP (another thread, library) — ignore and continue
    }

    guard.active = false;
    if (waiting_for_exit && ret_vaddr)
        restore_word(impl_->pid, ret_vaddr, orig_ret);
    if (entry_armed)
        restore_word(impl_->pid, entry_vaddr, orig_entry);
    ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);
    return results;
}

} // namespace uatu
