/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: 内存分配器实现。
 *               设计文档见 <工程根目录>/docs/ccalloc.md。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccalloc_internal.h"

#include <string.h>

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/*
 * ============================================================================
 * 全局状态
 * ============================================================================
 */

typedef struct CC_AllocatorState {
    CCBOOL                initialized;         // 分配器已初始化
    CCBOOL                protection_enabled;  // 页保护生效中
    CCUINT64              page_size;           // 系统页大小
    void                 *meta_head;           // 首个 Meta 段
    void                 *meta_cur;            // 最后一个 Meta 段
    void                 *flist_head;          // 首个 FreeList 段
    void                 *flist_cur;           // 最后一个 FreeList 段
    CC_UserSegmentHeader *user_head;           // 用户段链表头
    CC_UserSegmentHeader *user_tail;           // 用户段链表尾
} CC_AllocatorState;

static CC_AllocatorState g_cc = {
    CCFALSE, CCFALSE, 4096, NULL, NULL, NULL, NULL, NULL, NULL
};

/*
 * ============================================================================
 * 互斥
 *
 * 采用 GCC 原子内建实现的轻量自旋锁：
 *  - 不依赖 pthread / 平台互斥对象，也不增加链接参数；
 *  - 临界区内只做内存管理与少量输出，持锁时间短；
 *  - 分配器不追求高并发吞吐，自旋成本可接受。
 * ============================================================================
 */

static volatile CCUINT32 g_ccalloc_lock = 0;

static void ccalloc_lock(void)
{
    while (__atomic_test_and_set(&g_ccalloc_lock, __ATOMIC_ACQUIRE))
    {
        while (__atomic_load_n(&g_ccalloc_lock, __ATOMIC_RELAXED))
        {
            // 自旋等待
        }
    }
}

static void ccalloc_unlock(void)
{
    __atomic_store_n(&g_ccalloc_lock, 0, __ATOMIC_RELEASE);
}

/*
 * ============================================================================
 * 基础工具
 * ============================================================================
 */

static CCUINT64 detect_page_size(void)
{
#ifdef CC_WINDOWS
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (CCUINT64)info.dwPageSize;
#else
    long value = sysconf(_SC_PAGESIZE);
    return (value > 0) ? (CCUINT64)value : 4096;
#endif
}

// 向上取整；溢出时 ok = CCFALSE
static CCUINT64 round_up_u64(CCUINT64 value, CCUINT64 align, CCBOOL *ok)
{
    CCUINT64 remainder;
    if (!align)
    {
        *ok = CCFALSE;
        return 0;
    }
    remainder = value % align;
    if (!remainder)
    {
        *ok = CCTRUE;
        return value;
    }
    if (value > (CCUINT64)-1 - (align - remainder))
    {
        *ok = CCFALSE;
        return 0;
    }
    *ok = CCTRUE;
    return value + (align - remainder);
}

// 用户数据区相对用户段基址的偏移（默认对齐下等于 64）
static CCUINT64 user_data_offset(void)
{
    CCBOOL ok = CCFALSE;
    CCUINT64 offset = round_up_u64((CCUINT64)sizeof(CC_UserSegmentHeader),
                                   (CCUINT64)CC_MEM_ALIGN_MIN, &ok);
    return ok ? offset : (CCUINT64)sizeof(CC_UserSegmentHeader);
}

static CC_SegHeader *seg_header(void *seg)
{
    return (CC_SegHeader *)seg;
}

// 首个 Meta 段的锚块
static CC_MetaAnchor *meta_anchor(void)
{
    return (CC_MetaAnchor *)((CCUINT8 *)g_cc.meta_head + sizeof(CC_SegHeader));
}

// 首个 FreeList 段的锚块
static CC_FreeAnchor *free_anchor(void)
{
    return (CC_FreeAnchor *)((CCUINT8 *)g_cc.flist_head + sizeof(CC_SegHeader));
}

static MetaNode *meta_root(void)
{
    return meta_anchor()->root;
}

static void meta_root_set(void *root)
{
    meta_anchor()->root = (MetaNode *)root;
}

/*
 * ============================================================================
 * 前向声明
 * ============================================================================
 */

static CCBOOL meta_seg_new(void);
static CCBOOL flist_seg_new(void);
static CCBOOL user_seg_new(CCUINT64 need);
static void trim_segments(void);
static void free_by_ptr(void *ptr);

/*
 * ============================================================================
 * Meta 节点池
 * ============================================================================
 */

static void meta_pool_push(MetaNode *node)
{
    CC_MetaAnchor *anchor = meta_anchor();
    memset(node, 0, sizeof(MetaNode));
    node->right = anchor->pool_head;
    node->flags = CC_META_FLAG_FREE;
    anchor->pool_head = node;
}

