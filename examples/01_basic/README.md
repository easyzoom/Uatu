# Example 01: Basic Function Observation

演示如何用 `watch` 和 `stack` 命令观测一个持续运行的计算服务。

## 编译运行

```bash
cd /path/to/uatu
cmake --build build --target example_01_basic
./build/examples/example_01_basic &
PID=$!
echo "Target PID: $PID"
```

## 用 uatu 诊断

```bash
./build/src/cli/uatu --pid $PID
```

然后输入命令：

```
# 观测 add 函数的返回值和耗时（count=3，采集 3 次）
uatu> watch basic::add 3

# 预期输出（每次调用一行，参数从 i=0 开始递增）：
# ts=1234567890  func=basic::add  cost=0.001ms  ret=0
# ts=1234567890  func=basic::add  cost=0.001ms  ret=3
# ts=1234567890  func=basic::add  cost=0.001ms  ret=6

# 观测 power 函数
uatu> watch basic::power 3

# 查看当前调用栈快照（采集 1 次）
uatu> stack basic::add 1

# 预期输出（frame-pointer 展开的调用链）：
# func=basic::add
#   [0] main
#   [1] __libc_start_main
#   ...

# 测量 power 函数调用耗时
uatu> trace basic::power 2

# 预期输出：
# +-basic::power [0.002ms]
# +-basic::power [0.002ms]

uatu> quit
```

> **注意**：`watch` 只显示返回值（`ret=`），不显示参数。
> 如需参数信息，需要在函数内部打印或使用调试器。

## 停止目标进程

```bash
kill $PID
```
