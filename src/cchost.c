/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 宿主文件系统 Shell 能力实现。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "crystal_std_inner_header.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#define CC_HOST_COMMENT CC_DBG_REL_TAG("宿主文件", NULL)
#define CC_HOST_CHUNK 16384

static CCINT32 host_result_from_errno(int err)
{
    switch (err)
    {
    case ENOENT:
        return CC_HOST_ERR_NOENT;
    case ENOTDIR:
        return CC_HOST_ERR_NOTDIR;
    case EISDIR:
        return CC_HOST_ERR_ISDIR;
    case EACCES:
    case EPERM:
        return CC_HOST_ERR_PERM;
    case ENOSPC:
        return CC_HOST_ERR_NOSPACE;
    case EEXIST:
        return CC_HOST_ERR_EXIST;
    case ENOTEMPTY:
        return CC_HOST_ERR_NOTEMPTY;
    default:
        return CC_HOST_ERR_IO;
    }
}

#ifdef CC_WINDOWS
static CCINT32 host_stat_win32(const char *path, CCHostStat *out)
{
    HANDLE handle = CreateFileA(
        path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL
    );
    if (handle == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
            err == ERROR_INVALID_NAME)
        {
            out->kind = CC_HOST_MISSING;
            return CC_HOST_OK;
        }
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION)
        {
            out->kind = CC_HOST_DENIED;
            return CC_HOST_OK;
        }
        return CC_HOST_ERR_IO;
    }

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info))
    {
        CloseHandle(handle);
        return CC_HOST_ERR_IO;
    }
    CloseHandle(handle);

    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        out->kind = CC_HOST_DIR;
    else
        out->kind = CC_HOST_FILE;
    out->size = ((CCUINT64)info.nFileSizeHigh << 32) |
        (CCUINT64)info.nFileSizeLow;

    ULARGE_INTEGER file_time;
    file_time.LowPart = info.ftLastWriteTime.dwLowDateTime;
    file_time.HighPart = info.ftLastWriteTime.dwHighDateTime;
    const CCUINT64 epoch = 116444736000000000ULL;
    if (file_time.QuadPart > epoch)
        out->mtime_ns = (file_time.QuadPart - epoch) * 100ULL;
    else
        out->mtime_ns = 0;
    return CC_HOST_OK;
}
#else
static CCINT32 host_stat_posix(const char *path, CCHostStat *out)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        if (errno == ENOENT || errno == ENOTDIR)
        {
            out->kind = CC_HOST_MISSING;
            return CC_HOST_OK;
        }
        if (errno == EACCES || errno == EPERM)
        {
            out->kind = CC_HOST_DENIED;
            return CC_HOST_OK;
        }
        return CC_HOST_ERR_IO;
    }

    if (S_ISREG(st.st_mode))
        out->kind = CC_HOST_FILE;
    else if (S_ISDIR(st.st_mode))
        out->kind = CC_HOST_DIR;
    else
        out->kind = CC_HOST_OTHER;

    out->size = (CCUINT64)st.st_size;
    out->mtime_ns = (CCUINT64)st.st_mtim.tv_sec * 1000000000ULL +
        (CCUINT64)st.st_mtim.tv_nsec;
    return CC_HOST_OK;
}
#endif

CCINT32 cchost_stat(const char *path, CCHostStat *out)
{
    if (!path || !path[0] || !out)
        return CC_HOST_ERR_PARAM;
    memset(out, 0, sizeof(*out));
    out->kind = CC_HOST_MISSING;
#ifdef CC_WINDOWS
    return host_stat_win32(path, out);
#else
    return host_stat_posix(path, out);
#endif
}

