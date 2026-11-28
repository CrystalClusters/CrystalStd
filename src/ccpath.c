/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 宿主路径字符串工具实现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"

#include <errno.h>
#include <string.h>
#include <ctype.h>

#ifdef CC_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif

#define CC_PATH_COMMENT CC_DBG_REL_TAG("路径工具", NULL)

static CCBOOL path_is_sep(char c)
{
#ifdef CC_WINDOWS
    return (c == '/' || c == '\\') ? CCTRUE : CCFALSE;
#else
    return (c == '/') ? CCTRUE : CCFALSE;
#endif
}

static char *path_dup_range(const char *text, CCUINT32 len)
{
    char *out = (char *)ccalloc(
        NULL, (CCUINT64)len + 1, CC_PATH_COMMENT
    );
    if (!out)
        return NULL;
    if (len > 0)
        memcpy(out, text, len);
    out[len] = '\0';
    return out;
}

// 识别绝对路径前缀。返回 CCTRUE 表示路径结构合法。
// root_len 为根前缀长度：POSIX '/' 为 1；Windows 盘符 'C:/' 为 3；
// UNC 为 '//server/share' 的长度；相对路径为 0。
static CCBOOL path_prefix_len(const char *path, CCUINT32 *root_len)
{
    if (!path || !root_len)
        return CCFALSE;
#ifdef CC_WINDOWS
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
    {
        if (!path_is_sep(path[2]))
            return CCFALSE;
        *root_len = 3;
        return CCTRUE;
    }
    if (path_is_sep(path[0]))
    {
        if (path_is_sep(path[1]))
        {
            if (path[2] == '?' || path[2] == '.')
                return CCFALSE;
            CCUINT32 server_end = 2;
            while (path[server_end] && !path_is_sep(path[server_end]))
                server_end++;
            if (server_end == 2 || path[server_end] == '\0')
                return CCFALSE;
            CCUINT32 share_end = server_end + 1;
            while (path[share_end] && !path_is_sep(path[share_end]))
                share_end++;
            if (share_end == server_end + 1)
                return CCFALSE;
            *root_len = share_end;
            return CCTRUE;
        }
        *root_len = 1;
        return CCTRUE;
    }
    *root_len = 0;
    return CCTRUE;
#else
    if (path[0] == '/')
    {
        *root_len = 1;
        return CCTRUE;
    }
    *root_len = 0;
    return CCTRUE;
#endif
}

static char *path_work_copy(const char *path)
{
    CCUINT32 len = (CCUINT32)strlen(path);
    char *work = path_dup_range(path, len);
    if (!work)
        return NULL;
#ifdef CC_WINDOWS
    for (CCUINT32 i = 0; i < len; i++)
    {
        if (work[i] == '\\')
            work[i] = '/';
    }
#else
    CC_UNUSED(len);
#endif
    return work;
}

// 形式像绝对路径但结构非法：盘符相对、残缺 UNC、扩展长度前缀。
static CCBOOL path_is_bad_abs(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
#ifdef CC_WINDOWS
    CCUINT32 root_len = 0;
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
        return path_prefix_len(path, &root_len) ? CCFALSE : CCTRUE;
    if (path_is_sep(path[0]))
        return path_prefix_len(path, &root_len) ? CCFALSE : CCTRUE;
#else
    CC_UNUSED(path);
#endif
    return CCFALSE;
}

static CCBOOL path_buf_putc(CCDyn *buf, char c)
{
    if (!buf)
        return CCFALSE;
    if (cc_dyn_core_push(buf, 1, 1, &c) != 0)
        return CCFALSE;
    return CCTRUE;
}

static CCBOOL path_buf_putn(CCDyn *buf, const char *text, CCUINT32 len)
{
    if (!buf || (!text && len > 0))
        return CCFALSE;
    for (CCUINT32 i = 0; i < len; i++)
    {
        if (!path_buf_putc(buf, text[i]))
            return CCFALSE;
    }
    return CCTRUE;
}

static char *path_finish_buf(CCDyn *buf)
{
    if (!path_buf_putc(buf, '\0'))
    {
        if (buf->array)
            ccalloc(buf->array, 0, NULL);
        memset(buf, 0, sizeof(*buf));
        return NULL;
    }
    return (char *)buf->array;
}

// 释放规范化过程持有的工作串与组件表。
static void path_release_parts(char *work, CCDyn *comps)
{
    if (work)
        ccalloc(work, 0, NULL);
    if (comps && comps->array)
        ccalloc(comps->array, 0, NULL);
}

