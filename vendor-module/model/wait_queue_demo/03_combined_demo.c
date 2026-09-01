/*
 * 03_combined_demo.c — 完整可视化: 从驱动代码到底层唤醒
 *
 * 编译运行: gcc -Wall -o 03_combined_demo 03_combined_demo.c && ./03_combined_demo
 * 本 demo 模拟一个完整的内核模块, 展示 "文章中的驱动代码"
 * 如何在底层通过 wait_queue 和 wake_q_node 完成进程唤醒。
 *
 * 布局:
 *   ┌─────────────────────────────────────────────────────┐
 *   │  驱动代码 (上层)                                     │
 *   │  = 文章中的 test_wait.c =                            │
 *   │                                                      │
 *   │  my_init() {                                         │
 *   │      schedule_work(&wrk);  // 启动异步工作            │
 *   │      wait_event_interruptible(my_wq, condition != 0); │ ← 睡眠
 *   │      // 醒来后继续...                                │
 *   │  }                                                   │
 *   │                                                      │
 *   │  work_handler() {                                    │
 *   │      condition = 1;                                  │
 *   │      wake_up_interruptible(&my_wq);                  │ ← 唤醒
 *   │  }                                         │         │
 *   └──────────────────────────────────────────┬──────────┘
 *                                              │
 *                  wake_up_interruptible(&my_wq)
 *                                              │
 *   ┌──────────────────────────────────────────┼──────────┐
 *   │  wait_queue 内部 (中层)                   ▼          │
 *   │                                                      │
 *   │  __wake_up_common()                                  │
 *   │    │  遍历 my_wq->head 上的每个 wait_queue_entry     │
 *   │    │  调用 entry->func (autoremove_wake_function)    │
 *   │    │                                                 │
 *   │    └→ default_wake_function()                        │
 *   │         └→ try_to_wake_up(task)  ← 目标: 唤醒 task   │
 *   └──────────────────────────────────────┬──────────────┘
 *                                          │
 *         try_to_wake_up(task) 真的会立即执行吗?
 *                                          │
 *   ┌──────────────────────────────────────┼──────────────┐
 *   │  wake_q_node 优化 (底层)              ▼              │
 *   │                                                      │
 *   │  如果调用者持锁, 不会直接 try_to_wake_up:             │
 *   │                                                      │
 *   │    spin_lock(&lock);                                 │
 *   │    wake_q_add(&wq, task);   ← 只记录到链表           │
 *   │    spin_unlock(&lock);                               │
 *   │    wake_up_q(&wq);          ← 解锁后批量唤醒         │
 *   │      └→ 遍历 wq, 对每个 task:                        │
 *   │           container_of → 反推 task_struct             │
 *   │           wake_up_process(task)  ← 最终的真�唤醒     │
 *   └──────────────────────────────────────────────────────┘
 *
 * 关键洞察:
 *   这三个层级不是互相替代的, 而是各司其职:
 *     wait_queue  → 管理 "谁在等" (等待者注册)
 *     try_to_wake_up → 执行 "怎么醒" (调度器操作)
 *     wake_q_node → 优化 "何时醒" (延迟到锁释放后)
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdbool.h>

/* ============================================================
 * 复用内核数据结构和操作 (完整版)
 * ============================================================ */

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct wake_q_node {
	struct wake_q_node *next;
};
#define WAKE_Q_TAIL  ((struct wake_q_node *) 0x01)

struct wake_q_head {
	struct wake_q_node *first;
	struct wake_q_node **lastp;
};

struct list_node {
	struct list_node *next, *prev;
};

struct wait_queue_entry {
	void            *private;
	int            (*func)(struct wait_queue_entry *, unsigned int, int, void *);
	struct list_node entry;
};

struct wait_queue_head {
	struct list_node head;
};

struct task_struct {
	const char      *name;
	int              pid;
	int              state;         /* 0=RUNNING, 1=INTERRUPTIBLE */
	int              refcnt;

	/* ★ 关键: 一个 task 同时参与两个链表系统 ★ */
	struct wait_queue_entry  wait_entry;   /* 等待队列条目 */
	struct wake_q_node       wake_q;       /* 延迟唤醒节点 */
};

