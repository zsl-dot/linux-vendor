# 进程管理的睡眠/唤醒子系统 — 全景图

> 涵盖: 睡眠唤醒机制、调度器字段、CPU 亲和性、内核栈布局 (thread_union)、ABI

## 1. 你在看的是同一个子系统

之前分别讲了 `wake_q_node`、`wait_queue`、`set_current_state`、`try_to_wake_up`，它们不是孤立的，而是**同一个睡眠/唤醒机制的四个层次**：

```
                  ┌──────────────────────────────────────┐
                  │         进程 A (睡眠方)              │
                  │                                      │
  上层 (驱动API)   │  wait_event_interruptible(wq, cond) │
                  │    set_current_state(SLEEPING)       │
  中层 (状态机)    │    if (cond) break                  │
                  │    schedule()   ← 让出 CPU, 摘下队列 │
                  └──────────────────┬───────────────────┘
                                     │
                  ┌──────────────────┼───────────────────┐
                  │         进程 B (唤醒方)              │
                  │                  ▼                   │
  上层 (驱动API)   │  cond = 1;                          │
                  │  wake_up_interruptible(&wq)          │
  中层 (状态机)    │    → try_to_wake_up(A)              │
                  │        smp_mb()   ← 内存屏障配对     │
                  │        A->state = TASK_RUNNING       │
                  │        放回运行队列                  │
                  │                                      │
  底层 (优化)      │  如果持锁:                           │
                  │    wake_q_add(&wq, A)  ← 延迟记录    │
                  │    spin_unlock()                     │
                  │    wake_up_q(&wq)     ← 批量唤醒     │
                  └──────────────────────────────────────┘
```

## 2. 进程状态 — 一切的起点 (`include/linux/sched.h`)

```c
#define TASK_RUNNING            0x00000000   // 可运行/正在运行
#define TASK_INTERRUPTIBLE      0x00000001   // 可中断睡眠 (能被信号唤醒)
#define TASK_UNINTERRUPTIBLE    0x00000002   // 不可中断睡眠 (不响应信号)
#define __TASK_STOPPED          0x00000004   // 收到 SIGSTOP
#define EXIT_DEAD               0x00000010   // 进程已死亡
#define TASK_DEAD               0x00000080   // 等待父进程回收
#define TASK_WAKEKILL           0x00000100   // 收到致命信号时醒来
```

状态被编码为位掩码，`try_to_wake_up` 用 `state & p->state` 匹配可唤醒的状态：

```c
/* 唤醒任何普通睡眠状态的进程 */
#define TASK_NORMAL  (TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

wake_up_process(p)  →  try_to_wake_up(p, TASK_NORMAL, 0)
```

## 3. 四个层次的完整调用链

### 层次 1: 驱动 API (wait_queue)

驱动开发者调用的接口。**管理"谁在等什么"。**

```
睡眠方                       唤醒方
──────                       ──────
wait_event(wq, cond)         wake_up(&wq)
  wait_event_interruptible     wake_up_interruptible
```

[wait_queue_demo/01_basic_wait_queue.c] — 模拟这部分

### 层次 2: 状态机 (set_current_state + schedule + try_to_wake_up)

进程改变自己的状态，调度器摘除/恢复进程。**管理"进程能运行吗"。**

```
睡眠方                       唤醒方
──────                       ──────
set_current_state(SLEEPING)  try_to_wake_up(p, state, flags)
  smp_store_mb()               smp_mb()
  └─ 写 state + 完整屏障        └─ 完整屏障 + 读 state

schedule()                      └─ if (state & p->state)
  └─ 摘下 → 切换到其他进程          p->state = TASK_RUNNING
                                    放回运行队列
```

**内存屏障的配对**是这一层的核心。两边的屏障保证：要么睡眠方看到 `cond == true` 不睡，要么唤醒方看到 `state == SLEEPING` 执行唤醒。不会丢失。

[wait_queue_demo/README.md] — 完整分析这部分

### 层次 3: 运行时队列操作 (ttwu_runnable / ttwu_queue)

`try_to_wake_up` 内部根据 task 是否在运行队列上分两条路径：

- `ttwu_runnable()` — task 仍在运行队列上，只需要改状态
- `ttwu_queue()` — task 已摘下，需要重新入队 + 可能跨 CPU 迁移

内核注释说 "tries really hard to only take one rq lock"，因为锁竞争的代价很大。

### 层次 4: 延迟唤醒优化 (wake_q_node)

**不替代上面三层，只是优化唤醒时机。** 当 waker 持锁时，把昂贵的 `try_to_wake_up` 推迟到解锁之后。

```
持锁时 (不能直接唤醒):          解锁后 (安全了):
─────────────────────           ────────────────
spin_lock(&lock);               wake_up_q(&wq)
wake_q_add(&wq, task)             └→ 遍历链表
  └→ 只记录到链表 (O(1))              └→ try_to_wake_up(task)
spin_unlock(&lock);
```

[wake_q_demo/] — 6 个 demo 完整分析这部分

## 4. 完整的工作流 (一次典型唤醒)

```
进程 A: kworker                       进程 B: driver
──────────────────                     ──────────────

/* 准备睡眠 */
set_current_state(TASK_INTERRUPTIBLE)
  smp_store_mb: 写 SLEEPING + 屏障
                                       spin_lock(&lock);
                                       /* 设置条件 */
                                       cond = 1;
/* 检查条件 */                         /* 发现 A 需要醒来 */
if (!cond)                             wake_q_add(&wq, A);
    schedule();                          cmpxchg: NULL → WAKE_Q_TAIL
    └→ __schedule()                       *wq.lastp = &A.wake_q  // 记录
        deactivate_task(A)                wq.lastp = &A.wake_q.next
        上下文切换到 B                    get_task_struct(A)
                                       spin_unlock(&lock);

                                       wake_up_q(&wq)
                                         container_of → 反推 task_struct A
                                         wake_up_process(A)
                                           try_to_wake_up(A)
                                             smp_mb() ← 与上面配对
                                             A->state = TASK_RUNNING
                                             activate_task(A)
                                             put_task_struct(A)

/* A 回到 CPU */
schedule() 返回
__set_current_state(TASK_RUNNING)
// 继续执行...
```

## 5. 各数据结构的嵌入关系

所有东西都在 `task_struct` 里：

```c
struct task_struct {
    unsigned int            __state;        // 进程状态 (层次 2)
    struct wake_q_node      wake_q;         // 延迟唤醒节点 (层次 4)
    // wait_queue 不直接嵌入 — 通过 wait_queue_entry_t 间接关联
};
```

```
    ┌─────────── task_struct ───────────┐
    │                                   │
    │  __state  ←→ set_current_state()  │  ← 状态机
    │              try_to_wake_up()     │
    │                                   │
    │  wake_q   ←→ wake_q_add()         │  ← 延迟批量唤醒
    │              wake_up_q()          │
    │                                   │
    └───────────────────────────────────┘
                        ▲
                        │ private 指针
    ┌───────────────────┴─────────────┐
    │  wait_queue_entry               │  ← 等待队列条目
    │    .private = &task_struct      │
    │    .func = autoremove_wake_fn   │
    │    .entry = list_node           │
    └─────────────────────────────────┘
```

## 6. 唤醒关系探测器: last_wakee 与 wakee_flips

### 三个字段

```c
/* include/linux/sched.h — task_struct 内部 */
struct task_struct    *last_wakee;          // 我上次唤醒了谁 (指向被唤醒的 task)
unsigned int           wakee_flips;          // 我换了多少次不同的唤醒对象
unsigned long          wakee_flip_decay_ts;  // flip 计数的时间衰减时间戳
```

命名很形象: `waker` = 调用 `wake_up_process()` 的人，`wakee` = 被唤醒的人。

### 核心逻辑: 判断唤醒关系是"专一"还是"海王"

每次进程 A 醒来并唤醒进程 B 时（A 结束睡眠、变为 RUNNING，然后唤醒 B），调度器记录:

```c
/* kernel/sched/fair.c — 每次唤醒时触发 */
if (current->last_wakee != p) {   // 这次唤醒的目标和上次不一样?
    current->last_wakee = p;       // 记录新目标
    current->wakee_flips++;        // flip 计数 +1 ← "我换人了"
}
```

`wakee_flips` 不是唤醒的总次数，而是**换人次数**。连续唤醒同一个进程不会增加这个计数。

### 用途: 决定被唤醒的进程放哪个 CPU

```
情况 1: 1:1 关系
  last_wakee 总是指向同一个进程 → wakee_flips 很低
  进程 A 每次都是唤醒进程 B
  → 把 B 放 A 所在 CPU (共享 L1/L2 cache, 性能最好)

情况 2: 1:N 关系
  last_wakee 不断变化 → wakee_flips 很高
  进程 A 是调度中心 (如 dispatcher/webserver), 唤醒很多不同进程
  → 把被唤醒的进程分散到不同 CPU (避免全部挤在一个核上)
```

### wake_wide(): 决策函数

```c
/* kernel/sched/fair.c — 判断唤醒关系是否"够广" */
static int wake_wide(struct task_struct *p)
{
    unsigned int master = current->wakee_flips;    // waker 的换人次数
    unsigned int slave  = p->wakee_flips;           // wakee 的换人次数
    int factor = __this_cpu_read(sd_llc_size);      // 最后一级缓存的 CPU 数

    if (master < slave)
        swap(master, slave);                        // master 取较大者
    if (slave < factor || master < slave * factor)
        return 0;                                   // 关系不够广, 放一起
    return 1;                                       // 关系够广, 分散放置
}
```

`wake_wide()` 的逻辑:

