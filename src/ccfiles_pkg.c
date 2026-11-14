/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles 资源包：格式编解码、包投影、打包与解包。
 *               磁盘格式固定小端，定义见 ccfiles_internal.h。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles.h"
#include "ccfiles_internal.h"

#include <string.h>
#include <stdio.h>

/* ============================================================================
 * 资源包格式（定义见 ccfiles_internal.h）
 * ========================================================================== */

/* ---- 小端编解码：资源包磁盘格式固定小端，与宿主字节序无关 ---- */

static void fs_le16_put(CCUINT8 *p, CCUINT16 v)
{
    p[0] = (CCUINT8)(v & 0xFFu);
    p[1] = (CCUINT8)((v >> 8) & 0xFFu);
}

static CCUINT16 fs_le16_get(const CCUINT8 *p)
{
    return (CCUINT16)((CCUINT16)p[0] | ((CCUINT16)p[1] << 8));
}

static void fs_le32_put(CCUINT8 *p, CCUINT32 v)
{
    p[0] = (CCUINT8)(v & 0xFFu);
    p[1] = (CCUINT8)((v >> 8) & 0xFFu);
    p[2] = (CCUINT8)((v >> 16) & 0xFFu);
    p[3] = (CCUINT8)((v >> 24) & 0xFFu);
}

static CCUINT32 fs_le32_get(const CCUINT8 *p)
{
    return (CCUINT32)p[0] | ((CCUINT32)p[1] << 8) |
           ((CCUINT32)p[2] << 16) | ((CCUINT32)p[3] << 24);
}

static void fs_le64_put(CCUINT8 *p, CCUINT64 v)
{
    for (CCUINT32 i = 0; i < 8; i++)
        p[i] = (CCUINT8)((v >> (8u * i)) & 0xFFu);
}

static CCUINT64 fs_le64_get(const CCUINT8 *p)
{
    CCUINT64 v = 0;
    for (CCUINT32 i = 0; i < 8; i++)
        v |= (CCUINT64)p[i] << (8u * i);
    return v;
}

/* 落盘魔数字节序列：'_CPK'，与端序无关 */
static const CCUINT8 fs_pkg_magic[CC_FS_PKG_MAGIC_SIZE] = {'_', 'C', 'P', 'K'};

/* 取已打开流的总长度（保留当前位置） */
static CCINT32 fs_stream_size(FILE *fp, CCUINT64 *out)
{
    if (!fp || !out)
        return CC_FS_ERR_PARAM;
#ifdef CC_WINDOWS
    __int64 cur = _ftelli64(fp);
    if (cur < 0)
        return CC_FS_ERR_IO;
    if (_fseeki64(fp, 0, SEEK_END) != 0)
        return CC_FS_ERR_IO;
    __int64 end = _ftelli64(fp);
    if (end < 0)
        return CC_FS_ERR_IO;
    if (_fseeki64(fp, cur, SEEK_SET) != 0)
        return CC_FS_ERR_IO;
    *out = (CCUINT64)end;
#else
    off_t cur = ftello(fp);
    if (cur < 0)
        return CC_FS_ERR_IO;
    if (fseeko(fp, 0, SEEK_END) != 0)
        return CC_FS_ERR_IO;
    off_t end = ftello(fp);
    if (end < 0)
        return CC_FS_ERR_IO;
    if (fseeko(fp, cur, SEEK_SET) != 0)
        return CC_FS_ERR_IO;
    *out = (CCUINT64)end;
#endif
    return CC_FS_OK;
}

/* 头部字段 -> 32 字节小端磁盘镜像（header_crc 由调用方回填） */
static void fs_pkg_header_put(CCUINT8 *raw, const CCFsPkgHeader *h)
{
    memcpy(raw, fs_pkg_magic, CC_FS_PKG_MAGIC_SIZE);
    fs_le16_put(raw + 4, h->version);
    fs_le16_put(raw + 6, h->flags);
    fs_le32_put(raw + 8, h->file_count);
    fs_le32_put(raw + 12, h->block_count);
    fs_le64_put(raw + 16, h->data_size);
    fs_le32_put(raw + 24, h->header_crc);
    fs_le32_put(raw + 28, h->reserved);
}

/* 32 字节小端磁盘镜像 -> 头部字段 */
static void fs_pkg_header_get(const CCUINT8 *raw, CCFsPkgHeader *h)
{
    memcpy(h->magic, raw, CC_FS_PKG_MAGIC_SIZE);
    h->version = fs_le16_get(raw + 4);
    h->flags = fs_le16_get(raw + 6);
    h->file_count = fs_le32_get(raw + 8);
    h->block_count = fs_le32_get(raw + 12);
    h->data_size = fs_le64_get(raw + 16);
    h->header_crc = fs_le32_get(raw + 24);
    h->reserved = fs_le32_get(raw + 28);
}

