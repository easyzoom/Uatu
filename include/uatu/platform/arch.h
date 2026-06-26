#pragma once
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <cstdint>
#include <stdexcept>

// Platform abstraction for ptrace register access and breakpoint instructions.
// Each architecture uses different register names and ptrace APIs.

#if defined(__x86_64__)
#  include <sys/user.h>
   using NativeRegs = struct user_regs_struct;

   inline NativeRegs get_regs(pid_t pid) {
       NativeRegs r{};
       if (ptrace(PTRACE_GETREGS, pid, nullptr, &r) < 0)
           throw std::runtime_error("PTRACE_GETREGS failed");
       return r;
   }
   inline void set_regs(pid_t pid, const NativeRegs& r) {
       if (ptrace(PTRACE_SETREGS, pid, nullptr, &r) < 0)
           throw std::runtime_error("PTRACE_SETREGS failed");
   }
   inline uint64_t get_fp(const NativeRegs& r)       { return r.rbp; }
   inline uint64_t get_ip(const NativeRegs& r)       { return r.rip; }
   inline uint64_t get_sp(const NativeRegs& r)       { return r.rsp; }
   inline uint64_t get_ret(const NativeRegs& r)      { return r.rax; }
   inline void     set_ip(NativeRegs& r, uint64_t v) { r.rip = v; }

   // INT3 = 0xCC (1 byte)
   // After INT3 fires, RIP advances 1 byte past the instruction — must subtract 1 before restoring.
   constexpr int PC_ADJUST_AFTER_TRAP = 1;

   inline uint64_t make_patched_word(uint64_t orig) {
       return (orig & ~0xFFULL) | 0xCCULL;
   }

#elif defined(__aarch64__)
#  include <sys/user.h>
#  include <elf.h>
   using NativeRegs = struct user_regs_struct;

   // aarch64 removed PTRACE_GETREGS in kernel 5.19; use PTRACE_GETREGSET instead.
   inline NativeRegs get_regs(pid_t pid) {
       NativeRegs r{};
       struct iovec iov{ &r, sizeof(r) };
       if (ptrace(PTRACE_GETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &iov) < 0)
           throw std::runtime_error("PTRACE_GETREGSET failed");
       return r;
   }
   inline void set_regs(pid_t pid, const NativeRegs& r) {
       struct iovec iov{ const_cast<NativeRegs*>(&r), sizeof(r) };
       if (ptrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &iov) < 0)
           throw std::runtime_error("PTRACE_SETREGSET failed");
   }
   inline uint64_t get_fp(const NativeRegs& r)       { return r.regs[29]; } // x29 = frame pointer
   inline uint64_t get_ip(const NativeRegs& r)       { return r.pc; }
   inline uint64_t get_sp(const NativeRegs& r)       { return r.sp; }
   inline uint64_t get_ret(const NativeRegs& r)      { return r.regs[0]; }  // x0 = return value
   inline void     set_ip(NativeRegs& r, uint64_t v) { r.pc = v; }

   // BRK #0 = 0xD4200000 (4 bytes, little-endian)
   // After BRK fires, PC still points at the instruction — no adjustment needed.
   constexpr int PC_ADJUST_AFTER_TRAP = 0;

   inline uint64_t make_patched_word(uint64_t orig) {
       return (orig & ~0xFFFFFFFFULL) | 0xD4200000ULL;
   }

#elif defined(__riscv) && __riscv_xlen == 64
#  include <asm/ptrace.h>
#  include <elf.h>
   using NativeRegs = struct user_regs_struct;

   // riscv64 never implemented PTRACE_GETREGS; always use PTRACE_GETREGSET.
   inline NativeRegs get_regs(pid_t pid) {
       NativeRegs r{};
       struct iovec iov{ &r, sizeof(r) };
       if (ptrace(PTRACE_GETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &iov) < 0)
           throw std::runtime_error("PTRACE_GETREGSET failed");
       return r;
   }
   inline void set_regs(pid_t pid, const NativeRegs& r) {
       struct iovec iov{ const_cast<NativeRegs*>(&r), sizeof(r) };
       if (ptrace(PTRACE_SETREGSET, pid, reinterpret_cast<void*>(NT_PRSTATUS), &iov) < 0)
           throw std::runtime_error("PTRACE_SETREGSET failed");
   }
   inline uint64_t get_fp(const NativeRegs& r)       { return r.s0; }  // s0/fp
   inline uint64_t get_ip(const NativeRegs& r)       { return r.pc; }
   inline uint64_t get_sp(const NativeRegs& r)       { return r.sp; }
   inline uint64_t get_ret(const NativeRegs& r)      { return r.a0; }  // a0 = return value
   inline void     set_ip(NativeRegs& r, uint64_t v) { r.pc = v; }

   // EBREAK = 0x00100073 (4 bytes)
   // After EBREAK fires, PC still points at the instruction — no adjustment needed.
   constexpr int PC_ADJUST_AFTER_TRAP = 0;

   inline uint64_t make_patched_word(uint64_t orig) {
       return (orig & ~0xFFFFFFFFULL) | 0x00100073ULL;
   }

#else
#  error "Unsupported architecture. Supported: x86_64, aarch64, riscv64"
#endif
