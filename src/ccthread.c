/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: 异步模块实现
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "crystal_std_inner_header.h"
#include <string.h>

#define CCLOCK_MAGIC (CCUINT8)0x3b
typedef struct cclock
{
#ifdef CC_WINDOWS
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
    CCUINT8 magic;
} CCLockInner;

// 空节点下标哨兵。下标 0 是合法节点，因此不能用 0 表示空。
#define CC_THREAD_NIL ((CCUINT32)0xFFFFFFFFu)

/*
 * 线程实体参数：堆上分配，作为线程入口的传参结构体。
 *
 * 设计原因：
 * 1. 线程树节点（CCThreadNode）连续存放在 CCDyn 中，扩容/收缩时会整块搬移地址，
 *    因此绝不能把节点指针直接交给 pthread_create/CreateThread——线程运行期间
 *    该指针可能已经失效。用一个独立于数组、生命周期与线程完全一致的堆对象
 *    来承载入口信息即可规避这一点。
 * 2. 句柄（pthread_t / HANDLE）由创建者在创建成功后写入本结构体，
 *    包装层 _inner_thread_ 通过它即可读到自身句柄与 ID，无需 pthread_self /
 *    GetCurrentThreadId，从而在 Windows 下也能正确拿到「线程自身标识」
 *    （Win32 无法在线程内部直接取得自己的 HANDLE 值）。
 * 3. 线程结束由包装层就地释放本结构体，无需额外回收表。
 */
typedef struct cc_thread_arg
{
    CCThreadEntry entry; // 用户线程入口
    void *userData;      // 用户数据
    CCUINTPTR key;       // 线程 ID（取句柄值 / 线程 ID），位宽统一为 uintptr_t
#ifdef CC_WINDOWS
    HANDLE thread; // Windows 线程句柄
#else
    pthread_t thread; // POSIX 线程句柄
#endif
} CCThreadArg;

/*
 * 线程树节点：纯红黑树结构的载体，连续存放在 CCDyn 中，以「下标」而非指针互指。
 *
 * 设计原因：
 * 1. 用动态数组连续存储可提高缓存命中率；但数组扩容会搬移整块内存，
 *    若节点间用指针互指会全部失效，因此 parent/left/right 一律存下标。
 * 2. entry/userData/thread 等实体信息不放在节点里，而是放在 CCThreadArg 中。
 *    节点只保留 key 与树结构字段，两者通过 key（线程 ID）关联，职责单一、避免冗余。
 * 3. 删除节点时采用「末尾节点搬到空位 + 弹尾」的紧凑策略（见 thread_tree_remove_locked），
 *    使 thread_nodes.size / sizeof(CCThreadNode) 恒等于存活线程数，无需额外计数。
 */
typedef struct cc_thread
{
    CCUINTPTR key;   // 线程 ID，与 CCThreadArg.key 一致，用于树比较/检索
    CCUINT32 parent; // 父节点下标，CC_THREAD_NIL 表示无
    CCUINT32 left;   // 左子节点下标
    CCUINT32 right;  // 右子节点下标
    CCBOOL is_red;   // 是否红节点
} CCThreadNode;

// 内部全局变量
static struct
{
    CCDyn thread_nodes; // 连续存储线程树节点，提高缓存命中率
    CCUINT32 root;      // 根节点下标（不能存指针：数组扩容会搬移地址）
// 异步锁，增减线程实体节点的时候使用
#ifdef CC_WINDOWS
    CRITICAL_SECTION thread_tree_lock;
#else
    pthread_mutex_t thread_tree_lock;
#endif
} ccthread_var;

// 下标 -> 数组内节点地址
static inline CCThreadNode *node_at(CCUINT32 idx)
{
    return (CCThreadNode *)((CCUINT8 *)ccthread_var.thread_nodes.array + (CCUINT64)idx * sizeof(CCThreadNode));
}

