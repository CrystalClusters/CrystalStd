# cc_hash

MD5 摘要计算。模块属于文件管理/VFS 体系，同时支持对一段内存字节求摘要。

## 1. 函数签名

```c
#include <cc_hash.h>

#define CC_MD5_DIGEST_SIZE 16

typedef struct cc_hash_input
{
    const void *data;    /* 非 NULL：内存模式，优先级最高 */
    CCUINT64    size;    /* 仅内存模式使用；data 为 NULL 时忽略 */
    CCFiles    *fs;      /* VFS 模式：探针；data 为 NULL 时使用 */
    const char *v_path;  /* VFS 模式：节点路径；data 为 NULL 时使用 */
} CCHashInput;

typedef struct cc_md5_digest
{
    CCUINT8 bytes[CC_MD5_DIGEST_SIZE];
} CCMd5Digest;

CCINT32 cc_md5(const CCHashInput *input, CCMd5Digest *out);
```

## 2. 模式判定

| `data` | `fs` / `v_path` | 模式 |
|---|---|---|
| 非 `NULL` | 任意，忽略 | 内存模式，对 `data[0..size)` 求摘要 |
| `NULL` | 两者都有效 | VFS 节点模式 |
| `NULL` | 只给出其中一个 | `CC_FS_ERR_PARAM` |
| `NULL` | 两者都为空 | `CC_FS_ERR_PARAM` |

补充约定：

- `data` 非 `NULL` 时，VFS 参数不参与判定，也不读取 VFS。
- `data` 为 `NULL` 时，`size` 完全忽略，可以是任意值。
- 空内存字节串必须传非 `NULL` 指针与 `size == 0`，例如 `""` 或指向零长数组的指针。
- `input`、`out` 为 `NULL` 返回 `CC_FS_ERR_PARAM`。
- 失败时 `out` 清零；成功返回 `CC_FS_OK`。

## 3. 内存模式

- 对 `data[0..size)` 计算 MD5。
- `data` 指向的缓冲只读，函数不保留指针。
- `size` 为 0 时返回空字节串的 MD5。
- 不分配临时缓冲。

## 4. VFS 节点模式

- 对 `v_path` 对应节点的当前内容计算 MD5。
- 路径按 `ccfiles` 规则解析，支持相对当前工作目录。
- 磁盘节点、内存节点、资源包节点均可计算。
- 计算前刷新该节点的写缓冲，摘要包含尚未落盘的数据。
- 目录返回 `CC_FS_ERR_ISDIR`。
- 路径不存在返回 `CC_FS_ERR_NOENT`。
- 读取失败返回 `CC_FS_ERR_IO`。
- 按块读取，不把整个文件载入内存。
- 只读操作：不修改 CRC、mtime、当前文件指针或工作目录。
- VFS 节点来自外部修改且未刷新时，`size` 可能是旧值；此时应先 `ccfiles_refresh` 或对宿主字节求摘要。

## 5. 输出

- `CCMd5Digest.bytes` 为 16 字节原始摘要。
- 模块不提供十六进制字符串接口；格式化与比较由调用方完成。
- 比较摘要使用 `memcmp`。

## 6. 安全边界

MD5 用于非对抗性的变更检测与完整性判断。该算法不具备抗碰撞安全性，不用于签名、口令或对抗性场景。

## 7. 示例

```c
#include <ccalloc.h>
#include <cc_hash.h>

static void hash_memory(const void *bytes, CCUINT64 size)
{
    CCHashInput input = {0};
    CCMd5Digest digest;

    input.data = bytes;
    input.size = size;
    if (cc_md5(&input, &digest) == CC_FS_OK)
    {
        // 使用 digest.bytes
    }
}

static void hash_vfs(CCFiles *fs, const char *v_path)
{
    CCHashInput input = {0};
    CCMd5Digest digest;

    input.fs = fs;
    input.v_path = v_path;
    if (cc_md5(&input, &digest) == CC_FS_OK)
    {
        // 使用 digest.bytes
    }
}
```

## 8. 测试要点

- 标准向量：空串、`"abc"`、多块长度、长文件。
- 内存模式与 VFS 模式对同一内容得到相同摘要。
- `data` 与 VFS 参数同时存在时按内存模式。
- `data` 为 `NULL` 且只给出 `fs` 或只给出 `v_path` 返回参数错误。
- 目录、缺失路径、未 flush 写入、包节点、内存节点。
- 输出在失败时清零。
