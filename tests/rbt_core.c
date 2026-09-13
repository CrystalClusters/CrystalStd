/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 红黑树内核（cc_rbt_core）的测试。
 *               测试侧扮演「用户」角色：自定义节点结构并实现完整的 CC_RbtOps，
 *               以此验证内核与具体节点结构的解耦性。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"
#include <cc_rbt.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 用户侧：具体节点结构与 CC_RbtOps 实现
 * ============================================================================ */

typedef struct test_rbt_node {
    CCUINTPTR key;
    CCBOOL red;
    struct test_rbt_node *left;
    struct test_rbt_node *right;
    struct test_rbt_node *parent;
} test_rbt_node;

typedef struct test_rbt_tree {
    CC_RbtOps ops;
    test_rbt_node *root;
    CCINT32 ch_root_count;  // ch_root 被调用次数
    CCINT32 rotate_count;   // 旋转次数
} test_rbt_tree;

/* ---- 比较 ---- */

static CCINT32 test_cmp_key(void *user, void *node, CCUINTPTR key)
{
    (void)user;
    CCUINTPTR k = ((test_rbt_node*)node)->key;
    if (k == key) return 0;
    return (k > key) ? 1 : -1;
}

static CCINT32 test_cmp_node(void *user, void *node_1, void *node_2)
{
    (void)user;
    CCUINTPTR a = ((test_rbt_node*)node_1)->key;
    CCUINTPTR b = ((test_rbt_node*)node_2)->key;
    if (a == b) return 0;
    return (a > b) ? 1 : -1;
}

// 仅拷贝数据（键）；结构指针由内核维护，保持不动
static CCINT32 test_copy_node(void *user, void *dst_node, void *src_node)
{
    (void)user;
    ((test_rbt_node*)dst_node)->key = ((test_rbt_node*)src_node)->key;
    return 0;
}

// 中序后继
static void* test_find_instead(void *user, void *node)
{
    (void)user;
    test_rbt_node *succ = ((test_rbt_node*)node)->right;
    if (!succ) return NULL;
    while (succ->left) succ = succ->left;
    return succ;
}

/* ---- 结构访问 ---- */

static void* test_get_node(void *user, void *node, CC_RbtRel relation)
{
    (void)user;
    test_rbt_node *n = (test_rbt_node*)node;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   return n->left;
    case CC_RBT_REL_RIGHT:  return n->right;
    case CC_RBT_REL_PARENT: return n->parent;
    default:                return NULL;
    }
}

static CCUINT32 test_set_node(void *user, void *node_1, void *node_2, CC_RbtRel relation)
{
    (void)user;
    test_rbt_node *n1 = (test_rbt_node*)node_1;
    test_rbt_node *n2 = (test_rbt_node*)node_2;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:   n1->left = n2;   return 0;
    case CC_RBT_REL_RIGHT:  n1->right = n2;  return 0;
    case CC_RBT_REL_PARENT: n1->parent = n2; return 0;
    default:                return 1;
    }
}

/* ---- 颜色 ---- */

static CCBOOL test_get_color(void *user, void *node)
{
    (void)user;
    return ((test_rbt_node*)node)->red;
}

static CCINT32 test_set_color(void *user, void *node, CCBOOL is_red)
{
    (void)user;
    ((test_rbt_node*)node)->red = is_red;
    return 0;
}

/* ---- 旋转：仅重排节点指针，根指针的同步交由内核 ch_root 回调 ---- */

static CCINT32 test_left_rotate(void *user, void *node)
{
    test_rbt_tree *t = (test_rbt_tree*)user;
    test_rbt_node *x = (test_rbt_node*)node;
    test_rbt_node *y = x->right;
    if (!y) return 1;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent)
    {
        if (x->parent->left == x) x->parent->left = y;
        else x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
    t->rotate_count++;
    return 0;
}

static CCINT32 test_right_rotate(void *user, void *node)
{
    test_rbt_tree *t = (test_rbt_tree*)user;
    test_rbt_node *x = (test_rbt_node*)node;
    test_rbt_node *y = x->left;
    if (!y) return 1;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (x->parent)
    {
        if (x->parent->left == x) x->parent->left = y;
        else x->parent->right = y;
    }
    y->right = x;
    x->parent = y;
    t->rotate_count++;
    return 0;
}

/* ---- 根变更通知 ---- */

static void test_ch_root(void *user, void *new_root)
{
    test_rbt_tree *t = (test_rbt_tree*)user;
    t->root = (test_rbt_node*)new_root;
    t->ch_root_count++;
}

