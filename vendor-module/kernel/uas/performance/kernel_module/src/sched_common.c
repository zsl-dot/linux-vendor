#include <linux/version.h>
#include <linux/sched/task.h>
#include <linux/sched/topology.h>
#include <linux/errno.h>

#include "sched_common.h"
#include "trans_target.h"
#include "trans_sched_info.h"
#include "trans_balance.h"
#include "trans_locking.h"
#include "trans_loading.h"
#include "trans_workqueue.h"
#include "trans_futex.h"
#include "trans_binder.h"
#include "trans_trace.h"


int g_ux_share_limit[UX_SHARE_TYPE_MAX] = {
    [UX_SHARE_BINDER] = UX_SHARE_BINDER_TIMES_LIMIT,
    [UX_SHARE_MUTEX] = UX_SHARE_MUTEX_TIMES_LIMIT,
    [UX_SHARE_RWSEM] = UX_SHARE_RWSEM_TIMES_LIMIT,
};

struct trans_task_struct *trans_task_struct_create(struct task_struct *task)
{
    int ret = 0;
    struct trans_task_struct *trans_ts = NULL;

    if (!task) {
        return NULL;
    }

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        WARN(1, "tts error: task=%s, pid=%d\n", task->comm, task->pid);

        /*
         * WARNING
         * The logic below will be removed in the near future.
         */
        ret = trans_pcb_alloc(task);
        if (ret) {
            return NULL;
        }

        trans_ts = get_trans_task_struct(task);
    }

    return trans_ts;
}

int trans_task_struct_init(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = NULL;

    if (!task) {
        return -EFAULT;
    }

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return -EFAULT;
    }

    trans_ts->trans_magic_head = TRANS_MAGIC_HEAD;
    trans_ts->task = task;
    if (is_ux_node_uninit(&trans_ts->ux_node)) {
        INIT_LIST_HEAD(&trans_ts->ux_node);
    }
    trans_ts->total_run = 0;
    trans_ts->sum_exec_runtime = 0;
    trans_ts->ux_tags = 0;
    trans_ts->share = 0;
    trans_ts->ux_pending = 0;
    trans_ts->ux_share_period = 0;

    dequeue_locktask_on_fork(trans_ts);
    memset(&trans_ts->lockinfo, 0, sizeof(struct task_lock_info));

    init_task_load(&trans_ts->ld);

    trans_ts->trans_magic_tail = TRANS_MAGIC_TAIL;

    return 0;
}

static bool insert_ux_into_list(struct trans_task_struct *trans_task, struct trans_rq *trq)
{
    struct list_head *pos = NULL;
    struct trans_task_struct *trans_ts = NULL;

    list_for_each(pos, &trq->ux_list) {
        trans_ts = container_of(pos, struct trans_task_struct, ux_node);
        if (prio_higher(trans_task->ux_tags, trans_ts->ux_tags)) {
            break;
        }
    }

    list_add(&trans_task->ux_node, pos->prev);
    return true;
}

static void update_ux_pending(struct task_struct *task, u32 *ux_tags, bool *ux_ignore, char *thread_name)
{
    u32 ux_pending = 0;
    u32 ux_tags_set = *ux_tags;
    u32 ux_tags_curr = get_ux_tags(task);

    if ((!ux_tags_curr) || get_ux_share_state(ux_tags_curr))
        return;

    ux_pending = get_ux_pending(task);
    if ((!ux_tags_set) && (!ux_pending))
        return;

    // set
    if (ux_tags_set && (!ux_pending)) {
        if (prio_higher(ignore_ux_dprio(ux_tags_set), ignore_ux_dprio(ux_tags_curr))) {
            *ux_tags = ux_tags_set;
            set_ux_pending(task, ux_tags_curr);
            goto out;
        }

        set_ux_pending(task, ux_tags_set);
        *ux_ignore = true;
        goto out;
    }

    // cancel
    if ((!ux_tags_set) && ux_pending) {
        set_ux_pending(task, UX_TASK_TAGS_NONE);

        if (ux_tags_curr & UX_FLAG_EXACT) {
            *ux_tags = ux_pending;
            goto out;
        }

        *ux_ignore = true;
        goto out;
    }

    // do nothing
    if (ux_tags_set && ux_pending)
        *ux_ignore = true;

out:
    trans_debug_deferred(DEBUG_MODIFY_UX, "%s/%d/%d, ID=%s, ux_tags=%#x:%d, set/curr/pending=%#x:%#x:%#x\n", \
        task->comm, task->pid, task->tgid, (thread_name ? thread_name : ""), *ux_tags, !(*ux_ignore), \
        ux_tags_set, ux_tags_curr, get_ux_pending(task));

    return;
}