// 数组内节点地址 -> 下标
static inline CCUINT32 idx_of(CCThreadNode *node)
{
    return (CCUINT32)(((CCUINT8 *)node - (CCUINT8 *)ccthread_var.thread_nodes.array) / sizeof(CCThreadNode));
}

// 根节点地址（空树返回 NULL），供红黑树内核使用
static inline void *root_node(void)
{
    return ccthread_var.root == CC_THREAD_NIL ? NULL : node_at(ccthread_var.root);
}

/*
 * ============================================================================
 * 红黑树操作集：把「下标索引的连续节点」适配为「指针语义」的 CC_RbtOps 接口
 * ============================================================================
 */

static CCINT32 compare_key(void *user, void *node, CCUINTPTR key)
{
    CC_UNUSED(user);
    CCThreadNode *pInner = node;
    if (pInner->key > key)
        return 1;
    else if (pInner->key < key)
        return -1;
    return 0;
}

static CCINT32 compare_node(void *user, void *node_1, void *node_2)
{
    CC_UNUSED(user);
    CCThreadNode *pInner_1 = node_1;
    CCThreadNode *pInner_2 = node_2;
    if (pInner_1->key > pInner_2->key)
        return 1;
    else if (pInner_1->key < pInner_2->key)
        return -1;
    return 0;
}

// 删除双子节点时，内核会把后继节点的数据拷入被删节点；此处只需拷 key。
// entry/userData/thread 存于 CCThreadArg，随各自线程独立存在，节点仅以 key 参与树组织。
static CCINT32 copy_node(void *user, void *dst_node, void *src_node)
{
    CC_UNUSED(user);
    ((CCThreadNode *)dst_node)->key = ((CCThreadNode *)src_node)->key;
    return 0;
}

// 中序后继，供删除双子节点时替换
static void *find_instead(void *user, void *node)
{
    CC_UNUSED(user);
    CCThreadNode *pInner = node;
    if (pInner->right == CC_THREAD_NIL)
        return NULL;
    CCThreadNode *pCursor = node_at(pInner->right);
    while (pCursor->left != CC_THREAD_NIL)
        pCursor = node_at(pCursor->left);
    return pCursor;
}

static void *get_node(void *user, void *node, CC_RbtRel relation)
{
    CC_UNUSED(user);
    CCThreadNode *pInner = node;
    CCUINT32 idx;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:
        idx = pInner->left;
        break;
    case CC_RBT_REL_RIGHT:
        idx = pInner->right;
        break;
    case CC_RBT_REL_PARENT:
        idx = pInner->parent;
        break;
    default:
        return NULL;
    }
    return idx == CC_THREAD_NIL ? NULL : node_at(idx);
}

static CCUINT32 set_node(void *user, void *node_1, void *node_2, CC_RbtRel relation)
{
    CC_UNUSED(user);
    CCThreadNode *pInner = node_1;
    CCUINT32 idx = node_2 ? idx_of((CCThreadNode *)node_2) : CC_THREAD_NIL;
    switch (relation)
    {
    case CC_RBT_REL_LEFT:
        pInner->left = idx;
        return 0;
    case CC_RBT_REL_RIGHT:
        pInner->right = idx;
        return 0;
    case CC_RBT_REL_PARENT:
        pInner->parent = idx;
        return 0;
    default:
        return 1;
    }
}

static CCBOOL get_color(void *user, void *node)
{
    CC_UNUSED(user);
    return ((CCThreadNode *)node)->is_red;
}

static CCINT32 set_color(void *user, void *node, CCBOOL is_red)
{
    CC_UNUSED(user);
    ((CCThreadNode *)node)->is_red = is_red;
    return 0;
}

