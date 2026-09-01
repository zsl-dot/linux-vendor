/*
 * 03_wake_q_simulation.c — 手动模拟内核 wake-queue 完整机制
 *
 * 编译运行: gcc -Wall -o 03_wake_q_simulation 03_wake_q_simulation.c && ./03_wake_q_simulation
 * 本文件忠实地复刻了内核中 wake_q 的实现:
 *   - 每个 "进程" 里嵌入一个 wake_q_node
 *   - cmpxchg 原子操作防重复加入
 *   - get/put 引用计数保护 task_struct 生命周期
 *   - container_of 从 node 地址反推 task_struct
 *   - ptr-to-ptr 的 O(1) 尾部追加
 *   - WAKE_Q_TAIL 哨兵的三态协议
 *
 * 对照内核源码:
 *   include/linux/sched/wake_q.h
 *   kernel/sched/core.c: __wake_q_add(), wake_q_add(), wake_up_q()
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/* ============================================================
 * 第一部分: 内核数据结构的忠实复刻
 * ============================================================ */

/* --- container_of: 内核最核心的宏 --- */
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

#define WRITE_ONCE(x, val)  ((x) = (val))

/* --- wake_q_node: 嵌入在每个 task_struct 里的单链表节点 --- */
struct wake_q_node {
	struct wake_q_node *next;
};

/* WAKE_Q_TAIL: 不是 NULL! 值为 0x01, 充当哨兵 */
#define WAKE_Q_TAIL  ((struct wake_q_node *) 0x01)

/* --- wake_q_head: 队列头, lastp 是关键 --- */
struct wake_q_head {
	struct wake_q_node *first;        /* 第一个节点 (或 WAKE_Q_TAIL 表示空) */
	struct wake_q_node **lastp;       /* 指向 "下一个节点该写入的位置" */
};

/* --- 简化的 task_struct: 只保留与 wake_q 相关的字段 --- */
#define REFCNT_TRACK_SIZE 10

struct task_struct {
	const char *name;
	int pid;
	int state;                    /* 0=TASK_RUNNING, 1=TASK_INTERRUPTIBLE */
	int refcnt;                   /* 模拟引用计数 */
	struct wake_q_node wake_q;    /* ★ 内嵌的节点! 每个 task 只有一个 */
};

/* ============================================================
 * 第二部分: 辅助函数
 * ============================================================ */

static int refcnt_log[REFCNT_TRACK_SIZE];
static int refcnt_idx = 0;

/* 模拟 get_task_struct: 增加引用计数 */
static void get_task_struct(struct task_struct *task)
{
	task->refcnt++;
	printf("      get_task_struct(%s) → refcnt=%d\n",
	       task->name, task->refcnt);
	refcnt_log[refcnt_idx++] = task->pid;
}

/* 模拟 put_task_struct: 减少引用计数 */
static void put_task_struct(struct task_struct *task)
{
	task->refcnt--;
	printf("      put_task_struct(%s) → refcnt=%d\n",
	       task->name, task->refcnt);
}

/* 模拟 wake_up_process: 真正唤醒进程 */
static void wake_up_process(struct task_struct *task)
{
	printf("      wake_up_process(%s): state %d → 0 (RUNNING)\n",
	       task->name, task->state);
	task->state = 0;
}

/* 模拟 cmpxchg_relaxed: 比较并交换
 * 真实内核中这是原子指令, 这里简化但保留语义 */
static void *cmpxchg_relaxed(void *ptr, void *old_val, void *new_val)
{
	void **p = (void **)ptr;
	if (*p == old_val) {
		*p = new_val;
		return old_val;  /* 成功, 返回旧值 */
	}
	return *p;           /* 失败, 返回当前值(不等于 old) */
}

/* ============================================================
 * 第三部分: 内核 wake_q 函数的忠实复刻
 * ============================================================ */

