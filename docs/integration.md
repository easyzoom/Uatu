# 将 uatu 集成到你的 C++ 工程

本文面向希望在自己项目中使用 uatu 进行运行时诊断的 C++ 开发者。
uatu 当前处于 **Phase 1**（外部 CLI 工具，零侵入 attach），无需修改目标程序代码。
Phase 2 嵌入式 library 正在规划中，详见 [Roadmap](#八roadmap嵌入式-library-phase-2即将推出)。

---

## 一、前提条件（让你的程序对 uatu 可见）

### 编译选项

uatu 依赖 DWARF 调试信息来定位函数符号、解析参数类型。请在 CMakeLists.txt 中为目标加入相应选项：

```cmake
# CMakeLists.txt 中加入调试符号
target_compile_options(your_target PRIVATE
    -g          # 生成 DWARF 调试信息（必须）
    -O0         # 关闭优化，防止函数内联（推荐用于诊断环境）
    # 如果需要保持优化：
    # -O2 -fno-omit-frame-pointer  # 保留 frame pointer 用于 stack 命令
)
```

三种场景下的支持情况：

| 编译选项 | watch | trace | stack | 说明 |
|---|---|---|---|---|
| `-g -O0` | 全部支持 | 全部支持 | 全部支持 | 最完整支持，推荐诊断环境使用 |
| `-g -O2 -fno-omit-frame-pointer` | 非内联函数可用 | 非内联函数可用 | 可用 | 内联函数不可见 |
| strip 后（无调试符号） | 失败 | 失败 | 失败 | uatu 会给出明确提示 |

> **注意**：`-O2` 及以上优化级别可能将小函数内联展开，导致这些函数在符号表中消失，uatu 无法按名称找到它们。如果只是诊断生产问题，可临时使用 `-O1 -fno-inline` 重新构建。

### 命名空间建议

给类和函数加命名空间，可以让 uatu 更精确地匹配目标符号，避免名称冲突：

```cpp
// 推荐：有命名空间
namespace myapp {
    class OrderService {
    public:
        void processOrder(int orderId, double amount);
    };
}
// uatu 命令：watch myapp::OrderService::processOrder

// 不推荐：全局函数难以精确匹配，可能与系统库符号冲突
int processOrder(int orderId, double amount);
```

---

## 二、安装 uatu

### 方式一：从源码构建（推荐）

```bash
git clone https://github.com/YOUR_ORG/uatu
cd uatu

# Ubuntu 22.04 + kernel ≥ 6.x：先编译 vendored libbpf v1.4.3
# （系统 libbpf 0.5.0 与新内核 BTF 不兼容）
cd third_party/libbpf/src
make BUILD_STATIC_ONLY=1 OBJDIR=$(pwd)/../../../build/libbpf
make BUILD_STATIC_ONLY=1 OBJDIR=$(pwd)/../../../build/libbpf \
     DESTDIR=$(pwd)/../../../build/libbpf PREFIX="" install
cd ../../..

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build --prefix /usr/local
# 安装后：/usr/local/bin/uatu
```

**系统依赖**（Ubuntu/Debian）：

```bash
sudo apt-get install -y libdw-dev libelf-dev libbpf-dev
```

### 方式二：FetchContent 集成到 CMake 项目

适合需要在 CI 中自动下载并使用 uatu 的场景：

```cmake
include(FetchContent)
FetchContent_Declare(
    uatu
    GIT_REPOSITORY https://github.com/YOUR_ORG/uatu.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(uatu)

# 运行集成测试时，uatu CLI 已在 build tree 中：
# ${uatu_BINARY_DIR}/src/cli/uatu
```

### 方式三：Docker（不污染宿主机环境）

适合在容器化 CI 环境或不想在宿主机安装依赖的场景：

```bash
docker run --rm --privileged --pid=host \
    -v /usr/lib/debug:/usr/lib/debug:ro \
    uatu/uatu:latest --pid <YOUR_PID>
```

> `--pid=host` 使容器可见宿主机进程，`--privileged` 为 BPF 操作提供所需权限。

---

## 三、基本使用流程

以下是从编译到诊断的完整步骤：

```bash
# 1. 编译你的程序（加 -g）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug  # Debug 模式默认含 -g
cmake --build build

# 2. 启动你的程序
./build/your_program &
YOUR_PID=$!

# 3. attach uatu
uatu --pid $YOUR_PID

# 4. 交互式诊断
uatu> watch yourns::YourClass::yourMethod 5
uatu> trace yourns::YourClass::slowMethod 2 5000
uatu> stack yourns::YourClass::yourMethod 1
uatu> quit
```

各命令简介：

| 命令 | 用途 | 示例 |
|---|---|---|
| `watch <func> <count> [timeout_ms]` | 观测函数调用次数与入参 | `watch myapp::Svc::handle 10 5000` |
| `trace <func> <count> [timeout_ms]` | 测量函数执行耗时 | `trace myapp::Svc::compute 5 3000` |
| `stack <func> <count>` | 采集函数被调用时的调用栈 | `stack myapp::Svc::crash 1` |

详细命令文档见 [docs/commands/](commands/)。

---

## 四、在 CI/CD 中使用

### GitHub Actions 示例

```yaml
- name: Install uatu dependencies
  run: |
    sudo apt-get install -y libdw-dev libelf-dev libbpf-dev

- name: Build uatu
  run: |
    git clone https://github.com/YOUR_ORG/uatu /tmp/uatu
    cd /tmp/uatu/third_party/libbpf/src
    make BUILD_STATIC_ONLY=1 OBJDIR=/tmp/uatu/build/libbpf
    make BUILD_STATIC_ONLY=1 OBJDIR=/tmp/uatu/build/libbpf \
         DESTDIR=/tmp/uatu/build/libbpf PREFIX="" install
    cd /tmp/uatu
    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc)

- name: Run with diagnostics
  run: |
    ./build/your_program &
    PID=$!
    sleep 1
    # 收集 3 次调用数据后退出
    echo -e "watch yourns::criticalFunc 3 5000\nquit" | \
      /tmp/uatu/build/src/cli/uatu --pid $PID
    kill $PID
```

> CI 环境通常已有 root 权限，无需额外配置 ptrace 或 capabilities。

---

## 五、权限配置（生产环境）

生产环境通常不允许以 root 运行工具。有两种方式让普通用户使用 uatu：

```bash
# 方式一：给 uatu 二进制赋予 capabilities（推荐）
# 普通用户无需 sudo 即可使用
sudo setcap cap_bpf,cap_perfmon,cap_sys_ptrace+ep /usr/local/bin/uatu

# 方式二：放开 ptrace 限制（降级模式，功能受限）
# 临时生效
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

# 或永久配置
echo 'kernel.yama.ptrace_scope = 0' | sudo tee /etc/sysctl.d/10-ptrace.conf
sudo sysctl -p /etc/sysctl.d/10-ptrace.conf
```

**权限需求说明**：

| 功能 | 所需权限 |
|---|---|
| `watch` / `trace`（uprobe via BPF） | `cap_bpf` + `cap_perfmon` |
| `stack`（ptrace 采集） | `cap_sys_ptrace` |
| attach 到其他用户的进程 | `cap_sys_ptrace` 或 `ptrace_scope=0` |

---

## 六、常见集成场景示例

### 场景一：诊断 gRPC 服务

```bash
# 启动 gRPC 服务
./build/grpc_server &
PID=$!

# attach 并观测 RPC handler 的入参和耗时
uatu --pid $PID
uatu> watch grpc::MyService::ProcessRequest 10 30000
uatu> trace grpc::MyService::ProcessRequest 3 10000
uatu> quit
```

### 场景二：诊断 shared library 中的函数

uatu 同样可以观测动态链接库（.so）中的函数，前提是该库编译时包含 `-g`：

```bash
# 编译带调试符号的 .so
g++ -g -shared -fPIC -o libmylib.so mylib.cpp

# attach 后直接按全限定名 watch
uatu> watch libmylib::Parser::parse 5
```

### 场景三：脚本化批量收集

适合对多个关键函数批量采集诊断数据：

```bash
#!/bin/bash
PID=$1
FUNCS=(
    "myapp::OrderService::create"
    "myapp::PayService::charge"
    "myapp::NotifyService::send"
)

for func in "${FUNCS[@]}"; do
    echo "=== $func ===" >> diagnosis.log
    echo -e "watch $func 5 3000\nquit" | uatu --pid $PID >> diagnosis.log 2>&1
done
```

---

## 七、与构建系统集成的完整 CMake 示例

在项目中添加 `diagnose` 自定义目标，一键启动程序并进行 uatu 诊断采集：

```cmake
# 在你的项目 CMakeLists.txt 中

# 引入 uatu（构建时）
include(FetchContent)
FetchContent_Declare(
    uatu
    GIT_REPOSITORY https://github.com/YOUR_ORG/uatu.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(uatu)

# 自定义测试目标：启动程序 + uatu 采集 + 停止
add_custom_target(diagnose
    COMMAND ${CMAKE_COMMAND} -E env
        bash -c "
            $<TARGET_FILE:your_program> &
            PID=\$!
            sleep 1
            echo -e 'watch yourns::criticalFunc 5\nquit' |
                ${uatu_BINARY_DIR}/src/cli/uatu --pid \$PID
            kill \$PID
        "
    DEPENDS your_program uatu
    COMMENT "Running uatu diagnostics on your_program"
)
```

运行方式：

```bash
cmake --build build --target diagnose
```

---

## 八、Roadmap：嵌入式 library（Phase 2，即将推出）

Phase 2 将提供 `#include <uatu/agent.h>`，在程序内部嵌入 uatu agent：

- 无需 ptrace 权限，能力更强
- 自动开启 WebSocket 诊断端点（默认 `:8563`）
- 支持时间隧道（`tt`）、热修复（`retransform`）等高级功能
- Spring Boot Actuator 风格的零配置接入

```cpp
// Phase 2 预览（尚未实现）
#include <uatu/agent.h>

int main() {
    uatu::Agent::start(8563);  // 启动诊断端点
    // ... 你的业务代码
    your_server.run();
}
```

届时 CMake 集成将简化为：

```cmake
find_package(uatu REQUIRED)
target_link_libraries(your_target PRIVATE uatu::agent)
```

如果你对 Phase 2 功能感兴趣，欢迎在 GitHub 上提 issue 或参与讨论。

---

## 相关文档

- [安装指南](install.md)
- [快速上手](quick-start.md)
- [架构设计](architecture.md)
- [命令参考](commands/)
- [FAQ](faq.md)
- [Docker 部署](docker.md)
