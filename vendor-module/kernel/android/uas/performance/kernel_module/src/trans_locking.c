/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 transsion Inc.
 */

#include <trace/hooks/dtask.h>
#include <trace/hooks/sched.h>
#include <trace/events/sched.h>
#include <linux/rtmutex.h>

#include "sched_common.h"
#include "trans_fair.h"
#include "trans_locking.h"
#include "trans_trace.h"

u64 lock_trigger;

DECLARE_LOCK_OPTIONS(total);
DECLARE_LOCK_OPTIONS(mutex);
DECLARE_LOCK_OPTIONS(rtmutex);
DECLARE_LOCK_OPTIONS(rwsem);

#ifdef TRANS_LOCKING_DEBUG

#define LOCK_STACK_DEPTH        8
#define STCAK_BUFF_SIZE         1024
static void lock_dump_stack(struct task_struct *tsk)
{
    unsigned int depth;
    unsigned long stacks[LOCK_STACK_DEPTH] = {0};
    char buffer[STCAK_BUFF_SIZE];
    int offset = 0;
    int idx;

    depth = stack_trace_save_tsk(tsk, &stacks[0], LOCK_STACK_DEPTH, 0);
    if (!depth)
        return;

    for (idx = depth - 1; idx >= 0; idx--) {
        if (offset >= STCAK_BUFF_SIZE)
            break;
        offset += snprintf(buffer + offset, STCAK_BUFF_SIZE - offset,
        "%pS %s ", (void *)stacks[idx], idx ? "<-" : "\n");
    }
    trace_printk("[LOCKING_STACK] [%s$%d] : %s\n",
        tsk->comm, tsk->pid, buffer);
}

static void debug_lock_state(struct trans_task_struct *tts)
{
    int fail = 0;

    /*
     * When depth is 0, start must also be 0
     * When depth is not 0, start must also not be 0
     */
    if ((mutex.get_depth(tts) == 0 && mutex.get_lock_start(tts) != 0) ||
        (mutex.get_depth(tts) != 0 && mutex.get_lock_start(tts) == 0)) {
        fail++;
    }

    if ((rtmutex.get_depth(tts) == 0 && rtmutex.get_lock_start(tts) != 0) ||
        (rtmutex.get_depth(tts) != 0 && rtmutex.get_lock_start(tts) == 0)) {
        fail++;
    }

    if ((rwsem.get_depth(tts) == 0 && rwsem.get_lock_start(tts) != 0) ||
        (rwsem.get_depth(tts) != 0 && rwsem.get_lock_start(tts) == 0)) {
        fail++;
    }

    if ((total.get_depth(tts) == 0 && total.get_lock_start(tts) != 0) ||
        (total.get_depth(tts) != 0 && total.get_lock_start(tts) == 0)) {
        fail++;
    }

    /*
     * If the depth of the total is 0, which indicates that the task is not in
     * the critical section, and therefore, the depth variable for all types
     * of locks should also be 0.
     */
    if ((total.get_depth(tts) == 0) &
        ((mutex.get_depth(tts) != 0) ||
        (rtmutex.get_depth(tts) != 0) ||
        (rwsem.get_depth(tts) != 0))) {
        fail++;
    }

    if (fail) {
        trace_printk("[LOCKING_ERR] total: %10lu, %10lu, %10llu, %10llu, %10llu\n",
                    total.get_depth(tts),
                    total.get_lock_start(tts),
                    total.get_dcounts(tts),
                    total.get_dstart(tts),
                    total.get_duration(tts));

        trace_printk("[LOCKING_ERR] mutex: %10lu, %10lu, %10llu, %10llu, %10llu\n",
                    mutex.get_depth(tts),
                    mutex.get_lock_start(tts),
                    mutex.get_dcounts(tts),
                    mutex.get_dstart(tts),
                    mutex.get_duration(tts));

        trace_printk("[LOCKING_ERR] rtmutex: %10lu, %10lu, %10llu, %10llu, %10llu\n",
                    rtmutex.get_depth(tts),
                    rtmutex.get_lock_start(tts),
                    rtmutex.get_dcounts(tts),
                    rtmutex.get_dstart(tts),
                    rtmutex.get_duration(tts));

        trace_printk("[LOCKING_ERR] rwsem: %10lu, %10lu, %10llu, %10llu, %10llu\n",
                    rwsem.get_depth(tts),
                    rwsem.get_lock_start(tts),
                    rwsem.get_dcounts(tts),
                    rwsem.get_dstart(tts),
                    rwsem.get_duration(tts));
    }
}

static void lock_dump_status(struct task_struct *tsk, const char *caller)
{
    struct trans_task_struct *tts = NULL;

    if (!tsk)
        return;

    tts = get_trans_task_struct(tsk);
    if (IS_ERR_OR_NULL(tts))
        return;

    lock_debug_traceprintk("[LOCKING_DBG][%d] [%s$%d] %s\n",
                __LINE__, tsk->comm, tsk->pid, caller);

    lock_debug_systrace(TRANS_SYSTRACE_POS, mutex.get_depth(tts),
                "mutex_depth[%d]", task_pid_nr(tsk));
    lock_debug_systrace(TRANS_SYSTRACE_POS, rtmutex.get_depth(tts),
                "rtmutex_depth[%d]", task_pid_nr(tsk));
    lock_debug_systrace(TRANS_SYSTRACE_POS, rwsem.get_depth(tts),
                "rwsem_depth[%d]", task_pid_nr(tsk));
    lock_debug_systrace(TRANS_SYSTRACE_POS, total.get_depth(tts),
                "total_depth[%d]", task_pid_nr(tsk));
}
#endif

