/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles shell 式操作：mkdir / rm / mv / cp 及跨锚点原子搬移。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>

/* ============================================================================
 * shell 式文件操作
 * ========================================================================== */

CCINT32 ccfiles_create_dir(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    CCUINT32 parent = CC_FS_NIL;
    CCUINT32 idx = CC_FS_NIL;
    CCDyn leaf;
    memset(&leaf, 0, sizeof(leaf));
    CCINT32 r = fs_vwalk(fs, v_path, CCFALSE, &parent, &leaf, &idx);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        return r;
    }

    /* 已存在（文件或目录）即冲突，对齐 shell mkdir */
    if (idx != CC_FS_NIL)
    {
        fs_buf_free(&leaf);
        return CC_FS_ERR_EXIST;
    }

    const char *lf = fs_buf_str(&leaf);
    if (parent == CC_FS_NIL || lf[0] == '\0')
    {
        fs_buf_free(&leaf);
        return CC_FS_ERR_PARAM;
    }
    if (fs_is_protected(parent))
    {
        fs_buf_free(&leaf);
        return CC_FS_ERR_PROTECT;
    }

    idx = fs_create_node(parent, lf, CC_FS_TYPE_DIR, NULL, 0, FS_ORIGIN_MEM);
    if (idx == CC_FS_NIL)
    {
        fs_buf_free(&leaf);
        return CC_FS_ERR_NOSPACE;
    }

    /* 立即创建物理目录（含缺失的上级）；失败则回滚虚拟节点，不留下假象 */
    char *phys = fs_physical_dup(idx, CC_DBG_REL_TAG("目录物理路径", NULL));
    CCINT32 mr = phys ? fs_mkdirs(phys) : CC_FS_ERR_NOSPACE;
    if (phys)
        ccalloc(phys, 0, NULL);
    if (mr != CC_FS_OK)
    {
        fs_mount_free(node_at(idx)->mount);
        if (node_at(idx)->name)
            ccalloc(node_at(idx)->name, 0, NULL);
        fs_alloc_rollback();
        fs_buf_free(&leaf);
        return mr;
    }

    if (fs_insert_child(parent, idx) != 0)
    {
        fs_mount_free(node_at(idx)->mount);
        if (node_at(idx)->name)
            ccalloc(node_at(idx)->name, 0, NULL);
        fs_alloc_rollback();
        fs_buf_free(&leaf);
        return CC_FS_ERR_IO;
    }
    fs_buf_free(&leaf);
    return CC_FS_OK;
}

CCINT32 ccfiles_create_file(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    CCUINT32 parent = CC_FS_NIL;
    CCUINT32 idx = CC_FS_NIL;
    CCDyn leaf;
    memset(&leaf, 0, sizeof(leaf));
    CCINT32 r = fs_vwalk(fs, v_path, CCFALSE, &parent, &leaf, &idx);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        return r;
    }

    if (idx != CC_FS_NIL)
    {
        if (node_at(idx)->type != CC_FS_TYPE_FILE)
        {
            fs_buf_free(&leaf);
            return CC_FS_ERR_ISDIR;
        }
    }
    else
    {
        const char *lf = fs_buf_str(&leaf);
        if (parent == CC_FS_NIL || lf[0] == '\0')
        {
            fs_buf_free(&leaf);
            return CC_FS_ERR_PARAM;
        }
        if (fs_is_protected(parent))
        {
            fs_buf_free(&leaf);
            return CC_FS_ERR_PROTECT;
        }
        idx = fs_create_node(parent, lf, CC_FS_TYPE_FILE, NULL, 0, FS_ORIGIN_MEM);
        if (idx == CC_FS_NIL)
        {
            fs_buf_free(&leaf);
            return CC_FS_ERR_NOSPACE;
        }

        /* 立即创建物理文件（连同上级目录）；"ab" 保证同名已存在文件不被截断 */
        char *pp = fs_physical_dup(parent, CC_DBG_REL_TAG("父目录物理路径", NULL));
        CCINT32 mr = pp ? fs_mkdirs(pp) : CC_FS_ERR_NOSPACE;
        if (pp)
            ccalloc(pp, 0, NULL);

        FILE *nf = NULL;
        char *phys = NULL;
        if (mr == CC_FS_OK)
        {
            phys = fs_physical_dup(idx, CC_DBG_REL_TAG("文件物理路径", NULL));
            if (!phys)
                mr = CC_FS_ERR_NOSPACE;
            else
            {
                nf = fopen(phys, "ab");
                if (!nf)
                    mr = CC_FS_ERR_IO;
            }
        }
        if (nf)
            fclose(nf);
        if (phys)
            ccalloc(phys, 0, NULL);

        if (mr != CC_FS_OK)
        {
            fs_mount_free(node_at(idx)->mount);
            if (node_at(idx)->name)
                ccalloc(node_at(idx)->name, 0, NULL);
            fs_alloc_rollback();
            fs_buf_free(&leaf);
            return mr;
        }

        if (fs_insert_child(parent, idx) != 0)
        {
            fs_mount_free(node_at(idx)->mount);
            if (node_at(idx)->name)
                ccalloc(node_at(idx)->name, 0, NULL);
            fs_alloc_rollback();
            fs_buf_free(&leaf);
            return CC_FS_ERR_IO;
        }
    }

    fs_buf_free(&leaf);
    if (fs->cur != CC_FS_NIL)
        fs_node_unref(fs);
    fs->cur = idx;
    fs_node_ref(fs, idx);
    return CC_FS_OK;
}

