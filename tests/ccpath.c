/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccpath 测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <ccalloc.h>
#include <string.h>

static void path_check_resolve(
    const char *label, const char *path, const char *base,
    const char *expected
)
{
    char *got = ccpath_resolve(path, base);
    CC_CHECK_EQ_STR(label, expected, got);
    if (got)
        ccalloc(got, 0, NULL);
}

static void path_check_parent(
    const char *label, const char *path, const char *expected
)
{
    char *got = ccpath_parent(path);
    CC_CHECK_EQ_STR(label, expected, got);
    if (got)
        ccalloc(got, 0, NULL);
}

static void path_check_basename(
    const char *label, const char *path, const char *expected
)
{
    char *got = ccpath_basename(path);
    CC_CHECK_EQ_STR(label, expected, got);
    if (got)
        ccalloc(got, 0, NULL);
}

static void path_check_join(
    const char *label, const char *a, const char *b,
    const char *expected
)
{
    char *got = ccpath_join(a, b);
    CC_CHECK_EQ_STR(label, expected, got);
    if (got)
        ccalloc(got, 0, NULL);
}

void test_ccpath(void)
{
    CC_CASE("PATH-NORM-001", "绝对路径规范化");
#ifdef CC_WINDOWS
    path_check_resolve("盘符路径", "C:\\a\\b\\..\\c", NULL, "C:/a/c");
    path_check_resolve("盘符根", "C:\\", NULL, "C:/");
    path_check_resolve("UNC", "\\\\server\\share\\a\\..\\b", NULL,
        "//server/share/b");
    path_check_resolve("UNC 根", "\\\\server\\share", NULL,
        "//server/share");
    path_check_resolve("盘符相对", "C:foo", NULL, NULL);
    path_check_resolve("扩展长度前缀", "\\\\?\\C:\\a", NULL, NULL);
#else
    path_check_resolve("根", "/", NULL, "/");
    path_check_resolve("多点斜杠", "///a//b/", NULL, "/a/b");
    path_check_resolve("点段", "/a/./b/../c", NULL, "/a/c");
    path_check_resolve("根上回退", "/../..", NULL, "/");
#endif
    CC_CASE_END();

    CC_CASE("PATH-ABS-001", "相对路径解析");
#ifdef CC_WINDOWS
    path_check_resolve("盘符基准", "a/../b", "C:/work", "C:/work/b");
    path_check_resolve("已是绝对", "C:/x/./y", "C:/work", "C:/x/y");
#else
    path_check_resolve("绝对基准", "a/../b", "/work", "/work/b");
    path_check_resolve("已是绝对", "/x/./y", "/work", "/x/y");
#endif
    CC_CHECK(ccpath_is_absolute("/x") == CCTRUE, "根路径为绝对");
    CC_CHECK(ccpath_is_absolute("x") == CCFALSE, "相对路径非绝对");
    CC_CASE_END();

    CC_CASE("PATH-PARENT-001", "父目录");
    path_check_parent("/a/b", "/a/b", "/a");
    path_check_parent("/a", "/a", "/");
    path_check_parent("/", "/", "/");
    path_check_parent("相对多段", "a/b", "a");
    path_check_parent("相对单段", "a", ".");
    path_check_parent("尾斜杠", "a/b/", "a");
#ifdef CC_WINDOWS
    path_check_parent("盘符", "C:/a", "C:/");
    path_check_parent("盘符根", "C:/", "C:/");
    path_check_parent("UNC", "//server/share/a", "//server/share");
#endif
    CC_CASE_END();

    CC_CASE("PATH-BASE-001", "basename");
    path_check_basename("多段", "/a/b/", "b");
    path_check_basename("根", "/", "");
    path_check_basename("相对", "a", "a");
#ifdef CC_WINDOWS
    path_check_basename("盘符根", "C:/", "");
    path_check_basename("UNC", "//server/share", "share");
#endif
    CC_CASE_END();

    CC_CASE("PATH-JOIN-001", "路径拼接");
    path_check_join("普通", "build", "obj/main.o", "build/obj/main.o");
    path_check_join("左尾斜杠", "build/", "obj", "build/obj");
    path_check_join("右绝对", "build", "/x", "/x");
    path_check_join("空左", "", "a", "a");
    path_check_join("空右", "a", "", "a");
#ifdef CC_WINDOWS
    path_check_join("盘符根", "C:/", "a", "C:/a");
#endif
    CC_CASE_END();

    CC_CASE("PATH-EDGE-001", "边界与所有权");
    CC_CHECK(ccpath_resolve(NULL, NULL) == NULL, "空路径返回 NULL");
    CC_CHECK(ccpath_resolve("", NULL) == NULL, "空串返回 NULL");
    CC_CHECK(ccpath_parent(NULL) == NULL, "空父目录返回 NULL");
    CC_CHECK(ccpath_basename(NULL) == NULL, "空 basename 返回 NULL");
    CC_CHECK(ccpath_equal(NULL, NULL) == CCTRUE, "双 NULL 相等");
    CC_CHECK(ccpath_equal("a", NULL) == CCFALSE, "单 NULL 不相等");
#ifdef CC_WINDOWS
    CC_CHECK(ccpath_equal("C:/A", "c:/a") == CCTRUE, "Windows 大小写不敏感");
#else
    CC_CHECK(ccpath_equal("A", "a") == CCFALSE, "POSIX 大小写敏感");
#endif
    {
        char long_path[4096];
        memset(long_path, 'a', sizeof(long_path) - 1);
        long_path[sizeof(long_path) - 1] = '\0';
        char *got = ccpath_resolve(long_path, NULL);
        CC_CHECK(got != NULL, "超长相对路径可解析");
        if (got)
            ccalloc(got, 0, NULL);
    }
    CC_CASE_END();
}
