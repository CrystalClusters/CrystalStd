# ccalloc 内存分配器

为 CrystalStd 提供统一的内存分配 API。核心特色为：内存泄露检测、内存块注释功能。

---

## 1. 产品形态

- **单一入口** `ccalloc(old, size, comment)` 同时覆盖**分配 / 调整（realloc）/ 释放**三种模式。
- **Meta（管理数据）与 User Memory（用户数据）完全分离**：越界写最多破坏注释或相邻用户数据，尽可能阻止普通用户碰到 RB-tree 与空闲链表。对于蓄意破坏的行为，不予以防御。
- **内置非法 free / double-free 检测与泄漏检测**：区别于标准 `malloc` 的核心能力。
- **可选内存块注释（comment）**：存储在用户区尾部的隐藏空间，不暴露给用户。
- **基础页保护**：Meta 与 FreeList 存储区在分配器空闲时不可访问；平台不支持时自动降级为无保护模式继续运行。
- **内置对齐**：按 `CC_MEM_ALIGN_MIN` 自动向上取整，返回指针对齐，用户无需关心。
- **线程安全**：alloc / free / realloc 由全局互斥锁串行化。
- **Meta 节点按栈序复用**：释放的节点回归空闲池，成对 alloc/free 不单调消耗 Meta 段。
- **性能等级**：接受 alloc/free 路径的 O(log N) 树操作成本，换取运行期更干净、可靠、可诊断的内存模型。分配器自身可支撑中等频度热路径，不与 glibc / jemalloc / mimalloc 竞争性能；更高频场景由上层对象池 / Arena / 动态数组解决。

### 1.1 设计

| 项 | 决策 |
|---|---|
| 内存后端 | 仅使用平台虚拟内存 API（`VirtualAlloc` / `mmap`）；不可用或失败即返回 NULL，**不提供 malloc 回退** |
| 红黑树 | 复用 `cc_rbt_core`（节点为 `void*`、键为 `CCUINTPTR`、颜色为 `CCBOOL`），分配器只提供 `CC_RbtOps` 回调 |
| 节点链接 | **直接指针链接**：`MetaNode` 与 `FreeListNode` 的链接均为绝对指针，不使用段内下标 |
| 公开 API | `ccalloc` / `get_comment`；初始化与清理由 `crystal_std.c` 的构造 / 析构函数自动完成 |
| 平台 | **仅 GCC 体系**（`__attribute__((constructor/destructor))`），不提供 MSVC 分支 |
| 线程安全 | 全局互斥锁串行化 |
| 页保护 | Meta / FreeList 段在空闲时不可访问；平台不支持时自动降级 |

---

## 2. API

### 2.1 函数签名

```C
/* 唯一分配入口：按 (old, size) 组合覆盖分配 / 调整 / 释放 */
void* ccalloc(void *old, CCUINT64 size, const char *comment);

/* 返回内存块注释首地址；无注释或指针非法时返回 NULL（查询不报错） */
const char* get_comment(void *ptr);
```

内部自动初始化接口（由 `src/crystal_std.c` 的构造 / 析构函数调用，声明于 `src/crystal_std_inner_header.h`，不对外暴露）：

```C
/* 幂等：已初始化时直接返回 */
void init_ccalloc(void);

/* 遍历 RB-tree 输出全部泄漏，释放全部段与互斥锁，返回泄漏块数量（0 = 干净）。
 * 之后允许重新 init_ccalloc。 */
CCUINT32 deinit_ccalloc(void);
```

### 2.2 三合一模式

| old | size | 模式 | 行为 |
|---|---|---|---|
| NULL | > 0 | 分配 | 新分配 `size` 字节，返回按 `CC_MEM_ALIGN_MIN` 对齐的用户指针；实际用户大小 = `round_up(size, CC_MEM_ALIGN_MIN)` |
| NULL | 0 | 空操作 | 返回 NULL，不算错误 |
| 非 NULL | 0 | 释放 | 释放该块，返回 NULL；地址非法则报错且不执行释放 |
| 非 NULL | > 0 | 调整 | 调整大小；原地收缩或搬家扩容，返回新指针；失败时旧块保持不变并返回 NULL |

### 2.3 注释（comment）

