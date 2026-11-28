# ccpath 测试用例

覆盖 `ccpath` 的绝对路径规范化、相对路径解析、父目录、basename、拼接与边界行为。
被测接口见 [ccpath](../ccpath.md)。

## PATH-NORM-001 绝对路径规范化

- 状态：已实现
- 前置：无。
- 步骤：分别对 POSIX 根/多斜杠/点段/根上回退，或 Windows 盘符/UNC/盘符相对/扩展长度前缀调用 `ccpath_resolve(path, NULL)`。
- 预期：输出规范绝对路径；根保留一个分隔符；`..` 不越过根；盘符相对与扩展长度前缀返回 `NULL`。
- 真值来源：规范。
- 平台：all（分支平台相关）
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-NORM-001")`

## PATH-ABS-001 相对路径解析

- 状态：已实现
- 前置：无。
- 步骤：以绝对 `base` 解析 `a/../b`；解析已是绝对路径的输入；检查 `ccpath_is_absolute`。
- 预期：相对路径拼接到 base 后规范化；绝对输入直接规范化；根路径为绝对，普通相对路径非绝对。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-ABS-001")`

## PATH-PARENT-001 父目录

- 状态：已实现
- 前置：无。
- 步骤：对根、单段、多段、尾斜杠、盘符、UNC 调用 `ccpath_parent`。
- 预期：返回父目录；根与盘符根返回自身；相对单段返回 `.`；尾斜杠被忽略。
- 真值来源：规范。
- 平台：all（盘符/UNC 仅 Windows）
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-PARENT-001")`

## PATH-BASE-001 basename

- 状态：已实现
- 前置：无。
- 步骤：对多段、根、相对、盘符根、UNC 调用 `ccpath_basename`。
- 预期：返回最后一个组件；根路径返回空串；UNC 根返回 share 名。
- 真值来源：规范。
- 平台：all（盘符/UNC 仅 Windows）
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-BASE-001")`

## PATH-JOIN-001 路径拼接

- 状态：已实现
- 前置：无。
- 步骤：拼接普通路径、带尾斜杠的左段、绝对右段、空段、盘符根。
- 预期：使用单个 `/` 连接；右段绝对时直接返回右段；空段返回另一段；不解析 `.` / `..`。
- 真值来源：规范。
- 平台：all（盘符根仅 Windows）
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-JOIN-001")`

## PATH-EDGE-001 边界与所有权

- 状态：已实现
- 前置：无。
- 步骤：传入 `NULL` / 空串；比较双 `NULL`、单 `NULL`；Windows 大小写比较；POSIX 大小写比较；解析超长路径。
- 预期：非法输入返回 `NULL`；双 `NULL` 相等，单 `NULL` 不相等；Windows 大小写不敏感，POSIX 敏感；超长路径可解析；返回值可由 `ccalloc` 释放。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/ccpath.c` · `CC_CASE("PATH-EDGE-001")`
