# 安装指南

## 系统要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Linux x86_64 |
| 内核版本 | ≥ 4.18（eBPF uprobe 支持）；推荐 ≥ 5.8（CAP_BPF 正式引入） |
| 发行版 | Ubuntu 22.04+ 推荐；Debian 11+、Fedora 36+ 亦可 |
| 编译器 | GCC ≥ 11 或 Clang ≥ 14（C++20 支持） |
| CMake | ≥ 3.20 |

## 权限要求

uatu 提供两种工作模式，权限要求不同：

### eBPF 模式（默认，推荐）

需要以下能力之一：

- **root** — 最简单，直接 `sudo ./uatu`
- **CAP_BPF + CAP_PERFMON** — 最小权限原则：

  ```bash
  sudo setcap cap_bpf,cap_perfmon+ep ./build/src/cli/uatu
  ```

内核版本 < 5.8 时，CAP_BPF 尚未正式拆分，需使用 root。

### ptrace 模式（eBPF 不可用时自动启用）

需要以下条件之一：

- **root** — 总是可以 ptrace
- **ptrace_scope = 0** — 允许任意进程互相 ptrace：

  ```bash
  # 临时设置（重启后恢复）
  echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

  # 永久设置
  echo 'kernel.yama.ptrace_scope = 0' | sudo tee /etc/sysctl.d/10-ptrace.conf
  sudo sysctl -p /etc/sysctl.d/10-ptrace.conf
  ```

- **ptrace_scope = 1**（Ubuntu 默认）— 只允许父进程 ptrace 子进程，uatu 无法 attach 到任意进程。

> **建议：** 开发/测试环境设 ptrace_scope=0；生产环境使用 eBPF 模式并授予 CAP_BPF。

---

## 方式一：从源码构建

### 1. 安装依赖

```bash
sudo apt update
sudo apt install \
  libdw-dev \
  libelf-dev \
  libbpf-dev \
  clang \
  bpftool \
  cmake \
  ninja-build \
  pkg-config \
  g++
```

各依赖用途说明：

| 包名 | 用途 |
|---|---|
| `libdw-dev` | DWARF 调试信息解析（elfutils） |
| `libelf-dev` | ELF 文件读取 |
| `libbpf-dev` | BPF 程序加载（仅作系统版本 fallback；Ubuntu 22.04 的 0.5.0 与 kernel 6.x 不兼容，构建时会自动使用 `third_party/libbpf` 中的 vendored v1.4.3） |
| `clang` | 编译 eBPF 程序（`.bpf.c` → `.bpf.o`） |
| `bpftool` | 生成 BPF skeleton（`.skel.h`） |
| `cmake` | 构建系统 |
| `ninja-build` | 并行构建（可选，推荐） |
| `pkg-config` | 查找库路径 |
| `g++` | 编译 C++ 主程序 |

### 2. 克隆仓库

```bash
git clone https://github.com/YOUR_ORG/uatu.git
cd uatu
```

### 2.5 编译 vendored libbpf（Ubuntu 22.04 + kernel ≥ 6.x 必须）

Ubuntu 22.04 系统 libbpf（0.5.0）与 kernel 6.x 的 BTF 格式不兼容，需先编译仓库内置的 libbpf v1.4.3：

```bash
cd third_party/libbpf/src
make BUILD_STATIC_ONLY=1 OBJDIR=$(pwd)/../../../build/libbpf
make BUILD_STATIC_ONLY=1 OBJDIR=$(pwd)/../../../build/libbpf \
     DESTDIR=$(pwd)/../../../build/libbpf PREFIX="" install
cd ../../..
```

此步骤仅需执行一次（或在清除 `build/` 目录后重新执行）。CMake 会自动检测 `build/libbpf/libbpf.a` 并优先使用它，系统 libbpf 作为 fallback。

### 3. 构建

```bash
# Release 构建（推荐用于生产）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)

# Debug 构建（带调试信息）
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build-debug -j$(nproc)
```

构建产物：

```
build/
└── src/
    └── cli/
        └── uatu          # 主可执行文件
```

### 4. 可选：安装到系统路径

```bash
sudo cmake --install build --prefix /usr/local
# 之后可直接运行 uatu
```

---

## 方式二：安装 Clang（eBPF 编译支持）

uatu 的 eBPF 程序（`ebpf/*.bpf.c`）需要 Clang 编译为 BPF 字节码。

