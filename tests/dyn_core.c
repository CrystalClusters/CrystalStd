/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 动态数组内核（cc_dyn_core）的测试。
 *               测试侧直接持有 CCDyn，仅通过内核接口操作字节，
 *               验证对齐、越界、扩容、比特序等行为。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"
#include <cc_dyn.h>
#include <ccalloc.h>
#include <string.h>

/* ============================================================================
 * 断言与辅助
 * ============================================================================ */

#define dyn_expect(cond, name) CC_CHECK((cond), (name))

// 结构体由用户直接持有，初始为全 0
static CCDyn dyn_make(void)
{
    CCDyn d;
    d.array = NULL;
    d.size = 0;
    d.capacity = 0;
    return d;
}

// 数据由 ccalloc 分配，测试结束须归还，避免泄漏报告
static void dyn_release(CCDyn *d)
{
    if (d->array)
        ccalloc(d->array, 0, NULL);
    d->array = NULL;
    d->size = 0;
    d->capacity = 0;
}

// 逐字节比对有效数据
static CCBOOL dyn_eq(const CCDyn *d, const CCUINT8 *expect, CCUINT32 n)
{
    if (d->size < n)
        return CCFALSE;
    return memcmp(d->array, expect, n) == 0;
}

/* ============================================================================
 * 用例
 * ============================================================================ */

static void test_dyn_empty(void)
{
    CC_CASE("DYN-EMPTY-001", "空数组边界");
    CCDyn d = dyn_make();
    CCUINT8 buf[4] = {0};

    dyn_expect(cc_dyn_core_pop(&d, 1, 1, buf) == 1, "空数组 pop 返回 1");
    dyn_expect(cc_dyn_core_get(&d, 1, 1, buf, 0) == 1, "空数组 get 越界返回 1");
    dyn_expect(cc_dyn_core_set(&d, 1, 1, buf, 0) == 1, "空数组 set 越界返回 1");
    dyn_expect(cc_dyn_core_seek(&d, 1, 1, buf, 0) == 0, "空数组 seek 宽松返回 0");
    dyn_expect(buf[0] == 0, "空数组 seek 填充 0");

    dyn_release(&d);
}

static void test_dyn_params(void)
{
    CC_CASE("DYN-PARAM-001", "非法参数不改变结构");
    CCDyn d = dyn_make();
    CCUINT8 v = 1;
    CCUINT8 out = 0;

    dyn_expect(cc_dyn_core_push(NULL, 1, 1, &v) == -1, "push 空结构体返回 -1");
    dyn_expect(cc_dyn_core_push(&d, 0, 1, &v) == -1, "push align 为 0 返回 -1");
    dyn_expect(cc_dyn_core_push(&d, 1, -1, &v) == -1, "push size 为负返回 -1");
    dyn_expect(cc_dyn_core_push(&d, 1, 1, NULL) == -1, "push data 为空返回 -1");
    dyn_expect(cc_dyn_core_pop(&d, 0, 1, &out) == -1, "pop align 为 0 返回 -1");
    dyn_expect(cc_dyn_core_get(&d, 1, 1, NULL, 0) == -1, "get data 为空返回 -1");
    dyn_expect(cc_dyn_core_set(&d, 1, 1, NULL, 0) == -1, "set data 为空返回 -1");
    dyn_expect(cc_dyn_core_seek(&d, 1, 1, NULL, 0) == -1, "seek data 为空返回 -1");
    dyn_expect(cc_dyn_core_bitin(NULL, &v, 0, 1) == -1, "bitin 空结构体返回 -1");
    dyn_expect(cc_dyn_core_bitout(&d, NULL, 0, 1) == -1, "bitout data 为空返回 -1");

    dyn_expect(d.array == NULL && d.size == 0, "非法参数不改变结构体");
    dyn_release(&d);
}

static void test_dyn_push_pop_basic(void)
{
    CC_CASE("DYN-PUSH-001", "基本 push/pop");
    CCDyn d = dyn_make();
    CCUINT8 in[5] = {10, 20, 30, 40, 50};
    CCUINT8 expect[5] = {10, 20, 30, 40, 50};
    CCUINT8 out = 0;

    for (CCUINT32 i = 0; i < 5; i++)
        cc_dyn_core_push(&d, 1, 1, &in[i]);
    dyn_expect(d.size == 5, "push 五个字节后 size 为 5");
    dyn_expect(dyn_eq(&d, expect, 5), "push 后内容正确");

    dyn_expect(cc_dyn_core_pop(&d, 1, 1, &out) == 0 && out == 50, "pop 取回最后字节 50");
    dyn_expect(d.size == 4, "pop 后 size 为 4");
    dyn_expect(cc_dyn_core_pop(&d, 1, 1, &out) == 0 && out == 40, "再次 pop 取回 40");
    dyn_expect(d.size == 3, "两次 pop 后 size 为 3");

    dyn_release(&d);
}

