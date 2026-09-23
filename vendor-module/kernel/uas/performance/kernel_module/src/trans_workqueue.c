#include <linux/types.h>
#include <linux/version.h>
#include <linux/preempt.h>

#include "trans_workqueue.h"

#define UX_KWORKER_CNT_MAX 80

#define UX_KWORKER_ENABLE_PERIOD  (3)

static u64 g_ux_kworker_enable = 0;
static atomic_t g_ux_kworker_count = ATOMIC_INIT(0);

int set_ux_kworker_enable(pid_t pid, u32 ux_tags, bool enable)
{
    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_KWORKER)) {
        if (g_ux_kworker_enable > 0)
            g_ux_kworker_enable = 0;

        return -EPERM;
    }

    // enable
    if (enable && check_ux_tags_ok(ux_tags)) {
        g_ux_kworker_enable = get_jiffies_64() + UX_KWORKER_ENABLE_PERIOD;
        trans_sched_systrace_c(9000000, pid, "uas_kworker");
        return 0;
    }

    // disable
    if (!enable && check_ux_kworker_type(ux_tags)) {
        if (time_after64(get_jiffies_64(), g_ux_kworker_enable)) {
            g_ux_kworker_enable = 0;
            trans_sched_systrace_c(9000000, 0, "uas_kworker");
            return 0;
        }
    }

    return -EPERM;
}

bool check_ux_kworker_enable(void)
{
    return g_ux_kworker_enable > 0;
}

bool check_ux_kworker_type(u32 ux_tags)
{
    return check_ux_flag(ux_tags, UX_FLAG_KWORKER);
}

static inline struct trans_kthread *trans_to_kthread(struct task_struct *task)
{
    struct trans_kthread *kthread = NULL;

    if (!(task->flags & PF_KTHREAD))
        return NULL;

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 1, 0)
    kthread = (struct trans_kthread *)task->worker_private;
#else
    kthread = (__force void *)task->set_child_tid;
#endif

    return kthread;
}

static bool trans_kthread_is_per_cpu(struct task_struct *task)
{
    struct trans_kthread *kthread = trans_to_kthread(task);
    if (!kthread)
        return false;

    return test_bit(TRANS_KTHREAD_IS_PER_CPU, &kthread->flags);
}

static void inc_ux_kworker_count(struct task_struct *task)
{
    if (!check_ux_kworker_type(get_ux_tags(task)))
        return;

    atomic_inc(&g_ux_kworker_count);
    trans_debug(DEBUG_KLOG, "inc cnt: %d, %s/%d/%d\n", atomic_read(&g_ux_kworker_count), task->comm, task->pid, task->tgid);
}

void dec_ux_kworker_count(struct task_struct *task, int route)
{
    if (!task)
        return;

    if (route == FREE_TASK_DEC_UX_KWORKER_CNT) {
        if (!check_ux_kworker_type(get_ux_tags(task)))
            return;
    }

    atomic_dec(&g_ux_kworker_count);
    trans_debug(DEBUG_KLOG, "dec cnt: %d, %s/%d/%d\n", atomic_read(&g_ux_kworker_count), task->comm, task->pid, task->tgid);
}

static bool check_rescuer_kworker(struct task_struct *task)
{
    if (unlikely(sizeof("kworker/R-") > sizeof(task->comm)))
        return false;

    if (strncmp(&(task->comm[sizeof("kworker")]), "R-", 2) == 0) {
        trans_debug(DEBUG_KLOG, "skip: %s/%d/%d\n", task->comm, task->pid, task->tgid);
        return true;
    }

    return false;
}

static void workqueue_execute_start_hook(void *data, struct work_struct *work)
{
    u32 ux_tags = 0;
    u32 ux_tags_curr = 0;
    struct task_struct *task = current;

    if (!check_uas_feature(FEATURE_DEFAULT))
        return;

    if (unlikely(!work))
        return;

    if ((task_nice(task) != HIGHPRI_NICE_LEVEL) || (!trans_kthread_is_per_cpu(task)))
        return;

    if (check_rescuer_kworker(task))
        return;

    ux_tags_curr = get_ux_tags(task);
    if ((!ux_tags_curr) && check_uas_feature(FEATURE_DEFAULT) && check_uas_feature(FEATURE_KWORKER)) {
        if (atomic_read(&g_ux_kworker_count) >= UX_KWORKER_CNT_MAX)
            return;

        ux_tags = lower_ux_dprio(init_ux_dprio(UX_TASK_TAGS_KWORKER));
        ux_tags = change_ux_slice(ux_tags, UX_TASK_SLICE0);
        if (!set_ux_tags_to_task(task, ux_tags, "Kworker", false))
            inc_ux_kworker_count(task);

        return;
    }

    if (ux_tags_curr &&
        (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_KWORKER))) {
        if (!set_ux_tags_to_task(task, UX_TASK_TAGS_NONE, "Kworker", false))
            dec_ux_kworker_count(task, DISABLE_FEATURE_DEC_UX_KWORKER_CNT);

        return;
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static struct trace_points_table g_workqueue_tracepoints[] = {
    {
        .name = "workqueue_execute_start",
        .name_size = sizeof("workqueue_execute_start") - 1,
        .func = &workqueue_execute_start_hook,
        .tp = NULL,
    },
};

#define WORKQUEUE_TRACEPOINTS_NUM    (ARRAY_SIZE(g_workqueue_tracepoints))

static void lookup_trace_points(struct tracepoint *tp, void *ignore)
{
    int idx = 0;

    for (idx = 0; idx < WORKQUEUE_TRACEPOINTS_NUM; idx++) {
        if (!strncmp(g_workqueue_tracepoints[idx].name, tp->name, g_workqueue_tracepoints[idx].name_size))
            g_workqueue_tracepoints[idx].tp = tp;
    }
}

static int workqueue_trace_points_init(void)
{
    int idx = 0;

    for_each_kernel_tracepoint(lookup_trace_points, NULL);

    for (idx = 0; idx < WORKQUEUE_TRACEPOINTS_NUM; idx++) {
        if (!g_workqueue_tracepoints[idx].tp)
            return -EFAULT;
    }

    for (idx = 0; idx < WORKQUEUE_TRACEPOINTS_NUM; idx++)
        tracepoint_probe_register(g_workqueue_tracepoints[idx].tp, g_workqueue_tracepoints[idx].func,  NULL);

    return 0;
}

static void workqueue_trace_points_deinit(void)
{
    int idx = 0;

    for (idx = 0; idx < WORKQUEUE_TRACEPOINTS_NUM; idx++) {
        if (g_workqueue_tracepoints[idx].tp)
            tracepoint_probe_unregister(g_workqueue_tracepoints[idx].tp, g_workqueue_tracepoints[idx].func,  NULL);
    }
}

void register_workqueue_tracepoints(void)
{
    workqueue_trace_points_init();
}

void unregister_workqueue_tracepoints(void)
{
    workqueue_trace_points_deinit();
}
#endif
