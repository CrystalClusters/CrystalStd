/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: ccfiles 内部数据结构与资源包格式定义。
 *               仅供 src/ccfiles*.c 使用，不对外公开。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#ifndef _SRC_CCFILES_INTERNAL_H_
#define _SRC_CCFILES_INTERNAL_H_

#include <crystal_cluster.h>
#include <cc_dyn.h>
#include <stdio.h>
#include <ccfiles.h>

/*
 * ============================================================================
 * 常量与内部标记
 * ============================================================================
 */

/* 虚拟树节点下标哨兵：下标 0 合法，故不能用 0 表示空 */
#define CC_FS_NIL 0xFFFFFFFFu

/* 内部来源标记：区分内存创建 / 磁盘投影 / 包投影 */
#define FS_ORIGIN_MEM 0
#define FS_ORIGIN_DISK 1
#define FS_ORIGIN_PKG 2

/*
 * ============================================================================
 * 运行时数据结构
 * ============================================================================
 */

/* 挂载信息：每个节点持有；real_path == NULL 表示跟随上级 */
typedef struct cc_fs_mount
{
    char *real_path;   /* 真实路径（一律 `/`，相对路径已展开为绝对）；NULL = 跟随上级 */
    CCUINT32 type;     /* CCFsMountType */
    CCUINT8 origin;    /* FS_ORIGIN_* */
    CCUINT64 pkg_off;  /* 包内文件：数据起始偏移（绝对） */
    CCUINT64 pkg_size; /* 包内文件：数据字节数 */
} CCFsMount;

/* 前置声明：探针（CCFiles）完整定义见下方 */
struct cc_files;

/* 探针回指：节点持有「指向它的探针」双向链表；探针离开 / 节点删除时维护 */
typedef struct cc_fs_probe_ref
{
    struct cc_fs_probe_ref *prev;
    struct cc_fs_probe_ref *next;
    struct cc_files *probe;
} CCFsProbeRef;

/*
 * 虚拟树节点：连续存于动态数组，节点之间一律用下标互指。
 * 红黑树字段（parent/left/right/is_red）位于其父目录的子红黑树内。
 */
typedef struct cc_fs_node
{
    /* 键：文件名（字符串本体独立分配，节点只持指针） */
    char *name;
    /* 红黑树字段：一律存下标，CC_FS_NIL 表示空 */
    CCUINT32 parent;
    CCUINT32 left;
    CCUINT32 right;
    CCBOOL is_red;
    /* 目录层级与嵌套 */
    CCUINT32 fs_parent;     /* 父目录节点下标，用于向上导航 */
    CCUINT32 children_root; /* 目录节点：其子节点红黑树的根下标 */
    /* 类型与挂载 */
    CCFsType type;    /* 文件 / 目录 */
    CCFsMount *mount; /* 挂载信息；NULL 表示跟随上级 */
    /* 元数据：stat 直接读取，不现算 */
    CCUINT64 size;  /* 字节数 */
    CCUINT64 ctime; /* 创建时间（秒级系统时间戳） */
    CCUINT64 mtime; /* 修改时间（秒级系统时间戳） */
    CCUINT32 crc32; /* 持久化 CRC：包内取块头、打包 / 解包时记录；无则 0 */
    /* 指向该节点的探针回指链表头（NULL 表示无探针指向） */
    CCFsProbeRef *probe_head;
    /* 运行时读写句柄：仅在有探针指向该节点期间存在；最后一次摘除 / 节点被删 /
       物理身份改变（改名 / 跨锚点移动 / 重新挂载）时立即落盘并关闭 */
    void *fp;           /* FILE*；NULL 表示未打开 */
    CCUINT64 fp_pos;    /* 句柄当前定位，顺序写时省去 fseek */
    CCBOOL fp_writable; /* 句柄是否以可写方式打开 */
} CCFsNode;

/* 探针：记录持有者的操作起点（cwd + 当前指向的文件） */
struct cc_files
{
    _Atomic CCBOOL alive; /* CCFALSE = 该探针已失效（其 cur 指向的节点被删除） */
    CCUINT32 cwd;         /* 当前工作目录节点下标（默认指向 `/`） */
    CCUINT32 cur;         /* 当前指向的文件节点下标；CC_FS_NIL 表示未指向文件 */
    CCUINT32 flags;       /* 内部状态标志 */
    CCFsProbeRef ref;     /* 自身回指，随 cur 挂入 / 摘除节点链表 */
};

