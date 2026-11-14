# 统一文件管理内核测试用例

覆盖 `ccfiles` 的探针、路径、挂载、读写落盘、目录检索、cp/mv/rm、资源包和事务。
被测接口见 [ccfiles](../ccfiles.md)。

VFS 物理副作用优先在测试进程内用 `fopen` / `fread` / `stat` / `_stat` 读取真实磁盘；
资源包按 `src/ccfiles_internal.h` 的格式用宿主文件接口独立解析；只有创建目录链接
这类宿主 C 接口无法完成的操作才调用系统 shell。

## FS-API-001 探针、根目录与基础读写落盘

- 状态：已实现
- 前置：`out/ccfiles_test_tmp` 为空目录；创建探针。
- 步骤：检查根目录；`chdir("/")`、`getcwd`；把临时目录挂载到 `/m`；
  拒绝反斜杠路径和 Windows 段尾空格；创建 `/m/a.txt`，写入 `hello`；
  `stat`；读回；越界读；`flush`；用宿主文件接口核对磁盘。
- 预期：根为目录且非只读；`getcwd` 为 `/`；挂载成功；非法路径返回
  `CC_FS_ERR_PARAM`；写入 5 字节；`stat.size == 5`；读回内容一致；
  越界读返回 0；`flush` 成功；磁盘内容为 `hello`，`flush` 后 `stat.size` 仍为 5。
- 真值来源：规范 + 宿主文件内容。
- 平台：all（Windows 段尾空格检查仅 Windows）
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-001")`

## FS-API-002 跳写扩展与中间补零

- 状态：已实现
- 前置：`/m` 已挂载；`/m/a.txt` 已存在。
- 步骤：创建 `/m/b.bin`；在偏移 4 写入 `XY`；`stat`；读回前 6 字节。
- 预期：写入 2 字节；`stat.size == 6`；前 4 字节为 0，第 5/6 字节为 `X`/`Y`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-002")`

## FS-API-003 refresh、子目录与 list/find

- 状态：已实现
- 前置：`/m` 已挂载；宿主创建 `/m/sub` 目录。
- 步骤：`refresh("/m")`；`stat("/m/sub")`；创建 `/m/sub/c.txt` 并写入 `abc`；
  `flush`；`list("/m")`；`find("/m", "*.txt")`。
- 预期：刷新后发现 `sub` 为目录；子目录文件落盘为 `abc`；
  `list("/m")` 的条目顺序、类型、大小精确为 `a.txt:2:5;b.bin:2:6;sub:1:0;`；
  `find("/m", "*.txt")` 递归命中 `a.txt` 与 `sub/c.txt`，名称集合精确为
  `a.txt;c.txt;`。
- 真值来源：规范 + 宿主文件内容。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-003")`

## FS-API-004 mkdir、chdir 与 `..` 语义

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建 `/m/newdir`；检查宿主目录和 `stat`；重复创建；在已存在文件上 `mkdir`；
  在不存在的父目录下 `mkdir`；`chdir` 进入新目录并检查 `getcwd`；
  从 `/m/newdir` 返回 `/m`；从 `/m` 执行 `..`；在根执行 `..`；
  创建 `/m/newdir/f.txt` 并写入。
- 预期：目录立即在宿主存在；重复创建/已存在文件返回 `CC_FS_ERR_EXIST`；
  父目录缺失返回 `CC_FS_ERR_NOENT`；`getcwd` 正确；`..` 按 shell 语义回到上级，
  根目录停留于 `/`；新目录文件自动创建物理路径并落盘。
- 真值来源：规范 + 宿主目录/文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-004")`

## FS-API-005 文件 cp/mv

- 状态：已实现
- 前置：`/m/a.txt` 内容为 `hello`；`/m/sub` 存在。
- 步骤：`cp("/m/a.txt", "/m/sub/d.txt")`；打开并读回复制目标；
  `mv("/m/sub/d.txt", "/m/e.txt")`；`stat` 目标和源。
- 预期：复制返回 `CC_FS_OK`，读回 `hello`，宿主 `sub/d.txt` 内容也为
  `hello`；移动后目标存在、源返回 `CC_FS_ERR_NOENT`。
