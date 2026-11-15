/*
 * @Description: ccnet 客户端交互演示。
 *               支持域名/IP 字面量连接、多地址回退、发送队列与接收环形缓冲区。
 *               接收数据只入环形缓冲区，绝不打断命令行输入；refresh 显示最新未读。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "../common/net_common.h"

#define CLI_OUT_CAP   32
#define CLI_LINE_MAX  4096
#define CLI_ADDR_MAX  8
#define CLI_SEND_MAX  1023u

typedef struct out_msg {
    CCUINT32 len;
    char data[NET_LINE_CAP + 1];
} OutMsg;

typedef struct cli_link {
    CCLock lock;
    const CCNetTcpOps *ops;
    void *ctx;
    CCThread writer;
    _Atomic CCINT32 entry_alive;
    _Atomic CCINT32 entry_exited;
    _Atomic CCINT32 stop;
    _Atomic CCINT32 state;
    _Atomic CCINT32 last_error;

    OutMsg queue[CLI_OUT_CAP];
    CCUINT32 qhead;
    CCUINT32 qcount;

    char local_text[96];
    char peer_text[96];
    _Atomic CCINT64 connected_us;
    _Atomic CCUINT64 rx_bytes;
    _Atomic CCUINT64 tx_bytes;
    _Atomic CCUINT32 rx_frames;
    _Atomic CCUINT32 tx_frames;
    _Atomic CCUINT32 tx_dropped;
} CliLink;

typedef struct dns_result {
    CCINT32 source;                 /* 1=字面量，2=域名 */
    CCINT32 count;
    double ms;
    CCNetAddr addrs[CLI_ADDR_MAX];
    char text[CLI_ADDR_MAX][96];
} DnsResult;

typedef struct cli_state {
    CliLink link;
    NetRingEntry ring_slots[NET_RING_CAP];
    NetRing inbox;

    char host[256];
    CCUINT16 port;
    CCINT32 source;
    CCINT32 dns_count;
    double dns_ms;
    char dns_text[CLI_ADDR_MAX][96];
    char chosen_text[96];
    CCINT64 start_us;
} CliState;

static CliState g_cli;
static char g_prompt[160] = "cli[off]> ";

static void cli_print_help(void)
{
    color_print(NET_C_TITLE "客户端命令（<> 必填，[] 可选）：\n" NET_C_RESET
        "  connect <host> <port>     连接；host 可为域名、IPv4、IPv6 字面量\n"
        "  dns <host> <port>     只做地址解析并显示结果\n"
        "  send <text>               入队一条文本消息（<=1023 字节）\n"
        "  spam <n> [text]           连续入队 n 条，用于溢出演示\n"
        "  refresh [n]               显示最新未读消息\n"
        "  status                    连接、解析与缓冲状态\n"
        "  disconnect                断开当前连接\n"
        "  clear                     丢弃未读消息\n"
        "  help / -h                 帮助\n"
        "  quit / exit               断开并退出\n");
}

static void cli_prompt(void)
{
    CCBOOL alive = atomic_load(&g_cli.link.entry_alive) ? CCTRUE : CCFALSE;
    CCUINT32 unread = net_ring_unread(&g_cli.inbox);

    if (alive)
        snprintf(g_prompt, sizeof(g_prompt), "cli[on u=%u]> ", unread);
    else
        snprintf(g_prompt, sizeof(g_prompt), "cli[off u=%u]> ", unread);
    net_console_enter();
    color_print(alive ? NET_C_INFO "%s" NET_C_RESET : NET_C_DIM "%s" NET_C_RESET,
                g_prompt);
    fflush(stdout);
    net_console_leave();
}

/* ------------------------------------------------------------- 域名解析 */

static CCINT32 cli_dns(const char *host, CCUINT16 port, DnsResult *out)
{
    CCNetAddr literal;
    CCINT32 rc;
    CCINT32 count;
    double begin;

    memset(out, 0, sizeof(*out));
    rc = ccnet_addr_parse(host, port, &literal);
    if (rc == CC_NET_OK)
    {
        out->source = 1;
        out->count = 1;
        out->addrs[0] = literal;
        (void)ccnet_addr_text(&literal, out->text[0], sizeof(out->text[0]));
        return CC_NET_OK;
    }
    if (rc == CC_NET_ERR_PARAM)
        return rc;

    begin = cctime();
    count = ccnet_dns(host, port, out->addrs, CLI_ADDR_MAX);
    out->ms = (cctime() - begin) * 1000.0;
    if (count <= 0)
        return count < 0 ? count : CC_NET_ERR_DNS;
    out->count = count;
    out->source = 2;
    for (CCINT32 i = 0; i < out->count; i++)
        (void)ccnet_addr_text(&out->addrs[i], out->text[i], sizeof(out->text[i]));
    return CC_NET_OK;
}

