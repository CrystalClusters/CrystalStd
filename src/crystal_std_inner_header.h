/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 用于内部接口流通的头文件。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CRYSTAL_STD_INNER_H_
#define _INCLUDE_CRYSTAL_STD_INNER_H_

#include <crystal_cluster.h>
#include <color_print.h>
#include <cc_rbt.h>
#include <cc_dyn.h>
#include <ccalloc.h>
#include <cctime.h>
#include <ccthread.h>
#include <cc_crc.h>
#include <cc_wildcard.h>
#include <ccfiles.h>

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#endif

//
// 初始化color_print的内部接口
void init_console(void);
void deinit_console(void);
//
// 初始化ccalloc的内部接口
void init_ccalloc(void);
CCUINT32 deinit_ccalloc(void);
//
// 初始化cctime的内部参数
void init_cctime(void);
void deinit_cctime(void);
//
// 初始化ccthread的内部参数
void init_ccthread(void);
void deinit_ccthread(void);
//
// 初始化ccfiles的内部参数
void init_ccfiles(void);
void deinit_ccfiles(void);

//
// 总构造、析构函数
//

void _init_crystal_std_();
void _deinit_crystal_std_();

//
// 工具函数
//

void* alloc_segment(CCUINT64 size);
void free_segment(void *ptr, CCUINT64 size);

// 页保护：成功返回 CCTRUE，失败返回 CCFALSE
CCBOOL protect_segment(void *ptr, CCUINT64 size);
CCBOOL unprotect_segment(void *ptr, CCUINT64 size);

// 跨平台兼容接口
#ifdef CC_WINDOWS
static inline int pthread_mutex_lock(CRITICAL_SECTION *lock)
{
    EnterCriticalSection(lock);
    return 0;
}
static inline int pthread_mutex_unlock(CRITICAL_SECTION *lock)
{
    LeaveCriticalSection(lock);
    return 0;
}
static inline int pthread_mutex_init(CRITICAL_SECTION *lock, void *unUsed)
{
    CC_UNUSED(unUsed);
    InitializeCriticalSection(lock);
    return 0;
}
static inline int pthread_mutex_destroy(CRITICAL_SECTION *lock)
{
    DeleteCriticalSection(lock);
    return 0;
}
#endif

#endif