CCINT32 ccfiles_remove(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    /* 结构操作前先关闭 / 落盘所有常驻句柄（Windows 下打开中的文件删不掉） */
    fs_fh_close_all();
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (idx == ccfiles_var.root)
        return CC_FS_ERR_PERM;
    if (fs_is_protected(idx))
        return CC_FS_ERR_PROTECT;

    CCFsNode *n = node_at(idx);
    if (n->type == CC_FS_TYPE_DIR && n->children_root != CC_FS_NIL)
        return CC_FS_ERR_NOTEMPTY;

    CCUINT32 parent = n->fs_parent;
    if (parent == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    /* 立即删除物理实体；失败则保留虚拟节点，绝不返回假成功 */
    CCINT32 pr = fs_physical_delete(idx);
    if (pr != CC_FS_OK)
        return pr;

    /* fs_remove_child 内部可能改写节点 name，先做副本；探针失效在其中统一处理 */
    char *nm = fs_strdup(n->name, CC_DBG_REL_TAG("删除节点名称", NULL));
    if (!nm)
        return CC_FS_ERR_NOSPACE;
    CCINT32 ret = fs_remove_child(parent, nm);
    ccalloc(nm, 0, NULL);
    return ret;
}

/* rm -rf：递归删除任意子树，物理删除即刻生效；路径不存在返回 OK（-f） */
CCINT32 ccfiles_remove_recursive(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    /* 结构操作前先关闭 / 落盘所有常驻句柄 */
    fs_fh_close_all();
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r == CC_FS_ERR_NOENT)
        return CC_FS_OK; /* -f：不存在也算成功 */
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_OK;

    if (idx != ccfiles_var.root && fs_is_protected(idx))
        return CC_FS_ERR_PROTECT;

    /* 探针失效由 fs_delete_subtree_ex → fs_remove_child 内的 fs_invalidate_probes 统一处理 */

    if (idx == ccfiles_var.root)
    {
        /* rm -rf /：一次性收集并删除根的所有后代（物理即刻删除），
           保留根节点与根锚点目录本身；避免逐子树删除时 swap-remove 相互搬移下标 */
        return fs_delete_children(idx, CCTRUE);
    }

    return fs_delete_subtree_ex(idx, CCTRUE);
}

/* 确保节点的物理文件存在（含上级目录）；已存在则保持内容不变。
   cp / mv 的目标节点是纯虚拟新建，需要先落一个空文件才能打开写句柄。 */
