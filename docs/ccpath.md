# ccpath

宿主路径字符串工具。该模块只处理路径文本：不访问文件系统、不解析符号链接、不感知 VFS 挂载，也不依赖 `ccfiles`。

## 1. 函数签名

```c
#include <ccpath.h>

CCBOOL ccpath_is_absolute(const char *path);

char *ccpath_resolve(const char *path, const char *base);
char *ccpath_parent(const char *path);
char *ccpath_basename(const char *path);
char *ccpath_join(const char *a, const char *b);

CCBOOL ccpath_equal(const char *a, const char *b);
```

所有返回 `char *` 的内存由 `ccalloc` 分配，调用方用 `ccalloc(p, 0, NULL)` 释放。失败返回 `NULL`。

## 2. 路径模型

- `/` 是规范分隔符，所有输出使用 `/`。
- Windows 下 `\` 与 `/` 等价；POSIX 下只有 `/` 是分隔符，`\` 是普通字节。
- Windows 绝对路径：
  - 根相对：以 `/` 或 `\` 开头；
  - 盘符绝对：`X:/...` 或 `X:\...`；
  - UNC：`//server/share/...`。
- POSIX 绝对路径：以 `/` 开头。
- 盘符相对路径 `C:foo`、单独的 `C:` 语义含糊，`ccpath_resolve` 返回 `NULL`。
- 扩展长度前缀 `\\?\`、设备前缀 `\\.\` 不属于本模块支持的路径形式，`ccpath_resolve` 返回 `NULL`。

## 3. 规范化

`ccpath_resolve` 是唯一的规范化入口：

1. 绝对路径直接规范化；
2. 相对路径拼接到 `base` 后规范化；
3. `base` 为 `NULL` 或空串时使用当前工作目录；
4. `base` 为相对路径时，先按当前工作目录解析 `base`；
5. 返回规范绝对路径。

规范化规则：

- 丢弃空组件与 `.`；
- `..` 回退上一级；绝对路径在根处停止回退；
- 去掉尾部分隔符，根路径保留一个分隔符；
- Windows 盘符字母统一为大写；
- 不解析符号链接，不检查路径是否存在；
- 不修改调用方传入的字符串。

示例：

| 输入 | `ccpath_resolve(input, base)` |
|---|---|
| `/a/./b/../c` | `/a/c` |
| `a/../b`，`base = /work` | `/work/b` |
| `/../..` | `/` |
| `C:\a\b\..\c`（Windows） | `C:/a/c` |
| `//server/share/a/../b`（Windows） | `//server/share/b` |
| `C:foo`（Windows） | `NULL` |

## 4. 函数语义

### 4.1 `ccpath_is_absolute`

返回路径是否为平台绝对路径。`NULL` 或空串返回 `CCFALSE`。不做规范化，不检查存在性。

### 4.2 `ccpath_resolve`

返回规范绝对路径；语义见第 3 节。`path` 为 `NULL` 或空串返回 `NULL`。

### 4.3 `ccpath_parent`

返回父目录：

| 输入 | 输出 |
|---|---|
| `/a/b` | `/a` |
| `/a` | `/` |
| `/` | `/` |
| `a/b` | `a` |
| `a` | `.` |
| `a/` | `.` |
| `C:/a`（Windows） | `C:/` |
| `C:/`（Windows） | `C:/` |
| `//server/share/a`（Windows） | `//server/share` |
| `//server/share`（Windows） | `//server/share` |

只做字符串截断，不解析 `.` 或 `..`。

### 4.4 `ccpath_basename`

返回最后一个路径组件：

| 输入 | 输出 |
|---|---|
| `/a/b/` | `b` |
| `a` | `a` |
| `/` | 空串 |
| `C:/`（Windows） | 空串 |
| `//server/share`（Windows） | `share` |

根路径没有名称组件，返回空串。

### 4.5 `ccpath_join`

拼接两段路径：

- `a` 为空返回 `b` 的副本；`b` 为空返回 `a` 的副本；两者都为空返回空串；
- `b` 为绝对路径时返回 `b` 的副本；
- 否则去掉 `a` 的尾部分隔符和 `b` 的首部分隔符，用单个 `/` 连接；
- 不解析 `.` / `..`，不检查存在性。

### 4.6 `ccpath_equal`

按平台语义比较路径：

- POSIX 使用逐字节比较；
- Windows 使用 ASCII 大小写不敏感比较，非 ASCII 字节按原值比较；
- 不做规范化，不忽略尾部分隔符，不解析符号链接。

`a`、`b` 都为 `NULL` 返回 `CCTRUE`；仅一方为 `NULL` 返回 `CCFALSE`。

## 5. 所有权与失败

- 所有 `char *` 返回值由 `ccalloc` 分配。
- 调用方负责释放；模块不保留返回指针。
- 分配失败、参数非法、路径形式不支持时返回 `NULL`。
- 函数不设置全局状态，可在多线程中并发调用；每个返回值归各自调用方所有。

## 6. 与 VFS 的边界

- `ccpath` 面向宿主路径，不处理 VFS 路径。
- VFS 路径由 `ccfiles` 自行解析：固定以 `/` 为根、不接受盘符、挂载锚点具有专门语义。
- VFS 节点反查宿主路径使用 `ccfiles_realpath`，不使用 `ccpath`。

## 7. 示例

```c
#include <ccalloc.h>
#include <ccpath.h>

static void resolve_example(void)
{
    char *abs = ccpath_resolve("src/../include", NULL);
    char *dir = ccpath_parent(abs);
    char *name = ccpath_basename("/a/b/c.txt");
    char *joined = ccpath_join("build", "obj/main.o");

    if (abs)
        ccalloc(abs, 0, NULL);
    if (dir)
        ccalloc(dir, 0, NULL);
    if (name)
        ccalloc(name, 0, NULL);
    if (joined)
        ccalloc(joined, 0, NULL);
}
```
