/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MD5 摘要计算实现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include "ccfiles_internal.h"

#include <string.h>

#define CC_HASH_CHUNK 65536

typedef struct cc_md5_ctx
{
    CCUINT32 state[4];
    CCUINT64 total;
    CCUINT8 buffer[64];
    CCUINT32 used;
} CCMd5Ctx;

static const CCUINT32 md5_k[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const CCUINT8 md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static CCUINT32 md5_rotl(CCUINT32 value, CCUINT32 bits)
{
    return (value << bits) | (value >> (32u - bits));
}

static void md5_transform(CCMd5Ctx *ctx, const CCUINT8 block[64])
{
    CCUINT32 words[16];
    for (CCUINT32 i = 0; i < 16; i++)
    {
        words[i] = ((CCUINT32)block[i * 4]) |
            ((CCUINT32)block[i * 4 + 1] << 8) |
            ((CCUINT32)block[i * 4 + 2] << 16) |
            ((CCUINT32)block[i * 4 + 3] << 24);
    }

    CCUINT32 a = ctx->state[0];
    CCUINT32 b = ctx->state[1];
    CCUINT32 c = ctx->state[2];
    CCUINT32 d = ctx->state[3];

    for (CCUINT32 i = 0; i < 64; i++)
    {
        CCUINT32 f = 0;
        CCUINT32 g = 0;
        if (i < 16)
        {
            f = (b & c) | (~b & d);
            g = i;
        }
        else if (i < 32)
        {
            f = (d & b) | (~d & c);
            g = (5u * i + 1u) % 16u;
        }
        else if (i < 48)
        {
            f = b ^ c ^ d;
            g = (3u * i + 5u) % 16u;
        }
        else
        {
            f = c ^ (b | ~d);
            g = (7u * i) % 16u;
        }

        CCUINT32 tmp = d;
        d = c;
        c = b;
        b = b + md5_rotl(
            a + f + md5_k[i] + words[g], md5_s[i]
        );
        a = tmp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md5_init(CCMd5Ctx *ctx)
{
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->total = 0;
    ctx->used = 0;
}

static void md5_update(
    CCMd5Ctx *ctx, const CCUINT8 *data, CCUINT64 size
)
{
    if (!data || size == 0)
        return;

    ctx->total += size;
    CCUINT64 offset = 0;
    if (ctx->used > 0)
    {
        CCUINT32 need = 64 - ctx->used;
        CCUINT64 take = (size < need) ? size : need;
        memcpy(ctx->buffer + ctx->used, data, (size_t)take);
        ctx->used += (CCUINT32)take;
        offset += take;
        if (ctx->used < 64)
            return;
        md5_transform(ctx, ctx->buffer);
        ctx->used = 0;
    }

    while (size - offset >= 64)
    {
        md5_transform(ctx, data + offset);
        offset += 64;
    }

    CCUINT64 remain = size - offset;
    if (remain > 0)
    {
        memcpy(ctx->buffer, data + offset, (size_t)remain);
        ctx->used = (CCUINT32)remain;
    }
}

static void md5_final(CCMd5Ctx *ctx, CCMd5Digest *out)
{
    CCUINT64 bits = ctx->total * 8;
    CCUINT8 padding[64];
    memset(padding, 0, sizeof(padding));
    padding[0] = 0x80;

    CCUINT32 pad_len = (ctx->used < 56)
        ? 56 - ctx->used
        : 120 - ctx->used;
    md5_update(ctx, padding, pad_len);

    CCUINT8 length[8];
    for (CCUINT32 i = 0; i < 8; i++)
        length[i] = (CCUINT8)(bits >> (i * 8));
    md5_update(ctx, length, 8);

    for (CCUINT32 i = 0; i < 4; i++)
    {
        out->bytes[i * 4] = (CCUINT8)(ctx->state[i]);
        out->bytes[i * 4 + 1] = (CCUINT8)(ctx->state[i] >> 8);
        out->bytes[i * 4 + 2] = (CCUINT8)(ctx->state[i] >> 16);
        out->bytes[i * 4 + 3] = (CCUINT8)(ctx->state[i] >> 24);
    }
}

static CCINT32 cc_md5_memory(
    const void *data, CCUINT64 size, CCMd5Digest *out
)
{
    CCMd5Ctx ctx;
    md5_init(&ctx);
    md5_update(&ctx, (const CCUINT8 *)data, size);
    md5_final(&ctx, out);
    return CC_FS_OK;
}

static CCINT32 cc_md5_vfs(
    CCFiles *fs, const char *v_path, CCMd5Digest *out
)
{
    if (!fs || !v_path || !v_path[0])
        return CC_FS_ERR_PARAM;
    if (!fs->alive)
        return CC_FS_ERR_PARAM;

    CCUINT32 idx = CC_FS_NIL;
    CCINT32 result = fs_vwalk(
        fs, v_path, CCTRUE, NULL, NULL, &idx
    );
    if (result != CC_FS_OK)
        return result;
    if (idx == CC_FS_NIL)
        return CC_FS_ERR_NOENT;

    CCFsNode *node = node_at(idx);
    if (node->type != CC_FS_TYPE_FILE)
        return CC_FS_ERR_ISDIR;

    result = fs_fh_flush(idx);
    if (result != CC_FS_OK)
        return result;

    CCUINT64 size = node->size;
    CCUINT64 offset = 0;
    CCUINT8 buffer[CC_HASH_CHUNK];
    CCMd5Ctx ctx;
    md5_init(&ctx);

    while (offset < size)
    {
        CCUINT64 want = size - offset;
        if (want > CC_HASH_CHUNK)
            want = CC_HASH_CHUNK;
        CCINT64 got = fs_read_raw(
            idx, buffer, offset, want
        );
        if (got < 0)
            return (CCINT32)got;
        if (got == 0)
            return CC_FS_ERR_IO;
        md5_update(&ctx, buffer, (CCUINT64)got);
        offset += (CCUINT64)got;
    }

    md5_final(&ctx, out);
    return CC_FS_OK;
}

CCINT32 cc_md5(const CCHashInput *input, CCMd5Digest *out)
{
    if (!input || !out)
        return CC_FS_ERR_PARAM;
    memset(out, 0, sizeof(*out));

    if (input->data != NULL)
        return cc_md5_memory(input->data, input->size, out);
    if (input->fs != NULL && input->v_path != NULL)
        return cc_md5_vfs(input->fs, input->v_path, out);
    return CC_FS_ERR_PARAM;
}
