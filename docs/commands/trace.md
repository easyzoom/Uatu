# trace

追踪函数内部的子调用链路，以树形结构显示各层耗时，用于定位慢子调用。

## 语法

```
trace <function_name> [count] [timeout_ms]
```

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `function_name` | 字符串 | 必填 | 目标函数名，支持全限定名（`namespace::Class::method`）和正则表达式 |
| `count` | 整数 | 3 | 采集命中次数上限 |
| `timeout_ms` | 整数 | 3000 | 等待超时，单位毫秒 |

## 实现机制

trace 使用 **ptrace + INT3 软件断点** 实现：

1. attach 到目标进程后，在目标函数及其所有被调函数的入口/出口处注入 `INT3`（`0xCC`）断点。
2. 每次断点触发时，记录当前时间戳和调用深度。
3. 函数返回时计算耗时，并恢复原始指令。
4. 根据记录的调用深度重建调用树，缩进表示层级关系。

由于使用软件断点，每次函数入口/出口均会导致被追踪进程短暂停顿（通常在微秒级别）。

## 示例

```
# 追踪 slow 函数的子调用链路，默认3次
uatu> trace fixtures::Foo::slow

# 追踪1次，等待最长10秒
uatu> trace fixtures::Foo::slow 1 10000
```

## 输出格式

以缩进树形展示调用链，`+-` 前缀标记每一层调用节点，方括号内为该函数自身的总耗时。

### 示例输出

```
+-fixtures::Foo::slow [2.341ms]
  +-fixtures::Foo::add_internal [0.001ms]
  +-fixtures::Foo::compute [2.338ms]
    +-fixtures::Foo::heavy_loop [2.335ms]
```

缩进层级对应实际调用深度；同一层级的多个节点按调用顺序排列。

## 与 watch 的区别

| 命令 | 关注点 | 典型用途 |
|------|--------|----------|
| `watch` | 单次调用的入参、返回值、总耗时 | 确认函数被调用、查看传入参数是否正确 |
| `trace` | 函数内部的子调用链路及各层耗时 | 定位哪个子调用拖慢了整体执行 |

两者可配合使用：先用 watch 确认函数行为，再用 trace 深入分析慢路径。

## 限制

- **停顿影响**：每个断点触发会使被追踪进程暂停约数微秒；高频调用路径上使用 trace 可能对被追踪进程的延迟产生可见影响。
- **Frame pointer 依赖**：调用深度的判断依赖 frame pointer 链（RBP 寄存器）。`-O0` 默认保留 frame pointer；`-O2` 可能消除 frame pointer（需显式加 `-fno-omit-frame-pointer`）。
- **内联函数**：被编译器内联的函数没有独立的入口地址，无法注入断点，不会出现在调用树中。
- **递归函数**：支持有限深度的递归展开，深度过大时截断并显示提示。

## 权限要求

trace 仅使用 ptrace 模式，不需要 CAP_BPF。需满足以下条件之一：

- `/proc/sys/kernel/yama/ptrace_scope` 为 `0`
- 以 root 用户运行

查看当前 ptrace_scope：

```bash
cat /proc/sys/kernel/yama/ptrace_scope
```

临时放开（重启后恢复）：

```bash
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

## 常见错误

| 错误信息 | 原因 | 解决方法 |
|----------|------|----------|
| `no DWARF` | 目标二进制缺少调试信息，无法解析子调用符号 | 重新编译并加上 `-g` 选项 |
| `permission denied` | ptrace 权限不足 | 以 root 运行，或将 `ptrace_scope` 设为 `0` |
| `no frame pointer` | 编译器消除了 frame pointer，调用深度无法重建 | 编译时加 `-fno-omit-frame-pointer` |
| `timeout` | 超时时间内未命中指定次数 | 增大 `timeout_ms`，或确认目标进程正在执行该函数 |