static CCINT32 fs_ensure_physical_file(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (n->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;
    if (fs_is_protected(idx))
        return CC_FS_ERR_PROTECT;

    char *phys = fs_physical_dup(idx, CC_DBG_REL_TAG("文件物理路径", NULL));
    if (!phys)
        return CC_FS_ERR_NOSPACE;

    if (fs_file_exists(phys))
    {
        ccalloc(phys, 0, NULL);
        return CC_FS_OK;
    }

    char *pp = fs_strdup(phys, CC_DBG_REL_TAG("父目录物理路径", NULL));
    CCINT32 ret = CC_FS_OK;
    if (!pp)
        ret = CC_FS_ERR_NOSPACE;
    else
    {
        fs_dir_of(pp);
        if (pp[0] != '\0')
            ret = fs_mkdirs(pp);
        ccalloc(pp, 0, NULL);
    }
    if (ret == CC_FS_OK)
    {
        FILE *fp = fopen(phys, "ab"); /* 不截断：只保证存在 */
        if (!fp)
            ret = CC_FS_ERR_IO;
        else
            fclose(fp);
    }
    ccalloc(phys, 0, NULL);
    return ret;
}

/* 复制文件内容：逐块读源、直写目标；目标旧内容先截断，避免残留尾部 */
static CCINT32 fs_copy_file(CCUINT32 src_idx, CCUINT32 dst_idx)
{
    CCUINT64 total = node_at(src_idx)->size;

    /* 源实物必须先可读：否则下面截断目标后才发现失败，会丢掉目标旧数据。
       包内文件走 pack 偏移读取，没有独立物理路径，跳过此检查。 */
    CCUINT32 src_anchor = fs_find_anchor(src_idx);
    CCBOOL need_phys = (src_anchor != CC_FS_NIL && node_at(src_anchor)->mount &&
                        node_at(src_anchor)->mount->origin != FS_ORIGIN_PKG);
    if (need_phys)
    {
        char *sp = fs_physical_dup(src_idx, CC_DBG_REL_TAG("复制源物理路径", NULL));
        if (!sp)
            return CC_FS_ERR_NOSPACE;
        CCBOOL src_ok = fs_file_exists(sp);
        ccalloc(sp, 0, NULL);
        if (!src_ok)
            return CC_FS_ERR_NOENT;
    }

    CCINT32 r = fs_ensure_physical_file(dst_idx);
    if (r != CC_FS_OK)
        return r;
    r = fs_fh_open(dst_idx, CCTRUE);
    if (r != CC_FS_OK)
        return r;
    {
        CCFsNode *dn = node_at(dst_idx);
        if (fs_truncate((FILE *)dn->fp, 0) != 0)
            return CC_FS_ERR_IO;
        dn->fp_pos = 0;
        dn->size = 0;
    }

    CCUINT8 tmp[64 * 1024];
    CCUINT64 pos = 0;
    while (pos < total)
    {
        CCUINT64 n = total - pos;
        if (n > sizeof(tmp))
            n = sizeof(tmp);
        CCINT64 got = fs_read_raw(src_idx, tmp, pos, n);
        if (got < 0)
            return (CCINT32)got;
        CCINT64 put = fs_write_raw(dst_idx, tmp, pos, n);
        if (put < 0)
            return (CCINT32)put;
        pos += n;
    }

    node_at(dst_idx)->crc32 = node_at(src_idx)->crc32;
    node_at(dst_idx)->mtime = fs_now_seconds();
    /* 复制目标通常没有探针指向：立即落盘并关闭；源句柄同样按需释放 */
    CCINT32 fr = fs_fh_close(dst_idx);
    fs_fh_release_if_unused(src_idx);
    return fr;
}

/* 递归复制：把 src 复制为 parent 下名为 name 的节点 */
/* 复制单个节点（不处理其子节点）；成功时 out_dst 为目标下标 */
static CCINT32 fs_copy_one(CCUINT32 src_idx, CCUINT32 parent_idx,
                           const char *name, CCUINT32 *out_dst)
{
    *out_dst = CC_FS_NIL;
    CCBOOL src_is_dir = (node_at(src_idx)->type == CC_FS_TYPE_DIR);
    CCUINT32 dst = fs_child_get_idx(parent_idx, name);

    if (src_is_dir)
    {
        if (dst != CC_FS_NIL && node_at(dst)->type != CC_FS_TYPE_DIR)
            return CC_FS_ERR_EXIST;
        if (dst == CC_FS_NIL)
        {
            dst = fs_create_node(parent_idx, name, CC_FS_TYPE_DIR, NULL, 0, FS_ORIGIN_MEM);
            if (dst == CC_FS_NIL)
                return CC_FS_ERR_NOSPACE;
            if (fs_insert_child(parent_idx, dst) != 0)
            {
                fs_mount_free(node_at(dst)->mount);
                if (node_at(dst)->name)
                    ccalloc(node_at(dst)->name, 0, NULL);
                fs_alloc_rollback();
                return CC_FS_ERR_IO;
            }
        }

        /* 目录同样立即落盘（空目录也必须真实存在） */
        char *dphys = fs_physical_dup(dst, CC_DBG_REL_TAG("目录物理路径", NULL));
        if (!dphys)
            return CC_FS_ERR_NOSPACE;
        CCINT32 dr = fs_mkdirs(dphys);
        ccalloc(dphys, 0, NULL);
        if (dr != CC_FS_OK)
            return dr;
    }
    else
    {
        if (dst != CC_FS_NIL && node_at(dst)->type != CC_FS_TYPE_FILE)
            return CC_FS_ERR_ISDIR;
        if (dst == CC_FS_NIL)
        {
            dst = fs_create_node(parent_idx, name, CC_FS_TYPE_FILE, NULL, 0, FS_ORIGIN_MEM);
            if (dst == CC_FS_NIL)
                return CC_FS_ERR_NOSPACE;
            if (fs_insert_child(parent_idx, dst) != 0)
            {
                fs_mount_free(node_at(dst)->mount);
                if (node_at(dst)->name)
                    ccalloc(node_at(dst)->name, 0, NULL);
                fs_alloc_rollback();
                return CC_FS_ERR_IO;
            }
        }
        CCINT32 r = fs_copy_file(src_idx, dst);
        if (r != CC_FS_OK)
            return r;
    }

    *out_dst = dst;
    return CC_FS_OK;
}

typedef struct cc_fs_copy_item
{
    CCUINT32 src;
    CCUINT32 parent;
    char *name;
} CCFsCopyItem;

/* 复制整棵子树为 parent 下名为 name 的节点；显式栈迭代，无递归 */
static CCINT32 fs_copy_tree(CCUINT32 src_idx, CCUINT32 parent_idx, const char *name)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));

    char *root_name = fs_strdup(name, CC_DBG_REL_TAG("复制节点名称", NULL));
    if (!root_name)
        return CC_FS_ERR_NOSPACE;
    CCFsCopyItem item;
    item.src = src_idx;
    item.parent = parent_idx;
    item.name = root_name;
    if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCFsCopyItem),
                         (CCINT32)sizeof(CCFsCopyItem), &item) != 0)
    {
        ccalloc(root_name, 0, NULL);
        fs_buf_free(&stack);
        return CC_FS_ERR_NOSPACE;
    }

    CCINT32 ret = CC_FS_OK;
    while (stack.size >= (CCUINT32)sizeof(CCFsCopyItem))
    {
        CCFsCopyItem cur;
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCFsCopyItem),
                            (CCINT32)sizeof(CCFsCopyItem), &cur) != 0)
            break;

        CCUINT32 dst = CC_FS_NIL;
        ret = fs_copy_one(cur.src, cur.parent, cur.name, &dst);
        ccalloc(cur.name, 0, NULL);
        if (ret != CC_FS_OK)
            break;

        if (node_at(cur.src)->type != CC_FS_TYPE_DIR)
            continue;

        CCDyn kids;
        memset(&kids, 0, sizeof(kids));
        fs_collect_children(cur.src, &kids);
        CCUINT32 count = kids.size / (CCUINT32)sizeof(CCUINT32);
        CCUINT32 *arr = (CCUINT32 *)kids.array;
        for (CCUINT32 i = 0; i < count; i++)
        {
            CCUINT32 kid = arr[i];
            char *kn = fs_strdup(node_at(kid)->name,
                                 CC_DBG_REL_TAG("复制节点名称", NULL));
            if (!kn)
            {
                ret = CC_FS_ERR_NOSPACE;
                break;
            }
            CCFsCopyItem child;
            child.src = kid;
            child.parent = dst;
            child.name = kn;
            if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCFsCopyItem),
                                 (CCINT32)sizeof(CCFsCopyItem), &child) != 0)
            {
                ccalloc(kn, 0, NULL);
                ret = CC_FS_ERR_NOSPACE;
                break;
            }
        }
        if (kids.array)
            ccalloc(kids.array, 0, NULL);
        if (ret != CC_FS_OK)
            break;
    }

    fs_buf_free(&stack);
    return ret;
}