/* ============================================================================
 * 测试辅助
 * ============================================================================ */

static void tree_init(test_rbt_tree *t)
{
    memset(&t->ops, 0, sizeof(t->ops));
    t->ops.user         = t;
    t->ops.cmpare_key   = test_cmp_key;
    t->ops.cmpare_node  = test_cmp_node;
    t->ops.copy_node    = test_copy_node;
    t->ops.find_instead = test_find_instead;
    t->ops.get_node     = test_get_node;
    t->ops.set_node     = test_set_node;
    t->ops.get_color    = test_get_color;
    t->ops.set_color    = test_set_color;
    t->ops.left_rotate  = test_left_rotate;
    t->ops.right_rotate = test_right_rotate;
    t->ops.ch_root      = test_ch_root;
    t->root          = NULL;
    t->ch_root_count = 0;
    t->rotate_count  = 0;
}

static test_rbt_node* tree_insert_key(test_rbt_tree *t, CCUINTPTR key)
{
    test_rbt_node *n = (test_rbt_node*)malloc(sizeof(test_rbt_node));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->key = key;
    if (cc_rbt_core_insert(&t->ops, t->root, n) != 0)
    {
        free(n);
        return NULL;
    }
    return n;
}

static CCINT32 tree_size(const test_rbt_node *n)
{
    return n ? 1 + tree_size(n->left) + tree_size(n->right) : 0;
}

static void tree_free_nodes(test_rbt_node *n)
{
    if (!n) return;
    tree_free_nodes(n->left);
    tree_free_nodes(n->right);
    free(n);
}

static void tree_destroy(test_rbt_tree *t)
{
    tree_free_nodes(t->root);
    t->root = NULL;
}

// 红黑性质校验：父子双向一致、无红红、黑高一致，返回 CCTRUE 表示合法
static CCBOOL node_check(const test_rbt_node *n, CCINT32 *black_height)
{
    if (!n)
    {
        *black_height = 1;
        return CCTRUE;
    }
    if (n->left && n->left->parent != n) return CCFALSE;
    if (n->right && n->right->parent != n) return CCFALSE;
    if (n->red)
    {
        if (n->left && n->left->red) return CCFALSE;
        if (n->right && n->right->red) return CCFALSE;
    }
    CCINT32 lh, rh;
    if (!node_check(n->left, &lh)) return CCFALSE;
    if (!node_check(n->right, &rh)) return CCFALSE;
    if (lh != rh) return CCFALSE;
    *black_height = lh + (n->red ? 0 : 1);
    return CCTRUE;
}

static CCBOOL tree_valid(const test_rbt_tree *t)
{
    if (t->root)
    {
        if (t->root->red) return CCFALSE;      // 根必黑
        if (t->root->parent) return CCFALSE;   // 根无父
    }
    CCINT32 bh;
    return node_check(t->root, &bh);
}

static CCBOOL tree_get_all(test_rbt_tree *t, const CCUINTPTR *keys, CCINT32 n)
{
    for (CCINT32 i = 0; i < n; i++)
        if (cc_rbt_core_get(&t->ops, t->root, keys[i]) == NULL)
            return CCFALSE;
    return CCTRUE;
}

static CCBOOL tree_build(test_rbt_tree *t, const CCUINTPTR *keys, CCINT32 n)
{
    for (CCINT32 i = 0; i < n; i++)
        if (!tree_insert_key(t, keys[i]))
            return CCFALSE;
    return CCTRUE;
}

/*
 * 逐个删除并按步校验：返回值非空、树仍合法、节点数递减、
 * 被删 key 检索落空、其余 key 仍可检索。最终应删空。
 */
static CCBOOL tree_drain_verify(test_rbt_tree *t, const CCUINTPTR *keys, CCINT32 n)
{
    for (CCINT32 i = 0; i < n; i++)
    {
        void *removed = cc_rbt_core_remove(&t->ops, t->root, keys[i]);
        if (!removed) return CCFALSE;
        free(removed);
        if (!tree_valid(t)) return CCFALSE;
        if (tree_size(t->root) != n - i - 1) return CCFALSE;
        if (cc_rbt_core_get(&t->ops, t->root, keys[i]) != NULL) return CCFALSE;
        for (CCINT32 j = i + 1; j < n; j++)
            if (cc_rbt_core_get(&t->ops, t->root, keys[j]) == NULL) return CCFALSE;
    }
    return t->root == NULL;
}

/* ============================================================================
 * 断言工具与用例
 * ============================================================================ */

#define RBT_N 128