static MetaNode *meta_pool_pop(void)
{
    CC_MetaAnchor *anchor = meta_anchor();
    MetaNode *node = NULL;

    if (!anchor->pool_head && !meta_seg_new())
        return NULL;

    node = anchor->pool_head;
    if (!node)
        return NULL;

    anchor->pool_head = node->right;
    memset(node, 0, sizeof(MetaNode));
    return node;
}

// 追加一个 Meta 段，并把段内全部节点推入空闲池（首个 Meta 段由 init 单独建立）
static CCBOOL meta_seg_new(void)
{
    void *seg = alloc_segment(CC_MEM_META_SEGMENT_SIZE);
    CCUINT64 offset = sizeof(CC_SegHeader);
    CCUINT64 count = 0;
    CCUINT8 *base = NULL;

    if (!seg)
        return CCFALSE;

    if (CC_MEM_META_SEGMENT_SIZE > offset)
        count = (CC_MEM_META_SEGMENT_SIZE - offset) / sizeof(MetaNode);
    if (!count)
    {
        free_segment(seg, CC_MEM_META_SEGMENT_SIZE);
        return CCFALSE;
    }

    seg_header(seg)->next_seg = NULL;
    seg_header(seg)->raw_base = seg;
    if (g_cc.meta_cur)
        seg_header(g_cc.meta_cur)->next_seg = seg;
    g_cc.meta_cur = seg;

    base = (CCUINT8 *)seg + offset;
    for (CCUINT64 i = 0; i < count; ++i)
        meta_pool_push((MetaNode *)(base + i * sizeof(MetaNode)));

    return CCTRUE;
}

/*
 * ============================================================================
 * FreeList 槽位池
 * ============================================================================
 */

static void freeblock_release(FreeListNode *desc)
{
    CC_FreeAnchor *anchor = free_anchor();
    memset(desc, 0, sizeof(FreeListNode));
    desc->next = anchor->pool_head;
    desc->flags = CC_FREE_FLAG_SLOT_FREE;
    anchor->pool_head = desc;
}

static FreeListNode *freeblock_alloc(void)
{
    CC_FreeAnchor *anchor = free_anchor();
    FreeListNode *desc = NULL;

    if (!anchor->pool_head && !flist_seg_new())
        return NULL;

    desc = anchor->pool_head;
    if (!desc)
        return NULL;

    anchor->pool_head = desc->next;
    memset(desc, 0, sizeof(FreeListNode));
    return desc;
}

// 追加一个 FreeList 段，并把段内全部槽位推入空闲池（首个段由 init 单独建立）
static CCBOOL flist_seg_new(void)
{
    void *seg = alloc_segment(CC_MEM_FREELIST_SEGMENT_SIZE);
    CCUINT64 offset = sizeof(CC_SegHeader);
    CCUINT64 count = 0;
    CCUINT8 *base = NULL;

    if (!seg)
        return CCFALSE;

    if (CC_MEM_FREELIST_SEGMENT_SIZE > offset)
        count = (CC_MEM_FREELIST_SEGMENT_SIZE - offset) / sizeof(FreeListNode);
    if (!count)
    {
        free_segment(seg, CC_MEM_FREELIST_SEGMENT_SIZE);
        return CCFALSE;
    }

    seg_header(seg)->next_seg = NULL;
    seg_header(seg)->raw_base = seg;
    if (g_cc.flist_cur)
        seg_header(g_cc.flist_cur)->next_seg = seg;
    g_cc.flist_cur = seg;

    base = (CCUINT8 *)seg + offset;
    for (CCUINT64 i = 0; i < count; ++i)
        freeblock_release((FreeListNode *)(base + i * sizeof(FreeListNode)));

    return CCTRUE;
}

/*
 * ============================================================================
 * 用户段空闲链表
 * ============================================================================
 */

// 将 [ptr, ptr + size) 归还到 seg 的空闲链，并与相邻空闲区合并。
// 描述符耗尽时返回 CCFALSE（该段尾部空间暂时无法记账，但不影响已分配块）。
static CCBOOL free_list_insert(CC_UserSegmentHeader *seg, void *ptr, CCUINT64 size)
{
    CCUINT8 *start = (CCUINT8 *)ptr;
    FreeListNode *prev = NULL;
    FreeListNode *cur = NULL;
    FreeListNode *desc = NULL;

    if (!seg || !ptr || !size)
        return CCTRUE;

    cur = seg->free_head;
    while (cur && (CCUINTPTR)cur->start < (CCUINTPTR)start)
    {
        prev = cur;
        cur = cur->next;
    }

    // 优先并入前驱
    if (prev && (CCUINT8 *)prev->start + prev->size == start)
    {
        prev->size += size;
        if (cur && (CCUINT8 *)prev->start + prev->size == (CCUINT8 *)cur->start)
        {
            prev->size += cur->size;
            prev->next = cur->next;
            if (cur->next)
                cur->next->prev = prev;
            freeblock_release(cur);
        }
        return CCTRUE;
    }

    // 再尝试向后扩展后继
    if (cur && start + size == (CCUINT8 *)cur->start)
    {
        cur->start = start;
        cur->size += size;
        return CCTRUE;
    }

    // 两侧均不相邻，取一个新描述符插入
    desc = freeblock_alloc();
    if (!desc)
        return CCFALSE;

    desc->start = start;
    desc->size = size;
    desc->prev = prev;
    desc->next = cur;
    if (prev)
        prev->next = desc;
    else
        seg->free_head = desc;
    if (cur)
        cur->prev = desc;
    return CCTRUE;
}