- `factor` = LLC (Last Level Cache) 共享的 CPU 数量，通常是 4 或 8
- 如果两者 flip 都很少 → 1:1 关系 → 返回 0 (放一起)
- 如果一方 flip 远超另一方 × factor → 1:N 关系 → 返回 1 (分散)

### 时间衰减: wakee_flip_decay_ts

`wakee_flips` 不会无限增长。配合 `wakee_flip_decay_ts` 时间戳，老旧的 flip 记录会随时间衰减:

```
如果 T1 时刻换人 100 次，但此后很长时间都是同一对人 → flip 衰减 → 又算 1:1
如果 T2 时刻一直在频繁换人 → flip 不衰减 → 仍是 1:N
```

这样调度器能**根据当前行为的持续性**做决策，而不是被历史数据误导。

### 和前面知识体系的连接

```
wake_up_q(&wq)
  └→ wake_up_process(task)
       └→ try_to_wake_up(task, ...)
            │
            ├─ 状态机: TASK_RUNNING
            ├─ 内存屏障: smp_mb() (与 set_current_state 配对)
            ├─ 选 CPU: select_task_rq()
            │    └→ wake_wide() ← 用 last_wakee/wakee_flips 决策
            │        返回 0: 和 waker 共享 CPU
            │        返回 1: 分散到其他 CPU
            │
            └─ ttwu_queue(): 放回运行队列
```

`last_wakee` / `wakee_flips` 是 `try_to_wake_up` 在选 CPU 阶段的输入——它们是连接"唤醒机制"和"负载均衡"的桥梁。

### `wake_wide()` 决定"放一起还是分散"，那具体能选哪些 CPU 呢?

`select_task_rq()` 还要受 CPU 亲和性约束。这部分由 5 个字段控制:

```c
/* include/linux/sched.h — task_struct 内部 */
unsigned long      max_allowed_capacity;  // 允许的 CPU 中, 最大的算力值
int                nr_cpus_allowed;       // 允许的 CPU 数量 (位图 popcount 缓存)
const cpumask_t   *cpus_ptr;             // 指向实际生效的 CPU 位图
cpumask_t         *user_cpus_ptr;        // 用户自定义的 CPU 位图 (如 taskset/cgroup)
cpumask_t          cpus_mask;            // 默认的 CPU 位图 (存储本身)
```

### `cpumask_t` — CPU 集合的位图表示

每个 bit 代表一个 CPU，`cpumask_test_cpu(n, mask)` 判断 CPU n 是否可用:

```c
cpumask_t mask;
cpumask_set_cpu(3, &mask);           // "CPU 3 可以运行"
cpumask_test_cpu(5, &mask);          // "CPU 5 在允许列表里吗?"
```

### `cpus_ptr` — 为什么要用指针间接引用

```c
const cpumask_t *cpus_ptr;   // 指向实际生效的 CPU 掩码
cpumask_t        cpus_mask;  // 默认掩码 (存在 task_struct 内部)
cpumask_t       *user_cpus_ptr;  // 用户自定义掩码 (单独分配, 可为 NULL)
```

内核用指针来统一两种路径，省去分支判断:

```c
/* 普通进程 — cpus_ptr 指向自身的 cpus_mask */
p->cpus_ptr = &p->cpus_mask;

/* 用户通过 taskset -c 0-3 或 cgroup cpuset 限制了 CPU 范围 */
p->user_cpus_ptr = kzalloc(sizeof(cpumask_t));
cpumask_set_cpu(0, p->user_cpus_ptr);
cpumask_set_cpu(1, p->user_cpus_ptr);  // ... 只允许 CPU 0-3
p->cpus_ptr = p->user_cpus_ptr;        // cpus_ptr 指向用户自定义的
```

这样调度器快速路径只需一条指针解引用:

```c
/* 快速路径 — 零分支! */
if (cpumask_test_cpu(target_cpu, p->cpus_ptr))
    // 这个 CPU 可用 — 不管 cpus_ptr 指向的是 cpus_mask 还是 user_cpus_ptr
```

### `nr_cpus_allowed` — 位图 popcount 的缓存

```c
int nr_cpus_allowed;  // cpus_ptr 指向的位图中有几个 bit 是 1
```

计算位图的 population count 需要遍历所有 bit。缓存为整数后，快速判断:

```c
if (p->nr_cpus_allowed == 1)   // 只能跑在唯一一个 CPU 上
    // 无需负载均衡, 直接选那个 CPU

/* 对应的检测函数 */
static __always_inline bool is_percpu_thread(void)
{
    return (current->flags & PF_NO_SETAFFINITY) &&
           (current->nr_cpus_allowed == 1);
}
```

### `max_allowed_capacity` — 大小核系统的算力上限

ARM big.LITTLE 架构中，大核算力是小核的 2-4 倍。调度器选 CPU 时需要快速判断"这个进程能跑大核吗":

```c
unsigned long max_allowed_capacity;

/* 进程被限制在小核上 */
cpus_ptr = { CPU0, CPU1, CPU2 }   // 全是小核
max_allowed_capacity = 256         // 小核算力

/* 进程可以跑大核 */
cpus_ptr = { CPU0..CPU7 }         // 包含大核
max_allowed_capacity = 1024        // 大核算力
```

调度器用这个缓存值而非遍历位图来快速决定进程优先级的 CPU 选择范围。

### 两个缓存值的具体代码路径

**写入路径 (低频 — 只在 CPU 亲和性改变时)**

```c
/* kernel/sched/core.c:2684 — __set_cpus_allowed_ptr() */
cpumask_copy(&p->cpus_mask, ctx->new_mask);
p->nr_cpus_allowed = cpumask_weight(ctx->new_mask);  // 遍历位图, 缓存结果

/* kernel/sched/fair.c:8702-8720 */
static void set_task_max_allowed_capacity(struct task_struct *p)
{
    struct asym_cap_data *entry;

    if (!sched_asym_cpucap_active())          // 不是大小核系统
        return;

    rcu_read_lock();
    list_for_each_entry_rcu(entry, &asym_cap_list, link) {
        cpumask_t *cpumask = cpu_capacity_span(entry);
        if (!cpumask_intersects(p->cpus_ptr, cpumask))
            continue;                        // 允许的 CPU 不包含这个等级
        p->max_allowed_capacity = entry->capacity;  // 缓存最大算力
        break;
    }
    rcu_read_unlock();
}
```

**读取路径 (高频 — 调度器热路径, 每秒成千上万次)**

```c
/* kernel/sched/fair.c:5133-5139 — misfit 检测: 进程需要迁移到大核吗? */
if (!p ||
    (p->nr_cpus_allowed == 1) ||                          // 只能跑一个 CPU
    (arch_scale_cpu_capacity(cpu) == p->max_allowed_capacity) || // 已经是最大核
    task_fits_cpu(p, cpu)) {
    rq->misfit_task_load = 0;                             // 不需要迁移
    return;
}
rq->misfit_task_load = max_t(unsigned long, task_h_load(p), 1); // 需要迁移

/* kernel/sched/sched.h:2833 — push 任务到其他 CPU */
if (p->nr_cpus_allowed == 1)     // 只有一个 CPU 可选, 无需 push
    return NULL;

/* include/linux/sched.h:1824 — per-CPU 内核线程检测 */
static __always_inline bool is_percpu_thread(void)
{
    return (current->flags & PF_NO_SETAFFINITY) &&
           (current->nr_cpus_allowed == 1);               // 只绑一个 CPU
}
```

**有缓存 vs 没缓存的差距**

```
调度器热路径 — 每秒调用成千上万次

有缓存:
  if (p->nr_cpus_allowed == 1)       → cmp [addr], 1    (1 条指令)
  if (cpu_capacity == max_allowed)   → cmp [rax], [rbx]  (1 条指令)

没缓存:
  cpumask_weight(p->cpus_ptr)        → 遍历 1024 bit 位图
  for_each_capacity_level(...)       → 遍历大小核算力链表
```

两个缓存值本质相同——把已有数据（位图、算力表）的**计算结果**提前算好存起来，让热路径只做整数比较。

### 四种典型场景

| 场景 | cpus_ptr 指向 | user_cpus_ptr | nr_cpus_allowed |
|---|---|---|---|
| 普通进程 | `&cpus_mask` (所有 CPU) | NULL | 全部 CPU 数 |
| `taskset -c 0-3` | `user_cpus_ptr` | 已分配, CPU 0-3 | 4 |
| cgroup cpuset | `user_cpus_ptr` | 由 cgroup 写入 | 动态 |
| per-CPU 内核线程 | `&cpus_mask` (单个 CPU) | NULL, PF_NO_SETAFFINITY 锁定 | 1 |

### 和 `select_task_rq()` 的完整连接

```
try_to_wake_up()
  └→ select_task_rq()
       ├─ wake_wide()               ← 策略: 共享 CPU 还是分散?
       │   用 last_wakee / wakee_flips 判断
       │
       └─ cpus_ptr / nr_cpus_allowed / max_allowed_capacity
                                    ← 约束: 具体能选哪些 CPU?
                                     cpus_ptr 是最终边界
```

`wake_wide()` 决定了"方向"（放一起还是分散），`cpus_ptr` 决定了"边界"（哪些 CPU 是合法的）。两者共同作用才完成一次选 CPU 的决策。

## 7. 进程的容器: thread_union — 内核栈与 task_struct 的空间复用

### 数据结构

```c
/* include/linux/sched.h */
union thread_union {                              // union = 共享同一块内存
    struct task_struct task;                      // 身份 1: 进程描述符
#ifndef CONFIG_THREAD_INFO_IN_TASK
    struct thread_info thread_info;               // 身份 2: 线程元数据 (旧设计)
#endif
    unsigned long stack[THREAD_SIZE / sizeof(long)]; // 身份 3: 内核栈
};
```