// 规范化绝对路径。path 必须是绝对路径；返回 ccalloc 内存。
static char *path_normalize_abs(const char *path)
{
    CCUINT32 root_len = 0;
    if (!path_prefix_len(path, &root_len) || root_len == 0)
        return NULL;

    char *work = path_work_copy(path);
    if (!work)
        return NULL;

    CCDyn comps;
    memset(&comps, 0, sizeof(comps));
    CCDyn out;
    memset(&out, 0, sizeof(out));

    CCUINT32 len = (CCUINT32)strlen(work);
    CCUINT32 pos = root_len;
    while (pos < len)
    {
        while (pos < len && work[pos] == '/')
            pos++;
        if (pos >= len)
            break;

        CCUINT32 begin = pos;
        while (pos < len && work[pos] != '/')
            pos++;
        if (pos < len)
        {
            work[pos] = '\0';
            pos++;
        }

        char *comp = work + begin;
        if (strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0)
        {
            CCUINT32 count =
                comps.size / (CCUINT32)sizeof(char *);
            if (count > 0)
                comps.size -= (CCUINT32)sizeof(char *);
            continue;
        }
        if (cc_dyn_core_push(
                &comps, (CCUINT32)sizeof(char *),
                (CCINT32)sizeof(char *), &comp
            ) != 0)
        {
            ccalloc(work, 0, NULL);
            if (comps.array)
                ccalloc(comps.array, 0, NULL);
            return NULL;
        }
    }

    if (!path_buf_putn(&out, work, root_len))
        goto Failed;

    CCUINT32 count = comps.size / (CCUINT32)sizeof(char *);
    char **items = (char **)comps.array;
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCBOOL need_sep = (out.size == 0 ||
            ((char *)out.array)[out.size - 1] != '/') ? CCTRUE : CCFALSE;
        if (need_sep && !path_buf_putc(&out, '/'))
            goto Failed;
        if (!path_buf_putn(
                &out, items[i], (CCUINT32)strlen(items[i])
            ))
            goto Failed;
    }

    path_release_parts(work, &comps);
    return path_finish_buf(&out);

Failed:
    path_release_parts(work, &comps);
    if (out.array)
        ccalloc(out.array, 0, NULL);
    return NULL;
}

static char *path_cwd(void)
{
    CCUINT32 cap = 256;
    char *buf = (char *)ccalloc(
        NULL, (CCUINT64)cap, CC_PATH_COMMENT
    );
    if (!buf)
        return NULL;

    for (;;)
    {
#ifdef CC_WINDOWS
        if (_getcwd(buf, (int)cap) != NULL)
            break;
#else
        if (getcwd(buf, (size_t)cap) != NULL)
            break;
#endif
        if (errno != ERANGE)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        if (cap > 0x40000000u)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        CCUINT32 next = cap * 2;
        char *grown = (char *)ccalloc(
            buf, (CCUINT64)next, CC_PATH_COMMENT
        );
        if (!grown)
        {
            ccalloc(buf, 0, NULL);
            return NULL;
        }
        buf = grown;
        cap = next;
    }

    char *norm = path_normalize_abs(buf);
    ccalloc(buf, 0, NULL);
    return norm;
}

static char *path_join_raw(const char *left, const char *right)
{
    CCUINT32 left_len = (CCUINT32)strlen(left);
    CCUINT32 right_len = (CCUINT32)strlen(right);
    CCBOOL has_sep = (left_len > 0 &&
        path_is_sep(left[left_len - 1])) ? CCTRUE : CCFALSE;
    if (left_len > (CCUINT32)-1 - right_len - 2)
        return NULL;
    CCUINT32 need = left_len + (has_sep ? 0 : 1) + right_len + 1;
    char *out = (char *)ccalloc(
        NULL, (CCUINT64)need, CC_PATH_COMMENT
    );
    if (!out)
        return NULL;
    memcpy(out, left, left_len);
    CCUINT32 pos = left_len;
    if (!has_sep)
        out[pos++] = '/';
    memcpy(out + pos, right, right_len + 1);
    return out;
}

CCBOOL ccpath_is_absolute(const char *path)
{
    if (!path || !path[0])
        return CCFALSE;
    CCUINT32 root_len = 0;
    if (!path_prefix_len(path, &root_len))
        return CCFALSE;
    return (root_len > 0) ? CCTRUE : CCFALSE;
}

char *ccpath_resolve(const char *path, const char *base)
{
    if (!path || !path[0])
        return NULL;
    if (path_is_bad_abs(path))
        return NULL;

    if (ccpath_is_absolute(path))
        return path_normalize_abs(path);

    char *base_abs = NULL;
    if (base && base[0])
    {
        if (path_is_bad_abs(base))
            return NULL;
        if (ccpath_is_absolute(base))
        {
            base_abs = path_normalize_abs(base);
        }
        else
        {
            char *cwd = path_cwd();
            char *joined = cwd ? path_join_raw(cwd, base) : NULL;
            if (joined)
                base_abs = path_normalize_abs(joined);
            if (joined)
                ccalloc(joined, 0, NULL);
            if (cwd)
                ccalloc(cwd, 0, NULL);
        }
    }
    else
    {
        base_abs = path_cwd();
    }
    if (!base_abs)
        return NULL;

    char *joined = path_join_raw(base_abs, path);
    ccalloc(base_abs, 0, NULL);
    if (!joined)
        return NULL;
    char *result = path_normalize_abs(joined);
    ccalloc(joined, 0, NULL);
    return result;
}