int set_ux_tags_to_task(struct task_struct *task, u32 ux_tags, char *thread_name, bool record_ux)
{
    int ret = 0;
    int record_cmd = 0;
    struct rq *rq = NULL;
    bool ux_ignore = false;
    struct trans_rq *trq = NULL;
    struct rq_flags flags = {0};
    struct trans_task_struct *trans_ts = NULL;

    if (unlikely(!task))
        return -EFAULT;

    if (!trans_task_struct_create(task))
        return -ENOMEM;

    if (!futex_set_ux_tags(task, ux_tags))
        return 0;

    if (!abinder_set_ux_tags(task, ux_tags))
        return 0;

    ux_tags = check_unset_ux_tags(ux_tags) ? UX_TASK_TAGS_NONE : ux_tags;
    if ((ux_tags == UX_TASK_TAGS_NONE) && (get_ux_tags(task) == ux_tags))
        return 0;

    rq = task_rq_lock(task, &flags);

    update_ux_pending(task, &ux_tags, &ux_ignore, thread_name);
    if (ux_ignore)
        goto out;

    if (!is_trans_task_struct_init(task)) {
        ret = trans_task_struct_init(task);
        if (ret)
            goto out;

        record_cmd = RECORD_UX;
        trans_debug_deferred(DEBUG_MODIFY_UX, "init ux: %s/%d/%d, prio=%d, ux_tags=%#x, scene=%#x\n", \
            task->comm, task->pid, task->tgid, task->prio, ux_tags, g_sched_scene);
    }

    if ((!record_cmd) && ux_tags) {
        record_cmd = RE_RECORD_UX;
        trans_debug_deferred(DEBUG_MODIFY_UX, "reset ux: %s/%d/%d, prio=%d, ux_tags:%#x => %#x\n", \
            task->comm, task->pid, task->tgid, task->prio, get_ux_tags(task), init_ux_dprio(ux_tags));
    }

    if (ux_tags == UX_TASK_TAGS_NONE) {
        clear_trans_magic(task);
        record_cmd = UNRECORD_UX;
        trans_debug_deferred(DEBUG_MODIFY_UX, "clear ux: %s/%d/%d, prio=%d\n", task->comm, task->pid, task->tgid, task->prio);
    }

    if (ux_tags && (!get_ux_dprio(ux_tags)))
        ux_tags = init_ux_dprio(ux_tags);

    trans_ts = get_trans_task_struct(task);
    trans_ts->ux_tags = ux_tags;
    trans_ts->share = 0;
    trans_ts->ux_share_period = 0;

    if (check_ux_tags_ok(ux_tags) && check_task_on_rq(task, rq)) {
        if (is_ux_node_free(&trans_ts->ux_node) && (!is_ux_node_uninit(&trans_ts->ux_node))) {
            if (!(check_ux_kworker_type(ux_tags) && (!check_ux_kworker_enable()))) {
                trq = get_trans_rq(rq);
                if (insert_ux_into_list(trans_ts, trq))
                    get_task_struct(task);

                if (!trans_ts->total_run)
                    trans_ts->sum_exec_runtime = task->se.sum_exec_runtime;
            }
        }
    }

    ret = 0;
    trans_sched_systrace_c(task->tgid, ux_tags_align(ux_tags), "uas_boost-%d", task->pid);

out:
    task_rq_unlock(rq, task, &flags);

    if (record_ux)
        update_ux_account(task, thread_name, record_cmd);

    return ret;
}

int set_ux_tags_by_pid(struct task_struct *target_task, pid_t pid, u32 ux_tags, char *thread_name)
{
    int ret = 0;
    struct task_struct *task = NULL;

    if (unlikely(pid <= 0 || pid > PID_MAX_DEFAULT)) {
        trans_err("%d is not available pid!\n", pid);
        return -EINVAL;
    }

    if (unlikely(!is_available_ux_type(ux_tags)))
        return -EINVAL;

    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        ret = -ESRCH;
        goto unlock_rcu;
    }
    get_task_struct(task);

    if (target_task && (target_task != task)) {
        ret = -EFAULT;
        goto put_task;
    }

    ret = set_ux_tags_to_task(task, ux_tags, thread_name, true);

put_task:
    put_task_struct(task);

unlock_rcu:
    rcu_read_unlock();
    return ret;
}

int convert_ux_prio(pid_t pid, u32 shift)
{
    int ret = 0;
    u32 ux_tags = 0;
    struct rq *rq = NULL;
    struct rq_flags flags = {0};
    struct task_struct *task = NULL;

    if (unlikely(pid <= 0 || pid > PID_MAX_DEFAULT)) {
        trans_err("%d is not available pid!\n", pid);
        return -EINVAL;
    }

    if (unlikely(!is_available_shift(shift))) {
        trans_err("%d is not available shift!\n", shift);
        return -EINVAL;
    }

    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        ret = -ESRCH;
        goto unlock_rcu;
    }
    get_task_struct(task);

    ux_tags = get_ux_tags(task);
    if (unlikely(!(ux_tags & UX_TASK_PRIO_MASK))) {
        ret = -EPERM;
        goto put_task;
    }

    rq = task_rq_lock(task, &flags);

    ux_tags = get_ux_tags(task);
    if (unlikely(!check_ux_tags_ok(ux_tags))) {
        ret = -EPERM;
        goto out;
    }

    ux_tags = adjust_ux_prio(ux_tags, shift);
    set_ux_tags(task, ux_tags);
    ret = 0;

    trans_sched_systrace_c(task->tgid, ux_tags_align(ux_tags), "uas_boost-%d", pid);

