/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-10
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: ccalloc 内部数据结构定义。
 *               仅供 src/ccalloc.c 与 tests/ 使用，不对外公开。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _SRC_CCALLOC_INTERNAL_H_
#define _SRC_CCALLOC_INTERNAL_H_

#include <crystal_cluster.h>
#include <ccalloc.h>

/*
 * ============================================================================
 * 标志位
 * ============================================================================
 */

// MetaNode.flags
#define CC_META_FLAG_RED   (1u << 0)  // RB-tree 颜色：1 = 红
#define CC_META_FLAG_FREE  (1u << 1)  // 节点位于空闲池

// FreeListNode.flags
#define CC_FREE_FLAG_SLOT_FREE (1u << 0)  // 描述符槽位空闲（在全局池中）

/*
 * ============================================================================
 * 数据结构
 * ============================================================================
 */

struct CC_UserSegmentHeader;

// RB-tree 节点 + Meta 空闲池节点
typedef struct MetaNode {
    void             *ptr;           // 用户指针（RB-tree 键）
    CCUINT64          size;          // 实际用户字节数 = round_up(请求, 对齐)
    CCUINT64          comment_size;  // 注释字节数（含 NUL），0 = 无注释
    struct MetaNode  *left;          // 左子
    struct MetaNode  *right;         // 右子；空闲池中复用为 pool_next
    struct MetaNode  *parent;        // 父
    struct CC_UserSegmentHeader *seg;// 所属用户段（O(1) 归还）
    CCUINT8           flags;         // bit0: RB 颜色；bit1: 空闲池标记
    CCUINT8           _pad[7];
} MetaNode;

// 空闲区域描述符 + FreeList 槽位池节点
typedef struct FreeListNode {
    void                *start;  // 空闲区域起始地址
    CCUINT64             size;   // 空闲区域大小（对齐量倍数）
    struct FreeListNode *prev;   // 链表前驱（槽位池中不使用）
    struct FreeListNode *next;   // 链表后继；槽位池中复用为 pool_next
    CCUINT8              flags;  // bit0: 槽位空闲（可复用）
    CCUINT8              _pad[15];
} FreeListNode;

// Meta / FreeList 段共用的统一段头，位于段基址 base[0]
typedef struct CC_SegHeader {
    void *next_seg;  // 下一个同体系段（段基址）
    void *raw_base;  // 后端原始指针（释放用）
} CC_SegHeader;

// 首个 Meta 段的锚块，位于 base[1]
typedef struct CC_MetaAnchor {
    MetaNode *root;      // RB-tree 根节点
    MetaNode *pool_head; // Meta 空闲链头节点
    CCUINT8   _pad[48];
} CC_MetaAnchor;

// 首个 FreeList 段的锚块，位于 base[1]
typedef struct CC_FreeAnchor {
    FreeListNode *pool_head; // FreeList 空闲链头节点
    CCUINT8       _pad[40];
} CC_FreeAnchor;

// 用户段头，位于用户段首，固定 64 字节（一个对齐单位）
typedef struct CC_UserSegmentHeader {
    struct CC_UserSegmentHeader *next_seg;  // 下一个用户段
    FreeListNode                *free_head; // 本段空闲链表头
    CCUINT64                     total;     // 段总大小
    CCUINT64                     usable;    // 可用大小 = total - 64
    void                        *raw_base;  // 后端原始指针
    CCUINT8                      _pad[24];
} CC_UserSegmentHeader;

/*
 * ============================================================================
 * 尺寸静态断言
 * ============================================================================
 */

typedef char CC_STATIC_ASSERT_MetaNode_64[(sizeof(MetaNode) == 64) ? 1 : -1];
typedef char CC_STATIC_ASSERT_FreeListNode_48[(sizeof(FreeListNode) == 48) ? 1 : -1];
typedef char CC_STATIC_ASSERT_SegHeader_16[(sizeof(CC_SegHeader) == 16) ? 1 : -1];
typedef char CC_STATIC_ASSERT_MetaAnchor_64[(sizeof(CC_MetaAnchor) == 64) ? 1 : -1];
typedef char CC_STATIC_ASSERT_FreeAnchor_48[(sizeof(CC_FreeAnchor) == 48) ? 1 : -1];
typedef char CC_STATIC_ASSERT_UserSegHeader_64[(sizeof(CC_UserSegmentHeader) == 64) ? 1 : -1];

/*
 * ============================================================================
 * 测试接口
 * ============================================================================
 */

typedef struct CC_MemTestCtx {
    void *meta_head;   // 首个 Meta 段段头
    void *flist_head;  // 首个 FreeList 段段头
    void *user_head;   // 用户段链表头
} CC_MemTestCtx;

typedef CCBOOL (*CC_MemTestFn)(const CC_MemTestCtx *ctx);

// 持锁并解除保护后调用 fn；返回 fn 的结果
CCBOOL ccalloc_test_check(CC_MemTestFn fn);

#endif