- `comment != NULL`：以最新注释为准，替换该块原有注释；
- `comment == NULL`：保留已有注释（分配模式下则为无注释）；
- 注释按 `strlen + 1`（含 NUL）存储在用户区尾部的**隐藏空间**（紧随实际用户区，即 `ptr + meta->size`），不返回给用户；长度记录在 `MetaNode.comment_size`；
- 释放模式下注释参数被忽略；
- 泄漏报告会打印注释内容；
- `get_comment(ptr)` 返回该块注释的**首地址**（隐藏空间内的 NUL 结尾字符串）；无注释或指针非法时返回 NULL（查询不报错）。注释位于不受保护的用户区，若被越界写坏由读取方自行承担。

### 2.4 调整（realloc）语义

设对齐量 `A = CC_MEM_ALIGN_MIN`。

- 实际用户大小 `asize = round_up(请求 size, A)`；
- 槽位大小 `slot = round_up(asize + comment_size, A)`（空闲区 / 区域大小的记账单位）；
- 旧块：`old_asize`、`old_comment_size`、`old_slot`；
- 新注释 `new_comment_size = comment ? strlen(comment) + 1 : old_comment_size`；
- `new_asize = round_up(size, A)`，`new_slot = round_up(new_asize + new_comment_size, A)`。

规则：

1. **原地调整**（`new_asize <= old_asize` 且 `new_slot <= old_slot`）：指针不变；更新 `meta->size = new_asize`；若提供了新注释则写入 `ptr + new_asize`，否则把旧注释 `memmove` 到 `ptr + new_asize`；若 `new_slot < old_slot`，把尾部 `[ptr + new_slot, ptr + old_slot)` 作为空闲区域归还 FreeList（取一个描述符，插入所属用户段链表并做相邻合并）。
2. **搬家扩容**（其余情况）：先分配新块（槽位 `new_slot`），`memmove` 拷贝 `min(old_asize, new_asize)` 字节用户数据与注释，再释放旧块。
3. 新块分配失败：旧块保持不变，返回 NULL（与标准 `realloc` 失败语义一致）。
4. `ccalloc(ptr, 0, ...)` 走释放分支，不分配。

### 2.5 初始化 / 清理 / 错误

- `init_ccalloc()`：建立分配器状态、初始 User 段、Meta 段与 FreeList 段、初始化互斥锁、检测页保护能力。**幂等**：已初始化时直接返回；初始段申请失败则回滚并保持未初始化状态。
- `deinit_ccalloc()`：遍历 RB-tree 输出全部泄漏（地址 / 大小 / 注释），释放全部 User 段、Meta 段与 FreeList 段，清理互斥锁与状态，**返回泄漏块数量**（0 = 干净）；之后允许重新 `init_ccalloc`。
- 未初始化时调用 `ccalloc`：用 `color_print` 输出 `[ccalloc] ...` 错误并返回 NULL。
- 所有控制台文本信息（错误、泄漏报告等）统一使用 `color_print` 输出，带 `[ccalloc]` 前缀。

### 2.6 示例

```C
void *buf = ccalloc(NULL, 4096, "font atlas");   // 分配 + 注释
buf = ccalloc(buf, 8192, "font atlas v2");        // 扩容 + 更新注释
buf = ccalloc(buf, 2048, NULL);                   // 收缩，保留注释
ccalloc(buf, 0, NULL);                            // 释放

/* 进程退出时由析构函数自动 deinit_ccalloc()，输出残留泄漏并返回数量 */
```

---

## 3. 核心架构

```
                        ccalloc
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   Meta Storage      FreeList Storage    User Storage
   （受页保护）        （受页保护）         （不受页保护）
        │                  │                  │
   MetaNode 数组段    FreeListNode 数组段    用户段链表
   （节点池 + RB-tree）（槽位池 + 空闲链表）  [User][Comment]
        │                  │
     RB-tree          每用户段一条地址有序
 （键 = 用户指针）      空闲链表
```

- **User Memory 内不放任何 allocator 结构**：用户指针 = 纯用户数据起始地址。
- **RB-tree 是 allocation 状态的权威来源**：`user_ptr → MetaNode`；alloc 插入、free 删除、deinit 遍历。
- **FreeList 是用户区空闲空间的权威来源**：空闲区域由受保护描述符描述，不依赖用户内存内容。
- **MetaNode 二态**：每个 MetaNode 在任一时刻要么在 RB-tree 中（活跃块记账），要么在空闲节点池中（可复用）；两态以「树内删除 → 入池、出池 → 树内插入」为唯一转移点，无第三种归属。

