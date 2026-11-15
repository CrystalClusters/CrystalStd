/*
 * @Description: ccnet 服务端交互演示。
 *               监听 -> 多客户端接入 -> 把每个客户端的数据转发给其他客户端。
 *               每个连接一个读入口线程 + 一个写线程，写线程从有界发送队列取帧。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "../common/net_common.h"

#define SRV_MAX_CLIENTS 32
#define SRV_QUEUE_CAP   8
#define SRV_EVENT_CAP   64
#define SRV_LINE_MAX    4096

typedef enum srv_slot_state {
    SLOT_FREE = 0,
    SLOT_ACTIVE,
    SLOT_CLOSING,
} SrvSlotState;

typedef struct srv_frame {
    CCUINT32 len;
    char data[NET_FRAME_CAP];
} SrvFrame;

typedef struct srv_slot {
    SrvSlotState state;                 /* g_srv.lock 保护 */
    CCINT32 id;
    struct srv_state *srv;
    const CCNetTcpOps *ops;             /* 仅本连接读线程与写线程使用 */
    void *ctx;
    CCThread writer;
    _Atomic CCINT32 stop;

    CCLock qlock;                       /* 保护发送队列 */
    SrvFrame queue[SRV_QUEUE_CAP];
    CCUINT32 qhead;
    CCUINT32 qcount;

    char peer[96];
    CCINT64 connect_us;
    _Atomic CCINT64 last_active_us;
    _Atomic CCUINT64 rx_bytes;
    _Atomic CCUINT64 tx_bytes;
    _Atomic CCUINT32 rx_frames;
    _Atomic CCUINT32 tx_frames;
    _Atomic CCUINT32 tx_dropped;
} SrvSlot;

typedef struct srv_state {
    CCLock lock;
    _Atomic CCINT32 accept_stop;
    _Atomic CCINT32 shutdown;
    _Atomic CCINT32 listening;
    _Atomic CCINT32 listener_exited;
    _Atomic CCINT32 port_ready;
    CCUINT16 port;
    char listen_text[96];
    char bind_text[96];
    CCINT32 next_id;

    SrvSlot slots[SRV_MAX_CLIENTS];
    _Atomic CCINT32 online;
    _Atomic CCINT32 peak;
    _Atomic CCUINT32 accepted;
    _Atomic CCUINT32 rejected;
    _Atomic CCUINT64 rx_bytes;
    _Atomic CCUINT64 rx_frames;
    _Atomic CCUINT64 tx_bytes;
    _Atomic CCUINT64 tx_frames;
    _Atomic CCUINT64 tx_dropped;
    _Atomic CCUINT64 spawned;
    _Atomic CCUINT64 finished;

    NetRingEntry event_slots[SRV_EVENT_CAP];
    NetRing events;
    _Atomic CCINT32 watch_on;
    _Atomic CCINT32 watch_ms;
    CCINT64 start_us;
} Srv;

static Srv g_srv;
static CCThread g_watch_thread = 0;
static _Atomic CCINT32 g_watch_stop = 0;
static char g_prompt[160] = "srv> ";

/* ------------------------------------------------------------------ 工具 */

static void srv_enqueue(SrvSlot *slot, const char *data, CCUINT32 len)
{
    /* 调用方持 g_srv.lock，保证槽位不会在状态检查与入队之间复用 */
    cc_lock(slot->qlock);
    if (slot->qcount == SRV_QUEUE_CAP)
    {
        atomic_fetch_add(&slot->tx_dropped, 1);
        atomic_fetch_add(&g_srv.tx_dropped, 1);
    }
    else
    {
        SrvFrame *f = &slot->queue[(slot->qhead + slot->qcount) % SRV_QUEUE_CAP];
        f->len = len;
        memcpy(f->data, data, len);
        slot->qcount++;
    }
    cc_unlock(slot->qlock);
}

static void srv_broadcast(CCINT32 from_id, const char *data, CCUINT32 len)
{
    CCINT32 i;

    cc_lock(g_srv.lock);
    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        SrvSlot *slot = &g_srv.slots[i];
        if (slot->state != SLOT_ACTIVE || slot->id == from_id)
            continue;
        srv_enqueue(slot, data, len);
    }
    cc_unlock(g_srv.lock);
}