- 真值来源：规范 + VFS 读回 + 宿主文件内容。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-005")`

## FS-PKG-001 打包、解包与包挂载

- 状态：已实现
- 前置：`/m/sub/c.txt` 内容为 `abc`。
- 步骤：`pack("/m/sub", pkg)`；创建 `/m/pkg.pkg` 节点；解包到宿主目录；
  `mount("/pkg", pkg, PKG)`；`chdir` 和 `..`；`stat`、`open`、`read`；
  尝试写入和创建目录；卸载。
- 预期：打包/解包成功；按内部格式独立解析包，`file_count == 1`、
  `block_count == 1`、`data_size == 3`、`header_crc` 与块表一致，
  块名为 `c.txt`、数据为 `abc`；解包文件内容为 `abc`；包挂载成功；
  包内文件只读，`size == 3` 且 `crc32` 与块头一致；包内 `list` 为
  `c.txt:2:3;`；读取内容为 `abc`；写入和 `mkdir` 返回
  `CC_FS_ERR_PROTECT`；卸载成功。
- 真值来源：规范 + 宿主文件内容 + 包内读回。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-PKG-001")`

## FS-PKG-002 损坏资源包与解包原子性

- 状态：已实现
- 前置：`FS-PKG-001` 已生成有效包。
- 步骤：读取包原始字节，检查魔数；分别构造 header_crc 错误、截断、
  数据偏移越界、名字含 `..`、名字段尾空格、Windows 非法块名等样本；
  挂载或解包；检查目标目录和 `.part` staging。
- 预期：坏包挂载/解包返回 `CC_FS_ERR_PARAM`（Windows 写入阶段失败返回
  `CC_FS_ERR_IO`）；目标目录未发布；`.part` staging 已清理；
  staging 目录保持不存在。
- 真值来源：规范 + 宿主目录/文件 + 包字节（按内部格式解析）。
- 平台：all（Windows 非法块名子用例仅 Windows）
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-PKG-002")`

## FS-API-006 常用错误码

- 状态：已实现
- 前置：`/m` 已挂载；`/m/a.txt` 存在；`/m/sub` 非空。
- 步骤：`open` 不存在的路径；`open` 目录；`chdir` 文件；`remove` 非空目录。
- 预期：分别返回 `CC_FS_ERR_NOENT`、`CC_FS_ERR_ISDIR`、`CC_FS_ERR_NOTDIR`、
  `CC_FS_ERR_NOTEMPTY`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-006")`

## FS-MV-001 目录 mv

- 状态：已实现
- 前置：`/m/sub` 包含 `c.txt`。
- 步骤：`mv("/m/sub", "/m/sub2")`；检查目标子文件、源、宿主目标/源。
- 预期：移动成功；`/m/sub2/c.txt` 存在；`/m/sub` 返回 `CC_FS_ERR_NOENT`；
  宿主目标文件存在，源物理文件已移走。
- 真值来源：规范 + 宿主文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-MV-001")`

## FS-MV-002 内存文件 mv

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建 `/m/mvmem.txt`，写入 `move-me`；`mv` 到 `/m/mvmem2.txt`；
  检查 `stat`、宿主目标/源。
- 预期：改名后 `size == 7`；宿主目标内容立即可见，源物理文件已删。
- 真值来源：规范 + 宿主文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-MV-002")`

## FS-API-007 删除文件

- 状态：已实现
- 前置：`/m/a.txt` 存在。
- 步骤：`remove("/m/a.txt")`；`stat`。
- 预期：删除成功；`stat` 返回 `CC_FS_ERR_NOENT`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-API-007")`

## FS-MV-003 mv 失败保留目标

- 状态：已实现
- 前置：创建 `/m/mvfail_src.txt` 和 `/m/mvfail_dst.txt`，分别写入 `SRC`/`DST`；
  `flush`；删除宿主源文件。
- 步骤：`mv` 源到目标；检查目标宿主内容；清理节点。
- 预期：`mv` 返回失败；目标数据仍为 `DST`，不能先删再 rename 导致目标丢失。
- 真值来源：规范 + 宿主文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-MV-003")`

