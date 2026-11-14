/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 动态数组内核实现。仅负责字节级的数据搬运与容量管理，
 *               不感知元素的实际语义；align 决定定位/对齐粒度，
 *               size 决定每次操作读写的字节数。详见 docs/cc_dyn_core.md。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"

#include <string.h>

/*
 * ============================================================================
 * 内部工具
 * ============================================================================
 */

// 向上对齐到 align 的倍数（align 保证非 0）
static inline CCUINT32 _dyn_round_up_(CCUINT32 value, CCUINT32 align)
{
    CCUINT32 rem = value % align;
    return rem ? (value + align - rem) : value;
}

// 向下对齐到 align 的倍数（align 保证非 0）
static inline CCUINT32 _dyn_round_down_(CCUINT32 value, CCUINT32 align)
{
    return value - value % align;
}

// 统一参数校验：成功返回 0，参数非法返回 -1
static inline CCINT32 _dyn_check_args_(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data)
{
    if (!dyn || align == 0 || size < 0)
        return -1;
    if (size > 0 && !data)
        return -1;
    return 0;
}

// 确保容量不小于 need；成功返回 0，内存不足返回 1
static CCINT32 _dyn_ensure_capacity_(CCDyn *dyn, CCUINT32 need)
{
    if (need <= dyn->capacity)
        return 0;

    // 翻倍扩容，至少从一个基础粒度起步
    CCUINT64 new_cap = dyn->capacity ? dyn->capacity : 8;
    while (new_cap < need)
        new_cap <<= 1;
    if (new_cap > 0xFFFFFFFFu)
        return 1;

    CCUINT8 *tmp = (CCUINT8*)ccalloc(NULL, new_cap,
        CC_DBG_REL_TAG("动态数组数据", NULL));
    if (!tmp)
        return 1;
    // ccalloc 不清零，未定义区域必须为 0
    memset(tmp, 0, (size_t)new_cap);
    if (dyn->array && dyn->size > 0)
        memcpy(tmp, dyn->array, dyn->size);
    if (dyn->array)
        ccalloc(dyn->array, 0, NULL);

    dyn->array = tmp;
    dyn->capacity = (CCUINT32)new_cap;
    return 0;
}

// 滞回缩容：使用量不足容量 1/4 时容量减半；容量不大于对齐字节数时不再缩容。
// 对齐字节数取 ccalloc 的 CC_MEM_ALIGN_MIN，便于潜在的统一修改。
static void _dyn_maybe_shrink_(CCDyn *dyn)
{
    if (dyn->capacity <= (CCUINT32)CC_MEM_ALIGN_MIN)
        return;
    // 使用量不小于容量的 1/4 时保持不动，与“满了翻倍”的扩容策略形成滞回区间
    if (dyn->size >= (dyn->capacity >> 2))
        return;

    CCUINT32 new_cap = dyn->capacity >> 1;
    if (new_cap < (CCUINT32)CC_MEM_ALIGN_MIN)
        new_cap = (CCUINT32)CC_MEM_ALIGN_MIN;

    // realloc 语义原地缩容（指针不变），失败时保持原状
    void *tmp = ccalloc(dyn->array, new_cap,
        CC_DBG_REL_TAG("动态数组数据", NULL));
    if (tmp)
    {
        dyn->array = tmp;
        dyn->capacity = new_cap;
    }
}

// 读取单个比特（LSB-first：比特低位在字节低位，低位字节下标更低）
static inline CCUINT8 _dyn_get_bit_(const CCUINT8 *buf, CCUINT64 pos)
{
    return (CCUINT8)((buf[pos >> 3] >> (pos & 7)) & 1u);
}

// 写入单个比特（LSB-first）
static inline void _dyn_set_bit_(CCUINT8 *buf, CCUINT64 pos, CCUINT8 bit)
{
    CCUINT8 mask = (CCUINT8)(1u << (pos & 7));
    if (bit)
        buf[pos >> 3] |= mask;
    else
        buf[pos >> 3] &= (CCUINT8)~mask;
}

/*
 * ============================================================================
 * 公开接口
 * ============================================================================
 */

CCINT32 cc_dyn_core_push(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data)
{
    if (_dyn_check_args_(dyn, align, size, data) != 0)
        return -1;
    if (size == 0)
        return 0;

    CCUINT32 data_size = (CCUINT32)size;
    // 末尾向上对齐，避免出现半个元素
    CCUINT32 aligned = _dyn_round_up_(dyn->size, align);
    CCUINT64 need = (CCUINT64)aligned + data_size;
    if (need > 0xFFFFFFFFu)
        return 1;
    if (_dyn_ensure_capacity_(dyn, (CCUINT32)need) != 0)
        return 1;

    CCUINT8 *base = (CCUINT8*)dyn->array;
    // 对齐产生的缝隙默认为 0
    if (aligned > dyn->size)
        memset(base + dyn->size, 0, aligned - dyn->size);
    memcpy(base + aligned, data, data_size);
    dyn->size = aligned + data_size;
    return 0;
}

