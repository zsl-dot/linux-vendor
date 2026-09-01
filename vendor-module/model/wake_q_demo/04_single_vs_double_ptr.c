/*
 * 04_single_vs_double_ptr.c — 对比"一级指针"和"二级指针"实现链表追加
 *
 * 编译运行: gcc -Wall -o 04_single_vs_double_ptr 04_single_vs_double_ptr.c && ./04_single_vs_double_ptr
 * 核心目的：让你直观感受到为什么二级指针能消除 if/else 分支
 *
 *   ┌──────────────────────────────────────────────────────┐
 *   │   普通实现: 需要判断 head 是否为空 (if/else)          │
 *   │   lastp 实现: 统一处理，无需判断 (no branch)          │
 *   └──────────────────────────────────────────────────────┘
 *
 * 二级指针的本质一句话:
 *   lastp 存储的不是 "某个节点" 的地址，
 *   而是 "某个 next 字段" 的地址。
 *
 *   这个 next 字段可能是 head 自身 (空链表时)，
 *   也可能是 tail->next (非空链表时)。
 *   但无论如何，它都是一个 struct node ** 类型，
 *   对它赋值 *lastp = new_node 就是在"正确的位置"写入新节点。
 */

#include <stdio.h>
#include <stdlib.h>

struct node {
	int data;
	struct node *next;
};

/* =================================================================
 * 方法 A: 普通实现 — 每次追加都需要判断链表是否为空
 *
 *   if (head == NULL)
 *       head = new_node;          // 特殊情况: 修改 head
 *   else {
 *       tail->next = new_node;   // 一般情况: 修改 tail->next
 *   }
 *
 * ================================================================= */

void append_v1(struct node **head, int data)
{
	struct node *new_node = malloc(sizeof(*new_node));
	new_node->data = data;
	new_node->next = NULL;

	if (*head == NULL) {
		/* 情况 1: 空链表 → 修改 head */
		printf("  v1: 空链表，直接改 head\n");
		*head = new_node;
	} else {
		/* 情况 2: 非空链表 → 遍历到尾，修改 tail->next */
		struct node *tail = *head;
		while (tail->next)
			tail = tail->next;
		printf("  v1: 遍历到尾 %d，改 tail->next\n", tail->data);
		tail->next = new_node;
	}
}

/* =================================================================
 * 方法 B: 二级指针实现 — 无需分支
 *
 *   struct node **lastp = &head;   // 先指向 head 字段
 *   while (*lastp)                // 如果不为空，推进
 *       lastp = &(*lastp)->next;   // 指向下一个 next 字段
 *   *lastp = new_node;            // 在正确位置写入
 *
 * 关键洞察: head 本质上也是一个 "指向 node 的指针"，
 * 和 node->next 是同一类型。所以 &head 和 &node->next
 * 都是 "指向 node 指针的指针"（二级指针），可以统一处理。
 *
 * ================================================================= */

void append_v2(struct node **head, int data)
{
	struct node *new_node = malloc(sizeof(*new_node));
	new_node->data = data;
	new_node->next = NULL;

	/*
	 * lastp 的含义: "新节点应该被写入的地址"
	 *
	 * 最初: lastp = &head
	 *   ┌────┐
	 *   │head│ ← lastp 指向这里
	 *   └────┘
	 *
	 * 遍历: lastp = &(*lastp)->next
	 *   ┌────┐  ┌──────┐  ┌──────┐
	 *   │head│→ │node1 │→ │node2 │→ NULL
	 *   └────┘  │.next │  │.next │← lastp 最终指向这里
	 *           │      │  │      │
	 *           └──────┘  └──────┘
	 */

	printf("  v2: lastp = &head (= %p)\n", (void *)head);

	struct node **lastp = head;   /* 先指向 head 自身的地址 */
	int step = 0;

	/* 遍历: 只要 *lastp 不为 NULL，就继续往前走 */
	while (*lastp) {
		printf("  v2: step %d: *lastp 指向节点 %d → 推进\n",
		       step++, (*lastp)->data);
		lastp = &(*lastp)->next;
	}

	printf("  v2: step %d: *lastp == NULL，在 *lastp 写入新节点\n", step);
	*lastp = new_node;
}