/* 块字段 -> 32 字节小端磁盘镜像 */
static void fs_pkg_block_put(CCUINT8 *raw, const CCFsPkgBlock *b)
{
    fs_le32_put(raw + 0, b->name_len);
    fs_le32_put(raw + 4, b->name_off);
    fs_le64_put(raw + 8, b->offset);
    fs_le64_put(raw + 16, b->size);
    fs_le32_put(raw + 24, b->crc32);
    fs_le32_put(raw + 28, b->flags);
}

/* 32 字节小端磁盘镜像 -> 块字段 */
static void fs_pkg_block_get(const CCUINT8 *raw, CCFsPkgBlock *b)
{
    b->name_len = fs_le32_get(raw + 0);
    b->name_off = fs_le32_get(raw + 4);
    b->offset = fs_le64_get(raw + 8);
    b->size = fs_le64_get(raw + 16);
    b->crc32 = fs_le32_get(raw + 24);
    b->flags = fs_le32_get(raw + 28);
}

/* 头部 CRC：覆盖磁盘字节中除 header_crc 字段自身外的 28 字节（含 reserved） */
static CCUINT32 fs_pkg_header_crc(const CCUINT8 *raw)
{
    CCUINT32 crc = cc_crc32(raw, 24, 0);
    return cc_crc32(raw + 28, 4, crc);
}

/* 包内名字必须是相对路径：非空、不以 `/` 开头、无空段 / `.` / `..`、无内嵌 NUL、段尾不得为空格 / 点、无 `\` */
static CCBOOL fs_pkg_name_ok(const char *name, CCUINT32 len)
{
    if (!name || len == 0)
        return CCFALSE;
    if (name[0] == '/' || name[len - 1] == '/')
        return CCFALSE;
    if (memchr(name, '\0', len) != NULL)
        return CCFALSE;
    if (memchr(name, '\\', len) != NULL)
        return CCFALSE;

    CCUINT32 i = 0;
    while (i < len)
    {
        CCUINT32 start = i;
        while (i < len && name[i] != '/')
            i++;
        CCUINT32 seg = i - start;
        if (seg == 0)
            return CCFALSE;
        if ((seg == 1 && name[start] == '.') ||
            (seg == 2 && name[start] == '.' && name[start + 1] == '.'))
            return CCFALSE;
        /* Windows 会忽略段末尾的空格 / 点，拒绝以免包名绕出 staging 目录 */
        if (name[i - 1] == ' ' || name[i - 1] == '.')
            return CCFALSE;
        if (i < len)
            i++; /* 跳过 '/' */
    }
    return CCTRUE;
}

/* 读取并校验资源包头 + 块表。成功时 *out_blocks 由调用方 ccalloc(..., 0, NULL) 释放。
   校验项：魔数 / 版本 / header_crc / 文件长度与块表边界 / data_size /
   每块的 name 区与数据区范围 / 目录块 offset、size 为 0 / file_count 一致。 */
static CCINT32 fs_pkg_read_header(FILE *fp, CCFsPkgHeader *h,
                                  CCFsPkgBlock **out_blocks)
{
    if (!fp || !h || !out_blocks)
        return CC_FS_ERR_PARAM;
    *out_blocks = NULL;

    CCUINT8 raw[CC_FS_PKG_HEADER_SIZE];
    if (fread(raw, 1, sizeof(raw), fp) != sizeof(raw))
        return CC_FS_ERR_IO;
    if (memcmp(raw, fs_pkg_magic, CC_FS_PKG_MAGIC_SIZE) != 0)
        return CC_FS_ERR_PARAM;

    fs_pkg_header_get(raw, h);
    if (h->version != CC_FS_PKG_VERSION)
        return CC_FS_ERR_PARAM;
    if (fs_pkg_header_crc(raw) != h->header_crc)
        return CC_FS_ERR_PARAM;

    CCUINT64 total = 0;
    CCINT32 r = fs_stream_size(fp, &total);
    if (r != CC_FS_OK)
        return r;

    /* 块表必须整体落在文件内（先按总长限界，避免按 block_count 巨额分配） */
    if ((CCUINT64)h->block_count >
        (total - CC_FS_PKG_HEADER_SIZE) / CC_FS_PKG_BLOCK_SIZE)
        return CC_FS_ERR_PARAM;

    CCUINT64 table_end = (CCUINT64)CC_FS_PKG_HEADER_SIZE +
                         (CCUINT64)h->block_count * CC_FS_PKG_BLOCK_SIZE;
    if (h->data_size > total - table_end)
        return CC_FS_ERR_PARAM;
    CCUINT64 data_start = total - h->data_size;
    CCUINT64 name_area_len = data_start - table_end;

    CCFsPkgBlock *blocks = NULL;
    CCINT32 ret = CC_FS_OK;

    if (h->block_count > 0)
    {
        blocks = (CCFsPkgBlock *)ccalloc(NULL,
                                         (CCUINT64)h->block_count * sizeof(CCFsPkgBlock),
                                         CC_DBG_REL_TAG("资源包块表", NULL));
        if (!blocks)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoAllocateBlocks;
        }
    }

    CCUINT64 data_sum = 0;
    CCUINT32 files = 0;
    for (CCUINT32 i = 0; i < h->block_count; i++)
    {
        CCUINT8 braw[CC_FS_PKG_BLOCK_SIZE];
        if (fread(braw, 1, sizeof(braw), fp) != sizeof(braw))
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoReadBlocks;
        }
        fs_pkg_block_get(braw, &blocks[i]);

        CCFsPkgBlock *b = &blocks[i];
        if (b->name_len == 0 ||
            (CCUINT64)b->name_off + (CCUINT64)b->name_len > name_area_len)
        {
            ret = CC_FS_ERR_PARAM;
            goto FailedtoReadBlocks;
        }
        if (b->flags & CC_FS_PKG_FLAG_DIR)
        {
            if (b->offset != 0 || b->size != 0)
            {
                ret = CC_FS_ERR_PARAM;
                goto FailedtoReadBlocks;
            }
        }
        else
        {
            files++;
            if (b->offset < data_start || b->offset > total ||
                b->size > total - b->offset)
            {
                ret = CC_FS_ERR_PARAM;
                goto FailedtoReadBlocks;
            }
            data_sum += b->size;
        }
    }

    if (data_sum != h->data_size || files != h->file_count)
    {
        ret = CC_FS_ERR_PARAM;
        goto FailedtoReadBlocks;
    }

    *out_blocks = blocks;
    return CC_FS_OK;