static CCUINT32 srv_make_frame(char *out, CCUINT32 cap, CCINT32 id,
                               const char *peer, const char *line, CCUINT32 len,
                               CCBOOL truncated)
{
    int n = snprintf(out, cap, "<#%d %s> ", id, peer);
    CCUINT32 used;

    if (n < 0 || (CCUINT32)n >= cap)
        return 0;
    used = (CCUINT32)n;
    if (len > cap - used - 5u)
        len = cap - used - 5u;
    memcpy(out + used, line, len);
    used += len;
    if (truncated)
    {
        memcpy(out + used, "...", 3);
        used += 3;
    }
    out[used++] = '\n';
    out[used] = '\0';
    return used;
}

/* ------------------------------------------------------- 连接读 / 写线程 */

static void srv_on_line(void *user, const char *line, CCUINT32 len, CCBOOL truncated)
{
    SrvSlot *slot = (SrvSlot *)user;
    char frame[NET_FRAME_CAP];
    CCUINT32 flen;

    if (len == 0)
        return;
    flen = srv_make_frame(frame, sizeof(frame), slot->id, slot->peer,
                          line, len, truncated);
    if (flen == 0)
        return;

    atomic_fetch_add(&slot->rx_frames, 1);
    atomic_fetch_add(&g_srv.rx_frames, 1);
    atomic_store(&slot->last_active_us, net_now_us());
    srv_broadcast(slot->id, frame, flen);
}

static void srv_writer(void *data, CCThread id)
{
    SrvSlot *slot = (SrvSlot *)data;
    (void)id;

    for (;;)
    {
        SrvFrame frame;
        CCINT64 w;

        cc_lock(slot->qlock);
        if (atomic_load(&slot->stop))
        {
            atomic_fetch_add(&slot->tx_dropped, slot->qcount);
            atomic_fetch_add(&g_srv.tx_dropped, slot->qcount);
            slot->qcount = 0;
            cc_unlock(slot->qlock);
            break;
        }
        if (slot->qcount == 0)
        {
            cc_unlock(slot->qlock);
            ccsleep(2000);
            continue;
        }
        frame = slot->queue[slot->qhead];
        slot->qhead = (slot->qhead + 1u) % SRV_QUEUE_CAP;
        slot->qcount--;
        cc_unlock(slot->qlock);

        w = net_send_all(slot->ops, slot->ctx, frame.data, frame.len, 500000);
        if (w != (CCINT64)frame.len)
        {
            if (w >= 0)
                net_event_push(&g_srv.events, NET_EV_ERROR, "客户端 #%d 发送未完成（%ld/%u 字节）",
                          slot->id, w, frame.len);
            else
                net_event_push(&g_srv.events, NET_EV_ERROR, "客户端 #%d 发送失败：%s",
                          slot->id, net_err_name((CCINT32)w));
            atomic_store(&slot->stop, 1);
            cc_lock(slot->qlock);
            atomic_fetch_add(&slot->tx_dropped, slot->qcount);
            atomic_fetch_add(&g_srv.tx_dropped, slot->qcount);
            slot->qcount = 0;
            cc_unlock(slot->qlock);
            break;
        }

        atomic_fetch_add(&slot->tx_frames, 1);
        atomic_fetch_add(&slot->tx_bytes, frame.len);
        atomic_fetch_add(&g_srv.tx_frames, 1);
        atomic_fetch_add(&g_srv.tx_bytes, frame.len);
        atomic_store(&slot->last_active_us, net_now_us());
    }
}

static SrvSlot *srv_register(const CCNetTcpOps *ops, void *ctx, const char *peer)
{
    SrvSlot *slot = NULL;
    CCINT32 i;
    CCINT32 online;
    CCINT32 peak;

    cc_lock(g_srv.lock);
    if (atomic_load(&g_srv.shutdown))
    {
        cc_unlock(g_srv.lock);
        return NULL;
    }

    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        if (g_srv.slots[i].state == SLOT_FREE)
        {
            slot = &g_srv.slots[i];
            break;
        }
    }
    if (!slot)
    {
        atomic_fetch_add(&g_srv.rejected, 1);
        cc_unlock(g_srv.lock);
        return NULL;
    }

    slot->state = SLOT_ACTIVE;
    slot->srv = &g_srv;
    slot->ops = ops;
    slot->ctx = ctx;
    slot->id = g_srv.next_id++;
    net_copy_text(slot->peer, sizeof(slot->peer), peer);
    atomic_store(&slot->stop, 0);
    atomic_store(&slot->last_active_us, 0);
    atomic_store(&slot->rx_bytes, 0);
    atomic_store(&slot->tx_bytes, 0);
    atomic_store(&slot->rx_frames, 0);
    atomic_store(&slot->tx_frames, 0);
    atomic_store(&slot->tx_dropped, 0);
    slot->connect_us = net_now_us();
    atomic_store(&slot->last_active_us, slot->connect_us);
    slot->writer = 0;

    cc_lock(slot->qlock);
    slot->qhead = 0;
    slot->qcount = 0;
    cc_unlock(slot->qlock);

    online = atomic_fetch_add(&g_srv.online, 1) + 1;
    peak = atomic_load(&g_srv.peak);
    while (online > peak &&
           !atomic_compare_exchange_weak(&g_srv.peak, &peak, online))
    {
    }
    atomic_fetch_add(&g_srv.accepted, 1);
    cc_unlock(g_srv.lock);

    net_event_push(&g_srv.events, NET_EV_EVENT, "客户端 #%d 已连接 <%s>，在线 %d",
              slot->id, slot->peer, atomic_load(&g_srv.online));
    return slot;
}

