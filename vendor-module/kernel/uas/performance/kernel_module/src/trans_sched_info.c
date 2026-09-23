#include <linux/types.h>
#include <uapi/asm-generic/errno-base.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "sched_common.h"
#include "trans_sched_info.h"
#include "trans_target.h"

#define DINFO_DELAY_9MS    (9)
#define DINFO_STACK_DEPTH  (32)

#define UX_HP_MAX          (3)
#define BACKGROUND_CSS_ID  (3)

struct ux_account {
    int ux_nr;
    struct list_head list;
    spinlock_t lock;
    struct timer_list timer;
    struct work_struct work;
};

struct ux_recorder {
    pid_t pid;
    int ux_hp;
    struct list_head ux_node;
    struct task_struct *task;
    char thread_name[THREAD_NAME_LEN];
};

static struct ux_account g_ux_account_book = {0};

static void ux_account_work_func(struct work_struct *work);
static void ux_account_timer_func(struct timer_list *timer);

void ux_account_init(void)
{
    spin_lock_init (&g_ux_account_book.lock);
    INIT_LIST_HEAD(&g_ux_account_book.list);
    g_ux_account_book.ux_nr = 0;

    INIT_WORK(&g_ux_account_book.work, ux_account_work_func);
    timer_setup(&g_ux_account_book.timer, ux_account_timer_func, 0);
}

void ux_account_deinit(void)
{
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct ux_recorder *recorder = NULL;

    spin_lock(&g_ux_account_book.lock);
    list_for_each_safe(pos, next, &g_ux_account_book.list) {
        recorder = container_of(pos, struct ux_recorder, ux_node);
        recorder->pid = -1;
        recorder->ux_hp = 0;
        recorder->task = NULL;
        memset(recorder->thread_name, 0, sizeof(recorder->thread_name));
        list_del_init(&recorder->ux_node);
        kfree(recorder);
        recorder = NULL;
    }
    g_ux_account_book.ux_nr = 0;
    spin_unlock(&g_ux_account_book.lock);

    del_timer_sync(&g_ux_account_book.timer);
    cancel_work_sync(&g_ux_account_book.work);
}

int get_accent_book_ux_nr(void)
{
    return g_ux_account_book.ux_nr;
}

int record_ux(struct task_struct *task, char *thread_name)
{
    unsigned long flags = 0;
    struct ux_recorder *recorder = NULL;

    if (!task) {
        return -EINVAL;
    }

    recorder = (struct ux_recorder *) kmalloc(sizeof(struct ux_recorder), GFP_ATOMIC);
    if (!recorder) {
        trans_err("create ux_recorder failed!\n");
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&recorder->ux_node);
    recorder->pid = task->pid;
    recorder->ux_hp = UX_HP_MAX;
    recorder->task = task;
    memset(recorder->thread_name, 0, sizeof(recorder->thread_name));

    if (thread_name) {
        strncpy(recorder->thread_name, thread_name, sizeof(recorder->thread_name));
    }

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_add(&recorder->ux_node, g_ux_account_book.list.next);

    if (g_ux_account_book.ux_nr < 0) {
        g_ux_account_book.ux_nr = 0;
    }

    g_ux_account_book.ux_nr += 1;
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);

    return 0;
}

int re_record_ux(struct task_struct *task, char *thread_name)
{
    unsigned long flags = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct ux_recorder *recorder = NULL;

    if ((!task) || (!thread_name)) {
        return -EINVAL;
    }

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_for_each_safe(pos, next, &g_ux_account_book.list) {
        recorder = container_of(pos, struct ux_recorder, ux_node);
        if ((recorder->task == task) && (recorder->pid == task->pid)) {
            strncpy(recorder->thread_name, thread_name, sizeof(recorder->thread_name));
            break;
        }
    }
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);

    return 0;
}

int delete_ux_record(struct task_struct *task)
{
    unsigned long flags = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct ux_recorder *recorder = NULL;

    if (!task) {
        return -EINVAL;
    }

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_for_each_safe(pos, next, &g_ux_account_book.list) {
        recorder = container_of(pos, struct ux_recorder, ux_node);
        if (likely(!IS_ERR_OR_NULL(recorder->task)) && (recorder->task == task)) {
            recorder->pid = -1;
            recorder->ux_hp = 0;
            recorder->task = NULL;
            memset(recorder->thread_name, 0, sizeof(recorder->thread_name));
            list_del_init(&recorder->ux_node);
            g_ux_account_book.ux_nr -= 1;
            kfree(recorder);
            recorder = NULL;
            break;
        }
    }
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);

    return 0;
}

