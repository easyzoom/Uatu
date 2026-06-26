# Cross-Compilation Guide

uatu supports cross-compilation to Linux targets beyond x86_64.  
Platform-specific register names and ptrace APIs are abstracted in `include/uatu/platform/arch.h`.

## Supported Targets

| Architecture | Toolchain prefix | eBPF arch flag | ptrace API |
|---|---|---|---|
| x86_64 (native) | — | `x86` | `PTRACE_GETREGS` |
| aarch64 (ARM64) | `aarch64-linux-gnu-` | `arm64` | `PTRACE_GETREGSET` |
| riscv64 | `riscv64-linux-gnu-` | `riscv` | `PTRACE_GETREGSET` |

## Install Cross Toolchains (Ubuntu/Debian)

```bash
# ARM64
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

# RISC-V 64
sudo apt install gcc-riscv64-linux-gnu g++-riscv64-linux-gnu binutils-riscv64-linux-gnu
```

For the cross-compiled binary to link against libdw, libelf, and libbpf you need
their ARM64/riscv64 variants in a sysroot.  The simplest approach on Debian-based
systems is to enable multi-arch:

```bash
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install libdw-dev:arm64 libelf-dev:arm64 libbpf-dev:arm64
```

## Build for ARM64

```bash
cmake -B build-aarch64 -S . \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64 -j$(nproc)
```

The resulting binary is at `build-aarch64/src/cli/uatu`.

## Build for RISC-V 64

```bash
cmake -B build-riscv64 -S . \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-linux-gnu.cmake \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-riscv64 -j$(nproc)
```

## eBPF Program Compilation

The BPF kernel programs (`.bpf.c`) are always compiled by the **host** clang,
but the BPF target architecture is set automatically from `CMAKE_SYSTEM_PROCESSOR`:

| Target CPU | `__TARGET_ARCH_*` used |
|---|---|
| x86_64 | `__TARGET_ARCH_x86` |
| aarch64 | `__TARGET_ARCH_arm64` |
| riscv64 | `__TARGET_ARCH_riscv` |

You can override this with `-DBPF_TARGET_ARCH=<arch>` if needed.

## Platform Abstraction Notes

`include/uatu/platform/arch.h` handles:

- **Register access**: `get_fp()`, `get_ip()`, `get_sp()`, `get_ret()`, `set_ip()`
- **ptrace API**: `PTRACE_GETREGS` on x86_64; `PTRACE_GETREGSET + NT_PRSTATUS` on aarch64/riscv64
- **Breakpoint instruction**: INT3 (1 byte) on x86_64; BRK (4 bytes) on aarch64; EBREAK (4 bytes) on riscv64
- **PC adjustment**: x86_64 RIP advances 1 byte past INT3 on trap (subtract 1); aarch64/riscv64 PC stays at the breakpoint (no adjustment)