static void wake_q_init(struct wake_q_head *head)
{
	head->first = WAKE_Q_TAIL;
	head->lastp = &head->first;   /* lastp 指向 first 字段自身 */
}

/*
 * __wake_q_add — 尝试把 task 加入队列 (内部函数)
 *
 * 内核源码 (kernel/sched/core.c):
 *   static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
 *   {
 *       struct wake_q_node *node = &task->wake_q;
 *       smp_mb__before_atomic();
 *       if (unlikely(cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL)))
 *           return false;
 *       *head->lastp = node;
 *       head->lastp = &node->next;
 *       return true;
 *   }
 *
 * 核心逻辑:
 *   1. 获取 task 内部的 wake_q 节点地址 (不是分配新内存!)
 *   2. 尝试用 cmpxchg 把 next 从 NULL 改为 WAKE_Q_TAIL
 *      - 成功: 该 task 不在任何队列, 现在它属于我们了
 *      - 失败: next 已经是别的值, 说明已在队列中, 跳过
 *   3. 用 lastp 实现 O(1) 尾部追加
 */
static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	struct wake_q_node *node = &task->wake_q;
	void *old_next;

	printf("    __wake_q_add(%s): node=&%s.wake_q=%p, node->next=%p\n",
	       task->name, task->name, (void *)node, (void *)node->next);

	printf("    cmpxchg(&node->next, NULL, WAKE_Q_TAIL)... ");

	old_next = cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL);

	if (old_next != NULL) {
		printf("FAILED (next=%p, 已在队列中)\n", old_next);
		return false;
	}

	printf("SUCCESS (next 从 NULL 变为 WAKE_Q_TAIL)\n");

	/*
	 * ptr-to-ptr O(1) 追加:
	 *   *head->lastp = node   — 在正确的位置写入节点
	 *   head->lastp = &node->next — lastp 跟随到新的空位
	 */
	printf("    追加: *lastp(%p) = node(%p)\n",
	       (void *)head->lastp, (void *)node);
	*head->lastp = node;

	printf("    移动: lastp = &node->next(%p)\n",
	       (void *)&node->next);
	head->lastp = &node->next;

	return true;
}

/*
 * wake_q_add — 带引用计数的安全加入
 *
 * 内核源码:
 *   void wake_q_add(struct wake_q_head *head, struct task_struct *task)
 *   {
 *       if (__wake_q_add(head, task))
 *           get_task_struct(task);
 *   }
 *
 * 为什么需要 get_task_struct?
 *   task 在队列中等待唤醒期间，必须保证 task_struct 内存有效。
 *   引用计数确保即使原持有者在唤醒前释放了引用，内存也不会被回收。
 */
void wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	printf("  wake_q_add(&head, %s)\n", task->name);
	if (__wake_q_add(head, task))
		get_task_struct(task);
}

/*
 * wake_q_add_safe — 调用者已持有引用的版本
 *
 * 内核源码:
 *   void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task)
 *   {
 *       if (!__wake_q_add(head, task))
 *           put_task_struct(task);
 *   }
 *
 * 语义: "我先帮你拿着引用，如果你已经在队列中了，我就放掉我的这份"。
 */
void wake_q_add_safe(struct wake_q_head *head, struct task_struct *task)
{
	printf("  wake_q_add_safe(&head, %s) [调用者已持有引用]\n", task->name);
	if (!__wake_q_add(head, task))
		put_task_struct(task);
}

/*
 * wake_up_q — 批量唤醒队列中的所有 task
 *
 * 内核源码 (kernel/sched/core.c):
 *   void wake_up_q(struct wake_q_head *head)
 *   {
 *       struct wake_q_node *node = head->first;
 *       while (node != WAKE_Q_TAIL) {
 *           task = container_of(node, struct task_struct, wake_q);
 *           node = node->next;
 *           WRITE_ONCE(task->wake_q.next, NULL);
 *           wake_up_process(task);
 *           put_task_struct(task);
 *       }
 *   }
 *
 * 关键步骤:
 *   1. 用 container_of 从 node 地址反推 task_struct (逆运算!)
 *   2. 先读取 node->next，再重置 next 为 NULL (允许 task 重新入队)
 *   3. 真正唤醒
 *   4. 释放引用 (与 wake_q_add 中的 get 配对)
 */
