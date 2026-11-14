/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles 路径层：挂载锚点、物理路径解析、虚拟路径走查、
 *               宿主目录投影扫描，以及挂载 / 卸载 / 刷新。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* ============================================================================
 * 路径解析：挂载锚点 / 物理路径 / 虚拟路径
 * ========================================================================== */

/* 向上找到最近的显式锚点（real_path 非空的节点） */
CCUINT32 fs_find_anchor(CCUINT32 idx)
{
    CCUINT32 cur = idx;
    while (cur != CC_FS_NIL)
    {
        CCFsNode *n = node_at(cur);
        if (n->mount && n->mount->real_path)
            return cur;
        cur = n->fs_parent;
    }
    return CC_FS_NIL;
}

CCBOOL fs_is_protected(CCUINT32 idx)
{
    CCUINT32 a = fs_find_anchor(idx);
    if (a == CC_FS_NIL)
        return CCFALSE;
    CCFsMount *m = node_at(a)->mount;
    return (m && m->type == CC_FS_MOUNT_PKG) ? CCTRUE : CCFALSE;
}

/* 求节点物理路径：最近锚点 real_path + 相对子路径（写入动态缓冲 out） */
CCINT32 fs_physical(CCUINT32 idx, CCDyn *out)
{
    if (!out)
        return CC_FS_ERR_PARAM;
    CCUINT32 a = fs_find_anchor(idx);
    if (a == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    /* 用动态栈收集从 idx 到锚点（不含锚点）的各级祖先，深度只受内存约束 */
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCUINT32 cur = idx;
    while (cur != a)
    {
        if (cur == CC_FS_NIL)
        {
            fs_buf_free(&stack);
            return CC_FS_ERR_PARAM;
        }
        if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCUINT32),
                             (CCINT32)sizeof(CCUINT32), &cur) != 0)
        {
            fs_buf_free(&stack);
            return CC_FS_ERR_NOSPACE;
        }
        cur = node_at(cur)->fs_parent;
    }

    fs_buf_clear(out);
    CCINT32 r = fs_buf_put(out, node_at(a)->mount->real_path);
    CCUINT32 count = stack.size / (CCUINT32)sizeof(CCUINT32);
    CCUINT32 *arr = (CCUINT32 *)stack.array;
    for (CCINT32 i = (CCINT32)count - 1; r == CC_FS_OK && i >= 0; i--)
    {
        r = fs_buf_put(out, "/");
        if (r == CC_FS_OK)
            r = fs_buf_put(out, node_at(arr[i])->name);
    }
    fs_buf_free(&stack);
    return r;
}

/* 取物理路径的堆副本（调用方 ccalloc(p, 0, NULL) 释放）；失败返回 NULL */
char *fs_physical_dup(CCUINT32 idx, const char *comment)
{
    CCDyn b;
    memset(&b, 0, sizeof(b));
    if (fs_physical(idx, &b) != CC_FS_OK)
    {
        fs_buf_free(&b);
        return NULL;
    }
    char *p = fs_strdup(fs_buf_str(&b), comment);
    fs_buf_free(&b);
    return p;
}
/* 删除节点对应的物理文件 / 空目录（不存在视为成功）；句柄先关闭。
   包内只读投影无物理实体，直接跳过；无锚点的纯内存节点同样跳过。 */
CCINT32 fs_physical_delete(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (n->type == CC_FS_TYPE_NONE)
        return CC_FS_OK;

    /* Windows 下打开中的文件无法 unlink，必须先 flush + fclose */
    fs_fh_close(idx);
    if (fs_is_protected(idx))
        return CC_FS_OK;
    /* 显式挂载锚点是宿主的真实目录 / 文件，不属于虚拟树：删除只摘除虚拟节点 */
    if (n->mount && n->mount->real_path)
        return CC_FS_OK;

    char *phys = fs_physical_dup(idx, CC_DBG_REL_TAG("删除路径", NULL));
    if (!phys)
        return CC_FS_OK;

    CCINT32 ret = CC_FS_OK;
    /* 物理路径可能已被替换为链接：只删链接本身，避免把节点类型当目录下钻 */
    if (fs_is_link_path(phys))
    {
        ret = fs_remove_link_path(phys);
    }
    else if (n->type == CC_FS_TYPE_DIR)
    {
        if (fs_is_dir_path(phys))
            ret = fs_rmdir_dir(phys);
    }
    else if (fs_file_exists(phys))
    {
        ret = fs_unlink_file(phys);
    }
    ccalloc(phys, 0, NULL);
    return ret;
}

/*
 * 虚拟路径走查。
 * - strict == CCFALSE：允许最后一个组件不存在，此时 out_idx 置 CC_FS_NIL，
 *   out_parent / leaf 返回其父目录与叶子名（用于 create / mount）。
 * - 处理 "." 与 ".."；".." 按 shell 语义回到父目录，根目录处停留于 "/"。
 */