static void free_list_remove(CC_UserSegmentHeader *seg, FreeListNode *desc)
{
    if (desc->prev)
        desc->prev->next = desc->next;
    else
        seg->free_head = desc->next;
    if (desc->next)
        desc->next->prev = desc->prev;
    freeblock_release(desc);
}

// 新建用户段；need 为需要的连续用户区字节数
static CCBOOL user_seg_new(CCUINT64 need)
{
    CCBOOL ok = CCFALSE;
    CCUINT64 offset = user_data_offset();
    CCUINT64 want = 0;
    CCUINT64 total = 0;
    void *base = NULL;
    FreeListNode *desc = NULL;
    CC_UserSegmentHeader *seg = NULL;

    if (need > (CCUINT64)-1 - offset)
        return CCFALSE;
    want = need + offset;
    if (want < CC_MEM_USER_SEGMENT_SIZE)
        want = CC_MEM_USER_SEGMENT_SIZE;

    total = round_up_u64(want, g_cc.page_size ? g_cc.page_size : 4096, &ok);
    if (!ok)
        return CCFALSE;

    base = alloc_segment(total);
    if (!base)
        return CCFALSE;

    desc = freeblock_alloc();
    if (!desc)
    {
        free_segment(base, total);
        return CCFALSE;
    }

    seg = (CC_UserSegmentHeader *)base;
    seg->next_seg = NULL;
    seg->free_head = desc;
    seg->total = total;
    seg->usable = total - offset;
    seg->raw_base = base;
    memset(seg->_pad, 0, sizeof(seg->_pad));

    desc->start = (CCUINT8 *)base + offset;
    desc->size = seg->usable;
    desc->prev = NULL;
    desc->next = NULL;
    desc->flags = 0;

    if (g_cc.user_tail)
        g_cc.user_tail->next_seg = seg;
    else
        g_cc.user_head = seg;
    g_cc.user_tail = seg;
    return CCTRUE;
}

// 跨用户段 first-fit；找不到时扩容一次后重试
static CCBOOL user_alloc_slot(CCUINT64 slot, void **out_ptr, CC_UserSegmentHeader **out_seg)
{
    CCBOOL grown = CCFALSE;

    for (;;)
    {
        for (CC_UserSegmentHeader *seg = g_cc.user_head; seg; seg = seg->next_seg)
        {
            for (FreeListNode *desc = seg->free_head; desc; desc = desc->next)
            {
                if (desc->size < slot)
                    continue;

                *out_ptr = desc->start;
                *out_seg = seg;
                if (desc->size - slot >= CC_MEM_MIN_SPLIT)
                {
                    desc->start = (CCUINT8 *)desc->start + slot;
                    desc->size -= slot;
                }
                else
                {
                    free_list_remove(seg, desc);
                }
                return CCTRUE;
            }
        }

        if (grown || !user_seg_new(slot))
            return CCFALSE;
        grown = CCTRUE;
    }
}

/*
 * ============================================================================
 * cc_rbt_core 适配层
 * ============================================================================
 */

static CCINT32 meta_cmpare_key(void *user, void *node, CCUINTPTR key)
{
    CCUINTPTR current;
    CC_UNUSED(user);
    current = (CCUINTPTR)((MetaNode *)node)->ptr;
    if (current == key)
        return 0;
    return (current > key) ? 1 : -1;
}

static CCINT32 meta_cmpare_node(void *user, void *node_1, void *node_2)
{
    CCUINTPTR a;
    CCUINTPTR b;
    CC_UNUSED(user);
    a = (CCUINTPTR)((MetaNode *)node_1)->ptr;
    b = (CCUINTPTR)((MetaNode *)node_2)->ptr;
    if (a == b)
        return 0;
    return (a > b) ? 1 : -1;
}

// 只拷贝业务字段；链接与颜色由内核维护
static CCINT32 meta_copy_node(void *user, void *dst_node, void *src_node)
{
    MetaNode *dst = (MetaNode *)dst_node;
    MetaNode *src = (MetaNode *)src_node;
    CC_UNUSED(user);
    dst->ptr = src->ptr;
    dst->size = src->size;
    dst->comment_size = src->comment_size;
    dst->seg = src->seg;
    return 0;
}

