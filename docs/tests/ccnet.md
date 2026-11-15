# ccnet 测试用例

覆盖地址解析、同步 DNS、TCP 连接与收发、UDP 数据报、超时、状态与生命周期。被测接口见 [ccnet](../ccnet.md)。

## NET-ADDR-001 IPv4 / IPv6 地址解析与文本

- 状态：已实现
- 前置：无。
- 步骤：解析 `127.0.0.1:9000` 与 `::1:9000`，检查地址族、端口与原始字节；把 `1.2.3.4:9000` 与
  `[2001:db8::1]:9000` 格式化为文本；用域名文本调用 `ccnet_addr_parse`；用 4 字节缓冲调用
  `ccnet_addr_text`。
- 预期：IPv4 / IPv6 解析成功且字节正确；文本分别为 `1.2.3.4:9000` 与 `[2001:db8::1]:9000`；
  域名文本返回 `CC_NET_ERR_DNS`；小缓冲返回 `CC_NET_ERR_PARAM`。
- 真值来源：规范（`docs/ccnet.md` 2.7、3）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-ADDR-001")`

## NET-ADDR-002 IPv6 scope 解析与文本

- 状态：已实现
- 前置：无。
- 步骤：解析 `[fe80::1%3]:9000`；格式化为文本；解析带 `%3` 的 IPv4；解析未带 scope 的
  `fe80::1`；解析 `%4294967295`、`%4294967296`、`%-1`。
- 预期：scope 解析为 `3`；文本为 `[fe80::1%3]:9000`；IPv4 带 scope 返回 `CC_NET_ERR_PARAM`；
  未指定 scope 的链路本地地址 scope 为 `0`；`4294967295` 接受，`4294967296` 与 `-1` 返回 `CC_NET_ERR_PARAM`。
- 真值来源：规范（`docs/ccnet.md` 2.7、3）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-ADDR-002")`

## NET-DNS-001 同步 DNS 与字面量快速路径

- 状态：已实现
- 前置：系统解析器可用。
- 步骤：解析 `localhost:9000`；解析 `127.0.0.1:9000`；分别传入 `NULL` host、`cap == 0` 与 `cap == 0x80000000`。
- 预期：`localhost` 至少返回 1 个地址且端口为 9000；字面量直接返回 1 条且地址族为 IPv4；
  `NULL`、容量 0 与超过 `INT32_MAX` 的容量返回 `CC_NET_ERR_PARAM`。
- 真值来源：规范（`docs/ccnet.md` 2.7、7.4）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-DNS-001")`

## NET-TCP-001 TCP 回环收发与入口返回清理

- 状态：已实现
- 前置：本机回环可用。
- 步骤：监听 `127.0.0.1:0`，在 accept 入口取实际端口并接受一个连接；客户端入口发送
  `hello` 并读取回显；两端入口返回。
- 预期：连接建立返回 0；回显 5 字节一致；入口返回后连接与监听器由库清理，服务端在
  `recv` 上观察到 `0` 并退出。
- 真值来源：规范（`docs/ccnet.md` 2、4、5.2、5.3）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-001")`

## NET-TCP-002 接收超时与对端 FIN 状态

- 状态：已实现
- 前置：本机回环可用。
- 步骤：服务端连接入口以 100 ms 超时循环 `recv`；客户端入口以 100 ms 超时 `recv` 后直接返回；
  服务端在 `recv` 返回 0 后查询 `state`。
- 预期：客户端观察到 `CC_NET_ERR_TIMEOUT`；客户端入口返回后服务端 `recv` 返回 0；状态为
  `CC_NET_STATE_PEER_CLOSED`。
- 真值来源：规范（`docs/ccnet.md` 5.3、5.7）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-002")`

## NET-TCP-003 一读一写协作线程全双工

- 状态：已实现
- 前置：本机回环可用。
- 步骤：客户端入口创建一个写线程，连续发送 16 条 10 字节消息；入口线程同时 `recv` 回显；
  服务端连接入口持续回显；入口返回前等待写线程结束。
- 预期：收满 160 字节；字节顺序与内容符合发送序列；写线程在入口返回前结束；服务端回显后
  正常退出。
- 真值来源：规范（`docs/ccnet.md` 2.6、5.6）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-003")`

## NET-TCP-004 多客户端并发连接与隔离

