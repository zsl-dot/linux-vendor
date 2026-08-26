/*
 * 01_intrusive_list.c — 内嵌式单链表 + container_of 宏
 *
 * 编译运行: gcc -Wall -o 01_intrusive_list 01_intrusive_list.c && ./01_intrusive_list
 * 核心概念：
 *   wake_q_node 不持有数据，而是"嵌入"在 task_struct 里面。
 *   给定 node 的地址，用 container_of 反推出所属 task_struct 的地址。
 *
 *   "侵入式链表"(intrusive list) 和传统链表的区别：
 *   传统： struct list_node { void *data; struct list_node *next; };  // 节点包数据
 *   侵入： struct my_data   { struct list_node node; int payload;   };  // 数据包含节点
 *
 *   侵入式的好处：
 *   1. 不需要额外分配/释放节点内存（节点就在对象内部）
 *   2. 查找 O(1) — 从 node 地址减偏移量即可得到对象地址
 *   3. 一个对象可以同时嵌入多个不同的链表节点（属于多个链表）
 */

#include <stdio.h>
#include <stddef.h>

/* ---------- 模拟内核定义 ---------- */

/*
 * container_of — 内核最常用的宏之一
 * 给定 成员指针、结构体类型、成员名 → 反推出所在结构体的指针
 *
 *              task_struct
 *         ┌──────────────┐
 *         │  ...         │
 *         │  wake_q @ptr─┼──┐  ptr = &task->wake_q
 *         │  ...         │  │
 *         │              │◄─┘  container_of(ptr, struct task_struct, wake_q)
 *         └──────────────┘
 */
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct wake_q_node {
	struct wake_q_node *next;
};

/* 简化的 task_struct */
struct task_struct {
	char name[32];
	int pid;
	int state;		/* 0=sleeping, 1=running */
	struct wake_q_node wake_q; /* 内嵌的链表节点 */
};

/* ---------- 演示 ---------- */

void print_task(struct task_struct *t)
{
	printf("  task %p: name=%s pid=%d state=%d\n", (void *)t,
	       t->name, t->pid, t->state);
}

int main(void)
{
	struct task_struct t1 = { .name = "t1", .pid = 1, .state = 0 };
	struct task_struct t2 = { .name = "t2", .pid = 2, .state = 0 };

	/*
	 * 手动构造一个链表: t1 和 t2 放在同一个链表里。
	 * 让 head 指向 t1, t1->wake_q.next 指向 &t2->wake_q。
	 */
	struct wake_q_node *head = &t1.wake_q;
	t1.wake_q.next = &t2.wake_q;
	t2.wake_q.next = NULL;

	printf("=== 遍历链表，通过 container_of 找回 task_struct ===\n\n");

	int count = 1;
	for (struct wake_q_node *node = head; node; node = node->next) {
		struct task_struct *task;

		task = container_of(node, struct task_struct, wake_q);
		printf("节点 %d: node=%p → ", count, (void *)node);
		printf("container_of → task=%p → ", (void *)task);
		printf("name=%s pid=%d\n", task->name, task->pid);
		count++;
	}

	/*
	 * 证明 container_of 确实能精确反推：
	 * &t1.wake_q 和 &t1 的地址差是多少？
	 */
	printf("\n=== container_of 的数学原理 ===\n\n");
	printf("  &t1           = %p\n", (void *)&t1);
	printf("  &t1.wake_q    = %p\n", (void *)&t1.wake_q);
	printf("  offsetof      = %zu 字节\n",
	       offsetof(struct task_struct, wake_q));
	printf("  &t1.wake_q - offsetof = %p  (== &t1 ? %s)\n",
	       (void *)((char *)&t1.wake_q - offsetof(struct task_struct, wake_q)),
	       ((char *)&t1.wake_q - offsetof(struct task_struct, wake_q) == (char *)&t1)
	       ? "YES" : "NO");

	return 0;
}
