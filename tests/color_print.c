/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: color_print的测试
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"

/**
 * color_print 的测试，一部分只能人工核查。
 */
void test_color_print(void)
{
    // 16x16 色块矩阵
    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case1]"
        CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "[人工判断]16x16矩阵:\n"
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

}