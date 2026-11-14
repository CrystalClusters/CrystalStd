# cctime 测试用例

覆盖 `cctime`、`ccsleep`、`ccdate` 的基本契约。被测接口见 [cctime](../cctime.md)。

## BASIC-TIME-001 时间、休眠与日期字段

- 状态：已实现
- 前置：无。
- 步骤：
  1. 连续读取 `cctime()`，检查非负与单调不递减；
  2. 依次调用 `ccsleep(0)`、`ccsleep(1000)`、`ccsleep(100000)`，用前后 `cctime()` 差值检查休眠下界与及时返回；
  3. 调用 `ccdate()`，检查年月日、星期、时分秒、毫秒字段范围。
- 预期：
  - `cctime()` 返回值非负，后一次不小于前一次；
  - `ccsleep(0)` 在 50ms 内返回；
  - `ccsleep(1000us)` 实际耗时不少于约 1ms，且不超过 100ms；
  - `ccsleep(100000us)` 实际耗时不少于约 100ms，且不超过 1s；
  - `ccdate()` 的 `year` 在 `[2000, 9999]`，`month` 在 `[1, 12]`，`day` 在 `[1, 31]`，`dweek` 在 `[0, 6]`，`hour` 在 `[0, 23]`，`min`/`sec` 在 `[0, 59]`，`ms` 在 `[0, 999]`。
- 真值来源：规范（字段范围）；休眠下界由测试进程内的高精度时间差检查。
- 平台：all
- 级别：quick
- 代码：`tests/basic.c` · `CC_CASE("BASIC-TIME-001")`