CCINT32 cchost_read_all(const char *path, void **data, CCUINT64 *size)
{
    if (!path || !path[0] || !data || !size)
        return CC_HOST_ERR_PARAM;
    *data = NULL;
    *size = 0;

    CCHostStat st;
    CCINT32 result = cchost_stat(path, &st);
    if (result != CC_HOST_OK)
        return result;
    if (st.kind == CC_HOST_MISSING)
        return CC_HOST_ERR_NOENT;
    if (st.kind == CC_HOST_DENIED)
        return CC_HOST_ERR_PERM;
    if (st.kind == CC_HOST_DIR)
        return CC_HOST_ERR_ISDIR;
    if (st.kind != CC_HOST_FILE)
        return CC_HOST_ERR_IO;

    FILE *file = fopen(path, "rb");
    if (!file)
        return host_result_from_errno(errno);

    CCINT64 file_size = 0;
#ifdef CC_WINDOWS
    if (_fseeki64(file, 0, SEEK_END) != 0)
        file_size = -1;
    else
        file_size = (CCINT64)_ftelli64(file);
#else
    if (fseeko(file, 0, SEEK_END) != 0)
        file_size = -1;
    else
        file_size = (CCINT64)ftello(file);
#endif
    if (file_size < 0)
    {
        fclose(file);
        return CC_HOST_ERR_IO;
    }

    CCUINT64 need = (CCUINT64)file_size + 1;
    char *buffer = (char *)ccalloc(
        NULL, need, CC_HOST_COMMENT
    );
    if (!buffer)
    {
        fclose(file);
        return CC_HOST_ERR_NOSPACE;
    }

#ifdef CC_WINDOWS
    if (_fseeki64(file, 0, SEEK_SET) != 0)
#else
    if (fseeko(file, 0, SEEK_SET) != 0)
#endif
    {
        ccalloc(buffer, 0, NULL);
        fclose(file);
        return CC_HOST_ERR_IO;
    }

    CCUINT64 read_total = 0;
    while (read_total < (CCUINT64)file_size)
    {
        size_t want = (size_t)((CCUINT64)file_size - read_total);
        size_t got = fread(buffer + read_total, 1, want, file);
        if (got == 0)
            break;
        read_total += (CCUINT64)got;
    }
    if (read_total != (CCUINT64)file_size || ferror(file))
    {
        ccalloc(buffer, 0, NULL);
        fclose(file);
        return CC_HOST_ERR_IO;
    }
    fclose(file);

    buffer[file_size] = '\0';
    *data = buffer;
    *size = (CCUINT64)file_size;
    return CC_HOST_OK;
}

static CCUINT32 host_root_len(const char *path)
{
#ifdef CC_WINDOWS
    if (isalpha((unsigned char)path[0]) && path[1] == ':' &&
        path[2] == '/')
        return 3;
    if (path[0] == '/' && path[1] == '/')
    {
        CCUINT32 pos = 2;
        while (path[pos] && path[pos] != '/')
            pos++;
        if (!path[pos])
            return pos;
        pos++;
        while (path[pos] && path[pos] != '/')
            pos++;
        return pos;
    }
    if (path[0] == '/')
        return 1;
    return 0;
#else
    return (path[0] == '/') ? 1 : 0;
#endif
}

CCINT32 cchost_mkdirs(const char *path)
{
    if (!path || !path[0])
        return CC_HOST_ERR_PARAM;

    char *absolute = ccpath_resolve(path, NULL);
    if (!absolute)
        return CC_HOST_ERR_PARAM;

    CCUINT32 len = (CCUINT32)strlen(absolute);
    CCUINT32 start = host_root_len(absolute);
    CCINT32 result = CC_HOST_OK;
    for (CCUINT32 i = start; i <= len; i++)
    {
        if (i < len && absolute[i] != '/')
            continue;

        char saved = absolute[i];
        absolute[i] = '\0';
        if (absolute[0] != '\0')
        {
            CCHostStat st;
            result = cchost_stat(absolute, &st);
            if (result != CC_HOST_OK)
            {
                absolute[i] = saved;
                break;
            }
            if (st.kind == CC_HOST_MISSING)
            {
#ifdef CC_WINDOWS
                int created = _mkdir(absolute);
#else
                int created = mkdir(absolute, 0755);
#endif
                if (created != 0 && errno != EEXIST)
                {
                    result = host_result_from_errno(errno);
                    absolute[i] = saved;
                    break;
                }
                result = cchost_stat(absolute, &st);
                if (result != CC_HOST_OK || st.kind != CC_HOST_DIR)
                {
                    result = (result != CC_HOST_OK)
                        ? result : CC_HOST_ERR_NOTDIR;
                    absolute[i] = saved;
                    break;
                }
            }
            else if (st.kind != CC_HOST_DIR)
            {
                result = (st.kind == CC_HOST_DENIED)
                    ? CC_HOST_ERR_PERM : CC_HOST_ERR_NOTDIR;
                absolute[i] = saved;
                break;
            }
        }
        absolute[i] = saved;
        if (saved == '\0')
            break;
    }

    ccalloc(absolute, 0, NULL);
    return result;
}

