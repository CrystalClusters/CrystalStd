#!/usr/bin/env bash
set -euo pipefail

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
if [ ! -d "demo" ]; then
    echo "[错误] 找不到演示目录: demo"
    exit 1
fi

mkdir -p "${BIN_DIR}"

# 约定：
#   demo/<group>/main.c                     -> <group>_demo
#   demo/<group>/<target>/main.c            -> <group>_<target>_demo
#   共享源码放在 demo/<group>/common/ 下，自动参与同组全部目标。
# 构建脚本只认这些目录约定，不维护任何 demo 名单或源文件名单。
build_target() {
    local tdir="$1"
    local name="$2"
    local cdir="$3"
    local mode="$4"
    local -a srcs=()
    local -a incs=(-Iinclude "-I${tdir}")
    local f

    if [ ! -f "${tdir}/main.c" ]; then
        echo "[错误] 目标缺少 main.c: ${tdir}"
        return 1
    fi

    if [ "${mode}" = "single" ]; then
        while IFS= read -r -d '' f; do
            srcs+=("${f}")
        done < <(find "${tdir}" -maxdepth 1 -name '*.c' -print0)
    else
        while IFS= read -r -d '' f; do
            srcs+=("${f}")
        done < <(find "${tdir}" -name '*.c' -print0)
    fi

    if [ -d "${cdir}" ]; then
        incs+=("-I${cdir}")
        while IFS= read -r -d '' f; do
            srcs+=("${f}")
        done < <(find "${cdir}" -name '*.c' -print0)
    fi

    if [ ${#srcs[@]} -eq 0 ]; then
        echo "[错误] 目标没有源文件: ${tdir}"
        return 1
    fi

    echo "生成演示程序：${name}_demo"
    gcc "${srcs[@]}" -o "${BIN_DIR}/${name}_demo" "${incs[@]}" ${CC_BUILD_CFLAGS} \
        -Wl,--whole-archive "${LIB}" -Wl,--no-whole-archive -pthread
}

COUNT=0

for g in demo/*/; do
    g="${g%/}"
    if [ ! -d "${g}" ]; then
        continue
    fi
    if [ "$(basename "${g}")" = "common" ]; then
        continue
    fi

    if [ -f "${g}/main.c" ]; then
        build_target "${g}" "$(basename "${g}")" "${g}/common" single
        COUNT=$((COUNT + 1))
        continue
    fi

    for t in "${g}"/*/; do
        t="${t%/}"
        if [ ! -d "${t}" ]; then
            continue
        fi
        if [ "$(basename "${t}")" = "common" ]; then
            continue
        fi
        if [ ! -f "${t}/main.c" ]; then
            continue
        fi
        build_target "${t}" "$(basename "${g}")_$(basename "${t}")" "${g}/common" multi
        COUNT=$((COUNT + 1))
    done
done

if [ "${COUNT}" -eq 0 ]; then
    echo "[错误] 未发现任何演示目标（需要 demo/<group>/main.c 或 demo/<group>/<target>/main.c）"
    exit 1
fi

echo "演示程序构建完成，共 ${COUNT} 个目标。"