/* ============================================================
 * wait_queue 操作
 * ============================================================ */

static void init_waitqueue_head(struct wait_queue_head *wq)
{
	wq->head.next = &wq->head;
	wq->head.prev = &wq->head;
}

static void add_wait_queue(struct wait_queue_head *wq,
			   struct wait_queue_entry *entry)
{
	entry->entry.next = &wq->head;
	entry->entry.prev = wq->head.prev;
	wq->head.prev->next = &entry->entry;
	wq->head.prev = &entry->entry;
}

static int autoremove_wake_function(struct wait_queue_entry *wq_entry,
				    unsigned int mode, int sync, void *key)
{
	struct task_struct *task = wq_entry->private;
	(void)mode; (void)sync; (void)key;

	task->state = 0;  /* TASK_RUNNING */

	/* 从等待队列摘除 */
	wq_entry->entry.prev->next = wq_entry->entry.next;
	wq_entry->entry.next->prev = wq_entry->entry.prev;

	return 1;
}

static void wake_up_interruptible(struct wait_queue_head *wq)
{
	struct list_node *pos;
	for (pos = wq->head.next; pos != &wq->head; pos = pos->next) {
		struct wait_queue_entry *entry;
		entry = container_of(pos, struct wait_queue_entry, entry);
		entry->func(entry, 0x0001, 0, NULL);
		if (wq->head.next == &wq->head)
			break;
		pos = &wq->head;
	}
}

/* ============================================================
 * wake_q 操作 (延迟批量唤醒)
 * ============================================================ */

static void *cmpxchg_relaxed(void *ptr, void *old_val, void *new_val)
{
	void **p = (void **)ptr;
	if (*p == old_val) { *p = new_val; return old_val; }
	return *p;
}

static void get_task_struct(struct task_struct *t) { t->refcnt++; }
static void put_task_struct(struct task_struct *t) { t->refcnt--; }

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
		task->state = 0;
		put_task_struct(task);
	}
}

/* ============================================================
 * 可视化函数
 * ============================================================ */

static void print_box(const char *title, const char **lines, int n)
{
	int maxw = strlen(title) + 4;
	for (int i = 0; i < n; i++) {
		int w = strlen(lines[i]);
		if (w > maxw) maxw = w;
	}
	maxw += 2;

	printf("  ╔");
	for (int i = 0; i < maxw; i++) printf("═");
	printf("╗\n");

	printf("  ║ %-*s ║\n", maxw - 2, title);
	if (n > 0) {
		printf("  ╠");
		for (int i = 0; i < maxw; i++) printf("─");
		printf("╣\n");
		for (int i = 0; i < n; i++)
			printf("  ║ %-*s ║\n", maxw - 2, lines[i]);
	}

	printf("  ╚");
	for (int i = 0; i < maxw; i++) printf("═");
	printf("╝\n");
}

/* ============================================================
 * 主演示: 模拟文章中的 test_wait.c 模块
 * ============================================================ */

static int g_condition;  /* 全局唤醒条件 */
static struct wait_queue_head g_my_wq;      /* 全局等待队列 */
static struct task_struct *g_insmod_task;   /* insmod 进程 */

/* 将被 "异步工作" 唤醒 */

/*
 * 模拟 insmod 进程的行为:
 *   my_init() {
 *       schedule_work(&wrk);  // 启动异步工作
 *       wait_event_interruptible(my_wq, condition != 0);
 *       // ... 醒来后继续
 *   }
 */
static void simulate_my_init(struct task_struct *me)
{
	printf("\n════════════════════════════════════════════\n");
	printf("  [上层] my_init() — 驱动初始化函数\n");
	printf("════════════════════════════════════════════\n\n");

	/* 把自己加入等待队列 */
	printf("  ① 将 %s 加入等待队列 my_wq\n", me->name);
	me->wait_entry.private = me;
	me->wait_entry.func = autoremove_wake_function;
	add_wait_queue(&g_my_wq, &me->wait_entry);

	/* 检查条件 → 不满足 → 睡眠 */
	printf("  ② wait_event_interruptible(my_wq, condition != 0)\n");
	printf("     当前 condition = %d → 条件为假, 进入睡眠\n", g_condition);
	me->state = 1;  /* TASK_INTERRUPTIBLE */

	printf("  ③ schedule() → %s 让出 CPU, zzz...\n\n", me->name);
}

