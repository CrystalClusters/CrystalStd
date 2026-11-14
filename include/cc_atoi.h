/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 数字转为整型
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CC_ATOI_H_
#define _INCLUDE_CC_ATOI_H_

#include "crystal_cluster.h"

/**
 * 不符合数字格式的，返回0 
 * 符合数字格式，但是溢出的，返回 INT32_MAX
 * 正常情况下返回转化好的整数值
 */
CCINT32 cc_atoi(const char *str_num);

#endif