#!/usr/bin/env bash
set -e

# 检测是否为Android
if command -v getprop >/dev/null 2>&1; then
    android_ver=$(getprop ro.build.version.release 2>/dev/null)
    if [ -n "${android_ver}" ]; then
        OS=android
    fi
fi

# 非 Android 特殊情况
if [ -z "${OS}" ]; then
    OS=$(uname -s | tr '[:upper:]' '[:lower:]')
fi
ARCH=$(uname -m)

# 规范化 CPU 架构
case "${ARCH}" in
    x86_64|amd64) ARCH=amd64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) echo [错误] 不支持的 CPU 架构: $ARCH; exit 1 ;;
esac

# 规范化平台
case "$OS" in
    linux)   PLATFORM=linux ;;
    android) PLATFORM=android;;
    *)      echo [错误] 不支持的平台: $OS; exit 1 ;;
esac

echo 当前为 ${PLATFORM} 平台，CPU 指令集架构：${ARCH}

# 工具链检测：未通过则中止构建
CHECK=$(cd $(dirname $0) && pwd)/scripts/check.sh
if [ ! -f "${CHECK}" ]; then
    echo [错误] 找不到工具链检测脚本: $CHECK
    exit 1
fi
if ! bash "${CHECK}"; then
    echo [错误] 工具链检测未通过，中止构建。
    exit 1
fi

# 拼接工具路径
FILTER=tools/${PLATFORM}_${ARCH}/filter.run
echo $FILTER

# 创建输出目录（中间目标文件）
OBJ_DIR=out/obj
mkdir -p $OBJ_DIR

# 逐个编译obj
echo 开始编译源文件...
while IFS= read -r src; do
    name=${src#./}             # 去 ./ 前缀
    name=${name%.c}.o          # 扩展名 .c -> .o
    name=${name//\//_}         # 路径前缀：/ -> _
    obj=$OBJ_DIR/$name
    echo 编译 $src -> $obj
    if ! gcc -c "$src" -o "$obj" -Iinclude -Isrc; then
        echo "[错误] 编译失败：$src"
        exit 1
    fi
done < <("$FILTER" -./src -*.c)

# 打包静态库
echo 生成静态库...
bash scripts/build_lib.sh ${PLATFORM}_${ARCH}

# 编译测试程序
echo 生成测试程序...
bash scripts/build_test.sh ${PLATFORM}_${ARCH}
