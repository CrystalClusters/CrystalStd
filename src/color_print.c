/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-08
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-09
 * @Description: 开发文档参考项目根目录起：docs\dev\彩色输出.md
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */
#include "crystal_std_inner_header.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#ifdef CC_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_OUT_BUFFER 512

// 颜色查找表
// 索引 0（CC_DEFAULT）表示“复位该项为终端默认色”：前景 39 / 背景 49（标准 SGR）
static const char *fg_ansi[16] = {
    "39", "31", "32", "33", "34", "35", "36", "37",
    "90", "91", "92", "93", "94", "95", "96", "97"
};
static const char *bg_ansi[16] = {
    "49", "41", "42", "43", "44", "45", "46", "47",
    "100", "101", "102", "103", "104", "105", "106", "107"
};

#ifdef CC_WINDOWS
static const WORD fg_win32[16] = {
    0,
    FOREGROUND_RED,
    FOREGROUND_GREEN,
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_BLUE,
    FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_INTENSITY,
    FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
};
static const WORD bg_win32[16] = {
    0,
    BACKGROUND_RED,
    BACKGROUND_GREEN,
    BACKGROUND_RED | BACKGROUND_GREEN,
    BACKGROUND_BLUE,
    BACKGROUND_RED | BACKGROUND_BLUE,
    BACKGROUND_GREEN | BACKGROUND_BLUE,
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE,
    BACKGROUND_INTENSITY,
    BACKGROUND_RED | BACKGROUND_INTENSITY,
    BACKGROUND_GREEN | BACKGROUND_INTENSITY,
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_INTENSITY,
    BACKGROUND_BLUE | BACKGROUND_INTENSITY,
    BACKGROUND_RED | BACKGROUND_BLUE | BACKGROUND_INTENSITY,
    BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY,
    BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY
};
static WORD   original_attrs = 0;
static CCBOOL attrs_saved    = CCFALSE;
static HANDLE hConsole       = NULL;
static DWORD  original_mode  = 0;
static CCBOOL mode_saved     = CCFALSE;
#endif

static CCBOOL ansi_enabled   = CCFALSE;
static CCBOOL console_inited = CCFALSE;

void init_console(void)
{
    if (console_inited)
        return;
    console_inited = CCTRUE;
#ifdef CC_WINDOWS
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == NULL || hConsole == INVALID_HANDLE_VALUE)
        return;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi))
    {
        original_attrs = csbi.wAttributes;
        attrs_saved = CCTRUE;
    }
    DWORD mode;
    if (GetConsoleMode(hConsole, &mode))
    {
        original_mode = mode;
        mode_saved = CCTRUE;
        if (SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            ansi_enabled = CCTRUE;
    }
#else
    if (isatty(STDOUT_FILENO))
    {
        const char *term = getenv("TERM");
        if(term && strcmp(term,"dumb")!=0)
            ansi_enabled = CCTRUE;
    }
#endif
}

/**
 * 逆初始化终端控制台，恢复 init_console() 修改的一切系统状态。
 * 可安全重复调用（幂等）。
 */
void deinit_console(void)
{
    if (!console_inited)
        return;
#ifdef CC_WINDOWS
    // 恢复控制台文本属性到初始状态
    if (hConsole != NULL && attrs_saved)
        SetConsoleTextAttribute(hConsole, original_attrs);
    // 恢复控制台模式到初始状态（撤销 ENABLE_VIRTUAL_TERMINAL_PROCESSING）
    if (hConsole != NULL && mode_saved)
        SetConsoleMode(hConsole, original_mode);
#endif
    // 重置内部状态
    ansi_enabled   = CCFALSE;
    console_inited = CCFALSE;
}

static inline void buffer_append_chars(
    const char *src,
    CCUINT32 len,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT32 i = 0;
    while (i < len)
    {
        // 缓冲写满检测
        if (*p_buffer_counter >= MAX_OUT_BUFFER)
        {
            fwrite(out_buffer, *p_buffer_counter, 1, stdout);
            *p_counter += *p_buffer_counter;
            *p_buffer_counter = 0;
        }
        out_buffer[(*p_buffer_counter)++] = src[i++];
    }
}