/* 计算 cp / mv 的目标父目录与名称 */
static CCINT32 fs_target_of(CCUINT32 src_idx, CCUINT32 dst_idx, CCUINT32 dst_parent,
                            const char *leaf, CCUINT32 *out_parent, CCDyn *out_name)
{
    const char *name = leaf;
    CCUINT32 parent = dst_parent;

    if (dst_idx != CC_FS_NIL)
    {
        CCFsNode *d = node_at(dst_idx);
        if (d->type == CC_FS_TYPE_DIR)
        {
            parent = dst_idx;
            name = node_at(src_idx)->name;
        }
        else
        {
            parent = d->fs_parent;
            name = d->name;
        }
    }

    if (parent == CC_FS_NIL || !name || name[0] == '\0' || !out_name)
        return CC_FS_ERR_PARAM;
    fs_buf_clear(out_name);
    CCINT32 r = fs_buf_put(out_name, name);
    if (r != CC_FS_OK)
        return r;
    *out_parent = parent;
    return CC_FS_OK;
}

/* 目标父目录是否位于 ancestor 子树内部（禁止把目录复制进自身） */
static CCBOOL fs_is_inside(CCUINT32 node_idx, CCUINT32 ancestor_idx)
{
    CCUINT32 cur = node_idx;
    while (cur != CC_FS_NIL)
    {
        if (cur == ancestor_idx)
            return CCTRUE;
        cur = node_at(cur)->fs_parent;
    }
    return CCFALSE;
}

