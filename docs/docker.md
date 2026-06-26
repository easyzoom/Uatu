# Docker 使用指南

本文档说明如何使用 Docker 构建、测试和开发 uatu。

## 快速构建镜像

```bash
# 构建运行时镜像（精简，仅包含 uatu 二进制）
docker build --target runtime -t uatu:latest .

# 构建构建镜像（包含完整工具链 + 测试二进制）
docker build --target builder -t uatu:builder .
```

## 用 Docker 运行单元测试

单元测试不需要特殊权限，直接运行：

```bash
docker compose run --rm test-unit
```

或手动指定：

```bash
docker run --rm uatu:builder sh -c "
  /src/build/tests/unit/test_symbol_finder &&
  /src/build/tests/unit/test_json_codec
"
```

## 用 Docker 运行集成测试

集成测试需要 ptrace 权限和对宿主机进程的访问，**必须使用 `--privileged`**：

```bash
docker compose run --rm test-integration
```

或手动指定：

```bash
docker run --rm \
  --privileged \
  --pid=host \
  uatu:builder sh -c "
    /src/build/tests/integration/test_watch --gtest_filter='WatchStrip*' &&
    /src/build/tests/integration/test_trace &&
    /src/build/tests/integration/test_stack
  "
```

> **为什么需要 `--privileged`？**
> - `test_watch` 和 `test_trace` 使用 ptrace 附加到目标进程，需要 `CAP_SYS_PTRACE`。
> - `test_stack` 在 eBPF 模式下需要 `CAP_BPF` 和 `CAP_PERFMON`。
> - `--privileged` 一次性授予所有必要的 capabilities，无需逐一列举。

## 用宿主机编译的二进制测试 eBPF

如果已在宿主机完成构建，想跳过容器内重新编译、直接验证 eBPF 路径，可将宿主机的构建产物挂进特权容器运行。有一个关键约束：

> **fixture 路径必须在容器内保持与宿主机完全一致。**

原因：`FIXTURE_DIR` 在 CMake 构建时以绝对路径写死进测试二进制。例如宿主机路径为
`/home/alice/uatu/build/tests/fixtures/target_debug`，
容器内必须也能以同样路径访问该文件，否则子进程 `execl` 失败后静默退出，
引发 `cannot resolve /proc/<pid>/exe` 错误。

正确做法：**以原始路径挂载整个项目目录**，而非换一个挂载点：

```bash
# 正确：路径与宿主机一致
docker run --rm --privileged --pid=host \
  -v /home/alice/uatu:/home/alice/uatu \
  -v /lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu \
  -v /usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu \
  -v /lib64:/lib64 \
  -w /home/alice/uatu \
  ubuntu:22.04 \
  build/tests/integration/test_watch

# 错误：换了挂载点，fixture 路径对不上
# docker run ... -v /home/alice/uatu:/uatu ...
```

`-v /lib/x86_64-linux-gnu` 等挂载是为了让宿主机编译的二进制找到与构建时完全一致的动态库版本，避免 ABI 不兼容。如果容器内 `apt install` 的库版本足够新（≥ 宿主机版本），也可以省略这些挂载，直接安装依赖：

```bash
docker run --rm --privileged --pid=host \
  -v /home/alice/uatu:/home/alice/uatu \
  -w /home/alice/uatu \
  ubuntu:24.04 bash -c "
    apt-get update -qq && apt-get install -qq -y libdw1 libelf1 zlib1g libstdc++6
    build/tests/integration/test_watch
  "
```

## 开发环境（挂载源码的 dev 容器）

dev 容器挂载本地源码到 `/src`，修改代码后可直接在容器内重新编译，无需重建镜像：

```bash
docker compose run --rm dev
```

进入容器后：

```bash
# 增量编译（只编译变更文件）
cmake --build build -j$(nproc)

# 运行单个测试
./build/tests/unit/test_symbol_finder

# 运行 uatu CLI
./build/src/cli/uatu --help
```

## 注意事项

### eBPF 需要访问宿主机内核 BTF

eBPF uprobe 依赖宿主机内核的 BTF（BPF Type Format）信息，文件路径为：

```
/sys/kernel/btf/vmlinux
```

docker-compose.yml 中的 `dev` 服务已将 `/sys` 以只读方式挂载到容器内。如果手动运行，需要添加：

```bash
docker run --rm \
  --privileged \
  -v /sys:/sys:ro \
  uatu:builder bash
```

### watch 命令 attach 到宿主机进程

`uatu watch` 附加到宿主机进程时，需要容器与宿主机共享 PID 命名空间：

```bash
docker run --rm \
  --privileged \
  --pid=host \
  -v /sys:/sys:ro \
  uatu:latest watch --pid <宿主机 PID> <符号>
```

### CAP_BPF 权限（最小权限替代 --privileged）

生产环境中，建议替换 `--privileged` 为精确的 capabilities：

```bash
docker run --rm \
  --cap-add CAP_BPF \
  --cap-add CAP_PERFMON \
  --cap-add CAP_SYS_PTRACE \
  --pid=host \
  -v /sys:/sys:ro \
  uatu:latest watch --pid <PID> <符号>
```

| Capability | 用途 |
|---|---|
| `CAP_BPF` | 加载 eBPF 程序 |
| `CAP_PERFMON` | 访问性能计数器（eBPF perf 事件） |
| `CAP_SYS_PTRACE` | ptrace 模式附加进程 |

### Docker Desktop（Mac / Windows）不支持 eBPF

Docker Desktop 在 Mac 和 Windows 上运行的是轻量级 Linux VM，**其内核不支持宿主机 eBPF**：

- 无法访问宿主机的 `/sys/kernel/btf/vmlinux`
- eBPF 程序无法 uprobe 宿主机进程

在 Mac / Windows 上，uatu 只能使用 **ptrace 模式**。ptrace 模式在 Docker Desktop 中同样需要 `--privileged`，但功能上与原生 Linux 一致（无 eBPF 加速）。

**推荐做法**：在 Linux 宿主机上使用 Docker，以获得完整的 eBPF 支持。
