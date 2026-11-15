/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核 UDP 实现：端点绑定与数据报收发。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccnet_internal.h"

#ifndef MSG_TRUNC
#define MSG_TRUNC 0x20
#endif

CCINT32 _ccnet_udp_setup(CCNetStart *start, CCNetCtx *ctx)
{
    CCNetSocket sock;
    struct sockaddr_storage ss;
    CCNetSockLen len;
    CCINT32 rc;
    int opt = 1;

    if (!start || !ctx)
        return CC_NET_ERR_PARAM;
    if (start->addr.family != CC_NET_AF_INET &&
        start->addr.family != CC_NET_AF_INET6)
        return CC_NET_ERR_PARAM;
    if (!_ccnet_ready())
        return CC_NET_ERR_IO;

    sock = socket(start->addr.family == CC_NET_AF_INET ? AF_INET : AF_INET6,
                  SOCK_DGRAM, 0);
    if (sock == CC_NET_SOCK_INVALID)
        return _ccnet_map_os(_ccnet_last_error());

    if (start->addr.family == CC_NET_AF_INET6)
    {
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
                       (const char *)&opt, sizeof(opt)) != 0)
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

    len = (CCNetSockLen)sizeof(ss);
    if (getsockname(sock, (struct sockaddr *)&ss, &len) == 0)
        _ccnet_addr_from_sock(&ctx->local, (struct sockaddr *)&ss, len);

    ctx->sock = sock;
    return CC_NET_OK;
}

CCINT32 ccnet_udp_open(const CCNetAddr *addr, CCNetUdpEntry entry, void *user)
{
    CCNetStart start;

    if (!addr || !entry)
        return CC_NET_ERR_PARAM;

    memset(&start, 0, sizeof(start));
    start.kind = CC_NET_KIND_UDP;
    start.addr = *addr;
    start.udp_entry = entry;
    start.user = user;
    start.preconnected = CC_NET_SOCK_INVALID;
    start.heap = CCFALSE;
    return _ccnet_spawn(&start, _ccnet_udp_wrapper);
}

static CCINT32 _udp_local(void *ctx, CCNetAddr *out)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    if (!inner || !out)
        return CC_NET_ERR_PARAM;
    *out = inner->local;
    return CC_NET_OK;
}

static CCINT64 _udp_sendto(void *ctx, const CCNetAddr *to, const void *data, CCUINT64 size, CCUINT32 timeOut_us)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    struct sockaddr_storage ss;
    CCNetSockLen len;
    CCINT32 rc;
    int sent;

    if (!inner || !to || (size != 0 && !data))
        return CC_NET_ERR_PARAM;
    if (to->port == 0)
        return CC_NET_ERR_PARAM;

    rc = _ccnet_addr_to_sock(to, &ss, &len);
    if (rc != CC_NET_OK)
        return rc;

    rc = _ccnet_set_timeout(inner->sock, CCFALSE, timeOut_us);
    if (rc != CC_NET_OK)
        return rc;

#ifdef CC_WINDOWS
    if (size > 0x7FFFFFFFull)
        return CC_NET_ERR_TOOBIG;
    sent = sendto(inner->sock, (const char *)data, (int)size, 0, (struct sockaddr *)&ss, len);
#else
    sent = (int)sendto(inner->sock, data, (size_t)size, 0, (struct sockaddr *)&ss, len);
#endif
    if (sent >= 0)
        return (CCINT64)sent;
    return _ccnet_map_os(_ccnet_last_error());
}

static CCINT64 _udp_recvfrom(void *ctx, CCNetAddr *from, void *buf, CCUINT64 cap, CCUINT32 timeOut_us)
{
    CCNetCtx *inner = (CCNetCtx *)ctx;
    CCINT32 rc;
#ifdef CC_WINDOWS
    WSABUF ws;
    DWORD bytes;
    DWORD flags;
    struct sockaddr_storage ss;
    int from_len;
    int ret;
    CCINT32 err;
#else
    struct sockaddr_storage ss;
    struct iovec iov;
    struct msghdr msg;
    ssize_t got;
#endif

    if (!inner || !buf || cap == 0)
        return CC_NET_ERR_PARAM;

    rc = _ccnet_set_timeout(inner->sock, CCTRUE, timeOut_us);
    if (rc != CC_NET_OK)
        return rc;

#ifdef CC_WINDOWS
    if (cap > 0x7FFFFFFFull)
        return CC_NET_ERR_TOOBIG;

    bytes = 0;
    flags = 0;
    from_len = (int)sizeof(ss);
    ws.buf = (char *)buf;
    ws.len = (ULONG)cap;

    ret = WSARecvFrom(
        inner->sock, &ws, 1, &bytes, &flags,
        from ? (struct sockaddr *)&ss : NULL,
        from ? &from_len : NULL, NULL, NULL
    );
    if (ret == SOCKET_ERROR)
    {
        err = _ccnet_last_error();
        if (err == WSAEMSGSIZE)
            return CC_NET_ERR_TRUNCATED;
        return _ccnet_map_os(err);
    }
    if (from && from_len > 0)
        _ccnet_addr_from_sock(from, (struct sockaddr *)&ss,
                              (CCNetSockLen)from_len);
    return (CCINT64)bytes;
#else
    iov.iov_base = buf;
    iov.iov_len = (size_t)cap;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (from)
    {
        msg.msg_name = &ss;
        msg.msg_namelen = (socklen_t)sizeof(ss);
    }

    got = recvmsg(inner->sock, &msg, 0);
    if (got < 0)
        return _ccnet_map_os(_ccnet_last_error());
    if (msg.msg_flags & MSG_TRUNC)
        return CC_NET_ERR_TRUNCATED;
    if (from && msg.msg_namelen > 0)
        _ccnet_addr_from_sock(from, (struct sockaddr *)&ss, (CCNetSockLen)msg.msg_namelen);
    return (CCINT64)got;
#endif
}

const CCNetUdpOps _ccnet_udp_ops = {
    _udp_local,
    _udp_sendto,
    _udp_recvfrom,
};
