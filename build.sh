#!/usr/bin/env bash
set -e

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

# 拼接工具路径
FILTER="tools/${PLATFORM_STR}/filter.run"
echo "${FILTER}"

# 生成静态库
echo "生成静态库..."
bash scripts/build_lib.sh ${PLATFORM_STR}

# 编译测试程序
echo "生成测试程序..."
bash scripts/build_test.sh ${PLATFORM_STR}
