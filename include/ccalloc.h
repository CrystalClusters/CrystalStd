/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 自建内存分配器
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CCALLOC_C_
#define _INCLUDE_CCALLOC_C_

#include "crystal_cluster.h"

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