### 3.1 寻址方式

本实现采用**直接指针链接**：`MetaNode` 的 `left / right / parent` 与 `FreeListNode` 的 `prev / next` 均为绝对指针。

原因：`cc_rbt_core` 的节点参数与返回值统一为 `void *`，不携带段上下文。若采用「段内下标 + 跨段跳转」，`get_node / set_node` 在解析同段链接时需要当前节点所在段的基址，而该信息无法从单个 `void *` 节点无损推导（掩码推导要求段基址按段大小对齐，Linux `mmap` 只保证页对齐）。改用直接指针后：

- 与 `cc_rbt_core` 的 `void *` 节点语义天然契合，无需改造内核或增加辅助字段；
- 节点仍可控制在一到两条缓存行内（见 §4.5），不会显著降低局部性；
- 逻辑大幅简化，减少跨段标志位与段指针的维护成本。

代价是链接字段由 32 位下标变为 64 位指针；在本项目的目标平台上可接受。

---

## 4. 数据结构

内部结构统一定义在 `src/ccalloc_internal.h`，仅供 `src/ccalloc.c` 与 `tests/` 自测使用，不对外公开。

### 4.1 MetaNode（RB-tree 节点 + 空闲池节点）

```C
typedef struct MetaNode {
    void             *ptr;           /* 用户指针（RB-tree 键） */
    CCUINT64          size;          /* 实际用户字节数 = round_up(请求, 对齐) */
    CCUINT64          comment_size;  /* 注释字节数（含 NUL），0 = 无注释 */
    struct MetaNode  *left;          /* 左子 */
    struct MetaNode  *right;         /* 右子；空闲池中复用为 pool_next */
    struct MetaNode  *parent;        /* 父   */
    struct CC_UserSegmentHeader *seg;/* 所属用户段（O(1) 归还，免扫描段链表） */
    CCUINT8           flags;         /* bit0: RB 颜色(1=红)；bit1: 空闲池标记 */
    CCUINT8           _pad[7];
} MetaNode;   /* 64 字节 = 一条缓存行 */
```

- `MetaNode` 是 RB-tree 节点与空闲池节点的共用载体：
  - 在树内时，`left / right / parent` 为树链接；
  - 空闲时，`right` 复用为「池中下一个空闲节点」链，`left / parent` 恒为 `NULL`；
  - `seg` 记录所属用户段，释放时直接取用，避免扫描用户段链表。
- RB-tree 键比较按 `(CCUINTPTR)ptr`。
- 空闲节点标记 `CC_META_FLAG_FREE`（`flags.bit1`）供自检遍历区分在池 / 在树。

```C
#define CC_META_FLAG_RED    (1u << 0)
#define CC_META_FLAG_FREE   (1u << 1)
```

> `struct CC_UserSegmentHeader` 需在 `MetaNode` 之前前置声明。

### 4.2 FreeListNode（空闲区域描述符 + 槽位池节点）

```C
typedef struct FreeListNode {
    void                  *start;  /* 空闲区域在用户段内的起始地址 */
    CCUINT64               size;   /* 空闲区域大小（对齐量倍数） */
    struct FreeListNode   *prev;   /* 链表前驱（槽位池中不使用） */
    struct FreeListNode   *next;   /* 链表后继；槽位池中复用为 pool_next */
    CCUINT8                flags;  /* bit0: 槽位空闲(可复用) */
    CCUINT8                _pad[15];
} FreeListNode;   /* 48 字节 */
```

- 描述符槽位可复用：段创建时把描述符区全部槽位预格式化并链入全局池；槽位空 / 忙由 `flags.bit0` 区分。
- 取描述符弹池头、放回推池头（栈序复用）；池空则新建 FreeList 段并全量格式化入池。
- 每个用户段对应一条按地址有序的空闲描述符双向链表，链表头存在用户段头的 `free_head`。

```C
#define CC_FREE_FLAG_SLOT_FREE  (1u << 0)
```

### 4.3 段头与锚块

**统一段头**（Meta / FreeList 段共用，16 字节，嵌在 `base[0]` 元素槽内）：

