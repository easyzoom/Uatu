# 常见问题（FAQ）

## 使用基础

---

**Q: 需要修改目标程序的代码吗？**

不需要。uatu 完全从外部 attach 到运行中的进程，不修改任何源代码，不需要重新编译目标程序（前提是二进制带有 DWARF 调试信息，即编译时加了 `-g`）。

---

**Q: 目标程序需要重启吗？**

不需要。uatu 通过 eBPF uprobe 或 ptrace 动态 attach 到已经运行的进程，整个过程目标程序持续运行，对外提供服务不受影响。

---

**Q: uatu 会影响目标进程的性能吗？**

- **eBPF 模式（watch 命令）：** 影响极小。uprobe 的内核 hook 开销约为 100–500 纳秒/次，对大多数业务函数可忽略不计。
- **ptrace 模式（trace / stack 命令）：** 每次触发断点时，相关线程会短暂暂停（通常 < 100 微秒）。高频函数（> 1万次/秒）在生产环境建议谨慎使用 trace 命令。

---

## 错误排查

---

**Q: 为什么 watch 命令报错 "no DWARF info" 或 "symbol not found"？**

目标二进制被 strip 掉了调试信息，或编译时未加 `-g`。

解决方法：

```bash
# 重新编译目标程序，加 -g 保留 DWARF 信息
g++ -g -O0 -o my_app my_app.cpp

# 验证是否有 DWARF 信息
readelf --debug-dump=info my_app | head -20
# 有输出则表示包含调试信息
```

生产环境通常使用 `-O2 -g`（优化 + 调试信息），strip 发布包时保留一份带 debug 信息的二进制用于诊断。

---

**Q: 为什么 watch 对某些函数不起作用，提示 "function is inlined"？**

编译器（通常是 `-O1` 及以上）将该函数内联展开到调用点，函数不再以独立形式存在于二进制中，无法通过 uprobe 或 ptrace 断点 hook。

DWARF 中存有内联记录（`DW_AT_inline`），uatu 会检测并提示，而非静默失败。

解决方法：

```bash
# 方法 1：用 -O0 重新编译（关闭所有优化）
g++ -g -O0 -o my_app my_app.cpp

# 方法 2：对特定函数禁止内联
__attribute__((noinline)) int my_func() { ... }

# 方法 3：观测调用该内联函数的上层函数
```

---

**Q: 需要 root 权限吗？**

不强制要求 root，但需要对应权限：

| 模式 | 权限要求 |
|---|---|
| eBPF 模式 | root，或 `CAP_BPF + CAP_PERFMON` |
| ptrace 模式 | root，或 `ptrace_scope=0`，或是目标进程的父进程 |

临时降低权限要求：

```bash
# 允许任意进程互相 ptrace（测试环境）
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

# 给 uatu 授予 BPF 能力（无需 root 运行 eBPF）
sudo setcap cap_bpf,cap_perfmon+ep ./build/src/cli/uatu
```

---

**Q: 运行时报错 "Operation not permitted"，但我已经是 root？**

可能是以下原因：

1. **BPF JIT 硬化开启：** 某些安全强化内核禁止非特权 BPF。检查：

   ```bash
   cat /proc/sys/net/core/bpf_jit_harden
   # 2 表示完全硬化，0 表示关闭
   ```

2. **容器/沙箱限制：** Docker 默认 seccomp profile 可能限制 `bpf()` 系统调用。运行容器时加 `--cap-add CAP_BPF --cap-add CAP_PERFMON`。

3. **ptrace_scope 限制：** 即便是 root，某些发行版默认 ptrace_scope=1。检查 `/proc/sys/kernel/yama/ptrace_scope`。

---

**Q: 运行时报错 "libbpf: failed to find valid kernel BTF"（即便有 root）？**

这是 libbpf **版本**与内核不兼容的问题，与权限无关。

| 版本 | 情况 |
|---|---|
| Ubuntu 22.04 系统 libbpf | 0.5.0（2021 年），无法解析 kernel 6.x BTF |
| 仓库 vendored libbpf | v1.4.3，与 kernel 6.8 兼容 |

**解决方法：** 参见 [install.md](install.md) 中的"编译 vendored libbpf"步骤，重新构建即可。CMake 会自动优先选择 vendored 版本，确认方式：