FailedtoReadBlocks:
    ccalloc(blocks, 0, NULL);
FailedtoAllocateBlocks:
    return ret;
}
/* ============================================================================
 * 包投影：读入块表并构建只读子树
 * ========================================================================== */

static CCUINT32 fs_pkg_make_child(CCUINT32 parent_idx, const char *name,
                                  CCBOOL is_dir, CCUINT64 off, CCUINT64 size, CCUINT32 crc)
{
    CCUINT32 existing = fs_child_get_idx(parent_idx, name);
    if (existing != CC_FS_NIL)
        return existing;

    CCUINT32 idx = fs_create_node(parent_idx, name,
                                  is_dir ? CC_FS_TYPE_DIR : CC_FS_TYPE_FILE, NULL, CC_FS_MOUNT_PKG, FS_ORIGIN_PKG);
    if (idx == CC_FS_NIL)
        return CC_FS_NIL;

    CCFsNode *n = node_at(idx);
    if (n->mount)
    {
        n->mount->type = CC_FS_MOUNT_PKG;
        n->mount->pkg_off = off;
        n->mount->pkg_size = size;
        n->mount->origin = FS_ORIGIN_PKG;
    }
    n->size = size;
    n->crc32 = crc;
    n->mtime = 0;
    n->ctime = 0;

    if (fs_insert_child(parent_idx, idx) != 0)
    {
        fs_mount_free(node_at(idx)->mount);
        if (node_at(idx)->name)
            ccalloc(node_at(idx)->name, 0, NULL);
        fs_alloc_rollback();
        return CC_FS_NIL;
    }
    return idx;
}

/* 读取包文件并把它投影到 anchor_idx 之下 */
CCINT32 fs_load_pkg_into(CCUINT32 anchor_idx, const char *pkg_path)
{
    if (!pkg_path)
        return CC_FS_ERR_PARAM;

    FILE *fp = NULL;
    CCFsPkgHeader h;
    CCFsPkgBlock *blocks = NULL;
    char *name = NULL;
    CCINT32 ret = CC_FS_OK;

    fp = fopen(pkg_path, "rb");
    if (!fp)
        return CC_FS_ERR_IO;

    ret = fs_pkg_read_header(fp, &h, &blocks);
    if (ret != CC_FS_OK)
        goto FailedtoReadHeader;

    CCUINT64 name_area = (CCUINT64)CC_FS_PKG_HEADER_SIZE +
                         (CCUINT64)h.block_count * CC_FS_PKG_BLOCK_SIZE;

    for (CCUINT32 i = 0; i < h.block_count; i++)
    {
        CCFsPkgBlock *b = &blocks[i];
        /* 包内名称按 name_len 动态读取，长度只受内存约束 */
        name = (char *)ccalloc(NULL, (CCUINT64)b->name_len + 1,
                               CC_DBG_REL_TAG("包内路径", NULL));
        if (!name)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoAllocName;
        }
        if (fs_fseek(fp, name_area + b->name_off) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoReadName;
        }
        if (fread(name, 1, b->name_len, fp) != b->name_len)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoReadName;
        }
        name[b->name_len] = '\0';
        if (!fs_pkg_name_ok(name, b->name_len))
        {
            ret = CC_FS_ERR_PARAM;
            goto FailedtoReadName;
        }

        CCUINT32 parent = anchor_idx;
        char *p = name;
        while (*p)
        {
            while (*p == '/')
                p++;
            if (!*p)
                break;
            char *start = p;
            while (*p && *p != '/')
                p++;
            char saved = *p;
            *p = '\0';
            CCBOOL is_last = (saved == '\0');

            if (is_last)
            {
                CCBOOL is_dir = (b->flags & CC_FS_PKG_FLAG_DIR) ? CCTRUE : CCFALSE;
                if (fs_pkg_make_child(parent, start, is_dir,
                                      b->offset, b->size, b->crc32) == CC_FS_NIL)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto FailedtoMakeChild;
                }
            }
            else
            {
                CCUINT32 mid = fs_pkg_make_child(parent, start, CCTRUE, 0, 0, 0);
                if (mid == CC_FS_NIL)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto FailedtoMakeChild;
                }
                parent = mid;
            }

            if (saved == '\0')
                break;
            *p = saved;
        }
        ccalloc(name, 0, NULL);
        name = NULL;
    }

    if (blocks)
        ccalloc(blocks, 0, NULL);
    fclose(fp);
    return CC_FS_OK;

