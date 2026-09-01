/*
 * 05_lastp_visual.c — lastp 机制的逐步可视化
 *
 * 编译运行: gcc -Wall -o 05_lastp_visual 05_lastp_visual.c && ./05_lastp_visual
 * 本程序会在每一步都打印内存状态图，让你看清楚:
 *   - first 里存的是什么
 *   - lastp 指向哪里
 *   - *lastp 是什么
 *   - 为什么赋值给 *lastp 就是"在正确的位置写入"
 */

#include <stdio.h>
#include <stdlib.h>

struct node {
	int data;
	struct node *next;
};

/* 哨兵值，模拟 WAKE_Q_TAIL */
#define SENTINEL  ((struct node *) 0x01)

/* 安全打印指针值 */
static const char *ptr_str(void *p)
{
	static char buf[32];
	if (p == SENTINEL)
		return "WAKE_Q_TAIL(0x1)";
	if (p == NULL)
		return "NULL";
	snprintf(buf, sizeof(buf), "node@%p", p);
	return buf;
}

/* 打印队列当前状态 (图解) */
static void dump_queue(struct node *first, struct node **lastp, int step)
{
	printf("\n  ╔═══════════════════════ 步骤 %d ═══════════════════════╗\n", step);
	printf("  ║                                                   ║\n");

	/* 打印链表内容 */
	if (first == SENTINEL) {
		printf("  ║  [空链表]                                        ║\n");
	} else {
		printf("  ║  ");
		struct node *n = first;
		while (n != SENTINEL) {
			printf("[%d|next] → ", n->data);
			n = n->next;
		}
		printf("SENTINEL                                   ║\n");
	}

	/* 打印 lastp 指向 */
	printf("  ║                                                   ║\n");
	printf("  ║  first  = %-38p           ║\n", (void *)first);
	printf("  ║  lastp  = %-38p           ║\n", (void *)lastp);
	printf("  ║  *lastp = %-38p           ║\n", (void *)(*lastp));

	/* lastp 的语义解释 */
	if (lastp == NULL) return; /* unreachable */

	if (*lastp == SENTINEL)
		printf("  ║  → lastp 指向 first (空链表)                    ║\n");
	else if (*lastp == NULL)
		printf("  ║  → lastp 指向尾节点的 next (该 next 是 NULL)      ║\n");
	else
		printf("  ║  → lastp 指向某个 next 字段 (该字段存着: %s) ║\n",
		       ptr_str(*lastp));

	printf("  ╚═══════════════════════════════════════════════╝\n");
}

int main(void)
{
	struct node nodes[4];
	struct node *first = SENTINEL;       /* 空链表: WAKE_Q_TAIL */
	struct node **lastp = &first;        /* lastp 指向 first 自身 */

	printf("\n╔══════════════════════════════════════════════╗\n");
	printf("║  lastp 机制逐步可视化                        ║\n");
	printf("╚══════════════════════════════════════════════╝\n");
	printf("\n初始: 空链表\n");
	printf("  first 在栈上的地址: &first = %p\n", (void *)&first);
	printf("  first 的初始值: first = WAKE_Q_TAIL (0x1)\n");
	printf("  lastp 初始指向: &first = %p\n", (void *)lastp);
	printf("\n核心等价关系:\n");
	printf("  *lastp  === first\n");
	printf("  *lastp 总是等于 'lastp 指向的那个指针字段的当前值'\n");

	dump_queue(first, lastp, 0);

	/* 初始化节点 */
	for (int i = 0; i < 4; i++) {
		nodes[i].data = (i + 1) * 10;
		nodes[i].next = NULL;
	}

	/* ================================================================
	 * 逐步追加 4 个节点，每步都详细解释
	 * ================================================================ */

	for (int i = 0; i < 4; i++) {
		printf("\n──────────────────────────────────────────────\n");
		printf("追加节点 [%d]\n", nodes[i].data);
		printf("──────────────────────────────────────────────\n");

		/* 模拟内核的 atomic 状态转换 */
		printf("\n① 原子状态转换:\n");
		printf("  node->next 从 NULL 改为 WAKE_Q_TAIL\n");
		printf("  (防止被其他 CPU 重复加入)\n");
		nodes[i].next = SENTINEL;   /* 模仿 cmpxchg */

		/* 关键步骤 */
		printf("\n② lastp 追加 (ptr-to-ptr O(1)):\n");

		printf("  执行前: *lastp = %s\n", ptr_str(*lastp));
		printf("  执行:\n");
		printf("    *head->lastp = &nodes[%d];   ← 在 '正确位置' 写入新节点\n", i);
		printf("    head->lastp  = &node->next;  ← lastp 指向新尾部的 '空位'\n");

		*lastp = &nodes[i];        /* 写入 */
		lastp = &nodes[i].next;    /* 移动 */

		printf("  执行后: *lastp = %s\n", ptr_str(*lastp));

		dump_queue(first, lastp, i + 1);
	}

	/* ================================================================
	 * 模拟 wake_up_q 的遍历
	 * ================================================================ */

	printf("\n──────────────────────────────────────────────\n");
	printf("模拟 wake_up_q 遍历 (反向 container_of)\n");
	printf("──────────────────────────────────────────────\n");

	struct node *node = first;
	int pos = 1;

	printf("\nlwake_up_q 的循环:\n");
	printf("  node = head->first;\n");
	printf("  while (node != WAKE_Q_TAIL) {\n");
	printf("      task = container_of(node, ...);\n");
	printf("      node = node->next;\n");
	printf("      wake_up_process(task);\n");
	printf("  }\n\n");

	while (node != SENTINEL) {
		/* 注意: 这里演示的是链表遍历本身，不是 container_of。
		 * 在实际内核中，node 是 task_struct 内部的 wake_q 成员，
		 * 所以需要通过 container_of 反推 task_struct */
		printf("  迭代 %d:\n", pos);
		printf("    node=%p (第 %d 个节点的 wake_q 成员地址)\n", (void *)node, pos);
		printf("    通过 container_of → 得到 task_struct 地址\n");
		printf("    读取 node->next = %s → 推进到下一个节点\n", ptr_str(node->next));
		printf("    真正唤醒 task...\n");
		printf("    重置 node->next = NULL (允许 task 重新入队)\n\n");
		node = node->next;
		pos++;
	}

	/* ================================================================
	 * 最终总结图
	 * ================================================================ */

	printf("═══════════════════════════════════════════════════\n");
	printf("              总结: lastp 的本质\n");
	printf("═══════════════════════════════════════════════════\n\n");

	printf("  lastp 是 struct node ** 类型\n");
	printf("  它存储的不是'某个节点的地址'\n");
	printf("  而是 '某个 next 字段的地址'\n\n");

	printf("  这个 next 字段可能是:\n");
	printf("    - &first          (当链表为空时)\n");
	printf("    - &tail->next     (当链表非空时)\n\n");

	printf("  通过 *lastp = new_node:\n");
	printf("    空链表: *(&first) = new_node      → first = new_node\n");
	printf("    非空:   *(&tail->next) = new_node  → tail->next = new_node\n\n");

	printf("  两种情况，同一句代码。\n");
	printf("  不需要 if/else，不需要遍历。\n\n");

	printf("  这正是 Linus 的一句名言:\n");
	printf("  \"If you need more than 3 levels of indirection,\n");
	printf("   you're probably doing something wrong.\n");
	printf("   But 2 levels of indirection is often the right answer.\"\n");

	return 0;
}