out:
    task_rq_unlock(rq, task, &flags);

put_task:
    put_task_struct(task);

unlock_rcu:
    rcu_read_unlock();
    return ret;
}

static u64 get_ux_run_slice(unsigned int ux_tags)
{
    ux_tags = ignore_ux_share_state(ux_tags);
    if (!ux_tags) {
        return 0;
    }

    switch ((ux_tags & UX_TASK_SLICE_MASK)) {
        case UX_TASK_SLICE0:
            return RUN_SLICE_1; // 4ms
        case UX_TASK_SLICE1:
            return RUN_SLICE_2; // 8ms
        case UX_TASK_SLICE2:
            return RUN_SLICE_4; // 16ms
        case UX_TASK_SLICE3:
            return RUN_SLICE_8; // 32ms
        case UX_TASK_SLICE4:
            return RUN_SLICE_16; // 64ms
        case UX_TASK_SLICE5:
            return RUN_SLICE_16 + RUN_SLICE_4; // 64 + 16ms
        case UX_TASK_SLICE6:
            return RUN_SLICE_16 + RUN_SLICE_8; // 64 + 32ms
        case UX_TASK_SLICE7:
            return RUN_SLICE_32; // 128ms
        default:
            return 0;
    }
}

u64 ux_task_run_limit(struct task_struct *task)
{
    u64 run_limit = 0;
    unsigned int ux_tags = get_ux_tags(task);

    run_limit = get_ux_run_slice(ux_tags);

    if ((!get_ux_share_state(ux_tags)) && (!check_ux_kworker_type(ux_tags))) {
        if (check_sched_scene(SS_LAUNCH)) {
            if ((task->tgid == task->pid) && run_limit) {
                run_limit += RUN_SLICE_32;
            }
        }
        else if (check_sched_scene(SS_ANIMATION)) {
            run_limit += run_limit;
        }
    }

    return run_limit;
}

static bool __is_ux_task(struct task_struct *task,
            enum ux_task_state *state, bool check_timeout)
{
    u32 ux_tags = 0;
    struct trans_task_struct *trans_ts = NULL;
    enum ux_task_state ux_state = UX_TASK_NO_TAG;

    if (!check_uas_feature(FEATURE_DEFAULT)) {
        ux_state = UX_TASK_NO_FEATURE;
        goto out;
    }

    if (!is_fair_task(task)) {
        ux_state = UX_TASK_NOT_FAIR;
        goto out;
    }

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        goto out;
    }

    ux_tags = trans_ts->ux_tags;
    if (check_ux_tags_ok(ux_tags)) {
        if (check_ux_kworker_type(ux_tags)) {
            if (!check_ux_kworker_enable()) {
                ux_state = UX_KWORKER_DISABLE;
                goto out;
            }
        }

        if (unlikely(task->flags & PF_EXITING)) {
            ux_state = UX_TASK_EXITING;
            goto out;
        }

        if (check_timeout &&
            unlikely(trans_ts->total_run && (trans_ts->total_run > ux_task_run_limit(task)))) {
            ux_state = UX_TASK_TIMEOUT;
            goto out;
        }

        ux_state = UX_TASK_REAL;
    }

out:
    if (state) {
        *state = ux_state;
    }

    return (ux_state == UX_TASK_REAL) ? true: false;
}

/*
 * is_ux_task - Check whether a task is a valid UX task.
 * @task:  the task to be checked
 * @state: optional pointer to receive the detailed UX task state
 *
 * This function determines whether the given task should be treated as
 * an effective UX task. In addition to checking the UX identity and basic
 * eligibility, it also considers runtime-related constraints such as
 * timeout. A UX task that exceeds its runtime limit will be regarded as
 * invalid and may be downgraded accordingly.
 *
 * Return: true if the task is a valid UX task; false otherwise.
 */
bool is_ux_task(struct task_struct *task, enum ux_task_state *state)
{
    return __is_ux_task(task, state, true);
}

/*
 * is_ux_task_base - Check whether a task has UX identity attributes.
 * @task:  the task to be checked
 * @state: optional pointer to receive the detailed UX task state
 *
 * This function only checks whether the given task has UX-related identity
 * attributes and basic eligibility, without considering runtime or timeout
 * constraints. It is mainly intended for scheduling decisions such as CPU
 * selection or load balancing, where the UX identity itself is sufficient
 * and runtime degradation is not relevant.
 *
 * Return: true if the task has UX identity attributes; false otherwise.
 */
