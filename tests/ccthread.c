/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 异步组件（ccthread）的测试。
 *               覆盖线程创建/查询/等待、用户数据与 ID 传递、
 *               并发创建、锁互斥、超时语义以及反复创建回收的正确性。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#include "test_header.h"
#include <ccthread.h>

#define thread_expect(cond, name) CC_CHECK((cond), (name))

/* ============================================================================
 * 用例 1：单线程的创建、状态、等待与数据传递
 * ============================================================================ */

typedef struct single_ctx {
    volatile CCINT32 ran;
    volatile CCUINTPTR got_id;
    volatile void *got_data;
    CCINT32 payload;
} single_ctx;

// 入口先停住，直到测试确认状态后再放行，避免用例出现时序竞态
static volatile CCINT32 g_single_release = 0;

static void _entry_single_(void *data, CCThread idThis)
{
    single_ctx *ctx = (single_ctx*)data;
    ctx->got_id = (CCUINTPTR)idThis;
    ctx->got_data = data;
    while (!g_single_release)
        ccsleep(1000);
    ctx->ran = 1;
}

static void test_thread_single(void)
{
    CC_CASE("THREAD-SINGLE-001", "单线程生命周期与数据传递");
    single_ctx ctx;
    ctx.ran = 0;
    ctx.got_id = 0;
    ctx.got_data = NULL;
    ctx.payload = 1234;

    g_single_release = 0;
    CCThread t = cc_thread_create(_entry_single_, &ctx);
    thread_expect(t != 0, "创建线程返回非 0 的线程 ID");
    thread_expect(cc_thread_status(t) == 1, "刚创建的线程处于存活状态");

    g_single_release = 1;
    cc_thread_wait(t, 0);

    thread_expect(ctx.ran == 1, "等待结束后线程入口已执行");
    thread_expect(ctx.got_data == &ctx, "线程入口收到正确的用户数据");
    thread_expect(ctx.got_id == (CCUINTPTR)t, "线程入口收到与创建返回一致的线程 ID");
    thread_expect(cc_thread_status(t) == 0, "线程结束后状态查询为 0");
}

/* ============================================================================
 * 用例 2：并发创建，各线程完成任务，结束后状态可正确查询
 * ============================================================================ */

#define MANY_N 16

typedef struct slot_ctx {
    volatile CCINT32 *slots;
    CCINT32 index;
} slot_ctx;

static void _entry_slot_(void *data, CCThread idThis)
{
    CC_UNUSED(idThis);
    slot_ctx *ctx = (slot_ctx*)data;
    ctx->slots[ctx->index] = 1;
}

static void test_thread_many(void)
{
    CC_CASE("THREAD-MANY-001", "并发创建与完成");
    volatile CCINT32 slots[MANY_N];
    slot_ctx ctxs[MANY_N];
    CCThread ids[MANY_N];

    for (CCINT32 i = 0; i < MANY_N; i++)
    {
        slots[i] = 0;
        ctxs[i].slots = slots;
        ctxs[i].index = i;
        ids[i] = cc_thread_create(_entry_slot_, &ctxs[i]);
    }
    for (CCINT32 i = 0; i < MANY_N; i++)
        cc_thread_wait(ids[i], 0);

    CCBOOL all_ran = CCTRUE;
    CCBOOL all_done = CCTRUE;
    for (CCINT32 i = 0; i < MANY_N; i++)
    {
        if (!slots[i]) all_ran = CCFALSE;
        if (cc_thread_status(ids[i]) != 0) all_done = CCFALSE;
    }
    thread_expect(all_ran, "并发创建的线程全部完成各自任务");
    thread_expect(all_done, "并发线程结束后状态均可正确查询为 0");
}
/* ============================================================================
 * 用例 3：同时存活的线程 ID 互不相同
 * ============================================================================ */

static volatile CCINT32 g_hold_release = 0;

static void _entry_hold_(void *data, CCThread idThis)
{
    CC_UNUSED(data);
    CC_UNUSED(idThis);
    while (!g_hold_release)
        ccsleep(1000);
}

