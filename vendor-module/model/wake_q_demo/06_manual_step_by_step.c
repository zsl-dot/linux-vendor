/*
 * 06_manual_step_by_step.c — 模拟单步执行: 逐条语句展示内存变化
 *
 * 编译运行: gcc -Wall -o 06_manual_step_by_step 06_manual_step_by_step.c && ./06_manual_step_by_step
 * 目标: 让你像用 GDB 单步调试一样，看清每一步每个变量的值如何变化。
 * 每个操作都会打印:
 *   - 当前执行的是哪条语句
 *   - 该语句执行前后，相关变量的值
 *   - 内存布局的 ASCII 图
 */

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdbool.h>

/* ===== 从内核复刻的定义 ===== */

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

struct task_struct {
	const char *name;
	int pid;
	int state;
	int refcnt;
	struct wake_q_node wake_q;
};

/* ===== 模拟的原子操作 ===== */

static void *cmpxchg_relaxed(void *ptr, void *old_val, void *new_val)
{
	void **p = (void **)ptr;
	if (*p == old_val) { *p = new_val; return old_val; }
	return *p;
}

/* ===== 打印工具 ===== */

static void print_ptr(const char *label, void *p)
{
	if (p == WAKE_Q_TAIL)
		printf("%s = WAKE_Q_TAIL (0x1)\n", label);
	else if (p == NULL)
		printf("%s = NULL\n", label);
	else
		printf("%s = %p\n", label, p);
}

/* 画出内存布局 */
static void draw_memory(struct task_struct *ta, struct task_struct *tb,
			struct task_struct *tc, struct wake_q_head *wq,
			const char *step_desc)
{
	printf("\n");
	printf("  +--------------------------------------------------+\n");
	printf("  |  %-48s |\n", step_desc);
	printf("  +--------------------------------------------------+\n");
	printf("  |                                                  |\n");
	printf("  |  [wake_q_head] @ %-32p |\n", (void *)wq);
	printf("  |    &wq.first = %-32p   |\n", (void *)&wq->first);
	print_ptr("  |    wq.first  ", wq->first);
	printf("  |    wq.lastp  = %-32p   |\n", (void *)wq->lastp);
	if (wq->lastp)
		print_ptr("  |    *wq.lastp ", *wq->lastp);
	printf("  |                                                  |\n");

	struct task_struct *arr[] = { ta, tb, tc };
	for (int i = 0; i < 3; i++) {
		printf("  |  [%s pid=%d] @ %-30p |\n",
		       arr[i]->name, arr[i]->pid, (void *)arr[i]);
		printf("  |    &wake_q = %-32p     |\n",
		       (void *)&arr[i]->wake_q);
		if (arr[i]->wake_q.next == WAKE_Q_TAIL)
			printf("  |    .next   = WAKE_Q_TAIL (0x1)\n");
		else if (arr[i]->wake_q.next == NULL)
			printf("  |    .next   = NULL\n");
		else {
			struct task_struct *tgt;
			tgt = container_of(arr[i]->wake_q.next,
					   struct task_struct, wake_q);
			printf("  |    .next   = %p -> %s.wake_q\n",
			       (void *)arr[i]->wake_q.next, tgt->name);
		}
		printf("  |    state=%d, refcnt=%d                            |\n",
		       arr[i]->state, arr[i]->refcnt);
		printf("  |                                                  |\n");
	}
	printf("  +--------------------------------------------------+\n");
}

/* 画出逻辑链 */
static void draw_chain(struct wake_q_head *wq, const char *title)
{
	printf("  [%s]\n", title);
	printf("  逻辑链:  ");

	if (wq->first == WAKE_Q_TAIL) {
		printf("(空链表)\n");
		return;
	}

	struct wake_q_node *node = wq->first;
	while (node != WAKE_Q_TAIL) {
		struct task_struct *t;
		t = container_of(node, struct task_struct, wake_q);
		printf("[%s]", t->name);
		node = node->next;
		if (node != WAKE_Q_TAIL)
			printf(" -> ");
	}
	printf(" -> WAKE_Q_TAIL\n");

	printf("  lastp 指向: ");
	if (*wq->lastp == WAKE_Q_TAIL)
		printf("尾节点的 next (当前是 WAKE_Q_TAIL)\n");
	else if (*wq->lastp == NULL)
		printf("尾节点的 next (当前是 NULL)\n");
	else {
		struct task_struct *t;
		t = container_of(*wq->lastp, struct task_struct, wake_q);
		printf("%s.wake_q\n", t->name);
	}
}

/* ===== 内核函数复刻 ===== */

static void wake_q_init(struct wake_q_head *head)
{
	printf("\n  >>> wake_q_init(&head)\n");
	printf("      head->first = WAKE_Q_TAIL;\n");
	printf("      head->lastp = &head->first;\n");
	head->first = WAKE_Q_TAIL;
	head->lastp = &head->first;
	printf("      结果: first=WAKE_Q_TAIL, lastp=%p (= &first)\n",
	       (void *)head->lastp);
}