bool is_ux_task_base(struct task_struct *task, enum ux_task_state *state)
{
    return __is_ux_task(task, state, false);
}

bool tran_is_ux_task(struct task_struct *task)
{
    if (!task) {
        return false;
    }

    return is_ux_task(task, NULL);
}
EXPORT_SYMBOL_GPL(tran_is_ux_task);

static void enqueue_ux_task(struct rq *rq, struct task_struct *task, int flags)
{
    bool is_exist = false;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct trans_rq *trq = NULL;
    struct trans_task_struct *trans_ts = NULL;

    if (unlikely(!rq || !task)) {
        return;
    }

    /* Only enqueue the delayed task which being woken up. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    if (task->se.sched_delayed) {
        if ((!(flags & ENQUEUE_DELAYED)) || unlikely(!(READ_ONCE(task->on_rq))))
            return;
    }
#endif

    if (!is_ux_task(task, NULL)) {
        return;
    }

    trans_ts = get_trans_task_struct(task);
    if (unlikely(IS_ERR_OR_NULL(trans_ts))) {
        return;
    }

    if (!set_ux_kworker_enable(task->pid, trans_ts->ux_tags, false))
        return;

    if (unlikely(!is_ux_node_free(&trans_ts->ux_node))) {
        return;
    }

    if (unlikely(is_ux_node_uninit(&trans_ts->ux_node))) {
        INIT_LIST_HEAD(&trans_ts->ux_node);
    }

    trq = get_trans_rq(rq);

    list_for_each_safe(pos, next, &trq->ux_list) {
        if (pos == get_ux_node(task)) {
            is_exist = true;
            BUG_ON(1);
            break;
        }
    }

    if (!is_exist) {
        if (insert_ux_into_list(trans_ts, trq))
            get_task_struct(task);
    }

    if (!trans_ts->total_run) {
        trans_ts->sum_exec_runtime = task->se.sum_exec_runtime;
    }
}

void android_rvh_enqueue_task_hook(void *data, struct rq *rq, struct task_struct *task, int flags)
{
    trans_task_loading_enqueue(rq, task, flags);
    update_stats_enqueue_rt(rq, task, flags);
    enqueue_ux_task(rq, task, flags);
    enqueue_locking_task(rq, task);
    xmu_task_enqueue(rq, task, flags);
}

static void dequeue_ux_task(struct rq *rq, struct task_struct *task, int flags)
{
    bool oneshot = false;
    u64 ux_share_period = 0;
    u32 share_type = UX_SHARE_FAULT;
    struct trans_task_struct *trans_ts = NULL;

    if (unlikely(!rq || !task)) {
        return;
    }

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    if (!is_ux_node_free(&trans_ts->ux_node)) {
        list_del_init(&trans_ts->ux_node);

        if (task->in_iowait)
            set_ux_kworker_enable(task->pid, trans_ts->ux_tags, true);

        if (get_ux_share_state(trans_ts->ux_tags)) {
            oneshot = check_ux_flag(trans_ts->ux_tags, UX_FLAG_ONESHOT);
            ux_share_period = task->se.sum_exec_runtime - trans_ts->ux_share_period;

            if ((oneshot && (flags & DEQUEUE_SLEEP)) || (ux_share_period > get_ux_run_slice(trans_ts->ux_tags))) {
                share_type = get_ux_share_type(task);
                trans_debug_deferred(DEBUG_SHARE, "unshare[%d]: %s/%d/%d, ux_tags=%#x:%#x, period=%llu, slice=%llu, oneshot=%d\n", share_type, \
                    task->comm, task->pid, task->tgid, trans_ts->ux_tags, trans_ts->share, ux_share_period, get_ux_run_slice(trans_ts->ux_tags), oneshot);

                trans_ts->ux_tags = 0;
                trans_ts->share = 0;
                trans_ts->ux_share_period = 0;
                trans_ts->trans_magic_head = 0;
                trans_ts->trans_magic_tail = 0;
                trans_sched_systrace_c(task->tgid, 0, "uas_share[%d]-%d", share_type, task->pid);
            }
        }

        put_task_struct(task);
    }

    if (task_state(task) != TASK_RUNNING) {
        trans_ts->total_run = 0;
    }
}

void android_rvh_dequeue_task_hook(void *data, struct rq *rq, struct task_struct *task, int flags)
{
    trans_task_loading_dequeue(rq, task, flags);
    update_stats_dequeue_rt(rq, task, flags);
    dequeue_ux_task(rq, task, flags);
    dequeue_locking_task(rq, task);
    xmu_task_dequeue(rq, task, flags);
}

static void init_trans_rq_one(struct rq *rq, int cpu)
{
    struct trans_rq *trq = NULL;

    if (!rq) {
        return;
    }

    trq = get_trans_rq(rq);
    INIT_LIST_HEAD(&trq->ux_list);

    /*
     * allocate memory for lock_queue and initialize it.
     */
    trq->lock_queue = kmalloc(sizeof(struct lock_queue), GFP_KERNEL);
    INIT_LIST_HEAD(&trq->lock_queue->lock_list);
    spin_lock_init(&trq->lock_queue->list_lock);
    trq->lock_queue->nr_tasks = 0;
    trq->lock_queue->enable = true;

    trq->lb.irqwb_pid = INVALID_PID;
    trq->lb.irqwb_push_cpu = INVALID_CPU;
}

