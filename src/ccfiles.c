/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 虚拟文件空间核心：运行时状态、节点红黑树、节点/挂载生命周期、
 *               探针、初始化，以及 chdir / stat / list / find 查询接口。
 *               文档见 <工程根目录>/docs/ccfiles.md。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>

/* 运行时状态：节点数组、根下标、exe 同层 res 目录 */
CCFsRuntime ccfiles_var;

/* ch_root 回调当前所属父目录（用于把子红黑树新根写回 children_root） */
static CCUINT32 g_tree_parent = CC_FS_NIL;

/* 仅本文件使用的前置声明（定义在探针生命周期段） */
static void fs_invalidate_probes(CCUINT32 idx);

/*
 * ============================================================================
 * 红黑树操作集：把「下标索引的连续节点」适配为「指针语义」的 CC_RbtOps 接口
 * ==========================================================================
 */

static CCINT32 fs_compare_key(void *user, void *node, CCUINTPTR key)
{
    CC_UNUSED(user);
    const char *k = (const char *)key;
    return strcmp(((CCFsNode *)node)->name, k);
}

static CCINT32 fs_compare_node(void *user, void *node_1, void *node_2)
{
    CC_UNUSED(user);
    return strcmp(((CCFsNode *)node_1)->name, ((CCFsNode *)node_2)->name);
}

/* 删除双子节点时，内核把后继节点数据拷入被删槽位；
 * 除树结构字段与 probe_head 外，数据字段必须一并拷贝。 */
static CCINT32 fs_copy_node(void *user, void *dst_node, void *src_node)
{
    CC_UNUSED(user);
    CCFsNode *dst = (CCFsNode *)dst_node;
    CCFsNode *src = (CCFsNode *)src_node;
    dst->name = src->name;
    dst->fs_parent = src->fs_parent;
    dst->children_root = src->children_root;
    dst->type = src->type;
    dst->mount = src->mount;
    dst->size = src->size;
    dst->ctime = src->ctime;
    dst->mtime = src->mtime;
    dst->crc32 = src->crc32;
    /* 常驻句柄的所有权随之转移：漏拷会泄漏 FILE*（Windows 下文件再也删不掉） */
    dst->fp = src->fp;
    dst->fp_pos = src->fp_pos;
    dst->fp_writable = src->fp_writable;
    src->fp = NULL;
    src->fp_pos = 0;
    src->fp_writable = CCFALSE;
    return 0;
}

static void *fs_find_instead(void *user, void *node)
{
    CC_UNUSED(user);
    CCFsNode *p = (CCFsNode *)node;
    if (p->right == CC_FS_NIL)
        return NULL;
    CCFsNode *cursor = node_at(p->right);
    while (cursor->left != CC_FS_NIL)
        cursor = node_at(cursor->left);
    return cursor;
}

static void *fs_get_node(void *user, void *node, CC_RbtRel relation)
{
    CC_UNUSED(user);
    CCFsNode *p = (CCFsNode *)node;
    CCUINT32 idx;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:
        idx = p->left;
        break;
    case CC_RBT_REL_RIGHT:
        idx = p->right;
        break;
    case CC_RBT_REL_PARENT:
        idx = p->parent;
        break;
    default:
        return NULL;
    }
    return idx == CC_FS_NIL ? NULL : node_at(idx);
}

static CCUINT32 fs_set_node(void *user, void *node_1, void *node_2, CC_RbtRel relation)
{
    CC_UNUSED(user);
    CCFsNode *p = (CCFsNode *)node_1;
    CCUINT32 idx = node_2 ? idx_of((CCFsNode *)node_2) : CC_FS_NIL;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:
        p->left = idx;
        return 0;
    case CC_RBT_REL_RIGHT:
        p->right = idx;
        return 0;
    case CC_RBT_REL_PARENT:
        p->parent = idx;
        return 0;
    default:
        return 1;
    }
}

static CCBOOL fs_get_color(void *user, void *node)
{
    CC_UNUSED(user);
    return ((CCFsNode *)node)->is_red;
}

static CCINT32 fs_set_color(void *user, void *node, CCBOOL is_red)
{
    CC_UNUSED(user);
    ((CCFsNode *)node)->is_red = is_red;
    return 0;
}

static CCINT32 fs_left_rotate(void *user, void *node)
{
    CC_UNUSED(user);
    CCFsNode *x = (CCFsNode *)node;
    if (x->right == CC_FS_NIL)
        return 1;
    CCUINT32 xi = idx_of(x);
    CCUINT32 yi = x->right;
    CCFsNode *y = node_at(yi);

    x->right = y->left;
    if (y->left != CC_FS_NIL)
        node_at(y->left)->parent = xi;

    y->parent = x->parent;
    if (x->parent != CC_FS_NIL)
    {
        CCFsNode *pp = node_at(x->parent);
        if (pp->left == xi)
            pp->left = yi;
        else
            pp->right = yi;
    }

    y->left = xi;
    x->parent = yi;
    return 0;
}

