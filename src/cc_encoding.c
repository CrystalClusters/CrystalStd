/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: UTF-8 与本地编码转换实现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"

#include <string.h>

#define CC_ENCODING_COMMENT CC_DBG_REL_TAG("编码转换", NULL)

#ifndef CC_WINDOWS
static char *encoding_dup(const char *text)
{
    CCUINT32 len = (CCUINT32)strlen(text);
    char *out = (char *)ccalloc(
        NULL, (CCUINT64)len + 1, CC_ENCODING_COMMENT
    );
    if (!out)
        return NULL;
    memcpy(out, text, (size_t)len + 1);
    return out;
}
#endif

#ifdef CC_WINDOWS
static UINT encoding_code_page(CCLocalEncoding encoding)
{
    UINT code_page = (encoding == CC_LOCAL_CONSOLE)
        ? GetConsoleOutputCP() : GetACP();
    if (code_page == 0)
        code_page = GetACP();
    return code_page;
}

static char *encoding_wide_to_narrow(
    const WCHAR *wide, int wide_len, UINT code_page
)
{
    int narrow_len = WideCharToMultiByte(
        code_page, 0, wide, wide_len, NULL, 0, NULL, NULL
    );
    if (narrow_len <= 0)
        return NULL;

    char *narrow = (char *)ccalloc(
        NULL, (CCUINT64)narrow_len, CC_ENCODING_COMMENT
    );
    if (!narrow)
        return NULL;
    if (WideCharToMultiByte(
            code_page, 0, wide, wide_len, narrow, narrow_len,
            NULL, NULL
        ) <= 0)
    {
        ccalloc(narrow, 0, NULL);
        return NULL;
    }
    return narrow;
}

static WCHAR *encoding_narrow_to_wide(
    const char *narrow, UINT code_page, int *out_len
)
{
    int wide_len = MultiByteToWideChar(
        code_page, 0, narrow, -1, NULL, 0
    );
    if (wide_len <= 0)
        return NULL;

    WCHAR *wide = (WCHAR *)ccalloc(
        NULL, (CCUINT64)wide_len * sizeof(WCHAR),
        CC_ENCODING_COMMENT
    );
    if (!wide)
        return NULL;
    if (MultiByteToWideChar(
            code_page, 0, narrow, -1, wide, wide_len
        ) <= 0)
    {
        ccalloc(wide, 0, NULL);
        return NULL;
    }
    *out_len = wide_len;
    return wide;
}
#endif

char *cc_utf8_to_local(const char *utf8, CCLocalEncoding target)
{
    if (!utf8)
        return NULL;
#ifndef CC_WINDOWS
    CC_UNUSED(target);
    return encoding_dup(utf8);
#else
    int wide_len = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0
    );
    if (wide_len <= 0)
        return NULL;

    WCHAR *wide = (WCHAR *)ccalloc(
        NULL, (CCUINT64)wide_len * sizeof(WCHAR),
        CC_ENCODING_COMMENT
    );
    if (!wide)
        return NULL;
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, wide_len
        ) <= 0)
    {
        ccalloc(wide, 0, NULL);
        return NULL;
    }

    char *narrow = encoding_wide_to_narrow(
        wide, wide_len, encoding_code_page(target)
    );
    ccalloc(wide, 0, NULL);
    return narrow;
#endif
}

char *cc_local_to_utf8(const char *local, CCLocalEncoding source)
{
    if (!local)
        return NULL;
#ifndef CC_WINDOWS
    CC_UNUSED(source);
    return encoding_dup(local);
#else
    int wide_len = 0;
    WCHAR *wide = encoding_narrow_to_wide(
        local, encoding_code_page(source), &wide_len
    );
    if (!wide)
        return NULL;

    char *utf8 = encoding_wide_to_narrow(wide, wide_len, CP_UTF8);
    ccalloc(wide, 0, NULL);
    return utf8;
#endif
}
