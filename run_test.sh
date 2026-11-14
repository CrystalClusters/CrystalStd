#!/usr/bin/env bash
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
cd "${ROOT}"

PLATFORM_STR="$(bash scripts/detect.sh)"
TEST_EXE="out/${PLATFORM_STR}_bin/test"

if [ ! -x "${TEST_EXE}" ]; then
    echo "[错误] 找不到测试程序: ${TEST_EXE}，请先执行 build.sh"
    exit 2
fi

exec "${TEST_EXE}" "$@"
