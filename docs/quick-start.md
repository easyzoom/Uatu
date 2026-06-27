# 快速开始

从构建到第一次观测，约 5 分钟。

## 前提条件

- Ubuntu 22.04+（或其他 Linux x86_64，内核 ≥ 4.18）
- 已安装依赖（见 [install.md](install.md)）
- root 权限或已配置 CAP_BPF + ptrace_scope

---

## 步骤一：构建 uatu

```bash
git clone https://github.com/YOUR_ORG/uatu.git
cd uatu

cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build -j$(nproc)
```

构建完成后，可执行文件位于 `build/src/cli/uatu`。

---

## 步骤二：准备目标进程

uatu 的测试套件自带一个示例目标程序 `tests/fixtures/target.cpp`，包含若干可观测函数。

编译并启动它：

```bash
# 必须带 -g（DWARF 调试信息），否则 uatu 无法解析符号
g++ -g -O0 -std=c++17 -o /tmp/target tests/fixtures/target.cpp

# 在后台运行（它会周期性调用内部函数）
/tmp/target &
TARGET_PID=$!
echo "Target PID: $TARGET_PID"
```

> **为什么需要 `-g`？** uatu 通过 DWARF 调试信息定位函数地址。`-O2` 编译的 stripped 二进制缺少此信息，uatu 会报错并给出提示。

---

## 步骤三：attach 到目标进程

```bash
sudo ./build/src/cli/uatu --pid $TARGET_PID
```

成功 attach 后显示：

```
uatu 12345 attached
Commands: watch <func>  trace <func>  stack <func>  help  quit
uatu>
```

---

## 步骤四：演示三个核心命令

### watch — 观测函数返回值与耗时

```
uatu> watch fixtures::Calculator::add
```

每次目标函数被调用，uatu 打印一行（`Ctrl-C` 退出 uatu 进程）：

```
ts=1750000000123  func=fixtures::Calculator::add  cost=0.042ms  ret=3
  params=[1, 2]
ts=1750000000456  func=fixtures::Calculator::add  cost=0.038ms  ret=7
  params=[1, 3]
```

**字段说明：**

| 字段 | 含义 |
|---|---|
| `ts` | Unix 时间戳（纳秒） |
| `func` | 被观测的函数全名 |
| `cost` | 函数执行耗时 |
| `ret` | 函数返回值 |
| `params` | 函数入参（eBPF 路径；浮点参数显示为 `<xmmN>`，ptrace 降级时为空） |

---

### trace — 跟踪完整调用链与耗时

```
uatu> trace fixtures::Foo::slow
```

记录目标函数本次调用的耗时：

```
+-fixtures::Foo::slow [2.341ms]
```

> **注：** 当前版本追踪单层（目标函数本身），子调用展开功能规划中。

---

### stack — 捕获函数入口时的调用栈

```
uatu> stack fixtures::Calculator::add
```

在函数入口处暂停（极短暂），抓取完整调用栈：

```
func=fixtures::Calculator::add
  [0] fixtures::Calculator::add(int, int)
  [1] fixtures::Foo::compute()
  [2] main
  [3] __libc_start_main
```

适合排查"这个函数是从哪里被调用的"。

---

## 步骤五：退出

```bash
uatu> quit
```

或直接 `Ctrl-D`。uatu 退出后目标进程恢复正常运行，完全无副作用。

```bash
# 清理后台目标进程
kill $TARGET_PID
```

---

## 完整脚本（一键运行）

```bash
#!/bin/bash
set -e

cd /home/wyh/Desktop/self/uatu

# 构建
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -G Ninja -q
cmake --build build -j$(nproc) -q

# 启动目标进程
g++ -g -O0 -std=c++17 -o /tmp/uatu_target tests/fixtures/target.cpp
/tmp/uatu_target &
TARGET_PID=$!
echo "Target PID: $TARGET_PID"
sleep 0.5   # 等待进程初始化

# 交互演示
sudo ./build/src/cli/uatu --pid "$TARGET_PID"

kill "$TARGET_PID" 2>/dev/null || true
```

---

## 下一步

- [install.md](install.md) — 详细安装与权限配置
- [architecture.md](architecture.md) — 了解 uatu 内部工作原理
- [faq.md](faq.md) — 常见问题解答