// 中序后继：右子树最左节点
static void *meta_find_instead(void *user, void *node)
{
    MetaNode *cursor = ((MetaNode *)node)->right;
    CC_UNUSED(user);
    if (!cursor)
        return NULL;
    while (cursor->left)
        cursor = cursor->left;
    return cursor;
}

static void *meta_get_node(void *user, void *node, CC_RbtRel relation)
{
    MetaNode *n = (MetaNode *)node;
    CC_UNUSED(user);
    if (!n)
        return NULL;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   return n->left;
    case CC_RBT_REL_RIGHT:  return n->right;
    case CC_RBT_REL_PARENT: return n->parent;
    default:                return NULL;
    }
}

// 只修改 node_1 的对应链接；双向挂接由内核另行完成
static CCUINT32 meta_set_node(void *user, void *node_1, void *node_2, CC_RbtRel relation)
{
    MetaNode *n = (MetaNode *)node_1;
    CC_UNUSED(user);
    if (!n)
        return 1;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   n->left = (MetaNode *)node_2;   return 0;
    case CC_RBT_REL_RIGHT:  n->right = (MetaNode *)node_2;  return 0;
    case CC_RBT_REL_PARENT: n->parent = (MetaNode *)node_2; return 0;
    default:                return 1;
    }
}

static CCBOOL meta_get_color(void *user, void *node)
{
    CC_UNUSED(user);
    if (!node)
        return CCFALSE;
    return (((MetaNode *)node)->flags & CC_META_FLAG_RED) ? CCTRUE : CCFALSE;
}

static CCINT32 meta_set_color(void *user, void *node, CCBOOL is_red)
{
    MetaNode *n = (MetaNode *)node;
    CC_UNUSED(user);
    if (!n)
        return 1;
    if (is_red)
        n->flags |= CC_META_FLAG_RED;
    else
        n->flags &= (CCUINT8)~CC_META_FLAG_RED;
    return 0;
}

// 仅做链接重排；根更新由内核通过 ch_root 通知
static CCINT32 meta_left_rotate(void *user, void *node)
{
    MetaNode *x = (MetaNode *)node;
    MetaNode *y = NULL;
    CC_UNUSED(user);

    if (!x || !x->right)
        return 1;
    y = x->right;
    x->right = y->left;
    if (y->left)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent)
    {
        if (x->parent->left == x)
            x->parent->left = y;
        else
            x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
    return 0;
}

static CCINT32 meta_right_rotate(void *user, void *node)
{
    MetaNode *x = (MetaNode *)node;
    MetaNode *y = NULL;
    CC_UNUSED(user);

    if (!x || !x->left)
        return 1;
    y = x->left;
    x->left = y->right;
    if (y->right)
        y->right->parent = x;
    y->parent = x->parent;
    if (x->parent)
    {
        if (x->parent->left == x)
            x->parent->left = y;
        else
            x->parent->right = y;
    }
    y->right = x;
    x->parent = y;
    return 0;
}

static void meta_ch_root(void *user, void *new_root)
{
    CC_UNUSED(user);
    meta_root_set(new_root);
}

static CC_RbtOps g_meta_ops = {
    NULL,
    meta_cmpare_key,
    meta_cmpare_node,
    meta_copy_node,
    meta_find_instead,
    meta_get_node,
    meta_set_node,
    meta_get_color,
    meta_set_color,
    meta_left_rotate,
    meta_right_rotate,
    meta_ch_root
};

/*
 * ============================================================================
 * 页保护
 * ============================================================================
 */

static void protection_unprotect_all(void)
{
    void *seg = g_cc.meta_head;
    while (seg)
    {
        unprotect_segment(seg, CC_MEM_META_SEGMENT_SIZE);
        seg = seg_header(seg)->next_seg;
    }
    seg = g_cc.flist_head;
    while (seg)
    {
        unprotect_segment(seg, CC_MEM_FREELIST_SEGMENT_SIZE);
        seg = seg_header(seg)->next_seg;
    }
}

// 保护失败时降级：打印一次错误，并尽力解除全部保护
static void protection_disable(const char *reason)
{
    static CCBOOL warned = CCFALSE;

    g_cc.protection_enabled = CCFALSE;
    if (!warned && reason)
    {
        warned = CCTRUE;
        color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "%s", reason);
    }
    protection_unprotect_all();
}

static CCBOOL protect_all(void)
{
    void *seg = g_cc.meta_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        if (!protect_segment(seg, CC_MEM_META_SEGMENT_SIZE))
            return CCFALSE;
        seg = next;
    }
    seg = g_cc.flist_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        if (!protect_segment(seg, CC_MEM_FREELIST_SEGMENT_SIZE))
            return CCFALSE;
        seg = next;
    }
    return CCTRUE;
}