static CCINT32 passed = 0;
static CCINT32 failed = 0;
static CCUINT32 case_code = 1;

static void rbt_expect(CCBOOL cond, const char *name)
{
    if (cond)
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
            CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "[FAIL] "
            CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT) "%s\n",
            case_code++, name
        );
    }
}

static CCUINT32 rng_state = 0x12345678u;
static CCUINT32 rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7fffu;
}

typedef struct inorder_ctx {
    CCUINTPTR keys[RBT_N];
    CCINT32 count;
    CCINT32 stop_at;    // >0 时收集到该数量后即中断
} inorder_ctx;

static CCINT32 inorder_collect(void *data, void *user)
{
    inorder_ctx *ctx = (inorder_ctx*)user;
    if (ctx->count < RBT_N)
        ctx->keys[ctx->count] = ((test_rbt_node*)data)->key;
    ctx->count++;
    if (ctx->stop_at > 0 && ctx->count >= ctx->stop_at)
        return 42;
    return 0;
}

/* ---- 基础用例 ---- */

static void test_rbt_empty(void)
{
    test_rbt_tree t;
    tree_init(&t);
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 1) == NULL, "空树检索返回 NULL");
    rbt_expect(cc_rbt_core_remove(&t.ops, t.root, 1) == NULL, "空树删除返回 NULL");
    rbt_expect(tree_valid(&t), "空树满足红黑不变量");
    rbt_expect(cc_rbt_core_insert(NULL, NULL, NULL) != 0, "非法参数插入返回非 0");
    rbt_expect(cc_rbt_core_inorder_tranverse(&t.ops, t.root, NULL, NULL) != 0, "空回调遍历返回非 0");
    tree_destroy(&t);
}

static void test_rbt_single(void)
{
    test_rbt_tree t;
    tree_init(&t);

    test_rbt_node *n = tree_insert_key(&t, 10);
    rbt_expect(n != NULL, "单节点插入成功");
    rbt_expect(t.root == n, "插入触发 ch_root 通知根");
    rbt_expect(t.root && !t.root->red, "单节点根为黑");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 10) == n, "单节点检索命中");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 11) == NULL, "单节点检索未命中");
    rbt_expect(tree_valid(&t), "单节点满足红黑不变量");

    rbt_expect(tree_insert_key(&t, 10) == NULL, "重复键插入被拒绝");
    rbt_expect(t.root == n && tree_size(t.root) == 1, "重复键插入未改变树");

    CCINT32 before = t.ch_root_count;
    void *removed = cc_rbt_core_remove(&t.ops, t.root, 10);
    rbt_expect(removed == n, "删除唯一节点返回该节点");
    free(removed);
    rbt_expect(t.root == NULL, "删除唯一节点后树为空");
    rbt_expect(t.ch_root_count == before + 1, "删除根触发 ch_root 通知");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 10) == NULL, "删除后检索落空");
    tree_destroy(&t);
}

/* ---- 规模用例：升序 / 降序 / 随机 ---- */

static void shuffle_keys(CCUINTPTR *keys, CCINT32 n)
{
    for (CCINT32 i = n - 1; i > 0; i--)
    {
        CCINT32 j = (CCINT32)(rng_next() % (CCUINT32)(i + 1));
        CCUINTPTR tmp = keys[i];
        keys[i] = keys[j];
        keys[j] = tmp;
    }
}

static void test_rbt_ascending(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[RBT_N];
    for (CCINT32 i = 0; i < RBT_N; i++) keys[i] = (CCUINTPTR)(i + 1);

    rbt_expect(tree_build(&t, keys, RBT_N), "升序插入全部成功");
    rbt_expect(tree_size(t.root) == RBT_N, "升序插入后节点数正确");
    rbt_expect(tree_get_all(&t, keys, RBT_N), "升序插入后全部可检索");
    rbt_expect(tree_valid(&t), "升序插入满足红黑不变量");
    rbt_expect(t.root && !t.root->red, "升序插入后根为黑");
    rbt_expect(t.rotate_count > 0, "升序插入触发了旋转");
    rbt_expect(tree_drain_verify(&t, keys, RBT_N), "升序树逐个删除始终合法且检索正确");
    tree_destroy(&t);
}

