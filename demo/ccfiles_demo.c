/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 统一文件管理内核（ccfiles）的类 shell 交互演示。
 *               启动后输入 -h 查看命令帮助；输入 quit 退出。
 *               与测试程序 test 并列构建为独立的 ccfiles_demo 可执行文件。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include <ccfiles.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_LINE_MAX  4096
#define DEMO_IO_MAX    2048
#define DEMO_HEX_MAX   8192

/* 演示状态：仅持有探针 */
typedef struct demo_state {
    CCFiles *fs;
} DemoState;

/* ============================================================================
 * 行内解析：按空白取词，剩余部分原样返回
 * ========================================================================== */

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

/* 取一个空白分隔的词；超出目标缓冲容量时明确失败，绝不静默截断 */
static CCBOOL take_tok(const char **p, char *out, CCUINT32 cap)
{
    skip_ws(p);
    if (**p == '\0') return CCFALSE;
    if (cap == 0) return CCFALSE;
    CCUINT32 n = 0;
    while (**p != '\0' && **p != ' ' && **p != '\t')
    {
        if (n + 1 >= cap)
        {
            out[0] = '\0';
            return CCFALSE;   /* 词过长：拒绝，而不是截断成另一个路径 */
        }
        out[n++] = **p;
        (*p)++;
    }
    out[n] = '\0';
    return CCTRUE;
}

static const char* rest_of(const char **p)
{
    skip_ws(p);
    return **p ? *p : NULL;
}

static CCBOOL parse_u64(const char *s, CCUINT64 *out)
{
    if (!s || *s == '\0') return CCFALSE;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 0);
    if (end == s || (end && *end != '\0')) return CCFALSE;
    *out = (CCUINT64)v;
    return CCTRUE;
}

/* ============================================================================
 * 十六进制字节流解析：允许空白 / 逗号 / 下划线 / 冒号分隔及 0x 前缀
 * ========================================================================== */

static CCINT32 hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* 返回 0 成功；-1 非法字符或半字节不成对；-2 超出容量 */
static CCINT32 parse_hex_stream(const char *s, unsigned char *out,
    CCUINT32 cap, CCUINT32 *out_len)
{
    CCUINT32 n = 0;
    CCINT32 hi = -1;
    while (*s)
    {
        char ch = *s++;
        if (ch == ' ' || ch == '\t' || ch == ',' || ch == '_' || ch == ':')
            continue;
        if (hi < 0 && ch == '0' && (*s == 'x' || *s == 'X')) { s++; continue; }
        CCINT32 v = hex_val(ch);
        if (v < 0) return -1;
        if (hi < 0)
        {
            hi = v;
        }
        else
        {
            if (n >= cap) return -2;
            out[n++] = (unsigned char)((hi << 4) | v);
            hi = -1;
        }
    }
    if (hi >= 0) return -1;
    *out_len = n;
    return 0;
}

/* ============================================================================
 * 打印辅助
 * ========================================================================== */

static void print_hexdump(const unsigned char *buf, CCUINT32 len, CCUINT64 base)
{
    CCUINT32 i;
    for (i = 0; i < len; i += 16)
    {
        CCUINT32 j;
        printf("  %08llX  ", (unsigned long long)(base + i));
        for (j = 0; j < 16; j++)
        {
            if (i + j < len) printf("%02X ", buf[i + j]);
            else printf("   ");
        }
        printf(" |");
        for (j = 0; j < 16 && i + j < len; j++)
        {
            unsigned char c = buf[i + j];
            printf("%c", (c >= 32 && c < 127) ? (char)c : '.');
        }
        printf("|\n");
    }
}

static const char* type_name(CCFsType t)
{
    if (t == CC_FS_TYPE_DIR) return "DIR";
    if (t == CC_FS_TYPE_FILE) return "FILE";
    return "NONE";
}

static const char* fs_result_name(CCINT32 r)
{
    switch (r)
    {
    case CC_FS_OK:            return "OK";
    case CC_FS_ERR_PARAM:     return "PARAM";
    case CC_FS_ERR_NOENT:     return "NOENT";
    case CC_FS_ERR_NOTDIR:    return "NOTDIR";
    case CC_FS_ERR_ISDIR:     return "ISDIR";
    case CC_FS_ERR_PROTECT:   return "PROTECT";
    case CC_FS_ERR_PERM:      return "PERM";
    case CC_FS_ERR_NOSPACE:   return "NOSPACE";
    case CC_FS_ERR_EXIST:     return "EXIST";
    case CC_FS_ERR_NOTEMPTY:  return "NOTEMPTY";
    case CC_FS_ERR_IO:        return "IO";
    case CC_FS_ERR_NOTMOUNT:  return "NOTMOUNT";
    default:                  return "UNKNOWN";
    }
}