void wake_up_q(struct wake_q_head *head)
{
	struct wake_q_node *node = head->first;

	printf("\n  wake_up_q: 开始批量唤醒\n");
	printf("  head->first = %p\n", (void *)head->first);

	while (node != WAKE_Q_TAIL) {
		struct task_struct *task;

		/* 魔术: 从 node 地址反推出 task_struct 地址 */
		task = container_of(node, struct task_struct, wake_q);
		printf("\n    node=%p\n", (void *)node);
		printf("    container_of(node, task_struct, wake_q)\n");
		printf("    → task=%p (%s, pid=%d)\n",
		       (void *)task, task->name, task->pid);

		/* 先推进 node，再修改 (避免改完后找不到下一个) */
		node = node->next;
		printf("    node = node->next = %p\n", (void *)node);

		/* 重置 next = NULL: 允许 task 再次被加入队列 */
		WRITE_ONCE(task->wake_q.next, NULL);
		printf("    task->wake_q.next = NULL (允许重新入队)\n");

		/* 真正唤醒 */
		wake_up_process(task);

		/* 释放引用 (配� get_task_struct) */
		put_task_struct(task);
	}

	printf("\n  wake_up_q: 完成\n");
}

/* ============================================================
 * 第四部分: 模拟场景
 * ============================================================ */

/*
 * 场景 1: 正常延迟唤醒
 *
 *   持锁 → 修改状态 → wake_q_add (只记录) → 解锁 → wake_up_q (真正唤醒)
 */
static void scenario1_basic(void)
{
	printf("\n");
	printf("╔══════════════════════════════════════════════════╗\n");
	printf("║  场景 1: 正常延迟唤醒流程                        ║\n");
	printf("╚══════════════════════════════════════════════════╝\n");

	struct wake_q_head wq;
	struct task_struct t1 = { .name = "kworker/0", .pid = 10, .state = 1, .refcnt = 1 };
	struct task_struct t2 = { .name = "sshd",       .pid = 42, .state = 1, .refcnt = 1 };

	/* 初始状态: 两个 task 都在睡眠, wake_q 未使用 */
	printf("\n初始状态:\n");
	printf("  %s: wake_q.next=%p, state=%d, refcnt=%d\n",
	       t1.name, (void *)t1.wake_q.next, t1.state, t1.refcnt);
	printf("  %s: wake_q.next=%p, state=%d, refcnt=%d\n",
	       t2.name, (void *)t2.wake_q.next, t2.state, t2.refcnt);

	wake_q_init(&wq);
	printf("\nwake_q_init:\n");
	printf("  wq.first = %p (WAKE_Q_TAIL = 空链表)\n", (void *)wq.first);
	printf("  wq.lastp = %p (= &wq.first)\n", (void *)wq.lastp);

	/* --- 持锁阶段 --- */
	printf("\n--- spin_lock(&lock); ---\n");
	printf("(开始修改共享状态，发现 t1 和 t2 需要被唤醒)\n\n");

	wake_q_add(&wq, &t1);
	wake_q_add(&wq, &t2);

	printf("\n--- spin_unlock(&lock); ---\n");
	printf("(锁已释放，现在可以安全地做批量唤醒了)\n");

	/* 队列现在: t1 → t2 → WAKE_Q_TAIL */
	printf("\n队列状态:\n");
	printf("  wq.first → %s.wake_q (next=%p)\n",
	       t1.name, (void *)t1.wake_q.next);
	printf("  %s.wake_q.next → %s.wake_q (next=%p)\n",
	       t1.name, t2.name, (void *)t2.wake_q.next);

	/* --- 解锁后 --- */
	wake_up_q(&wq);

	/* 验证 */
	printf("\n最终状态:\n");
	printf("  %s: wake_q.next=%p, state=%d, refcnt=%d\n",
	       t1.name, (void *)t1.wake_q.next, t1.state, t1.refcnt);
	printf("  %s: wake_q.next=%p, state=%d, refcnt=%d\n",
	       t2.name, (void *)t2.wake_q.next, t2.state, t2.refcnt);
}

