/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles 读写层：节点常驻 FILE* 句柄、原始读写、公开的
 *               open / read / write / flush。物理身份变化由路径层统一处理。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>

/* ============================================================================
 * 节点常驻读写句柄
 * 句柄与探针引用同寿命：只要还有探针指向该节点（probe_head != NULL）就复用同一个
 * FILE*；顺序读写靠 fp_pos 省去 fseek。物理身份改变（删除 / 改名 / 跨锚点移动 /
 * refresh / 卸载 / 打包前）统一 flush + fclose，写失败一律如实上报。
 * ========================================================================== */

// 打开（或复用）节点句柄；want_write 时以可写方式打开，必要时从只读升级
CCINT32 fs_fh_open(CCUINT32 idx, CCBOOL want_write)
{
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_PARAM;
    CCFsNode *n = node_at(idx);
    if (n->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;
    if (fs_is_protected(idx))
        return CC_FS_ERR_PROTECT;

    if (n->fp)
    {
        if (!want_write || n->fp_writable)
            return CC_FS_OK;
        CCINT32 ret = fs_fh_close(idx);
        if (ret != CC_FS_OK)
            return ret;
        n = node_at(idx);
    }

    char *phys = fs_physical_dup(
        idx, CC_DBG_REL_TAG("读写路径", NULL)
    );
    if (!phys)
        return CC_FS_ERR_NOENT;

    FILE *fp = fopen(phys, "r+b");
    CCBOOL writable = CCTRUE;
    if (!fp)
    {
        if (want_write)
        {
            ccalloc(phys, 0, NULL);
            return CC_FS_ERR_IO;
        }
        fp = fopen(phys, "rb");
        writable = CCFALSE;
        if (!fp)
        {
            ccalloc(phys, 0, NULL);
            return CC_FS_ERR_IO;
        }
    }
    ccalloc(phys, 0, NULL);

    n = node_at(idx);
    n->fp = (void *)fp;
    n->fp_pos = 0;
    n->fp_writable = writable;
    return CC_FS_OK;
}

// 把某节点句柄的缓冲落盘；未打开视为成功
CCINT32 fs_fh_flush(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (!n->fp)
        return CC_FS_OK;
    return (fflush((FILE *)n->fp) == 0)
        ? CC_FS_OK : CC_FS_ERR_IO;
}

// flush + fclose 某节点句柄；失败如实返回，句柄一律置空
CCINT32 fs_fh_close(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (!n->fp)
        return CC_FS_OK;

    FILE *fp = (FILE *)n->fp;
    n->fp = NULL;
    n->fp_pos = 0;
    n->fp_writable = CCFALSE;

    CCINT32 ret = (fflush(fp) == 0)
        ? CC_FS_OK : CC_FS_ERR_IO;
    if (fclose(fp) != 0 && ret == CC_FS_OK)
        ret = CC_FS_ERR_IO;
    return ret;
}

CCINT32 fs_fh_flush_all(void)
{
    CCINT32 ret = CC_FS_OK;
    CCUINT32 count = node_count();
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCINT32 result = fs_fh_flush(i);
        if (result != CC_FS_OK && ret == CC_FS_OK)
            ret = result;
    }
    return ret;
}

CCINT32 fs_fh_close_all(void)
{
    CCINT32 ret = CC_FS_OK;
    CCUINT32 count = node_count();
    for (CCUINT32 i = 0; i < count; i++)
    {
        CCINT32 result = fs_fh_close(i);
        if (result != CC_FS_OK && ret == CC_FS_OK)
            ret = result;
    }
    return ret;
}

// 若节点已无探针指向，立即落盘并关闭其句柄
void fs_fh_release_if_unused(CCUINT32 idx)
{
    CCFsNode *n = node_at(idx);
    if (n->fp && !n->probe_head)
        fs_fh_close(idx);
}

