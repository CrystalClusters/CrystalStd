/*
 * @Description: ccnet 交互演示公共设施实现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "net_common.h"

#ifdef CC_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

static CCLock g_out_lock = NULL;
static _Atomic CCINT32 g_busy = 0;
static CCBOOL g_tty = CCFALSE;

CCINT64 net_now_us(void)
{
    return (CCINT64)(cctime() * 1000000.0);
}

void net_common_init(void)
{
    g_out_lock = cc_create_lock();
#ifdef CC_WINDOWS
    g_tty = _isatty(_fileno(stdin)) ? CCTRUE : CCFALSE;
#else
    g_tty = isatty(STDIN_FILENO) ? CCTRUE : CCFALSE;
#endif
}

void net_common_deinit(void)
{
    if (g_out_lock)
    {
        cc_destroy_lock(g_out_lock);
        g_out_lock = NULL;
    }
}

CCBOOL net_console_tty(void)
{
    return g_tty;
}

void net_console_enter(void)
{
    if (!g_out_lock)
        return;
    atomic_store_explicit(&g_busy, 1, memory_order_release);
    cc_lock(g_out_lock);
}

void net_console_leave(void)
{
    if (!g_out_lock)
        return;
    cc_unlock(g_out_lock);
    atomic_store_explicit(&g_busy, 0, memory_order_release);
}

CCBOOL net_console_try_enter(void)
{
    if (!g_out_lock)
        return CCFALSE;
    if (atomic_load_explicit(&g_busy, memory_order_acquire))
        return CCFALSE;
    cc_lock(g_out_lock);
    if (atomic_load_explicit(&g_busy, memory_order_acquire))
    {
        cc_unlock(g_out_lock);
        return CCFALSE;
    }
    return CCTRUE;
}

CCBOOL net_ring_init(NetRing *ring, NetRingEntry *slots, CCUINT32 cap)
{
    if (!ring || !slots || cap == 0)
        return CCFALSE;
    memset(ring, 0, sizeof(*ring));
    ring->lock = cc_create_lock();
    if (!ring->lock)
        return CCFALSE;
    ring->cap = cap;
    ring->slots = slots;
    return CCTRUE;
}

void net_ring_destroy(NetRing *ring)
{
    if (!ring)
        return;
    if (ring->lock)
        cc_destroy_lock(ring->lock);
    ring->lock = NULL;
}

void net_ring_push(NetRing *ring, CCINT32 kind, const char *text, CCUINT32 len,
                   CCINT64 wall_sec)
{
    NetRingEntry *e;
    if (!ring || !ring->lock)
        return;
    if (len >= NET_MSG_CAP)
        len = NET_MSG_CAP - 1u;
    cc_lock(ring->lock);
    if (ring->count == ring->cap)
    {
        ring->head = (ring->head + 1u) % ring->cap;
        ring->count--;
        ring->dropped++;
    }
    e = &ring->slots[(ring->head + ring->count) % ring->cap];
    memset(e, 0, sizeof(*e));
    e->seq = ring->seq_next++;
    e->kind = kind;
    e->len = len;
    e->wall_sec = wall_sec;
    if (len > 0 && text)
        memcpy(e->text, text, len);
    e->text[len] = '\0';
    ring->count++;
    cc_unlock(ring->lock);
}

CCUINT32 net_ring_pop(NetRing *ring, NetRingEntry *out, CCUINT32 max)
{
    CCUINT32 n = 0;
    if (!ring || !ring->lock || !out || max == 0)
        return 0;
    cc_lock(ring->lock);
    while (n < max && ring->count > 0)
    {
        out[n++] = ring->slots[ring->head];
        ring->head = (ring->head + 1u) % ring->cap;
        ring->count--;
    }
    cc_unlock(ring->lock);
    return n;
}

CCUINT32 net_ring_skip(NetRing *ring, CCUINT32 n)
{
    CCUINT32 done = 0;
    if (!ring || !ring->lock)
        return 0;
    cc_lock(ring->lock);
    while (done < n && ring->count > 0)
    {
        ring->head = (ring->head + 1u) % ring->cap;
        ring->count--;
        done++;
    }
    cc_unlock(ring->lock);
    return done;
}

CCUINT32 net_ring_clear(NetRing *ring)
{
    CCUINT32 n;
    if (!ring || !ring->lock)
        return 0;
    cc_lock(ring->lock);
    n = ring->count;
    ring->count = 0;
    ring->head = 0;
    cc_unlock(ring->lock);
    return n;
}

CCUINT32 net_ring_unread(NetRing *ring)
{
    CCUINT32 n;
    if (!ring || !ring->lock)
        return 0;
    cc_lock(ring->lock);
    n = ring->count;
    cc_unlock(ring->lock);
    return n;
}

CCUINT32 net_ring_dropped(NetRing *ring)
{
    CCUINT32 n;
    if (!ring || !ring->lock)
        return 0;
    cc_lock(ring->lock);
    n = ring->dropped;
    cc_unlock(ring->lock);
    return n;
}

void net_event_push(NetRing *ring, CCINT32 kind, const char *fmt, ...)
{
    char text[NET_MSG_CAP];
    va_list args;
    int n;

    if (!ring || !fmt)
        return;
    va_start(args, fmt);
    n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (n < 0)
        text[0] = '\0';
    if (n >= (int)sizeof(text))
        n = (int)sizeof(text) - 1;
    net_ring_push(ring, kind, text, (CCUINT32)n, (CCINT64)time(NULL));
}

void net_line_init(NetLine *line)
{
    if (!line)
        return;
    line->len = 0;
    line->over = CCFALSE;
}

void net_line_feed(NetLine *line, const char *data, CCUINT32 n,
                   NetLineFn fn, void *user)
{
    CCUINT32 i;
    if (!line || !data || !fn)
        return;

    for (i = 0; i < n; i++)
    {
        char c = data[i];
        if (line->over)
        {
            if (c == '\n')
            {
                line->over = CCFALSE;
                line->len = 0;
            }
            continue;
        }
        if (c == '\n')
        {
            CCUINT32 len = line->len;
            if (len > 0 && line->buf[len - 1u] == '\r')
                len--;
            if (len > 0)
                fn(user, line->buf, len, CCFALSE);
            line->len = 0;
        }
        else if (line->len < NET_LINE_CAP - 1u)
        {
            line->buf[line->len++] = c;
        }
        else
        {
            line->over = CCTRUE;
            fn(user, line->buf, line->len, CCTRUE);
            line->len = 0;
        }
    }
}

CCINT64 net_send_all(const CCNetTcpOps *ops, void *ctx, const char *data,
                     CCUINT64 len, CCUINT32 timeout_us)
{
    CCINT64 sent = 0;
    CCINT64 deadline;

    if (!ops || !ctx || (!data && len > 0))
        return CC_NET_ERR_PARAM;
    if (len == 0)
        return 0;

    deadline = net_now_us() + (CCINT64)timeout_us;
    while ((CCUINT64)sent < len)
    {
        CCINT64 n = ops->send(ctx, data + sent, len - (CCUINT64)sent, timeout_us);
        if (n > 0)
        {
            sent += n;
            continue;
        }
        if (n == 0)
        {
            if (net_now_us() >= deadline)
                break;
            continue;
        }
        if (n == CC_NET_ERR_TIMEOUT)
        {
            if (net_now_us() >= deadline)
                break;
            continue;
        }
        return n;
    }

    if ((CCUINT64)sent < len)
        return sent > 0 ? sent : CC_NET_ERR_TIMEOUT;
    return sent;
}

const char *net_err_name(CCINT32 err)
{
    switch (err)
    {
    case CC_NET_OK:              return "OK";
    case CC_NET_ERR_PARAM:       return "PARAM";
    case CC_NET_ERR_NOMEM:       return "NOMEM";
    case CC_NET_ERR_RESOURCE:    return "RESOURCE";
    case CC_NET_ERR_STATE:       return "STATE";
    case CC_NET_ERR_TIMEOUT:     return "TIMEOUT";
    case CC_NET_ERR_DNS:         return "DNS";
    case CC_NET_ERR_REFUSED:     return "REFUSED";
    case CC_NET_ERR_UNREACH:     return "UNREACH";
    case CC_NET_ERR_RESET:       return "RESET";
    case CC_NET_ERR_ADDRINUSE:   return "ADDRINUSE";
    case CC_NET_ERR_ADDRNOTAVAIL:return "ADDRNOTAVAIL";
    case CC_NET_ERR_TOOBIG:      return "TOOBIG";
    case CC_NET_ERR_TRUNCATED:   return "TRUNCATED";
    case CC_NET_ERR_IO:          return "IO";
    default:                     return "UNKNOWN";
    }
}

const char *net_state_name(CCINT32 state)
{
    switch (state)
    {
    case CC_NET_STATE_OPEN:        return "OPEN";
    case CC_NET_STATE_PEER_CLOSED: return "PEER_CLOSED";
    case CC_NET_STATE_FAILED:      return "FAILED";
    default:                       return "IDLE";
    }
}

void net_sanitize(char *dst, CCUINT32 cap, const char *src, CCUINT32 len,
                  CCUINT32 *out_len)
{
    CCUINT32 i;
    CCUINT32 n = 0;
    if (!dst || cap == 0)
    {
        if (out_len) *out_len = 0;
        return;
    }
    for (i = 0; i < len && n + 1u < cap; i++)
    {
        unsigned char c = (unsigned char)src[i];
        if (c == '\t')
            dst[n++] = ' ';
        else if (c < 0x20 || c == 0x7F)
            dst[n++] = '.';
        else
            dst[n++] = (char)c;
    }
    dst[n] = '\0';
    if (out_len)
        *out_len = n;
}

void net_fmt_time(CCINT64 wall_sec, char *buf, CCUINT32 cap)
{
    time_t t = (time_t)wall_sec;
    struct tm tmv;
    if (!buf || cap == 0)
        return;
#ifdef CC_WINDOWS
    if (localtime_s(&tmv, &t) != 0)
    {
        snprintf(buf, cap, "--:--:--");
        return;
    }
#else
    if (!localtime_r(&t, &tmv))
    {
        snprintf(buf, cap, "--:--:--");
        return;
    }
#endif
    snprintf(buf, cap, "%02d:%02d:%02d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

void net_fmt_duration(CCINT64 usec, char *buf, CCUINT32 cap)
{
    CCINT64 total;
    CCINT64 h;
    CCINT64 m;
    CCINT64 s;
    if (usec < 0)
        usec = 0;
    total = usec / 1000000;
    h = total / 3600;
    m = (total % 3600) / 60;
    s = total % 60;
    snprintf(buf, cap, "%02ld:%02ld:%02ld", (long)h, (long)m, (long)s);
}

CCBOOL net_parse_u32(const char *s, CCUINT32 *out)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !*s || !out)
        return CCFALSE;
    v = strtoul(s, &end, 10);
    if (end == s || (end && *end != '\0'))
        return CCFALSE;
    if (v > 0xFFFFFFFFul)
        return CCFALSE;
    *out = (CCUINT32)v;
    return CCTRUE;
}

void net_copy_text(char *dst, CCUINT32 cap, const char *src)
{
    if (!dst || cap == 0)
        return;
    snprintf(dst, cap, "%s", src ? src : "");
}

/* 把一行拆成命令词与参数；空行返回 CCFALSE。命令行与两个入口共用同一份解析逻辑。 */
CCBOOL net_split_command(char *line, char *cmd, CCUINT32 cap, const char **args)
{
    char *p = line;
    CCUINT32 n = 0;

    if (!line || !cmd || cap == 0)
        return CCFALSE;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0')
        return CCFALSE;
    while (*p && *p != ' ' && *p != '\t' && n + 1u < cap)
        cmd[n++] = *p++;
    cmd[n] = '\0';
    while (*p == ' ' || *p == '\t')
        p++;
    *args = *p ? p : NULL;
    return CCTRUE;
}
