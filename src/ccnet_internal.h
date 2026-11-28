/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核内部声明：上下文、启动块、平台原语。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _SRC_CCNET_INTERNAL_H_
#define _SRC_CCNET_INTERNAL_H_

#include <ccnet.h>
#include <ccalloc.h>
#include <ccthread.h>
#include <cctime.h>

#include <stdatomic.h>
#include <string.h>

#ifdef CC_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <mstcpip.h>
// Winsock 侧保持平台原生类型：SOCKET 是句柄，长度参数按 Winsock 的接口约定为 int
typedef SOCKET CCNetSocket;
typedef int CCNetSockLen;
#define CC_NET_SOCK_INVALID INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
// POSIX 侧保持平台原生类型：套接字是 int 文件描述符，长度参数为 socklen_t
typedef int CCNetSocket;
typedef socklen_t CCNetSockLen;
#define CC_NET_SOCK_INVALID (-1)
#endif

// 对象类型
typedef enum cc_net_kind
{
    CC_NET_KIND_TCP      = 1,
    CC_NET_KIND_UDP      = 2,
    CC_NET_KIND_LISTENER = 3,
} CCNetKind;

// 每个网络对象一个上下文；入口执行期间有效
typedef struct cc_net_ctx
{
    CCNetKind kind;
    CCNetSocket sock;
    _Atomic CCINT32 state;
    CCNetAddr local;
    CCNetAddr peer;
    CCThread thread;
    CCNetAcceptEntry accept_entry;
    CCNetTcpEntry tcp_entry;
    CCNetUdpEntry udp_entry;
    void *user;
} CCNetCtx;

// 创建函数与善后线程之间的一次同步
typedef struct cc_net_start
{
    CCNetKind kind;
    CCNetAddr addr;
    CCUINT32 timeOut_us;
    CCNetAcceptEntry accept_entry;
    CCNetTcpEntry tcp_entry;
    CCNetUdpEntry udp_entry;
    void *user;
    CCNetSocket preconnected;   // accept 交出的已连接 socket
    CCBOOL heap;                // start 由堆分配（accept 路径）
    CCLock lock;
    volatile CCBOOL done;
    CCINT32 result;
} CCNetStart;

// 初始化 / 逆初始化（由 crystal_std 调用）
void init_ccnet(void);
void deinit_ccnet(void);
CCBOOL _ccnet_ready(void);

// 善后线程入口
void _ccnet_tcp_wrapper(void *arg, CCThread idThis);
void _ccnet_listen_wrapper(void *arg, CCThread idThis);
void _ccnet_udp_wrapper(void *arg, CCThread idThis);

// 创建与收尾
CCINT32 _ccnet_spawn(CCNetStart *start, CCThreadEntry wrapper);
void _ccnet_finish(CCNetStart *start, CCINT32 result);
void _ccnet_ctx_cleanup(CCNetCtx *ctx);

// 各协议的 setup：在善后线程内调用
CCINT32 _ccnet_tcp_setup(CCNetStart *start, CCNetCtx *ctx);
CCINT32 _ccnet_listen_setup(CCNetStart *start, CCNetCtx *ctx);
CCINT32 _ccnet_udp_setup(CCNetStart *start, CCNetCtx *ctx);

// 平台原语
CCINT32 _ccnet_last_error(void);
CCINT32 _ccnet_map_os(CCINT32 os_code);
CCINT32 _ccnet_map_gai(CCINT32 gai_code);
CCINT32 _ccnet_set_timeout(CCNetSocket sock, CCBOOL recv, CCUINT32 us);
CCINT32 _ccnet_poll(CCNetSocket sock, CCBOOL write, CCUINT32 us);
CCINT32 _ccnet_set_blocking(CCNetSocket sock, CCBOOL blocking);
CCINT32 _ccnet_sock_close(CCNetSocket sock);
CCUINT32 _ccnet_norm_timeout(CCUINT32 us);
CCINT32 _ccnet_tcp_keepalive(CCNetSocket sock);

// 地址转换
CCINT32 _ccnet_addr_to_sock(const CCNetAddr *addr, struct sockaddr_storage *ss, CCNetSockLen *len);
void _ccnet_addr_from_sock(CCNetAddr *out, const struct sockaddr *sa, CCNetSockLen len);

// 操作表
extern const CCNetTcpOps _ccnet_tcp_ops;
extern const CCNetUdpOps _ccnet_udp_ops;
extern const CCNetAcceptOps _ccnet_accept_ops;

#endif
