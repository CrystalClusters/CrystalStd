# ccthread

并行内核用于管理水晶簇项目内所有的并行行为、异步同步行为。

## 1.接口列表

### 1.1 异步锁

```C
// 创建锁
CCLock cc_create_lock();
// 销毁锁
CCINT32 cc_destroy_lock(CCLock lock);
// 加锁
void cc_lock(CCLock lock);
// 解锁
void cc_unlock(CCLock lock);
```

**参数含义：**
- `lock`：异步锁

**返回值：**

- **cc_create_lock：**
成功返回锁，失败返回`NULL`。
- **cc_destroy_lock：**
成功返回0，无效的锁返回-1。
- **cc_lock：**
无
- **cc_unlock：**
无

**注意事项：**
- cc_lock 会阻塞线程，直到锁处于解锁状态。

### 1.2 线程

```C
// 创建线程；data 为用户数据，会原样传给入口函数
CCThread cc_thread_create(CCThreadEntry entry, void *data);

// 查询线程
CCINT32 cc_thread_status(CCThread thr);

// 等待线程；timeOut_us 单位为微秒，0 表示无限等待
void cc_thread_wait(CCThread thr, CCUINT32 timeOut_us);
```

**参数含义：**
- `entry`：线程入口，本质为函数指针，签名见 `CCThreadEntry`。
- `data`：用户数据指针，创建时保存、入口执行时原样回传；可为 `NULL`。
- `thr`：线程ID（`cc_thread_create` 的返回值）。
- `timeOut_us`：最长等待时间，单位**微秒**（与 `ccsleep` 一致）；`0` 表示无限等待。

**返回值：**
- **cc_thread_create：**
  创建线程失败返回0，正常返回内部线程ID。
- **cc_thread_status：**
  0：线程不存在/已退出；1：线程存活。
- **cc_thread_wait：**
  无

**注意事项：**
- `cc_thread_wait` 在线程ID不存在时会立即返回，不会阻塞。
- `timeOut_us` 非 0 时，超过指定时长即返回，此时线程可能仍在运行；返回后应通过 `cc_thread_status` 判断是「已结束」还是「仅超时」。
- 并行模块内部维护一个线程树，以线程ID为键：创建线程成功时由创建函数挂上线程树然后返回；线程结束后由线程自身在退出前将其从树中摘除。
- 线程ID 取平台原生线程标识，理论上可能被操作系统复用（旧线程已结束、ID 又被新线程占用）。本模块保证「同时存活的线程 ID 互不相同」，但不保证「历史上出现过的 ID 永不重复」。

## 2.结构体/数据类型定义

```C
#ifdef CC_WINDOWS
#include <Windows.h>
#else
#include <unistd.h>
#endif

typedef void* CCLock;

// 线程ID：与指针等宽，可容纳 pthread_t / HANDLE / Win32 线程ID，
// 避免不同平台原生类型位宽不一致导致截断或告警
typedef CCUINTPTR CCThread;
typedef void(*CCThreadEntry)(void* data, CCThread idThis);

// 对外以 void* 暴露，内部实体为 CCLockInner
typedef struct cclock{
    #ifdef CC_WINDOWS
    CRITICAL_SECTION lock;
    #else
    pthread_mutex_t lock;
    #endif
} CCLockInner;

// 线程实体参数：堆上分配，生命周期与线程一致
typedef struct cc_thread_arg {
    CCThreadEntry entry;
    void *userData;
    CCUINTPTR key;
    #ifdef CC_WINDOWS
    HANDLE thread;
    #else
    pthread_t thread;
    #endif
} CCThreadArg;

// 线程树节点：纯红黑树结构，连续存放于 CCDyn
typedef struct cc_thread {
    CCUINTPTR key;
    CCUINT32 parent;   // 父节点下标，CC_THREAD_NIL 表示无
    CCUINT32 left;     // 左子节点下标
    CCUINT32 right;    // 右子节点下标
    CCBOOL is_red;
} CCThreadNode;
```

## 3.关键设计说明

本节解释实现中若干「看起来可以更简单」的选择背后的原因，便于后来者理解与维护。

### 3.1 线程创建后立即 detach

**做法：** `pthread_create` 成功后立刻 `pthread_detach`（Windows 下等价地关闭线程句柄）。

**原因：** joinable 线程结束后若无人 `pthread_join`，其内核资源（线程控制块、栈等）不会释放，会成为「僵尸线程」，长期运行的程序会持续泄漏。本项目没有固定的 join 点，线程可能发起后无人等待。

**解决的问题：** 线程结束即释放系统资源；等待语义由库自己用线程树实现，无论调用方是否等待都能安全回收，不会泄漏。

### 3.2 线程树使用「连续数组 + 下标互指」

**做法：** 节点存放在 `CCDyn` 连续内存中，节点之间的 `parent/left/right` 全部存数组下标（`CCUINT32`），空为 `CC_THREAD_NIL = 0xFFFFFFFF`；全局只保存根节点下标 `root`。

**原因：**
1. 连续存储让遍历/查找有更好的缓存局部性；
2. 但动态数组扩容、收缩会整体搬移内存，任何保存下来的节点指针都会失效；
3. 若节点间用指针互指，扩容后整棵树的指针都要重定位，代价高且易错。

**解决的问题：** 扩容/缩容只改变数组基址，节点之间的相对关系（下标）保持不变，树天然无需修复；`root` 存下标同理，不会因搬移失效。

