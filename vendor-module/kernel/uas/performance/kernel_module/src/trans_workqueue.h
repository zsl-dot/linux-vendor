#ifndef _TRANS_WORKQUEUE_H
#define _TRANS_WORKQUEUE_H
#include <linux/workqueue.h>
#include <linux/kthread.h>

#include "sched_common.h"

#define FREE_TASK_DEC_UX_KWORKER_CNT        (1)
#define DISABLE_FEATURE_DEC_UX_KWORKER_CNT  (2)

struct worker;

// Only focus on the top 5 of "struct kthread"
struct trans_kthread {
    unsigned long flags;
    unsigned int cpu;
#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
    int result;
#endif
    int (*threadfn)(void *);
    void *data;
};

enum TRANS_KTHREAD_BITS {
    TRANS_KTHREAD_IS_PER_CPU = 0,
};

enum {
    HIGHPRI_NICE_LEVEL    = MIN_NICE,
};

bool check_ux_kworker_enable(void);
bool check_ux_kworker_type(u32 ux_tags);
int set_ux_kworker_enable(pid_t pid, u32 ux_tags, bool enable);
void dec_ux_kworker_count(struct task_struct *task, int route);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
void register_workqueue_tracepoints(void);
void unregister_workqueue_tracepoints(void);
#else
static void __maybe_unused register_workqueue_tracepoints(void) {}
static void __maybe_unused unregister_workqueue_tracepoints(void) {}
#endif

#endif /* _TRANS_WORKQUEUE_H */