`union` 意味着三个成员从**同一个地址**开始，共享同一块物理内存。不是三个分开的变量，是一个内存块有三种解读方式。

### 内存布局: task 在底部, 栈在顶部

```
THREAD_SIZE = 8KB 或 16KB

 低地址                                             高地址
 ┌─────────────────────────────────────────────────────┐
 │                                                    │
 │  task_struct  (占 ~6-8KB, 在底部)                   │
 │  ═══════════════════════                            │
 │                       │                             │
 │                       │ 栈的增长方向 ←              │
 │                       │ (函数调用时 sp 向低地址移动)  │
 │                       │                             │
 │                       │        剩余空间用作内核栈    │
 │                       │                             │
 │                       └────────→ 栈顶 (初始 sp)     │
 │                                                    │
 └─────────────────────────────────────────────────────┘
 │←──────────── stack[] 覆盖整个 THREAD_SIZE ─────────→│
 │←── task 区域 ──→│←──── 栈使用区域 ────────────────→│
```

`task_struct` 放在数组的起始位置（低地址），剩余空间是内核栈（从高地址向低地址增长）。只要栈没溢出，两者相安无事。

栈指针的初始位置是数组的高地址端:

```c
/* kernel/fork.c — fork 时设置栈指针 */
unsigned long *sp = (unsigned long *)task + THREAD_SIZE / sizeof(long);
child->thread.sp = (unsigned long)sp;
```

### 为什么用 union 而不是分开分配: 省内存 + 快速定位

```
分开分配 (浪费, 需要额外指针):
  ┌── task_struct ──┐    ┌── 内核栈 ────────┐
  │  需要单独分配     │    │  又一块单独内存    │
  │  task->stack 指针  │→  │                  │
  └─────────────────┘    └──────────────────┘

union (高效, 一块内存两个用途):
  ┌─────────────────────┐
  │ task_struct (底部)   │
  │ ...                 │
  │ 剩余空间用作内核栈    │
  │ (顶部向底部增长)      │
  └─────────────────────┘
```

最关键的好处: **从栈指针反推 task_struct — O(1), 零额外空间**:

```c
/* 给定任意内核栈地址, 快速找到 task_struct */
#define current  ((struct task_struct *) \
                   ((unsigned long)sp & ~(THREAD_SIZE - 1)))
//
//  当前栈指针 sp
//  把 sp 的低 14 位清零 (16KB = 2^14)
//  结果就是栈所在内存块的起始地址 = task_struct 的地址!
//
//  不需要查表, 不需要额外指针, 一条 AND 指令
```

任何内核路径都能通过当前栈指针瞬间拿到 `task_struct`。这是 `current` 宏的背后实现。

### CONFIG_THREAD_INFO_IN_TASK: 新旧设计的分界

新设计 (默认 y): `thread_info` 直接嵌入 `task_struct` 的第一个字段，不再是 union 的独立成员:

```c
struct task_struct {
    struct thread_info thread_info;   // ← 第一个字段!
    // ...
};
```

旧设计: `thread_info` 在 union 里，和 `task_struct` 平级。访问 `thread_info` 需要区分"在 union 里还是 task_struct 里"两条路径。

### task_struct 膨胀与栈空间挤压

task_struct 一直在变大。目前在 8KB+ 左右（开启 randstruct 可超 11KB）。栈空间被持续挤压:

| 配置 | task_struct 大小 | 剩余栈空间 |
|---|---|---|
| x86-64 (4K pages) | ~7KB | ~9KB (THREAD_SIZE=16KB) |
| ARM64 大核 | ~8KB | ~8KB (THREAD_SIZE=16KB) |
| CONFIG_RANDSTRUCT=y | ~11KB+ | ~5KB 或更少 |

**堆栈溢出防护**: 栈用得太深 → 覆盖 task_struct → `CONFIG_VMAP_STACK` 在栈页前后加不可访问的 guard page → 溢出一碰就触发 page fault → 安全崩溃而非数据损坏:

```
vmalloc 区域:
  ┌─────────┬────────────────┬─────────┐
  │ guard   │ 内核栈(16KB)    │ guard   │
  │ page    │ + task_struct  │ page    │
  │ 不可访问 │                │ 不可访问 │
  └─────────┴────────────────┴─────────┘
```

### 编译期兜底

```c
/* kernel/fork.c */
BUILD_BUG_ON(sizeof(struct task_struct) > THREAD_SIZE);
```

如果 `task_struct` 大到连 THREAD_SIZE 都装不下，内核编译直接失败。这是最后防线。

### 用户进程: 有两个栈

```
┌─────────────────────────────────────────┐
│  用户栈 (用户态使用)                      │
│  通常 8MB, 按需增长                       │  ← 不在 thread_union 里
├─────────────────────────────────────────┤
│  内核栈 + task_struct (THREAD_SIZE)      │  ← thread_union
│  只有进内核态 (系统调用/中断) 时才使用    │
└─────────────────────────────────────────┘
```

每个进程的虚拟内存开销: `THREAD_SIZE + 8KB` (含 guard pages 的虚拟地址) + 用户栈的虚拟地址空间。但物理内存上，内核栈分配 THREAD_SIZE 的物理页面（可能是不连续页面），guard page 不占物理页。

## 8. 调度通知机制: TIF_NEED_RESCHED — "你该让出 CPU 了"

### 核心机制: 一个 bit 引发一次调度

```c
/* include/linux/sched.h */
static inline void set_tsk_need_resched(struct task_struct *tsk)
{
    set_tsk_thread_flag(tsk, TIF_NEED_RESCHED);   // 在 thread_info.flags 中置位
}

static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
    atomic_long_andnot(TIF_NEED_RESCHED | TIF_NEED_RESCHED_LAZY,
                       (atomic_long_t *)&task_thread_info(tsk)->flags);
}

static inline int test_tsk_need_resched(struct task_struct *tsk)
{
    return unlikely(test_tsk_thread_flag(tsk, TIF_NEED_RESCHED));
}

static inline void set_need_resched_current(void)
{
    lockdep_assert_irqs_disabled();     // 必须关中断!
    set_tsk_need_resched(current);      // 贴标签
    set_preempt_need_resched();         // 设置抢占计数器
}
```

### 完整流程: 从唤醒到让出 CPU

```
进程 A 在 CPU0 上运行            进程 B 被唤醒 (更高优先级)
───────────────                 ─────────────────────────
                                 try_to_wake_up(B)
                                   → B->state = TASK_RUNNING
                                   → select_task_rq() 选 CPU
                                   → B 放回运行队列
                                   → resched_curr(CPU0)
                                      → set_tsk_need_resched(A)
                                         → TIF_NEED_RESCHED 置位
                                         → 如果 B 在不同 CPU 上,
                                           发送 IPI (核间中断)

A 继续执行, 直到下一个边界:
  系统调用返回 / 中断返回 / 抢占检查:
    if (test_tsk_need_resched(current))
        schedule();                   ← 主动让出 CPU
          clear_tsk_need_resched(current)
          切换到 B
```

`TIF_NEED_RESCHED` 是**异步通知机制**——唤醒方不直接抢占当前进程，而是通过一个标志位"通知"目标 CPU 上的当前进程: "你有更高优先级的任务在等了，方便的时候让一下"。目标 CPU 在当前进程的下一个安全边界主动检查并让出。

### 标志位存在 thread_info.flags

```c
struct task_struct {
    struct thread_info thread_info;   // ← 第一个字段! (快速访问)
    ...
};

struct thread_info {
    unsigned long flags;              // TIF_NEED_RESCHED 等 per-thread 标志
};
```

放在 `thread_info` 而非 `task_struct.flags` 的原因: 需要 `atomic_long_andnot` 做原子位操作。清除标志（中断返回路径）和设置标志（其他 CPU 发来的 IPI）可能同时发生，必须用原子操作。

### 两个变体: 立即 vs 延迟

```c
TIF_NEED_RESCHED        // 立即 — 系统调用/中断返回时无条件调用 schedule()
TIF_NEED_RESCHED_LAZY   // 延迟 — 只在返回用户态时检查 (内核态不打断)
```

LAZY 用于"不太紧急"的抢占: 让当前进程先把内核工作做完，返回用户态之前再让出 CPU。避免在持锁时被抢占比锁持有者。

### set_need_resched_current() 的约束

```c
static inline void set_need_resched_current(void)
{
    lockdep_assert_irqs_disabled();     // ← 必须关中断!
    ...
}
```

这要求调用者禁用中断。因为 `set_preempt_need_resched()` 修改当前 CPU 的抢占计数，不是线程局部的操作。如果中断在此刻发生并重入代码，计数会被破坏。

### 为什么 clear 在 schedule() 内部而非之前

`clear_tsk_need_resched` 为什么不放在 `schedule()` 调用之前? "既然都要让出 CPU 了, 先撕标签再调度不是更自然吗?"

**答案: 如果在 schedule() 之前清除, 会有一个竞态窗口, 漏掉高优先级进程。**

```
CPU 0 (当前进程 A)                 CPU 1 (唤醒方)
───────────────────               ───────────────────
test_tsk_need_resched → true
clear_tsk_need_resched(A)          try_to_wake_up(C)  ← C 优先级比 B 高!
  TIF_NEED_RESCHED = 0              select_task_rq() → CPU0
                                    resched_curr(CPU0)
             ← 窗口! ───────────→    set_tsk_need_resched(A)
                                        TIF_NEED_RESCHED = 1
进入 schedule()
  deactivate_task(A)                 ← A 不会再检查标志
  pick_next_task → 选中 B           ← B 被选中, 但 C 已经在等!
  context_switch(A → B)             ← 漏了 C! C 本应优先于 B
```