// 左旋：仅重排节点间的下标关系；根节点变化由内核检测后经 ch_root 通知
static CCINT32 left_rotate(void *user, void *node)
{
    CC_UNUSED(user);
    CCThreadNode *pX = node;
    if (pX->right == CC_THREAD_NIL)
        return 1;
    CCUINT32 x_idx = idx_of(pX);
    CCUINT32 y_idx = pX->right;
    CCThreadNode *pY = node_at(y_idx);

    // x->right = y->left
    pX->right = pY->left;
    if (pY->left != CC_THREAD_NIL)
        node_at(pY->left)->parent = x_idx;

    // y 顶替 x 的位置
    pY->parent = pX->parent;
    if (pX->parent != CC_THREAD_NIL)
    {
        CCThreadNode *pParent = node_at(pX->parent);
        if (pParent->left == x_idx)
            pParent->left = y_idx;
        else
            pParent->right = y_idx;
    }

    // y->left = x
    pY->left = x_idx;
    pX->parent = y_idx;
    return 0;
}

// 右旋：与左旋对称
static CCINT32 right_rotate(void *user, void *node)
{
    CC_UNUSED(user);
    CCThreadNode *pX = node;
    if (pX->left == CC_THREAD_NIL)
        return 1;
    CCUINT32 x_idx = idx_of(pX);
    CCUINT32 y_idx = pX->left;
    CCThreadNode *pY = node_at(y_idx);

    // x->left = y->right
    pX->left = pY->right;
    if (pY->right != CC_THREAD_NIL)
        node_at(pY->right)->parent = x_idx;

    // y 顶替 x 的位置
    pY->parent = pX->parent;
    if (pX->parent != CC_THREAD_NIL)
    {
        CCThreadNode *pParent = node_at(pX->parent);
        if (pParent->left == x_idx)
            pParent->left = y_idx;
        else
            pParent->right = y_idx;
    }

    // y->right = x
    pY->right = x_idx;
    pX->parent = y_idx;
    return 0;
}

// 根节点变化通知：把新根地址转成下标保存（数组会搬移，不能存指针）
static void ch_root(void *user, void *new_root)
{
    CC_UNUSED(user);
    ccthread_var.root = new_root ? idx_of((CCThreadNode *)new_root) : CC_THREAD_NIL;
}

static CC_RbtOps thread_tree_ops = {
    .user = NULL,
    .compare_key = compare_key,
    .compare_node = compare_node,
    .copy_node = copy_node,
    .find_instead = find_instead,
    .get_node = get_node,
    .set_node = set_node,
    .get_color = get_color,
    .set_color = set_color,
    .left_rotate = left_rotate,
    .right_rotate = right_rotate,
    .ch_root = ch_root,
};

//
// 异步锁实现

// 创建锁
CCLock cc_create_lock()
{
    CCLockInner *pInner = ccalloc(NULL, sizeof(CCLockInner), CC_DBG_REL_TAG("异步锁", NULL));
    if (!pInner)
        return NULL;
    int errcode = pthread_mutex_init(&pInner->lock, NULL);
    if (errcode)
    {
        ccalloc(pInner, 0, NULL);
        return NULL;
    }
    pInner->magic = CCLOCK_MAGIC;
    return pInner;
}

// 销毁锁
CCINT32 cc_destroy_lock(CCLock lock)
{
    CCLockInner *pInner = lock;
    if (!pInner || pInner->magic != CCLOCK_MAGIC)
        return -1;
    pthread_mutex_destroy(&pInner->lock);
    ccalloc(pInner, 0, NULL);
    return 0;
}

// 加锁
void cc_lock(CCLock lock)
{
    CCLockInner *pInner = lock;
    if (!pInner || pInner->magic != CCLOCK_MAGIC)
        return;
    pthread_mutex_lock(&pInner->lock);
}

// 解锁
void cc_unlock(CCLock lock)
{
    CCLockInner *pInner = lock;
    if (!pInner || pInner->magic != CCLOCK_MAGIC)
        return;
    pthread_mutex_unlock(&pInner->lock);
}