/* 同锚点原子移动：物理 rename，虚拟层重建目标投影。
   前置条件由调用方保证：源为磁盘投影节点、目标父目录物理存在、同锚点、均非保护。 */
static CCINT32 fs_mv_atomic(CCUINT32 src_idx, CCUINT32 parent, const char *name)
{
    CCFsNode *src = node_at(src_idx);
    CCBOOL src_is_dir = (src->type == CC_FS_TYPE_DIR);

    /* 物理身份即将改变（含子树内所有文件）：先落盘并关闭全部常驻句柄 */
    fs_fh_close_all();

    char *src_phys = fs_physical_dup(src_idx, CC_DBG_REL_TAG("移动源路径", NULL));
    char *parent_phys = fs_physical_dup(parent, CC_DBG_REL_TAG("移动目标目录", NULL));
    if (!src_phys || !parent_phys)
    {
        if (src_phys)
            ccalloc(src_phys, 0, NULL);
        if (parent_phys)
            ccalloc(parent_phys, 0, NULL);
        return CC_FS_ERR_NOSPACE;
    }

    CCDyn dstp;
    memset(&dstp, 0, sizeof(dstp));
    if (fs_buf_put(&dstp, parent_phys) != CC_FS_OK ||
        fs_buf_put(&dstp, "/") != CC_FS_OK ||
        fs_buf_put(&dstp, name) != CC_FS_OK)
    {
        ccalloc(src_phys, 0, NULL);
        ccalloc(parent_phys, 0, NULL);
        fs_buf_free(&dstp);
        return CC_FS_ERR_NOSPACE;
    }
    const char *dstp_str = fs_buf_str(&dstp);
    CCINT32 r = CC_FS_OK;

    /* 源实物已不存在时不要先动目标，否则 rename 失败会直接丢掉目标数据 */
    if (!fs_file_exists(src_phys) && !fs_is_dir_path(src_phys) &&
        !fs_is_link_path(src_phys))
    {
        ccalloc(src_phys, 0, NULL);
        ccalloc(parent_phys, 0, NULL);
        fs_buf_free(&dstp);
        return CC_FS_ERR_NOENT;
    }

    /* 覆盖目标时先改名到同目录备份；源 rename 失败则用备份恢复目标 */
    CCDyn bak;
    memset(&bak, 0, sizeof(bak));
    CCBOOL use_bak = CCFALSE;
    if (fs_file_exists(dstp_str) || fs_is_dir_path(dstp_str) ||
        fs_is_link_path(dstp_str))
    {
        CCBOOL got_bak = CCFALSE;
        for (CCUINT32 n = 0; n < 1000; n++)
        {
            char suffix[32];
            snprintf(suffix, sizeof(suffix), ".ccmv%u", (unsigned)n);
            fs_buf_clear(&bak);
            if (fs_buf_put(&bak, dstp_str) != CC_FS_OK ||
                fs_buf_put(&bak, suffix) != CC_FS_OK)
            {
                ccalloc(src_phys, 0, NULL);
                ccalloc(parent_phys, 0, NULL);
                fs_buf_free(&bak);
                fs_buf_free(&dstp);
                return CC_FS_ERR_NOSPACE;
            }
            const char *bak_str = fs_buf_str(&bak);
            if (!fs_file_exists(bak_str) && !fs_is_dir_path(bak_str) &&
                !fs_is_link_path(bak_str))
            {
                got_bak = CCTRUE;
                break;
            }
        }
        if (!got_bak)
        {
            ccalloc(src_phys, 0, NULL);
            ccalloc(parent_phys, 0, NULL);
            fs_buf_free(&bak);
            fs_buf_free(&dstp);
            return CC_FS_ERR_IO;
        }

        r = fs_rename_path(dstp_str, fs_buf_str(&bak));
        if (r != CC_FS_OK)
        {
            ccalloc(src_phys, 0, NULL);
            ccalloc(parent_phys, 0, NULL);
            fs_buf_free(&bak);
            fs_buf_free(&dstp);
            return r;
        }
        use_bak = CCTRUE;
    }

    r = fs_rename_path(src_phys, dstp_str);
    if (r != CC_FS_OK)
    {
        /* 恢复旧目标；恢复也失败时保留备份，至少不丢数据 */
        if (use_bak &&
            fs_rename_path(fs_buf_str(&bak), dstp_str) != CC_FS_OK)
            r = CC_FS_ERR_IO;
        ccalloc(src_phys, 0, NULL);
        ccalloc(parent_phys, 0, NULL);
        fs_buf_free(&bak);
        fs_buf_free(&dstp);
        return r;
    }

    /* 新目标已就位，旧目标备份只做尽力清理；失败不影响本次移动结果 */
    if (use_bak)
        (void)fs_remove_tree(fs_buf_str(&bak));
    fs_buf_free(&bak);
    ccalloc(src_phys, 0, NULL);
    ccalloc(parent_phys, 0, NULL);

    /* 虚拟层：删除源虚拟节点（不删物理，物理已 rename 走）；删除旧目标虚拟；重建投影 */
    fs_delete_subtree(src_idx);

    CCUINT32 existing = fs_child_get_idx(parent, name);
    if (existing != CC_FS_NIL)
        fs_remove_child(parent, name);

    CCUINT32 dst = fs_create_node(parent, name,
                                  src_is_dir ? CC_FS_TYPE_DIR : CC_FS_TYPE_FILE, NULL, 0, FS_ORIGIN_DISK);
    if (dst == CC_FS_NIL)
    {
        fs_buf_free(&dstp);
        return CC_FS_ERR_NOSPACE;
    }

    CCFsNode *dn = node_at(dst);
    dn->mtime = src_is_dir ? fs_now_seconds() : fs_file_mtime(dstp_str);
    if (dn->ctime == 0)
        dn->ctime = dn->mtime;
    if (!src_is_dir)
        dn->size = fs_file_size(dstp_str);

    if (fs_insert_child(parent, dst) != 0)
    {
        fs_mount_free(node_at(dst)->mount);
        if (node_at(dst)->name)
            ccalloc(node_at(dst)->name, 0, NULL);
        fs_alloc_rollback();
        fs_buf_free(&dstp);
        return CC_FS_ERR_IO;
    }

    if (src_is_dir)
    {
        char *np = fs_physical_dup(dst, CC_DBG_REL_TAG("目录投影", NULL));
        if (np)
        {
            (void)fs_scan_dir_into(dst, np);
            ccalloc(np, 0, NULL);
        }
    }

    fs_buf_free(&dstp);
    return CC_FS_OK;
}