static CCINT32 fs_right_rotate(void *user, void *node)
{
    CC_UNUSED(user);
    CCFsNode *x = (CCFsNode *)node;
    if (x->left == CC_FS_NIL)
        return 1;
    CCUINT32 xi = idx_of(x);
    CCUINT32 yi = x->left;
    CCFsNode *y = node_at(yi);

    x->left = y->right;
    if (y->right != CC_FS_NIL)
        node_at(y->right)->parent = xi;

    y->parent = x->parent;
    if (x->parent != CC_FS_NIL)
    {
        CCFsNode *pp = node_at(x->parent);
        if (pp->left == xi)
            pp->left = yi;
        else
            pp->right = yi;
    }

    y->right = xi;
    x->parent = yi;
    return 0;
}

/* 子红黑树根变化：把新根下标写回其父目录的 children_root */
static void fs_ch_root(void *user, void *new_root)
{
    CC_UNUSED(user);
    if (g_tree_parent == CC_FS_NIL)
        return;
    CCFsNode *parent = node_at(g_tree_parent);
    parent->children_root = new_root ? idx_of((CCFsNode *)new_root) : CC_FS_NIL;
}

static CC_RbtOps fs_tree_ops = {
    .user = NULL,
    .compare_key = fs_compare_key,
    .compare_node = fs_compare_node,
    .copy_node = fs_copy_node,
    .find_instead = fs_find_instead,
    .get_node = fs_get_node,
    .set_node = fs_set_node,
    .get_color = fs_get_color,
    .set_color = fs_set_color,
    .left_rotate = fs_left_rotate,
    .right_rotate = fs_right_rotate,
    .ch_root = fs_ch_root,
};
/* ============================================================================
 * 节点 / 挂载 生命周期
 * ========================================================================== */

void fs_mount_free(CCFsMount *m)
{
    if (!m)
        return;
    if (m->real_path)
        ccalloc(m->real_path, 0, NULL);
    ccalloc(m, 0, NULL);
}

CCFsMount *fs_mount_new(const char *real_path, CCUINT32 type, CCUINT8 origin)
{
    CCFsMount *m = (CCFsMount *)ccalloc(NULL, sizeof(CCFsMount),
                                        CC_DBG_REL_TAG("文件挂载信息", NULL));
    if (!m)
        return NULL;
    m->real_path = real_path ? fs_strdup(real_path,
                                         CC_DBG_REL_TAG("文件挂载真实路径", NULL))
                             : NULL;
    if (real_path && !m->real_path)
    {
        ccalloc(m, 0, NULL);
        return NULL;
    }
    m->type = type;
    m->origin = origin;
    m->pkg_off = 0;
    m->pkg_size = 0;
    return m;
}

/* 追加一个新槽位（纯追加，配合 swap-remove 保持数组紧凑） */
static CCUINT32 fs_alloc_slot(void)
{
    CCFsNode zero;
    memset(&zero, 0, sizeof(zero));
    zero.parent = CC_FS_NIL;
    zero.left = CC_FS_NIL;
    zero.right = CC_FS_NIL;
    zero.fs_parent = CC_FS_NIL;
    zero.children_root = CC_FS_NIL;
    zero.is_red = CCFALSE;
    zero.probe_head = NULL;
    if (cc_dyn_core_push(&ccfiles_var.nodes, (CCUINT32)sizeof(CCFsNode),
                         (CCINT32)sizeof(CCFsNode), &zero) != 0)
        return CC_FS_NIL;
    return node_count() - 1;
}

/* 回滚最近一次分配：创建节点失败时回收刚追加的末尾槽位 */
void fs_alloc_rollback(void)
{
    CCFsNode tmp;
    cc_dyn_core_pop(&ccfiles_var.nodes, (CCUINT32)sizeof(CCFsNode),
                    (CCINT32)sizeof(CCFsNode), &tmp);
}

/* ============================================================================
 * 子红黑树操作
 * ========================================================================== */

