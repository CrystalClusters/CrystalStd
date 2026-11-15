/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核（ccnet）测试：地址、DNS、TCP/UDP 收发、生命周期。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "test_header.h"
#include <ccnet.h>
#include <ccthread.h>
#include "ccnet_internal.h"
#include <stdatomic.h>

typedef struct net_case {
    _Atomic CCINT32 flag_port;
    _Atomic CCINT32 flag_client_done;
    _Atomic CCINT32 flag_server_done;
    _Atomic CCINT32 flag_ok;
    _Atomic CCINT32 flag_timeout;
    _Atomic CCINT32 flag_zero;
    _Atomic CCINT32 flag_trunc;
    _Atomic CCINT32 flag_stop;
    _Atomic CCINT32 flag_reset;
    _Atomic CCINT32 flag_state;
    _Atomic CCINT32 flag_keepalive_client;
    _Atomic CCINT32 flag_keepalive_server;
    _Atomic CCINT32 state;
    _Atomic CCUINT32 port;
} net_case;

static void _net_case_init(net_case *c)
{
    atomic_init(&c->flag_port, 0);
    atomic_init(&c->flag_client_done, 0);
    atomic_init(&c->flag_server_done, 0);
    atomic_init(&c->flag_ok, 0);
    atomic_init(&c->flag_timeout, 0);
    atomic_init(&c->flag_zero, 0);
    atomic_init(&c->flag_trunc, 0);
    atomic_init(&c->flag_stop, 0);
    atomic_init(&c->flag_reset, 0);
    atomic_init(&c->flag_state, 0);
    atomic_init(&c->flag_keepalive_client, 0);
    atomic_init(&c->flag_keepalive_server, 0);
    atomic_init(&c->state, 0);
    atomic_init(&c->port, 0);
}

static CCBOOL _wait_flag(_Atomic CCINT32 *flag, CCUINT32 time_out_us)
{
    CCUINT32 elapsed = 0;
    while (!atomic_load_explicit(flag, memory_order_acquire))
    {
        if (time_out_us && elapsed >= time_out_us)
            return CCFALSE;
        ccsleep(1000);
        elapsed += 1000;
    }
    return CCTRUE;
}

/* ------------------------------------------------------------------ TCP */

static void _tcp_echo_conn(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    char buf[512];

    for (;;)
    {
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 100000);
        if (n == CC_NET_ERR_TIMEOUT)
        {
            if (c->flag_stop)
                break;
            continue;
        }
        if (n <= 0)
            break;

        {
            CCINT64 off = 0;
            while (off < n)
            {
                CCINT64 w = ops->send(ctx, buf + off,
                                      (CCUINT64)(n - off), 100000);
                if (w == CC_NET_ERR_TIMEOUT)
                    continue;
                if (w < 0)
                {
                    c->flag_server_done = 1;
                    return;
                }
                off += w;
            }
        }
    }
    c->flag_server_done = 1;
}

static void _tcp_accept_one(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }
    (void)ops->accept(ctx, _tcp_echo_conn, user, 3000000);
}

static void _tcp_client_echo(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    const char msg[] = "hello";
    char buf[16];
    CCINT64 off = 0;

    while (off < 5)
    {
        CCINT64 w = ops->send(ctx, msg + off, (CCUINT64)(5 - off), 1000000);
        if (w == CC_NET_ERR_TIMEOUT)
            continue;
        if (w < 0)
            break;
        off += w;
    }
    if (off == 5)
    {
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 2000000);
        if (n == 5 && memcmp(buf, msg, 5) == 0)
            c->flag_ok = 1;
    }
    c->flag_client_done = 1;
}

