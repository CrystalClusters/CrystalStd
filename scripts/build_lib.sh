#!/usr/bin/env bash
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

# 创建输出目录（中间目标文件）
mkdir -p ${OBJ_DIR}
mkdir -p ${BIN_DIR}

if [ ! -f "${FILTER}" ]; then
    echo "[错误] 找不到 filter 工具: ${FILTER}"
    exit 1
fi

# 逐个编译obj
while IFS= read -r src; do
    name="${src#./}"             # 去 ./ 前缀
    name="${name%.c}.o"          # 扩展名 .c -> .o
    name="${name//\//_}"         # 路径前缀：/ -> _
    obj="${OBJ_DIR}/${name}"
    echo "编译 ${src} -> ${obj}"
    if ! gcc -c "${src}" -o "${obj}" -Iinclude -Isrc; then
        echo "[错误] 编译失败：${src}"
        exit 1
    fi
done < <("${FILTER}" -./src -*.c)

OBJ_FILES=$($FILTER -./$OBJ_DIR -*.o)
if [ -z "${OBJ_FILES}" ]; then
    echo "[错误] 未扫描到目标文件(.o)，中止打包。"
    exit 1
fi
ar rcs ${BIN_DIR}/libCrystalStd.a ${OBJ_FILES}
echo "已生成静态库：${BIN_DIR}/libCrystalStd.a"
