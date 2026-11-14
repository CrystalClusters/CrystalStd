/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 动态数组相关定义
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CC_DYN_H_
#define _INCLUDE_CC_DYN_H_

#include "crystal_cluster.h"

// 动态数组结构体
// 用户直接持有
typedef struct cc_dyn {
    void *array;        // 内存块
    CCUINT32 size;      // 数据大小
    CCUINT32 capacity;  // 实际容量
} CCDyn;

// 压入元素
CCINT32 cc_dyn_core_push( CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data);

// 弹出元素
CCINT32 cc_dyn_core_pop(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data);

// 设置元素
CCINT32 cc_dyn_core_set(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

// 取出元素
CCINT32 cc_dyn_core_get(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

// 查询元素
CCINT32 cc_dyn_core_seek(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

/**
 * 下为亚字节操作
 */

// 比特插入
CCINT32 cc_dyn_core_bitin(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len);

// 比特查询
CCINT32 cc_dyn_core_bitout(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len);

#endif