CCINT32 cchost_remove(const char *path)
{
    if (!path || !path[0])
        return CC_HOST_ERR_PARAM;

    CCHostStat st;
    CCINT32 result = cchost_stat(path, &st);
    if (result != CC_HOST_OK)
        return result;
    if (st.kind == CC_HOST_MISSING)
        return CC_HOST_ERR_NOENT;
    if (st.kind == CC_HOST_DENIED)
        return CC_HOST_ERR_PERM;
    if (st.kind == CC_HOST_OTHER)
        return CC_HOST_ERR_IO;

#ifdef CC_WINDOWS
    int removed = (st.kind == CC_HOST_DIR)
        ? _rmdir(path) : _unlink(path);
#else
    int removed = remove(path);
#endif
    if (removed != 0)
        return host_result_from_errno(errno);
    return CC_HOST_OK;
}

static CCINT32 host_copy_file(
    const char *from, const char *to
)
{
    FILE *src = fopen(from, "rb");
    if (!src)
        return host_result_from_errno(errno);
    FILE *dst = fopen(to, "wb");
    if (!dst)
    {
        fclose(src);
        return host_result_from_errno(errno);
    }

    CCUINT8 buffer[CC_HOST_CHUNK];
    CCINT32 result = CC_HOST_OK;
    for (;;)
    {
        size_t got = fread(buffer, 1, sizeof(buffer), src);
        if (got > 0)
        {
            if (fwrite(buffer, 1, got, dst) != got)
            {
                result = CC_HOST_ERR_IO;
                break;
            }
        }
        if (got < sizeof(buffer))
        {
            if (ferror(src))
                result = CC_HOST_ERR_IO;
            break;
        }
    }

    if (fclose(dst) != 0 && result == CC_HOST_OK)
        result = CC_HOST_ERR_IO;
    fclose(src);
    if (result != CC_HOST_OK)
        remove(to);
    return result;
}

CCINT32 cchost_copy(const char *from, const char *to)
{
    if (!from || !from[0] || !to || !to[0])
        return CC_HOST_ERR_PARAM;

    char *abs_from = ccpath_resolve(from, NULL);
    char *abs_to = ccpath_resolve(to, NULL);
    if (!abs_from || !abs_to)
    {
        if (abs_from)
            ccalloc(abs_from, 0, NULL);
        if (abs_to)
            ccalloc(abs_to, 0, NULL);
        return CC_HOST_ERR_PARAM;
    }

    CCINT32 result = CC_HOST_OK;
    if (ccpath_equal(abs_from, abs_to))
    {
        ccalloc(abs_from, 0, NULL);
        ccalloc(abs_to, 0, NULL);
        return CC_HOST_OK;
    }

    CCHostStat st;
    result = cchost_stat(abs_from, &st);
    if (result != CC_HOST_OK)
        goto Cleanup;
    if (st.kind == CC_HOST_MISSING)
    {
        result = CC_HOST_ERR_NOENT;
        goto Cleanup;
    }
    if (st.kind == CC_HOST_DENIED)
    {
        result = CC_HOST_ERR_PERM;
        goto Cleanup;
    }
    if (st.kind == CC_HOST_DIR)
    {
        result = CC_HOST_ERR_ISDIR;
        goto Cleanup;
    }
    if (st.kind != CC_HOST_FILE)
    {
        result = CC_HOST_ERR_IO;
        goto Cleanup;
    }

    CCHostStat dst_st;
    result = cchost_stat(abs_to, &dst_st);
    if (result != CC_HOST_OK)
        goto Cleanup;
    if (dst_st.kind == CC_HOST_DIR)
    {
        result = CC_HOST_ERR_ISDIR;
        goto Cleanup;
    }
    if (dst_st.kind == CC_HOST_DENIED)
    {
        result = CC_HOST_ERR_PERM;
        goto Cleanup;
    }

    result = host_copy_file(abs_from, abs_to);

Cleanup:
    if (abs_from)
        ccalloc(abs_from, 0, NULL);
    if (abs_to)
        ccalloc(abs_to, 0, NULL);
    return result;
}

