#!/usr/bin/env bash
set -e

if [ $# -lt 1 ] || [ -z "$1" ]; then
    echo "[错误] 用法: bash build_demo.sh <platform_arch> [build_flags]"
    exit 2
fi
PFX="$1"
CC_BUILD_CFLAGS="${2:-}"

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "${ROOT}"

BIN_DIR="out/${PFX}_bin"
LIB="${BIN_DIR}/libCrystalStd.a"

if [ ! -f "${LIB}" ]; then
    echo "[错误] 找不到静态库: ${LIB}，请先执行 build_lib"
    exit 1
fi
if [ ! -f "demo/ccfiles_demo.c" ]; then
    echo "[错误] 找不到演示源文件: demo/ccfiles_demo.c"
    exit 1
fi

# 收集源码
SRC=$(find ./demo -name '*.c')

gcc ${SRC} -o ${BIN_DIR}/ccfiles_demo -Iinclude ${CC_BUILD_CFLAGS} \
    -Wl,--whole-archive "${LIB}" -Wl,--no-whole-archive
echo "已生成演示程序：${BIN_DIR}/ccfiles_demo"
