/*
 * 02_wake_q_internal.c — 从 wait_queue 底层追踪到 wake_q_node
 *
 * 编译运行: gcc -Wall -o 02_wake_q_internal 02_wake_q_internal.c && ./02_wake_q_internal
 * 核心问题: wake_up_interruptible() 内部到底发生了什么?
 *           wake_q_node 在中间扮演了什么角色?
 *
 * 完整调用链 (基于内核源码 kernel/sched/wait.c 和 core.c):
 *
 *   wake_up_interruptible(&wq)
 *     └→ __wake_up(&wq, TASK_INTERRUPTIBLE, ...)
 *          └→ __wake_up_common(&wq, ...)
 *               │  遍历等待队列的每个 entry
 *               │  调用 entry->func(entry, mode, ...)
 *               │
 *               └→ autoremove_wake_function(entry, mode, ...)
 *                    └→ default_wake_function(entry, mode, ...)
 *                         └→ try_to_wake_up(task, ...)
 *                              │
 *                              │  如果 task 在另一个 CPU 上:
 *                              │    ttwu_queue() → 用 per-CPU wake_q 延迟
 *                              │
 *                              └→ 最终: task 状态变为 TASK_RUNNING
 *                                   task 被放回运行队列
 *
 *  另外, 还有一个独立的优化模式 (不是 wait_queue 内部, 而是调用者用的):
 *
 *   持锁的调用者:
 *     spin_lock(&lock);
 *     // ... 不能直接 wake_up_process (太贵, 且持锁)
 *     wake_q_add(&wake_q, task);   // 轻量: 只是把 task 记下来
 *     spin_unlock(&lock);
 *     wake_up_q(&wake_q);          // 解锁后, 批量唤醒
 *
 * 本 demo 模拟这两种路径, 并展示它们的连接点。
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
 * 内核数据结构 (完整版)
 * ============================================================ */

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* --- 底层: wake_q_node (我们已经很熟悉了) --- */
struct wake_q_node {
	struct wake_q_node *next;
};

#define WAKE_Q_TAIL  ((struct wake_q_node *) 0x01)

struct wake_q_head {
	struct wake_q_node *first;
	struct wake_q_node **lastp;
};

/* 前向声明 */
struct list_node;

/* --- task_struct: 把 wait_queue 和 wake_q 都放进去 --- */
struct task_struct {
	const char *name;
	int pid;
	int state;               /* 0=TASK_RUNNING, 1=TASK_INTERRUPTIBLE */
	int condition;           /* 每个 task 私有的等待条件 */
	int refcnt;

	/* 这个 task 同时是 wait_queue 和 wake_q 的一部分 */
	struct wake_q_node      wake_q;       /* 底层: 延迟唤醒链表节点 */
};

/* --- 双向链表 (用于 wait_queue) --- */
struct list_node {
	struct list_node *next, *prev;
};

/* --- wait_queue_entry: 等待队列条目 --- */
struct wait_queue_entry {
	void                   *private;     /* 指向 task_struct */
	int                   (*func)(struct wait_queue_entry *, unsigned int,
					int, void *); /* 唤醒回调 */
	struct list_node        entry;       /* 链表节点 */
};

/* --- wait_queue_head: 等待队列头 --- */
struct wait_queue_head {
	struct list_node        head;        /* 哨兵头节点 */
};

/* ============================================================
 * 辅助函数
 * ============================================================ */

static void *cmpxchg_relaxed(void *ptr, void *old_val, void *new_val)
{
	void **p = (void **)ptr;
	if (*p == old_val) { *p = new_val; return old_val; }
	return *p;
}

static void get_task_struct(struct task_struct *t) { t->refcnt++; }
static void put_task_struct(struct task_struct *t) { t->refcnt--; }

/* ============================================================
 * 底层: wake_q_node 操作 (wait_queue_demo 复用之前的知识)
 * ============================================================ */

static void wake_q_init(struct wake_q_head *head)
{
	head->first = WAKE_Q_TAIL;
	head->lastp = &head->first;
}

static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	struct wake_q_node *node = &task->wake_q;
	if (cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL))
		return false;
	*head->lastp = node;
	head->lastp = &node->next;
	return true;
}

