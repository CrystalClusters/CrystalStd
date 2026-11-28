# cc_hash 测试用例

覆盖 `cc_hash` 的标准向量、内存与 VFS 一致性、模式优先级、参数判定与 VFS 错误。
被测接口见 [cc_hash](../cc_hash.md)。

## HASH-001 MD5 标准向量

- 状态：已实现
- 前置：无。
- 步骤：对空串、`"abc"`、`"message digest"` 调用内存模式。
- 预期：摘要分别为 `d41d8cd98f00b204e9800998ecf8427e`、`900150983cd24fb0d6963f7d28e17f72`、`f96b697d7cb7938d525a2f31aaf161d0`。
- 真值来源：MD5 标准测试向量。
- 平台：all
- 级别：quick
- 代码：`tests/cc_hash.c` · `CC_CASE("HASH-001")`

## HASH-002 内存与 VFS 一致

- 状态：已实现
- 前置：`out/hash_test_tmp` 可写；创建探针并挂载到 `/m`。
- 步骤：创建 `/m/a.bin` 并写入 `abc`；flush；对 VFS 节点求摘要；对同内容内存求摘要。
- 预期：两者均为 `900150983cd24fb0d6963f7d28e17f72`，逐字节一致。
- 真值来源：MD5 标准测试向量 + 规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_hash.c` · `CC_CASE("HASH-002")`

## HASH-003 data 优先

- 状态：已实现
- 前置：无。
- 步骤：同时提供 `data`、`size` 与无效的 `fs` / `v_path`。
- 预期：按内存模式计算，摘要为 `"abc"` 的标准值。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_hash.c` · `CC_CASE("HASH-003")`

## HASH-004 参数判定

- 状态：已实现
- 前置：无。
- 步骤：空输入；只有 `v_path`；只有 `fs`；空输出指针。
- 预期：均返回 `CC_FS_ERR_PARAM`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_hash.c` · `CC_CASE("HASH-004")`

## HASH-005 VFS 目录与缺失

- 状态：已实现
- 前置：探针挂载 `out` 到 `/m`。
- 步骤：对 `/m` 目录求摘要；对缺失路径求摘要。
- 预期：目录返回 `CC_FS_ERR_ISDIR`；缺失返回 `CC_FS_ERR_NOENT`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_hash.c` · `CC_CASE("HASH-005")`