CCINT32 cchost_move(const char *from, const char *to)
{
    if (!from || !from[0] || !to || !to[0])
        return CC_HOST_ERR_PARAM;

    char *abs_from = ccpath_resolve(from, NULL);
    char *abs_to = ccpath_resolve(to, NULL);
    if (!abs_from || !abs_to)
    {
        if (abs_from)
            ccalloc(abs_from, 0, NULL);
        if (abs_to)
            ccalloc(abs_to, 0, NULL);
        return CC_HOST_ERR_PARAM;
    }

    CCINT32 result = CC_HOST_OK;
    if (ccpath_equal(abs_from, abs_to))
    {
        ccalloc(abs_from, 0, NULL);
        ccalloc(abs_to, 0, NULL);
        return CC_HOST_OK;
    }

    CCHostStat st;
    result = cchost_stat(abs_from, &st);
    if (result != CC_HOST_OK)
        goto Cleanup;
    if (st.kind == CC_HOST_MISSING)
    {
        result = CC_HOST_ERR_NOENT;
        goto Cleanup;
    }
    if (st.kind == CC_HOST_DIR)
    {
        result = CC_HOST_ERR_ISDIR;
        goto Cleanup;
    }
    if (st.kind != CC_HOST_FILE)
    {
        result = (st.kind == CC_HOST_DENIED)
            ? CC_HOST_ERR_PERM : CC_HOST_ERR_IO;
        goto Cleanup;
    }

    CCHostStat dst_st;
    result = cchost_stat(abs_to, &dst_st);
    if (result != CC_HOST_OK)
        goto Cleanup;
    if (dst_st.kind == CC_HOST_DIR)
    {
        result = CC_HOST_ERR_ISDIR;
        goto Cleanup;
    }
    if (dst_st.kind == CC_HOST_DENIED)
    {
        result = CC_HOST_ERR_PERM;
        goto Cleanup;
    }

    CCBOOL cross_device = CCFALSE;
#ifdef CC_WINDOWS
    if (MoveFileExA(
            abs_from, abs_to, MOVEFILE_REPLACE_EXISTING
        ))
        result = CC_HOST_OK;
    else
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            result = CC_HOST_ERR_NOENT;
        else if (err == ERROR_ACCESS_DENIED)
            result = CC_HOST_ERR_PERM;
        else if (err == ERROR_NOT_SAME_DEVICE)
        {
            cross_device = CCTRUE;
            result = CC_HOST_OK;
        }
        else
            result = CC_HOST_ERR_IO;
    }
#else
    if (rename(abs_from, abs_to) == 0)
        result = CC_HOST_OK;
    else if (errno == EXDEV)
    {
        cross_device = CCTRUE;
        result = CC_HOST_OK;
    }
    else
        result = host_result_from_errno(errno);
#endif

    if (result == CC_HOST_OK && cross_device)
    {
        result = cchost_copy(abs_from, abs_to);
        if (result == CC_HOST_OK)
        {
            result = cchost_remove(abs_from);
            if (result != CC_HOST_OK)
                result = CC_HOST_ERR_IO;
        }
    }

Cleanup:
    if (abs_from)
        ccalloc(abs_from, 0, NULL);
    if (abs_to)
        ccalloc(abs_to, 0, NULL);
    return result;
}