static void srv_unregister(SrvSlot *slot, const char *reason)
{
    cc_lock(g_srv.lock);
    if (slot->state != SLOT_FREE)
    {
        slot->state = SLOT_CLOSING;
        atomic_fetch_sub(&g_srv.online, 1);
    }
    cc_unlock(g_srv.lock);

    atomic_store(&slot->stop, 1);
    if (slot->writer)
    {
        cc_thread_wait(slot->writer, 0);
        slot->writer = 0;
    }

    net_event_push(&g_srv.events, NET_EV_EVENT, "客户端 #%d 断开 <%s>：%s，在线 %d",
              slot->id, slot->peer, reason ? reason : "结束",
              atomic_load(&g_srv.online));

    cc_lock(g_srv.lock);
    slot->state = SLOT_FREE;
    cc_unlock(g_srv.lock);
}

static void srv_conn_entry(const CCNetTcpOps *ops, void *ctx, void *user)
{
    Srv *s = (Srv *)user;
    SrvSlot *slot;
    CCNetAddr peer_addr;
    char peer[96] = "?";
    char reason[64] = "结束";
    char buf[2048];
    NetLine line;
    CCThread writer;

    if (atomic_load(&s->shutdown))
    {
        atomic_fetch_add(&s->finished, 1);
        return;
    }

    if (ops->peer(ctx, &peer_addr) == CC_NET_OK)
        (void)ccnet_addr_text(&peer_addr, peer, sizeof(peer));

    slot = srv_register(ops, ctx, peer);
    if (!slot)
    {
        atomic_fetch_add(&s->finished, 1);
        return;
    }

    writer = cc_thread_create(srv_writer, slot);
    if (!writer)
    {
        net_event_push(&g_srv.events, NET_EV_ERROR, "客户端 #%d 写线程创建失败", slot->id);
        srv_unregister(slot, "写线程创建失败");
        atomic_fetch_add(&s->finished, 1);
        return;
    }
    slot->writer = writer;

    net_line_init(&line);
    for (;;)
    {
        CCINT64 n;

        if (atomic_load(&s->shutdown) || atomic_load(&slot->stop))
        {
            net_copy_text(reason, sizeof(reason),
                      atomic_load(&s->shutdown) ? "服务端退出" : "连接被关闭");
            break;
        }

        n = ops->recv(ctx, buf, sizeof(buf), 200000);
        if (n == CC_NET_ERR_TIMEOUT)
            continue;
        if (n == 0)
        {
            net_copy_text(reason, sizeof(reason), "对端关闭");
            break;
        }
        if (n < 0)
        {
            net_copy_text(reason, sizeof(reason), net_err_name((CCINT32)n));
            break;
        }

        atomic_fetch_add(&slot->rx_bytes, (CCUINT64)n);
        atomic_fetch_add(&s->rx_bytes, (CCUINT64)n);
        atomic_store(&slot->last_active_us, net_now_us());
        net_line_feed(&line, buf, (CCUINT32)n, srv_on_line, slot);
    }

    srv_unregister(slot, reason);
    atomic_fetch_add(&s->finished, 1);
}