static void print_result(const char *what, CCINT32 r)
{
    if (r < 0)
    {
        printf("[失败] %s: %s (%d)\n", what, fs_result_name(r), (int)r);
    }
    else
    {
        printf("[完成] %s\n", what);
    }
}

static CCINT32 cb_list(void *data, void *user)
{
    const CCFsEntry *e = (const CCFsEntry*)data;
    if (e->type == CC_FS_TYPE_DIR)
        printf("  %-28s <DIR>\n", e->name);
    else
        printf("  %-28s %llu B\n", e->name, (unsigned long long)e->size);
    (*(CCUINT32*)user)++;
    return 0;
}

static CCINT32 cb_find(void *data, void *user)
{
    const CCFsEntry *e = (const CCFsEntry*)data;
    printf("  %s%s\n", e->name,
        (e->type == CC_FS_TYPE_DIR) ? "/" : "");
    (*(CCUINT32*)user)++;
    return 0;
}

static void print_help(void)
{
    printf(
"命令一览（<> 必填，[] 可选）：\n"
"  导航/查询\n"
"    pwd                         显示当前工作目录\n"
"    cd <path>                   切换目录\n"
"    ls [path]                   列出目录（默认当前目录）\n"
"    find <path> <pattern>       按通配符查找（如 *.txt）\n"
"    stat <path>                 查看类型/大小/时间/CRC\n"
"  挂载\n"
"    mount <v> <real> <dir|pkg|file>  挂载真实目录/资源包/单文件\n"
"    unmount <v>                 卸载\n"
"    refresh <v>                 重新扫描真实目录\n"
"    （real 为宿主路径，用 / 分隔；相对路径基于 exe 同层 res；/x 在 Windows 指当前盘根，别与虚拟路径混淆）\n"
"  读写（作用于最近一次 open/create 的文件）\n"
"    open <path>                 打开文件\n"
"    create <path>               创建文件（并设为当前文件）\n"
"    read [<off> <size>]         读取并显示文本 + 十六进制\n"
"    read -x <off> <size>        仅以十六进制转储显示\n"
"    write <off> <text>          文本模式写入（字面量，不做转义）\n"
"    write -t <off> <text>       同上，显式文本模式（仍需 <off>）\n"
"    write -x <off> <hex>        十六进制字节流写入任意二进制\n"
"    flush                       立即把写缓冲刷到磁盘（其他程序可读到）\n"
"  文件操作\n"
"    mkdir <path>                创建空目录（同步落盘）\n"
"    rm <path>                   删除文件 / 空目录\n"
"    rmr <path>                  递归删除（rm -rf）\n"

"    cp <src> <dst>              复制\n"
"    mv <src> <dst>              移动/重命名\n"
"    pack <v> <real>             把虚拟目录打包为资源包文件\n"
"    unpack <v> <real>           把资源包（经虚拟文件）解包到真实目录\n"
"\n"
"示例：\n"
"  create /m/demo.bin\n"
"  write -x 0 DE AD BE EF 00 01 02\n"
"  read -x 0 16\n"
"  flush\n"
"  pack /m demo.pkg\n"
"\n"
"输入 quit 退出。\n");
}

/* ============================================================================
 * 命令实现
 * ========================================================================== */

