#ifndef _TRANS_LOADING_H_
#define _TRANS_LOADING_H_

/*
 * The value 600 is calculated as ~60% of (1024).
 * When a task load exceeds this threshold it will be prioritized
 * to run on a powerful CPU.
 */
#define HIGH_LOAD_THRESHOLD        600


extern int frame_rate;

enum call_path {
    CPATH_UPDATE = 1,
    CPATH_TICK = 2,
    CPATH_ENQUEUE = 3,
    CPATH_DEQUEUE = 4,
    CPATH_SWITCH = 5
};

struct load_env {
    struct task_struct *tsk;
    struct task_load *ld;

    u64 now;
    u64 raw_contrib;
    u64 scale_contrib;
    u64 raw_delta;
    u64 scale_delta;

    u64 delta_contrib;
    u64 delta;
    u64 periods;
    u64 u;
    u64 d1;
    u64 c1;
    u64 c2;
    u64 d3;
    u64 c3;
    u64 load_sum;
};

static inline unsigned long task_runnable_avg(struct task_struct *p)
{
    return READ_ONCE(p->se.avg.runnable_avg);
}

static inline unsigned long task_util(struct task_struct *p)
{
    return READ_ONCE(p->se.avg.util_avg);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static inline unsigned long _task_util_est(struct task_struct *p)
{
    return READ_ONCE(p->se.avg.util_est) & ~UTIL_AVG_UNCHANGED;
}
#else
static inline unsigned long _task_util_est(struct task_struct *p)
{
    struct util_est ue = READ_ONCE(p->se.avg.util_est);

    return max(ue.ewma, (ue.enqueued & ~UTIL_AVG_UNCHANGED));
}
#endif

static inline unsigned long task_util_est(struct task_struct *p)
{
    return max(task_util(p), _task_util_est(p));
}

bool task_is_on_runqueue(struct task_struct *tsk);
bool task_is_runnnig_on_cpu(struct task_struct *tsk);
bool task_is_runnable_on_runqueue(struct task_struct *tsk);
void record_enqueue_time(struct task_struct *tsk, u64 enqueue_time);
void get_task_time(struct task_struct *tsk, u64 *running, u64 *runnable, enum call_path cpath);
u64 get_runnable_time(struct task_struct *tsk);
u64 get_running_time(struct task_struct *tsk);
void update_task_loading(struct task_struct *tsk, enum call_path cp);

void trans_task_loading_enqueue(struct rq *rq, struct task_struct *tsk, int flags);
void trans_task_loading_dequeue(struct rq *rq, struct task_struct *tsk, int flags);
unsigned long trans_task_load(struct task_struct *p);
unsigned long trans_task_load_uclamp(struct task_struct *p);
bool task_fits_cpu(struct task_struct *p, int cpu);
bool load_fits_cpu(unsigned long load_avg, int cpu);
bool is_highload(unsigned long load_avg);
bool need_boost(unsigned long load_avg);
void init_task_load(struct task_load *ld);


#endif /* _TRANS_LOADING_H_ */