static void srv_accept_entry(const CCNetAcceptOps *ops, void *ctx, void *user)
{
    Srv *s = (Srv *)user;
    CCNetAddr local;

    if (ops->local(ctx, &local) == CC_NET_OK)
    {
        (void)ccnet_addr_text(&local, s->listen_text, sizeof(s->listen_text));
        s->port = local.port;
        atomic_store(&s->port_ready, 1);
        atomic_store(&s->listening, 1);
    }

    net_event_push(&g_srv.events, NET_EV_EVENT, "开始监听 %s", s->listen_text);

    while (!atomic_load(&s->accept_stop))
    {
        CCINT32 rc = ops->accept(ctx, srv_conn_entry, s, 200000);
        if (rc == CC_NET_ERR_TIMEOUT)
            continue;
        if (rc == 0)
        {
            atomic_fetch_add(&s->spawned, 1);
            continue;
        }
        if (rc == CC_NET_ERR_RESOURCE)
        {
            net_event_push(&g_srv.events, NET_EV_ERROR, "接入资源不足：%s", net_err_name(rc));
            ccsleep(10000);
            continue;
        }
        net_event_push(&g_srv.events, NET_EV_ERROR, "accept 失败：%s", net_err_name(rc));
        break;
    }

    atomic_store(&s->listening, 0);
    atomic_store(&s->listener_exited, 1);
    net_event_push(&g_srv.events, NET_EV_EVENT, "监听结束");
}

/* ------------------------------------------------------------ 状态与命令 */

typedef struct srv_snap {
    CCINT32 listening;
    char listen_text[96];
    CCINT32 online;
    CCINT32 peak;
    CCUINT32 accepted;
    CCUINT32 rejected;
    CCUINT64 rx_bytes;
    CCUINT64 rx_frames;
    CCUINT64 tx_bytes;
    CCUINT64 tx_frames;
    CCUINT64 tx_dropped;
    CCUINT32 event_unread;
    CCUINT32 event_dropped;
    CCINT64 uptime_us;
} SrvSnap;

static void srv_snapshot(SrvSnap *snap)
{
    memset(snap, 0, sizeof(*snap));
    cc_lock(g_srv.lock);
    snap->listening = atomic_load(&g_srv.listening);
    net_copy_text(snap->listen_text, sizeof(snap->listen_text), g_srv.listen_text);
    snap->online = atomic_load(&g_srv.online);
    snap->peak = atomic_load(&g_srv.peak);
    snap->accepted = atomic_load(&g_srv.accepted);
    snap->rejected = atomic_load(&g_srv.rejected);
    snap->rx_bytes = atomic_load(&g_srv.rx_bytes);
    snap->rx_frames = atomic_load(&g_srv.rx_frames);
    snap->tx_bytes = atomic_load(&g_srv.tx_bytes);
    snap->tx_frames = atomic_load(&g_srv.tx_frames);
    snap->tx_dropped = atomic_load(&g_srv.tx_dropped);
    cc_unlock(g_srv.lock);
    snap->event_unread = net_ring_unread(&g_srv.events);
    snap->event_dropped = net_ring_dropped(&g_srv.events);
    snap->uptime_us = net_now_us() - g_srv.start_us;
}

static void srv_print_status(void)
{
    SrvSnap s;
    char up[32];

    srv_snapshot(&s);
    net_fmt_duration(s.uptime_us, up, sizeof(up));
    color_print(NET_C_TITLE "[状态] 运行 %s\n", up);
    if (s.listening)
        color_print(NET_C_INFO "  监听: %s\n", s.listen_text);
    else
        color_print(NET_C_WARN "  监听: 未监听（已有连接保持）\n");
    color_print("  在线: %d / 峰值 %d / 累计 %u / 拒绝 %u\n",
                s.online, s.peak, s.accepted, s.rejected);
    color_print("  接收: %lu 行 / %lu 字节\n", s.rx_frames, s.rx_bytes);
    color_print("  转发: %lu 行 / %lu 字节 / 丢弃 %lu\n",
                s.tx_frames, s.tx_bytes, s.tx_dropped);
    color_print("  事件: 未读 %u / 累计丢弃 %u\n", s.event_unread, s.event_dropped);
}

typedef struct srv_row {
    CCINT32 id;
    CCINT32 state;
    char peer[96];
    CCUINT64 rx_bytes;
    CCUINT64 tx_bytes;
    CCUINT32 rx_frames;
    CCUINT32 tx_frames;
    CCUINT32 tx_dropped;
    CCINT64 connect_us;
    CCINT64 last_active_us;
} SrvRow;