static void cmd_write(DemoState *st, const char **pp)
{
    const char *p = *pp;
    char t1[32], t2[64];
    CCBOOL is_hex = CCFALSE;

    if (!take_tok(&p, t1, sizeof(t1)))
    {
        printf("用法: write [-t|-x] <offset> <内容>\n");
        return;
    }
    if (strcmp(t1, "-x") == 0) is_hex = CCTRUE;
    else if (strcmp(t1, "-t") == 0) is_hex = CCFALSE;

    if (t1[0] == '-')
    {
        if (!take_tok(&p, t2, sizeof(t2)))
        {
            printf("缺少偏移\n");
            return;
        }
    }
    else
    {
        strncpy(t2, t1, sizeof(t2) - 1);
        t2[sizeof(t2) - 1] = '\0';
    }

    CCUINT64 off = 0;
    if (!parse_u64(t2, &off))
    {
        printf("偏移非法: %s\n", t2);
        return;
    }
    char cur[512];
    if (ccfiles_getcur(st->fs, cur, sizeof(cur)) == CC_FS_ERR_NOENT)
    {
        printf("请先 open 或 create 一个文件\n");
        return;
    }

    const char *payload = rest_of(&p);
    if (!payload)
    {
        printf("缺少写入内容\n");
        return;
    }

    static unsigned char buf[DEMO_HEX_MAX];
    CCUINT32 n = 0;
    if (is_hex)
    {
        CCINT32 r = parse_hex_stream(payload, buf, sizeof(buf), &n);
        if (r == -1)
        {
            printf("十六进制串非法（需成对十六进制位）\n");
            return;
        }
        if (r == -2)
        {
            printf("内容超出 %u 字节上限\n", (unsigned)sizeof(buf));
            return;
        }
    }
    else
    {
        n = (CCUINT32)strlen(payload);
        if (n > sizeof(buf)) n = sizeof(buf);
        memcpy(buf, payload, n);
    }

    CCINT64 w = ccfiles_write(st->fs, buf, off, n);
    if (w < 0)
        printf("[失败] 写入: %s (%d)\n", fs_result_name((CCINT32)w), (int)w);
    else
        printf("[完成] 写入 %lld 字节 @ %llu（%s）\n",
            (long long)w, (unsigned long long)off, is_hex ? "hex" : "text");
}

static void cmd_read(DemoState *st, const char **pp)
{
    const char *p = *pp;
    char t1[64], t2[64];
    CCBOOL hexonly = CCFALSE;
    CCUINT64 off = 0;
    CCUINT64 size = 256;
    CCBOOL have = take_tok(&p, t1, sizeof(t1));

    if (have && strcmp(t1, "-x") == 0)
    {
        hexonly = CCTRUE;
        have = take_tok(&p, t1, sizeof(t1));
    }
    if (have && !parse_u64(t1, &off))
    {
        printf("偏移非法: %s\n", t1);
        return;
    }
    if (take_tok(&p, t2, sizeof(t2)) && !parse_u64(t2, &size))
    {
        printf("大小非法: %s\n", t2);
        return;
    }

    if (size > DEMO_IO_MAX) size = DEMO_IO_MAX;
    if (size == 0) size = 1;
    char cur[512];
    if (ccfiles_getcur(st->fs, cur, sizeof(cur)) == CC_FS_ERR_NOENT)
    {
        printf("请先 open 或 create 一个文件\n");
        return;
    }

    static unsigned char buf[DEMO_IO_MAX];
    CCINT64 rd = ccfiles_read(st->fs, buf, off, size);
    if (rd < 0)
    {
        printf("[失败] 读取: %s (%d)\n", fs_result_name((CCINT32)rd), (int)rd);
        return;
    }
    if (rd == 0)
    {
        printf("(EOF)\n");
        return;
    }

    if (!hexonly)
    {
        CCINT64 i;
        printf("文本> ");
        for (i = 0; i < rd; i++)
        {
            unsigned char c = buf[i];
            if (c == '\n') printf("\\n");
            else if (c == '\r') printf("\\r");
            else if (c == '\t') printf("\\t");
            else if (c >= 32 && c < 127) printf("%c", (char)c);
            else printf("\\x%02X", c);
        }
        printf("\n");
    }
    print_hexdump(buf, (CCUINT32)rd, off);
}

static void cmd_stat(DemoState *st, const char **pp)
{
    char path[512];
    if (!take_tok(pp, path, sizeof(path)))
    {
        printf("用法: stat <path>\n");
        return;
    }
    CCFsStat stt;
    memset(&stt, 0, sizeof(stt));
    CCINT32 r = ccfiles_stat(st->fs, path, &stt);
    if (r != CC_FS_OK)
    {
        printf("[失败] stat: %s (%d)\n", fs_result_name(r), (int)r);
        return;
    }
    printf("  type    : %s\n", type_name(stt.type));
    printf("  size    : %llu B\n", (unsigned long long)stt.size);
    printf("  ctime   : %llu\n", (unsigned long long)stt.ctime);
    printf("  mtime   : %llu\n", (unsigned long long)stt.mtime);
    printf("  readonly: %s\n", stt.readonly ? "true" : "false");
    printf("  crc32   : 0x%08X\n", (unsigned)stt.crc32);
}

