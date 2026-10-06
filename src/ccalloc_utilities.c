/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-10
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: 平台兼容层，工具函数集
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

// 段内存分配
// 段内存是天然大尺度对齐的
void* alloc_segment(CCUINT64 size)
{
    void *p = NULL;
#ifdef CC_WINDOWS
    p = VirtualAlloc(NULL, (SIZE_T)size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    p = mmap(NULL, (size_t)size, PORT_READ | PORT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
#endif
    return p;
}

// 段内存释放
void free_segment(void *ptr, CCUINT64 size)
{
    if (!ptr) return;
#ifdef CC_WINDOWS
    CC_UNUSED(size);
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, (size_t)size);
#endif
}