FailedtoMakeChild:
FailedtoReadName:
    ccalloc(name, 0, NULL);
FailedtoAllocName:
FailedtoReadHeader:
    if (blocks)
        ccalloc(blocks, 0, NULL);
    fclose(fp);
    return ret;
}
/* ============================================================================
 * 打包 / 解包
 * ========================================================================== */

typedef struct cc_fs_pkg_entry
{
    char *name; /* 相对路径（`/` 分隔） */
    CCUINT32 name_len;
    CCUINT32 name_off;
    CCBOOL is_dir;
    CCUINT32 node_idx;
    CCUINT64 offset; /* 数据在包内的绝对偏移 */
    CCUINT64 size;
    CCUINT32 crc32;
} CCFsPkgEntry;

typedef struct cc_fs_pack_item
{
    CCUINT32 dir;
    char *prefix; /* 堆分配 */
} CCFsPackItem;

/* 释放条目表内所有名称与条目数组本身 */
static void fs_pkg_entries_free(CCDyn *entries)
{
    if (!entries)
        return;
    CCUINT32 n = entries->size / (CCUINT32)sizeof(CCFsPkgEntry);
    CCFsPkgEntry *ea = (CCFsPkgEntry *)entries->array;
    for (CCUINT32 i = 0; i < n; i++)
    {
        if (ea[i].name)
            ccalloc(ea[i].name, 0, NULL);
    }
    fs_buf_free(entries);
}

/* 弹出并释放栈内所有尚未处理的目录前缀 */
static void fs_pack_items_free(CCDyn *stack)
{
    if (!stack)
        return;
    while (stack->size >= (CCUINT32)sizeof(CCFsPackItem))
    {
        CCFsPackItem it;
        if (cc_dyn_core_pop(stack, (CCUINT32)sizeof(CCFsPackItem),
                            (CCINT32)sizeof(CCFsPackItem), &it) != 0)
            break;
        if (it.prefix)
            ccalloc(it.prefix, 0, NULL);
    }
    fs_buf_free(stack);
}

