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
    default:
        break;
    }
    return 0;
}

static void _inner_print_usage_(void)
{
    color_print("键入选项以指定测试条目：\n");
    color_print("0：基础测试\n");
    color_print("1：color_print专项\n");
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
    while (CCTRUE)
    {
        char buffer[64];
        _inner_print_usage_();
        _inner_read_choice_(buffer, sizeof(buffer));
        if (!strcmp(buffer, "q"))
            break;
        _inner_direct_run_(cc_atoi(buffer));
    }
    return 0;
}