// 进入临界区：解除全部 Meta / FreeList 段保护
static void ccalloc_enter(void)
{
    void *seg = NULL;

    if (!g_cc.protection_enabled)
        return;

    seg = g_cc.meta_head;
    while (seg)
    {
        if (!unprotect_segment(seg, CC_MEM_META_SEGMENT_SIZE))
        {
            protection_disable(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT)
                "[ccalloc] 解除页保护失败，降级为无保护模式\n");
            return;
        }
        seg = seg_header(seg)->next_seg;
    }

    seg = g_cc.flist_head;
    while (seg)
    {
        if (!unprotect_segment(seg, CC_MEM_FREELIST_SEGMENT_SIZE))
        {
            protection_disable(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT)
                "[ccalloc] 解除页保护失败，降级为无保护模式\n");
            return;
        }
        seg = seg_header(seg)->next_seg;
    }
}

// 离开临界区：恢复全部 Meta / FreeList 段保护
static void ccalloc_leave(void)
{
    void *seg = NULL;

    if (!g_cc.protection_enabled)
        return;

    seg = g_cc.meta_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        if (!protect_segment(seg, CC_MEM_META_SEGMENT_SIZE))
        {
            protection_disable(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT)
                "[ccalloc] 恢复页保护失败，降级为无保护模式\n");
            return;
        }
        seg = next;
    }

    seg = g_cc.flist_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        if (!protect_segment(seg, CC_MEM_FREELIST_SEGMENT_SIZE))
        {
            protection_disable(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT)
                "[ccalloc] 恢复页保护失败，降级为无保护模式\n");
            return;
        }
        seg = next;
    }
}

/*
 * ============================================================================
 * 分配 / 释放 / 调整
 * ============================================================================
 */

// 计算 asize 与槽位大小；溢出返回 CCFALSE
static CCBOOL calc_sizes(CCUINT64 size, CCUINT64 comment_size,
                         CCUINT64 *asize, CCUINT64 *slot)
{
    CCBOOL ok = CCFALSE;
    CCUINT64 align = (CCUINT64)CC_MEM_ALIGN_MIN;
    CCUINT64 aligned = round_up_u64(size, align, &ok);
    CCUINT64 total = 0;
    CCUINT64 rounded = 0;

    if (!ok)
        return CCFALSE;
    if (comment_size > (CCUINT64)-1 - aligned)
        return CCFALSE;
    total = aligned + comment_size;
    rounded = round_up_u64(total, align, &ok);
    if (!ok)
        return CCFALSE;

    *asize = aligned;
    *slot = rounded;
    return CCTRUE;
}

// 注释长度（含 NUL）；comment == NULL 时返回 0
static CCBOOL comment_length(const char *comment, CCUINT64 *out)
{
    size_t length;
    if (!comment)
    {
        *out = 0;
        return CCTRUE;
    }
    length = strlen(comment);
    if (length == (size_t)-1)
        return CCFALSE;
    *out = (CCUINT64)length + 1;
    return CCTRUE;
}

static void *alloc_internal(CCUINT64 size, const char *comment)
{
    CCUINT64 csize = 0;
    CCUINT64 asize = 0;
    CCUINT64 slot = 0;
    MetaNode *node = NULL;
    void *ptr = NULL;
    CC_UserSegmentHeader *seg = NULL;

    if (!comment_length(comment, &csize) || !calc_sizes(size, csize, &asize, &slot))
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 分配失败：尺寸计算溢出\n");
        return NULL;
    }

    node = meta_pool_pop();
    if (!node)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 分配失败：Meta 段申请失败\n");
        return NULL;
    }

    if (!user_alloc_slot(slot, &ptr, &seg))
    {
        meta_pool_push(node);
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 分配失败：用户段申请失败\n");
        return NULL;
    }

    node->ptr = ptr;
    node->size = asize;
    node->comment_size = csize;
    node->seg = seg;
    if (csize)
        memcpy((CCUINT8 *)ptr + asize, comment, (size_t)csize);

    if (cc_rbt_core_insert(&g_meta_ops, meta_root(), node) != 0)
    {
        if (!free_list_insert(seg, ptr, slot))
            color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
                "[ccalloc] 警告：回滚时空闲描述符耗尽\n");
        meta_pool_push(node);
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 内部错误：元数据键冲突，分配已回滚\n");
        return NULL;
    }

    return ptr;
}

