/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 统一文件管理内核（ccfiles）的测试。
 *               覆盖探针、路径、挂载、读写落盘、目录检索、cp/mv 与打包解包。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"
#include "../src/ccfiles_internal.h"
#include <ccfiles.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef CC_WINDOWS
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ============================================================================
 * 断言与辅助
 * ============================================================================ */

#define fs_expect(cond, name) CC_CHECK((cond), (name))

static void fs_test_mkdir(const char *path)
{
#ifdef CC_WINDOWS
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void fs_test_rmdir(const char *path)
{
#ifdef CC_WINDOWS
    if (_rmdir(path) == 0)
        return;
#else
    if (rmdir(path) == 0)
        return;
#endif
    /* 符号链接 / junction 无法用 rmdir 删除时，删除链接本身 */
    remove(path);
}

static CCBOOL fs_test_path_exists(const char *path);

/* 创建目录链接：Windows 用 junction，POSIX 用 symlink；失败时跳过对应用例 */
static CCBOOL fs_test_make_dir_link(const char *link, const char *target)
{
#ifdef CC_WINDOWS
    char cmd[2600];
    snprintf(cmd, sizeof(cmd), "cmd /c mklink /J \"%s\" \"%s\" >nul 2>nul",
             link, target);
    if (system(cmd) != 0)
        return CCFALSE;
    return fs_test_path_exists(link);
#else
    return (symlink(target, link) == 0) ? CCTRUE : CCFALSE;
#endif
}

/* 清理本用例可能残留的全部磁盘产物；成功/失败/中断后都能安全重跑。
 * staging(.part) 目录由 ccfiles_unpack 自己在开始时递归清理，这里只处理
 * 用例直接创建的文件与目标目录。 */
static void fs_test_cleanup_artifacts(const char *tmp)
{
    static const char *files[] = {
        "a.txt", "b.bin", "e.txt", "keepmem.txt", "mvmem.txt", "mvmem2.txt",
        "byte.txt", "res.txt", "res2.txt",
        "keepdir/keep.txt", "sub/c.txt", "sub/d.txt", "newdir/f.txt",
        "sub2/sub/c.txt", "sub2/c.txt", "rtree/a/b.txt", "unpacked/c.txt",
        "bad_crc.pkg", "bad_trunc.pkg", "bad_off.pkg", "bad_name.pkg",
        "bad_trail.pkg", "bad_unpack.pkg", "bad_stage.pkg", "pkg.pkg",
        "longpkg.pkg", "x", "a\\b", "bslash\\evil.txt", "bslash/evil.txt",
        "outside_j/sentinel.txt", "mvfail_src.txt", "mvfail_dst.txt"
    };
    static const char *dirs[] = {
        "keepdir", "sub", "newdir", "sub2/sub", "sub2", "rtree/a", "rtree",
        "unpacked", "bad_unpacked", "bad_stage_out", "bad_trail_out",
        "link_j", "outside_j", "bslash"
    };
    char p[1200];
    for (CCUINT32 i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        snprintf(p, sizeof(p), "%s/%s", tmp, files[i]);
        remove(p);
    }
    for (CCUINT32 i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        snprintf(p, sizeof(p), "%s/%s", tmp, dirs[i]);
        fs_test_rmdir(p);
    }
}

static CCBOOL fs_test_dir_exists(const char *path)
{
#ifdef CC_WINDOWS
    struct _stat st;
    if (_stat(path, &st) != 0) return CCFALSE;
    return ((st.st_mode & _S_IFDIR) != 0) ? CCTRUE : CCFALSE;
#else
    struct stat st;
    if (stat(path, &st) != 0) return CCFALSE;
    return S_ISDIR(st.st_mode) ? CCTRUE : CCFALSE;
#endif
}

static CCBOOL fs_test_path_exists(const char *path)
{
#ifdef CC_WINDOWS
    struct _stat st;
    return (_stat(path, &st) == 0) ? CCTRUE : CCFALSE;
#else
    struct stat st;
    return (stat(path, &st) == 0) ? CCTRUE : CCFALSE;
#endif
}

static CCBOOL fs_test_file_has(const char *path, const char *expect)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return CCFALSE;
    char buf[256];
    size_t got = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[got] = '\0';
    return (strcmp(buf, expect) == 0) ? CCTRUE : CCFALSE;
}