/* flush 输出缓冲中尚未写入的字符 */
static inline void flush_buffer(
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    if (*p_buffer_counter > 0)
    {
        fwrite(out_buffer, *p_buffer_counter, 1, stdout);
        *p_counter += *p_buffer_counter;
        *p_buffer_counter = 0;
    }
}

/**
 * 单个 hex 字符 → 0~15 索引；非法字符返回 16
*/
static inline CCUINT32 hex2int(char c)
{
    if (c >= '0' && c <= '9')
        return (CCUINT32)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (CCUINT32)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (CCUINT32)(c - 'A' + 10);
    return 16;
}

/**
 * 输出 ANSI 颜色序列到 stdout（vt_enabled 为真时调用）。
 * fg/bg 为 0（CC_DEFAULT）时对应输出 39/49，即复位该项为默认色。
 */
static void write_ansi_color(CCUINT32 fg, CCUINT32 bg)
{
    if (fg>=16 || bg>=16)
        return;

    fwrite("\033[", 2, 1, stdout);
    fwrite(fg_ansi[fg], 2, 1, stdout); /* FG 始终 2 字符，0→"39" */
    fwrite(";", 1, 1, stdout);
    {
        CCUINT32 bg_len = (bg >= 8) ? 3 : 2; /* "49"/"41"=2 / "100"=3 */
        fwrite(bg_ansi[bg], bg_len, 1, stdout);
    }
    fwrite("m", 1, 1, stdout);
}

#ifdef CC_WINDOWS
#define CC_FG_MASK (WORD)(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CC_BG_MASK (WORD)(BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY)

static void write_win32_color(CCUINT32 fg, CCUINT32 bg)
{
    if (hConsole == NULL)
        return;

    WORD attr = attrs_saved ? original_attrs : 0;

    // 前景：0=默认→恢复 original_attrs 的前景位；否则置为指定色
    attr &= ~CC_FG_MASK;
    attr |= (fg == 0) ? (WORD)(original_attrs & CC_FG_MASK) : fg_win32[fg];

    // 背景：0=默认→恢复 original_attrs 的背景位；否则置为指定色
    attr &= ~CC_BG_MASK;
    attr |= (bg == 0) ? (WORD)(original_attrs & CC_BG_MASK) : bg_win32[bg];

    SetConsoleTextAttribute(hConsole, attr);
}

static void write_win32_reset(void)
{
    if (hConsole != NULL && attrs_saved)
        SetConsoleTextAttribute(hConsole, original_attrs);
}
#endif

/**
 * 设置颜色，注意 flush 缓冲确保颜色覆盖范围正确，
 */
static void set_color(
    CCUINT32 fg,
    CCUINT32 bg,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    flush_buffer(out_buffer, p_buffer_counter, p_counter);
    if (ansi_enabled)
        write_ansi_color(fg, bg);
#ifdef CC_WINDOWS
    else
        write_win32_color(fg, bg);
#else
    /* 非 Windows 且 VT 不可用（理论上不会发生）：静默跳过 */
    (void)fg;
    (void)bg;
#endif
}

/**
 * 重置颜色到终端默认值
 */
static void reset_color(char *out_buffer, CCUINT32 *p_buffer_counter, CCUINT32 *p_counter)
{
    flush_buffer(out_buffer, p_buffer_counter, p_counter);
    if (ansi_enabled)
        fwrite("\033[0m", 4, 1, stdout);
#ifdef CC_WINDOWS
    else
        write_win32_reset();
#endif
}

/**
 * 带颜色的换行：flush 缓冲 → 重置颜色 → 输出'\n' → 重新应用当前颜色。
 * 换行符是可见字符，计入返回值计数器；ANSI 转义码不计入。
 * 供 color_print 主循环与 fmt_string 复用，防止背景色填充行尾空白。
 */
