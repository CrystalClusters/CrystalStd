# cc_encoding 测试用例

覆盖 `cc_encoding` 的 ASCII/空串、中文编码转换、非法输入与 NULL。
被测接口见 [cc_encoding](../cc_encoding.md)。

## ENC-001 ASCII 与空串

- 状态：已实现
- 前置：无。
- 步骤：`cc_utf8_to_local("abc", CC_LOCAL_SYSTEM)`；反向转换；转换空串。
- 预期：ASCII 原样；空串返回分配的空串。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_encoding.c` · `CC_CASE("ENC-001")`

## ENC-002 中文编码转换

- 状态：已实现
- 前置：无。
- 步骤：对 UTF-8 中文调用 `cc_utf8_to_local(..., CC_LOCAL_SYSTEM)`；Windows 在 ACP=936 时核对 GBK 字节；再转回 UTF-8。POSIX 做恒等核对。
- 预期：Windows 输出 GBK 字节并可回转；POSIX 输入输出内容相同。
- 真值来源：规范。
- 平台：all（字节值仅 Windows ACP=936）
- 级别：quick
- 代码：`tests/cc_encoding.c` · `CC_CASE("ENC-002")`

## ENC-003 非法输入与 NULL

- 状态：已实现
- 前置：无。
- 步骤：传入非法 UTF-8 `FF FE`；传入 `NULL`。
- 预期：Windows 非法 UTF-8 返回 `NULL`；POSIX 原样返回；`NULL` 输入返回 `NULL`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cc_encoding.c` · `CC_CASE("ENC-003")`
