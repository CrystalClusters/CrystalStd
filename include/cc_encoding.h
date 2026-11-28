/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: UTF-8 与本地编码转换。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CC_ENCODING_H_
#define _INCLUDE_CC_ENCODING_H_

#include <crystal_cluster.h>

typedef enum cc_local_encoding
{
    CC_LOCAL_SYSTEM  = 0,
    CC_LOCAL_CONSOLE = 1
} CCLocalEncoding;

/**
 * UTF-8 转本地编码。
 * 失败返回 NULL；成功返回 ccalloc 分配的内存。
 */
char *cc_utf8_to_local(const char *utf8, CCLocalEncoding target);

/**
 * 本地编码转 UTF-8。
 * 失败返回 NULL；成功返回 ccalloc 分配的内存。
 */
char *cc_local_to_utf8(const char *local, CCLocalEncoding source);

#endif