- 状态：已实现
- 前置：本机回环可用。
- 步骤：监听 `127.0.0.1:0`，accept 循环连续接受 8 个连接（full 级别 32 个）；每个连接线程独立回显；
  8 个客户端并发连接，各自发送带唯一序号的 4 字节负载并校验回显；全部入口返回后统计服务端 FIN 数。
- 预期：所有连接建立成功；每个客户端回显与自己的序号一致；服务端看到全部连接结束；accept 循环退出。
- 真值来源：规范（`docs/ccnet.md` 2.3、4、5.6）。
- 平台：all
- 级别：quick / full（full 使用 32 个客户端）
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-004")`

## NET-TCP-005 对端 RST 的错误与 FAILED 状态

- 状态：已实现
- 前置：本机回环可用。
- 步骤：服务端监听并接受连接；原生 socket 客户端连接后设置 `SO_LINGER = {1, 0}` 并关闭，制造 RST；
  服务端连接入口在 `recv` 上观察错误并查询状态，再调用一次 `recv`。
- 预期：`recv` 返回 `CC_NET_ERR_RESET`；状态为 `CC_NET_STATE_FAILED`；后续 `recv` 返回
  `CC_NET_ERR_STATE`。
- 真值来源：规范（`docs/ccnet.md` 5.3、5.7）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-005")`

## NET-TCP-006 TCP keepalive 默认开启且参数匹配

- 状态：已实现
- 前置：本机回环可用。
- 步骤：服务端监听并接受一个连接，客户端入口连接；两端入口通过内部上下文读取底层 socket 的
  `SO_KEEPALIVE`；POSIX 平台同时读取 `TCP_KEEPIDLE` / `TCP_KEEPINTVL` / `TCP_KEEPCNT`。
- 预期：两端连接均报告 keepalive 已开启；POSIX 参数分别为 60 秒、10 秒、6 次；Windows 使用
  `SIO_KEEPALIVE_VALS`，测试只核对开关。
- 真值来源：规范（`docs/ccnet.md` 5.9）与 `include/ccnet.h` 的 `CC_NET_KEEPALIVE_*`。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-TCP-006")`（白盒读取 socket 选项）

## NET-UDP-001 UDP 回显

- 状态：已实现
- 前置：本机回环可用。
- 步骤：打开 `127.0.0.1:0` 端点，在入口取实际端口；客户端端点发送 `udp` 并等待回显。
- 预期：`sendto` 返回 3；`recvfrom` 返回 3 且内容一致；端点入口返回后资源释放。
- 真值来源：规范（`docs/ccnet.md` 5.4）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-UDP-001")`

## NET-UDP-002 零长度数据报合法

- 状态：已实现
- 前置：本机回环可用。
- 步骤：客户端端点发送 0 字节数据报；服务端端点接收并回显 0 字节；客户端等待回显。
- 预期：`sendto` 返回 0；服务端 `recvfrom` 返回 0 并置零长度标志；客户端 `recvfrom` 返回 0。
- 真值来源：规范（`docs/ccnet.md` 5.4）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-UDP-002")`

## NET-UDP-003 截断数据报整包丢弃

- 状态：已实现
- 前置：本机回环可用。
- 步骤：服务端入口用 8 字节缓冲调用 `recvfrom`；客户端发送 32 字节数据报。
- 预期：`recvfrom` 返回 `CC_NET_ERR_TRUNCATED`；该数据报不被交付；服务端入口退出。
- 真值来源：规范（`docs/ccnet.md` 5.4、8.12）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-UDP-003")`

## NET-LIFE-001 setup 失败不调用入口

- 状态：已实现
- 前置：本机 `127.0.0.1:1` 无监听。
- 步骤：对 `127.0.0.1:1` 调用 `ccnet_tcp_connect`，入口设置“已调用”标志。
- 预期：创建返回负值；入口未被调用。
- 真值来源：规范（`docs/ccnet.md` 2、4.5）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-LIFE-001")`

## NET-LIFE-002 反复创建与入口返回

- 状态：已实现
- 前置：无。
- 步骤：连续 16 次在 `127.0.0.1:0` 上创建监听器，accept 入口立即返回；短睡眠后结束。
- 预期：每次创建返回 0；入口返回后监听器与线程正常回收；进程无崩溃。
- 真值来源：规范（`docs/ccnet.md` 4）。
- 平台：all
- 级别：quick
- 代码：`tests/ccnet.c` · `CC_CASE("NET-LIFE-002")`
