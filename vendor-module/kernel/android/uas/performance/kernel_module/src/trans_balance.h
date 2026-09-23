#ifndef _TRANS_BALANCE_H_
#define _TRANS_BALANCE_H_

#include <linux/version.h>

/*
 * DOWN_MIGR              : tasks allow migration to smaller cores.
 * UP_MIGR                : tasks are allowed to migrate from small cores to
 *                          medium or large cores, and medium cores do not
 *                          need to be balanced.
 * NORMAL_MIGR            : similar to UP_MIGR, but can be in the same cluster.
 * NEWIDLE_MIGR_RUNNABLE  : migrate tasks from CPUs of smaller or same capacity.
 * TICKPULL_MIGR_RUNNING  : pull long-running tasks from other CPUs. It should
 *                          be noted that tasks can only be migrated from CPUs
 *                          with smaller capacity to CPUs with larger capacity.
 * TICKPULL_MIGR_RUNNABLE : pull long-runnable tasks from other CPUs. Unlike
 *                          TICKPULL_MIGR_RUNNING, tasks can be migrated to
 *                          CPUs with the same capacity.
 */
enum migr_type {
    DOWN_MIGR = 1,
    UP_MIGR,
    NORMAL_MIGR,
    NEWIDLE_MIGR_RUNNABLE,
    TICKPULL_MIGR_RUNNING,
    TICKPULL_MIGR_RUNNABLE,
    WAKEUP,

    /* Add the new type above this line. */
    INVALID_MIGR_TYPE
};

extern cpumask_t __read_mostly **cpu_array;


bool has_rt_tasks(int cpu);
bool has_ux_tasks(int cpu);
bool has_runnable_rt_tasks(int cpu);
bool has_runnable_ux_tasks(int cpu);
void build_trans_cpu_array(int cls_nr);
bool balance_on_wakeup_ux(struct task_struct *tsk, int *orig_cpu, int sync);
void trans_should_honor_sync(struct rq *rq, struct task_struct *wakee, bool *sync);
bool filterout_on_wakeup_rt(struct task_struct *p, int cpu);
void ut_dump_cpu_array(void);
void ut_calc_order_idx(void);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 23)
void android_rvh_find_lowest_rq_hook(void *unused,
            struct task_struct *p, struct task_struct *exec_ctx,
            struct cpumask *lowest_mask, int ret, int *best_cpu);
#else
void android_rvh_find_lowest_rq_hook(void *unused,
            struct task_struct *p, struct cpumask *lowest_mask,
            int ret, int *best_cpu);
#endif

void adjust_rt_lowest_mask(struct task_struct *p, struct cpumask *lowest_mask);
bool trans_select_task_rq_rt(struct task_struct *tsk,
            int *orig_cpu, struct cpumask *lowest_mask, int wake_flags);
bool __trans_tick_balance(void *data, struct rq *rq);
bool __trans_newidle_balance(void *data, struct rq *this_rq,
            struct rq_flags *rf, int *pulled_task, int *done);
void trans_tick_balance(void *data, struct rq *rq);
void trans_newidle_balance(void *data, struct rq *this_rq,
                    struct rq_flags *rf, int *pulled_task, int *done);
void wakeup_irqwb(struct task_struct *prev, struct task_struct *next,
            struct rq *this_rq);
void init_irqwb(void);


struct proc_dir_entry *trans_lb_stat_proc_init(struct proc_dir_entry *pde);
void trans_lb_stat_proc_deinit(void);


extern struct cpumask *vendor_cpu_halt_mask;
extern struct cpumask *vendor_cpu_partial_halt_mask;
extern unsigned int *vendor_sched_boost_type_ptr;


void trans_register_kprobe(void);
void trans_unregister_kprobe(void);

static inline unsigned int get_sched_boost_type(void)
{
    unsigned int boost_type = 0;

    if (!vendor_sched_boost_type_ptr)
        return 0;

    boost_type = *vendor_sched_boost_type_ptr;

    return boost_type;
}


/**
 * uas_cpu_halted - Check if the specified CPU is fully halted.
 * @cpu: CPU index
 *
 * Return: true if the CPU is halted,
 *         false otherwise.
 */
static inline bool uas_cpu_halted(int cpu)
{
    if (cpu < 0)
        return false;

    if (!vendor_cpu_halt_mask)
        return false;

    return cpumask_test_cpu((cpu), vendor_cpu_halt_mask);
}

/**
 * uas_cpu_partial_halted - Check if the specified CPU is partially halted.
 * @cpu: CPU index
 *
 * Return: true if the CPU is partially halted,
 *         false otherwise.
 */
static inline bool uas_cpu_partial_halted(int cpu)
{
    if (cpu < 0)
        return false;

    if (!vendor_cpu_partial_halt_mask)
        return false;

    return cpumask_test_cpu((cpu), vendor_cpu_partial_halt_mask);
}

#if IS_ENABLED(CONFIG_MTK_SCHEDULER) || IS_ENABLED(CONFIG_PLATFORM_QCOM)
static inline int trans_topology_cluster_id(int cpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
    return topology_cluster_id(cpu);
#else
    return topology_physical_package_id(cpu);
#endif
}
#else
/*
 * On UNISOC platforms, all CPUs are described as belonging to the same
 * physical package (socket), even if they are actually located in
 * different clusters.
 *
 * As a result, both topology_physical_package_id() and
 * topology_cluster_id() always return 0 for all CPUs, making cluster
 * identification invalid.
 *
 * Therefore, we maintain a logical cluster id mapping internally.
 */
static inline int trans_topology_cluster_id(int cpu)
{
    return get_cpu_cluster_id(cpu);
}
#endif

bool trans_is_pipeline_task(struct task_struct *tsk);
bool trans_is_pipeline_cpu(int cpu);

#endif /* _TRANS_BALANCE_H_ */
