/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 宿主路径字符串工具。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCPATH_H_
#define _INCLUDE_CCPATH_H_

#include <crystal_cluster.h>

/**
 * 判断路径是否为平台绝对路径。
 * NULL 或空串返回 CCFALSE。
 */
CCBOOL ccpath_is_absolute(const char *path);

/**
 * 返回规范绝对路径。
 * base 为 NULL 或空串时使用当前工作目录；base 为相对路径时按当前工作目录解析。
 * 失败返回 NULL，成功返回 ccalloc 分配的内存。
 */
char *ccpath_resolve(const char *path, const char *base);

/**
 * 返回父目录。
 * 失败返回 NULL，成功返回 ccalloc 分配的内存。
 */
char *ccpath_parent(const char *path);

/**
 * 返回最后一个路径组件；根路径返回空串。
 * 失败返回 NULL，成功返回 ccalloc 分配的内存。
 */
char *ccpath_basename(const char *path);

/**
 * 拼接两段路径，使用单个 '/' 分隔，不解析 '.' / '..'。
 * 失败返回 NULL，成功返回 ccalloc 分配的内存。
 */
char *ccpath_join(const char *a, const char *b);

/**
 * 按平台语义比较路径。
 * Windows 下 ASCII 大小写不敏感；POSIX 下逐字节比较。
 */
CCBOOL ccpath_equal(const char *a, const char *b);

#endif
