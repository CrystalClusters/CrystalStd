/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核平台原语：错误映射、超时、轮询、阻塞开关。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccnet_internal.h"
#include <limits.h>

#ifndef CC_WINDOWS
#include <poll.h>
#endif

CCINT32 _ccnet_last_error(void)
{
#ifdef CC_WINDOWS
    return (CCINT32)WSAGetLastError();
#else
    return (CCINT32)errno;
#endif
}

CCINT32 _ccnet_map_os(CCINT32 os_code)
{
#ifdef CC_WINDOWS
    switch (os_code)
    {
    case WSAEWOULDBLOCK:
    case WSAEINPROGRESS:
    case WSAETIMEDOUT:
        return CC_NET_ERR_TIMEOUT;
    case WSAECONNREFUSED:
        return CC_NET_ERR_REFUSED;
    case WSAEHOSTUNREACH:
    case WSAENETUNREACH:
    case WSAENETDOWN:
    case WSAEHOSTDOWN:
        return CC_NET_ERR_UNREACH;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENETRESET:
    case WSAENOTCONN:
        return CC_NET_ERR_RESET;
    case WSAEADDRINUSE:
        return CC_NET_ERR_ADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return CC_NET_ERR_ADDRNOTAVAIL;
    case WSAEMSGSIZE:
        return CC_NET_ERR_TOOBIG;
    case WSAEMFILE:
    case WSAENOBUFS:
        return CC_NET_ERR_RESOURCE;
    default:
        return CC_NET_ERR_IO;
    }
#else
    switch (os_code)
    {
    case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
    case EWOULDBLOCK:
#endif
    case ETIMEDOUT:
        return CC_NET_ERR_TIMEOUT;
    case ECONNREFUSED:
        return CC_NET_ERR_REFUSED;
    case EHOSTUNREACH:
    case ENETUNREACH:
    case ENETDOWN:
    case EHOSTDOWN:
        return CC_NET_ERR_UNREACH;
    case ECONNRESET:
    case EPIPE:
    case ECONNABORTED:
        return CC_NET_ERR_RESET;
    case EADDRINUSE:
        return CC_NET_ERR_ADDRINUSE;
    case EADDRNOTAVAIL:
        return CC_NET_ERR_ADDRNOTAVAIL;
    case EMSGSIZE:
        return CC_NET_ERR_TOOBIG;
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
        return CC_NET_ERR_RESOURCE;
    case ENOMEM:
        return CC_NET_ERR_NOMEM;
    default:
        return CC_NET_ERR_IO;
    }
#endif
}

CCINT32 _ccnet_map_gai(CCINT32 gai_code)
{
#ifdef CC_WINDOWS
    switch (gai_code)
    {
    case WSAHOST_NOT_FOUND:
    case WSATRY_AGAIN:
    case WSANO_RECOVERY:
    case WSANO_DATA:
        return CC_NET_ERR_DNS;
    default:
        return CC_NET_ERR_DNS;
    }
#else
    switch (gai_code)
    {
    case EAI_AGAIN:
    case EAI_FAIL:
    case EAI_NONAME:
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
    case EAI_NODATA:
#endif
    case EAI_SERVICE:
        return CC_NET_ERR_DNS;
    default:
        return CC_NET_ERR_DNS;
    }
#endif
}

CCUINT32 _ccnet_norm_timeout(CCUINT32 us)
{
    return us == 0 ? CC_NET_DEFAULT_TIMEOUT_US : us;
}

CCINT32 _ccnet_set_timeout(CCNetSocket sock, CCBOOL recv, CCUINT32 us)
{
#ifdef CC_WINDOWS
    DWORD ms;
    int level;
    int name;
#else
    struct timeval tv;
#endif

    us = _ccnet_norm_timeout(us);
    if (us == CC_NET_TIMEOUT_INFINITE)
        us = 0;

#ifdef CC_WINDOWS
    ms = us ? (DWORD)(us / 1000u + (us % 1000u != 0)) : 0;
    level = SOL_SOCKET;
    name = recv ? SO_RCVTIMEO : SO_SNDTIMEO;
    if (setsockopt(sock, level, name, (const char *)&ms, sizeof(ms)) == SOCKET_ERROR)
        return _ccnet_map_os(_ccnet_last_error());
    return CC_NET_OK;
#else
    tv.tv_sec = (time_t)(us / 1000000u);
    tv.tv_usec = (suseconds_t)(us % 1000000u);
    if (setsockopt(sock, SOL_SOCKET, recv ? SO_RCVTIMEO : SO_SNDTIMEO,
                   &tv, sizeof(tv)) != 0)
        return _ccnet_map_os(_ccnet_last_error());
    return CC_NET_OK;
#endif
}