static void srv_print_clients(void)
{
    SrvRow rows[SRV_MAX_CLIENTS];
    CCUINT32 count = 0;
    CCINT32 i;
    CCINT64 now = net_now_us();

    cc_lock(g_srv.lock);
    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        SrvSlot *slot = &g_srv.slots[i];
        if (slot->state == SLOT_FREE)
            continue;
        rows[count].id = slot->id;
        rows[count].state = (CCINT32)slot->state;
        net_copy_text(rows[count].peer, sizeof(rows[count].peer), slot->peer);
        rows[count].rx_bytes = atomic_load(&slot->rx_bytes);
        rows[count].tx_bytes = atomic_load(&slot->tx_bytes);
        rows[count].rx_frames = atomic_load(&slot->rx_frames);
        rows[count].tx_frames = atomic_load(&slot->tx_frames);
        rows[count].tx_dropped = atomic_load(&slot->tx_dropped);
        rows[count].connect_us = slot->connect_us;
        rows[count].last_active_us = atomic_load(&slot->last_active_us);
        count++;
    }
    cc_unlock(g_srv.lock);

    if (count == 0)
    {
        color_print(NET_C_DIM "  (无客户端)\n");
        return;
    }

    color_print(NET_C_TITLE "  %-4s %-22s %-8s %-10s %-12s %-12s %-8s\n",
                "ID", "对端", "状态", "在线", "收(行/字节)", "发(行/字节)", "丢弃");
    for (i = 0; i < (CCINT32)count; i++)
    {
        char conn[32];
        char rx[48];
        char tx[48];
        net_fmt_duration(rows[i].connect_us > 0 ? now - rows[i].connect_us : 0,
                         conn, sizeof(conn));
        snprintf(rx, sizeof(rx), "%u/%llu", rows[i].rx_frames,
                 (unsigned long long)rows[i].rx_bytes);
        snprintf(tx, sizeof(tx), "%u/%llu", rows[i].tx_frames,
                 (unsigned long long)rows[i].tx_bytes);
        color_print("  #%-3d %-22s %-8s %-10s %-12s %-12s %-8u\n",
                    rows[i].id, rows[i].peer,
                    rows[i].state == SLOT_ACTIVE ? "ACTIVE" : "CLOSING",
                    conn, rx, tx, rows[i].tx_dropped);
    }
}

static void srv_print_help(void)
{
    color_print(NET_C_TITLE "服务端命令（<> 必填，[] 可选）：\n" NET_C_RESET
        "  listen <port> [bindAddr]   开始监听；port=0 临时端口；默认 0.0.0.0\n"
        "  stop                       停止接受新连接，已有客户端保持\n"
        "  status                     实时状态快照\n"
        "  clients                    当前连接表\n"
        "  say <text>                 向所有客户端广播\n"
        "  kick <id>                  断开指定客户端\n"
        "  refresh [n]                显示事件环形缓冲区的最新未读\n"
        "  watch [ms|on|off]          实时状态行开关/间隔\n"
        "  clear                      丢弃未读事件\n"
        "  help / -h                  帮助\n"
        "  quit / exit                关闭全部连接并退出\n");
}

static void srv_prompt(void)
{
    CCINT32 online = atomic_load(&g_srv.online);
    CCUINT32 unread = net_ring_unread(&g_srv.events);
    snprintf(g_prompt, sizeof(g_prompt), "srv[on=%d u=%u]> ", online, unread);
    net_console_enter();
    color_print(NET_C_INFO "%s" NET_C_RESET, g_prompt);
    fflush(stdout);
    net_console_leave();
}

static void cmd_listen(const char *args)
{
    char port_text[32];
    char bind_text[64];
    CCUINT32 port;
    CCNetAddr addr;
    CCINT32 rc;
    CCINT32 i;

    if (!args || sscanf(args, "%31s %63s", port_text, bind_text) < 1)
    {
        color_print(NET_C_WARN "用法: listen <port> [bindAddr]\n");
        return;
    }
    if (!net_parse_u32(port_text, &port) || port > 65535u)
    {
        color_print(NET_C_ERR "端口非法: %s\n", port_text);
        return;
    }
    if (sscanf(args, "%31s %63s", port_text, bind_text) < 2)
        net_copy_text(bind_text, sizeof(bind_text), "0.0.0.0");

    if (atomic_load(&g_srv.listening))
    {
        color_print(NET_C_WARN "已在监听 %s，先 stop\n", g_srv.listen_text);
        return;
    }
    if (!atomic_load(&g_srv.listener_exited))
    {
        color_print(NET_C_WARN "上一个监听器尚未退出，请稍候\n");
        return;
    }

    rc = ccnet_addr_parse(bind_text, (CCUINT16)port, &addr);
    if (rc != CC_NET_OK)
    {
        color_print(NET_C_ERR "绑定地址非法（必须是 IP 字面量）: %s（%s）\n",
                    bind_text, net_err_name(rc));
        return;
    }

    net_copy_text(g_srv.bind_text, sizeof(g_srv.bind_text), bind_text);
    atomic_store(&g_srv.accept_stop, 0);
    atomic_store(&g_srv.listener_exited, 0);
    atomic_store(&g_srv.port_ready, 0);
    atomic_store(&g_srv.listening, 0);
    g_srv.port = 0;
    g_srv.listen_text[0] = '\0';

    rc = ccnet_tcp_listen(&addr, srv_accept_entry, &g_srv);
    if (rc != CC_NET_OK)
    {
        atomic_store(&g_srv.listener_exited, 1);
        color_print(NET_C_ERR "监听失败: %s (%d)\n", net_err_name(rc), (int)rc);
        return;
    }

    for (i = 0; i < 2000; i++)
    {
        if (atomic_load(&g_srv.port_ready) || atomic_load(&g_srv.listener_exited))
            break;
        ccsleep(1000);
    }

    if (atomic_load(&g_srv.port_ready))
        color_print(NET_C_OK "[完成] 正在监听 %s\n", g_srv.listen_text);
    else
        color_print(NET_C_ERR "[失败] 监听线程未就绪\n");
}

