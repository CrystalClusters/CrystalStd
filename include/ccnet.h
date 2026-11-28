/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核（TCP / UDP）。文档见 <工程根目录>/docs/ccnet.md。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCNET_H_
#define _INCLUDE_CCNET_H_

#include <crystal_cluster.h>

// 收发与连接的默认超时（微秒）：timeOut_us 传 0 时使用
#define CC_NET_DEFAULT_TIMEOUT_US 30000000u

// 显式无限等待：调用方承担线程长期停留在阻塞操作的风险
#define CC_NET_TIMEOUT_INFINITE 0xFFFFFFFFu

// TCP keepalive 默认参数（构建期固定）：空闲 60 秒后探测，间隔 10 秒，连续 6 次无响应判定断开
#define CC_NET_KEEPALIVE_IDLE_SEC 60
#define CC_NET_KEEPALIVE_INTERVAL_SEC 10
#define CC_NET_KEEPALIVE_COUNT 6

// 网络状态：只描述入口执行期间可观察的 TCP 状态
typedef enum cc_net_state
{
    CC_NET_STATE_OPEN        = 1,   // 已连接，可收发
    CC_NET_STATE_PEER_CLOSED = 2,   // TCP 对端有序关闭（FIN），读方向结束
    CC_NET_STATE_FAILED      = 3,   // 连接重置 / keepalive 判定断开
} CCNetState;

// 统一错误码：32 位有符号，全部为负值
typedef enum cc_net_error
{
    CC_NET_OK             =   0,    // 成功
    CC_NET_ERR_PARAM      =  -1,    // 参数非法（空指针 / 容量为 0 / entry 为空）
    CC_NET_ERR_NOMEM      =  -2,    // 内存分配失败
    CC_NET_ERR_RESOURCE   =  -3,    // 系统资源耗尽（fd / 端口 / 内核缓冲）
    CC_NET_ERR_STATE      =  -4,    // 当前状态拒绝该操作（例如 FAILED 后继续收发）
    CC_NET_ERR_TIMEOUT    =  -5,    // 本次调用的等待超过 timeOut_us
    CC_NET_ERR_DNS        =  -6,    // 域名解析失败
    CC_NET_ERR_REFUSED    =  -7,    // 连接被拒绝
    CC_NET_ERR_UNREACH    =  -8,    // 网络 / 主机不可达
    CC_NET_ERR_RESET      =  -9,    // 连接被对端重置
    CC_NET_ERR_ADDRINUSE  = -10,    // 地址 / 端口已被占用
    CC_NET_ERR_ADDRNOTAVAIL = -11,  // 地址不可用（不属于本机等）
    CC_NET_ERR_TOOBIG     = -12,    // 数据报超过平台上限
    CC_NET_ERR_TRUNCATED  = -13,    // 数据报被截断（该数据报被丢弃）
    CC_NET_ERR_IO         = -14,    // 其他底层 IO 错误
} CCNetError;

// 地址族
typedef enum cc_net_family
{
    CC_NET_AF_INET  = 4,
    CC_NET_AF_INET6 = 6,
} CCNetFamily;

// 地址：值类型，可自由拷贝
typedef struct cc_net_addr
{
    CCNetFamily family;     // 地址族
    CCUINT16    port;       // 端口，主机字节序
    CCUINT32    scope;      // IPv6 接口索引；0 表示未指定；IPv4 恒 0
    CCUINT8     addr[16];   // 网络字节序地址：IPv4 前 4 字节，其余为 0
} CCNetAddr;

// 先声明表类型：入口与表互相引用
typedef struct cc_net_accept_ops CCNetAcceptOps;
typedef struct cc_net_tcp_ops    CCNetTcpOps;
typedef struct cc_net_udp_ops    CCNetUdpOps;

// 用户入口：ops 由库注入，ctx 为网络对象上下文，user 为创建时传入的指针
typedef void (*CCNetAcceptEntry)(const CCNetAcceptOps *ops, void *ctx, void *user);
typedef void (*CCNetTcpEntry)(const CCNetTcpOps *ops, void *ctx, void *user);
typedef void (*CCNetUdpEntry)(const CCNetUdpOps *ops, void *ctx, void *user);

// TCP 操作表：常量，所有成员以上下文指针为第一参数
struct cc_net_tcp_ops
{
    CCINT32 (*state)(void *ctx, CCNetState *out);
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
    CCINT32 (*peer)(void *ctx, CCNetAddr *out);
    CCINT64 (*send)(void *ctx, const void *data, CCUINT64 size, CCUINT32 timeOut_us);
    CCINT64 (*recv)(void *ctx, void *buf, CCUINT64 cap, CCUINT32 timeOut_us);
};

// UDP 操作表：端点没有状态机，只提供地址与收发
struct cc_net_udp_ops
{
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
    CCINT64 (*sendto)(void *ctx, const CCNetAddr *to, const void *data, CCUINT64 size, CCUINT32 timeOut_us);
    CCINT64 (*recvfrom)(void *ctx, CCNetAddr *from, void *buf, CCUINT64 cap, CCUINT32 timeOut_us);
};

// 监听器操作表：accept 为连接创建一个善后线程
struct cc_net_accept_ops
{
    CCINT32 (*accept)(void *ctx, CCNetTcpEntry entry, void *user, CCUINT32 timeOut_us);
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
};

// 创建接口：接收已构造地址，只返回错误码
CCINT32 ccnet_tcp_connect(
    const CCNetAddr *addr,
    CCUINT32 timeOut_us,
    CCNetTcpEntry entry,
    void *user
);
CCINT32 ccnet_tcp_listen(
    const CCNetAddr *addr,
    CCNetAcceptEntry entry,
    void *user
);
CCINT32 ccnet_udp_open(
    const CCNetAddr *addr,
    CCNetUdpEntry entry,
    void *user
);

// 地址与 DNS
CCINT32 ccnet_addr_parse(const char *text, CCUINT16 port, CCNetAddr *out);
CCINT32 ccnet_dns(
    const char *host,
    CCUINT16 port,
    CCNetAddr *out,
    CCUINT32 cap
);
CCINT32 ccnet_addr_text(const CCNetAddr *addr, char *buf, CCUINT32 size);

#endif