static void cmd_ls(DemoState *st, const char **pp)
{
    char path[512];
    if (!take_tok(pp, path, sizeof(path))) strcpy(path, ".");
    CCUINT32 cnt = 0;
    CCINT32 r = ccfiles_list(st->fs, path, cb_list, &cnt);
    if (r != CC_FS_OK)
    {
        printf("[失败] ls: %s (%d)\n", fs_result_name(r), (int)r);
        return;
    }
    if (cnt == 0) printf("  (空)\n");
    else printf("  共 %u 项\n", cnt);
}

static void cmd_find(DemoState *st, const char **pp)
{
    const char *p = *pp;
    char path[512], pat[256];
    if (!take_tok(&p, path, sizeof(path)) ||
        !take_tok(&p, pat, sizeof(pat)))
    {
        printf("用法: find <path> <pattern>\n");
        return;
    }
    CCUINT32 cnt = 0;
    CCINT32 r = ccfiles_find(st->fs, path, pat, cb_find, &cnt);
    if (r != CC_FS_OK)
    {
        printf("[失败] find: %s (%d)\n", fs_result_name(r), (int)r);
        return;
    }
    printf("  共命中 %u 项\n", cnt);
}

static CCBOOL parse_mount_type(const char *s, CCUINT32 *out)
{
    if (strcmp(s, "dir") == 0) { *out = CC_FS_MOUNT_DIR; return CCTRUE; }
    if (strcmp(s, "pkg") == 0) { *out = CC_FS_MOUNT_PKG; return CCTRUE; }
    if (strcmp(s, "file") == 0) { *out = CC_FS_MOUNT_FILE; return CCTRUE; }
    return CCFALSE;
}

static void cmd_mount(DemoState *st, const char **pp)
{
    const char *p = *pp;
    char v[512], real[1024], type[16];
    if (!take_tok(&p, v, sizeof(v)) ||
        !take_tok(&p, real, sizeof(real)) ||
        !take_tok(&p, type, sizeof(type)))
    {
        printf("用法: mount <v> <real> <dir|pkg|file>\n");
        return;
    }
    CCUINT32 mt = 0;
    if (!parse_mount_type(type, &mt))
    {
        printf("未知挂载类型: %s\n", type);
        return;
    }
    print_result("挂载", ccfiles_mount(st->fs, v, real, mt));
}

static void cmd_unmount(DemoState *st, const char **pp)
{
    char v[512];
    if (!take_tok(pp, v, sizeof(v)))
    {
        printf("用法: unmount <v>\n");
        return;
    }
    print_result("卸载", ccfiles_unmount(st->fs, v));
}

static void cmd_refresh(DemoState *st, const char **pp)
{
    char v[512];
    if (!take_tok(pp, v, sizeof(v)))
    {
        printf("用法: refresh <v>\n");
        return;
    }
    print_result("刷新", ccfiles_refresh(st->fs, v));
}

/* ============================================================================
 * 命令分发
 * ========================================================================== */