/*
 * ============================================================================
 * 运行时状态与共享内部接口
 * 状态定义在 ccfiles.c，其余模块通过 extern 访问；均不属于公开接口。
 * ============================================================================
 */

typedef struct cc_fs_runtime
{
    CCDyn nodes;    /* CCFsNode[] */
    CCUINT32 root;  /* 根 `/` 节点下标（恒为 0） */
    char *res_base; /* 可执行程序同层 res 目录（绝对、`/` 分隔） */
} CCFsRuntime;

extern CCFsRuntime ccfiles_var;

static inline CCFsNode *node_at(CCUINT32 idx)
{
    return (CCFsNode *)((CCUINT8 *)ccfiles_var.nodes.array + (CCUINT64)idx * sizeof(CCFsNode));
}

static inline CCUINT32 idx_of(CCFsNode *node)
{
    return (CCUINT32)(((CCUINT8 *)node - (CCUINT8 *)ccfiles_var.nodes.array) / sizeof(CCFsNode));
}

static inline CCUINT32 node_count(void)
{
    return ccfiles_var.nodes.size / (CCUINT32)sizeof(CCFsNode);
}

/* ---- 通用工具（ccfiles_utilities.c） ---- */
CCUINT64 fs_now_seconds(void);
char *fs_strdup(const char *s, const char *comment);
CCINT32 fs_buf_putn(CCDyn *b, const char *s, CCUINT32 n);
CCINT32 fs_buf_put(CCDyn *b, const char *s);
void fs_buf_clear(CCDyn *b);
const char *fs_buf_str(const CCDyn *b);
void fs_buf_free(CCDyn *b);
CCINT32 fs_real_resolve(const char *real_path, CCDyn *out);
CCINT32 fs_unlink_file(const char *path);
CCINT32 fs_rmdir_dir(const char *path);
CCINT32 fs_rename_path(const char *from, const char *to);
CCBOOL fs_is_link_path(const char *path);
CCINT32 fs_remove_link_path(const char *path);
CCBOOL fs_file_exists(const char *path);
CCBOOL fs_is_dir_path(const char *path);
CCUINT64 fs_file_size(const char *path);
CCUINT64 fs_file_mtime(const char *path);
CCINT32 fs_mkdirs(const char *path);
CCINT32 fs_remove_tree(const char *path);
void fs_init_exe_dir(void);
CCINT32 fs_fseek(FILE *fp, CCUINT64 off);
CCINT32 fs_truncate(FILE *fp, CCUINT64 size);
void fs_dir_of(char *path);

/* ---- 核心节点 / 挂载 / 探针（ccfiles.c） ---- */
CCFsMount *fs_mount_new(const char *real_path, CCUINT32 type, CCUINT8 origin);
void fs_mount_free(CCFsMount *m);
void fs_alloc_rollback(void);
CCUINT32 fs_child_get_idx(CCUINT32 parent_idx, const char *name);
CCINT32 fs_insert_child(CCUINT32 parent_idx, CCUINT32 child_idx);
CCUINT32 fs_create_node(CCUINT32 parent_idx, const char *name, CCFsType type, const char *real_path, CCUINT32 mount_type, CCUINT8 origin);
CCINT32 fs_remove_child(CCUINT32 parent_idx, const char *name);
CCINT32 fs_delete_subtree_ex(CCUINT32 idx, CCBOOL unlink_physical);
CCINT32 fs_delete_subtree_watch(CCUINT32 idx, CCBOOL unlink_physical, CCUINT32 *watch, CCUINT32 watch_count);
CCINT32 fs_collect_subtree(CCUINT32 idx, CCDyn *out);
CCINT32 fs_delete_list_watch(CCDyn *list, CCBOOL unlink_physical, CCUINT32 *watch, CCUINT32 watch_count);
void fs_discard_detached_node(CCUINT32 idx);
CCINT32 fs_collect_children_ex(CCUINT32 dir_idx, CCDyn *out);
CCINT32 fs_children_walk(CCUINT32 dir_idx, void *user, CCINT32 (*cb)(void *data, void *user));
void fs_set_children_parent(CCUINT32 dir_idx, CCUINT32 new_parent);
CCINT32 fs_delete_children(CCUINT32 idx, CCBOOL unlink_physical);
void fs_delete_subtree(CCUINT32 idx);
void fs_collect_children(CCUINT32 dir_idx, CCDyn *out);
void fs_node_ref(CCFiles *fs, CCUINT32 idx);
void fs_node_unref(CCFiles *fs);

