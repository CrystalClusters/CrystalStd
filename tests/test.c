/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-09
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 测试入口：套件表、交互菜单、命令行参数与退出码。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <windows.h>
#define TEST_GETCWD _getcwd
#define TEST_CHDIR _chdir
#else
#include <unistd.h>
#define TEST_GETCWD getcwd
#define TEST_CHDIR chdir
#endif

/* 判断 stdin 和 stdout 是否都是真正的控制台终端；NUL、文件、管道都不算。 */
static CCBOOL inner_console_is_tty(void)
{
#ifdef CC_WINDOWS
    DWORD in_mode;
    DWORD out_mode;
    return (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &in_mode) &&
            GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &out_mode))
               ? CCTRUE : CCFALSE;
#else
    return (isatty(fileno(stdin)) && isatty(fileno(stdout))) ? CCTRUE : CCFALSE;
#endif
}

typedef struct test_suite {
    const char *name;
    const char *title;
    void (*run)(void);
} test_suite;

static const test_suite g_suites[] = {
    { "BASIC", "基础测试", test_basic },
    { "COLOR", "color_print 专项", test_color_print },
    { "RBT", "红黑树内核专项", test_rbt_core },
    { "DYN", "动态数组内核专项", test_dyn_core },
    { "ALLOC", "ccalloc 专项", test_ccalloc },
    { "THREAD", "异步组件专项", test_thread },
    { "FS", "统一文件管理专项", test_ccfiles },
};

#define SUITE_COUNT ((CCUINT32)(sizeof(g_suites) / sizeof(g_suites[0])))

static CCBOOL inner_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return CCFALSE;
    fclose(fp);
    return CCTRUE;
}

static void inner_normalize(char *path)
{
    for (char *p = path; *p; p++)
        if (*p == '\\') *p = '/';
}

/* 从 start 向上查找 tests/test.c，找到后返回项目根。 */
static CCBOOL inner_find_root(const char *start, char *out, size_t cap)
{
    char buf[1024];
    int n;

    if (!start || !*start || !out || cap == 0) return CCFALSE;
    n = snprintf(buf, sizeof(buf), "%s", start);
    if (n <= 0 || (size_t)n >= sizeof(buf)) return CCFALSE;
    inner_normalize(buf);

    for (;;)
    {
        char marker[1200];
        snprintf(marker, sizeof(marker), "%s/tests/test.c", buf);
        if (inner_file_exists(marker))
        {
            snprintf(out, cap, "%s", buf);
            return CCTRUE;
        }

        char *slash = strrchr(buf, '/');
        if (!slash) break;
        if (slash == buf)
        {
            buf[1] = '\0';
            break;
        }
        *slash = '\0';
    }
    return CCFALSE;
}

/* 切换工作目录到项目根，保证测试临时目录等相对路径稳定。 */
static void inner_init_project(void)
{
    char cwd[1024];
    char root[1024];

    if (!TEST_GETCWD(cwd, sizeof(cwd)))
        return;
    inner_normalize(cwd);
    if (inner_find_root(cwd, root, sizeof(root)))
        TEST_CHDIR(root);
}

static void inner_print_usage(void)
{
    printf("用法: test [all|manual|<套件名>] [选项]\n");
    printf("套件名: ");
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
        printf("%s%s", i ? "|" : "", g_suites[i].name);
    printf("\n");
    printf("  --level quick|full\n");
    printf("  --seed <uint32>\n");
    printf("  --help\n");
    printf("无参数时进入交互菜单。\n");
}

static void inner_print_menu(void)
{
    printf("\n请选择测试项：\n");
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
        printf("%u：%s\n", i + 1, g_suites[i].title);
    printf("a：全部自动测试\n");
    printf("m：人工确认项\n");
    printf("q：退出\n");
    printf("输入选项并回车：");
    fflush(stdout);
}

static CCBOOL inner_read_choice(char *buf, CCUINT32 size)
{
    CCUINT32 len;

    if (!fgets(buf, (int)size, stdin)) return CCFALSE;
    buf[size - 1] = '\0';
    len = (CCUINT32)strlen(buf);
    while (len > 0)
    {
        if (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
            buf[len - 1] == '\t' || buf[len - 1] == ' ')
            buf[--len] = '\0';
        else
            break;
    }

    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (p != buf) memmove(buf, p, strlen(p) + 1);
    return CCTRUE;
}

static int inner_find_suite(const char *name)
{
    if (!name || !*name) return -1;
    if (!strcmp(name, "all")) return 100;
    if (!strcmp(name, "manual")) return 101;
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
        if (!strcmp(name, g_suites[i].name)) return (int)i;
    return -1;
}