void update_ux_account(struct task_struct *task, char *thread_name, int record_cmd)
{
    switch (record_cmd) {
        case RECORD_UX:
            record_ux(task, thread_name);
            break;
        case RE_RECORD_UX:
            re_record_ux(task, thread_name);
            break;
        case UNRECORD_UX:
            delete_ux_record(task);
            break;
        default:
            break;
    }

    return;
}

int get_threadid_ref(char *thread_name, int *ref)
{
    int cnt = 0;
    unsigned long flags = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct ux_recorder *recorder = NULL;

    if (!thread_name || !ref) {
        return -EINVAL;
    }

    if (strncmp(thread_name, UX_EXACT_HEAD, UX_EXACT_HEAD_LEN)) {
        goto out;
    }

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_for_each_safe(pos, next, &g_ux_account_book.list) {
        recorder = container_of(pos, struct ux_recorder, ux_node);
        if (!strncmp(recorder->thread_name, thread_name, sizeof(recorder->thread_name))) {
            cnt += 1;
        }
    }
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);

out:
    *ref = cnt;
    return 0;
}

static void liquidate_debtor(struct ux_recorder *recorder)
{
    struct rq *rq = NULL;
    struct rq_flags flags = {0};
    struct task_struct *task = NULL;

    if (IS_ERR_OR_NULL(recorder)) {
        return;
    }

    task = recorder->task;
    if (!IS_ERR_OR_NULL(task)) {
        rq = task_rq_lock(task, &flags);

        clear_trans_magic(task);
        set_ux_share_val(task, 0);
        set_ux_share_period(task, 0);
        set_ux_tags(task, 0);

        task_rq_unlock(rq, task, &flags);

        trans_info("del ux: %s/%d/%d\n", task->comm, task->pid, task->tgid);
        trans_sched_systrace_c(task->tgid, 0, "uas_boost-%d", task->pid);
    }

    recorder->pid = -1;
    recorder->ux_hp = 0;
    recorder->task = NULL;
    memset(recorder->thread_name, 0, sizeof(recorder->thread_name));
    list_del_init(&recorder->ux_node);
    g_ux_account_book.ux_nr -= 1;
    kfree(recorder);
    recorder = NULL;
}

static int get_task_css_id(struct task_struct *task)
{
    struct cgroup_subsys_state *css = NULL;

    if (unlikely((!task))) {
        return -EINVAL;
    }

    rcu_read_lock();
    css = task_css(task, cpu_cgrp_id);
    rcu_read_unlock();

    if (!css) {
        return -EFAULT;
    }

    return css->id;
}

static void audit_ux_task(struct ux_recorder *recorder)
{
    int css_id = -1;
    u32 ux_tags = 0;
    struct task_struct *task = recorder->task;

    css_id = get_task_css_id(task);
    if (css_id != BACKGROUND_CSS_ID) {
        goto out;
    }

    ux_tags = get_ux_tags(task);
    if (check_ux_flag(ux_tags, UX_TASK_FLAG0)) {
        goto out;
    }

    if (recorder->ux_hp > 0) {
        recorder->ux_hp -= 1;
        goto out;
    }

    liquidate_debtor(recorder);
    return;

out:
    trans_debug(DEBUG_KLOG, "audit ux: %s/%d/%d, ux_tags=%#x, css.id=%d, hp=%d\n", task->comm, task->pid, task->tgid, get_ux_tags(task), css_id, recorder->ux_hp);
    return;
}