窗口在 `clear` 和 `deactivate_task` 之间。一旦 A 开始进入 `schedule()`, 它不会再检查 `TIF_NEED_RESCHED`——"反正已经在调度了"。但此时 CPU1 刚把 C 唤醒, C 优先级高于 A, CPU1 试图通过 `set_tsk_need_resched(A)` 通知 A 让位。标志位确实被置上了, 但 A 已经进了 `schedule()`, 不会再回头检查。而 `pick_next_task` 在 `clear` 之后执行, 可能看不到刚入队的 C。

**正确位置: `__schedule()` 内部, `pick_next_task` 之后、`context_switch` 之前。**

```c
/* kernel/sched/core.c — __schedule() 的关键逻辑, 简化版 */
static void __sched notrace __schedule(int sched_mode)
{
    struct task_struct *prev, *next;
    prev = current;

    /* ... 保存 prev 状态 ... */

    next = pick_next_task(rq, prev, &rf);
    //   ↑ ① 选举。所有可运行进程(含刚被 CPU1 唤醒的 C)都已纳入考量。
    //       此时已经选出了真正应该运行的 next。

    /* 确定要切换了, 才清除标志 — 唯一安全的位置 */
    clear_tsk_need_resched(prev);
    //  ↑ ② 撕标签。不再需要"请让位"的通知了, 已经在让了。
    //    如果 CPU1 此时再 set_tsk_need_resched(prev)?
    //    无所谓 — prev 确定要被换下, pick_next_task 已纳入了 C。
    //    即使 prev 标志位上残留 NEED_RESCHED, 下次被选中时会重新置上。

    if (likely(prev != next)) {
        rq->nr_switches++;
        context_switch(rq, prev, next);  // ③ 真正的硬件上下文切换
    }
}
```

三步的顺序至关重要:

```
pick_next_task()          ① 选举 — 所有候选者都纳入, 包括刚被唤醒的
clear_tsk_need_resched()  ② 撕标签 — 不再需要通知了, 已经在让了
context_switch()           ③ 硬件切换
```

① 确保不会漏掉刚被唤醒的进程, ② 在选举完成后清理通知标志, ③ 执行 CPU 上下文切换。

### schedule() 入口处的双重检查

```c
/* kernel/sched/core.c */
asmlinkage __visible void __sched schedule(void)
{
    struct task_struct *tsk = current;

    if (!task_is_running(tsk))          // ① 如果当前进程不是 RUNNING
        sched_submit_work(tsk);          //    先清一下未完成的 I/O

    __schedule_loop(SM_NONE);           // ② 进入主循环 (可能重试)
}
```

`schedule()` 入口有两层检查: `task_is_running(tsk)` 检查 `task_struct.__state`（状态机层面）, `TIF_NEED_RESCHED` 检查 `thread_info.flags`（通知机制层面）。两者互补——状态机控制"我能不能被调度", 通知机制控制"我该不该调度"。

### 和完整调用链的连接

```
wake_up_q(&wq)                        ← 批量唤醒
  └→ wake_up_process(B)
       └→ try_to_wake_up(B)           ← 状态机 + 内存屏障
            ├─ B->state = TASK_RUNNING
            ├─ select_task_rq()       ← wake_wide + cpus_ptr 选 CPU
            │
            └─ resched_curr(目标CPU)  ← 通知目标 CPU!
                 └→ set_tsk_need_resched(A)  ← ★ 贴标签!

══════════════ 目标 CPU 边界 ═══════════════

  A 在安全边界检查:
    if (test_tsk_need_resched(current))
        schedule()
          └→ __schedule()
               ├─ pick_next_task()           ← ① 选举 (纳入刚唤醒的)
               ├─ clear_tsk_need_resched()   ← ② 撕标签 (唯一安全位置)
               └─ context_switch()            ← ③ 硬件切换
```

整个唤醒链路的最后一步: `try_to_wake_up` 做完了所有准备（状态、选 CPU、入队），`TIF_NEED_RESCHED` 异步通知目标 CPU。目标 CPU 在安全边界进 `schedule()`, 选举完所有候选者之后清标志, 执行切换。清标志的位置——在 `pick_next_task` 之后、`context_switch` 之前——是这个系统不被竞态破坏的关键。

## 9. 调度属性修改的事务机制: sched_change_begin/end

### 核心问题: 如何安全修改一个正在跑的进程的调度属性?

进程可能同时在运行队列上 (`on_rq`) 和 CPU 上 (`on_cpu`)。修改它的优先级、调度类或 CPU 亲和性时，需要先把它从运行队列摘下，改完再放回去。但如果 dequeue 和 enqueue 没有精确配对，就会损坏调度器数据结构:

```
进程 A 在运行队列上:                 你想修改 A 的优先级:
┌────────────────────────┐
│ rq (运行队列)           │
│  ┌── A ──┐             │        dequeue_task(A)  → 从队列摘下
│  │ on_rq │  ← 在队列里  │        改 priority
│  │ on_cpu│  ← 在 CPU 上 │        enqueue_task(A)  → 放回队列 (新 priority 生效)
│  │ class │  ← CFS/RT/.. │
│  └───────┘              │        两步之间有窗口! 中间状态不能暴露给并发方。
└────────────────────────┘

旧代码: 每次手动写 dequeue/enqueue, 散落在十几个地方, 容易漏。
```

### 解决方案: sched_change_begin/end — 事务原语

`begin` 保存快照并摘下, `end` 根据快照恢复:

```c
/* kernel/sched/core.c — begin: 保存当前状态, 摘下 (如果需要) */
struct sched_change_ctx *sched_change_begin(struct task_struct *p, unsigned int flags)
{
    struct sched_change_ctx *ctx = this_cpu_ptr(&sched_change_ctx);

    lockdep_assert_rq_held(rq);   // 必须持有运行队列锁!

    *ctx = (struct sched_change_ctx){
        .p       = p,                         // 哪个进程
        .class   = p->sched_class,            // 当前调度类 (CFS/RT/DL/...)
        .flags   = flags,                     // DEQUEUE_*/ENQUEUE_* 标志
        .queued  = task_on_rq_queued(p),     // 在运行队列上吗?
        .running = task_current_donor(rq, p), // 正在 CPU 上跑吗?
    };

    if (ctx->queued)   dequeue_task(rq, p, flags);   // 摘下
    if (ctx->running)  put_prev_task(rq, p);          // 标记为不运行

    return ctx;
}

/* end: 根据快照恢复, 放回 (如果需要) */
void sched_change_end(struct sched_change_ctx *ctx)
{
    if (ctx->queued)  enqueue_task(rq, p, ctx->flags);   // 放回 (新属性生效)
    if (ctx->running) set_next_task(rq, p);               // 重新标记为运行

    /* 调度类变了? */
    if (flags & ENQUEUE_CLASS) {
        if (sched_class_above(p->sched_class, ctx->class))
            /* 提升: 通知旧类"你被抢了" */
            rq->next_class->wakeup_preempt(rq, p, 0);
        if (sched_class_above(ctx->class, p->sched_class))
            /* 降级: 主动让出 CPU */
            resched_curr(rq);
    }
}
```

关键: `begin` 和 `end` **必须精确对称**。`begin` 中 dequeue 了几次, `end` 中必须 enqueue 几次。快照 `ctx->queued` 和 `ctx->running` 记录了 `begin` 时刻的真实状态, `end` 据此决定是否 enqueue/set_next。即使调用者在 begin/end 之间提前 return 或 goto, 也不会破坏对称——因为 RAII 保证 end 一定执行。

### per-CPU 分配: 为什么零开销

```c
static DEFINE_PER_CPU(struct sched_change_ctx, sched_change_ctx);
struct sched_change_ctx *ctx = this_cpu_ptr(&sched_change_ctx);
```

每个 CPU 有自己独立的 `sched_change_ctx` 实例。begin 前调用者必须持有 `rq->lock`, 同一时刻一个 CPU 上只有一个线程持锁 → 无并发 → 不需要堆分配, 直接在 per-CPU 数据区读写, 零开销。

### RAII 接口: scoped_guard

```c
/* 调用方式 — 作用域退出自动执行 sched_change_end() */
scoped_guard (sched_change, p, DEQUEUE_SAVE) {
    p->prio         = new_prio;       // 只关心改什么
    p->cpus_ptr     = new_mask;       // 不用管 dequeue/enqueue 配对
    /* ... */
}  // ← 离开作用域, sched_change_end() 自动调用, 保证对称
```

背后的机制:

```c
/* sched.h — DEFINE_CLASS 自动生成 RAII 包装 */
DEFINE_CLASS(sched_change,
    struct sched_change_ctx *,
    sched_change_end(_T),                               // ← "析构函数"
    sched_change_begin(p, flags),                       // ← "构造函数"
    struct task_struct *p, unsigned int flags)

// 本质: GCC __attribute__((cleanup(...))) → C 语言的 Drop trait
// 离开作用域时无条件执行 sched_change_end(), 即使中途 return/goto
```

### 三种典型调用场景