static void cmd_stop(void)
{
    CCINT32 i;

    if (!atomic_load(&g_srv.listening) && atomic_load(&g_srv.listener_exited))
    {
        color_print(NET_C_DIM "当前未监听\n");
        return;
    }
    atomic_store(&g_srv.accept_stop, 1);
    for (i = 0; i < 3000 && !atomic_load(&g_srv.listener_exited); i++)
        ccsleep(1000);
    atomic_store(&g_srv.accept_stop, 0);
    if (atomic_load(&g_srv.listener_exited))
        color_print(NET_C_OK "[完成] 已停止监听（已有客户端保持）\n");
    else
        color_print(NET_C_ERR "[失败] 监听器未在 3 秒内退出\n");
}

static void cmd_say(const char *args)
{
    char text[NET_LINE_CAP];
    char frame[NET_FRAME_CAP];
    int n;

    if (!args || !*args)
    {
        color_print(NET_C_WARN "用法: say <text>\n");
        return;
    }
    if (strlen(args) > 1023u)
    {
        color_print(NET_C_ERR "消息过长（上限 1023 字节）\n");
        return;
    }
    net_copy_text(text, sizeof(text), args);
    n = snprintf(frame, sizeof(frame), "<* server> %s\n", text);
    if (n < 0)
        return;
    srv_broadcast(0, frame, (CCUINT32)n);
    color_print(NET_C_OK "[完成] 已广播 %u 字节\n", (CCUINT32)n);
}

static void cmd_kick(const char *args)
{
    CCUINT32 id;
    CCINT32 i;
    SrvSlot *found = NULL;

    if (!args || !net_parse_u32(args, &id))
    {
        color_print(NET_C_WARN "用法: kick <id>\n");
        return;
    }

    cc_lock(g_srv.lock);
    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        SrvSlot *slot = &g_srv.slots[i];
        if (slot->state == SLOT_ACTIVE && slot->id == (CCINT32)id)
        {
            found = slot;
            slot->state = SLOT_CLOSING;
            break;
        }
    }
    cc_unlock(g_srv.lock);

    if (!found)
    {
        color_print(NET_C_ERR "没有在线客户端 #%u\n", id);
        return;
    }
    atomic_store(&found->stop, 1);
    color_print(NET_C_OK "[完成] 已请求断开 #%u\n", id);
}

static void cmd_refresh(const char *args)
{
    CCUINT32 total;
    CCUINT32 limit = 0;
    CCUINT32 skip;
    CCUINT32 shown = 0;

    if (args && *args && !net_parse_u32(args, &limit))
    {
        color_print(NET_C_WARN "用法: refresh [n]\n");
        return;
    }
    total = net_ring_unread(&g_srv.events);
    if (total == 0)
    {
        color_print(NET_C_DIM "[刷新] 无未读事件（累计丢弃 %u）\n",
                    net_ring_dropped(&g_srv.events));
        return;
    }
    if (limit > 0 && limit < total)
        skip = total - limit;
    else
        skip = 0;
    total = net_ring_unread(&g_srv.events);

    color_print(NET_C_TITLE "[刷新] 未读 %u 条，丢弃 %u 条\n",
                total, net_ring_dropped(&g_srv.events));
    if (skip > 0)
    {
        net_ring_skip(&g_srv.events, skip);
        color_print(NET_C_DIM "  （跳过更早的 %u 条）\n", skip);
    }

    for (;;)
    {
        NetRingEntry batch[16];
        CCUINT32 n = net_ring_pop(&g_srv.events, batch, 16);
        CCUINT32 i;
        if (n == 0)
            break;
        for (i = 0; i < n; i++)
        {
            char clock[16];
            net_fmt_time(batch[i].wall_sec, clock, sizeof(clock));
            if (batch[i].kind == NET_EV_ERROR)
                color_print(NET_C_ERR "#%06u %s [错误] %s\n",
                            batch[i].seq, clock, batch[i].text);
            else if (batch[i].kind == NET_EV_EVENT)
                color_print(NET_C_EVENT "#%06u %s [事件] %s\n",
                            batch[i].seq, clock, batch[i].text);
            else
                color_print(NET_C_INFO "#%06u %s %s\n",
                            batch[i].seq, clock, batch[i].text);
            shown++;
        }
    }
    color_print(NET_C_DIM "[刷新] 共显示 %u 条\n", shown);
}