static void cli_print_dns(const char *host, CCUINT16 port, const DnsResult *d)
{
    if (d->source == 1)
    {
        color_print(NET_C_INFO "[解析] %s:%u -> 字面量 %s\n",
                    host, (CCUINT32)port, d->text[0]);
        return;
    }
    color_print(NET_C_INFO "[解析] %s:%u -> %d 条，用时 %.2f ms\n",
                host, (CCUINT32)port, d->count, d->ms);
    for (CCINT32 i = 0; i < d->count; i++)
        color_print("  %d) %s (%s)\n", i + 1, d->text[i],
                    d->addrs[i].family == CC_NET_AF_INET ? "IPv4" : "IPv6");
}

/* --------------------------------------------------------- 连接与读写线程 */

static void cli_writer(void *data, CCThread id)
{
    CliLink *link = (CliLink *)data;
    (void)id;

    for (;;)
    {
        OutMsg msg;
        CCINT64 w;

        cc_lock(link->lock);
        if (atomic_load(&link->stop))
        {
            atomic_fetch_add(&link->tx_dropped, link->qcount);
            link->qcount = 0;
            cc_unlock(link->lock);
            break;
        }
        if (link->qcount == 0)
        {
            cc_unlock(link->lock);
            ccsleep(2000);
            continue;
        }
        msg = link->queue[link->qhead];
        link->qhead = (link->qhead + 1u) % CLI_OUT_CAP;
        link->qcount--;
        cc_unlock(link->lock);

        w = net_send_all(link->ops, link->ctx, msg.data, msg.len, 1000000);
        if (w != (CCINT64)msg.len)
        {
            cc_lock(link->lock);
            atomic_store(&link->last_error,
                         (CCINT32)(w < 0 ? w : CC_NET_ERR_IO));
            atomic_store(&link->stop, 1);
            atomic_fetch_add(&link->tx_dropped, link->qcount);
            link->qcount = 0;
            cc_unlock(link->lock);
            net_event_push(&g_cli.inbox, NET_EV_ERROR, "发送失败：%s",
                      w < 0 ? net_err_name((CCINT32)w) : "发送未完成");
            break;
        }
        atomic_fetch_add(&link->tx_frames, 1);
        atomic_fetch_add(&link->tx_bytes, msg.len);
    }
}

static void cli_on_line(void *user, const char *line, CCUINT32 len, CCBOOL truncated)
{
    char text[NET_MSG_CAP];
    CCUINT32 n;

    (void)user;
    if (len == 0)
        return;
    net_sanitize(text, sizeof(text), line, len, &n);
    if (truncated && n + 3u < sizeof(text))
    {
        memcpy(text + n, "...", 3);
        n += 3;
        text[n] = '\0';
    }
    net_ring_push(&g_cli.inbox, NET_EV_MSG, text, n, (CCINT64)time(NULL));
    atomic_fetch_add(&g_cli.link.rx_frames, 1);
}