/*
 * ============================================================================
 * 多线程实现
 *
 * 整体思路：线程一旦创建就立即 detach，避免 joinable 线程残留为僵尸线程；
 * 存活线程统一登记在红黑树中，由 cc_thread_status / cc_thread_wait 查询与等待。
 * 线程自身结束时由包装层带锁摘除节点，实现「自回收」。
 * ============================================================================
 */

/*
 * 从线程树移除 key 对应节点并回收其槽位（须在锁内调用）。
 *
 * 槽位回收采用「末尾节点搬到空位 + 弹尾」策略：
 * 1. 保持节点在数组中紧凑排列，使 size / sizeof(CCThreadNode) 恒等于存活线程数；
 * 2. 相比 memmove 平移后续所有元素（O(n)，且需逐项修正大量下标），
 *    这里只需修正「指向末尾节点的 3 处引用 + 根」，为 O(1)；
 * 3. 存活节点即便位置改变，其对外身份仍是 key：_inner_thread_ 与 status/wait
 *    全部按 key 定位，从不依赖下标，因此搬移不影响其他正在运行的线程。
 */
static void thread_tree_remove_locked(CCUINTPTR key)
{
    if (ccthread_var.root == CC_THREAD_NIL)
        return;
    CCThreadNode *pRemoved = cc_rbt_core_remove(&thread_tree_ops, root_node(), key);
    if (!pRemoved)
        return;

    CCUINT32 removed_idx = idx_of(pRemoved);
    CCUINT32 count = ccthread_var.thread_nodes.size / (CCUINT32)sizeof(CCThreadNode);
    CCUINT32 last_idx = count - 1;

    if (removed_idx != last_idx)
    {
        CCThreadNode *pLast = node_at(last_idx);
        // 修正树中所有指向末尾节点的引用（至多：父的一个子槽、左子的 parent、右子的 parent）。
        // 被删节点此时已摘出树，不可能有节点再指向 removed_idx，
        // 因此下面读到的 pLast->parent/left/right 均不会等于 removed_idx，不存在自指。
        if (pLast->parent != CC_THREAD_NIL)
        {
            CCThreadNode *pParent = node_at(pLast->parent);
            if (pParent->left == last_idx)
                pParent->left = removed_idx;
            else if (pParent->right == last_idx)
                pParent->right = removed_idx;
        }
        if (pLast->left != CC_THREAD_NIL)
            node_at(pLast->left)->parent = removed_idx;
        if (pLast->right != CC_THREAD_NIL)
            node_at(pLast->right)->parent = removed_idx;
        if (ccthread_var.root == last_idx)
            ccthread_var.root = removed_idx;
        // 末尾节点搬入空位
        *node_at(removed_idx) = *pLast;
    }

    // 弹出末尾元素（内部必要时还会收缩容量）
    CCThreadNode tmp;
    cc_dyn_core_pop(
        &ccthread_var.thread_nodes, (CCUINT32)sizeof(CCThreadNode),
        (CCINT32)sizeof(CCThreadNode), &tmp);
}

/*
 * 线程包装层：用户入口不直接作为系统线程入口。
 *
 * 设计原因：
 * 1. 包装层通过传参结构体 CCThreadArg 持有自身入口、用户数据、句柄与 ID，
 *    从而能在入口返回后带锁地把自身节点从线程树摘除并释放传参结构体；
 * 2. 用户入口在锁外执行，避免用户代码中再次调用 cc_thread_status / cc_thread_wait
 *    时发生自锁（内部锁不是递归锁）。
 */
#ifdef CC_WINDOWS
static DWORD WINAPI _inner_thread_(LPVOID lp)
#else
static void *_inner_thread_(void *lp)
#endif
{
    CCThreadArg *pArg = (CCThreadArg *)lp;

    // 锁内读取 ID：创建者在持锁期间完成句柄写入，故此处读到的一定是有效 ID
    pthread_mutex_lock(&ccthread_var.thread_tree_lock);
    CCThread id = (CCThread)pArg->key;
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);

    // 用户入口在锁外执行
    pArg->entry(pArg->userData, id);

    // 线程收尾：带锁摘除自身节点、回收槽位、释放传参结构体
    pthread_mutex_lock(&ccthread_var.thread_tree_lock);
    thread_tree_remove_locked(pArg->key);
    ccalloc(pArg, 0, NULL);
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);

