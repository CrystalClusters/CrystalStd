#!/usr/bin/env bash
# 用法: bash build_test.sh <platform_arch>
#   示例: bash build_test.sh linux_amd64 | android_arm64 | windows_amd64
# 行为: 编译 tests/test.c 并链接 libCrystalStd.a，产出 out/<platform_arch>_bin/test
set -e

if [ $# -lt 1 ] || [ -z "$1" ]; then
    echo "[错误] 用法: bash build_test.sh <platform_arch>"
    exit 2
fi
PFX="$1"

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

BIN_DIR="out/${PFX}_bin"
LIB="${BIN_DIR}/libCrystalStd.a"
SRC="tests/test.c"

if [ ! -f "${LIB}" ]; then
    echo "[错误] 找不到静态库: ${LIB}，请先执行 build_lib"
    exit 1
fi
if [ ! -f "${SRC}" ]; then
    echo "[错误] 找不到测试源文件: ${SRC}"
    exit 1
fi

echo "编译测试程序..."
gcc ${SRC} -o ${BIN_DIR}/test -Iinclude \
    -Wl,--whole-archive "${LIB}" -Wl,--no-whole-archive
echo "已生成测试程序：${BIN_DIR}/test"