static int high_load_count[TRANS_NR_CPUS] = {0};
static int low_load_count[TRANS_NR_CPUS] = {0};
static enum cpu_load_state cpu_load_states[TRANS_NR_CPUS] = {LOW_LOAD};

void update_cpu_load(struct rq *rq)
{
    int cpu = cpu_of(rq);

    if (rq->nr_running >= HIGH_LOAD_NR_RUNNING) {
        high_load_count[cpu]++;
        low_load_count[cpu] = 0;
    } else if (rq->nr_running <= LOW_LOAD_NR_RUNNING) {
        low_load_count[cpu]++;
        high_load_count[cpu] = 0;
    } else {
        /*
         * If the number of tasks falls between 1 and 2, reset the counters.
         */
        high_load_count[cpu] = 0;
        low_load_count[cpu] = 0;
    }

    /*
     * Update the CPU's load status.
     */
    if (high_load_count[cpu] >= CONSECUTIVE_COUNT) {
        cpu_load_states[cpu] = HIGH_LOAD;
    } else if (low_load_count[cpu] >= CONSECUTIVE_COUNT) {
        cpu_load_states[cpu] = LOW_LOAD;
    }

#ifdef TRANS_LOCKING_DEBUG
    trace_show_loadstates(cpu, cpu_load_states[cpu]);
#endif
}

/*
 * Determine if the specified CPU is in a high load state.
 */
bool is_cpu_high_load(int cpu)
{
    return cpu_load_states[cpu] == HIGH_LOAD;
}

/*
 * Determine if the specified CPU is in a low load state.
 */
bool is_cpu_low_load(int cpu)
{
    return cpu_load_states[cpu] == LOW_LOAD;
}

bool is_topapp(struct task_struct *tsk)
{
    if (!tsk)
        return false;

    return tsk->pid == tsk->tgid;
}

#define MAX_LOCK_TASKS              5

static inline
void inc_task_count(struct trans_rq *trq, struct task_struct *tsk)
{
    if (tsk)
        get_task_struct(tsk);

    trq->lock_queue->nr_tasks++;
}

static inline
void dec_task_count(struct trans_rq *trq, struct task_struct *tsk)
{
    if (tsk)
        put_task_struct(tsk);

    if (--trq->lock_queue->nr_tasks <= 0)
        trq->lock_queue->enable = true;
}

static inline
struct task_struct *lock_entry_to_task(struct list_head *entity)
{
    struct trans_task_struct *tts = NULL;

    tts = container_of(entity, struct trans_task_struct, lock_entry);
    if (IS_ERR_OR_NULL(tts))
        return NULL;

    return tts->task;
}

static inline
struct trans_task_struct *lock_entry_to_tts(struct list_head *entity)
{
    return container_of(entity, struct trans_task_struct, lock_entry);
}

void dump_lock_queue(struct rq *rq)
{
    struct list_head *pos = NULL;
    struct list_head *n = NULL;
    struct list_head *locklist = NULL;
    struct trans_rq *trq = NULL;
    struct task_struct *tsk;
    struct trans_task_struct *lock_tts;
    unsigned long tsk_state;
    int idx = 0;

    if (likely(!(g_systrace_mask & (TRANS_TRACE_LK_DUMP_LOCKTASK))))
        return;

    if (!rq)
        return;

    trq = get_trans_rq(rq);
    if (!trq || !trq->lock_queue || !trq->lock_queue->nr_tasks)
        return;

    locklist = &trq->lock_queue->lock_list;
    if (!locklist || trans_list_empty(locklist))
        return;

    list_for_each_safe(pos, n, locklist) {
        lock_tts = lock_entry_to_tts(pos);
        if (IS_ERR_OR_NULL(lock_tts)) {
            continue;
        }

        tsk = lock_tts->task;
        if (IS_ERR_OR_NULL(tsk)) {
            continue;
        }

        tsk_state = task_state(tsk);
        trace_printk("[LOCKING_DUMP] [CPU%d] [%d/%d] enable=%d, task=%s$%d, "
                    "prio=%d, state=0x%lx\n",
            cpu_of(rq), ++idx,
            trq->lock_queue->nr_tasks, trq->lock_queue->enable,
            tsk->comm, tsk->pid, tsk->prio, tsk_state);
    }
}

void dequeue_locktask_on_fork(struct trans_task_struct *tts)
{
    struct rq *rq = NULL;
    struct trans_rq *trq = NULL;
    unsigned long irqflag;
    struct task_struct *tsk;

    if (!tts)
        return;

    rq = tts->rq;
    if (!rq)
        return;

    trq = get_trans_rq(rq);
    if (!trq)
        return;

    /*
     * When a task terminates or is newly created, if its corresponding
     * trans_task_struct structure is still present in the lock_list,
     * it should be removed from the list.
     */
    spin_lock_irqsave(&trq->lock_queue->list_lock, irqflag);
    if (!trans_list_empty(&tts->lock_entry)) {
        tsk = tts->task;
        list_del_init(&tts->lock_entry);
        dec_task_count(trq, tsk);
        tts->rq = NULL;
    }
    spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);
}

