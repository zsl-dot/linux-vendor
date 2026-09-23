#ifndef _TRANS_FAIR_H
#define _TRANS_FAIR_H

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
        for (; se; se = se->parent)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 15, 0)
/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
    return se->cfs_rq;
}
#endif
#else
#define for_each_sched_entity(se) \
        for (; se; se = NULL)

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 15, 0)
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
    struct task_struct *p = task_of(se);
    struct rq *rq = task_rq(p);

    return &rq->cfs;
}
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
extern void set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
void android_rvh_replace_next_task_fair_hook(void *data, \
    struct rq *rq, struct task_struct **task, struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev);
#else
void android_rvh_replace_next_task_fair_hook(void *data, \
    struct rq *rq, struct task_struct **task, struct task_struct *prev);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt, \
	int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked, unsigned int granularity);
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)))
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt, \
	int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked);
#else
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt,
    int wake_flags, struct sched_entity *se, struct sched_entity *pse);
#endif /* KERNEL_VERSION(6, 6, 0) */

#endif /* _TRANS_FAIR_H */
