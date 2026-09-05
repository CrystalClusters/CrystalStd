/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

CC_RUN_BEFORE_MAIN void _init_crystal_std_()
{
    init_console();
    color_print(CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT) "开启水晶簇标准库\n");
}

CC_RUN_AFTER_MAIN void _deinit_crystal_std_()
{
    color_print(CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT) "关闭水晶簇标准库\n");
    deinit_console();
}