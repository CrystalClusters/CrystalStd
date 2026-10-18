/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-10
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-10
 * @Description: ccalloc 内存分配器专项测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"
#include "../src/crystal_std_inner_header.h"
#include "../src/ccalloc_internal.h"

#include <stdlib.h>
#include <string.h>

static CCINT32 passed = 0;
static CCINT32 failed = 0;
static CCUINT32 case_code = 1;

CCINT32 test_ccalloc_failures(void) { return failed; }

static const char *g_check_error = NULL;

static void cc_expect(CCBOOL condition, const char *name)
{
    if (condition)
    {
        passed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%u]"
            CC_TEXT_COLOR(CC_GREEN, CC_DEFAULT) "[PASS] "
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s\n",
            case_code++, name
        );
    }
    else
    {
        failed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%u]"
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "[FAIL]"
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) " %s\n",
            case_code++, name
        );
    }
}

static void cc_expect_close(CCBOOL condition, const char *name)
{
    if (!condition && g_check_error)
    {
        failed++;
        color_print(
            CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT) "[case%u]"
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "[FAIL]"
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) " %s（%s）\n",
            case_code++, name, g_check_error
        );
        return;
    }
    cc_expect(condition, name);
}

static void cc_reset(void)
{
    deinit_ccalloc();
    init_ccalloc();
}

/*
 * ============================================================================
 * 测试侧结构遍历工具
 * ============================================================================
 */

static CCUINT64 test_user_offset(void)
{
    CCUINT64 align = (CCUINT64)CC_MEM_ALIGN_MIN;
    CCUINT64 size = (CCUINT64)sizeof(CC_UserSegmentHeader);
    return ((size + align - 1) / align) * align;
}

static CC_MetaAnchor *test_meta_anchor(const CC_MemTestCtx *ctx)
{
    return (CC_MetaAnchor *)((CCUINT8 *)ctx->meta_head + sizeof(CC_SegHeader));
}

static CC_FreeAnchor *test_free_anchor(const CC_MemTestCtx *ctx)
{
    return (CC_FreeAnchor *)((CCUINT8 *)ctx->flist_head + sizeof(CC_SegHeader));
}

static MetaNode *test_tree_root(const CC_MemTestCtx *ctx)
{
    return test_meta_anchor(ctx)->root;
}

static MetaNode *test_find_meta(const CC_MemTestCtx *ctx, const void *ptr)
{
    MetaNode *node = test_tree_root(ctx);
    CCUINTPTR key = (CCUINTPTR)ptr;

    while (node)
    {
        CCUINTPTR current = (CCUINTPTR)node->ptr;
        if (current == key)
            return node;
        node = (key < current) ? node->left : node->right;
    }
    return NULL;
}

static CCUINT64 test_meta_segment_count(const CC_MemTestCtx *ctx)
{
    CCUINT64 count = 0;
    CC_SegHeader *seg = (CC_SegHeader *)ctx->meta_head;
    while (seg)
    {
        count++;
        seg = (CC_SegHeader *)seg->next_seg;
    }
    return count;
}

static CCUINT64 test_flist_segment_count(const CC_MemTestCtx *ctx)
{
    CCUINT64 count = 0;
    CC_SegHeader *seg = (CC_SegHeader *)ctx->flist_head;
    while (seg)
    {
        count++;
        seg = (CC_SegHeader *)seg->next_seg;
    }
    return count;
}

static CCUINT64 test_user_segment_count(const CC_MemTestCtx *ctx)
{
    CCUINT64 count = 0;
    CC_UserSegmentHeader *seg = (CC_UserSegmentHeader *)ctx->user_head;
    while (seg)
    {
        count++;
        seg = seg->next_seg;
    }
    return count;
}

static CCUINT64 test_meta_total_slots(const CC_MemTestCtx *ctx)
{
    CCUINT64 total = 0;
    CCBOOL first = CCTRUE;
    CC_SegHeader *seg = (CC_SegHeader *)ctx->meta_head;

    while (seg)
    {
        CCUINT64 offset = first
            ? (CCUINT64)(sizeof(CC_SegHeader) + sizeof(CC_MetaAnchor))
            : (CCUINT64)sizeof(CC_SegHeader);
        if ((CCUINT64)CC_MEM_META_SEGMENT_SIZE > offset)
            total += ((CCUINT64)CC_MEM_META_SEGMENT_SIZE - offset) / sizeof(MetaNode);
        first = CCFALSE;
        seg = (CC_SegHeader *)seg->next_seg;
    }
    return total;
}

