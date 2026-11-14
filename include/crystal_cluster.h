/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 水晶簇体系的第一个头文件，也作为通用基础定义头文件。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CRYSTAL_CLUSTER_H_
#define _INCLUDE_CRYSTAL_CLUSTER_H_

/**
 * 如非必要，勿增实体。
 * 按需定义。
 */

#define _XOPEN_SOURCE 600  // 在严格模式下，通常应放在所有 #include 之前

#include <stdint.h>

/**
 * 调试/发布宏切换
 * 默认情况下采用debug模式
 */
#if !defined(CC_RELEASE) && !defined (CC_DEBUG)
#define CC_DEBUG
#endif

#ifdef CC_DEBUG
    /**
     * 用于控制发行版变量剔除的宏，仅能用于变量的剔除，不可用于其他场景。
     * 在debug模式下选择保留debug，其余选择保留release
     */
    #define CC_DBG_REL_TAG(debug, release) (debug)
#else
    /**
     * 用于控制发行版变量剔除的宏，仅能用于变量的剔除，不可用于其他场景。
     * 在debug模式下选择保留debug，其余选择保留release
     */
    #define CC_DBG_REL_TAG(debug, release) (release)
#endif

typedef uint8_t CCUINT8;
typedef uint16_t CCUINT16;
typedef uint32_t CCUINT32;
typedef uint64_t CCUINT64;
typedef uintptr_t CCUINTPTR;

typedef int8_t CCINT8;
typedef int16_t CCINT16;
typedef int32_t CCINT32;
typedef int64_t CCINT64;
typedef intptr_t CCINTPTR;

#ifdef __ANDROID__
#  define CC_ANDROID 
#elif defined _WIN32
#  define CC_WINDOWS
#elif defined __linux__
#  define CC_LINUX
#else
#  error unsupported platform
#endif

#define CCBOOL _Bool
#define CCTRUE  1
#define CCFALSE 0
#ifndef NULL
#  define NULL (void*)0
#endif

#define CC_RUN_BEFORE_MAIN __attribute__((constructor))
#define CC_RUN_AFTER_MAIN  __attribute__((destructor))

#define CC_UNUSED(v) (void)(v)

/**
 * 通用函数签名定义
 */

typedef CCINT32(*CCDataCallback)(void *data, void *user);

#endif