/* 收集整棵目录树的打包条目；显式栈迭代，无递归 */
static CCINT32 fs_pack_collect(CCUINT32 dir_idx, const char *prefix, CCDyn *entries)
{
    CCDyn stack;
    memset(&stack, 0, sizeof(stack));
    CCDyn full;
    memset(&full, 0, sizeof(full));
    CCDyn cpfx;
    memset(&cpfx, 0, sizeof(cpfx));
    CCDyn kids;
    memset(&kids, 0, sizeof(kids));
    CCFsPackItem cur;
    memset(&cur, 0, sizeof(cur));
    CCFsPkgEntry e;
    memset(&e, 0, sizeof(e));
    char *p0 = NULL;
    char *dup = NULL;
    CCINT32 ret = CC_FS_OK;

    p0 = fs_strdup(prefix, CC_DBG_REL_TAG("打包前缀", NULL));
    if (!p0)
    {
        ret = CC_FS_ERR_NOSPACE;
        goto FailedtoAllocRoot;
    }

    CCFsPackItem it;
    it.dir = dir_idx;
    it.prefix = p0;
    if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCFsPackItem),
                         (CCINT32)sizeof(CCFsPackItem), &it) != 0)
    {
        ret = CC_FS_ERR_NOSPACE;
        goto FailedtoPushRoot;
    }
    p0 = NULL;

    while (stack.size >= (CCUINT32)sizeof(CCFsPackItem))
    {
        if (cc_dyn_core_pop(&stack, (CCUINT32)sizeof(CCFsPackItem),
                            (CCINT32)sizeof(CCFsPackItem), &cur) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoPop;
        }

        fs_collect_children(cur.dir, &kids);
        CCUINT32 count = kids.size / (CCUINT32)sizeof(CCUINT32);
        CCUINT32 *arr = (CCUINT32 *)kids.array;

        for (CCUINT32 i = 0; i < count; i++)
        {
            CCUINT32 kid = arr[i];
            CCFsNode *n = node_at(kid);
            if (n->type == CC_FS_TYPE_NONE || !n->name)
                continue;

            fs_buf_clear(&full);
            if (fs_buf_put(&full, cur.prefix) != CC_FS_OK ||
                fs_buf_put(&full, n->name) != CC_FS_OK)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoBuildEntry;
            }

            memset(&e, 0, sizeof(e));
            e.name = fs_strdup(fs_buf_str(&full),
                               CC_DBG_REL_TAG("打包条目名称", NULL));
            if (!e.name)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoPushEntry;
            }
            e.name_len = (CCUINT32)strlen(fs_buf_str(&full));
            e.is_dir = (n->type == CC_FS_TYPE_DIR) ? CCTRUE : CCFALSE;
            e.node_idx = kid;
            e.size = e.is_dir ? 0 : n->size;
            e.crc32 = n->crc32;

            if (cc_dyn_core_push(entries, (CCUINT32)sizeof(CCFsPkgEntry),
                                 (CCINT32)sizeof(CCFsPkgEntry), &e) != 0)
            {
                ret = CC_FS_ERR_NOSPACE;
                goto FailedtoPushEntry;
            }
            e.name = NULL;

            if (e.is_dir)
            {
                fs_buf_clear(&cpfx);
                if (fs_buf_put(&cpfx, cur.prefix) != CC_FS_OK ||
                    fs_buf_put(&cpfx, n->name) != CC_FS_OK ||
                    fs_buf_put(&cpfx, "/") != CC_FS_OK)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto FailedtoBuildPrefix;
                }

                dup = fs_strdup(fs_buf_str(&cpfx),
                                CC_DBG_REL_TAG("打包前缀", NULL));
                if (!dup)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto FailedtoBuildPrefix;
                }

                CCFsPackItem child;
                child.dir = kid;
                child.prefix = dup;
                if (cc_dyn_core_push(&stack, (CCUINT32)sizeof(CCFsPackItem),
                                     (CCINT32)sizeof(CCFsPackItem), &child) != 0)
                {
                    ret = CC_FS_ERR_NOSPACE;
                    goto FailedtoPushChild;
                }
                dup = NULL;
            }
        }

        if (kids.array)
            ccalloc(kids.array, 0, NULL);
        kids.array = NULL;
        kids.size = 0;
        kids.capacity = 0;
        ccalloc(cur.prefix, 0, NULL);
        cur.prefix = NULL;
    }

    fs_buf_free(&full);
    fs_buf_free(&cpfx);
    fs_pack_items_free(&stack);
    return CC_FS_OK;

FailedtoPushChild:
    ccalloc(dup, 0, NULL);
FailedtoBuildPrefix:
    fs_buf_free(&cpfx);
FailedtoPushEntry:
    ccalloc(e.name, 0, NULL);
FailedtoBuildEntry:
    fs_buf_free(&full);
    if (kids.array)
        ccalloc(kids.array, 0, NULL);
    ccalloc(cur.prefix, 0, NULL);
FailedtoPop:
    fs_pack_items_free(&stack);
FailedtoPushRoot:
    ccalloc(p0, 0, NULL);
    fs_buf_free(&stack);
FailedtoAllocRoot:
    return ret;
}

/* 逻辑文件全量 CRC（经物理层 / 包内块） */
static CCUINT32 fs_crc_of_file(CCUINT32 idx)
{
    CCUINT8 tmp[64 * 1024];
    CCUINT64 total = node_at(idx)->size;
    CCUINT64 pos = 0;
    CCUINT32 crc = 0;
    while (pos < total)
    {
        CCUINT64 n = total - pos;
        if (n > sizeof(tmp))
            n = sizeof(tmp);
        CCINT64 got = fs_read_raw(idx, tmp, pos, n);
        if (got <= 0)
            break;
        crc = cc_crc32(tmp, (CCUINT64)got, crc);
        pos += (CCUINT64)got;
    }
    fs_fh_release_if_unused(idx);
    return crc;
}