/* =================================================================
 * 方法 C: wake_q_head 风格 — lastp 始终指向尾部"下一个空位"
 *
 * 这是 wake-queue 的实际用法。它的链表不是 NULL 结尾，
 * 而是 WAKE_Q_TAIL 哨兵结尾。但原理完全一样：
 *
 *   struct node *first = NULL;         ← head
 *   struct node **lastp = &first;     ← 始终指向"该写的地方"
 *
 * 每次追加:
 *   *lastp = new_node;     // 写入
 *   lastp = &new_node->next; // lastp 移到新的空位
 *
 * 区别: 不用每次都遍历，因为 lastp 始终保持在尾部！
 * ================================================================= */

void append_v3(struct node **first __attribute__((unused)),
	       struct node ***lastp, int data)
{
	struct node *new_node = malloc(sizeof(*new_node));
	new_node->data = data;
	new_node->next = NULL;

	printf("  v3: *lastp 指向 %p\n", (void *)(*lastp));

	/* 无需遍历，无需判断，就两步 */
	*(*lastp) = new_node;           /* 写入 */
	*lastp = &new_node->next;       /* lastp 指向新的空位 */
}

/* ---------- 测试 ---------- */

void print_list(struct node *head)
{
	for (struct node *n = head; n; n = n->next)
		printf("%d → ", n->data);
	printf("NULL\n");
}

void free_list(struct node *head)
{
	while (head) {
		struct node *next = head->next;
		free(head);
		head = next;
	}
}

int main(void)
{
	printf("╔═════════════════════════════════════╗\n");
	printf("║  一级指针 vs 二级指针 链表追加     ║\n");
	printf("╚═════════════════════════════════════╝\n\n");

	/* --- 方法 A: 普通实现 --- */
	printf("=== 方法 A: 普通实现 (需要 if/else) ===\n\n");
	{
		struct node *head = NULL;

		printf("[追加 10]\n");
		append_v1(&head, 10);
		print_list(head);

		printf("[追加 20]\n");
		append_v1(&head, 20);
		print_list(head);

		printf("[追加 30]\n");
		append_v1(&head, 30);
		print_list(head);

		free_list(head);
	}

	/* --- 方法 B: 二级指针遍历版本 --- */
	printf("\n=== 方法 B: 二级指针消除分支 ===\n\n");
	{
		struct node *head = NULL;

		printf("[追加 10]\n");
		append_v2(&head, 10);
		print_list(head);

		printf("[追加 20]\n");
		append_v2(&head, 20);
		print_list(head);

		printf("[追加 30]\n");
		append_v2(&head, 30);
		print_list(head);

		free_list(head);
	}

	/* --- 方法 C: wake_q_head 风格 --- */
	printf("\n=== 方法 C: wake_q_head 风格 (O(1) 追加) ===\n\n");
	{
		struct node *first = NULL;
		struct node **lastp = &first;   /* lastp 始终指向 "空位" */

		printf("初始: first=%p, lastp=%p (= &first)\n",
		       (void *)first, (void *)lastp);

		printf("\n[追加 100]\n");
		append_v3(&first, &lastp, 100);
		printf("  → first=%p, lastp=%p (= &first->next)\n",
		       (void *)first, (void *)lastp);

		printf("\n[追加 200]\n");
		append_v3(&first, &lastp, 200);
		printf("  → first=%p, lastp=%p (= &second->next)\n",
		       (void *)first, (void *)lastp);

		printf("\n[追加 300]\n");
		append_v3(&first, &lastp, 300);
		printf("  → first=%p, lastp=%p (= &third->next)\n",
		       (void *)first, (void *)lastp);

		printf("\n链表内容: ");
		print_list(first);
		free_list(first);
	}

	printf("\n======== 关键总结 ========\n\n");
	printf("方法 A: if/else 分支 + 遍历 → O(n), 有分支\n");
	printf("方法 B: lastp 遍历 → O(n), 无分支\n");
	printf("方法 C: lastp 始终在尾部 → O(1), 无分支, 无需遍历\n\n");
	printf("方法 C 就是 wake_q_head 的核心思路:\n");
	printf("  lastp 永远指向尾节点的 next 字段 (空表时指向 first 自身)\n");
	printf("  追加只需要两步: *lastp = node; lastp = &node->next;\n");

	return 0;
}
