#!/usr/bin/env bash
# 用法: bash build_lib.sh <platform_arch>
#   示例: bash build_lib.sh linux_amd64 | android_arm64 | windows_amd64
# 行为: 用 filter 扫描 out/obj 下的 .o，打包为 out/<platform_arch>_bin/libCrystalStd.a
set -e

if [ $# -lt 1 ] || [ -z "$1" ]; then
    echo "[错误] 用法: bash build_lib.sh <platform_arch>"
    exit 2
fi
PFX=$1

# 定位项目根并切换（本脚本位于 scripts/ 下）
ROOT=$(cd $(dirname $0)/.. && pwd)
cd ${ROOT}

OBJ_DIR="out/obj"
BIN_DIR="out/${PFX}_bin"
FILTER="tools/${PFX}/filter.run"

if [ ! -f "${FILTER}" ]; then
    echo "[错误] 找不到 filter 工具: ${FILTER}"
    exit 1
fi
if [ ! -d "${OBJ_DIR}" ]; then
    echo "[错误] 未找到 obj 目录: ${OBJ_DIR}"
    exit 1
fi

mkdir -p ${BIN_DIR}

echo "开始打包静态库..."
OBJ_FILES=$($FILTER -./$OBJ_DIR -*.o) || true
if [ -z "${OBJ_FILES}" ]; then
    echo "[错误] 未扫描到目标文件(.o)，中止打包。"
    exit 1
fi
ar rcs ${BIN_DIR}/libCrystalStd.a ${OBJ_FILES}
echo "已生成静态库：${BIN_DIR}/libCrystalStd.a"