static void cli_entry(const CCNetTcpOps *ops, void *ctx, void *user)
{
    CliState *cli = (CliState *)user;
    CliLink *link = &cli->link;
    CCNetAddr addr;
    char buf[2048];
    NetLine line;
    CCThread writer;

    cc_lock(link->lock);
    link->ops = ops;
    link->ctx = ctx;
    if (ops->local(ctx, &addr) == CC_NET_OK)
        (void)ccnet_addr_text(&addr, link->local_text, sizeof(link->local_text));
    if (ops->peer(ctx, &addr) == CC_NET_OK)
        (void)ccnet_addr_text(&addr, link->peer_text, sizeof(link->peer_text));
    link->writer = cc_thread_create(cli_writer, link);
    writer = link->writer;
    if (!writer)
    {
        atomic_store(&link->last_error, CC_NET_ERR_RESOURCE);
        atomic_store(&link->entry_alive, 0);
        atomic_store(&link->entry_exited, 1);
        atomic_store(&link->state, CC_NET_STATE_FAILED);
        cc_unlock(link->lock);
        net_event_push(&g_cli.inbox, NET_EV_ERROR, "写线程创建失败");
        return;
    }
    atomic_store(&link->entry_alive, 1);
    atomic_store(&link->state, CC_NET_STATE_OPEN);
    atomic_store(&link->connected_us, net_now_us());
    cc_unlock(link->lock);
    net_event_push(&g_cli.inbox, NET_EV_EVENT, "已连接 %s", link->peer_text);

    net_line_init(&line);
    for (;;)
    {
        CCINT64 n;

        if (atomic_load(&link->stop))
            break;
        n = ops->recv(ctx, buf, sizeof(buf), 200000);
        if (n == CC_NET_ERR_TIMEOUT)
            continue;
        if (n == 0)
        {
            net_event_push(&g_cli.inbox, NET_EV_EVENT, "对端关闭连接");
            atomic_store(&link->state, CC_NET_STATE_PEER_CLOSED);
            break;
        }
        if (n < 0)
        {
            cc_lock(link->lock);
            atomic_store(&link->last_error, (CCINT32)n);
            if ((CCINT32)n == CC_NET_ERR_RESET)
                atomic_store(&link->state, CC_NET_STATE_FAILED);
            cc_unlock(link->lock);
            net_event_push(&g_cli.inbox, NET_EV_ERROR, "接收失败：%s", net_err_name((CCINT32)n));
            break;
        }

        atomic_fetch_add(&link->rx_bytes, (CCUINT64)n);
        net_line_feed(&line, buf, (CCUINT32)n, cli_on_line, cli);
    }

    atomic_store(&link->stop, 1);
    cc_thread_wait(writer, 0);

    cc_lock(link->lock);
    link->writer = 0;
    atomic_store(&link->entry_alive, 0);
    atomic_store(&link->entry_exited, 1);
    if (atomic_load(&link->state) == CC_NET_STATE_OPEN)
        atomic_store(&link->state, CC_NET_STATE_PEER_CLOSED);
    cc_unlock(link->lock);
    net_event_push(&g_cli.inbox, NET_EV_EVENT, "连接结束");
}

/* --------------------------------------------------------------- 命令实现 */

static void cmd_dns(const char *args)
{
    char host[256];
    CCUINT32 port;
    DnsResult d;
    CCINT32 rc;

    if (!args || sscanf(args, "%255s %u", host, &port) != 2 || port == 0 ||
        port > 65535u)
    {
        color_print(NET_C_WARN "用法: dns <host> <port>\n");
        return;
    }
    rc = cli_dns(host, (CCUINT16)port, &d);
    if (rc != CC_NET_OK)
    {
        color_print(NET_C_ERR "[失败] 解析 %s:%u：%s (%d)\n",
                    host, port, net_err_name(rc), (int)rc);
        return;
    }
    cli_print_dns(host, (CCUINT16)port, &d);
}

static CCBOOL cli_wait_ready(CCUINT32 timeout_us)
{
    CCUINT32 elapsed = 0;
    for (;;)
    {
        if (atomic_load(&g_cli.link.entry_alive) ||
            atomic_load(&g_cli.link.entry_exited))
            return CCTRUE;
        if (elapsed >= timeout_us)
            return CCFALSE;
        ccsleep(1000);
        elapsed += 1000;
    }
}

