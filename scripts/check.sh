#!/usr/bin/env bash

echo "[检测] 正在检测工具链..."

# 1) 编译器是否存在
if ! command -v gcc >/dev/null 2>&1; then
    echo "[错误] 未找到编译器 '$CC'，请先安装 gcc 并将其加入 PATH。"
    exit 1
fi

# 2) ar 是否存在（用于静态库打包）
if ! command -v ar >/dev/null 2>&1; then
    echo "[错误] 未找到 ar，请先安装 binutils 并将其加入 PATH。"
    exit 2
fi

echo "[检测] gcc 版本：$(gcc --version 2>/dev/null | head -n 1)"
echo "[检测] ar  版本：$(ar --version 2>/dev/null | head -n 1)"

echo [检测] 工具链检测通过。

exit 0