CCINT32 fs_vwalk(CCFiles *fs, const char *path, CCBOOL strict,
                 CCUINT32 *out_parent, CCDyn *leaf, CCUINT32 *out_idx)
{
    if (!fs || !path || !path[0])
        return CC_FS_ERR_PARAM;

    /* 虚拟路径统一用 '/'；反斜杠会被 Win32 当作分隔符，必须在入口拒绝 */
    if (strchr(path, '\\') != NULL)
        return CC_FS_ERR_PARAM;

    /* 把整条路径复制进动态缓冲后原地切分；长度只受内存约束 */
    CCDyn buf;
    memset(&buf, 0, sizeof(buf));
    CCINT32 ret = fs_buf_put(&buf, path);
    if (ret != CC_FS_OK)
    {
        fs_buf_free(&buf);
        return ret;
    }
    char *base = (char *)buf.array;

    /* 收集各组件起始指针（动态，无深度上限） */
    CCDyn comps;
    memset(&comps, 0, sizeof(comps));
    char *s = base;
    while (*s)
    {
        while (*s == '/')
            s++;
        if (!*s)
            break;
        if (cc_dyn_core_push(&comps, (CCUINT32)sizeof(char *),
                             (CCINT32)sizeof(char *), &s) != 0)
        {
            fs_buf_free(&comps);
            fs_buf_free(&buf);
            return CC_FS_ERR_NOSPACE;
        }
        while (*s && *s != '/')
            s++;
        if (*s)
        {
            *s = '\0';
            s++;
        }
    }

    CCUINT32 ncomp = comps.size / (CCUINT32)sizeof(char *);
    char **cs = (char **)comps.array;

    CCUINT32 root = ccfiles_var.root;
    CCUINT32 cur = (path[0] == '/') ? root : (fs->cwd == CC_FS_NIL ? root : fs->cwd);

    ret = CC_FS_OK;
    if (ncomp == 0)
    {
        if (out_idx)
            *out_idx = cur;
        if (out_parent)
            *out_parent = cur;
        if (leaf)
            fs_buf_clear(leaf);
        goto Succeeded;
    }

    for (CCUINT32 i = 0; i < ncomp; i++)
    {
        const char *c = cs[i];
        CCBOOL last = (i == ncomp - 1);

        if (strcmp(c, ".") == 0)
        {
            if (last)
            {
                if (out_idx)
                    *out_idx = cur;
                if (out_parent)
                    *out_parent = cur;
                if (leaf)
                    fs_buf_clear(leaf);
            }
            continue;
        }

        if (strcmp(c, "..") == 0)
        {
            if (cur != root)
            {
                CCUINT32 up = node_at(cur)->fs_parent;
                cur = (up == CC_FS_NIL) ? root : up;
            }
            if (last)
            {
                if (out_idx)
                    *out_idx = cur;
                if (out_parent)
                    *out_parent = cur;
                if (leaf)
                    fs_buf_clear(leaf);
            }
            continue;
        }

#ifdef CC_WINDOWS
        /* Windows 会忽略组件末尾的空格 / 点，拒绝以免与磁盘上的真实名字错位 */
        {
            size_t clen = strlen(c);
            if (clen > 0 && (c[clen - 1] == ' ' || c[clen - 1] == '.'))
            {
                ret = CC_FS_ERR_PARAM;
                goto FailedtoWalkPath;
            }
        }
#endif

        CCFsNode *d = node_at(cur);
        if (d->type != CC_FS_TYPE_DIR)
        {
            ret = CC_FS_ERR_NOTDIR;
            goto FailedtoWalkPath;
        }

        CCUINT32 child = fs_child_get_idx(cur, c);
        if (child == CC_FS_NIL)
        {
            if (last && !strict)
            {
                if (out_parent)
                    *out_parent = cur;
                if (leaf)
                {
                    fs_buf_clear(leaf);
                    ret = fs_buf_put(leaf, c);
                    if (ret != CC_FS_OK)
                        goto FailedtoWalkPath;
                }
                if (out_idx)
                    *out_idx = CC_FS_NIL;
                goto Succeeded;
            }
            ret = CC_FS_ERR_NOENT;
            goto FailedtoWalkPath;
        }

        if (last)
        {
            if (out_idx)
                *out_idx = child;
            if (out_parent)
                *out_parent = cur;
            if (leaf)
            {
                fs_buf_clear(leaf);
                ret = fs_buf_put(leaf, c);
                if (ret != CC_FS_OK)
                    goto FailedtoWalkPath;
            }
            goto Succeeded;
        }

        cur = child;
    }

    if (out_idx)
        *out_idx = cur;
    if (out_parent)
        *out_parent = cur;

Succeeded:
    fs_buf_free(&comps);
    fs_buf_free(&buf);
    return CC_FS_OK;

FailedtoWalkPath:
    fs_buf_free(&comps);
    fs_buf_free(&buf);
    return ret;
}
/* 把真实目录的内容投影为虚拟子节点（origin = 磁盘投影），显式栈迭代，无递归 */
typedef struct cc_fs_scan_item
{
    CCUINT32 parent;
    char *path;
} CCFsScanItem;

