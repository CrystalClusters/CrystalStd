/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-08
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_COLOR_PRINT_H_
#define _INCLUDE_COLOR_PRINT_H_

#include "crystal_cluster.h"

/* ============================================================
 * 颜色代码（十六进制）
 * 颜色代码不区分前景/背景，由在 CC_TEXT_COLOR 中的位置决定角色。
 * 0 = 默认色（复位该项为终端默认色；填 0 的一项即单独复位）
 * 1~F = 标准 16 色
 *
 * 宏值采用字符串字面量，利用 C 字符串拼接生成格式串。
 * ============================================================ */
#define CC_DEFAULT    "0"
#define CC_RED        "1"
#define CC_GREEN      "2"
#define CC_YELLOW     "3"
#define CC_BLUE       "4"
#define CC_MAGENTA    "5"
#define CC_CYAN       "6"
#define CC_WHITE      "7"
#define CC_GRAY       "8"
#define CC_BRED       "9"
#define CC_BGREEN     "A"
#define CC_BYELLOW    "B"
#define CC_BBLUE      "C"
#define CC_BMAGENTA   "D"
#define CC_BCYAN      "E"
#define CC_BWHITE     "F"

/* 构造颜色控制符：CC_TEXT_COLOR(前景, 背景) → "%c_XY"（字符串拼接） */
#define CC_TEXT_COLOR(fg, bg)  "%c_" fg bg

/**
 * 用于提供彩色命令行输出
 * 格式控制符大部分与 print 一样，颜色控制符：
 *   CC_TEXT_COLOR(前景色, 背景色)  →  "%c_<FG><BG>"
 * 颜色代码：0~F 十六进制，位置决定角色（前景/背景）。
 *
 * 示例：
 *   color_print(CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "OK\n");   // 绿字 + 默认背景
 *   color_print(CC_TEXT_COLOR(CC_RED, CC_BLACK) "错误\n");     // 红字黑底
 *   color_print("%c_20绿色文本\n");
 * CC_TEXT_COLOR 中某一项填 CC_DEFAULT 表示该项单独复位为默认色，
 * 两项均填 CC_DEFAULT（%c_00）即前景、背景同时复位为默认。
 * 调用结束后自动恢复终端默认颜色（仅当颜色曾被修改时）。
 */
CCINT32 color_print(const char* pattern, ...);

#endif