```C
typedef struct CC_SegHeader {
    void *next_seg;   /* 下一个同体系段（段基址） */
    void *raw_base;   /* 后端原始指针（释放用） */
} CC_SegHeader;       /* 16 字节 */
```

**Meta 锚块**（仅首个 Meta 段，占 `base[1]` 槽）：

```C
typedef struct CC_MetaAnchor {
    MetaNode *root;      /* RB-tree 根节点 */
    MetaNode *pool_head; /* Meta 空闲链头节点 */
    CCUINT8   _pad[48];
} CC_MetaAnchor;         /* 64 字节 */
```

**FreeList 锚块**（仅首个 FreeList 段，占 `base[1]` 槽）：

```C
typedef struct CC_FreeAnchor {
    FreeListNode *pool_head; /* FreeList 空闲链头节点 */
    CCUINT8       _pad[40];
} CC_FreeAnchor;             /* 48 字节 */
```

**用户段头**（用户段首，固定 64 字节 = 一个对齐单位）：

```C
typedef struct CC_UserSegmentHeader {
    struct CC_UserSegmentHeader *next_seg;   /* 下一个用户段 */
    FreeListNode                *free_head;  /* 本段空闲链表头 */
    CCUINT64                     total;      /* 段总大小 */
    CCUINT64                     usable;     /* 可用大小 = total - 64 */
    void                        *raw_base;   /* 后端原始指针 */
    CCUINT8                      _pad[24];
} CC_UserSegmentHeader;                      /* 64 字节 */
```

### 4.4 段布局

```
首个 Meta 段：  [统一段头][Meta 锚块][MetaNode 节点区]
其余 Meta 段：  [统一段头][MetaNode 节点区]

首个 FreeList 段：[统一段头][FreeList 锚块][FreeListNode 描述符区]
其余 FreeList 段：[统一段头][FreeListNode 描述符区]

用户段：        [CC_UserSegmentHeader 64B][用户可用区 ...]
```

- 锚块不参与任何池 / 树，不对应用户分配；只记录全局唯一的节点引用。
- 段创建时即全量预格式化：本节点的全部元素槽依次入池（首段跳过段头与锚块）。
- 段以段头串成链表，`meta_head / meta_cur`、`flist_head / flist_cur`、`user_head / user_tail` 由全局状态维护。

### 4.5 尺寸与静态断言

| 结构 | 尺寸 | 说明 |
|---|---|---|
| `MetaNode` | 64 B | 一条缓存行 |
| `FreeListNode` | 48 B | |
| `CC_SegHeader` | 16 B | |
| `CC_MetaAnchor` | 64 B | |
| `CC_FreeAnchor` | 48 B | |
| `CC_UserSegmentHeader` | 64 B | 一个对齐单位 |

```C
typedef char CC_STATIC_ASSERT_MetaNode_64[(sizeof(MetaNode) == 64) ? 1 : -1];
typedef char CC_STATIC_ASSERT_FreeListNode_48[(sizeof(FreeListNode) == 48) ? 1 : -1];
typedef char CC_STATIC_ASSERT_SegHeader_16[(sizeof(CC_SegHeader) == 16) ? 1 : -1];
typedef char CC_STATIC_ASSERT_MetaAnchor_64[(sizeof(CC_MetaAnchor) == 64) ? 1 : -1];
typedef char CC_STATIC_ASSERT_FreeAnchor_48[(sizeof(CC_FreeAnchor) == 48) ? 1 : -1];
typedef char CC_STATIC_ASSERT_UserSegHeader_64[(sizeof(CC_UserSegmentHeader) == 64) ? 1 : -1];
```

---

## 5. Meta 节点池与 RB-tree

### 5.1 节点池

- Meta 段创建时，将段内全部 `MetaNode` 槽位（首段跳过 `base[0]` 段头与 `base[1]` 锚块）依次推入全局空闲池。
- 池为单向栈链，池头记录在首段锚块 `CC_MetaAnchor.pool_head`。
- `meta_pool_pop()`：池空时先 `meta_seg_new()` 扩容；弹出池头、读 `right` 推进池头、整节点清零后交给插入逻辑。
- `meta_pool_push(node)`：整节点清零、把 `right` 设为当前池头、置 `CC_META_FLAG_FREE`、更新池头。
- 扩容失败或后续步骤失败时，预留节点必须推回池头（回滚）。

### 5.2 入池 / 出池清零规则

