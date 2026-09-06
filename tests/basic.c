/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 针对基础功能组件的测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"

static CCINT32 _passed = 0;
static CCINT32 _failed = 0;

static void _expect_(const char *name, const char *input, CCINT32 expected)
{
    CCINT32 got = cc_atoi(input);
    if (got == expected)
    {
        _passed++;
        color_print(CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "[PASS] %s -> %d\n", name, got);
    }
    else
    {
        _failed++;
        color_print(
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[FAIL] %-12s input=\"%s\", expected %d, got %d\n",
            name, input ? input : "(null)", expected, got
        );
    }
}

/**
 * 成功返回CCTRUE，失败返回CCFALSE
 */
void test_cc_atoi(void)
{
    _expect_("null",        NULL,            0);
    _expect_("empty",       "",              0);
    _expect_("zero",        "0",             0);
    _expect_("positive",    "123",           123);
    _expect_("negative",    "-123",          -123);
    _expect_("plus sign",   "+123",          0);
    _expect_("mixed",       "12a3",          0);
    _expect_("lead space",  " 123",          0);
    _expect_("only minus",  "-",             0);
    _expect_("negative zero","-0",           0);
    _expect_("lead zeros",  "000123",        123);
    _expect_("int32 max",   "2147483647",    2147483647);
    _expect_("int32 max+1", "2147483648",    2147483647);
    _expect_("int32 min",   "-2147483648",   -2147483648);
    _expect_("int32 min-1", "-2147483649",   -2147483648);
    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT)
        "total: %d, passed: %d, failed: %d\n",
        _passed + _failed, _passed, _failed
    );
}