static CCUINT64 test_flist_total_slots(const CC_MemTestCtx *ctx)
{
    CCUINT64 total = 0;
    CCBOOL first = CCTRUE;
    CC_SegHeader *seg = (CC_SegHeader *)ctx->flist_head;

    while (seg)
    {
        CCUINT64 offset = first
            ? (CCUINT64)(sizeof(CC_SegHeader) + sizeof(CC_FreeAnchor))
            : (CCUINT64)sizeof(CC_SegHeader);
        if ((CCUINT64)CC_MEM_FREELIST_SEGMENT_SIZE > offset)
            total += ((CCUINT64)CC_MEM_FREELIST_SEGMENT_SIZE - offset) / sizeof(FreeListNode);
        first = CCFALSE;
        seg = (CC_SegHeader *)seg->next_seg;
    }
    return total;
}

/*
 * ============================================================================
 * 不变式校验
 * ============================================================================
 */

#define CC_CHECK_FAIL(msg) do { if (!g_check_error) g_check_error = (msg); } while (0)

static CCBOOL test_seg_belongs(const CC_MemTestCtx *ctx, CC_UserSegmentHeader *target)
{
    for (CC_UserSegmentHeader *seg = (CC_UserSegmentHeader *)ctx->user_head; seg; seg = seg->next_seg)
    {
        if (seg == target)
            return CCTRUE;
    }
    return CCFALSE;
}

static CCINT32 test_check_rb(const CC_MemTestCtx *ctx, MetaNode *node, MetaNode *parent,
                             CCUINTPTR min, CCUINTPTR max, CCUINT64 *count)
{
    CCUINTPTR key;
    CCUINTPTR start;
    CCUINTPTR end;
    CCUINT64 offset;
    CCINT32 left_height;
    CCINT32 right_height;

    if (!node)
        return 1;

    key = (CCUINTPTR)node->ptr;
    if (key <= min || key >= max)
    {
        CC_CHECK_FAIL("BST 序错误");
        return -1;
    }
    if (node->parent != parent)
    {
        CC_CHECK_FAIL("父链接错误");
        return -1;
    }
    if (node->flags & CC_META_FLAG_FREE)
    {
        CC_CHECK_FAIL("树内节点带有空闲标记");
        return -1;
    }
    if (!node->seg || !test_seg_belongs(ctx, node->seg))
    {
        CC_CHECK_FAIL("MetaNode.seg 指向非法用户段");
        return -1;
    }
    if (CC_MEM_ALIGN_MIN && (node->size % (CCUINT64)CC_MEM_ALIGN_MIN) != 0)
    {
        CC_CHECK_FAIL("MetaNode.size 未对齐");
        return -1;
    }

    offset = test_user_offset();
    start = (CCUINTPTR)node->ptr;
    end = start + (CCUINTPTR)node->size + (CCUINTPTR)node->comment_size;
    if (end < start ||
        start < (CCUINTPTR)node->seg + offset ||
        end > (CCUINTPTR)node->seg + node->seg->total)
    {
        CC_CHECK_FAIL("用户块越出所属用户段");
        return -1;
    }
    if (node->comment_size &&
        ((const char *)node->ptr)[node->size + node->comment_size - 1] != '\0')
    {
        CC_CHECK_FAIL("注释缺少 NUL 结尾");
        return -1;
    }

    (*count)++;
    left_height = test_check_rb(ctx, node->left, node, min, key, count);
    if (left_height < 0)
        return -1;
    right_height = test_check_rb(ctx, node->right, node, key, max, count);
    if (right_height < 0)
        return -1;
    if (left_height != right_height)
    {
        CC_CHECK_FAIL("黑高不一致");
        return -1;
    }

    if (node->flags & CC_META_FLAG_RED)
    {
        if (parent && (parent->flags & CC_META_FLAG_RED))
        {
            CC_CHECK_FAIL("出现连续红节点");
            return -1;
        }
        if ((node->left && (node->left->flags & CC_META_FLAG_RED)) ||
            (node->right && (node->right->flags & CC_META_FLAG_RED)))
        {
            CC_CHECK_FAIL("红节点的子节点为红");
            return -1;
        }
        return left_height;
    }
    return left_height + 1;
}

static CCUINT64 test_check_meta_pool(const CC_MemTestCtx *ctx, CCUINT64 limit)
{
    MetaNode *node = test_meta_anchor(ctx)->pool_head;
    CCUINT64 count = 0;

    while (node)
    {
        if (!(node->flags & CC_META_FLAG_FREE))
        {
            CC_CHECK_FAIL("Meta 池节点缺少空闲标记");
            return (CCUINT64)-1;
        }
        if (node->left || node->parent)
        {
            CC_CHECK_FAIL("Meta 池节点保留树链接");
            return (CCUINT64)-1;
        }
        count++;
        if (count > limit)
        {
            CC_CHECK_FAIL("Meta 池成环");
            return (CCUINT64)-1;
        }
        node = node->right;
    }
    return count;
}