static void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	if (__wake_q_add(head, task))
		get_task_struct(task);
}

static void wake_up_q(struct wake_q_head *head)
{
	struct wake_q_node *node = head->first;
	while (node != WAKE_Q_TAIL) {
		struct task_struct *task;
		task = container_of(node, struct task_struct, wake_q);
		node = node->next;
		task->wake_q.next = NULL;
		printf("            [wake_up_q] 真正唤醒 %s (pid=%d)\n",
		       task->name, task->pid);
		task->state = 0;  /* TASK_RUNNING */
		put_task_struct(task);
	}
}

/* ============================================================
 * 中层: wait_queue 操作
 * ============================================================ */

static void init_waitqueue_head(struct wait_queue_head *wq)
{
	wq->head.next = &wq->head;
	wq->head.prev = &wq->head;
}

/*
 * autoremove_wake_function — 默认的唤醒回调
 *
 * 内核源码 (kernel/sched/wait.c):
 *   int autoremove_wake_function(struct wait_queue_entry *wq_entry,
 *                                 unsigned mode, int sync, void *key)
 *   {
 *       int ret = default_wake_function(wq_entry, mode, sync, key);
 *       if (ret)
 *           list_del_init(&wq_entry->entry);
 *       return ret;
 *   }
 *
 * 做的事情:
 *   1. 调用 default_wake_function → 最终到 try_to_wake_up
 *   2. 如果唤醒成功, 把自己从等待队列中摘除
 */
static int autoremove_wake_function(struct wait_queue_entry *wq_entry,
				    unsigned int mode, int sync, void *key)
{
	struct task_struct *task = wq_entry->private;
	(void)mode; (void)sync; (void)key;

	printf("          autoremove_wake_function: 尝试唤醒 %s\n", task->name);

	/* default_wake_function → try_to_wake_up 的模拟 */
	task->state = 0;   /* TASK_RUNNING */
	printf("          try_to_wake_up(%s) → state=RUNNING\n", task->name);

	/* 把自己从等待队列中摘除 */
	wq_entry->entry.prev->next = wq_entry->entry.next;
	wq_entry->entry.next->prev = wq_entry->entry.prev;
	printf("          list_del_init → 从等待队列摘除 %s\n", task->name);

	return 1;  /* 成功 */
}

/*
 * __wake_up_common — 遍历等待队列, 调用每个 entry 的 func
 *
 * 内核源码 (kernel/sched/wait.c):
 *   static int __wake_up_common(struct wait_queue_head *wq_head, ...)
 *   {
 *       list_for_each_entry_safe_from(curr, next, &wq_head->head, entry) {
 *           ret = curr->func(curr, mode, wake_flags, key);
 *           ...
 *       }
 *   }
 */
static void __wake_up_common(struct wait_queue_head *wq,
			     unsigned int mode, int nr_exclusive,
			     void *key, const char *caller)
{
	struct list_node *pos;
	int count = 0;

	printf("\n    [%s] __wake_up_common: 遍历等待队列\n", caller);

	for (pos = wq->head.next; pos != &wq->head; pos = pos->next) {
		struct wait_queue_entry *entry;
		entry = container_of(pos, struct wait_queue_entry, entry);

		count++;
		printf("      处理 entry %d: private=%s\n",
		       count, ((struct task_struct *)entry->private)->name);

		entry->func(entry, mode, 0, key);

		/* 因为 autoremove 会删除自己, 需要从 head 重新开始 */
		if (wq->head.next == &wq->head)
			break;
		pos = &wq->head; /* 重置, for 循环推进到 wq->head.next */
	}
	printf("    共处理 %d 个条目\n", count);
}

/*
 * wake_up_interruptible — 驱动开发者调用的 API
 *
 * 内核源码 (include/linux/wait.h):
 *   #define wake_up_interruptible(x) \
 *       __wake_up(x, TASK_INTERRUPTIBLE, 1, NULL)
 */