static CCINT32 fs_scan_push(CCDyn *stack, CCUINT32 parent, const char *path)
{
    char *dup = fs_strdup(path, CC_DBG_REL_TAG("目录扫描路径", NULL));
    if (!dup)
        return -1;
    CCFsScanItem it;
    it.parent = parent;
    it.path = dup;
    if (cc_dyn_core_push(stack, (CCUINT32)sizeof(CCFsScanItem),
                         (CCINT32)sizeof(CCFsScanItem), &it) != 0)
    {
        ccalloc(dup, 0, NULL);
        return -1;
    }
    return 0;
}

/* 释放扫描栈中尚未处理的路径副本（失败路径 / 收尾共用） */
static void fs_scan_items_free(CCDyn *stack)
{
    if (!stack)
        return;
    while (stack->size >= (CCUINT32)sizeof(CCFsScanItem))
    {
        CCFsScanItem it;
        if (cc_dyn_core_pop(stack, (CCUINT32)sizeof(CCFsScanItem),
                            (CCINT32)sizeof(CCFsScanItem), &it) != 0)
            break;
        if (it.path)
            ccalloc(it.path, 0, NULL);
    }
    fs_buf_free(stack);
}

/* 处理一个目录项；若需继续展开其子目录，out_sub 返回该子目录下标 */
static CCINT32 fs_add_scan_child(CCUINT32 parent_idx, const char *name,
                                 CCBOOL is_dir, const char *child_path, CCUINT32 *out_sub)
{
    if (!out_sub)
        return CC_FS_ERR_PARAM;
    *out_sub = CC_FS_NIL;

    CCUINT32 existing = fs_child_get_idx(parent_idx, name);
    if (existing != CC_FS_NIL)
    {
        CCFsNode *n = node_at(existing);
        /* 显式子挂载锚点原样保留，不把真实目录内容覆盖其上 */
        if (n->mount && n->mount->real_path)
            return CC_FS_OK;
        if (is_dir && n->type == CC_FS_TYPE_DIR)
        {
            *out_sub = existing;
            return CC_FS_OK;
        }
        if (!is_dir && n->type == CC_FS_TYPE_FILE)
        {
            n->size = fs_file_size(child_path);
            n->mtime = fs_file_mtime(child_path);
        }
        return CC_FS_OK;
    }

    CCUINT32 idx = fs_create_node(parent_idx, name,
                                  is_dir ? CC_FS_TYPE_DIR : CC_FS_TYPE_FILE, NULL, 0, FS_ORIGIN_DISK);
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOSPACE;

    CCFsNode *n = node_at(idx);
    n->mtime = fs_file_mtime(child_path);
    if (n->ctime == 0)
        n->ctime = n->mtime;
    if (!is_dir)
        n->size = fs_file_size(child_path);

    if (fs_insert_child(parent_idx, idx) != 0)
    {
        fs_mount_free(node_at(idx)->mount);
        if (node_at(idx)->name)
            ccalloc(node_at(idx)->name, 0, NULL);
        fs_alloc_rollback();
        return CC_FS_ERR_IO;
    }
    *out_sub = is_dir ? idx : CC_FS_NIL;
    return CC_FS_OK;
}

