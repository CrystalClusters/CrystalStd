/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: cchost 测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <ccalloc.h>
#include <stdio.h>
#include <string.h>

static CCBOOL host_write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return CCFALSE;
    size_t len = strlen(text);
    CCBOOL ok = (fwrite(text, 1, len, file) == len) ? CCTRUE : CCFALSE;
    fclose(file);
    return ok;
}

static void host_make_path(char *out, CCUINT32 size, const char *name)
{
    snprintf(out, size, "out/cchost_test_tmp/%s", name);
}

void test_cchost(void)
{
    char root[512];
    char sub[512];
    char file_a[512];
    char file_b[512];
    char file_c[512];

    host_make_path(root, sizeof(root), "root");
    host_make_path(sub, sizeof(sub), "root/sub");
    host_make_path(file_a, sizeof(file_a), "root/a.txt");
    host_make_path(file_b, sizeof(file_b), "root/b.txt");
    host_make_path(file_c, sizeof(file_c), "root/sub/c.txt");

    cchost_remove(file_a);
    cchost_remove(file_b);
    cchost_remove(file_c);
    cchost_remove(sub);
    cchost_remove(root);

    CC_CASE("HOST-STAT-001", "宿主 stat 与整文件读取");
    CC_CHECK_EQ_I64("建根目录", CC_HOST_OK, cchost_mkdirs(root));
    CC_CHECK_EQ_I64("建子目录", CC_HOST_OK, cchost_mkdirs(sub));
    CC_CHECK(host_write_file(file_a, "hello"), "写入测试文件");

    CCHostStat st;
    CC_CHECK_EQ_I64("查询文件", CC_HOST_OK, cchost_stat(file_a, &st));
    CC_CHECK(st.kind == CC_HOST_FILE, "类型为文件");
    CC_CHECK_EQ_U64("文件大小", 5, st.size);
    CC_CHECK(st.mtime_ns > 0, "纳秒时间戳");

    CC_CHECK_EQ_I64("查询目录", CC_HOST_OK, cchost_stat(sub, &st));
    CC_CHECK(st.kind == CC_HOST_DIR, "类型为目录");

    CC_CHECK_EQ_I64("查询缺失", CC_HOST_OK, cchost_stat(file_b, &st));
    CC_CHECK(st.kind == CC_HOST_MISSING, "缺失分类");

    void *data = NULL;
    CCUINT64 size = 0;
    CC_CHECK_EQ_I64("读取文件", CC_HOST_OK,
        cchost_read_all(file_a, &data, &size));
    CC_CHECK_EQ_U64("读取长度", 5, size);
    CC_CHECK(data != NULL && memcmp(data, "hello", 5) == 0, "内容一致");
    if (data)
        ccalloc(data, 0, NULL);

    CC_CHECK_EQ_I64("缺失文件读取", CC_HOST_ERR_NOENT,
        cchost_read_all(file_b, &data, &size));
    CC_CHECK(data == NULL && size == 0, "失败清空输出");
    CC_CASE_END();

    CC_CASE("HOST-DIR-001", "递归目录与冲突");
    CC_CHECK_EQ_I64("重复建目录", CC_HOST_OK, cchost_mkdirs(root));
    CC_CHECK_EQ_I64("以文件为父目录", CC_HOST_ERR_NOTDIR,
        cchost_mkdirs("out/cchost_test_tmp/root/a.txt/sub"));
    CC_CASE_END();

    CC_CASE("HOST-COPY-001", "复制文件");
    CC_CHECK_EQ_I64("复制", CC_HOST_OK, cchost_copy(file_a, file_b));
    CC_CHECK_EQ_I64("复制结果查询", CC_HOST_OK, cchost_stat(file_b, &st));
    CC_CHECK_EQ_U64("复制大小", 5, st.size);
    CC_CHECK_EQ_I64("复制到目录", CC_HOST_ERR_ISDIR,
        cchost_copy(file_a, sub));
    CC_CHECK_EQ_I64("复制缺失源", CC_HOST_ERR_NOENT,
        cchost_copy(file_c, file_a));
    CC_CASE_END();

    CC_CASE("HOST-MOVE-001", "移动文件");
    CC_CHECK_EQ_I64("移动", CC_HOST_OK, cchost_move(file_b, file_c));
    CC_CHECK_EQ_I64("源已消失", CC_HOST_OK, cchost_stat(file_b, &st));
    CC_CHECK(st.kind == CC_HOST_MISSING, "移动后源缺失");
    CC_CHECK_EQ_I64("目标存在", CC_HOST_OK, cchost_stat(file_c, &st));
    CC_CHECK(st.kind == CC_HOST_FILE, "目标为文件");
    CC_CHECK_EQ_I64("移动到目录", CC_HOST_ERR_ISDIR,
        cchost_move(file_c, sub));
    CC_CASE_END();

    CC_CASE("HOST-REMOVE-001", "删除文件与目录");
    CC_CHECK_EQ_I64("非空目录", CC_HOST_ERR_NOTEMPTY, cchost_remove(sub));
    CC_CHECK_EQ_I64("删除文件", CC_HOST_OK, cchost_remove(file_c));
    CC_CHECK_EQ_I64("重复删除", CC_HOST_ERR_NOENT, cchost_remove(file_c));
    CC_CHECK_EQ_I64("删除空目录", CC_HOST_OK, cchost_remove(sub));
    CC_CHECK_EQ_I64("删除遗留文件", CC_HOST_OK, cchost_remove(file_a));
    CC_CHECK_EQ_I64("删除根目录", CC_HOST_OK, cchost_remove(root));
    CC_CASE_END();
}
