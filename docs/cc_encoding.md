# cc_encoding

UTF-8 与本地编码之间的文本转换。该模块只做编码转换，不判断 stdout 是否重定向，不修改控制台代码页，不写任何输出。

## 1. 函数签名

```c
#include <cc_encoding.h>

typedef enum cc_local_encoding
{
    CC_LOCAL_SYSTEM  = 0,  /* Windows: GetACP();           POSIX: UTF-8 */
    CC_LOCAL_CONSOLE = 1   /* Windows: GetConsoleOutputCP(); POSIX: UTF-8 */
} CCLocalEncoding;

char *cc_utf8_to_local(const char *utf8, CCLocalEncoding target);
char *cc_local_to_utf8(const char *local, CCLocalEncoding source);
```

返回值由 `ccalloc` 分配，调用方用 `ccalloc(p, 0, NULL)` 释放。失败返回 `NULL`。

## 2. 编码目标

| 目标 | Windows | POSIX |
|---|---|---|
| `CC_LOCAL_SYSTEM` | 系统 ANSI 代码页 `GetACP()` | UTF-8 |
| `CC_LOCAL_CONSOLE` | 控制台输出代码页 `GetConsoleOutputCP()`；取值为 0 时回退 `GetACP()` | UTF-8 |

`CC_LOCAL_SYSTEM` 用于 `argv`、`env`、文件名、`fopen` / `stat` 等 CRT 接口。
`CC_LOCAL_CONSOLE` 用于写入控制台的文本，例如 `color_print` 的输出。

## 3. 语义

- 输入为 NUL 结尾字符串；空串返回一个分配的空串。
- 输出为 NUL 结尾字符串，长度只受内存限制。
- 非法 UTF-8：`cc_utf8_to_local` 返回 `NULL`，不静默丢弃字符。
- 目标代码页无法表示的字符：替换为 `?`，转换继续。该行为有损，调用方需要无损时不得依赖此接口。
- `cc_local_to_utf8` 对本地编码中的非法字节按平台转换规则替换，不返回半成品。
- 转换不修改输入字符串，不保留内部指针。
- 模块不设置全局状态；两个函数可在多线程中并发调用。

## 4. 使用边界

- 只用于文本，不用于二进制数据。
- 模块不判断 stdout 是否为控制台。重定向场景若需要保留 UTF-8 字节，调用方不调用转换即可。
- 模块不调用 `SetConsoleOutputCP` / `SetConsoleCP`，不要求调用方执行 `chcp`。
- Windows 上把来自 UTF-8 配置文件的路径交给宿主文件接口前，使用 `CC_LOCAL_SYSTEM` 转换；把宿主路径输出到控制台前，使用 `CC_LOCAL_CONSOLE` 转换。

## 5. 示例

```c
#include <ccalloc.h>
#include <cc_encoding.h>
#include <color_print.h>

static void print_utf8(const char *text, CCBOOL console)
{
    if (!text)
        return;

    if (console)
    {
        char *local = cc_utf8_to_local(text, CC_LOCAL_CONSOLE);
        if (local)
        {
            color_print("%s", local);
            ccalloc(local, 0, NULL);
        }
        return;
    }

    color_print("%s", text);
}
```

反向转换：

```c
static char *path_from_config(const char *utf8_path)
{
    return cc_utf8_to_local(utf8_path, CC_LOCAL_SYSTEM);
}
```

## 6. 测试要点

- 内存模式：中文、空串、长字符串、非法 UTF-8。
- Windows：`CC_LOCAL_SYSTEM` 与 `CC_LOCAL_CONSOLE` 在 936 / 65001 下的结果。
- 目标代码页无法表示的字符替换为 `?`。
- POSIX：两个方向均返回内容相同的新分配字符串。
- 返回值可由 `ccalloc(p, 0, NULL)` 释放。