节点在树内与池内复用同一组链接字段，必须严格清理，避免陈旧链接被树操作误读：

- **入池**：`memset(node, 0, sizeof(MetaNode))` → 写 `right = pool_head` → 置 `CC_META_FLAG_FREE` → 更新池头。
- **出池**：先读 `right` 推进池头 → `memset(node, 0, sizeof(MetaNode))` → 交给插入逻辑。
- 空闲节点无颜色语义（清零后 RED = 0），出池后由插入统一染红，fixup 收尾把根染黑。
- 不变式（自测项）：
  1. 池内节点数 + 树内节点数 = 各 Meta 段可用槽数之和（不计段头与锚块）；
  2. 池链上节点 `FREE` 位一致为 1，树内节点一致为 0；
  3. 空闲节点的 `left / parent` 恒为 `NULL`，`right` 指向下一空闲节点；
  4. 红黑树性质（BST 序 / 黑高 / 无红红）持续合法；
  5. 纯成对 alloc/free 压力后 Meta 段数不增长。

### 5.3 `cc_rbt_core` 适配

本实现不为红黑树另写算法，全部树形态逻辑复用 `src/rbt_core.c`。`ccalloc.c` 只提供 `CC_RbtOps` 回调：

| 回调 | 实现要点 |
|---|---|
| `cmpare_key(user, node, key)` | 比较 `(CCUINTPTR)((MetaNode*)node)->ptr` 与 `key`，返回负 / 零 / 正 |
| `cmpare_node(user, n1, n2)` | 比较两节点的 `ptr`（用于插入定位） |
| `copy_node(user, dst, src)` | **只拷贝业务字段** `ptr / size / comment_size / seg`，不动链接与颜色 |
| `find_instead(user, node)` | 返回中序后继：右子树最左节点（删除双子树节点时使用） |
| `get_node(user, node, rel)` | 返回 `left / right / parent`（节点为空返回 `NULL`） |
| `set_node(user, n1, n2, rel)` | **只修改 `n1` 的对应链接**为 `n2`（内核负责双向挂接） |
| `get_color(user, node)` | `flags & CC_META_FLAG_RED` |
| `set_color(user, node, is_red)` | 置 / 清 `CC_META_FLAG_RED` |
| `left_rotate(user, node)` | 仅做链接重排（标准左旋）；**不自行更新根** |
| `right_rotate(user, node)` | 仅做链接重排（标准右旋）；**不自行更新根** |
| `ch_root(user, new_root)` | 把 `new_root` 写入 Meta 锚块 `root` |

关键约定：

- 内核以**值传递**接收 `root`，根变化时通过 `ch_root` 通知。因此每次调用 `cc_rbt_core_insert / remove / get / inorder_tranverse` 时，`root` 参数都从锚块读取（`meta_anchor()->root`），并由 `ch_root` 保持锚块同步。
- `cc_rbt_core_remove` 在双子树场景下会先 `copy_node(z, successor)`（把后继业务数据拷入 `z`），再物理摘除后继并**返回该后继节点**。因此释放流程必须：
  1. 在调用 `remove` **之前**捕获待释放块的 `ptr / size / comment_size`；
  2. 调用 `remove` 并接收返回的「被物理摘除节点」；
  3. 把**返回的节点**（而非原检索节点）推回空闲池。
- `left_rotate / right_rotate` 不承担根更新：内核在旋转前后自行判断是否为根，并通过 `ch_root` 通知。

**根操作封装**：

```C
static MetaNode *meta_root(void)              { return meta_anchor()->root; }
static void      meta_root_set(void *root)    { meta_anchor()->root = (MetaNode *)root; }
```

---

## 6. FreeList 槽位池与空闲链表

### 6.1 槽位池

- FreeList 段创建时，把描述符区全部槽位（首段跳过段头与锚块）预格式化并入全局池。
- 池为单向栈链，池头记录在首段锚块 `CC_FreeAnchor.pool_head`；复用 `next` 字段，`CC_FREE_FLAG_SLOT_FREE` 标记槽位空闲。
- `freeblock_alloc()`：池空则 `flist_seg_new()` 扩容；弹出池头、读 `next` 推进池头、清零后返回。
- `freeblock_release(desc)`：清零、`next = pool_head`、置 `SLOT_FREE`、更新池头。

### 6.2 空闲链表与合并不变式