void trans_rqs_init(void)
{
    int cpu;
    struct rq *tmp_rq = NULL;

    for_each_possible_cpu(cpu) {
        tmp_rq = cpu_rq(cpu);
        init_trans_rq_one(tmp_rq, cpu);
    }
}

static void ux_sched_domains_init(void)
{
    int cpu_idx;

    g_ux_sched_domains.group_nr = 0;
    for (cpu_idx = 0; cpu_idx < TRANS_NR_CPUS; cpu_idx++) {
        cpumask_clear(&g_ux_sched_domains.sched_group[cpu_idx].cpus);
        g_ux_sched_domains.sched_group[cpu_idx].capacity = ULONG_MAX;
    }
}

void build_ux_sched_domains(void)
{
    unsigned int cpu;
    unsigned long capacity = 0;
    unsigned long tmp_capacity;
    unsigned int tmp_group_nr = 0;

    ux_sched_domains_init();

    for_each_possible_cpu(cpu) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 136)
        tmp_capacity = arch_scale_cpu_capacity(cpu);
#else
        tmp_capacity = arch_scale_cpu_capacity(NULL, cpu);
#endif
        if (!tmp_capacity) {
            continue;
        }

        if (capacity == tmp_capacity) {
            cpumask_set_cpu(cpu, &g_ux_sched_domains.sched_group[tmp_group_nr - 1].cpus);
            set_cpu_cluster_id(cpu, tmp_group_nr - 1);
            continue;
        }

        g_ux_sched_domains.sched_group[tmp_group_nr].capacity = tmp_capacity;
        cpumask_set_cpu(cpu, &g_ux_sched_domains.sched_group[tmp_group_nr].cpus);
        set_cpu_cluster_id(cpu, tmp_group_nr);
        capacity = tmp_capacity;
        tmp_group_nr++;
    }

    g_ux_sched_domains.group_nr = tmp_group_nr;

    build_trans_cpu_array(g_ux_sched_domains.group_nr);
}

void dump_ux_sched_domains(void)
{
    int idx = 0;

    for (idx = 0; idx < g_ux_sched_domains.group_nr; idx++) {
        trans_info("ux sched domains: nr=%d, cpus=%*pbl, capacity=%lu\n",g_ux_sched_domains.group_nr, \
            cpumask_pr_args(&g_ux_sched_domains.sched_group[idx].cpus), g_ux_sched_domains.sched_group[idx].capacity);
    }
}

void android_vh_build_sched_domains_hook(void *data, bool has_asym)
{
    if (!check_uas_feature(FEATURE_DEFAULT)) {
        return;
    }

    build_ux_sched_domains();
}

void update_ux_runtime(struct rq *rq, struct task_struct *task)
{
    s64 delta;
    u64 run_limit = 0;
    struct trans_rq *trq = NULL;
    struct trans_task_struct *trans_ts = NULL;

    if (unlikely(!rq || !task)) {
        return;
    }

    trq = get_trans_rq(rq);
    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    lockdep_assert_rq_held(rq);
#else
    lockdep_assert_held(&rq->lock);
#endif /* KERNEL_VERSION(5, 15, 0) */

    if (!(rq->clock_update_flags & RQCF_UPDATED)) {
        update_rq_clock(rq);
    }

    delta = task->se.sum_exec_runtime - trans_ts->sum_exec_runtime;
    if (delta < 0) {
        delta = 0;
    }
    else {
        delta += rq_clock_task(rq) - task->se.exec_start;
    }

    if (delta < UX_RUN_SLICE) {
        return;
    }

    trans_ts->sum_exec_runtime += delta;
    trans_ts->total_run += delta;

    run_limit = ux_task_run_limit(task);
    if (trans_ts->total_run > run_limit) {
        trans_debug_deferred(DEBUG_KLOG, "ux timeout: %s/%d/%d, total_run=%llu, run_limit=%llu, ux_tags=%#x:%#x, scene=%#x\n", task->comm, \
            task->pid, task->tgid, trans_ts->total_run, run_limit, trans_ts->ux_tags, trans_ts->share, g_sched_scene);
        list_del_init(&trans_ts->ux_node);
        put_task_struct(task);
        return;
    }

    list_del_init(&trans_ts->ux_node);
    insert_ux_into_list(trans_ts, trq);
}