static void cmd_connect(const char *args)
{
    char host[256];
    CCUINT32 port;
    DnsResult d;
    CCINT32 rc;
    CCINT32 last_err = CC_NET_OK;

    if (!args || sscanf(args, "%255s %u", host, &port) != 2 || port == 0 ||
        port > 65535u)
    {
        color_print(NET_C_WARN "用法: connect <host> <port>\n");
        return;
    }
    if (atomic_load(&g_cli.link.entry_alive))
    {
        color_print(NET_C_WARN "已连接，先 disconnect\n");
        return;
    }
    if (!atomic_load(&g_cli.link.entry_exited))
    {
        color_print(NET_C_WARN "上一次连接仍在收尾，请稍候\n");
        return;
    }

    memset(&g_cli.link.queue, 0, sizeof(g_cli.link.queue));
    cc_lock(g_cli.link.lock);
    atomic_store(&g_cli.link.stop, 0);
    atomic_store(&g_cli.link.entry_alive, 0);
    atomic_store(&g_cli.link.entry_exited, 0);
    atomic_store(&g_cli.link.state, 0);
    atomic_store(&g_cli.link.last_error, 0);
    g_cli.link.qhead = 0;
    g_cli.link.qcount = 0;
    g_cli.link.writer = 0;
    cc_unlock(g_cli.link.lock);

    rc = cli_dns(host, (CCUINT16)port, &d);
    if (rc != CC_NET_OK)
    {
        atomic_store(&g_cli.link.entry_exited, 1);
        color_print(NET_C_ERR "[失败] 解析 %s:%u：%s (%d)\n",
                    host, port, net_err_name(rc), (int)rc);
        return;
    }

    net_copy_text(g_cli.host, sizeof(g_cli.host), host);
    g_cli.port = (CCUINT16)port;
    g_cli.source = d.source;
    g_cli.dns_count = d.count;
    g_cli.dns_ms = d.ms;
    for (CCINT32 i = 0; i < d.count; i++)
        net_copy_text(g_cli.dns_text[i], sizeof(g_cli.dns_text[i]),
                  d.text[i]);
    g_cli.chosen_text[0] = '\0';
    cli_print_dns(host, (CCUINT16)port, &d);

    for (CCINT32 i = 0; i < d.count; i++)
    {
        color_print(NET_C_DIM "[尝试] %s\n", d.text[i]);
        rc = ccnet_tcp_connect(&d.addrs[i], 5000000, cli_entry, &g_cli);
        if (rc != CC_NET_OK)
        {
            last_err = rc;
            color_print(NET_C_ERR "  失败：%s (%d)\n", net_err_name(rc), (int)rc);
            continue;
        }
        if (!cli_wait_ready(2000000))
        {
            last_err = CC_NET_ERR_TIMEOUT;
            color_print(NET_C_ERR "  入口未在 2 秒内就绪\n");
            continue;
        }
        if (atomic_load(&g_cli.link.entry_alive))
        {
            net_copy_text(g_cli.chosen_text, sizeof(g_cli.chosen_text), d.text[i]);
            color_print(NET_C_OK "[完成] 已连接 %s（第 %d/%d 条）\n",
                        d.text[i], i + 1, d.count);
            return;
        }
        last_err = CC_NET_ERR_IO;
        color_print(NET_C_ERR "  连接立即结束（第 %d/%d 条）\n", i + 1, d.count);
    }

    atomic_store(&g_cli.link.entry_exited, 1);
    color_print(NET_C_ERR "[失败] 所有地址均未连通，最后错误：%s\n",
                net_err_name(last_err));
}

static void cmd_send(const char *args)
{
    CCUINT32 len;
    OutMsg *msg;

    if (!args || !*args)
    {
        color_print(NET_C_WARN "用法: send <text>\n");
        return;
    }
    len = (CCUINT32)strlen(args);
    if (len > CLI_SEND_MAX)
    {
        color_print(NET_C_ERR "消息过长（上限 %u 字节）\n", CLI_SEND_MAX);
        return;
    }

    cc_lock(g_cli.link.lock);
    if (!atomic_load(&g_cli.link.entry_alive))
    {
        cc_unlock(g_cli.link.lock);
        color_print(NET_C_ERR "未连接\n");
        return;
    }
    if (g_cli.link.qcount == CLI_OUT_CAP)
    {
        cc_unlock(g_cli.link.lock);
        color_print(NET_C_ERR "发送队列已满，请稍后再试\n");
        return;
    }
    msg = &g_cli.link.queue[(g_cli.link.qhead + g_cli.link.qcount) % CLI_OUT_CAP];
    memcpy(msg->data, args, len);
    msg->data[len++] = '\n';
    msg->len = len;
    g_cli.link.qcount++;
    cc_unlock(g_cli.link.lock);

    color_print(NET_C_OK "[完成] 已提交 %u 字节\n", len);
}

