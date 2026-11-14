# wildcard

## 函数签名

```C
CCBOOL cc_wildcard(const char *pattern, const char *str);
```

参数列表：
- `pattern`：通配符模式，支持 `*`（任意长度，含空）与 `?`（任意单字节）。
- `str`：待匹配字符串。

返回值：`CCTRUE` 匹配 / `CCFALSE` 不匹配。

## 匹配规则

- `*` 匹配任意长度字节序列（含空）；`?` 匹配任意单字节；
- 逐字节比较、区分大小写，对 UTF-8 字节流同样适用。

## 实现

DP 滚动数组：时间 O(m·n)、空间 O(n)。

## 错误处理

**`pattern` 或 `str` 为 NULL：** 返回 `CCFALSE`。
