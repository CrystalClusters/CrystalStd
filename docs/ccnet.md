# ccnet

水晶簇标准库的网络模块。在 TCP / UDP 之上提供统一的连接管理与数据收发能力，屏蔽 Winsock / POSIX Socket 的平台差异。协议范围是 TCP 与 UDP；TLS、HTTP、WebSocket 等应用层协议留给扩展模块，数据组织方式由用户决定。

> 头文件为 `include/ccnet.h`，使用 `#include <ccnet.h>`。交互演示见 [ccnet_demo](ccnet_demo.md)。

---

## 1. 产品定位

### 1.1 目标

CrystalStd Network 只负责**网络连接管理**与**网络数据收发**，数据组织、应用层协议、业务逻辑与通信策略由用户自行提供解决方案。

### 1.2 内部依赖

| 模块 | 关系 |
|---|---|
| `ccthread` | 每个网络对象一个善后线程，统一用 `cc_thread_create` 创建 |
| `ccalloc` | 上下文、启动块、地址缓冲等全部堆分配走 `ccalloc` |
| `cctime` | 超时、轮询退避的计时基准 |

---

## 2. 接口列表

```C
// 连接一个已构造的地址；setup 在善后线程内完成，成功后由该线程调用入口
CCINT32 ccnet_tcp_connect(
    const CCNetAddr *addr,
    CCUINT32 timeOut_us,
    CCNetTcpEntry entry,
    void *user
);
```

**参数含义：**
- `addr`：目标地址（含端口）。由 `ccnet_addr_parse`（IP 字面量）或 `ccnet_dns`（主机名）构造；库在调用期间复制该结构。
- `timeOut_us`：握手的等待上限；`0` 使用 `CC_NET_DEFAULT_TIMEOUT_US`，`>0` 为本次预算，`CC_NET_TIMEOUT_INFINITE` 表示显式无限等待。
- `entry`：用户入口，**必填**；传 `NULL` 返回 `CC_NET_ERR_PARAM`。
- `user`：用户数据指针，创建时保存、入口执行时原样回传；可为 `NULL`。

**返回值：**
- **成功：** `0`；setup 已完成，善后线程将调用 `entry`。
- **失败：** 负值（对应错误码）；socket 与线程由善后线程清理，入口只在 setup 成功后调用。

**注意事项：**
- 创建函数创建善后线程，并**等待该线程完成 setup**（建 socket + 连接 `addr`）；连接错误由返回值表达。
- 本接口是**一次连接尝试**：`addr` 是确定的单个地址；`ccnet_dns` 返回多个地址时，由调用方逐个调用并决定回退策略。
- DNS 由 `ccnet_dns` 独立完成；本接口接收已解析的地址。
- `addr->port` 为 `0`、`family` 非法、地址字节非法时返回 `CC_NET_ERR_PARAM`。
- TCP 连接默认开启 keepalive（空闲 60 秒后探测），参数见 5.9。
- 线程可能在创建函数返回**之前**就调用入口、甚至入口已经返回；这属于正常情况，清理由善后线程完成。

### 2.3 TCP 服务端

```C
// 绑定并启动善后线程；setup 完成后由该线程调用 accept 入口
CCINT32 ccnet_tcp_listen(
    const CCNetAddr *addr,
    CCNetAcceptEntry entry,
    void *user
);
```

**参数含义：**
- `addr`：本地绑定地址（含端口）；`port` 为 `0` 表示临时端口；通配地址由 `ccnet_addr_parse("0.0.0.0", port)` 或 `ccnet_addr_parse("::", port)` 构造；库在调用期间复制该结构。
- `entry`：accept 入口，**必填**；在其中循环调用 `ops->accept`。
- `user`：用户数据指针，原样回传给 `entry`。

**返回值：**
- **成功：** `0`；绑定与 listen 已完成，善后线程将调用 accept 入口。
- **失败：** 负值（对应错误码）；socket 与线程由善后线程清理，入口只在 setup 成功后调用。

**注意事项：**
- `bind` + `listen` 在善后线程内完成；创建函数**等待 setup 结果**，失败能立刻返回。
- `addr->family` 决定 IPv4 / IPv6；绑定 `::` 时显式设置 `IPV6_V6ONLY = 1`。
- backlog 固定为 `128` 的请求值，实际值由平台决定。
- 监听器的操作全部通过 accept 入口的 `ops` 完成：`accept` / `local`；`port = 0` 时，实际端口在 accept 入口里用 `ops->local` 取。
- `accept` 是阻塞调用：`timeOut_us` 为 `0` 时使用默认 30 秒；长期运行的 accept 循环在 `CC_NET_ERR_TIMEOUT` 后继续调用，或用 `CC_NET_TIMEOUT_INFINITE` 显式无限等待。
- `accept` 成功后会为连接创建善后线程：该线程持有新 socket，构建自己的上下文，调用连接入口，返回后清理。
- accept 得到的连接同样默认开启 keepalive，参数见 5.9。
- 并发上限、限流等接入策略由 accept 循环自行维护（例如在 `user` 里计数，连接入口退出前减计数）；连接表由业务自行维护。
- `accept` 内线程创建失败时返回 `CC_NET_ERR_RESOURCE`，已接受的 socket 由库关闭，监听器继续工作。

### 2.4 UDP

```C
// 绑定本地地址并启动善后线程；setup 完成后由该线程调用入口
CCINT32 ccnet_udp_open(
    const CCNetAddr *addr,
    CCNetUdpEntry entry,
    void *user
);
```

**参数含义：**
- `addr`：本地绑定地址（含端口）；`port` 为 `0` 表示临时端口（实际端口在入口里用 `ops->local` 取）；通配地址由 `ccnet_addr_parse` 构造；库在调用期间复制该结构。
- `entry`：用户入口，必填。
- `user`：用户数据指针，原样回传。

**返回值：**
- **成功：** `0`；绑定已完成，善后线程将调用入口。
- **失败：** 负错误码；socket 与线程由善后线程清理，入口只在 setup 成功后调用。