/*
 * This is primarily to take into account the impact of task migration.
 */
void dequeue_locking_task(struct rq *rq,
            struct task_struct *tsk)
{
    struct trans_task_struct *tts = NULL;
    struct trans_rq *trq = NULL;
    unsigned long irqflag;

    if (!rq || !tsk)
        return;

    tts = get_trans_task_struct(tsk);
    trq = get_trans_rq(rq);
    if (IS_ERR_OR_NULL(tts) || IS_ERR_OR_NULL(trq))
        return;

    spin_lock_irqsave(&trq->lock_queue->list_lock, irqflag);

    /*
     * Lockable access
     * If there are no tasks on the lock_list, return directly
     */
    if (trans_list_empty(&tts->lock_entry)) {
        spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);
        return;
    }

    /*
     * Remove tasks from the linked list and update nr_tasks.
     */
    list_del_init(&tts->lock_entry);
    tts->rq = NULL;
    dec_task_count(trq, tsk);

    spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);

    trace_dequeue_lock_thread(rq, tsk);
}

void enqueue_locking_task(struct rq *rq,
            struct task_struct *tsk)
{
    struct trans_task_struct *tts = NULL;
    struct trans_rq *trq = NULL;
    unsigned long irqflag;
    bool tune_highloag = false;
    bool high_load = false;
    bool low_prio = false;

    if (!check_uas_feature(FEATURE_LOCK_PROTECT))
        return;

    if (check_sched_scene(SS_BENCH))
        return;

    if (!(lock_tuner & TUNER_PICK_LOCK_TASK))
        return;

    if (!rq || !tsk)
        return;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    if (tsk->se.sched_delayed)
        return;
#endif

    tts = get_trans_task_struct(tsk);
    trq = get_trans_rq(rq);
    if (IS_ERR_OR_NULL(tts) || IS_ERR_OR_NULL(trq))
        return;

    /*
     * dump the tasks on lock_queue one by one
     */
    dump_lock_queue(rq);

    if (!is_topapp(tsk) && !trq->lock_queue->enable)
        return;

    /*
     * Check if the task is already in the lock_list without holding
     * the lock. If it is, there is no need to perform the enqueue
     * operation again.
     */
    if (!trans_list_empty(&tts->lock_entry))
        return;

    /*
     * Focus on CFS tasks.
     */
    if (!is_fair_task(tsk))
        return;

    /*
     * UX tasks also do not need to be inserted into the lock_list
     * linked list, because they can already use CPU resources first.
     */
    if (is_ux_task(tsk, NULL))
        return;

    /*
     * If the task is not currently in a locked critical section,
     * there is no need to perform subsequent operations.
     */
    if (!task_inlock_protect(tsk))
        return;

    /*
     * In high-load scenarios, only low-priority tasks are allowed to enqueue.
     */
    tune_highloag = lock_tuner & TUNER_ENABLE_ON_HIGHLOAD;
    high_load = is_cpu_high_load(cpu_of(rq));
    low_prio = task_low_prio(tsk);
    if (tune_highloag) {
        if (high_load && !low_prio)
            return;
    }

    /*
     * Lockless access
     * Each CPU is capable of enqueuing up to five locked tasks.
     */
    if (!is_topapp(tsk) && trq->lock_queue->nr_tasks > MAX_LOCK_TASKS) {
        trq->lock_queue->enable = false;
        return;
    }

    spin_lock_irqsave(&trq->lock_queue->list_lock, irqflag);

    /*
     * After obtaining the list_lock, check again to see if the
     * task already exists in the lock_list list.
     */
    if (!trans_list_empty(&tts->lock_entry)) {
        spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);
        return;
    }

    /*
     * Lockable access
     * Each CPU is capable of enqueuing up to five locked tasks.
     */
    if (!is_topapp(tsk) && trq->lock_queue->nr_tasks > MAX_LOCK_TASKS) {
        trq->lock_queue->enable = false;
        spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);
        return;
    }

    /*
     * Insert to the end of the lock_list list directly.
     */
    list_add_tail(&tts->lock_entry, &trq->lock_queue->lock_list);
    tts->rq = rq;
    inc_task_count(trq, tsk);

    spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);

    trace_enqueue_lock_thread(rq, tsk);
}