/* 解出一个文件块：先写 dst.part，完整关闭后再 rename 到 dst */
static CCINT32 fs_unpack_file(FILE *fp, const CCFsPkgBlock *b, const char *dst)
{
    if (!fp || !b || !dst)
        return CC_FS_ERR_PARAM;

    CCINT32 ret = CC_FS_OK;
    CCDyn tmp;
    memset(&tmp, 0, sizeof(tmp));
    FILE *of = NULL;

    ret = fs_buf_put(&tmp, dst);
    if (ret == CC_FS_OK)
        ret = fs_buf_put(&tmp, ".part");
    if (ret != CC_FS_OK)
        goto FailedtoMakeTemp;

    if (fs_fseek(fp, b->offset) != 0)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoSeek;
    }

    of = fopen(fs_buf_str(&tmp), "wb");
    if (!of)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoOpenDest;
    }

    CCUINT64 remain = b->size;
    CCUINT8 buf[64 * 1024];
    while (remain > 0)
    {
        size_t want = (size_t)(remain > sizeof(buf) ? sizeof(buf) : remain);
        size_t got = fread(buf, 1, want, fp);
        if (got != want)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoCopy;
        }
        if (fwrite(buf, 1, got, of) != got)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoCopy;
        }
        remain -= (CCUINT64)got;
    }

    if (fclose(of) != 0)
    {
        of = NULL;
        ret = CC_FS_ERR_IO;
        goto FailedtoCloseDest;
    }
    of = NULL;

    ret = fs_rename_path(fs_buf_str(&tmp), dst);
    if (ret != CC_FS_OK)
        goto FailedtoRename;

    fs_buf_free(&tmp);
    return CC_FS_OK;

FailedtoRename:
FailedtoCloseDest:
FailedtoCopy:
    if (of)
        fclose(of);
    fs_unlink_file(fs_buf_str(&tmp));
FailedtoOpenDest:
FailedtoSeek:
    fs_unlink_file(fs_buf_str(&tmp));
FailedtoMakeTemp:
    fs_buf_free(&tmp);
    return ret;
}

CCINT32 ccfiles_pack(CCFiles *fs, const char *v_path, const char *real_path)
{
    if (!fs || !v_path || !real_path)
        return CC_FS_ERR_PARAM;

    CCINT32 ret = CC_FS_OK;
    CCUINT32 idx = CC_FS_NIL;
    CCDyn out;
    memset(&out, 0, sizeof(out));
    CCDyn entries;
    memset(&entries, 0, sizeof(entries));
    CCDyn temp;
    memset(&temp, 0, sizeof(temp));
    FILE *fp = NULL;
    CCUINT32 block_count = 0;
    CCFsPkgEntry *ea = NULL;

    ret = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (ret != CC_FS_OK)
        goto FailedtoWalk;
    if (idx == CC_FS_NIL)
    {
        ret = CC_FS_ERR_NOENT;
        goto FailedtoWalk;
    }
    if (node_at(idx)->type != CC_FS_TYPE_DIR)
    {
        ret = CC_FS_ERR_NOTDIR;
        goto FailedtoWalk;
    }

    ret = fs_real_resolve(real_path, &out);
    if (ret != CC_FS_OK)
        goto FailedtoResolve;
    const char *outp = fs_buf_str(&out);
    if (fs_file_exists(outp))
    {
        ret = CC_FS_ERR_EXIST;
        goto FailedtoCheckTarget;
    }

    ret = fs_pack_collect(idx, "", &entries);
    if (ret != CC_FS_OK)
        goto FailedtoCollect;

    block_count = entries.size / (CCUINT32)sizeof(CCFsPkgEntry);
    ea = (CCFsPkgEntry *)entries.array;

    CCUINT64 name_area = 0;
    for (CCUINT32 i = 0; i < block_count; i++)
    {
        ea[i].name_off = (CCUINT32)name_area;
        name_area += ea[i].name_len;
    }

    CCUINT64 data_start = (CCUINT64)CC_FS_PKG_HEADER_SIZE +
                          (CCUINT64)block_count * CC_FS_PKG_BLOCK_SIZE + name_area;
    CCUINT64 data_cursor = data_start;
    CCUINT32 file_count = 0;

    for (CCUINT32 i = 0; i < block_count; i++)
    {
        if (ea[i].is_dir)
        {
            ea[i].offset = 0;
            continue;
        }
        file_count++;
        ea[i].crc32 = fs_crc_of_file(ea[i].node_idx);
        ea[i].offset = data_cursor;
        data_cursor += ea[i].size;
    }

    /* 先写同目录临时文件，完整落盘并关闭后再原子发布最终路径 */
    ret = fs_buf_put(&temp, outp);
    if (ret == CC_FS_OK)
        ret = fs_buf_put(&temp, ".part");
    if (ret != CC_FS_OK)
        goto FailedtoMakeTemp;

    fp = fopen(fs_buf_str(&temp), "wb");
    if (!fp)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoOpenTemp;
    }

    CCFsPkgHeader h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, fs_pkg_magic, CC_FS_PKG_MAGIC_SIZE);
    h.version = CC_FS_PKG_VERSION;
    h.flags = 0;
    h.file_count = file_count;
    h.block_count = block_count;
    h.data_size = data_cursor - data_start;
    h.header_crc = 0;
    h.reserved = 0;

    /* 序列化为固定小端磁盘镜像：CRC 覆盖磁盘字节，与宿主端序无关 */
    CCUINT8 hraw[CC_FS_PKG_HEADER_SIZE];
    fs_pkg_header_put(hraw, &h);
    h.header_crc = fs_pkg_header_crc(hraw);
    fs_le32_put(hraw + 24, h.header_crc);

    if (fwrite(hraw, 1, sizeof(hraw), fp) != sizeof(hraw))
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoWritePackage;
    }

    for (CCUINT32 i = 0; i < block_count; i++)
    {
        CCFsPkgBlock b;
        memset(&b, 0, sizeof(b));
        b.name_len = ea[i].name_len;
        b.name_off = ea[i].name_off;
        b.offset = ea[i].is_dir ? 0 : ea[i].offset;
        b.size = ea[i].size;
        b.crc32 = ea[i].crc32;
        b.flags = ea[i].is_dir ? CC_FS_PKG_FLAG_DIR : 0;

        CCUINT8 braw[CC_FS_PKG_BLOCK_SIZE];
        fs_pkg_block_put(braw, &b);
        if (fwrite(braw, 1, sizeof(braw), fp) != sizeof(braw))
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoWritePackage;
        }
    }

    for (CCUINT32 i = 0; i < block_count; i++)
    {
        if (fwrite(ea[i].name, 1, ea[i].name_len, fp) != ea[i].name_len)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoWritePackage;
        }
    }

    for (CCUINT32 i = 0; i < block_count; i++)
    {
        if (ea[i].is_dir)
            continue;
        CCUINT64 pos = 0;
        CCUINT8 tmp[64 * 1024];
        while (pos < ea[i].size)
        {
            CCUINT64 n = ea[i].size - pos;
            if (n > sizeof(tmp))
                n = sizeof(tmp);
            CCINT64 got = fs_read_raw(ea[i].node_idx, tmp, pos, n);
            if (got <= 0)
            {
                ret = CC_FS_ERR_IO;
                goto FailedtoWritePackage;
            }
            if (fwrite(tmp, 1, (size_t)got, fp) != (size_t)got)
            {
                ret = CC_FS_ERR_IO;
                goto FailedtoWritePackage;
            }
            pos += (CCUINT64)got;
        }
        fs_fh_release_if_unused(ea[i].node_idx); /* 打包读取不长期占用 fd */
    }

    if (fclose(fp) != 0)
    {
        fp = NULL;
        ret = CC_FS_ERR_IO;
        goto FailedtoCloseTemp;
    }
    fp = NULL;

    ret = fs_rename_path(fs_buf_str(&temp), outp);
    if (ret != CC_FS_OK)
        goto FailedtoRename;

    fs_buf_free(&temp);
    fs_pkg_entries_free(&entries);
    fs_buf_free(&out);
    fs_fh_release_if_unused(idx);
    return CC_FS_OK;