```bash
cmake -B build 2>&1 | grep libbpf
# 正确输出：libbpf: using vendored static library (1.x) at ...
```

---

## 兼容性

---

**Q: 支持哪些 C++ 编译器？**

GCC 和 Clang 均支持，条件是编译目标程序时生成标准 DWARF 格式：

| 编译器 | 最低版本 | 备注 |
|---|---|---|
| GCC | 7+ | DWARF4 默认；GCC 11+ 生成 DWARF5 |
| Clang | 6+ | 默认生成 DWARF4 |

确保编译时包含 `-g` 标志。`-g3` 包含更多宏信息但非必要。

---

**Q: 支持 Rust 或 Go 程序吗？**

原理上可行，两者都生成标准 ELF + DWARF，uatu 的 DWARF 解析和 eBPF uprobe 机制不依赖语言：

- **Rust：** 编译时加 `--emit=debuginfo` 或在 `Cargo.toml` 设 `debug = true`，函数名会有 mangling，需用正则匹配（`watch` 支持正则）。
- **Go：** Go 的 goroutine 调度和 defer 机制复杂，`stack` 命令的 frame pointer walk 可能不完整（Go 的栈是分段的）。

**当前状态：** 未经系统测试，不保证可靠性。欢迎提 issue 反馈测试结果。

---

**Q: 支持 arm64 / RISC-V 吗？**

当前仅支持 **Linux x86_64**。eBPF uprobe 机制本身是架构无关的，但 ptrace 模式的寄存器读取（参数：rdi/rsi/rdx；返回值：rax）是 x86_64 ABI 专用。

arm64 支持在 Phase 2 路线图中（需要适配 arm64 调用约定 x0-x7/x0）。

---

## 与其他工具对比

---

**Q: uatu 和 gdb 有什么区别？**

| 特性 | uatu | gdb |
|---|---|---|
| attach 时暂停进程 | 否（eBPF 模式）；仅断点瞬间（ptrace） | 是，attach 后所有线程暂停 |
| 修改代码 | 不需要 | 不需要 |
| 观测实时流量 | 是（持续 watch） | 需要手动 continue/stop |
| 调用链计时 | 是（trace 命令） | 需要手写脚本 |
| 生产环境友好 | 是（eBPF 模式低开销） | 否（暂停线程影响服务） |
| 交互调试（变量修改、条件断点）| 否 | 是 |
| 核心转储分析 | 否 | 是 |

uatu 的定位是**零侵入的运行时观测**，不是通用调试器。要单步调试、修改变量，仍应使用 gdb。

---

**Q: uatu 和 perf / BCC / bpftrace 有什么区别？**

| 特性 | uatu | perf | bpftrace |
|---|---|---|---|
| 目标受众 | C++ 开发者 | 系统工程师 | 内核/系统工程师 |
| 使用门槛 | 低（attach + 命令） | 中（需理解采样/事件） | 高（需要写脚本） |
| 函数参数/返回值 | 是（DWARF 解析） | 否（采样不含参数） | 是（需要手写 uprobe 脚本） |
| 调用树可视化 | 是（trace 命令） | 需要 perf report | 需要手写脚本 |
| 符号解析 | DWARF 精确解析 | 依赖符号表 | 依赖符号表 |

uatu 的差异化在于**面向 C++ 开发者的友好 UX**：直接用函数名，自动处理符号解析，输出即时可读。

---

**Q: uatu 和 Arthas（Java 诊断工具）有什么关系？**

uatu 的灵感来自 Alibaba 开源的 Java 诊断工具 Arthas，目标是为 C++ 提供类似的"运行时诊断"体验。

核心理念相同：attach 到运行中进程，零侵入观测，REPL 交互界面，对开发者友好的输出格式。实现上完全不同：Java 依赖 JVM Instrumentation API，C++ 使用 eBPF + ptrace + DWARF。

---

## 其他

---

**Q: uatu 这个名字是什么意思？**

Uatu 是漫威宇宙中的**观察者（The Watcher）**，一个古老种族的成员，他的誓言是只观察宇宙中发生的事件，绝不干涉。

这正是 uatu 工具的核心哲学：**只观察，不干涉**——零侵入地观测 C++ 进程的运行状态，不修改代码，不影响业务逻辑。
