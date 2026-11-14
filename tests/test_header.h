/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 测试程序内部头文件。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_TEST_HEADER_H_
#define _INCLUDE_TEST_HEADER_H_

#include <cc_atoi.h>
#include <cc_crc.h>
#include <cc_wildcard.h>
#include <color_print.h>
#include <cctime.h>
#include <ccfiles.h>

#include "test_harness.h"

/* 七个进程内套件；每个套件内部用 CC_CASE 显式标记用例编号。 */
void test_basic(void);
void test_color_print(void);
void test_rbt_core(void);
void test_dyn_core(void);
void test_ccalloc(void);
void test_thread(void);
void test_ccfiles(void);

/* 人工确认项：只在交互终端中、全部自动套件之后运行。 */
void test_manual(void);

#endif