static void cmd_watch(const char *args)
{
    CCUINT32 ms;

    if (!args || !*args)
    {
        color_print(NET_C_INFO "watch: %s，间隔 %d ms\n",
                    atomic_load(&g_srv.watch_on) ? "on" : "off",
                    atomic_load(&g_srv.watch_ms));
        return;
    }
    if (strcmp(args, "off") == 0)
    {
        atomic_store(&g_srv.watch_on, 0);
        color_print(NET_C_DIM "实时状态已关闭\n");
    }
    else if (strcmp(args, "on") == 0)
    {
        atomic_store(&g_srv.watch_on, 1);
        color_print(NET_C_OK "实时状态已开启（%d ms）\n",
                    atomic_load(&g_srv.watch_ms));
    }
    else if (net_parse_u32(args, &ms) && ms >= 200u && ms <= 60000u)
    {
        atomic_store(&g_srv.watch_ms, (CCINT32)ms);
        atomic_store(&g_srv.watch_on, 1);
        color_print(NET_C_OK "实时状态间隔设为 %u ms\n", ms);
    }
    else
    {
        color_print(NET_C_WARN "用法: watch [ms|on|off]（200..60000）\n");
    }
}

static void cmd_clear(void)
{
    CCUINT32 n = net_ring_clear(&g_srv.events);
    color_print(NET_C_DIM "已丢弃 %u 条未读事件\n", n);
}

/* --------------------------------------------------------------- 实时状态 */

static void srv_watch_entry(void *data, CCThread id)
{
    Srv *s = (Srv *)data;
    CCUINT32 last_online = 0xFFFFFFFFu;
    CCUINT64 last_rx = 0;
    CCUINT64 last_tx = 0;
    CCUINT64 last_drop = 0;
    CCINT64 next = net_now_us();
    CCUINT32 online;
    CCUINT64 rx;
    CCUINT64 tx;
    CCUINT64 drop;
    char up[32];

    (void)id;
    while (!atomic_load(&g_watch_stop))
    {
        CCINT32 i;
        for (i = 0; i < 20 && !atomic_load(&g_watch_stop); i++)
            ccsleep(1000);
        if (atomic_load(&g_watch_stop))
            break;
        if (net_now_us() < next)
            continue;
        next = net_now_us() + (CCINT64)atomic_load(&s->watch_ms) * 1000;

        if (!atomic_load(&s->watch_on) || !net_console_tty())
            continue;
        if (!atomic_load(&s->listening))
            continue;

        online = (CCUINT32)atomic_load(&s->online);
        rx = atomic_load(&s->rx_frames);
        tx = atomic_load(&s->tx_frames);
        drop = atomic_load(&s->tx_dropped);

        if (online == last_online && rx == last_rx && tx == last_tx &&
            drop == last_drop)
            continue;
        last_online = online;
        last_rx = rx;
        last_tx = tx;
        last_drop = drop;

        if (!net_console_try_enter())
            continue;
        net_fmt_duration(net_now_us() - s->start_us, up, sizeof(up));
        color_print(NET_C_DIM "\n[状态 %s] " NET_C_RESET
                    "在线 %u | 收 %lu 行 | 转发 %lu 行 | 丢 %lu | 事件未读 %u\n",
                    up, online, rx, tx, drop,
                    net_ring_unread(&s->events));
        color_print(NET_C_INFO "%s" NET_C_RESET, g_prompt);
        fflush(stdout);
        net_console_leave();
    }
}

/* ------------------------------------------------------------------ 退出 */

