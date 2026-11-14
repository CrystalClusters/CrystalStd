/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 针对基础功能组件的测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

/* ============================================================================
 * atoi
 * ============================================================================ */

static void atoi_check(const char *name, const char *input, CCINT32 expected)
{
    CC_CHECK_EQ_I64(name, expected, cc_atoi(input));
}

static void case_atoi_valid(void)
{
    CC_CASE("BASIC-ATOI-001", "合法整数解析");
    atoi_check("zero", "0", 0);
    atoi_check("positive", "123", 123);
    atoi_check("negative", "-123", -123);
    atoi_check("lead zeros", "000123", 123);
    atoi_check("int32 max", "2147483647", 2147483647);
    atoi_check("int32 min", "-2147483648", -2147483648);
    atoi_check("negative zero", "-0", 0);
}

static void case_atoi_invalid(void)
{
    CC_CASE("BASIC-ATOI-002", "非法输入");
    atoi_check("null", NULL, 0);
    atoi_check("empty", "", 0);
    atoi_check("plus sign", "+123", 0);
    atoi_check("mixed", "12a3", 0);
    atoi_check("lead space", " 123", 0);
    atoi_check("only minus", "-", 0);
}

static void case_atoi_overflow(void)
{
    CC_CASE("BASIC-ATOI-003", "溢出饱和与长前导零");
    atoi_check("int32 max+1", "2147483648", 2147483647);
    atoi_check("int32 min-1", "-2147483649", -2147483648);
    atoi_check("overflow 10^19", "10000000000000000000", 2147483647);
    atoi_check("underflow -10^19", "-10000000000000000000", -2147483648);
    atoi_check("long leading zeros", "0000000000000000000000000001", 1);
    atoi_check("long leading zeros negative",
               "-0000000000000000000000000005", -5);
}

/* ============================================================================
 * cctime / ccsleep / ccdate
 * ============================================================================ */

static void case_time(void)
{
    CC_CASE("BASIC-TIME-001", "时间、休眠与日期字段");

    CC_CHECK(cctime() >= 0.0, "cctime non-negative");
    double t1 = cctime();
    double t2 = cctime();
    CC_CHECK(t2 >= t1, "cctime monotonic");

    double start = cctime();
    ccsleep(0);
    double elapsed = cctime() - start;
    CC_CHECK(elapsed >= 0.0 && elapsed < 0.05, "ccsleep(0) returns quickly");

    start = cctime();
    ccsleep(1000);
    elapsed = cctime() - start;
    CC_CHECK(elapsed >= 0.0009 && elapsed < 0.1,
             "ccsleep(1000us) sleeps >=1ms");

    start = cctime();
    ccsleep(100000);
    elapsed = cctime() - start;
    CC_CHECK(elapsed >= 0.09 && elapsed < 1.0,
             "ccsleep(100000us) sleeps >=100ms");

    CCDate date = ccdate();
    CC_CHECK(date.year >= 2000 && date.year <= 9999,
             "ccdate year in [2000,9999]");
    CC_CHECK(date.month >= 1 && date.month <= 12,
             "ccdate month in [1,12]");
    CC_CHECK(date.day >= 1 && date.day <= 31,
             "ccdate day in [1,31]");
    CC_CHECK(date.dweek >= 0 && date.dweek <= 6,
             "ccdate dweek in [0,6]");
    CC_CHECK(date.hour >= 0 && date.hour <= 23,
             "ccdate hour in [0,23]");
    CC_CHECK(date.min >= 0 && date.min <= 59,
             "ccdate min in [0,59]");
    CC_CHECK(date.sec >= 0 && date.sec <= 59,
             "ccdate sec in [0,59]");
    CC_CHECK(date.ms >= 0 && date.ms <= 999,
             "ccdate ms in [0,999]");
}

/* ============================================================================
 * CRC-32
 * ============================================================================ */

static void crc_check(const char *name, CCUINT32 expected, CCUINT32 got)
{
    CC_CHECK_EQ_U64(name, expected, got);
}