CCINT32 cc_dyn_core_pop(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data)
{
    if (_dyn_check_args_(dyn, align, size, data) != 0)
        return -1;
    if (size == 0)
        return 0;
    if (dyn->size == 0)
        return 1;

    CCUINT32 data_size = (CCUINT32)size;
    // 定位到末尾元素的对齐起点
    CCUINT32 start;
    if (dyn->size % align)
        start = _dyn_round_down_(dyn->size, align);
    else
        start = dyn->size - align;

    // 从 start 读出 size 字节，超出有效数据的部分为 0
    CCUINT8 *base = (CCUINT8*)dyn->array;
    memset(data, 0, data_size);
    if (start < dyn->size)
    {
        CCUINT32 avail = dyn->size - start;
        if (avail > data_size)
            avail = data_size;
        memcpy(data, base + start, avail);
    }
    dyn->size = start;
    _dyn_maybe_shrink_(dyn);
    return 0;
}

CCINT32 cc_dyn_core_set(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index)
{
    if (_dyn_check_args_(dyn, align, size, data) != 0)
        return -1;
    if (size == 0)
        return 0;

    CCUINT32 data_size = (CCUINT32)size;
    CCUINT64 offset = (CCUINT64)align * index;
    // set 越界即失败，不自动扩容
    if (offset + data_size > dyn->size)
        return 1;

    memcpy((CCUINT8*)dyn->array + (CCUINT32)offset, data, data_size);
    return 0;
}

CCINT32 cc_dyn_core_get(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index)
{
    if (_dyn_check_args_(dyn, align, size, data) != 0)
        return -1;
    if (size == 0)
        return 0;

    CCUINT32 data_size = (CCUINT32)size;
    CCUINT64 offset = (CCUINT64)align * index;
    // get 越界即失败
    if (offset + data_size > dyn->size)
        return 1;

    CCUINT32 off = (CCUINT32)offset;
    CCUINT8 *base = (CCUINT8*)dyn->array;
    // 取出数据
    memcpy(data, base + off, data_size);
    // 删除该元素，尾部元素内存向前平移
    CCUINT32 tail_start = off + data_size;
    CCUINT32 tail_len = dyn->size - tail_start;
    if (tail_len > 0)
        memmove(base + off, base + tail_start, tail_len);
    dyn->size -= data_size;
    _dyn_maybe_shrink_(dyn);
    return 0;
}

CCINT32 cc_dyn_core_seek(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index)
{
    if (_dyn_check_args_(dyn, align, size, data) != 0)
        return -1;
    if (size == 0)
        return 0;

    CCUINT32 data_size = (CCUINT32)size;
    CCUINT64 offset = (CCUINT64)align * index;
    // 宽松查询：越界不报错，未定义数据默认为 0
    memset(data, 0, data_size);
    if (offset < dyn->size)
    {
        CCUINT32 avail = dyn->size - (CCUINT32)offset;
        if (avail > data_size)
            avail = data_size;
        memcpy(data, (CCUINT8*)dyn->array + (CCUINT32)offset, avail);
    }
    return 0;
}

CCINT32 cc_dyn_core_bitin(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len)
{
    if (!dyn || !data)
        return -1;
    if (len == 0)
        return 0;

    CCUINT64 old_bits = (CCUINT64)dyn->size * 8;
    // 插入点必须在已定义比特范围内（末尾可追加）
    if ((CCUINT64)begin > old_bits)
        return 1;
    CCUINT64 new_bits = old_bits + len;
    CCUINT64 new_bytes = (new_bits + 7) >> 3;
    if (new_bytes > 0xFFFFFFFFu)
        return 1;
    if (_dyn_ensure_capacity_(dyn, (CCUINT32)new_bytes) != 0)
        return 1;

    CCUINT8 *base = (CCUINT8*)dyn->array;
    // 自后向前，把 [begin, old_bits) 的比特整体后移 len 位
    for (CCUINT64 i = old_bits; i > (CCUINT64)begin; i--)
        _dyn_set_bit_(base, i + len - 1, _dyn_get_bit_(base, i - 1));
    // 把 data 的前 len 个比特写入 [begin, begin + len)
    const CCUINT8 *src = (const CCUINT8*)data;
    for (CCUINT64 i = 0; i < len; i++)
        _dyn_set_bit_(base, (CCUINT64)begin + i, _dyn_get_bit_(src, i));
    dyn->size = (CCUINT32)new_bytes;
    return 0;
}

CCINT32 cc_dyn_core_bitout(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len)
{
    if (!dyn || !data)
        return -1;
    if (len == 0)
        return 0;

    CCUINT64 total_bits = (CCUINT64)dyn->size * 8;
    // 读取范围超出已定义比特即失败
    if ((CCUINT64)begin + len > total_bits)
        return 1;

    CCUINT8 *dst = (CCUINT8*)data;
    const CCUINT8 *base = (const CCUINT8*)dyn->array;
    memset(dst, 0, (size_t)(((CCUINT64)len + 7) >> 3));
    for (CCUINT64 i = 0; i < len; i++)
        _dyn_set_bit_(dst, i, _dyn_get_bit_(base, (CCUINT64)begin + i));
    return 0;
}