char *ccpath_parent(const char *path)
{
    if (!path || !path[0])
        return NULL;
    if (path_is_bad_abs(path))
        return NULL;

    CCUINT32 root_len = 0;
    if (!path_prefix_len(path, &root_len))
        return NULL;

    CCUINT32 len = (CCUINT32)strlen(path);
    while (len > root_len && path_is_sep(path[len - 1]))
        len--;

    if (len == root_len)
    {
        if (root_len > 0)
            return path_dup_range(path, root_len);
        return path_dup_range(".", 1);
    }

    CCINT32 last_sep = -1;
    for (CCINT32 i = (CCINT32)len - 1; i >= 0; i--)
    {
        if (path_is_sep(path[i]))
        {
            last_sep = i;
            break;
        }
    }

    if (last_sep < 0)
    {
        if (root_len > 0)
            return path_dup_range(path, root_len);
        return path_dup_range(".", 1);
    }

    CCUINT32 parent_len = (CCUINT32)last_sep;
    while (parent_len > root_len && path_is_sep(path[parent_len - 1]))
        parent_len--;
    if (parent_len < root_len)
        parent_len = root_len;
    return path_dup_range(path, parent_len);
}

char *ccpath_basename(const char *path)
{
    if (!path || !path[0])
        return NULL;
    if (path_is_bad_abs(path))
        return NULL;

    CCUINT32 root_len = 0;
    if (!path_prefix_len(path, &root_len))
        return NULL;

    CCUINT32 len = (CCUINT32)strlen(path);
    while (len > root_len && path_is_sep(path[len - 1]))
        len--;

    if (len == root_len)
    {
#ifdef CC_WINDOWS
        if (root_len > 1 && path_is_sep(path[0]) &&
            path_is_sep(path[1]))
        {
            CCUINT32 start = root_len;
            while (start > 0 && !path_is_sep(path[start - 1]))
                start--;
            return path_dup_range(path + start, root_len - start);
        }
#endif
        return path_dup_range("", 0);
    }

    CCUINT32 start = len;
    while (start > 0 && !path_is_sep(path[start - 1]))
        start--;
    return path_dup_range(path + start, len - start);
}

char *ccpath_join(const char *a, const char *b)
{
    if (!a || !a[0])
    {
        if (b && b[0])
            return path_dup_range(b, (CCUINT32)strlen(b));
        return path_dup_range("", 0);
    }
    if (!b || !b[0])
        return path_dup_range(a, (CCUINT32)strlen(a));
    if (ccpath_is_absolute(b))
        return path_dup_range(b, (CCUINT32)strlen(b));

    CCUINT32 a_len = (CCUINT32)strlen(a);
    CCUINT32 a_root = 0;
    if (ccpath_is_absolute(a))
        (void)path_prefix_len(a, &a_root);
    while (a_len > a_root && path_is_sep(a[a_len - 1]))
        a_len--;

    CCUINT32 b_len = (CCUINT32)strlen(b);
    CCUINT32 b_start = 0;
    while (b_start < b_len && path_is_sep(b[b_start]))
        b_start++;
    b_len -= b_start;

    CCBOOL need_sep = (a_len > 0 &&
        !path_is_sep(a[a_len - 1])) ? CCTRUE : CCFALSE;
    if (a_len > (CCUINT32)-1 - b_len - 2)
        return NULL;
    CCUINT32 need = a_len + (need_sep ? 1 : 0) + b_len + 1;
    char *out = (char *)ccalloc(
        NULL, (CCUINT64)need, CC_PATH_COMMENT
    );
    if (!out)
        return NULL;
    memcpy(out, a, a_len);
    CCUINT32 pos = a_len;
    if (need_sep)
        out[pos++] = '/';
    memcpy(out + pos, b + b_start, b_len);
    out[pos + b_len] = '\0';
    return out;
}

CCBOOL ccpath_equal(const char *a, const char *b)
{
    if (!a && !b)
        return CCTRUE;
    if (!a || !b)
        return CCFALSE;
#ifdef CC_WINDOWS
    while (*a && *b)
    {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return CCFALSE;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? CCTRUE : CCFALSE;
#else
    return (strcmp(a, b) == 0) ? CCTRUE : CCFALSE;
#endif
}
