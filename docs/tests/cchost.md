# cchost 测试用例

覆盖 `cchost` 的宿主 stat、整文件读取、目录创建、删除、复制与移动。
被测接口见 [cchost](../cchost.md)。

## HOST-STAT-001 宿主 stat 与整文件读取

- 状态：已实现
- 前置：清理 `out/cchost_test_tmp`。
- 步骤：创建 `root` 与 `root/sub`；写入 `root/a.txt`；查询文件、目录、缺失路径；读取文件；读取缺失文件。
- 预期：文件类型为 `CC_HOST_FILE`，size 为 5，`mtime_ns > 0`；目录类型为 `CC_HOST_DIR`；缺失为 `CC_HOST_MISSING`；读取内容为 `hello`；缺失读取返回 `CC_HOST_ERR_NOENT` 且清空输出。
- 真值来源：规范 + 宿主文件内容。
- 平台：all
- 级别：quick
- 代码：`tests/cchost.c` · `CC_CASE("HOST-STAT-001")`

## HOST-DIR-001 递归目录与冲突

- 状态：已实现
- 前置：`root` 已存在。
- 步骤：重复创建 `root`；在普通文件 `a.txt` 之下创建子目录。
- 预期：重复创建成功；以文件为父目录返回 `CC_HOST_ERR_NOTDIR`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cchost.c` · `CC_CASE("HOST-DIR-001")`

## HOST-COPY-001 复制文件

- 状态：已实现
- 前置：`root/a.txt` 存在。
- 步骤：复制到 `root/b.txt`；查询目标；复制到目录；复制缺失源。
- 预期：复制成功，目标 size 为 5；复制到目录返回 `CC_HOST_ERR_ISDIR`；缺失源返回 `CC_HOST_ERR_NOENT`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cchost.c` · `CC_CASE("HOST-COPY-001")`

## HOST-MOVE-001 移动文件

- 状态：已实现
- 前置：`root/b.txt` 存在。
- 步骤：移动到 `root/sub/c.txt`；查询源和目标；移动到目录。
- 预期：移动成功；源缺失；目标为文件；移动到目录返回 `CC_HOST_ERR_ISDIR`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cchost.c` · `CC_CASE("HOST-MOVE-001")`

## HOST-REMOVE-001 删除文件与目录

- 状态：已实现
- 前置：`root/sub` 非空，`root/a.txt` 存在。
- 步骤：删除非空目录；删除文件；重复删除；删除空目录；删除根目录。
- 预期：非空目录返回 `CC_HOST_ERR_NOTEMPTY`；删除文件成功；重复删除返回 `CC_HOST_ERR_NOENT`；空目录与根目录删除成功。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/cchost.c` · `CC_CASE("HOST-REMOVE-001")`