// 释放一个已定位的 Meta 块（含树节点回收与用户区归还）
static void release_meta_block(MetaNode *meta)
{
    void *ptr = meta->ptr;
    CCUINT64 asize = meta->size;
    CCUINT64 csize = meta->comment_size;
    CC_UserSegmentHeader *seg = meta->seg;
    CCUINT64 aligned = 0;
    CCUINT64 slot = 0;
    MetaNode *removed = NULL;

    if (!calc_sizes(asize, csize, &aligned, &slot))
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 内部错误：释放尺寸异常\n");
        return;
    }
    CC_UNUSED(aligned);

    // 双子树删除时内核返回中序后继节点，必须回收该节点
    removed = (MetaNode *)cc_rbt_core_remove(&g_meta_ops, meta_root(), (CCUINTPTR)ptr);
    if (!removed)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 内部错误：元数据删除失败\n");
        return;
    }
    meta_pool_push(removed);

    if (!free_list_insert(seg, ptr, slot))
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 警告：空闲描述符耗尽，区域暂时无法回收\n");
}

static void free_by_ptr(void *ptr)
{
    MetaNode *meta = (MetaNode *)cc_rbt_core_get(&g_meta_ops, meta_root(), (CCUINTPTR)ptr);

    if (!meta)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 非法释放：%p 未命中分配记录\n", ptr);
        return;
    }

    release_meta_block(meta);
    trim_segments();
}

static void *realloc_internal(void *old_ptr, CCUINT64 size, const char *comment)
{
    MetaNode *meta = (MetaNode *)cc_rbt_core_get(&g_meta_ops, meta_root(), (CCUINTPTR)old_ptr);
    CCUINT64 old_asize = 0;
    CCUINT64 old_csize = 0;
    CC_UserSegmentHeader *old_seg = NULL;
    const char *effective_comment = NULL;
    CCUINT64 new_csize = 0;
    CCUINT64 old_aligned = 0;
    CCUINT64 old_slot = 0;
    CCUINT64 new_asize = 0;
    CCUINT64 new_slot = 0;
    void *new_ptr = NULL;
    CCUINT64 copy_size = 0;

    if (!meta)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 非法调整：%p 未命中分配记录\n", old_ptr);
        return NULL;
    }

    old_asize = meta->size;
    old_csize = meta->comment_size;
    old_seg = meta->seg;

    if (comment)
    {
        if (!comment_length(comment, &new_csize))
            return NULL;
        effective_comment = comment;
    }
    else if (old_csize)
    {
        new_csize = old_csize;
        effective_comment = (const char *)((CCUINT8 *)old_ptr + old_asize);
    }

    if (!calc_sizes(old_asize, old_csize, &old_aligned, &old_slot) ||
        !calc_sizes(size, new_csize, &new_asize, &new_slot))
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 调整失败：尺寸计算溢出\n");
        return NULL;
    }
    CC_UNUSED(old_aligned);

    // 原地调整：用户大小不增，且注释也在原槽位内
    if (new_asize <= old_asize && new_slot <= old_slot)
    {
        meta->size = new_asize;
        meta->comment_size = new_csize;

        if (comment)
        {
            memmove((CCUINT8 *)old_ptr + new_asize, comment, (size_t)new_csize);
        }
        else if (old_csize && new_asize != old_asize)
        {
            memmove((CCUINT8 *)old_ptr + new_asize,
                    (CCUINT8 *)old_ptr + old_asize,
                    (size_t)old_csize);
        }

        if (new_slot < old_slot)
        {
            if (!free_list_insert(old_seg, (CCUINT8 *)old_ptr + new_slot,
                                  old_slot - new_slot))
                color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
                    "[ccalloc] 警告：收缩尾部归还失败（描述符耗尽）\n");
        }
        return old_ptr;
    }

    // 搬家扩容：新块分配成功后才释放旧块
    new_ptr = alloc_internal(size, effective_comment);
    if (!new_ptr)
        return NULL;

    copy_size = (old_asize < new_asize) ? old_asize : new_asize;
    if (copy_size)
        memmove(new_ptr, old_ptr, (size_t)copy_size);

    release_meta_block(meta);
    trim_segments();
    return new_ptr;
}

static void trim_segments(void)
{
#if CC_MEM_TRIM_SEGMENTS
    CCUINT64 offset = user_data_offset();
    CC_UserSegmentHeader *prev = NULL;
    CC_UserSegmentHeader *seg = g_cc.user_head;

    while (seg)
    {
        CC_UserSegmentHeader *next = seg->next_seg;
        FreeListNode *desc = seg->free_head;
        CCBOOL empty = (desc && !desc->next &&
                        (void *)((CCUINT8 *)seg + offset) == desc->start &&
                        desc->size == seg->usable) ? CCTRUE : CCFALSE;

        // 始终保留至少一个用户段
        if (empty && (prev || next))
        {
            seg->free_head = NULL;
            freeblock_release(desc);
            if (prev)
                prev->next_seg = next;
            else
                g_cc.user_head = next;
            if (g_cc.user_tail == seg)
                g_cc.user_tail = prev;
            free_segment(seg->raw_base, seg->total);
        }
        else
        {
            prev = seg;
        }
        seg = next;
    }
#endif
}