static CCUINT64 test_check_flist_pool(const CC_MemTestCtx *ctx, CCUINT64 limit)
{
    FreeListNode *desc = test_free_anchor(ctx)->pool_head;
    CCUINT64 count = 0;

    while (desc)
    {
        if (!(desc->flags & CC_FREE_FLAG_SLOT_FREE))
        {
            CC_CHECK_FAIL("FreeList 池槽位缺少空闲标记");
            return (CCUINT64)-1;
        }
        if (desc->prev || desc->start || desc->size)
        {
            CC_CHECK_FAIL("FreeList 池槽位保留链表数据");
            return (CCUINT64)-1;
        }
        count++;
        if (count > limit)
        {
            CC_CHECK_FAIL("FreeList 池成环");
            return (CCUINT64)-1;
        }
        desc = desc->next;
    }
    return count;
}

static CCBOOL test_check_user_lists(const CC_MemTestCtx *ctx, CCUINT64 *in_use)
{
    CCUINT64 offset = test_user_offset();
    CC_UserSegmentHeader *seg = (CC_UserSegmentHeader *)ctx->user_head;

    *in_use = 0;
    while (seg)
    {
        FreeListNode *prev = NULL;
        FreeListNode *desc = seg->free_head;

        if (seg->usable != seg->total - offset)
        {
            CC_CHECK_FAIL("用户段 usable 记账错误");
            return CCFALSE;
        }

        while (desc)
        {
            CCUINT8 *start = (CCUINT8 *)desc->start;
            if (desc->prev != prev)
            {
                CC_CHECK_FAIL("空闲链 prev 链接错误");
                return CCFALSE;
            }
            if (desc->flags & CC_FREE_FLAG_SLOT_FREE)
            {
                CC_CHECK_FAIL("在链描述符带有空闲槽位标记");
                return CCFALSE;
            }
            if (start < (CCUINT8 *)seg + offset ||
                start + desc->size > (CCUINT8 *)seg + seg->total)
            {
                CC_CHECK_FAIL("空闲区越出用户段");
                return CCFALSE;
            }
            // 相邻空闲区必须已合并
            if (prev && (CCUINT8 *)prev->start + prev->size >= start)
            {
                CC_CHECK_FAIL("存在重叠或相邻的空闲区");
                return CCFALSE;
            }
            prev = desc;
            (*in_use)++;
            desc = desc->next;
        }

        seg = seg->next_seg;
    }
    return CCTRUE;
}

static CCBOOL test_check_all_cb(const CC_MemTestCtx *ctx)
{
    CCUINT64 tree_count = 0;
    CCUINT64 meta_total = 0;
    CCUINT64 meta_pool = 0;
    CCUINT64 flist_total = 0;
    CCUINT64 flist_pool = 0;
    CCUINT64 desc_in_use = 0;
    CCINT32 black_height = 0;

    g_check_error = NULL;

    if (!ctx->meta_head || !ctx->flist_head || !ctx->user_head)
    {
        CC_CHECK_FAIL("测试上下文存在空段");
        return CCFALSE;
    }

    black_height = test_check_rb(ctx, test_tree_root(ctx), NULL, 0,
                                 (CCUINTPTR)-1, &tree_count);
    if (black_height < 0)
        return CCFALSE;
    if (test_tree_root(ctx) && (test_tree_root(ctx)->flags & CC_META_FLAG_RED))
    {
        CC_CHECK_FAIL("根节点为红");
        return CCFALSE;
    }

    meta_total = test_meta_total_slots(ctx);
    meta_pool = test_check_meta_pool(ctx, meta_total);
    if (meta_pool == (CCUINT64)-1)
        return CCFALSE;
    if (tree_count + meta_pool != meta_total)
    {
        CC_CHECK_FAIL("Meta 节点守恒失败");
        return CCFALSE;
    }

    if (!test_check_user_lists(ctx, &desc_in_use))
        return CCFALSE;

    flist_total = test_flist_total_slots(ctx);
    flist_pool = test_check_flist_pool(ctx, flist_total);
    if (flist_pool == (CCUINT64)-1)
        return CCFALSE;
    if (desc_in_use + flist_pool != flist_total)
    {
        CC_CHECK_FAIL("FreeList 描述符守恒失败");
        return CCFALSE;
    }

    return CCTRUE;
}

static void cc_expect_invariants(const char *name)
{
    CCBOOL ok = ccalloc_test_check(test_check_all_cb);
    cc_expect_close(ok, name);
}

/*
 * ============================================================================
 * 段计数回调
 * ============================================================================
 */

static CCUINT64 g_meta_segments = 0;
static CCUINT64 g_flist_segments = 0;
static CCUINT64 g_user_segments = 0;

static CCBOOL test_count_cb(const CC_MemTestCtx *ctx)
{
    g_meta_segments = test_meta_segment_count(ctx);
    g_flist_segments = test_flist_segment_count(ctx);
    g_user_segments = test_user_segment_count(ctx);
    return CCTRUE;
}

