/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 红黑树内核实现。仅负责红黑树形态逻辑，
 *               所有节点访问（结构、颜色、旋转、比较、拷贝）均通过 CC_RbtOps 提供的
 *               接口完成，内核不感知具体节点结构，以实现高度解耦。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "crystal_std_inner_header.h"

/*
 * ============================================================================
 * 内部辅助：全部经由 CC_RbtOps 访问节点，内核不感知节点内存布局。
 * ============================================================================
 */

// 取父节点（节点为空返回 NULL）
static inline void* node_parent(CC_RbtOps *ops, void *node)
{
    return node ? ops->get_node(ops->user, node, CC_RBT_REL_PARENT) : NULL;
}

// 取左子节点
static inline void* node_left(CC_RbtOps *ops, void *node)
{
    return node ? ops->get_node(ops->user, node, CC_RBT_REL_LEFT) : NULL;
}

// 取右子节点
static inline void* node_right(CC_RbtOps *ops, void *node)
{
    return node ? ops->get_node(ops->user, node, CC_RBT_REL_RIGHT) : NULL;
}

// 节点是否为红（空节点视为黑）
static inline CCBOOL node_is_red(CC_RbtOps *ops, void *node)
{
    return node ? ops->get_color(ops->user, node) : CCFALSE;
}

// 建立 parent 与 child 在 side 槽位上的双向挂载（child 可为 NULL）
static inline void node_link(CC_RbtOps *ops, void *parent, void *child, CC_RbtRel side)
{
    if (parent)
        ops->set_node(ops->user, parent, child, side);
    if (child)
        ops->set_node(ops->user, child, parent, CC_RBT_REL_PARENT);
}

// 左旋，并在必要时通过 ch_root 同步根节点。实际指针重排委托 ops->left_rotate
static inline void rotate_left(CC_RbtOps *ops, void **root, void *node)
{
    CCBOOL was_root = (node_parent(ops, node) == NULL);
    ops->left_rotate(ops->user, node);
    if (was_root)
    {
        *root = node_parent(ops, node);
        if (ops->ch_root)
            ops->ch_root(ops->user, *root);
    }
}

// 右旋，并在必要时通过 ch_root 同步根节点。实际指针重排委托 ops->right_rotate
static inline void rotate_right(CC_RbtOps *ops, void **root, void *node)
{
    CCBOOL was_root = (node_parent(ops, node) == NULL);
    ops->right_rotate(ops->user, node);
    if (was_root)
    {
        *root = node_parent(ops, node);
        if (ops->ch_root)
            ops->ch_root(ops->user, *root);
    }
}

// 中序遍历递归体；回调返回非 0 时中断并透传
static inline CCINT32 inorder_rec(CC_RbtOps *ops, void *node, void *user, CCDataCallback cbk)
{
    if (!node)
        return 0;
    CCINT32 ret = inorder_rec(ops, node_left(ops, node), user, cbk);
    if (ret)
        return ret;
    ret = cbk(node, user);
    if (ret)
        return ret;
    return inorder_rec(ops, node_right(ops, node), user, cbk);
}

// 按 key 做二叉搜索
static inline void* find_node(CC_RbtOps *ops, void *root, CCUINTPTR key)
{
    void *cursor = root;
    while (cursor)
    {
        CCINT32 cmp = ops->cmpare_key(ops->user, cursor, key);
        if (cmp == 0)
            return cursor;
        cursor = (cmp > 0) ? node_left(ops, cursor) : node_right(ops, cursor);
    }
    return NULL;
}