static bool ux_record_audit(struct ux_recorder *recorder, bool audit_ux)
{
    bool ret = false;
    struct task_struct *tsk = NULL;

    if (!recorder || !recorder->task) {
        return false;
    }

    if (recorder->pid < 0 || recorder->pid > PID_MAX_DEFAULT) {
        return false;
    }

    rcu_read_lock();
    tsk = find_task_by_vpid(recorder->pid);
    if (!tsk || (tsk != recorder->task)) {
        goto unlock_rcu;
    }

    get_task_struct(tsk);
    ret = is_trans_task_struct_init(tsk);
    if (ret) {
        if (audit_ux) {
            audit_ux_task(recorder);
        }
    }
    put_task_struct(tsk);

unlock_rcu:
    rcu_read_unlock();
    return ret;
}

static void ux_account_audit(void)
{
    unsigned long flags = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct list_head list_backup = {0};
    struct ux_recorder *recorder = NULL;

    INIT_LIST_HEAD(&list_backup);

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_splice_init(&g_ux_account_book.list, &list_backup);
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);

    list_for_each_safe(pos, next, &list_backup) {
        recorder = container_of(pos, struct ux_recorder, ux_node);

        if (!ux_record_audit(recorder, true)) {
            recorder->pid = -1;
            recorder->ux_hp = 0;
            recorder->task = NULL;
            memset(recorder->thread_name, 0, sizeof(recorder->thread_name));
            list_del_init(&recorder->ux_node);
            g_ux_account_book.ux_nr -= 1;
            kfree(recorder);
            recorder = NULL;
            continue;
        }
    }

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_splice_init(&list_backup, g_ux_account_book.list.prev);
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);
}

static void ux_account_work_func(struct work_struct *work)
{
    ux_account_audit();
}

static void ux_account_timer_func(struct timer_list *timer)
{
    schedule_work(&g_ux_account_book.work);
}

void ux_account_book_audit(void)
{
    mod_timer(&g_ux_account_book.timer, get_jiffies_64() + msecs_to_jiffies(50));
}

void dump_ux_record(void)
{
    unsigned long flags = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct task_struct *tsk = NULL;
    struct ux_recorder *recorder = NULL;
    struct trans_task_struct *trans_ts = NULL;

    spin_lock_irqsave(&g_ux_account_book.lock, flags);
    list_for_each_safe(pos, next, &g_ux_account_book.list) {
        recorder = container_of(pos, struct ux_recorder, ux_node);
        if (!ux_record_audit(recorder, false)) {
            trans_info("%d-invalid, task=%p, pid=%d\n", g_ux_account_book.ux_nr, recorder->task, recorder->pid);
            continue;
        }

        tsk = recorder->task;
        trans_ts = get_trans_task_struct(tsk);
        if (IS_ERR_OR_NULL(trans_ts)) {
            continue;
        }

        trans_info("%d-%s/%d/%d, ID=%s, tags=%d/%#x, css.id=%d, hp=%d\n", \
            g_ux_account_book.ux_nr, tsk->comm, tsk->pid, tsk->tgid, recorder->thread_name, tsk->prio, \
            trans_ts->ux_tags, get_task_css_id(tsk), recorder->ux_hp);
    }
    spin_unlock_irqrestore(&g_ux_account_book.lock, flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static void dump_stack_info(unsigned long *store, unsigned int size, bool is_waker, struct task_struct *task, u64 delay_ms)
{
    if (!store || !size) {
        return;
    }

    trace_dump_info(store, size, is_waker, task, delay_ms);
}

static void dump_sched_dinfo(struct task_struct *task, u64 delay_ms)
{
    unsigned int waker_stack_depth = 0;
    unsigned int wakee_stack_depth = 0;
    unsigned long waker_stacks[DINFO_STACK_DEPTH] = {0};
    unsigned long wakee_stacks[DINFO_STACK_DEPTH] = {0};

    if (!task) {
        return;
    }

    waker_stack_depth = stack_trace_save(&waker_stacks[0], DINFO_STACK_DEPTH, 0);
    wakee_stack_depth = stack_trace_save_tsk(task, &wakee_stacks[0], DINFO_STACK_DEPTH, 0);

    dump_stack_info(&waker_stacks[0], waker_stack_depth, true, task, delay_ms);
    dump_stack_info(&wakee_stacks[0], wakee_stack_depth, false, task, delay_ms);
}

void sched_stat_blocked_hook(void *data, struct task_struct *task, u64 delay_ns)
{
    u64 delay_ms = delay_ns >> 20;

    if (!check_uas_feature(FEATURE_DINFO) || !uas_trace_enabled()) {
        return;
    }

    if (!task || (task->flags & PF_WQ_WORKER)) {
        return;
    }

    if (!task->in_iowait && delay_ms >= DINFO_DELAY_9MS) {
        if (check_sf_target(task) || check_overlayengine_target(task) || is_ux_task(task, NULL)) {
            dump_sched_dinfo(task, delay_ms);
        }
    }
}
#endif /* KERNEL_VERSION(5, 10, 0) */

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)))