/* 扫描一个目录层，把直接子项投影到虚拟树；子目录压栈待后续展开 */
static CCINT32 fs_scan_dir_one(CCUINT32 parent_idx, const char *dir_path, CCDyn *stack)
{
    CCDyn child_path;
    memset(&child_path, 0, sizeof(child_path));
    CCINT32 ret = CC_FS_OK;

#ifdef CC_WINDOWS
    CCDyn pattern;
    memset(&pattern, 0, sizeof(pattern));
    HANDLE h = INVALID_HANDLE_VALUE;

    ret = fs_buf_put(&pattern, dir_path);
    if (ret == CC_FS_OK)
        ret = fs_buf_put(&pattern, "/*");
    if (ret != CC_FS_OK)
    {
        ret = CC_FS_ERR_NOSPACE;
        goto FailedtoBuildPattern;
    }

    WIN32_FIND_DATAA fd;
    h = FindFirstFileA(fs_buf_str(&pattern), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        /* 空目录是正常的；权限 / IO 问题必须如实上报，不能投影成空目录。 */
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoEnumerate;
        }
    }
    else
    {
        do
        {
            const char *name = fd.cFileName;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            fs_buf_clear(&child_path);
            if (fs_buf_put(&child_path, dir_path) != CC_FS_OK ||
                fs_buf_put(&child_path, "/") != CC_FS_OK ||
                fs_buf_put(&child_path, name) != CC_FS_OK)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoEnumerate;
            }
            /* 待删遮罩已随「删除即刻落盘」取消：磁盘上还有什么就投影什么 */
            /* 重解析点（符号链接 / junction）不跟随，按普通文件项投影 */
            CCBOOL is_link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? CCTRUE : CCFALSE;
            CCBOOL is_dir = (!is_link && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ? CCTRUE : CCFALSE;
            CCUINT32 sub = CC_FS_NIL;
            ret = fs_add_scan_child(parent_idx, name, is_dir,
                                    fs_buf_str(&child_path), &sub);
            if (ret != CC_FS_OK)
                goto FailedtoEnumerate;
            if (sub != CC_FS_NIL &&
                fs_scan_push(stack, sub, fs_buf_str(&child_path)) != 0)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoEnumerate;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        h = INVALID_HANDLE_VALUE;
    }

    fs_buf_free(&pattern);
    fs_buf_free(&child_path);
    return CC_FS_OK;

FailedtoEnumerate:
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    fs_buf_free(&pattern);
FailedtoBuildPattern:
#else
    DIR *d = opendir(dir_path);
    if (!d)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoOpenDir;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL)
    {
        const char *name = e->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        fs_buf_clear(&child_path);
        if (fs_buf_put(&child_path, dir_path) != CC_FS_OK ||
            fs_buf_put(&child_path, "/") != CC_FS_OK ||
            fs_buf_put(&child_path, name) != CC_FS_OK)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoEnumerate;
        }
        /* POSIX 同样不跟随符号链接 */
        CCBOOL is_dir = (fs_is_dir_path(fs_buf_str(&child_path)) &&
                         !fs_is_link_path(fs_buf_str(&child_path))) ? CCTRUE : CCFALSE;
        CCUINT32 sub = CC_FS_NIL;
        ret = fs_add_scan_child(parent_idx, name, is_dir,
                                fs_buf_str(&child_path), &sub);
        if (ret != CC_FS_OK)
            goto FailedtoEnumerate;
        if (sub != CC_FS_NIL &&
            fs_scan_push(stack, sub, fs_buf_str(&child_path)) != 0)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoEnumerate;
        }
    }
    closedir(d);

    fs_buf_free(&child_path);
    return CC_FS_OK;

FailedtoEnumerate:
    closedir(d);
FailedtoOpenDir:
#endif
    fs_buf_free(&child_path);
    return ret;
}

CCINT32 fs_scan_dir_into(CCUINT32 parent_idx, const char *real_path)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    if (fs_scan_push(&stack, parent_idx, real_path) != 0)
    {
        fs_buf_free(&stack);
        return CC_FS_ERR_NOSPACE;
    }

    while (stack.size >= (CCUINT32)sizeof(CCFsScanItem))
    {
        CCFsScanItem cur;
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCFsScanItem),
                            (CCINT32)sizeof(CCFsScanItem), &cur) != 0)
        {
            fs_scan_items_free(&stack);
            return CC_FS_ERR_IO;
        }

        CCINT32 ret = fs_scan_dir_one(cur.parent, cur.path, &stack);
        ccalloc(cur.path, 0, NULL);
        if (ret != CC_FS_OK)
        {
            fs_scan_items_free(&stack);
            return ret;
        }
    }

    fs_scan_items_free(&stack);
    return CC_FS_OK;
}
/* ============================================================================
 * 挂载 / 卸载 / 刷新
 * ========================================================================== */

/* 需要跨挂载覆盖保留的节点：显式锚点子节点，或内存节点 */
static CCBOOL fs_is_preserved_node(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (!n || n->type == CC_FS_TYPE_NONE || !n->name || !n->mount)
        return CCFALSE;
    if (n->mount->real_path)
        return CCTRUE;
    return n->mount->origin == FS_ORIGIN_MEM ? CCTRUE : CCFALSE;
}

static CCFsMount *fs_mount_clone(const CCFsMount *src)
{
    if (!src)
        return fs_mount_new(NULL, 0, FS_ORIGIN_MEM);
    CCFsMount *m = fs_mount_new(src->real_path, src->type, src->origin);
    if (!m)
        return NULL;
    m->pkg_off = src->pkg_off;
    m->pkg_size = src->pkg_size;
    return m;
}

static void fs_copy_node_meta(CCFsNode *dst, const CCFsNode *src)
{
    dst->size = src->size;
    dst->ctime = src->ctime;
    dst->mtime = src->mtime;
    dst->crc32 = src->crc32;
}

/* 把旧树中的保留节点复制到 stage；旧树在提交前完全不动。
   冲突规则：旧节点优先；同类目录则递归合并，把新投影并入旧目录。 */
static CCINT32 fs_merge_preserved_children(CCUINT32 old_dir,
                                           CCUINT32 new_dir, CCDyn *watch);

