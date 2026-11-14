/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 时间相关组件实现
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"

struct crystal_timer_data_collection
{
#ifdef CC_WINDOWS
    LARGE_INTEGER frequency;
    LARGE_INTEGER count;
#else
    time_t crTime;
    struct tm *ptm;
    struct timeval ti;
    struct timespec ts;
#endif
} collection;

void init_cctime(void)
{
#ifdef CC_WINDOWS
    if (!QueryPerformanceFrequency(&collection.frequency))
        color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "QueryPerformanceFrequency执行失败\n");
#endif
    // 只有Windows需要初始化
}

void deinit_cctime(void)
{
    // 无需任何操作
}

// 获取高精度相对时间戳
double cctime()
{
#ifdef CC_WINDOWS
    QueryPerformanceCounter(&collection.count);
    return (double)(collection.count.QuadPart) / (double)(collection.frequency.QuadPart);
#else
    clock_gettime(CLOCK_MONOTONIC, &collection.ts);
    return (double)(collection.ts.tv_sec) + (double)(collection.ts.tv_nsec) / 1000000000;
#endif
}

// 高精度休眠
void ccsleep(CCUINT32 us)
{
#ifdef CC_WINDOWS
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    CCUINT64 target_ticks = (us * collection.frequency.QuadPart) / 1000000;
    while (TRUE)
    {
        QueryPerformanceCounter(&end);
        CCUINT64 elapsed = end.QuadPart - start.QuadPart;
        if (elapsed >= target_ticks)
        {
            break;
        }
        // 使用Yield或短睡眠避免忙等待
        if (target_ticks - elapsed > (CCUINT64)(collection.frequency.QuadPart / 1000))
        {
            // 还有超过1ms，使用短Sleep
            Sleep(1);
        }
        else
        {
            // 短时间，使用SwitchToThread或yield
            SwitchToThread();
        }
    }
#else
    struct timespec req = {
        .tv_sec = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000};
    struct timespec rem;
    while (nanosleep(&req, &rem) == -1)
    {
        if (errno == EINTR)
        {
            req = rem;
        }
        else
        {
            break;
        }
    }
#endif
}

// 日期获取
CCDate ccdate()
{
    CCDate date;
#ifdef CC_WINDOWS
    GetLocalTime((LPSYSTEMTIME)&date);
#else
    static time_t t = 0;
    static struct tm *ptm = NULL;
    static struct timeval ti = {0};
    t = time(0);
    ptm = localtime(&t);
    date.year = ptm->tm_year + 1900; // 在Linux就只能手动搬砖了
    date.month = ptm->tm_mon + 1;
    date.day = ptm->tm_mday;
    date.dweek = ptm->tm_wday;
    date.hour = ptm->tm_hour;
    date.min = ptm->tm_min;
    date.sec = ptm->tm_sec;
    gettimeofday(&ti, NULL);
    date.ms = ti.tv_usec % 1000000 / 1000;
#endif
    return date;
}