static bool __wake_q_add(struct wake_q_head *head, struct task_struct *task)
{
	struct wake_q_node *node = &task->wake_q;

	printf("\n  >>> __wake_q_add(&head, %s)\n", task->name);
	printf("      (1/4) node = &task->wake_q;\n");
	printf("            &%s.wake_q = %p\n", task->name, (void *)node);

	printf("      (2/4) cmpxchg(&node->next, NULL, WAKE_Q_TAIL);\n");
	printf("            node->next 当前值: %p\n", (void *)node->next);

	void *old = cmpxchg_relaxed(&node->next, NULL, WAKE_Q_TAIL);
	if (old != NULL) {
		printf("            -> 失败! next 已经是 %p (非 NULL), 跳过\n", old);
		return false;
	}
	printf("            -> 成功! next: NULL -> WAKE_Q_TAIL\n");

	printf("      (3/4) *head->lastp = node;  // 写入到正确位置\n");
	printf("            *lastp (%p) 原值: %p\n",
	       (void *)head->lastp, (void *)(*head->lastp));
	*head->lastp = node;
	printf("            *lastp 新值: %p\n", (void *)(*head->lastp));

	printf("      (4/4) head->lastp = &node->next;  // lastp 跟随到新空位\n");
	printf("            lastp 原值: %p\n", (void *)head->lastp);
	head->lastp = &node->next;
	printf("            lastp 新值: %p\n", (void *)head->lastp);

	return true;
}

static void wake_up_q(struct wake_q_head *head,
		      struct task_struct *ta,
		      struct task_struct *tb,
		      struct task_struct *tc)
{
	printf("\n  >>> wake_up_q(&head)\n");
	printf("      node = head->first;\n");

	struct wake_q_node *node = head->first;
	print_ptr("      初始 node", node);

	if (node == WAKE_Q_TAIL) {
		printf("      队列为空, 直接返回\n");
		return;
	}

	int count = 0;
	while (node != WAKE_Q_TAIL) {
		count++;
		printf("\n      --- 迭代 %d ---\n", count);

		printf("      (1) task = container_of(node, task_struct, wake_q);\n");
		struct task_struct *task;
		task = container_of(node, struct task_struct, wake_q);
		printf("          node=%p -> offset=%zu -> task=%p (%s)\n",
		       (void *)node,
		       offsetof(struct task_struct, wake_q),
		       (void *)task, task->name);

		printf("      (2) node = node->next;\n");
		printf("          读取 node->next = %p (推进)\n", (void *)node->next);
		node = node->next;

		printf("      (3) task->wake_q.next = NULL;  // 重置\n");
		task->wake_q.next = NULL;

		printf("      (4) wake_up_process(task);  // 真正唤醒\n");
		printf("          %s: state %d -> 0\n", task->name, task->state);
		task->state = 0;

		printf("      (5) put_task_struct(task);  // 释放引用\n");
		task->refcnt--;
		printf("          %s: refcnt %d -> %d\n",
		       task->name, task->refcnt + 1, task->refcnt);
	}
	printf("      node == WAKE_Q_TAIL -> 循环结束\n");
}

/* ===== 主流程 ===== */