static CCINT32 fs_merge_preserved_node(CCUINT32 old_idx, CCDyn *watch)
{
    CCFsNode *old = node_at(old_idx);
    if (!fs_is_preserved_node(old_idx))
        return CC_FS_OK;

    CCUINT32 wcnt = watch->size / (CCUINT32)sizeof(CCUINT32);
    CCUINT32 *warr = (CCUINT32 *)watch->array;
    CCUINT32 new_parent = warr[wcnt - 1];
    CCUINT32 existing = fs_child_get_idx(new_parent, old->name);

    if (existing != CC_FS_NIL)
    {
        CCFsNode *dst = node_at(existing);
        if (old->type == CC_FS_TYPE_DIR && dst->type == CC_FS_TYPE_DIR)
        {
            CCFsMount *m = fs_mount_clone(old->mount);
            if (!m)
                return CC_FS_ERR_NOSPACE;
            fs_mount_free(dst->mount);
            dst->mount = m;
            fs_copy_node_meta(dst, old);
            return fs_merge_preserved_children(old_idx, existing, watch);
        }

        /* 文件/类型冲突：旧节点胜出，丢弃 stage 中的新投影子树 */
        CCINT32 ret = fs_delete_subtree_watch(existing, CCFALSE,
                                              warr, wcnt);
        if (ret != CC_FS_OK)
            return ret;
        new_parent = ((CCUINT32 *)watch->array)[wcnt - 1];
    }

    CCUINT32 dst = fs_create_node(new_parent, old->name, old->type,
                                  old->mount->real_path, old->mount->type, old->mount->origin);
    if (dst == CC_FS_NIL)
        return CC_FS_ERR_NOSPACE;

    CCFsNode *dn = node_at(dst);
    fs_copy_node_meta(dn, old);
    dn->mount->pkg_off = old->mount->pkg_off;
    dn->mount->pkg_size = old->mount->pkg_size;

    if (fs_insert_child(new_parent, dst) != 0)
    {
        fs_mount_free(dn->mount);
        if (dn->name)
            ccalloc(dn->name, 0, NULL);
        fs_alloc_rollback();
        return CC_FS_ERR_IO;
    }

    return fs_merge_preserved_children(old_idx, dst, watch);
}

static CCINT32 fs_merge_preserved_children(CCUINT32 old_dir,
                                           CCUINT32 new_dir, CCDyn *watch)
{
    CCUINT32 mark = watch->size;
    if (cc_dyn_core_push(watch, (CCUINT32)sizeof(CCUINT32),
                         (CCINT32)sizeof(CCUINT32), &new_dir) != 0)
        return CC_FS_ERR_NOSPACE;

    CCDyn kids;
    memset(&kids, 0, sizeof(kids));
    CCINT32 ret = fs_collect_children_ex(old_dir, &kids);
    if (ret != CC_FS_OK)
        goto FailedtoCollect;

    CCUINT32 *arr = (CCUINT32 *)kids.array;
    CCUINT32 cnt = kids.size / (CCUINT32)sizeof(CCUINT32);
    for (CCUINT32 i = 0; i < cnt; i++)
    {
        if (!fs_is_preserved_node(arr[i]))
            continue;
        ret = fs_merge_preserved_node(arr[i], watch);
        if (ret != CC_FS_OK)
            goto FailedtoMerge;
    }

    fs_buf_free(&kids);
    watch->size = mark;
    return CC_FS_OK;

FailedtoMerge:
    fs_buf_free(&kids);
FailedtoCollect:
    watch->size = mark;
    return ret;
}
/* 把旧保留节点的探针与常驻句柄迁到 stage 中同名的副本，随后旧树即可安全删除 */
static void fs_transfer_node_state(CCUINT32 old_idx, CCUINT32 new_idx)
{
    CCFsNode *old = node_at(old_idx);
    CCFsNode *dst = node_at(new_idx);

    if (old->fp)
    {
        if (dst->fp && dst->fp != old->fp)
            (void)fs_fh_close(new_idx);
        dst->fp = old->fp;
        dst->fp_pos = old->fp_pos;
        dst->fp_writable = old->fp_writable;
        old->fp = NULL;
        old->fp_pos = 0;
        old->fp_writable = CCFALSE;
    }

    CCFsProbeRef *head = old->probe_head;
    if (!head)
        return;

    CCFsProbeRef *tail = head;
    while (tail->next)
        tail = tail->next;
    tail->next = dst->probe_head;
    if (dst->probe_head)
        dst->probe_head->prev = tail;
    dst->probe_head = head;
    head->prev = NULL;
    for (CCFsProbeRef *pr = head; pr; pr = pr->next)
        if (pr->probe)
            pr->probe->cur = new_idx;
    old->probe_head = NULL;
}

static void fs_transfer_preserved_state(CCUINT32 old_dir, CCUINT32 new_dir);

