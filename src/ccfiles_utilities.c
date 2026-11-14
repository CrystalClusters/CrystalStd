/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles 通用工具：字符串与路径缓冲、时间、宿主文件系统原语、
 *               exe 同层 res 目录解析、64 位定位与截断。不感知虚拟树节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#include <dirent.h>
#endif

CCUINT64 fs_now_seconds(void)
{
    return (CCUINT64)time(NULL);
}

char *fs_strdup(const char *s, const char *comment)
{
    if (!s)
        return NULL;
    CCUINT64 len = (CCUINT64)strlen(s) + 1;
    char *p = (char *)ccalloc(NULL, len, comment);
    if (!p)
        return NULL;
    memcpy(p, s, (size_t)len);
    return p;
}

/*
 * ----------------------------------------------------------------------------
 * 动态路径缓冲
 * 以 CCDyn 承载的可增长字节序列，恒以 '\0' 结尾。路径长度只受内存约束，
 * 不再有固定的 CC_FS_PATH_MAX 上限。
 * ----------------------------------------------------------------------------
 */
CCINT32 fs_buf_putn(CCDyn *b, const char *s, CCUINT32 n)
{
    if (!b)
        return CC_FS_ERR_PARAM;
    /* 移除旧终止符，避免每次追加都多留一个 '\0' */
    if (b->size > 0 && ((char *)b->array)[b->size - 1] == '\0')
        b->size -= 1;
    if (n > 0 && cc_dyn_core_push(b, 1, (CCINT32)n, (void *)s) != 0)
        return CC_FS_ERR_NOSPACE;
    char z = '\0';
    if (cc_dyn_core_push(b, 1, 1, &z) != 0)
        return CC_FS_ERR_NOSPACE;
    return CC_FS_OK;
}

CCINT32 fs_buf_put(CCDyn *b, const char *s)
{
    if (!s)
        return CC_FS_ERR_PARAM;
    return fs_buf_putn(b, s, (CCUINT32)strlen(s));
}

void fs_buf_clear(CCDyn *b)
{
    if (b)
        b->size = 0;
}

const char *fs_buf_str(const CCDyn *b)
{
    if (!b || b->size == 0 || !b->array)
        return "";
    return (const char *)b->array;
}

void fs_buf_free(CCDyn *b)
{
    if (!b)
        return;
    if (b->array)
        ccalloc(b->array, 0, NULL);
    b->array = NULL;
    b->size = 0;
    b->capacity = 0;
}
/* 真实路径解析：绝对路径原样使用，相对路径拼接到可执行程序同层 res 目录 */
CCINT32 fs_real_resolve(const char *real_path, CCDyn *out)
{
    if (!out || !real_path || !real_path[0])
        return CC_FS_ERR_PARAM;
    if (strchr(real_path, '\\'))
        return CC_FS_ERR_PARAM;
    CCBOOL absolute = (real_path[0] == '/') ||
                      (strlen(real_path) >= 2 && real_path[1] == ':');

    fs_buf_clear(out);
    if (absolute)
        return fs_buf_put(out, real_path);

    if (!ccfiles_var.res_base)
        return CC_FS_ERR_NOSPACE;
    CCINT32 r = fs_buf_put(out, ccfiles_var.res_base);
    if (r == CC_FS_OK)
        r = fs_buf_put(out, "/");
    if (r == CC_FS_OK)
        r = fs_buf_put(out, real_path);
    return r;
}
/* 是否为符号链接 / Windows junction 等重解析点（不跟随） */
CCBOOL fs_is_link_path(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
#ifdef CC_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return CCFALSE;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? CCTRUE : CCFALSE;
#else
    struct stat st;
    if (lstat(path, &st) != 0)
        return CCFALSE;
    return S_ISLNK(st.st_mode) ? CCTRUE : CCFALSE;
#endif
}

/* 只删除链接本身，绝不进入链接目标；file symlink / directory junction 均可 */
CCINT32 fs_remove_link_path(const char *path)
{
    if (!path || !path[0])
        return CC_FS_ERR_PARAM;
#ifdef CC_WINDOWS
    /* junction / 目录符号链接用 rmdir 删除链接本身 */
    if (_rmdir(path) == 0)
        return CC_FS_OK;
#endif
    return (remove(path) == 0) ? CC_FS_OK : CC_FS_ERR_IO;
}

