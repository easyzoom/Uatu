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
#include <elf.h>
#include <fcntl.h>
#include <map>
#include <optional>

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

std::string format_int_val(uint64_t v, const std::string& type_name) {
    if (type_name == "int" || type_name == "long")
        return std::to_string(static_cast<int64_t>(v));
    return std::to_string(v);
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

    // Step 2: BPF attach + poll（若 .bpf.o 不存在则 fallback 到 ptrace 模式）
    std::vector<WatchEvent> events;
    events.reserve(max_events);

    // 检查 eBPF 对象文件是否存在
    bool ebpf_available = (access(BPF_OBJ_PATH, R_OK) == 0);

    if (!ebpf_available) {
        auto trace_nodes = trace(func_name, max_events, timeout_ms);
        for (auto& node : trace_nodes) {
            WatchEvent ev;
            ev.func_name   = node.func_name;
            ev.duration_ns = node.duration_ns;
            ev.ret_value   = format_int_val(node.ret_raw, info.return_type);
            events.push_back(std::move(ev));
        }
        return events;
    }

    // eBPF section: any exception here (e.g. no CAP_BPF) becomes an EngineError
    // so watch_checked() always honours its tl::expected contract.
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
                if (!raw.is_exit) return;
                if (static_cast<int>(events.size()) >= max_events) return;

                WatchEvent ev;
                ev.timestamp_ns = raw.timestamp_ns;
                ev.duration_ns  = raw.duration_ns;
                ev.func_name    = func_name;
                ev.ret_value    = format_int_val(raw.ret_val, info.return_type);
                events.push_back(std::move(ev));
            });
        }

        loader.detach_all();
        return events;
    } catch (const std::exception& ebpf_err) {
        return tl::unexpected(EngineError{std::string("eBPF error: ") + ebpf_err.what()});
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
// stack() — capture a stack trace using ptrace.
// ptrace_scope=1 allows a parent process to trace its children; since the
// test forks the fixture process, ptrace works without root.
// We attach, read RBP/RIP, walk the frame-pointer chain, then detach.
// ---------------------------------------------------------------------------
std::vector<StackEvent> AttachEngine::stack(const std::string& func_name,
                                             int count, int timeout_ms) {
    // Verify the function exists in DWARF
    auto& finder    = impl_->get_finder();
    auto  opt       = finder.find(func_name);
    if (!opt)
        throw std::runtime_error("stack: function not found in DWARF: " + func_name);

    uint64_t aslr_slide = impl_->get_aslr_slide();

    std::vector<StackEvent> results;
    results.reserve(count);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    // Helper: read a 64-bit word from target process memory via ptrace
    auto read_word = [&](uint64_t addr) -> std::optional<uint64_t> {
        errno = 0;
        long val = ptrace(PTRACE_PEEKDATA, impl_->pid,
                          reinterpret_cast<void*>(addr), nullptr);
        if (errno != 0) return std::nullopt;
        return static_cast<uint64_t>(val);
    };

    int collected = 0;
    while (collected < count) {
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;

        // Attach to target
        if (ptrace(PTRACE_ATTACH, impl_->pid, nullptr, nullptr) < 0)
            break;

        int wstatus = 0;
        int rem_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (!waitpid_timeout(impl_->pid, wstatus, rem_ms)) {
            ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);
            break;
        }

        StackEvent ev;
        ev.timestamp_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        ev.func_name = func_name;

        // Read registers via platform-abstracted get_regs()
        try {
            NativeRegs regs = get_regs(impl_->pid);
            uint64_t rbp = get_fp(regs);
            uint64_t rip = get_ip(regs);

            auto sym_for = [&](uint64_t runtime_addr) -> std::string {
                uint64_t link_addr = (runtime_addr >= aslr_slide)
                                     ? (runtime_addr - aslr_slide) : runtime_addr;
                auto sym = finder.lookup_by_addr(link_addr);
                if (sym) return *sym;
                char buf[32];
                snprintf(buf, sizeof(buf), "0x%016lx", runtime_addr);
                return buf;
            };

            ev.frames.push_back(sym_for(rip));

            // Walk up to 16 frames via frame-pointer chain
            for (int i = 0; i < 16 && rbp != 0; ++i) {
                if (rbp % 8 != 0) break;  // alignment guard

                auto saved_rbp = read_word(rbp);
                auto ret_addr  = read_word(rbp + 8);
                if (!ret_addr || *ret_addr == 0) break;

                ev.frames.push_back(sym_for(*ret_addr));

                if (!saved_rbp || *saved_rbp == 0 || *saved_rbp <= rbp) break;
                rbp = *saved_rbp;
            }
        } catch (...) {
            ev.frames.push_back("stack capture unavailable");
        }

        ptrace(PTRACE_DETACH, impl_->pid, nullptr, nullptr);

        results.push_back(std::move(ev));
        collected++;

        // Brief wait before next sample
        if (collected < count) {
            auto remaining_ms = static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count());
            if (remaining_ms > 0) {
                long long sleep_ms_ll = std::min(remaining_ms / 2, (long long)50);
                int sleep_ms = static_cast<int>(sleep_ms_ll);
                if (sleep_ms > 0)
                    usleep(sleep_ms * 1000);
            }
        }
    }

    return results;
}

} // namespace uatu
