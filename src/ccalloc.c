/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 内存分配器实现
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

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

static struct ccalloc_inner {
    CCBOOL initialised;
    CCBOOL protection;
    //
} ccalloc_inner;

// 初始化相关

void init_ccalloc(void)
{
    if (ccalloc_inner.initialised)
    {
        color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "ccalloc 重复初始化不予执行。\n");
        return;
    }
    ccalloc_inner.initialised = CCTRUE;
}

void deinit_ccalloc(void)
{
    ccalloc_inner.initialised = CCFALSE;
}