**注意事项：**
- UDP 端点直接绑定收发；一个端点服务任意多个对端，对端地址随 `recvfrom` 给出。
- 零长度数据报是合法数据报；截断数据报按整包丢弃处理（见 5.4）。
- 顺序、重传、去重、送达由上层负责；远端地址变化属于正常收发现象。
- 端点同样由善后线程包裹，生命周期规则与 TCP 连接一致。

### 2.5 网络线程与接口注入

善后线程在 setup 成功后，把**该对象的常量操作表**与**上下文指针**一起交给用户入口：

```C
typedef void (*CCNetAcceptEntry)(const CCNetAcceptOps *ops, void *ctx, void *user);
typedef void (*CCNetTcpEntry)(const CCNetTcpOps *ops, void *ctx, void *user);
typedef void (*CCNetUdpEntry)(const CCNetUdpOps *ops, void *ctx, void *user);
```

- `ops` 是每个协议一张的 `const` 表，进程生命周期内恒定。
- `ctx` 是 CrystalStd 管理的网络对象上下文：库创建、库释放，用户只通过 `ops` 的成员使用。
- `user` 是创建网络对象时注入的用户数据，与 `ctx` 分开。
- 表内成员就是「网络线程可以做的事」的完整集合；入口返回即线程结束，由善后线程关闭 socket、释放上下文。
- `ops` 与 `ctx` 的有效期到入口返回为止（见 2.6）。

| 表 | 成员 | 语义 |
|---|---|---|
| `CCNetAcceptOps` | `accept` | 等待并接受一个连接，成功后为它创建善后线程 |
| | `local` | 查询监听器的本地地址 |
| `CCNetTcpOps` | `state` | 读取 `CCNetState` |
| | `local` / `peer` | 查询本地 / 远端地址 |
| | `send` / `recv` | 阻塞收发（带 `timeOut_us`），单次发送 / 单次读取 |
| `CCNetUdpOps` | `local` | 查询绑定地址 |
| | `sendto` / `recvfrom` | 数据报收发（带 `timeOut_us`） |

> TCP 与 UDP 各使用一张专用表：协议语义各自独立，专用表让成员集合与协议一致（见 8.3）。

收发成员都带 `timeOut_us`：`0` 使用 `CC_NET_DEFAULT_TIMEOUT_US`，`>0` 为本次调用预算，`CC_NET_TIMEOUT_INFINITE` 表示显式无限等待；超时返回 `CC_NET_ERR_TIMEOUT`。

### 2.6 上下文有效期与协作线程

- `ctx` 只在**入口执行期间**有效；入口返回后，善后线程关闭 socket、释放上下文，此后的任何 `ops` 调用属于**未定义行为**。
- 入口可以创建协作线程，分别处理发送和接收；这些线程由用户创建、用户管理。
- 协作线程必须在入口返回前结束全部网络操作；入口返回与协作线程的退出顺序由用户保证。
- 入口返回即收回上下文；引用计数与自动等待由业务按需自行实现。

### 2.7 地址与 DNS

```C
// 从 IP 字面量构造地址（"1.2.3.4"、"::1"、"[::1]"、带 %scope 的 IPv6）
CCINT32 ccnet_addr_parse(const char *text, CCUINT16 port, CCNetAddr *out);

// 同步解析主机名（getaddrinfo）；结果写入数组，返回写入个数
CCINT32 ccnet_dns(const char *host, CCUINT16 port, CCNetAddr *out, CCUINT32 cap);

// 格式化为 "ip:port" / "[ip]:port" / "[ip%scope]:port"
CCINT32 ccnet_addr_text(const CCNetAddr *addr, char *buf, CCUINT32 size);
```

**参数含义：**
- `text`：IP 字面量文本（IPv4、IPv6、带方括号的 IPv6），端口单独传。
- `host`：主机名或 IP 字面量；字面量直接填充结果，跳过 DNS。
- `port`：端口（主机字节序），可为 `0`；`dns` 把该端口填进每个结果。
- `out` / `cap`：地址输出数组与容量；`dns` 最多写入 `cap` 个。
- `buf` / `size`：文本输出缓冲与容量，建议 `>= 64` 字节。

**返回值：**
- **ccnet_addr_parse：**
  `0` 成功；参数非法返回 `CC_NET_ERR_PARAM`；看起来像域名时返回 `CC_NET_ERR_DNS`。
- **ccnet_dns：**
  成功返回写入个数（`>= 1`，最多 `cap` 个）；解析失败返回 `CC_NET_ERR_DNS`；参数非法返回 `CC_NET_ERR_PARAM`。
- **ccnet_addr_text：**
  `0` 成功；参数非法或缓冲容量偏小返回 `CC_NET_ERR_PARAM`。

**注意事项：**
- `ccnet_dns` 是**独立的地址获取操作**：创建接口接收它的结果，连接失败的回退由调用方决定。
- `ccnet_dns` 直接调用系统 `getaddrinfo`，同步返回；解析时间由平台 resolver 与网络环境决定。
- 结果顺序即平台返回顺序；去重与 IPv4 / IPv6 偏好由调用方决定。库不提供缓存、重试与地址优先级策略。
- `cap` 大于 `INT32_MAX` 时返回 `CC_NET_ERR_PARAM`。
- `scope` 是 IPv6 接口索引：`0` 表示未指定；链路本地地址（`fe80::/10`）连接时通常需要显式 scope。`ccnet_addr_parse` 接受 `%<数字>` 与 `%<接口名>`，接口名通过系统接口表解析；数字范围为 `0..UINT32_MAX`，越界或负数返回 `CC_NET_ERR_PARAM`。`ccnet_addr_text` 在 `scope != 0` 时输出 `%<数字>`。
- `CCNetAddr` 字段公开，可以直接填充；`parse` / `dns` 是两个构造入口。
- 文本格式固定为 `ip:port`，IPv6 带方括号；端口永远输出（`0` 输出 `:0`）。

---

## 3. 数据类型定义

以下为 `include/ccnet.h` 的**类型部分**（实现以此为准；函数原型见第 2 章）。全部类型只追加，枚举可追加。

