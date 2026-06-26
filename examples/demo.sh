#!/usr/bin/env bash
# uatu 一键演示脚本
# 用法: ./examples/demo.sh [01|02|03]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
EXAMPLE="${1:-01}"

# 颜色
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${CYAN}=== uatu Demo: Example ${EXAMPLE} ===${NC}"

# 确认已构建
if [ ! -f "$BUILD_DIR/src/cli/uatu" ]; then
    echo -e "${YELLOW}Building uatu...${NC}"
    cmake -B "$BUILD_DIR" -S "$ROOT_DIR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

# 选择示例
case "$EXAMPLE" in
    01)
        EXE="$BUILD_DIR/examples/example_01_basic"
        FUNC="basic::add"
        DESC="Basic function observation"
        CMDS="watch basic::add 3\nwatch basic::greet 2\nstack basic::add 1\nquit"
        ;;
    02)
        EXE="$BUILD_DIR/examples/example_02_exception"
        FUNC="exception_demo::safe_divide"
        DESC="Exception diagnosis"
        CMDS="watch exception_demo::safe_divide 10\nwatch exception_demo::process 10\nquit"
        ;;
    03)
        EXE="$BUILD_DIR/examples/example_03_slow_path"
        FUNC="slow_path::handle_request"
        DESC="Slow path diagnosis"
        CMDS="trace slow_path::handle_request 4 8000\nquit"
        ;;
    *)
        echo "Usage: $0 [01|02|03]"
        exit 1
        ;;
esac

# 编译示例
TARGET_NAME="$(basename "$EXE")"
cmake --build "$BUILD_DIR" --target "$TARGET_NAME" 2>/dev/null || \
    cmake --build "$BUILD_DIR" 2>/dev/null

# 验证二进制存在
if [ ! -f "$EXE" ]; then
    echo -e "\033[0;31mError: binary not found: $EXE\033[0m"
    exit 1
fi

# 启动目标进程
echo -e "${GREEN}Starting target: $(basename "$EXE")${NC}"
"$EXE" &
TARGET_PID=$!
sleep 0.8

# 确认进程仍在运行
if ! kill -0 "$TARGET_PID" 2>/dev/null; then
    echo -e "\033[0;31mError: target process exited immediately (PID $TARGET_PID)\033[0m"
    exit 1
fi

echo -e "${GREEN}Target PID: $TARGET_PID${NC}"
echo -e "${CYAN}Running: $DESC${NC}"
echo -e "${YELLOW}Commands to be sent:${NC}"
echo -e "$CMDS" | sed 's/^/  > /'
echo ""

# 运行 uatu
echo -e "$CMDS" | "$BUILD_DIR/src/cli/uatu" --pid "$TARGET_PID"

# 清理
kill "$TARGET_PID" 2>/dev/null
wait "$TARGET_PID" 2>/dev/null || true
echo -e "\n${GREEN}Demo complete.${NC}"
