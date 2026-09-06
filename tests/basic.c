/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 针对基础功能组件的测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"

static CCINT32 passed = 0;
static CCINT32 failed = 0;
static CCUINT32 case_code = 1;  // 自动用例编号

static void _expect_(const char *name, const char *input, CCINT32 expected, CCUINT32 case_code)
{
    CCINT32 got = cc_atoi(input);
    if (got == expected)
    {
        passed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%d]"
            CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "[PASS] "
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s -> %d\n", case_code, name, got
        );
    }
    else
    {
        failed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%d]"
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "[FAIL]"
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) " %-12s input=\"%s\", expected %d, got %d\n",
            case_code, name, input ? input : "(null)", expected, got
        );
    }
}

static void test_cc_atoi(void)
{
    _expect_("null", NULL, 0, case_code++);
    _expect_("empty", "", 0, case_code++);
    _expect_("zero", "0", 0, case_code++);
    _expect_("positive", "123", 123, case_code++);
    _expect_("negative", "-123", -123, case_code++);
    _expect_("plus sign", "+123", 0, case_code++);
    _expect_("mixed", "12a3", 0, case_code++);
    _expect_("lead space", " 123", 0, case_code++);
    _expect_("only minus", "-", 0, case_code++);
    _expect_("negative zero", "-0", 0, case_code++);
    _expect_("lead zeros", "000123", 123, case_code++);
    _expect_("int32 max", "2147483647", 2147483647, case_code++);
    _expect_("int32 max+1", "2147483648", 2147483647, case_code++);
    _expect_("int32 min", "-2147483648", -2147483648, case_code++);
    _expect_("int32 min-1", "-2147483649", -2147483648, case_code++);
}

void test_basic(void)
{
    test_cc_atoi();
    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT)
        "total: %d, passed: %d, failed: %d\n",
        passed + failed, passed, failed
    );
}