```C
// 收发与连接的默认超时（微秒）：timeOut_us 传 0 时使用
#define CC_NET_DEFAULT_TIMEOUT_US 30000000u

// 显式无限等待：调用方承担线程长期停留在阻塞操作的风险
#define CC_NET_TIMEOUT_INFINITE   0xFFFFFFFFu

// TCP keepalive 默认参数（构建期固定）
#define CC_NET_KEEPALIVE_IDLE_SEC     60
#define CC_NET_KEEPALIVE_INTERVAL_SEC 10
#define CC_NET_KEEPALIVE_COUNT        6

// 网络状态：只描述入口执行期间可观察的 TCP 状态
typedef enum cc_net_state {
    CC_NET_STATE_OPEN        = 1,   //已连接，可收发
    CC_NET_STATE_PEER_CLOSED = 2,   //TCP 对端有序关闭（收到 FIN），读方向结束
    CC_NET_STATE_FAILED      = 3,   //连接重置 / keepalive 判定断开
} CCNetState;

// 统一错误码：32 位有符号，全部为负值；CC_NET_OK 等价于 0
typedef enum cc_net_error {
    CC_NET_OK             =   0,    //成功
    CC_NET_ERR_PARAM      =  -1,    //参数非法（空指针 / 容量为 0 / entry 为空）
    CC_NET_ERR_NOMEM      =  -2,    //内存分配失败
    CC_NET_ERR_RESOURCE   =  -3,    //系统资源耗尽（fd / 端口 / 内核缓冲）
    CC_NET_ERR_STATE      =  -4,    //当前状态拒绝该操作（例如 FAILED 后继续收发）
    CC_NET_ERR_TIMEOUT    =  -5,    //本次调用的等待超过 timeOut_us
    CC_NET_ERR_DNS    =  -6,    //域名解析失败
    CC_NET_ERR_REFUSED    =  -7,    //连接被拒绝
    CC_NET_ERR_UNREACH    =  -8,    //网络 / 主机不可达
    CC_NET_ERR_RESET      =  -9,    //连接被对端重置
    CC_NET_ERR_ADDRINUSE  = -10,    //地址 / 端口已被占用
    CC_NET_ERR_ADDRNOTAVAIL = -11,  //地址不可用（不属于本机等）
    CC_NET_ERR_TOOBIG     = -12,    //数据报超过平台上限
    CC_NET_ERR_TRUNCATED  = -13,    //数据报被截断（该数据报被丢弃）
    CC_NET_ERR_IO         = -14,    //其他底层 IO 错误
} CCNetError;

// 地址族
typedef enum cc_net_family {
    CC_NET_AF_INET  = 4,
    CC_NET_AF_INET6 = 6,
} CCNetFamily;

// 地址：值类型，可自由拷贝
typedef struct cc_net_addr {
    CCNetFamily family;     //地址族
    CCUINT16    port;       //端口，主机字节序
    CCUINT32    scope;      //IPv6 接口索引；0 表示未指定；IPv4 恒 0
    CCUINT8     addr[16];   //网络字节序的原始地址字节：IPv4 有效前 4 字节其余为 0
} CCNetAddr;

// 先声明表类型：入口与表互相引用
typedef struct cc_net_accept_ops CCNetAcceptOps;
typedef struct cc_net_tcp_ops    CCNetTcpOps;
typedef struct cc_net_udp_ops    CCNetUdpOps;

// 用户入口：ops 由库注入，ctx 为网络对象上下文，user 为创建时传入的指针
typedef void (*CCNetAcceptEntry)(const CCNetAcceptOps *ops, void *ctx, void *user);
typedef void (*CCNetTcpEntry)(const CCNetTcpOps *ops, void *ctx, void *user);
typedef void (*CCNetUdpEntry)(const CCNetUdpOps *ops, void *ctx, void *user);

// TCP 操作表：常量，所有成员以上下文指针为第一参数
struct cc_net_tcp_ops {
    CCINT32 (*state)(void *ctx, CCNetState *out);
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
    CCINT32 (*peer)(void *ctx, CCNetAddr *out);
    CCINT64 (*send)(void *ctx, const void *data, CCUINT64 size, CCUINT32 timeOut_us);
    CCINT64 (*recv)(void *ctx, void *buf, CCUINT64 cap, CCUINT32 timeOut_us);
};

// UDP 操作表
struct cc_net_udp_ops {
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
    CCINT64 (*sendto)(void *ctx, const CCNetAddr *to, const void *data, CCUINT64 size, CCUINT32 timeOut_us);
    CCINT64 (*recvfrom)(void *ctx, CCNetAddr *from, void *buf, CCUINT64 cap, CCUINT32 timeOut_us);
};

// 监听器操作表：accept 为连接创建一个善后线程
struct cc_net_accept_ops {
    CCINT32 (*accept)(void *ctx, CCNetTcpEntry entry, void *user, CCUINT32 timeOut_us);
    CCINT32 (*local)(void *ctx, CCNetAddr *out);
};
```

**约定：**
- 连接与监听器的标识留在善后线程内；`ctx` 是库管理的上下文指针，只在入口执行期间有效。
- `CCNetAddr` 是纯值类型，跨模块传递可直接拷贝；创建接口直接接收它。
- 地址由 `ccnet_addr_parse`（IP 字面量）或 `ccnet_dns`（主机名）构造；`CCNetAddr` 字段公开，也可以直接填充。
- 错误表示是 32 位有符号的 `CCNetError`（`0` 成功，负值失败）；平台原生码留在实现内部。
- 收发与连接的超时参数共用 `0`、正数、`CC_NET_TIMEOUT_INFINITE` 三种取值。
- 三张操作表都是 `const` 常量，进程生命周期内地址稳定；表内成员始终可用。

---

## 4. 线程模型与生命周期

### 4.1 对象与线程的对应

| 对象 | 善后线程 |
|---|---|
| TCP 连接（connect / accept） | 1 个：setup（连接）→ 用户入口 → 清理 |
| UDP 端点 | 1 个：setup（bind）→ 用户入口 → 清理 |
| TCP 监听器 | 1 个：setup（bind / listen）→ accept 入口 → 清理 |

每个网络对象都由一个善后线程从头管到尾：创建 socket、调用入口、关闭 socket 都在同一个线程里。全局状态为零，所有对象都由善后线程驱动；入口由善后线程当作普通函数调用。