static void srv_shutdown(void)
{
    CCINT32 i;

    atomic_store(&g_srv.shutdown, 1);
    atomic_store(&g_srv.accept_stop, 1);

    cc_lock(g_srv.lock);
    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        if (g_srv.slots[i].state != SLOT_FREE)
        {
            g_srv.slots[i].state = SLOT_CLOSING;
            atomic_store(&g_srv.slots[i].stop, 1);
        }
    }
    cc_unlock(g_srv.lock);

    for (i = 0;; i++)
    {
        if (atomic_load(&g_srv.listener_exited))
            break;
        ccsleep(1000);
        if (i == 3000)
            color_print(NET_C_WARN "[提示] 监听线程仍在退出，继续等待...\n");
    }

    for (i = 0;; i++)
    {
        if (atomic_load(&g_srv.finished) >= atomic_load(&g_srv.spawned) &&
            atomic_load(&g_srv.online) == 0)
            break;
        ccsleep(1000);
        if (i == 10000)
            color_print(NET_C_WARN "[提示] 仍有连接线程未退出，继续等待...\n");
    }

    atomic_store(&g_watch_stop, 1);
    if (g_watch_thread)
    {
        cc_thread_wait(g_watch_thread, 0);
        g_watch_thread = 0;
    }

    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        if (g_srv.slots[i].qlock)
        {
            cc_destroy_lock(g_srv.slots[i].qlock);
            g_srv.slots[i].qlock = NULL;
        }
    }
    net_ring_destroy(&g_srv.events);
    if (g_srv.lock)
    {
        cc_destroy_lock(g_srv.lock);
        g_srv.lock = NULL;
    }
}

/* ---------------------------------------------------------------- 命令分发 */

static CCBOOL srv_dispatch(char *line)
{
    char cmd[32] = "";
    const char *args = NULL;

    if (!net_split_command(line, cmd, sizeof(cmd), &args))
        return CCTRUE;

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0)
        srv_print_help();
    else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 ||
             strcmp(cmd, "q") == 0)
        return CCFALSE;
    else if (strcmp(cmd, "listen") == 0)
        cmd_listen(args);
    else if (strcmp(cmd, "stop") == 0)
        cmd_stop();
    else if (strcmp(cmd, "status") == 0)
        srv_print_status();
    else if (strcmp(cmd, "clients") == 0)
        srv_print_clients();
    else if (strcmp(cmd, "say") == 0)
        cmd_say(args);
    else if (strcmp(cmd, "kick") == 0)
        cmd_kick(args);
    else if (strcmp(cmd, "refresh") == 0)
        cmd_refresh(args);
    else if (strcmp(cmd, "watch") == 0)
        cmd_watch(args);
    else if (strcmp(cmd, "clear") == 0)
        cmd_clear();
    else
        color_print(NET_C_ERR "未知命令: %s（输入 -h 查看帮助）\n", cmd);

    return CCTRUE;
}

int main(void)
{
    char line[SRV_LINE_MAX];
    CCINT32 i;
    CCBOOL keep;

    memset(&g_srv, 0, sizeof(g_srv));
    net_common_init();
    g_srv.lock = cc_create_lock();
    if (!g_srv.lock)
    {
        color_print(NET_C_ERR "[错误] 服务端锁创建失败\n");
        return 1;
    }
    for (i = 0; i < SRV_MAX_CLIENTS; i++)
    {
        g_srv.slots[i].state = SLOT_FREE;
        g_srv.slots[i].qlock = cc_create_lock();
        if (!g_srv.slots[i].qlock)
        {
            color_print(NET_C_ERR "[错误] 连接槽位锁创建失败\n");
            return 1;
        }
    }
    if (!net_ring_init(&g_srv.events, g_srv.event_slots, SRV_EVENT_CAP))
    {
        color_print(NET_C_ERR "[错误] 事件环形缓冲区初始化失败\n");
        return 1;
    }
    g_srv.next_id = 1;
    g_srv.start_us = net_now_us();
    atomic_store(&g_srv.listener_exited, 1);
    atomic_store(&g_srv.watch_ms, 2000);
    atomic_store(&g_srv.watch_on, net_console_tty() ? 1 : 0);

    color_print(NET_C_TITLE "水晶簇标准库 · 网络 demo 服务端（输入 -h 查看帮助，quit 退出）\n");

    g_watch_thread = cc_thread_create(srv_watch_entry, &g_srv);
    if (!g_watch_thread)
        color_print(NET_C_WARN "[提示] 实时状态线程创建失败，watch 不可用\n");

    for (;;)
    {
        size_t len;

        srv_prompt();
        if (!fgets(line, sizeof(line), stdin))
        {
            color_print("\n");
            break;
        }

        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        net_console_enter();
        keep = srv_dispatch(line);
        net_console_leave();
        if (!keep)
            break;
    }

    srv_shutdown();
    net_common_deinit();
    return 0;
}