CCINT32 _ccnet_tcp_keepalive(CCNetSocket sock)
{
#ifdef CC_WINDOWS
    struct tcp_keepalive ka;
    DWORD bytes = 0;

    ka.onoff = 1;
    ka.keepalivetime = (u_long)(CC_NET_KEEPALIVE_IDLE_SEC * 1000);
    ka.keepaliveinterval = (u_long)(CC_NET_KEEPALIVE_INTERVAL_SEC * 1000);
    if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &ka, sizeof(ka),
                 NULL, 0, &bytes, NULL, NULL) == SOCKET_ERROR)
        return _ccnet_map_os(_ccnet_last_error());
    return CC_NET_OK;
#else
    int on = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) != 0)
        return _ccnet_map_os(_ccnet_last_error());

#if defined(TCP_KEEPIDLE)
    {
        int idle = CC_NET_KEEPALIVE_IDLE_SEC;
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,
                       &idle, sizeof(idle)) != 0)
            return _ccnet_map_os(_ccnet_last_error());
    }
#elif defined(TCP_KEEPALIVE)
    {
        int idle = CC_NET_KEEPALIVE_IDLE_SEC;
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE,
                       &idle, sizeof(idle)) != 0)
            return _ccnet_map_os(_ccnet_last_error());
    }
#endif

#if defined(TCP_KEEPINTVL)
    {
        int interval = CC_NET_KEEPALIVE_INTERVAL_SEC;
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL,
                       &interval, sizeof(interval)) != 0)
            return _ccnet_map_os(_ccnet_last_error());
    }
#endif

#if defined(TCP_KEEPCNT)
    {
        int count = CC_NET_KEEPALIVE_COUNT;
        if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,
                       &count, sizeof(count)) != 0)
            return _ccnet_map_os(_ccnet_last_error());
    }
#endif
    return CC_NET_OK;
#endif
}

CCINT32 _ccnet_poll(CCNetSocket sock, CCBOOL write, CCUINT32 us)
{
    int timeout_ms;
    us = _ccnet_norm_timeout(us);
    if (us == CC_NET_TIMEOUT_INFINITE)
        timeout_ms = -1;
    else
    {
        CCUINT32 ms32 = us / 1000u + (us % 1000u != 0);
        timeout_ms = ms32 > (CCUINT32)INT_MAX ? INT_MAX : (int)ms32;
    }

    for (;;)
    {
#ifdef CC_WINDOWS
        WSAPOLLFD pfd;
        pfd.fd = sock;
        pfd.events = write ? POLLWRNORM : POLLRDNORM;
        pfd.revents = 0;
        int rc = WSAPoll(&pfd, 1, timeout_ms);
#else
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = write ? POLLOUT : POLLIN;
        pfd.revents = 0;
        int rc = poll(&pfd, 1, timeout_ms);
#endif
        if (rc == 0)
            return CC_NET_ERR_TIMEOUT;
        if (rc < 0)
        {
#ifdef CC_WINDOWS
            CCINT32 err = _ccnet_last_error();
            if (err == WSAEINTR)
                continue;
#else
            CCINT32 err = _ccnet_last_error();
            if (err == EINTR)
                continue;
#endif
            return _ccnet_map_os(err);
        }
        return CC_NET_OK;
    }
}

CCINT32 _ccnet_set_blocking(CCNetSocket sock, CCBOOL blocking)
{
#ifdef CC_WINDOWS
    u_long mode = blocking ? 0u : 1u;
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR)
        return _ccnet_map_os(_ccnet_last_error());
    return CC_NET_OK;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return _ccnet_map_os(_ccnet_last_error());
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, flags) < 0)
        return _ccnet_map_os(_ccnet_last_error());
    return CC_NET_OK;
#endif
}

CCINT32 _ccnet_sock_close(CCNetSocket sock)
{
    if (sock == CC_NET_SOCK_INVALID)
        return CC_NET_OK;
#ifdef CC_WINDOWS
    return closesocket(sock) == 0 ? CC_NET_OK : CC_NET_ERR_IO;
#else
    return close(sock) == 0 ? CC_NET_OK : CC_NET_ERR_IO;
#endif
}
