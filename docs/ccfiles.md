# ccfiles

统一文件管理内核。提供一套虚拟文件空间，用于检索、映射与操作文件：既可将磁盘中的真实文件 / 目录 / 资源包挂载到虚拟空间，也可在虚拟空间中创建文件并**直接写入磁盘**。目标是模仿 shell 的文件操作，把 `ls`、`cp`、`mv`、`rm`、`find` 等能力内化到库中（相当于库内置的跨平台 shell）。

> 头文件为 `include/ccfiles.h`，使用 `#include <ccfiles.h>`。
> 公开头文件只暴露枚举、`CCFsStat` / `CCFsEntry` 输出结构、不透明探针句柄 `CCFiles` 与函数原型；节点、挂载、探针等内部结构定义于 `src/ccfiles_internal.h`，**不属于公开契约**，字段随时可能调整。

---

## 1. 接口

### 1.1 通用约定

- 含 `v_path` 的接口：`/` 开头为绝对寻址，否则相对当前工作目录（cwd）解析，`.` / `..` 按 shell 语义处理。
- `real_path` 一律用 `/`，禁止反斜杠（Windows 下由内核在调用系统 API 前转换）；相对路径以**可执行程序同层的 `res` 目录**为基准解析，亦可直接传绝对路径。注意 `real_path` 是**宿主路径**：Windows 下不带盘符的 `/x` 指**当前盘根**（如 `D:\x`），与虚拟根 `/` 不是一回事；建议显式写 `D:/x`，或直接用相对路径（落在 exe 同层 `res`，正好对应虚拟根 `/`）。
- 操作类接口返回 `CCINT32`，取下方 `CCFsResult`；读写类返回**有符号** `CCINT64` 实际字节数，失败返回负的 `CCFsResult`（如 `CC_FS_ERR_PROTECT`），即返回值 `< 0` 为错误、`>= 0` 为实际字节数。
- **路径长度与树深度均无固定上限**：内部路径以 `CCDyn` 动态拼接、按需扩容，只受可用内存约束；目录扫描、子树删除、复制、打包等均以显式栈迭代实现，不存在递归深度上限（超长路径仅可能因内存不足返回 `CC_FS_ERR_NOSPACE`）。`ccfiles_getcwd` 的 `size` 是调用方提供的输出缓冲容量，属于外部约束。

错误码（可扩展，按需增删，用于快捷区分错误类型）：

```C
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
```

### 1.2 探针与生命周期

```C
CCFiles* ccfiles_create(void);          /* 创建探针 */
void     ccfiles_destroy(CCFiles *fs);  /* 销毁探针 */
```

**参数含义：**
- `fs`：待销毁的探针。

**返回值：**

- **ccfiles_create：**
成功返回堆分配的探针（初始 cwd 为 `/`、未指向文件），失败返回 `NULL`。
- **ccfiles_destroy：**
无返回值。

**注意事项：**
- 虚拟树本身是全局单例，随 `crystal_std` 自动初始化与释放，无需手动管理。
- `CCFiles` 是**不透明句柄**（`include/ccfiles.h` 只有前置声明），只能通过接口操作；其内部结构见第 6 章。
- `ccfiles_destroy` 会自动从节点摘除探针回指并释放探针。

### 1.3 工作目录与当前文件

```C
CCINT32 ccfiles_chdir(CCFiles *fs, const char *path);           /* 切换工作目录 */
CCINT32 ccfiles_getcwd(CCFiles *fs, char *buf, CCUINT32 size);  /* 取工作目录 */
CCINT32 ccfiles_getcur(CCFiles *fs, char *buf, CCUINT32 size);  /* 取当前指向文件的绝对路径 */
```

**参数含义：**
- `path`：目标目录，支持绝对（`/` 开头）与相对（相对当前 cwd）。
- `buf` / `size`：输出缓冲区及其容量。

**返回值：**

- **ccfiles_chdir：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`（`fs` / `path` 为空）；`CC_FS_ERR_NOENT`（路径不存在）；`CC_FS_ERR_NOTDIR`（不是目录）。
- **ccfiles_getcwd / ccfiles_getcur：**
成功返回写入字节数（含结尾 `0`）；`CC_FS_ERR_PARAM`（参数非法或缓冲区不足）；`getcur` 在探针尚未指向文件时返回 `CC_FS_ERR_NOENT`。

**注意事项：**
- 探针初始 cwd 为 `/`。
- `chdir` 只改变 cwd，不影响探针当前指向（`cur`）。
- `ccfiles_getcur` 是查询「是否已 open / create、以及指向哪个文件」的唯一公开途径；探针内部字段不对外暴露。

### 1.4 挂载与刷新

```C
typedef enum cc_fs_mount_type {
    CC_FS_MOUNT_DIR  = 1,   /* 挂载真实目录，递归生成子树 */
    CC_FS_MOUNT_PKG  = 2,   /* 挂载只读资源包 */
    CC_FS_MOUNT_FILE = 3,   /* 挂载单个真实文件 */
} CCFsMountType;

CCINT32 ccfiles_mount(CCFiles *fs, const char *v_path, const char *real_path, CCUINT32 type); /* 挂载 */
CCINT32 ccfiles_unmount(CCFiles *fs, const char *v_path);                                     /* 卸载 */
CCINT32 ccfiles_refresh(CCFiles *fs, const char *v_path);                                     /* 刷新子树 */
```

**参数含义：**
- `v_path`：挂载点、卸载点或刷新根。
- `real_path`：真实（宿主）路径（`/` 分隔，**不可为 `NULL`**）；相对路径以可执行程序同层 `res` 目录为基准；`/` 开头为宿主绝对路径（Windows 下无盘符的 `/x` 即当前盘根，别与虚拟根 `/` 混淆）。
- `type`：`CCFsMountType`。

**返回值：**

- **ccfiles_mount：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`（`fs` / `v_path` / `real_path` 为空、`type` 非法）；`CC_FS_ERR_PROTECT`（`v_path` 处于包投影保护范围）；`CC_FS_ERR_PERM`（系统权限错误）；`CC_FS_ERR_IO`（真实路径无法访问 / 挂载失败）。
- **ccfiles_unmount：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`（目标不存在）；`CC_FS_ERR_NOTMOUNT`（不是挂载锚点）。
- **ccfiles_refresh：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_NOTDIR`。