static void test_dyn_push_align(void)
{
    CC_CASE("DYN-ALIGN-001", "改变对齐时 push");
    CCDyn d = dyn_make();
    CCUINT8 a[3] = {0x11, 0x22, 0x33};
    CCUINT8 b[4] = {0x44, 0x55, 0x66, 0x77};
    CCUINT8 expect[8] = {0x11, 0x22, 0x33, 0x00, 0x44, 0x55, 0x66, 0x77};

    for (CCUINT32 i = 0; i < 3; i++)
        cc_dyn_core_push(&d, 1, 1, &a[i]);
    dyn_expect(d.size == 3, "对齐 1 插入三元素 size 为 3");

    dyn_expect(cc_dyn_core_push(&d, 4, 4, b) == 0, "对齐 4 插入一个元素成功");
    dyn_expect(d.size == 8, "改对齐 4 插入后 size 为 8");
    dyn_expect(dyn_eq(&d, expect, 8), "第 4 字节为 0，后续为新数据");

    dyn_release(&d);
}

static void test_dyn_pop_align(void)
{
    CC_CASE("DYN-ALIGN-002", "改变对齐时 pop");
    CCDyn d = dyn_make();
    CCUINT8 a[3] = {1, 2, 3};
    CCUINT8 out[4] = {0xff, 0xff, 0xff, 0xff};

    for (CCUINT32 i = 0; i < 3; i++)
        cc_dyn_core_push(&d, 1, 1, &a[i]);
    dyn_expect(d.size == 3, "对齐 1 插入三元素 size 为 3");

    dyn_expect(cc_dyn_core_pop(&d, 4, 4, out) == 0, "对齐 4 弹出成功");
    dyn_expect(out[0] == 1 && out[1] == 2 && out[2] == 3, "弹出结果前三个字节为原数据");
    dyn_expect(out[3] == 0, "弹出结果第四个字节为 0");
    dyn_expect(d.size == 0, "对齐 4 弹出后 size 归零");

    dyn_release(&d);
}