```c
/* 1. 修改优先级 — DEQUEUE_SAVE/ENQUEUE_RESTORE 保存并恢复排队状态 */
scoped_guard (sched_change, p, DEQUEUE_SAVE) {
    p->prio = new_prio;
}

/* 2. 修改 CPU 亲和性 — 同上, 改 cpus_ptr/nr_cpus_allowed */
scoped_guard (sched_change, p, DEQUEUE_SAVE | DEQUEUE_MOVE) {
    p->cpus_ptr = new_mask;
    p->nr_cpus_allowed = cpumask_weight(new_mask);
}

/* 3. 切换调度类 — DEQUEUE_CLASS/ENQUEUE_CLASS 启用完整类转换回调 */
scoped_guard (sched_change, p, DEQUEUE_CLASS | ENQUEUE_CLASS) {
    p->sched_class = &rt_sched_class;    // 切到实时调度类
    p->prio = new_prio;
}
```

调度类切换时, `DEQUEUE_CLASS`/`ENQUEUE_CLASS` flag 触发类级别的 callback:

```c
/* begin 中 — 通知旧 class "这个进程要离开你了" */
if (flags & DEQUEUE_CLASS && p->sched_class->switching_from)
    p->sched_class->switching_from(rq, p);

/* end 中 — 通知新 class "这个进程加入你了" */
if (flags & ENQUEUE_CLASS && p->sched_class->switching_to)
    p->sched_class->switching_to(rq, p);
```

类似于进入和离开一个 class 的构造/析构——CFS 和 RT 各自有独立的运行队列数据结构, 切换时必须做两边各自的簿记。

### 和 TIF_NEED_RESCHED 的连接

```c
/* sched_change_end 内部 — 调度类降级时触发 schedule() */
if (sched_class_above(ctx->class, p->sched_class))
    resched_curr(rq);                    // ← 就是 set_tsk_need_resched!
```

如果进程的调度类被降级 (RT→CFS 或 CFS→IDLE), 必须让出 CPU。`resched_curr` 就是第 8 节讲的 `TIF_NEED_RESCHED` 机制——通知当前 CPU"你该重新调度了"。

### 引入时间与重构动机

这是 Linux **6.12** 周期 (2024 年 11 月) Peter Zijlstra 在 EEVDF/代理执行调度重构中引入的。

6.12 之前, 修改进程属性的 dequeue→改→enqueue 逻辑散布在 `sched_setscheduler`、`set_user_nice`、`set_cpus_allowed_ptr`、`sched_move_task`、`uclamp_update_active` 等十几个函数里, 各有各的 bug (忘了恢复 running 状态、忘了更新 clock、忘了 enqueue 回去)。`sched_change_begin/end` 把这些分散的配对收进一个事务原语, 用 RAII 保证对称, 一次性修了所有这些路径。

识别依据 (无 git 历史时的推理方法):
1. `DEFINE_CLASS` 和 `scoped_guard` 是内核 6.x 才引入的 RAII 模式 (基于 GCC cleanup 属性)
2. `DEQUEUE_CLASS`/`ENQUEUE_CLASS` flag 和 `switching_from`/`switched_to` callback 是 EEVDF + Proxy Exec 重构的产物
3. Peter Zijlstra 的代码风格 — 函数命名、注释习惯、per-CPU 数据区的使用方式
4. 内核源码树中 `Makefile` 首行的 `VERSION`/`PATCHLEVEL` 表明这是 7.x 新版内核

## 10. 调度器锁规则: task_struct 字段的保护方式

这是调度器里最重要的注释之一 (`kernel/sched/core.c:560-641`)，定义了一个核心契约：**每个 `task_struct` 字段由什么锁保护，以及在不同场景下怎么访问才安全**。

### 总则: rq->lock 是主锁

```c
/*
 * Normal scheduling state is serialized by rq->lock. __schedule() takes the
 * local CPU's rq->lock, it optionally removes the task from the runqueue and
 * always looks at the local rq data structures to find the most eligible task
 * to run next.
 *
 * Task enqueue is also under rq->lock, possibly taken from another CPU.
 * Wakeups from another LLC domain might use an IPI to transfer the enqueue to
 * the local CPU to avoid bouncing the runqueue state around.
 *
 * Task wakeup, specifically wakeups that involve migration, are horribly
 * complicated to avoid having to take two rq->locks.
 */
```

三条要点:

1. **调度器内部** (`__schedule`, `enqueue_task`, `dequeue_task`) — 必须持有本 CPU 的 `rq->lock`
2. **跨 CPU 唤醒** — 可能从其他 CPU 持锁做 enqueue。如果两个 CPU 不在同一个 LLC 域，会通过 IPI 把 enqueue 操作转发到目标 CPU，避免运行队列数据在 CPU 间反复跳转
3. **涉及迁移的唤醒** — 最复杂的部分。"horribly complicated to avoid having to take two rq->locks" — 为了性能，`try_to_wake_up` 只用一把 `rq->lock`，代价是代码复杂度极高

### p->state — 无锁写, 有锁读

```c
/*
 * p->state <- TASK_*:
 *
 *   is changed locklessly using set_current_state(), __set_current_state() or
 *   set_special_state(), see their respective comments, or by
 *   try_to_wake_up(). This latter uses p->pi_lock to serialize against
 *   concurrent self.
 */
```

```
写 p->state 的三条路径:

  路径 1: set_current_state(SLEEPING)    ← 进程自己写自己, 无锁 (谁会和自己竞争?)
  路径 2: __set_current_state(RUNNING)   ← 进程醒来恢复自己, 无锁
  路径 3: try_to_wake_up(p) → p->state = TASK_RUNNING  ← 别人唤醒我
            ↑ 必须持 p->pi_lock!         防止同时有两个 CPU 都来唤醒 p
```

核心思想: **自己改自己不需要锁，别人改我需要锁。** `pi_lock` 的唯一作用就是序列化多个并发唤醒方——保证一个 task 不会被两个 CPU 同时"唤醒成功"。

### p->on_rq — 三态, 必须持 rq->lock

```c
/*
 * p->on_rq <- { 0, 1 = TASK_ON_RQ_QUEUED, 2 = TASK_ON_RQ_MIGRATING }:
 *
 *   is set by activate_task() and cleared by deactivate_task()/block_task(),
 *   under rq->lock. Non-zero indicates the task is runnable, the special
 *   ON_RQ_MIGRATING state is used for migration without holding both
 *   rq->locks. It indicates task_cpu() is not stable, see task_rq_lock().
 *
 *   Additionally it is possible to be ->on_rq but still be considered not
 *   runnable when p->se.sched_delayed is true. These tasks are on the runqueue
 *   but will be dequeued as soon as they get picked again.
 */
```

`on_rq` 不是布尔值，是三态:

| 值 | 含义 | 谁设置 | 谁清除 |
|---|---|---|---|
| 0 | 不在任何运行队列上 | | `deactivate_task()` / `block_task()` |
| 1 (QUEUED) | 在运行队列上, 可运行 | `activate_task()` | 同上 |
| 2 (MIGRATING) | 正在跨 CPU 迁移 | 迁移代码 | 迁移完成后清除 |

`TASK_ON_RQ_MIGRATING` 是一个特殊的中间态——当需要把 task 从 CPU A 迁到 CPU B，但不想同时持两把 `rq->lock` 时，先把 `on_rq` 设为 2（表示"正在路上"），然后逐把锁操作。这意味着**`task_cpu(p)` 在 `on_rq == 2` 时不稳定**——读到的是旧值或过渡值。

还有一个微妙之处: `on_rq == 1` 但 `sched_delayed == true` → 进程在队列上但**暂时不可运行**，被 pick 到时立即 dequeue。这是 EEVDF 延迟排队的机制。

### p->on_cpu — 一个看起来简单但实际微妙的字段

```c
/*
 * p->on_cpu <- { 0, 1 }:
 *
 *   is set by prepare_task() before p is scheduled-in and cleared by
 *   finish_task() after p is scheduled-out, both under rq->lock.
 *
 *   [ The astute reader will observe that it is possible for two tasks
 *     on one CPU to have ->on_cpu = 1 at the same time. ]
 */
```

正常情况下同一 CPU 同时只有一个 task 在跑，`on_cpu` 应该也是独占的。为什么注释说可能同时有两个 `on_cpu == 1`？

**答案: 上下文切换的中间状态。**

```
时间线:
                prepare_task(next)       context_switch(prev → next)
  prev 还在跑   next->on_cpu = 1         CPU 从 prev 切换到 next
  prev->on_cpu = 1                       ↓
                     ↑              prev 已不在 CPU 上了
                 这个窗口!             prev->on_cpu 还是 1
  next->on_cpu 和 prev->on_cpu 同时为 1!    finish_task(prev)
                                            prev->on_cpu = 0
```

`prepare_task` 在切换前就把 next 的 `on_cpu` 设为 1，而 `finish_task` 在切换后才清除 prev 的 `on_cpu`。切换的瞬间，两个字段都是 1。

为什么不能先清 prev 再设 next？
- 如果在 prev 还在 CPU 上时就清 `on_cpu`，其他 CPU 看到 `on_cpu == 0` 会误以为 prev 已经离线，可能错误地发 IPI
- 如果在 next 已经跑了但 `on_cpu` 还是 0，其他 CPU 回看到 next 在跑但不认为它已在线

所以必须先设 next、再切、再清 prev——三个步骤不可重排。短暂的"双 1" 是设计中可接受的。

### task_cpu(p) — 三条修改规则

```c
/*
 * task_cpu(p): is changed by set_task_cpu(), the rules are:
 *
 *  - Don't call set_task_cpu() on a blocked task
 *  - for try_to_wake_up(), called under p->pi_lock
 *    This allows try_to_wake_up() to only take one rq->lock
 *  - for migration:
 *      under rq->lock:           move_queued_task(), detach_task()
 *      under double_rq_lock():   __migrate_swap_task(), push/pull
 */
```

三条规则定义了对 `task_cpu(p)` 的不同修改权限:

| 规则 | 持什么锁 | 适用场景 |
|---|---|---|
| 不要改阻塞 task 的 CPU | N/A | task 在睡觉, CPU 归属无所谓, 改了也白改 |
| `try_to_wake_up` 中改 | `p->pi_lock` | 唤醒方, 只持一把 rq->lock |
| 单 rq->lock 改 | 当前 rq->lock | 简单同 CPU 迁移 |
| double_rq_lock 改 | 两把 rq->lock | 跨 CPU 交换, push/pull |

**规则 2 是性能关键**: `try_to_wake_up` 可以只持 `p->pi_lock` 就改 `task_cpu(p)`。这意味着唤醒方在选 CPU 时不需要锁目标 CPU 的 rq。注释说 "This allows try_to_wake_up() to only take one rq->lock" — 一把锁原则是这里来的。

### 稳定场: task_rq_lock() 的双锁保证

```c
/*
 * System-calls and anything external will use task_rq_lock() which acquires
 * both p->pi_lock and rq->lock. As a consequence the state they change is
 * stable while holding either lock:
 *
 *  - sched_setaffinity():        p->cpus_ptr, p->nr_cpus_allowed
 *  - set_user_nice():            p->se.load, p->*prio
 *  - __sched_setscheduler():     p->sched_class, p->policy, p->*prio,
 *                                p->se.load, p->rt_priority, p->dl.dl_*
 *  - sched_setnuma():            p->numa_preferred_nid
 *  - sched_move_task():          p->sched_task_group
 *  - uclamp_update_active():     p->uclamp*
 */
```

**外部世界**（系统调用等）通过 `task_rq_lock(p)` 同时持有 `p->pi_lock` 和 `rq->lock`。之后在修改这些字段的整个过程中，两把锁共同保证稳定性。注释列的每个字段都有自己特定的修改路径：

```
p->cpus_ptr, nr_cpus_allowed    ← sched_setaffinity() (taskset 命令, cgroup cpuset)
                                   (第 6 节分析过)
p->se.load, p->*prio            ← set_user_nice() (renice 命令)
p->sched_class, p->policy       ← __sched_setscheduler() (chrt 命令切换实时/普通)
p->dl.dl_{runtime,deadline,...} ← 修改 DEADLINE 调度参数
p->numa_preferred_nid           ← NUMA 亲和性 (mbind 等)
p->sched_task_group             ← cgroup CPU 控制器移动进程
p->uclamp*                      ← util clamp (限制进程性能上限/下限)
```

### raw_spin_rq_lock_nested: 函数本身

```c
void raw_spin_rq_lock_nested(struct rq *rq, int subclass)
{
    preempt_disable();                      // ① 关抢占

    /* 快速路径: Core Scheduling 未启用 */
    if (sched_core_disabled()) {
        raw_spin_lock_nested(&rq->__lock, subclass);
        preempt_enable_no_resched();        // 开抢占, 但不触发调度
        return;
    }

    /* Core Scheduling 路径: 锁指针可能被并发修改 */
    for (;;) {
        lock = __rq_lockp(rq);              // ② 读有效锁指针
        raw_spin_lock_nested(lock, subclass); // ③ 拿锁
        if (likely(lock == __rq_lockp(rq))) {  // ④ 指针没变?
            preempt_enable_no_resched();
            return;
        }
        raw_spin_unlock(lock);               // ⑤ 变了, 释放重试
    }
}
```

Core Scheduling 启用时，同一个物理核的多个超线程的 rq 可能共享同一把锁。`__rq_lockp(rq)` 返回的指针可能被另一个 CPU 上的 `__sched_core_enable()` 并发修改。循环重试保证你拿到的锁和当前的指针一致。

`subclass` 参数告诉 lockdep 这是嵌套的同类型锁，不是死锁。`double_rq_lock` 中按地址排序先锁小的后锁大的，第二个锁传 `SINGLE_DEPTH_NESTING`:

```c
/* double_rq_lock — 按地址排序锁两把 rq->lock, 避免死锁 */
if (rq_order_less(rq2, rq1))
    swap(rq1, rq2);                            // rq1 = 地址较小的
raw_spin_rq_lock(rq1);
if (__rq_lockp(rq1) != __rq_lockp(rq2))        // 两把不同的锁?
    raw_spin_rq_lock_nested(rq2, SINGLE_DEPTH_NESTING);  // 拿第二把
else
    __acquire_ctx_lock(...);                   // 同一把 → 假装拿了
```

### `preempt_enable_no_resched` 的含义

```c
preempt_disable();                  // preempt_count >= 1
raw_spin_lock_nested(...);          // preempt_count >= 2 (自旋锁也禁止抢占)
preempt_enable_no_resched();        // preempt_count -= 1, 但不检查 TIF_NEED_RESCHED
/* preempt_count *MUST* be > 1 */   // 注释强调: 此时必须还持着锁!
```

为什么不能在这个位置做 `schedule()`？因为持着 `rq->lock`——如果在锁内调度，被调走的进程永远不可能释放这把锁，整条运行队列就被锁死了。`preempt_enable_no_resched` 跳过调度检查，让抢占保持禁用状态直到锁被释放。

### 锁规则的完整心智模型

```
操作类型                 持什么锁              可修改的字段
────────                ────────              ──────────
主动睡眠                无锁 (自己改自己)      p->state
被唤醒                   p->pi_lock            p->state, task_cpu
调度器内部               rq->lock              p->on_rq, p->on_cpu
迁移 (简单)              rq->lock              task_cpu
迁移 (跨CPU交换)         double_rq_lock        task_cpu, 运行队列顺序
外部 (系统调用)          pi_lock + rq->lock    p->cpus_ptr, p->prio,
                                               p->sched_class, p->policy,
                                               p->rt_priority, p->dl.*,
                                               p->se.load, p->uclamp*
```

逐步收紧的锁层：无锁（自己改自己）→ 单锁（pi_lock 或 rq_lock）→ 双锁（pi_lock + rq_lock）→ 双 rq 锁（两把 rq->lock）。

## 11. __schedule(): 内核上下文切换的完整流程

`__schedule()` 是调度器的心脏——`pick_next_task` 选下一个进程，`context_switch` 切过去。但它的代码结构被三个 `goto` 标签切分成了三条不等价的路径。

### 外层入口: schedule() → __schedule_loop() → __schedule()

```c
asmlinkage __visible void __sched schedule(void)
{
    struct task_struct *tsk = current;

    if (!task_is_running(tsk))          // 当前进程状态不是 RUNNING?
        sched_submit_work(tsk);          // 先清一下未完成的 I/O 块设备请求

    __schedule_loop(SM_NONE);           // 进入主循环
    sched_update_worker(tsk);           // 更新 worker 池统计
}
```

`schedule()` 入口两个检查: `task_is_running(tsk)` 验证当前进程确实不是 RUNNING 状态（如果是从 `wait_event` 路径进来，状态应该是 SLEEPING），`sched_submit_work` 在睡眠前刷新待处理的块设备 I/O——避免在持资源时睡眠死锁。

```c
static __always_inline void __schedule_loop(int sched_mode)
{
    do {
        preempt_disable();
        __schedule(sched_mode);              // 核心调度
        sched_preempt_enable_no_resched();
    } while (need_resched());               // TIF 还在? 再来!
}
```

`__schedule_loop` 是一个**重试循环**——如果 `__schedule` 从 `keep_resched` 路径返回，`TIF_NEED_RESCHED` 没有被清除，`need_resched()` 返回 true，循环再次进入 `__schedule`。这避免了返回用户态再走系统调用边界的开销。

### __schedule() 的三个 goto 标签

```c
static void __sched notrace __schedule(int sched_mode)
{
    struct task_struct *prev, *next;
    bool preempt = sched_mode > SM_NONE;
    struct rq *rq;
    int cpu;

    cpu = smp_processor_id();
    rq = cpu_rq(cpu);
    prev = rq->curr;

    /* ① 准备阶段: 关中断、RCU 上下文切换通知、拿 rq->lock */
    local_irq_disable();
    rcu_note_context_switch(preempt);
    rq_lock(rq, &rf);
    smp_mb__after_spinlock();            // 与 signal_wake_up 配对

    /* ② 读取 prev 状态 — 确定它是被抢占还是自己睡觉 */
    prev_state = READ_ONCE(prev->__state);

    /* SM_IDLE: idle 进程调度, 没任务就选自己继续 idle */
    if (sched_mode == SM_IDLE) {
        if (!rq->nr_running && !scx_enabled()) {
            next = prev;                 // 没可运行的任务, 继续跑自己
            goto picked;
        }
    }
    /* 不是被抢占 + prev 不是 RUNNING → 尝试摘除 */
    else if (!preempt && prev_state) {
        try_to_block_task(rq, prev, &prev_state,
                          !task_is_blocked(prev));
        //  ↑ 正常睡眠→摘下; 等mutex→留队; 有信号→改RUNNING (第 10 节)
    }

    /* ③ 选举循环 */
pick_again:                              // ← 标签 1: 选举入口
    next = pick_next_task(rq, rq->donor, &rf);   // EEVDF: 用 prio_less() 选
    rq_set_donor(rq, next);
    rq->next_class = next->sched_class;

    /* 选中的进程在等 mutex? → 代理执行 */
    if (unlikely(task_is_blocked(next))) {
        next = find_proxy_task(rq, next, &rf);   // 找锁持有者
        if (!next)
            goto pick_again;                     // 找不到 → 重新选举
        if (next == rq->idle)
            goto keep_resched;                   // 代理是 idle → 保留标志重试
    }

picked:                                  // ← 标签 2: 正常切换路径
    clear_tsk_need_resched(prev);        // 撕标签 (第 8 节)
    clear_preempt_need_resched();

keep_resched:                           // ← 标签 3: 短路 — 不撕标签, 不切换
    rq->last_seen_need_resched_ns = 0;

    /* ④ 执行硬件上下文切换 */
    if (prev != next) {
        rq->nr_switches++;
        RCU_INIT_POINTER(rq->curr, next);
        context_switch(rq, prev, next);  // 保存寄存器 → 恢复 next
    }

    rq_unlock(rq, &rf);
}
```