void android_vh_scheduler_tick_hook(void *data, struct rq *rq)
{
    struct list_head *ux_node = NULL;
    struct task_struct *curr_task = NULL;

#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
     trans_tick_balance(data, rq);
#endif

    if (!rq) {
        return;
    }

    curr_task = rq->curr;
    if (!is_trans_task_struct_init(curr_task)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    raw_spin_rq_lock(rq);
#else
    raw_spin_lock(&rq->lock);
#endif /* KERNEL_VERSION(5, 15, 0) */
    ux_node = get_ux_node(curr_task);
    if (!is_ux_node_free(ux_node)) {
        update_ux_runtime(rq, curr_task);
    }

    update_task_loading(curr_task, CPATH_TICK);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    raw_spin_rq_unlock(rq);
#else
    raw_spin_unlock(&rq->lock);
#endif /* KERNEL_VERSION(5, 15, 0) */
}

void android_rvh_sched_fork_hook(void *data, struct task_struct *task)
{
    return;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
void android_rvh_dequeue_entity_delayed_hook(void *unused,
            struct cfs_rq *cfs_rq, struct sched_entity *se, bool *delay)
{
    struct task_struct *tsk = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT))
        return;

    if (!entity_is_task(se))
        return;

    tsk = task_of(se);
    if (tran_is_ux_task(tsk)) {
        *delay = false;
    }

    return;
}
#endif

void update_ux_share_hold(struct task_struct *task, enum ux_share_type share_type)
{
    struct rq *rq = NULL;
    u32 ux_share_tags = 0;
    u32 ux_share_data = 0;
    struct rq_flags flags = {0};
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely((!task) || (share_type >= UX_SHARE_TYPE_MAX))) {
        return;
    }

    if (!check_ux_share_type(get_ux_tags(task), share_type)) {
        return;
    }

    rq = task_rq_lock(task, &flags);

    if (!is_ux_task(task, NULL)) {
        goto unlock;
    }

    ux_share_tags = get_ux_tags(task);
    if (!check_ux_share_type(ux_share_tags, share_type)) {
        goto unlock;
    }

    if (check_ux_flag(ux_share_tags, UX_FLAG_ONESHOT)) {
        ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ONESHOT);
        trans_debug_deferred(DEBUG_SHARE, "clear oneshot[%d]: %s/%d/%d, ux_tags=%#x:%#x\n", share_type, \
            task->comm, task->pid, task->tgid, ux_share_tags, ux_share_data);
        set_ux_tags(task, ux_share_tags);
    }

    ux_share_data = get_ux_share_val(task);
    inc_ux_share_hold(&ux_share_data, share_type);
    set_ux_share_val(task, ux_share_data);
    trans_sched_systrace_c(task->tgid, share_filed->share_hold, "uas_share[%d]-%d", share_type, task->pid);

unlock:
    task_rq_unlock(rq, task, &flags);

    trans_debug(DEBUG_SHARE, "hold share[%d]: %s/%d/%d, ux_tags=%#x:%#x\n", share_type, \
            task->comm, task->pid, task->tgid, ux_share_tags, ux_share_data);
}

void set_ux_share(struct task_struct *task, enum ux_share_type share_type, u32 share, u32 ux_tags)
{
    int ret = 0;
    struct rq *rq = NULL;
    struct rq_flags flags = {0};
    u32 ux_share_data = share;
    u32 ux_share_tags = ux_tags;
    struct trans_rq *trq = NULL;
    struct trans_task_struct *trans_ts = NULL;

    if (unlikely((!task) || (share_type >= UX_SHARE_TYPE_MAX)))
        return;

    /*
     * Ignore tasks related to the specified target
     */
    if (check_sf_target(task) || check_overlayengine_target(task))
        return;

    if (check_ux_flag(ux_share_tags, UX_FLAG_ONESHOT)) {
        ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ONESHOT);
        trans_debug(DEBUG_SHARE, "clear oneshot[%d]: %s/%d/%d, ux_tags=%#x:%#x\n", share_type, \
            task->comm, task->pid, task->tgid, ux_share_tags, ux_share_data);
    }

    if (!trans_task_struct_create(task))
        return;

    rq = task_rq_lock(task, &flags);

    if (!is_rt_fair_task(task))
        goto unlock;

    if (!is_trans_task_struct_init(task)) {
        ret = trans_task_struct_init(task);
        if (ret)
            goto unlock;
    }

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts))
        goto unlock;

    inc_ux_share(&ux_share_tags, &ux_share_data, share_type);
    trans_ts->ux_tags = ux_share_tags;
    trans_ts->share = ux_share_data;
    trans_ts->ux_share_period = task->se.sum_exec_runtime;
    trans_sched_systrace_c(task->tgid, UX_SHARE_HOLD_INIT, "uas_share[%d]-%d", share_type, task->pid);

    if (check_task_on_rq(task, rq)) {
        if (is_ux_node_free(&trans_ts->ux_node) && (!is_ux_node_uninit(&trans_ts->ux_node))) {
            trq = get_trans_rq(rq);
            if (insert_ux_into_list(trans_ts, trq))
                get_task_struct(task);

            if (!trans_ts->total_run)
                trans_ts->sum_exec_runtime = task->se.sum_exec_runtime;
        }
    }