CCUINT32 fs_child_get_idx(CCUINT32 parent_idx, const char *name)
{
    CCUINT32 root_idx = node_at(parent_idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return CC_FS_NIL;
    g_tree_parent = parent_idx;
    CCFsNode *hit = (CCFsNode *)cc_rbt_core_get(&fs_tree_ops, node_at(root_idx), (CCUINTPTR)name);
    return hit ? idx_of(hit) : CC_FS_NIL;
}

/* 把 child 插入 parent 的子红黑树；成功 0，重复 1，参数 -1，内存不足 1 */
CCINT32 fs_insert_child(CCUINT32 parent_idx, CCUINT32 child_idx)
{
    CCUINT32 root_idx = node_at(parent_idx)->children_root;
    g_tree_parent = parent_idx;
    CCFsNode *root = (root_idx == CC_FS_NIL) ? NULL : node_at(root_idx);
    return cc_rbt_core_insert(&fs_tree_ops, root, node_at(child_idx));
}

/* 新建节点并挂到 parent 下（不插入树），返回下标 */
CCUINT32 fs_create_node(CCUINT32 parent_idx, const char *name,
                        CCFsType type, const char *real_path, CCUINT32 mount_type, CCUINT8 origin)
{
    CCUINT32 idx = fs_alloc_slot();
    if (idx == CC_FS_NIL)
        return CC_FS_NIL;

    CCFsNode *n = node_at(idx);
    n->name = fs_strdup(name, CC_DBG_REL_TAG("文件节点名称", NULL));
    if (!n->name)
    {
        fs_alloc_rollback();
        return CC_FS_NIL;
    }
    n->parent = CC_FS_NIL;
    n->left = CC_FS_NIL;
    n->right = CC_FS_NIL;
    n->is_red = CCFALSE;
    n->fs_parent = parent_idx;
    n->children_root = CC_FS_NIL;
    n->type = type;
    n->mount = fs_mount_new(real_path, mount_type, origin);
    if (!n->mount)
    {
        ccalloc(n->name, 0, NULL);
        fs_alloc_rollback();
        return CC_FS_NIL;
    }
    n->size = 0;
    n->ctime = fs_now_seconds();
    n->mtime = n->ctime;
    n->crc32 = 0;
    n->probe_head = NULL;
    n->fp = NULL;
    n->fp_pos = 0;
    n->fp_writable = CCFALSE;
    return idx;
}

typedef struct cc_fs_fix_parent_ctx
{
    CCUINT32 new_parent;
} CCFsFixParentCtx;

/* swap-remove 搬移目录时，修正其所有子节点的 fs_parent */
static CCINT32 fs_fix_fs_parent_cb(void *data, void *user)
{
    CCFsNode *n = (CCFsNode *)data;
    CCFsFixParentCtx *c = (CCFsFixParentCtx *)user;
    n->fs_parent = c->new_parent;
    return 0;
}

/* 把 dir 的全部直接子节点 fs_parent 指向 new_parent（子树整体换父时使用） */
void fs_set_children_parent(CCUINT32 dir_idx, CCUINT32 new_parent)
{
    CCUINT32 root_idx = node_at(dir_idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return;
    CCFsFixParentCtx ctx;
    ctx.new_parent = new_parent;
    cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                  node_at(root_idx), &ctx, fs_fix_fs_parent_cb);
}

/* 把已从树中摘除的槽位 swap-remove 掉，并修正所有指向末尾节点的引用。
   调用方负责先释放该槽位的 name / mount 等业务资源。 */
static void fs_remove_slot(CCUINT32 y_idx)
{
    CCUINT32 last_idx = node_count() - 1;
    if (y_idx != last_idx)
    {
        CCFsNode *last = node_at(last_idx);
        /* 修正名称树中指向末尾节点的引用 */
        if (last->parent != CC_FS_NIL)
        {
            CCFsNode *pp = node_at(last->parent);
            if (pp->left == last_idx)
                pp->left = y_idx;
            else if (pp->right == last_idx)
                pp->right = y_idx;
        }
        else if (last->fs_parent != CC_FS_NIL)
        {
            if (node_at(last->fs_parent)->children_root == last_idx)
                node_at(last->fs_parent)->children_root = y_idx;
        }
        else if (last_idx == ccfiles_var.root)
        {
            ccfiles_var.root = y_idx;
        }
        if (last->left != CC_FS_NIL)
            node_at(last->left)->parent = y_idx;
        if (last->right != CC_FS_NIL)
            node_at(last->right)->parent = y_idx;

        /* 修正末尾节点的子节点 fs_parent（若它是目录） */
        if (last->children_root != CC_FS_NIL)
        {
            CCFsFixParentCtx ctx;
            ctx.new_parent = y_idx;
            cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                          node_at(last->children_root), &ctx, fs_fix_fs_parent_cb);
        }

        /* 指向末尾节点的探针 cur 跟随 */
        for (CCFsProbeRef *pr = last->probe_head; pr; pr = pr->next)
            if (pr->probe)
                pr->probe->cur = y_idx;

        *node_at(y_idx) = *last;
    }
    CCFsNode tmp;
    cc_dyn_core_pop(&ccfiles_var.nodes, (CCUINT32)sizeof(CCFsNode),
                    (CCINT32)sizeof(CCFsNode), &tmp);
}

/* 下标搬移跟踪：用于事务提交阶段，保证调用方持有的关键节点下标始终有效 */
static void fs_watch_relocate(CCUINT32 *watch, CCUINT32 count,
                              CCUINT32 from, CCUINT32 to)
{
    if (!watch || from == to)
        return;
    for (CCUINT32 i = 0; i < count; i++)
        if (watch[i] == from)
            watch[i] = to;
}

/* 从父目录子红黑树中按名删除，并释放被删条目数据；swap-remove 保持数组紧凑。
   两组 watch 均可跟踪节点下标搬移；提交阶段用它保护 stage 根等关键节点，
   批量删除则用第一组跟踪待删表自身。 */