static CCINT32 leak_cbk(void *data, void *user)
{
    MetaNode *node = (MetaNode *)data;
    CCUINT32 *count = (CCUINT32 *)user;
    const char *comment = NULL;

    if (node->comment_size)
        comment = (const char *)((CCUINT8 *)node->ptr + node->size);

    if (comment)
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] leak: ptr=%p size=%lu comment=\"%s\"\n",
            node->ptr, (CCUINT64)node->size, comment);
    }
    else
    {
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] leak: ptr=%p size=%lu comment=(无)\n",
            node->ptr, (CCUINT64)node->size);
    }

    (*count)++;
    return 0;
}

/*
 * ============================================================================
 * 初始化 / 清理
 * ============================================================================
 */

void init_ccalloc(void)
{
    CCBOOL ok = CCFALSE;
    CCUINT64 user_total = 0;
    CCUINT64 user_offset = 0;
    CCUINT64 meta_offset = sizeof(CC_SegHeader) + sizeof(CC_MetaAnchor);
    CCUINT64 meta_count = 0;
    CCUINT64 flist_offset = sizeof(CC_SegHeader) + sizeof(CC_FreeAnchor);
    CCUINT64 flist_count = 0;
    void *meta = NULL;
    void *flist = NULL;
    void *user = NULL;
    CC_UserSegmentHeader *user_seg = NULL;
    FreeListNode *desc = NULL;

    ccalloc_lock();
    if (g_cc.initialized)
    {
        ccalloc_unlock();
        return;
    }

    g_cc.page_size = detect_page_size();
    if (!g_cc.page_size)
        g_cc.page_size = 4096;

    user_offset = user_data_offset();
    user_total = round_up_u64(CC_MEM_USER_SEGMENT_SIZE, g_cc.page_size, &ok);
    if (!ok || user_total <= user_offset)
    {
        ccalloc_unlock();
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 初始化失败：用户段尺寸非法\n");
        return;
    }

    meta = alloc_segment(CC_MEM_META_SEGMENT_SIZE);
    if (!meta)
    {
        ccalloc_unlock();
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 初始化失败：Meta 段申请失败\n");
        return;
    }

    flist = alloc_segment(CC_MEM_FREELIST_SEGMENT_SIZE);
    if (!flist)
    {
        free_segment(meta, CC_MEM_META_SEGMENT_SIZE);
        ccalloc_unlock();
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 初始化失败：FreeList 段申请失败\n");
        return;
    }

    user = alloc_segment(user_total);
    if (!user)
    {
        free_segment(flist, CC_MEM_FREELIST_SEGMENT_SIZE);
        free_segment(meta, CC_MEM_META_SEGMENT_SIZE);
        ccalloc_unlock();
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 初始化失败：用户段申请失败\n");
        return;
    }

    g_cc.meta_head = g_cc.meta_cur = meta;
    g_cc.flist_head = g_cc.flist_cur = flist;

    // 首个 Meta 段：[段头][锚块][节点区]
    seg_header(meta)->next_seg = NULL;
    seg_header(meta)->raw_base = meta;
    meta_anchor()->root = NULL;
    meta_anchor()->pool_head = NULL;
    memset(meta_anchor()->_pad, 0, sizeof(meta_anchor()->_pad));

    meta_count = (CC_MEM_META_SEGMENT_SIZE > meta_offset)
        ? (CC_MEM_META_SEGMENT_SIZE - meta_offset) / sizeof(MetaNode) : 0;
    if (!meta_count)
        goto failed;

    for (CCUINT64 i = 0; i < meta_count; ++i)
        meta_pool_push((MetaNode *)((CCUINT8 *)meta + meta_offset + i * sizeof(MetaNode)));

    // 首个 FreeList 段：[段头][锚块][描述符区]
    seg_header(flist)->next_seg = NULL;
    seg_header(flist)->raw_base = flist;
    free_anchor()->pool_head = NULL;
    memset(free_anchor()->_pad, 0, sizeof(free_anchor()->_pad));

    flist_count = (CC_MEM_FREELIST_SEGMENT_SIZE > flist_offset)
        ? (CC_MEM_FREELIST_SEGMENT_SIZE - flist_offset) / sizeof(FreeListNode) : 0;
    if (!flist_count)
        goto failed;

    for (CCUINT64 i = 0; i < flist_count; ++i)
        freeblock_release((FreeListNode *)((CCUINT8 *)flist + flist_offset + i * sizeof(FreeListNode)));

    // 初始用户段：[段头][用户区]
    user_seg = (CC_UserSegmentHeader *)user;
    user_seg->next_seg = NULL;
    user_seg->free_head = NULL;
    user_seg->total = user_total;
    user_seg->usable = user_total - user_offset;
    user_seg->raw_base = user;
    memset(user_seg->_pad, 0, sizeof(user_seg->_pad));

    desc = freeblock_alloc();
    if (!desc)
        goto failed;

    desc->start = (CCUINT8 *)user + user_offset;
    desc->size = user_seg->usable;
    desc->prev = NULL;
    desc->next = NULL;
    desc->flags = 0;
    user_seg->free_head = desc;
    g_cc.user_head = g_cc.user_tail = user_seg;

    g_cc.initialized = CCTRUE;
    if (protect_all())
    {
        g_cc.protection_enabled = CCTRUE;
    }
    else
    {
        protection_disable(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT)
            "[ccalloc] 启用页保护失败，降级为无保护模式\n");
    }

    ccalloc_unlock();
    return;