#ifdef CC_WINDOWS
    return 0;
#else
    return NULL;
#endif
}

//
// 内部初始化 / 反初始化

void init_ccthread(void)
{
    ccthread_var.thread_nodes.array = NULL;
    ccthread_var.thread_nodes.size = 0;
    ccthread_var.thread_nodes.capacity = 0;
    ccthread_var.root = CC_THREAD_NIL;
    pthread_mutex_init(&ccthread_var.thread_tree_lock, NULL);
}

void deinit_ccthread(void)
{
    // 调用方应确保所有线程已结束；这里在锁内释放节点数组并销毁锁
    pthread_mutex_lock(&ccthread_var.thread_tree_lock);
    if (ccthread_var.thread_nodes.array)
        ccalloc(ccthread_var.thread_nodes.array, 0, NULL);
    ccthread_var.thread_nodes.array = NULL;
    ccthread_var.thread_nodes.size = 0;
    ccthread_var.thread_nodes.capacity = 0;
    ccthread_var.root = CC_THREAD_NIL;
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
    pthread_mutex_destroy(&ccthread_var.thread_tree_lock);
}

// 创建线程
// 采用 goto 链式回退：每个失败点只标注失败并跳到对应标签，
// 由标签按资源获取的逆序统一释放，最终只有“完整成功 / 完整失败”两种原子结果。
CCThread cc_thread_create(CCThreadEntry entry, void *data)
{
    CCThread id = 0;
    CCThreadArg *pArg = NULL;
    CCThreadNode *pNode = NULL;

    if (!entry)
        return 0;

    pthread_mutex_lock(&ccthread_var.thread_tree_lock);

    // 1. 线程实体参数
    pArg = ccalloc(NULL, sizeof(CCThreadArg), CC_DBG_REL_TAG("线程实体参数", NULL));
    if (!pArg)
        goto fail_unlock;
    pArg->entry = entry;
    pArg->userData = data;
    pArg->key = 0;

    // 2. 追加一个空树节点（key 待句柄确定后回填）
    {
        CCThreadNode node;
        memset(&node, 0, sizeof(node));
        node.parent = CC_THREAD_NIL;
        node.left = CC_THREAD_NIL;
        node.right = CC_THREAD_NIL;
        node.is_red = CCFALSE;
        if (
            cc_dyn_core_push(&ccthread_var.thread_nodes,
                             (CCUINT32)sizeof(CCThreadNode),
                             (CCINT32)sizeof(CCThreadNode), &node) != 0)
            goto fail_free_arg;
    }
    pNode = node_at(ccthread_var.thread_nodes.size / (CCUINT32)sizeof(CCThreadNode) - 1);

// 3. 持锁创建线程：新线程包装层的首次加锁会阻塞到本函数解锁，
//    由此保证它读到的句柄/key 已完成初始化，且节点已入树。
#ifdef CC_WINDOWS
    pArg->thread = CreateThread(NULL, 0, _inner_thread_, pArg, 0, NULL);
    if (!pArg->thread)
        goto fail_pop_node;
    // Windows 的 HANDLE 不等于线程 ID，取 GetThreadId 作为对外 key；
    // 该值在本进程内唯一且与线程一一对应，可直接用作树 key。
    pArg->key = (CCUINTPTR)GetThreadId(pArg->thread);
#else
    if (pthread_create(&pArg->thread, NULL, _inner_thread_, pArg) != 0)
        goto fail_pop_node;
    // POSIX 下 pthread_t 本身即可作为线程标识
    pArg->key = (CCUINTPTR)pArg->thread;
#endif

    // 4. 回填 key 并插入线程树
    pNode->key = pArg->key;
    if (cc_rbt_core_insert(&thread_tree_ops, root_node(), pNode) != 0)
        goto fail_insert;

// detach / 关闭句柄：不等待线程结束，资源由包装层收尾
#ifdef CC_WINDOWS
    CloseHandle(pArg->thread);
#else
    pthread_detach(pArg->thread);
#endif

    id = (CCThread)pArg->key;
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
    return id;

// 线程已创建且在运行：先解锁等待其结束，再回收数组末尾的孤立节点。
// pArg 已由包装层自行释放，因此句柄必须在持锁时先取到局部变量再解锁使用。
fail_insert:
#ifdef CC_WINDOWS
    {
        HANDLE rawThread = pArg->thread;
        pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
        WaitForSingleObject(rawThread, INFINITE);
        CloseHandle(rawThread);
    }
#else
    {
        pthread_t rawThread = pArg->thread;
        pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
        pthread_join(rawThread, NULL);
    }
#endif
    // 包装层已尝试摘除（未入树，故失败）并释放 pArg，此处回收孤立节点
    pthread_mutex_lock(&ccthread_var.thread_tree_lock);
    if (ccthread_var.thread_nodes.size >= sizeof(CCThreadNode))
    {
        CCThreadNode tmp;
        cc_dyn_core_pop(&ccthread_var.thread_nodes,
                        (CCUINT32)sizeof(CCThreadNode), (CCINT32)sizeof(CCThreadNode), &tmp);
    }
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
    return 0;

// 线程未创建、节点已追加：摘除节点后继续回收参数
fail_pop_node:
    {
        CCThreadNode tmp;
        cc_dyn_core_pop(
            &ccthread_var.thread_nodes,
            (CCUINT32)sizeof(CCThreadNode),
            (CCINT32)sizeof(CCThreadNode),
            &tmp);
    }
fail_free_arg:
    ccalloc(pArg, 0, NULL);
fail_unlock:
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
    return 0;
}

