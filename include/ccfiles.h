/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 统一文件管理内核（虚拟文件空间）。
 *               文档见 <工程根目录>/docs/ccfiles.md。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _INCLUDE_CCFILES_H_
#define _INCLUDE_CCFILES_H_

#include "crystal_cluster.h"

/* 操作类返回值；读写类失败时直接返回其中的负值 */
typedef enum cc_fs_result {
    CC_FS_OK           =  0,   /* 成功 */
    CC_FS_ERR_PARAM    = -1,   /* 参数非法（空指针 / type 非法 / 缓冲区不足） */
    CC_FS_ERR_NOENT    = -2,   /* 路径 / 节点不存在 */
    CC_FS_ERR_NOTDIR   = -3,   /* 不是目录 */
    CC_FS_ERR_ISDIR    = -4,   /* 是目录（此操作要求文件） */
    CC_FS_ERR_PROTECT  = -5,   /* 包投影节点受保护（非读操作失效） */
    CC_FS_ERR_PERM     = -6,   /* 操作系统权限错误（如实透传） */
    CC_FS_ERR_NOSPACE  = -7,   /* 空间不足（磁盘满 / 内存分配失败） */
    CC_FS_ERR_EXIST    = -8,   /* 目标已存在 */
    CC_FS_ERR_NOTEMPTY = -9,   /* 目录非空 */
    CC_FS_ERR_IO       = -10,  /* 底层 IO 失败 */
    CC_FS_ERR_NOTMOUNT = -11,  /* 不是挂载锚点（unmount） */
} CCFsResult;

/* 挂载类型 */
typedef enum cc_fs_mount_type {
    CC_FS_MOUNT_DIR  = 1,   /* 挂载真实目录，递归生成子树 */
    CC_FS_MOUNT_PKG  = 2,   /* 挂载只读资源包 */
    CC_FS_MOUNT_FILE = 3,   /* 挂载单个真实文件 */
} CCFsMountType;

/* 节点类型 */
typedef enum cc_fs_type {
    CC_FS_TYPE_NONE = 0,
    CC_FS_TYPE_DIR  = 1,
    CC_FS_TYPE_FILE = 2,
} CCFsType;

/* 元信息输出（stat 直接读取节点保存值，不现算） */
typedef struct cc_fs_stat {
    CCFsType  type;      /* 文件 / 目录 */
    CCUINT64  size;      /* 字节数 */
    CCUINT64  ctime;     /* 创建时间（秒级系统时间戳）；无则 0 */
    CCUINT64  mtime;     /* 修改时间（秒级系统时间戳）；无则 0 */
    CCBOOL    readonly;  /* 是否只读（包节点恒为真） */
    CCUINT32  crc32;     /* 持久化 CRC：包内文件取块头；打包 / 解包时记录；纯磁盘文件无记录为 0 */
} CCFsStat;

/*
 * 探针句柄：不透明类型。内部定义（cwd / cur / 回指链表）位于 src/ccfiles_internal.h，
 * 调用方只能通过本头文件中的接口操作。
 */
typedef struct cc_files CCFiles;

/* 检索结果条目：仅在回调执行期间有效；name 指向节点内部字符串，回调返回后失效 */
typedef struct cc_fs_entry {
    const char *name;      /* 文件 / 目录名（不含路径） */
    CCFsType    type;      /* 文件 / 目录 */
    CCUINT64    size;      /* 字节数 */
    CCUINT64    ctime;     /* 创建时间（秒级系统时间戳）；无则 0 */
    CCUINT64    mtime;     /* 修改时间（秒级系统时间戳）；无则 0 */
    CCBOOL      readonly;  /* 是否只读（包节点恒为真） */
    CCUINT32    crc32;     /* 持久化 CRC；无则 0 */
} CCFsEntry;

/* 探针生命周期 */
CCFiles* ccfiles_create(void);
void     ccfiles_destroy(CCFiles *fs);

/* 工作目录与当前文件 */
CCINT32 ccfiles_chdir(CCFiles *fs, const char *path);
CCINT32 ccfiles_getcwd(CCFiles *fs, char *buf, CCUINT32 size);
CCINT32 ccfiles_getcur(CCFiles *fs, char *buf, CCUINT32 size);  /* 无当前文件返回 CC_FS_ERR_NOENT */

/* 挂载与刷新 */
CCINT32 ccfiles_mount(CCFiles *fs, const char *v_path, const char *real_path, CCUINT32 type);
CCINT32 ccfiles_unmount(CCFiles *fs, const char *v_path);
CCINT32 ccfiles_refresh(CCFiles *fs, const char *v_path);

/* 文件读写（读写类失败返回负的 CCFsResult）
   写入直通物理文件：数据写入该文件常驻句柄的 stdio 缓冲；句柄随探针引用归零、
   结构操作、ccfiles_flush 或析构时 flush + fclose，失败如实返回真实错误。 */
CCINT32 ccfiles_open(CCFiles *fs, const char *v_path);
CCINT64 ccfiles_read(CCFiles *fs, void *buf, CCUINT64 offset, CCUINT64 size);
CCINT64 ccfiles_write(CCFiles *fs, const void *data, CCUINT64 offset, CCUINT64 size);
CCINT32 ccfiles_flush(CCFiles *fs);   /* fs == NULL：flush 全部打开的文件 */
CCINT32 ccfiles_stat(CCFiles *fs, const char *v_path, CCFsStat *out);

/* 目录与检索 */
CCINT32 ccfiles_list(CCFiles *fs, const char *v_path, CCDataCallback cbk, void *user);
CCINT32 ccfiles_find(CCFiles *fs, const char *v_path, const char *pattern, CCDataCallback cbk, void *user);

/* shell 式文件操作 */
CCINT32 ccfiles_create_file(CCFiles *fs, const char *v_path);
CCINT32 ccfiles_create_dir(CCFiles *fs, const char *v_path);
CCINT32 ccfiles_remove(CCFiles *fs, const char *v_path);
CCINT32 ccfiles_remove_recursive(CCFiles *fs, const char *v_path);  /* rm -rf */
CCINT32 ccfiles_cp(CCFiles *fs, const char *src, const char *dst);
CCINT32 ccfiles_mv(CCFiles *fs, const char *src, const char *dst);
CCINT32 ccfiles_pack(CCFiles *fs, const char *v_path, const char *real_path);
CCINT32 ccfiles_unpack(CCFiles *fs, const char *v_path, const char *real_path);

#endif