static CCINT32 fs_remove_child_watch(CCUINT32 parent_idx, const char *name,
                                     CCUINT32 *watch, CCUINT32 count, CCUINT32 *watch2, CCUINT32 count2)
{
    if (parent_idx == CC_FS_NIL || !name)
        return CC_FS_ERR_PARAM;
    CCUINT32 root_idx = node_at(parent_idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    g_tree_parent = parent_idx;
    CCFsNode *z = (CCFsNode *)cc_rbt_core_get(&fs_tree_ops, node_at(root_idx), (CCUINTPTR)name);
    if (!z)
        return CC_FS_ERR_NOENT;

    CCUINT32 z_idx = idx_of(z);
    char *z_name = node_at(z_idx)->name;
    CCFsMount *z_mount = node_at(z_idx)->mount;

    /* 被删条目指向的探针失效；其常驻读写句柄随之 flush + fclose。
       删除路径的调用方已先做 fs_fh_close_all，此处仅兜底。 */
    fs_invalidate_probes(z_idx);

    g_tree_parent = parent_idx;
    CCFsNode *y = (CCFsNode *)cc_rbt_core_remove(&fs_tree_ops, node_at(root_idx), (CCUINTPTR)name);
    if (!y)
        return CC_FS_ERR_NOENT;
    CCUINT32 y_idx = idx_of(y);

    if (y_idx != z_idx)
    {
        /* 后继被拷入 z 槽位：修正其子树根的 fs_parent，并让后继的探针回指跟随
           （句柄随整个节点结构一起搬到 z 槽位，无需按节点下标另存） */
        CCFsNode *zt = node_at(z_idx);
        if (zt->children_root != CC_FS_NIL)
            node_at(zt->children_root)->fs_parent = z_idx;

        CCFsProbeRef *h = node_at(y_idx)->probe_head;
        node_at(y_idx)->probe_head = NULL;
        node_at(z_idx)->probe_head = h;
        for (CCFsProbeRef *pr = h; pr; pr = pr->next)
            if (pr->probe)
                pr->probe->cur = z_idx;
    }

    if (z_mount)
        fs_mount_free(z_mount);
    if (z_name)
        ccalloc(z_name, 0, NULL);

    CCUINT32 last_idx = node_count() - 1;
    if (y_idx != z_idx)
    {
        fs_watch_relocate(watch, count, y_idx, z_idx);
        if (watch2 && watch2 != watch)
            fs_watch_relocate(watch2, count2, y_idx, z_idx);
    }
    fs_watch_relocate(watch, count, last_idx, y_idx);
    if (watch2 && watch2 != watch)
        fs_watch_relocate(watch2, count2, last_idx, y_idx);

    fs_remove_slot(y_idx);
    return CC_FS_OK;
}

CCINT32 fs_remove_child(CCUINT32 parent_idx, const char *name)
{
    return fs_remove_child_watch(parent_idx, name, NULL, 0, NULL, 0);
}

typedef struct cc_fs_collect_ctx
{
    CCDyn *stack;
} CCFsCollectCtx;

/* 收集子树时，把子节点下标压入显式栈 */
static CCINT32 fs_collect_child_cb(void *data, void *user)
{
    CCFsNode *n = (CCFsNode *)data;
    CCFsCollectCtx *c = (CCFsCollectCtx *)user;
    CCUINT32 idx = idx_of(n);
    return cc_dyn_core_push(c->stack, (CCUINT32)sizeof(CCUINT32),
                            (CCINT32)sizeof(CCUINT32), &idx) != 0
               ? 1
               : 0;
}

/* 收集某目录全部直接子节点下标（先收集再处理，避免遍历中被 realloc 打断） */
static CCINT32 fs_collect_cb(void *data, void *user)
{
    CCFsNode *n = (CCFsNode *)data;
    CCDyn *d = (CCDyn *)user;
    CCUINT32 idx = idx_of(n);
    return cc_dyn_core_push(d, (CCUINT32)sizeof(CCUINT32),
                            (CCINT32)sizeof(CCUINT32), &idx) != 0
               ? 1
               : 0;
}

CCINT32 fs_collect_children_ex(CCUINT32 dir_idx, CCDyn *out)
{
    if (!out)
        return CC_FS_ERR_PARAM;
    CCUINT32 root_idx = node_at(dir_idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return CC_FS_OK;
    return cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                         node_at(root_idx), out, fs_collect_cb) == 0
               ? CC_FS_OK
               : CC_FS_ERR_NOSPACE;
}

void fs_collect_children(CCUINT32 dir_idx, CCDyn *out)
{
    (void)fs_collect_children_ex(dir_idx, out);
}

/* 不分配内存地遍历某目录的直接子节点 */
CCINT32 fs_children_walk(CCUINT32 dir_idx, void *user,
                         CCINT32 (*cb)(void *data, void *user))
{
    if (dir_idx == CC_FS_NIL || !cb)
        return CC_FS_ERR_PARAM;
    CCUINT32 root_idx = node_at(dir_idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return CC_FS_OK;
    return cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                         node_at(root_idx), user, cb) == 0
               ? CC_FS_OK
               : CC_FS_ERR_NOSPACE;
}

/* 收集 idx 的后代（include_self 决定是否含 idx 本身）到 list，下标此刻稳定 */
static CCINT32 fs_collect_tree(CCUINT32 idx, CCBOOL include_self, CCDyn *list)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCINT32 ret = CC_FS_OK;

    if (include_self)
    {
        if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCUINT32),
                             (CCINT32)sizeof(CCUINT32), &idx) != 0)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoPushRoot;
        }
    }
    else if (node_at(idx)->children_root != CC_FS_NIL)
    {
        CCFsCollectCtx ctx;
        ctx.stack = &stack;
        if (cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                          node_at(node_at(idx)->children_root), &ctx, fs_collect_child_cb) != 0)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoPushChildren;
        }
    }

    while (stack.size >= (CCUINT32)sizeof(CCUINT32))
    {
        CCUINT32 cur;
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCUINT32),
                            (CCINT32)sizeof(CCUINT32), &cur) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoPushRoot;
        }
        if (cc_dyn_core_push(list, (CCUINT32)sizeof(CCUINT32),
                             (CCINT32)sizeof(CCUINT32), &cur) != 0)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoPushList;
        }
        if (node_at(cur)->children_root != CC_FS_NIL)
        {
            CCFsCollectCtx ctx;
            ctx.stack = &stack;
            if (cc_rbt_core_inorder_tranverse(&fs_tree_ops,
                                              node_at(node_at(cur)->children_root), &ctx, fs_collect_child_cb) != 0)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoPushChildren;
            }
        }
    }

    fs_buf_free(&stack);
    return CC_FS_OK;