static void wake_up_interruptible(struct wait_queue_head *wq,
				  const char *caller)
{
	printf("  [%s] wake_up_interruptible(&wq)\n", caller);
	printf("    → __wake_up(wq, TASK_INTERRUPTIBLE, 1, NULL)\n");
	__wake_up_common(wq, 0x0001 /* TASK_INTERRUPTIBLE */, 1, NULL, caller);
}

/* 添加一个等待条目到队列 */
static void add_wait_queue(struct wait_queue_head *wq,
			   struct wait_queue_entry *entry)
{
	entry->entry.next = &wq->head;
	entry->entry.prev = wq->head.prev;
	wq->head.prev->next = &entry->entry;
	wq->head.prev = &entry->entry;
}

/* ============================================================
 * 场景演示
 * ============================================================ */

static struct task_struct t1, t2, t3;

/*
 * 场景 1: 标准 wait_queue 路径 (没有 wake_q_node 参与)
 *
 *   这是 wait_queue 的正常运作方式:
 *     wake_up_interruptible → __wake_up_common → autoremove_wake_function
 *     → try_to_wake_up → task 直接被唤醒
 */
static void scenario1_standard_wait_queue(void)
{
	printf("╔══════════════════════════════════════════════════════╗\n");
	printf("║  场景 1: 标准 wait_queue 路径                        ║\n");
	printf("║  wake_up_interruptible 内部调用链                    ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n\n");

	struct wait_queue_head wq;
	struct wait_queue_entry e1 = { .private = &t1, .func = autoremove_wake_function };
	struct wait_queue_entry e2 = { .private = &t2, .func = autoremove_wake_function };

	init_waitqueue_head(&wq);
	add_wait_queue(&wq, &e1);
	add_wait_queue(&wq, &e2);

	printf("初始状态: 等待队列中有 %s 和 %s\n\n", t1.name, t2.name);

	printf("调用链:\n");
	printf("  wake_up_interruptible(&wq)\n");
	printf("    └→ __wake_up(wq, TASK_INTERRUPTIBLE, ...)\n");
	printf("         └→ __wake_up_common(wq, ...)\n");
	printf("              遍历每个 entry, 调用 entry->func():\n");
	printf("              └→ autoremove_wake_function(entry)\n");
	printf("                   ├→ try_to_wake_up(task)   ← 真正唤醒\n");
	printf("                   └→ list_del_init(entry)   ← 摘除条目\n\n");

	wake_up_interruptible(&wq, "driver_code");

	printf("\n结果: %s state=%d, %s state=%d\n",
	       t1.name, t1.state, t2.name, t2.state);
}

/*
 * 场景 2: wake_q_node 优化模式
 *
 *   有些调用者持锁时不能直接做昂贵的唤醒操作,
 *   于是用 wake_q_node 把唤醒推迟到解锁之后:
 *
 *     spin_lock(&lock);
 *     // 发现 task 需要唤醒, 但不能直接 wake_up_process (太贵)
 *     wake_q_add(&wake_q, task);    // 只是记录
 *     spin_unlock(&lock);
 *     wake_up_q(&wake_q);           // 批量执行真正的唤醒
 *
 *   这就是 wake_q_node 和 wait_queue 的连接点:
 *   wait_queue 的 wake 回调最终调用 try_to_wake_up,
 *   try_to_wake_up 内部可能使用 wake_q_node 来延迟唤醒。
 */