static void cmd_spam(const char *args)
{
    char text[NET_LINE_CAP];
    char num[32];
    CCUINT32 count = 0;
    CCUINT32 sent = 0;
    const char *body = NULL;

    if (!args)
    {
        color_print(NET_C_WARN "用法: spam <n> [text]\n");
        return;
    }
    if (sscanf(args, "%31s", num) != 1 || !net_parse_u32(num, &count) ||
        count == 0 || count > 10000u)
    {
        color_print(NET_C_WARN "用法: spam <n> [text]（1..10000）\n");
        return;
    }
    body = strstr(args, num);
    if (body)
    {
        body += strlen(num);
        while (*body == ' ' || *body == '\t') body++;
    }
    if (!body || !*body)
        body = "demo";
    net_copy_text(text, sizeof(text), body);
    if (strlen(text) > CLI_SEND_MAX)
        text[CLI_SEND_MAX] = '\0';

    while (sent < count)
    {
        OutMsg *msg;
        CCUINT32 len = (CCUINT32)strlen(text);

        cc_lock(g_cli.link.lock);
        if (!atomic_load(&g_cli.link.entry_alive) ||
            atomic_load(&g_cli.link.stop))
        {
            cc_unlock(g_cli.link.lock);
            color_print(NET_C_ERR "连接已断开，已提交 %u 条\n", sent);
            return;
        }
        if (g_cli.link.qcount == CLI_OUT_CAP)
        {
            cc_unlock(g_cli.link.lock);
            ccsleep(1000);
            continue;
        }
        msg = &g_cli.link.queue[(g_cli.link.qhead + g_cli.link.qcount) % CLI_OUT_CAP];
        memcpy(msg->data, text, len);
        msg->data[len++] = '\n';
        msg->len = len;
        g_cli.link.qcount++;
        cc_unlock(g_cli.link.lock);
        sent++;
    }
    color_print(NET_C_OK "[完成] 已提交 %u 条消息\n", sent);
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
    total = net_ring_unread(&g_cli.inbox);
    if (total == 0)
    {
        color_print(NET_C_DIM "[刷新] 无未读消息（累计丢弃 %u）\n",
                    net_ring_dropped(&g_cli.inbox));
        return;
    }
    skip = (limit > 0 && limit < total) ? total - limit : 0;
    color_print(NET_C_TITLE "[刷新] 未读 %u 条，丢弃 %u 条\n",
                total, net_ring_dropped(&g_cli.inbox));
    if (skip > 0)
    {
        net_ring_skip(&g_cli.inbox, skip);
        color_print(NET_C_DIM "  （跳过更早的 %u 条）\n", skip);
    }

    for (;;)
    {
        NetRingEntry batch[16];
        CCUINT32 n = net_ring_pop(&g_cli.inbox, batch, 16);
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

static void cmd_status(void)
{
    CCBOOL alive;
    CCINT32 state;
    CCINT32 last_error;
    CCUINT32 qcount;
    CCUINT64 rx_bytes;
    CCUINT64 tx_bytes;
    CCUINT32 rx_frames;
    CCUINT32 tx_frames;
    CCUINT32 tx_dropped;
    CCINT64 connected_us;
    char local[96];
    char peer[96];
    char dur[32];

    cc_lock(g_cli.link.lock);
    alive = atomic_load(&g_cli.link.entry_alive) ? CCTRUE : CCFALSE;
    state = atomic_load(&g_cli.link.state);
    last_error = atomic_load(&g_cli.link.last_error);
    qcount = g_cli.link.qcount;
    rx_bytes = atomic_load(&g_cli.link.rx_bytes);
    tx_bytes = atomic_load(&g_cli.link.tx_bytes);
    rx_frames = atomic_load(&g_cli.link.rx_frames);
    tx_frames = atomic_load(&g_cli.link.tx_frames);
    tx_dropped = atomic_load(&g_cli.link.tx_dropped);
    connected_us = atomic_load(&g_cli.link.connected_us);
    net_copy_text(local, sizeof(local), g_cli.link.local_text);
    net_copy_text(peer, sizeof(peer), g_cli.link.peer_text);
    cc_unlock(g_cli.link.lock);

    if (g_cli.host[0])
    {
        if (g_cli.source == 1)
            color_print(NET_C_TITLE "[状态] 目标 %s:%u（字面量）\n",
                        g_cli.host, (CCUINT32)g_cli.port);
        else
            color_print(NET_C_TITLE "[状态] 目标 %s:%u（域名，%d 条，%.2f ms）\n",
                        g_cli.host, (CCUINT32)g_cli.port,
                        g_cli.dns_count, g_cli.dns_ms);
    }
    else
    {
        color_print(NET_C_TITLE "[状态] 未连接\n");
    }

    if (alive)
    {
        net_fmt_duration(connected_us > 0 ? net_now_us() - connected_us : 0,
                         dur, sizeof(dur));
        color_print(NET_C_OK "  连接: OPEN（%s）\n", net_state_name(state));
        color_print("  本地: %s\n", local[0] ? local : "?");
        color_print("  对端: %s%s%s\n", peer[0] ? peer : "?",
                    g_cli.chosen_text[0] ? " <- " : "",
                    g_cli.chosen_text[0] ? g_cli.chosen_text : "");
        color_print("  在线: %s\n", dur);
    }
    else
    {
        color_print(NET_C_DIM "  连接: 未连接（状态 %s）\n", net_state_name(state));
    }
    color_print("  收: %u 条 / %lu 字节\n", rx_frames, rx_bytes);
    color_print("  发: %u 条 / %lu 字节\n", tx_frames, tx_bytes);
    color_print("  队列: %u/%u | 发送丢弃 %u\n", qcount, CLI_OUT_CAP, tx_dropped);
    color_print("  未读: %u（丢弃 %u）| 最后错误: %s\n",
                net_ring_unread(&g_cli.inbox), net_ring_dropped(&g_cli.inbox),
                last_error ? net_err_name(last_error) : "无");
}

static CCBOOL cli_disconnect(void)
{
    CCINT32 i;

    cc_lock(g_cli.link.lock);
    if (!atomic_load(&g_cli.link.entry_alive) &&
        atomic_load(&g_cli.link.entry_exited))
    {
        cc_unlock(g_cli.link.lock);
        color_print(NET_C_DIM "当前未连接\n");
        return CCTRUE;
    }
    atomic_store(&g_cli.link.stop, 1);
    atomic_store(&g_cli.link.entry_alive, 0);
    cc_unlock(g_cli.link.lock);

    for (i = 0; i < 5000 && !atomic_load(&g_cli.link.entry_exited); i++)
        ccsleep(1000);
    if (atomic_load(&g_cli.link.entry_exited))
    {
        color_print(NET_C_OK "[完成] 已断开\n");
        return CCTRUE;
    }
    color_print(NET_C_ERR "[失败] 连接未在 5 秒内退出\n");
    return CCFALSE;
}

static CCBOOL cli_dispatch(char *line)
{
    char cmd[32] = "";
    const char *args = NULL;

    if (!net_split_command(line, cmd, sizeof(cmd), &args))
        return CCTRUE;

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0)
        cli_print_help();
    else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 ||
             strcmp(cmd, "q") == 0)
        return CCFALSE;
    else if (strcmp(cmd, "connect") == 0)
        cmd_connect(args);
    else if (strcmp(cmd, "dns") == 0)
        cmd_dns(args);
    else if (strcmp(cmd, "send") == 0)
        cmd_send(args);
    else if (strcmp(cmd, "spam") == 0)
        cmd_spam(args);
    else if (strcmp(cmd, "refresh") == 0)
        cmd_refresh(args);
    else if (strcmp(cmd, "status") == 0)
        cmd_status();
    else if (strcmp(cmd, "disconnect") == 0)
        (void)cli_disconnect();
    else if (strcmp(cmd, "clear") == 0)
        color_print(NET_C_DIM "已丢弃 %u 条未读消息\n", net_ring_clear(&g_cli.inbox));
    else
        color_print(NET_C_ERR "未知命令: %s（输入 -h 查看帮助）\n", cmd);

    return CCTRUE;
}