FailedtoRename:
FailedtoCloseTemp:
FailedtoWritePackage:
    for (CCUINT32 i = 0; i < block_count; i++)
        fs_fh_release_if_unused(ea[i].node_idx);
    if (fp)
        fclose(fp);
    fs_unlink_file(fs_buf_str(&temp));
FailedtoOpenTemp:
FailedtoMakeTemp:
    fs_buf_free(&temp);
FailedtoCollect:
    fs_pkg_entries_free(&entries);
FailedtoCheckTarget:
FailedtoResolve:
    fs_buf_free(&out);
FailedtoWalk:
    if (idx != CC_FS_NIL)
        fs_fh_release_if_unused(idx);
    return ret;
}

CCINT32 ccfiles_unpack(CCFiles *fs, const char *v_path, const char *real_path)
{
    if (!fs || !v_path || !real_path)
        return CC_FS_ERR_PARAM;

    CCINT32 ret = CC_FS_OK;
    CCUINT32 idx = CC_FS_NIL;
    CCDyn src;
    memset(&src, 0, sizeof(src));
    CCDyn target;
    memset(&target, 0, sizeof(target));
    CCDyn stage;
    memset(&stage, 0, sizeof(stage));
    CCDyn dest;
    memset(&dest, 0, sizeof(dest));
    CCDyn dir;
    memset(&dir, 0, sizeof(dir));
    FILE *fp = NULL;
    CCFsPkgHeader h;
    CCFsPkgBlock *blocks = NULL;
    char *name = NULL;

    ret = fs_vwalk(fs, v_path, CCTRUE, NULL, NULL, &idx);
    if (ret != CC_FS_OK)
        goto FailedtoWalk;
    if (idx == CC_FS_NIL)
    {
        ret = CC_FS_ERR_NOENT;
        goto FailedtoWalk;
    }
    if (node_at(idx)->type != CC_FS_TYPE_FILE)
    {
        ret = CC_FS_ERR_ISDIR;
        goto FailedtoWalk;
    }

    /* 包文件自身的写缓冲必须先落盘，随后按物理路径 fopen 才能读到最新内容 */
    ret = fs_fh_flush(idx);
    if (ret != CC_FS_OK)
        goto FailedtoFlush;

    ret = fs_physical(idx, &src);
    if (ret != CC_FS_OK)
        goto FailedtoResolveSource;

    ret = fs_real_resolve(real_path, &target);
    if (ret != CC_FS_OK)
        goto FailedtoResolveTarget;
    const char *tgt = fs_buf_str(&target);
    if (fs_file_exists(tgt))
    {
        ret = CC_FS_ERR_EXIST;
        goto FailedtoCheckTarget;
    }

    fp = fopen(fs_buf_str(&src), "rb");
    if (!fp)
    {
        ret = CC_FS_ERR_IO;
        goto FailedtoOpenPackage;
    }

    ret = fs_pkg_read_header(fp, &h, &blocks);
    if (ret != CC_FS_OK)
        goto FailedtoReadHeader;

    CCUINT64 name_area = (CCUINT64)CC_FS_PKG_HEADER_SIZE +
                         (CCUINT64)h.block_count * CC_FS_PKG_BLOCK_SIZE;

    /* 先解到 tgt.part staging 树，全部成功后再整体 rename 发布 */
    ret = fs_buf_put(&stage, tgt);
    if (ret == CC_FS_OK)
        ret = fs_buf_put(&stage, ".part");
    if (ret != CC_FS_OK)
        goto FailedtoMakeStage;

    ret = fs_remove_tree(fs_buf_str(&stage));
    if (ret == CC_FS_OK)
        ret = fs_mkdirs(fs_buf_str(&stage));
    if (ret != CC_FS_OK)
        goto FailedtoCreateStage;

    for (CCUINT32 i = 0; i < h.block_count; i++)
    {
        CCFsPkgBlock *b = &blocks[i];
        /* 包内名称按 name_len 动态读取，长度只受内存约束 */
        name = (char *)ccalloc(NULL, (CCUINT64)b->name_len + 1,
                               CC_DBG_REL_TAG("解包名称", NULL));
        if (!name)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoAllocName;
        }
        if (fs_fseek(fp, name_area + b->name_off) != 0)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoReadName;
        }
        if (fread(name, 1, b->name_len, fp) != b->name_len)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoReadName;
        }
        name[b->name_len] = '\0';
        if (!fs_pkg_name_ok(name, b->name_len))
        {
            ret = CC_FS_ERR_PARAM;
            goto FailedtoReadName;
        }

        fs_buf_clear(&dest);
        if (fs_buf_put(&dest, fs_buf_str(&stage)) != CC_FS_OK ||
            fs_buf_put(&dest, "/") != CC_FS_OK ||
            fs_buf_put(&dest, name) != CC_FS_OK)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoReadName;
        }
        ccalloc(name, 0, NULL);
        name = NULL;
        const char *dstp = fs_buf_str(&dest);

        if (b->flags & CC_FS_PKG_FLAG_DIR)
        {
            if (fs_mkdirs(dstp) != CC_FS_OK)
            {
                ret = CC_FS_ERR_IO;
                goto FailedtoMakeBlockPath;
            }
            continue;
        }

        fs_buf_clear(&dir);
        if (fs_buf_put(&dir, dstp) != CC_FS_OK)
        {
            ret = CC_FS_ERR_NOSPACE;
            goto FailedtoMakeBlockPath;
        }
        fs_dir_of((char *)dir.array);
        if (fs_buf_str(&dir)[0] != '\0' &&
            fs_mkdirs(fs_buf_str(&dir)) != CC_FS_OK)
        {
            ret = CC_FS_ERR_IO;
            goto FailedtoMakeBlockPath;
        }

        ret = fs_unpack_file(fp, b, dstp);
        if (ret != CC_FS_OK)
            goto FailedtoUnpackBlocks;
    }

    ret = fs_rename_path(fs_buf_str(&stage), tgt);
    if (ret != CC_FS_OK)
        goto FailedtoPublish;

    fs_buf_free(&dest);
    fs_buf_free(&dir);
    fs_buf_free(&stage);
    if (blocks)
        ccalloc(blocks, 0, NULL);
    fclose(fp);
    fs_buf_free(&target);
    fs_buf_free(&src);
    fs_fh_release_if_unused(idx);
    return CC_FS_OK;

FailedtoReadName:
    ccalloc(name, 0, NULL);
FailedtoAllocName:
FailedtoPublish:
FailedtoUnpackBlocks:
FailedtoMakeBlockPath:
FailedtoCreateStage:
    fs_remove_tree(fs_buf_str(&stage));
    fs_buf_free(&dest);
    fs_buf_free(&dir);
    fs_buf_free(&stage);
FailedtoMakeStage:
    if (blocks)
        ccalloc(blocks, 0, NULL);
FailedtoReadHeader:
    if (fp)
        fclose(fp);
FailedtoOpenPackage:
FailedtoCheckTarget:
FailedtoResolveTarget:
    fs_buf_free(&target);
FailedtoResolveSource:
    fs_buf_free(&src);
FailedtoFlush:
FailedtoWalk:
    if (idx != CC_FS_NIL)
        fs_fh_release_if_unused(idx);
    return ret;
}