CCINT32 ccfiles_cp(CCFiles *fs, const char *src, const char *dst)
{
    if (!fs || !src || !dst)
        return CC_FS_ERR_PARAM;

    CCUINT32 src_idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, src, CCTRUE, NULL, NULL, &src_idx);
    if (r != CC_FS_OK)
        return r;
    if (src_idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    CCUINT32 dst_parent = CC_FS_NIL;
    CCUINT32 dst_idx = CC_FS_NIL;
    CCDyn leaf;
    memset(&leaf, 0, sizeof(leaf));
    r = fs_vwalk(fs, dst, CCFALSE, &dst_parent, &leaf, &dst_idx);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        return r;
    }

    CCUINT32 parent = CC_FS_NIL;
    CCDyn name;
    memset(&name, 0, sizeof(name));
    r = fs_target_of(src_idx, dst_idx, dst_parent, fs_buf_str(&leaf), &parent, &name);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return r;
    }

    const char *nm = fs_buf_str(&name);
    if (fs_is_protected(parent))
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_ERR_PROTECT;
    }
    if (node_at(src_idx)->type == CC_FS_TYPE_DIR && fs_is_inside(parent, src_idx))
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_ERR_PARAM;
    }

    if (parent == node_at(src_idx)->fs_parent &&
        strcmp(nm, node_at(src_idx)->name) == 0)
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_OK;
    }

    r = fs_copy_tree(src_idx, parent, nm);
    fs_buf_free(&leaf);
    fs_buf_free(&name);
    return r;
}

