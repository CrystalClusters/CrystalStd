/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 网络内核公共部分：善后线程、启动同步、上下文生命周期。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "ccnet_internal.h"

static CCBOOL _ccnet_net_ready = CCFALSE;

void init_ccnet(void)
{
    if (_ccnet_net_ready)
        return;

#ifdef CC_WINDOWS
    WSADATA wsa;
    _ccnet_net_ready = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? CCTRUE : CCFALSE;
#else
    _ccnet_net_ready = CCTRUE;
#endif
}

void deinit_ccnet(void)
{
#ifdef CC_WINDOWS
    if (_ccnet_net_ready)
        WSACleanup();
#endif
    _ccnet_net_ready = CCFALSE;
}

CCBOOL _ccnet_ready(void)
{
    return _ccnet_net_ready;
}

void _ccnet_finish(CCNetStart *start, CCINT32 result)
{
    if (!start || !start->lock)
        return;

    cc_lock(start->lock);
    start->result = result;
    start->done = CCTRUE;
    cc_unlock(start->lock);
}

CCINT32 _ccnet_spawn(CCNetStart *start, CCThreadEntry wrapper)
{
    if (!start || !wrapper)
        return CC_NET_ERR_PARAM;
    if (!_ccnet_ready())
        return CC_NET_ERR_IO;

    start->lock = cc_create_lock();
    if (!start->lock)
        return CC_NET_ERR_RESOURCE;

    start->done = CCFALSE;
    start->result = CC_NET_ERR_IO;

    if (!cc_thread_create(wrapper, start))
    {
        cc_destroy_lock(start->lock);
        start->lock = NULL;
        return CC_NET_ERR_RESOURCE;
    }

    for (;;)
    {
        cc_lock(start->lock);
        CCBOOL done = start->done;
        cc_unlock(start->lock);
        if (done)
            break;
        ccsleep(1000);
    }

    CCINT32 result = start->result;
    cc_destroy_lock(start->lock);
    start->lock = NULL;
    return result;
}

void _ccnet_ctx_cleanup(CCNetCtx *ctx)
{
    if (!ctx)
        return;

    if (ctx->sock != CC_NET_SOCK_INVALID)
    {
        _ccnet_sock_close(ctx->sock);
        ctx->sock = CC_NET_SOCK_INVALID;
    }
}

void _ccnet_tcp_wrapper(void *arg, CCThread idThis)
{
    CCNetStart *start = arg;
    CCBOOL heap = start->heap;
    CCNetCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = CC_NET_KIND_TCP;
    ctx.sock = CC_NET_SOCK_INVALID;
    ctx.thread = idThis;
    ctx.tcp_entry = start->tcp_entry;
    ctx.user = start->user;
    atomic_init(&ctx.state, CC_NET_STATE_OPEN);

    CCINT32 result = _ccnet_tcp_setup(start, &ctx);

    _ccnet_finish(start, result);

    if (result == CC_NET_OK && ctx.tcp_entry)
        ctx.tcp_entry(&_ccnet_tcp_ops, &ctx, ctx.user);

    _ccnet_ctx_cleanup(&ctx);

    if (heap)
        ccalloc(start, 0, NULL);
}

void _ccnet_listen_wrapper(void *arg, CCThread idThis)
{
    CCNetStart *start = arg;
    CCBOOL heap = start->heap;
    CCNetCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = CC_NET_KIND_LISTENER;
    ctx.sock = CC_NET_SOCK_INVALID;
    ctx.thread = idThis;
    ctx.accept_entry = start->accept_entry;
    ctx.user = start->user;
    atomic_init(&ctx.state, CC_NET_STATE_OPEN);

    CCINT32 result = _ccnet_listen_setup(start, &ctx);

    _ccnet_finish(start, result);

    if (result == CC_NET_OK && ctx.accept_entry)
        ctx.accept_entry(&_ccnet_accept_ops, &ctx, ctx.user);

    _ccnet_ctx_cleanup(&ctx);

    if (heap)
        ccalloc(start, 0, NULL);
}

void _ccnet_udp_wrapper(void *arg, CCThread idThis)
{
    CCNetStart *start = arg;
    CCBOOL heap = start->heap;
    CCNetCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = CC_NET_KIND_UDP;
    ctx.sock = CC_NET_SOCK_INVALID;
    ctx.thread = idThis;
    ctx.udp_entry = start->udp_entry;
    ctx.user = start->user;
    atomic_init(&ctx.state, CC_NET_STATE_OPEN);

    CCINT32 result = _ccnet_udp_setup(start, &ctx);

    _ccnet_finish(start, result);

    if (result == CC_NET_OK && ctx.udp_entry)
        ctx.udp_entry(&_ccnet_udp_ops, &ctx, ctx.user);

    _ccnet_ctx_cleanup(&ctx);

    if (heap)
        ccalloc(start, 0, NULL);
}