void trans_pick_next_lock_task(struct rq *rq,
            struct task_struct **p, struct sched_entity **se,
            bool *repick, bool simple)
{
    struct trans_rq *trq = NULL;
    struct list_head *pos = NULL;
    struct list_head *n = NULL;
    struct sched_entity *lock_se;
    struct task_struct *lock_task;
    struct trans_task_struct *lock_tts;
    unsigned long irqflag;

    if (!rq || !p || !se)
        return;

    trq = get_trans_rq(rq);

    /*
     * Lockless access
     * If there is no task on the linked list, return directly.
     */
    if (!trq || !trq->lock_queue || !trq->lock_queue->nr_tasks)
        return;

    spin_lock_irqsave(&trq->lock_queue->list_lock, irqflag);

    /*
     * Lockable access
     * If there are no tasks on the lock_list, return directly
     */
    if (!trq->lock_queue->nr_tasks) {
        spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);
        return;
    }

    /*
     * pick a task from the lock_list list to run.
     */
    list_for_each_safe(pos, n, &trq->lock_queue->lock_list) {
        lock_tts = lock_entry_to_tts(pos);
        if (IS_ERR_OR_NULL(lock_tts))
            continue;

        lock_task = lock_tts->task;
        if (IS_ERR_OR_NULL(lock_task)) {
            list_del_init(&lock_tts->lock_entry);
            lock_tts->rq = NULL;
            dec_task_count(trq, NULL);
            continue;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        if (lock_task->se.sched_delayed) {
            continue;
        }
#endif

        lock_se = &lock_task->se;

        /*
         * This is only applicable to CFS threads;
         * UX threads do not require our assistance.
         */
        if (unlikely(!lock_se) || !is_fair_task(lock_task)
            || is_ux_task(lock_task, NULL)) {
            continue;
        }

        /*
         * If the task being traversed is not in the lock's critical
         * section, it needs to be removed from the list.
         */
        if (!task_inlock_protect(lock_task)) {
            continue;
        }

        /*
         * Remove the exiting thread.
         */
        if (lock_task->flags & PF_EXITING) {
            continue;
        }

        /*
         * If the task is running on another CPU, skip it, as it will
         * be removed after it finishes executing.
         */
        if (unlikely(task_cpu(lock_task) != rq->cpu))
            continue;

        /*
         * Successfully picked up a task in the lock critical region.
         */
        *p = lock_task;
        *se = lock_se;
        *repick = true;

        break;
    }

    spin_unlock_irqrestore(&trq->lock_queue->list_lock, irqflag);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    if (simple && true == *repick) {
        for_each_sched_entity((*se)) {
            set_next_entity(cfs_rq_of((*se)), (*se));
        }
    }
#endif

    if (*repick) {
        trace_pick_lock_thread(rq, *p);
#ifdef TRANS_LOCKING_DEBUG
        trace_lock_trigger(task_pid_nr(*p));
#endif
        lock_trigger++;
    }
}

static inline
void mark_task_lock(struct trans_task_struct *tts,
            struct trans_lock_operations *ops)
{
    tts->lockinfo.ops = (void *) ops;
}

static inline
struct trans_lock_operations * get_task_lock(struct trans_task_struct *tts)
{
    return (struct trans_lock_operations *)tts->lockinfo.ops;
}

bool task_inlock(struct task_struct *tsk)
{
    struct trans_task_struct *tts = NULL;
    unsigned long lock_start;

    if (!check_uas_feature(FEATURE_LOCK_PROTECT))
        return false;

    if (check_sched_scene(SS_BENCH))
        return false;

    if (!tsk)
        return false;

    tts = get_trans_task_struct(tsk);
    if (IS_ERR_OR_NULL(tts))
        return false;

    lock_start = total.get_lock_start(tts);

    return lock_start > 0;
}

/*
 * In units of jiffies
 *
 * The default lock protection duration is 4 ms.
 */

#define LOCK_PROTECT_TIMEOUT        MSEC_TO_NSEC(4)
bool task_inlock_protect(struct task_struct *tsk)
{
    struct trans_task_struct *tts = NULL;
    unsigned long lock_start;

    bool ret = false;
    bool timeout = false;
    u64 running_time;

    if (!check_uas_feature(FEATURE_LOCK_PROTECT))
        return false;

    if (check_sched_scene(SS_BENCH))
        return false;

    if (!tsk)
        return false;

    tts = get_trans_task_struct(tsk);
    if (IS_ERR_OR_NULL(tts))
        return false;

    /*
     * This task does not hold any locks
     */
    if (!total.get_depth(tts))
        return false;

    lock_start = total.get_lock_start(tts);

    /*
     * Return true if the task is in the critical region of the lock
     * and has not timed out, otherwise clear all lock information
     * and return false if it has timed out
     */
    running_time = tsk->se.sum_exec_runtime - tsk->se.prev_sum_exec_runtime;
    if (lock_tuner & TUNE_CONSIDER_RUNNING_TIME) {
        timeout = lock_start && time_after(jiffies, lock_start) &&
                            (running_time > LOCK_PROTECT_TIMEOUT);
    } else {
        timeout = lock_start && time_after(jiffies, lock_start);
    }

    if (timeout) {
        memset(&tts->lockinfo, 0, sizeof(struct task_lock_info));
        ret = false;
    } else {
        ret = true;
    }

    trace_task_inlock_protect(tsk,
        total.get_depth(tts), total.get_lock_start(tts), running_time, timeout);

#ifdef TRANS_LOCKING_DEBUG
    lock_dump_status(tsk, __func__);
    debug_lock_state(tts);
#endif
    return ret;
}

static void record_lock_starttime(struct trans_lock_operations *ops,
            struct task_struct *tsk, unsigned long settime)
{
    struct trans_task_struct *tts = NULL;
    bool exit_dstate = false;

    if (!tsk)
        return;

    tts = get_trans_task_struct(tsk);
    if (IS_ERR_OR_NULL(tts))
        return;