static inline void newline_recolor(
    CCUINT32 cur_fg,
    CCUINT32 cur_bg,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    flush_buffer(out_buffer, p_buffer_counter, p_counter);
    if (ansi_enabled)
    {
        fwrite("\033[0m\n", 5, 1, stdout);
        write_ansi_color(cur_fg, cur_bg);
    }
#ifdef CC_WINDOWS
    else
    {
        write_win32_reset();
        fwrite("\n", 1, 1, stdout);
        write_win32_color(cur_fg, cur_bg);
    }
#endif
    // 换行符是可见字符，计入返回值（不含 ANSI 转义码）
    (*p_counter)++;
}

/**
 * 返回值为模板字符串需要跳过的字节数
 */
static inline CCUINT32 fmt_string(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter,
    CCUINT32 cur_fg,
    CCUINT32 cur_bg
)
{
    char *s = (char*)va_arg(*p_args, char*);
    if (!s) // 空指针检测
        buffer_append_chars("(null)", 6, out_buffer, p_buffer_counter, p_counter);
    else
    {
        while (*s != '\0')
        {
            // 有颜色状态下的换行：复用换行去色逻辑（防背景色填充行尾）+ 计数
            if (*s == '\n' && (cur_fg != 0 || cur_bg != 0))
            {
                newline_recolor(cur_fg, cur_bg, out_buffer, p_buffer_counter, p_counter);
            }
            else
            {
                // 缓冲写满检测
                if (*p_buffer_counter >= MAX_OUT_BUFFER)
                {
                    fwrite(out_buffer, *p_buffer_counter, 1, stdout);
                    *p_counter += *p_buffer_counter;
                    *p_buffer_counter = 0;
                }
                out_buffer[(*p_buffer_counter)++] = *s;
            }
            s++;
        }
    }
    return 2;
}

/**
 * 返回值为模板字符串需要跳过的字节数
 */
static inline CCUINT32 fmt_char(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    char c = (char)va_arg(*p_args, CCINT32);
    // 缓冲写满检测
    if (*p_buffer_counter >= MAX_OUT_BUFFER)
    {
        fwrite(out_buffer, *p_buffer_counter, 1, stdout);
        *p_counter += *p_buffer_counter;
        *p_buffer_counter = 0;
    }
    out_buffer[(*p_buffer_counter)++] = c;
    return 2;
}

/**
 * %c_<FG><BG> → ANSI SGR 序列
 * ctl_body 指向 '_' 之后第一个字符（即 FG hex 字符）
 * 返回值为模板字符串需要跳过的字节数
 *
 * 若 hex 字符非法，回退为普通 %c 格式符处理。
 */
static inline CCUINT32 fmt_color(
    const char *ctl_body,
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter,
    CCBOOL *p_color_modified,
    CCUINT32 *p_cur_fg,
    CCUINT32 *p_cur_bg
)
{
    CCUINT32 fg = hex2int(ctl_body[0]);
    CCUINT32 bg = hex2int(ctl_body[1]);

    if (fg >= 16 || bg >= 16)
    {
        // 非法 hex：当作普通 %c 格式符处理，_<XX> 由外层作为普通字符输出
        return fmt_char(p_args, out_buffer, p_buffer_counter, p_counter);
    }

    // 0（CC_DEFAULT）表示复位该项为默认色，因此任何组合都应执行一次 set_color
    set_color(fg, bg, out_buffer, p_buffer_counter, p_counter);
    // 仅在出现非默认色时置位，供调用结束后一次性复位终端（\033[0m）
    if (fg != 0 || bg != 0)
        *p_color_modified = CCTRUE;
    // 更新当前颜色状态，供跨行重新应用
    *p_cur_fg = fg;
    *p_cur_bg = bg;
    return 5; //跳过 %c_ + 2位hex
}

/*
 * 将无符号十进制整数逆序生成后正序写入输出缓冲（支持至 CCUINT64 全宽）。
 */