/* 删除物理文件（unlink）；链接只删除链接本身 */
CCINT32 fs_unlink_file(const char *path)
{
    if (fs_is_link_path(path))
        return fs_remove_link_path(path);
    return (remove(path) == 0) ? CC_FS_OK : CC_FS_ERR_IO;
}

/* 删除物理空目录（rmdir）；链接只删除链接本身 */
CCINT32 fs_rmdir_dir(const char *path)
{
    if (fs_is_link_path(path))
        return fs_remove_link_path(path);
#ifdef CC_WINDOWS
    return (_rmdir(path) == 0) ? CC_FS_OK : CC_FS_ERR_IO;
#else
    return (rmdir(path) == 0) ? CC_FS_OK : CC_FS_ERR_IO;
#endif
}

/* 物理重命名 / 移动。目标已存在时由调用方先删除（rename 的覆盖语义因平台而异）。 */
CCINT32 fs_rename_path(const char *from, const char *to)
{
    return (rename(from, to) == 0) ? CC_FS_OK : CC_FS_ERR_IO;
}

/* ============================================================================
 * 跨平台文件系统基础操作
 * ========================================================================== */

CCBOOL fs_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? CCTRUE : CCFALSE;
}

CCBOOL fs_is_dir_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return CCFALSE;
#ifdef CC_WINDOWS
    return ((st.st_mode & _S_IFDIR) != 0) ? CCTRUE : CCFALSE;
#else
    return S_ISDIR(st.st_mode) ? CCTRUE : CCFALSE;
#endif
}

CCUINT64 fs_file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (CCUINT64)st.st_size;
}

CCUINT64 fs_file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (CCUINT64)st.st_mtime;
}

/* 逐级创建目录（`/` 或 `\` 分隔，已归一为 `/`）；路径长度只受内存约束。
   真实创建失败（权限 / 非法名）如实返回 CC_FS_ERR_IO，不再静默忽略。 */
CCINT32 fs_mkdirs(const char *path)
{
    if (!path)
        return CC_FS_ERR_PARAM;
    CCDyn buf;
    memset(&buf, 0, sizeof(buf));
    CCINT32 r = fs_buf_put(&buf, path);
    if (r != CC_FS_OK)
    {
        fs_buf_free(&buf);
        return r;
    }

    char *b = (char *)buf.array;
    CCUINT32 len = buf.size - 1; /* 去掉末尾 '\0' */
    for (CCUINT32 i = 0; i <= len; i++)
    {
        if (b[i] == '/' || b[i] == '\0')
        {
            char saved = b[i];
            b[i] = '\0';
            if (b[0] != '\0' && !(i == 2 && b[1] == ':'))
            {
                if (!fs_is_dir_path(b))
                {
#ifdef CC_WINDOWS
                    _mkdir(b);
#else
                    mkdir(b, 0755);
#endif
                    /* 并发 / 已存在不算错；创建后仍不是目录才是真失败 */
                    if (!fs_is_dir_path(b))
                    {
                        r = CC_FS_ERR_IO;
                        goto FailedtoCreateDir;
                    }
                }
            }
            b[i] = saved;
            if (saved == '\0')
                break;
        }
    }
    fs_buf_free(&buf);
    return CC_FS_OK;

FailedtoCreateDir:
    fs_buf_free(&buf);
    return r;
}

/* ============================================================================
 * 物理目录树删除
 * 显式栈迭代，无递归；失败时回收栈内所有剩余路径副本。
 * ========================================================================== */

typedef struct cc_fs_rm_item
{
    char *path;
    CCBOOL expanded;
} CCFsRmItem;