**若系统无 Clang**，uatu 构建时将跳过 eBPF 模块，自动降级到纯 ptrace 模式。功能完整，但 `watch` 命令的开销略高（ptrace 每次调用需暂停目标线程）。

安装最新版 Clang（Ubuntu）：

```bash
# 官方 LLVM apt 源（推荐，可获取最新版本）
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 17   # 安装 Clang 17

# 或使用系统源（版本可能较旧）
sudo apt install clang
```

验证 Clang 可用：

```bash
clang --version
# clang version 17.0.x ...
```

---

## 验证安装

### 运行单元测试

```bash
cd build
ctest --output-on-failure
```

预期输出（全部通过）：

```
Test project /path/to/uatu/build
    Start 1: unit_symbol_finder
1/5 Test #1: unit_symbol_finder ...........   Passed    0.01 sec
    Start 2: unit_json_codec
2/5 Test #2: unit_json_codec ..............   Passed    0.00 sec
    Start 3: integration_trace
3/5 Test #3: integration_trace ............   Passed    0.64 sec
    Start 4: integration_stack
4/5 Test #4: integration_stack ............   Passed    0.62 sec
    Start 5: integration_watch
5/5 Test #5: integration_watch ............   Passed    1.21 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   2.47 sec
```

> `integration_watch` 需要 `CAP_BPF`；无权限时自动 SKIP（计入 Passed）。

### 快速冒烟测试

```bash
# 编译并运行测试目标进程（需要 -g）
g++ -g -O0 -o /tmp/target tests/fixtures/target.cpp
/tmp/target &
TARGET_PID=$!

# attach uatu
sudo ./build/src/cli/uatu --pid $TARGET_PID
# uatu 1234 attached
# Commands: watch <func>  trace <func>  stack <func>  help  quit
# uatu> quit

kill $TARGET_PID
```

---

## 常见安装问题

### libbpf 找不到

```
CMake Error: Could not find package libbpf
```

**解决：**

```bash
# Ubuntu 22.04+
sudo apt install libbpf-dev

# 手动指定路径
cmake -B build -S . -DLIBBPF_INCLUDE_DIRS=/usr/include -DLIBBPF_LIBRARIES=/usr/lib/x86_64-linux-gnu/libbpf.a
```

---

### bpftool 版本问题

```
bpftool gen skeleton: unknown option '--large-insns'
```

bpftool 版本与内核不匹配。建议安装与内核版本匹配的 bpftool：

```bash
# 检查当前版本
bpftool version

# Ubuntu 22.04 使用系统 bpftool
sudo apt install linux-tools-$(uname -r)
```

---

### eBPF 加载失败：Operation not permitted

```
libbpf: failed to load BPF program: Operation not permitted
```

**解决：**

```bash
# 方案 1：以 root 运行
sudo ./build/src/cli/uatu --pid <PID>

# 方案 2：授予 capability
sudo setcap cap_bpf,cap_perfmon+ep ./build/src/cli/uatu

# 方案 3：关闭 BPF JIT 硬化（仅限测试环境）
echo 0 | sudo tee /proc/sys/net/core/bpf_jit_harden
```

---

### 编译报错：C++20 feature not supported

```
error: 'std::format' is not available
```

GCC 版本过低。升级至 GCC 11+：

```bash
sudo apt install gcc-12 g++-12
cmake -B build -S . -DCMAKE_CXX_COMPILER=g++-12
```

---

### libdw / elfutils 找不到

```
fatal error: elfutils/libdw.h: No such file or directory
```

**解决：**

```bash
sudo apt install libdw-dev libelf-dev
```

Fedora / RHEL：

```bash
sudo dnf install elfutils-devel elfutils-libelf-devel
```

---

### eBPF 加载失败：failed to find valid kernel BTF

```
libbpf: failed to find valid kernel BTF
libbpf: Error loading vmlinux BTF: -3
```

**原因：** Ubuntu 22.04 系统 libbpf（0.5.0）无法解析 kernel 6.x 的 BTF 格式，与权限无关。

**解决：** 按上方"2.5 编译 vendored libbpf"步骤操作，然后重新 `cmake --build build`。vendored libbpf v1.4.3 与 kernel 6.8 兼容。

验证是否使用了 vendored 版本：

```bash
cmake -B build 2>&1 | grep libbpf
# 应输出：libbpf: using vendored static library (1.x) at .../build/libbpf/libbpf.a
```
