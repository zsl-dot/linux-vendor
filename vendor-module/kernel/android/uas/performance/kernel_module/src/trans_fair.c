#include <linux/version.h>


#include "sched_common.h"
#include "trans_fair.h"
#include "trans_locking.h"
#include "trans_trace.h"

static void pick_next_ux_fair(struct rq *rq, struct task_struct **task, struct sched_entity **se, bool *repick, bool simple)
{
    enum ux_task_state state = 0;
    struct trans_rq *trq = NULL;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct task_struct *tsk = NULL;
    struct trans_task_struct *trans_ts = NULL;

    trq = get_trans_rq(rq);
    if (is_ux_list_free(&trq->ux_list)) {
        return;
    }

    list_for_each_safe(pos, next, &trq->ux_list) {
        trans_ts = container_of(pos, struct trans_task_struct, ux_node);
        if (unlikely(IS_ERR_OR_NULL(trans_ts))) {
            continue;
        }

        tsk = transts_to_ts(trans_ts);
        if (unlikely(IS_ERR_OR_NULL(tsk))) {
            continue;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        if (tsk->se.sched_delayed) {
            continue;
        }
#endif

        if (unlikely(SCHED_WARN_ON(!tsk->on_rq))) {
            trans_warn_deferred("skip pick ux:task=%#llx\n", (u64)tsk);
            continue;
        }

        if (unlikely(task_cpu(tsk) != rq->cpu)) {
            trans_debug_deferred(DEBUG_PICK, "skip pick ux: rqcpu=%d tskcpu=%d comm=%s, pid=%d, tgid=%d, ux_tags=%#x, total_run=%llu\n", \
                rq->cpu, task_cpu(tsk), tsk->comm, tsk->pid, tsk->tgid, trans_ts->ux_tags, trans_ts->total_run);

            continue;
        }

        if (unlikely(!is_ux_task(tsk, &state))) {
            if (state) {
                trans_debug_deferred(DEBUG_PICK, "skip pick ux[%d]: cpu=%d comm=%s, pid=%d, tgid=%d, ux_tags=%#x, total_run=%llu\n", \
                    state, rq->cpu, tsk->comm, tsk->pid, tsk->tgid, trans_ts->ux_tags, trans_ts->total_run);
            }

            continue;
        }

        *task = tsk;
        *se = &tsk->se;
        *repick = true;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
        if (simple) {
            for_each_sched_entity((*se)) {
                set_next_entity(cfs_rq_of((*se)), (*se));
            }
        }
#endif

        trans_debug_deferred(DEBUG_PICK, "pick ux: cpu=%d, %s/%d/%d, ux_tags=%#x:%#x\n", rq->cpu, tsk->comm, tsk->pid, tsk->tgid, trans_ts->ux_tags, trans_ts->share);
        break;
    }
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
void android_rvh_replace_next_task_fair_hook(void *data,
    struct rq *rq, struct task_struct **task, struct sched_entity **se, bool *repick, bool simple, struct task_struct *prev)
{
    if (!check_uas_feature(FEATURE_DEFAULT)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    lockdep_assert_rq_held(rq);
#else
    lockdep_assert_held(&rq->lock);
#endif
    pick_next_ux_fair(rq, task, se, repick, simple);

    /*
     * The locked-task is prioritized only when there are no
     * UX tasks available, ensuring minimal impact on UX tasks.
     */
    if (*repick != true)
        trans_pick_next_lock_task(rq, task, se, repick, simple);
}
#else
void android_rvh_replace_next_task_fair_hook(void *data,
    struct rq *rq, struct task_struct **task, struct task_struct *prev)
{
    bool simple = false;
    bool repick = false;
    struct sched_entity *unused_se = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT)) {
        return;
    }

    lockdep_assert_rq_held(rq);

    pick_next_ux_fair(rq, task, &unused_se, &repick, simple);

    /*
     * The locked-task is prioritized only when there are no
     * UX tasks available, ensuring minimal impact on UX tasks.
     */
    if (repick != true)
        trans_pick_next_lock_task(rq, task, &unused_se, &repick, simple);
}
#endif

static void trans_check_preempt_wakeup(struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt)
{
    bool curr_is_ux = false;
    bool wake_is_ux = false;
    struct task_struct *curr_task = NULL;

    if (unlikely(!rq || !task || !preempt || !nopreempt)) {
        return;
    }

    /*
     * Prevent any thread from preempting a pipeline thread, even if
     * the waking thread is a UX or RT thread.
     */
    if (trans_is_pipeline_cpu(cpu_of(rq))) {
        *nopreempt = true;
        return;
    }

    curr_task = rq->curr;
    curr_is_ux = is_ux_task(curr_task, NULL);
    wake_is_ux = is_ux_task(task, NULL);

    /*
     * Determine whether it is necessary to ensure the continuous
      *execution of the current task.
     */
    if (lock_tuner & TUNER_CONTINUOUS) {
        /*
         * If high-load consideration is enabled (i.e., the
         * TUNER_ENABLE_ON_HIGHLOAD bit is set), in a high-load situation,
         * the current task can only continue to run if its priority is
         * low, and within the lock protection zone, and the newly awakened
         * task is not ux. In a non-high-load situation, the priority of
         * the current task is not a concern. As long as the current task
         * is within the lock protection zone and the newly awakened task
         * is not ux, the current task will not be preempted, ensuring its
         * uninterrupted execution.
         *
         * If high-load consideration is not enabled (i.e., the
         * TUNER_ENABLE_ON_HIGHLOAD bit is not set), the priority of the
         * current task is again not a concern. As long as the current
         * task is within the lock protection zone and the newly awakened
         * task is not ux, the current task will not be preempted,
         * ensuring its uninterrupted execution.
         */
        bool tune_highloag = lock_tuner & TUNER_ENABLE_ON_HIGHLOAD;
        bool high_load = is_cpu_high_load(cpu_of(rq));
        bool lock_protect = task_inlock_protect(curr_task) && !wake_is_ux;
        bool low_prio = task_low_prio(curr_task);

        if ((tune_highloag && ((high_load && low_prio && lock_protect)
                            || (!high_load && lock_protect)))
            || (!tune_highloag && lock_protect)) {
            *nopreempt = true;
#ifdef TRANS_LOCKING_DEBUG
            trace_lock_protect("continuous", cpu_of(rq), task_pid_nr(curr_task));
            trace_lock_trigger(task_pid_nr(curr_task));
#endif
            lock_trigger++;
            return;
        }
    }

    /*
     * Determine whether the newly awakened task should preempt the
     * current task.
     */
    if (lock_tuner & TUNER_PREEMPT) {
        /*
         * If high-load consideration is enabled (i.e., the
         * TUNER_ENABLE_ON_HIGHLOAD bit is set), in a high-load situation,
         * the newly awakened task can preempt the current task's execution
         * only if it is a lock-protected task with a lower priority, and
         * the current task is not ux. In a non-high-load situation, the
         * newly awakened task can preempt the current task as long as it
         * is within the lock protection zone, and the current task is not
         * within the lock protection zone and is not ux.
         *
         * If high-load consideration is not enabled (i.e., the
         * TUNER_ENABLE_ON_HIGHLOAD bit is not set), the newly awakened
         * task can preempt the current task as long as it is within the
         * lock protection zone, and the current task is not within the
         * lock protection zone and is not ux.
         */
        bool tune_highloag = lock_tuner & TUNER_ENABLE_ON_HIGHLOAD;
        bool high_load = is_cpu_high_load(cpu_of(rq));
        bool lock_protect = task_inlock_protect(task) && !curr_is_ux &&
                            !task_inlock_protect(curr_task);
        bool low_prio = task_low_prio(task);

        if ((tune_highloag && ((high_load && low_prio && lock_protect)
                            || (!high_load && lock_protect)))
            || (!tune_highloag && lock_protect)) {
            *preempt = true;
#ifdef TRANS_LOCKING_DEBUG
            trace_lock_protect("preempt", cpu_of(rq), task_pid_nr(task));
            trace_lock_trigger(task_pid_nr(task));
#endif
            lock_trigger++;
            return;
        }
    }

    if (!curr_is_ux && !wake_is_ux) {
        return;
    }

    if (!curr_is_ux && wake_is_ux) {
        *preempt = true;
        return;
    }

    if (curr_is_ux && !wake_is_ux) {
        *nopreempt = true;
    }
    else {
        if (prio_higher(get_ux_tags(task), get_ux_tags(curr_task))) {
            *preempt = true;
            trans_debug_deferred(DEBUG_KLOG, "cpu=%d, wake:%s/%d/%d, ux_tags=%#x; curr:%s/%d/%d, ux_tags=%#x\n", rq->cpu, \
                task->comm, task->pid, task->tgid, get_ux_tags(task), curr_task->comm, curr_task->pid, curr_task->tgid, get_ux_tags(curr_task));
        }
    }

    /* if curr is ux task, update it's runtime */
    if (!is_ux_node_free(get_ux_node(curr_task))) {
        update_ux_runtime(rq, curr_task);
    }
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt,
    int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked, unsigned int granularity)
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)))
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt,
    int wake_flags, struct sched_entity *se, struct sched_entity *pse, int next_buddy_marked)
#else
void android_rvh_check_preempt_wakeup_hook(void *data, struct rq *rq, struct task_struct *task, bool *preempt, bool *nopreempt,
    int wake_flags, struct sched_entity *se, struct sched_entity *pse)
#endif
{
    if (!check_uas_feature(FEATURE_DEFAULT)) {
        return;
    }

    trans_check_preempt_wakeup(rq, task, preempt, nopreempt);
}
