/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: cc_encoding 测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <ccalloc.h>
#include <string.h>

#ifdef CC_WINDOWS
#include <windows.h>
#endif

static const char utf8_cn[] = {
    (char)0xE4, (char)0xB8, (char)0xAD,
    (char)0xE6, (char)0x96, (char)0x87, '\0'
};

#ifdef CC_WINDOWS
static const char gbk_cn[] = {
    (char)0xD6, (char)0xD0, (char)0xCE, (char)0xC4, '\0'
};
#endif

void test_cc_encoding(void)
{
    CC_CASE("ENC-001", "ASCII 与空串");
    {
        char *local = cc_utf8_to_local("abc", CC_LOCAL_SYSTEM);
        CC_CHECK_EQ_STR("ASCII 原样", "abc", local);
        if (local)
            ccalloc(local, 0, NULL);

        char *utf8 = cc_local_to_utf8("abc", CC_LOCAL_SYSTEM);
        CC_CHECK_EQ_STR("ASCII 反向", "abc", utf8);
        if (utf8)
            ccalloc(utf8, 0, NULL);

        char *empty = cc_utf8_to_local("", CC_LOCAL_SYSTEM);
        CC_CHECK_EQ_STR("空串", "", empty);
        if (empty)
            ccalloc(empty, 0, NULL);
    }
    CC_CASE_END();

    CC_CASE("ENC-002", "中文编码转换");
#ifdef CC_WINDOWS
    {
        UINT acp = GetACP();
        char *local = cc_utf8_to_local(utf8_cn, CC_LOCAL_SYSTEM);
        CC_CHECK(local != NULL, "UTF-8 转本地编码");
        if (local && acp == 936)
            CC_CHECK_EQ_STR("GBK 字节", gbk_cn, local);
        if (local)
        {
            char *back = cc_local_to_utf8(local, CC_LOCAL_SYSTEM);
            CC_CHECK_EQ_STR("本地编码回转 UTF-8", utf8_cn, back);
            if (back)
                ccalloc(back, 0, NULL);
            ccalloc(local, 0, NULL);
        }
    }
#else
    {
        char *local = cc_utf8_to_local(utf8_cn, CC_LOCAL_SYSTEM);
        CC_CHECK_EQ_STR("POSIX 恒等", utf8_cn, local);
        if (local)
        {
            char *back = cc_local_to_utf8(local, CC_LOCAL_SYSTEM);
            CC_CHECK_EQ_STR("POSIX 反向恒等", utf8_cn, back);
            if (back)
                ccalloc(back, 0, NULL);
            ccalloc(local, 0, NULL);
        }
    }
#endif
    CC_CASE_END();

    CC_CASE("ENC-003", "非法输入与 NULL");
    {
        char invalid[] = {(char)0xFF, (char)0xFE, '\0'};
        char *got = cc_utf8_to_local(invalid, CC_LOCAL_SYSTEM);
#ifdef CC_WINDOWS
        CC_CHECK(got == NULL, "Windows 非法 UTF-8 返回 NULL");
#else
        CC_CHECK_EQ_STR("POSIX 原样返回", invalid, got);
#endif
        if (got)
            ccalloc(got, 0, NULL);

        CC_CHECK(cc_utf8_to_local(NULL, CC_LOCAL_SYSTEM) == NULL,
            "NULL 输入返回 NULL");
        CC_CHECK(cc_local_to_utf8(NULL, CC_LOCAL_SYSTEM) == NULL,
            "NULL 反向输入返回 NULL");
    }
    CC_CASE_END();
}
