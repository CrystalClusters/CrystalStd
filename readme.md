# CrystalStd-水晶簇标准库

> 首个版本开发中……

该标准库是专为水晶簇件体系设计的二进制标准解决方案，纯C编写。用于替代对C标准库的引用，增强开发可控性和一致性。此解决方案跨平台，支持amd64、arm64等指令集，Linux、Windows、Android等平台。

# 定位

- 与 CrystalClusters 体系内的软件项目深度绑定，定向优化，不参与耦合。可以当作一个普通的C标准库替代品。
- CrystalStd 无意与 glibc 一类成熟体系竞争性能，请勿直接对比。

# 目录结构

```
docs/        项目文档
include/     公开头文件（对外API）
scripts/     各平台细分构建脚本
src/         源码、内部头文件
tests/       测试用例
tools/       平台辅助工具
out/         构建产物
```

# 构建

运行对应平台的构建脚本：

|平台|脚本|
|---|---|
|Linux|`build.sh`|
|Windows|`build.bat`|
|Android|`build.sh`|

> 注：通常将 Windows 体系归为一类，使用`build.bat`脚本，Windows 外全部归类到 Unix Like 类，使用`build.sh`脚本。

**构建流程：**

根目录下的基础构建脚本运行时会自动检测平台信息，然后运行 scripts 文件夹内对应的细分脚本。

构建过程中源文件收集会借助 CrystalCluster 体系内的一个小工具 CrystalFilter 来实现，使用示例：

```shell
# windows
.\filter.exe -./src -*.c
# linux
./filter.run -./src -*.c
```

该工具的仓库地址：[CrystalFilter](https://github.com/CrystalClusters/CrystalFilter)

**注意：**

> 链接 `libCrystalStd.a` 时需加 `-Wl,--whole-archive ... -Wl,--no-whole-archive`（或 `-Wl,-u,_inner_init_ -Wl,-u,_inner_deinit_`），否则 `crystal_std.c` 的 **自动初始化/逆初始化** 会被归档抽取规则丢弃。详见[开发手册](docs/开发手册.md)「链接注意事项（自动初始化 / 逆初始化）」

**产物：**

```
out/
├── obj/               中间目标文件（.o）
├── libCrystalStd.a    静态库
└── test_runner(.exe)  测试程序
```

# 测试

运行测试程序即可执行全部自测：

```bash
./out/test_runner          # Linux
out\\test_runner.exe       # Windows
```

# 文档

[开发文档](docs/开发手册.md)