// 插入后修复红黑性质
static inline void insert_fixup(CC_RbtOps *ops, void **root, void *node)
{
    void *parent;
    while ((parent = node_parent(ops, node)) != NULL && node_is_red(ops, parent))
    {
        void *grand = node_parent(ops, parent);
        if (!grand)
            break;
        if (node_left(ops, grand) == parent)
        {
            void *uncle = node_right(ops, grand);
            if (node_is_red(ops, uncle))
            {
                ops->set_color(ops->user, parent, CCFALSE);
                ops->set_color(ops->user, uncle, CCFALSE);
                ops->set_color(ops->user, grand, CCTRUE);
                node = grand;
            }
            else
            {
                if (node_right(ops, parent) == node)
                {
                    node = parent;
                    rotate_left(ops, root, node);
                    parent = node_parent(ops, node);
                    grand = node_parent(ops, parent);
                }
                ops->set_color(ops->user, parent, CCFALSE);
                ops->set_color(ops->user, grand, CCTRUE);
                rotate_right(ops, root, grand);
            }
        }
        else
        {
            void *uncle = node_left(ops, grand);
            if (node_is_red(ops, uncle))
            {
                ops->set_color(ops->user, parent, CCFALSE);
                ops->set_color(ops->user, uncle, CCFALSE);
                ops->set_color(ops->user, grand, CCTRUE);
                node = grand;
            }
            else
            {
                if (node_left(ops, parent) == node)
                {
                    node = parent;
                    rotate_right(ops, root, node);
                    parent = node_parent(ops, node);
                    grand = node_parent(ops, parent);
                }
                ops->set_color(ops->user, parent, CCFALSE);
                ops->set_color(ops->user, grand, CCTRUE);
                rotate_left(ops, root, grand);
            }
        }
    }
    if (*root)
        ops->set_color(ops->user, *root, CCFALSE);
}

// 用 replacement 顶替 node 的位置（node 至多一个非空子）
static inline void transplant(CC_RbtOps *ops, void **root, void *node, void *replacement)
{
    void *parent = node_parent(ops, node);
    if (!parent)
    {
        *root = replacement;
        if (replacement)
            ops->set_node(ops->user, replacement, NULL, CC_RBT_REL_PARENT);
        if (ops->ch_root)
            ops->ch_root(ops->user, replacement);
    }
    else if (node_left(ops, parent) == node)
    {
        node_link(ops, parent, replacement, CC_RBT_REL_LEFT);
    }
    else
    {
        node_link(ops, parent, replacement, CC_RBT_REL_RIGHT);
    }
}

// 删除后修复红黑性质；x 为顶替节点（可为 NULL），xp 为其父
static inline void remove_fixup(CC_RbtOps *ops, void **root, void *x, void *xp)
{
    while (x != *root && !node_is_red(ops, x))
    {
        if (!xp)
            break;
        if (node_left(ops, xp) == x)
        {
            void *w = node_right(ops, xp);
            if (node_is_red(ops, w))
            {
                ops->set_color(ops->user, w, CCFALSE);
                ops->set_color(ops->user, xp, CCTRUE);
                rotate_left(ops, root, xp);
                w = node_right(ops, xp);
            }
            if (!w)
                break;
            if (!node_is_red(ops, node_left(ops, w)) && !node_is_red(ops, node_right(ops, w)))
            {
                ops->set_color(ops->user, w, CCTRUE);
                x = xp;
                xp = node_parent(ops, x);
            }
            else
            {
                if (!node_is_red(ops, node_right(ops, w)))
                {
                    if (node_left(ops, w))
                        ops->set_color(ops->user, node_left(ops, w), CCFALSE);
                    ops->set_color(ops->user, w, CCTRUE);
                    rotate_right(ops, root, w);
                    w = node_right(ops, xp);
                }
                ops->set_color(ops->user, w, ops->get_color(ops->user, xp));
                ops->set_color(ops->user, xp, CCFALSE);
                if (node_right(ops, w))
                    ops->set_color(ops->user, node_right(ops, w), CCFALSE);
                rotate_left(ops, root, xp);
                x = *root;
                xp = NULL;
            }
        }
        else
        {
            void *w = node_left(ops, xp);
            if (node_is_red(ops, w))
            {
                ops->set_color(ops->user, w, CCFALSE);
                ops->set_color(ops->user, xp, CCTRUE);
                rotate_right(ops, root, xp);
                w = node_left(ops, xp);
            }
            if (!w)
                break;
            if (!node_is_red(ops, node_left(ops, w)) && !node_is_red(ops, node_right(ops, w)))
            {
                ops->set_color(ops->user, w, CCTRUE);
                x = xp;
                xp = node_parent(ops, x);
            }
            else
            {
                if (!node_is_red(ops, node_left(ops, w)))
                {
                    if (node_right(ops, w))
                        ops->set_color(ops->user, node_right(ops, w), CCFALSE);
                    ops->set_color(ops->user, w, CCTRUE);
                    rotate_left(ops, root, w);
                    w = node_left(ops, xp);
                }
                ops->set_color(ops->user, w, ops->get_color(ops->user, xp));
                ops->set_color(ops->user, xp, CCFALSE);
                if (node_left(ops, w))
                    ops->set_color(ops->user, node_left(ops, w), CCFALSE);
                rotate_right(ops, root, xp);
                x = *root;
                xp = NULL;
            }
        }
    }
    if (x)
        ops->set_color(ops->user, x, CCFALSE);
}

