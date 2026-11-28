# cchost

宿主文件系统 Shell 能力。该模块只操作真实路径，不创建 VFS 探针、不挂载、不维护虚拟树，也不依赖 `ccfiles`。

## 1. 函数签名

```c
#include <cchost.h>

typedef enum cc_host_kind
{
    CC_HOST_MISSING = 0,
    CC_HOST_FILE    = 1,
    CC_HOST_DIR     = 2,
    CC_HOST_OTHER   = 3,
    CC_HOST_DENIED  = 4
} CCHostKind;

typedef struct cc_host_stat
{
    CCHostKind kind;
    CCUINT64   size;
    CCUINT64   mtime_ns;
} CCHostStat;

typedef enum cc_host_result
{
    CC_HOST_OK           =  0,
    CC_HOST_ERR_PARAM    = -1,
    CC_HOST_ERR_NOENT    = -2,
    CC_HOST_ERR_NOTDIR   = -3,
    CC_HOST_ERR_ISDIR    = -4,
    CC_HOST_ERR_PERM     = -6,
    CC_HOST_ERR_NOSPACE  = -7,
    CC_HOST_ERR_EXIST    = -8,
    CC_HOST_ERR_NOTEMPTY = -9,
    CC_HOST_ERR_IO       = -10
} CCHostResult;

CCINT32 cchost_stat(const char *path, CCHostStat *out);
CCINT32 cchost_read_all(const char *path, void **data, CCUINT64 *size);
CCINT32 cchost_mkdirs(const char *path);
CCINT32 cchost_remove(const char *path);
CCINT32 cchost_copy(const char *from, const char *to);
CCINT32 cchost_move(const char *from, const char *to);
```

`CCHostResult` 的数值与 `CCFsResult` 对齐，便于上层统一处理；两个模块的类型定义相互独立。

## 2. `cchost_stat`

一次查询返回宿主条目类型、大小与修改时间。

- `out` 先清零。
- 查询完成返回 `CC_HOST_OK`，`out->kind` 为 `MISSING` / `FILE` / `DIR` / `OTHER` / `DENIED`。
- `MISSING` 与 `DENIED` 属于正常分类结果，不作为函数错误返回。
- `size`、`mtime_ns` 仅对 `FILE` / `DIR` 有意义。
- 跟随符号链接，与 `stat` 语义一致。
- Windows 使用 `CreateFileA` + `GetFileInformationByHandle`，时间精度 100ns。
- POSIX 使用 `stat` 与 `st_mtim`，时间精度纳秒。
- `mtime_ns` 为 Unix 纪元起的纳秒数；无法表示时置 0。

## 3. `cchost_read_all`

读取整个文件到新分配的缓冲。

- `*data` 由 `ccalloc` 分配，末尾补 `'\0'`；`*size` 不含结尾 NUL。
- 空文件返回长度为 1 的缓冲，`*size == 0`。
- 二进制安全，不做编码转换。
- 目录返回 `CC_HOST_ERR_ISDIR`，不存在返回 `CC_HOST_ERR_NOENT`，权限不足返回 `CC_HOST_ERR_PERM`。
- 失败时 `*data == NULL`、`*size == 0`。

## 4. `cchost_mkdirs`

递归创建目录。

- 相对路径按当前工作目录解析。
- 已存在的目录不报错。
- 路径中任一组件是普通文件时返回 `CC_HOST_ERR_NOTDIR`。
- 根路径返回 `CC_HOST_OK`。

## 5. `cchost_remove`

删除文件或空目录。

- 不存在的路径返回 `CC_HOST_ERR_NOENT`。
- 普通文件直接删除。
- 空目录删除；非空目录返回 `CC_HOST_ERR_NOTEMPTY`。
- 不做递归删除。
- 符号链接按平台默认语义删除链接本身；不进入链接目标。

## 6. `cchost_copy`

复制普通文件。

- 目标已存在普通文件时覆盖。
- 目标是目录时返回 `CC_HOST_ERR_ISDIR`。
- 目标的父目录必须存在，否则返回 `CC_HOST_ERR_NOENT`。
- 源与目标是同一路径时返回 `CC_HOST_OK`，不改动文件。
- 只复制内容，不保留权限、时间等元数据。
- 失败时删除目标文件，避免留下半成品；目标原有内容不保证保留。

## 7. `cchost_move`

移动普通文件。

- 目标已存在普通文件时覆盖。
- 目标是目录时返回 `CC_HOST_ERR_ISDIR`。
- 源与目标是同一路径时返回 `CC_HOST_OK`。
- 跨卷移动按“复制 + 删除源”完成；删除源失败时目标已生成，函数返回错误。
- 只处理普通文件；目录返回 `CC_HOST_ERR_ISDIR`。

## 8. 与 VFS 的同步

`cchost` 与 `ccfiles` 没有代码依赖，二者操作同一份宿主文件时遵循显式同步：

```text
cchost 修改真实文件
        │
        ▼
VFS 节点树与元数据保持原状
        │
        ▼
调用方执行 ccfiles_refresh(fs, mount_v_path) 或重新挂载
```

```text
ccfiles_write / ccfiles_flush
        │
        ▼
cchost 立即可见
```

- `cchost` 不感知挂载点，不自动通知 VFS。
- 外部修改后直接对 VFS 节点求哈希或读取，可能得到按旧 `size` 截断的结果；需要先刷新。
- VFS 的 `ccfiles_stat` 是挂载/刷新时的缓存；`cchost_stat` 是实时宿主状态。

## 9. 所有权与失败

- `cchost_read_all` 返回的 `*data` 由调用方释放。
- 其余接口不返回分配内存。
- 所有接口不设置全局状态；对同一路径的并发修改由调用方负责串行化。
- 路径按宿主本地编码解释，Windows 下是 ANSI 代码页；来自 UTF-8 配置的路径先用 `cc_utf8_to_local(..., CC_LOCAL_SYSTEM)` 转换。

## 10. 示例

```c
#include <ccalloc.h>
#include <cchost.h>

static void inspect_file(const char *path)
{
    CCHostStat st;
    if (cchost_stat(path, &st) != CC_HOST_OK)
        return;

    if (st.kind != CC_HOST_FILE)
        return;

    void *data = NULL;
    CCUINT64 size = 0;
    if (cchost_read_all(path, &data, &size) == CC_HOST_OK)
    {
        // 使用 data / size
        ccalloc(data, 0, NULL);
    }
}
```
