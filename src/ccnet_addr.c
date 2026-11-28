/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核地址解析、格式化与同步 DNS。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccnet_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static CCINT32 _scope_from_text(const char *text, CCUINT32 *out)
{
    char *end = NULL;
    unsigned long value;    // strtoul 的原生返回宽度：ERANGE 与 UINT32 上界判定依赖它
    unsigned int index;     // if_nametoindex 的返回类型：与 IPv6 接口索引同宽

    if (!text || !*text)
        return CC_NET_ERR_PARAM;

    if (*text >= '0' && *text <= '9')
    {
        errno = 0;
        value = strtoul(text, &end, 10);
        if (end && *end == '\0')
        {
            if (errno == ERANGE || value > (unsigned long)UINT32_MAX)
                return CC_NET_ERR_PARAM;
            *out = (CCUINT32)value;
            return CC_NET_OK;
        }
    }

    index = if_nametoindex(text);
    if (index != 0)
    {
        *out = (CCUINT32)index;
        return CC_NET_OK;
    }
    return CC_NET_ERR_PARAM;
}

CCINT32 _ccnet_addr_to_sock(const CCNetAddr *addr, struct sockaddr_storage *ss, CCNetSockLen *len)
{
    if (!addr || !ss || !len)
        return CC_NET_ERR_PARAM;

    memset(ss, 0, sizeof(*ss));
    if (addr->family == CC_NET_AF_INET)
    {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(addr->port);
        memcpy(&sin->sin_addr, addr->addr, 4);
        *len = (CCNetSockLen)sizeof(*sin);
        return CC_NET_OK;
    }
    if (addr->family == CC_NET_AF_INET6)
    {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(addr->port);
        sin6->sin6_scope_id = addr->scope;
        memcpy(&sin6->sin6_addr, addr->addr, 16);
        *len = (CCNetSockLen)sizeof(*sin6);
        return CC_NET_OK;
    }
    return CC_NET_ERR_PARAM;
}

void _ccnet_addr_from_sock(CCNetAddr *out, const struct sockaddr *sa, CCNetSockLen len)
{
    if (!out || !sa)
        return;

    memset(out, 0, sizeof(*out));
    if (sa->sa_family == AF_INET && (size_t)len >= sizeof(struct sockaddr_in))
    {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        out->family = CC_NET_AF_INET;
        out->port = ntohs(sin->sin_port);
        memcpy(out->addr, &sin->sin_addr, 4);
    }
    else if (sa->sa_family == AF_INET6 && (size_t)len >= sizeof(struct sockaddr_in6))
    {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        out->family = CC_NET_AF_INET6;
        out->port = ntohs(sin6->sin6_port);
        out->scope = (CCUINT32)sin6->sin6_scope_id;
        memcpy(out->addr, &sin6->sin6_addr, 16);
    }
}

