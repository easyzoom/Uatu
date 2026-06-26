# Contributing to uatu

[中文](#贡献指南) | [English](#contributing-guide)

---

## Contributing Guide

Thank you for your interest in contributing to **uatu** — a Linux C++ runtime diagnostic tool inspired by Java Arthas. Contributions of all kinds are welcome: bug reports, documentation improvements, new commands, and core engine enhancements.

### Development Environment

**Platform:** Ubuntu 22.04+ (or any Linux distro with kernel 5.8+)

**Required dependencies:**

```bash
# Build toolchain
sudo apt install -y \
    build-essential cmake ninja-build \
    clang clang-format clang-tidy \
    pkg-config

# eBPF toolchain (for uprobe support)
sudo apt install -y \
    libbpf-dev bpftool \
    linux-headers-$(uname -r)

# DWARF / debug info
sudo apt install -y \
    libdw-dev libelf-dev

# Testing
sudo apt install -y libgtest-dev

# Optional: vcpkg (managed via vcpkg.json)
git clone https://github.com/microsoft/vcpkg.git ~/.vcpkg
~/.vcpkg/bootstrap-vcpkg.sh
```

**Minimum versions:**
- CMake >= 3.20
- GCC >= 11 or Clang >= 14 (C++20 support required)
- Linux kernel >= 5.8 (for BPF ring buffer)
- libbpf >= 0.6

### Build Steps

```bash
git clone https://github.com/your-org/uatu.git
cd uatu

# Configure (Debug build)
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
cmake --build build -j$(nproc)

# The binary will be at:
./build/uatu
```

For a Release build:

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(nproc)
```

### Running Tests

**Unit tests** (no special privileges required):

```bash
cd build
ctest --output-on-failure -L unit
```

**Integration tests** (require root or `CAP_BPF` + `CAP_SYS_PTRACE`):

```bash
# Integration tests attach to real processes via ptrace/eBPF
sudo ctest --output-on-failure -L integration

# Or run a specific test binary directly:
sudo ./build/tests/test_attach_engine
```

> **Note:** Integration tests spawn child processes and attach to them. They will fail without elevated privileges. On CI, these run inside a privileged container.

To run all tests:

```bash
sudo ctest --output-on-failure
```

### Code Style

- **Standard:** C++20
- **Style guide:** [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) with minor project-specific overrides (see `.clang-format`)
- **Formatter:** `clang-format` — run before every commit:

```bash
find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

- **Linter:** `clang-tidy` — check with:

```bash
clang-tidy -p build src/**/*.cpp
```

A pre-commit hook that runs `clang-format` is recommended:

```bash
cp scripts/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

### Commit Convention

We follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <short description>

[optional body]

[optional footer]
```

**Types:**

| Type       | When to use                                      |
|------------|--------------------------------------------------|
| `feat`     | New feature or command                           |
| `fix`      | Bug fix                                          |
| `docs`     | Documentation changes only                      |
| `chore`    | Build, CI, dependency, tooling changes           |
| `refactor` | Code restructuring without behavior change       |
| `test`     | Adding or updating tests                         |
| `perf`     | Performance improvements                         |

**Examples:**

```
feat(watch): add expression filter support for return values
fix(trace): handle SIGTRAP edge case in multi-threaded targets
docs: update CONTRIBUTING with vcpkg setup instructions
chore: bump libbpf to 1.3.0
```

### Pull Request Process

1. Fork the repository and create a feature branch from `main`:
   ```bash
   git checkout -b feat/my-new-feature
   ```
2. Make your changes, ensuring tests pass locally.
3. Run `clang-format` and `clang-tidy` to ensure code style compliance.
4. Push to your fork and open a PR against `main`.
5. Fill in the PR template, linking any related issues.
6. At least one maintainer review is required before merging.
7. Squash-merge is preferred for feature branches; merge commits for release branches.

### How to Add a New Command

New commands are implemented as modules that integrate with `AttachEngine`. The pattern is:

1. **Define the command class** in `include/commands/`:
   - Inherit from `CommandBase` (or implement the command interface directly).
   - Implement `execute(const CommandArgs& args, AttachEngine& engine)`.

2. **Register the command** in `src/cli/repl.cpp` (or wherever the command dispatcher lives) by adding it to the command registry map.

3. **Add engine hooks** if the command needs to observe or intercept process execution:
   - For eBPF-based observation: add a uprobe program in `ebpf/` and load it via `AttachEngine::attachUprobe()`.
   - For ptrace-based fallback: implement the required `ptrace(PTRACE_*)` calls via `AttachEngine`'s ptrace interface.

4. **Write tests:**
   - Unit tests in `tests/unit/` for logic that doesn't require a live process.
   - Integration tests in `tests/integration/` for tests that attach to a real process.

5. **Update documentation:** add the command to `README.md` and `README_CN.md`.

Refer to the existing `watch`, `trace`, and `stack` command implementations as reference examples.

---

## 贡献指南

感谢你对 **uatu** 的关注！uatu 是一个对标 Java Arthas 的 Linux C++ 运行时诊断工具。欢迎各类贡献：Bug 报告、文档改进、新命令开发以及核心引擎增强。

### 开发环境要求

**平台：** Ubuntu 22.04+（或任何内核版本 >= 5.8 的 Linux 发行版）

**必需依赖：**

```bash
# 构建工具链
sudo apt install -y \
    build-essential cmake ninja-build \
    clang clang-format clang-tidy \
    pkg-config

# eBPF 工具链（uprobe 支持）
sudo apt install -y \
    libbpf-dev bpftool \
    linux-headers-$(uname -r)

# DWARF / 调试信息
sudo apt install -y \
    libdw-dev libelf-dev

# 测试框架
sudo apt install -y libgtest-dev
```

**版本要求：**
- CMake >= 3.20
- GCC >= 11 或 Clang >= 14（需要 C++20 支持）
- Linux 内核 >= 5.8（BPF ring buffer 支持）
- libbpf >= 0.6

### 构建步骤

```bash
git clone https://github.com/your-org/uatu.git
cd uatu

# 配置（Debug 构建）
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 构建
cmake --build build -j$(nproc)

# 可执行文件位于：
./build/uatu
```

### 运行测试

**单元测试**（无需特殊权限）：

```bash
cd build
ctest --output-on-failure -L unit
```

**集成测试**（需要 root 或 `CAP_BPF` + `CAP_SYS_PTRACE`）：

```bash
sudo ctest --output-on-failure -L integration
```

> **注意：** 集成测试会 attach 到真实进程，必须以提升权限运行。

### 代码风格

- **标准：** C++20
- **风格指南：** Google C++ Style Guide（参见 `.clang-format`）
- **格式化：** 提交前务必运行 `clang-format -i`

### 提交规范

遵循 [Conventional Commits](https://www.conventionalcommits.org/)，类型包括：
`feat` / `fix` / `docs` / `chore` / `refactor` / `test` / `perf`

### 如何添加新命令

1. 在 `include/commands/` 中定义命令类，继承 `CommandBase`。
2. 在命令分发器中注册新命令。
3. 如需观测进程：eBPF 路径通过 `AttachEngine::attachUprobe()` 挂载；ptrace 回退通过 `AttachEngine` 的 ptrace 接口实现。
4. 在 `tests/unit/` 和 `tests/integration/` 中添加对应测试。
5. 更新 `README.md` 和 `README_CN.md`。

可参考已有的 `watch`、`trace`、`stack` 命令实现作为示例。
