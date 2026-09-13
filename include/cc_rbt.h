/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 红黑树头文件
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CC_RBT_H_
#define _INCLUDE_CC_RBT_H_

#include "crystal_cluster.h"

// 红黑树节点关系枚举
typedef enum cc_rbt_relation {
    CC_RBT_REL_LEFT,    // 左子节点
    CC_RBT_REL_RIGHT,   // 右子节点
    CC_RBT_REL_PARENT   // 父节点
} CC_RbtRel;

// 提供对红黑树完备操作的接口的结构体；
// 应当能够仅依赖本结构体提供的接口，
// 就可以根据需求对合法红黑树达成任意合法变动。
typedef struct cc_rbt_ops {
    // 用户自定义上下文
    void *user;
    // 键值对比
    CCINT32 (*cmpare_key)(void *user, void *node, CCUINTPTR key);
    // 节点键值对比
    CCINT32 (*cmpare_node)(void *user, void *node_1, void *node_2);
    // 拷贝节点
    CCINT32 (*copy_node)(void *user, void *dst_node, void *src_node);
    // 如有必要，寻找删除时的替换节点
    void* (*find_instead)(void *user, void *node);

    // 获取和节点对应关系的另一节点
    void* (*get_node)(void *user, void *node, CC_RbtRel relation);
    // 设置对应关系的节点挂载
    CCUINT32 (*set_node)(void *user, void *node_1, void *node_2, CC_RbtRel relation);

    // 获取节点颜色
    CCBOOL (*get_color)(void *user, void *node);
    // 设置节点颜色
    CCINT32 (*set_color)(void *user, void *node, CCBOOL is_red);

    // 左旋
    CCINT32 (*left_rotate)(void *user, void *node);
    // 右旋
    CCINT32 (*right_rotate)(void *user, void* node);

    // 通知调用方根节点已变为新节点
    void (*ch_root)(void *user, void *new_root);
} CC_RbtOps;

// 插入节点
CCINT32 cc_rbt_core_insert(CC_RbtOps *ops, void *root, void *node);

// 移除节点
void* cc_rbt_core_remove(CC_RbtOps *ops, void *root, CCUINTPTR key);

// 检索节点
void* cc_rbt_core_get(CC_RbtOps *ops, void *root, CCUINTPTR key);

// 遍历树（中序遍历回调）
CCINT32 cc_rbt_core_inorder_tranverse(CC_RbtOps *ops, void *root, void *user, CCDataCallback cbk);

#endif