/*
 * 模拟 work_handler 的行为:
 *   work_handler() {
 *       condition = 1;
 *       wake_up_interruptible(&my_wq);
 *   }
 *
 * 但这个版本用 wake_q_node 优化:
 *   持锁时只记录 → 解锁后批量唤醒
 */
static void simulate_work_handler(void)
{
	printf("\n════════════════════════════════════════════\n");
	printf("  [上层] work_handler() — 异步工作回调\n");
	printf("════════════════════════════════════════════\n\n");

	printf("  ① 设置唤醒条件: condition = 1\n");
	g_condition = 1;

	/*
	 * 方案 A (文章中的写法): 直接唤醒
	 *   wake_up_interruptible(&my_wq);
	 *
	 * 方案 B (优化版): 持锁时用 wake_q_node 延迟
	 *   如果在持锁上下文中, 避免持锁时做昂贵的唤醒
	 */
	printf("  ② 需要调用 wake_up_interruptible(&my_wq)\n");
	printf("     但当前持有一把锁, 持锁时唤醒太贵!\n\n");

	printf("  ─── 方案 A (文章中的写法) ───\n");
	printf("    直接 wake_up_interruptible(&my_wq):\n");

	/* 模拟方案 A: 直接路径 */
	struct wait_queue_head tmp_wq;
	init_waitqueue_head(&tmp_wq);

	struct task_struct *dummy = g_insmod_task;
	dummy->wait_entry.private = dummy;
	dummy->wait_entry.func = autoremove_wake_function;
	add_wait_queue(&tmp_wq, &dummy->wait_entry);

	printf("    __wake_up_common → 遍历队列\n");
	printf("    autoremove_wake_function → try_to_wake_up(%s)\n",
	       dummy->name);
	printf("    → %s 被直接唤醒 (在持锁上下文中!)\n\n", dummy->name);

	/* 重置 */
	dummy->state = 1;
	init_waitqueue_head(&tmp_wq);
	dummy->wait_entry.func = autoremove_wake_function;
	add_wait_queue(&tmp_wq, &dummy->wait_entry);

	printf("  ─── 方案 B (wake_q_node 优化版) ───\n");
	printf("    spin_lock(&lock);\n");

	/* 方案 B: 用 wake_q_node 延迟 */
	struct wake_q_head deferred;
	wake_q_init(&deferred);

	printf("    // 持锁时只记录, 不真正唤醒:\n");
	printf("    wake_q_add(&deferred, %s);\n", dummy->name);
	wake_q_add(&deferred, dummy);

	printf("    // wake_q 队列: %s.wake_q → WAKE_Q_TAIL\n\n", dummy->name);

	printf("    spin_unlock(&lock);\n");
	printf("    // 锁已释放, 现在安全了:\n");
	printf("    wake_up_q(&deferred);\n");
	wake_up_q(&deferred);

	printf("      └→ container_of → %s\n", dummy->name);
	printf("      └→ wake_up_process(%s) ← 最终真正的唤醒\n", dummy->name);

	printf("\n  ★ 方案 B 是更优的内核实践 ★\n");
}

/* ============================================================
 * 整体架构图
 * ============================================================ */