**注意事项：**
- `mount` 幂等：`v_path` 不存在则创建挂载点，已存在则覆盖挂载信息并自动 `refresh`（锚点阻断，嵌套挂载点原样保留）。
- `unmount` 类比 Linux `umount`：节点还原为跟随上级；其下显式子挂载锚点原样保留；指向该后端的探针立即失效，后续 `read` / `write` 失败，需重新 `mount` + 指向。
- `refresh` 以 `v_path` 为根递归重建子树，遇到显式挂载锚点停止深入、原样保留（同 Linux 嵌套挂载不穿透）；纯内存虚拟文件不被清除；对包投影节点执行 `refresh` 原样保留、不穿透。

### 1.5 文件读写与落盘

```C
CCINT32  ccfiles_open(CCFiles *fs, const char *v_path);                                /* 指向文件 */
CCINT64  ccfiles_read(CCFiles *fs, void *buf, CCUINT64 offset, CCUINT64 size);         /* 读取 */
CCINT64  ccfiles_write(CCFiles *fs, const void *data, CCUINT64 offset, CCUINT64 size); /* 写入 */
CCINT32  ccfiles_flush(CCFiles *fs);                                                   /* 刷新写缓冲（NULL = 全部） */
CCINT32  ccfiles_stat(CCFiles *fs, const char *v_path, CCFsStat *out);                 /* 查询元信息 */
```

**参数含义：**
- `v_path`：文件路径（`open` / `stat`）。
- `buf` / `data`：用户读 / 写缓冲区。
- `offset`：显式偏移（字节）。
- `size`：读 / 写字节数。
- `out`：`CCFsStat` 输出。
- `flush` 参数可选：传探针则刷新该探针当前文件的写缓冲，传 `NULL` 刷新全部已打开文件。

**返回值：**

- **ccfiles_open：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_ISDIR`（是目录）。
- **ccfiles_read：**
实际读取字节数（`0..size`）；失败返回负的 `CCFsResult`（如 `CC_FS_ERR_PROTECT` / `CC_FS_ERR_IO`）。
- **ccfiles_write：**
实际写入字节数；失败返回负的 `CCFsResult`（如 `CC_FS_ERR_PROTECT` / `CC_FS_ERR_NOSPACE`）。
- **ccfiles_flush：**
`CC_FS_OK`；`CC_FS_ERR_IO`（落盘失败）。
- **ccfiles_stat：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`。

**注意事项：**
- `read` 仿 C 标准库 `fread`：从 `offset` 起读至多 `size` 字节；`offset` 已到 / 越过文件末尾返回 `0`（EOF，非错误）；失败（未指向文件 / 参数非法 / 后端只读或 IO 错误）返回负的 `CCFsResult`，而非 `-1` 哨兵。
- `write` 自动扩展文件：`offset` 越过当前末尾时扩展，**中间未定义字节补 `0`**；写后 `size` = `max(旧size, offset + 实际写入量)`；写入**直通该文件的物理实体**（数据进入该文件常驻 `FILE*` 的 stdio 缓冲）；空间不足（磁盘满）返回 `CC_FS_ERR_NOSPACE`，写保护包节点返回 `CC_FS_ERR_PROTECT`。
- 读写均按显式 `offset` 随机访问，**无状态，需每次指定值**。
- **常驻句柄与探针同寿命**：首次读写时按需打开该文件，只要还有探针指向它（`probe_head != NULL`）就复用同一个 `FILE*`；顺序读写靠内部记录的当前位置省去 `fseek`，因此 1 字节 × N 次顺序写只有 memcpy 级开销。最后一个探针离开、节点被删除、改名 / 跨锚点移动、`refresh` / `unmount`、结构操作与析构时统一 `fflush + fclose`。
- `ccfiles_flush(fs)`：把当前文件的写缓冲立即刷到磁盘；`fs == NULL` 刷新全部已打开文件。需要「其他程序立刻能读到」时显式调用。
- 任何落盘失败（磁盘满 / IO 错误）都**如实返回负错误码**，绝不吞掉失败返回成功。
- `stat` 只读取持久化保存的 CRC，**绝不现算**；`ctime` / `mtime` 为**秒级**时间戳（统一取节点创建 / 修改时刻的系统秒值，无则 `0`）。

`CCFsStat`：

```C
typedef struct cc_fs_stat {
    CCFsType  type;      /* 文件 / 目录 */
    CCUINT64  size;      /* 字节数 */
    CCUINT64  ctime;     /* 创建时间（秒级系统时间戳）；无则 0 */
    CCUINT64  mtime;     /* 修改时间（秒级系统时间戳）；无则 0 */
    CCBOOL    readonly;  /* 是否只读（包节点恒为真） */
    CCUINT32  crc32;     /* 持久化 CRC：包内文件取块头，打包 / 解包时记录；纯磁盘文件无记录为 0 */
} CCFsStat;
```

### 1.6 目录与检索

```C
/* 检索结果条目：仅在回调执行期间有效 */
typedef struct cc_fs_entry {
    const char *name;      /* 文件 / 目录名（不含路径） */
    CCFsType    type;      /* 文件 / 目录 */
    CCUINT64    size;      /* 字节数 */
    CCUINT64    ctime;     /* 创建时间（秒级系统时间戳）；无则 0 */
    CCUINT64    mtime;     /* 修改时间（秒级系统时间戳）；无则 0 */
    CCBOOL      readonly;  /* 是否只读（包节点恒为真） */
    CCUINT32    crc32;     /* 持久化 CRC；无则 0 */
} CCFsEntry;

CCINT32 ccfiles_list(CCFiles *fs, const char *v_path, CCDataCallback cbk, void *user);                   /* 遍历目录 */
CCINT32 ccfiles_find(CCFiles *fs, const char *v_path, const char *pattern, CCDataCallback cbk, void *user); /* 通配符检索 */
```

**参数含义：**
- `v_path`：`list` 的目标目录；`find` 的搜索根（在其子树内递归）。
- `cbk`：结果回调 `CCDataCallback`；`data` 传 `CCFsEntry*`（名字 + 类型 / 大小 / 时间 / 只读标记 / CRC 的公开快照），`user` 为用户上下文。
- `pattern`：`find` 的通配符，仅匹配 basename（不含 `/`），支持 `*` / `?`。

**返回值：**

