# Example 03: Slow Path Diagnosis

演示如何用 `trace` 找到慢请求的具体瓶颈。

## 场景说明

`handle_request()` 大部分时候很快，但每 4 次会触发 `slow_op()`，
导致该次请求耗时 50ms+。用 trace 可以一眼看出是哪个子调用拖慢了。

## 编译运行

```bash
cmake --build build --target example_03_slow_path
./build/examples/example_03_slow_path &
PID=$!
```

## 用 uatu 诊断

```bash
./build/src/cli/uatu --pid $PID
```

```
# 测量 handle_request 每次调用的端到端耗时（count=4，timeout=8000ms）
uatu> trace slow_path::handle_request 4 8000

# 预期输出（每次调用一行，慢次约 50ms，快次约 1ms）：
# +-slow_path::handle_request [1.234ms]
# +-slow_path::handle_request [1.198ms]
# +-slow_path::handle_request [1.211ms]
# +-slow_path::handle_request [51.432ms]   ← request_id % 4 == 0，触发 slow_op

# 通过对比各次耗时即可识别慢请求；
# 若需定位是哪个子函数慢，对子函数单独 trace：
uatu> trace slow_path::slow_op 2 8000

# 预期输出：
# +-slow_path::slow_op [50.012ms]
# +-slow_path::slow_op [50.008ms]

uatu> quit
```

> **注意**：`trace` 测量指定函数本身的端到端耗时，不展开子调用树。
> 若需定位子调用瓶颈，对各子函数（`fast_op`、`medium_op`、`slow_op`）逐一 trace。
