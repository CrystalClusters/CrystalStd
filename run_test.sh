# 平台检测
PLATFORM_STR="$(bash "$(dirname "$0")/scripts/detect.sh")"

# 运行可执行文件
out/${PLATFORM_STR}_bin/test