- **ccfiles_list：**
遍历完毕返回 `CC_FS_OK`；回调返回非 `0` 时中断并透传该值；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_NOTDIR`。
- **ccfiles_find：**
遍历完毕返回 `CC_FS_OK`；回调返回非 `0` 时中断并透传该值；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`。

**注意事项：**
- `CCDataCallback` 定义于 `crystal_cluster.h`（`typedef CCINT32(*CCDataCallback)(void *data, void *user);`），`data` 指向 `CCFsEntry`。
- `list` 按字典序（红黑树中序）回调。
- `find` 递归遍历整棵子树；`*` 不跨目录，跨目录递归由 `find` 遍历完成。
- `CCFsEntry` 是栈上快照，**仅在回调执行期间有效**；`name` 指向节点内部字符串，回调返回后同样失效。需要保留名字请立即拷贝。
- 回调内**不得对树做结构性修改**（创建 / 删除 / 移动等），否则遍历状态失效；该约束由调用方自行保证。

### 1.7 shell 式文件操作

```C
CCINT32 ccfiles_create_file(CCFiles *fs, const char *v_path);                       /* 创建文件 */
CCINT32 ccfiles_create_dir(CCFiles *fs, const char *v_path);                        /* 创建空目录（mkdir） */
CCINT32 ccfiles_remove(CCFiles *fs, const char *v_path);                            /* 删除（rm） */
CCINT32 ccfiles_remove_recursive(CCFiles *fs, const char *v_path);                  /* 递归删除（rm -rf） */
CCINT32 ccfiles_cp(CCFiles *fs, const char *src, const char *dst);                  /* 复制（cp） */
CCINT32 ccfiles_mv(CCFiles *fs, const char *src, const char *dst);                  /* 移动/改名（mv） */
CCINT32 ccfiles_pack(CCFiles *fs, const char *v_path, const char *real_path);       /* 打包 */
CCINT32 ccfiles_unpack(CCFiles *fs, const char *v_path, const char *real_path);     /* 解包 */
```

**参数含义：**
- `v_path`：目标文件 / 目录 / 包路径（`create_file` / `create_dir` / `remove` / `pack` / `unpack`）。
- `src`：源路径（`cp` / `mv`）。
- `dst`：目标路径（`cp` / `mv`，可为文件或目录）。
- `real_path`：`pack` 输出包路径 / `unpack` 输出目录路径。

**返回值：**

- **ccfiles_create_file：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`（父目录不存在）；`CC_FS_ERR_PROTECT`（包节点）；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_create_dir：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_EXIST`（同名文件或目录已存在）；`CC_FS_ERR_NOENT`（父目录不存在）；`CC_FS_ERR_PROTECT`（包节点）；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_remove：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_NOTEMPTY`（非空目录）；`CC_FS_ERR_PROTECT`；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_remove_recursive：**
`CC_FS_OK`（路径不存在也返回 `OK`，`-f` 语义）；`CC_FS_ERR_PARAM`；`CC_FS_ERR_PROTECT`；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_cp：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_PROTECT`；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_mv：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_PROTECT`；`CC_FS_ERR_IO`。
- **ccfiles_pack：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_NOTDIR`；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。
- **ccfiles_unpack：**
`CC_FS_OK`；`CC_FS_ERR_PARAM`；`CC_FS_ERR_NOENT`；`CC_FS_ERR_EXIST`（目标已存在）；`CC_FS_ERR_NOSPACE`；`CC_FS_ERR_IO`。

**注意事项（对齐 shell 习惯）：**
- `create_file`：创建并立即在物理层落一个空文件（已存在的同名物理文件内容保持不变），返回的探针自动指向它；未显式挂载时自动沿用上级挂载链路；物理创建失败返回 `CC_FS_ERR_IO` 并回滚虚拟节点。
- `create_dir` 同 `mkdir`：立即在虚拟树与物理层同时建立目录（含缺失的上级）；已存在同名文件或目录返回 `CC_FS_ERR_EXIST`；物理创建失败返回 `CC_FS_ERR_IO` 并回滚虚拟节点。
- `remove` 同 `rm`：删除文件或空目录；非空目录返回 `CC_FS_ERR_NOTEMPTY`（需先删子项）；包节点返回 `CC_FS_ERR_PROTECT`。
- `remove_recursive` 同 `rm -rf`：递归删除任意子树（文件 / 非空目录）；路径不存在返回 `OK`（`-f`）；`/` 等价 `rm -rf /*`——清空根所有内容、保留根节点与根锚点目录本身；包节点返回 `CC_FS_ERR_PROTECT`。
- **删除立即落盘**：`remove` / `remove_recursive` 删除虚拟节点的同时立即 unlink / rmdir 对应物理实体；物理删除失败则返回真实错误码并保留虚拟节点。显式挂载锚点自身只摘除虚拟节点，不删除宿主的真实目录 / 文件。**符号链接 / junction 只删除链接本身，绝不递归进入链接目标。**
- `cp` 同 `cp -r`：递归复制文件或目录，**穿透子树内的显式挂载锚点、复制其内容（挂载关系不复制）**；`dst` 为已存在目录时复制到其内部，否则复制为 `dst`；目标文件已存在则覆盖。
- `mv` 同 `mv`：`dst` 为已存在目录时移入其内部，否则移动 / 改名；目标文件已存在则覆盖。**同锚点（同一显式挂载锚点）走原子 `rename`**（物理即时移动、虚拟层重建目标投影，不复制数据）；**跨锚点 / 内存↔磁盘**等价「复制 + 删除源」，两端立即落盘，并**穿透子树内的显式挂载锚点、移动其内容（挂载关系不跟随）**。移动前先 flush + 关闭涉及的全部常驻句柄；覆盖目标时先将旧目标改名备份，移动失败会恢复备份，避免目标数据丢失。
- `pack` / `unpack`：目录 ⇄ 资源包；包投影节点为受保护只读，写 / 删 / 改名 / 重新 mount 一律返回 `CC_FS_ERR_PROTECT`，修改包只能「解包 → 修改 → 重新打包」。
- `pack` 的目标是**宿主文件**（按 `real_path` 规则解析，见第 2 节），不会作为虚拟节点进入虚拟树；若目标落在某个挂载子树内，需 `refresh` 对应目录后 `ls` 才能看到。`unpack` 同理，数据直接写到宿主。

---

## 2. 路径规则