int main(void)
{
	struct task_struct tA = { .name = "A", .pid = 100, .state = 1, .refcnt = 1 };
	struct task_struct tB = { .name = "B", .pid = 200, .state = 1, .refcnt = 1 };
	struct task_struct tC = { .name = "C", .pid = 300, .state = 1, .refcnt = 1 };

	struct wake_q_head wq;

	printf("================================================================\n");
	printf("  手动单步模拟 wake_q -- 每一步的内存变化\n");
	printf("================================================================\n");

	printf("\n本演示模拟以下完整流程:\n");
	printf("  1. 初始化空队列\n");
	printf("  2. 加入 task A\n");
	printf("  3. 加入 task B\n");
	printf("  4. 加入 task C\n");
	printf("  5. 验证 cmpxchg 防重复\n");
	printf("  6. 批量唤醒\n");

	/* ============================================================
	 * 步骤 1: 初始化
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 1: 初始化空队列\n");
	printf("================================================================\n");

	wake_q_init(&wq);
	draw_memory(&tA, &tB, &tC, &wq, "步骤 1: 空队列初始化完成");

	printf("\n  [理解 lastp 的初始状态]\n");
	printf("  空链表时, lastp 指向 first 字段自身:\n");
	printf("    *lastp === wq.first  (等价关系)\n");
	printf("    *lastp === WAKE_Q_TAIL\n");
	printf("  后续第一次追加时: *lastp = node  <==>  wq.first = node\n");
	printf("  所以不需要判断链表是否为空!\n");

	/* ============================================================
	 * 步骤 2: 加入 task A
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 2: 加入 task A\n");
	printf("================================================================\n");

	printf("\n  /* -- 模拟 wake_q_add(&wq, &tA) -- */");
	if (__wake_q_add(&wq, &tA)) {
		printf("\n  >>> get_task_struct(%s);  // 加入成功, 增加引用\n", tA.name);
		tA.refcnt++;
		printf("      %s refcnt: %d\n", tA.name, tA.refcnt);
	}
	draw_memory(&tA, &tB, &tC, &wq, "步骤 2: 加入 A 后");
	draw_chain(&wq, "步骤 2 逻辑链");

	printf("\n  [lastp 的跟踪]\n");
	printf("  lastp 现在指向: %p\n", (void *)wq.lastp);
	printf("  &tA.wake_q.next  = %p\n", (void *)&tA.wake_q.next);
	printf("  两者相同! lastp 指向 A 的 next 字段\n");
	printf("  *lastp = WAKE_Q_TAIL (等待下一个节点填入这里)\n");

	/* ============================================================
	 * 步骤 3: 加入 task B
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 3: 加入 task B\n");
	printf("================================================================\n");

	printf("\n  /* -- 模拟 wake_q_add(&wq, &tB) -- */");
	if (__wake_q_add(&wq, &tB)) {
		printf("\n  >>> get_task_struct(%s);\n", tB.name);
		tB.refcnt++;
	}
	draw_memory(&tA, &tB, &tC, &wq, "步骤 3: 加入 B 后");
	draw_chain(&wq, "步骤 3 逻辑链");

	printf("\n  [lastp 的跟踪]\n");
	printf("  lastp 现在指向: %p\n", (void *)wq.lastp);
	printf("  &tB.wake_q.next  = %p\n", (void *)&tB.wake_q.next);
	printf("  lastp 跟随到了 B 的 next 字段!\n");

	/* ============================================================
	 * 步骤 4: 加入 task C
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 4: 加入 task C\n");
	printf("================================================================\n");

	printf("\n  /* -- 模拟 wake_q_add(&wq, &tC) -- */");
	if (__wake_q_add(&wq, &tC)) {
		printf("\n  >>> get_task_struct(%s);\n", tC.name);
		tC.refcnt++;
	}
	draw_memory(&tA, &tB, &tC, &wq, "步骤 4: 加入 C 后 (满队列)");
	draw_chain(&wq, "步骤 4 逻辑链");

	/* ============================================================
	 * 步骤 5: 验证 cmpxchg 防重复
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 5: 验证 cmpxchg 防重复 -- 再次加入 task A\n");
	printf("================================================================\n");

	printf("\n  此时 A.wake_q.next = %p (不是 NULL!)\n",
	       (void *)tA.wake_q.next);
	printf("  cmpxchg 期望 next 为 NULL, 但实际上 next 是 WAKE_Q_TAIL\n");
	printf("  -> cmpxchg 失败 -> __wake_q_add 返回 false\n");
	printf("  -> 防止了 A 同时出现在两个队列中!\n");

	bool dup = __wake_q_add(&wq, &tA);
	printf("\n  结果: __wake_q_add 返回 %s\n", dup ? "true" : "false");
	draw_memory(&tA, &tB, &tC, &wq, "步骤 5: 队列未变 (A 未重复加入)");

	/* ============================================================
	 * 步骤 6: 批量唤醒
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("步骤 6: 批量唤醒 (wake_up_q)\n");
	printf("================================================================\n");

	wake_up_q(&wq, &tA, &tB, &tC);
	draw_memory(&tA, &tB, &tC, &wq, "步骤 6: 全部唤醒, next 重置为 NULL");

	/* ============================================================
	 * 最终总结
	 * ============================================================ */

	printf("\n================================================================\n");
	printf("  总结: lastp 的变化轨迹\n");
	printf("================================================================\n");
	printf("\n");
	printf("  初始:   lastp = &wq.first\n");
	printf("  加 A 后: lastp = &A.wake_q.next\n");
	printf("  加 B 后: lastp = &B.wake_q.next\n");
	printf("  加 C 后: lastp = &C.wake_q.next\n");
	printf("\n");
	printf("  lastp 始终等于 [尾节点的 next 字段的地址]。\n");
	printf("  空表时没有尾节点, 所以指向 first (first 充当了伪 next 字段)。\n");
	printf("\n");
	printf("  *lastp = new_node 这条语句:\n");
	printf("    空表时: *(&wq.first)    = ...  -> wq.first = ...\n");
	printf("    非空时: *(&tail->next)  = ...  -> tail->next = ...\n");
	printf("\n");
	printf("  两种情况, 一条语句, 零分支。\n");

	return 0;
}
