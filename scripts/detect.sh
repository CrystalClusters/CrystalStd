#!/usr/bin/env bash
set -e

# 检测是否为 Android（优先）
OS=""
if command -v getprop >/dev/null 2>&1; then
    android_ver="$(getprop ro.build.version.release 2>/dev/null)"
    if [ -n "${android_ver}" ]; then
        OS="android"
    fi
fi

# 非 Android 则使用 uname
if [ -z "${OS}" ]; then
    OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
fi

# 检测 CPU 架构
ARCH="$(uname -m)"
case "${ARCH}" in
    x86_64|amd64)   ARCH="amd64" ;;
    aarch64|arm64)  ARCH="arm64" ;;
    *) ARCH="unknown" ;;
esac

# 输出平台描述串
echo "${OS}_${ARCH}"