static void case_crc_vectors(void)
{
    CC_CASE("BASIC-CRC-001", "CRC-32 标准向量与续算");
    const char *s = "123456789";

    crc_check("standard \"123456789\"", 0xCBF43926u, cc_crc32(s, 9, 0));
    crc_check("single \"a\"", 0xE8B7BE43u, cc_crc32("a", 1, 0));
    crc_check("\"abc\"", 0x352441C2u, cc_crc32("abc", 3, 0));
    crc_check("pangram", 0x414FA339u,
              cc_crc32("The quick brown fox jumps over the lazy dog", 43, 0));
    crc_check("single zero byte", 0xD202EF8Du, cc_crc32("\x00", 1, 0));

    crc_check("empty -> 0", 0x00000000u, cc_crc32("", 0, 0));
    crc_check("NULL size 0 -> seed", 0x00000000u, cc_crc32(NULL, 0, 0));
    crc_check("NULL size>0 -> 0", 0x00000000u, cc_crc32(NULL, 4, 0x12345678u));
    crc_check("size 0 -> seed", 0x12345678u, cc_crc32("ignored", 0, 0x12345678u));

    crc_check("chunk 4+5", 0xCBF43926u,
              cc_crc32(s + 4, 5, cc_crc32(s, 4, 0)));
    crc_check("chunk 1+8", 0xCBF43926u,
              cc_crc32(s + 1, 8, cc_crc32(s, 1, 0)));
    crc_check("chunk 3+6", 0xCBF43926u,
              cc_crc32(s + 3, 6, cc_crc32(s, 3, 0)));
    crc_check("empty chunk keeps state", 0xCBF43926u,
              cc_crc32("", 0, cc_crc32(s, 9, 0)));
}

/* ============================================================================
 * wildcard
 * ============================================================================ */

static void wc_check(const char *name, const char *pattern,
                     const char *str, CCBOOL expected)
{
    CCBOOL got = cc_wildcard(pattern, str);
    CC_CHECK(got == expected, name);
}

static void case_wildcard(void)
{
    CC_CASE("BASIC-WILDCARD-001", "通配符匹配");

    wc_check("empty vs empty", "", "", CCTRUE);
    wc_check("empty vs text", "", "abc", CCFALSE);
    wc_check("exact", "abc", "abc", CCTRUE);
    wc_check("exact mismatch", "abc", "abd", CCFALSE);
    wc_check("case sensitive", "abc", "ABC", CCFALSE);

    wc_check("? single", "?", "a", CCTRUE);
    wc_check("? empty", "?", "", CCFALSE);
    wc_check("? multi", "??", "ab", CCTRUE);
    wc_check("? too short", "??", "a", CCFALSE);
    wc_check("mid ?", "a?c", "abc", CCTRUE);
    wc_check("mid ? too short", "a?c", "ac", CCFALSE);

    wc_check("* empty text", "*", "", CCTRUE);
    wc_check("* text", "*", "abc", CCTRUE);
    wc_check("trailing *", "a*", "abc", CCTRUE);
    wc_check("leading *", "*c", "abc", CCTRUE);
    wc_check("mid *", "a*c", "abc", CCTRUE);
    wc_check("mid * empty", "a*c", "ac", CCTRUE);
    wc_check("mid * overlong", "a*c", "abcd", CCFALSE);
    wc_check("two *", "a*b*c", "aXbYc", CCTRUE);
    wc_check("two * adjacent", "a**b", "ab", CCTRUE);
    wc_check("all *", "***", "anything", CCTRUE);

    wc_check("*abc*", "*abc*", "xxabcyy", CCTRUE);
    wc_check("*abc* miss", "*abc*", "xxabyy", CCFALSE);
    wc_check("*.c hit", "*.c", "main.c", CCTRUE);
    wc_check("*.c miss", "*.c", "main.h", CCFALSE);
    wc_check("*.* hit", "*.*", "file.txt", CCTRUE);
    wc_check("?ain.c", "?ain.c", "main.c", CCTRUE);

    wc_check("* crosses slash", "*", "a/b/c.txt", CCTRUE);
    wc_check("* around slash", "a*b", "a/b", CCTRUE);
    wc_check("? matches slash", "a?b", "a/b", CCTRUE);

    wc_check("NULL pattern", NULL, "abc", CCFALSE);
    wc_check("NULL str", "abc", NULL, CCFALSE);
    wc_check("both NULL", NULL, NULL, CCFALSE);
}

void test_basic(void)
{
    case_atoi_valid();
    case_atoi_invalid();
    case_atoi_overflow();
    case_time();
    case_crc_vectors();
    case_wildcard();
}
