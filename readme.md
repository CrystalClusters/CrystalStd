# CrystalStd-水晶簇标准库

> v1.1.0 · [版本发行说明](CHANGELOG.md)

该标准库是专为水晶簇件体系设计的二进制标准解决方案，纯C编写。用于替代 crystal clusters 体系内对C标准库的直接引用，增强开发可控性和一致性。此解决方案跨平台，支持amd64、arm64等指令集，Linux、Windows、Android等平台。

快速上手与各模块组的典型用法见 [快速入门](docs/快速入门.md)。

# 定位

- 与 CrystalClusters 体系内的软件项目深度绑定，定向优化，不参与耦合。基础兼容层。
- CrystalStd 无意与 glibc 一类成熟体系竞争性能，请勿直接对比。

# 目录结构

```
CHANGELOG.md 版本发行说明
docs/        项目文档（测试文档在 docs/tests/）
include/     公开头文件（对外API）
scripts/     各平台细分构建脚本
src/         源码、内部头文件
tests/       测试代码（逐用例文档在 docs/tests/）
demo/        交互演示程序
out/         构建产物
```

# 构建

运行对应平台的构建脚本：

|平台|脚本|
|---|---|
|Linux 及其他 Unix Like|`build.sh`|
|Windows|`build.bat`|

> 注：通常将 Windows 体系归为一类，使用`build.bat`脚本，Windows 外全部归类到 Unix Like 类，使用`build.sh`脚本。

默认执行 DEBUG 构建；只有显式传入 `release` 时才启用 RELEASE 构建：

```bash
./build.sh          # DEBUG
./build.sh release  # RELEASE
```

```bat
build.bat
build.bat release
```

RELEASE 使用 `-O2 -DNDEBUG -DCC_RELEASE -Wall -Wextra -Werror`，DEBUG 使用 `-O0 -g3 -Wall -Wextra`；Windows 两种模式均额外追加 `-fexec-charset=GBK`。编译选项由顶层脚本统一传给 `scripts/build_*.sh` / `.bat` 子脚本。

**构建流程：**

根目录下的基础构建脚本运行时会自动检测平台信息，然后运行 scripts 文件夹内对应的细分脚本。

构建过程中源文件收集使用 Shell 原生指令实现：

```shell
# Unix Like / Android：递归收集指定目录下源码（输出形如 ./src/xxx.c）
find ./src -name '*.c'

# Windows：递归收集并逐个处理
for /r src %%s in (*.c) do ...
```

**注意：**

> 链接 `libCrystalStd.a` 时需加 `-Wl,--whole-archive ... -Wl,--no-whole-archive`（或 `-Wl,-u,_inner_init_ -Wl,-u,_inner_deinit_`），否则 `crystal_std.c` 的 **自动初始化/逆初始化** 会被归档抽取规则丢弃。详见[开发手册](docs/开发手册.md)。

**产物：**

```
out/
├── obj/                  中间目标文件（.o）
└── <平台>_bin/           按平台架构命名的产物目录
    ├── libCrystalStd.a           静态库
    ├── test(.exe)                测试程序
    ├── ccfiles_demo(.exe)        虚拟文件系统交互演示
    ├── ccnet_server_demo(.exe)   网络演示服务端
    └── ccnet_client_demo(.exe)   网络演示客户端
```

# 测试

运行测试脚本进入交互菜单：

```bash
./run_test.sh        # Linux
.\run_test.bat       # Windows
```

菜单中 `a` 运行全部自动测试，`m` 运行人工确认项，`q` 退出。人工确认项只在交互
终端中运行，放在全部自动套件之后，每项之间等待回车，避免长输出被控制台缓冲截断。

非交互使用：

```bash
./run_test.sh all             # 全部自动套件，终端中再执行人工确认项
./run_test.sh manual          # 只执行人工确认项
./run_test.sh <套件名>         # 单个自动套件
./run_test.sh all --level full --seed 123
```

套件名称只在 `tests/test.c` 的套件表中维护；查看当前套件和选项：

```bash
./run_test.sh --help
```

`--level quick` 为默认值，会跳过耗时的压力用例；`--level full` 包含压力测试。

逐用例文档在 [docs/tests](docs/tests/README.md)，测试架构见
[测试架构设计](docs/tests/测试架构.md)，覆盖与缺陷回归审计见
[覆盖与回归审计](docs/tests/覆盖与回归.md)。每条用例有唯一的手工编号，
测试代码用 `CC_CASE("<编号>", ...)` 与文档条目对应。测试结论直接输出到控制台。
任一自动用例失败时测试进程以非 0 退出码结束；人工判定项单独统计。

# 演示

演示按 `demo/<group>/main.c` 或 `demo/<group>/<target>/main.c` 分目录存放，构建脚本自动检索，不需要维护源文件清单；空目录不会被识别为演示。同组共享代码放在 `demo/<group>/common/`，磁盘上只有一份，构建时分别编入同组多个目标。

直接运行不带参数的 `run_demo.*` 会列出全部产物并进入选择菜单：输入序号运行对应演示，直接回车、输入 `0` 或 `q` 可不运行直接退出。带参数时直接运行目标，`--list` 只列出产物：

```bash
./run_demo.sh                    # 菜单
./run_demo.sh --list             # 只列出
./run_demo.sh ccfiles
./run_demo.sh ccnet_server
./run_demo.sh ccnet_client
```

```bat
run_demo.bat                     :: 菜单
run_demo.bat --list              :: 只列出
run_demo.bat ccfiles
run_demo.bat ccnet_server
run_demo.bat ccnet_client
```

`ccnet` 演示需要一个服务端窗口和至少一个客户端窗口。服务端用 `listen <port>` 开启监听，客户端用 `connect <host> <port>` 连接；完整命令、消息来源与环形缓冲区语义见 [网络演示文档](docs/ccnet_demo.md)。

`ccfiles` 演示启动后界面保持简洁，输入 `-h` 查看命令帮助，`quit` 退出。

支持通过十六进制字节流写入任意二进制内容，写入直通物理文件（`flush` 可强制立刻可见）：

```text
mount /m /绝对路径/演示目录 dir
mkdir /m/demo
create /m/demo.bin
write -x 0 DE AD BE EF 00 01 02
read -x 0 16
flush
```

> 挂载/打包等命令中的真实路径使用 `/` 分隔；相对路径基于可执行文件同级的 `res` 目录解析。

# 文档

[开发文档](docs/开发手册.md)

[代码风格指南](docs/代码风格指南.md)