### 4.2 创建时序

- **创建函数：** 分配启动块（参数、锁、结果标志）→ 创建善后线程 → 等待 setup 结果 → 返回错误码或 `0`。
- **善后线程：** 建 socket → 连接 / bind（按对象类型，地址由参数给出）→ 把结果写回启动块并唤醒创建函数 → 成功则调用用户入口，失败则跳过入口 → 清理。
- **accept：** 监听线程的 `accept` 拿到新 socket → 为连接创建善后线程 → 连接线程构建上下文、调用连接入口、清理；`accept` 本身只等待线程创建结果。

创建函数返回 `0` 只代表 setup 成功；此后入口可能刚开始、也可能已经结束。

### 4.3 清理顺序

用户入口返回后，善后线程在**同一个线程内**执行：

1. 关闭底层 socket（`closesocket` / `close`）；
2. 释放上下文（启动块由创建函数自己管理）。

清理在入口返回后进行，此时 `ops` 调用已经结束，同步面为零。

保证：

- **I1** 入口执行期间，`ctx` 指向的上下文一直有效。
- **I2** 入口返回后，库关闭 socket、释放上下文。
- **I3** 入口返回后，`ops` 与 `ctx` 立即失效；后续使用属于未定义行为。
- **I4** 入口是对象生命周期的唯一终点；结束动作只有入口返回。
- **I5** 协作线程的网络操作必须在入口返回前结束；协作线程由用户回收。

### 4.4 入口返回是唯一终点

| 事件 | 触发者 | 效果 |
|---|---|---|
| 入口返回 | 用户代码 | 善后线程关闭 socket、释放上下文，随后线程退出 |
| 线程退出 | 善后线程返回 | 该对象彻底结束，资源随线程释放 |

用户入口决定何时结束：读完协议、处理完业务、检查到退出条件，直接返回即可。库在返回后完成资源释放，通信策略由用户决定。

### 4.5 setup 失败与接口失效

- setup 失败：善后线程关闭已建 socket、释放上下文，把错误码写回启动块；只有 setup 成功才调用入口；创建函数返回该错误码。
- 入口返回后：`ops` / `ctx` 失效；此后使用是未定义行为。
- 存活线程由调用方掌握：`deinit_ccnet` 前确保所有善后线程已退出。

---

## 5. 语义约定

### 5.1 阻塞与超时

- 所有收发都是**阻塞**的，等待上限由 `timeOut_us` 给出。
- `timeOut_us` 的三种取值：`0` 用 `CC_NET_DEFAULT_TIMEOUT_US`（30 秒）；正数为本次预算；`CC_NET_TIMEOUT_INFINITE` 为显式无限等待。
- 超时返回 `CC_NET_ERR_TIMEOUT`；超时只影响当次调用，连接保持可用（`send` 的部分发送见 5.2，keepalive 判死见 5.9）。
- 无限等待会让网络线程长期停留在阻塞操作中；外部结束动作只有入口返回，线程只能等对端活动、出错或进程退出。选择无限等待的调用方承担这一后果。
- 默认单线程顺序调用；入口可以创建协作线程分别收发，约束见 5.6。

### 5.2 TCP 发送

- **单次发送：** 一次调用执行一次底层 `send`，返回实际发送的字节数（`0..size`）。
- 部分发送是正常结果：调用方拿到返回值后从剩余位置继续调用，循环由调用方控制。
- `size == 0` 立即返回 `0`。
- `timeOut_us` 内一个字节都没能发出时返回 `CC_NET_ERR_TIMEOUT`，连接保持可用；已经发出的字节按实际数量返回。
- 返回负值表示本次调用失败：`CC_NET_ERR_RESET` 表示对端重置，`CC_NET_ERR_IO` 表示其他底层错误。失败后入口应返回，由库完成清理。
- `send_all`、发送队列与自动重试由调用方实现。

### 5.3 TCP 接收

- **单次读语义：** 返回当前可读的任意字节数（`1..cap`），可能少于 `cap`；消息边界由调用方定义。
- `timeOut_us` 内未收到数据时返回 `CC_NET_ERR_TIMEOUT`；本次调用从流中取走 `0` 字节，连接保持可用，入口可以继续调用。
- 返回 `0`：对端有序关闭（FIN）。此后状态为 `CC_NET_STATE_PEER_CLOSED`，再次 `recv` 仍返回 `0`；写方向在出现重置前仍然可用。
- 返回 `CC_NET_ERR_RESET`：对端重置，状态置为 `CC_NET_STATE_FAILED`；此后收发返回 `CC_NET_ERR_STATE`。
- `cap == 0` 返回 `CC_NET_ERR_PARAM`。

### 5.4 UDP 收发

- **`sendto`：** 一次调用一个数据报，整包原子；成功返回 `size`（`size == 0` 是合法空数据报）；超过平台上限返回 `CC_NET_ERR_TOOBIG`；`to` 为空返回 `CC_NET_ERR_PARAM`；`timeOut_us` 内内核仍未接收时返回 `CC_NET_ERR_TIMEOUT`，该数据报未发送，可以重试。
- **`recvfrom`：** 一次调用一个数据报，返回拷贝字节数（`0` 表示空数据报）；`timeOut_us` 内未收到数据时返回 `CC_NET_ERR_TIMEOUT`，端点保持可用。数据报大于 `cap` 时返回 `CC_NET_ERR_TRUNCATED`，该数据报被丢弃；`from` 可为 `NULL`。
- 零长度数据报按合法数据报交付；顺序、重传、去重、送达由上层负责；远端地址变化属于正常收发现象。

### 5.5 入口返回后的行为

入口返回后，善后线程关闭 socket、释放上下文；`ops` 与 `ctx` 同时失效。此后对 `ops` 成员的任何调用属于**未定义行为**，库按“接口已经过期”处理。

### 5.6 并发契约