int main(void)
{
    char line[CLI_LINE_MAX];
    CCBOOL keep;

    memset(&g_cli, 0, sizeof(g_cli));
    net_common_init();
    g_cli.link.lock = cc_create_lock();
    if (!g_cli.link.lock)
    {
        color_print(NET_C_ERR "[错误] 客户端锁创建失败\n");
        return 1;
    }
    if (!net_ring_init(&g_cli.inbox, g_cli.ring_slots, NET_RING_CAP))
    {
        color_print(NET_C_ERR "[错误] 环形缓冲区初始化失败\n");
        return 1;
    }
    g_cli.start_us = net_now_us();
    atomic_store(&g_cli.link.entry_exited, 1);

    color_print(NET_C_TITLE "水晶簇标准库 · 网络 demo 客户端（输入 -h 查看帮助，quit 退出）\n");

    for (;;)
    {
        size_t len;

        cli_prompt();
        if (!fgets(line, sizeof(line), stdin))
        {
            color_print("\n");
            break;
        }

        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        net_console_enter();
        keep = cli_dispatch(line);
        net_console_leave();
        if (!keep)
            break;
    }

    if (!atomic_load(&g_cli.link.entry_exited))
        (void)cli_disconnect();
    if (g_cli.link.lock)
        cc_destroy_lock(g_cli.link.lock);
    net_ring_destroy(&g_cli.inbox);
    net_common_deinit();
    return 0;
}
