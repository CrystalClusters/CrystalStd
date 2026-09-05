#!/usr/bin/env bash
set -e

# 检测是否为Android
if command -v getprop >/dev/null 2>&1; then
    android_ver=$(getprop ro.build.version.release 2>/dev/null)
    if [ -n "$android_ver" ]; then
        OS=android
    fi
fi

# 非 Android 特殊情况
if [ -z "$OS" ]; then
    OS=$(uname -s | tr '[:upper:]' '[:lower:]')
fi
ARCH=$(uname -m)

# 规范化 CPU 架构
case "$ARCH" in
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

echo 当前为 $PLATFORM 平台，CPU 指令集架构：$ARCH

# 工具链检测：未通过则中止构建
CHECK=$(cd $(dirname $0) && pwd)/scripts/check.sh
if [ ! -f $CHECK ]; then
    echo [错误] 找不到工具链检测脚本: $CHECK
    exit 1
fi
if ! bash $CHECK; then
    echo [错误] 工具链检测未通过，中止构建。
    exit 1
fi

# 分流到 scripts 中的细分脚本
SCRIPT=$(cd $(dirname $0) && pwd)/scripts/build_${PLATFORM}_${ARCH}.sh
if [ ! -f $SCRIPT ]; then
    echo [错误] 找不到细分脚本: $SCRIPT
    exit 1
fi

exec bash $SCRIPT