void update_stats_dequeue_rt(struct rq *rq, struct task_struct *task, int flags)
{
    unsigned int state = 0;

    if (!check_uas_feature(FEATURE_DINFO) || !uas_trace_enabled()) {
        return;
    }

    if (!task || !rq) {
        return;
    }

    if (!is_rt_task(task)) {
        return;
    }

    if (!check_sf_target(task) && !check_overlayengine_target(task)) {
        return;
    }

    if (flags & DEQUEUE_SLEEP) {
        state = task_state(task);
        if (state & TASK_UNINTERRUPTIBLE) {
            __schedstat_set(task->se.statistics.block_start, rq_clock(rq));
        }
    }
}

void update_stats_enqueue_rt(struct rq *rq, struct task_struct *task, int flags)
{
    u64 block_start = 0;

    if (!check_uas_feature(FEATURE_DINFO) || !uas_trace_enabled()) {
        return;
    }

    if (!(flags & ENQUEUE_WAKEUP)) {
        return;
    }

    if (!task || !rq) {
        return;
    }

    if (!is_rt_task(task)) {
        return;
    }

    if (!check_sf_target(task) && !check_overlayengine_target(task)) {
        return;
    }

    block_start = schedstat_val(task->se.statistics.block_start);
    if (block_start) {
        __schedstat_set(task->se.statistics.block_start, 0);
    }
}

static void sched_blocked_reason_hook(void *data, struct task_struct *task)
{
    u64 delta = 0;
    u64 delay_ms = 0;
    u64 block_start = 0;
    struct rq *rq = NULL;
    unsigned int wakee_stack_depth = 0;
    unsigned long wakee_stacks[DINFO_STACK_DEPTH] = {0};

    if (!check_uas_feature(FEATURE_DINFO) || !uas_trace_enabled()) {
        return;
    }

    if (!task) {
        return;
    }

    if (!is_rt_task(task)) {
        return;
    }

    if (!check_sf_target(task) && !check_overlayengine_target(task)) {
        return;
    }

    block_start = schedstat_val(task->se.statistics.block_start);
    if (!block_start) {
        return;
    }

    rq = task_rq(task);
    if (!rq) {
        return;
    }

    delta = rq->clock - block_start;
    if ((s64)delta < 0) {
        delta = 0;
    }

    delay_ms = delta >> 20;

    if ((!task->in_iowait) && (delay_ms >= DINFO_DELAY_9MS)) {
        wakee_stack_depth = stack_trace_save_tsk(task, &wakee_stacks[0], DINFO_STACK_DEPTH, 0);
        dump_stack_info(&wakee_stacks[0], wakee_stack_depth, false, task, delay_ms);
    }
}

static struct trace_points_table g_sched_tracepoints = {
    .name = "sched_blocked_reason",
    .name_size = sizeof("sched_blocked_reason") - 1,
    .func = &sched_blocked_reason_hook,
    .tp = NULL,
};

static void lookup_trace_points(struct tracepoint *tp, void *ignore)
{
    if (!strncmp(g_sched_tracepoints.name, tp->name, g_sched_tracepoints.name_size)) {
        g_sched_tracepoints.tp = tp;
    }
}

int trace_points_init(void)
{
    for_each_kernel_tracepoint(lookup_trace_points, NULL);
    if (!g_sched_tracepoints.tp) {
        return -EFAULT;
    }

	return tracepoint_probe_register(g_sched_tracepoints.tp, g_sched_tracepoints.func,  NULL);
}

void trace_points_deinit(void)
{
    if (g_sched_tracepoints.tp) {
        tracepoint_probe_unregister(g_sched_tracepoints.tp, g_sched_tracepoints.func,  NULL);
    }
}
#endif /* KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) */