    if (settime) {
        /*
         * Enter the critical section
         */

        /*
         * used to mark whether the task has exited from
         * the D state and is ready to allocate a lock.
         */
        exit_dstate = !!get_task_lock(tts);

        /*
         * The task's timestamp needs to be refreshed if it has just exited
         * the D state. If the task holds locks in a nested manner, only the
         * earliest timestamp, corresponding to the first lock acquisition,
         * will be recorded.
         */
        if (exit_dstate || ops->get_depth(tts) == 0)
            ops->set_lock_start(tts, settime);

        if (exit_dstate || total.get_depth(tts) == 0)
            total.set_lock_start(tts, settime);

        /*
         * If exit_dstate variable is not NULL, it indicates that the task
         * has just exited the D state and is attempting to acquire the
         * lock again. In this case, there is no need to increment the depth.
         */
        if (exit_dstate) {
            mark_task_lock(tts, NULL);
        } else {
            ops->inc_depth(tts);
            total.inc_depth(tts);
        }
    } else {

        /*
         * Exit the critical section
         */

        /*
         * Clear after all locks have exited the critical section.
         */
        ops->dec_depth(tts);
        if (ops->get_depth(tts) == 0) {
            ops->set_lock_start(tts, 0);
        }

        total.dec_depth(tts);
        if (total.get_depth(tts) == 0) {
            total.set_lock_start(tts, 0);
        }
    }

#ifdef TRANS_LOCKING_DEBUG
    trace_alloc_lock(ops->lock_type, tsk,
                ops->get_depth(tts), total.get_depth(tts));

    lock_dump_status(tsk, __func__);
    debug_lock_state(tts);
#endif

    return;
}

void lock_wait_start(struct trans_lock_operations *ops)
{
    struct trans_task_struct *tts = NULL;
    u64 dstart;
    u64 now;

    tts = get_trans_task_struct(current);
    if (IS_ERR_OR_NULL(tts))
        return;

    dstart = ops->get_dstart(tts);
    if (dstart == 0) {
        now = ktime_get_ns();
        ops->set_dstart(tts, now);


        /*
         * Record the timestamp of the first lock acquisition when
         * there is nested locking
         */
        if (ops->get_depth(tts) == 0)
            ops->set_lock_start(tts, jiffies);

        /*
         * When a task enters the D state, the critical section
         * of the lock needs to be pre-marked to ensure it can be
         * prioritized for execution upon waking.
         */
        ops->inc_depth(tts);

        if (total.get_depth(tts) == 0)
            total.set_lock_start(tts, jiffies);
        total.inc_depth(tts);
    }

    /*
     * Record the reason for entering the D state this time,
     * specifically the type of lock being awaited.
     */
    mark_task_lock(tts, ops);

#ifdef TRANS_LOCKING_DEBUG
    lock_dump_status(current, __func__);
    debug_lock_state(tts);
#endif
}

#define OPT_SPIN_THRESHOLD_US       (500)

/*
 * It is end of the D state, as the task has already been awakened.
 */
static void sched_waking_dstate_end(void *unused,
            struct task_struct *tsk)
{
    u64 now = 0;
    u64 dstart = 0;
    u64 duration_ns = 0;
    u64 duration_ms = 0;
    struct trans_task_struct *tts = NULL;
    struct trans_lock_operations *ops = NULL;

    if (!tsk)
        return;

    tts = get_trans_task_struct(tsk);
    if (IS_ERR_OR_NULL(tts))
        return;

    /*
     * If it holds another type of lock, return here without
     * performing subsequent operations.
     */
    ops = get_task_lock(tts);
    if (!ops)
        return;

    dstart = ops->get_dstart(tts);
    if (!dstart)
        return;

    /*
     * Calculate the duration spent in the D state.
     */
    now = ktime_get_ns();
    duration_ns = now - dstart;
    duration_ms = NSEC_TO_MSEC(duration_ns);

    /*
     * Clear after use to indicate that the D state has been exited.
     */
    ops->set_dstart(tts, 0);

    /*
     * NOTE:
     * Due to mechanisms like optimistic spinning and try_lock in
     * some type of locks, trace_android_vh_rwsem_write_wait_start
     * might not be the actual starting point of the D state.
     * Therefore, if the calculated duration of the D state is less
     * than 500 microseconds, we consider this instance not to be
     * a true D state.
     */
    if (NSEC_TO_USEC(duration_ns) < OPT_SPIN_THRESHOLD_US)
        return;

    /*
     * The lock acquisition timestamp needs to be refreshed
     * when a task exits the D state.
     */
    ops->set_lock_start(tts, jiffies);
    total.set_lock_start(tts, jiffies);

    /*
     * Update the total number of times that a task enters the D
     * state and the cumulative duration of the task in the D state.
     */
    ops->inc_dcounts(tts);
    ops->add_duration(tts, duration_ns);

    /*
     * Update the total number of times the entire system enters
     * the D state and the cumulative duration spent in the D state.
     */
    total.inc_dcounts(tts);
    ops->stats.dcounts++;
    ops->stats.duration_ns += duration_ns;
    total.stats.dcounts++;
    total.stats.duration_ns += duration_ns;