- 虚拟路径一律以 `/` 开头，组件之间用 `/` 分隔。
- **统一用 `/`，禁止反斜杠**：虚拟路径与 `real_path` 均不得包含反斜杠，Windows 下由内核在调用系统 API 前转换。虚拟路径入口会拒绝 `\`（返回 `CC_FS_ERR_PARAM`），不会把它当成路径分隔符。
- **Windows 路径组件不得以空格或点结尾**：Win32 会静默去掉这些字符，若放行会让 `".. /x"` 之类的名字绕出目标目录。此类路径与资源包内名字一律拒绝。
- `real_path` 是**宿主路径**：相对路径以可执行程序同层的 `res` 目录为基准；`/` 开头的绝对路径原样交给宿主 API。Windows 下不带盘符的 `/x` 指**当前盘根**（`D:\x`），**不是**虚拟根 `/`；需要绝对路径时请显式写 `D:/x`，或使用相对路径。
- 入参路径先做规范化：合并重复 `/`、解析 `.` 与 `..`、去除空段。
- `..` 按 shell 语义回到虚拟父目录；根目录 `/` 处停留于 `/`。挂载锚点不阻断 `..`，物理路径归属仍由最近锚点决定。
- 虚拟树内比较为字节序、大小写敏感，与平台解耦。

---

## 3. 检索与通配符

- 检索发生在虚拟树内（含内存虚拟文件与包内文件），不直接扫描磁盘。
- 通配符匹配复用基础组件 `cc_wildcard`（见 `wildcard.md`）：支持 `*`（任意长度）与 `?`（单字符），逐字节比较，DP 滚动数组实现。
- `*` 不跨目录；跨目录递归由 `find` 遍历整棵子树完成。
- `pattern` 仅作用于单个文件名（basename），不含 `/`；例如 `find(v_path, "*.txt")` 在任意深度命中名为 `*.txt` 的文件。
- 结果通过 `CCDataCallback` 逐个交付；回调返回非 `0` 时中断并透传。

---

## 4. 产品形态

- **全局单例虚拟树**：全进程只有一棵虚拟文件树（节点连续存于 `cc_dyn_core`），由 `crystal_std` 在构造 / 析构时自动初始化与释放，任意合法位置均可操作。
- **`CCFiles` 是探针**：探针记录持有者的操作起点——工作目录（cwd）与当前指向的文件。探针**指向**文件即把自身回指挂入节点链表；多个探针可指向同一节点，节点删除时沿回指链表统一失效。
- **显式偏移、随机访问**：读写按用户给定的 `offset` 进行，**无状态，需每次指定值**。
- **Linux 风格挂载**：虚拟空间以 `/` 为根，所有挂载点都位于 `/` 之下，挂载 / 卸载语义类比 Linux `mount` / `umount`。
- **每个节点都有挂载信息**：节点持有「物理挂载链路」；物理挂载填 `NULL` 表示**跟随上级**。每个虚拟文件都有物理实体对应（创建 / 写入时立刻在磁盘生成，不留延迟）；未显式指定挂载的新建节点自动基于上级目录生成挂载信息。
- **根挂载默认指向 `res`**：`/` 节点默认挂载到可执行程序同级目录的 `res` 文件夹；任意节点的挂载信息均可通过接口随时修改。
- **只读资源包**：包（光盘语义）挂载后在虚拟树中生成**受保护的只读投影节点**——只提供读取，写 / 删 / 改名 / 重新 mount 等**一切非读操作全部失效**；修改包内容只能「解包 → 修改 → 重新打包」。读写权限由类型决定，一般只有包不可写，其余类型遇到操作系统权限错误时**如实透传**。
- **写入直通物理文件**：每个文件在其有探针指向期间持有常驻 `FILE*`，写入直接进该句柄的 stdio 缓冲；最后一个探针离开、结构操作、`ccfiles_flush` 或析构时 `fflush + fclose`。没有任何延迟落盘的中间层，落盘失败如实返回错误码。
- **按需流式读写**：读按 `offset` 定位、按需分块搬运，挂载的真实文件只登记元数据，不会把整文件载入内存。
- **手动刷新（锚点阻断）**：挂载文件夹时递归遍历并生成虚拟树；之后不主动刷新。显式 `refresh` 以指定节点为根递归重建子树，遇到**显式挂载锚点**（`real_path != NULL`）即停止深入、原样保留，语义与 Linux 嵌套挂载不穿透一致；纯内存虚拟文件不因刷新被清除。
- **检索在虚拟树内完成**：`find` / `ls` 遍历虚拟文件树（含内存虚拟文件与包内文件）。
- **回调式结果**：检索类接口通过回调逐个交付结果，避免一次性大数组的内存峰值。
- **不内置锁**：调用方需按自身业务流程在合适粒度自行加锁（详见第 7 节）。

### 4.1 设计决策

| 项 | 决策 |
|---|---|
| 上下文模型 | 虚拟树为全局单例；`CCFiles` 是记录操作起点的探针（堆对象），可建多个、各自独立位置 |
| 生命周期 | 全局树随 `crystal_std` 自动 init/deinit；探针由 `ccfiles_create/destroy` 建销 |
| 虚拟根 | Linux 风格：初始化创建 `/` 挂载点，新增挂载点全部位于 `/` 之下 |
| 默认根挂载 | `/` 默认挂载到可执行程序同级目录的 `res` 文件夹 |
| 路径风格 | 内部统一 Unix 风格 `/`；Windows 反斜杠不进入内部表示 |
| 路径上下文 | 探针持有 cwd（默认 `/`）；支持 shell 风格相对寻址（`.`/`..`）与绝对寻址（`/` 开头） |
| 挂载模型 | 每个节点持有挂载信息；新建节点默认 `real_path == NULL`（跟随上级），显式 `mount` 必须给出非 NULL 的 `real_path` 才成为锚点；物理对应 = 最近锚点 + 相对路径；任意非包投影节点可再次 mount 覆盖并重建 |
| 索引结构 | 复合结构：节点连续存 `cc_dyn_core`（下标互指），每个目录节点向下挂一棵子红黑树（`cc_rbt_core`，经 `CC_RbtOps` 适配下标语义）做同层检索；参考 `ccthread` 实现 |
| 红黑树键值 | 键为节点内 `name` 字符串指针；`strcmp` 逐字节**无符号**比较，首个不同字节决定序 |
| 大小写 | 虚拟树内一律大小写敏感（字节序），与平台解耦 |
| 资源包 | 只读（光盘语义），挂载后生成受保护只读投影节点（写 / 删 / 改名 / 重新 mount 等非读操作失效）；修改需解包后重新打包 |
| 权限 | 由类型决定：包节点为受保护只读投影（写 / 删 / 改名 / 重新 mount 等非读操作全部失效）；其余类型可写，操作系统权限错误如实透传 |
| 落盘 | 写入直通物理文件的常驻句柄；句柄随最后一个探针离开 / 结构操作 / `ccfiles_flush` / 析构关闭并落盘；包内文件只读 |
| 存储模型 | 文件内容全在物理实体中按 `offset` 流式读写；挂载的真实文件只登记元数据，不整文件载入内存 |
| CRC | 包内文件存块头、打包 / 解包时记录；`stat` 只读取保存值、绝不现算，无记录返回 0 |
| 刷新 | `refresh` 以指定节点为根递归重建子树；显式挂载锚点为界、原样保留（同 Linux 嵌套挂载不穿透）；纯内存虚拟文件不因刷新被清除 |
| 包格式 | 自定义只读二进制包：32 字节头 + 32 字节/块 的块表 + 名字区 + 数据区，固定小端显式序列化（见 6.3） |
| 检索 | 遍历虚拟树；结果回调式交付；挂载时递归生成，手动刷新 |
| 返回码 | 操作类返回 `CCFsResult`（可扩展错误码，快捷区分错误类型）；读写类返回实际字节数，失败 `(CCUINT64)-1`；详见第 1 节 |
| 内存 | 统一使用 `ccalloc(old, size, comment)` |
| 并发 | 库不内置锁，调用方自行局部加锁；探针失效标志用 `_Atomic CCBOOL` |

---

## 5. 核心概念

| 概念 | 说明 |
|---|---|
| 虚拟文件树 | 全局单例，一棵以 `/` 为根的树（嵌套红黑树 + 节点连续存于动态数组），每个节点是一个虚拟文件或目录 |
| 探针（`CCFiles`） | 对外是不透明句柄：内部记录 cwd、当前指向的文件与 `_Atomic` 失效标志；指向文件即把回指挂入节点链表（多探针共用，删除节点时统一失效） |
| 挂载信息 | 每个节点持有的物理对应：真实路径 + 挂载类型 + 后端句柄；`real_path == NULL` 表示跟随上级 |
| 挂载锚点 | `real_path != NULL` 的节点；其下未显式挂载的节点继承该锚点的物理路径 |
| 后端 | 节点的实际数据来源：磁盘文件 / 包内块 / 内存分块，由挂载类型决定 |
| 刷新 | 以指定节点为根递归重建子树；遇到显式挂载锚点即停止深入、原样保留 |

---

## 6. 内部数据结构

> 本章描述实现细节，供维护者阅读。内部结构与共享声明集中定义在 `src/ccfiles_internal.h`，由 `src/ccfiles*.c` 共同包含，**不在 `include/` 中出现**；节点下标、`fp`、`probe_head` 等字段是纯实现细节，一旦被外部代码依赖即视为缺陷。

### 6.1 源码布局

| 文件 | 职责 |
|---|---|
| `src/ccfiles.c` | 运行时状态、节点红黑树、节点 / 挂载生命周期、探针、初始化，以及 chdir / stat / list / find 查询 |
| `src/ccfiles_utilities.c` | 字符串与动态路径缓冲、时间、宿主文件系统原语、res 目录解析、64 位定位 / 截断 |
| `src/ccfiles_path.c` | 挂载锚点、物理路径解析、虚拟路径走查、目录投影扫描、挂载 / 卸载 / 刷新 |
| `src/ccfiles_rw.c` | 节点常驻句柄、原始读写、公开的 open / read / write / flush |
| `src/ccfiles_shell.c` | mkdir / rm / mv / cp 及跨锚点原子搬移 |
| `src/ccfiles_pkg.c` | 资源包编解码、包投影、打包 / 解包 |
| `src/ccfiles_internal.h` | 内部数据结构、运行时状态、`static inline` 下标访问器与跨模块内部接口声明 |

### 6.2 虚拟树节点 `CCFsNode`

采用「动态数组存节点 + 嵌套红黑树做同层检索」的复合结构，参考实现见 `src/ccthread.c` 的 `CCThreadNode` / `thread_tree_ops`：

- **节点连续存储**：`CCFsNode` 逐个 push 进全局单例虚拟树的 `CCDyn`；节点之间一律用**下标**互指（数组扩容会整块搬移地址，指针会失效，下标不变）。
- **下标 ↔ 地址换算**：`node_at(idx)` / `idx_of(node)`；根也以 `CCUINT32` 下标保存，`CC_FS_NIL = 0xFFFFFFFF` 表示空（与 `ccthread` 的 `CC_THREAD_NIL` 一致）。
- **嵌套红黑树**：每个目录节点持有其子节点红黑树的根下标 `children_root`，形成「目录 → 子红黑树 → 子节点」的嵌套结构；同层按名检索通过 `cc_rbt_core` 完成。
- **适配 `CC_RbtOps`**：`get_node` / `set_node` / `get_color` / `set_color` / `left_rotate` / `right_rotate` / `ch_root` 等回调，把「下标索引的连续节点」适配为内核的「指针语义」。
- **删除策略（swap-remove + 探针失效）**：节点从父目录红黑树摘除后，采用「末尾节点搬到空位 + 弹尾」（swap-remove）保持数组紧凑，`node_count()` 恒等于存活节点数，无空洞、内存可随删除回收。搬移时同步修正指向末尾节点的引用（其父的左右子槽、其左右子的 `parent`、其所在目录的 `children_root`），并把其子节点的 `fs_parent` 改到新下标（搬目录为 O(子节点数)、搬文件 O(1)）。删除前先 `fs_fh_close_all` 落盘并关闭全部常驻句柄，再沿节点探针回指链表失效所有指向它的探针（`_Atomic CCBOOL alive = CCFALSE`），从而把删除锁死在红黑树内部下标关系内，无需对外 ID。
- **拷入/迁移要点**：删除双子节点时内核会把后继节点数据拷入被删槽位（`copy_node`），实现需同步：① 修正被拷入槽位命名子树根的 `fs_parent`；② 把常驻句柄 `fp` / `fp_pos` / `fp_writable` 按所有权转移拷入（源槽位置空，避免 `FILE*` 泄漏）；③ 把后继节点的探针回指链表迁到新槽位并更新各探针 `cur`；④ 释放被删条目原有数据。swap-remove 时整结构复制，句柄随节点一起搬移。子树删除采用「先收集节点下标、从大到小逐个 swap-remove」：父下标恒小于子下标，故从大到小即后序，且搬移的末尾节点必为「不在删除集合内的存活节点」，收集表下标不会悬空。

类型标识符（实现时取值可自定）：

```C
typedef enum cc_fs_type {
    CC_FS_TYPE_NONE = 0,
    CC_FS_TYPE_DIR  = 1,
    CC_FS_TYPE_FILE = 2,
    /* 预留：符号链接、硬链接等 */
} CCFsType;
```

```C
/* 虚拟树节点下标哨兵 */
#define CC_FS_NIL             0xFFFFFFFFu
```

```C
/* 前置声明：CCFsMount 定义见 6.2 */
typedef struct cc_fs_mount CCFsMount;

