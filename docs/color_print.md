# color_print

## 1.函数签名

```C
CCINT32 color_print(const char* pattern, ...);
```

参数列表：
- pattern：模板串，控制输出格式。只有模板串中的格式控制符会生效。
- 可变参数：格式控制符对应的输入参数，从前到后顺序排列。

返回值：输出到stdout的可见字符数（含换行符，不含ANSI转义码）。

## 2.格式控制符

## 2.1控制符表

|控制符|参数类型|说明|
|---|---|---|
|`%%`|无|输出 `%`|
|`%s`|`const char *`|字符串，空指针输出 `(null)`|
|`%c`|`char`(`CCINT32`)|ASCII 字符|
|`%d`|`CCINT32`|有符号32位（二进制）整数，输出十进制|
|`%ld`|`CCINT64`|有符号64位（二进制）整数，输出十进制|
|`%u`|`CCUINT32`|无符号32位（二进制）整数，输出十进制|
|`%lu`|`CCUINT64`|无符号64位（二进制）整数，输出十进制|
|`%x`|`CCUINT32`|32位（二进制）整数，输出为8位16进制小写，带 `0x` 前缀|
|`%lx`|`CCUINT64`|64位（二进制）整数，输出为16位16进制小写，带 `0x` 前缀|
|`%f`|`float`|单精度浮点数输出，默认6位小数|
|`%.Nf`|`float`|指定`N`位小数的单精度浮点数输出（N ≤ 9）|
|`%.lf`|`double`|双精度浮点数输出，默认6位小数|
|`%.Nlf`|`double`|指定 `N` 位小数的双精度浮点数输出（N≤15）|
|`%p`|`CCUINTPTR`|指针，位宽自适应，以当前平台位宽为准，输出格式与十六进制输出类似（带0x前缀）|
|`%C_HH`|无|颜色控制，其中 `H` 对应一个十六进制数，前后两个十六进制数分别控制前景色和背景色|

> 浮点数额外支持 NaN、Inf、-Inf 特殊值；数值 >= 2^64 时自动切换科学计数法（e±d）。

### 2.2颜色代码表

|Hex|颜色|Hex|颜色|
|---|---|---|---|
|0|默认|8|亮黑（灰）|
|1|红色|9|亮红|
|2|绿色|A|亮绿|
|3|黄色|B|亮黄|
|4|蓝色|C|亮蓝|
|5|品红|D|亮品红|
|6|青色|E|亮青|
|7|白色|F|亮白|

用法示例：
```C
color_print("%c_20绿色前景，默认背景\n");
```

### 2.3颜色宏速记

定义在 `color_print.h`：

```C
/* 颜色代码宏（字符串字面量，用于拼接） */
#define CC_DEFAULT    "0"
#define CC_RED        "1"
#define CC_GREEN      "2"
#define CC_YELLOW     "3"
#define CC_BLUE       "4"
#define CC_MAGENTA    "5"
#define CC_CYAN       "6"
#define CC_WHITE      "7"
#define CC_GRAY       "8"
#define CC_BRED       "9"
#define CC_BGREEN     "A"
#define CC_BYELLOW    "B"
#define CC_BBLUE      "C"
#define CC_BMAGENTA   "D"
#define CC_BCYAN      "E"
#define CC_BWHITE     "F"

/* 组合宏：CC_TEXT_COLOR(前景, 背景) → "%c_XY"（字符串拼接） */
#define CC_TEXT_COLOR(fg, bg)  "%c_" fg bg
```

用法示例：

```C
color_print(CC_TEXT_COLOR(CC_RED, CC_BLACK) "错误\n");
```

## 3.实现架构

### 3.1流程简述

```
color_print(pattern, ...)
  │
  ├── 获取 va_list
  ├── 初始化内部输出缓冲区（栈上 512 字节）与当前颜色状态 cur_fg/cur_bg
  │
  ├── 遍历 pattern 每个字符:
  │     ├── 普通字符 → 写入缓冲区
  │     ├── 遇到换行且当前有颜色（cur_fg/cur_bg 非 0）→
  │     │     newline_recolor()：flush 缓冲 → 重置颜色 → 输出 '\n' → 重新应用当前颜色
  │     │     （换行符是可见字符计入返回值；ANSI 转义码不计）
  │     ├── 遇到 '%' → try_match_control()（switch 分发）:
  │     │     ├── '%'  → 写 '%' 到缓冲区
  │     │     ├── 'c'  → [2]=='_' ? fmt_color() : fmt_char()
  │     │     │        fmt_color():
  │     │     │          1. hex2int 解析 2 位 hex
  │     │     │          2. 非法 hex → 回退 fmt_char()（消费一个 va_arg）
  │     │     │          3. 合法 → set_color()：先 flush 缓冲，再写 ANSI / Win32 API
  │     │     │          4. 更新 cur_fg/cur_bg、置 color_modified 旗标
  │     │     │          5. 返回步长 5（跳过 "%c_XX"）
  │     │     ├── 's' → fmt_string()（空指针 → "(null)"；实参内含换行且当前有颜色时
  │     │     │         同样复用 newline_recolor()，避免背景色填充行尾 + 正确计数）
  │     │     ├── 'd'/'u'/'x'/'p' → fmt_decimal()/fmt_unsigned_decimal()/fmt_hex()/fmt_pointer()
  │     │     ├── 'f' → fmt_float()（默认 6 位；NaN/Inf；>= 2^64 转科学计数法）
  │     │     ├── '.' → fmt_float_precision()（%.Nf / %.Nlf）
  │     │     ├── 'l' → fmt_long()（%ld/%lu/%lx/%lf）
  │     │     └── 未知 → 回退，'%' 作为普通字符输出
  │     └── 缓冲区满 → fwrite 批量输出 → 清空缓冲区
  │
  ├── 刷新剩余缓冲区 → fwrite 输出
  └── if color_modified: reset_color()
        ├── ANSI 路径：fwrite("\033[0m")
        └── Win32 路径：SetConsoleTextAttribute(hOut, original_attrs)
```

### 3.2工程细节阐述

**缓冲区批量输出：**
color_print使用fwrite写stdout输出字符，为了减少调用开销，内部维护一个缓冲数组，当数组满后批量输出。函数结束后会将剩余缓冲全部输出并清空。
> 注意：颜色切换会触发缓冲区清空，以确保颜色覆盖范围正确。

**错误处理：**
在`%c_HH`控制符中，如果 `NN` 中有任意一个非法 hex 字符（不在0\~9、a\~f、A\~F范围内），则回退为`%c`格式控制符。`%c`之后的`_HH`部分按普通字符输出处理。

**颜色控制后端：**
||ANSI 路径|Win32 Console API 路径|
|---|---|---|
|触发条件|VT 启用成功 / Linux、Android 终端|VT 启用失败（Win7 等）|
|颜色设置|fwrite("\033[XXm")|SetConsoleTextAttribute(hOut, attr)|
|重置|fwrite("\033[0m")|SetConsoleTextAttribute(hOut, original_attrs)|
|缓冲|直接写 stdout|直接操作控制台句柄|