# cc_rbt_core

红黑树内核被设计为高度解耦、易于客制化的状态，其只负责形而上的红黑树形态逻辑部分。涉及节点具体结构的部分由用户自行维护，并需提供标准操作接口。

## 1.接口列表

```C
// 插入节点
CCINT32 cc_rbt_core_insert(CC_RbtOps *ops, void *root, void *node);

// 移除节点
void* cc_rbt_core_remove(CC_RbtOps *ops, void *root, CCUINTPTR key);

// 检索节点
void* cc_rbt_core_get(CC_RbtOps *ops, void *root, CCUINTPTR key);

// 遍历树（中序遍历回调）
CCINT32 cc_rbt_core_inorder_tranverse(CC_RbtOps *ops, void *root, void *user, CCDataCallback cbk);
```

**参数含义：**
- `CC_RbtOps`：操作接口组
- `root`：指向根节点的指针（黑盒）
- `node`：指向待插入节点的指针（黑盒）
- `key`：节点键值，和指针等宽的整型
- `cbk`：遍历时提供操作的回调函数

**返回值：**
- **cc_rbt_core_insert：**
`0`插入成功；`1`键值重复（树未发生变动）；`-1`参数非法（`ops`或`node`为空）。
- **cc_rbt_core_remove：**
成功时返回被物理摘除的节点指针，由调用方负责回收；未检索到对应`key`、`ops`为空、或无法取得/拷贝替换节点时返回`NULL`。
- **cc_rbt_core_get：**
命中时返回对应节点指针；未命中或`ops`为空时返回`NULL`。
- **cc_rbt_core_inorder_tranverse：**
正常遍历完毕返回`0`；`cbk`返回非`0`时立即中断并透传该值；参数非法（`ops`或`cbk`为空）返回`-1`。

**注意事项：**
- `root`为值传递，内核无法改写调用方持有的根指针；根节点一旦变化（首次插入、旋转、删除根等），内核会调用`ch_root`通知，调用方必须在该回调中更新自身的根指针。
- 当被删节点拥有两个子节点时，内核使用`find_instead`取得替换节点、`copy_node`拷贝数据，随后物理摘除替换节点；`cc_rbt_core_remove`返回该替换节点。如果想要获取检索到的节点（以使用其中数据），请使用`cc_rbt_core_get`。
- `set_node`只修改`node_1`的对应挂载点的指针，不修改`node_2`。

## 2. CC_RbtOps

### 2.1结构体定义

```C
// 红黑树节点关系枚举
typedef enum cc_rbt_relation {
    CC_RBT_REL_LEFT,    // 左子节点
    CC_RBT_REL_RIGHT,   // 右子节点
    CC_RBT_REL_PARENT   // 父节点
} CC_RbtRel;

// 提供对红黑树完备操作的接口的结构体；
// 应当能够仅依赖本结构体提供的接口，
// 就可以根据需求对合法红黑树达成任意合法变动。
typedef struct cc_rbt_ops {
    // 用户自定义上下文
    void *user;
    // 键值对比
    CCINT32 (*cmpare_key)(void *user, void *node, CCUINTPTR key);
    // 节点键值对比
    CCINT32 (*cmpare_node)(void *user, void *node_1, void *node_2);
    // 拷贝节点
    CCINT32 (*copy_node)(void *user, void *dst_node, void *src_node);
    // 如有必要，寻找删除时的替换节点
    void* (*find_instead)(void *user, void *node);

    // 获取和节点对应关系的另一节点
    void* (*get_node)(void *user, void *node, CC_RbtRel relation);
    // 设置对应关系的节点挂载
    CCUINT32 (*set_node)(void *user, void *node_1, void *node_2, CC_RbtRel relation);

    // 获取节点颜色
    CCBOOL (*get_color)(void *user, void *node);
    // 设置节点颜色
    CCINT32 (*set_color)(void *user, void *node, CCBOOL is_red);

    // 左旋
    CCINT32 (*left_rotate)(void *user, void *node);
    // 右旋
    CCINT32 (*right_rotate)(void *user, void* node);

    // 通知调用方根节点已变为新节点
    void (*ch_root)(void *user, void *new_root);
} CC_RbtOps;
```

**compare_key：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。
- `key`：待对比的键值。

返回值：
- `0`：`node`键值等于`key`。
- `1`：`node`键值大于`key`。
- `-1`：`node`键值小于`key`。

**compare_node：**
参数：
- `user`：用户自定义上下文。
- `node_1`：参考节点1。
- `node_2`：参考节点2。

返回值：
- `0`：`node_1`键值等于`node_2`键值。
- `1`：`node_1`键值大于`node_2`键值。
- `-1`：`node_1`键值小于`node_2`键值。

**copy_node：**
参数：
- `user`：用户自定义上下文。
- `dst_node`：拷贝的目标节点。
- `src_node`：拷贝数据来源节点。

返回值：
- `0`：拷贝成功。
- 其他：拷贝失败。

**find_instead：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。

返回值：
- `NULL`：寻找替换节点失败。
- 有效指针：可用于替换的节点。

**get_node：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。
- `relation`：和参考节点的关系。

返回值：
- `NULL`：对应关系的节点为空。
- 有效指针：对应关系的节点。

**set_node：**
参数：
- `user`：用户自定义上下文。
- `node_1`：参考节点。
- `node_2`：待挂载节点（可为`NULL`）。
- `relation`：`node_2`和`node_1`的关系。

返回值：
- `0`：设置对应关系点位的挂载成功。
- 其他：设置对应关系点位的挂载失败。

**get_color：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。

返回值：
- `CCTRUE`：参考节点为红色节点。
- `CCFALSE`：参考姐弟啊那位黑色节点或参考节点不存在。

**set_color：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。
- `is_red`：染色是否为红色。

返回值：
- `0`：节点染色成功（依据`is_red`）。
- 其他：节点染色失败。

**left_rotate：**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。

返回值：
- `0`：以参考节点为初始挂点左旋成功。
- 其他：以参考节点为初始挂点左旋失败。

**right_rotate:**
参数：
- `user`：用户自定义上下文。
- `node`：参考节点。

返回值：
- `0`：以参考节点为初始挂点右旋成功。
- 其他：以参考节点为初始挂点右旋失败。

**ch_root：**
参数：
- `user`：用户自定义上下文。
- `new_root`：新的根节点。

返回值：无