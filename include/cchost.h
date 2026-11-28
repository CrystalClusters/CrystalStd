/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 宿主文件系统 Shell 能力。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCHOST_H_
#define _INCLUDE_CCHOST_H_

#include <crystal_cluster.h>

typedef enum cc_host_kind
{
    CC_HOST_MISSING = 0,
    CC_HOST_FILE    = 1,
    CC_HOST_DIR     = 2,
    CC_HOST_OTHER   = 3,
    CC_HOST_DENIED  = 4
} CCHostKind;

typedef struct cc_host_stat
{
    CCHostKind kind;
    CCUINT64   size;
    CCUINT64   mtime_ns;
} CCHostStat;

typedef enum cc_host_result
{
    CC_HOST_OK           =  0,
    CC_HOST_ERR_PARAM    = -1,
    CC_HOST_ERR_NOENT    = -2,
    CC_HOST_ERR_NOTDIR   = -3,
    CC_HOST_ERR_ISDIR    = -4,
    CC_HOST_ERR_PERM     = -6,
    CC_HOST_ERR_NOSPACE  = -7,
    CC_HOST_ERR_EXIST    = -8,
    CC_HOST_ERR_NOTEMPTY = -9,
    CC_HOST_ERR_IO       = -10
} CCHostResult;

/**
 * 查询宿主条目。MISSING / DENIED 属于正常分类结果。
 */
CCINT32 cchost_stat(const char *path, CCHostStat *out);

/**
 * 读取整个文件。*data 由 ccalloc 分配，末尾补 '\0'。
 */
CCINT32 cchost_read_all(const char *path, void **data, CCUINT64 *size);

/**
 * 递归创建目录。
 */
CCINT32 cchost_mkdirs(const char *path);

/**
 * 删除文件或空目录。
 */
CCINT32 cchost_remove(const char *path);

/**
 * 复制普通文件。
 */
CCINT32 cchost_copy(const char *from, const char *to);

/**
 * 移动普通文件。
 */
CCINT32 cchost_move(const char *from, const char *to);

#endif