static CCBOOL demo_dispatch(DemoState *st, char *line)
{
    const char *p = line;
    char cmd[32];
    if (!take_tok(&p, cmd, sizeof(cmd))) return CCTRUE;   /* 空行 */

    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0)
    {
        print_help();
        return CCTRUE;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 ||
        strcmp(cmd, "q") == 0)
        return CCFALSE;

    if (strcmp(cmd, "pwd") == 0)
    {
        char cwd[512];
        if (ccfiles_getcwd(st->fs, cwd, sizeof(cwd)) <= 0) strcpy(cwd, "?");
        printf("%s\n", cwd);
    }
    else if (strcmp(cmd, "cd") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: cd <path>\n");
            return CCTRUE;
        }
        print_result("切换目录", ccfiles_chdir(st->fs, path));
    }
    else if (strcmp(cmd, "ls") == 0) cmd_ls(st, &p);
    else if (strcmp(cmd, "find") == 0) cmd_find(st, &p);
    else if (strcmp(cmd, "stat") == 0) cmd_stat(st, &p);
    else if (strcmp(cmd, "mount") == 0) cmd_mount(st, &p);
    else if (strcmp(cmd, "unmount") == 0) cmd_unmount(st, &p);
    else if (strcmp(cmd, "refresh") == 0) cmd_refresh(st, &p);
    else if (strcmp(cmd, "open") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: open <path>\n");
            return CCTRUE;
        }
        CCINT32 r = ccfiles_open(st->fs, path);
        if (r == CC_FS_OK)
        {
            printf("[完成] 打开 %s\n", path);
        }
        else
        {
            printf("[失败] 打开: %s (%d)\n", fs_result_name(r), (int)r);
        }
    }
    else if (strcmp(cmd, "create") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: create <path>\n");
            return CCTRUE;
        }
        CCINT32 r = ccfiles_create_file(st->fs, path);
        if (r == CC_FS_OK)
        {
            printf("[完成] 创建并选中 %s\n", path);
        }
        else
        {
            printf("[失败] 创建: %s (%d)\n", fs_result_name(r), (int)r);
        }
    }
    else if (strcmp(cmd, "read") == 0) cmd_read(st, &p);
    else if (strcmp(cmd, "write") == 0) cmd_write(st, &p);
    else if (strcmp(cmd, "flush") == 0) print_result("刷新写缓冲", ccfiles_flush(st->fs));
    else if (strcmp(cmd, "mkdir") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: mkdir <path>\n");
            return CCTRUE;
        }
        print_result("创建目录", ccfiles_create_dir(st->fs, path));
    }
    else if (strcmp(cmd, "rm") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: rm <path>\n");
            return CCTRUE;
        }
        print_result("删除", ccfiles_remove(st->fs, path));
    }
    else if (strcmp(cmd, "rmr") == 0)
    {
        char path[512];
        if (!take_tok(&p, path, sizeof(path)))
        {
            printf("用法: rmr <path>（rm -rf 递归删除）\n");
            return CCTRUE;
        }
        print_result("递归删除", ccfiles_remove_recursive(st->fs, path));
    }
    else if (strcmp(cmd, "cp") == 0)
    {
        char a[512], b[512];
        if (!take_tok(&p, a, sizeof(a)) || !take_tok(&p, b, sizeof(b)))
        {
            printf("用法: cp <src> <dst>\n");
            return CCTRUE;
        }
        print_result("复制", ccfiles_cp(st->fs, a, b));
    }
    else if (strcmp(cmd, "mv") == 0)
    {
        char a[512], b[512];
        if (!take_tok(&p, a, sizeof(a)) || !take_tok(&p, b, sizeof(b)))
        {
            printf("用法: mv <src> <dst>\n");
            return CCTRUE;
        }
        print_result("移动", ccfiles_mv(st->fs, a, b));
    }
    else if (strcmp(cmd, "pack") == 0)
    {
        char a[512], b[1024];
        if (!take_tok(&p, a, sizeof(a)) || !take_tok(&p, b, sizeof(b)))
        {
            printf("用法: pack <v> <real>\n");
            return CCTRUE;
        }
        print_result("打包", ccfiles_pack(st->fs, a, b));
    }
    else if (strcmp(cmd, "unpack") == 0)
    {
        char a[512], b[1024];
        if (!take_tok(&p, a, sizeof(a)) || !take_tok(&p, b, sizeof(b)))
        {
            printf("用法: unpack <v> <real>\n");
            return CCTRUE;
        }
        print_result("解包", ccfiles_unpack(st->fs, a, b));
    }
    else
    {
        printf("未知命令: %s（输入 -h 查看帮助）\n", cmd);
    }

    return CCTRUE;
}

/* ============================================================================
 * 入口
 * ========================================================================== */

int main(void)
{
    printf("水晶簇标准库 · 虚拟文件系统 demo（输入 -h 查看帮助，quit 退出）\n");

    CCFiles *fs = ccfiles_create();
    if (!fs)
    {
        printf("[错误] ccfiles_create 失败\n");
        return 1;
    }

    DemoState st;
    memset(&st, 0, sizeof(st));
    st.fs = fs;

    char line[DEMO_LINE_MAX];
    for (;;)
    {
        char cwd[512];
        if (ccfiles_getcwd(fs, cwd, sizeof(cwd)) <= 0) strcpy(cwd, "/");
        printf("%s> ", cwd);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            printf("\n");
            break;
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (!demo_dispatch(&st, line)) break;
    }

    ccfiles_destroy(fs);
    return 0;
}
