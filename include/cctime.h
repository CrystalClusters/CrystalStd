/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 高精度时间组件
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CCTIME_H_
#define _INCLUDE_CCTIME_H_

#include "crystal_cluster.h"

// 日期结构体
typedef struct cc_date {
    CCINT16 year;  //年
    CCINT16 month; //月份
    CCINT16 dweek; //周
    CCINT16 day;   //日
    CCINT16 hour;  //时
    CCINT16 min;   //分
    CCINT16 sec;   //秒
    CCINT16 ms;    //毫秒
} CCDate;

// 获取高精度时间戳
double cctime();

// 高精度休眠
void ccsleep(CCUINT32 us);

// 日期获取
CCDate ccdate();

#endif