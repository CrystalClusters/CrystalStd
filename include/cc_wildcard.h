/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 通配符匹配
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CC_WILDCARD_H_
#define _INCLUDE_CC_WILDCARD_H_

#include "crystal_cluster.h"

/**
 * 通配符匹配。
 * pattern：模式串，支持 '*'（任意长度，含空）与 '?'（任意单字节）。
 * str：待匹配字符串。
 * pattern 或 str 为 NULL 时返回 CCFALSE。
 */
CCBOOL cc_wildcard(const char *pattern, const char *str);

#endif