CCINT32 ccnet_addr_parse(const char *text, CCUINT16 port, CCNetAddr *out)
{
    char base[128];
    char scope_text[64];
    const char *p;
    const char *close;
    char *pct;
    size_t n;
    struct in6_addr a6;
    struct in_addr a4;
    CCUINT32 scope = 0;
    CCBOOL has_scope = CCFALSE;
    CCINT32 rc;

    if (!text || !*text || !out)
        return CC_NET_ERR_PARAM;

    memset(base, 0, sizeof(base));
    memset(scope_text, 0, sizeof(scope_text));
    memset(out, 0, sizeof(*out));

    p = text;
    if (*p == '[')
    {
        close = strchr(p, ']');
        if (!close)
            return CC_NET_ERR_PARAM;
        n = (size_t)(close - (p + 1));
        if (n == 0 || n >= sizeof(base))
            return CC_NET_ERR_PARAM;
        memcpy(base, p + 1, n);
        p = close + 1;
        if (*p == '%')
        {
            has_scope = CCTRUE;
            if (strlen(p + 1) >= sizeof(scope_text))
                return CC_NET_ERR_PARAM;
            strcpy(scope_text, p + 1);
        }
        else if (*p != '\0')
        {
            return CC_NET_ERR_PARAM;
        }
    }
    else
    {
        if (strlen(p) >= sizeof(base))
            return CC_NET_ERR_PARAM;
        strcpy(base, p);
    }

    pct = strchr(base, '%');
    if (pct)
    {
        if (has_scope)
            return CC_NET_ERR_PARAM;
        if (strlen(pct + 1) >= sizeof(scope_text))
            return CC_NET_ERR_PARAM;
        *pct = '\0';
        strcpy(scope_text, pct + 1);
        has_scope = CCTRUE;
    }

    if (has_scope)
    {
        rc = _scope_from_text(scope_text, &scope);
        if (rc != CC_NET_OK)
            return rc;
    }

    if (inet_pton(AF_INET6, base, &a6) == 1)
    {
        out->family = CC_NET_AF_INET6;
        out->port = port;
        out->scope = scope;
        memcpy(out->addr, &a6, 16);
        return CC_NET_OK;
    }

    if (has_scope)
        return CC_NET_ERR_PARAM;

    if (inet_pton(AF_INET, base, &a4) == 1)
    {
        out->family = CC_NET_AF_INET;
        out->port = port;
        memcpy(out->addr, &a4, 4);
        return CC_NET_OK;
    }

    return CC_NET_ERR_DNS;
}

CCINT32 ccnet_addr_text(const CCNetAddr *addr, char *buf, CCUINT32 size)
{
    char ip[64];
    CCINT32 n;

    if (!addr || !buf || size == 0)
        return CC_NET_ERR_PARAM;

    if (addr->family == CC_NET_AF_INET)
    {
        if (!inet_ntop(AF_INET, addr->addr, ip, (CCNetSockLen)sizeof(ip)))
            return CC_NET_ERR_PARAM;
        n = snprintf(buf, (size_t)size, "%s:%u", ip, (unsigned)addr->port);
    }
    else if (addr->family == CC_NET_AF_INET6)
    {
        if (!inet_ntop(AF_INET6, addr->addr, ip, (CCNetSockLen)sizeof(ip)))
            return CC_NET_ERR_PARAM;
        if (addr->scope != 0)
            n = snprintf(
                buf, (size_t)size, "[%s%%%u]:%u", ip,
                (unsigned)addr->scope, (unsigned)addr->port
            );
        else
            n = snprintf(buf, (size_t)size, "[%s]:%u", ip, (unsigned)addr->port);
    }
    else
    {
        return CC_NET_ERR_PARAM;
    }

    if (n < 0 || (CCUINT32)n >= size)
        return CC_NET_ERR_PARAM;
    return CC_NET_OK;
}

CCINT32 ccnet_dns(const char *host, CCUINT16 port, CCNetAddr *out, CCUINT32 cap)
{
    CCNetAddr literal;
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *cur;
    CCINT32 count = 0;
    int rc;     // getaddrinfo 的返回类型：Winsock 与 POSIX 均为 int

    if (!host || !*host || !out || cap == 0)
        return CC_NET_ERR_PARAM;
    if (cap > 0x7FFFFFFFu)
        return CC_NET_ERR_PARAM;

    rc = ccnet_addr_parse(host, port, &literal);
    if (rc == CC_NET_OK)
    {
        out[0] = literal;
        return 1;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0)
        return _ccnet_map_gai(rc);

    for (cur = res; cur && count < (CCINT32)cap; cur = cur->ai_next)
    {
        CCNetAddr item;
        if (cur->ai_family != AF_INET && cur->ai_family != AF_INET6)
            continue;
        _ccnet_addr_from_sock(&item, cur->ai_addr, (CCNetSockLen)cur->ai_addrlen);
        item.port = port;
        out[count++] = item;
    }

    // addrinfo 链表由 getaddrinfo 分配，只能用 freeaddrinfo 释放
    freeaddrinfo(res);
    if (count == 0)
        return CC_NET_ERR_DNS;
    return count;
}