    /*
     * Calculates the number of times D states with different
     * durations are generated
     */
    if (duration_ms < 1) {
        ops->stats.dcounts_1ms++;
        total.stats.dcounts_1ms++;
    } else if (duration_ms < 5) {
        ops->stats.dcounts_5ms++;
        total.stats.dcounts_5ms++;
    } else if (duration_ms < 10) {
        ops->stats.dcounts_10ms++;
        total.stats.dcounts_10ms++;
    } else if (duration_ms < 20) {
        ops->stats.dcounts_20ms++;
        total.stats.dcounts_20ms++;
    } else if (duration_ms < 50) {
        ops->stats.dcounts_50ms++;
        total.stats.dcounts_50ms++;
    } else if (duration_ms < 100) {
        ops->stats.dcounts_100ms++;
        total.stats.dcounts_100ms++;
    } else {
        ops->stats.dcounts_long++;
        total.stats.dcounts_long++;
    }

#ifdef TRANS_LOCKING_DEBUG
    trace_show_dstate_duration(ops->lock_type,
            tsk, NSEC_TO_USEC(duration_ns));

    lock_dump_status(tsk, __func__);
    debug_lock_state(tts);
#endif
}

void trans_set_next_task(struct task_struct *tsk)
{
    struct sched_entity *se = NULL;

    if (!tsk)
        return;

    /*
     * set_next_buddy
     */
    se = &tsk->se;
    for_each_sched_entity(se) {
        if (se->on_rq) {
            cfs_rq_of(se)->next = se;
        }
    }
}

static void sched_switch_prev(struct task_struct *prev)
{
    unsigned long tsk_state;
    struct trans_task_struct *tts = NULL;

    if (!prev)
        return;

    tts = get_trans_task_struct(prev);
    if (IS_ERR_OR_NULL(tts))
        return;

    /*
     * If the task is preempted or enters the D state, the lock
     * information should be retained; otherwise, it should be
     * cleared.
     */
    tsk_state = task_state(prev);
    if (tsk_state == TASK_RUNNING || (tsk_state & TASK_UNINTERRUPTIBLE))
        return;

    memset(&tts->lockinfo, 0, sizeof(struct task_lock_info));

#ifdef TRANS_LOCKING_DEBUG
    lock_dump_status(prev, __func__);
    debug_lock_state(tts);
#endif
}

