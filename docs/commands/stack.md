# stack

采集某函数被调用时的完整调用栈，用于分析该函数的调用来源（caller chain）。

## 语法

```
stack <function_name> [count] [timeout_ms]
```

## 参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `function_name` | 字符串 | 必填 | 目标函数名，支持全限定名（`namespace::Class::method`）和正则表达式 |
| `count` | 整数 | 3 | 采集命中次数上限 |
| `timeout_ms` | 整数 | 3000 | 等待超时，单位毫秒 |

## 实现机制

stack 使用 **ptrace + frame pointer 遍历** 实现：

1. attach 到目标进程，在目标函数入口注入 INT3 断点。
2. 断点触发时，读取 `RBP`（base pointer）寄存器的当前值。
3. 沿 frame pointer 链向上遍历：每一帧的 `[RBP+8]` 存放返回地址，`[RBP]` 指向上一帧的 RBP。
4. 对每个返回地址，通过 DWARF 的 `lookup_by_addr` 接口解析为函数名和源码位置。
5. 恢复被追踪进程，记录本次完整调用栈。

## 示例

```
# 采集 add 函数的调用栈，默认3次
uatu> stack fixtures::Calculator::add

# 采集1次，等待最长5秒
uatu> stack fixtures::Calculator::add 1 5000
```

## 输出格式

每次命中输出一条调用栈记录，`[N]` 表示帧编号（0 为目标函数本身，编号越大越靠近调用起点）。

### 示例输出

```
func=fixtures::Calculator::add
  [0] fixtures::Calculator::add(int, int)
  [1] fixtures::Foo::compute(int, int)
  [2] main
```

多次采集时，每次输出之间以空行分隔，方便对比不同调用来源。

## 与 trace 的区别

| 命令 | 视角 | 含义 |
|------|------|------|
| `stack` | 向上看（caller chain） | 目标函数是被谁调用的 |
| `trace` | 向下看（callee chain） | 目标函数内部调用了哪些函数 |

典型场景：

- 想知道某个函数在生产流量中**被哪条路径触发** → 用 `stack`
- 想知道某个慢函数**内部哪里花了时间** → 用 `trace`

## 限制

- **Frame pointer 依赖**：遍历调用栈需要每一帧都保留 frame pointer。
  - `-O0` 默认保留，结果准确。
  - `-O2` 编译器默认开启 `-fomit-frame-pointer`，frame pointer 链可能断裂，导致调用栈截断或不准确。
  - 使用 `-fno-omit-frame-pointer` 重新编译可恢复准确性。
- **strip 二进制**：缺少 DWARF 信息时，`lookup_by_addr` 无法解析符号，帧地址将以十六进制原始地址显示（如 `0x7ffff7a3c21b`）。
- **尾调用优化**：编译器将尾调用优化（TCO）后的调用合并，被合并的帧不会出现在调用栈中。

## 权限要求

stack 仅使用 ptrace 模式。需满足以下条件之一：

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
| `no DWARF` | 目标二进制缺少调试信息，符号无法解析 | 重新编译并加上 `-g` 选项 |
| `permission denied` | ptrace 权限不足 | 以 root 运行，或将 `ptrace_scope` 设为 `0` |
| `incomplete stack (no frame pointer)` | frame pointer 链断裂，调用栈不完整 | 编译时加 `-fno-omit-frame-pointer` |
| `timeout` | 超时时间内未命中指定次数 | 增大 `timeout_ms`，或确认目标进程正在调用该函数 |