/* 读取整个文件到堆缓冲（调用方 free）；失败返回 NULL */
static CCUINT8* fs_test_read_all(const char *path, long *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long len = ftell(fp);
    if (len < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    CCUINT8 *buf = (CCUINT8*)malloc(len > 0 ? (size_t)len : 1);
    if (!buf) { fclose(fp); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, fp) != (size_t)len)
    {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = len;
    return buf;
}

static CCBOOL fs_test_write_all(const char *path, const CCUINT8 *buf, long len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return CCFALSE;
    CCBOOL ok = (len <= 0 || fwrite(buf, 1, (size_t)len, fp) == (size_t)len)
        ? CCTRUE : CCFALSE;
    fclose(fp);
    return ok;
}

static CCINT32 fs_count_cb(void *data, void *user)
{
    (void)data;
    (*(CCUINT32*)user)++;
    return 0;
}

static CCINT32 fs_find_cb(void *data, void *user)
{
    (void)data;
    (*(CCUINT32*)user)++;
    return 0;
}

/* 收集 list 结果的 name:type:size，按回调顺序拼接。 */
typedef struct fs_names_ctx
{
    char text[2048];
} fs_names_ctx;

static CCINT32 fs_collect_entry_cb(void *data, void *user)
{
    CCFsEntry *e = (CCFsEntry*)data;
    fs_names_ctx *ctx = (fs_names_ctx*)user;
    size_t len = strlen(ctx->text);
    if (len < sizeof(ctx->text))
        snprintf(ctx->text + len, sizeof(ctx->text) - len, "%s:%d:%llu;",
                 e->name, (int)e->type, (unsigned long long)e->size);
    return 0;
}

static CCINT32 fs_collect_name_cb(void *data, void *user)
{
    CCFsEntry *e = (CCFsEntry*)data;
    fs_names_ctx *ctx = (fs_names_ctx*)user;
    size_t len = strlen(ctx->text);
    if (len < sizeof(ctx->text))
        snprintf(ctx->text + len, sizeof(ctx->text) - len, "%s;", e->name);
    return 0;
}

static CCUINT32 fs_test_le32(const CCUINT8 *p)
{
    return ((CCUINT32)p[0]) | ((CCUINT32)p[1] << 8) |
           ((CCUINT32)p[2] << 16) | ((CCUINT32)p[3] << 24);
}

static CCUINT64 fs_test_le64(const CCUINT8 *p)
{
    return ((CCUINT64)fs_test_le32(p)) |
           (((CCUINT64)fs_test_le32(p + 4)) << 32);
}

/* 按 src/ccfiles_internal.h 的格式独立读取一个单文件资源包。 */
static CCBOOL fs_test_check_pkg(const char *path, const char *name,
                                CCUINT64 size, const char *data)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return CCFALSE;

    CCUINT8 hraw[CC_FS_PKG_HEADER_SIZE];
    CCUINT8 braw[CC_FS_PKG_BLOCK_SIZE];
    CCUINT8 *name_buf = NULL;
    CCUINT8 *data_buf = NULL;
    CCBOOL ok = CCFALSE;

    if (fread(hraw, 1, sizeof(hraw), fp) != sizeof(hraw)) goto done;
    if (memcmp(hraw, "_CPK", 4) != 0) goto done;
    if (fs_test_le32(hraw + 8) != 1) goto done;   /* file_count */
    if (fs_test_le32(hraw + 12) != 1) goto done;  /* block_count */
    if (fs_test_le64(hraw + 16) != size) goto done;
    if (fs_test_le32(hraw + 28) != 0) goto done;  /* reserved */

    CCUINT32 crc = cc_crc32(hraw, 24, 0);
    crc = cc_crc32(hraw + 28, 4, crc);
    if (fs_test_le32(hraw + 24) != crc) goto done;

    if (fread(braw, 1, sizeof(braw), fp) != sizeof(braw)) goto done;
    CCUINT32 name_len = fs_test_le32(braw + 0);
    CCUINT32 name_off = fs_test_le32(braw + 4);
    CCUINT64 data_off = fs_test_le64(braw + 8);
    CCUINT64 data_size = fs_test_le64(braw + 16);
    CCUINT32 data_crc = fs_test_le32(braw + 24);
    CCUINT32 flags = fs_test_le32(braw + 28);

    if (name_len != (CCUINT32)strlen(name)) goto done;
    if (name_off != 0) goto done;
    if (data_off != (CCUINT64)CC_FS_PKG_HEADER_SIZE +
                    (CCUINT64)CC_FS_PKG_BLOCK_SIZE + name_len) goto done;
    if (data_size != size || flags != 0) goto done;
    if (data_crc != cc_crc32(data, size, 0)) goto done;

    name_buf = (CCUINT8*)malloc(name_len + 1);
    data_buf = (CCUINT8*)malloc((size_t)(size ? size : 1));
    if (!name_buf || !data_buf) goto done;

    if (fseek(fp, (long)(CC_FS_PKG_HEADER_SIZE + CC_FS_PKG_BLOCK_SIZE), SEEK_SET) != 0)
        goto done;
    if (fread(name_buf, 1, name_len, fp) != name_len) goto done;
    name_buf[name_len] = '\0';
    if (memcmp(name_buf, name, name_len) != 0) goto done;

    if (fseek(fp, (long)data_off, SEEK_SET) != 0) goto done;
    if (size > 0 && fread(data_buf, 1, (size_t)size, fp) != (size_t)size) goto done;
    if (size > 0 && memcmp(data_buf, data, (size_t)size) != 0) goto done;

    ok = CCTRUE;

done:
    free(name_buf);
    free(data_buf);
    fclose(fp);
    return ok;
}