static void test_dyn_set_get_seek(void)
{
    CC_CASE("DYN-SEEK-001", "set/get/seek 基本语义");
    CCDyn d = dyn_make();
    CCUINT8 init[4][4] = {
        {0, 1, 2, 3}, {10, 11, 12, 13}, {20, 21, 22, 23}, {30, 31, 32, 33}
    };
    CCUINT8 rep[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    CCUINT8 out[4] = {0};

    for (CCUINT32 i = 0; i < 4; i++)
        cc_dyn_core_push(&d, 4, 4, init[i]);
    dyn_expect(d.size == 16, "四个 4 字节元素 size 为 16");

    // set 下标 1
    dyn_expect(cc_dyn_core_set(&d, 4, 4, rep, 1) == 0, "set 下标 1 成功");
    dyn_expect(memcmp((CCUINT8*)d.array + 4, rep, 4) == 0, "set 后下标 1 内容已替换");

    // get 下标 1（取回并删除）
    dyn_expect(cc_dyn_core_get(&d, 4, 4, out, 1) == 0, "get 下标 1 成功");
    dyn_expect(memcmp(out, rep, 4) == 0, "get 取回下标 1 的内容");
    dyn_expect(d.size == 12, "get 后 size 减为 12");

    // get 删除后，原下标 2 前移到下标 1
    CCUINT8 out2[4] = {0};
    dyn_expect(cc_dyn_core_seek(&d, 4, 4, out2, 1) == 0, "seek 前移后的下标 1 成功");
    dyn_expect(memcmp(out2, init[2], 4) == 0, "尾部元素前移正确");
    dyn_expect(d.size == 12, "seek 不改变 size");

    dyn_release(&d);
}

static void test_dyn_get_shift(void)
{
    CC_CASE("DYN-SHIFT-001", "取走中间元素后的前移");
    CCDyn d = dyn_make();
    for (CCUINT32 i = 0; i < 5; i++)
    {
        CCUINT8 v = (CCUINT8)i;
        cc_dyn_core_push(&d, 1, 1, &v);
    }
    CCUINT8 out = 0xff;
    dyn_expect(cc_dyn_core_get(&d, 1, 1, &out, 2) == 0 && out == 2, "get 中间下标 2 取回 2");
    CCUINT8 expect[4] = {0, 1, 3, 4};
    dyn_expect(d.size == 4, "取走中间元素后 size 为 4");
    dyn_expect(dyn_eq(&d, expect, 4), "[0,1,2,3,4] 取走 2 后为 [0,1,3,4]");

    dyn_release(&d);
}

static void test_dyn_get_multi(void)
{
    CC_CASE("DYN-MULTI-001", "一次取走多个连续元素");
    CCDyn d = dyn_make();
    for (CCUINT32 i = 0; i < 8; i++)
    {
        CCUINT8 v = (CCUINT8)i;
        cc_dyn_core_push(&d, 1, 1, &v);
    }
    CCUINT8 out[4] = {0};
    // 假装 4 个元素是一个元素：size = 1 * 4
    dyn_expect(cc_dyn_core_get(&d, 1, 4, out, 0) == 0, "size*N 一次取走 4 个连续元素成功");
    dyn_expect(out[0] == 0 && out[1] == 1 && out[2] == 2 && out[3] == 3, "取回的 4 个字节正确");
    CCUINT8 expect[4] = {4, 5, 6, 7};
    dyn_expect(d.size == 4, "取走 4 个元素后 size 为 4");
    dyn_expect(dyn_eq(&d, expect, 4), "剩余元素为 [4,5,6,7]");

    dyn_release(&d);
}

static void test_dyn_oob(void)
{
    CC_CASE("DYN-OOB-001", "set/get 越界");
    CCDyn d = dyn_make();
    for (CCUINT32 i = 0; i < 3; i++)
    {
        CCUINT8 v = (CCUINT8)i;
        cc_dyn_core_push(&d, 1, 1, &v);
    }
    CCUINT8 v = 0x99;
    CCUINT8 out = 0x99;

    dyn_expect(cc_dyn_core_set(&d, 1, 1, &v, 5) == 1, "set 下标 5 越界失败");
    dyn_expect(cc_dyn_core_get(&d, 1, 1, &out, 5) == 1, "get 下标 5 越界失败");
    dyn_expect(cc_dyn_core_set(&d, 1, 1, &v, 3) == 1, "set 下标 3（等于 size）越界失败");
    dyn_expect(d.size == 3, "越界操作不改变 size");
    dyn_expect(cc_dyn_core_set(&d, 1, 1, &v, 2) == 0, "set 最后一个下标 2 成功");

    dyn_release(&d);
}

static void test_dyn_seek_oob(void)
{
    CC_CASE("DYN-OOB-002", "seek 越界");
    CCDyn d = dyn_make();
    for (CCUINT32 i = 0; i < 3; i++)
    {
        CCUINT8 v = (CCUINT8)(i + 1);
        cc_dyn_core_push(&d, 1, 1, &v);
    }
    CCUINT8 out[4] = {0xEE, 0xEE, 0xEE, 0xEE};

    dyn_expect(cc_dyn_core_seek(&d, 1, 1, out, 5) == 0, "seek 越界返回 0 不报错");
    dyn_expect(out[0] == 0, "seek 越界填充 0");
    dyn_expect(d.size == 3, "seek 不改变 size");

    // 部分越界：从下标 2 读 4 字节，仅首字节有效
    CCUINT8 part[4] = {0xEE, 0xEE, 0xEE, 0xEE};
    dyn_expect(cc_dyn_core_seek(&d, 1, 4, part, 2) == 0, "seek 部分越界返回 0");
    dyn_expect(part[0] == 3 && part[1] == 0 && part[2] == 0 && part[3] == 0, "部分越界有效部分读取、其余为 0");

    dyn_release(&d);
}

static void test_dyn_grow(void)
{
    CC_CASE("DYN-GROW-001", "连续 push 触发扩容");
    CCDyn d = dyn_make();
    CCUINT32 n = 100;
    CCBOOL ok = CCTRUE;
    for (CCUINT32 i = 0; i < n; i++)
    {
        CCUINT32 v = i * 3u + 1u;
        if (cc_dyn_core_push(&d, 4, 4, &v) != 0)
        {
            ok = CCFALSE;
            break;
        }
    }
    dyn_expect(ok, "连续 push 100 个 4 字节元素成功");
    dyn_expect(d.size == n * 4, "扩容后 size 正确");
    dyn_expect(d.capacity >= d.size, "capacity 不小于 size");
    for (CCUINT32 i = 0; i < n; i++)
    {
        CCUINT32 v = 0;
        if (cc_dyn_core_seek(&d, 4, 4, &v, i) != 0 || v != i * 3u + 1u)
        {
            ok = CCFALSE;
            break;
        }
    }
    dyn_expect(ok, "扩容后全部元素内容保持正确");

    dyn_release(&d);
}

static void test_dyn_shrink(void)
{
    CC_CASE("DYN-SHRINK-001", "pop 触发缩容");
    CCDyn d = dyn_make();
    CCUINT32 n = 200;   // 默认 CC_MEM_ALIGN_MIN=64 时，容量翻倍增长到 256
    CCBOOL ok = CCTRUE;

    for (CCUINT32 i = 0; i < n; i++)
    {
        CCUINT8 v = (CCUINT8)(i & 0xFF);
        if (cc_dyn_core_push(&d, 1, 1, &v) != 0)
        {
            ok = CCFALSE;
            break;
        }
    }
    dyn_expect(ok && d.size == n, "缩容测试：push 200 字节成功");
    dyn_expect(d.capacity == 256, "缩容测试：容量按翻倍增长到 256");

    // pop 到 size=63（跌破 256/4=64），容量应减半到 128
    while (d.size > 63)
    {
        CCUINT8 out = 0;
        cc_dyn_core_pop(&d, 1, 1, &out);
    }
    dyn_expect(d.size == 63, "缩容测试：pop 到 size=63");
    dyn_expect(d.capacity == 128, "使用量跌破 1/4 时容量减半到 128");

    // 缩容后有效数据（前 size 字节）应完整保留
    CCBOOL data_ok = CCTRUE;
    for (CCUINT32 i = 0; i < d.size; i++)
    {
        CCUINT8 chk = 0xFF;
        if (cc_dyn_core_seek(&d, 1, 1, &chk, i) != 0 || chk != (CCUINT8)(i & 0xFF))
        {
            data_ok = CCFALSE;
            break;
        }
    }
    dyn_expect(data_ok, "缩容后有效数据完整");

    // pop 到 size=31（跌破 128/4=32），容量应再减半到 64
    while (d.size > 31)
    {
        CCUINT8 out = 0;
        cc_dyn_core_pop(&d, 1, 1, &out);
    }
    dyn_expect(d.size == 31, "缩容测试：pop 到 size=31");
    dyn_expect(d.capacity == 64, "再次跌破 1/4 时容量减半到 64");

    // pop 到 size=15（已跌破 64/4=16），但 capacity <= CC_MEM_ALIGN_MIN，不再缩容
    while (d.size > 15)
    {
        CCUINT8 out = 0;
        cc_dyn_core_pop(&d, 1, 1, &out);
    }
    dyn_expect(d.size == 15, "缩容测试：pop 到 size=15");
    dyn_expect(d.capacity == CC_MEM_ALIGN_MIN, "容量不大于对齐字节数时不再缩容");

    // 全部弹出后容量保持下限
    while (d.size > 0)
    {
        CCUINT8 out = 0;
        cc_dyn_core_pop(&d, 1, 1, &out);
    }
    dyn_expect(d.size == 0, "缩容测试：全部弹出后 size 为 0");
    dyn_expect(d.capacity == CC_MEM_ALIGN_MIN, "全部弹出后容量保持对齐字节数下限");

    dyn_release(&d);
}

static void test_dyn_shrink_get(void)
{
    CC_CASE("DYN-SHRINK-002", "get 触发缩容");
    CCDyn d = dyn_make();
    CCUINT32 n = 200;
    CCBOOL ok = CCTRUE;

    for (CCUINT32 i = 0; i < n; i++)
    {
        CCUINT8 v = (CCUINT8)(i & 0xFF);
        if (cc_dyn_core_push(&d, 1, 1, &v) != 0)
        {
            ok = CCFALSE;
            break;
        }
    }
    dyn_expect(ok && d.capacity == 256, "get 缩容：push 后容量为 256");

    // 从下标 0 反复 get 删除，size 递减同样应触发缩容
    while (d.size > 63)
    {
        CCUINT8 out = 0;
        cc_dyn_core_get(&d, 1, 1, &out, 0);
    }
    dyn_expect(d.size == 63, "get 缩容：删除到 size=63");
    dyn_expect(d.capacity == 128, "get 删除使使用量跌破 1/4 时容量减半到 128");

    while (d.size > 31)
    {
        CCUINT8 out = 0;
        cc_dyn_core_get(&d, 1, 1, &out, 0);
    }
    dyn_expect(d.size == 31, "get 缩容：删除到 size=31");
    dyn_expect(d.capacity == 64, "get 删除再次跌破 1/4 时容量减半到 64");

    dyn_release(&d);
}

static void test_dyn_bits_lsb(void)
{
    CC_CASE("DYN-BITS-001", "LSB-first 比特写入/读取");
    CCDyn d = dyn_make();
    CCUINT8 src = 0x05;     // 比特 0=1, 1=0, 2=1
    dyn_expect(cc_dyn_core_bitin(&d, &src, 0, 3) == 0, "bitin 3 比特成功");
    dyn_expect(d.size == 1, "3 比特占 1 字节");
    dyn_expect(((CCUINT8*)d.array)[0] == 0x05, "LSB-first：低位比特在字节低位");

    CCUINT8 dst = 0;
    dyn_expect(cc_dyn_core_bitout(&d, &dst, 0, 3) == 0, "bitout 3 比特成功");
    dyn_expect((dst & 0x07) == 0x05, "bitout 取回的 3 比特与写入一致");

    // 追加到末尾后跨到第二个字节
    CCUINT8 one = 0x01;
    dyn_expect(cc_dyn_core_bitin(&d, &one, 8, 1) == 0, "在末尾追加 1 比特成功");
    dyn_expect(d.size == 2, "跨字节后 size 为 2");
    dyn_expect(((CCUINT8*)d.array)[1] == 0x01, "第二字节的 bit0 为 1");

    dyn_release(&d);
}

static void test_dyn_bits_insert(void)
{
    CC_CASE("DYN-BITS-002", "中间插入比特");
    CCDyn d = dyn_make();
    CCUINT8 ones = 0xFF;
    dyn_expect(cc_dyn_core_bitin(&d, &ones, 0, 8) == 0, "bitin 8 个 1 成功");
    dyn_expect(d.size == 1 && ((CCUINT8*)d.array)[0] == 0xFF, "8 比特全 1 内容正确");

    // 在 begin=4 处插入 4 个 0 比特，把高位整体后移
    CCUINT8 zeros = 0x00;
    dyn_expect(cc_dyn_core_bitin(&d, &zeros, 4, 4) == 0, "中间插入 4 个 0 比特成功");
    dyn_expect(d.size == 2, "插入 4 比特后 size 为 2");
    dyn_expect(((CCUINT8*)d.array)[0] == 0x0F, "低半字节保留 1，高半字节为插入的 0");
    dyn_expect(((CCUINT8*)d.array)[1] == 0x0F, "被后移的高 4 位 1 落在第二字节低半字节");

    CCUINT8 out[2] = {0};
    dyn_expect(cc_dyn_core_bitout(&d, out, 0, 12) == 0, "bitout 12 比特成功");
    dyn_expect(out[0] == 0x0F && out[1] == 0x0F, "bitout 12 比特内容正确");

    dyn_release(&d);
}

static void test_dyn_bits_oob(void)
{
    CC_CASE("DYN-BITS-003", "比特越界");
    CCDyn d = dyn_make();
    CCUINT8 v = 0x01;
    dyn_expect(cc_dyn_core_bitin(&d, &v, 0, 0) == 0, "bitin len 为 0 返回 0");
    dyn_expect(cc_dyn_core_bitout(&d, &v, 0, 0) == 0, "bitout len 为 0 返回 0");
    dyn_expect(cc_dyn_core_bitin(&d, &v, 1, 1) == 1, "bitin 起点超过已定义比特失败");

    CCUINT8 ones = 0xFF;
    cc_dyn_core_bitin(&d, &ones, 0, 8);
    dyn_expect(cc_dyn_core_bitout(&d, &v, 7, 2) == 1, "bitout 读取范围越界失败");
    dyn_expect(cc_dyn_core_bitout(&d, &v, 0, 8) == 0, "bitout 读完整个字节成功");

    dyn_release(&d);
}

/* ============================================================================
 * 入口
 * ============================================================================ */

void test_dyn_core(void)
{
    test_dyn_empty();
    test_dyn_params();
    test_dyn_push_pop_basic();
    test_dyn_push_align();
    test_dyn_pop_align();
    test_dyn_set_get_seek();
    test_dyn_get_shift();
    test_dyn_get_multi();
    test_dyn_oob();
    test_dyn_seek_oob();
    test_dyn_grow();
    test_dyn_shrink();
    test_dyn_shrink_get();
    test_dyn_bits_lsb();
    test_dyn_bits_insert();
    test_dyn_bits_oob();
}