### 3.3 为什么要 CCThreadArg 包装层，且创建全程持锁

**做法：** 用户入口不直接作为系统线程入口，而是包一层 `_inner_thread_`，其参数是堆上的 `CCThreadArg`。`cc_thread_create` 在**持锁**状态下依次完成：分配 `CCThreadArg` → 追加树节点 → 创建线程 → 取得线程 ID 回填 `key` → 插入红黑树 → detach → 解锁。

**原因：**
1. 线程节点在数组里，地址会变，不能把节点指针交给 `pthread_create`；`CCThreadArg` 是独立堆对象，地址稳定，且生命周期恰好等于线程；
2. `pthread_create` 返回后才拿得到句柄/线程 ID，而线程可能已经开始执行。若此时新线程直接读 `arg->key`，会读到尚未写入的值（数据竞争）；
3. 让新线程的包装层**第一步就加同一把锁**，由于创建者仍持锁，新线程必然阻塞到创建者完成 `key` 回填与入树、解锁之后。于是新线程读到的一定是完整状态，无需 `pthread_self` 之类的补丁；
4. 用户入口在**锁外**调用，避免用户代码里再调 `cc_thread_status/cc_thread_wait` 时对非递归锁自锁。

**解决的问题：** 消除「线程已运行但标识尚未写入」的竞态，同时保证 API 可重入安全。

### 3.4 线程 ID 取句柄值；Windows 特判 GetThreadId

**做法：** `CCThread` 定义为 `CCUINTPTR`。POSIX 下直接用 `pthread_t` 的值作为 `key`；Windows 下 `HANDLE` 不是线程 ID，用 `GetThreadId(handle)` 的返回值作为 `key`。

**原因：**
1. POSIX 的 `pthread_t` 本身就代表线程标识，可直接比较、作键；
2. Windows 的 `HANDLE` 只是内核对象句柄，语义上并不是「线程 ID」；`GetThreadId` 返回进程内唯一、与线程一一对应的 ID，更适合作为对外标识；
3. `CCUINTPTR` 与指针等宽，可无告警地容纳上述各类原生值（64 位平台下 `pthread_t`/`HANDLE` 均为 8 字节）。

**解决的问题：** 统一跨平台 ID 类型，同时避免 Windows 上把句柄误当线程 ID 使用。

### 3.5 节点回收用「末尾节点搬到空位 + 弹尾」

**做法：** 红黑树删除返回被物理摘除的节点 `y` 后，若 `y` 不是数组末尾，则把末尾节点 `last` 整体搬到 `y` 的位置，修正树中指向 `last` 的至多 3 处引用（父节点的子槽、左子的 parent、右子的 parent）和 `root`，然后 `cc_dyn_core_pop` 弹掉末尾。

**原因：**
1. 若直接 `memmove` 平移被删位置之后的所有元素，需要 O(n) 搬移，并且要重新扫描修正大量下标；
2. 搬移末尾只需 O(1) 修正固定几处引用；
3. 本模块从不依赖下标标识线程：`_inner_thread_`、`cc_thread_status`、`cc_thread_wait` 全部按 `key` 定位，因此其他存活节点位置变化没有任何影响。

**解决的问题：** 删除保持 O(1)，数组始终紧凑、无空洞，`thread_nodes.size / sizeof(CCThreadNode)` 恒等于存活线程数，将来可直接据此提供计数接口。

### 3.6 等待采用轮询 + 退避；超时单位为微秒

**做法：** `cc_thread_wait` 周期性查询线程树，线程不在树中即返回。无限等待时退避从 1ms 平缓增长到 100ms；有限等待时从 1ms 快速翻倍到 16ms，累计睡眠时间达到 `timeOut_us` 即超时返回。首次检查前也会先判断线程是否存在，不存在则立即返回。

**原因：**
1. 线程已 detach，没有可 `join` 的对象，只能通过「是否仍登记在线程树中」判断存活；
2. 单位与 `ccsleep` 统一为微秒，避免调用方在秒/毫秒/微秒之间换算出错；
3. 有限等待使用更快的退避，保证超时判断足够灵敏；无限等待使用更慢的退避，降低长期轮询的 CPU 开销。

**解决的问题：** 修复了参考实现中「超时判断条件恒为假、导致超时参数失效」的缺陷，使超时与无限等待两条路径都按预期工作。

### 3.7 初始化与析构

`init_ccthread` 在 `init_ccalloc` 之后被调用（线程数组依赖 `ccalloc`），负责清空动态数组、把 `root` 置为 `CC_THREAD_NIL`、初始化内部锁；`deinit_ccthread` 在析构阶段逆序调用，释放节点数组并销毁锁。

> 注意：`deinit_ccthread` 假定调用时已无存活线程。若仍有线程在运行，它们会在退出时访问已释放的内部数组，属于调用方责任。正常用法是在关闭库之前等待/确认所有线程结束。

### 3.8 已知限制

- 线程 ID 可能被操作系统复用：本模块只保证「同时存活」的 ID 唯一；对已结束的 ID 调用 `cc_thread_wait` 会立即返回。
- 未提供「当前存活线程数」与「按 ID 遍历」等接口；需要时可在锁内用 `thread_nodes.size / sizeof(CCThreadNode)` 取得数量，再通过红黑树中序遍历枚举。
- 句柄管理器（线程与外部资源的句柄映射）不在本模块范围内，后续如需再单独设计。