- 每个用户段维护一条按地址有序的空闲描述符双向链表，头指针存在 `CC_UserSegmentHeader.free_head`。
- **合并不变式（禁止碎片化）**：任意时刻，一段连续空闲内存**至多对应一个 `FreeListNode`**。所有产生新空闲区域的操作（释放、原地收缩归还尾部、分配拆分）之后都必须检查并合并相邻空闲区域。
- 插入 `free_list_insert(seg, ptr, size)`：
  1. 按地址找到插入位置（`prev` 为 `start < ptr` 的最后一个，`cur` 为 `start >= ptr` 的第一个）；
  2. 与前驱相邻则合并到前驱，并顺带检查与后继合并；
  3. 否则与后继相邻则向后扩展后继；
  4. 均不相邻则取新描述符插入。
- 移除 `free_list_remove(seg, desc)`：摘链后把描述符归还槽位池。
- 分配时在**跨用户段**范围内做 first-fit：每段按地址顺序找首个 `size >= 槽位` 的区域；找不到则新建用户段。

---

## 7. 页保护

- 每次 `ccalloc` 进入临界区后，先对所有 **Meta 段与 FreeList 段**解除保护（RW），完成操作后再全部恢复保护（不可访问），最后释放锁。
- Windows：`VirtualProtect`（`PAGE_NOACCESS` / `PAGE_READWRITE`）；POSIX：`mprotect`（`PROT_NONE` / `PROT_READ | PROT_WRITE`）。
- **降级处理**：保护或解除保护任一失败 → 用 `color_print` 打印一次错误，将 `protection_enabled` 置 FALSE，分配器以降级（无保护）模式继续运行，保证功能性不受影响。
- 段链表经由各段 `base[0]` 段头串联（受保护区内）：
  - 解除保护：`解除当前段 → 读 next_seg → 推进`；
  - 恢复保护：`先读 next_seg → 再保护当前段`。
- 分配器全局状态只保留 `meta_head / flist_head / user_head` 等少数不受保护指针；树根与各池头引用存放在首段锚块（受保护区内），保护循环从这些入口出发。
- `deinit_ccalloc` 先解除全部保护再释放各段。
- 保护粒度是整段（不做单块 guard page）。

进入 / 离开临界区：

```C
static void ccalloc_enter(void);   /* 解除全部 Meta / FreeList 段保护（失败则降级） */
static void ccalloc_leave(void);   /* 恢复全部保护（失败则降级） */
```

---

## 8. 平台后端

```
Allocator Core（RB-tree / Meta / FreeList / Segment / 泄漏检测 / 注释 / 扩容）
        ↓
Platform Memory Backend（区域申请、释放、权限）
```

| 平台 | 申请 / 释放 | 权限 |
|---|---|---|
| Windows（`CC_WINDOWS`） | `VirtualAlloc` / `VirtualFree` | `VirtualProtect` |
| POSIX（`CC_LINUX` / `CC_ANDROID`） | `mmap` / `munmap` | `mprotect` |

- **不提供回退**：平台 API 不可用或申请失败即返回 NULL，由调用方处理失败；不提供任何回退路径。
- 后端函数集中在 `src/ccalloc_utilities.c`：

```C
void*   alloc_segment(CCUINT64 size);                 /* 申请段，失败返回 NULL */
void    free_segment(void *ptr, CCUINT64 size);       /* 释放段 */
CCBOOL  protect_segment(void *ptr, CCUINT64 size);    /* 置为不可访问 */
CCBOOL  unprotect_segment(void *ptr, CCUINT64 size);  /* 置为可读写 */
```

- 段内存天然页对齐；用户段头恰好占一个对齐单位（64 B）。

---

## 9. 分配 / 释放 / 调整流程

### 9.1 顶层分发

```C
void *ccalloc(void *old, CCUINT64 size, const char *comment)
{
    /* (NULL, 0) 空操作 */
    if (!old && size == 0) return NULL;

    /* 未初始化：报错返回 NULL */
    if (!g_cc.initialized) { ...错误输出...; return NULL; }

    lock(); ccalloc_enter();

    if (!old)               result = alloc_internal(size, comment);
    else if (size == 0)     free_by_ptr(old);
    else                    result = realloc_internal(old, size, comment);

    ccalloc_leave(); unlock();
    return result;
}
```