CCINT32 ccfiles_mv(CCFiles *fs, const char *src, const char *dst)
{
    if (!fs || !src || !dst)
        return CC_FS_ERR_PARAM;
    /* 物理身份即将改变：先落盘并关闭全部常驻句柄 */
    fs_fh_close_all();

    CCUINT32 src_idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, src, CCTRUE, NULL, NULL, &src_idx);
    if (r != CC_FS_OK)
        return r;
    if (src_idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (src_idx == ccfiles_var.root)
        return CC_FS_ERR_PERM;
    if (fs_is_protected(src_idx))
        return CC_FS_ERR_PROTECT;

    CCUINT32 dst_parent = CC_FS_NIL;
    CCUINT32 dst_idx = CC_FS_NIL;
    CCDyn leaf;
    memset(&leaf, 0, sizeof(leaf));
    r = fs_vwalk(fs, dst, CCFALSE, &dst_parent, &leaf, &dst_idx);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        return r;
    }

    CCUINT32 parent = CC_FS_NIL;
    CCDyn name;
    memset(&name, 0, sizeof(name));
    r = fs_target_of(src_idx, dst_idx, dst_parent, fs_buf_str(&leaf), &parent, &name);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return r;
    }

    const char *nm = fs_buf_str(&name);
    if (fs_is_protected(parent))
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_ERR_PROTECT;
    }
    if (node_at(src_idx)->type == CC_FS_TYPE_DIR && fs_is_inside(parent, src_idx))
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_ERR_PARAM;
    }

    if (parent == node_at(src_idx)->fs_parent &&
        strcmp(nm, node_at(src_idx)->name) == 0)
    {
        fs_buf_free(&leaf);
        fs_buf_free(&name);
        return CC_FS_OK;
    }

    /* 同锚点 + 源为磁盘投影 + 目标父目录物理存在 → 原子 rename；否则 copy + 删源 */
    CCUINT32 src_anchor = fs_find_anchor(src_idx);
    CCUINT32 dst_anchor = fs_find_anchor(parent);
    CCBOOL atomic_ok = (node_at(src_idx)->mount &&
                        node_at(src_idx)->mount->origin == FS_ORIGIN_DISK &&
                        src_anchor != CC_FS_NIL && src_anchor == dst_anchor);
    if (atomic_ok)
    {
        char *pphys = fs_physical_dup(parent, CC_DBG_REL_TAG("移动目标目录", NULL));
        if (pphys)
        {
            atomic_ok = fs_is_dir_path(pphys);
            ccalloc(pphys, 0, NULL);
        }
        else
            atomic_ok = CCFALSE;
    }

    if (atomic_ok)
        r = fs_mv_atomic(src_idx, parent, nm);
    else
        r = fs_copy_tree(src_idx, parent, nm);

    fs_buf_free(&leaf);
    fs_buf_free(&name);
    if (r != CC_FS_OK)
        return r;

    /* 探针失效由删源路径（fs_delete_subtree_ex / fs_mv_atomic 的 fs_delete_subtree）统一处理 */

    if (!atomic_ok)
    {
        /* 跨锚点：删源虚拟节点 + 立即删除源物理实体（内容已写入目标） */
        fs_delete_subtree_ex(src_idx, CCTRUE);
    }
    return CC_FS_OK;
}