static void fs_rm_items_free(CCDyn *stack)
{
    if (!stack)
        return;
    while (stack->size >= (CCUINT32)sizeof(CCFsRmItem))
    {
        CCFsRmItem it;
        if (cc_dyn_core_pop(stack, (CCUINT32)sizeof(CCFsRmItem),
                            (CCINT32)sizeof(CCFsRmItem), &it) != 0)
            break;
        if (it.path)
            ccalloc(it.path, 0, NULL);
    }
    fs_buf_free(stack);
}

static CCINT32 fs_rm_push(CCDyn *stack, const char *path, CCBOOL expanded)
{
    char *dup = fs_strdup(path, CC_DBG_REL_TAG("待删除路径", NULL));
    if (!dup)
        return CC_FS_ERR_NOSPACE;

    CCFsRmItem it;
    it.path = dup;
    it.expanded = expanded;
    if (cc_dyn_core_push(stack, (CCUINT32)sizeof(CCFsRmItem),
                         (CCINT32)sizeof(CCFsRmItem), &it) != 0)
    {
        ccalloc(dup, 0, NULL);
        return CC_FS_ERR_NOSPACE;
    }
    return CC_FS_OK;
}

/* 把 dir 下的直接子项压栈；目录自身由调用方压 expanded 标记 */
static CCINT32 fs_rm_push_children(CCDyn *stack, const char *dir)
{
    CCDyn child;
    memset(&child, 0, sizeof(child));
    CCINT32 ret = CC_FS_OK;

#ifdef CC_WINDOWS
    CCDyn pattern;
    memset(&pattern, 0, sizeof(pattern));
    HANDLE h = INVALID_HANDLE_VALUE;

    ret = fs_buf_put(&pattern, dir);
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
        /* 空目录 / 路径不存在交给后续 rmdir 判定；真实 IO/权限错误必须上抛 */
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
            err == ERROR_NO_MORE_FILES)
        {
            ret = CC_FS_OK;
            goto Succeeded;
        }
        ret = CC_FS_ERR_IO;
        goto FailedtoEnumerate;
    }

    do
    {
        const char *name = fd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        fs_buf_clear(&child);
        ret = fs_buf_put(&child, dir);
        if (ret == CC_FS_OK)
            ret = fs_buf_put(&child, "/");
        if (ret == CC_FS_OK)
            ret = fs_buf_put(&child, name);
        if (ret != CC_FS_OK)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoEnumerate;
        }

        ret = fs_rm_push(stack, fs_buf_str(&child), CCFALSE);
        if (ret != CC_FS_OK)
            goto FailedtoEnumerate;
    } while (FindNextFileA(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoEnumerate;
    }
    FindClose(h);
    h = INVALID_HANDLE_VALUE;

Succeeded:
    fs_buf_free(&pattern);
    fs_buf_free(&child);
    return ret;

FailedtoEnumerate:
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    fs_buf_free(&pattern);
FailedtoBuildPattern:
#else
    DIR *d = opendir(dir);
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

        fs_buf_clear(&child);
        ret = fs_buf_put(&child, dir);
        if (ret == CC_FS_OK)
            ret = fs_buf_put(&child, "/");
        if (ret == CC_FS_OK)
            ret = fs_buf_put(&child, name);
        if (ret != CC_FS_OK)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoEnumerate;
        }

        ret = fs_rm_push(stack, fs_buf_str(&child), CCFALSE);
        if (ret != CC_FS_OK)
            goto FailedtoEnumerate;
    }
    closedir(d);

    fs_buf_free(&child);
    return CC_FS_OK;

FailedtoEnumerate:
    closedir(d);
FailedtoOpenDir:
#endif
    fs_buf_free(&child);
    return ret;
}

