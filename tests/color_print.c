/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: color_print的测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"

/*
 * color_print 的返回值 = 输出的可见字符数（%c_XY 颜色控制符、ANSI 转义码不计入）。
 * 据此可自动断言返回值契约；颜色本身的呈现（前景/背景/复位）只能人工判断。
 */

/* 自动用例断言：校验 color_print 返回值（可见字符数） */
static void _expect_(const char *name, CCINT32 expected, CCINT32 got)
{
    CC_CHECK_EQ_I64(name, expected, got);
}

/*
 * 自动测试：color_print 返回值（可见字符计数）。
 * 每个用例先输出其有效负载（便于肉眼对照），随后打印 [PASS]/[FAIL] 结论。
 */
static void _test_auto_(void)
{
    CC_CASE("COLOR-AUTO-001", "可见字符计数");
    _expect_("plain text", 6, color_print("hello\n"));
    _expect_("multi newline", 4, color_print("a\nb\n"));
    _expect_("color + text", 3, color_print(CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "OK\n"));
    _expect_("color not consume arg", 2, color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "%d\n", 7));
    _expect_("reset both (%c_00)", 6, color_print(CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "reset\n"));
    _expect_("color + mid reset", 3, color_print("%c_20a%c_00b\n"));
    _expect_("%d", 5, color_print("n=%d\n", 42));
    _expect_("%d negative", 5, color_print("%d\n", -123));
    _expect_("%u max", 11, color_print("%u\n", 4294967295u));
    _expect_("%x", 11, color_print("%x\n", 0xABCDu));
    _expect_("%ld", 14, color_print("%ld\n", (CCINT64)1234567890123LL));
    _expect_("%s", 4, color_print("%s\n", "abc"));
    _expect_("%s null", 7, color_print("%s\n", (const char*)NULL));
    _expect_("%c", 2, color_print("%c\n", 'x'));
    _expect_("%% literal", 2, color_print("%%\n"));
    _expect_("%f", 9, color_print("%f\n", 1.5));
    _expect_("%.2f", 5, color_print("%.2f\n", 3.14159));
    _expect_("invalid hex fallback", 5, color_print("%c_GG\n", 'x'));
    _expect_("incomplete color escape eof", 2, color_print("%c_", 'x'));
    _expect_("incomplete color escape newline", 3, color_print("%c_\n", 'x'));
}

void test_color_print(void)
{
    _test_auto_();
}