typedef struct
{
    CCUINT32 new_dir;
} CCFsTransferCtx;

static CCINT32 fs_transfer_state_cb(void *data, void *user)
{
    CCUINT32 oi = idx_of((CCFsNode *)data);
    CCFsTransferCtx *ctx = (CCFsTransferCtx *)user;
    if (!fs_is_preserved_node(oi))
        return 0;

    CCUINT32 ni = fs_child_get_idx(ctx->new_dir, node_at(oi)->name);
    if (ni == CC_FS_NIL)
        return 0;
    fs_transfer_node_state(oi, ni);
    if (node_at(oi)->type == CC_FS_TYPE_DIR &&
        node_at(ni)->type == CC_FS_TYPE_DIR)
        fs_transfer_preserved_state(oi, ni);
    return 0;
}

/* 直接把旧保留节点的句柄 / 探针迁到 stage 同名副本；遍历不再分配内存，
   因此提交点之后不会因内存不足丢失常驻句柄。 */
static void fs_transfer_preserved_state(CCUINT32 old_dir, CCUINT32 new_dir)
{
    if (old_dir == CC_FS_NIL || new_dir == CC_FS_NIL)
        return;
    CCFsTransferCtx ctx;
    ctx.new_dir = new_dir;
    (void)fs_children_walk(old_dir, &ctx, fs_transfer_state_cb);
}

/* stage 构建完成后的原子提交：先预检旧树回收所需内存并关闭目标句柄，
   随后替换挂载 / 元数据 / 子树；越过提交点后不再分配内存也不返回失败。 */
static CCINT32 fs_mount_commit(CCUINT32 target_idx, CCUINT32 stage_idx)
{
    CCFsNode *t = node_at(target_idx);
    CCFsNode *s = node_at(stage_idx);

    CCUINT32 old_root = t->children_root;
    CCFsMount *old_mount = t->mount;
    CCUINT64 old_ctime = t->ctime;

    /* 提交前先收集旧树：内存不足时直接失败，旧树、stage 与句柄均保持原样。 */
    CCDyn doomed;
    memset(&doomed, 0, sizeof(doomed));
    if (old_root != CC_FS_NIL)
    {
        CCINT32 cr = fs_collect_subtree(old_root, &doomed);
        if (cr != CC_FS_OK)
        {
            fs_buf_free(&doomed);
            return cr;
        }
    }

    /* 旧树上的句柄先尝试落盘：失败可在提交点前中止，避免提交后才发现写失败。 */
    CCUINT32 dcnt = doomed.size / (CCUINT32)sizeof(CCUINT32);
    CCUINT32 *darr = (CCUINT32 *)doomed.array;
    CCINT32 ret = CC_FS_OK;
    for (CCUINT32 i = 0; i < dcnt; i++)
    {
        CCUINT32 di = darr[i];
        if (di >= node_count())
            continue;
        CCINT32 fr = fs_fh_flush(di);
        if (fr != CC_FS_OK)
        {
            ret = fr;
            break;
        }
    }
    if (ret == CC_FS_OK)
        ret = fs_fh_close(target_idx);
    if (ret != CC_FS_OK)
    {
        fs_buf_free(&doomed);
        return ret;
    }

    t->mount = s->mount;
    s->mount = NULL;
    t->type = s->type;
    t->size = s->size;
    t->ctime = old_ctime ? old_ctime : s->ctime;
    t->mtime = s->mtime;
    t->crc32 = s->crc32;

    t->children_root = s->children_root;
    s->children_root = CC_FS_NIL;
    if (t->children_root != CC_FS_NIL)
    {
        node_at(t->children_root)->fs_parent = target_idx;
        fs_set_children_parent(target_idx, target_idx);
    }
    if (old_root != CC_FS_NIL)
        node_at(old_root)->fs_parent = CC_FS_NIL;

    fs_transfer_preserved_state(old_root, target_idx);

    if (old_mount)
        fs_mount_free(old_mount);

    /* 已过提交点：只消费预先收集好的列表，swap-remove 用 watch 跟踪 stage /
       target 新下标；删除本身不再分配内存，也不再向上返回失败。 */
    if (old_root != CC_FS_NIL)
    {
        CCUINT32 watch[2] = {target_idx, stage_idx};
        (void)fs_delete_list_watch(&doomed, CCFALSE, watch, 2);
        stage_idx = watch[1];
    }
    fs_buf_free(&doomed);

    /* stage 已空壳化，直接回收，避免提交后半段再分配内存。 */
    fs_discard_detached_node(stage_idx);
    return CC_FS_OK;
}

