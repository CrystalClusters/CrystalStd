/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核 TCP 实现：连接、监听、收发。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccnet_internal.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static CCINT32 _fill_addrs(CCNetCtx *ctx)
{
    struct sockaddr_storage ss;
    CCNetSockLen len;

    // len 为传入容量 / 传出实际长度，每次调用前都要重置为缓冲区大小
    len = (CCNetSockLen)sizeof(ss);
    if (getsockname(ctx->sock, (struct sockaddr *)&ss, &len) == 0)
        _ccnet_addr_from_sock(&ctx->local, (struct sockaddr *)&ss, len);

    len = (CCNetSockLen)sizeof(ss);
    if (getpeername(ctx->sock, (struct sockaddr *)&ss, &len) == 0)
        _ccnet_addr_from_sock(&ctx->peer, (struct sockaddr *)&ss, len);
    return CC_NET_OK;
}

static CCINT32 _connect_addr(CCNetSocket sock, const CCNetAddr *addr, CCUINT32 time_out_us)
{
    struct sockaddr_storage ss;
    CCNetSockLen len;
    CCINT32 rc;

    rc = _ccnet_addr_to_sock(addr, &ss, &len);
    if (rc != CC_NET_OK)
        return rc;

    rc = _ccnet_set_blocking(sock, CCFALSE);
    if (rc != CC_NET_OK)
        return rc;

    if (connect(sock, (struct sockaddr *)&ss, len) == 0)
    {
        rc = CC_NET_OK;
    }
    else
    {
        CCINT32 err = _ccnet_last_error();
#ifdef CC_WINDOWS
        CCBOOL pending = (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS);
#else
        CCBOOL pending = (err == EINPROGRESS || err == EWOULDBLOCK);
#endif
        if (!pending)
        {
            rc = _ccnet_map_os(err);
        }
        else
        {
            rc = _ccnet_poll(sock, CCTRUE, time_out_us);
            if (rc == CC_NET_OK)
            {
                int so_error = 0;   // getsockopt 的 SO_ERROR 输出：两种平台均为 int
                CCNetSockLen so_len = (CCNetSockLen)sizeof(so_error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &so_len) != 0)
                    rc = _ccnet_map_os(_ccnet_last_error());
                else if (so_error != 0)
                    rc = _ccnet_map_os((CCINT32)so_error);
            }
        }
    }

    if (rc == CC_NET_OK)
        rc = _ccnet_set_blocking(sock, CCTRUE);
    return rc;
}

CCINT32 _ccnet_tcp_setup(CCNetStart *start, CCNetCtx *ctx)
{
    CCNetSocket sock;
    CCINT32 rc;

    if (!start || !ctx)
        return CC_NET_ERR_PARAM;

    if (start->preconnected != CC_NET_SOCK_INVALID)
    {
        ctx->sock = start->preconnected;
        rc = _ccnet_tcp_keepalive(ctx->sock);
        if (rc != CC_NET_OK)
            return rc;
        return _fill_addrs(ctx);
    }

    if (start->addr.family != CC_NET_AF_INET &&
        start->addr.family != CC_NET_AF_INET6)
        return CC_NET_ERR_PARAM;
    if (start->addr.port == 0)
        return CC_NET_ERR_PARAM;
    if (!_ccnet_ready())
        return CC_NET_ERR_IO;

    // 地址族直接来自已校验的 CCNetAddr，不经过 getaddrinfo
    sock = socket(
        start->addr.family == CC_NET_AF_INET ? AF_INET : AF_INET6,
        SOCK_STREAM, 0
    );
    if (sock == CC_NET_SOCK_INVALID)
        return _ccnet_map_os(_ccnet_last_error());

    rc = _connect_addr(sock, &start->addr, start->timeOut_us);
    if (rc != CC_NET_OK)
    {
        _ccnet_sock_close(sock);
        return rc;
    }

    ctx->sock = sock;
    rc = _ccnet_tcp_keepalive(sock);
    if (rc != CC_NET_OK)
        return rc;
    return _fill_addrs(ctx);
}