/*
 * ============================================================================
 * 公开接口
 * ============================================================================
 */

CCINT32 cc_rbt_core_insert(CC_RbtOps *ops, void *root, void *node)
{
    if (!ops || !node)
        return -1;

    // 空树：新节点即为根，恒为黑
    if (!root)
    {
        ops->set_node(ops->user, node, NULL, CC_RBT_REL_LEFT);
        ops->set_node(ops->user, node, NULL, CC_RBT_REL_RIGHT);
        ops->set_node(ops->user, node, NULL, CC_RBT_REL_PARENT);
        ops->set_color(ops->user, node, CCFALSE);
        if (ops->ch_root)
            ops->ch_root(ops->user, node);
        return 0;
    }

    // 定位插入点
    void *cursor = root;
    void *parent = NULL;
    CC_RbtRel side = CC_RBT_REL_LEFT;
    while (cursor)
    {
        CCINT32 cmp = ops->cmpare_node(ops->user, cursor, node);
        parent = cursor;
        if (cmp == 0)
            return 1; // 键值重复
        if (cmp > 0)
        {
            cursor = node_left(ops, cursor);
            side = CC_RBT_REL_LEFT;
        }
        else
        {
            cursor = node_right(ops, cursor);
            side = CC_RBT_REL_RIGHT;
        }
    }

    // 挂载并染红
    ops->set_node(ops->user, node, NULL, CC_RBT_REL_LEFT);
    ops->set_node(ops->user, node, NULL, CC_RBT_REL_RIGHT);
    node_link(ops, parent, node, side);
    ops->set_color(ops->user, node, CCTRUE);

    insert_fixup(ops, &root, node);
    return 0;
}

/*
 * 当被删节点拥有两个子节点时，经 find_instead 取得替换节点，
 * 用 copy_node 将其数据拷入被删节点，再物理摘除替换节点。
 * 返回值为「物理上被摘除的节点」，调用方应对其进行回收；
 * 未找到对应 key 时返回 NULL。
 */
void* cc_rbt_core_remove(CC_RbtOps *ops, void *root, CCUINTPTR key)
{
    if (!ops)
        return NULL;

    void *z = find_node(ops, root, key);
    if (!z)
        return NULL;

    // y 为最终被物理摘除的节点
    void *y = z;
    if (node_left(ops, z) && node_right(ops, z))
    {
        void *instead = ops->find_instead ? ops->find_instead(ops->user, z) : NULL;
        if (!instead)
            return NULL;
        if (!ops->copy_node || ops->copy_node(ops->user, z, instead) != 0)
            return NULL;
        y = instead;
    }

    // y 至多拥有一个非空子节点
    void *child = node_left(ops, y);
    if (!child)
        child = node_right(ops, y);
    CCBOOL y_red = ops->get_color(ops->user, y);
    void *y_parent = node_parent(ops, y);

    transplant(ops, &root, y, child);
    if (!y_red)
        remove_fixup(ops, &root, child, y_parent);

    // 摘除后的节点不应再指向树内数据
    ops->set_node(ops->user, y, NULL, CC_RBT_REL_LEFT);
    ops->set_node(ops->user, y, NULL, CC_RBT_REL_RIGHT);
    ops->set_node(ops->user, y, NULL, CC_RBT_REL_PARENT);
    return y;
}

void* cc_rbt_core_get(CC_RbtOps *ops, void *root, CCUINTPTR key)
{
    if (!ops)
        return NULL;
    return find_node(ops, root, key);
}

CCINT32 cc_rbt_core_inorder_tranverse(CC_RbtOps *ops, void *root, void *user, CCDataCallback cbk)
{
    if (!ops || !cbk)
        return -1;
    return inorder_rec(ops, root, user, cbk);
}