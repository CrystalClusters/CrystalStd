# 版本发行说明

每次发版在本文件顶部追加一节。

## v1.1.0（2026-11-28）

- 模块：ccpath、cc_encoding、cchost、cc_hash、ccfiles。
- 变更：新增宿主路径工具、UTF-8/本地编码转换、宿主文件系统 Shell 能力、MD5 摘要接口；ccfiles 增加带深度与相对路径的检索接口、节点真实路径反查接口和 `CC_FS_ERR_NOPHYS` 错误码。
- 文档：新增《代码风格指南》，补充新模块、测试用例与快速入门文档。
- 构建：Debug / Release 编译选项加入 `-fPIC`。

## v1.0.2（2026-11-26）

- 模块：crystal_std。
- 变更：调整条件编译选项。

## v1.0.1（2026-11-17）

- 模块：cc_dyn_core。
- 变更：修复内部可能32位溢出导致绕回的缺陷。

## v1.0.0（2026-11-16）

- 模块：color_print、ccnet。
- 变更：增强color_print功能，添加ccnet网络模块，添加网络demo，文档新增快速入门模块。

## v0.9.0-beta（2026-11-14）

- 版本：v0.9.0-beta，公开测试版。
- 平台：Windows / Linux / Android，amd64 / arm64。
- 模块：atoi、crc、wildcard、color_print、ccalloc、cctime、cc_dyn_core、cc_rbt_core、ccthread、ccfiles。
- 变更：无变更。