// 读取物理层内容：包投影节点读包内数据，其余读真实文件（复用常驻句柄）
CCINT64 fs_read_raw(
    CCUINT32 idx, void *buf, CCUINT64 offset, CCUINT64 size
)
{
    if (!buf && size > 0)
        return CC_FS_ERR_PARAM;
    if (size == 0)
        return 0;

    CCFsNode *n = node_at(idx);
    if (n->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;

    CCUINT64 start = offset;
    FILE *fp = NULL;
    CCBOOL temp = CCFALSE;

    if (fs_is_protected(idx))
    {
        CCUINT32 anchor = fs_find_anchor(idx);
        if (anchor == CC_FS_NIL)
            return CC_FS_ERR_NOENT;
        if (anchor == idx)
            return CC_FS_ERR_PROTECT;
        fp = fopen(node_at(anchor)->mount->real_path, "rb");
        if (!fp)
            return CC_FS_ERR_IO;
        temp = CCTRUE;
        start = (n->mount ? n->mount->pkg_off : 0) + offset;
    }
    else
    {
        CCINT32 ret = fs_fh_open(idx, CCFALSE);
        if (ret != CC_FS_OK)
            return ret;
        n = node_at(idx);
        fp = (FILE *)n->fp;
    }

    if (fs_fseek(fp, start) != 0)
    {
        if (temp)
            fclose(fp);
        return CC_FS_ERR_IO;
    }

    CCUINT64 total = 0;
    while (total < size)
    {
        CCUINT64 remain = size - total;
        size_t want = (size_t)(
            remain > (1u << 20) ? (1u << 20) : remain
        );
        size_t got = fread(
            (CCUINT8 *)buf + total, 1, want, fp
        );
        total += got;
        if (got < want)
        {
            memset(
                (CCUINT8 *)buf + total, 0,
                (size_t)(size - total)
            );
            total = size;
            break;
        }
    }

    if (temp)
    {
        fclose(fp);
    }
    else
    {
        n = node_at(idx);
        n->fp_pos = start + total;
    }
    return (CCINT64)size;
}

// 直写物理层：按需扩展文件；返回实际写入字节数或负错误码
CCINT64 fs_write_raw(
    CCUINT32 idx, const void *data, CCUINT64 offset, CCUINT64 size
)
{
    if (!data && size > 0)
        return CC_FS_ERR_PARAM;
    if (size == 0)
        return 0;

    CCINT32 ret = fs_fh_open(idx, CCTRUE);
    if (ret != CC_FS_OK)
        return ret;

    CCFsNode *n = node_at(idx);
    FILE *fp = (FILE *)n->fp;
    if (n->fp_pos != offset)
    {
        if (fs_fseek(fp, offset) != 0)
            return CC_FS_ERR_IO;
        n->fp_pos = offset;
    }

    const CCUINT8 *p = (const CCUINT8 *)data;
    CCUINT64 done = 0;
    while (done < size)
    {
        CCUINT64 remain = size - done;
        size_t want = (size_t)(
            remain > (1u << 20) ? (1u << 20) : remain
        );
        size_t put = fwrite(p + done, 1, want, fp);
        done += (CCUINT64)put;
        if (put < want)
            return CC_FS_ERR_NOSPACE;
    }

    n->fp_pos = offset + size;
    n->mtime = fs_now_seconds();
    if (offset + size > n->size)
        n->size = offset + size;
    return (CCINT64)size;
}

/* ============================================================================
 * 文件读写与落盘
 * ========================================================================== */

CCINT32 ccfiles_open(CCFiles *fs, const char *v_path)
{
    if (!fs || !v_path)
        return CC_FS_ERR_PARAM;
    CCUINT32 idx = CC_FS_NIL;
    CCINT32 ret = fs_vwalk(
        fs, v_path, CCTRUE, NULL, NULL, &idx
    );
    if (ret != CC_FS_OK)
        return ret;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;
    if (node_at(idx)->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;

    if (fs->cur != CC_FS_NIL)
        fs_node_unref(fs);
    fs->cur = idx;
    fs_node_ref(fs, idx);
    return CC_FS_OK;
}

CCINT64 ccfiles_read(
    CCFiles *fs, void *buf, CCUINT64 offset, CCUINT64 size
)
{
    if (!fs || (!buf && size > 0))
        return CC_FS_ERR_PARAM;
    if (fs->cur == CC_FS_NIL)
        return CC_FS_ERR_PARAM;
    CCFsNode *n = node_at(fs->cur);
    if (n->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;
    if (size == 0)
        return 0;
    if (offset >= n->size)
        return 0;

    CCUINT64 avail = n->size - offset;
    if (size < avail)
        avail = size;

    CCINT64 got = fs_read_raw(
        fs->cur, buf, offset, avail
    );
    if (got < 0)
        return got;
    return (CCINT64)avail;
}

CCINT64 ccfiles_write(
    CCFiles *fs, const void *data, CCUINT64 offset, CCUINT64 size
)
{
    if (!fs || (!data && size > 0))
        return CC_FS_ERR_PARAM;
    if (fs->cur == CC_FS_NIL)
        return CC_FS_ERR_PARAM;
    CCFsNode *n = node_at(fs->cur);
    if (n->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;
    if (fs_is_protected(fs->cur))
        return CC_FS_ERR_PROTECT;
    if (size == 0)
        return 0;

    return fs_write_raw(fs->cur, data, offset, size);
}

// 把写缓冲显式刷到磁盘：fs == NULL 刷新全部已打开文件
CCINT32 ccfiles_flush(CCFiles *fs)
{
    if (!fs)
        return fs_fh_flush_all();
    if (fs->cur == CC_FS_NIL)
        return CC_FS_OK;
    return fs_fh_flush(fs->cur);
}