/*
 * 场景 2: 重复加入保护 (cmpxchg 的作用)
 *
 *   CPU0 把 t1 加入了队列 A
 *   CPU1 也尝试把 t1 加入队列 B
 *   但因为 t1->wake_q.next 已经是 WAKE_Q_TAIL (非 NULL)，
 *   cmpxchg 会失败 → __wake_q_add 返回 false → 不会重复加入
 */
static void scenario2_cmpxchg_guard(void)
{
	printf("\n\n");
	printf("╔══════════════════════════════════════════════════╗\n");
	printf("║  场景 2: cmpxchg 防止重复加入                    ║\n");
	printf("╚══════════════════════════════════════════════════╝\n");

	struct wake_q_head wq;
	struct task_struct t1 = { .name = "nginx", .pid = 99, .state = 1, .refcnt = 1 };

	wake_q_init(&wq);

	/*
	 * CPU0 持锁，发现 t1 需要唤醒，把它加入自己的队列。
	 * cmpxchg 成功: t1->wake_q.next 从 NULL → WAKE_Q_TAIL
	 */
	printf("\nCPU0 (持锁):\n");
	wake_q_add(&wq, &t1);
	printf("  t1->wake_q.next 现在是: %p (WAKE_Q_TAIL)\n",
	       (void *)t1.wake_q.next);

	/*
	 * CPU1 也持着另一个锁，也发现 t1 需要唤醒。
	 * 它尝试把 t1 加入另一个队列。
	 * cmpxchg 会看到 next 不是 NULL → 失败!
	 * 所以 t1 不会被重复加入。
	 *
	 * 但 CPU1 需要确保 t1 仍然会被唤醒:
	 * 它需要一个 smp_mb() 屏障来确保能看到 CPU0 已经设置的唤醒条件。
	 */
	printf("\nCPU1 (持另一把锁，也尝试唤醒 t1):\n");
	{
		struct wake_q_head wq_other;
		wake_q_init(&wq_other);
		bool added = __wake_q_add(&wq_other, &t1);
		printf("    结果: %s\n", added ? "加入成功" : "已跳过(在 CPU0 的队列中)");
	}

	/* CPU0 完成工作，批量唤醒 */
	printf("\nCPU0 释放锁后:\n");
	wake_up_q(&wq);

	printf("\n验证: t1->wake_q.next = %p (NULL，可以再次被加入)\n",
	       (void *)t1.wake_q.next);
}

/*
 * 场景 3: wake_q_add  vs wake_q_add_safe
 *
 *   两者的区别在于引用计数的管理:
 *     wake_q_add:      加入成功才 get_task_struct
 *     wake_q_add_safe: 调用者已持有引用, 加入失败才 put_task_struct
 */