static void scenario2_wake_q_deferral(void)
{
	printf("\n\n╔══════════════════════════════════════════════════════╗\n");
	printf("║  场景 2: wake_q_node 延迟唤醒优化                   ║\n");
	printf("║  持锁时记录, 解锁后批量唤醒                          ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n\n");

	printf("为什么需要 wake_q_node?\n\n");
	printf("  持锁时调用 wake_up_process 的代价很大:\n");
	printf("    - 可能触发跨 CPU 调度\n");
	printf("    - 可能触发 IPI (核间中断)\n");
	printf("    - 可能在锁保护的临界区里做太多事\n\n");
	printf("  wake_q_node 的解决方案:\n");
	printf("    持锁时只做轻量的记录 (wake_q_add)\n");
	printf("    解锁后再做昂贵的唤醒 (wake_up_q)\n\n");

	/* 重置 task 状态 */
	t1.state = 1;  /* sleeping */
	t2.state = 1;
	t3.state = 1;

	struct wake_q_head wq;
	wake_q_init(&wq);

	printf("--- 模拟: 持锁时发现 t1, t2, t3 需要被唤醒 ---\n\n");

	printf("spin_lock(&lock);\n");

	/* 持锁阶段: 只记录, 不真正唤醒 */
	printf("  wake_q_add(&wq, %s);  // 轻量! 只是链表追加\n", t1.name);
	wake_q_add(&wq, &t1);

	printf("  wake_q_add(&wq, %s);  // 轻量! 只是链表追加\n", t2.name);
	wake_q_add(&wq, &t2);

	printf("  wake_q_add(&wq, %s);  // 轻量! 只是链表追加\n", t3.name);
	wake_q_add(&wq, &t3);

	printf("\n  队列状态 (持锁期间):\n");
	printf("  %s → %s → %s → WAKE_Q_TAIL\n", t1.name, t2.name, t3.name);

	printf("\nspin_unlock(&lock);\n");
	printf("(锁已释放, 现在可以安全地做批量唤醒了)\n\n");

	printf("wake_up_q(&wq);  ← 批量执行所有真正的唤醒:\n\n");
	wake_up_q(&wq);

	printf("\n结果: %s(state=%d), %s(state=%d), %s(state=%d) 全部被唤醒\n",
	       t1.name, t1.state, t2.name, t2.state, t3.name, t3.state);
}

/*
 * 场景 3: 两层结合 — wait_queue 的 waker 使用 wake_q_node
 *
 *   这是 "全栈" 视角:
 *     上层: 驱动用 wait_event / wake_up
 *     中层: wait_queue 条目和 autoremove_wake_function
 *     底层: waker 在持锁时用 wake_q_node 延迟唤醒
 */
static void scenario3_two_layers(void)
{
	printf("\n\n╔══════════════════════════════════════════════════════╗\n");
	printf("║  场景 3: 两层结合 — 完整调用栈                      ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n\n");

	printf("完整的两层架构:\n\n");
	printf("  ┌──────────────────────────────────────────────────┐\n");
	printf("  │  上层: wait_queue API                           │\n");
	printf("  │  ┌────────────────────────────────────────────┐ │\n");
	printf("  │  │ 进程 A:                                     │ │\n");
	printf("  │  │   wait_event_interruptible(wq, cond)        │ │\n");
	printf("  │  │   → 加入等待队列 → 睡眠                     │ │\n");
	printf("  │  │                                            │ │\n");
	printf("  │  │ 进程 B (持锁):                              │ │\n");
	printf("  │  │   cond = 1;  // 修改条件                    │ │\n");
	printf("  │  │   wake_up_interruptible(&wq)                │ │\n");
	printf("  │  │     → __wake_up_common 遍历等待队列          │ │\n");
	printf("  │  └────────────────────────────────────────────┘ │\n");
	printf("  │                      │                          │\n");
	printf("  │                      ▼                          │\n");
	printf("  │  ┌────────────────────────────────────────────┐ │\n");
	printf("  │  │  底层: wake_q_node 优化                     │ │\n");
	printf("  │  │                                            │ │\n");
	printf("  │  │  如果 waker 持锁:                           │ │\n");
	printf("  │  │    wake_q_add(&wq, task) ← 只记录           │ │\n");
	printf("  │  │    spin_unlock(&lock)                       │ │\n");
	printf("  │  │    wake_up_q(&wq) ← 批量真正唤醒             │ │\n");
	printf("  │  └────────────────────────────────────────────┘ │\n");
	printf("  └──────────────────────────────────────────────────┘\n\n");

	/* --- 模拟这个两层场景 --- */
	t1.state = 1;  /* sleeping */
	t2.state = 1;
	t3.state = 1;
	t1.condition = 0;
	t2.condition = 0;
	t3.condition = 0;

	/* 上层: 模拟 wait_queue 中有两个等待者 */
	struct wait_queue_head my_wq;
	struct wait_queue_entry e1 = { .private = &t1, .func = autoremove_wake_function };
	struct wait_queue_entry e2 = { .private = &t2, .func = autoremove_wake_function };

	init_waitqueue_head(&my_wq);
	add_wait_queue(&my_wq, &e1);
	add_wait_queue(&my_wq, &e2);

	printf("初始状态:\n");
	printf("  wait_queue:  [%s] ⇄ [%s]  (两个等待者)\n", t1.name, t2.name);
	printf("  %s state=%d, %s state=%d\n\n", t1.name, t1.state, t2.name, t2.state);

	/* 现在模拟 "持锁的 waker" */
	printf("--- 进程 C 持锁, 需要唤醒等待者 ---\n\n");

	struct wake_q_head deferred;
	wake_q_init(&deferred);

	printf("spin_lock(&lock);\n");
	printf("// 发现需要唤醒 t1 和 t2\n");
	printf("// 但持锁期间不能直接 wake_up_process\n");
	printf("// 于是用 wake_q_node 延迟:\n\n");

	/* 模拟: 遍历等待队列, 对每个 task 调用 wake_q_add 而不是立即唤醒 */
	printf("  for each entry in wait_queue:\n");
	printf("    task = entry->private;\n");
	printf("    wake_q_add(&deferred, task);  // 延迟! 不直接唤醒\n\n");

	wake_q_add(&deferred, &t1);
	wake_q_add(&deferred, &t2);

	printf("\n  持锁时的 wake_q 队列:\n");
	printf("  %s.wake_q → %s.wake_q → WAKE_Q_TAIL\n", t1.name, t2.name);

	printf("\nspin_unlock(&lock);\n\n");

	/* 解锁后, 真正批量唤醒 */
	printf("wake_up_q(&deferred);  ← 批量唤醒时间!\n");
	wake_up_q(&deferred);

	printf("\n最终结果:\n");
	printf("  %s: state=%d (0=运行), wake_q.next=%p (NULL=可被复用)\n",
	       t1.name, t1.state, (void *)t1.wake_q.next);
	printf("  %s: state=%d (0=运行), wake_q.next=%p (NULL=可被复用)\n",
	       t2.name, t2.state, (void *)t2.wake_q.next);
}