static inline void append_decimal(
    CCUINT64 value,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // CCUINT64 最多 20 位十进制 + 安全余量
    char tmp[21];
    CCUINT32 len = 0;
    if (value == 0)
        tmp[len++] = '0';
    else
    {
        while (value > 0)
        {
            tmp[len++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }
    // 反转写入输出缓冲
    while (len > 0)
    {
        len--;
        if (*p_buffer_counter >= MAX_OUT_BUFFER)
        {
            fwrite(out_buffer, *p_buffer_counter, 1, stdout);
            *p_counter += *p_buffer_counter;
            *p_buffer_counter = 0;
        }
        out_buffer[(*p_buffer_counter)++] = tmp[len];
    }
}

/*
 * 将无符号整数的十六进制数字逆序生成后正序写入输出缓冲（支持至 CCUINT64 全宽）。
 * digits 指定输出位数（32 位=8，64 位=16），高位以 '0' 补齐。不含 "0x" 前缀。
 */
static inline void append_hex(
    CCUINT64 value,
    CCUINT32 digits,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // CCUINT64 全宽最多 16 位十六进制
    char hex[16];
    for (CCUINT32 i = 0; i < digits; i++)
    {
        CCUINT8 tmp = (CCUINT8)(value & 0xf);
        value >>= 4;
        hex[i] = (char)((tmp < 10) ? ('0' + tmp) : ('a' + tmp - 10));
    }
    // 逆序写入到输出缓冲
    for (CCINT32 j = (CCINT32)digits - 1; j >= 0; j--)
    {
        if (*p_buffer_counter >= MAX_OUT_BUFFER)
        {
            fwrite(out_buffer, *p_buffer_counter, 1, stdout);
            *p_counter += *p_buffer_counter;
            *p_buffer_counter = 0;
        }
        out_buffer[(*p_buffer_counter)++] = hex[j];
    }
}

static inline CCUINT32 fmt_decimal(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCINT32 d = va_arg(*p_args, CCINT32);
    if (d < 0)
    {
        buffer_append_chars("-", 1, out_buffer, p_buffer_counter, p_counter);
        append_decimal((CCUINT64)(-(CCUINT32)d), out_buffer, p_buffer_counter, p_counter);
    }
    else
        append_decimal((CCUINT64)d, out_buffer, p_buffer_counter, p_counter);
    return 2;
}

/*
 * %ld：CCINT64 有符号十进制。
 * 复用 append_decimal 输出绝对值，避免 INT64_MIN 溢出。
 */
static inline void fmt_long_decimal(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCINT64 ld = va_arg(*p_args, CCINT64);
    if (ld < 0)
    {
        buffer_append_chars("-", 1, out_buffer, p_buffer_counter, p_counter);
        append_decimal((CCUINT64)(-(CCUINT64)ld), out_buffer, p_buffer_counter, p_counter);
    }
    else
        append_decimal((CCUINT64)ld, out_buffer, p_buffer_counter, p_counter);
}

static inline CCUINT32 fmt_unsigned_decimal(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT32 d = va_arg(*p_args, CCUINT32);
    append_decimal((CCUINT64)d, out_buffer, p_buffer_counter, p_counter);
    return 2;
}

/*
 * %lu：CCINT64 无符号十进制。
 * 复用 append_decimal。
 */
static inline void fmt_long_unsigned_decimal(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT64 lu = va_arg(*p_args, CCUINT64);
    append_decimal((CCUINT64)lu, out_buffer, p_buffer_counter, p_counter);
}

static inline CCUINT32 fmt_hex(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT32 x = va_arg(*p_args, CCUINT32);
    // 0x 前缀
    buffer_append_chars("0x", 2, out_buffer, p_buffer_counter, p_counter);
    // 32 位变量输出固定长度的 8 位十六进制字符串（小写）
    append_hex((CCUINT64)x, 8, out_buffer, p_buffer_counter, p_counter);
    return 2;
}

static inline CCUINT32 fmt_long_hex(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT64 x = va_arg(*p_args, CCUINT64);
    // 0x 前缀
    buffer_append_chars("0x", 2, out_buffer, p_buffer_counter, p_counter);
    // 64 位变量输出固定长度的 16 位十六进制字符串（小写）
    append_hex(x, 16, out_buffer, p_buffer_counter, p_counter);
    return 2;
}

// 编译期确定指针位宽对应的十六进制位数：32位=8，64位=16，以此类推
#define PTR_HEX_DIGITS ((CCINT32)(sizeof(CCUINTPTR) * 2))
static inline CCUINT32 fmt_pointer(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINTPTR p = va_arg(*p_args, CCUINTPTR);
    // 0x 前缀
    buffer_append_chars("0x", 2, out_buffer, p_buffer_counter, p_counter);
    // 位宽自适应：32 位=8，64 位=16，以此类推
    append_hex((CCUINT64)p, (CCUINT32)PTR_HEX_DIGITS, out_buffer, p_buffer_counter, p_counter);
    #undef PTR_HEX_DIGITS
    return 2;
}

/*
 * 核心：将已去除符号、非 NaN/Inf 的非负 double 按精度格式化为十进制小数。
 * 供 fmt_float_core / fmt_double_core 复用（%f / %lf / %.Nf 共用）。
 */
static inline void append_double_fraction(
    double value,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // 精度上限：double 有效数字约 15 位，且防止 CCUINT64 的 10^p 溢出
    if (precision > 15)
        precision = 15;

    // 计算 10^precision
    CCUINT64 pow10 = 1;
    for (CCUINT32 i = 0; i < precision; i++)
        pow10 *= 10;

    // 分离整数与小数部分
    CCUINT64 int_part = (CCUINT64)value;
    double frac = value - (double)int_part;

    // 小数部分四舍五入到 precision 位（缩放与舍入在 double 中进行，减少精度丢失）
    CCUINT64 frac_part = (CCUINT64)(frac * (double)pow10 + 0.5);
    // 舍入进位（如 0.9996 → 1.000）
    if (frac_part >= pow10)
    {
        int_part += 1;
        frac_part = 0;
    }

    // 整数部分
    append_decimal(int_part, out_buffer, p_buffer_counter, p_counter);
    // 精度为 0 时省略小数点（与 printf %.0f 行为一致）
    if (precision > 0)
    {
        // 小数点
        buffer_append_chars(".", 1, out_buffer, p_buffer_counter, p_counter);
        // 小数部分逆序生成后正序写入（自动补零到 precision 位）
        char frac_str[15];
        for (CCUINT32 i = 0; i < precision; i++)
        {
            frac_str[precision - 1 - i] = (char)('0' + (frac_part % 10));
            frac_part /= 10;
        }
        buffer_append_chars(frac_str, precision, out_buffer, p_buffer_counter, p_counter);
    }
}

/*
 * 10^d 紧凑查表（d ∈ [0,308]），分解为个位段与十位段：10^d = tens[d/10] * ones[d%10]。
 * 每项单独正确舍入，避免运行时连乘累积误差；供两个科学计数法函数共享（类型无关数据）。
 */
static const double cc_pow10_ones[10] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9
};
static const double cc_pow10_tens[31] = {
    1e0, 1e10, 1e20, 1e30, 1e40, 1e50, 1e60, 1e70, 1e80, 1e90, 1e100,
    1e110, 1e120, 1e130, 1e140, 1e150, 1e160, 1e170, 1e180, 1e190, 1e200,
    1e210, 1e220, 1e230, 1e240, 1e250, 1e260, 1e270, 1e280, 1e290, 1e300
};

static inline double cc_pow10_lookup(CCINT32 d)
{
    return cc_pow10_tens[d / 10] * cc_pow10_ones[d % 10];
}

/*
 * 科学计数法尾段（类型无关）：输出已归一化到 [1,10) 的尾数与 "e±d"。
 * 供 append_scientific_double / append_scientific_float 复用。
 */
static inline void append_scientific_tail(
    double scaled,
    CCINT32 d,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    append_double_fraction(scaled, precision, out_buffer, p_buffer_counter, p_counter);
    buffer_append_chars("e", 1, out_buffer, p_buffer_counter, p_counter);
    if (d < 0)
    {
        buffer_append_chars("-", 1, out_buffer, p_buffer_counter, p_counter);
        d = -d;
    }
    else
        buffer_append_chars("+", 1, out_buffer, p_buffer_counter, p_counter);
    append_decimal((CCUINT64)d, out_buffer, p_buffer_counter, p_counter);
}

/*
 * %lf 大数科学计数法（解析 binary64：11 位阶码偏置 1023、52 位尾数）。
 * 仅处理非负、有限、且 >= 2^64 的值（符号/Inf/NaN 已在外层处理）。
 */
static void append_scientific_double(
    double value,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT64 bits;
    memcpy(&bits, &value, sizeof(bits));
    CCINT32 bin_exp = (CCINT32)((bits >> 52) & 0x7FF) - 1023;

    // 十进制指数估计：d = floor(bin_exp * log10(2))
    CCINT32 d = (CCINT32)((double)bin_exp * 0.30102999566398119521);
    // 归一化到 [1,10)（查表 10^d，校正 ±1）
    double scaled = value / cc_pow10_lookup(d);
    if (scaled >= 10.0) { scaled *= 0.1; d++; }
    else if (scaled < 1.0) { scaled *= 10.0; d--; }

    append_scientific_tail(scaled, d, precision, out_buffer, p_buffer_counter, p_counter);
}

/*
 * %f 大数科学计数法（解析 binary32：8 位阶码偏置 127、23 位尾数）。
 * 仅处理非负、有限、且 >= 2^64 的值（符号/Inf/NaN 已在外层处理）。
 */
static void append_scientific_float(
    float value,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT32 bits;
    memcpy(&bits, &value, sizeof(bits));
    CCINT32 bin_exp = (CCINT32)((bits >> 23) & 0xFF) - 127;

    // 十进制指数估计：d = floor(bin_exp * log10(2))
    CCINT32 d = (CCINT32)((double)bin_exp * 0.30102999566398119521);
    // 归一化到 [1,10)（查表 10^d，校正 ±1）
    double scaled = (double)value / cc_pow10_lookup(d);
    if (scaled >= 10.0) { scaled *= 0.1; d++; }
    else if (scaled < 1.0) { scaled *= 10.0; d--; }

    append_scientific_tail(scaled, d, precision, out_buffer, p_buffer_counter, p_counter);
}

/* %lf 内核：double 全精度路径 */
static inline void fmt_double_core(
    double value,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // 特殊值：NaN
    if (value != value)
    {
        buffer_append_chars("NaN", 3, out_buffer, p_buffer_counter, p_counter);
        return;
    }
    // 特殊值：±Inf（double 超出 DBL_MAX 即为无穷）
    if (value > DBL_MAX)
    {
        buffer_append_chars("Inf", 3, out_buffer, p_buffer_counter, p_counter);
        return;
    }
    if (value < -DBL_MAX)
    {
        buffer_append_chars("-Inf", 4, out_buffer, p_buffer_counter, p_counter);
        return;
    }

    // 符号
    if (value < 0.0)
    {
        buffer_append_chars("-", 1, out_buffer, p_buffer_counter, p_counter);
        value = -value;
    }

    // 大指数：超出定点（CCUINT64）可表示范围，改走科学计数法（解析 binary64）
    if (value >= 0x1p64)
    {
        append_scientific_double(value, precision, out_buffer, p_buffer_counter, p_counter);
        return;
    }

    append_double_fraction(value, precision, out_buffer, p_buffer_counter, p_counter);
}

/* %f 内核：float 路径（float 尾数 24 位，有效数字约 7 位，精度上限 9） */
static inline void fmt_float_core(
    float value,
    CCUINT32 precision,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // 特殊值：NaN
    if (value != value)
    {
        buffer_append_chars("NaN", 3, out_buffer, p_buffer_counter, p_counter);
        return;
    }
    // 特殊值：±Inf（float 最大值约 3.4e38，超出即视为无穷）
    if (value > 3.5e38)
    {
        buffer_append_chars("Inf", 3, out_buffer, p_buffer_counter, p_counter);
        return;
    }
    if (value < -3.5e38)
    {
        buffer_append_chars("-Inf", 4, out_buffer, p_buffer_counter, p_counter);
        return;
    }

    // 符号
    if (value < 0.0f)
    {
        buffer_append_chars("-", 1, out_buffer, p_buffer_counter, p_counter);
        value = -value;
    }

    // 精度上限：float 有效数字约 7 位，超过 9 位无意义
    if (precision > 9)
        precision = 9;

    // 大指数：超出定点（CCUINT64）可表示范围，改走科学计数法（解析 binary32）
    if (value >= 0x1p64)
    {
        append_scientific_float(value, precision, out_buffer, p_buffer_counter, p_counter);
        return;
    }

    append_double_fraction((double)value, precision, out_buffer, p_buffer_counter, p_counter);
}

/* %f：默认 6 位小数 */
static inline CCUINT32 fmt_float(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    // float 实参经变参默认提升为 double，故以 double 取出后截断回 float（32 位）
    float value = (float)va_arg(*p_args, double);
    fmt_float_core(value, 6, out_buffer, p_buffer_counter, p_counter);
    return 2;
}

/*
 * %lf：double 浮点数（默认 6 位小数）。
 * 复用 fmt_double_core 全精度 double 路径，不做 float 截断。
 */
static inline void fmt_long_float(
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    double value = va_arg(*p_args, double);
    fmt_double_core(value, 6, out_buffer, p_buffer_counter, p_counter);
}

/*
 * %.Nf / %.Nlf：解析精度。digits 指向 "." 之后第一个字符。
 * 返回模板需跳过的字节数；格式非法时返回 0，由调用方回退为普通字符。
 */
static inline CCUINT32 fmt_float_precision(
    const char *digits,
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    CCUINT32 precision = 0;
    CCUINT32 n = 0;
    // 解析十进制精度数字
    while (digits[n] >= '0' && digits[n] <= '9')
    {
        precision = precision * 10 + (CCUINT32)(digits[n] - '0');
        n++;
        if (precision > 15)
            precision = 15; // 防溢出（double 有效数字约 15 位）
    }
    // 结尾须为 'f'（或 'l'+'f'）才视为合法的 %.Nf / %.Nlf
    CCBOOL is_long = CCFALSE;
    if (digits[n] == 'l')
    {
        is_long = CCTRUE;
        n++;
    }
    if (digits[n] != 'f')
        return 0;

    if (is_long)
    {
        // %.Nlf：double 全精度路径（不截断为 float）
        double value = va_arg(*p_args, double);
        fmt_double_core(value, precision, out_buffer, p_buffer_counter, p_counter);
        // 跳过 '%' + '.' + n 位数字(含 'l') + 'f'
        return n + 3;
    }
    else
    {
        // %.Nf：float 实参经变参默认提升为 double，以 double 取出后截断回 float（32 位）
        float value = (float)va_arg(*p_args, double);
        fmt_float_core(value, precision, out_buffer, p_buffer_counter, p_counter);
        // 跳过 '%' + '.' + n 位数字 + 'f'
        return n + 3;
    }
}

/**
 * 长变量，一般用于指定64位变量
 * fmt 指向 %l<此处>，"%l" 之后的第一个字符
 */
static inline CCUINT32 fmt_long(
    const char *fmt,
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter
)
{
    switch (fmt[0])
    {
    case 'd':  // CCINT64
    {
        fmt_long_decimal(p_args, out_buffer, p_buffer_counter, p_counter);
        return 3;
    }
    case 'u':  // CCUINT64
    {
        fmt_long_unsigned_decimal(p_args, out_buffer, p_buffer_counter,p_counter);
        return 3;
    }
    case 'x':  // CCUINT64 - hex
    {
        fmt_long_hex(p_args, out_buffer, p_buffer_counter, p_counter);
        return 3;
    }
    case 'f':  // double
    {
        fmt_long_float(p_args, out_buffer, p_buffer_counter, p_counter);
        return 3;
    }
    default:
    {
        return 0;
    }
    }
}

static inline CCBOOL try_match_control(
    const char **current,
    va_list *p_args,
    char *out_buffer,
    CCUINT32 *p_buffer_counter,
    CCUINT32 *p_counter,
    CCBOOL *p_color_modified,
    CCUINT32 *p_cur_fg,
    CCUINT32 *p_cur_bg
)
{
    // 在控制符匹配成功的情况下，步长至少为2
    CCINTPTR step = 2;
    switch ((*current)[1])
    {
    case '%':
    {
        buffer_append_chars("%", 1, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case 's':
    {
        step = fmt_string(p_args, out_buffer, p_buffer_counter, p_counter, *p_cur_fg, *p_cur_bg);
        break;
    }
    case 'c':
    {
        if ((*current)[2] != '_')
            step = fmt_char(p_args, out_buffer, p_buffer_counter, p_counter);
        else
            step = fmt_color((*current) + 3, p_args, out_buffer, p_buffer_counter, p_counter, p_color_modified, p_cur_fg, p_cur_bg);
        break;
    }
    case 'd':
    {
        step = fmt_decimal(p_args, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case 'u':
    {
        step = fmt_unsigned_decimal(p_args, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case 'x':
    {
        step = fmt_hex(p_args, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case 'p':
    {
        step = fmt_pointer(p_args, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case 'f':
    {
        step = fmt_float(p_args, out_buffer, p_buffer_counter, p_counter);
        break;
    }
    case '.':
    {
        // %.Nf：解析精度控制小数位数
        step = fmt_float_precision((*current) + 2, p_args, out_buffer, p_buffer_counter, p_counter);
        if (step == 0)
            return CCFALSE; // 非法格式：回退为普通字符输出
        break;
    }
    case 'l':
    {
        step = fmt_long((*current) + 2, p_args, out_buffer, p_buffer_counter, p_counter);
        if (step == 0)
            return CCFALSE;
        break;
    }
    default:
    {
        return CCFALSE;
    }
    }
    *current += step;
    return CCTRUE;
}

CCINT32 color_print(const char *pattern, ...)
{
    va_list args;
    va_start(args, pattern);
    // 输出缓冲
    CCUINT32 buffer_counter = 0, counter = 0;
    char out_buffer[MAX_OUT_BUFFER];
    // 颜色修改追踪旗标
    CCBOOL color_modified = CCFALSE;
    // 当前生效的颜色（0=默认），跨行时在 \n 后重新应用，防止背景色填充行尾空白
    CCUINT32 cur_fg = 0, cur_bg = 0;
    // 滑动指针头
    const char *current = pattern;
    // 逐字符解析
    while (*current != '\0')
    {
        // 控制符匹配
        if (*current == '%' && try_match_control(&current, &args, out_buffer, &buffer_counter, &counter, &color_modified, &cur_fg, &cur_bg))
            continue;
        // 遇到换行：flush + 重置背景色（防行尾填充）→ 输出 \n → 重新应用当前颜色（内联函数内含计数）
        if (*current == '\n' && (cur_fg != 0 || cur_bg != 0))
        {
            newline_recolor(cur_fg, cur_bg, out_buffer, &buffer_counter, &counter);
            current++;
            continue;
        }
        // 普通的输出环节
        if (buffer_counter >= MAX_OUT_BUFFER)
        {
            fwrite(out_buffer, buffer_counter, 1, stdout);
            counter += buffer_counter;
            buffer_counter = 0;
        }
        out_buffer[buffer_counter++] = *current;
        // 指针头递增
        current++;
    }
    if (buffer_counter > 0)
    {
        fwrite(out_buffer, buffer_counter, 1, stdout);
        counter += buffer_counter;
        buffer_counter = 0; // 清零，避免 reset_color 内部的 flush_buffer 重复写出尾部
    }
    // 若颜色曾被修改，恢复终端默认颜色
    if (color_modified)
        reset_color(out_buffer, &buffer_counter, &counter);
    //
    va_end(args);
    return counter;
}