static void scenario3_get_put(void)
{
	printf("\n\n");
	printf("╔══════════════════════════════════════════════════╗\n");
	printf("║  场景 3: get/put 引用计数语义                    ║\n");
	printf("╚══════════════════════════════════════════════════╝\n");

	struct wake_q_head wq;
	struct task_struct t1 = { .name = "task_A", .pid = 1,
				  .state = 1, .refcnt = 1 };
	struct task_struct t2 = { .name = "task_B", .pid = 2,
				  .state = 1, .refcnt = 1 };

	wake_q_init(&wq);

	/*
	 * wake_q_add: 调用者不持有引用
	 *   加入成功 → 自动 get_task_struct (refcnt++)
	 *   加入失败 → 什么也不做 (task 在别人的队列里，由别人负责唤醒)
	 */
	printf("\n--- wake_q_add (调用者不持有额外引用) ---\n");
	printf("t1 refcnt: 1 (仅创建时的引用)\n");
	wake_q_add(&wq, &t1);
	printf("加入成功后: t1 refcnt = %d (get_task_struct +1)\n", t1.refcnt);

	/* 模拟: 调用者自己持有了一份引用，想委托 wake_q 唤醒 */
	printf("\n--- wake_q_add_safe (调用者已持有引用) ---\n");
	printf("t2 refcnt: 1\n");
	t2.refcnt++;  /* 调用者 get_task_struct(t2) — 自己持有引用 */
	printf("调用者 get_task_struct(t2) → t2 refcnt = %d\n", t2.refcnt);

	wake_q_add_safe(&wq, &t2);
	printf("加入成功后: t2 refcnt = %d (没有变化)\n", t2.refcnt);

	/* 如果尝试再次加入同一个 task */
	printf("\n--- 重复加入测试 ---\n");
	printf("t1 refcnt = %d\n", t1.refcnt);
	t1.refcnt++;  /* 模拟另一个调用者持有引用 */
	printf("另一个调用者持有引用: t1 refcnt = %d\n", t1.refcnt);

	/* wake_q_add_safe 发现 t1 已在队列中 → __wake_q_add 返回 false
	 * → put_task_struct(t1) 释放多余的引用 */
	wake_q_add_safe(&wq, &t1);
	printf("加入失败后: t1 refcnt = %d (put -1, 释放了多余的引用)\n", t1.refcnt);

	printf("\n--- 最终唤醒, 释放所有引用 ---\n");
	wake_up_q(&wq);
	printf("\n最终:\n");
	printf("  t1 refcnt = %d, t1.wake_q.next = %p\n",
	       t1.refcnt, (void *)t1.wake_q.next);
	printf("  t2 refcnt = %d, t2.wake_q.next = %p\n",
	       t2.refcnt, (void *)t2.wake_q.next);
}

/* --- 入口 --- */
int main(void)
{
	printf("╔══════════════════════════════════════════════════╗\n");
	printf("║  内核 wake_q 机制 — 手动完整模拟               ║\n");
	printf("╚══════════════════════════════════════════════════╝\n");

	printf("\n关键数据结构关系:\n");
	printf("  ┌──────────────────────┐\n");
	printf("  │    task_struct       │\n");
	printf("  │  ┌────────────────┐  │     wake_q_node 是 task_struct 的一部分,\n");
	printf("  │  │ name, pid, ... │  │     不是独立分配的内存。\n");
	printf("  │  ├────────────────┤  │\n");
	printf("  │  │ wake_q_node    │  │     给定 node 地址 → container_of → task_struct 地址\n");
	printf("  │  │  └─ next       │  │\n");
	printf("  │  └────────────────┘  │\n");
	printf("  └──────────────────────┘\n");

	scenario1_basic();
	scenario2_cmpxchg_guard();
	scenario3_get_put();

	printf("\n\n════════════════════════════════════════════════════\n");
	printf("  总结\n");
	printf("════════════════════════════════════════════════════\n\n");
	printf("  1. wake_q_node 嵌入在 task_struct 中 — 侵入式链表\n");
	printf("  2. cmpxchg NULL→WAKE_Q_TAIL 原子抢占, 防重复加入\n");
	printf("  3. lastp(二级指针) 实现 O(1) 尾部追加, 无分支\n");
	printf("  4. container_of 从节点地址反推进程地址\n");
	printf("  5. get/put 引用计数保护 task_struct 生命周期\n");
	printf("  6. 持锁记录, 解锁唤醒 — 减少锁竞争\n\n");

	return 0;
}