// 查询线程
CCINT32 cc_thread_status(CCThread thr)
{
    if (!thr)
        return 0;

    CCINT32 alive = 0;
    pthread_mutex_lock(&ccthread_var.thread_tree_lock);
    if (ccthread_var.root != CC_THREAD_NIL &&
        cc_rbt_core_get(&thread_tree_ops, root_node(), (CCUINTPTR)thr))
        alive = 1;
    pthread_mutex_unlock(&ccthread_var.thread_tree_lock);
    return alive;
}

// 等待线程
void cc_thread_wait(CCThread thr, CCUINT32 timeOut_us)
{
    if (!thr)
        return;

    CCUINT32 elapsed = 0;
    CCUINT32 sleepTime = 1000; // 首次退避 1ms（1000us）

    for (;;)
    {
        pthread_mutex_lock(&ccthread_var.thread_tree_lock);
        CCBOOL alive = ccthread_var.root != CC_THREAD_NIL &&
                       cc_rbt_core_get(&thread_tree_ops, root_node(), (CCUINTPTR)thr) != NULL;
        pthread_mutex_unlock(&ccthread_var.thread_tree_lock);

        // 线程不存在或已结束：直接返回（ID 不存在时不会阻塞）
        if (!alive)
            return;
        // 有限等待且已超时
        if (timeOut_us && elapsed >= timeOut_us)
            return;

        ccsleep(sleepTime);
        elapsed += sleepTime;

        if (timeOut_us)
        {
            // 有限等待：快速翻倍退避到 16ms，保证超时判断足够灵敏
            if (sleepTime < 16000)
                sleepTime <<= 1;
        }
        else
        {
            // 无限等待：平缓增长到 100ms，降低长期轮询开销
            if (sleepTime < 100000)
                sleepTime += 4000;
        }
    }
}