## FS-RM-001 rm 后 refresh 不复活

- 状态：已实现
- 前置：`/m/a.txt` 已删除。
- 步骤：检查宿主文件；`refresh("/m")`；再次检查。
- 预期：宿主文件立即消失；刷新后仍不存在。
- 真值来源：规范 + 宿主文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-RM-001")`

## FS-RMRF-001 递归删除非空目录

- 状态：已实现
- 前置：创建 `/m/rtree/a/b.txt` 并写入 `X`。
- 步骤：`remove_recursive("/m/rtree")`；检查 VFS 和宿主。
- 预期：目录节点、宿主文件、宿主目录都立即删除。
- 真值来源：规范 + 宿主文件/目录。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-RMRF-001")`

## FS-LINK-001 目录链接安全

- 状态：已实现
- 前置：创建 `outside_j/sentinel.txt`；拥有创建目录链接的能力。
- 步骤：创建指向 `outside_j` 的 junction/symlink；`refresh`；`stat`；
  `remove_recursive` 链接；检查哨兵文件和链接本身。
- 预期：链接投影为文件项；递归删除只删链接本身；目标目录内 `sentinel.txt`
  仍为 `KEEP`；链接本身已删除。环境不支持链接时记为 `SKIP`。
- 真值来源：规范 + 宿主文件/目录。
- 平台：all（不支持的平台 SKIP）
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-LINK-001")`

## FS-BYTE-001 逐字节写入

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建 `/m/byte.txt`；连续 4096 次写入 1 字节；`stat`；`flush`；
  用宿主文件接口逐字节读取。
- 预期：4096 次写入全部成功；`stat.size == 4096`；磁盘内容逐字节正确。
- 真值来源：规范 + 宿主文件内容。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-BYTE-001")`

## FS-PERSIST-001 mv/rm 立即落盘

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建 `/m/res.txt`，写入 `residue`；`mv` 到 `/m/res2.txt`；
  检查宿主；`remove` 目标；再次检查宿主。
- 预期：未显式 `flush` 时，`mv` 后目标内容已正确、源无残留；
  `rm` 后宿主文件同步删除。
- 真值来源：规范 + 宿主文件。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-PERSIST-001")`

## FS-LONG-001 超长路径全链路

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建 `/long` 及 5 层、每层 20 字节的目录；创建叶子文件并写入 `LONG`；
  `stat`、读回；`list`、`find`；`chdir`/`getcwd`；`pack`；
  挂载包并读回包内长路径文件。
- 预期：逐层创建成功；叶子文件可读写；`list`/`find` 各命中 1 项；
  `getcwd` 还原完整路径；打包和包挂载成功；包内长路径文件可读。
- 真值来源：规范 + VFS 读回。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-LONG-001")`

## FS-TXN-001 覆盖挂载/卸载/刷新事务

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：创建内存目录/文件并保持句柄；用第二个探针打开嵌套文件；
  覆盖挂载 `/m`；检查内存节点和磁盘投影；尝试无效覆盖挂载；
  卸载 `/m`；检查内存节点、常驻句柄、磁盘投影；重新挂载并刷新。
- 预期：覆盖挂载保留内存节点和嵌套句柄，重建磁盘投影；
  无效挂载返回 `CC_FS_ERR_NOENT` 且不改树；卸载保留内存节点和常驻句柄，
  移除磁盘投影；刷新保留内存节点和句柄。
- 真值来源：规范 + 内部节点/句柄状态 + 宿主目录。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-TXN-001")`

## FS-RMRFROOT-001 rm -rf /

- 状态：已实现
- 前置：`/m` 已挂载。
- 步骤：`remove_recursive("/")`；检查根和 `/m`。
- 预期：根仍为目录；`/m` 返回 `CC_FS_ERR_NOENT`；物理内容同步删除。
- 真值来源：规范 + VFS 状态 + 宿主目录。
- 平台：all
- 级别：quick
- 代码：`tests/ccfiles.c` · `CC_CASE("FS-RMRFROOT-001")`