unlock:
    task_rq_unlock(rq, task, &flags);

    trans_debug(DEBUG_SHARE, "ux share[%d]: cpu=%d, %s/%d/%d, ux_tags=%#x:%#x\n", share_type, \
            (trq ? rq->cpu : -1), task->comm, task->pid, task->tgid, (trans_ts ? trans_ts->ux_tags : -1), (trans_ts ? trans_ts->share : -1));
}

void unset_ux_share(struct task_struct *task, enum ux_share_type share_type)
{
    struct rq *rq = NULL;
    bool oneshot = false;
    u32 ux_share_data = 0;
    u32 ux_share_tags = 0;
    struct rq_flags flags = {0};
    struct trans_task_struct *trans_ts = NULL;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely(!task))
        return;

    if (!is_trans_task_struct_init(task))
        return;

    if (check_ux_flag(get_ux_tags(task), UX_FLAG_ONESHOT))
        return;

    rq = task_rq_lock(task, &flags);

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts))
        goto unlock;

    ux_share_tags = trans_ts->ux_tags;
    ux_share_data = trans_ts->share;
    if ((!ux_share_tags) && (!ux_share_data))
        goto unlock;

    dec_ux_share(&ux_share_tags, &ux_share_data, share_type);
    if (check_ux_share_type(ux_share_tags, share_type)) {
        trans_ts->ux_tags = ux_share_tags;
        trans_ts->share = ux_share_data;
        trans_sched_systrace_c(task->tgid, share_filed->share_hold, "uas_share[%d]-%d", share_type, task->pid);
        goto unlock;
    }

    if (!is_ux_node_free(&trans_ts->ux_node)) {
        if (share_type == UX_SHARE_BINDER) {
            trans_ts->ux_tags = set_ux_flag(trans_ts->ux_tags, UX_FLAG_ONESHOT);
            oneshot = true;
            goto unlock;
        }

        list_del_init(&trans_ts->ux_node);
        put_task_struct(task);
    }

    trans_ts->ux_tags = 0;
    trans_ts->share = 0;
    trans_ts->ux_share_period = 0;
    trans_ts->trans_magic_head = 0;
    trans_ts->trans_magic_tail = 0;
    trans_sched_systrace_c(task->tgid, 0, "uas_share[%d]-%d", share_type, task->pid);

unlock:
    task_rq_unlock(rq, task, &flags);

    trans_debug(DEBUG_SHARE, "unshare[%d]: %s/%d/%d, ux_tags=%#x:%#x, oneshot=%d\n", share_type, task->comm, task->pid, task->tgid, \
            (trans_ts ? trans_ts->ux_tags : ux_share_tags), (trans_ts ? trans_ts->share : ux_share_data), oneshot);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
