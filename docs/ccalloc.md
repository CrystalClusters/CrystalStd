# ccalloc

## 函数签名

```C
void* ccalloc(void *old, CCUINT64 size, const char *comment);
const char* get_comment(void *ptr);
```

**参数含义：**
- `old`：旧指针，如果传入`NULL`则新分配内存，可用于调整内存大小。
- `size`：内存大小（字节），传入0则释放内存。
- `comment`：文字注释，可为`NULL`。
- `ptr`：内存首地址（指针），可用于检索对应注释。

**返回值：**
- **ccalloc：**
正常情况下返回新分配或者调整大小后的内存块首地址，如果内存分配、调整失败或者释放内存块，返回`NULL`。
- **get_comment：**
获取该首地址对应内存块的注释，如果是无效的首地址，则返回`NULL`。

**工作模式表：**
|old|size|模式|说明|
|---|---|---|---|
|NULL|＞ 0|分配|新分配 size 字节，返回64字节对齐的用户指针|
|NULL|0|空操作|返回 NULL，不报错|
|非 NULL|0|释放|释放该内存块，非法地址报错且不执行释放|
|非 NULL|＞ 0|调整|调整大小，返回新指针，会拷贝可容纳部分|