static void print_architecture(void)
{
	printf("╔══════════════════════════════════════════════════════════╗\n");
	printf("║         三层架构总览                                     ║\n");
	printf("╚══════════════════════════════════════════════════════════╝\n\n");

	printf("  ┌────────────────────────────────────────────────────┐\n");
	printf("  │  上层: 驱动 API (文章中的代码)                     │\n");
	printf("  │                                                    │\n");
	printf("  │  进程 A (insmod):                                  │\n");
	printf("  │    wait_event_interruptible(my_wq, condition != 0) │\n");
	printf("  │    → 加入等待队列 → 睡眠                           │\n");
	printf("  │                                                    │\n");
	printf("  │  进程 B (work_handler):                            │\n");
	printf("  │    condition = 1;                                  │\n");
	printf("  │    wake_up_interruptible(&my_wq);                  │\n");
	printf("  └──────────────────────┬─────────────────────────────┘\n");
	printf("                         │\n");
	printf("                         ▼\n");
	printf("  ┌────────────────────────────────────────────────────┐\n");
	printf("  │  中层: wait_queue 机制                             │\n");
	printf("  │                                                    │\n");
	printf("  │  wait_queue_head (my_wq)                           │\n");
	printf("  │    ├── wait_queue_entry → task A (private)         │\n");
	printf("  │    ├── wait_queue_entry → task C (private)         │\n");
	printf("  │    └── ...                                         │\n");
	printf("  │                                                    │\n");
	printf("  │  __wake_up_common(): 遍历 → func() → try_to_wake_up│\n");
	printf("  └──────────────────────┬─────────────────────────────┘\n");
	printf("                         │\n");
	printf("                         ▼\n");
	printf("  ┌────────────────────────────────────────────────────┐\n");
	printf("  │  底层: wake_q_node 优化                            │\n");
	printf("  │                                                    │\n");
	printf("  │  task_struct                                       │\n");
	printf("  │    ├── wait_entry  (参与等待队列)                  │\n");
	printf("  │    └── wake_q      (参与延迟唤醒队列)              │\n");
	printf("  │                                                    │\n");
	printf("  │  wake_q_node.next 三态:                            │\n");
	printf("  │    NULL        → 未入队                            │\n");
	printf("  │    WAKE_Q_TAIL → 入队, 是尾节点                    │\n");
	printf("  │    ptr         → 入队, 指向下一个节点              │\n");
	printf("  └────────────────────────────────────────────────────┘\n\n");
}

/* ============================================================
 * 主函数
 * ============================================================ */

int main(void)
{
	struct task_struct insmod_task = {
		.name = "insmod", .pid = 1234, .state = 0, .refcnt = 1
	};
	g_insmod_task = &insmod_task;
	g_condition = 0;
	init_waitqueue_head(&g_my_wq);

	printf("╔══════════════════════════════════════════════════════════╗\n");
	printf("║  完整模拟: 文章 test_wait.c 的三层架构                 ║\n");
	printf("╚══════════════════════════════════════════════════════════╝\n\n");

	printf("本 demo 对照阿里云文章的 test_wait.c 内核模块,\n");
	printf("展示从驱动代码到底层 wake_q_node 的完整调用链。\n\n");

	/* 画总架构 */
	print_architecture();

	/* 模拟 my_init */
	simulate_my_init(&insmod_task);

	/* 模拟 work_handler (两种方案对比) */
	simulate_work_handler();

	/* 总结 */
	printf("\n\n════════════════════════════════════════════════════════\n");
	printf("  wait_queue 和 wake_q_node 的分工\n");
	printf("════════════════════════════════════════════════════════\n\n");

	printf("  wait_queue (中层):\n");
	printf("    管理 [谁在等] — 等待者注册/注销/查询\n");
	printf("    wake_up 时遍历等待者, 逐个调用 wake 函数\n\n");

	printf("  wake_q_node (底层):\n");
	printf("    优化 [何时醒] — 延迟到锁释放后\n");
	printf("    避免持锁时做昂贵的跨 CPU 唤醒操作\n\n");

	printf("  关系:\n");
	printf("    两者不是替代关系, 而是互补关系。\n");
	printf("    wait_queue 决定 [唤醒谁],\n");
	printf("    wake_q_node 决定 [什么时候唤醒]。\n");
	printf("    它们可以在同一个 task_struct 里并存:\n\n");
	printf("    ┌───────────────────────┐\n");
	printf("    │     task_struct       │\n");
	printf("    │  ┌─────────────────┐  │\n");
	printf("    │  │ wait_queue_entry│  │ ← 参与等待队列\n");
	printf("    │  ├─────────────────┤  │\n");
	printf("    │  │ wake_q_node     │  │ ← 参与延迟唤醒队列\n");
	printf("    │  └─────────────────┘  │\n");
	printf("    └───────────────────────┘\n\n");

	return 0;
}