/* 删除物理文件 / 整棵目录树；路径不存在视为成功。 */
CCINT32 fs_remove_tree(const char *path)
{
    if (!path || !path[0])
        return CC_FS_ERR_PARAM;

    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCINT32 ret = fs_rm_push(&stack, path, CCFALSE);
    if (ret != CC_FS_OK)
    {
        fs_rm_items_free(&stack);
        return ret;
    }

    CCFsRmItem cur;
    memset(&cur, 0, sizeof(cur));

    while (stack.size >= (CCUINT32)sizeof(CCFsRmItem))
    {
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCFsRmItem),
                            (CCINT32)sizeof(CCFsRmItem), &cur) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoRemoveTree;
        }

        if (cur.expanded)
        {
            ret = fs_rmdir_dir(cur.path);
            if (ret != CC_FS_OK)
                goto FailedtoRemoveTree;
            ccalloc(cur.path, 0, NULL);
            cur.path = NULL;
            continue;
        }

        /* 符号链接 / junction：只删链接本身，禁止下钻目标目录 */
        if (fs_is_link_path(cur.path))
        {
            ret = fs_remove_link_path(cur.path);
            if (ret != CC_FS_OK)
                goto FailedtoRemoveTree;
            ccalloc(cur.path, 0, NULL);
            cur.path = NULL;
            continue;
        }

        if (!fs_is_dir_path(cur.path))
        {
            if (fs_file_exists(cur.path))
            {
                ret = fs_unlink_file(cur.path);
                if (ret != CC_FS_OK)
                    goto FailedtoRemoveTree;
            }
            ccalloc(cur.path, 0, NULL);
            cur.path = NULL;
            continue;
        }

        /* 目录：先压回展开标记，再压子项，保证子项先于目录 rmdir */
        ret = fs_rm_push(&stack, cur.path, CCTRUE);
        if (ret != CC_FS_OK)
            goto FailedtoRemoveTree;

        ret = fs_rm_push_children(&stack, cur.path);
        if (ret != CC_FS_OK)
            goto FailedtoRemoveTree;

        ccalloc(cur.path, 0, NULL);
        cur.path = NULL;
    }

    fs_rm_items_free(&stack);
    return CC_FS_OK;

FailedtoRemoveTree:
    if (cur.path)
        ccalloc(cur.path, 0, NULL);
    fs_rm_items_free(&stack);
    return ret;
}

/* 取可执行程序所在目录（`/` 归一） */
void fs_init_exe_dir(void)
{
    /* 这是操作系统路径，受 OS 自身长度上限约束，与 VFS 路径无关 */
    char buf[4096];
    buf[0] = '\0';
#ifdef CC_WINDOWS
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
        buf[0] = '\0';
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        buf[0] = '\0';
    else
        buf[n] = '\0';
#endif
    if (buf[0] == '\0')
    {
        if (!getcwd(buf, sizeof(buf)))
            buf[0] = '\0';
    }
    for (CCUINT32 i = 0; buf[i]; i++)
        if (buf[i] == '\\')
            buf[i] = '/';

    /* 去掉末尾文件名 */
    CCINT32 cut = -1;
    for (CCINT32 i = (CCINT32)strlen(buf) - 1; i >= 0; i--)
    {
        if (buf[i] == '/')
        {
            cut = i;
            break;
        }
    }
    if (cut >= 0)
        buf[cut] = '\0';
    else
        buf[0] = '\0';

    char full[4200];
    snprintf(full, sizeof(full), "%s/res", buf);
    ccfiles_var.res_base = fs_strdup(full, CC_DBG_REL_TAG("资源根目录", NULL));
}
/* 64 位定位 */
CCINT32 fs_fseek(FILE *fp, CCUINT64 off)
{
#ifdef CC_WINDOWS
    return (_fseeki64(fp, (__int64)off, SEEK_SET) == 0) ? 0 : 1;
#else
    return (fseeko(fp, (off_t)off, SEEK_SET) == 0) ? 0 : 1;
#endif
}

/* 把文件截断 / 扩展到指定长度 */
CCINT32 fs_truncate(FILE *fp, CCUINT64 size)
{
#ifdef CC_WINDOWS
    return (_chsize_s(_fileno(fp), (__int64)size) == 0) ? 0 : 1;
#else
    return (ftruncate(fileno(fp), (off_t)size) == 0) ? 0 : 1;
#endif
}
/* 取文件路径的父目录（就地截断 path） */
void fs_dir_of(char *path)
{
    CCINT32 cut = -1;
    for (CCINT32 i = (CCINT32)strlen(path) - 1; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            cut = i;
            break;
        }
    }
    if (cut > 0)
        path[cut] = '\0';
}