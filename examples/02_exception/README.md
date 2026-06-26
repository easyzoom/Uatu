# Example 02: Exception Diagnosis

演示如何用 `watch` 找到偶发异常的触发条件（线上吞异常是常见 bug 模式）。

## 场景说明

`process()` 函数每 5 次调用就触发一次除零，但异常被 catch 吞掉，
外部看不到任何错误——这是线上排查最难的情况。

## 编译运行

```bash
cmake --build build --target example_02_exception
./build/examples/example_02_exception &
PID=$!
```

## 用 uatu 诊断

```bash
./build/src/cli/uatu --pid $PID
```

```
# 观测 safe_divide 是否被调用（通过耗时判断是否正常返回）
uatu> watch exception_demo::safe_divide 10

# 观测 process 函数（count=10，采集 10 次调用）
uatu> watch exception_demo::process 10

# 预期输出（ret 是返回值的 64-bit 整数位表示）：
# ts=...  func=exception_demo::process  cost=0.002ms  ret=4637863191261036544
#   ← 正常路径：100.0/request_id，例如 100.0/1 的 IEEE754 位表示
# ts=...  func=exception_demo::process  cost=0.001ms  ret=13826050856027422720
#   ← 异常路径：-1.0 的 IEEE754 位表示（request_id % 5 == 0 时触发）

# 判断异常路径：ret=13826050856027422720 即 0xBFF0000000000000 = -1.0 (double)
# 每 5 次出现一次，可据此定位触发条件

uatu> quit
```

> **注意**：`watch` 以 64-bit 无符号整数打印返回值的原始位模式，
> 对于 `double` 返回值需手动解析 IEEE 754（或在源码中改为返回整型错误码）。
> 若未编译 eBPF（无 watch.bpf.o），`ret` 会显示 ptrace 模式提示字符串。
