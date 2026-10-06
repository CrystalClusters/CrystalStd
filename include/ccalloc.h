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
// 该对齐为最低要求。如果有容量较大的情况，导致无法按照最低粒度对齐，
// 那就对对齐粒度做乘2处理，直到能够容纳
// 例如 70 字节无法放入 64 字节对齐粒度，则对齐到 128 字节
#define CC_MEM_ALIGN_MIN 64

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