### 9.2 分配（`old == NULL, size > 0`）

1. 计算 `comment_size = comment ? strlen(comment) + 1 : 0`；`asize = round_up(size, A)`；`slot = round_up(asize + comment_size, A)`；全程检查 `CCUINT64` 溢出。
2. 从 Meta 池弹出一个 `MetaNode`。
3. **跨用户段 first-fit**：遍历用户段，在每段空闲链表中找首个 `size >= slot` 的区域；找不到则 `user_seg_new(slot)` 新建段后重试。
4. **拆分空闲区**：若剩余 `>= CC_MEM_MIN_SPLIT`，把描述符 `start` 前移 `slot`、`size` 减去 `slot`；否则整块分配并移除描述符。
5. 填充 `MetaNode`：`ptr = start`、`size = asize`、`comment_size = csize`、`seg = 所属用户段`；若带注释，`memcpy(ptr + asize, comment, csize)`。
6. 插入 RB-tree：`cc_rbt_core_insert(&g_meta_ops, meta_root(), node)`：
   - 成功返回 `0`；
   - 键冲突（理论不应发生）→ 把用户区归还空闲链、Meta 节点推回池、报错返回 NULL。
7. 返回 `ptr`。

### 9.3 释放（`old != NULL, size == 0`）

1. `cc_rbt_core_get(&g_meta_ops, meta_root(), (CCUINTPTR)old)` 检索；未命中 → 报错（非法地址或 double-free），不执行释放。
2. **先捕获** `ptr / asize / comment_size`，再计算原槽位 `slot = round_up(asize + comment_size, A)`。
3. 取捕获的 `meta->seg` 作为所属用户段（O(1)，无需扫描段链表）。
4. `removed = cc_rbt_core_remove(&g_meta_ops, meta_root(), (CCUINTPTR)ptr)`；把 `removed` 推回 Meta 池。
5. `free_list_insert(seg, ptr, slot)` 归还用户区并做相邻合并。
6. 若开启 `CC_MEM_TRIM_SEGMENTS`，扫描并回收 100% 空闲的用户段（始终保留至少 1 段）。

### 9.4 调整（`old != NULL, size > 0`）

按 §2.4 语义执行：原地收缩优先；否则「先分配新块（成功才释放旧块）」搬家。旧块信息在 `remove` 前捕获，避免 `copy_node` 覆盖。

### 9.5 用户段回收（`CC_MEM_TRIM_SEGMENTS`）

- 段 100% 空闲的判定：空闲链恰为单个描述符，且 `start == seg + 64`、`size == seg->usable`。
- 释放后扫描全部用户段，回收 100% 空闲段；始终保留至少 1 段。
- 回收时把该段唯一描述符还回槽位池，再 `free_segment(seg->raw_base, seg->total)`。

---

## 10. 释放合法性

- 只有 allocator 返回过的 **allocation 起始地址** 是合法释放键：`free(p + 1)`、`free(p + 50)`、随机指针、栈指针均非法。
- RB-tree 找不到 → 输出明确错误、**不执行释放**、返回 NULL；MetaNode的记录为ccalloc内存块合法性的唯一凭据。
- 天然支持 double-free 检测：第二次释放时 Meta 已不存在 → 报错。

---

## 11. 泄漏检测

`deinit_ccalloc()` 顺序：

1. 加锁并解除全部页保护；
2. 中序遍历 RB-tree，逐块用 `color_print` 输出
   `[ccalloc] leak: ptr=%p size=%lu comment="%s"`（`size` 以 `CCUINT64` 经 `%lu` 打印；无注释则标 `comment=(无)`），并累加计数；
3. 释放全部 User 段、Meta 段与 FreeList 段；
4. 清理互斥锁与状态（`initialized = FALSE`、`protection_enabled = FALSE`）；
5. 返回泄漏块数量（0 = 干净）；
6. 之后允许重新 `init_ccalloc()`。

> `color_print` 不重入分配器，持锁输出安全。

---

## 12. 默认参数

可在编译前通过宏覆盖（定义在 `include/ccalloc.h`）：