- **默认单线程：** 一个入口线程顺序调用收发即可。
- **协作线程：** 入口可以创建协作线程；推荐一个读线程、一个写线程，形成全双工。
- **方向约束：** 同一时刻同一方向只允许一个调用（一个 `send` / `sendto`、一个 `recv` / `recvfrom`）；不同方向可以各有一个线程并发。同方向的并发调用由用户串行化。
- **只读查询：** `state` / `local` / `peer` 可以在入口与协作线程中调用；`local` / `peer` 在 setup 后保持不变，`state` 由库以原子方式维护。
- **有效期：** 所有协作线程的网络操作必须在入口返回前结束；协作线程由用户回收。
- **accept：** 同一个监听器同一时刻只允许一个线程调用 `accept`。

### 5.7 状态与错误

- 状态描述 TCP 连接处在什么阶段（`CCNetState`），由 TCP 的 `ops->state` 读取；查询只读，状态保持原样。
- `state` 成功返回 `0` 并写入输出；`out` 为 `NULL` 返回 `CC_NET_ERR_PARAM`；上下文有效时，状态查询按返回值报告结果。
- 状态转换：
  - setup 成功：状态为 `OPEN`，入口从这里开始；
  - TCP `recv` 返回 `0`：`OPEN` → `PEER_CLOSED`；
  - TCP 重置（`RESET`）→ `FAILED`；
  - POSIX 下 keepalive / 重传超时（`ETIMEDOUT`）→ `FAILED`，见 5.9；
  - 单次调用超时（`TIMEOUT`）与一般 `IO` 错误不改变状态，连接可以继续使用。
- `PEER_CLOSED` 只表示 TCP 对端有序关闭（FIN）；UDP 端点没有状态机。
- **错误只通过调用返回值给出**；每次调用的返回值就是结果。
- 统一码是 32 位有符号整数；平台原生码只在实现内部参与映射。
- `CC_NET_ERR_STATE` 用于当前状态拒绝操作，例如 `FAILED` 后的继续收发。
- `CC_NET_ERR_TRUNCATED` 与其它错误码一样是失败码：`recvfrom` 返回它时该数据报被丢弃。

### 5.8 超时

| 接口 | `timeOut_us == 0` | `timeOut_us > 0` | `CC_NET_TIMEOUT_INFINITE` |
|---|---|---|---|
| `ccnet_tcp_connect` | 默认 30 秒 | 握手预算 | 一直等待 |
| `CCNetAcceptOps.accept` | 默认 30 秒 | 本次预算 | 一直等待 |
| `send` / `recv` / `sendto` / `recvfrom` | 默认 30 秒 | 本次预算 | 一直等待 |
| `ccnet_dns` | 系统 `getaddrinfo`，由平台 resolver 决定耗时 | 同左 | 同左 |

超时只影响当次调用：accept 超时后监听器继续 accept；connect 超时直接失败；`recv` / `recvfrom` 超时后连接保持可用，入口可以继续调用；`sendto` 超时后该数据报未发送，可以重试；`send` 超时按 5.2 处理。keepalive 判死返回的 `TIMEOUT` 属于连接错误，按 5.9 处理。无限等待的风险由调用方承担。

### 5.9 TCP keepalive

- 每个 TCP 连接（`connect` 与 `accept`）默认开启 keepalive。
- 默认参数：空闲 60 秒后开始探测，探测间隔 10 秒，连续 6 次无响应判定断开；Linux / Android 的检测窗口约 120 秒。
- Windows 使用 `SIO_KEEPALIVE_VALS`（开关、空闲时间、间隔），探测次数由系统决定，检测窗口与上述量级一致。
- keepalive 基础开关与平台提供的细化参数设置失败时，连接 setup 失败；平台未提供某细化选项时跳过，使用系统默认值。
- 探测由内核完成，对应用不可见；不改变 `send` / `recv` 的数据语义，也不产生应用数据。
- keepalive 只对空闲连接探测；有持续数据时由 TCP 重传与错误路径处理。
- 检测到断开后，阻塞中的收发返回错误，状态进入 `FAILED`：POSIX 通常返回 `TIMEOUT`（内核 `ETIMEDOUT`），Windows 通常返回 `RESET`（`WSAENETRESET`）；后续收发返回 `STATE`。
- 空闲探测可能被中间设备丢弃；这类链路需要调整构建期参数，或用应用层心跳补充。
- keepalive 是半开连接的兜底；需要更快检测或自定义周期时，应用层心跳仍是补充手段。
- 参数定义在 `include/ccnet.h`，属于构建期常量；调整后需要重新构建库。

---

## 6. 内部实现

> 本章描述实现细节，供维护者阅读。内部结构定义在 `src/ccnet_internal.h`，**仅内部使用**。

### 6.1 善后线程

每个网络对象一个线程，线程体就是「setup → 入口 → 清理」：

```C
/* 内部形式，示意；wrapper 本身是 CCThreadEntry */
static void _tcp_wrapper_(void *arg, CCThread idThis)
{
    CCNetStart *start = arg;
    CCNetCtx    ctx;
    CCINT32     rc;

    rc = _tcp_setup_(start, &ctx);   /* 把 entry / user 等取进 ctx */
    ctx.thread = idThis;
    _start_finish_(start, rc);       /* start 只在此前读取 */
    if (rc != CC_NET_OK) {
        _ctx_free_(&ctx);
        return;                       /* 入口只在 setup 成功后调用 */
    }
    ctx.tcp_entry(&tcp_ops, &ctx, ctx.user);
    _ctx_teardown_(&ctx);
    _ctx_free_(&ctx);
}
```

- setup 与清理都在同一个线程里，socket 的创建者、使用者、关闭者是同一个执行流。
- 入口拿到的是 `&ctx` 与常量表；入口返回后 `ctx` 出栈/释放，接口随之失效。
- 全局状态为零；线程结束即对象结束。

### 6.2 上下文与启动块

上下文是善后线程私有的，入口执行期间一直有效：

```C
typedef struct cc_net_ctx {
    CCNetKind  kind;        /* TCP_CONN / UDP_EP / LISTENER */
    CCNetSocket sock;       /* 平台 socket：Windows 为 SOCKET，POSIX 为 int */
    CCNetState state;
    CCNetAddr  local;
    CCNetAddr  peer;        /* TCP 用；UDP 的对端随数据报给出 */
    CCThread   thread;      /* 善后线程 ID */
    CCNetAcceptEntry accept_entry;
    CCNetTcpEntry tcp_entry;
    CCNetUdpEntry udp_entry;
    void      *user;
} CCNetCtx;
```

