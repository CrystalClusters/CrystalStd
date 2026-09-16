/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 测试用例
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"
#include <stdio.h>
#include <string.h>

static int _inner_direct_run_(CCINT32 index)
{
    switch (index)
    {
    case 0:
        test_basic();
        break;
    case 1:
        test_color_print();
        break;
    case 2:
        test_rbt_core();
        break;
    default:
        break;
    }
    // 任一组用例存在失败即返回非 0，便于脚本/CI 判断结果
    CCINT32 failures = test_basic_failures() + test_color_print_failures() + test_rbt_failures();
    return failures ? 1 : 0;
}

static void _inner_print_usage_(void)
{
    color_print("键入选项以指定测试条目：\n");
    color_print("0：基础测试\n");
    color_print("1：color_print专项\n");
    color_print("2：红黑树内核专项\n");
    color_print("q：退出\n");
    color_print("输入选项并回车：");
}

// 去掉首尾空白；EOF 返回 CCFALSE
static CCBOOL _inner_read_choice_(char *buf, CCUINT32 size)
{
    CCUINT32 len;
    if (!fgets(buf, (int)size, stdin)) return CCFALSE;
    buf[size - 1] = '\0';
    len = (CCUINT32)strlen(buf);
    // 去除行尾空白字符
    while (len > 0)
    {
        if (
            buf[len - 1] == '\n' ||
            buf[len - 1] == '\r' ||
            buf[len - 1] == '\t' ||
            buf[len - 1] == ' '
        )
            buf[--len] = '\0';
        else break;
    }
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (p != buf) memmove(buf, p, strlen(p) + 1);
    return CCTRUE;
}

int main(int argc, char **argv)
{
    if (argc > 1) return _inner_direct_run_(cc_atoi(argv[1]));
    CCINT32 code = 0;
    while (CCTRUE)
    {
        char buffer[64];
        _inner_print_usage_();
        // stdin 已结束（无终端、被重定向到空）时退出交互：避免自动化调用下空转刷屏
        if (!_inner_read_choice_(buffer, sizeof(buffer)))
            break;
        if (!strcmp(buffer, "q"))
            break;
        code = _inner_direct_run_(cc_atoi(buffer));
    }
    return code;
}