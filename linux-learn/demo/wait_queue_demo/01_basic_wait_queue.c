/*
 * 01_basic_wait_queue.c — 模拟 wait_event_interruptible + wake_up_interruptible
 *
 * 编译运行: gcc -Wall -pthread -o 01_basic_wait_queue 01_basic_wait_queue.c && ./01_basic_wait_queue
 * 对应阿里云文章的内核模块 test_wait.c。
 * 在用户态用 pthread 模拟内核 wait queue 的用法:
 *   - wait_event_interruptible(wq, condition) 的行为
 *   - wake_up_interruptible(&wq) 的行为
 *   - condition 为 false 时为什么不会被唤醒
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* ============================================================
 * 模拟内核 wait queue 数据结构
 * ============================================================ */

struct my_wait_queue_entry {
	pthread_cond_t  cond;
	pthread_mutex_t mutex;     /* 每个 entry 有自己的 mutex */
	int            *woken;     /* 指向调用者的 woken 标志 */
	int             th_num;    /* 标号 */
	struct my_wait_queue_entry *next;
};

struct my_wait_queue_head {
	pthread_mutex_t  lock;
	struct my_wait_queue_entry *first;
	int              nr_waiting;
};

static int g_condition = 0;

/* ============================================================
 * API 模拟
 * ============================================================ */

static void init_waitqueue_head(struct my_wait_queue_head *wq)
{
	pthread_mutex_init(&wq->lock, NULL);
	wq->first = NULL;
	wq->nr_waiting = 0;
}

/*
 * wait_event_interruptible(wq, condition):
 *   1. 检查 condition → 为真则直接返回
 *   2. 把自己加入等待队列
 *   3. 睡眠 (释放 CPU)
 *   4. 被唤醒 → 重新检查 condition → 为假则继续睡
 */
static void wait_event_interruptible(struct my_wait_queue_head *wq,
				     int *condition, int th_num,
				     const char *caller)
{
	int woken = 0;
	struct my_wait_queue_entry entry = {
		.mutex   = PTHREAD_MUTEX_INITIALIZER,
		.woken   = &woken,
		.th_num  = th_num,
		.next    = NULL,
	};

	pthread_cond_init(&entry.cond, NULL);

	printf("  [%s] wait_event_interruptible(wq, condition!=0)\n", caller);
	printf("       当前 condition=%d → %s\n", *condition,
	       *condition ? "直接返回(不睡)" : "需要睡眠");

	if (*condition) {
		printf("       条件已满足, 直接返回\n");
		return;
	}

	/* 把自己加入等待队列 */
	pthread_mutex_lock(&wq->lock);
	wq->nr_waiting++;
	entry.next = wq->first;
	wq->first = &entry;
	printf("       将自己加入等待队列 (当前 %d 人等待)\n", wq->nr_waiting);
	pthread_mutex_unlock(&wq->lock);

	/* 睡眠循环 */
	printf("       zzz... 进入睡眠\n");
	pthread_mutex_lock(&entry.mutex);
	while (!woken) {
		pthread_cond_wait(&entry.cond, &entry.mutex);
		/* 被唤醒了! 检查 condition */
		printf("       被唤醒! 检查 condition=%d\n", *condition);
		if (*condition) {
			printf("       条件满足, 继续执行!\n");
			break;
		}
		printf("       条件仍未满足, 继续睡眠...\n");
	}
	pthread_mutex_unlock(&entry.mutex);
}

/*
 * wake_up_interruptible(wq):
 *   遍历等待队列, 逐个调用 pthread_cond_signal 唤醒等待者。
 *   唤醒只是把进程叫起来; 被叫醒的进程会重新检查 condition。
 */
static void wake_up_interruptible(struct my_wait_queue_head *wq,
				  const char *caller)
{
	struct my_wait_queue_entry *entry;
	int count = 0;

	printf("\n  [%s] wake_up_interruptible(&wq)\n", caller);

	pthread_mutex_lock(&wq->lock);
	entry = wq->first;
	while (entry) {
		count++;
		*entry->woken = 1;
		pthread_cond_signal(&entry->cond);
		printf("       唤醒第 %d 个等待者 (task %d)\n", count, entry->th_num);
		entry = entry->next;
	}
	wq->nr_waiting = 0;
	printf("       共计唤醒 %d 个进程\n", count);
	pthread_mutex_unlock(&wq->lock);
}

/* ============================================================
 * 场景演示
 * ============================================================ */

static struct my_wait_queue_head g_wq;

static void *work_handler_thread(void *arg)
{
	(void)arg;
	sleep(2);

	printf("\n  [work_handler] 异步工作完成, 设置 condition=1 并唤醒\n");
	g_condition = 1;
	wake_up_interruptible(&g_wq, "work_handler");
	return NULL;
}

/*
 * 场景 A: 正常唤醒 (对应文章第 5 节)
 */