static void _test_tcp_echo(void)
{
    net_case c;
    CCNetAddr addr;
    CCINT32 rc;

    CC_CASE("NET-TCP-001", "TCP 回环收发与入口返回清理");
    _net_case_init(&c);

    rc = ccnet_addr_parse("127.0.0.1", 0, &addr);
    CC_CHECK_EQ_I64("parse 127.0.0.1:0", CC_NET_OK, rc);
    rc = ccnet_tcp_listen(&addr, _tcp_accept_one, &c);
    CC_CHECK_EQ_I64("listen 返回 0", CC_NET_OK, rc);
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "accept 入口取到本地端口");

    rc = ccnet_addr_parse("127.0.0.1", c.port, &addr);
    CC_CHECK_EQ_I64("parse 127.0.0.1:port", CC_NET_OK, rc);
    rc = ccnet_tcp_connect(&addr, 3000000, _tcp_client_echo, &c);
    CC_CHECK_EQ_I64("connect 返回 0", CC_NET_OK, rc);
    CC_CHECK(_wait_flag(&c.flag_client_done, 5000000), "客户端入口执行完毕");

    c.flag_stop = 1;
    CC_CHECK(_wait_flag(&c.flag_server_done, 5000000), "服务端入口执行完毕");
    CC_CHECK(c.flag_ok == 1, "回显字节一致");
    CC_CASE_END();
}

static void _tcp_fin_conn(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    char buf[16];
    CCNetState st;

    for (;;)
    {
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 100000);
        if (n == CC_NET_ERR_TIMEOUT)
        {
            c->flag_timeout = 1;
            continue;
        }
        if (n <= 0)
            break;
    }
    if (ops->state(ctx, &st) == CC_NET_OK)
        c->state = (CCINT32)st;
    c->flag_server_done = 1;
}

static void _tcp_accept_fin(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;
    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }
    (void)ops->accept(ctx, _tcp_fin_conn, user, 3000000);
}

static void _tcp_client_silent(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    char buf[8];
    CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 100000);

    if (n == CC_NET_ERR_TIMEOUT)
        c->flag_timeout = 1;
    c->flag_client_done = 1;
}