void sched_switch_prev_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state)
#else
void sched_switch_prev_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next)
#endif
{
    cpumask_t mask_prev = {0};
    cpumask_t mask_next = {0};

    if (unlikely(!prev) || unlikely(!next)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    cpumask_copy(&mask_prev, &prev->cpus_mask);
    cpumask_copy(&mask_next, &next->cpus_mask);
#else
    cpumask_copy(&mask_prev, &prev->cpus_allowed);
    cpumask_copy(&mask_next, &next->cpus_allowed);
#endif

    if (is_rt_task(prev))
        prev->prio = (prev->prio | (((*((unsigned int *) &mask_prev)) & 0xFF) << 8));
    else if (is_ux_task(prev, NULL))
        prev->prio = (prev->prio | (((*((unsigned int *) &mask_prev)) & 0xFF) << 8) | ((get_ux_tags(prev) & 0xFFFF)<< 16));

    if (is_rt_task(next))
        next->prio = (next->prio | (((*((unsigned int *) &mask_next)) & 0xFF) << 8));
    else if (is_ux_task(next, NULL))
        next->prio = (next->prio | (((*((unsigned int *) &mask_next)) & 0xFF) << 8) | ((get_ux_tags(next) & 0xFFFF)<< 16));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
void sched_switch_next_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state)
#else
void sched_switch_next_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next)
#endif
{
    int prev_prio = 0;
    int next_prio = 0;

    if (unlikely(!prev) || unlikely(!next)) {
        return;
    }

    prev_prio = prev->prio;
    next_prio = next->prio;

    if ((prev_prio & (~0xFF)) && (prev_prio > 0)) {
        prev->prio = (prev_prio & 0xFF);
    }

    if ((next_prio & (~0xFF)) && (next_prio > 0)) {
        next->prio = (next_prio & 0xFF);
    }
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
void android_rvh_schedule_hook(void *unused,
            unsigned int sched_mode, struct task_struct *prev, struct task_struct *next, struct rq *rq)
#else
void android_rvh_schedule_hook(void *unused,
            struct task_struct *prev, struct task_struct *next, struct rq *rq)
#endif
{
    if (likely(prev != next)) {
        wakeup_irqwb(prev, next, rq);
        ux_state_systrace_c(cpu_of(rq), next);
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
void trans_sched_switch(void *unused,
            bool preempt, struct task_struct *prev, struct task_struct *next,
            unsigned int prev_state)
#else
void trans_sched_switch(void *unused,
            bool preempt, struct task_struct *prev, struct task_struct *next)
#endif
{
    update_task_loading(prev, CPATH_SWITCH);
    sched_switch_reset_lock_info(prev, next);
}

noinline int tracing_mark_write(const char *buf)
{
    trace_printk(buf);
    return 0;
}

void trans_sched_systrace_c(pid_t pid, u64 val, const char *fmt, ...)
{
    int len = 0;
    va_list args = {0};
    char log[TRACE_BUFFER_LEN] = {0};
    char buf[TRACE_BUFFER_LEN] = {0};

    if (!uas_trace_enabled())
        return;

    if (!fmt) {
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(log, sizeof(log), fmt, args);
    va_end(args);

    if (unlikely(len <= 0)) {
        return;
    }
    else if (unlikely(len >= TRACE_BUFFER_LEN)) {
        log[TRACE_BUFFER_LEN - 1] = '\0';
    }

    len = snprintf(buf, sizeof(buf), "C|%d|%s|%lld\n", pid, log, val);
    if (unlikely(len <= 0)) {
        return;
    }
    else if (unlikely(len >= TRACE_BUFFER_LEN)) {
        buf[TRACE_BUFFER_LEN - 1] = '\0';
    }

    tracing_mark_write(buf);
}

void trans_sched_systrace_b(pid_t tgid, const char *fmt, ...)
{
    int len = 0;
    va_list args = {0};
    char log[TRACE_BUFFER_LEN] = {0};
    char buf[TRACE_BUFFER_LEN] = {0};

    if (!uas_trace_enabled())
        return;

    if (!fmt) {
        return;
    }

    va_start(args, fmt);
    len = vsnprintf(log, sizeof(log), fmt, args);
    va_end(args);

    if (unlikely(len <= 0)) {
        return;
    }
    else if (unlikely(len >= TRACE_BUFFER_LEN)) {
        log[TRACE_BUFFER_LEN - 1] = '\0';
    }

    len = snprintf(buf, sizeof(buf), "B|%d|%s\n", tgid, log);

    if (unlikely(len <= 0)) {
        return;
    }
    else if (unlikely(len >= TRACE_BUFFER_LEN)) {
        buf[TRACE_BUFFER_LEN - 1] = '\0';
    }

    tracing_mark_write(buf);
}

void trans_sched_systrace_e(void)
{
    int len = 0;
    char buf[TRACE_BUFFER_LEN] = {0};

    if (!uas_trace_enabled())
        return;

    len = snprintf(buf, sizeof(buf), "E\n");

    if (unlikely(len <= 0)) {
        return;
    }
    else if (unlikely(len >= TRACE_BUFFER_LEN)) {
        buf[TRACE_BUFFER_LEN - 1] = '\0';
    }

    tracing_mark_write(buf);
}

static DEFINE_PER_CPU(int, prev_ux_state);
#define WARN_NO_TTS  123456
void ux_state_systrace_c(unsigned int cpu, struct task_struct *p)
{
    int ux_state = 0;

    if (!uas_trace_enabled())
        return;

    /*
     * If the trans_task_struct structure has not been allocated
     * for this task, error code 123456 will be returned.
     */
    if (IS_ERR_OR_NULL(get_trans_task_struct(p)))
        ux_state = WARN_NO_TTS;
    else
        ux_state = is_ux_task(p, NULL);// get_ux_tags(p);   TODO

    /*
     * update load status
     */
    update_cpu_load(cpu_rq(cpu));
    trace_show_nr_running(cpu, cpu_rq(cpu)->nr_running);
    trace_show_pipeline(cpu, trans_is_pipeline_task(p));
    trace_show_uxtags(cpu, get_ux_tags(p));

    /*
     * output only when the ux_state changes.
     */
    if (per_cpu(prev_ux_state, cpu) != ux_state) {
        trace_sched_switch_ux(cpu, ux_state);
        per_cpu(prev_ux_state, cpu) = ux_state;
    }
}


