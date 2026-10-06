/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: 内存分配器实现
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

// 段内存分配时的默认大小（1MB）
#define CC_DEFAULT_SEGMENT (((CCUINT64)(1)) << 20)

// 内部数据结构

typedef struct MetaNode
{
    void *ptr;  //用户拿到的内存块首地址，作为红黑树键
    CCUINT64 size;  //实际分配的内存块大小（字节）
    CCUINT32 comment_size;  //注释长度（字节）
    CCUINT32 parent;  //父节点下标，0为空节点
    CCUINT32 left;  //左子节点下标，0为空节点
    union {
        CCUINT32 right;  //右子节点下标，0为空节点
        CCUINT32 free_list_next;  //链表模式下，下一个空闲节点下标，0表示没有
    };
    CCUINT8 flags;  //标志位：0-颜色（1为红），1、2、3位为1时分别表明parent、left、right需要跨段寻址
    struct BlockMeta *parent_base;  //需要跨段时，父节点所在的段基址
    struct BlockMeta *left_base;  //需要跨段时，左子节点所在的段基址
    union {
        struct BlockMeta *right_base;  //需要跨段时，右子节点所在的段基址
        struct BlockMeta *free_list_next_base;  //链表模式下，需要跨段时，后继节点所在的段基址
    };
} MetaNode;

typedef struct FreeListNode {
    void *ptr;  //内存块其实起始地址
    CCUINT64 size;  //内存块大小
    CCUINT32 prev;  //前驱节点下标
    CCUINT32 next;  //后继节点下标
    CCUINT8 flags;  //标志位：0、1分别表面前驱和后继节点是否需要跨段寻址
    struct FreeListNode *prev_base;  //需要跨段时，前驱节点所在的段基址
    struct FreeListNode *next_base;  //需要跨段时，后继节点所在的段基址
} FreeListNode;

// 注意对齐到 CC_MEM_ALIGN_MIN
typedef struct MemoryHeader {
    CCUINT32 header_size;  //块头大小
    CCUINT32 align;        //实际对齐粒度（字节）
    CCUINT64 size;         //该内存块的实际总和大小（含头）
    struct MemoryHeader *next;
} MemoryHeader;

static struct ccalloc_inner {
    CCBOOL initialised;  //初始化标志位
    CCBOOL protection;
    MemoryHeader *MetaBaseHeader;  //元数据首块基地址
    MemoryHeader *FreeBaseHeader;  //空闲链首块基地址
    MemoryHeader *UserBaseHeader;  //用户区首块基地址
    //
} ccalloc_inner = {
    .initialised = CCFALSE,
    .protection = CCFALSE,
    .MetaBaseHeader = NULL,
    .FreeBaseHeader = NULL,
    .UserBaseHeader = NULL
};

// 初始化相关

void init_ccalloc(void)
{
    if (ccalloc_inner.initialised)
    {
        color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "ccalloc 重复初始化不予执行。\n");
        return;
    }
    // 默认参数
    CCUINT64 segment_size = CC_DEFAULT_SEGMENT;
    CCUINT32 header_size = sizeof(MemoryHeader);
    CCUINT32 align = CC_MEM_ALIGN_MIN;
    while (align < header_size) align <<= 1;
    //
    ccalloc_inner.MetaBaseHeader = alloc_segment(segment_size);
    if (!ccalloc_inner.MetaBaseHeader)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "内存元数据段分配失败\n");
        goto FailedtoAllocMeta;
    }
    ccalloc_inner.FreeBaseHeader = alloc_segment(segment_size);
    if (!ccalloc_inner.FreeBaseHeader)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "内存空闲表段分配失败\n");
        goto FailedtoAllocFree;
    }
    ccalloc_inner.UserBaseHeader = alloc_segment(segment_size);
    if (!ccalloc_inner.UserBaseHeader)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "用户段分配失败\n");
        goto FailedtoAllocUser;
    }
    // 写入参数
    ccalloc_inner.MetaBaseHeader->header_size = header_size;
    ccalloc_inner.MetaBaseHeader->align = align;
    ccalloc_inner.MetaBaseHeader->size = segment_size;
    ccalloc_inner.MetaBaseHeader->next = NULL;
    
    ccalloc_inner.FreeBaseHeader->header_size = header_size;
    ccalloc_inner.FreeBaseHeader->align = align;
    ccalloc_inner.FreeBaseHeader->size = segment_size;
    ccalloc_inner.FreeBaseHeader->next = NULL;

    ccalloc_inner.UserBaseHeader->header_size = header_size;
    ccalloc_inner.UserBaseHeader->align = align;
    ccalloc_inner.UserBaseHeader->size = segment_size;
    ccalloc_inner.UserBaseHeader->next = NULL;
    // 全流程正常才标注为初始化成功
    ccalloc_inner.initialised = CCTRUE;
    return;
FailedtoAllocUser:
    free_segment(ccalloc_inner.FreeBaseHeader, segment_size);
FailedtoAllocFree:
    free_segment(ccalloc_inner.MetaBaseHeader, segment_size);
FailedtoAllocMeta:
    return;
}

static void clear_segments()
{
    // 将内存段串成的链表全部释放
    MemoryHeader *p = NULL;
    MemoryHeader *q = NULL;
    // 元数据
    p = ccalloc_inner.MetaBaseHeader;
    while (p)
    {
        q = p->next;
        free_segment(p, p->size);
        p = q;
    }
    // 空闲链
    p = ccalloc_inner.FreeBaseHeader;
    while (p)
    {
        q = p->next;
        free_segment(p, p->size);
        p = q;
    }
    // 用户区
    p = ccalloc_inner.UserBaseHeader;
    while (p)
    {
        q = p->next;
        free_segment(p, p->size);
        p = q;
    }
}

void deinit_ccalloc(void)
{
    if (!ccalloc_inner.initialised) return;
    clear_segments();
    ccalloc_inner.initialised = CCFALSE;
}