FailedtoPushChildren:
FailedtoPushList:
FailedtoPushRoot:
    fs_buf_free(&stack);
    return ret;
}

/* 预先收集以 idx 为根的整棵子树，供事务提交前预检内存、失败时不改动旧树 */
CCINT32 fs_collect_subtree(CCUINT32 idx, CCDyn *out)
{
    return fs_collect_tree(idx, CCTRUE, out);
}

/* 按「先序的逆序」删除 list 中的节点：先序保证父节点先于所有后代出现，
   反转后即保证后代先于父节点删除。删除过程中的 swap-remove / 后继拷入会
   搬动下标，因此直接把待删表当作第一组 watch 原地修正，外部 watch 作为
   第二组同步修正，全程不再分配内存。 */
CCINT32 fs_delete_list_watch(CCDyn *list, CCBOOL unlink_physical,
                             CCUINT32 *watch, CCUINT32 watch_count)
{
    CCINT32 ret = CC_FS_OK;
    CCUINT32 *arr = (CCUINT32 *)list->array;
    CCUINT32 cnt = list->size / (CCUINT32)sizeof(CCUINT32);
    if (cnt == 0)
        return CC_FS_OK;

    for (CCUINT32 i = 0; i < cnt / 2; i++)
    {
        CCUINT32 tmp = arr[i];
        arr[i] = arr[cnt - 1 - i];
        arr[cnt - 1 - i] = tmp;
    }

    for (CCUINT32 i = 0; i < cnt; i++)
    {
        CCUINT32 cur = arr[i];
        if (cur >= node_count())
            continue;
        CCFsNode *n = node_at(cur);
        if (unlink_physical)
        {
            /* 先 flush + fclose（Windows 下不关就删不掉），再删物理 */
            CCINT32 r = fs_physical_delete(cur);
            if (r != CC_FS_OK && ret == CC_FS_OK)
                ret = r;
        }
        else
        {
            CCINT32 r = fs_fh_close(cur);
            if (r != CC_FS_OK && ret == CC_FS_OK)
                ret = r;
        }
        CCUINT32 parent_idx = n->fs_parent;
        char *nm = n->name;
        if (parent_idx == CC_FS_NIL)
        {
            /* 已脱离名称树的根：直接释放业务资源并回收槽位 */
            fs_invalidate_probes(cur);
            CCFsMount *mount = n->mount;
            if (mount)
                fs_mount_free(mount);
            if (nm)
                ccalloc(nm, 0, NULL);
            CCUINT32 last_idx = node_count() - 1;
            fs_watch_relocate(arr, cnt, last_idx, cur);
            fs_watch_relocate(watch, watch_count, last_idx, cur);
            fs_remove_slot(cur);
            continue;
        }
        CCINT32 rr = fs_remove_child_watch(parent_idx, nm,
                                           arr, cnt, watch, watch_count);
        if (rr != CC_FS_OK && ret == CC_FS_OK)
            ret = rr;
    }
    return ret;
}

/* 删除以 idx 为根的子树（含 idx 本身）；无递归。
   watch 中的下标会随 swap-remove / 后继拷入自动修正。 */
CCINT32 fs_delete_subtree_watch(CCUINT32 idx, CCBOOL unlink_physical,
                                CCUINT32 *watch, CCUINT32 watch_count)
{
    if (idx == CC_FS_NIL)
        return CC_FS_OK;
    CCDyn list;
    memset(&list, 0, sizeof(list));
    CCINT32 ret = fs_collect_tree(idx, CCTRUE, &list);
    if (ret == CC_FS_OK)
        ret = fs_delete_list_watch(&list, unlink_physical, watch, watch_count);
    fs_buf_free(&list);
    return ret;
}

