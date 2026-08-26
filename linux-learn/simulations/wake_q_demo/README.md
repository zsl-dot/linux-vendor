# wake_q_head — 二级指针实现 O(1) 无分支尾部追加

## 1. 数据结构

```c
struct wake_q_node {
    struct wake_q_node *next;
};

struct wake_q_head {
    struct wake_q_node *first;        // 指向第一个节点(或 WAKE_Q_TAIL 表示空)
    struct wake_q_node **lastp;       // 指向"下一个节点该写入的位置"
};
```

### lastp 存的是什么?

`lastp` 存的不是某个节点地址，而是**"当前尾部 next 指针字段的地址"**。

| 状态 | `lastp` 的值 | `*lastp` 等价于 |
|---|---|---|
| 空链表 | `&wq.first` | `wq.first` |
| 有一个尾节点 | `&tail->next` | `tail->next` |

```
空链表:
  wq.first = WAKE_Q_TAIL
  wq.lastp = &wq.first        ← 指向 first (first 充当了 "伪 next")

加 A 后:
  wq.first = &A.wake_q
  wq.lastp = &A.wake_q.next   ← 指向 A 的 next 字段

加 B 后:
  wq.lastp = &B.wake_q.next   ← 指向 B 的 next 字段
```

## 2. 解决了什么问题

### 问题: 单链表尾部追加需要分支

普通单链表插入尾部必须判断空链表:

```c
/* 没有 lastp — 必须 if/else + 遍历 O(n) */
void append(struct wake_q_head *wq, struct wake_q_node *node)
{
    if (wq->first == NULL)
        wq->first = node;              // 情况1: 改 first
    else {
        struct wake_q_node *tail = wq->first;
        while (tail->next)
            tail = tail->next;         // 遍历到尾部
        tail->next = node;             // 情况2: 改 tail->next
    }
}
```

**要修改的目标类型不一样** — 空表改 `wq->first`，非空改 `tail->next`，必须分支。

### 方案: 二级指针统一修改目标

`wq.first` 和 `tail->next` 都是 `struct wake_q_node *` 类型。它们的地址 — `&wq.first` 和 `&tail->next` — 都是 `struct wake_q_node **` 类型。

`lastp` 始终指向"下一个节点该写入的那个指针字段"，把两种情况统一：

```c
/* 有 lastp — O(1), 零分支 */
void append(struct wake_q_head *wq, struct wake_q_node *node)
{
    *wq->lastp = node;          // 无论空不空, 都在"正确位置"写入
    wq->lastp = &node->next;    // lastp 跟随到新的尾部
}
```

| | 时间复杂度 | 分支 |
|---|---|---|
| 无 lastp (遍历到尾) | O(n) | if/else |
| 有 lastp (ptr-to-ptr) | O(1) | 零分支 |

### 额外好处

- **无锁**: `wake_q_head` 是调用者栈上的局部变量，不存在并发，`lastp` 操作无需锁
- **无分配**: 不需要为队列头额外分配内存，栈上即可

## 3. 为什么不用双向链表 (`list_head`)?

内核里有现成的双向链表 `linux/list.h`，但 `wake_q` 没有用它。原因:

### 操作需求极简 — 只需要单向

wake_q 的生命周期只有三步:

| 操作 | 需要双向? |
|---|---|
| 追加到尾部 | 不需要, `lastp` 已做到 O(1) |
| 从头正向遍历 | 不需要, 单向指针即可 |
| 从中间删除节点 | **不需要** — 唤醒是一次性消费整个队列 |

`wake_up_q()` 从 `first` 走到 `WAKE_Q_TAIL`，全部唤醒，清空。
中途不删节点，不反向查找。`prev` 指针完全是死重。

### 内存代价不可忽略

`wake_q_node` 嵌入在 `task_struct` 里 — **系统中每个进程都有一个**。

```
单链表 (现状):              双链表 (如果用了):
  wake_q_node                  wake_q_node
    next  (8B)                   next  (8B)
    === 8 字节 ===               prev  (8B)
                                 === 16 字节 ===
```

每个 task 省 8 字节。服务器上几十万个 task，就是几 MB 内存。
`task_struct` 已经 ~1300 字节，每多加一字节都要有充分理由。

### 内核中两种链表的定位

| |`wake_q_node` (单链表)|`list_head` (双链表)|
|---|---|---|
| 嵌入位置 | `task_struct` (每进程) | 等待队列头、调度队列等 |
| 遍历方向 | 只正向 | 双向 |
| 中间删除 | 不需要 | 需要 (信号打断等待等) |
| 内存敏感度 | 极高 (每进程) | 相对宽松 |
| 插入复杂度 | O(1) + `lastp` 技巧 | O(1) 天然支持 |