failed:
    if (user)
        free_segment(user, user_total);
    if (flist)
        free_segment(flist, CC_MEM_FREELIST_SEGMENT_SIZE);
    if (meta)
        free_segment(meta, CC_MEM_META_SEGMENT_SIZE);
    g_cc.meta_head = g_cc.meta_cur = NULL;
    g_cc.flist_head = g_cc.flist_cur = NULL;
    g_cc.user_head = g_cc.user_tail = NULL;
    g_cc.initialized = CCFALSE;
    g_cc.protection_enabled = CCFALSE;
    ccalloc_unlock();
    color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
        "[ccalloc] 初始化失败：段布局过小\n");
}

CCUINT32 deinit_ccalloc(void)
{
    CCUINT32 leaks = 0;
    CC_UserSegmentHeader *user_seg = NULL;
    void *seg = NULL;

    ccalloc_lock();
    if (!g_cc.initialized)
    {
        ccalloc_unlock();
        return 0;
    }

    protection_unprotect_all();
    g_cc.protection_enabled = CCFALSE;

    // 泄漏报告：持锁、已解除保护，color_print 不重入分配器
    cc_rbt_core_inorder_tranverse(&g_meta_ops, meta_root(), &leaks, leak_cbk);

    user_seg = g_cc.user_head;
    while (user_seg)
    {
        CC_UserSegmentHeader *next = user_seg->next_seg;
        free_segment(user_seg->raw_base, user_seg->total);
        user_seg = next;
    }

    seg = g_cc.meta_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        free_segment(seg_header(seg)->raw_base, CC_MEM_META_SEGMENT_SIZE);
        seg = next;
    }

    seg = g_cc.flist_head;
    while (seg)
    {
        void *next = seg_header(seg)->next_seg;
        free_segment(seg_header(seg)->raw_base, CC_MEM_FREELIST_SEGMENT_SIZE);
        seg = next;
    }

    g_cc.initialized = CCFALSE;
    g_cc.protection_enabled = CCFALSE;
    g_cc.meta_head = g_cc.meta_cur = NULL;
    g_cc.flist_head = g_cc.flist_cur = NULL;
    g_cc.user_head = g_cc.user_tail = NULL;

    ccalloc_unlock();
    return leaks;
}

/*
 * ============================================================================
 * 公开 API
 * ============================================================================
 */

void *ccalloc(void *old, CCUINT64 size, const char *comment)
{
    void *result = NULL;

    // (NULL, 0) 空操作
    if (!old && size == 0)
        return NULL;

    ccalloc_lock();
    if (!g_cc.initialized)
    {
        ccalloc_unlock();
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 分配器未初始化，请求被拒绝\n");
        return NULL;
    }

    ccalloc_enter();

    if (!old)
        result = alloc_internal(size, comment);
    else if (size == 0)
        free_by_ptr(old);
    else
        result = realloc_internal(old, size, comment);

    ccalloc_leave();
    ccalloc_unlock();
    return result;
}

const char *get_comment(void *ptr)
{
    const char *result = NULL;
    MetaNode *node = NULL;

    if (!ptr)
        return NULL;

    ccalloc_lock();
    if (!g_cc.initialized)
    {
        ccalloc_unlock();
        return NULL;
    }

    ccalloc_enter();
    node = (MetaNode *)cc_rbt_core_get(&g_meta_ops, meta_root(), (CCUINTPTR)ptr);
    if (node && node->comment_size)
        result = (const char *)((CCUINT8 *)node->ptr + node->size);
    ccalloc_leave();
    ccalloc_unlock();

    return result;
}

/*
 * ============================================================================
 * 测试接口
 * ============================================================================
 */

CCBOOL ccalloc_test_check(CC_MemTestFn fn)
{
    CCBOOL result = CCFALSE;
    CC_MemTestCtx ctx;

    if (!fn)
        return CCFALSE;

    ccalloc_lock();
    if (!g_cc.initialized)
    {
        ccalloc_unlock();
        return CCFALSE;
    }

    ccalloc_enter();
    ctx.meta_head = g_cc.meta_head;
    ctx.flist_head = g_cc.flist_head;
    ctx.user_head = g_cc.user_head;
    result = fn(&ctx);
    ccalloc_leave();
    ccalloc_unlock();
    return result;
}