CCINT32 fs_delete_subtree_ex(CCUINT32 idx, CCBOOL unlink_physical)
{
    return fs_delete_subtree_watch(idx, unlink_physical, NULL, 0);
}

/* 删除 idx 的所有后代（不含 idx 本身）；无递归 */
CCINT32 fs_delete_children(CCUINT32 idx, CCBOOL unlink_physical)
{
    if (idx == CC_FS_NIL)
        return CC_FS_OK;
    CCDyn list;
    memset(&list, 0, sizeof(list));
    CCINT32 ret = fs_collect_tree(idx, CCFALSE, &list);
    if (ret == CC_FS_OK)
        ret = fs_delete_list_watch(&list, unlink_physical, NULL, 0);
    fs_buf_free(&list);
    return ret;
}

/* 只删虚拟节点、不记录物理删除（refresh / unmount / 事务收尾使用） */
void fs_delete_subtree(CCUINT32 idx)
{
    fs_delete_subtree_ex(idx, CCFALSE);
}

/* 释放一个已脱离名称树、且没有子节点的空壳节点（事务 stage 收尾）。
   不分配内存，因此不会在提交后半段失败。 */
void fs_discard_detached_node(CCUINT32 idx)
{
    if (idx == CC_FS_NIL || idx >= node_count())
        return;
    CCFsNode *n = node_at(idx);
    if (n->fs_parent != CC_FS_NIL || n->children_root != CC_FS_NIL)
        return;
    fs_invalidate_probes(idx);
    if (n->mount)
        fs_mount_free(n->mount);
    if (n->name)
        ccalloc(n->name, 0, NULL);
    fs_remove_slot(idx);
}
/* ============================================================================
 * 初始化 / 反初始化
 * ========================================================================== */

/* 探针 cur 指向节点：把探针回指头插到节点链表，并标记探针有效 */
void fs_node_ref(CCFiles *fs, CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    fs->ref.prev = NULL;
    fs->ref.next = n->probe_head;
    if (n->probe_head)
        n->probe_head->prev = &fs->ref;
    n->probe_head = &fs->ref;
    fs->ref.probe = fs;
    fs->alive = CCTRUE;
}

/* 探针离开节点：从节点链表摘除自身回指；若这是最后一个引用，立即 flush + fclose */
void fs_node_unref(CCFiles *fs)
{
    CCUINT32 idx = fs->cur;
    CCFsProbeRef *r = &fs->ref;
    if (idx != CC_FS_NIL && idx < node_count() && node_at(idx)->probe_head == r)
        node_at(idx)->probe_head = r->next;
    if (r->prev)
        r->prev->next = r->next;
    if (r->next)
        r->next->prev = r->prev;
    r->prev = NULL;
    r->next = NULL;
    r->probe = NULL;
    fs->cur = CC_FS_NIL;
    fs->alive = CCFALSE;

    if (idx != CC_FS_NIL && idx < node_count() && node_at(idx)->probe_head == NULL)
        fs_fh_close(idx); /* 无探针再指向该节点：句柄不再需要常驻 */
}

/* 删除节点时：失效所有指向它的探针（多探针场景），并关闭其常驻句柄 */
static void fs_invalidate_probes(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    CCFsProbeRef *r = n->probe_head;
    while (r)
    {
        CCFsProbeRef *next = r->next;
        CCFiles *p = r->probe;
        if (p)
        {
            p->alive = CCFALSE;
            p->cur = CC_FS_NIL;
        }
        r->prev = NULL;
        r->next = NULL;
        r->probe = NULL;
        r = next;
    }
    n->probe_head = NULL;
    fs_fh_close(idx);
}

void init_ccfiles(void)
{
    ccfiles_var.nodes.array = NULL;
    ccfiles_var.nodes.size = 0;
    ccfiles_var.nodes.capacity = 0;
    ccfiles_var.root = CC_FS_NIL;
    ccfiles_var.res_base = NULL;
    g_tree_parent = CC_FS_NIL;

    fs_init_exe_dir();

    CCFsNode zero;
    memset(&zero, 0, sizeof(zero));
    zero.parent = CC_FS_NIL;
    zero.left = CC_FS_NIL;
    zero.right = CC_FS_NIL;
    zero.fs_parent = CC_FS_NIL;
    zero.children_root = CC_FS_NIL;
    zero.is_red = CCFALSE;
    if (cc_dyn_core_push(&ccfiles_var.nodes, (CCUINT32)sizeof(CCFsNode),
                         (CCINT32)sizeof(CCFsNode), &zero) != 0)
        return;

    ccfiles_var.root = 0;
    CCFsNode *r = node_at(0);
    r->name = fs_strdup("/", CC_DBG_REL_TAG("根目录名称", NULL));
    r->type = CC_FS_TYPE_DIR;
    r->mount = fs_mount_new(ccfiles_var.res_base, CC_FS_MOUNT_DIR, FS_ORIGIN_MEM);
    r->size = 0;
    r->ctime = fs_now_seconds();
    r->mtime = r->ctime;
    r->crc32 = 0;
    r->probe_head = NULL;
}