### 三个标签的语义 — 三种终点

```
pick_next_task 的结果               goto 到哪      clear TIF?    context_switch?
────────────────────               ────────       ──────────    ──────────────
选中正常进程                        picked          ✓  撕标签     ✓  切换
选中阻塞进程 + 代理是 idle           keep_resched    ✗  不撕      ✗  不切换
选中阻塞进程 + 找不到代理            pick_again      重新选举       重新选举
```

`picked` 是正常路径 — 选出了该跑的进程，清标志，切过去。

`keep_resched` 是短路 — **不撕标签、不切换**。`TIF_NEED_RESCHED` 还在，外层的 `__schedule_loop` 循环马上再次进入 `__schedule` 重试。保留标志的语义是"这次没选出有效的，赶紧再来"。

`pick_again` 是重试 — 选中的进程在等锁，但锁持有者不在这个 CPU 上，没法代理。跳回选举入口，`pick_next_task` 会跳过刚才选中的阻塞进程，选队列中下一个可运行的。

### 为什么是三个标签而不是 if/else

`picked` 和 `keep_resched` 之间的代码是**落差执行**——从 `picked` 进来会执行 `clear_tsk_need_resched`，从 `keep_resched` 进来跳过它。传统写法需要：

```c
bool should_clear = true;
if (next == idle) {
    should_clear = false;
    goto out;
}
/* ... 中间可能有其他条件 ... */
out:
if (should_clear)
    clear_tsk_need_resched();
```

用 goto 标签，代码是直线落下的：路径 1 自然经过 `clear_tsk_need_resched`，路径 2 直接跳到 `clear` 之后。不需要 `if` 和中间变量，更接近汇编思维（`jmp` 到特定位置）。

### try_to_block_task: 摘除决策

```c
static bool try_to_block_task(struct rq *rq, struct task_struct *p,
                              unsigned long *task_state_p, bool should_block)
{
    /* 睡觉中的进程收到信号 → 别睡了, 直接改 RUNNING, 留在队列上 */
    if (signal_pending_state(task_state, p)) {
        WRITE_ONCE(p->__state, TASK_RUNNING);
        return false;
    }

    /* should_block=false → 等 mutex 的, 留在队列上 (代理执行需要) */
    if (!should_block)
        return false;

    /* 正常睡眠 → 真正从运行队列摘除 */
    block_task(rq, p, flags);  // → deactivate_task()
    return true;
}
```

| 状态 | should_block | 信号? | 结果 |
|---|---|---|---|
| INTERRUPTIBLE 睡眠 | true | 无 | 摘下队列 |
| 睡眠中收到信号 | true | 有 | 改为 RUNNING, 不摘 |
| mutex 阻塞 (BLOCKED) | false | 无 | 留队, 等代理执行 |

### 代理执行: find_proxy_task

当选中的进程在等 mutex 时:

```
实际跑的人 (代理):                     CPU 时间的所有者 (donor):
─────────────────                     ──────────────────────────
mutex 持有者 A                        等待者 B (BLOCKED)
用 A 的栈、A 的代码                    pick_next_task 选中 B
A 释放锁后 → 切回 B                    find_proxy_task → 返回 A
                                       B 把 CPU 时间 "捐" 给 A
```

**等锁的进程不下队列，选到它时找锁持有者代为执行。持有者越早释放锁，等锁的进程越早跑起来。**

### 完整控制流图

```
schedule()
  └→ __schedule_loop(SM_NONE)
       └→ do {
            preempt_disable()

            __schedule(SM_NONE)
              │
              ├─ 准备: 关中断, 拿 rq->lock, 内存屏障
              ├─ try_to_block_task(prev)    ← 摘除决策
              │
              ├─ pick_again ────────────────────────────┐
              │   pick_next_task(rq, rq->donor)          │
              │   if (task_is_blocked)                   │
              │     find_proxy_task()                    │
              │       → !next → goto pick_again ─────────┘
              │       → idle → goto keep_resched ────┐
              │                                      │
              ├─ picked ─────────────────────────┐   │
              │   clear_tsk_need_resched(prev)    │   │
              │   clear_preempt_need_resched()    │   │
              │                                   │   │
              ├─ keep_resched ────────────────────┘   │
              │   (不撕标签, 不切换)                    │
              │                                      │
              ├─ if (prev != next)                    │
              │     context_switch(rq, prev, next)    │
              └─ rq_unlock(rq, &rf)

            sched_preempt_enable_no_resched()
          } while (need_resched());
              ↑ 从 keep_resched 返回时 TIF 还在 → 循环重试!
```

### 和 Python/Go 协程的对应

| | Linux `__schedule` | Go `schedule()` | Python `asyncio` |
|---|---|---|---|
| 调度对象 | `task_struct` | goroutine (g) | coroutine (生成器帧) |
| 保存什么 | 全部寄存器 + 内核栈 | ~5 个寄存器 | 帧对象 f_lasti |
| 怎么选 | EEVDF (`prio_less`) | work stealing | FIFO 就绪队列 |
| 怎么切 | `context_switch` (硬件) | `gogo` 汇编 | `send(None)` |
| 切换代价 | ~1000-2000 周期 | ~100 周期 | ~200 Python 字节码周期 |
| 触发方式 | 抢占式 (时钟中断) | 协作式+抢占检查点 | 完全协作式 (`await`) |

核心区别: `__schedule` 切换的是**不同的地址空间和硬件上下文**（两个不同的进程，可能来自不同用户程序）。Go 和 Python 的切换在**同一地址空间、同一用户态进程内**——只是换了栈和几个寄存器，代价差两个数量级。但三步模式完全一样: **保存当前 → 选下一个 → 切过去**。

## 12. 调度器优先级比较: __task_prio 与 prio_less

`pick_next_task` 用这两个函数决定从运行队列中选谁。它们把四种调度类（STOP、DL/RT、CFS、IDLE）的不同优先级体系压成一条统一的数值线。

### __task_prio: 异构优先级压成一条线

```c
/* kernel/sched/core.c — 越小越优先 */
static inline int __task_prio(const struct task_struct *p)
{
    if (p->sched_class == &stop_sched_class)   // STOP 类 — 绝对最高优先级
        return -2;
    if (p->dl_server)                            // DL Server 代理 — CFS 借 DL 带宽
        return -1;
    if (rt_or_dl_prio(p->prio))                 // RT/DEADLINE — 优先级 0..99
        return p->prio;
    if (p->sched_class == &idle_sched_class)    // IDLE 类 — 绝对最低
        return MAX_RT_PRIO + NICE_WIDTH;         // 140
    if (task_on_scx(p))                          // sched_ext — 可扩展调度
        return MAX_RT_PRIO + MAX_NICE + 1;      // 120
    return MAX_RT_PRIO + MAX_NICE;              // 普通 CFS — 119
}
```

数值线:

```
   -2        -1      0 .. 99     100 .. 139    140
   │         │       │           │             │
  STOP   DL_SERVER   RT/DEADLINE   CFS/NICE    IDLE
 (最高) (代理执行)  (硬实时)     (普通进程)   (最低)
```

CFS 的 `p->prio` 是 100..139（nice -20=100, nice +19=139），但 `__task_prio` 固定返回 119。因为 CFS 内部不用严格优先级——它用 vruntime 做加权公平分配。返回 119 只用于和 RT/IDLE 比较，两个 CFS 进程之间的比较走 `cfs_prio_less`。

### prio_less: 三层决策选出"谁更该跑"

```c
/* kernel/sched/core.c — 四行数学定义在注释里 */
/*
 * l(a,b)             ← prio_less(a,b) — "a 比 b 更该跑吗?"
 * le(a,b) := !l(b,a) ← "a 优先于或等于 b"
 * g(a,b)  := l(b,a)  ← "b 比 a 更该跑" = "a 比 b 更不该跑"
 * ge(a,b) := !l(a,b) ← "a 不优于 b"
 */

static inline bool prio_less(const struct task_struct *a,
                             const struct task_struct *b, bool in_fi)
{
    int pa = __task_prio(a), pb = __task_prio(b);

    /* 第一层: 比较全局优先级 */
    if (-pa < -pb) return true;     // 取负后, 更大负数 = 更高优先
    if (-pb < -pa) return false;

    /* 第二层: 全局优先级相同 → 调度类内部比较 */
    if (pa == -1) {                 // 两个都是 DL / DL Server
        /* 比 deadline — 考虑带宽和截止时间 */
        a_dl = a->dl_server ? a->dl_server : &a->dl;
        b_dl = b->dl_server ? b->dl_server : &b->dl;
        return !dl_time_before(a_dl->deadline, b_dl->deadline);
    }

    if (pa == MAX_RT_PRIO + MAX_NICE)   // 119 = 两个都是 CFS
        return cfs_prio_less(a, b, in_fi);  // 比 vruntime

    if (pa == MAX_RT_PRIO + MAX_NICE + 1) // 120 = 两个都是 sched_ext
        return scx_prio_less(a, b, in_fi);

    return false;                    // 完全相同
}
```

决策树:

```
                    prio_less(a, b)
                          │
              全局优先级 (__task_prio) 不同?
                    │
              ┌─────┴─────┐
             是           否 (同一调度类)
              │               │
         返回更小者      ┌────┼────┬────┐
                       DL   CFS  EXT  RT
                     deadline vruntime  scx  优先级值
```

### 为什么用 -pa < -pb 而不是 pa > pb

`__task_prio` 定义的是"越小越优先"。但 `prio_less` 的语义是"a 该先于 b 吗"。取负后比较:

```
原始 (越小越优先):              取负后 (比数值):
  STOP(-2) 最高                  STOP → +2
  RT(50)                         RT   → -50
  CFS(119)                       CFS  → -119

  -50 > -119 → RT 优先于 CFS ✓
  (等价于 pa > pb → pa 更优先)
```

用取负是因为 `prio_less` 函数名暗示"小于比较"，取负后保持了这个语义连贯性。

### DL Server 的特殊处理: 代理执行下的 deadline 比较

```c
if (pa == -1) {
    a_dl = &a->dl;
    if (a->dl_server)             // a 不是原生 DL 进程, 是借 DL Server 跑的 CFS
        a_dl = a->dl_server;      // 转向 DL Server 的 DL entity (有真实 deadline)
    // ...
    return !dl_time_before(a_dl->deadline, b_dl->deadline);
}
```

`dl_server` 是代理执行的一部分 — CFS 进程可以借 DEADLINE 调度类的 CPU 带宽短时间提升优先级。`__task_prio` 返回 -1（DL 级别），但实际的 deadline 在 `dl_server` 的 DL entity 里，指针要切换过去。

### pick_next_task 怎么用 prio_less

```c
/* EEVDF: 遍历运行队列候选者, 用 prio_less 选出最优 */
for_each_task_in_order(rq) {
    if (!best) { best = p; continue; }
    if (prio_less(p, best, false))    // p 比当前最佳更该跑?
        best = p;                      // 替换最佳
}
```

四种比较只需一个函数 — `prio_less(a, best)` 就是 `l(a, best)`。不需要"不小于"或"等于"的单独实现。

## 13. task_struct 布局技巧: 类型选择、安全加固与对齐

### unsigned int vs int: 什么时候用哪个

`task_struct` 中的整数字段选择不是随意的。两类字段的两个原则:

**用 `unsigned int` — 值不可能为负:**

```c
unsigned int flags;        // 位掩码 — 最高位是数据的一部分, 不是符号位
                           // PF_SPEC_IB_DISABLE = 0x80000000, bit 31
                           // 如果 flags 是 int, 对 0x80000000 移位是 UB
unsigned int ptrace;       // ptrace 标志 — 同样是位掩码
unsigned int wakee_flips;  // 翻转计数 — 只增不减, 永远非负
```

**用 `int` — 需要 -1 哨兵:**

```c
int on_cpu;                // CPU 编号, -1 表示 "不在任何 CPU 上"
int wake_cpu;              // 唤醒目标 CPU, -1 表示 "让调度器自己选"
int recent_used_cpu;       // 最近使用的 CPU, -1 表示 "还没有记录"
int on_rq;                 // 调度器内部有复杂状态编码, 可能设负值
```

如果 `on_cpu` 用 `unsigned int`，就无法表达"不在任何 CPU 上"——`unsigned` 的最小值是 0，而 0 是合法的 CPU 编号。

法则: **值永远不会是负数 → `unsigned`。值可能是负数（包括 -1 哨兵）→ `int`。**

### unsigned :0 — 零宽度位域, 强制机器字边界

```c
unsigned sched_task_hot:1;

/* Force alignment to the next boundary: */
unsigned :0;                   // ← 零宽度位域, 强制跳到下一个 unsigned int 边界

/* Unserialized, strictly 'current' */
// 下面的字段只允许 current 自己访问, 不受 rq->lock 保护
```

`unsigned :0` 不是分配一个字段，而是**人为插入 padding 到下一个存储单元**。它前面的字段属于"序列化区域"（rq->lock 保护），后面的字段属于"非序列化区域"（仅 current 可访问）。

作用: **用硬件对齐隔离两类访问模式的字段**，防止写撕裂——一个 CPU 修改锁保护字段时，不会同时影响另一个 CPU 正在读取的无锁字段。

```
  [word N — 序列化字段]          [word N+1 — 非序列化字段]
  ┌────────────────────┐        ┌─────────────────────┐
  │ ...位域...         │        │ 只允许 current 读写   │
  │ sched_task_hot:1   │        │ (无锁, 不序列化)     │
  │ unsigned :0 → 边界 →       │                     │
  └────────────────────┘        └─────────────────────┘
```

### randomized_struct_fields — 编译期布局随机化 (randstruct)

```c
struct task_struct {
    /* 调度器热路径字段 — 固定位置, 不随机化 */

    randomized_struct_fields_start
    /* 从这里开始, GCC randstruct 插件随机打乱所有字段顺序 */
    void                 *stack;
    refcount_t            usage;
    unsigned int          flags;
    struct wake_q_node    wake_q;
    /* ... 信号、文件、内存等所有其他字段 ... */
    randomized_struct_fields_end
} __attribute__ ((aligned (64)));
```

randstruct 是 GCC 插件，在**编译时**随机重排标记之间的所有字段。每个编译出的内核，`task_struct` 内部布局都不同:

```
内核编译 A:                        内核编译 B:
  stack    @ offset 0x530            stack   @ offset 0x7A0  ← 和 A 完全不同!
  usage    @ offset 0x538            usage   @ offset 0x2C8
  flags    @ offset 0x540            flags   @ offset 0x910
```

这是防漏洞利用的 — exploit 经常硬编码字段偏移量去提权:

```c
/* 攻击代码 (不启用 randstruct 时有效) */
#define CRED_OFFSET 0x530
*(void **)((char *)p + 0x530) = init_cred;  // 把自己的 cred 改成 root
```

启用 randstruct 后, 每个内核的 `CRED_OFFSET` 都不同, 攻击者无法提前知道偏移量。

### __attribute__((aligned(64))) — 缓存行对齐, 消除假共享

```c
} __attribute__ ((aligned (64)));    // task_struct 按 64 字节对齐
```

`task_struct` 整体按 64 字节对齐, 防止两个 task_struct 挤在同一个缓存行内导致 false sharing:

```
没有对齐:                          64 字节对齐:
  ┌── cache line ──┐                ┌── cache line ──┐┌── cache line ──┐
  │ task A 后半│task B 前半│         │    task A      ││    task B      │
  └──────────────────────┘           └────────────────┘└────────────────┘
   两个 CPU 共享一行                   各占一行, 互不干扰
   → 互相 invalidate
   → false sharing 性能下降
```

配合 slab 分配器的 `task_struct_cache`，确保每次分配的 task_struct 都从缓存行边界开始。

## 14. 学习路径

```
1. 先理解进程状态
   → sched.h: TASK_RUNNING / TASK_INTERRUPTIBLE / TASK_UNINTERRUPTIBLE

2. 再理解 sleep/wakeup 契约
   → wait_queue_demo/01_basic_wait_queue.c
   → wait_queue_demo/README.md (set_current_state / try_to_wake_up 注释)
   → 核心: 条件检查循环 + 内存屏障配对

3. 再理解中层 wait_queue
   → wait_queue_demo/02_wake_q_internal.c
   → 核心: __wake_up_common → autoremove_wake_function → try_to_wake_up

4. 再理解底层优化 wake_q_node
   → wake_q_demo/README.md
   → wake_q_demo/01 → 06 逐步深入
   → 核心: 侵入式链表 + lastp + cmpxchg 三态 + 延迟唤醒

5. 再理解调度器核心机制
   → process-management.md 第 6 节: last_wakee / wakee_flips (唤醒关系探测)
   → process-management.md 第 6 节: cpus_ptr / nr_cpus_allowed (CPU 亲和性)
   → process-management.md 第 7 节: thread_union (内核栈布局)
   → process-management.md 第 8 节: TIF_NEED_RESCHED (调度通知机制)
   → process-management.md 第 9 节: sched_change_begin/end (事务原语)

6. 最后看全景
   → wait_queue_demo/03_combined_demo.c  (三层架构)
   → process-management.md (进程管理睡眠/唤醒子系统全景图)
```

## 15. 相关文件索引

| 内核文件 | 内容 |
|---|---|
| `include/linux/sched.h` | `task_struct`、进程状态、`set_current_state`、`thread_union`、`TIF_NEED_RESCHED` |
| `include/linux/sched/wake_q.h` | `wake_q_head`、`DEFINE_WAKE_Q` |
| `include/linux/wait.h` | `wait_queue_head`、`wait_event` 系列宏 |
| `kernel/sched/core.c` | `__wake_q_add`、`wake_up_q`、`try_to_wake_up`、`__schedule`、`__task_prio`、`prio_less` |
| `kernel/sched/wait.c` | `__wake_up_common`、`autoremove_wake_function` |
| `kernel/sched/fair.c` | `wake_wide()`、`wakee_flips` 唤醒关系探测 |
| `kernel/sched/sched.h` | `sched_change_ctx`、`DEFINE_CLASS(sched_change)`, DEQUEUE/ENQUEUE flag |
| `kernel/fork.c` | `alloc_thread_stack_node()`、`dup_task_struct()` |
| `scripts/gcc-plugins/randomize_layout_plugin.c` | randstruct — task_struct 布局随机化 |

| demo | 内容 |
|---|---|
| `wake_q_demo/` | 6 个递进 demo + README: wake_q_node 底层链表机制 |
| `wait_queue_demo/` | 3 个递进 demo + README: wait_queue 中层 + 内存屏障协议 |
