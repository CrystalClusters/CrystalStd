# 测试文档

本目录存放 CrystalStd 的逐用例文档。测试架构、`test_harness` 的职责、真值来源
和新增用例流程见 [测试架构设计](测试架构.md)；覆盖与缺陷回归用例的审计见
[覆盖与回归审计](覆盖与回归.md)。

## 逐用例文档

按模块分文件：

- [atoi](atoi.md)
- [crc](crc.md)
- [wildcard](wildcard.md)
- [cctime](cctime.md)
- [color_print](color_print.md)
- [ccalloc](ccalloc.md)
- [cc_dyn_core](cc_dyn_core.md)
- [cc_rbt_core](cc_rbt_core.md)
- [ccthread](ccthread.md)
- [ccfiles](ccfiles.md)

每个文件中的 `## <用例编号> <名称>` 就是一个逐用例条目，条目内写前置、步骤、
预期、真值来源、平台、级别和对应代码。例如 [ccfiles.md](ccfiles.md) 中的
`## FS-API-003 ...` 对应 `tests/ccfiles.c` 中的 `CC_CASE("FS-API-003", ...)`。
人工确认项在 `tests/manual.c`，只在全部自动套件之后执行。

用例编号由人工分配，全局唯一；删除用例后编号留空洞。测试代码用 `CC_CASE`
手写编号，用例只存在于文档条目和测试代码。

## 运行

套件名称由 `tests/test.c` 的套件表维护。查看当前可用套件和选项：

```bash
./run_test.sh --help
```

Windows：

```bat
run_test.bat --help
```

运行：

```bash
./run_test.sh                 # 交互菜单
./run_test.sh all             # 全部自动套件，随后执行人工确认项
./run_test.sh manual          # 只执行人工确认项
./run_test.sh <套件名>         # 单个自动套件
./run_test.sh all --level full --seed 123
```

`all` 在交互终端中会把人工确认项放在全部自动套件之后，每项之间等待回车；
非交互终端跳过人工项并提示。测试结论直接输出到控制台。任一自动用例失败时
进程返回非 0；人工项单独统计。

## 新增用例

1. 先在对应模块文档中新增 `## <编号> <名称>` 条目，写清前置、步骤、预期、
   真值来源、平台、级别；代码位置可以先写“待实现”。
2. 再在 `tests/` 中实现 `CC_CASE("<编号>", "<名称>")` 和 `CC_CHECK_*`。
3. 更新文档中的代码位置和状态。
4. 运行对应套件和 `all`。

详细说明见 [测试架构设计](测试架构.md)。
