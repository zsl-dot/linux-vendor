#ifndef _TRANS_SCHED_H
#define _TRANS_SCHED_H

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <drivers/android/binder_internal.h>
#include <linux/sched/cputime.h>

struct rwsem_waiter;
struct mutex_waiter;
struct rq_flags;

struct trans_sched_class {
    void (*android_rvh_enqueue_task_hook)(void *data, struct rq *rq, struct task_struct *task, int flags);
    void (*android_rvh_dequeue_task_hook)(void *data, struct rq *rq, struct task_struct *task, int flags);
    void (*android_rvh_sched_fork_hook)(void *data, struct task_struct *task);
    void (*android_vh_build_sched_domains_hook)(void *data, bool has_asym);
    void (*android_vh_scheduler_tick_hook)(void *data, struct rq *rq);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    void (*android_rvh_replace_next_task_fair_hook)(void *data, struct rq *rq, struct task_struct **task, struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev);
#else
    void (*android_rvh_replace_next_task_fair_hook)(void *data, struct rq *rq, struct task_struct **task, struct task_struct *prev);
#endif /* KERNEL_VERSION(6, 12, 0) */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
    void (*android_rvh_check_preempt_wakeup_hook)(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt, int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked, unsigned int granularity);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)))
    void (*android_rvh_check_preempt_wakeup_hook)(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt, int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked);
#else
    void (*android_rvh_check_preempt_wakeup_hook)(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt, int wake_flags, struct sched_entity *se, struct sched_entity *pse);
#endif /* KERNEL_VERSION(6, 6, 0) */
    void (*android_vh_binder_restore_priority_hook)(void *data, struct binder_transaction *reply_to, struct task_struct *task);
    void (*android_vh_binder_wait_for_work_hook)(void *data, bool do_proc_work, struct binder_thread *thread, struct binder_proc *proc);
    void (*android_vh_sync_txn_recvd_hook)(void *data, struct task_struct *to_task, struct task_struct *from_task);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    void (*android_vh_binder_proc_transaction_finish_hook)(void *data, struct binder_proc *binder_proc, struct binder_transaction *t, struct task_struct *thread_task, bool pending_async, bool sync);
#else
    void (*android_vh_binder_proc_transaction_end_hook)(void *data, struct task_struct *caller_task, struct task_struct *proc_task, struct task_struct *thread_task, unsigned int code, bool pending_async, bool sync);
#endif /* KERNEL_VERSION(5, 15, 0) */
    void (*android_vh_mutex_wait_start_hook)(void *data, struct mutex *lock);
    void (*android_vh_mutex_unlock_slowpath_hook)(void *data, struct mutex *lock);
    void (*android_vh_alter_mutex_list_add_hook)(void *data, struct mutex *lock, struct mutex_waiter *waiter, struct list_head *list, bool *already_on_list);
    void (*android_vh_rwsem_wake_hook)(void *data, struct rw_semaphore *sem);
    void (*android_vh_rwsem_wake_finish_hook)(void *data, struct rw_semaphore *sem);
    void (*android_vh_alter_rwsem_list_add_hook)(void *data, struct rwsem_waiter *waiter, struct rw_semaphore *sem, bool *already_on_list);
    void (*android_vh_free_task_hook)(void *data, struct task_struct *task);
    void (*task_rename_hook)(void *data, struct task_struct *task, const char *comm);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    void (*sched_switch_prev_hook)(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state);
    void (*sched_switch_next_hook)(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state);
#else
    void (*sched_switch_prev_hook)(void *data, bool preempt, struct task_struct *prev, struct task_struct *next);
    void (*sched_switch_next_hook)(void *data, bool preempt, struct task_struct *prev, struct task_struct *next);
#endif
    bool (*check_ux_task)(struct task_struct *task);
    void (*trans_should_honor_sync)(struct rq *rq, struct task_struct *wakee, bool *sync);
    bool (*balance_on_wakeup_ux)(struct task_struct *tsk, int *orig_cpu, int sync);
    bool (*trans_select_task_rq_rt)(struct task_struct *tsk, int *orig_cpu, struct cpumask *lowest_mask, int wake_flags);
    bool (*filterout_on_wakeup_rt)(struct task_struct *p, int cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 23)
    void (*android_rvh_find_lowest_rq_hook)(void *unused, struct task_struct *p, struct task_struct *exec_ctx, struct cpumask *lowest_mask, int ret, int *best_cpu);
#else
    void (*android_rvh_find_lowest_rq_hook)(void *unused, struct task_struct *p, struct cpumask *lowest_mask, int ret, int *best_cpu);
#endif
    void (*adjust_rt_lowest_mask)(struct task_struct *p, struct cpumask *lowest_mask);
    bool (*android_vh_scheduler_tick)(void *unused, struct rq *rq);
    bool (*android_rvh_sched_newidle_balance)(void *unused, struct rq *this_rq, struct rq_flags *rf, int *pulled_task, int *done);
    void (*schedule)(struct task_struct *prev, struct task_struct *next, struct rq *rq);
    void (*scheduler_tick_entry)(struct rq *rq);
    u64 (*xmu_cpufreq_limit)(int cpu, unsigned long *freq, unsigned long max);
    bool (*is_runnable_boost_enabled)(void);
    unsigned long (*xmu_cpufreq_set)(int cpu);
};

#endif /* _TRANS_SCHED_H */