typedef struct cc_fs_node {
    /* 键：文件名（字符串本体独立分配，节点只持指针） */
    char     *name;
    /* 红黑树字段：位于父目录的子红黑树中，一律存下标，CC_FS_NIL 表示空 */
    CCUINT32  parent;
    CCUINT32  left;
    CCUINT32  right;
    CCBOOL    is_red;
    /* 目录层级与嵌套 */
    CCUINT32  fs_parent;      /* 父目录节点下标，用于向上导航 */
    CCUINT32  children_root;  /* 目录节点：其子节点红黑树的根下标 */
    /* 类型与挂载 */
    CCFsType  type;           /* 文件 / 目录 */
    CCFsMount *mount;         /* 挂载信息；NULL 表示跟随上级，见 6.2 */
    /* 元数据：stat 直接读取，不现算 */
    CCUINT64  size;           /* 字节数 */
    CCUINT64  ctime;          /* 创建时间（秒级系统时间戳） */
    CCUINT64  mtime;          /* 修改时间（秒级系统时间戳） */
    CCUINT32  crc32;          /* 持久化 CRC：包内取块头、打包 / 解包时记录；无则 0 */
    /* 指向该节点的探针回指链表头（NULL 表示无探针指向）；删除节点时沿此失效探针 */
    CCFsProbeRef *probe_head;
    /* 常驻读写句柄：有探针指向且已读写时存在；结构操作 / 最后一个探针离开时 flush + fclose */
    void     *fp;             /* FILE*；NULL 表示未打开 */
    CCUINT64  fp_pos;         /* 句柄当前定位，顺序写时省去 fseek */
    CCBOOL    fp_writable;    /* 句柄是否以可写方式打开 */
} CCFsNode;
```

**红黑树键值：** 键为节点内 `name` 字符串指针，`compare_key` / `compare_node` 回调使用 `strcmp`。`strcmp` 按 `unsigned char` 逐字节比较、首个不同字节决定序，与「把字符串视作 8 位无符号数字逐个比大小」一致；UTF-8 文件名（字节 ≥ 0x80）也同样正确。

**探针回指：** 每个节点维护一条「指向它的探针」双向链表（`probe_head`），探针 `open` / `create_file` 指向节点时把自身回指头插进链表；离开（`destroy` / 改指向）时 O(1) 摘除。删除节点时沿该链表把每个探针的 `_Atomic CCBOOL alive` 置 `CCFALSE`、`cur` 置 `CC_FS_NIL`，多探针指向同一节点的场景也一个不漏。回指链表长度即指向数，取代了原来的 `refcount`。

**删除与 `copy_node`：** `cc_rbt_core` 删除双子节点时，会把后继节点的数据拷入被删节点槽位（`copy_node`）。因此除树结构字段（`parent`/`left`/`right`/`is_red`）外，`name`、`children_root`、`type`、`mount`、`fs_parent`、`size`、`ctime`、`mtime`、`crc32` 等数据字段必须一并拷贝，否则后继数据会错位；常驻读写句柄 `fp` / `fp_pos` / `fp_writable` 也必须按**所有权转移**拷贝（拷贝后源槽位置 `NULL`），否则 `FILE*` 会泄漏。`probe_head` 属运行时状态，**不参与 `copy_node` 拷贝**，删除时先把后继槽的探针回指链表迁到被拷入槽位（并更新各探针 `cur`），再失效被删节点自身的探针。其中 `children_root` 指向的子红黑树以「子树根下标」独立存在，子树内部 `parent` 不跨树指向外部节点，故被整体过继后不受影响。另：`swap-remove` 把末尾节点整体搬入空位（`*node_at(y) = *last`），句柄随结构一起搬走，无需按节点下标另存。

**`fs_parent`：** 本设计保留该字段（方便第三方直接读取资源包时向上导航）；但由于父目录节点可能被后继覆盖搬运导致 `fs_parent` 悬空，求绝对路径时一律采用「自根向下携带路径栈」，不依赖 `fs_parent`；`fs_parent` 仅在目录删除 / 显式挂载重建时尽力维护。

**根下标管理：** `cc_rbt_core` 的 `root` 为值传递，根变化通过 `ch_root` 通知；回调中把新根地址经 `idx_of` 转回下标，写入对应目录节点的 `children_root`（顶层红黑树的根写入全局树的根下标）。

### 6.3 挂载信息 `CCFsMount`、探针 `CCFiles` 与后端

**挂载信息**由每个节点持有；新建节点的 `real_path` 默认为 `NULL`（跟随上级，属内部默认态），显式 `mount` 必须给出非 NULL 的 `real_path` 才能建立锚点：

```C
typedef struct cc_fs_mount {
    char     *real_path;   /* 真实路径（一律用 `/`，禁止反斜杠）；NULL = 跟随上级（继承，仅内部默认态，mount 不接受 NULL） */
    CCUINT32  type;        /* CCFsMountType，见 1.4 */
    CCUINT8   origin;      /* 来源：内存创建 / 磁盘投影 / 包投影 */
    CCUINT64  pkg_off;     /* 包内文件：数据起始偏移 */
    CCUINT64  pkg_size;    /* 包内文件：数据字节数 */
} CCFsMount;
```

- 节点的物理对应 = 向上最近的显式锚点的 `real_path` + 相对路径拼接；未显式指定挂载的新建节点自动生成继承关系；
- 任意非包投影节点均可通过接口随时改写挂载信息（再次 mount 即覆盖并重建）。

`CCFiles` 是堆分配的探针，记录持有者的操作起点：

```C
typedef struct cc_files {
    _Atomic CCBOOL alive;  /* CCFALSE = 该探针已失效（其 cur 指向的节点被删除） */
    CCUINT32  cwd;         /* 当前工作目录节点下标（默认指向 `/`） */
    CCUINT32  cur;         /* 当前指向的文件节点下标；CC_FS_NIL 表示未指向文件 */
    CCUINT32  flags;       /* 内部状态标志 */
    CCFsProbeRef ref;      /* 自身回指，随 cur 挂入 / 摘除节点链表 */
} CCFiles;
```

- **探针指向即激活**：探针指向某文件节点时，把自身回指头插到该节点的 `probe_head` 链表，`alive` 置 `CCTRUE`。
- **多探针共用**：多个探针可指向同一节点；探针改变指向或销毁时从链表 O(1) 摘除自身回指。删除节点时沿链表逐个把探针 `alive` 置 `CCFALSE`、`cur` 置 `CC_FS_NIL`，此后该探针的 `read` / `write` 因 `cur == CC_FS_NIL` 而返回 `CC_FS_ERR_PARAM`。
- **指向即可操作**：指向后直接 `read` / `write`，按显式 `offset` 随机访问，**无状态，需每次指定值**。

所有后端对外暴露同一套读写语义（`read` / `write`），由节点内常驻的 `FILE*`（`CCFsNode.fp`）按挂载类型分派。权限由类型决定：

| 类型 | 来源 | 读 | 写 | 删 / 改名 / 重新 mount | flush |
|---|---|---|---|---|---|
| `DIR` / `FILE`（磁盘） | 挂载的真实文件 | ✅ | ✅（直写物理文件；系统权限错误如实透传） | ✅（物理删除 / 重命名即刻生效） | ✅ |
| `PKG` | 资源包内块 | ✅ | ❌（受保护只读投影） | ❌（全部失效） | ❌ |
| 新建虚拟文件 | 上级锚点下的真实文件 | ✅ | ✅ | ✅ | ✅ |

**包节点保护：** 包挂载后投影出的整套节点处于保护状态，**一切非读操作（写 / 删 / 改名 / 重新 mount）全部失效并返回 `CC_FS_ERR_PROTECT`**；仅 `unmount` 整个包时才随投影子树一并摘除。`flush` 对包节点为空操作（包内文件没有可写句柄）。

**存储模型（直写 + 常驻句柄）：**

- 文件内容全部保存在物理实体中，按 `offset` 定位、分块流式搬运，不把整文件载入内存；挂载的真实文件只登记元数据（`name` / `size` / `mtime` / `crc32` 等）；
- 每个文件节点持有 `fp`（`FILE*`）、`fp_pos`（当前定位）、`fp_writable`；**不变量：`node->fp != NULL ⇒ node->probe_head != NULL`**——有探针指向才保留句柄；
- 句柄在首次读写时按需打开（`"r+b"`，只读文件降级为 `"rb"`）；顺序读写只在 `fp_pos != offset` 时才 `fseek`；
- 句柄在以下时机统一 `fflush + fclose`：最后一个探针离开、节点被删除、改名 / 跨锚点移动、`refresh` / `unmount`、`ccfiles_flush`、`deinit_ccfiles`；`cp` / `pack` / CRC 等内部批量操作读取结束后，对没有探针指向的文件立即关闭；
- `create_dir` / `create_file`、`remove` / `remove_recursive`、`mv`（同锚点 `rename`）均**立即作用于物理层**；任何失败都返回真实错误码，不会出现「虚拟层成功、磁盘没变」；
- **CRC**：包内文件取块头记录，打包 / 解包时写入；`stat` 读取已保存的 CRC，**绝不重新扫描文件**；无 CRC 记录时返回 `0` 表示「无校验元数据」。

**探针状态机：**

| 状态 | `cur` | 说明 | 可执行操作 |
|---|---|---|---|
| 未指向文件 | `CC_FS_NIL` | 探针未定位到具体文件 | `chdir` / `create_file` / 检索 / 挂载等目录级操作 |
| 指向文件 | 文件节点下标 | 回指已挂入节点链表、`alive == CCTRUE` | `read` / `write`（按 `offset` 随机访问） |

- `open(v_path)` 负责让探针**指向**文件：解析到文件节点后置 `cur` 并把回指挂入节点链表；若此前已指向别的文件，先摘除旧回指再挂入新节点。
- `create_file(v_path)` 创建（或复用）虚拟文件后**自动指向**该节点，可立即 `write`。
- `read` / `write` 仅对「指向文件」状态有效；未指向文件时返回失败。

### 6.4 资源包格式（扁平块表）

包是只读、可随机访问的归档：头 + 块表 + 名字区 + 数据区，全部**固定小端**、无对齐填充。多字节字段由实现显式按小端序列化（`fs_le16/32/64_put/get`），**不直接映射结构体**，因此与宿主 CPU 端序无关。结构与常量定义见 `src/ccfiles_internal.h`（实现细节，非公开契约）。

```text
+--------------------------------------------------------+
| Header（32 字节）                                       |
+--------------------------------------------------------+
| Block[0 .. block_count)（每块 32 字节）                  |
+--------------------------------------------------------+
| 名字区（UTF-8 相对路径，无 NUL，按 name_len 定界）        |
+--------------------------------------------------------+
| 数据区（文件数据按块表顺序紧凑排列）                       |
+--------------------------------------------------------+
```

**Header（32 字节）**

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u8[4] | `magic` | 字节序列 `'_CPK'`（`5F 43 50 4B`），与端序无关 |
| 4 | u16 | `version` | 当前为 1 |
| 6 | u16 | `flags` | 预留 |
| 8 | u32 | `file_count` | 文件块数量 |
| 12 | u32 | `block_count` | 块总数（含目录） |
| 16 | u64 | `data_size` | 数据区总字节数 = 所有文件 `size` 之和 |
| 24 | u32 | `header_crc` | 头部 CRC |
| 28 | u32 | `reserved` | 保留，恒 0 |

**Block（32 字节）**

| 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|
| 0 | u32 | `name_len` | 名字字节数（必须 > 0） |
| 4 | u32 | `name_off` | 相对名字区起点的偏移 |
| 8 | u64 | `offset` | 文件数据绝对偏移；目录为 0 |
| 16 | u64 | `size` | 文件字节数；目录为 0 |
| 24 | u32 | `crc32` | 文件数据 CRC32（算法见 `crc.md`） |
| 28 | u32 | `flags` | bit0 = 1 表示目录 |

**规则与读取端校验：**

- 名字是**相对路径**（`/` 分隔，可含多级），挂载建树时逐段创建；禁止以 `/` 开头，禁止 `.` / `..` / 空段，且每段不得以空格或点结尾（避免 Windows 归一化后绕出暂存目录）；
- `header_crc` 覆盖除该字段自身外的 28 字节（**含 `reserved`**），读取端复算并比对；
- 读取端同时校验：`magic`、`version`、块表整体落在文件内、`data_size`、每块 `name_off + name_len` 落在名字区、文件块 `offset / size` 落在数据区、目录块 `offset / size` 为 0、`file_count` 与实际文件块数一致；任一不符返回 `CC_FS_ERR_PARAM`；
- 每文件 `crc32` 仅作元数据（经 `stat` 暴露，供调用方或外部工具校验），读取路径不强制校验。

---

## 7. 并发与线程安全

**本内核不内置锁。** 虚拟树为全局单例；探针记录各自的位置，彼此独立。是否加锁、在什么粒度加锁，由调用方按自身业务流程决定：

- 全局树的结构性操作（挂载 / 卸载 / 刷新 / 创建 / 删除 / 复制 / 移动）并发时需调用方加锁保护；
- 每个探针的位置（cwd、当前指向）仅由其持有者使用，不同探针可并行；
- 同一文件节点被删除时，沿探针回指链表统一失效所有指向它的探针（`_Atomic CCBOOL`）；多探针并发读写同一文件仍需调用方按文件粒度加锁；
- 同一探针上的并发访问需持有者自行加锁；
- 建议按子树或按业务流程划分锁范围，避免全局锁；
- 结构性操作与读写、回调内修改树、删除 / 移动 / 卸载时探针是否仍指向等合法性，均由调用方自行保证；结构性操作会使其涉及的正被指向节点失效（探针 `cur` 置 `CC_FS_NIL`）。

---

## 8. 测试与验收

- 全局树初始化后仅含 `/`，且 `/` 默认挂载到可执行程序同级 `res`；`ccfiles_create` 得到 cwd=`/`、未指向文件的空探针；
- 挂载信息：显式 `mount` 给出非 NULL 的 `real_path` 生成锚点（传 `NULL` 返回 `CC_FS_ERR_PARAM`）；新建节点默认 `real_path` 为 NULL、跟随上级，物理对应 = 最近锚点 + 相对路径；
- 挂载：目录挂载后递归生成的虚拟树与磁盘一致；
- 刷新：`refresh` 同步新增 / 删除；遇到嵌套显式锚点停止深入、原样保留（同 Linux 嵌套挂载不穿透）；包投影节点刷新不穿透；
- 检索：`find` 命中虚拟文件、包内文件与内存文件；通配符边界用例；`find` 递归整棵子树且 `*` 不跨目录；
- 只读包：对包内文件的写 / 删 / 改名 / 重新 mount 均返回 `CC_FS_ERR_PROTECT`；
- 落盘：写入后 `ccfiles_flush` 即磁盘内容正确；最后一个探针离开 / `destroy` 后无需任何显式落盘，磁盘已是最新；
- 物理联动：`mkdir` / `create_file` 后物理实体立即存在；`rm` / `rm -rf` 后磁盘文件与目录立即消失；`mv` 后目标磁盘内容正确且源无残留；探针句柄随红黑树 `copy_node` / `swap-remove` 搬迁不泄漏、不重复关闭；
- 存储模型：直写物理文件，挂载含大尺寸文件的目录不产生整文件内存峰值；1 字节 × N 次顺序写与磁盘内容逐字节一致；
- 路径：`..` 按 shell 语义回溯（含挂载锚点与根目录）；含反斜杠的 `real_path` 被拒绝或转换；长路径（系统 PATH_MAX 内，如 5 层 × 20 字节组件）在 mkdir / create / write / read / stat / list / find / getcwd / pack / 包挂载读回全链路均正确，且虚拟层本身不受固定长度或深度上限约束；
- 包格式：魔数字节序列 `5F 43 50 4B`（`'_CPK'`）、块表 / 名字区 / 数据区边界、`header_crc` / `data_size` / `file_count` 校验生效，损坏包（CRC 不符、截断、偏移越界、名字含 `..`）一律返回 `CC_FS_ERR_PARAM`；`crc32` 元数据一致；
- 读写：`read` 到末尾返回 `0`、越过末尾返回 `0`（EOF）；`write` 跳写自动扩展且中间补 `0`，写后 `size` 正确；空间不足返回 `CC_FS_ERR_NOSPACE`（负值）、写保护返回 `CC_FS_ERR_PROTECT`；
- 探针失效：多个探针指向同一文件时，删除该文件会沿回指链表让所有探针 `alive` 置 `CCFALSE`、`cur` 归 `CC_FS_NIL`；
- 指向即操作：`open` / `create_file` 后可直接 `read` / `write`；
- shell 操作：`create_file` / `create_dir` / `remove` / `cp` / `mv` 行为对齐 shell（`remove` 非空目录返回 `CC_FS_ERR_NOTEMPTY`；`cp` / `mv` 目标为已存在目录时放入其内部）；
- 跨锚点：`cp -r` / `mv` 递归穿透子树内显式挂载锚点、复制 / 移动其内容（挂载关系不跟随、不复制）；
- unmount 非锚点返回 `CC_FS_ERR_NOTMOUNT`；
- 元数据：`stat` 返回节点保存的 `size` / `ctime` / `mtime` / `crc32`，不重新扫描文件；
- 错误码：各失败场景返回对应 `CCFsResult`（如 `CC_FS_ERR_NOENT` / `CC_FS_ERR_NOTDIR` / `CC_FS_ERR_ISDIR`），可区分错误类型；
- unmount：卸载锚点后相关探针失效，后续 `read` / `write` 返回失败；
- refresh：纯内存虚拟文件在刷新后仍保留；
- CRC：包内文件 `stat` 返回的 `crc32` 与块头一致，且 `stat` 不重新扫描文件。