启动块只用于「创建函数 ↔ 善后线程」之间的一次同步：

```C
typedef struct cc_net_start {
    CCNetAddr   addr;
    CCUINT32    timeOut_us;
    CCNetKind   kind;
    CCNetAcceptEntry accept_entry;
    CCNetTcpEntry    tcp_entry;
    CCNetUdpEntry    udp_entry;
    void       *user;
    CCLock      lock;
    CCBOOL      done;
    CCINT32     result;
} CCNetStart;
```

- 创建函数在 `lock` 保护下轮询 `done`（`ccthread` 只提供锁，轮询配短睡眠），读走 `result` 后返回。
- 善后线程在通知结果**之前**已把 `addr` / `entry` / `user` 等取进自己的上下文；启动块可以放在创建函数的栈上。

### 6.3 生命周期

- **创建：** 创建函数启动线程并等待 setup；失败则线程自行清理，创建函数返回错误。
- **运行：** 入口在善后线程内执行；库的职责是 setup 与清理。
- **结束：** 入口返回 → 善后线程关闭 socket、释放上下文 → 线程退出。
- **全局状态为零：** 初始化只做平台准备。

### 6.4 初始化与逆初始化

- `init_ccnet` / `deinit_ccnet` 由 `crystal_std` 各调用一次；重复调用为空操作，并发调用由调用方串行。
- `init_ccnet`：已 ready 时直接返回；Windows 下 `WSAStartup(MAKEWORD(2, 2))`；POSIX 下由系统提供。
- `deinit_ccnet`：未 ready 时直接返回；Windows 下 `WSACleanup`；调用前确保所有善后线程已经退出。
- 全局状态为零；`deinit_ccnet` 只做平台清理，网络对象由调用方确保已退出。

### 6.5 源码布局

| 文件 | 职责 |
|---|---|
| `src/ccnet.c` | 善后线程的通用部分、启动块、上下文生命周期、三个创建接口 |
| `src/ccnet_tcp.c` | TCP setup（连接）、accept、TCP 与 accept 操作表 |
| `src/ccnet_udp.c` | UDP setup（bind）、UDP 操作表 |
| `src/ccnet_addr.c` | 地址解析与格式化、同步 `getaddrinfo` 封装、错误映射 |
| `src/ccnet_os.c` | 平台原语：socket 创建 / 关闭 / 超时等待 |
| `src/ccnet_internal.h` | 内部结构、启动块、共享内部接口声明 |

---

## 7. 跨平台实现

### 7.1 平台映射

| 事项 | Windows | Linux / Android |
|---|---|---|
| 网络 API | Winsock 2.2 | POSIX Socket |
| socket 类型 | `SOCKET`（`CCNetSocket` 内部别名） | `int` |
| 关闭 | `closesocket` | `close` |
| 发送触发 SIGPIPE | 由系统处理 | `send` / `sendto` 带 `MSG_NOSIGNAL` |
| keepalive | `SIO_KEEPALIVE_VALS` | `SO_KEEPALIVE` + `TCP_KEEPIDLE` / `TCP_KEEPINTVL` / `TCP_KEEPCNT` |
| 超时等待 | `WSAPoll` / `select` / `SO_RCVTIMEO` | `poll` / `select` / `SO_RCVTIMEO` |
| 错误源 | `WSAGetLastError` | `errno` |

socket 选项采用平台默认值；TCP keepalive 由库按 5.9 配置；地址复用、广播、组播与缓冲区配置由后续统一配置接口承担。`backlog` 在 `listen` 时使用 128 的请求值，实际值由平台决定。

### 7.2 错误映射

| `CCNetError` | POSIX | Winsock |
|---|---|---|
| `CC_NET_ERR_NOMEM` | `ENOMEM` | —（`ccalloc` 分配失败） |
| `CC_NET_ERR_RESOURCE` | `EMFILE` / `ENFILE` / `ENOBUFS` | `WSAEMFILE` / `WSAENOBUFS` |
| `CC_NET_ERR_REFUSED` | `ECONNREFUSED` | `WSAECONNREFUSED` |
| `CC_NET_ERR_UNREACH` | `EHOSTUNREACH` / `ENETUNREACH` | `WSAEHOSTUNREACH` / `WSAENETUNREACH` |
| `CC_NET_ERR_RESET` | `ECONNRESET` | `WSAECONNRESET` / `WSAENETRESET` |
| `CC_NET_ERR_TIMEOUT` | `ETIMEDOUT` | `WSAETIMEDOUT` |
| `CC_NET_ERR_ADDRINUSE` | `EADDRINUSE` | `WSAEADDRINUSE` |
| `CC_NET_ERR_ADDRNOTAVAIL` | `EADDRNOTAVAIL` | `WSAEADDRNOTAVAIL` |
| `CC_NET_ERR_TOOBIG` | `EMSGSIZE` | `WSAEMSGSIZE` |
| `CC_NET_ERR_DNS` | `EAI_*` | `WSAHOST_NOT_FOUND` 等 |
| `CC_NET_ERR_IO` | 其余 | 其余 |

> 平台原生码只在实现内部用于映射；统一码是公开的错误表示。

### 7.3 信号

- Linux / Android 上发送使用 `MSG_NOSIGNAL`，进程级 SIGPIPE 处理保持原样。
- Windows 侧由系统处理 SIGPIPE。

### 7.4 DNS 与双栈

- DNS 由 `ccnet_dns` 独立完成：同步调用系统 `getaddrinfo`，返回一组 `CCNetAddr`；解析时间由平台 resolver 决定。
- 创建接口接收地址结构：`connect` 只做一次连接尝试，多地址的回退由调用方决定。
- 绑定地址由 `CCNetAddr.family` 决定 IPv4 / IPv6；绑定 `::` 时显式 `IPV6_V6ONLY = 1`；通配地址由 `ccnet_addr_parse("0.0.0.0" / "::", port)` 构造。
- IPv6 链路本地地址的 `scope` 由调用方填入；`0` 表示未指定，连接时由平台决定行为。
- 需要同时监听 IPv4 与 IPv6 时，创建两个监听器（两个善后线程；同一端口能否同时绑两族由平台决定）。

