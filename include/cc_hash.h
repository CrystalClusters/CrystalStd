/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: MD5 摘要计算，支持内存字节与 VFS 节点。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CC_HASH_H_
#define _INCLUDE_CC_HASH_H_

#include "ccfiles.h"

#define CC_MD5_DIGEST_SIZE 16

typedef struct cc_hash_input
{
    const void *data;
    CCUINT64    size; // 仅内存模式使用；data 为 NULL 时忽略
    CCFiles    *fs;
    const char *v_path;
} CCHashInput;

typedef struct cc_md5_digest
{
    CCUINT8 bytes[CC_MD5_DIGEST_SIZE];
} CCMd5Digest;

/**
 * 计算 MD5 摘要。
 * data 非 NULL 时按内存模式处理，VFS 参数忽略。
 * data 为 NULL 时使用 fs + v_path 的 VFS 节点。
 * 两者都不可用时返回 CC_FS_ERR_PARAM。
 */
CCINT32 cc_md5(const CCHashInput *input, CCMd5Digest *out);

#endif
