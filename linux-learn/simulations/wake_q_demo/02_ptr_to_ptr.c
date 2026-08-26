/*
 * 02_ptr_to_ptr.c — struct wake_q_head 的 lastp 二级指针实现 O(1) 尾部追加
 *
 * 编译运行: gcc -Wall -o 02_ptr_to_ptr 02_ptr_to_ptr.c && ./02_ptr_to_ptr
 * 核心问题：
 *   普通单链表在尾部追加需要遍历整个链表找到尾节点 (O(n))。
 *   但如果维护一个"lastp"（指向下一个节点该写入的位置），就能做到 O(1)。
 *
 * 关键理解：
 *   初始时  lastp 指向 wq.first 自身 —— 因为链表为空，第一个节点写入 wq.first
 *   追加后  lastp 指向 tail->next  —— 因为下一个节点写入当前尾节点的 next
 *
 *   图示：
 *
 *   空链表:
 *     wq.first = NULL
 *     wq.lastp = &wq.first
 *
 *   追加 A:
 *     *wq.lastp = A    →  wq.first = A
 *     wq.lastp = &A.next
 *
 *     wq.first → [A|next] → NULL
 *                     ↑
 *                   wq.lastp
 *
 *   追加 B:
 *     *wq.lastp = B    →  A.next = B
 *     wq.lastp = &B.next
 *
 *     wq.first → [A|next] → [B|next] → NULL
 *                                 ↑
 *                              wq.lastp
 *
 *   无论链表多长，每次追加都是 O(1) —— 不需要遍历!
 */

#include <stdio.h>
#include <stddef.h>

#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct wake_q_node {
	struct wake_q_node *next;
};

struct wake_q_head {
	struct wake_q_node *first;
	struct wake_q_node **lastp;
};

struct task_struct {
	char name[32];
	int pid;
	int state;
	struct wake_q_node wake_q;
};

int main(void)
{
	struct task_struct tasks[] = {
		{ .name = "A", .pid = 1, .state = 0 },
		{ .name = "B", .pid = 2, .state = 0 },
		{ .name = "C", .pid = 3, .state = 0 },
	};
	int n = sizeof(tasks) / sizeof(tasks[0]);

	/*
	 * 使用 struct wake_q_head (和内核完全一致, 参见 include/linux/sched/wake_q.h)
	 * 空链表: wq.first=NULL, wq.lastp=&wq.first
	 */
	struct wake_q_head wq = {
		.first = NULL,
		.lastp = &wq.first,
	};

	printf("=== ptr-to-ptr 模式的 O(1) 尾部追加 ===\n\n");
	printf("struct wake_q_head wq = {\n");
	printf("    .first = NULL,\n");
	printf("    .lastp = &wq.first,  // = %p\n", (void *)wq.lastp);
	printf("};\n\n");

	for (int i = 0; i < n; i++) {
		struct wake_q_node *node = &tasks[i].wake_q;
		node->next = NULL;

		printf("[追加 %s]\n", tasks[i].name);
		printf("  追加前: wq.first=%p, wq.lastp=%p",
		       (void *)wq.first, (void *)wq.lastp);
		printf("  (指向 &wq.first? %s)\n",
		       wq.lastp == &wq.first ? "YES" : "NO");

		/* O(1) 追加 —— 无论链表多长，只执行这两步 */
		*wq.lastp = node;         /* 写入要追加的位置 */
		wq.lastp = &node->next;   /* 更新 lastp, 指向下一个空位 */

		printf("  追加后: wq.first=%p, wq.lastp → %s.next (地址 %p)\n\n",
		       (void *)wq.first,
		       tasks[i].name, (void *)&node->next);
	}

	printf("=== 遍历验证 ===\n\n");
	int count = 1;
	for (struct wake_q_node *node = wq.first; node; node = node->next) {
		struct task_struct *t;
		t = container_of(node, struct task_struct, wake_q);
		printf("  %d: %s (pid=%d)\n", count++, t->name, t->pid);
	}

	/*
	 * 图解最终状态:
	 *
	 *   wq.first ──→ [A.wake_q|next] ──→ [B.wake_q|next] ──→ [C.wake_q|next] → NULL
	 *                                                                             ↑
	 *                                                                          wq.lastp
	 *
	 *   wq.lastp 始终指向尾节点的 next 字段。
	 *   空链表时指向 wq.first 自身。
	 */

	return 0;
}