static void test_rbt_descending(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[RBT_N];
    for (CCINT32 i = 0; i < RBT_N; i++) keys[i] = (CCUINTPTR)(RBT_N - i);

    rbt_expect(tree_build(&t, keys, RBT_N), "降序插入全部成功");
    rbt_expect(tree_size(t.root) == RBT_N, "降序插入后节点数正确");
    rbt_expect(tree_get_all(&t, keys, RBT_N), "降序插入后全部可检索");
    rbt_expect(tree_valid(&t), "降序插入满足红黑不变量");
    rbt_expect(t.root && !t.root->red, "降序插入后根为黑");
    rbt_expect(tree_drain_verify(&t, keys, RBT_N), "降序树逐个删除始终合法且检索正确");
    tree_destroy(&t);
}

static void test_rbt_random(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[RBT_N];
    CCUINTPTR lookup[RBT_N];
    for (CCINT32 i = 0; i < RBT_N; i++)
    {
        keys[i] = (CCUINTPTR)(i + 1);
        lookup[i] = keys[i];
    }
    shuffle_keys(keys, RBT_N);

    rbt_expect(tree_build(&t, keys, RBT_N), "随机插入全部成功");
    rbt_expect(tree_size(t.root) == RBT_N, "随机插入后节点数正确");
    rbt_expect(tree_get_all(&t, lookup, RBT_N), "随机插入后全部可检索");
    rbt_expect(tree_valid(&t), "随机插入满足红黑不变量");
    rbt_expect(t.root && !t.root->red, "随机插入后根为黑");
    rbt_expect(tree_drain_verify(&t, keys, RBT_N), "随机树逐个删除始终合法且检索正确");
    tree_destroy(&t);
}

/* ---- 中序遍历 ---- */

static void test_rbt_traverse(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[RBT_N];
    for (CCINT32 i = 0; i < RBT_N; i++) keys[i] = (CCUINTPTR)(i + 1);
    shuffle_keys(keys, RBT_N);
    tree_build(&t, keys, RBT_N);

    inorder_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    CCINT32 ret = cc_rbt_core_inorder_tranverse(&t.ops, t.root, &ctx, inorder_collect);
    rbt_expect(ret == 0, "中序遍历正常返回 0");
    rbt_expect(ctx.count == RBT_N, "中序遍历访问全部节点");

    CCBOOL sorted = CCTRUE;
    for (CCINT32 i = 1; i < ctx.count; i++)
    {
        if (ctx.keys[i] <= ctx.keys[i - 1])
        {
            sorted = CCFALSE;
            break;
        }
    }
    rbt_expect(sorted, "中序遍历结果严格递增");

    memset(&ctx, 0, sizeof(ctx));
    ctx.stop_at = 8;
    ret = cc_rbt_core_inorder_tranverse(&t.ops, t.root, &ctx, inorder_collect);
    rbt_expect(ret == 42, "回调返回非 0 时中断并透传返回值");
    rbt_expect(ctx.count == 8, "中断后回调次数符合预期");

    tree_destroy(&t);
}

/* ---- 删除：定向用例 ---- */

static test_rbt_node* find_one_child(test_rbt_node *n)
{
    if (!n) return NULL;
    CCINT32 c = (n->left ? 1 : 0) + (n->right ? 1 : 0);
    if (c == 1) return n;
    test_rbt_node *r = find_one_child(n->left);
    return r ? r : find_one_child(n->right);
}

static void test_rbt_remove_leaf(void)
{
    test_rbt_tree t;
    tree_init(&t);
    tree_insert_key(&t, 10);
    tree_insert_key(&t, 5);
    tree_insert_key(&t, 15);
    rbt_expect(tree_valid(&t), "结构 {10,5,15} 合法");

    void *removed = cc_rbt_core_remove(&t.ops, t.root, 5);
    rbt_expect(removed != NULL, "删除叶子返回非空");
    free(removed);
    rbt_expect(tree_valid(&t), "删除叶子后仍合法");
    rbt_expect(tree_size(t.root) == 2, "删除叶子后节点数=2");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 5) == NULL, "删除叶子后检索落空");
    rbt_expect(tree_get_all(&t, (CCUINTPTR[]){10, 15}, 2), "删除叶子后其余节点可检索");
    tree_destroy(&t);
}