static void test_thread_unique_ids(void)
{
    CC_CASE("THREAD-UNIQUE-001", "同时存活线程 ID 唯一");
    #define UNIQ_N 8
    CCThread ids[UNIQ_N];

    g_hold_release = 0;
    for (CCINT32 i = 0; i < UNIQ_N; i++)
        ids[i] = cc_thread_create(_entry_hold_, NULL);

    CCBOOL unique = CCTRUE;
    for (CCINT32 i = 0; i < UNIQ_N; i++)
    {
        if (!ids[i]) unique = CCFALSE;
        for (CCINT32 j = i + 1; j < UNIQ_N; j++)
            if (ids[i] == ids[j]) unique = CCFALSE;
    }
    thread_expect(unique, "同时存活的线程 ID 互不相同");

    g_hold_release = 1;
    for (CCINT32 i = 0; i < UNIQ_N; i++)
        cc_thread_wait(ids[i], 0);
}

/* ============================================================================
 * 用例 4：有限超时等待与无限等待
 * ============================================================================ */

static void _entry_sleep_(void *data, CCThread idThis)
{
    CC_UNUSED(data);
    CC_UNUSED(idThis);
    ccsleep(200000);  // 200ms
}

static void test_thread_wait_timeout(void)
{
    CC_CASE("THREAD-TIMEOUT-001", "有限等待与无限等待");
    CCThread t = cc_thread_create(_entry_sleep_, NULL);
    thread_expect(t != 0, "创建长任务线程成功");

    double begin = cctime();
    cc_thread_wait(t, 10000);  // 仅等待 10ms
    double cost = cctime() - begin;

    thread_expect(cost >= 0.005 && cost < 0.5, "有限超时等待在预期时间内返回");
    thread_expect(cc_thread_status(t) == 1, "等待超时后线程仍在运行");

    cc_thread_wait(t, 0);  // 收尾：无限等待至线程结束
    thread_expect(cc_thread_status(t) == 0, "无限等待后线程已结束");
}

/* ============================================================================
 * 用例 5：无效/不存在的线程 ID
 * ============================================================================ */

static void test_thread_invalid(void)
{
    CC_CASE("THREAD-INVALID-001", "非法线程 ID");
    thread_expect(cc_thread_status(0) == 0, "ID 为 0 时状态查询为 0");
    thread_expect(cc_thread_status((CCThread)0xDEADBEEFull) == 0, "不存在的 ID 状态查询为 0");

    double begin = cctime();
    cc_thread_wait((CCThread)0xDEADBEEFull, 0);
    double cost = cctime() - begin;
    thread_expect(cost < 0.05, "等待不存在的 ID 时立即返回而非阻塞");
}

/* ============================================================================
 * 用例 6：锁的互斥性
 * ============================================================================ */

#define LOCK_N 4
#define LOCK_ROUNDS 2000

typedef struct lock_ctx {
    CCLock lock;
    volatile CCINT32 *counter;
    CCINT32 rounds;
} lock_ctx;

static void _entry_lock_(void *data, CCThread idThis)
{
    CC_UNUSED(idThis);
    lock_ctx *ctx = (lock_ctx*)data;
    for (CCINT32 i = 0; i < ctx->rounds; i++)
    {
        cc_lock(ctx->lock);
        (*(ctx->counter))++;
        cc_unlock(ctx->lock);
    }
}

static void test_thread_lock(void)
{
    CC_CASE("THREAD-LOCK-001", "锁互斥");
    CCLock lock = cc_create_lock();
    thread_expect(lock != NULL, "创建锁成功");

    volatile CCINT32 counter = 0;
    lock_ctx ctxs[LOCK_N];
    CCThread ids[LOCK_N];
    for (CCINT32 i = 0; i < LOCK_N; i++)
    {
        ctxs[i].lock = lock;
        ctxs[i].counter = &counter;
        ctxs[i].rounds = LOCK_ROUNDS;
        ids[i] = cc_thread_create(_entry_lock_, &ctxs[i]);
    }
    for (CCINT32 i = 0; i < LOCK_N; i++)
        cc_thread_wait(ids[i], 0);

    thread_expect(counter == (LOCK_N * LOCK_ROUNDS), "多线程加锁累加无丢失");

    thread_expect(cc_destroy_lock(lock) == 0, "销毁锁成功");
}

/* ============================================================================
 * 用例 7：线程入口内查询自身，验证入口不在内部锁内执行
 * ============================================================================ */

