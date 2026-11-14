# crc

## 函数签名

```C
CCUINT32 cc_crc32(const void *data, CCUINT64 size, CCUINT32 seed);
```

参数列表：
- `data`：待计算数据起始地址。
- `size`：数据字节数。
- `seed`：续算种子。首块传 `0`；分块追加时传上一块的返回值。

返回值：32 位 CRC32 校验值。

## 算法

采用 CRC-32（IEEE 802.3）标准参数：

| 参数 | 取值 |
|---|---|
| 多项式（反射形式） | `0xEDB88320` |
| 初始值 | `0xFFFFFFFF` |
| 输入反射 | 是 |
| 输出反射 | 是 |
| 结果异或 | `0xFFFFFFFF` |

分块续算：对同一段数据分成 `A`、`B` 两块顺序计算时，`cc_crc32(B, nb, cc_crc32(A, na, 0))` 与一次性 `cc_crc32(A+B, na+nb, 0)` 结果一致。

## 错误处理

**空指针且 `size` 非 0：**
返回 `0`。

**`size` 为 0：**
返回 `seed`（空数据不改变校验状态；首块 `seed=0` 时即为空数据 CRC = `0`）。