void deinit_ccfiles(void)
{
    fs_fh_close_all(); /* 先落盘并关闭所有常驻句柄 */

    CCUINT32 count = node_count();
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCFsNode *n = node_at(i);
        if (n->type == CC_FS_TYPE_NONE)
            continue;
        if (n->name)
            ccalloc(n->name, 0, NULL);
        if (n->mount)
            fs_mount_free(n->mount);
    }

    if (ccfiles_var.res_base)
        ccalloc(ccfiles_var.res_base, 0, NULL);
    if (ccfiles_var.nodes.array)
        ccalloc(ccfiles_var.nodes.array, 0, NULL);

    ccfiles_var.nodes.array = NULL;
    ccfiles_var.nodes.size = 0;
    ccfiles_var.nodes.capacity = 0;
    ccfiles_var.root = CC_FS_NIL;
    ccfiles_var.res_base = NULL;
}

/* ============================================================================
 * 探针生命周期 / 工作目录
 * ========================================================================== */

CCFiles *ccfiles_create(void)
{
    if (ccfiles_var.root == CC_FS_NIL)
        return NULL;
    CCFiles *fs = (CCFiles *)ccalloc(NULL, sizeof(CCFiles),
                                     CC_DBG_REL_TAG("文件探针", NULL));
    if (!fs)
        return NULL;
    fs->alive = CCTRUE;
    fs->cwd = ccfiles_var.root;
    fs->cur = CC_FS_NIL;
    fs->flags = 0;
    fs->ref.prev = NULL;
    fs->ref.next = NULL;
    fs->ref.probe = NULL;
    return fs;
}

void ccfiles_destroy(CCFiles *fs)
{
    if (!fs)
        return;
    if (fs->cur != CC_FS_NIL)
        fs_node_unref(fs);
    ccalloc(fs, 0, NULL);
}

CCINT32 ccfiles_chdir(CCFiles *fs, const char *path)
{
    if (!fs || !path)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (node_at(idx)->type != CC_FS_TYPE_DIR)
        return CC_FS_ERR_NOTDIR;
    fs->cwd = idx;
    return CC_FS_OK;
}
/* 把节点下标转换为绝对虚拟路径（写入 out，调用方负责 fs_buf_free） */
static CCINT32 fs_path_of_idx(CCUINT32 idx, CCDyn *out)
{
    if (!out || idx == CC_FS_NIL)
        return CC_FS_ERR_PARAM;

    CCUINT32 root = ccfiles_var.root;
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCINT32 ret = CC_FS_OK;

    if (idx == root)
    {
        ret = fs_buf_put(out, "/");
    }
    else
    {
        CCUINT32 cur = idx;
        while (cur != root && cur != CC_FS_NIL)
        {
            if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCUINT32),
                                 (CCINT32)sizeof(CCUINT32), &cur) != 0)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoPushPath;
            }
            cur = node_at(cur)->fs_parent;
        }
        if (cur != root)
            ret = CC_FS_ERR_NOENT; /* 下标已脱离根链 */
        CCUINT32 count = stack.size / (CCUINT32)sizeof(CCUINT32);
        CCUINT32 *arr = (CCUINT32 *)stack.array;
        for (CCINT32 i = (CCINT32)count - 1; ret == CC_FS_OK && i >= 0; i--)
        {
            ret = fs_buf_put(out, "/");
            if (ret == CC_FS_OK)
                ret = fs_buf_put(out, node_at(arr[i])->name);
        }
    }

    fs_buf_free(&stack);
    return ret;

FailedtoPushPath:
    fs_buf_free(&stack);
    return CC_FS_ERR_NOSPACE;
}

/* 公共辅助：路径写入调用方缓冲；容量不足返回 CC_FS_ERR_PARAM，成功返回含 '\0' 的长度 */
static CCINT32 fs_copy_path_to_buf(CCUINT32 idx, char *buf, CCUINT32 size)
{
    if (!buf || size == 0)
        return CC_FS_ERR_PARAM;

    CCDyn path;
    memset(&path, 0, sizeof(path));
    CCINT32 ret = fs_path_of_idx(idx, &path);
    if (ret == CC_FS_OK)
    {
        const char *s = fs_buf_str(&path);
        CCUINT32 len = (CCUINT32)strlen(s);
        /* 用户提供的缓冲区容量是外部约束，超出则明确返回参数错误 */
        if (len + 1 > size)
            ret = CC_FS_ERR_PARAM;
        else
        {
            memcpy(buf, s, len + 1);
            ret = (CCINT32)(len + 1);
        }
    }
    fs_buf_free(&path);
    return ret;
}

