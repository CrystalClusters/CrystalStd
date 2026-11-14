#!/usr/bin/env bash
set -e

# 构建模式：仅显式传入 release 时启用发行构建
BUILD_MODE="DEBUG"
CC_BUILD_CFLAGS="-O0 -g3 -Wall -Wextra"
case "${1:-}" in
    [Rr][Ee][Ll][Ee][Aa][Ss][Ee])
        BUILD_MODE="RELEASE"
        CC_BUILD_CFLAGS="-O2 -DNDEBUG -DCC_RELEASE -Wall -Wextra -Werror"
        ;;
    "")
        ;;
    *)
        echo "[提示] 未知构建模式：$1，按 DEBUG 构建。"
        ;;
esac

echo "构建模式：【${BUILD_MODE}】"
echo "编译选项：${CC_BUILD_CFLAGS}"

# 平台检测
PLATFORM_STR="$(bash "$(dirname "$0")/scripts/detect.sh")"

echo "当前平台为：【${PLATFORM_STR}】"

# 工具链检测：未通过则中止构建
CHECK=$(cd $(dirname $0) && pwd)/scripts/check.sh
if [ ! -f "${CHECK}" ]; then
    echo "[错误] 找不到工具链检测脚本: ${CHECK}"
    exit 1
fi
if ! bash "${CHECK}"; then
    echo "[错误] 工具链检测未通过，中止构建。"
    exit 1
fi

# 生成静态库
echo "生成静态库..."
bash scripts/build_lib.sh "${PLATFORM_STR}" "${CC_BUILD_CFLAGS}"

# 编译测试程序
echo "生成测试程序..."
bash scripts/build_test.sh "${PLATFORM_STR}" "${CC_BUILD_CFLAGS}"

# 编译演示程序
echo "生成演示程序..."
bash scripts/build_demo.sh "${PLATFORM_STR}" "${CC_BUILD_CFLAGS}"