static void test_rbt_remove_one_child(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[] = {50, 20, 70, 10, 30, 60, 80, 5, 65, 75, 85};
    CCINT32 n = (CCINT32)(sizeof(keys) / sizeof(keys[0]));
    tree_build(&t, keys, n);
    rbt_expect(tree_valid(&t), "单子用例树合法");

    test_rbt_node *target = find_one_child(t.root);
    rbt_expect(target != NULL, "成功构造出单子节点");
    CCUINTPTR k = target->key;
    CCINT32 size_before = tree_size(t.root);

    void *removed = cc_rbt_core_remove(&t.ops, t.root, k);
    rbt_expect(removed != NULL, "删除单子节点返回非空");
    free(removed);
    rbt_expect(tree_valid(&t), "删除单子节点后仍合法");
    rbt_expect(tree_size(t.root) == size_before - 1, "删除单子节点后节点数-1");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, k) == NULL, "删除单子节点后检索落空");

    CCBOOL ok = CCTRUE;
    for (CCINT32 i = 0; i < n; i++)
        if (keys[i] != k && cc_rbt_core_get(&t.ops, t.root, keys[i]) == NULL)
        {
            ok = CCFALSE;
            break;
        }
    rbt_expect(ok, "删除单子节点后其余节点可检索");
    tree_destroy(&t);
}

static void test_rbt_remove_two_children(void)
{
    test_rbt_tree t;
    tree_init(&t);
    CCUINTPTR keys[] = {20, 10, 30, 5, 15, 25, 35};
    CCINT32 n = (CCINT32)(sizeof(keys) / sizeof(keys[0]));
    tree_build(&t, keys, n);
    rbt_expect(tree_valid(&t), "双子用例树合法");

    CCINT32 size_before = tree_size(t.root);
    void *removed = cc_rbt_core_remove(&t.ops, t.root, 10);
    rbt_expect(removed != NULL, "删除双子节点返回非空");
    rbt_expect(removed != (void*)cc_rbt_core_get(&t.ops, t.root, 15), "双子删除返回的是被摘除的替换节点");
    rbt_expect(((test_rbt_node*)removed)->key == 15, "双子删除返回中序后继节点");
    free(removed);
    rbt_expect(tree_valid(&t), "删除双子节点后仍合法");
    rbt_expect(tree_size(t.root) == size_before - 1, "删除双子节点后节点数-1");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 10) == NULL, "被删 key 检索落空");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 15) != NULL, "后继 key 仍在树中");

    CCBOOL ok = CCTRUE;
    for (CCINT32 i = 0; i < n; i++)
        if (keys[i] != 10 && cc_rbt_core_get(&t.ops, t.root, keys[i]) == NULL)
        {
            ok = CCFALSE;
            break;
        }
    rbt_expect(ok, "删除双子节点后其余节点可检索");
    tree_destroy(&t);
}

static void test_rbt_remove_root(void)
{
    test_rbt_tree t;
    tree_init(&t);
    test_rbt_node *r = tree_insert_key(&t, 10);
    test_rbt_node *c = tree_insert_key(&t, 5);
    rbt_expect(r != NULL && c != NULL, "根提升用例构造成功");
    rbt_expect(t.root == r, "根提升前根为 10");

    CCINT32 ch_before = t.ch_root_count;
    void *removed = cc_rbt_core_remove(&t.ops, t.root, 10);
    rbt_expect(removed == r, "删除根返回原根节点");
    free(removed);
    rbt_expect(t.root == c, "删除根后根提升为唯一子节点");
    rbt_expect(t.root && !t.root->red, "新根为黑");
    rbt_expect(t.ch_root_count == ch_before + 1, "删除根触发 ch_root 通知");
    rbt_expect(tree_valid(&t), "删除根后仍合法");
    rbt_expect(cc_rbt_core_get(&t.ops, t.root, 5) == c, "提升后的节点可检索");
    tree_destroy(&t);
}

static void test_rbt_remove_missing(void)
{
    test_rbt_tree t;
    tree_init(&t);
    tree_insert_key(&t, 10);
    tree_insert_key(&t, 5);
    tree_insert_key(&t, 15);
    CCINT32 size_before = tree_size(t.root);

    rbt_expect(cc_rbt_core_remove(&t.ops, t.root, 99) == NULL, "删除不存在的 key 返回 NULL");
    rbt_expect(tree_size(t.root) == size_before, "删除不存在的 key 不改变树");
    rbt_expect(tree_valid(&t), "删除不存在的 key 后仍合法");
    tree_destroy(&t);
}

/* ============================================================================
 * 入口
 * ============================================================================ */

void test_rbt_core(void)
{
    test_rbt_empty();
    test_rbt_single();
    test_rbt_ascending();
    test_rbt_descending();
    test_rbt_random();
    test_rbt_traverse();
    test_rbt_remove_leaf();
    test_rbt_remove_one_child();
    test_rbt_remove_two_children();
    test_rbt_remove_root();
    test_rbt_remove_missing();

    color_print(
        CC_TEXT_COLOR(CC_CYAN, CC_DEFAULT)
        "total: %d, passed: %d, failed: %d\n",
        passed + failed, passed, failed
    );
}