CCINT32 ccfiles_getcwd(CCFiles *fs, char *buf, CCUINT32 size)
{
    if (!fs)
        return CC_FS_ERR_PARAM;
    return fs_copy_path_to_buf(fs->cwd, buf, size);
}

CCINT32 ccfiles_getcur(CCFiles *fs, char *buf, CCUINT32 size)
{
    if (!fs)
        return CC_FS_ERR_PARAM;
    if (fs->cur == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    return fs_copy_path_to_buf(fs->cur, buf, size);
}
CCINT32 ccfiles_stat(CCFiles *fs, const char *v_path, CCFsStat *out)
{
    if (!fs || !v_path || !out)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    CCFsNode *n = node_at(idx);
    out->type = n->type;
    out->size = n->size;
    out->ctime = n->ctime;
    out->mtime = n->mtime;
    out->readonly = fs_is_protected(idx);
    out->crc32 = n->crc32;
    return CC_FS_OK;
}

/* ============================================================================
 * 目录遍历与检索
 * ========================================================================== */

typedef struct cc_fs_list_ctx
{
    CCDataCallback cbk;
    void *user;
} CCFsListCtx;

/* 把节点打包成公开条目；仅在本次回调期间有效 */
static void fs_make_entry(CCFsNode *n, CCFsEntry *e)
{
    e->name = n->name;
    e->type = n->type;
    e->size = n->size;
    e->ctime = n->ctime;
    e->mtime = n->mtime;
    e->readonly = fs_is_protected(idx_of(n));
    e->crc32 = n->crc32;
}

static CCINT32 fs_list_cb(void *data, void *user)
{
    CCFsListCtx *c = (CCFsListCtx *)user;
    CCFsNode *n = (CCFsNode *)data;
    if (!n || n->type == CC_FS_TYPE_NONE || !n->name)
        return 0;
    CCFsEntry e;
    fs_make_entry(n, &e);
    return c->cbk(&e, c->user);
}

CCINT32 ccfiles_list(CCFiles *fs, const char *v_path, CCDataCallback cbk, void *user)
{
    if (!fs || !v_path || !cbk)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (node_at(idx)->type != CC_FS_TYPE_DIR)
        return CC_FS_ERR_NOTDIR;

    CCUINT32 root_idx = node_at(idx)->children_root;
    if (root_idx == CC_FS_NIL)
        return CC_FS_OK;

    CCFsListCtx ctx;
    ctx.cbk = cbk;
    ctx.user = user;
    return cc_rbt_core_inorder_tranverse(&fs_tree_ops, node_at(root_idx), &ctx, fs_list_cb);
}

/* 递归检索整棵子树；用显式栈而不是 C 递归，避免深目录耗尽调用栈。 */
static CCINT32 fs_find_descendants(CCUINT32 root_idx, const char *pattern,
                                   CCDataCallback cbk, void *user)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCDyn kids;
    memset(&kids, 0, sizeof(kids));
    CCINT32 ret = CC_FS_OK;

    if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCUINT32),
                         (CCINT32)sizeof(CCINT32), &root_idx) != 0)
    {
        ret = CC_FS_ERR_NOSPACE;
        goto Done;
    }

    while (stack.size >= (CCUINT32)sizeof(CCUINT32))
    {
        CCUINT32 cur = CC_FS_NIL;
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCUINT32),
                            (CCINT32)sizeof(CCINT32), &cur) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto Done;
        }

        kids.size = 0;
        ret = fs_collect_children_ex(cur, &kids);
        if (ret != CC_FS_OK)
            goto Done;

        CCUINT32 count = kids.size / (CCUINT32)sizeof(CCUINT32);
        CCUINT32 *arr = (CCUINT32 *)kids.array;
        for (CCUINT32 i = 0; i < count; i++)
        {
            CCFsNode *n = node_at(arr[i]);
            if (n->type == CC_FS_TYPE_NONE || !n->name)
                continue;

            if (cc_wildcard(pattern, n->name))
            {
                CCFsEntry e;
                fs_make_entry(n, &e);
                CCINT32 cb_ret = cbk(&e, user);
                if (cb_ret != 0)
                {
                    ret = cb_ret;
                    goto Done;
                }
            }

            if (n->type == CC_FS_TYPE_DIR)
            {
                if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCUINT32),
                                     (CCINT32)sizeof(CCINT32), &arr[i]) != 0)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto Done;
                }
            }
        }
    }

Done:
    if (kids.array)
        ccalloc(kids.array, 0, NULL);
    if (stack.array)
        ccalloc(stack.array, 0, NULL);
    return ret;
}

CCINT32 ccfiles_find(CCFiles *fs, const char *v_path, const char *pattern,
                     CCDataCallback cbk, void *user)
{
    if (!fs || !v_path || !pattern || !cbk)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (node_at(idx)->type != CC_FS_TYPE_DIR)
        return CC_FS_ERR_NOTDIR;

    if (node_at(idx)->children_root == CC_FS_NIL)
        return CC_FS_OK;

    return fs_find_descendants(idx, pattern, cbk, user);
}