/* 生成规范化（`/` 分隔）的绝对临时目录 */
static void fs_test_tmpdir(char *out, CCUINT32 cap)
{
    char cwd[512];
#ifdef CC_WINDOWS
    if (!_getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
#else
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
#endif
    for (CCUINT32 i = 0; cwd[i]; i++)
        if (cwd[i] == '\\') cwd[i] = '/';
    snprintf(out, cap, "%s/out/ccfiles_test_tmp", cwd);
}
/* ============================================================================
 * 用例
 * ============================================================================ */

void test_ccfiles(void)
{
    char tmp[1024];
    fs_test_tmpdir(tmp, sizeof(tmp));

    char sub[1024];
    char pkg[1024];
    char p[1200];
    snprintf(sub, sizeof(sub), "%s/sub", tmp);
    snprintf(pkg, sizeof(pkg), "%s/pkg.pkg", tmp);

    /* 预清理：上次运行若中途失败/中断，残留文件不能影响本轮结果 */
    fs_test_cleanup_artifacts(tmp);
    fs_test_mkdir(tmp);

    CC_CASE("FS-API-001", "探针、根目录与基础读写落盘");
    CCFiles *fs = ccfiles_create();
    fs_expect(fs != NULL, "创建探针成功");
    if (!fs)
        return;

    CCFsStat st;
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/", &st) == CC_FS_OK && st.type == CC_FS_TYPE_DIR,
        "根目录默认为目录");
    fs_expect(st.readonly == CCFALSE, "根目录默认非只读");

    char cwd[256];
    fs_expect(ccfiles_chdir(fs, "/") == CC_FS_OK, "chdir 根目录");
    fs_expect(ccfiles_getcwd(fs, cwd, sizeof(cwd)) > 0 && strcmp(cwd, "/") == 0,
        "getcwd 根目录为 /");

    fs_expect(ccfiles_mount(fs, "/m", tmp, CC_FS_MOUNT_DIR) == CC_FS_OK,
        "挂载真实目录到 /m");

    fs_expect(ccfiles_create_file(fs, "/m/a\\b") == CC_FS_ERR_PARAM,
        "虚拟路径拒绝反斜杠分隔符");
#ifdef CC_WINDOWS
    fs_expect(ccfiles_create_file(fs, "/m/.. /evil.txt") == CC_FS_ERR_PARAM,
        "Windows 虚拟路径拒绝段尾空格");
#endif
    fs_expect(ccfiles_create_file(fs, "/m/a.txt") == CC_FS_OK, "创建 /m/a.txt");
    const char *hello = "hello";
    fs_expect(ccfiles_write(fs, hello, 0, 5) == 5, "写入 5 字节");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/a.txt", &st) == CC_FS_OK && st.size == 5,
        "stat size 为 5");

    char rb[64];
    memset(rb, 0, sizeof(rb));
    CCINT64 rd = ccfiles_read(fs, rb, 0, sizeof(rb));
    fs_expect(rd == 5 && memcmp(rb, "hello", 5) == 0, "读回内容一致");
    fs_expect(ccfiles_read(fs, rb, 5, 10) == 0, "越过末尾返回 0（EOF）");
    /* 直写：数据在句柄的 stdio 缓冲中，flush 即落盘，无需 push */
    fs_expect(ccfiles_flush(fs) == CC_FS_OK, "flush 落盘成功");
    snprintf(p, sizeof(p), "%s/a.txt", tmp);
    fs_expect(fs_test_file_has(p, "hello"), "落盘后磁盘内容正确");
    memset(&st, 0, sizeof(st));
    ccfiles_stat(fs, "/m/a.txt", &st);
    fs_expect(st.size == 5, "flush 后 stat size 仍为 5");

    CC_CASE("FS-API-002", "跳写扩展与中间补零");
    fs_expect(ccfiles_create_file(fs, "/m/b.bin") == CC_FS_OK, "创建 /m/b.bin");
    fs_expect(ccfiles_write(fs, "XY", 4, 2) == 2, "跳写扩展文件");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/b.bin", &st) == CC_FS_OK && st.size == 6,
        "跳写后 size 为 6");
    char b6[8];
    memset(b6, 0xFF, sizeof(b6));
    rd = ccfiles_read(fs, b6, 0, 6);
    fs_expect(rd == 6 && b6[0] == 0 && b6[1] == 0 && b6[2] == 0 && b6[3] == 0 &&
        b6[4] == 'X' && b6[5] == 'Y', "跳写中间补 0");
    ccfiles_flush(fs);

    CC_CASE("FS-API-003", "refresh、子目录与 list/find");
    fs_test_mkdir(sub);
    fs_expect(ccfiles_refresh(fs, "/m") == CC_FS_OK, "刷新 /m");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/sub", &st) == CC_FS_OK && st.type == CC_FS_TYPE_DIR,
        "刷新后发现子目录 sub");

    fs_expect(ccfiles_create_file(fs, "/m/sub/c.txt") == CC_FS_OK, "创建 /m/sub/c.txt");
    fs_expect(ccfiles_write(fs, "abc", 0, 3) == 3, "写入 abc");
    ccfiles_flush(fs);
    snprintf(p, sizeof(p), "%s/sub/c.txt", tmp);
    fs_expect(fs_test_file_has(p, "abc"), "子目录文件落盘正确");

    fs_names_ctx names;
    memset(&names, 0, sizeof(names));
    fs_expect(ccfiles_list(fs, "/m", fs_collect_entry_cb, &names) == CC_FS_OK &&
        strcmp(names.text, "a.txt:2:5;b.bin:2:6;sub:1:0;") == 0,
        "list /m 的名称、类型、大小与宿主一致");

    memset(&names, 0, sizeof(names));
    fs_expect(ccfiles_find(fs, "/m", "*.txt", fs_collect_name_cb, &names) == CC_FS_OK,
        "find *.txt 成功");
    CC_CHECK_EQ_STR("find *.txt 名称", "a.txt;c.txt;", names.text);

    /* mkdir：目录与物理目录同时建立，立刻可见 */
    CC_CASE("FS-API-004", "mkdir、chdir 与 .. 语义");
    fs_expect(ccfiles_create_dir(fs, "/m/newdir") == CC_FS_OK, "创建空目录 /m/newdir");
    snprintf(p, sizeof(p), "%s/newdir", tmp);
    fs_expect(fs_test_dir_exists(p), "mkdir 后物理目录立即存在");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/newdir", &st) == CC_FS_OK && st.type == CC_FS_TYPE_DIR,
        "stat 新目录类型为 DIR");
    memset(&names, 0, sizeof(names));
    fs_expect(ccfiles_list(fs, "/m", fs_collect_entry_cb, &names) == CC_FS_OK &&
        strcmp(names.text, "a.txt:2:5;b.bin:2:6;newdir:1:0;sub:1:0;") == 0,
        "list /m 包含新建目录且顺序、类型、大小正确");
    fs_expect(ccfiles_create_dir(fs, "/m/newdir") == CC_FS_ERR_EXIST, "重复 mkdir 返回 EXIST");
    fs_expect(ccfiles_create_dir(fs, "/m/a.txt") == CC_FS_ERR_EXIST, "mkdir 已存在文件返回 EXIST");
    fs_expect(ccfiles_create_dir(fs, "/m/nope/x") == CC_FS_ERR_NOENT, "mkdir 父目录缺失返回 NOENT");
    fs_expect(ccfiles_chdir(fs, "/m/newdir") == CC_FS_OK, "chdir 进入新建目录");
    fs_expect(ccfiles_getcwd(fs, cwd, sizeof(cwd)) > 0 && strcmp(cwd, "/m/newdir") == 0,
        "getcwd 位于新建目录");
    fs_expect(ccfiles_chdir(fs, "/m") == CC_FS_OK, "返回 /m");
    fs_expect(ccfiles_chdir(fs, "..") == CC_FS_OK, "从挂载锚点 /m 返回上级");
    fs_expect(ccfiles_getcwd(fs, cwd, sizeof(cwd)) > 0 && strcmp(cwd, "/") == 0,
        "挂载锚点返回上级后 cwd 为 /");
    fs_expect(ccfiles_chdir(fs, "..") == CC_FS_OK, "根目录执行 .. 保持在根");
    fs_expect(ccfiles_getcwd(fs, cwd, sizeof(cwd)) > 0 && strcmp(cwd, "/") == 0,
        "根目录 .. 后 cwd 仍为 /");
    fs_expect(ccfiles_create_file(fs, "/m/newdir/f.txt") == CC_FS_OK, "在新目录下创建文件");
    fs_expect(ccfiles_write(fs, "x", 0, 1) == 1, "写入新目录文件");
    ccfiles_flush(fs);
    snprintf(p, sizeof(p), "%s/newdir/f.txt", tmp);
    fs_expect(fs_test_file_has(p, "x"), "新目录物理路径自动创建并落盘");

    CC_CASE("FS-API-005", "文件 cp/mv");
    fs_expect(ccfiles_cp(fs, "/m/a.txt", "/m/sub/d.txt") == CC_FS_OK,
        "cp a.txt -> sub/d.txt");
    fs_expect(ccfiles_open(fs, "/m/sub/d.txt") == CC_FS_OK, "open 复制目标");
    memset(rb, 0, sizeof(rb));
    rd = ccfiles_read(fs, rb, 0, sizeof(rb));
    fs_expect(rd == 5 && memcmp(rb, "hello", 5) == 0, "复制内容一致");
    snprintf(p, sizeof(p), "%s/sub/d.txt", tmp);
    fs_expect(fs_test_file_has(p, "hello"), "复制目标在宿主磁盘上内容正确");

    fs_expect(ccfiles_mv(fs, "/m/sub/d.txt", "/m/e.txt") == CC_FS_OK,
        "mv sub/d.txt -> e.txt");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/e.txt", &st) == CC_FS_OK, "mv 后目标存在");
    fs_expect(ccfiles_stat(fs, "/m/sub/d.txt", &st) == CC_FS_ERR_NOENT,
        "mv 后源不存在");

    CC_CASE("FS-PKG-001", "打包、解包与包挂载");
    fs_expect(ccfiles_pack(fs, "/m/sub", pkg) == CC_FS_OK, "打包 /m/sub 到资源包");
    fs_expect(fs_test_check_pkg(pkg, "c.txt", 3, "abc"),
        "资源包头/块表/数据与内部格式一致");

    /* 解包：为包文件建虚拟节点（物理路径即该包），再解到磁盘目录 */
    char unpack_dir[1024];
    snprintf(unpack_dir, sizeof(unpack_dir), "%s/unpacked", tmp);
    snprintf(p, sizeof(p), "%s/c.txt", unpack_dir); remove(p);
    fs_test_rmdir(unpack_dir);
    fs_expect(ccfiles_create_file(fs, "/m/pkg.pkg") == CC_FS_OK, "创建包文件节点");
    fs_expect(ccfiles_unpack(fs, "/m/pkg.pkg", unpack_dir) == CC_FS_OK, "解包到磁盘目录");
    snprintf(p, sizeof(p), "%s/c.txt", unpack_dir);
    fs_expect(fs_test_file_has(p, "abc"), "解包文件内容正确");
    fs_expect(ccfiles_mount(fs, "/pkg", pkg, CC_FS_MOUNT_PKG) == CC_FS_OK,
        "挂载资源包到 /pkg");
    fs_expect(ccfiles_chdir(fs, "/pkg") == CC_FS_OK, "chdir 包挂载锚点 /pkg");
    fs_expect(ccfiles_chdir(fs, "..") == CC_FS_OK, "从包挂载锚点返回上级");
    fs_expect(ccfiles_getcwd(fs, cwd, sizeof(cwd)) > 0 && strcmp(cwd, "/") == 0,
        "包挂载点返回上级后 cwd 为 /");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/pkg/c.txt", &st) == CC_FS_OK && st.readonly == CCTRUE &&
        st.size == 3 && st.crc32 == cc_crc32("abc", 3, 0),
        "包内文件只读，size/crc32 与块头一致");
    memset(&names, 0, sizeof(names));
    fs_expect(ccfiles_list(fs, "/pkg", fs_collect_entry_cb, &names) == CC_FS_OK &&
        strcmp(names.text, "c.txt:2:3;") == 0,
        "包内 list 名称、类型、大小正确");
    fs_expect(ccfiles_open(fs, "/pkg/c.txt") == CC_FS_OK, "open 包内文件");
    memset(rb, 0, sizeof(rb));
    rd = ccfiles_read(fs, rb, 0, sizeof(rb));
    fs_expect(rd == 3 && memcmp(rb, "abc", 3) == 0, "读取包内文件内容");
    fs_expect(ccfiles_write(fs, "z", 0, 1) == CC_FS_ERR_PROTECT, "包内文件写保护");
    fs_expect(ccfiles_create_dir(fs, "/pkg/x") == CC_FS_ERR_PROTECT, "包内 mkdir 返回 PROTECT");
    fs_expect(ccfiles_unmount(fs, "/pkg") == CC_FS_OK, "卸载资源包");

    /* ---------- 资源包鲁棒性：磁盘格式固定小端，结构损坏必须返回 PARAM ---------- */
    CC_CASE("FS-PKG-002", "损坏资源包与解包原子性");
    {
        long plen = 0;
        CCUINT8 *raw_pkg = fs_test_read_all(pkg, &plen);
        fs_expect(raw_pkg != NULL && plen >= 72, "资源包：读取原始字节");
        if (raw_pkg && plen >= 72)
        {
            /* 魔数为 4 字节序列 '_CPK'（5F 43 50 4B），与宿主端序无关 */
            fs_expect(raw_pkg[0] == 0x5Fu && raw_pkg[1] == 0x43u &&
                raw_pkg[2] == 0x50u && raw_pkg[3] == 0x4Bu,
                "资源包：磁盘魔数为 _CPK");

            char bad[1200];
            CCUINT8 *copy = (CCUINT8*)malloc((size_t)plen);
            fs_expect(copy != NULL, "资源包：分配损坏样本缓冲");
            if (copy)
            {
                /* header_crc 位于偏移 24 */
                memcpy(copy, raw_pkg, (size_t)plen);
                copy[24] ^= 0xFFu;
                snprintf(bad, sizeof(bad), "%s/bad_crc.pkg", tmp);
                fs_test_write_all(bad, copy, plen);
                fs_expect(ccfiles_mount(fs, "/badcrc", bad, CC_FS_MOUNT_PKG) == CC_FS_ERR_PARAM,
                    "坏包：header_crc 不符返回 PARAM");
                remove(bad);

                /* 截断数据区尾部 */
                memcpy(copy, raw_pkg, (size_t)plen);
                snprintf(bad, sizeof(bad), "%s/bad_trunc.pkg", tmp);
                fs_test_write_all(bad, copy, plen - 4);
                fs_expect(ccfiles_mount(fs, "/badtrunc", bad, CC_FS_MOUNT_PKG) == CC_FS_ERR_PARAM,
                    "坏包：截断返回 PARAM");
                remove(bad);

                /* block[0].offset（偏移 40，u64 小端）越界 */
                memcpy(copy, raw_pkg, (size_t)plen);
                copy[40] = 0xFFu; copy[41] = 0xFFu; copy[42] = 0xFFu; copy[43] = 0x7Fu;
                snprintf(bad, sizeof(bad), "%s/bad_off.pkg", tmp);
                fs_test_write_all(bad, copy, plen);
                fs_expect(ccfiles_mount(fs, "/badoff", bad, CC_FS_MOUNT_PKG) == CC_FS_ERR_PARAM,
                    "坏包：数据偏移越界返回 PARAM");
                remove(bad);

                /* 名字区起点 = 32 + block_count*32 = 64；把 "c.txt" 改成 "../xx" */
                memcpy(copy, raw_pkg, (size_t)plen);
                memcpy(copy + 64, "../xx", 5);
                snprintf(bad, sizeof(bad), "%s/bad_name.pkg", tmp);
                fs_test_write_all(bad, copy, plen);
                fs_expect(ccfiles_mount(fs, "/badname", bad, CC_FS_MOUNT_PKG) == CC_FS_ERR_PARAM,
                    "坏包：名字含 .. 返回 PARAM");
                remove(bad);

                /* 名字段以空格 / 点结尾：Windows 会归一化，必须拒绝以免写出 staging */
                memcpy(copy, raw_pkg, (size_t)plen);
                memcpy(copy + 64, ".. /x", 5);
                char trail_pkg[1200];
                char trail_out[1200];
                char trail_part[1200];
                snprintf(trail_pkg, sizeof(trail_pkg), "%s/bad_trail.pkg", tmp);
                snprintf(trail_out, sizeof(trail_out), "%s/bad_trail_out", tmp);
                snprintf(trail_part, sizeof(trail_part), "%s/bad_trail_out.part", tmp);
                fs_test_write_all(trail_pkg, copy, plen);
                CCINT32 trail_mount = ccfiles_mount(fs, "/badtrail", trail_pkg, CC_FS_MOUNT_PKG);
                fs_expect(trail_mount == CC_FS_ERR_PARAM,
                    "坏包：名字段以空格结尾返回 PARAM");
                if (trail_mount == CC_FS_OK)
                    ccfiles_unmount(fs, "/badtrail");
                fs_expect(ccfiles_create_file(fs, "/m/bad_trail.pkg") == CC_FS_OK,
                    "坏包：创建段尾空格包文件节点");
                fs_expect(ccfiles_unpack(fs, "/m/bad_trail.pkg", trail_out) == CC_FS_ERR_PARAM,
                    "坏包解包：段尾空格名字返回 PARAM");
                fs_expect(!fs_test_path_exists(trail_out),
                    "坏包解包：段尾空格目标目录未发布");
                fs_expect(!fs_test_path_exists(trail_part),
                    "坏包解包：段尾空格 .part 已清理");
                char escaped[1200];
                snprintf(escaped, sizeof(escaped), "%s/x", tmp);
                fs_expect(!fs_test_path_exists(escaped),
                    "坏包解包：不得写出 staging 目录");
                ccfiles_remove(fs, "/m/bad_trail.pkg");
                remove(trail_pkg);

                /* 解包失败必须不可见：目标目录不出现，staging 的 .part 不残留 */
                char bad_pkg[1200];
                char bad_out[1200];
                char bad_part[1200];

                memcpy(copy, raw_pkg, (size_t)plen);
                snprintf(bad_pkg, sizeof(bad_pkg), "%s/bad_unpack.pkg", tmp);
                snprintf(bad_out, sizeof(bad_out), "%s/bad_unpacked", tmp);
                snprintf(bad_part, sizeof(bad_part), "%s/bad_unpacked.part", tmp);
                fs_test_write_all(bad_pkg, copy, plen - 4);
                fs_expect(ccfiles_create_file(fs, "/m/bad_unpack.pkg") == CC_FS_OK,
                    "坏包解包：创建包文件节点");
                fs_expect(ccfiles_unpack(fs, "/m/bad_unpack.pkg", bad_out) == CC_FS_ERR_PARAM,
                    "坏包解包：截断数据返回 PARAM");
                fs_expect(!fs_test_path_exists(bad_out), "坏包解包：目标目录未出现");
                fs_expect(!fs_test_path_exists(bad_part), "坏包解包：.part staging 无残留");
                ccfiles_remove(fs, "/m/bad_unpack.pkg");
                remove(bad_pkg);

#ifdef CC_WINDOWS
                /* 头部通过但块名在 Windows 非法：staging 已建目录后写文件失败，必须整体回滚 */
                memcpy(copy, raw_pkg, (size_t)plen);
                memcpy(copy + 64, "c/:x?", 5);
                snprintf(bad_pkg, sizeof(bad_pkg), "%s/bad_stage.pkg", tmp);
                snprintf(bad_out, sizeof(bad_out), "%s/bad_stage_out", tmp);
                snprintf(bad_part, sizeof(bad_part), "%s/bad_stage_out.part", tmp);
                fs_test_write_all(bad_pkg, copy, plen);
                fs_expect(ccfiles_create_file(fs, "/m/bad_stage.pkg") == CC_FS_OK,
                    "坏包解包：创建 staging 回归包节点");
                fs_expect(ccfiles_unpack(fs, "/m/bad_stage.pkg", bad_out) == CC_FS_ERR_IO,
                    "坏包解包：写入阶段失败返回 IO");
                fs_expect(!fs_test_path_exists(bad_out), "坏包解包：失败后目标未发布");
                fs_expect(!fs_test_path_exists(bad_part), "坏包解包：失败后 .part 已清理");
                ccfiles_remove(fs, "/m/bad_stage.pkg");
                remove(bad_pkg);
#endif

                free(copy);
            }
            free(raw_pkg);
        }
    }

    CC_CASE("FS-API-006", "常用错误码");
    fs_expect(ccfiles_open(fs, "/nope") == CC_FS_ERR_NOENT, "open 不存在返回 NOENT");
    fs_expect(ccfiles_open(fs, "/m") == CC_FS_ERR_ISDIR, "open 目录返回 ISDIR");
    fs_expect(ccfiles_chdir(fs, "/m/a.txt") == CC_FS_ERR_NOTDIR, "chdir 文件返回 NOTDIR");
    fs_expect(ccfiles_remove(fs, "/m/sub") == CC_FS_ERR_NOTEMPTY,
        "删除非空目录返回 NOTEMPTY");
    CC_CASE("FS-MV-001", "目录 mv");
    fs_expect(ccfiles_mv(fs, "/m/sub", "/m/sub2") == CC_FS_OK, "mv 目录 sub -> sub2");
    memset(&st, 0, sizeof(st));
    fs_expect(ccfiles_stat(fs, "/m/sub2/c.txt", &st) == CC_FS_OK,
        "mv 目录后子文件存在");
    fs_expect(ccfiles_stat(fs, "/m/sub", &st) == CC_FS_ERR_NOENT,
        "mv 目录后源不存在");
    snprintf(p, sizeof(p), "%s/sub2/c.txt", tmp);
    fs_expect(fs_test_file_has(p, "abc"), "mv 目录同锚点：目标物理文件存在");
    snprintf(p, sizeof(p), "%s/sub/c.txt", tmp);
    fs_expect(!fs_test_file_has(p, "abc"), "mv 目录同锚点：源物理文件已原子移走");

    /* ---------- mv 虚拟文件：copy 分支（句柄随 swap-remove 搬迁的回归） ---------- */
    CC_CASE("FS-MV-002", "内存文件 mv");
    {
        fs_expect(ccfiles_create_file(fs, "/m/mvmem.txt") == CC_FS_OK, "mvmem：创建内存文件");
        fs_expect(ccfiles_write(fs, "move-me", 0, 7) == 7, "mvmem：写入 7 字节");
        fs_expect(ccfiles_mv(fs, "/m/mvmem.txt", "/m/mvmem2.txt") == CC_FS_OK, "mvmem：mv 改名");
        memset(&st, 0, sizeof(st));
        fs_expect(ccfiles_stat(fs, "/m/mvmem2.txt", &st) == CC_FS_OK && st.size == 7,
            "mvmem：改名后 size 正确");
        /* mv 结束即关闭句柄：无需任何 flush / push，磁盘已是最新 */
        snprintf(p, sizeof(p), "%s/mvmem2.txt", tmp);
        fs_expect(fs_test_file_has(p, "move-me"),
            "mvmem：改名后磁盘内容立即正确");
        snprintf(p, sizeof(p), "%s/mvmem.txt", tmp);
        fs_expect(!fs_test_file_has(p, "move-me"), "mvmem：源物理文件已删");
    }
    CC_CASE("FS-API-007", "删除文件");
    fs_expect(ccfiles_remove(fs, "/m/a.txt") == CC_FS_OK, "删除文件成功");
    fs_expect(ccfiles_stat(fs, "/m/a.txt", &st) == CC_FS_ERR_NOENT, "删除后不存在");

    /* ---------- mv 失败：源实物已丢失时必须保留目标，不能先删再 rename ---------- */
    CC_CASE("FS-MV-003", "mv 失败保留目标");
    {
        fs_expect(ccfiles_create_file(fs, "/m/mvfail_src.txt") == CC_FS_OK,
            "mvfail：创建源");
        fs_expect(ccfiles_write(fs, "SRC", 0, 3) == 3, "mvfail：写源");
        fs_expect(ccfiles_create_file(fs, "/m/mvfail_dst.txt") == CC_FS_OK,
            "mvfail：创建目标");
        fs_expect(ccfiles_write(fs, "DST", 0, 3) == 3, "mvfail：写目标");
        fs_expect(ccfiles_flush(fs) == CC_FS_OK, "mvfail：落盘");

        snprintf(p, sizeof(p), "%s/mvfail_src.txt", tmp);
        remove(p);
        fs_expect(ccfiles_mv(fs, "/m/mvfail_src.txt", "/m/mvfail_dst.txt") != CC_FS_OK,
            "mvfail：源实物缺失返回失败");
        snprintf(p, sizeof(p), "%s/mvfail_dst.txt", tmp);
        fs_expect(fs_test_file_has(p, "DST"),
            "mvfail：失败后目标数据仍保留");

        fs_expect(ccfiles_remove(fs, "/m/mvfail_src.txt") == CC_FS_OK,
            "mvfail：清理源节点");
        fs_expect(ccfiles_remove(fs, "/m/mvfail_dst.txt") == CC_FS_OK,
            "mvfail：清理目标节点");
        remove(p);
    }

    /* ---------- rm：物理删除立即生效，refresh 不会复活 ---------- */
    CC_CASE("FS-RM-001", "rm 后 refresh 不复活");
    {
        char p1[1200];
        snprintf(p1, sizeof(p1), "%s/a.txt", tmp);
        fs_expect(!fs_test_file_has(p1, "hello"), "rm 后磁盘文件立即消失（无需 push）");
        fs_expect(ccfiles_refresh(fs, "/m") == CC_FS_OK, "rm 后 refresh /m");
        fs_expect(ccfiles_stat(fs, "/m/a.txt", &st) == CC_FS_ERR_NOENT,
            "refresh 后仍不存在");
        fs_expect(!fs_test_file_has(p1, "hello"), "refresh 后磁盘文件仍不存在");
    }

    /* ---------- rm -rf：递归删除非空目录，物理删除立即生效 ---------- */
    CC_CASE("FS-RMRF-001", "递归删除非空目录");
    {
        fs_expect(ccfiles_create_dir(fs, "/m/rtree") == CC_FS_OK, "rm-rf：建目录");
        fs_expect(ccfiles_create_dir(fs, "/m/rtree/a") == CC_FS_OK, "rm-rf：建子目录");
        fs_expect(ccfiles_create_file(fs, "/m/rtree/a/b.txt") == CC_FS_OK, "rm-rf：建文件");
        fs_expect(ccfiles_write(fs, "X", 0, 1) == 1, "rm-rf：写文件");
        ccfiles_flush(fs);
        char p2[1200];
        snprintf(p2, sizeof(p2), "%s/rtree/a/b.txt", tmp);
        fs_expect(fs_test_file_has(p2, "X"), "rm-rf：磁盘文件存在（无需 push）");

        fs_expect(ccfiles_remove_recursive(fs, "/m/rtree") == CC_FS_OK, "rm-rf：递归删除");
        fs_expect(ccfiles_stat(fs, "/m/rtree", &st) == CC_FS_ERR_NOENT, "rm-rf：目录已删");
        fs_expect(!fs_test_file_has(p2, "X"), "rm-rf：磁盘文件立即已删");
        snprintf(p2, sizeof(p2), "%s/rtree", tmp);
        fs_expect(!fs_test_dir_exists(p2), "rm-rf：磁盘目录立即已删");
    }

    /* ---------- 链接安全：junction/symlink 只删链接本身，禁止下钻目标 ---------- */
    CC_CASE("FS-LINK-001", "目录链接安全");
    {
        char linkp[1200];
        char outp[1200];
        char sentp[1200];
        snprintf(linkp, sizeof(linkp), "%s/link_j", tmp);
        snprintf(outp, sizeof(outp), "%s/outside_j", tmp);
        snprintf(sentp, sizeof(sentp), "%s/outside_j/sentinel.txt", tmp);
        fs_test_mkdir(outp);
        fs_test_write_all(sentp, (const CCUINT8 *)"KEEP", 4);

        if (fs_test_make_dir_link(linkp, outp))
        {
            fs_expect(ccfiles_refresh(fs, "/m") == CC_FS_OK, "链接：refresh /m");
            CCFsStat lk;
            memset(&lk, 0, sizeof(lk));
            fs_expect(ccfiles_stat(fs, "/m/link_j", &lk) == CC_FS_OK &&
                lk.type == CC_FS_TYPE_FILE, "链接：投影为文件项，不当作目录");
            fs_expect(ccfiles_remove_recursive(fs, "/m/link_j") == CC_FS_OK,
                "链接：递归删除只删链接");
            fs_expect(fs_test_file_has(sentp, "KEEP"),
                "链接：目标目录内的哨兵文件仍存在");
            fs_expect(!fs_test_path_exists(linkp), "链接：链接本身已删除");
        }
        else
        {
            CC_SKIP("当前环境不支持目录链接");
        }
    }

    /* ---------- 逐字节写入：句柄常驻，1 字节 × N 次仍与磁盘一致 ---------- */
    CC_CASE("FS-BYTE-001", "逐字节写入");
    {
        fs_expect(ccfiles_create_file(fs, "/m/byte.txt") == CC_FS_OK, "逐字节：创建文件");
        CCBOOL w_ok = CCTRUE;
        for (CCUINT32 i = 0; i < 4096; i++)
        {
            char ch = (char)('A' + (i % 26));
            if (ccfiles_write(fs, &ch, i, 1) != 1) { w_ok = CCFALSE; break; }
        }
        fs_expect(w_ok, "逐字节：连续写 4096 字节");
        memset(&st, 0, sizeof(st));
        fs_expect(ccfiles_stat(fs, "/m/byte.txt", &st) == CC_FS_OK && st.size == 4096,
            "逐字节：stat size 为 4096");
        fs_expect(ccfiles_flush(fs) == CC_FS_OK, "逐字节：flush");

        CCBOOL disk_ok = CCFALSE;
        snprintf(p, sizeof(p), "%s/byte.txt", tmp);
        FILE *bf = fopen(p, "rb");
        if (bf)
        {
            CCUINT32 n = 0;
            int c;
            while ((c = fgetc(bf)) != EOF)
            {
                if ((char)c != (char)('A' + (n % 26))) break;
                n++;
            }
            fclose(bf);
            disk_ok = (n == 4096) ? CCTRUE : CCFALSE;
        }
        fs_expect(disk_ok, "逐字节：磁盘内容逐字节正确");
    }

    /* ---------- mv / rm 立即落盘：不留旧文件残留 ---------- */
    CC_CASE("FS-PERSIST-001", "mv/rm 立即落盘");
    {
        fs_expect(ccfiles_create_file(fs, "/m/res.txt") == CC_FS_OK, "立即 mv：创建文件");
        fs_expect(ccfiles_write(fs, "residue", 0, 7) == 7, "立即 mv：写入");
        fs_expect(ccfiles_mv(fs, "/m/res.txt", "/m/res2.txt") == CC_FS_OK, "立即 mv：改名");
        char p_r[1200];
        snprintf(p_r, sizeof(p_r), "%s/res2.txt", tmp);
        fs_expect(fs_test_file_has(p_r, "residue"), "立即 mv：目标磁盘内容正确（未 flush）");
        snprintf(p_r, sizeof(p_r), "%s/res.txt", tmp);
        fs_expect(!fs_test_file_has(p_r, "residue"), "立即 mv：源磁盘文件无残留");
        fs_expect(ccfiles_remove(fs, "/m/res2.txt") == CC_FS_OK, "立即 rm：删除文件");
        snprintf(p_r, sizeof(p_r), "%s/res2.txt", tmp);
        fs_expect(!fs_test_file_has(p_r, "residue"), "立即 rm：磁盘文件同步删除");
    }

    /* ---------- 超长路径：系统路径上限内逐层创建并端到端读写 ---------- */
    CC_CASE("FS-LONG-001", "超长路径全链路");
    {
        const CCUINT32 depth = 5, comp_len = 20;
        char long_path[16000];
        CCUINT32 pos = 0;
        memcpy(long_path, "/long", 6);
        pos = 5;

        char longpkg[1200];
        snprintf(longpkg, sizeof(longpkg), "%s/longpkg.pkg", tmp);
        remove(longpkg);

        fs_expect(ccfiles_create_dir(fs, long_path) == CC_FS_OK, "长路径：创建根目录 /long");

        CCBOOL all_ok = CCTRUE;
        for (CCUINT32 i = 0; i < depth; i++)
        {
            long_path[pos++] = '/';
            for (CCUINT32 j = 0; j < comp_len; j++)
                long_path[pos++] = (char)('a' + (i % 26));
            long_path[pos] = '\0';
            if (ccfiles_create_dir(fs, long_path) != CC_FS_OK) { all_ok = CCFALSE; break; }
        }
        fs_expect(all_ok, "长路径：逐层创建 5 层目录");

        CCUINT32 leaf_pos = pos;
        const char *leaf = "/leaf.txt";
        memcpy(long_path + leaf_pos, leaf, strlen(leaf) + 1);
        fs_expect(ccfiles_create_file(fs, long_path) == CC_FS_OK, "长路径：创建叶子文件");
        fs_expect(ccfiles_write(fs, "LONG", 0, 4) == 4, "长路径：写入叶子文件");

        CCFsStat lst;
        memset(&lst, 0, sizeof(lst));
        fs_expect(ccfiles_stat(fs, long_path, &lst) == CC_FS_OK && lst.size == 4,
            "长路径：stat 叶子文件");

        char lrb[16];
        memset(lrb, 0, sizeof(lrb));
        fs_expect(ccfiles_read(fs, lrb, 0, sizeof(lrb)) == 4 && memcmp(lrb, "LONG", 4) == 0,
            "长路径：读回叶子文件");

        /* 父目录 list / find */
        long_path[leaf_pos] = '\0';
        CCUINT32 lcount = 0;
        fs_expect(ccfiles_list(fs, long_path, fs_count_cb, &lcount) == CC_FS_OK && lcount == 1,
            "长路径：list 父目录命中 1 项");
        lcount = 0;
        fs_expect(ccfiles_find(fs, long_path, "*.txt", fs_find_cb, &lcount) == CC_FS_OK && lcount == 1,
            "长路径：find *.txt 命中 1 项");

        /* getcwd 还原完整长路径 */
        char *lcwd = (char*)malloc(16000);
        CCBOOL cwd_ok = CCFALSE;
        if (lcwd && ccfiles_chdir(fs, long_path) == CC_FS_OK)
        {
            CCINT32 n = ccfiles_getcwd(fs, lcwd, 16000);
            cwd_ok = (n > 0 && strcmp(lcwd, long_path) == 0);
            ccfiles_chdir(fs, "/");
        }
        fs_expect(cwd_ok, "长路径：getcwd 还原完整路径");
        free(lcwd);

        /* 恢复完整路径后打包（长名称写入短路径包文件） */
        memcpy(long_path + leaf_pos, leaf, strlen(leaf) + 1);
        fs_expect(ccfiles_pack(fs, "/long", longpkg) == CC_FS_OK,
            "长路径：打包为短路径包文件");

        /* 挂载包并读回长路径文件（验证包内长名称处理） */
        char *lp_path = (char*)malloc(16000);
        CCBOOL pk_ok = CCFALSE;
        if (lp_path)
        {
            snprintf(lp_path, 16000, "/lp%.*s%s",
                (int)(leaf_pos - 5), long_path + 5, long_path + leaf_pos);
            fs_expect(ccfiles_mount(fs, "/lp", longpkg, CC_FS_MOUNT_PKG) == CC_FS_OK,
                "长路径：挂载资源包到 /lp");
            memset(&lst, 0, sizeof(lst));
            if (ccfiles_stat(fs, lp_path, &lst) == CC_FS_OK && lst.size == 4 &&
                ccfiles_open(fs, lp_path) == CC_FS_OK)
            {
                memset(lrb, 0, sizeof(lrb));
                if (ccfiles_read(fs, lrb, 0, sizeof(lrb)) == 4 &&
                    memcmp(lrb, "LONG", 4) == 0)
                    pk_ok = CCTRUE;
            }
            fs_expect(pk_ok, "长路径：包内长路径文件可读");
            fs_expect(ccfiles_unmount(fs, "/lp") == CC_FS_OK, "长路径：卸载资源包");
        }
        free(lp_path);
        remove(longpkg);
    }

    /* ---------- 覆盖挂载 / 卸载 / 刷新：内存节点与常驻句柄跨事务保留 ---------- */
    CC_CASE("FS-TXN-001", "覆盖挂载/卸载/刷新事务");
    {
        fs_expect(ccfiles_create_dir(fs, "/m/keepdir") == CC_FS_OK,
            "事务：创建内存目录");
        fs_expect(ccfiles_create_file(fs, "/m/keepdir/keep.txt") == CC_FS_OK,
            "事务：创建嵌套内存文件");
        fs_expect(ccfiles_write(fs, "nested", 0, 6) == 6,
            "事务：写入嵌套内存文件");
        /* 用第二个探针保持嵌套文件句柄常驻，验证递归迁移而非仅结构保留 */
        CCFiles *fs2 = ccfiles_create();
        fs_expect(fs2 != NULL &&
            ccfiles_open(fs2, "/m/keepdir/keep.txt") == CC_FS_OK,
            "事务：保持嵌套文件句柄");
        fs_expect(ccfiles_create_file(fs, "/m/keepmem.txt") == CC_FS_OK,
            "事务：创建内存文件");
        fs_expect(ccfiles_write(fs, "mem", 0, 3) == 3, "事务：写入内存文件");

        fs_expect(ccfiles_mount(fs, "/m", tmp, CC_FS_MOUNT_DIR) == CC_FS_OK,
            "事务：覆盖挂载成功");
        memset(&st, 0, sizeof(st));
        fs_expect(ccfiles_stat(fs, "/m/keepmem.txt", &st) == CC_FS_OK && st.size == 3,
            "事务：覆盖挂载保留内存节点");
        fs_expect(ccfiles_stat(fs, "/m/byte.txt", &st) == CC_FS_OK && st.size == 4096,
            "事务：覆盖挂载重建磁盘投影");
        fs_expect(ccfiles_stat(fs, "/m/keepdir/keep.txt", &st) == CC_FS_OK && st.size == 6,
            "事务：覆盖挂载递归保留嵌套内存节点");

        char missing[1100];
        snprintf(missing, sizeof(missing), "%s/no_such_anchor", tmp);
        fs_expect(ccfiles_mount(fs, "/m", missing, CC_FS_MOUNT_DIR) == CC_FS_ERR_NOENT,
            "事务：无效覆盖挂载返回 NOENT");
        fs_expect(ccfiles_stat(fs, "/m/keepmem.txt", &st) == CC_FS_OK && st.size == 3,
            "事务：失败覆盖挂载不改动现有树");

        fs_expect(ccfiles_unmount(fs, "/m") == CC_FS_OK, "事务：卸载锚点");
        memset(&st, 0, sizeof(st));
        fs_expect(ccfiles_stat(fs, "/m/keepmem.txt", &st) == CC_FS_OK && st.size == 3,
            "事务：卸载保留内存节点");
        fs_expect(ccfiles_stat(fs, "/m/unpacked", &st) == CC_FS_ERR_NOENT,
            "事务：卸载移除磁盘投影");
        memset(rb, 0, sizeof(rb));
        fs_expect(ccfiles_read(fs, rb, 0, sizeof(rb)) == 3 && memcmp(rb, "mem", 3) == 0,
            "事务：卸载后常驻句柄仍可读");
        fs_expect(ccfiles_stat(fs, "/m/keepdir/keep.txt", &st) == CC_FS_OK && st.size == 6,
            "事务：卸载保留嵌套内存节点");
        memset(rb, 0, sizeof(rb));
        fs_expect(fs2 && ccfiles_read(fs2, rb, 0, sizeof(rb)) == 6 &&
            memcmp(rb, "nested", 6) == 0,
            "事务：卸载后嵌套常驻句柄仍可读");
        if (fs2) ccfiles_destroy(fs2);

        fs_expect(ccfiles_mount(fs, "/m", tmp, CC_FS_MOUNT_DIR) == CC_FS_OK,
            "事务：重新挂载真实目录");
        fs_expect(ccfiles_refresh(fs, "/m") == CC_FS_OK, "事务：刷新挂载点");
        fs_expect(ccfiles_open(fs, "/m/keepmem.txt") == CC_FS_OK &&
            ccfiles_read(fs, rb, 0, sizeof(rb)) == 3 && memcmp(rb, "mem", 3) == 0,
            "事务：刷新保留内存节点与句柄");
        fs_expect(ccfiles_remove(fs, "/m/keepmem.txt") == CC_FS_OK,
            "事务：清理内存文件");
    }

    /* ---------- rm -rf /：清空根内容并同步删除物理文件，保留根节点 ---------- */
    CC_CASE("FS-RMRFROOT-001", "rm -rf /");
    {
        fs_expect(ccfiles_remove_recursive(fs, "/") == CC_FS_OK, "rm -rf /：清空根");
        fs_expect(ccfiles_stat(fs, "/", &st) == CC_FS_OK && st.type == CC_FS_TYPE_DIR,
            "rm -rf /：根仍为目录");
        fs_expect(ccfiles_stat(fs, "/m", &st) == CC_FS_ERR_NOENT, "rm -rf /：/m 已删");
    }

    ccfiles_destroy(fs);

    /* 清理磁盘临时文件：无论本轮成功与否都恢复干净现场 */
    fs_test_cleanup_artifacts(tmp);
    fs_test_rmdir(tmp);
    CC_CASE_END();
}