static void test_normal_wakeup(void)
{
	pthread_t work_th;
	int th_num = 100;

	printf("╔══════════════════════════════════════════════╗\n");
	printf("║  场景 A: 正常唤醒 (condition 最终为 1)        ║\n");
	printf("╚══════════════════════════════════════════════╝\n\n");

	g_condition = 0;
	init_waitqueue_head(&g_wq);

	printf("--- schedule_work(&wrk) ---\n");
	pthread_create(&work_th, NULL, work_handler_thread, NULL);

	printf("\n--- wait_event_interruptible(wq, condition != 0) ---\n");
	wait_event_interruptible(&g_wq, &g_condition, th_num, "my_init");

	printf("\n  => 进程成功被唤醒并继续执行!\n");
	pthread_join(work_th, NULL);
}

/*
 * 场景 B: condition 始终为 false (对应文章第 6 节)
 */
static void *bad_work_handler(void *arg)
{
	(void)arg;
	sleep(2);

	printf("\n  [work_handler] 异步工作完成, 但设置 condition=0 (忘记改了!)\n");
	g_condition = 0;
	wake_up_interruptible(&g_wq, "work_handler");
	return NULL;
}

static void test_condition_false(void)
{
	pthread_t work_th;
	int th_num = 200;

	printf("\n\n╔══════════════════════════════════════════════╗\n");
	printf("║  场景 B: 唤醒但条件不满足                  ║\n");
	printf("╚══════════════════════════════════════════════╝\n\n");

	g_condition = 0;
	init_waitqueue_head(&g_wq);

	printf("--- schedule_work(&wrk) ---\n");
	pthread_create(&work_th, NULL, bad_work_handler, NULL);

	printf("\n--- wait_event_interruptible(wq, condition != 0) ---\n");

	/* 模拟带超时的等待: 3 秒后放弃 */
	{
		int woken = 0;
		struct my_wait_queue_entry entry = {
			.mutex  = PTHREAD_MUTEX_INITIALIZER,
			.woken  = &woken,
			.th_num = th_num,
			.next   = NULL,
		};
		pthread_cond_init(&entry.cond, NULL);

		pthread_mutex_lock(&g_wq.lock);
		entry.next = g_wq.first;
		g_wq.first = &entry;
		g_wq.nr_waiting++;
		pthread_mutex_unlock(&g_wq.lock);

		printf("       zzz... 进入睡眠 (最多 3 秒)\n");

		pthread_mutex_lock(&entry.mutex);

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 3;
		int rc = 0;
		while (!woken && rc == 0) {
			rc = pthread_cond_timedwait(&entry.cond, &entry.mutex, &ts);
			if (!woken && rc == 0) {
				printf("       被唤醒! 检查 condition=%d\n", g_condition);
				if (!g_condition)
					printf("       条件仍未满足, 继续睡眠...\n");
			}
		}
		pthread_mutex_unlock(&entry.mutex);

		if (rc != 0) {  /* ETIMEDOUT on timeout */
			printf("       被唤醒! 检查 condition=%d\n", g_condition);
			printf("       条件仍未满足, 继续睡眠...\n");
			printf("       (超时! 一直卡在这里, 就像文章中 insmod 卡住一样)\n");
		}
	}

	printf("\n  => 虽然 wake_up 被调用了, 但 condition=0 不满足\n");
	printf("     进程醒来检查条件后继续睡眠, 永远无法继续执行!\n");

	pthread_cancel(work_th);
	pthread_join(work_th, NULL);
}

/* ============================================================
 * 总结
 * ============================================================ */

int main(void)
{
	printf("╔══════════════════════════════════════════════════════╗\n");
	printf("║  等待队列 (wait queue) 用户态模拟                   ║\n");
	printf("║  对照: 阿里云文章 [工作队列与等待队列]              ║\n");
	printf("╚══════════════════════════════════════════════════════╝\n\n");

	printf("核心机制:\n\n");
	printf("  wait_event_interruptible(wq, condition)\n");
	printf("    ├─ 1. 检查 condition, 为真则立即返回\n");
	printf("    ├─ 2. 将当前进程加入等待队列\n");
	printf("    ├─ 3. 设置状态为 TASK_INTERRUPTIBLE\n");
	printf("    ├─ 4. 调用 schedule() 让出 CPU\n");
	printf("    └─ 5. 被唤醒后, 重新检查 condition\n\n");
	printf("  wake_up_interruptible(&wq)\n");
	printf("    └─ 遍历 wq 中所有条目, 逐个唤醒\n\n");
	printf("  关键: wake_up 只是 [叫醒], 不等于 [条件满足]!\n\n");

	test_normal_wakeup();
	test_condition_false();

	printf("\n\n════════════════════════════════════════════════════════\n");
	printf("  总结\n");
	printf("════════════════════════════════════════════════════════\n\n");
	printf("  文章第 5 节 (condition=1): 进程正常被唤醒, 继续执行\n");
	printf("  文章第 6 节 (condition=0): wait_event 醒来后\n");
	printf("    检查条件 → false → 继续睡 → insmod 永远卡住\n\n");
	printf("  内核编程要点:\n");
	printf("    1. 必须在 wake_up 之前修改 condition!\n");
	printf("    2. wait_event 系列自动处理虚假唤醒\n");
	printf("    3. 这是内核 sleep/wakeup 最基本契约\n\n");

	return 0;
}