**一句话: 用不着, 划不来。** 单链表已经满足全部需求，多一个 `prev` 指针只会浪费每进程 8 字节。

## 4. 内核源码对应

### 结构体定义 (`include/linux/sched/wake_q.h`)

```c
struct wake_q_head {
    struct wake_q_node *first;
    struct wake_q_node **lastp;
};

#define WAKE_Q_TAIL ((struct wake_q_node *) 0x01)   // 哨兵, 非空链表结尾

#define DEFINE_WAKE_Q(name) \
    struct wake_q_head name = { WAKE_Q_TAIL, &name.first }

static inline void wake_q_init(struct wake_q_head *head)
{
    head->first = WAKE_Q_TAIL;
    head->lastp = &head->first;
}
```

### 追加操作 (`kernel/sched/core.c: __wake_q_add`)

```c
static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
    struct wake_q_node *node = &task->wake_q;

    /* cmpxchg: 原子抢占, 防止 task 被重复加入 */
    if (cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL))
        return false;

    /* ptr-to-ptr O(1) 追加 */
    *head->lastp = node;
    head->lastp = &node->next;

    return true;
}
```

### 批量唤醒 (`kernel/sched/core.c: wake_up_q`)

```c
void wake_up_q(struct wake_q_head *head)
{
    struct wake_q_node *node = head->first;

    while (node != WAKE_Q_TAIL) {
        struct task_struct *task;
        task = container_of(node, struct task_struct, wake_q);
        node = node->next;
        task->wake_q.next = NULL;       // 重置, 允许重新入队
        wake_up_process(task);
        put_task_struct(task);
    }
}
```

### `next` 指针的三态 + `WAKE_Q_TAIL` 哨兵

`wake_q_node.next` 指针被复用为三种含义，用于追踪 task 的队列状态:

| `next` 的值 | 含义 |
|---|---|
| `NULL` | task **不在**任何唤醒队列中 |
| `WAKE_Q_TAIL` (0x01) | task **在**队列中，且是尾节点 |
| 真实指针 | task **在**队列中，next 指向下一个节点 |

**为什么不能用 NULL 标记结尾?**

因为 NULL 已经被占用了——它表示"task 不在任何队列中"。如果尾节点的 `next` 也用 NULL，遍历时就无法区分"到达链表结尾"和"遇到一个未入队的 task"。

```
普通链表 (NULL 结尾):               wake_q 链表 (WAKE_Q_TAIL 结尾):

[A] → [B] → [C] → NULL             [A] → [B] → [C] → WAKE_Q_TAIL(0x1)

while (node != NULL)               while (node != WAKE_Q_TAIL)
    node = node->next                   node = node->next
```

`0x01` 是一个**哨兵值**(sentinel)，永远不会被误认为真实节点地址(节点地址至少对齐到 4 或 8 字节)，三态清晰互斥。

初始化时 `wq.first = WAKE_Q_TAIL` 表示空链表，`while (node != WAKE_Q_TAIL)` 遍历到哨兵时自然终止。被唤醒后 `task->wake_q.next = NULL` 重置，允许 task 再次被加入队列。

## 5. demo 文件导航

| 文件 | 内容 |
|---|---|
| `01_intrusive_list.c` | 侵入式链表 + `container_of` 反推 |
| `02_ptr_to_ptr.c` | `struct wake_q_head` + `lastp` 二级指针 O(1) 追加 |
| `03_wake_q_simulation.c` | 完整复刻内核 wake_q: 三态 next, cmpxchg, 引用计数, 延迟唤醒 |
| `04_single_vs_double_ptr.c` | 横向对比三种追加方式 (if/else vs lastp遍历 vs lastp始终在尾部) |
| `05_lastp_visual.c` | 逐步可视化: 打印每步内存状态和逻辑链 |
| `06_manual_step_by_step.c` | 单步调试级模拟: 逐条语句展示变量和内存变化 |

### 运行

```bash
make          # 编译全部
make run      # 运行全部
./02_ptr_to_ptr           # 只看二级指针
./03_wake_q_simulation    # 只看完整模拟
```

## 6. 相关资源

- [wait_queue_demo/](../wait_queue_demo/) — 上层: wait_queue 睡眠唤醒 API + 内存屏障协议
- 内核源码: `include/linux/sched/wake_q.h`, `kernel/sched/core.c`
