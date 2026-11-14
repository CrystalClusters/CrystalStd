# 动态数组内核测试用例

覆盖 `CCDyn` 的 push/pop、set/get/seek、扩容/缩容和比特读写。被测接口见 [cc_dyn_core](../cc_dyn_core.md)。

## DYN-EMPTY-001 空数组边界

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：pop、get、set、seek 一个不存在的元素。
- 预期：pop/get/set 返回 1（越界）；宽松 seek 返回 0，输出缓冲填 0。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-EMPTY-001")`

## DYN-PARAM-001 非法参数不改变结构

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：对 push/pop/get/set/seek/bitin/bitout 传入空结构、空数据、
  零对齐、负 size 等非法参数。
- 预期：返回 -1；结构体保持 `array == NULL && size == 0`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-PARAM-001")`

## DYN-PUSH-001 基本 push/pop

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：依次 push 10..50；pop 两次。
- 预期：push 后 size 为 5 且内容正确；pop 取回 50、40；size 依次为 4、3。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-PUSH-001")`

## DYN-ALIGN-001 改变对齐时 push

- 状态：已实现
- 前置：空 `CCDyn`；先按 1 字节对齐 push 3 字节。
- 步骤：再按 4 字节对齐 push 4 字节 `{0x44,0x55,0x66,0x77}`。
- 预期：size 变为 8；中间第 4 字节补 0；后续数据正确。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-ALIGN-001")`

## DYN-ALIGN-002 改变对齐时 pop

- 状态：已实现
- 前置：按 1 字节对齐 push 3 字节。
- 步骤：按 4 字节对齐 pop。
- 预期：返回 0；输出前 3 字节为原数据，第 4 字节为 0；size 归零。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-ALIGN-002")`

## DYN-SEEK-001 set/get/seek 基本语义

- 状态：已实现
- 前置：push 4 个 4 字节元素。
- 步骤：set 下标 1；get 下标 1（取走并删除）；seek 前移后的下标 1。
- 预期：set 后内容替换且 size 不变；get 取回替换值且 size 减 4；
  原下标 2 前移到下标 1；seek 不改变 size。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-SEEK-001")`

## DYN-SHIFT-001 取走中间元素后的前移

- 状态：已实现
- 前置：push `[0,1,2,3,4]`。
- 步骤：get 下标 2。
- 预期：取回 2；size 为 4；剩余为 `[0,1,3,4]`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-SHIFT-001")`

## DYN-MULTI-001 一次取走多个连续元素

- 状态：已实现
- 前置：push `[0..7]`。
- 步骤：以 `size=1, N=4` 一次 get 下标 0。
- 预期：取回 `[0,1,2,3]`；size 为 4；剩余 `[4,5,6,7]`。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-MULTI-001")`

## DYN-OOB-001 set/get 越界

- 状态：已实现
- 前置：push `[0,1,2]`。
- 步骤：set/get 下标 5；set 下标 3；set 下标 2。
- 预期：下标 5/3 返回 1 且 size 不变；下标 2 成功。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-OOB-001")`

## DYN-OOB-002 seek 越界

- 状态：已实现
- 前置：push `[1,2,3]`。
- 步骤：从下标 5 seek 1 字节；从下标 2 seek 4 字节。
- 预期：越界 seek 返回 0，输出填 0，size 不变；
  部分越界时有效部分读取，其余填 0。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-OOB-002")`

## DYN-GROW-001 连续 push 触发扩容

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：连续 push 100 个 4 字节元素；逐个 seek 校验。
- 预期：全部成功；size 为 400；capacity 不小于 size；内容保持正确。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-GROW-001")`

## DYN-SHRINK-001 pop 触发缩容

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：push 200 字节，容量增长到 256；pop 到 63、31、15、0。
- 预期：跌破 1/4 时容量依次减半到 128、64；容量降到
  `CC_MEM_ALIGN_MIN` 后不再缩容；有效数据完整保留。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-SHRINK-001")`

## DYN-SHRINK-002 get 触发缩容

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：push 200 字节；从下标 0 反复 get 删除到 63、31。
- 预期：容量依次减半到 128、64。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-SHRINK-002")`

## DYN-BITS-001 LSB-first 比特写入/读取

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：写入 3 比特 `0x05`；读取 3 比特；在末尾追加 1 比特。
- 预期：3 比特占 1 字节，字节值为 `0x05`；读回一致；
  追加后 size 为 2，第二字节 bit0 为 1。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-BITS-001")`

## DYN-BITS-002 中间插入比特

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：写入 8 个 1；在下标 4 处插入 4 个 0；读取 12 比特。
- 预期：插入后 size 为 2，字节为 `0x0F, 0x0F`；读回一致。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-BITS-002")`

## DYN-BITS-003 比特越界

- 状态：已实现
- 前置：空 `CCDyn`。
- 步骤：len 为 0 的 bitin/bitout；起点超过已定义比特；读取范围越界；
  读完整个字节。
- 预期：len 0 返回 0；起点越界返回 1；范围越界返回 1；完整读取返回 0。
- 真值来源：规范。
- 平台：all
- 级别：quick
- 代码：`tests/dyn_core.c` · `CC_CASE("DYN-BITS-003")`