void sched_switch_reset_lock_info(struct task_struct *prev,
            struct task_struct *next)
{
    sched_switch_prev(prev);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void android_vh_record_mutex_lock_starttime(void *unused,
            struct mutex *lock, unsigned long settime)
{
    record_lock_starttime(&mutex, current, settime);
}
#else
static void android_vh_record_mutex_lock_starttime(void *unused,
            struct task_struct *tsk, unsigned long settime)
{
    record_lock_starttime(&mutex, tsk, settime);
}
#endif
static void android_vh_mutex_wait_start(void *unused,
            struct mutex *lock)
{
    lock_wait_start(&mutex);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void android_vh_record_rtmutex_lock_starttime(void *unused,
            struct rt_mutex *lock, unsigned long settime)
{
    record_lock_starttime(&rtmutex, current, settime);
}
#else
static void android_vh_record_rtmutex_lock_starttime(void *unused,
            struct task_struct *tsk, unsigned long settime)
{
    record_lock_starttime(&rtmutex, tsk, settime);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
static void android_vh_rtmutex_wait_start(void *unused,
            struct rt_mutex_base *lock)
#else
static void android_vh_rtmutex_wait_start(void *unused,
            struct rt_mutex *lock)
#endif
{
    lock_wait_start(&rtmutex);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void android_vh_record_rwsem_lock_starttime(void *unused,
            struct rw_semaphore *sem, unsigned long settime)
{
    record_lock_starttime(&rwsem, current, settime);
}
#else
static void android_vh_record_rwsem_lock_starttime(void *unused,
            struct task_struct *tsk, unsigned long settime)
{
    record_lock_starttime(&rwsem, tsk, settime);
}

#endif

static void android_vh_rwsem_read_wait_start(void *unused,
            struct rw_semaphore *sem)
{
    lock_wait_start(&rwsem);
}

static void android_vh_rwsem_write_wait_start(void *unused,
            struct rw_semaphore *sem)
{
    lock_wait_start(&rwsem);
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0)
void android_rvh_check_preempt_tick(void *unused,
            struct task_struct *task,
            unsigned long *ideal_runtime, bool *skip_preempt,
            unsigned long delta_exec, struct cfs_rq *cfs_rq,
            struct sched_entity *se, unsigned int granularity)
{
    struct task_struct *curr = NULL;
    bool tune_highloag = false;
    bool high_load = false;
    bool low_prio = false;
    bool lock_protect = false;

    if (!check_uas_feature(FEATURE_LOCK_PROTECT))
        return;

    if (check_sched_scene(SS_BENCH))
        return;

    if (!(lock_tuner & TUNER_CONTINUOUS))
        return;

    curr = entity_is_task(se) ? task_of(se) : NULL;
    if (curr == NULL)
        return;

    if (is_ux_task(curr, NULL))
        return;

    tune_highloag = lock_tuner & TUNER_ENABLE_ON_HIGHLOAD;
    high_load = is_cpu_high_load(smp_processor_id());
    low_prio = task_low_prio(curr);
    lock_protect = task_inlock_protect(curr);

    /*
     * When high-load consideration is enabled (i.e., the
     * TUNER_ENABLE_ON_HIGHLOAD bit is set), in high-load scenarios,
     * only low-priority tasks holding locks are allowed to continue
     * running. In non-high-load scenarios, the system operates according
     * to the original logic. If the current task is not a low-priority
     * task, no action is taken.
     *
     * When high-load consideration is not enabled (i.e., the
     * TUNER_ENABLE_ON_HIGHLOAD bit is not set), the priority of the
     * current task is not a concern. As long as the current task is
     * within the lock protection zone, it will be ensured that it
     * can continue running uninterrupted.
     */
    if ((tune_highloag && ((high_load && low_prio && lock_protect)
                        || (!high_load && lock_protect)))
        || (!tune_highloag && lock_protect)) {
        *skip_preempt = true;

#ifdef TRANS_LOCKING_DEBUG
        trace_lock_protect("continuous", smp_processor_id(), task_pid_nr(curr));
        trace_lock_trigger(task_pid_nr(curr));
#endif
        lock_trigger++;
    }
}
#endif

int register_trans_locking_hook(void)
{
    int ret = 0;

    /*
     * mutex
     */
    ret = register_trace_android_vh_record_mutex_lock_starttime(
                android_vh_record_mutex_lock_starttime, NULL);
    if (ret != 0) {
        trans_warn("android_vh_record_mutex_lock_starttime failed! ret=%d\n", ret);
        goto out1;
    }

    ret = register_trace_android_vh_mutex_wait_start(
                android_vh_mutex_wait_start, NULL);
    if (ret != 0) {
        trans_warn("android_vh_mutex_wait_start failed! ret=%d\n", ret);
        goto out2;
    }

    /*
     * rtmutex
     */
    ret = register_trace_android_vh_record_rtmutex_lock_starttime(
                android_vh_record_rtmutex_lock_starttime, NULL);
    if (ret != 0) {
        pr_err("android_vh_record_rtmutex_lock_starttime failed! ret=%d\n", ret);
        goto out3;
    }

    ret = register_trace_android_vh_rtmutex_wait_start(
                android_vh_rtmutex_wait_start, NULL);
    if (ret != 0) {
        pr_err("android_vh_rtmutex_wait_start failed! ret=%d\n", ret);
        goto out4;
    }

    /*
     * rwsem
     */
    ret = register_trace_android_vh_record_rwsem_lock_starttime(
                android_vh_record_rwsem_lock_starttime, NULL);
    if (ret != 0) {
        pr_err("android_vh_record_rwsem_lock_starttime failed! ret=%d\n", ret);
        goto out5;
    }

    ret = register_trace_android_vh_rwsem_read_wait_start(
                android_vh_rwsem_read_wait_start, NULL);
    if (ret != 0) {
        pr_err("android_vh_rwsem_read_wait_start failed! ret=%d\n", ret);
        goto out6;
    }

    ret = register_trace_android_vh_rwsem_write_wait_start(
                android_vh_rwsem_write_wait_start, NULL);
    if (ret != 0) {
        pr_err("android_vh_rwsem_write_wait_start failed! ret=%d\n", ret);
        goto out7;
    }

    /*
     * Calculates the duration in D state.
     */
    ret = register_trace_sched_waking(sched_waking_dstate_end, NULL);
    if (ret != 0) {
        trans_warn("sched_waking_dstate_end failed! ret=%d\n", ret);
        goto out8;
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0)
    /*
     * preempt protection
     */
    ret = register_trace_android_rvh_check_preempt_tick(
                android_rvh_check_preempt_tick, NULL);
    if (ret != 0) {
        trans_warn("android_rvh_check_preempt_tick failed! ret=%d\n", ret);
        goto out9;
    }
#endif

    return ret;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 6, 0)
out9:
#endif
    unregister_trace_sched_waking(sched_waking_dstate_end, NULL);

out8:
    unregister_trace_android_vh_rwsem_write_wait_start(
                    android_vh_rwsem_write_wait_start, NULL);

out7:
    unregister_trace_android_vh_rwsem_read_wait_start(
                    android_vh_rwsem_read_wait_start, NULL);

out6:
    unregister_trace_android_vh_record_rwsem_lock_starttime(
                    android_vh_record_rwsem_lock_starttime, NULL);

out5:
    unregister_trace_android_vh_rtmutex_wait_start(
                    android_vh_rtmutex_wait_start, NULL);

out4:
    unregister_trace_android_vh_record_rtmutex_lock_starttime(
                    android_vh_record_rtmutex_lock_starttime, NULL);

out3:
    unregister_trace_android_vh_mutex_wait_start(
                    android_vh_mutex_wait_start, NULL);

out2:
    unregister_trace_android_vh_record_mutex_lock_starttime(
                    android_vh_record_mutex_lock_starttime, NULL);

out1:
    return ret;
}

void unregister_trans_locking_hook(void)
{
    unregister_trace_sched_waking(sched_waking_dstate_end, NULL);

    unregister_trace_android_vh_rwsem_write_wait_start(
                    android_vh_rwsem_write_wait_start, NULL);

    unregister_trace_android_vh_rwsem_read_wait_start(
                    android_vh_rwsem_read_wait_start, NULL);

    unregister_trace_android_vh_record_rwsem_lock_starttime(
                    android_vh_record_rwsem_lock_starttime, NULL);

    unregister_trace_android_vh_rtmutex_wait_start(
                    android_vh_rtmutex_wait_start, NULL);

    unregister_trace_android_vh_record_rtmutex_lock_starttime(
                    android_vh_record_rtmutex_lock_starttime, NULL);

    unregister_trace_android_vh_mutex_wait_start(
                    android_vh_mutex_wait_start, NULL);

    unregister_trace_android_vh_record_mutex_lock_starttime(
                    android_vh_record_mutex_lock_starttime, NULL);
}

static ssize_t proc_lock_status_write(struct file *file, const char __user *buf,
            size_t count, loff_t *ppos)
{
    char buffer[8] = {0};

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    /*
     * clear lock_status
     */
    memset(&total.stats, 0, sizeof(struct lock_stats));
    memset(&mutex.stats, 0, sizeof(struct lock_stats));
    memset(&rtmutex.stats, 0, sizeof(struct lock_stats));
    memset(&rwsem.stats, 0, sizeof(struct lock_stats));
    lock_trigger = 0;

    return count;
}

static ssize_t proc_lock_status_read(struct file *file, char __user *buf,
            size_t count, loff_t *ppos)
{
    size_t len = 0;
    char buffer[1024];

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "             duration     dcount       <1ms       <5ms      <10ms      <20ms      <50ms     <100ms       long\n");

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "    total: %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu\n",
                NSEC_TO_MSEC(total.stats.duration_ns),
                total.stats.dcounts,
                total.stats.dcounts_1ms,
                total.stats.dcounts_5ms,
                total.stats.dcounts_10ms,
                total.stats.dcounts_20ms,
                total.stats.dcounts_50ms,
                total.stats.dcounts_100ms,
                total.stats.dcounts_long);