CCINT32 _ccnet_listen_setup(CCNetStart *start, CCNetCtx *ctx)
{
    CCNetSocket sock;
    struct sockaddr_storage ss;
    CCNetSockLen len;
    CCINT32 rc;
    int opt = 1;    // setsockopt 的开关量 optval 按接口约定为 int

    if (!start || !ctx)
        return CC_NET_ERR_PARAM;
    if (start->addr.family != CC_NET_AF_INET &&
        start->addr.family != CC_NET_AF_INET6)
        return CC_NET_ERR_PARAM;
    if (!_ccnet_ready())
        return CC_NET_ERR_IO;

    sock = socket(
        start->addr.family == CC_NET_AF_INET ? AF_INET : AF_INET6,
        SOCK_STREAM, 0
    );
    if (sock == CC_NET_SOCK_INVALID)
        return _ccnet_map_os(_ccnet_last_error());

    if (start->addr.family == CC_NET_AF_INET6)
    {
        if (setsockopt(
                sock, IPPROTO_IPV6, IPV6_V6ONLY,
                (const char *)&opt, sizeof(opt)
            ) != 0
        )
        {
            rc = _ccnet_map_os(_ccnet_last_error());
            _ccnet_sock_close(sock);
            return rc;
        }
    }

    rc = _ccnet_addr_to_sock(&start->addr, &ss, &len);
    if (rc != CC_NET_OK)
    {
        _ccnet_sock_close(sock);
        return rc;
    }

    if (bind(sock, (struct sockaddr *)&ss, len) != 0)
    {
        rc = _ccnet_map_os(_ccnet_last_error());
        _ccnet_sock_close(sock);
        return rc;
    }

    if (listen(sock, 128) != 0)
    {
        rc = _ccnet_map_os(_ccnet_last_error());
        _ccnet_sock_close(sock);
        return rc;
    }

    len = (CCNetSockLen)sizeof(ss);
    if (getsockname(sock, (struct sockaddr *)&ss, &len) == 0)
        _ccnet_addr_from_sock(&ctx->local, (struct sockaddr *)&ss, len);

    ctx->sock = sock;
    return CC_NET_OK;
}

CCINT32 ccnet_tcp_connect(const CCNetAddr *addr, CCUINT32 timeOut_us, CCNetTcpEntry entry, void *user)
{
    CCNetStart start;

    if (!addr || !entry)
        return CC_NET_ERR_PARAM;

    memset(&start, 0, sizeof(start));
    start.kind = CC_NET_KIND_TCP;
    start.addr = *addr;
    start.timeOut_us = timeOut_us;
    start.tcp_entry = entry;
    start.user = user;
    start.preconnected = CC_NET_SOCK_INVALID;
    start.heap = CCFALSE;
    return _ccnet_spawn(&start, _ccnet_tcp_wrapper);
}

CCINT32 ccnet_tcp_listen(const CCNetAddr *addr, CCNetAcceptEntry entry, void *user)
{
    CCNetStart start;

    if (!addr || !entry)
        return CC_NET_ERR_PARAM;

    memset(&start, 0, sizeof(start));
    start.kind = CC_NET_KIND_LISTENER;
    start.addr = *addr;
    start.accept_entry = entry;
    start.user = user;
    start.preconnected = CC_NET_SOCK_INVALID;
    start.heap = CCFALSE;
    return _ccnet_spawn(&start, _ccnet_listen_wrapper);
}

static CCINT32 _tcp_accept(void *ctx, CCNetTcpEntry entry, void *user, CCUINT32 timeOut_us)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    CCNetSocket sock;
    CCNetStart *start;
    CCINT32 rc;

    if (!inner || inner->kind != CC_NET_KIND_LISTENER || !entry)
        return CC_NET_ERR_PARAM;
    if (!_ccnet_ready())
        return CC_NET_ERR_IO;

    rc = _ccnet_poll(inner->sock, CCFALSE, timeOut_us);
    if (rc != CC_NET_OK)
        return rc;

    sock = accept(inner->sock, NULL, NULL);
    if (sock == CC_NET_SOCK_INVALID)
        return _ccnet_map_os(_ccnet_last_error());

    start = ccalloc(NULL, sizeof(*start), "ccnet accept");
    if (!start)
    {
        _ccnet_sock_close(sock);
        return CC_NET_ERR_NOMEM;
    }

    memset(start, 0, sizeof(*start));
    start->kind = CC_NET_KIND_TCP;
    start->tcp_entry = entry;
    start->user = user;
    start->preconnected = sock;
    start->heap = CCTRUE;
    start->lock = NULL;

    if (!cc_thread_create(_ccnet_tcp_wrapper, start))
    {
        _ccnet_sock_close(sock);
        ccalloc(start, 0, NULL);
        return CC_NET_ERR_RESOURCE;
    }
    return CC_NET_OK;
}