| 参数 | 默认值 | 宏 |
|---|---|---|
| 内置对齐量 | 64 字节 | `CC_MEM_ALIGN_MIN` |
| User 段大小 | 1 MiB | `CC_MEM_USER_SEGMENT_SIZE` |
| Meta 段大小 | 64 KiB | `CC_MEM_META_SEGMENT_SIZE` |
| FreeList 段大小 | 64 KiB | `CC_MEM_FREELIST_SEGMENT_SIZE` |
| 最小拆分 | 32 字节 | `CC_MEM_MIN_SPLIT` |
| 自动回收空用户段 | 开 | `CC_MEM_TRIM_SEGMENTS` |

容量估算：

- Meta 64 KiB 段 ≈ 1024 个 `MetaNode`（首段因锚块少 2 个）；
- FreeList 64 KiB 段 ≈ 1365 个 `FreeListNode`（首段因锚块少 1 个）；
- 格式化成本 O(capacity)，仅在池空且需要扩容时发生。

---

## 13. 测试与验收

### 13.1 测试接口

标准（release）构建不导出任何调试统计、遍历、校验符号。`src/ccalloc_internal.h` 只保留内部结构定义，供测试方编译期持有。唯一测试入口：

```C
typedef struct CC_MemTestCtx {
    void *meta_head;   /* 首个 Meta 段段头 */
    void *flist_head;  /* 首个 FreeList 段段头 */
    void *user_head;   /* 用户段链表头 */
} CC_MemTestCtx;

typedef CCBOOL (*CC_MemTestFn)(const CC_MemTestCtx *ctx);

/* 持锁并解除保护后调用 fn；返回 fn 的结果 */
CCBOOL ccalloc_test_check(CC_MemTestFn fn);
```

测试方在回调中按自己持有的内部结构定义直接遍历段 / 池 / 树，自行计数与校验，返回 TRUE 表示通过。

### 13.2 功能自测（`tests/ccalloc.c`）

分节覆盖：

1. 基础分配 / 释放 / `(NULL, 0)` 空操作；
2. 对齐与向上取整（1、63、64、65、70、4096 等边界）；
3. 注释：分配 / 更新 / 保留 / 查询 / 泄漏输出；
4. RB-tree 跨段：强制分配 ≥ 2600 块以触发 ≥ 3 个 Meta 段，再伪随机顺序删除；
5. 空闲链表 / 槽位池：合并不变式、描述符复用；
6. 用户段自动扩容与大块分配（> 1 MiB）；
7. 整段回收（开 / 关 `CC_MEM_TRIM_SEGMENTS`）；
8. 原地收缩 / 原地扩容 / 搬家扩容；
9. 非法释放 / double-free：应被拒绝且不破坏分配器；
10. 页保护冒烟测试（若平台支持）与降级路径；
11. 泄漏检测：故意泄漏若干块，`deinit_ccalloc()` 应输出并返回正确数量。

每个子测试独立 `deinit_ccalloc()` / `init_ccalloc()`，互不影响。

### 13.3 随机压力测试

- 容量随机：请求大小在 [1, max_block] 间随机，覆盖 0、对齐边界、随机注释长度；允许大块分配（默认最大 16 MiB）；
- 动作随机：分配 / 释放 / realloc 按随机概率选择；
- 顺序随机：由固定种子驱动，可复现失败现场；
- 内存占用有界：配置最大占用量（默认 1 GiB）与最大活跃块数，达到上限后只允许释放；
- 规模：默认 1e6 次操作（快速回归可用宏缩小）；
- 校验点：红黑树性质合法、全部释放后泄漏数 = 0、相邻空闲合并不变式成立、池守恒、Meta / FreeList 段数不随总操作数增长、非法释放检测仍生效。

### 13.4 测试入口集成

- `tests/test_header.h` 增加 `void test_ccalloc(void);` 与 `CCINT32 test_ccalloc_failures(void);`；
- `tests/test.c` 增加测试组编号 `3`，菜单项「3：ccalloc 专项」；
- `tests/ccalloc.c` 实现用例与失败计数。

---

## 14. 内部接口

```C
/* src/ccalloc_internal.h */
CCBOOL ccalloc_test_check(CC_MemTestFn fn);

/* src/crystal_std_inner_header.h */
void     init_ccalloc(void);
CCUINT32 deinit_ccalloc(void);

void*    alloc_segment(CCUINT64 size);
void     free_segment(void *ptr, CCUINT64 size);
CCBOOL   protect_segment(void *ptr, CCUINT64 size);
CCBOOL   unprotect_segment(void *ptr, CCUINT64 size);
```