static void _test_tcp_timeout_fin(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-TCP-002", "接收超时与对端 FIN 状态");
    _net_case_init(&c);
    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("listen", CC_NET_OK,
                    ccnet_tcp_listen(&addr, _tcp_accept_fin, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到监听端口");

    CC_CHECK_EQ_I64("parse connect", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", c.port, &addr));
    CC_CHECK_EQ_I64("connect", CC_NET_OK,
                    ccnet_tcp_connect(&addr, 3000000, _tcp_client_silent, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 3000000), "客户端超时返回");
    CC_CHECK(c.flag_timeout == 1, "客户端观察到 TIMEOUT");
    CC_CHECK(_wait_flag(&c.flag_server_done, 3000000), "服务端观察到 FIN");
    CC_CHECK_EQ_I64("FIN 后状态为 PEER_CLOSED",
                    (CCINT64)CC_NET_STATE_PEER_CLOSED, (CCINT64)c.state);
    CC_CASE_END();
}

typedef struct duplex_writer {
    const CCNetTcpOps *ops;
    void *ctx;
    _Atomic CCINT32 done;
    _Atomic CCINT32 ok;
} duplex_writer;

#define DUPLEX_MSG 16
#define DUPLEX_LEN 10

static void _duplex_writer(void *data, CCThread idThis)
{
    duplex_writer *w = (duplex_writer *)data;
    CC_UNUSED(idThis);
    CCINT32 i;

    w->ok = 1;
    for (i = 0; i < DUPLEX_MSG; i++)
    {
        char msg[DUPLEX_LEN];
        CCINT32 j;
        CCINT64 off = 0;
        for (j = 0; j < DUPLEX_LEN; j++)
            msg[j] = (char)('0' + ((i * DUPLEX_LEN + j) % 10));

        while (off < DUPLEX_LEN)
        {
            CCINT64 sent = w->ops->send(w->ctx, msg + off,
                                        (CCUINT64)(DUPLEX_LEN - off), 1000000);
            if (sent == CC_NET_ERR_TIMEOUT)
                continue;
            if (sent < 0)
            {
                w->ok = 0;
                w->done = 1;
                return;
            }
            off += sent;
        }
    }
    w->done = 1;
}

static void _duplex_client(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    duplex_writer w;
    CCUINT32 total = DUPLEX_MSG * DUPLEX_LEN;
    CCUINT32 got = 0;
    CCBOOL pattern = CCTRUE;
    CCThread t;

    atomic_init(&w.done, 0);
    atomic_init(&w.ok, 1);
    w.ops = ops;
    w.ctx = ctx;

    t = cc_thread_create(_duplex_writer, &w);
    if (!t)
    {
        c->flag_client_done = 1;
        return;
    }

    while (got < total)
    {
        char buf[64];
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 1000000);
        CCINT64 i;
        if (n == CC_NET_ERR_TIMEOUT)
        {
            if (w.done)
                break;
            continue;
        }
        if (n <= 0)
            break;
        for (i = 0; i < n; i++)
        {
            char expect = (char)('0' + ((got + (CCUINT32)i) % 10));
            if (buf[i] != expect)
                pattern = CCFALSE;
        }
        got += (CCUINT32)n;
    }

    cc_thread_wait(t, 0);
    c->flag_ok = (got == total && pattern && w.ok) ? 1 : 0;
    c->flag_client_done = 1;
}

static void _test_tcp_duplex(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-TCP-003", "一读一写协作线程全双工");
    _net_case_init(&c);
    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("listen", CC_NET_OK,
                    ccnet_tcp_listen(&addr, _tcp_accept_one, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到监听端口");
    CC_CHECK_EQ_I64("parse connect", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", c.port, &addr));
    CC_CHECK_EQ_I64("connect", CC_NET_OK,
                    ccnet_tcp_connect(&addr, 3000000, _duplex_client, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 8000000), "全双工收发完成");
    c.flag_stop = 1;
    CC_CHECK(_wait_flag(&c.flag_server_done, 5000000), "服务端入口退出");
    CC_CHECK(c.flag_ok == 1, "全双工数据顺序与内容一致");
    CC_CASE_END();
}

/* ------------------------------------------------------- 多客户端 / RST */

#define MULTI_MAX 32

typedef struct multi_shared {
    CCINT32 clients;
    _Atomic CCINT32 port_ready;
    _Atomic CCINT32 listener_done;
    _Atomic CCINT32 accept_fail;
    _Atomic CCINT32 server_done;
    _Atomic CCUINT32 port;
} multi_shared;

typedef struct multi_client {
    multi_shared *shared;
    CCINT32 id;
    _Atomic CCINT32 done;
    _Atomic CCINT32 ok;
} multi_client;

static void _multi_shared_init(multi_shared *s, CCINT32 clients)
{
    s->clients = clients;
    atomic_init(&s->port_ready, 0);
    atomic_init(&s->listener_done, 0);
    atomic_init(&s->accept_fail, 0);
    atomic_init(&s->server_done, 0);
    atomic_init(&s->port, 0);
}

static void _multi_conn(const CCNetTcpOps *ops, void *ctx, void *user)
{
    multi_shared *s = (multi_shared *)user;
    char buf[64];
    CCINT64 off;

    for (;;)
    {
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 100000);
        if (n == CC_NET_ERR_TIMEOUT)
            continue;
        if (n <= 0)
            break;

        off = 0;
        while (off < n)
        {
            CCINT64 w = ops->send(ctx, buf + off, (CCUINT64)(n - off), 100000);
            if (w == CC_NET_ERR_TIMEOUT)
                continue;
            if (w < 0)
                break;
            off += w;
        }
        if (off < n)
            break;
    }

    atomic_fetch_add_explicit(&s->server_done, 1, memory_order_release);
}

static void _multi_accept(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    multi_shared *s = (multi_shared *)user;
    CCNetAddr local;
    CCINT32 i;

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        atomic_store_explicit(&s->port, local.port, memory_order_release);
        atomic_store_explicit(&s->port_ready, 1, memory_order_release);
    }

    for (i = 0; i < s->clients; i++)
    {
        CCINT32 rc = ops->accept(ctx, _multi_conn, user, 5000000);
        if (rc != CC_NET_OK)
        {
            atomic_store_explicit(&s->accept_fail, 1, memory_order_release);
            break;
        }
    }
    atomic_store_explicit(&s->listener_done, 1, memory_order_release);
}

static void _multi_client_entry(const CCNetTcpOps *ops, void *ctx, void *user)
{
    multi_client *c = (multi_client *)user;
    CCINT32 id = c->id;
    char out[4];
    char in[4];
    CCINT64 off = 0;

    memcpy(out, &id, sizeof(out));
    while (off < 4)
    {
        CCINT64 w = ops->send(ctx, out + off, (CCUINT64)(4 - off), 1000000);
        if (w == CC_NET_ERR_TIMEOUT)
            continue;
        if (w < 0)
            break;
        off += w;
    }

    if (off == 4)
    {
        off = 0;
        while (off < 4)
        {
            CCINT64 n = ops->recv(ctx, in + off, (CCUINT64)(4 - off), 2000000);
            if (n == CC_NET_ERR_TIMEOUT)
                continue;
            if (n <= 0)
                break;
            off += n;
        }
        if (off == 4 && memcmp(in, out, 4) == 0)
            c->ok = 1;
    }
    c->done = 1;
}

static CCINT32 _multi_done_count(multi_shared *s)
{
    return atomic_load_explicit(&s->server_done, memory_order_acquire);
}

static void _test_tcp_multi(void)
{
    CCINT32 clients = cc_test_level_full() ? MULTI_MAX : 8;
    CCINT32 i;
    CCUINT32 waited;
    multi_shared s;
    multi_client cs[MULTI_MAX];
    CCNetAddr addr;

    CC_CASE("NET-TCP-004", "多客户端并发连接与隔离");
    _multi_shared_init(&s, clients);

    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("listen", CC_NET_OK,
                    ccnet_tcp_listen(&addr, _multi_accept, &s));
    CC_CHECK(_wait_flag(&s.port_ready, 3000000), "取到监听端口");

    CC_CHECK_EQ_I64("parse connect", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", (CCUINT16)s.port, &addr));
    for (i = 0; i < clients; i++)
    {
        cs[i].shared = &s;
        cs[i].id = i;
        atomic_init(&cs[i].done, 0);
        atomic_init(&cs[i].ok, 0);
        CC_CHECK_EQ_I64("connect", CC_NET_OK,
                        ccnet_tcp_connect(&addr, 3000000,
                                          _multi_client_entry, &cs[i]));
    }

    for (i = 0; i < clients; i++)
    {
        CC_CHECK(_wait_flag(&cs[i].done, 5000000), "客户端完成");
        CC_CHECK(cs[i].ok == 1, "客户端回显匹配");
    }

    waited = 0;
    while (waited < 10000000 && _multi_done_count(&s) < clients)
    {
        ccsleep(1000);
        waited += 1000;
    }

    CC_CHECK_EQ_I64("服务端连接全部结束", clients, _multi_done_count(&s));
    CC_CHECK(atomic_load_explicit(&s.listener_done, memory_order_acquire) == 1,
             "监听器 accept 循环结束");
    CC_CHECK(atomic_load_explicit(&s.accept_fail, memory_order_acquire) == 0,
             "所有 accept 成功");
    CC_CASE_END();
}

static CCINT32 _raw_rst_client(const CCNetAddr *addr)
{
#ifdef CC_WINDOWS
    SOCKET sock;
#else
    int sock;
#endif
    struct sockaddr_in sa;
    struct linger lg;
    int rc;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(addr->port);
    memcpy(&sa.sin_addr, addr->addr, 4);

#ifdef CC_WINDOWS
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return -1;
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return -1;
#endif

    rc = connect(sock, (struct sockaddr *)&sa, (int)sizeof(sa));
    if (rc != 0)
    {
#ifdef CC_WINDOWS
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    lg.l_onoff = 1;
    lg.l_linger = 0;
    (void)setsockopt(sock, SOL_SOCKET, SO_LINGER,
                     (const char *)&lg, sizeof(lg));

#ifdef CC_WINDOWS
    closesocket(sock);
#else
    close(sock);
#endif
    return 0;
}

static void _rst_conn(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    char buf[8];
    CCNetState st;

    for (;;)
    {
        CCINT64 n = ops->recv(ctx, buf, sizeof(buf), 2000000);
        if (n > 0)
            continue;
        if (n == CC_NET_ERR_RESET)
            c->flag_reset = 1;
        break;
    }

    if (ops->state(ctx, &st) == CC_NET_OK)
        c->state = (CCINT32)st;

    if (ops->recv(ctx, buf, sizeof(buf), 1000000) == CC_NET_ERR_STATE)
        c->flag_state = 1;

    c->flag_server_done = 1;
}

static void _rst_accept(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }
    (void)ops->accept(ctx, _rst_conn, user, 5000000);
}

static void _test_tcp_reset(void)
{
    net_case c;
    CCNetAddr addr;
    CCINT32 rc;

    CC_CASE("NET-TCP-005", "对端 RST 的错误与 FAILED 状态");
    _net_case_init(&c);

    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("listen", CC_NET_OK,
                    ccnet_tcp_listen(&addr, _rst_accept, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到监听端口");

    CC_CHECK_EQ_I64("parse connect", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", c.port, &addr));
    rc = _raw_rst_client(&addr);
    CC_CHECK_EQ_I64("原生客户端发送 RST", 0, rc);

    CC_CHECK(_wait_flag(&c.flag_server_done, 5000000), "服务端观察到 RST");
    CC_CHECK(c.flag_reset == 1, "recv 返回 RESET");
    CC_CHECK_EQ_I64("RST 后状态为 FAILED",
                    (CCINT64)CC_NET_STATE_FAILED, (CCINT64)c.state);
    CC_CHECK(c.flag_state == 1, "后续 recv 返回 STATE");
    CC_CASE_END();
}

static CCBOOL _keepalive_enabled(CCNetSocket sock)
{
    int on = 0;
    CCNetSockLen len = (CCNetSockLen)sizeof(on);

    if (getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
                   (char *)&on, &len) != 0 || on == 0)
        return CCFALSE;

#ifndef CC_WINDOWS
#if defined(TCP_KEEPIDLE)
    {
        int idle = 0;
        len = (CCNetSockLen)sizeof(idle);
        if (getsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,
                       (char *)&idle, &len) != 0 ||
            idle != CC_NET_KEEPALIVE_IDLE_SEC)
            return CCFALSE;
    }
#endif
#if defined(TCP_KEEPINTVL)
    {
        int interval = 0;
        len = (CCNetSockLen)sizeof(interval);
        if (getsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL,
                       (char *)&interval, &len) != 0 ||
            interval != CC_NET_KEEPALIVE_INTERVAL_SEC)
            return CCFALSE;
    }
#endif
#if defined(TCP_KEEPCNT)
    {
        int count = 0;
        len = (CCNetSockLen)sizeof(count);
        if (getsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,
                       (char *)&count, &len) != 0 ||
            count != CC_NET_KEEPALIVE_COUNT)
            return CCFALSE;
    }
#endif
#endif
    return CCTRUE;
}

static void _keepalive_conn(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetCtx *inner = (CCNetCtx *)ctx;

    (void)ops;
    c->flag_keepalive_server = _keepalive_enabled(inner->sock) ? 1 : 0;
    c->flag_server_done = 1;
}

static void _keepalive_accept(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }
    (void)ops->accept(ctx, _keepalive_conn, user, 5000000);
}

static void _keepalive_client(const CCNetTcpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetCtx *inner = (CCNetCtx *)ctx;

    (void)ops;
    c->flag_keepalive_client = _keepalive_enabled(inner->sock) ? 1 : 0;
    c->flag_client_done = 1;
}

static void _test_tcp_keepalive(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-TCP-006", "TCP keepalive 默认开启且参数匹配");
    _net_case_init(&c);

    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("listen", CC_NET_OK,
                    ccnet_tcp_listen(&addr, _keepalive_accept, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到监听端口");

    CC_CHECK_EQ_I64("parse connect", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", c.port, &addr));
    CC_CHECK_EQ_I64("connect", CC_NET_OK,
                    ccnet_tcp_connect(&addr, 3000000, _keepalive_client, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 3000000), "客户端入口完成");
    CC_CHECK(_wait_flag(&c.flag_server_done, 3000000), "服务端入口完成");
    CC_CHECK(c.flag_keepalive_client == 1, "客户端 socket 已开启 keepalive");
    CC_CHECK(c.flag_keepalive_server == 1, "服务端连接 socket 已开启 keepalive");
    CC_CASE_END();
}

/* ------------------------------------------------------------------ UDP */

static void _udp_echo_entry(const CCNetUdpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;
    CCNetAddr from;
    char buf[64];

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }

    for (;;)
    {
        CCINT64 n = ops->recvfrom(ctx, &from, buf, sizeof(buf), 100000);
        if (n == CC_NET_ERR_TIMEOUT)
        {
            if (c->flag_stop)
                break;
            continue;
        }
        if (n < 0)
            break;
        if (n == 0)
            c->flag_zero = 1;
        (void)ops->sendto(ctx, &from, buf, (CCUINT64)n, 100000);
    }
    c->flag_server_done = 1;
}

static void _udp_client_echo(const CCNetUdpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr to;
    char buf[16];

    if (ccnet_addr_parse("127.0.0.1", c->port, &to) == CC_NET_OK)
    {
        if (ops->sendto(ctx, &to, "udp", 3, 1000000) == 3)
        {
            CCINT64 n = ops->recvfrom(ctx, NULL, buf, sizeof(buf), 2000000);
            if (n == 3 && memcmp(buf, "udp", 3) == 0)
                c->flag_ok = 1;
        }
    }
    c->flag_client_done = 1;
}

static void _test_udp_echo(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-UDP-001", "UDP 回显");
    _net_case_init(&c);
    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_echo_entry, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到端点端口");

    CC_CHECK_EQ_I64("parse bind client", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open client", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_client_echo, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 3000000), "UDP 客户端完成");
    c.flag_stop = 1;
    CC_CHECK(_wait_flag(&c.flag_server_done, 3000000), "UDP 服务端退出");
    CC_CHECK(c.flag_ok == 1, "UDP 回显字节一致");
    CC_CASE_END();
}

static void _udp_zero_client(const CCNetUdpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr to;
    char buf[8];

    if (ccnet_addr_parse("127.0.0.1", c->port, &to) == CC_NET_OK)
    {
        if (ops->sendto(ctx, &to, "", 0, 1000000) == 0)
        {
            CCINT64 n = ops->recvfrom(ctx, NULL, buf, sizeof(buf), 2000000);
            if (n == 0)
                c->flag_ok = 1;
        }
    }
    c->flag_client_done = 1;
}

static void _test_udp_zero(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-UDP-002", "零长度数据报合法");
    _net_case_init(&c);
    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_echo_entry, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到端点端口");
    CC_CHECK_EQ_I64("parse client", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open client", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_zero_client, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 3000000), "零长度客户端完成");
    c.flag_stop = 1;
    CC_CHECK(_wait_flag(&c.flag_server_done, 3000000), "服务端退出");
    CC_CHECK(c.flag_zero == 1, "服务端收到零长度数据报");
    CC_CHECK(c.flag_ok == 1, "零长度回显一致");
    CC_CASE_END();
}

static void _udp_trunc_entry(const CCNetUdpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr local;
    CCNetAddr from;
    char buf[8];

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        c->port = local.port;
        c->flag_port = 1;
    }

    for (;;)
    {
        CCINT64 n = ops->recvfrom(ctx, &from, buf, sizeof(buf), 2000000);
        if (n == CC_NET_ERR_TRUNCATED)
        {
            c->flag_trunc = 1;
            break;
        }
        if (n < 0)
            break;
    }
    c->flag_server_done = 1;
}

static void _udp_trunc_client(const CCNetUdpOps *ops, void *ctx, void *user)
{
    net_case *c = (net_case *)user;
    CCNetAddr to;
    char data[32];

    memset(data, 'x', sizeof(data));
    if (ccnet_addr_parse("127.0.0.1", c->port, &to) == CC_NET_OK)
        (void)ops->sendto(ctx, &to, data, sizeof(data), 1000000);
    c->flag_client_done = 1;
}

static void _test_udp_trunc(void)
{
    net_case c;
    CCNetAddr addr;

    CC_CASE("NET-UDP-003", "截断数据报整包丢弃");
    _net_case_init(&c);
    CC_CHECK_EQ_I64("parse bind", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_trunc_entry, &c));
    CC_CHECK(_wait_flag(&c.flag_port, 3000000), "取到端点端口");
    CC_CHECK_EQ_I64("parse client", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 0, &addr));
    CC_CHECK_EQ_I64("udp open client", CC_NET_OK,
                    ccnet_udp_open(&addr, _udp_trunc_client, &c));
    CC_CHECK(_wait_flag(&c.flag_client_done, 3000000), "截断客户端完成");
    CC_CHECK(_wait_flag(&c.flag_server_done, 3000000), "服务端观察到截断");
    CC_CHECK(c.flag_trunc == 1, "recvfrom 返回 TRUNCATED");
    CC_CASE_END();
}

/* ----------------------------------------------------------- 地址 / DNS */

static void _test_addr(void)
{
    CCNetAddr a;
    char text[64];

    CC_CASE("NET-ADDR-001", "IPv4 / IPv6 地址解析与文本");
    CC_CHECK_EQ_I64("parse IPv4", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 9000, &a));
    CC_CHECK_EQ_I64("family v4", CC_NET_AF_INET, a.family);
    CC_CHECK_EQ_I64("port v4", 9000, a.port);
    CC_CHECK(a.addr[0] == 127 && a.addr[1] == 0 &&
             a.addr[2] == 0 && a.addr[3] == 1, "IPv4 字节");

    CC_CHECK_EQ_I64("parse IPv6", CC_NET_OK,
                    ccnet_addr_parse("::1", 9000, &a));
    CC_CHECK_EQ_I64("family v6", CC_NET_AF_INET6, a.family);
    CC_CHECK(a.addr[15] == 1, "IPv6 末字节");

    CC_CHECK_EQ_I64("parse domain", CC_NET_ERR_DNS,
                    ccnet_addr_parse("example.com", 9000, &a));

    CC_CHECK_EQ_I64("parse v4", CC_NET_OK,
                    ccnet_addr_parse("1.2.3.4", 9000, &a));
    CC_CHECK_EQ_I64("text v4", CC_NET_OK, ccnet_addr_text(&a, text, sizeof(text)));
    CC_CHECK_EQ_STR("text v4 内容", "1.2.3.4:9000", text);

    CC_CHECK_EQ_I64("parse v6", CC_NET_OK,
                    ccnet_addr_parse("2001:db8::1", 9000, &a));
    CC_CHECK_EQ_I64("text v6", CC_NET_OK, ccnet_addr_text(&a, text, sizeof(text)));
    CC_CHECK_EQ_STR("text v6 内容", "[2001:db8::1]:9000", text);

    CC_CHECK_EQ_I64("text 小缓冲", CC_NET_ERR_PARAM,
                    ccnet_addr_text(&a, text, 4));
    CC_CASE_END();
}

static void _test_scope(void)
{
    CCNetAddr a;
    char text[64];

    CC_CASE("NET-ADDR-002", "IPv6 scope 解析与文本");
    CC_CHECK_EQ_I64("parse scope", CC_NET_OK,
                    ccnet_addr_parse("[fe80::1%3]", 9000, &a));
    CC_CHECK_EQ_I64("scope 值", 3, a.scope);
    CC_CHECK_EQ_I64("text scope", CC_NET_OK, ccnet_addr_text(&a, text, sizeof(text)));
    CC_CHECK_EQ_STR("text scope 内容", "[fe80::1%3]:9000", text);

    CC_CHECK_EQ_I64("v4 带 scope", CC_NET_ERR_PARAM,
                    ccnet_addr_parse("1.2.3.4%3", 9000, &a));
    CC_CHECK_EQ_I64("v6 未指定 scope", CC_NET_OK,
                    ccnet_addr_parse("fe80::1", 9000, &a));
    CC_CHECK_EQ_I64("scope 为 0", 0, a.scope);

    CC_CHECK_EQ_I64("scope 超界", CC_NET_ERR_PARAM,
                    ccnet_addr_parse("[fe80::1%4294967296]", 9000, &a));
    CC_CHECK_EQ_I64("scope 负数", CC_NET_ERR_PARAM,
                    ccnet_addr_parse("[fe80::1%-1]", 9000, &a));
    CC_CHECK_EQ_I64("scope 最大值", CC_NET_OK,
                    ccnet_addr_parse("[fe80::1%4294967295]", 9000, &a));
    CC_CHECK_EQ_I64("scope 最大值", 4294967295, a.scope);
    CC_CASE_END();
}

static void _test_dns(void)
{
    CCNetAddr out[8];
    CCINT32 n;

    CC_CASE("NET-DNS-001", "同步 DNS 与字面量快速路径");
    n = ccnet_dns("localhost", 9000, out, 8);
    CC_CHECK(n >= 1, "localhost 至少一个结果");
    if (n >= 1)
        CC_CHECK_EQ_I64("dns 端口", 9000, out[0].port);

    CC_CHECK_EQ_I64("字面量快速路径", 1,
                    ccnet_dns("127.0.0.1", 9000, out, 8));
    CC_CHECK_EQ_I64("字面量 family", CC_NET_AF_INET, out[0].family);

    CC_CHECK_EQ_I64("空 host", CC_NET_ERR_PARAM,
                    ccnet_dns(NULL, 9000, out, 8));
    CC_CHECK_EQ_I64("cap 0", CC_NET_ERR_PARAM,
                    ccnet_dns("localhost", 9000, out, 0));
    CC_CHECK_EQ_I64("cap 超界", CC_NET_ERR_PARAM,
                    ccnet_dns("localhost", 9000, out, 0x80000000u));
    CC_CASE_END();
}

/* -------------------------------------------------------------- 生命周期 */

static _Atomic CCINT32 g_setup_entry_called;

static void _setup_fail_entry(const CCNetTcpOps *ops, void *ctx, void *user)
{
    CC_UNUSED(ops);
    CC_UNUSED(ctx);
    CC_UNUSED(user);
    atomic_store_explicit(&g_setup_entry_called, 1, memory_order_release);
}

static void _test_setup_failure(void)
{
    CCNetAddr a;
    CCINT32 rc;

    CC_CASE("NET-LIFE-001", "setup 失败不调用入口");
    atomic_store_explicit(&g_setup_entry_called, 0, memory_order_release);
    CC_CHECK_EQ_I64("parse", CC_NET_OK,
                    ccnet_addr_parse("127.0.0.1", 1, &a));
    rc = ccnet_tcp_connect(&a, 1000000, _setup_fail_entry, NULL);
    CC_CHECK(rc < 0, "连接失败返回负值");
    CC_CHECK(atomic_load_explicit(&g_setup_entry_called, memory_order_acquire) == 0, "入口未被调用");
    CC_CASE_END();
}

static void _noop_accept(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    CC_UNUSED(ops);
    CC_UNUSED(ctx);
    CC_UNUSED(user);
}

static void _test_repeat(void)
{
    CCINT32 i;

    CC_CASE("NET-LIFE-002", "反复创建与入口返回");
    for (i = 0; i < 16; i++)
    {
        CCNetAddr a;
        CCINT32 rc = ccnet_addr_parse("127.0.0.1", 0, &a);
        CC_CHECK_EQ_I64("parse", CC_NET_OK, rc);
        rc = ccnet_tcp_listen(&a, _noop_accept, NULL);
        CC_CHECK_EQ_I64("listen", CC_NET_OK, rc);
    }
    ccsleep(50000);
    CC_CHECK(1, "反复创建与返回完成");
    CC_CASE_END();
}

void test_ccnet(void)
{
    _test_addr();
    _test_scope();
    _test_dns();
    _test_tcp_echo();
    _test_tcp_timeout_fin();
    _test_tcp_duplex();
    _test_tcp_multi();
    _test_tcp_reset();
    _test_tcp_keepalive();
    _test_udp_echo();
    _test_udp_zero();
    _test_udp_trunc();
    _test_setup_failure();
    _test_repeat();
}
