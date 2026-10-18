/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: 自建内存分配器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CCALLOC_C_
#define _INCLUDE_CCALLOC_C_

#include "crystal_cluster.h"

// 内存对齐粒度（字节）
// 实际用户大小按该粒度向上取整；该值应不大于系统页大小。
#ifndef CC_MEM_ALIGN_MIN
#define CC_MEM_ALIGN_MIN 64
#endif

// 用户段默认大小（字节）
#ifndef CC_MEM_USER_SEGMENT_SIZE
#define CC_MEM_USER_SEGMENT_SIZE (((CCUINT64)1) << 20)
#endif

// Meta 段默认大小（字节）
#ifndef CC_MEM_META_SEGMENT_SIZE
#define CC_MEM_META_SEGMENT_SIZE (((CCUINT64)1) << 16)
#endif

// FreeList 段默认大小（字节）
#ifndef CC_MEM_FREELIST_SEGMENT_SIZE
#define CC_MEM_FREELIST_SEGMENT_SIZE (((CCUINT64)1) << 16)
#endif

// 拆分空闲区的最小剩余量（字节）
#ifndef CC_MEM_MIN_SPLIT
#define CC_MEM_MIN_SPLIT 32
#endif

// 是否自动回收 100% 空闲的用户段（1 = 开，0 = 关）
#ifndef CC_MEM_TRIM_SEGMENTS
#define CC_MEM_TRIM_SEGMENTS 1
#endif

/**
 * 内存分配器，详情见文档：
 * <工程根目录>/docs/ccalloc.md
 */
void* ccalloc(void *old, CCUINT64 size, const char *comment);

/**
 * 获取合法内存块注释
 * 非法内存块返回 NULL
 */
const char* get_comment(void *ptr);

#endif