/* 在 stage 中构建挂载内容；失败时 stage 自行回收。stage 始终暂不挂入名称树。 */
static CCUINT32 fs_mount_stage_create(const char *name, CCFsType type,
                                      const char *real_path, CCUINT32 mount_type, CCUINT8 origin,
                                      CCINT32 *out_ret)
{
    CCUINT32 stage = fs_create_node(CC_FS_NIL, name, type,
                                    real_path, mount_type, origin);
    if (stage == CC_FS_NIL)
    {
        if (out_ret)
            *out_ret = CC_FS_ERR_NOSPACE;
        return CC_FS_NIL;
    }

    CCFsNode *n = node_at(stage);
    n->mtime = fs_now_seconds();
    if (n->ctime == 0)
        n->ctime = n->mtime;

    CCINT32 ret = CC_FS_OK;
    if (type == CC_FS_TYPE_FILE)
    {
        n->size = fs_file_size(real_path);
        n->mtime = fs_file_mtime(real_path);
        if (n->ctime == 0)
            n->ctime = n->mtime;
    }
    else if (mount_type == CC_FS_MOUNT_PKG)
    {
        ret = fs_load_pkg_into(stage, real_path);
    }
    else
    {
        ret = fs_scan_dir_into(stage, real_path);
    }

    if (ret != CC_FS_OK)
    {
        fs_delete_subtree(stage);
        if (out_ret)
            *out_ret = ret;
        return CC_FS_NIL;
    }
    if (out_ret)
        *out_ret = CC_FS_OK;
    return stage;
}

CCINT32 ccfiles_mount(CCFiles *fs, const char *v_path, const char *real_path, CCUINT32 type)
{
    if (!fs || !v_path || !real_path)
        return CC_FS_ERR_PARAM;
    if (type != CC_FS_MOUNT_DIR && type != CC_FS_MOUNT_PKG && type != CC_FS_MOUNT_FILE)
        return CC_FS_ERR_PARAM;

    CCDyn resolved;
    memset(&resolved, 0, sizeof(resolved));
    CCDyn leaf;
    memset(&leaf, 0, sizeof(leaf));
    CCINT32 ret = CC_FS_OK;

    ret = fs_real_resolve(real_path, &resolved);
    if (ret != CC_FS_OK)
        goto FailedtoResolve;
    const char *rpath = fs_buf_str(&resolved);
    if (rpath[0] == '\0')
    {
        ret = CC_FS_ERR_PARAM;
        goto FailedtoResolve;
    }

    if (type == CC_FS_MOUNT_DIR && !fs_is_dir_path(rpath))
    {
        ret = CC_FS_ERR_NOENT;
        goto FailedtoResolve;
    }
    if (type == CC_FS_MOUNT_FILE && !fs_file_exists(rpath))
    {
        ret = CC_FS_ERR_NOENT;
        goto FailedtoResolve;
    }
    if (type == CC_FS_MOUNT_PKG && !fs_file_exists(rpath))
    {
        ret = CC_FS_ERR_NOENT;
        goto FailedtoResolve;
    }

    CCUINT32 parent = CC_FS_NIL;
    CCUINT32 idx = CC_FS_NIL;
    ret = fs_vwalk(fs, v_path, CCFALSE, &parent, &leaf, &idx);
    if (ret != CC_FS_OK)
        goto FailedtoResolve;

    const char *lf = fs_buf_str(&leaf);
    CCFsType node_type = (type == CC_FS_MOUNT_FILE) ? CC_FS_TYPE_FILE : CC_FS_TYPE_DIR;
    CCUINT8 origin = (type == CC_FS_MOUNT_PKG) ? FS_ORIGIN_PKG : FS_ORIGIN_DISK;

    if (idx == CC_FS_NIL)
    {
        if (lf[0] == '\0' || parent == CC_FS_NIL)
        {
            ret = CC_FS_ERR_PARAM;
            goto FailedtoResolve;
        }
        if (fs_is_protected(parent))
        {
            ret = CC_FS_ERR_PROTECT;
            goto FailedtoResolve;
        }

        /* 新锚点：先成功构建整棵 stage，再挂入名称树；扫描失败不留半截节点 */
        CCUINT32 stage = fs_mount_stage_create(lf, node_type, rpath, type, origin, &ret);
        if (stage == CC_FS_NIL)
            goto FailedtoResolve;
        node_at(stage)->fs_parent = parent;
        if (fs_insert_child(parent, stage) != 0)
        {
            node_at(stage)->fs_parent = CC_FS_NIL;
            fs_delete_subtree(stage);
            ret = CC_FS_ERR_IO;
            goto FailedtoResolve;
        }
        idx = stage;
    }
    else
    {
        if (fs_is_protected(idx) && fs_find_anchor(idx) != idx)
        {
            ret = CC_FS_ERR_PROTECT;
            goto FailedtoResolve;
        }

        /* 覆盖锚点：stage 中构建新投影，再把旧内存节点/显式锚点合并进来 */
        CCUINT32 stage = fs_mount_stage_create("", node_type, rpath, type, origin, &ret);
        if (stage == CC_FS_NIL)
            goto FailedtoResolve;

        CCDyn watch;
        memset(&watch, 0, sizeof(watch));
        if (cc_dyn_core_push(&watch, (CCUINT32)sizeof(CCUINT32),
                             (CCINT32)sizeof(CCUINT32), &stage) != 0)
        {
            fs_delete_subtree(stage);
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoResolve;
        }

        ret = fs_merge_preserved_children(idx, stage, &watch);
        stage = ((CCUINT32 *)watch.array)[0];
        fs_buf_free(&watch);
        if (ret != CC_FS_OK)
        {
            fs_delete_subtree(stage);
            goto FailedtoResolve;
        }

        ret = fs_mount_commit(idx, stage);
        if (ret != CC_FS_OK)
        {
            fs_delete_subtree(stage);
            goto FailedtoResolve;
        }
    }

    fs_buf_free(&resolved);
    fs_buf_free(&leaf);
    return CC_FS_OK;

FailedtoResolve:
    fs_buf_free(&resolved);
    fs_buf_free(&leaf);
    return ret;
}

