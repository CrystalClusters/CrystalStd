/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

CC_RUN_BEFORE_MAIN void _init_crystal_std_()
{
    init_console();
#ifdef CC_DEBUG
    color_print(CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT) "开启水晶簇标准库\n");
#endif
    init_ccalloc();
    init_cctime();
    init_ccthread();
    init_ccfiles();
    init_ccnet();
}

CC_RUN_AFTER_MAIN void _deinit_crystal_std_()
{
    deinit_ccnet();
    deinit_ccfiles();
    deinit_ccthread();
    deinit_cctime();
    deinit_ccalloc();
#ifdef CC_DEBUG
    color_print(CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT) "关闭水晶簇标准库\n");
#endif
    deinit_console();
}