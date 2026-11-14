# cc_dyn_core

动态数组内核被设计为高度解耦、客制化结构，使用方全凭自觉，动态数组内核并不存储任何实际数据。

## 1.接口列表

```C
// 压入元素
CCINT32 cc_dyn_core_push( CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data);

// 弹出元素
CCINT32 cc_dyn_core_pop(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data);

// 设置元素
CCINT32 cc_dyn_core_set(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

// 取出元素
CCINT32 cc_dyn_core_get(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

// 查询元素
CCINT32 cc_dyn_core_seek(CCDyn *dyn, CCUINT32 align, CCINT32 size, void *data, CCUINT32 index);

/**
 * 下为亚字节操作
 */

// 比特插入
CCINT32 cc_dyn_core_bitin(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len);

// 比特查询
CCINT32 cc_dyn_core_bitout(CCDyn *dyn, void *data, CCUINT32 begin, CCUINT32 len);

```
**参数含义：**
- `dyn`：指向动态数组结构体的指针
- `align`：对齐粒度（字节）
- `size`：数据大小（字节）
- `data`：指向数据存储位置的指针，用于用户和内核交换数据
- `index`：下标，配合align、size定位操作区域
- `begin`：比特开始的位置（将字节折算为比特要乘八）
- `len`：比特长度

**返回值：**

范式：`0` 表示成功，`-1` 表示参数非法（如 `dyn`/`data` 为空、`align` 为 0、`size` 为负），`1` 表示业务失败。

- **cc_dyn_core_push：**
成功返回 0；容量不足且扩容失败、或长度溢出返回 1；参数非法返回 -1。
- **cc_dyn_core_pop：**
成功返回 0；数组为空返回 1；参数非法返回 -1。
- **cc_dyn_core_set：**
成功返回 0；`align * index + size` 超出有效数据（越界）返回 1；参数非法返回 -1。
- **cc_dyn_core_get：**
成功返回 0；越界返回 1；参数非法返回 -1。
- **cc_dyn_core_seek：**
恒返回 0（越界部分填充 0，不报错）；仅在参数非法时返回 -1。
- **cc_dyn_core_bitin：**
成功返回 0（`len` 为 0 亦成功）；`begin` 超出已定义比特范围、长度溢出或扩容失败返回 1；参数非法返回 -1。
- **cc_dyn_core_bitout：**
成功返回 0；读取范围超出已定义比特返回 1；参数非法返回 -1。

**注意事项：**
- 比特流跨字节时，比特的低位放在字节的低位，低位的字节下标更低。
- 每次插入、弹出、置位、查询数组中数据时，都可以指定任意的对齐粒度和下标，其中定位字节计算为：对齐 * 下标（align * index）。对齐（align）的单位为字节。
- 未定义数据默认为 0，例如 align、size 为 1 时在空白动态数组插入三个元素，然后改为 align、size 为 4 插入一个元素，最终会有8个字节的有效数据，第 4 个字节确定为 0。在空白动态数组 align、size 为 1 时插入三个元素，以 size、align 为 4 弹出下标 0 的元素，弹出结果的第四个字节确定为 0。
- align 和 size 之间没有必然联系，align * index 定位到字节，然后以 size 为准计算操作的字节范围。size 可以任取。
- 当 set/get 越界时，例如数组有效大小只有 3，尝试在 5 下标 set/get，将失败。
- 当 get 到中间元素时，会将尾部元素内存向前平移，并更新 size。例如有 5 个元素：[0, 1, 2, 3, 4]，取走下标为 2 的元素，动态数组将更新为：[0, 1, 3, 4]。
- 动态数组不知道用户的数据有多大，假如用户需要一次取走 N 个连续元素，只需假装这几个元素是一个元素，然后用 size * N 来取即可。
- seek为宽松查询，越界返回 0，无报错（用户可以自己根据 CCDyn 内 size 信息计算有没有越界）。

## 2. 容量策略（扩容与滞回缩容）

动态数组的 `capacity` 随使用量自动调整，采用**滞回（hysteresis）**策略，避免在容量临界点频繁分配/释放引起抖动：

- **扩容**：当 `push` / `bitin` 需要的容量超过 `capacity` 时，容量从 `8` 起步按需翻倍（`8 → 16 → 32 → …`）。即使用量达到容量上限才翻倍。
- **缩容**：`pop` / `get` 删除元素后，若使用量（`size`）**不足当前容量（`capacity`）的 1/4**，则容量**减半**。
- **缩容下限（对齐字节数）**：当 `capacity` **小于或等于对齐字节数**时不再缩容。对齐字节数取 ccalloc 的 `CC_MEM_ALIGN_MIN`（默认 64 字节，见 `include/ccalloc.h`），便于潜在的统一修改。

由此形成 `[1/4, 1]` 的滞回区间：使用率在 1/4 到满之间时容量保持不变，只有跌破 1/4 才缩容、达到上限才扩容。缩容复用 `ccalloc` 的 realloc 语义原地完成（指针不变），失败时保持原状、不影响数据正确性。缩容前的有效数据（前 `size` 字节）完整保留。

## 3. 结构体定义

```C
// 动态数组结构体
// 用户直接持有
typedef struct cc_dyn {
    void *array;        // 内存块
    CCUINT32 size;      // 数据大小
    CCUINT32 capacity;  // 实际容量
}CCDyn;
```