/*
 * @Description: ccnet 交互演示的公共设施：环形缓冲、行分帧、控制台锁与格式化工具。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _DEMO_CCNET_COMMON_H_
#define _DEMO_CCNET_COMMON_H_

#include <ccnet.h>
#include <color_print.h>
#include <ccthread.h>
#include <cctime.h>

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 控制台配色 */
#define NET_C_OK      CC_TEXT_COLOR(CC_BGREEN, CC_DEFAULT)
#define NET_C_ERR     CC_TEXT_COLOR(CC_BRED, CC_DEFAULT)
#define NET_C_WARN    CC_TEXT_COLOR(CC_BYELLOW, CC_DEFAULT)
#define NET_C_INFO    CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT)
#define NET_C_EVENT   CC_TEXT_COLOR(CC_BMAGENTA, CC_DEFAULT)
#define NET_C_TITLE   CC_TEXT_COLOR(CC_BCYAN, CC_DEFAULT)
#define NET_C_DIM     CC_TEXT_COLOR(CC_GRAY, CC_DEFAULT)
#define NET_C_RESET   CC_TEXT_COLOR(CC_DEFAULT, CC_DEFAULT)

/* 客户端接收环：满时丢最旧，保留最新未读 */
#define NET_MSG_CAP   1280u
#define NET_RING_CAP  128u

/* 行协议：单行上限与转发帧上限（帧头包含来源信息） */
#define NET_LINE_CAP  1280u
#define NET_FRAME_CAP (NET_LINE_CAP + 160u)

typedef enum net_ev_kind {
    NET_EV_MSG = 1,
    NET_EV_EVENT = 2,
    NET_EV_ERROR = 3,
} NetEvKind;

typedef struct net_ring_entry {
    CCUINT32 seq;
    CCINT32  kind;
    CCUINT32 len;
    CCINT64  wall_sec;
    char     text[NET_MSG_CAP];
} NetRingEntry;

typedef struct net_ring {
    CCLock   lock;
    CCUINT32 cap;
    CCUINT32 head;
    CCUINT32 count;
    CCUINT32 seq_next;
    CCUINT32 dropped;
    NetRingEntry *slots;
} NetRing;

/* 控制台：所有输出经同一把锁；busy 时后台 ticker 跳过 */
void net_common_init(void);
void net_common_deinit(void);
void net_console_enter(void);
void net_console_leave(void);
CCBOOL net_console_try_enter(void);
CCBOOL net_console_tty(void);

/* 环形缓冲 */
CCBOOL net_ring_init(NetRing *ring, NetRingEntry *slots, CCUINT32 cap);
void net_ring_destroy(NetRing *ring);
void net_ring_push(NetRing *ring, CCINT32 kind, const char *text, CCUINT32 len,
                   CCINT64 wall_sec);
CCUINT32 net_ring_pop(NetRing *ring, NetRingEntry *out, CCUINT32 max);
CCUINT32 net_ring_skip(NetRing *ring, CCUINT32 n);
CCUINT32 net_ring_clear(NetRing *ring);
CCUINT32 net_ring_unread(NetRing *ring);
CCUINT32 net_ring_dropped(NetRing *ring);
void net_event_push(NetRing *ring, CCINT32 kind, const char *fmt, ...);

/* 行累积：每收到一个完整行回调一次；超长行截断并丢弃到下一个换行 */
typedef void (*NetLineFn)(void *user, const char *line, CCUINT32 len,
                          CCBOOL truncated);
typedef struct net_line {
    char buf[NET_LINE_CAP];
    CCUINT32 len;
    CCBOOL over;
} NetLine;
void net_line_init(NetLine *line);
void net_line_feed(NetLine *line, const char *data, CCUINT32 n,
                   NetLineFn fn, void *user);

/* 工具 */
CCINT64 net_send_all(const CCNetTcpOps *ops, void *ctx, const char *data,
                     CCUINT64 len, CCUINT32 timeout_us);
const char *net_err_name(CCINT32 err);
const char *net_state_name(CCINT32 state);
void net_sanitize(char *dst, CCUINT32 cap, const char *src, CCUINT32 len,
                  CCUINT32 *out_len);
void net_fmt_time(CCINT64 wall_sec, char *buf, CCUINT32 cap);
void net_fmt_duration(CCINT64 usec, char *buf, CCUINT32 cap);
CCBOOL net_parse_u32(const char *s, CCUINT32 *out);
CCINT64 net_now_us(void);
void net_copy_text(char *dst, CCUINT32 cap, const char *src);
CCBOOL net_split_command(char *line, char *cmd, CCUINT32 cap, const char **args);

#endif