    len += snprintf(buffer + len, sizeof(buffer) - len,
        "    mutex: %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu\n",
                NSEC_TO_MSEC(mutex.stats.duration_ns),
                mutex.stats.dcounts,
                mutex.stats.dcounts_1ms,
                mutex.stats.dcounts_5ms,
                mutex.stats.dcounts_10ms,
                mutex.stats.dcounts_20ms,
                mutex.stats.dcounts_50ms,
                mutex.stats.dcounts_100ms,
                mutex.stats.dcounts_long);

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "  rtmutex: %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu\n",
                NSEC_TO_MSEC(rtmutex.stats.duration_ns),
                rtmutex.stats.dcounts,
                rtmutex.stats.dcounts_1ms,
                rtmutex.stats.dcounts_5ms,
                rtmutex.stats.dcounts_10ms,
                rtmutex.stats.dcounts_20ms,
                rtmutex.stats.dcounts_50ms,
                rtmutex.stats.dcounts_100ms,
                rtmutex.stats.dcounts_long);

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "    rwsem: %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu %10llu\n",
                NSEC_TO_MSEC(rwsem.stats.duration_ns),
                rwsem.stats.dcounts,
                rwsem.stats.dcounts_1ms,
                rwsem.stats.dcounts_5ms,
                rwsem.stats.dcounts_10ms,
                rwsem.stats.dcounts_20ms,
                rwsem.stats.dcounts_50ms,
                rwsem.stats.dcounts_100ms,
                rwsem.stats.dcounts_long);

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "  trigger: %10llu\n",
                lock_trigger);

    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

int lock_tuner = (TUNER_CONTINUOUS |
                TUNER_PREEMPT |
                TUNER_PICK_LOCK_TASK);

static ssize_t proc_lock_tuner_write(struct file *file, const char __user *buf,
            size_t count, loff_t *ppos)
{
    int ret, val;
    char buffer[64] = {0};

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    buffer[count] = '\0';
    ret = kstrtoint(strstrip(buffer), 10, &val);
    if (ret) {
        return ret;
    }

    lock_tuner = val;

    return count;
}

static ssize_t proc_lock_tuner_read(struct file *file, char __user *buf,
            size_t count, loff_t *ppos)
{
    size_t len = 0;
    char buffer[64];

    len = snprintf(buffer, sizeof(buffer), "0x%02x\n", lock_tuner);

    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 136)
static const struct proc_ops proc_lock_status_fops = {
    .proc_write = proc_lock_status_write,
    .proc_read = proc_lock_status_read,
};

static const struct proc_ops proc_lock_tuner_fops = {
    .proc_write = proc_lock_tuner_write,
    .proc_read = proc_lock_tuner_read,
};
#else
static const struct file_operations proc_lock_status_fops = {
    .write = proc_lock_status_write,
    .read  = proc_lock_status_read,
};

static const struct file_operations proc_lock_tuner_fops = {
    .write = proc_lock_tuner_write,
    .read  = proc_lock_tuner_read,
};
#endif

struct proc_dir_entry *lock_status_node = NULL;

struct proc_dir_entry * lock_status_proc_init(struct proc_dir_entry *pdir)
{
    memset(&total.stats, 0, sizeof(struct lock_stats));
    memset(&mutex.stats, 0, sizeof(struct lock_stats));
    memset(&rtmutex.stats, 0, sizeof(struct lock_stats));
    memset(&rwsem.stats, 0, sizeof(struct lock_stats));

    lock_status_node = proc_create("lock_status", 0666, pdir, &proc_lock_status_fops);
    if (!lock_status_node) {
        trans_err("create proc/trans_scheduler/lock_status failed\n");
        goto err;
    }

    proc_create("lock_tuner", 0666, pdir, &proc_lock_tuner_fops);

    return lock_status_node;
err:
    return NULL;
}

void lock_status_proc_deinit(void)
{
    if (lock_status_node) {
        proc_remove(lock_status_node);
        lock_status_node = NULL;
    }
}

