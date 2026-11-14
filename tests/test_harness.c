/*
 * @Description: 测试用例状态、统计与控制台失败输出。
 *               不维护用例注册表；用例编号只在测试代码的 CC_CASE 中显式出现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CC_TEST_NAME_MAX 192
#define CC_TEST_DETAIL_MAX 512

static const char *g_build = "debug";
static const char *g_level = "quick";
static CCUINT32 g_seed = 0;

static CCINT32 g_pass = 0;
static CCINT32 g_fail = 0;
static CCINT32 g_skip = 0;
static CCINT32 g_manual = 0;
static CCUINT32 g_checks = 0;

static CCINT32 g_suite_pass = 0;
static CCINT32 g_suite_fail = 0;
static CCINT32 g_suite_skip = 0;
static CCINT32 g_suite_manual = 0;
static CCUINT32 g_suite_checks = 0;

static char g_suite[64] = "TEST";

static CCBOOL g_case_active = CCFALSE;
static char g_case_id[64] = "";
static char g_case_name[CC_TEST_NAME_MAX] = "";
static int g_case_checks = 0;
static int g_case_failed = 0;

static CCUINT32 g_harness_errors = 0;

static void harness_error(const char *text)
{
    g_harness_errors++;
    printf("[HARNESS][FAIL] %s\n", text ? text : "");
    fflush(stdout);
}

static void format_detail(char *out, CCUINT32 cap, const char *label,
                          const char *expected, const char *actual)
{
    const char *name = (label && *label) ? label : "(unnamed)";
    if (expected && actual)
        snprintf(out, cap, "%s: expected %s, actual %s", name, expected, actual);
    else if (expected)
        snprintf(out, cap, "%s: expected %s", name, expected);
    else if (actual)
        snprintf(out, cap, "%s: actual %s", name, actual);
    else
        snprintf(out, cap, "%s", name);
}

void cc_test_begin(const char *build, const char *level, CCUINT32 seed)
{
    g_build = (build && *build) ? build : "debug";
    g_level = (level && *level) ? level : "quick";
    g_seed = seed;

    g_pass = 0;
    g_fail = 0;
    g_skip = 0;
    g_manual = 0;
    g_checks = 0;
    g_suite_pass = 0;
    g_suite_fail = 0;
    g_suite_skip = 0;
    g_suite_manual = 0;
    g_suite_checks = 0;
    g_case_active = CCFALSE;
    g_harness_errors = 0;

    printf("== CrystalStd test ==\n");
    printf("build=%s level=%s seed=%u\n", g_build, g_level, (unsigned)g_seed);
}

void cc_test_suite_begin(const char *suite, const char *title)
{
    if (g_case_active) cc_test_case_end();

    snprintf(g_suite, sizeof(g_suite), "%s", (suite && *suite) ? suite : "TEST");
    g_suite_pass = 0;
    g_suite_fail = 0;
    g_suite_skip = 0;
    g_suite_manual = 0;
    g_suite_checks = 0;

    printf("\n== %s (%s) ==\n", (title && *title) ? title : g_suite, g_suite);
    fflush(stdout);
}

void cc_test_suite_end(void)
{
    if (g_case_active) cc_test_case_end();

    printf("[%s] total=%d passed=%d failed=%d skipped=%d manual=%d checks=%u\n",
           g_suite,
           g_suite_pass + g_suite_fail + g_suite_skip,
           g_suite_pass, g_suite_fail, g_suite_skip, g_suite_manual,
           g_suite_checks);
    fflush(stdout);
}

void cc_test_case_begin(const char *id, const char *name,
                        const char *file, int line)
{
    if (g_case_active) cc_test_case_end();

    if (!id || !*id)
    {
        harness_error("用例编号为空");
        return;
    }

    snprintf(g_case_id, sizeof(g_case_id), "%s", id);
    snprintf(g_case_name, sizeof(g_case_name), "%s",
             (name && *name) ? name : id);
    g_case_checks = 0;
    g_case_failed = 0;
    g_case_active = CCTRUE;
    (void)file;
    (void)line;
}

void cc_test_check(CCBOOL condition, const char *label,
                   const char *expected, const char *actual,
                   const char *file, int line)
{
    (void)file;
    (void)line;

    if (!g_case_active)
    {
        harness_error("CC_CHECK 出现在 CC_CASE 之外");
        return;
    }

    g_case_checks++;
    g_suite_checks++;
    g_checks++;
    if (condition) return;

    g_case_failed++;
    char detail[CC_TEST_DETAIL_MAX];
    format_detail(detail, sizeof(detail), label, expected, actual);
    printf("[%s][FAIL] %s\n", g_case_id, detail);
    fflush(stdout);
}

void cc_test_case_end(void)
{
    if (!g_case_active) return;
    g_case_active = CCFALSE;

    if (g_case_failed > 0)
    {
        g_suite_fail++;
        g_fail++;
        printf("[%s][FAIL] %s (%d checks, %d failed)\n",
               g_case_id, g_case_name, g_case_checks, g_case_failed);
    }
    else if (g_case_checks == 0)
    {
        g_suite_fail++;
        g_fail++;
        printf("[%s][FAIL] %s (no checks)\n", g_case_id, g_case_name);
    }
    else
    {
        g_suite_pass++;
        g_pass++;
        printf("[%s][PASS] %s (%d checks)\n",
               g_case_id, g_case_name, g_case_checks);
    }
    fflush(stdout);
}

void cc_test_case_skip(const char *reason)
{
    if (!g_case_active)
    {
        harness_error("CC_SKIP 出现在 CC_CASE 之外");
        return;
    }
    g_case_active = CCFALSE;
    g_suite_skip++;
    g_skip++;

    printf("[%s][SKIP] %s%s%s\n",
           g_case_id, g_case_name,
           (reason && *reason) ? ": " : "",
           (reason && *reason) ? reason : "");
    fflush(stdout);
}

void cc_test_manual(const char *id, const char *title,
                    const char *file, int line)
{
    (void)line;
    if (!id || !*id)
    {
        harness_error("人工用例编号为空");
        return;
    }

    (void)file;
    g_suite_manual++;
    g_manual++;
    printf("[%s][MANUAL] %s\n", id, (title && *title) ? title : "");
    fflush(stdout);
}

void cc_test_print_summary(void)
{
    printf("\n# SUMMARY total=%d pass=%d fail=%d skip=%d manual=%d checks=%u harness_errors=%u\n",
           g_pass + g_fail + g_skip, g_pass, g_fail, g_skip, g_manual,
           (unsigned)g_checks, (unsigned)g_harness_errors);
    fflush(stdout);
}

CCINT32 cc_test_failures(void) { return g_fail; }
CCINT32 cc_test_suite_failures(void) { return g_suite_fail; }
CCUINT32 cc_test_harness_errors(void) { return g_harness_errors; }
CCBOOL cc_test_level_full(void)
{
    return (g_level && strcmp(g_level, "full") == 0) ? CCTRUE : CCFALSE;
}
CCUINT32 cc_test_seed(void) { return g_seed; }