---

## 8. 设计思路Q&A

### 8.1 为什么创建接口只返回结果

提供线程的意义就是让连接**自给自足**：善后线程完成 setup 后，入口拿到 `ops + ctx`，收发、状态、地址都能做。创建方需要的只有「成功还是失败」。

- 线程是所有者，标识留在线程内；创建方拿到结果即可。
- setup 失败时线程自行清理，创建方拿到错误码即可。
- 需要协作时，入口把 `(ops, ctx)` 交给协作线程，有效期到入口返回为止（2.6）；库把协作方式留给业务。
- 这与设计稿「用户提供网络线程」一致：线程负责，库负责 setup、把接口递到入口、在入口返回后收尾。

### 8.2 为什么错误只经返回值

- setup 失败的原因很重要（拒绝、不可达、超时、资源不足），由返回值表达；创建函数等待 setup，因此拿得到。主机名解析失败由 `ccnet_dns` 返回。
- 运行期的错误由 `ops` 成员的返回值表达，错误随返回值即取即用。
- 错误表示就是 32 位统一码；平台原生码与文案留在实现内部，诊断交给日志与调试器。

### 8.3 为什么采用常量表 + `ctx` 参数

- 表是 `const`，进程生命周期内共享，每连接开销为零。
- `ctx` 是善后线程私有的上下文指针：入口执行期间有效，入口返回即失效；上下文由指针直接访问。
- 跨线程有效期由调用方保证；换来的是一张常量表、零全局状态。
- 表内成员就是网络线程可做的事；将来可以按连接类型注入不同的表（如 TLS 包装表）。

### 8.4 为什么生命周期只有一个终点

- 生命周期终点是入口返回；善后线程完成清理。第二个结束动作会与线程退出竞争。
- 对象归线程所有，释放由线程完成。
- 每个对象都必须有善后线程：setup、超时等待、释放都需要一个明确的执行者。

### 8.5 为什么发送与接收都是单次操作

- **发送单次**：一次 `send` 对应一次内核写，返回实际字节数；部分发送由调用方继续，发送队列由业务维护。
- **接收单次**：一次 `recv` 对应一次内核读，可能只有半个包、也可能一次读到两条消息。库补齐一条消息就需要内置分帧规则，而分帧属于用户的应用层协议。
- 两条合起来意味着：**库只搬字节，缓冲与消息边界由调用方负责**（8.6）。

### 8.6 为什么缓冲留在调用方

- 缓冲留在调用方，库只做直通读写；内存上限与 flush 语义由业务掌握，与「精简」的取向一致。
- 背压由阻塞读写直接体现：对端读取变慢，`send` 就阻塞到超时。
- 需要用户态缓冲的协议（粘包处理、批量发送）由用户在 ops 之上自行实现。

### 8.7 为什么监听器也由善后线程驱动

- 监听器由它的善后线程标识：该线程 bind / listen，调用 accept 入口，入口返回后关闭监听 socket。
- 接入策略（并发上限、限流、按 IP 拒绝、何时退出）全部由用户的 accept 循环决定；库提供 `accept` / `local` 两个动作。
- 每个 accept 成功的连接再单独起一个善后线程，监听器与连接各自独立。

### 8.8 为什么由入口决定何时结束

- 入口掌握协议状态与业务状态，只有它知道何时读完了、何时该收尾。
- 库在入口返回后关闭 socket、释放上下文；资源释放与业务收尾由同一个返回动作串起来。
- 库的外部结束动作就是入口返回：阻塞中的调用按 `timeOut_us` 返回，入口在 `TIMEOUT` 后检查自己的退出条件。
- 无限等待是显式选择，调用方承担线程长期停留在阻塞操作中的后果。

### 8.9 为什么所有状态都留在线程内

- 创建方拿到的只有结果，`ops` / `ctx` 只交给入口，入口返回即作废。
- 上下文由善后线程私有，线程结束即释放，生命周期只有一个端点。
- 登记、查找、引用计数、全局锁全部省去，`init` / `deinit` 只做平台准备。

代价是有效期判断与 deinit 前的存活确认由调用方负责，这与「线程自己负责」的模型一致。

### 8.10 为什么阻塞模型与一线程一连接匹配

- 阻塞的影响范围限于该对象的善后线程：一条连接停在 `recv` 或 `send`，其它连接、监听器与业务线程照常运行。
- 每次等待都有上限（`timeOut_us`）；入口可以在 `TIMEOUT` 后检查退出条件，也可以用协作线程拆分收发。
- 代码按协议顺序书写：读、判断、写都是普通控制流，调试器里的调用栈就是业务栈。
- 空闲时线程停在内核等待，CPU 占用低；代价是每连接一份线程栈与内核资源，连接规模由线程预算决定。
- 一连接一个善后线程描述的是**资源归属**；入口内部可以有一个读线程与一个写线程同时访问连接（5.6）。
- 这与设计稿「用户提供网络线程」一致：线程就是连接，阻塞就是等待自己的对端。

### 8.11 为什么 DNS 是独立操作

- 地址获取与连接是两种操作：DNS 有自己的延迟与多结果；连接只面对一个确定的地址。
- 独立接口让职责清晰：`ccnet_dns` 负责地址，`ccnet_tcp_connect` 负责握手。
- 多地址结果交给调用方：先试哪个、失败是否回退、是否缓存，都由业务决定；创建接口保持单一动作。
- 地址是值类型，解析结果可以缓存、比较、打印；同一地址可以复用多次连接。
- `ccnet_addr_parse` 覆盖 IP 字面量与通配地址，`ccnet_dns` 覆盖主机名，二者产出同一种 `CCNetAddr`。

### 8.12 为什么截断按整包丢弃处理

