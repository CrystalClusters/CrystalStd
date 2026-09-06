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

static CCINT32 passed = 0;      // 自动用例通过数
static CCINT32 failed = 0;      // 自动用例失败数
static CCUINT32 case_code = 1;  // 自动用例编号
static CCUINT32 manual_code = 1;// 人工判断编号

/* 自动用例断言：校验 color_print 返回值（可见字符数） */
static void _expect_(const char *name, CCINT32 expected, CCINT32 got)
{
    if (expected == got)
    {
        passed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%u]"
            CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "[PASS] "
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s | expected %d, got %d\n",
            case_code++, name, expected, got
        );
    }
    else
    {
        failed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%u]"
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "[FAIL] "
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s | expected %d, got %d\n",
            case_code++, name, expected, got
        );
    }
}

/* 人工判断用例：仅打印标题，供肉眼核对，不计入 passed/failed */
static void _manual_(const char *title)
{
    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[manual_case%u]"
        CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "[人工判定]"
        CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s\n",
        manual_code++, title
    );
}

/*
 * 自动测试：color_print 返回值（可见字符计数）。
 * 每个用例先输出其有效负载（便于肉眼对照），随后打印 [PASS]/[FAIL] 结论。
 */
static void _test_auto_(void)
{
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
}

/* 人工测试：仅供肉眼核对颜色呈现，不计入统计 */
static void _test_manual_(void)
{
    // 16x16 色块矩阵
    _manual_("16x16色块矩阵（前景x背景）：核对每个格子的颜色组合");
    color_print(
        "16x16矩阵:\n"
        // 表头
        "%c_00    "
        "%c_10 1 %c_20 2 %c_30 3 %c_40 4 %c_50 5 %c_60 6 %c_70 7 "
        "%c_80 8 %c_90 9 %c_A0 A %c_B0 B %c_C0 C %c_D0 D %c_E0 E %c_F0 F\n"
        // 颜色组合
        "%c_01 1: %c_11 @ %c_21 @ %c_31 @ %c_41 @ %c_51 @ %c_61 @ %c_71 @ "
        "%c_81 @ %c_91 @ %c_A1 @ %c_B1 @ %c_C1 @ %c_D1 @ %c_E1 @ %c_F1 @\n"
        "%c_02 2: %c_12 @ %c_22 @ %c_32 @ %c_42 @ %c_52 @ %c_62 @ %c_72 @ "
        "%c_82 @ %c_92 @ %c_A2 @ %c_B2 @ %c_C2 @ %c_D2 @ %c_E2 @ %c_F2 @\n"
        "%c_03 3: %c_13 @ %c_23 @ %c_33 @ %c_43 @ %c_53 @ %c_63 @ %c_73 @ "
        "%c_83 @ %c_93 @ %c_A3 @ %c_B3 @ %c_C3 @ %c_D3 @ %c_E3 @ %c_F3 @\n"
        "%c_04 4: %c_14 @ %c_24 @ %c_34 @ %c_44 @ %c_54 @ %c_64 @ %c_74 @ "
        "%c_84 @ %c_94 @ %c_A4 @ %c_B4 @ %c_C4 @ %c_D4 @ %c_E4 @ %c_F4 @\n"
        "%c_05 5: %c_15 @ %c_25 @ %c_35 @ %c_45 @ %c_55 @ %c_65 @ %c_75 @ "
        "%c_85 @ %c_95 @ %c_A5 @ %c_B5 @ %c_C5 @ %c_D5 @ %c_E5 @ %c_F5 @\n"
        "%c_06 6: %c_16 @ %c_26 @ %c_36 @ %c_46 @ %c_56 @ %c_66 @ %c_76 @ "
        "%c_86 @ %c_96 @ %c_A6 @ %c_B6 @ %c_C6 @ %c_D6 @ %c_E6 @ %c_F6 @\n"
        "%c_07 7: %c_17 @ %c_27 @ %c_37 @ %c_47 @ %c_57 @ %c_67 @ %c_77 @ "
        "%c_87 @ %c_97 @ %c_A7 @ %c_B7 @ %c_C7 @ %c_D7 @ %c_E7 @ %c_F7 @\n"
        "%c_08 8: %c_18 @ %c_28 @ %c_38 @ %c_48 @ %c_58 @ %c_68 @ %c_78 @ "
        "%c_88 @ %c_98 @ %c_A8 @ %c_B8 @ %c_C8 @ %c_D8 @ %c_E8 @ %c_F8 @\n"
        "%c_09 9: %c_19 @ %c_29 @ %c_39 @ %c_49 @ %c_59 @ %c_69 @ %c_79 @ "
        "%c_89 @ %c_99 @ %c_A9 @ %c_B9 @ %c_C9 @ %c_D9 @ %c_E9 @ %c_F9 @\n"
        "%c_0A A: %c_1A @ %c_2A @ %c_3A @ %c_4A @ %c_5A @ %c_6A @ %c_7A @ "
        "%c_8A @ %c_9A @ %c_AA @ %c_BA @ %c_CA @ %c_DA @ %c_EA @ %c_FA @\n"
        "%c_0B B: %c_1B @ %c_2B @ %c_3B @ %c_4B @ %c_5B @ %c_6B @ %c_7B @ "
        "%c_8B @ %c_9B @ %c_AB @ %c_BB @ %c_CB @ %c_DB @ %c_EB @ %c_FB @\n"
        "%c_0C C: %c_1C @ %c_2C @ %c_3C @ %c_4C @ %c_5C @ %c_6C @ %c_7C @ "
        "%c_8C @ %c_9C @ %c_AC @ %c_BC @ %c_CC @ %c_DC @ %c_EC @ %c_FC @\n"
        "%c_0D D: %c_1D @ %c_2D @ %c_3D @ %c_4D @ %c_5D @ %c_6D @ %c_7D @ "
        "%c_8D @ %c_9D @ %c_AD @ %c_BD @ %c_CD @ %c_DD @ %c_ED @ %c_FD @\n"
        "%c_0E E: %c_1E @ %c_2E @ %c_3E @ %c_4E @ %c_5E @ %c_6E @ %c_7E @ "
        "%c_8E @ %c_9E @ %c_AE @ %c_BE @ %c_CE @ %c_DE @ %c_EE @ %c_FE @\n"
        "%c_0F F: %c_1F @ %c_2F @ %c_3F @ %c_4F @ %c_5F @ %c_6F @ %c_7F @ "
        "%c_8F @ %c_9F @ %c_AF @ %c_BF @ %c_CF @ %c_DF @ %c_EF @ %c_FF @\n"
    );

    // 单项/双向复位为默认色
    _manual_("默认色复位：红色前景应复位为默认，绿色背景应复位为默认");
    color_print(
        CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "红色前景第一行\n红色前景第二行（延续测试）"
        CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "前景已复位"
        CC_TEXT_COLOR(CC_BLUE, CC_GREEN) "绿色背景"
        CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "背景已复位\n"
    );
}

void test_color_print(void)
{
    _test_auto_();
    _test_manual_();
    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT)
        "total: %d, passed: %d, failed: %d（人工判断项不计入统计）\n",
        passed + failed, passed, failed
    );
}