/* ---- 路径 / 挂载投影（ccfiles_path.c） ---- */
CCUINT32 fs_find_anchor(CCUINT32 idx);
CCBOOL fs_is_protected(CCUINT32 idx);
CCINT32 fs_physical(CCUINT32 idx, CCDyn *out);
char *fs_physical_dup(CCUINT32 idx, const char *comment);
CCINT32 fs_physical_delete(CCUINT32 idx);
CCINT32 fs_vwalk(CCFiles *fs, const char *path, CCBOOL strict, CCUINT32 *out_parent, CCDyn *leaf, CCUINT32 *out_idx);
CCINT32 fs_scan_dir_into(CCUINT32 parent_idx, const char *real_path);

/* ---- 常驻句柄 / 原始读写（ccfiles_rw.c） ---- */
CCINT32 fs_fh_open(CCUINT32 idx, CCBOOL want_write);
CCINT32 fs_fh_flush(CCUINT32 idx);
CCINT32 fs_fh_close(CCUINT32 idx);
CCINT32 fs_fh_flush_all(void);
CCINT32 fs_fh_close_all(void);
void fs_fh_release_if_unused(CCUINT32 idx);
CCINT64 fs_read_raw(CCUINT32 idx, void *buf, CCUINT64 offset, CCUINT64 size);
CCINT64 fs_write_raw(CCUINT32 idx, const void *data, CCUINT64 offset, CCUINT64 size);

/* ---- 资源包（ccfiles_pkg.c） ---- */
CCINT32 fs_load_pkg_into(CCUINT32 anchor_idx, const char *pkg_path);

/*
 * ============================================================================
 * 资源包格式（只读投影）
 *
 * 磁盘布局固定小端、与宿主字节序无关，禁止直接 fwrite / fread 结构体，
 * 读写一律经 ccfiles_pkg.c 的 fs_le*_put / fs_le*_get 显式序列化。
 *
 *   [0, 32)                      头（CC_FS_PKG_HEADER_SIZE 字节）
 *       magic[4] | version u16 | flags u16 | file_count u32 |
 *       block_count u32 | data_size u64 | header_crc u32 | reserved u32
 *   [32, 32 + block_count * 32)  块表（每块 CC_FS_PKG_BLOCK_SIZE 字节）
 *       name_len u32 | name_off u32 | offset u64 | size u64 |
 *       crc32 u32 | flags u32（bit0 = 目录）
 *   名字区：紧跟块表；name_off 相对名字区起点；UTF-8 相对路径、`/` 分隔、
 *           无 NUL、无 `.` / `..` / 空段、段尾不得为空格或点
 *   数据区：紧跟名字区；off 为文件内绝对偏移，文件按块表顺序紧凑排布，无对齐
 *
 * 下方结构体只是内存表示，不参与磁盘布局。
 * ============================================================================
 */

#define CC_FS_PKG_HEADER_SIZE 32u
#define CC_FS_PKG_BLOCK_SIZE 32u
#define CC_FS_PKG_MAGIC_SIZE 4u
#define CC_FS_PKG_VERSION 1u
#define CC_FS_PKG_FLAG_DIR 0x1u

typedef struct cc_fs_pkg_header
{
    CCUINT8 magic[CC_FS_PKG_MAGIC_SIZE]; /* '_CPK' = 5F 43 50 4B */
    CCUINT16 version;
    CCUINT16 flags;
    CCUINT32 file_count;
    CCUINT32 block_count;
    CCUINT64 data_size;
    CCUINT32 header_crc;
    CCUINT32 reserved;
} CCFsPkgHeader;

typedef struct cc_fs_pkg_block
{
    CCUINT32 name_len;
    CCUINT32 name_off;
    CCUINT64 offset;
    CCUINT64 size;
    CCUINT32 crc32;
    CCUINT32 flags; /* bit0 = 目录 */
} CCFsPkgBlock;

#endif