CCINT32 ccfiles_unmount(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    CCFsNode *n = node_at(idx);
    if (!n->mount || !n->mount->real_path)
        return CC_FS_ERR_NOTMOUNT;

    /* 空 stage 保留原类型与元数据；旧内存节点/显式锚点复制进来后原子提交 */
    CCUINT32 stage = fs_create_node(CC_FS_NIL, "", n->type,
                                    NULL, 0, FS_ORIGIN_MEM);
    if (stage == CC_FS_NIL)
        return CC_FS_ERR_NOSPACE;

    CCFsNode *sn = node_at(stage);
    fs_copy_node_meta(sn, n);

    CCDyn watch;
    memset(&watch, 0, sizeof(watch));
    if (cc_dyn_core_push(&watch, (CCUINT32)sizeof(CCUINT32),
                         (CCINT32)sizeof(CCUINT32), &stage) != 0)
    {
        fs_delete_subtree(stage);
        return CC_FS_ERR_NOSPACE;
    }

    r = fs_merge_preserved_children(idx, stage, &watch);
    stage = ((CCUINT32 *)watch.array)[0];
    fs_buf_free(&watch);
    if (r != CC_FS_OK)
    {
        fs_delete_subtree(stage);
        return r;
    }

    r = fs_mount_commit(idx, stage);
    if (r != CC_FS_OK)
    {
        fs_delete_subtree(stage);
        return r;
    }
    return CC_FS_OK;
}

CCINT32 ccfiles_refresh(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 r = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (r != CC_FS_OK)
        return r;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (node_at(idx)->type != CC_FS_TYPE_DIR)
        return CC_FS_ERR_NOTDIR;
    if (fs_is_protected(idx))
        return CC_FS_OK;

    char *phys = fs_physical_dup(idx, CC_DBG_REL_TAG("物理路径", NULL));
    if (!phys)
        return CC_FS_ERR_NOSPACE;
    if (!fs_is_dir_path(phys))
    {
        ccalloc(phys, 0, NULL);
        return CC_FS_OK;
    }

    CCFsNode *n = node_at(idx);
    const char *rp = n->mount ? n->mount->real_path : NULL;
    CCUINT32 mtype = n->mount ? n->mount->type : 0;
    CCUINT8 origin = n->mount ? n->mount->origin : FS_ORIGIN_DISK;

    CCUINT32 stage = fs_mount_stage_create("", CC_FS_TYPE_DIR, phys,
                                           mtype, origin, &r);
    ccalloc(phys, 0, NULL);
    if (stage == CC_FS_NIL)
        return r;

    /* stage 的挂载身份保持与刷新目标一致（子目录刷新不额外生成锚点） */
    CCFsNode *sn = node_at(stage);
    CCFsMount *sm = sn->mount;
    if (sm)
        fs_mount_free(sm);
    sn->mount = fs_mount_new(rp, mtype, origin);
    if (!sn->mount)
    {
        fs_delete_subtree(stage);
        return CC_FS_ERR_NOSPACE;
    }
    fs_copy_node_meta(sn, n);

    CCDyn watch;
    memset(&watch, 0, sizeof(watch));
    if (cc_dyn_core_push(&watch, (CCUINT32)sizeof(CCUINT32),
                         (CCINT32)sizeof(CCUINT32), &stage) != 0)
    {
        fs_delete_subtree(stage);
        return CC_FS_ERR_NOSPACE;
    }

    r = fs_merge_preserved_children(idx, stage, &watch);
    stage = ((CCUINT32 *)watch.array)[0];
    fs_buf_free(&watch);
    if (r != CC_FS_OK)
    {
        fs_delete_subtree(stage);
        return r;
    }

    r = fs_mount_commit(idx, stage);
    if (r != CC_FS_OK)
    {
        fs_delete_subtree(stage);
        return r;
    }
    return CC_FS_OK;
}