/* ============================================================
 * 最终总结图
 * ============================================================ */

int main(void)
{
	/* 初始化全局 task_struct */
	t1 = (struct task_struct){ .name = "kworker", .pid = 10, .state = 1, .refcnt = 1 };
	t2 = (struct task_struct){ .name = "sshd",    .pid = 42, .state = 1, .refcnt = 1 };
	t3 = (struct task_struct){ .name = "nginx",   .pid = 99, .state = 1, .refcnt = 1 };

	printf("╔══════════════════════════════════════════════════════╗\n");
	printf("║  wait_queue → wake_q_node 调用链追踪               ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n\n");

	printf("三个层级的关系:\n\n");
	printf("  上层 (驱动开发者调用):\n");
	printf("    wait_event_interruptible(wq, condition)\n");
	printf("    wake_up_interruptible(&wq)\n");
	printf("      │\n");
	printf("      ▼\n");
	printf("  中层 (wait_queue 内部):\n");
	printf("    __wake_up_common() 遍历等待队列条目\n");
	printf("    autoremove_wake_function() → try_to_wake_up()\n");
	printf("      │\n");
	printf("      ▼\n");
	printf("  底层 (wake_q_node 优化):\n");
	printf("    wake_q_add()   — 持锁时轻量记录\n");
	printf("    wake_up_q()    — 解锁后批量唤醒\n\n");

	scenario1_standard_wait_queue();
	scenario2_wake_q_deferral();
	scenario3_two_layers();

	printf("\n\n════════════════════════════════════════════════════════\n");
	printf("  总结: wait_queue 和 wake_q_node 的关系\n");
	printf("════════════════════════════════════════════════════════\n\n");
	printf("  wait_queue:     [谁需要被唤醒] — 等待队列记录\n");
	printf("  wake_q_node:    [什么时候唤醒] — 延迟到解锁后\n\n");
	printf("  它们解决的是不同层面的问题:\n");
	printf("    wait_queue   → 管理 '等待者' 的组织方式\n");
	printf("    wake_q_node  → 优化 '唤醒动作' 的执行时机\n\n");

	return 0;
}