/*
 * ============================================================================
 * 1. 基础分配 / 2. 对齐 / 3. 注释
 * ============================================================================
 */

static void test_basic_alloc(void)
{
    unsigned char *p = NULL;

    cc_reset();

    p = (unsigned char *)ccalloc(NULL, 1, "one");
    cc_expect(p != NULL, "基础分配返回非空");
    cc_expect(get_comment(p) != NULL && strcmp(get_comment(p), "one") == 0,
              "基础注释可查询");

    p[0] = 0xA5;
    cc_expect(p[0] == 0xA5, "用户区可读写");

    cc_expect(ccalloc(NULL, 0, NULL) == NULL, "(NULL, 0) 为空操作");
    cc_expect(ccalloc(NULL, (CCUINT64)-1, NULL) == NULL, "超大尺寸分配被拒绝");
    cc_expect(ccalloc(p, 0, NULL) == NULL, "释放模式返回 NULL");
    cc_expect(get_comment(p) == NULL, "释放后注释查询返回 NULL");

    cc_expect_invariants("基础用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "基础用例无泄漏");

    cc_expect(ccalloc(NULL, 64, NULL) == NULL, "未初始化时分配被拒绝");
    cc_expect(get_comment((void *)(CCUINTPTR)1) == NULL,
              "未初始化时注释查询返回 NULL");
    init_ccalloc();
}

static void *g_align_ptrs[6] = { NULL };
static const CCUINT64 g_align_expected[6] = { 64, 64, 64, 128, 128, 4096 };

static CCBOOL test_alignment_cb(const CC_MemTestCtx *ctx)
{
    g_check_error = NULL;
    for (CCINT32 i = 0; i < 6; ++i)
    {
        MetaNode *node = test_find_meta(ctx, g_align_ptrs[i]);
        if (!node)
        {
            CC_CHECK_FAIL("对齐用例的 MetaNode 缺失");
            return CCFALSE;
        }
        if (node->size != g_align_expected[i])
        {
            CC_CHECK_FAIL("MetaNode.size 取整结果错误");
            return CCFALSE;
        }
    }
    return CCTRUE;
}

static void test_alignment(void)
{
    static const CCUINT64 sizes[6] = { 1, 63, 64, 65, 70, 4096 };

    cc_reset();
    for (CCINT32 i = 0; i < 6; ++i)
    {
        g_align_ptrs[i] = ccalloc(NULL, sizes[i], NULL);
        cc_expect(g_align_ptrs[i] != NULL, "对齐用例分配成功");
        cc_expect(((CCUINTPTR)g_align_ptrs[i] % (CCUINTPTR)CC_MEM_ALIGN_MIN) == 0,
                  "返回指针对齐到 CC_MEM_ALIGN_MIN");
    }

    g_check_error = NULL;
    cc_expect_close(ccalloc_test_check(test_alignment_cb),
                    "MetaNode.size 按对齐量向上取整");

    for (CCINT32 i = 0; i < 6; ++i)
        ccalloc(g_align_ptrs[i], 0, NULL);

    cc_expect_invariants("对齐用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "对齐用例无泄漏");
    init_ccalloc();
}

static void test_comment(void)
{
    unsigned char pattern[64];
    char old_comment[201];
    char new_comment[221];
    unsigned char *p = NULL;
    unsigned char *q = NULL;
    unsigned char *r = NULL;
    unsigned char *s = NULL;
    unsigned char *t = NULL;
    unsigned char *u = NULL;
    unsigned char *v = NULL;

    cc_reset();

    p = (unsigned char *)ccalloc(NULL, 128, "alpha");
    cc_expect(p != NULL && get_comment(p) && strcmp(get_comment(p), "alpha") == 0,
              "分配时写入注释");

    q = (unsigned char *)ccalloc(p, 128, "beta");
    cc_expect(q == p, "同尺寸更新注释可原地完成");
    cc_expect(get_comment(q) && strcmp(get_comment(q), "beta") == 0,
              "注释更新为最新值");

    for (CCINT32 i = 0; i < 64; ++i)
    {
        pattern[i] = (unsigned char)i;
    }
    memcpy(q, pattern, sizeof(pattern));

    r = (unsigned char *)ccalloc(q, 64, NULL);
    cc_expect(r == q, "收缩可原地完成");
    cc_expect(get_comment(r) && strcmp(get_comment(r), "beta") == 0,
              "NULL 注释参数保留旧注释");
    cc_expect(memcmp(r, pattern, 64) == 0, "收缩保留用户数据");

    s = (unsigned char *)ccalloc(r, 4096, "gamma");
    cc_expect(s != NULL, "扩容分配成功");
    cc_expect(s != r, "扩容触发搬家");
    cc_expect(memcmp(s, pattern, 64) == 0, "搬家扩容保留用户数据");
    cc_expect(get_comment(s) && strcmp(get_comment(s), "gamma") == 0,
              "搬家扩容写入新注释");
    cc_expect(get_comment(r) == NULL, "搬家后旧指针已失效");

    t = (unsigned char *)ccalloc(s, 4096, "delta");
    cc_expect(t == s, "同尺寸更新注释不搬家");
    cc_expect(get_comment(t) && strcmp(get_comment(t), "delta") == 0,
              "同尺寸更新后的注释正确");

    memset(old_comment, 'A', 200);
    old_comment[200] = '\0';
    memset(new_comment, 'B', 220);
    new_comment[220] = '\0';

    u = (unsigned char *)ccalloc(NULL, 1024, old_comment);
    v = (unsigned char *)ccalloc(u, 1024, new_comment);
    cc_expect(v == u, "注释增长仍在原槽位内完成");
    cc_expect(get_comment(v) && strcmp(get_comment(v), new_comment) == 0,
              "注释增长后的内容正确");
    cc_expect(get_comment((CCUINT8 *)v + 1) == NULL,
              "非法指针查询注释返回 NULL");

    ccalloc(t, 0, NULL);
    ccalloc(v, 0, NULL);

    cc_expect_invariants("注释用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "注释用例无泄漏");
    init_ccalloc();
}

static void cc_read_counts(void)
{
    g_meta_segments = 0;
    g_flist_segments = 0;
    g_user_segments = 0;
    ccalloc_test_check(test_count_cb);
}

/*
 * ============================================================================
 * 4. RB-tree 跨段 / 5. 空闲链表与描述符池
 * ============================================================================
 */

#define CC_TEST_MANY 2600

static void *g_many[CC_TEST_MANY] = { NULL };

static void test_rb_cross_segments(void)
{
    CCBOOL all_ok = CCTRUE;
    CCUINT64 meta_before = 0;
    CCUINT32 seed = 2463534242u;

    cc_reset();

    for (CCINT32 i = 0; i < CC_TEST_MANY; ++i)
    {
        g_many[i] = ccalloc(NULL, 32 + (i % 5) * 16, NULL);
        if (!g_many[i])
            all_ok = CCFALSE;
    }
    cc_expect(all_ok, "跨段用例 2600 块全部分配成功");

    cc_read_counts();
    cc_expect(g_meta_segments >= 3, "分配 2600 块后 Meta 段数 >= 3");
    cc_expect_invariants("跨段分配后不变式成立");

    // 伪随机顺序释放
    for (CCINT32 n = CC_TEST_MANY; n > 0; --n)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        CCINT32 j = (CCINT32)(seed % (CCUINT32)n);
        void *tmp = g_many[n - 1];
        g_many[n - 1] = g_many[j];
        g_many[j] = tmp;
        ccalloc(g_many[n - 1], 0, NULL);
    }

    cc_expect_invariants("随机顺序删除后不变式成立");
    cc_read_counts();
    meta_before = g_meta_segments;

    // 成对 alloc/free 应复用 Meta 节点，不触发扩容
    for (CCINT32 i = 0; i < CC_TEST_MANY; ++i)
        g_many[i] = ccalloc(NULL, 64, NULL);
    for (CCINT32 i = 0; i < CC_TEST_MANY; ++i)
        ccalloc(g_many[i], 0, NULL);

    cc_read_counts();
    cc_expect(g_meta_segments == meta_before, "成对 alloc/free 不增长 Meta 段");
    cc_expect_invariants("成对 alloc/free 后不变式成立");

    cc_expect(deinit_ccalloc() == 0, "跨段用例无泄漏");
    init_ccalloc();
}

#define CC_TEST_FREE_N 200

static void *g_free_ptrs[CC_TEST_FREE_N] = { NULL };

static CCBOOL test_single_free_cb(const CC_MemTestCtx *ctx)
{
    CCUINT64 offset = test_user_offset();
    g_check_error = NULL;

    for (CC_UserSegmentHeader *seg = (CC_UserSegmentHeader *)ctx->user_head; seg; seg = seg->next_seg)
    {
        FreeListNode *desc = seg->free_head;
        if (!desc || desc->next)
        {
            CC_CHECK_FAIL("用户段空闲链不是单个描述符");
            return CCFALSE;
        }
        if ((CCUINT8 *)seg + offset != (CCUINT8 *)desc->start ||
            desc->size != seg->usable)
        {
            CC_CHECK_FAIL("空闲描述符未覆盖整个用户段");
            return CCFALSE;
        }
    }
    return CCTRUE;
}

static void test_free_lists(void)
{
    cc_reset();

    for (CCINT32 i = 0; i < CC_TEST_FREE_N; ++i)
        g_free_ptrs[i] = ccalloc(NULL, 128, NULL);

    // 释放三分之一，制造空洞
    for (CCINT32 i = 0; i < CC_TEST_FREE_N; i += 3)
    {
        ccalloc(g_free_ptrs[i], 0, NULL);
        g_free_ptrs[i] = NULL;
    }

    cc_expect_invariants("部分释放后空闲区合并正确");
    cc_read_counts();
    cc_expect(g_flist_segments == 1, "描述符池未额外扩容");

    // 重新分配应复用空洞
    for (CCINT32 i = 0; i < CC_TEST_FREE_N; i += 3)
    {
        g_free_ptrs[i] = ccalloc(NULL, 128, NULL);
        cc_expect(g_free_ptrs[i] != NULL, "空洞可被重新分配");
    }

    cc_expect_invariants("空洞复用后不变式成立");
    cc_read_counts();
    cc_expect(g_flist_segments == 1, "描述符槽位复用未新增 FreeList 段");

    for (CCINT32 i = 0; i < CC_TEST_FREE_N; ++i)
    {
        if (g_free_ptrs[i])
        {
            ccalloc(g_free_ptrs[i], 0, NULL);
            g_free_ptrs[i] = NULL;
        }
    }

    g_check_error = NULL;
    cc_expect_close(ccalloc_test_check(test_single_free_cb),
                    "全部释放后每段空闲链恰为整段");
    cc_expect_invariants("全部释放后不变式成立");
    cc_read_counts();
    cc_expect(g_user_segments == 1, "空用户段被自动回收");

    cc_expect(deinit_ccalloc() == 0, "空闲链表用例无泄漏");
    init_ccalloc();
}

#define CC_TEST_DESC_N 3000

static void *g_desc_ptrs[CC_TEST_DESC_N] = { NULL };

static void test_flist_expansion(void)
{
    CCBOOL all_ok = CCTRUE;
    CCBOOL reuse_ok = CCTRUE;

    cc_reset();

    for (CCINT32 i = 0; i < CC_TEST_DESC_N; ++i)
    {
        g_desc_ptrs[i] = ccalloc(NULL, 64, NULL);
        if (!g_desc_ptrs[i])
            all_ok = CCFALSE;
    }
    cc_expect(all_ok, "描述符扩容用例分配成功");

    // 每隔一个释放，制造 1500 个互不相邻的空洞，耗尽首个 FreeList 段
    for (CCINT32 i = 0; i < CC_TEST_DESC_N; i += 2)
    {
        ccalloc(g_desc_ptrs[i], 0, NULL);
        g_desc_ptrs[i] = NULL;
    }

    cc_read_counts();
    cc_expect(g_flist_segments >= 2, "描述符耗尽时新增 FreeList 段");
    cc_expect_invariants("FreeList 扩容后不变式成立");

    // 空洞应能复用，且不破坏跨 FreeList 段的描述符池
    for (CCINT32 i = 0; i < CC_TEST_DESC_N; i += 2)
    {
        g_desc_ptrs[i] = ccalloc(NULL, 64, NULL);
        if (!g_desc_ptrs[i])
            reuse_ok = CCFALSE;
    }
    cc_expect(reuse_ok, "扩容后空洞全部可复用");

    for (CCINT32 i = 0; i < CC_TEST_DESC_N; ++i)
    {
        if (g_desc_ptrs[i])
        {
            ccalloc(g_desc_ptrs[i], 0, NULL);
            g_desc_ptrs[i] = NULL;
        }
    }

    cc_expect_invariants("描述符扩容用例释放后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "描述符扩容用例无泄漏");
    init_ccalloc();
}

/*
 * ============================================================================
 * 6. 用户段扩容与大块 / 7. 整段回收 / 8. 调整变体
 * ============================================================================
 */

static void test_large_and_segments(void)
{
    CCUINT64 segments_before = 0;
    unsigned char *p = NULL;
    const CCUINT64 big = ((CCUINT64)2) << 20;

    cc_reset();
    cc_read_counts();
    segments_before = g_user_segments;

    p = (unsigned char *)ccalloc(NULL, big, "big");
    cc_expect(p != NULL, "2 MiB 大块分配成功");
    cc_expect(((CCUINTPTR)p % (CCUINTPTR)CC_MEM_ALIGN_MIN) == 0,
              "大块指针对齐");
    p[0] = 0x11;
    p[big - 1] = 0x22;
    cc_expect(p[0] == 0x11 && p[big - 1] == 0x22, "大块首尾可写");

    cc_read_counts();
    cc_expect(g_user_segments > segments_before, "大块触发用户段扩容");
    cc_expect_invariants("大块分配后不变式成立");

    ccalloc(p, 0, NULL);

#if CC_MEM_TRIM_SEGMENTS
    cc_read_counts();
    cc_expect(g_user_segments == segments_before, "大块释放后空用户段被回收");
#endif

    cc_expect(deinit_ccalloc() == 0, "大块用例无泄漏");
    init_ccalloc();
}

static void test_trim_segments(void)
{
    CCUINT64 segments_before = 0;
    CCUINT64 segments_grown = 0;
    unsigned char *p = NULL;
    const CCUINT64 big = ((CCUINT64)4) << 20;

    cc_reset();
    cc_read_counts();
    segments_before = g_user_segments;

    p = (unsigned char *)ccalloc(NULL, big, NULL);
    cc_read_counts();
    segments_grown = g_user_segments;
    cc_expect(p != NULL && segments_grown > segments_before,
              "扩张用例成功创建新用户段");

    ccalloc(p, 0, NULL);
    cc_read_counts();

#if CC_MEM_TRIM_SEGMENTS
    cc_expect(g_user_segments == segments_before,
              "CC_MEM_TRIM_SEGMENTS=1 时回收 100% 空闲段");
#else
    cc_expect(g_user_segments == segments_grown,
              "CC_MEM_TRIM_SEGMENTS=0 时保留 100% 空闲段");
#endif

    cc_expect_invariants("整段回收用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "整段回收用例无泄漏");
    init_ccalloc();
}

static void test_realloc_variants(void)
{
    unsigned char pattern[1024];
    unsigned char *p = NULL;
    unsigned char *q = NULL;
    unsigned char *r = NULL;
    unsigned char *s = NULL;
    unsigned char *t = NULL;

    cc_reset();

    for (CCINT32 i = 0; i < 1024; ++i)
        pattern[i] = (unsigned char)(i & 0xFF);

    // 原地收缩
    p = (unsigned char *)ccalloc(NULL, 4096, "keep");
    cc_expect(p != NULL, "调整用例初始分配成功");
    memcpy(p, pattern, sizeof(pattern));

    q = (unsigned char *)ccalloc(p, 1024, NULL);
    cc_expect(q == p, "缩小尺寸原地完成");
    cc_expect(memcmp(q, pattern, sizeof(pattern)) == 0, "原地收缩保留数据");
    cc_expect(get_comment(q) && strcmp(get_comment(q), "keep") == 0,
              "原地收缩保留注释");

    // 搬家扩容
    r = (unsigned char *)ccalloc(q, 16384, "grown");
    cc_expect(r != NULL, "搬家扩容成功");
    cc_expect(r != q, "扩容超出原槽位时触发搬家");
    cc_expect(memcmp(r, pattern, sizeof(pattern)) == 0, "搬家扩容保留数据");
    cc_expect(get_comment(r) && strcmp(get_comment(r), "grown") == 0,
              "搬家扩容更新注释");
    cc_expect(get_comment(q) == NULL, "搬家扩容后旧指针失效");

    // 极端收缩：注释需整体搬移到新用户区尾部
    s = (unsigned char *)ccalloc(NULL, 200, "tiny");
    t = (unsigned char *)ccalloc(s, 1, NULL);
    cc_expect(t == s, "极端收缩仍可原地完成");
    cc_expect(get_comment(t) && strcmp(get_comment(t), "tiny") == 0,
              "极端收缩后注释仍可查询");

    ccalloc(r, 0, NULL);
    ccalloc(t, 0, NULL);

    cc_expect_invariants("调整用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "调整用例无泄漏");
    init_ccalloc();
}

/*
 * ============================================================================
 * 9. 释放合法性 / 10. 页保护 / 11. 泄漏检测
 * ============================================================================
 */

static void test_invalid_free(void)
{
    char *p = NULL;
    char *q = NULL;

    cc_reset();

    p = (char *)ccalloc(NULL, 64, "ok");
    cc_expect(p != NULL, "非法释放用例初始分配成功");

    cc_expect(ccalloc((CCUINT8 *)p + 1, 0, NULL) == NULL,
              "free(p + 1) 被拒绝");
    cc_expect(ccalloc((CCUINT8 *)p + 1, 128, NULL) == NULL,
              "非法调整被拒绝");
    cc_expect(ccalloc((void *)(CCUINTPTR)0x1234, 0, NULL) == NULL,
              "随机地址释放被拒绝");
    cc_expect(get_comment(p) && strcmp(get_comment(p), "ok") == 0,
              "非法释放未破坏原分配");

    cc_expect(ccalloc(p, 0, NULL) == NULL, "合法释放返回 NULL");
    cc_expect(ccalloc(p, 0, NULL) == NULL, "double-free 被拒绝");

    q = (char *)ccalloc(NULL, 64, "after");
    cc_expect(q != NULL && get_comment(q) && strcmp(get_comment(q), "after") == 0,
              "非法释放后分配器仍可用");
    ccalloc(q, 0, NULL);

    cc_expect_invariants("非法释放用例后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "非法释放用例无泄漏");
    init_ccalloc();
}

static void test_protection_smoke(void)
{
    void *p = NULL;

    cc_reset();

    p = ccalloc(NULL, 128, "guard");
    cc_expect(p != NULL, "页保护用例初始分配成功");

    g_check_error = NULL;
    cc_expect_close(ccalloc_test_check(test_check_all_cb),
                    "页保护下测试回调可访问并校验元数据");

    cc_expect(get_comment(p) && strcmp(get_comment(p), "guard") == 0,
              "页保护下公开查询正常");

    ccalloc(p, 0, NULL);
    cc_expect(deinit_ccalloc() == 0, "页保护用例无泄漏");
    init_ccalloc();
}

static void test_leak_detection(void)
{
    CCUINT32 leaks = 0;

    cc_reset();

    ccalloc(NULL, 100, "leak-a");
    ccalloc(NULL, 200, "leak-b");
    ccalloc(NULL, 300, NULL);

    leaks = deinit_ccalloc();
    cc_expect(leaks == 3, "deinit 返回的泄漏块数量正确");

    init_ccalloc();
    cc_expect(deinit_ccalloc() == 0, "重新初始化后无泄漏");
    init_ccalloc();
}

/*
 * ============================================================================
 * 随机压力测试
 * ============================================================================
 */

#ifndef CC_TEST_STRESS_OPS
#define CC_TEST_STRESS_OPS 1000000
#endif
#define CC_STRESS_LIVE 512

static void test_random_stress(void)
{
    static void *ptrs[CC_STRESS_LIVE];
    static CCUINT64 sizes[CC_STRESS_LIVE];
    CCINT32 live = 0;
    CCUINT32 seed = 0x9E3779B9u;
    CCBOOL ok = CCTRUE;

    cc_reset();
    memset(ptrs, 0, sizeof(ptrs));
    memset(sizes, 0, sizeof(sizes));

    for (CCINT32 step = 0; step < CC_TEST_STRESS_OPS && ok; ++step)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;

        if (live == 0 || (live < CC_STRESS_LIVE && (seed % 100) < 55))
        {
            CCUINT64 size = 1 + (seed % 8192);
            char comment[64];
            const char *cp = NULL;
            void *p = NULL;

            if ((seed % 3) == 0)
            {
                CCUINT32 length = seed % 63;
                for (CCUINT32 k = 0; k < length; ++k)
                    comment[k] = (char)('a' + (seed + k) % 26);
                comment[length] = '\0';
                cp = comment;
            }

            p = ccalloc(NULL, size, cp);
            if (!p)
            {
                ok = CCFALSE;
                break;
            }
            if (((CCUINTPTR)p % (CCUINTPTR)CC_MEM_ALIGN_MIN) != 0)
            {
                ok = CCFALSE;
                break;
            }

            ((unsigned char *)p)[0] = 0x5A;
            ptrs[live] = p;
            sizes[live] = size;
            live++;
        }
        else
        {
            CCINT32 index = (CCINT32)(seed % (CCUINT32)live);
            if ((seed % 2) == 0)
            {
                ccalloc(ptrs[index], 0, NULL);
                live--;
                ptrs[index] = ptrs[live];
                sizes[index] = sizes[live];
                ptrs[live] = NULL;
                sizes[live] = 0;
            }
            else
            {
                CCUINT64 new_size = 1 + (seed % 16384);
                const char *cp = ((seed % 5) == 0) ? "rw" : NULL;
                void *np = ccalloc(ptrs[index], new_size, cp);

                if (!np)
                {
                    ok = CCFALSE;
                    break;
                }
                // 用户区首字节在任意尺寸下都应保留
                if (((unsigned char *)np)[0] != 0x5A)
                {
                    ok = CCFALSE;
                    break;
                }

                ptrs[index] = np;
                sizes[index] = new_size;
                ((unsigned char *)np)[0] = 0x5A;
            }
        }

        if ((step % 2000) == 0 && !ccalloc_test_check(test_check_all_cb))
        {
            ok = CCFALSE;
            break;
        }
    }

    cc_expect(ok, "随机压力操作无异常");
    if (!ok && g_check_error)
        color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT)
            "[ccalloc] 压力测试中止原因：%s\n", g_check_error);

    for (CCINT32 i = 0; i < live; ++i)
        ccalloc(ptrs[i], 0, NULL);

    cc_expect_invariants("随机压力全部释放后不变式成立");
    cc_expect(deinit_ccalloc() == 0, "随机压力无泄漏");
    init_ccalloc();
}

/*
 * ============================================================================
 * 入口
 * ============================================================================
 */

void test_ccalloc(void)
{
    test_basic_alloc();
    test_alignment();
    test_comment();
    test_rb_cross_segments();
    test_free_lists();
    test_flist_expansion();
    test_large_and_segments();
    test_trim_segments();
    test_realloc_variants();
    test_invalid_free();
    test_protection_smoke();
    test_leak_detection();
    test_random_stress();

    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT)
        "total: %d, passed: %d, failed: %d\n",
        passed + failed, passed, failed
    );
}
