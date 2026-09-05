#!/usr/bin/env bash
set -e

# 平台识别：Windows 外全部归为 Unix Like 类
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

# 规范化 CPU 架构
case "$ARCH" in
    x86_64|amd64) ARCH=amd64 ;;
    aarch64|arm64) ARCH=arm64 ;;
    *) echo "[错误] 不支持的 CPU 架构: $ARCH"; exit 1 ;;
esac

# 规范化平台（Android 亦基于 Linux 内核，可用环境变量覆盖细分）
case "$OS" in
    linux)  PLATFORM=linux ;;
    *)      echo "[错误] 不支持的平台: $OS"; exit 1 ;;
esac

# 分流到 scripts 中的细分脚本
SCRIPT="$(cd "$(dirname "$0")" && pwd)/scripts/build_${PLATFORM}_${ARCH}.sh"
if [ ! -f "$SCRIPT" ]; then
    echo "[错误] 找不到细分脚本: $SCRIPT"
    exit 1
fi

exec bash "$SCRIPT"