static int inner_run_one(CCUINT32 index)
{
    cc_test_suite_begin(g_suites[index].name, g_suites[index].title);
    g_suites[index].run();
    cc_test_suite_end();
    return cc_test_suite_failures() ? 1 : 0;
}

static int inner_run_all(void)
{
    int failed = 0;
    for (CCUINT32 i = 0; i < SUITE_COUNT; i++)
    {
        if (inner_run_one(i)) failed = 1;
    }
    return failed;
}

/*
 * required 为真表示用户明确要求人工项；非交互环境应报错。
 * required 为假表示全量运行顺带执行人工项；非交互环境只提示并跳过。
 */
static int inner_run_manual(CCBOOL required)
{
    if (!inner_console_is_tty())
    {
        if (required)
        {
            printf("[错误] 人工确认项需要交互终端，请在终端中直接运行。\n");
            return 2;
        }
        printf("[提示] 当前不是交互终端，人工确认项已跳过。\n");
        return 0;
    }

    test_manual();
    return 0;
}

static int inner_run_all_with_manual(void)
{
    int failed = inner_run_all();
    printf("\n");
    if (inner_run_manual(CCFALSE) == 2)
        failed = 1;
    return failed;
}

static int inner_menu(void)
{
    int any_fail = 0;

    for (;;)
    {
        char choice[64];
        inner_print_menu();
        if (!inner_read_choice(choice, sizeof(choice)))
        {
            printf("\n输入已结束，退出。\n");
            break;
        }

        if (!strcmp(choice, "q")) break;
        if (!strcmp(choice, "a") || !strcmp(choice, "all"))
        {
            if (inner_run_all_with_manual()) any_fail = 1;
            continue;
        }
        if (!strcmp(choice, "m") || !strcmp(choice, "manual"))
        {
            if (inner_run_manual(CCTRUE) == 2) any_fail = 1;
            continue;
        }

        char *end = NULL;
        unsigned long index = strtoul(choice, &end, 10);
        if (end && *end == '\0' && index >= 1 && index <= SUITE_COUNT)
        {
            if (inner_run_one((CCUINT32)(index - 1))) any_fail = 1;
            continue;
        }

        int suite = inner_find_suite(choice);
        if (suite >= 0 && suite < 100)
        {
            if (inner_run_one((CCUINT32)suite)) any_fail = 1;
            continue;
        }

        printf("[错误] 未知选项: %s\n", choice);
    }

    return any_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *suite = NULL;
    const char *level = "quick";
    CCUINT32 seed = 0;

    inner_init_project();

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--level") && i + 1 < argc)
            level = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (CCUINT32)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
        {
            inner_print_usage();
            return 0;
        }
        else if (argv[i][0] == '-')
        {
            printf("[错误] 未知选项: %s\n", argv[i]);
            inner_print_usage();
            return 2;
        }
        else if (suite)
        {
            printf("[错误] 多余的套件参数: %s\n", argv[i]);
            inner_print_usage();
            return 2;
        }
        else
        {
            suite = argv[i];
        }
    }

    if (strcmp(level, "quick") && strcmp(level, "full"))
    {
        printf("[错误] --level 只能是 quick 或 full\n");
        return 2;
    }

    if (!suite && argc == 1)
    {
#ifdef CC_RELEASE
        cc_test_begin("release", level, seed);
#else
        cc_test_begin("debug", level, seed);
#endif
        int failed = inner_menu();
        cc_test_print_summary();
        return (failed || cc_test_failures() || cc_test_harness_errors()) ? 1 : 0;
    }

    if (!suite)
        suite = "all";

    int index = inner_find_suite(suite);
    if (index < 0)
    {
        printf("[错误] 未知套件: %s\n", suite);
        inner_print_usage();
        return 2;
    }

    if (index == 101 && !inner_console_is_tty())
    {
        printf("[错误] 人工确认项需要交互终端，请在终端中直接运行。\n");
        return 2;
    }

#ifdef CC_RELEASE
    cc_test_begin("release", level, seed);
#else
    cc_test_begin("debug", level, seed);
#endif

    int failed = 0;
    if (index == 100)
        failed = inner_run_all_with_manual();
    else if (index == 101)
        failed = inner_run_manual(CCTRUE) == 2 ? 1 : 0;
    else
        failed = inner_run_one((CCUINT32)index);

    cc_test_print_summary();

    if (failed || cc_test_failures() || cc_test_harness_errors())
        return 1;
    return 0;
}