static CCINT32 _tcp_state(void *ctx, CCNetState *out)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;

    if (!inner || !out)
        return CC_NET_ERR_PARAM;
    *out = (CCNetState)atomic_load_explicit(&inner->state, memory_order_acquire);
    return CC_NET_OK;
}

static CCINT32 _tcp_local(void *ctx, CCNetAddr *out)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;

    if (!inner || !out)
        return CC_NET_ERR_PARAM;
    *out = inner->local;
    return CC_NET_OK;
}

static CCINT32 _tcp_peer(void *ctx, CCNetAddr *out)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;

    if (!inner || !out)
        return CC_NET_ERR_PARAM;
    *out = inner->peer;
    return CC_NET_OK;
}

static CCINT64 _tcp_send(void *ctx, const void *data, CCUINT64 size, CCUINT32 timeOut_us)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    CCINT32 rc;
    CCINT32 os_err;
    int sent;       // send 的返回值：Winsock 为 int，POSIX 为 ssize_t，此处统一按 int 保存

    if (!inner || (size != 0 && !data))
        return CC_NET_ERR_PARAM;
    if (atomic_load_explicit(&inner->state, memory_order_acquire) ==
        CC_NET_STATE_FAILED)
        return CC_NET_ERR_STATE;
    if (size == 0)
        return 0;

    rc = _ccnet_set_timeout(inner->sock, CCFALSE, timeOut_us);
    if (rc != CC_NET_OK)
        return rc;

#ifdef CC_WINDOWS
    // 长度参数按 Winsock 约定为 int；数据超过 int 上限时取 0x7FFFFFFF
    sent = send(inner->sock, (const char *)data, size > 0x7FFFFFFFull ? 0x7FFFFFFF : (int)size, 0);
#else
    sent = (int)send(inner->sock, data, (size_t)size, MSG_NOSIGNAL);
#endif
    if (sent >= 0)
        return (CCINT64)sent;

    os_err = _ccnet_last_error();
    rc = _ccnet_map_os(os_err);
#ifndef CC_WINDOWS
    if (os_err == ETIMEDOUT)
        atomic_store_explicit(&inner->state, CC_NET_STATE_FAILED, memory_order_release);
#endif
    if (rc == CC_NET_ERR_RESET)
        atomic_store_explicit(&inner->state, CC_NET_STATE_FAILED, memory_order_release);
    return rc;
}

static CCINT64 _tcp_recv(void *ctx, void *buf, CCUINT64 cap, CCUINT32 timeOut_us)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    CCINT32 rc;
    CCINT32 os_err;
    int got;        // recv 的返回值：Winsock 为 int，POSIX 为 ssize_t，此处统一按 int 保存

    if (!inner || !buf || cap == 0)
        return CC_NET_ERR_PARAM;
    if (atomic_load_explicit(&inner->state, memory_order_acquire) ==
        CC_NET_STATE_FAILED)
        return CC_NET_ERR_STATE;

    rc = _ccnet_set_timeout(inner->sock, CCTRUE, timeOut_us);
    if (rc != CC_NET_OK)
        return rc;

#ifdef CC_WINDOWS
    // 长度参数按 Winsock 约定为 int；缓冲区超过 int 上限时取 0x7FFFFFFF
    got = recv(inner->sock, (char *)buf, cap > 0x7FFFFFFFull ? 0x7FFFFFFF : (int)cap, 0);
#else
    got = (int)recv(inner->sock, buf, (size_t)cap, 0);
#endif
    if (got > 0)
        return (CCINT64)got;
    if (got == 0)
    {
        atomic_store_explicit(&inner->state, CC_NET_STATE_PEER_CLOSED, memory_order_release);
        return 0;
    }

    os_err = _ccnet_last_error();
    rc = _ccnet_map_os(os_err);
#ifndef CC_WINDOWS
    if (os_err == ETIMEDOUT)
        atomic_store_explicit(&inner->state, CC_NET_STATE_FAILED, memory_order_release);
#endif
    if (rc == CC_NET_ERR_RESET)
        atomic_store_explicit(&inner->state, CC_NET_STATE_FAILED, memory_order_release);
    return rc;
}

const CCNetTcpOps _ccnet_tcp_ops = {
    _tcp_state,
    _tcp_local,
    _tcp_peer,
    _tcp_send,
    _tcp_recv,
};

const CCNetAcceptOps _ccnet_accept_ops = {
    _tcp_accept,
    _tcp_local,
};