- UDP 数据报是一条完整消息；截断后只剩部分字节，前 `cap` 字节只对特定协议有意义，通用传输层拿不到解释它的依据。
- 返回前 `cap` 字节要多一个出参和一个「部分数据」分支，调用方容易把这段部分数据当成完整消息；整包丢弃让截断只有一个结果：错误。
- 需要看头部的协议，按协议上限分配接收缓冲区；需要 peek 时使用独立的 peek 接口，与 `recvfrom` 分开。
- Windows 与 POSIX 对截断的原生行为各有差异；统一成「错误 + 丢弃」后，跨平台语义一致。

### 8.13 为什么默认开启 keepalive

- 对端断电、断网、NAT 老化都不会发 FIN / RST；只靠 `recv` 超时只能知道“本轮没数据”，区分不了空闲与半开。
- keepalive 由内核在空闲连接上发探测，不进入应用数据流；默认 60 秒 / 10 秒 / 6 次让半开连接在约 120 秒内被内核判死，代价是空闲连接上的少量探测包。
- 用户拿不到 socket，无法自行 `setsockopt`；默认关闭等于把半开检测完全留给应用层心跳，而心跳只能覆盖主动发消息的协议。
- 需要更快检测时，应用层心跳仍可与 keepalive 并用；keepalive 不改变 `send` / `recv` 的语义，也不替代业务超时。

---

## 9. 验收要点

以下条目是验收依据；自动用例见 `docs/tests/ccnet.md`，跨机连通与真实域名解析保留为人工确认项。

- **接口面：** 三张操作表只包含 `accept` / `local` / `state` / `peer` / `send` / `recv` / `sendto` / `recvfrom`。
- **生命周期：** 入口直接返回后 socket 关闭、上下文释放；线程退出后资源回落；结束动作只有入口返回。
- **ctx 有效期：** 入口内 `ctx` 可用；入口返回后对 `ops` 的调用属于未定义行为，验收只覆盖“入口返回前协作线程已停止”的正确用法。
- **协作线程：** 入口内一个读线程 + 一个写线程同时收发，数据正确；同方向调用由用户串行化；入口返回前两个线程都已结束。
- **多客户端：** 8 个客户端并发连接同一监听器（full 级别 32 个），各连接回显不串线；监听器 accept 循环与连接线程正常收尾。
- **RST：** 对端以 `SO_LINGER = 0` 关闭后，库侧 `recv` 返回 `RESET`，状态进入 `FAILED`，后续收发返回 `STATE`。
- **keepalive：** `connect` 与 `accept` 的连接默认开启；空闲 60 秒后探测，间隔 10 秒，连续 6 次无响应判死；基础开关或平台提供的细化参数设置失败时 setup 失败；Windows 探测次数由系统决定。
- **connect：** 本机 `listen(127.0.0.1:0)` 取实际端口，`connect` 后入口开始执行；未监听端口返回 `REFUSED`；`addr.port == 0` 返回 `PARAM`；`addr.family` 非 4/6 返回 `PARAM`。
- **多地址回退：** 对同一主机的 `dns` 结果逐个 `connect`，首个成功即停止；全部失败时调用方拿到最后一次的错误码。
- **地址构造与文本：** `parse("127.0.0.1", 9000)` 得到 IPv4、端口 9000、字节 `7F 00 00 01`；`parse("::1", 9000)` 得到 IPv6；`parse("example.com", 9000)` 返回 `CC_NET_ERR_DNS`；`text` 与 `parse` 往返一致；IPv6 带 `%scope` 时 `text` 输出 `%N`。
- **IPv6 scope：** `parse("[fe80::1%3]", 9000)` 的 `scope == 3`；`parse("[fe80::1%lo]", 9000)` 通过接口名解析；`scope == 0` 的链路本地地址在连接时由平台决定行为；`text` 对 `scope != 0` 输出 `%3`。
- **DNS：** `dns("localhost", 9000, arr, 8)` 返回 ≥1，端口为 9000；字面量直接返回 1 条；解析失败返回 `CC_NET_ERR_DNS`；`cap` 为 2 时最多写入 2 条；`cap` 超过 `INT32_MAX` 返回 `PARAM`。
- **TCP 发送单次：** 大块数据在慢读对端下首次 `send` 返回 `0 < n < size`，调用方从 `n` 继续；`size == 0` 返回 `0`；`timeOut_us` 内未发出任何字节时返回 `TIMEOUT`。
- **TCP 接收：** 一次 `recv` 可能小于发送块；对端两次 `send` 可能被一次 `recv` 读出；`TIMEOUT` 后连接可用；`0` 表示 FIN，状态 `PEER_CLOSED`；RST 返回 `RESET` 且状态 `FAILED`，后续收发返回 `STATE`。
- **超时：** `recv` 在 `timeOut_us = 500000` 内未收到数据时约 500 ms 返回 `TIMEOUT`；`0` 使用 `CC_NET_DEFAULT_TIMEOUT_US`；`CC_NET_TIMEOUT_INFINITE` 让调用一直等待，由对端活动或错误结束。
- **状态：** TCP 的 `state` 写入输出并返回 `0`；`out == NULL` 返回 `PARAM`；UDP 操作表没有 `state`；`PEER_CLOSED` 只在 TCP FIN 后出现。
- **UDP：** 绑 `0.0.0.0:0` 后 `local` 取实际端口；零长度数据报合法；截断返回 `TRUNCATED` 且整包丢弃；超长包返回 `TOOBIG`；多对端共用一个端点。
- **错误码：** 内存分配失败返回 `NOMEM`；fd 耗尽返回 `RESOURCE`；连接被拒、主机不可达、端口占用、解析失败分别返回 `REFUSED` / `UNREACH` / `ADDRINUSE` / `CC_NET_ERR_DNS`。
- **绑定：** `parse("0.0.0.0", 0)` 仅监听 IPv4；`parse("::", 0)` 仅监听 IPv6（`V6ONLY = 1`）；端口冲突返回 `ADDRINUSE`；backlog 请求值为 128。
- **初始化：** `init` / `deinit` 各调用一次；重复调用为空操作；`deinit` 前所有善后线程已退出；Windows 上 `WSAStartup` / `WSACleanup` 顺序正确。
- **压力（`--level full`）：** 大量短连接建立与入口返回；长时间空闲连接后由入口返回结束；反复创建后内存与线程数回落。