typedef struct self_ctx {
    volatile CCINT32 status_seen;
} self_ctx;

static void _entry_self_(void *data, CCThread idThis)
{
    self_ctx *ctx = (self_ctx*)data;
    // 若包装层持锁调用入口，这里会因非递归锁而卡死
    ctx->status_seen = cc_thread_status(idThis);
}

static void test_thread_reentrant_query(void)
{
    CC_CASE("THREAD-LOCK-002", "线程入口内查询自身");
    self_ctx ctx;
    ctx.status_seen = -1;
    CCThread t = cc_thread_create(_entry_self_, &ctx);
    cc_thread_wait(t, 0);
    thread_expect(ctx.status_seen == 1, "线程入口内查询自身状态不会自锁且返回存活");
}

/* ============================================================================
 * 用例 8：反复创建/结束，验证节点回收后树与下标映射始终正确
 * ============================================================================ */

static void _entry_empty_(void *data, CCThread idThis)
{
    CC_UNUSED(data);
    CC_UNUSED(idThis);
}

static void test_thread_churn(void)
{
    CC_CASE("THREAD-CHURN-001", "反复创建与回收");
    #define CHURN_ROUNDS 200
    CCBOOL ok = CCTRUE;
    for (CCINT32 i = 0; i < CHURN_ROUNDS; i++)
    {
        CCThread t = cc_thread_create(_entry_empty_, NULL);
        if (!t)
        {
            ok = CCFALSE;
            break;
        }
        cc_thread_wait(t, 0);
        if (cc_thread_status(t) != 0)
        {
            ok = CCFALSE;
            break;
        }
    }
    thread_expect(ok, "反复创建/结束后状态查询与槽位回收保持正确");
}

/* ============================================================================
 * 用例 9：非顺序回收，专门验证「末尾节点搬到空位」的下标修正
 * ============================================================================ */

#define GATE_N 8

typedef struct gate_ctx {
    volatile CCINT32 *gates;
    CCINT32 index;
} gate_ctx;

static void _entry_gate_(void *data, CCThread idThis)
{
    CC_UNUSED(idThis);
    gate_ctx *ctx = (gate_ctx*)data;
    while (!ctx->gates[ctx->index])
        ccsleep(500);
}

static void test_thread_gate_recycle(void)
{
    CC_CASE("THREAD-RECYCLE-001", "非顺序回收的下标修正");
    volatile CCINT32 gates[GATE_N];
    gate_ctx ctxs[GATE_N];
    CCThread ids[GATE_N];

    for (CCINT32 i = 0; i < GATE_N; i++)
    {
        gates[i] = 0;
        ctxs[i].gates = gates;
        ctxs[i].index = i;
        ids[i] = cc_thread_create(_entry_gate_, &ctxs[i]);
    }
    thread_expect(ids[0] != 0, "批量创建被闸门阻塞的线程成功");

    // 乱序放行，强制出现「被删节点不是数组末尾」的情形，从而走 swap-with-last 分支
    static const CCINT32 order[GATE_N] = { 3, 0, 6, 1, 5, 2, 7, 4 };
    CCBOOL ok = CCTRUE;
    for (CCINT32 k = 0; k < GATE_N; k++)
    {
        CCINT32 i = order[k];
        gates[i] = 1;
        cc_thread_wait(ids[i], 0);
        if (cc_thread_status(ids[i]) != 0) ok = CCFALSE;
        // 其余仍存活的线程必须仍能按 key 正确检索（证明下标搬移没有破坏树）
        for (CCINT32 j = 0; j < GATE_N; j++)
        {
            if (gates[j]) continue;
            if (cc_thread_status(ids[j]) != 1) ok = CCFALSE;
        }
    }
    thread_expect(ok, "非顺序回收后树内其余线程的状态查询仍然正确");
}

/* ============================================================================
 * 测试入口
 * ============================================================================ */

void test_thread(void)
{
    test_thread_single();
    test_thread_many();
    test_thread_unique_ids();
    test_thread_wait_timeout();
    test_thread_invalid();
    test_thread_lock();
    test_thread_reentrant_query();
    test_thread_churn();
    test_thread_gate_recycle();
}

