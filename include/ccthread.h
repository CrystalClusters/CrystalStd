/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */
#ifndef _INCLUDE_CCTHREAD_H_
#define _INCLUDE_CCTHREAD_H_

#include "crystal_cluster.h"

typedef void* CCLock;

// 线程 ID 统一使用与指针等宽的整型，便于把平台原生线程标识
// （pthread_t / HANDLE / 线程 ID）无位宽告警地收敛到同一类型中比较。
typedef CCUINTPTR CCThread;

// 线程入口函数的签名形式
typedef void(*CCThreadEntry)(void* data, CCThread idThis);

// 创建锁
CCLock cc_create_lock();
// 销毁锁
CCINT32 cc_destroy_lock(CCLock lock);
// 加锁
void cc_lock(CCLock lock);
// 解锁
void cc_unlock(CCLock lock);

// 创建线程；data 为用户数据，会原样传给入口函数
CCThread cc_thread_create(CCThreadEntry entry, void *data);
// 查询线程；0：线程不存在/已退出，1：线程存活
CCINT32 cc_thread_status(CCThread thr);
// 等待线程；timeOut_us 单位为微秒，0 表示无限等待
void cc_thread_wait(CCThread thr, CCUINT32 timeOut_us);

#endif