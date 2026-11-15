#!/usr/bin/env bash
set -e

# 平台检测
PLATFORM_STR="$(bash "$(dirname "$0")/scripts/detect.sh")"
BIN_DIR="out/${PLATFORM_STR}_bin"

list_demos()
{
    if [ ! -d "${BIN_DIR}" ]; then
        return 0
    fi
    for f in "${BIN_DIR}"/*_demo; do
        if [ -x "${f}" ]; then
            echo "  $(basename "${f}")"
        fi
    done
}

# 无参数：进入交互菜单；直接回车、0、q 都可退出
if [ $# -eq 0 ]; then
    demos=()
    if [ -d "${BIN_DIR}" ]; then
        while IFS= read -r f; do
            if [ -x "${f}" ]; then
                demos+=("${f}")
            fi
        done < <(find "${BIN_DIR}" -maxdepth 1 -type f -name '*_demo' | sort)
    fi

    if [ ${#demos[@]} -eq 0 ]; then
        echo "[错误] 没有可运行的演示程序，请先构建。"
        exit 1
    fi

    echo
    echo "可用演示："
    for i in "${!demos[@]}"; do
        echo "  $((i + 1)). $(basename "${demos[$i]}")"
    done
    echo "  0. 退出"
    echo

    printf '请选择要运行的演示（直接回车退出）：'
    choice=""
    if ! IFS= read -r choice; then
        echo
        exit 0
    fi

    case "${choice}" in
        ""|0|q|Q|quit|exit)
            exit 0
            ;;
    esac

    case "${choice}" in
        *[!0-9]*)
            echo "[错误] 无效选择: ${choice}"
            exit 1
            ;;
    esac

    index=$((10#${choice}))
    if [ "${index}" -lt 1 ] || [ "${index}" -gt ${#demos[@]} ]; then
        echo "[错误] 无效选择: ${choice}"
        exit 1
    fi
    exec "${demos[$((index - 1))]}"
fi

DEMO="$1"

if [ "${DEMO}" = "--list" ]; then
    echo "可用演示："
    list_demos
    exit 0
fi

EXE="${BIN_DIR}/${DEMO}_demo"
if [ -x "${EXE}" ]; then
    exec "${EXE}"
fi

echo "[错误] 未找到演示程序: ${EXE}"
echo "可用演示："
list_demos
exit 1
