/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 transsion Inc.
 */
#include <linux/version.h>
#include <linux/cpuidle.h>
#include <linux/sched/clock.h>
#include <linux/kprobes.h>

#include "sched_common.h"
#include "trans_target.h"
#include "trans_balance.h"

#define CREATE_TRACE_POINTS
#include "trans_trace.h"

/*
 * Enable load balance stress testing.
 *
 * When enabled, this option increases the probability of triggering
 * load balance operations in order to expose potential stability
 * issues under high-frequency balance scenarios.
 *
 * This option is intended for stress testing and debugging only and
 * should be disabled in production builds.
 */
//#define CONFIG_LB_STRESS_TEST

u8 cpu_arrays_init_x11[2][1] = {
    {0},            /* up */

    {0},            /* down */
};

u8 cpu_arrays_init_x22[4][2] = {
    {0, 1},         /* up */
    {1, 0},

    {0, 1},         /* down */
    {1, 0},

};

u8 cpu_arrays_init_x33[6][3] = {
    {0, 1, 2},      /* up */
    {1, 2, 0},
    {2, 1, 0},

    {0, 1, 2},      /* down */
    {1, 0, 2},
    {2, 1, 0},
};

cpumask_t __read_mostly **cpu_array;

bool is_little_cpu(int cpu)
{
    return cpumask_test_cpu(cpu, &g_ux_sched_domains.sched_group[0].cpus);
}

void get_little_cpu_mask(struct cpumask *mask)
{
    cpumask_copy(mask, &g_ux_sched_domains.sched_group[0].cpus);
}

static inline bool is_key_scene(void)
{
    /*
     * WARNING:
     * Ensures consistency with the logic prior to the introduction
     * of the FEATURE_LOAD_TRACK feature. It is intended solely for
     * comparison testing purposes and will be removed soon.
     */
    if (!check_uas_feature(FEATURE_LOAD_TRACK))
        return true;

    /*
     * Apply boost in the following key scenarios.
     */
    return !!(g_sched_scene & (SS_TOUCH | SS_FLING | SS_LAUNCH | SS_ANIMATION));
}

/**
 * whether there are running or runnable RT tasks on
 * the runqueue of the specified cpu.
 */
bool has_rt_tasks(int cpu)
{
    struct rq *rq = cpu_rq(cpu);

    /* running rt */
    if (is_rt_task(rq->curr))
        return true;

    /* runnable rt */
    if (rt_rq_is_runnable(&rq->rt))
        return true;

    return false;
}

/**
 * whether there are runnable RT tasks on
 * the runqueue of the specified cpu.
 */
bool has_runnable_rt_tasks(int cpu)
{
    struct rq *rq = cpu_rq(cpu);

    /* runnable rt */
    if (rt_rq_is_runnable(&rq->rt))
        return true;

    return false;
}

/**
 * whether there are running or runnable UX tasks on
 * the runqueue of the specified cpu.
 */
bool has_ux_tasks(int cpu)
{
    struct rq *rq = NULL;
    struct trans_rq *trq = NULL;

    rq = cpu_rq(cpu);
    trq = get_trans_rq(rq);

    /* running ux */
    if (is_ux_task(rq->curr, NULL))
        return true;

    /* runnable ux */
    if (!is_ux_list_free(&trq->ux_list))
        return true;

    return false;
}

/**
 * whether there are runnable UX tasks on
 * the runqueue of the specified cpu.
 */
bool has_runnable_ux_tasks(int cpu)
{
    struct rq *rq = NULL;
    struct trans_rq *trq = NULL;

    rq = cpu_rq(cpu);
    trq = get_trans_rq(rq);

    /* runnable ux */
    if (!is_ux_list_free(&trq->ux_list))
        return true;

    return false;
}

static void init_cpu_array(void)
{
    int i;
    int cols = g_ux_sched_domains.group_nr;
    int rows = cols * 2;

    if (cpu_array)
        return;

    cpu_array = kcalloc(rows, sizeof(cpumask_t *), GFP_ATOMIC | __GFP_NOFAIL);
    if (!cpu_array)
        panic("Failed to allocate memory for cpu_array\n");

    for (i = 0; i < rows; i++) {
        cpu_array[i] = kcalloc(cols, sizeof(cpumask_t),
                GFP_ATOMIC | __GFP_NOFAIL);
        if (!cpu_array[i])
            panic("Failed to allocate memory for cpu_array\n");
    }
}

__maybe_unused static void deinit_cpu_array(void)
{
    int i;
    int cols = g_ux_sched_domains.group_nr;
    int rows = cols * 2;

    if (!cpu_array)
        return;

    for (i = 0; i < rows; i++) {
        if (cpu_array[i]) {
            kfree(cpu_array[i]);
            cpu_array[i] = NULL;
        }
    }

    kfree(cpu_array);
    cpu_array = NULL;
}

void build_trans_cpu_array(int cls_nr)
{
    u8 *select_init_list;
    int idx;
    int i, j;

    init_cpu_array();

    switch (cls_nr) {
    case 1:
        select_init_list = (u8 *)cpu_arrays_init_x11;
        break;
    case 2:
        select_init_list = (u8 *)cpu_arrays_init_x22;
        break;
    case 3:
        select_init_list = (u8 *)cpu_arrays_init_x33;
        break;
    default:
        panic("unsupported cluster num=%d\n", cls_nr);
    }

    for (i = 0; i < 2*cls_nr; i++) {
        for (j = 0; j < cls_nr; j++) {
            idx = select_init_list[i * cls_nr + j];
            cpumask_copy(&cpu_array[i][j],
                    &g_ux_sched_domains.sched_group[idx].cpus);
        }
    }
}

void ut_dump_cpu_array(void)
{
    int i, j;
    char buf[256];
    int count = 0;
    int cls_nr = g_ux_sched_domains.group_nr;

    for (i = 0; i < 2*cls_nr; i++) {
        count += snprintf(buf + count, PAGE_SIZE - count,
                    "CPU_ARRAY: order_idx=%d, [", i);
        for (j = 0; j < cls_nr; j++) {
            count += snprintf(buf + count, PAGE_SIZE - count, "%d%s",
                trans_topology_cluster_id(cpumask_first(&cpu_array[i][j])),
                j == cls_nr-1 ? "":" -> ");
        }
        count += snprintf(buf + count, PAGE_SIZE - count, "]\n");
        trans_info("%s", buf);
        memset(buf, 0, sizeof(buf));
        count = 0;
    }
}

enum task_type {
    TASK_NONE = 0,
    TASK_CFS,       /* Normal CFS task(reserved) */
    TASK_UX,        /* UX task */
    TASK_RT,        /* Generic RT task */
    TASK_SF,        /* SurfaceFlinger-related RT task */
};

/**
 * struct trans_lb_env - Context for task load balancing and CPU selection.
 *
 * @migr_type:  Migration type describing the reason and direction of
 *              the load balancing operation.
 * @task_type:  Classified task type used to guide scheduling and CPU
 *              selection decisions (e.g. CFS, UX, RT, SF).
 * @pref_cls:   Preferred cluster for CPU selection. This is a policy
 *              hint rather than a hard constraint. A negative value
 *              indicates no specific cluster preference.
 * @curr_cls:   Cluster index where the task is currently running.
 * @load_avg:   Averaged load contribution of the task, used for load
 *              balancing decisions.
 * @ux_tags:    UX-related attributes associated with the task.
 */
struct trans_lb_env {
    enum migr_type migr_type;
    enum task_type task_type;

    int pref_cls;
    int curr_cls;

    unsigned long load_avg;
    u32 ux_tags;
};

static inline enum task_type get_task_type(struct task_struct *p)
{
    /* SurfaceFlinger-related RT task */
    if (check_sf_target(p))
        return TASK_SF;

    /* Generic RT task */
    if (rt_task(p))
        return TASK_RT;

    /* UX task (CFS-based) */
    if (is_ux_task_base(p, NULL))
        return TASK_UX;

    /* Normal CFS task */
    if (is_fair_task(p))
        return TASK_CFS;

    return TASK_NONE;
}


/**
 * calc_order_idx - calculate cluster traversal order for task wakeup
 *                  and migration
 * @env: migration environment describing the migration type and the
 *       task's current cluster
 * @order_idx: returned index into cpu_arrays_init[][] selecting the
 *             starting cluster traversal order
 * @walk_cnt: number of clusters to walk starting from @order_idx
 *
 * Calculate the cluster traversal order used during task migration based
 * on the migration type and the task's current cluster position.
 *
 * The cluster traversal order is defined by cpu_arrays_init[][], which
 * contains pre-defined permutations of cluster indices. The calculated
 * @order_idx selects one of these permutations, and @walk_cnt limits how
 * many clusters will be visited starting from that index.
 *
 * This function only computes the traversal order and walk range. It does
 * not select tasks or perform the actual migration.
 *
 * The examples below assume a system with three clusters (0, 1, 2) and
 * illustrate how cpu_arrays_init[][] is initialized and how @order_idx
 * and @walk_cnt are derived for each migration type.
 *
 * cpu_arrays_init[][] reference (3 clusters):
 *
 *   index : cluster walk order
 *   --------------------------
 *     0   : 0 -> 1 -> 2
 *     1   : 1 -> 2 -> 0
 *     2   : 2 -> 1 -> 0
 *     3   : 0 -> 1 -> 2
 *     4   : 1 -> 0 -> 2
 *     5   : 2 -> 1 -> 0
 *
 * ---------------------------------------------------------------------
 * DOWN_MIGR
 * cur_idx    order_idx   walk_cnt
 *    0            X          X
 *    1            3          1
 *    2            4          2
 *
 * ---------------------------------------------------------------------
 * UP_MIGR
 * cur_idx    order_idx   walk_cnt
 *    0            1          2
 *    1            X          X
 *    2            X          X
 *
 * ---------------------------------------------------------------------
 * NORMAL_MIGR
 * cur_idx    order_idx   walk_cnt
 *    0            0          3
 *    1            1          2
 *    2            2          2
 *
 * ---------------------------------------------------------------------
 * NEWIDLE_MIGR_RUNNABLE
 * cur_idx    order_idx   walk_cnt
 *    0            0          1
 *    1            0          2
 *    2            0          3
 *
 * ---------------------------------------------------------------------
 * TICKPULL_MIGR_RUNNING
 * cur_idx    order_idx   walk_cnt
 *    0            X          X
 *    1            0          1
 *    2            0          2
 *
 * ---------------------------------------------------------------------
 * TICKPULL_MIGR_RUNNABLE
 * cur_idx    order_idx   walk_cnt
 *    0            X          X
 *    1            0          2
 *    2            0          3
 *
 * ---------------------------------------------------------------------
 * WAKEUP
 * cur_idx    order_idx   walk_cnt
 *    X            1          3
 *    X            0          1    (UX_FLAG_CPU_L)
 *
 * Return: true if a valid cluster traversal order was computed, or false
 * if the migration type or cluster configuration does not permit a valid
 * walk order.
 */
bool calc_order_idx(struct trans_lb_env *env,
            int *order_idx, int *walk_cnt)
{
    enum migr_type migr_type = env->migr_type;
    enum task_type task_type = env->task_type;
    int pref_cls = env->pref_cls;
    int curr_cls = env->curr_cls;
    u32 ux_tags = env->ux_tags;
    int cls_nr = g_ux_sched_domains.group_nr;
    bool boost = false;

    if (cls_nr <= 0)
        return false;

    /*
     * valid type?
     */
    if (migr_type < DOWN_MIGR || migr_type >= INVALID_MIGR_TYPE)
        return false;

    /*
     * The following operations cannot be performed when
     * there is only one cluster
     */
    if (cls_nr == 1 &&
        ((migr_type == DOWN_MIGR) ||
        (migr_type == UP_MIGR) ||
        (migr_type == TICKPULL_MIGR_RUNNING) ||
        (migr_type == TICKPULL_MIGR_RUNNABLE)))
        return false;

    switch (migr_type) {
    case DOWN_MIGR:
        /*
         * If p is already running on the silver core, there is
         * no need to perform the DOWN_MIGR operation.
         */
        if (curr_cls == 0)
            goto fail;

        *order_idx = curr_cls + cls_nr -1;
        *walk_cnt = curr_cls;
        break;
    case UP_MIGR:
        /*
         * Only tasks on the silver core are allowed to perform
         * the UP_MIGR operation.
         */
        if (curr_cls > 0)
            goto fail;

        *order_idx = curr_cls + 1;
        *walk_cnt = cls_nr - *order_idx;
        break;
    case NORMAL_MIGR:
        *order_idx = curr_cls;
        if ((curr_cls == cls_nr-1) && (cls_nr >= 3)) {
            *walk_cnt = cls_nr - 1;
        } else {
            *walk_cnt = cls_nr - *order_idx;
        }
        break;
    case NEWIDLE_MIGR_RUNNABLE:
        /*
         * Pull tasks from the small core directly.
         */
        *order_idx = 0;

        /*
         * Allow pulling tasks from CPUs with lower compute capacity than
         * the current CPU, and also from other CPUs within the same cluster.
         */
        *walk_cnt = curr_cls + 1;
        break;
    case TICKPULL_MIGR_RUNNING:
        /*
         * Forbid pulling tasks to small cores.
         */
        if (curr_cls == 0)
            goto fail;

        /*
         * Pull tasks from the small core directly.
         */
        *order_idx = 0;

        /*
         * Only allow pulling tasks from CPUs with lower compute capacity
         * than the current CPU.
         */
        *walk_cnt = curr_cls;
        break;
    case TICKPULL_MIGR_RUNNABLE:
        /*
         * Forbid pulling tasks to small cores.
         */
        if (curr_cls == 0)
            goto fail;

        /*
         * Pull tasks from the small core directly.
         */
        *order_idx = 0;

        /*
         * Allow pulling tasks from CPUs with lower compute capacity than
         * the current CPU, and also from other CPUs within the same cluster.
         */
        *walk_cnt = curr_cls + 1;
        break;
    case WAKEUP:
        if (task_type == TASK_UX || task_type == TASK_SF) {
            /*
             * The 'boost' flag determines whether to prioritize the powerful
             * CPU cores for performance enhancement.
             *
             * The current boost strategy follows these rules:
             * 1. By default, the boost strategy is always enabled.
             * 2. When boost is enabled, critical threads are scheduled with a
             *    preference order of: medium cores -> big cores -> little cores.
             * 3. If a critical thread is temporarily scheduled on a little core,
             *    it will be migrated back to a medium or big cores once such
             *    resources become available.
             *
             * However, if the user space sets the UX_FLAG_CPU_L flag, the
             * system will prioritize the little CPU cores, completely ignoring
             * the 'boost' status. This provides a mechanism for user space to
             * enforce specific CPU selection policies, which might be required
             * for workload isolation, thermal control, or other considerations
             * unrelated to power-saving.
             *
             * NOTE:
             * This strategy completely ignores the impact of key_sence and
             * task load. Whether this behavior is reasonable, and whether it may
             * introduce negative effects on power consumption, still requires
             * further validation.
             */
            boost = !(ux_tags & UX_FLAG_CPU_L);

            *order_idx = ((cls_nr > 1) && boost) ? 1 : 0;
            *walk_cnt = boost ? cls_nr : 1;
        } else if (task_type == TASK_RT) {
            /*
             * For generic RT tasks, honor the preferred cluster selected
             * by the platform-specific (e.g. MTK/QCOM) RT scheduling logic,
             * and start CPU selection from that cluster.
             */
            *order_idx = pref_cls;
            *walk_cnt = cls_nr;
        } else {
            /* Other task types are not handled for wakeup. */
            goto fail;
        }

        break;
    default:
        goto fail;
        break;
    }

    trace_calc_order_idx(migr_type, task_type, curr_cls, *order_idx, *walk_cnt);
    return true;

fail:
    *order_idx = -1;
    *walk_cnt = -1;
    return false;
}

void ut_calc_order_idx(void)
{
    struct trans_lb_env env;
    int cls_nr = g_ux_sched_domains.group_nr;
    bool ret;
    int order_idx = -1;
    int walk_cnt = -1;
    int cur_cls = 0;
    int migr_type;

    for (migr_type = DOWN_MIGR; migr_type < INVALID_MIGR_TYPE; migr_type++) {
        for (cur_cls = 0; cur_cls < cls_nr; cur_cls++) {
            env.migr_type = migr_type;
            env.curr_cls = cur_cls;
            env.load_avg = trans_task_load_uclamp(current);
            env.ux_tags = get_ux_tags(current);
            ret = calc_order_idx(&env, &order_idx, &walk_cnt);

            trans_info("ORDER_IDX: ret=%d, "
                "migr_type=%d, cur_cls=%d, "
                "load_avg=%d, ux_tags=0x%x, "
                "order_idx=%d, walk_cnt=%d\n",
                (int)ret, migr_type, cur_cls,
                (int)env.load_avg, env.ux_tags,
                ret?order_idx:-1, ret?walk_cnt:-1);
        }
    }
}

/**
 * return false indicates that need to re-select an available cpu,
 * and true to use orig_cpu
 */
static bool is_orig_cpu_available(int orig_cpu,
            unsigned long load_avg, enum task_type task_type)
{
    if (orig_cpu < 0 || orig_cpu >= TRANS_NR_CPUS)
        return false;

    /*
     * We need to re-select an available cpu if there are
     * running/runnable ux or rt tasks on the runqueue of
     * the orig_cpu.
     */
    if (has_ux_tasks(orig_cpu) || has_rt_tasks(orig_cpu))
        return false;

    /*
     * Only UX and SF tasks need to further consider scenario
     * state and load information. Other task types, such as
     * generic RT tasks, only need to satisfy the mutual
     * exclusion conditions above.
     */
    if (task_type != TASK_UX && task_type != TASK_SF)
        return true;

    /*
     * Prioritize using high-performance CPUs in key scenarios.
     */
    if (is_key_scene())
        return !is_little_cpu(orig_cpu);

    /*
     * Select the CPU based on load_avg in non-key scenarios.
     * Prioritize high-performance CPUs if need_boost is true;
     * otherwise, prioritize little cores.
     */
    if (need_boost(load_avg))
        return !is_little_cpu(orig_cpu);

    return is_little_cpu(orig_cpu);
}

/*
 * The maximum exit delay allowed for latency-sensitive tasks.
 */
#define TRANS_EXIT_LATENCY_US   2000        /* 2ms */

/**
 * find_available_cpu - Common function to find an available CPU for ux and rt.
 *
 * Overview:
 *   Attempts to find the best available CPU for a UX or RT task from
 *   its allowed set. If the given orig_cpu meets load, online, and
 *   activity requirements, it is used directly; otherwise, the
 *   function recalculates candidate order and iterates until a
 *   suitable CPU is found.
 *
 * Selection process:
 *   1. Check if orig_cpu is available based on load and policy.
 *   2. Compute candidate order_idx and walk_cnt from task load info.
 *   3. Iterate candidate CPUs, skipping cores that are offline,
 *      inactive, hosting RT/UX tasks, or deep-sleep with high latency.
 *   4. If a suitable CPU is found, update orig_cpu and return true.
 *
 * Notes:
 *   - Disabled when system has only one cluster.
 *   - lowest_mask can further restrict candidate CPUs.
 *
 * Parameters:
 *   @tsk         Pointer to target task_struct to schedule.
 *   @orig_cpu    Pointer to current or selected CPU ID for the task.
 *   @lowest_mask Optional mask to limit candidate CPUs; may be NULL.
 *
 * Return:
 *   true  - A suitable CPU was found and *orig_cpu updated.
 *   false - No suitable CPU found; *orig_cpu unchanged or original.
 */
static bool find_available_cpu(struct task_struct *tsk,
            int *orig_cpu, struct cpumask *lowest_mask,
            int sync)
{
    struct trans_lb_env env;
    struct cpuidle_state *idle = NULL;
    cpumask_t search_cpus = CPU_MASK_NONE;
    int orig_cpu_backup = *orig_cpu;
    int order_idx = -1, walk_cnt = -1, idx;
    int reason = 0;
    int cpu = -1;
    bool ret = false;
    u64 sc[2];

    /*
     * TODO
     */
    bool latency_sensive = false;

    /*
     * This feature is disabled when there is only one cluster.
     */
    if (g_ux_sched_domains.group_nr <= 1)
        return false;

    sc[0] = sched_clock();

    env.migr_type = WAKEUP;
    env.task_type = get_task_type(tsk);
    env.curr_cls = 0;       /* Not used; ignored in the WAKEUP path */

    /* Use the original CPU's cluster as the preferred one if valid. */
    env.pref_cls = 0;
    if (*orig_cpu > 0 && *orig_cpu < TRANS_NR_CPUS)
        env.pref_cls = trans_topology_cluster_id(*orig_cpu);

    env.load_avg = trans_task_load_uclamp(tsk);
    env.ux_tags = get_ux_tags(tsk);

    /*
     * Step 1:
     * Check whether orig_cpu can meet the requirements.
     */
    if (is_orig_cpu_available(*orig_cpu, env.load_avg, env.task_type)) {
        ret = false;
        reason = 1;
        goto out;
    }

    /*
     * Step 2:
     * Calculate order_idx and walk_cnt.
     */
    ret = calc_order_idx(&env, &order_idx, &walk_cnt);
    if (!ret) {
        reason = 2;
        goto out;
    }

    /*
     * Step 3:
     * Find an available cpu from search_cpus.
     */
    ret = false;
    reason = 3;
    for (idx = 0; idx < walk_cnt; idx++) {
        cpumask_and(&search_cpus, &tsk->cpus_mask, &cpu_array[order_idx][idx]);
        if (lowest_mask && !cpumask_empty(lowest_mask)) {
            cpumask_and(&search_cpus, &search_cpus, lowest_mask);
        }

        for_each_cpu(cpu, &search_cpus) {

            trace_walk_each_cpu(tsk, search_cpus, cpu);

            /*
             * affinify
             */
            if (!cpumask_test_cpu(cpu, tsk->cpus_ptr))
                continue;

            /*
             * cannot place tasks on a offline/inactive cpu.
             */
            if (!cpu_online(cpu) || !cpu_active(cpu))
                continue;

            /*
             * cannot place tasks on a halt cpu.
             */
            if (uas_cpu_halted(cpu) || uas_cpu_partial_halted(cpu))
                continue;

            /*
             * ignore those CPUs if there is a rt task running/runnable on it.
             */
            if (has_rt_tasks(cpu))
                continue;

            /*
             * ignore those CPUs if there is a ux task running/runnable on it.
             */
            if (has_ux_tasks(cpu))
                continue;

            /*
             * ignore those CPUs if there is a pileline task running on it.
             */
            if (trans_is_pipeline_cpu(cpu))
                continue;

            /*
             * avoid placing latency-sensitive tasks on a deep-sleeping CPU.
             */
            idle = idle_get_state(cpu_rq(cpu));
            if (latency_sensive &&
                idle && idle->exit_latency > TRANS_EXIT_LATENCY_US)
                continue;

            *orig_cpu = cpu;
            ret = true;
            reason = 0;
            goto out;
        }
    }

out:
    sc[1] = sched_clock();
    trace_find_available_cpu(ret, reason, tsk, env.ux_tags, env.load_avg,
            order_idx, walk_cnt, orig_cpu_backup, cpu);
    trace_find_available_cpu_consume(tsk, sc[1]-sc[0]);

    if (unlikely(g_trans_sched_debug & DEBUG_BALANCE)) {
        trans_sched_systrace_c(TRANS_SYSTRACE_POS, cpu,
                "find_available_cpu-%s-%d",
                ret ? "SUCC" : "FAIL", tsk->pid);
    }

    /*
     * Failed, no available cpu found.
     */
    return ret;
}

bool balance_on_wakeup_ux(
            struct task_struct *tsk, int *orig_cpu,
            int sync)
{
    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return false;

    /*
     * Do not interfere with pipeline tasks, even if the task is
     * marked as a UX task.
     */
    if (trans_is_pipeline_task(tsk))
        return false;

    /*
     * Valid for ux tasks only.
     */
    if (!is_ux_task(tsk, NULL))
        return false;

    return find_available_cpu(tsk, orig_cpu, NULL, sync);
}

/**
 * trans_should_honor_sync - Decide whether to honor RT sync wakeup.
 * @rq:     Runqueue on which the wakeup is being evaluated.
 * @wakee:  The task being woken up.
 * @sync:   Pointer to the sync wakeup flag. The value may be cleared
 *          by this function if sync semantics should be suppressed.
 *
 * Design rationale:
 * RT sync wakeups allow the woken RT task to immediately preempt the
 * current task, assuming the waker will block soon. While this can
 * reduce handoff latency, it may also introduce unnecessary preemption
 * and scheduling jitter when the target CPU is already contended.
 *
 * This helper selectively disables RT sync wakeups when the destination
 * CPU already has runnable UX or RT tasks. In such cases, honoring sync
 * semantics provides little benefit and may instead cause excessive
 * preemption, cache thrashing, and latency inflation for existing
 * high-priority workloads.
 *
 * By clearing @sync under contention, the wakeup falls back to a normal
 * (non-sync) RT wakeup, preserving system stability while still
 * respecting RT priority ordering.
 */
void trans_should_honor_sync(struct rq *rq,
            struct task_struct *wakee, bool *sync)
{
    int cpu;
    bool raw_sync;

    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return;

    cpu = cpu_of(rq);
    raw_sync = *sync;

    /*
     * If the target CPU already has runnable UX or RT tasks, suppress
     * RT sync semantics to avoid unnecessary preemption and scheduling
     * jitter under contention.
     */
    if (has_ux_tasks(cpu) || has_rt_tasks(cpu)) {
        *sync = false;
    }

    trace_should_honor_sync(cpu, current, wakee, raw_sync, *sync);
    return;
}


/**
 * trans_select_task_rq_rt - Optional RT wakeup CPU selection hook.
 * @tsk:        RT task being woken up.
 * @orig_cpu:   Pointer to the CPU selected by the vendor scheduler.
 *              May be updated by this function if an alternative CPU
 *              is chosen.
 * @lowest_mask:
 *              Candidate CPU mask provided by the scheduler. For
 *              non-sync RT wakeups, the mask is expected to be
 *              pre-populated by the vendor scheduler. For sync RT
 *              wakeups, the mask may be uninitialized.
 * @wake_flags: Wakeup flags associated with the RT wakeup (e.g. WF_SYNC).
 *
 * Design rationale:
 * This hook provides a controlled mechanism to refine RT wakeup CPU
 * selection while preserving the default behavior of the vendor
 * scheduler whenever possible.
 *
 * Return:
 *   true  - The CPU selection has been overridden and *@orig_cpu has
 *           been updated.
 *   false - No override is required; the original CPU selection should
 *           be honored.
 */
bool trans_select_task_rq_rt(struct task_struct *tsk,
            int *orig_cpu, struct cpumask *lowest_mask,
            int wake_flags)
{
    bool sync;

    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return false;

    if (*orig_cpu < 0 || *orig_cpu >= TRANS_NR_CPUS)
        return false;

    /*
     * All RT tasks must satisfy basic mutual exclusion requirements.
     * If the original CPU already hosts runnable UX or RT tasks,
     * CPU re-selection is required regardless of task classification.
     */
    if (has_ux_tasks(*orig_cpu) || has_rt_tasks(*orig_cpu))
        goto select_cpu;

    /*
     * Do not interfere with pipeline tasks, even if the task is
     * marked as a UX task.
     */
    if (trans_is_pipeline_task(tsk))
        return false;

    /*
     * For non-SurfaceFlinger RT tasks, the original CPU selection
     * is honored once basic mutual exclusion constraints are met.
     * No further intervention is required.
     */
    if (!check_sf_target(tsk))
        return false;

select_cpu:
    /*
     * For RT sync wakeups, some platforms do not initialize @lowest_mask.
     * To establish a well-defined baseline, the mask is temporarily expanded
     * to include all CPUs. The final eligibility filtering (capacity,
     * priority, UX/RT exclusion, halted CPUs, etc.) is deferred to
     * find_available_cpu().
     */
    sync = !!(wake_flags & WF_SYNC);
    if (sync && lowest_mask)
        cpumask_full(lowest_mask);

    return find_available_cpu(tsk, orig_cpu, lowest_mask, sync);
}

/**
 * filterout_on_wakeup_rt - determine whether a CPU should be excluded
 *                          during RT task wakeup CPU selection
 * @p:   RT task being woken up
 * @cpu: candidate CPU to be evaluated
 *
 * This helper is used during RT task wakeup to decide whether a
 * candidate CPU should be filtered out from the selection set.
 *
 * The decision is based on:
 *   - mutual exclusion with running or runnable UX tasks
 *   - task classification (SurfaceFlinger-related or not)
 *   - system scenario and task utilization
 *   - CPU capacity (little vs non-little cores)
 *
 * For non-SurfaceFlinger RT tasks, only UX task exclusion is enforced.
 * For SurfaceFlinger-related RT tasks, additional constraints are
 * applied to avoid placing them on little cores under high-load or
 * key scenarios.
 *
 * Return:
 *   true  - @cpu does not meet the requirements and should be filtered out
 *   false - @cpu is eligible as a backup CPU candidate
 */
bool filterout_on_wakeup_rt(struct task_struct *p, int cpu)
{
    unsigned long load_avg = 0;
    bool ret = false;
    u64 sc[2];

    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return false;

    sc[0] = sched_clock();

    /**
     * If the cpu has a running/runnable UX/RT task on its runqueue,
     * the cpu should be filtered out when selecting cores for
     * rt tasks.
     */
    if (has_ux_tasks(cpu) || has_rt_tasks(cpu)) {
        ret = true;
        goto out;
    }

    /*
     * Preemption is not allowed if a pipeline-related task is running
     * on the target CPU.
     */
    if (trans_is_pipeline_cpu(cpu)) {
        ret = true;
        goto out;
    }

    /*
     * Only SurfaceFlinger-related tasks are sensitive to the following
     * scenarios and utilization constraints. Regular RT tasks only
     * require mutual exclusion guarantees.
     */
    if (!check_sf_target(p)) {
        ret = false;
        goto out;
    }

    /*
     * Tasks related to SurfaceFlinger should not run on little
     * cores during key scenarios or when the utilization exceeds
     * HIGH_LOAD_THRESHOLD.
     */
    load_avg = trans_task_load_uclamp(p);
    if ((is_key_scene() || need_boost(load_avg)) && is_little_cpu(cpu)) {
        ret = true;
        goto out;
    }

out:
    sc[1] = sched_clock();

    trace_filterout_on_wakeup_rt(ret, p, load_avg, cpu);
    trace_filterout_on_wakeup_rt_consume(p, sc[1]-sc[0]);

    if (unlikely(g_trans_sched_debug & DEBUG_BALANCE) && ret) {
        trans_sched_systrace_c(TRANS_SYSTRACE_POS, p->pid,
                    "filterout_wakeup_rt[%d]", cpu);
    }

    return ret;
}

/**
 * Filter out cpus that are processing UX tasks.
 *
 * TODO:
 * Decide whether to filter out the CPU based on
 * the priority of the UX tasks.
 */
void adjust_rt_lowest_mask(struct task_struct *p,
                struct cpumask *lowest_mask)
{
    cpumask_t backup_lowest_mask = CPU_MASK_NONE;
    struct task_struct *curr;
    struct rq *rq;
    unsigned int iter_cpu;
    u64 sc[2];

    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return;

    if (!lowest_mask || cpumask_empty(lowest_mask))
        return;

    sc[0] = sched_clock();

    cpumask_copy(&backup_lowest_mask, lowest_mask);

    iter_cpu = cpumask_first(lowest_mask);
    while (iter_cpu < nr_cpu_ids) {
        /*
         * unlocked access
         */
        rq = cpu_rq(iter_cpu);
        curr = rcu_dereference(rq->curr);

        /*
         * No need to worry about whether curr is a
         * ux task or not if it is exiting.
         */
        if (!curr || (curr->flags & PF_EXITING)) {
            iter_cpu = cpumask_next(iter_cpu, lowest_mask);
            continue;
        }

        /*
         * iter_cpu can be used to process RT tasks if there are
         * no UX tasks in running/runnable state on it's runqueue.
         *
         * In addition, RT tasks should not be allowed to preempt
         * pipeline-related threads.
         *
         * Note:
         * There is no need to filter the CPU running the RT task,
         * which may result in the failure to find the right CPU
         * for the RT task.
         */
        if (!has_ux_tasks(iter_cpu) && !trans_is_pipeline_cpu(iter_cpu)) {
            iter_cpu = cpumask_next(iter_cpu, lowest_mask);
            continue;
        }

        cpumask_clear_cpu(iter_cpu, lowest_mask);
        iter_cpu = cpumask_next(iter_cpu, lowest_mask);
    }

    sc[1] = sched_clock();

    trace_adjust_rt_lowest_mask(p, backup_lowest_mask, *lowest_mask);
    trace_adjust_rt_lowest_mask_consume(p, sc[1]-sc[0]);

    /*
     * TODO:
     * The lowest_mask may be empty after the above filtering.
     */
}

static int trans_find_lowest_rq(struct task_struct *p,
            struct cpumask *lowest_mask)
{
    cpumask_t search_cpus = CPU_MASK_NONE;
    int cls_nr = g_ux_sched_domains.group_nr;
    static int last_cpu_rr = 0;
    unsigned long load_avg;
    int target_cpu;
    int idx = 0;
    u64 sc[2];

    load_avg = trans_task_load_uclamp(p);
    if (check_sf_target(p) && (is_key_scene() || need_boost(load_avg))) {
        idx = (cls_nr > 1) ? 1 : 0;
    }

    sc[0] = sched_clock();

    cpumask_and(&search_cpus, lowest_mask,
            &g_ux_sched_domains.sched_group[idx].cpus);

    if (!cpumask_empty(&search_cpus)) {
        /*
         * Round-Robin among the allowed ones to avoid load
         * imbalance between CPUs.
         */
        target_cpu = cpumask_next(last_cpu_rr, &search_cpus);
        if (unlikely(target_cpu >= nr_cpu_ids)) {
            target_cpu = cpumask_first(&search_cpus);
        }
        last_cpu_rr = target_cpu;

    } else {
        target_cpu = cpumask_test_cpu(task_cpu(p), lowest_mask) ?
                task_cpu(p) :
                cpumask_first(lowest_mask);
    }
    sc[1] = sched_clock();

    trace_trans_find_lowest_rq(p, load_avg, search_cpus, target_cpu);
    trace_trans_find_lowest_rq_consume(p, sc[1]-sc[0]);

    if (unlikely(g_trans_sched_debug & DEBUG_BALANCE)) {
        trans_sched_systrace_c(TRANS_SYSTRACE_POS, target_cpu,
                    "select_for_rt[%d]", p->pid);
    }

    return target_cpu;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 23)
void android_rvh_find_lowest_rq_hook(void *unused,
            struct task_struct *p, struct task_struct *exec_ctx, struct cpumask *lowest_mask,
            int ret, int *best_cpu)
#else
void android_rvh_find_lowest_rq_hook(void *unused,
            struct task_struct *p, struct cpumask *lowest_mask,
            int ret, int *best_cpu)
#endif
{
    u64 sc[2];
    unsigned long load_avg;
    cpumask_t lowest_mask_backup = CPU_MASK_NONE;
    int best_cpu_backup = *best_cpu;
    enum task_type task_type;

    if (!check_uas_feature(FEATURE_SELECT_CPUS))
        return;

    if (!ret || !lowest_mask)
        return;

    sc[0] = sched_clock();

    cpumask_copy(&lowest_mask_backup, lowest_mask);

    /*
     * Check whether orig_cpu can meet the requirements.
     * Prefer using the vendor-provided best_cpu first.
     */
    load_avg = trans_task_load_uclamp(p);
    task_type = get_task_type(p);
    if (is_orig_cpu_available(*best_cpu, load_avg, task_type))
        goto out;

    /*
     * Adjust lowest_mask
     * RT should not preempt UX task.
     */
    adjust_rt_lowest_mask(p, lowest_mask);
    if (cpumask_empty(lowest_mask)) {
        cpumask_copy(lowest_mask, &lowest_mask_backup);
        goto out;
    }

    /*
     * Select a best cpu for the rt task from lowest_mask.
     */
    *best_cpu = trans_find_lowest_rq(p, lowest_mask);

out:
    sc[1] = sched_clock();
    trace_android_rvh_find_lowest_rq_hook(p,
            lowest_mask_backup, *lowest_mask, best_cpu_backup, *best_cpu);
    trace_android_rvh_find_lowest_rq_hook_consume(p, sc[1]-sc[0]);
}


/**
 * stop_task - check whether a task is the stop task of a runqueue
 * @p: task to be checked
 * @rq: target runqueue
 *
 * Determine whether the given task @p is the stop task associated
 * with the specified runqueue @rq.
 *
 * Return: 1 if @p is the stop task of @rq, 0 otherwise.
 */
static inline int stop_task(struct task_struct *p, struct rq *rq)
{
    return p == rq->stop;
}

/**
 * trigger_tickpull_balance - decide whether to trigger tick pull balance
 * @rq: runqueue of the current CPU
 * @curr: currently running task on @rq
 *
 * Determine whether the current CPU should trigger a tick pull balance
 * operation to pull running/runnable tasks from other CPUs.
 *
 * Tick pull balance is only allowed on non-little CPUs that are online,
 * active, and not in halted or partially halted state.
 *
 * The balance operation is suppressed if the current CPU is already
 * occupied by critical tasks, including UX, RT, DL, or stop tasks, or
 * if there are runnable UX or RT tasks queued on the runqueue. This is
 * intended to avoid overloading a single CPU with multiple high-priority
 * or latency-sensitive tasks.
 *
 * Return: true if tick pull balance should be triggered, false otherwise.
 */
bool trigger_tickpull_balance(struct rq *rq, struct task_struct *curr)
{
    /* CPU hit by the tick interrupt */
    int cpu = rq->cpu;

    /* Only allow middle and big CPUs to trigger tick pull balance */
    if (is_little_cpu(cpu))
        return false;

    /*
     * Do not trigger if the CPU is unavailable, including being offline,
     * inactive, halted, or partially halted.
     */
    if (!cpu_online(cpu) || !cpu_active(cpu))
        return false;

    if (uas_cpu_halted(cpu) || uas_cpu_partial_halted(cpu))
        return false;

    /*
     * If the current CPU is already running a critical task (UX, RT,
     * DL, or stop task), avoid pulling additional tasks from other CPUs.
     *
     * Note:
     * is_ux_task() is used instead of is_ux_task_base(), meaning that
     * a UX task that has exceeded its runtime threshold on middle or
     * big CPUs is allowed to trigger pull balance.
     */
    if (is_ux_task(curr, NULL) ||
        is_rt_task(curr) ||
        dl_task(curr) ||
        stop_task(curr, rq))
        return false;

    /*
     * Balance must not be triggered if the current task is pipeline-related.
     */
    if (trans_is_pipeline_task(curr))
        return false;

    /*
     * Avoid pull balance if there are already runnable UX or RT tasks
     * queued on this CPU, to prevent stacking multiple critical tasks
     * on a single runqueue.
     */
    if (has_runnable_ux_tasks(cpu) || has_runnable_rt_tasks(cpu))
        return false;

    /* Just do it ! */
    return true;
}




/******** The following code is copied from fair.c ********/

enum fbq_type { regular, remote, all };

enum migration_type {
    migrate_load = 0,
    migrate_util,
    migrate_task,
    migrate_misfit
};

#define LBF_ALL_PINNED  0x01
#define LBF_NEED_BREAK  0x02
#define LBF_DST_PINNED  0x04
#define LBF_SOME_PINNED 0x08
#define LBF_ACTIVE_LB   0x10

struct lb_env {
    struct sched_domain *sd;

    struct rq       *src_rq;
    int         src_cpu;

    int         dst_cpu;
    struct rq       *dst_rq;

    struct cpumask      *dst_grpmask;
    int         new_dst_cpu;
    enum cpu_idle_type  idle;
    long            imbalance;
    /* The set of CPUs under consideration for load-balancing */
    struct cpumask      *cpus;

    unsigned int        flags;

    unsigned int        loop;
    unsigned int        loop_break;
    unsigned int        loop_max;

    enum fbq_type       fbq_type;
    enum migration_type migration_type;
    struct list_head    tasks;
    struct rq_flags     *src_rq_rf;
};

enum KTHREAD_BITS {
    KTHREAD_IS_PER_CPU = 0,
    KTHREAD_SHOULD_STOP,
    KTHREAD_SHOULD_PARK,
};

struct kthread {
    unsigned long flags;
    unsigned int cpu;
    int result;
    int (*threadfn)(void *);
    void *data;
    struct completion parked;
    struct completion exited;
#ifdef CONFIG_BLK_CGROUP
    struct cgroup_subsys_state *blkcg_css;
#endif
    /* To store the full name if task comm is truncated. */
    char *full_name;
};

/*
 * Variant of to_kthread() that doesn't assume @p is a kthread.
 *
 * Per construction; when:
 *
 *   (p->flags & PF_KTHREAD) && p->worker_private
 *
 * the task is both a kthread and struct kthread is persistent. However
 * PF_KTHREAD on it's own is not, kernel_thread() can exec() (See umh.c and
 * begin_new_exec()).
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
static inline struct kthread *__to_kthread(struct task_struct *p)
{
    void *kthread = p->worker_private;

    if (kthread && !(p->flags & PF_KTHREAD))
        kthread = NULL;
    return kthread;
}
#else
static inline struct kthread *__to_kthread(struct task_struct *p)
{
    void *kthread = (__force void *)p->set_child_tid;

    if (kthread && !(p->flags & PF_KTHREAD))
        kthread = NULL;
    return kthread;
}
#endif

bool kthread_is_per_cpu(struct task_struct *p)
{
    struct kthread *kthread = __to_kthread(p);

    if (!kthread)
        return false;

    return test_bit(KTHREAD_IS_PER_CPU, &kthread->flags);
}

/******** The above code is copied from fair.c ********/




/**
 * can_migrate_task - check whether a task is allowed to migrate to
 *                    a target CPU
 * @p: task being considered for migration
 * @cpu: destination CPU
 *
 * Perform basic feasibility checks to determine whether @p may be
 * migrated from the source runqueue to the destination CPU described
 * by @cpu.
 *
 * This function verifies that the task is not a per-CPU kthread, that
 * migration is not disabled for the task, and that the task is allowed
 * to run on the destination CPU according to its current CPU affinity.
 *
 * Both the task's cpus_mask and cpus_ptr are validated, as they may
 * temporarily diverge during affinity updates.
 *
 * Cache locality, load-balance heuristics, and whether the task is
 * currently running are intentionally not considered here.
 *
 * The caller must hold the source runqueue lock.
 *
 * Return: 1 if the task is allowed to migrate under the current
 * constraints, or 0 otherwise.
 */
static int can_migrate_task(struct task_struct *p, int cpu)
{
    /* Per-cpu kthreads must not be migrated. */
    if (kthread_is_per_cpu(p))
        return 0;

    /*
     * Check CPU affinity. Both cpus_mask and cpus_ptr are
     * validated as they may temporarily diverge during
     * affinity updates.
     */
    if (!cpumask_test_cpu(cpu, &p->cpus_mask))
        return 0;

    if (!cpumask_test_cpu(cpu, p->cpus_ptr))
        return 0;

    /* Respect migrate_disable(). */
    if (is_migration_disabled(p))
        return 0;

    /*
     * Note:
     * running and runnable tasks may be selected by design.
     * Cache locality is intentionally not considered here.
     */

    return 1;
}

/*
 * detach_task() -- detach the task for the migration specified in env
 */
static void detach_task(struct task_struct *p, struct lb_env *env)
{
    lockdep_assert_rq_held(env->src_rq);

    deactivate_task(env->src_rq, p, 0);
    set_task_cpu(p, env->dst_cpu);
}

/*
 * attach_task() -- attach the task detached by detach_task() to its new rq.
 */
static void attach_task(struct rq *rq, struct task_struct *p)
{
    lockdep_assert_rq_held(rq);

    WARN_ON(task_rq(p) != rq);
    activate_task(rq, p, 0);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
    wakeup_preempt(rq, p, 0);
#else
    check_preempt_curr(rq, p, 0);
#endif
}

/*
 * attach_one_task() -- attaches the task returned from detach_one_task() to
 * its new rq.
 */
static void attach_one_task(struct rq *rq, struct task_struct *p)
{
    struct rq_flags rf;

    rq_lock(rq, &rf);
    update_rq_clock(rq);
    attach_task(rq, p);
    rq_unlock(rq, &rf);
}

/**
 * trans_kick_active_balance - trigger active balance to migrate a running task
 * @rq: runqueue on which the task is currently running
 * @p: task to be actively migrated
 * @new_cpu: destination CPU for migration
 * @ux: whether the task is a UX or RT task
 *
 * Trigger an active balance operation to force the migration of the
 * currently running task @p from its current CPU to @new_cpu.
 *
 * This function sets up the active balance state on the runqueue and
 * records the migration request if no active balance is already in
 * progress. The migration type is recorded based on whether the task
 * is a UX or RT task.
 *
 * The operation is protected by the runqueue lock to ensure that only
 * one active balance request is issued at a time.
 *
 * Return: 1 if active balance is successfully triggered, 0 otherwise.
 */
int trans_kick_active_balance(struct rq *rq,
            struct task_struct *p, int new_cpu,
            bool ux)
{
    unsigned long flags;
    bool ret = false;
    struct trans_rq *trq = get_trans_rq(rq);

    /*
     * Invoke active balance to force migrate currently running task
     */
    raw_spin_lock_irqsave(trans_rq_lockp(rq), flags);
    if (!rq->active_balance) {
        rq->active_balance = 1;
        rq->push_cpu = new_cpu;

        /* Record the task to be migrated */
        trq->lb.pid = p->pid;
        trq->lb.mig_type = ux ? MIG_TASK_UX : MIG_TASK_RT;
        ret = true;
    }
    raw_spin_unlock_irqrestore(trans_rq_lockp(rq), flags);

    return ret;
}

/**
 * __detach_running_rt - detach an RT task for migration
 * @env: load balance environment describing the migration context
 * @target_pid: PID of the preferred task to detach
 *
 * Scan the RT pushable task list of the source runqueue and select
 * a task to detach for migration.
 *
 * The task recorded as most recently running on the source CPU, as
 * stored in the trans_rq load-balance state, is given priority and
 * will be selected if it is still present and allowed to migrate.
 *
 * If the recorded task cannot be found or is no longer migratable,
 * a fallback RT task that is eligible for migration may be selected
 * instead, so that the migration attempt can still make progress.
 *
 * The caller must hold the source runqueue lock.
 *
 * Return: the detached task if a suitable candidate was found, or
 * NULL if no migratable RT task was available.
 */
static struct task_struct *__detach_running_rt(struct lb_env *env,
            pid_t target_pid)
{
    struct rq *rq = env->src_rq;
    struct plist_head *head = &rq->rt.pushable_tasks;
    struct task_struct *p;
    struct task_struct *picked = NULL;
    struct task_struct *fallback = NULL;

    lockdep_assert_rq_held(env->src_rq);

    plist_for_each_entry(p, head, pushable_tasks) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        /* Skip delayed tasks */
        if (p->se.sched_delayed)
            continue;
#endif

        /*
         * The task may be in the middle of exit or teardown.
         * Skip non-running tasks to avoid pulling unstable tasks.
         */
        if (task_state(p) != TASK_RUNNING)
            continue;

        /*
         * The task may be migrating to another CPU.
         * Skip if the task is no longer bound to the source CPU.
         */
        if (task_cpu(p) != env->src_cpu)
            continue;

        /*
         * The task may be in the middle of dequeue or migration.
         * Skip tasks that are not queued on the runqueue.
         */
        if (!task_on_rq_queued(p))
            continue;

        /*
         * First priority: migrate the task that was recorded
         * as running on this CPU.
         */
        if (p->pid == target_pid) {
            if (can_migrate_task(p, env->dst_cpu)) {
                picked = p;
                goto out;
            }
            continue;
        }

        /*
         * Record a fallback candidate in case the recorded
         * RT task cannot be selected for migration.
         */
        if (!fallback && can_migrate_task(p, env->dst_cpu)
#ifndef CONFIG_LB_STRESS_TEST
            /*
             * Skip this check in stress-test mode to relax
             * constraints and increase issue reproduction
             * probability.
             */
            && check_sf_target(p)
#endif
            )
            fallback = p;
    }

    picked = fallback;

out:
    if (picked) {
        detach_task(picked, env);
        trace_migration_detach_task(env->src_cpu, env->dst_cpu, target_pid, picked);
    }

    return picked;
}


/**
 * __detach_running_ux - detach a UX task for migration
 * @env: load balance environment describing the migration context
 * @target_pid: PID of the preferred task to detach
 *
 * Scan the CFS task list of the source runqueue and select a UX task
 * to detach for migration.
 *
 * The task recorded as most recently running on the source CPU, as
 * stored in the trans_rq load-balance state, is given priority and
 * will be selected if it is still present and allowed to migrate.
 *
 * If the recorded task cannot be selected, a fallback UX task that
 * is eligible for migration may be chosen instead, so that the
 * migration attempt can still make progress.
 *
 * The caller must hold the source runqueue lock.
 *
 * Return: the detached task if a suitable candidate was found, or
 * NULL if no migratable UX task was available.
 */
static struct task_struct *__detach_running_ux(struct lb_env *env,
            pid_t target_pid)
{
    struct task_struct *p;
    struct task_struct *picked = NULL;
    struct task_struct *fallback = NULL;
    int src_cpu = env->src_cpu;

    lockdep_assert_rq_held(env->src_rq);

    list_for_each_entry_reverse(p,
            &env->src_rq->cfs_tasks, se.group_node) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        /* Skip delayed tasks */
        if (p->se.sched_delayed)
            continue;
#endif

        /*
         * The task may be in the middle of exit or teardown.
         * Skip non-running tasks to avoid pulling unstable tasks.
         */
        if (task_state(p) != TASK_RUNNING)
            continue;

        /*
         * The task may be migrating to another CPU.
         * Skip if the task is no longer bound to the source CPU.
         */
        if (task_cpu(p) != src_cpu)
            continue;

        /*
         * The task may be in the middle of dequeue or migration.
         * Skip tasks that are not queued on the runqueue.
         */
        if (!task_on_rq_queued(p))
            continue;

        /*
         * First priority: migrate the task that was recorded
         * as running on this CPU.
         */
        if (p->pid == target_pid) {
            if (can_migrate_task(p, env->dst_cpu)) {
                picked = p;
                goto out;
            }
            continue;
        }

        /*
         * Record a fallback candidate in case the recorded
         * UX task cannot be selected for migration.
         */
        if (!fallback &&
            is_ux_task_base(p, NULL) &&
            can_migrate_task(p, env->dst_cpu))
            fallback = p;
    }

    picked = fallback;

out:
    if (picked) {
        detach_task(picked, env);
        trace_migration_detach_task(env->src_cpu, env->dst_cpu, target_pid, picked);
    }

    return picked;
}



/**
 * trans_detach_running_task - detach the recorded running task for migration
 * @env: load balance environment describing the migration context
 *
 * Detach the task that was most recently running on the source CPU for
 * active balance or tick pull balance, based on the migration type
 * recorded in the trans_rq load-balance state.
 *
 * The actual detach operation is delegated to the corresponding helper
 * based on the migration type (e.g. UX or RT).
 *
 * The caller must hold the source runqueue lock.
 *
 * Return: detached task if successful, or NULL if no suitable task was
 * found or the migration type is unsupported.
 */
static struct task_struct *trans_detach_running_task(struct lb_env *env)
{
    struct trans_rq *trq = get_trans_rq(env->src_rq);
    enum migrate_task_type mig_type = trq->lb.mig_type;
    struct task_struct *p = NULL;

    switch (mig_type) {
    case MIG_TASK_UX:
        p = __detach_running_ux(env, trq->lb.pid);
        break;
    case MIG_TASK_RT:
        p = __detach_running_rt(env, trq->lb.pid);
        break;
    default:
        pr_warn("unsupported migration type: %d\n", mig_type);
        break;
    }

    return p;
}

enum migration_exit_reason {
    /* success or no early exit */
    EXIT_NONE = 0,

    /* No pending or invalid migration request */
    EXIT_NO_REQUEST,

    /* Source or destination CPU is not active */
    EXIT_CPU_INACTIVE,

    /* Higher-priority tasks exist on destination CPU */
    EXIT_DST_BUSY,

    /* Active balance state is no longer valid */
    EXIT_BALANCE_ABORTED,

    /* No eligible task to migrate */
    EXIT_NO_TASK,

    /* Failed to detach a migration candidate from the source runqueue */
    EXIT_DETACH_FAILED,

    /* No suitable scheduling domain available for migration */
    EXIT_NO_DOMAIN,
};

/**
 * trans_active_load_balance_cpu_stop - CPU stopper callback for active balance
 * @data: pointer to the source runqueue (struct rq)
 *
 * This function is executed by the CPU stopper to actively migrate the
 * task that was previously running on the source CPU to the destination
 * CPU specified by rq->push_cpu.
 *
 * The task to be migrated and its migration type are recorded in the
 * trans_rq load-balance state before the stopper is queued.
 *
 * Return: 0
 */
static int trans_active_load_balance_cpu_stop(void *data)
{
    struct rq *this_rq = data;
    int this_cpu = cpu_of(this_rq);
    int target_cpu = this_rq->push_cpu;
    struct rq *target_rq = cpu_rq(target_cpu);
    struct sched_domain *sd;
    struct task_struct *p = NULL;
    struct rq_flags rf;
    struct trans_rq *trq = get_trans_rq(this_rq);
    enum migration_exit_reason reason = EXIT_NONE;

    rq_lock_irq(this_rq, &rf);

    /*
     * Between queueing the stop-work and running it is a hole in which
     * CPUs can become inactive. We should not move tasks from or to
     * inactive CPUs.
     */
    if (!cpu_active(this_cpu) || !cpu_active(target_cpu)) {
        reason = EXIT_CPU_INACTIVE;
        goto out_unlock;
    }

    /* Make sure the requested CPU hasn't gone down in the meantime: */
    if (unlikely(this_cpu != smp_processor_id() ||
             !this_rq->active_balance)) {
        reason = EXIT_DST_BUSY;
        goto out_unlock;
    }

    /* Is there any task to move? */
    if (this_rq->nr_running <= 1) {
        reason = EXIT_NO_TASK;
        goto out_unlock;
    }

    /*
     * This condition is "impossible", if it occurs
     * we need to fix it. Originally reported by
     * Bjorn Helgaas on a 128-CPU setup.
     */
    WARN_ON(this_rq == target_rq);

    /* Search for an sd spanning us and the target CPU. */
    rcu_read_lock();
    for_each_domain(target_cpu, sd) {
        if (cpumask_test_cpu(this_cpu, sched_domain_span(sd)))
            break;
    }

    if (likely(sd)) {
        struct lb_env env = {
            .sd     = sd,
            .dst_cpu    = target_cpu,
            .dst_rq     = target_rq,
            .src_cpu    = this_rq->cpu,
            .src_rq     = this_rq,
            .idle       = CPU_IDLE,
            .flags      = LBF_ACTIVE_LB,
            .src_rq_rf  = &rf,
        };

        update_rq_clock(this_rq);

        /*
         * The default active balance logic may select a task that was
         * not most recently running on the CPU.
         *
         * For UX and RT active balance, explicitly detach the previously
         * running task recorded in the trans_rq load-balance state.
         */
        p = trans_detach_running_task(&env);
        if (p) {
            /* Active balancing done, reset the failure counter. */
            sd->nr_balance_failed = 0;
        } else {
            reason = EXIT_DETACH_FAILED;
        }
    } else {
        reason = EXIT_NO_DOMAIN;
    }
    rcu_read_unlock();

out_unlock:
    /*
     * NOTE:
     * The tasks to be migrated may have been enqueued to
     * the runqueue of other CPUs when the migration thread
     * is awakened for execution, but anyway, trq->lb.pid
     * and trq->lb.mig_type needs to be cleared here.
     */
    trq->lb.pid = INVALID_PID;
    trq->lb.mig_type = MIG_TASK_INVALID;

    this_rq->active_balance = 0;
    rq_unlock(this_rq, &rf);

    if (p)
        attach_one_task(target_rq, p);

    local_irq_enable();

    trace_migration_exit_reason((int)reason);
    return 0;
}

/**
 * __pull_running_task - pull a running task from a remote CPU via active balance
 * @dst_cpu: destination CPU to pull the task to (current CPU)
 * @ux: true to pull a running UX task, false to pull a running RT task
 *
 * Attempt to actively pull a currently running task from a remote source
 * CPU to @dst_cpu. The source CPUs are selected based on topology ordering,
 * and only CPUs that are online, active and not halted are considered.
 *
 * Depending on @ux, either a running UX task or a running RT task is
 * targeted. The task must be allowed to run on @dst_cpu and pass migration
 * constraints.
 *
 * This function must NOT be called with any rq->lock held.
 *
 * Return: true if a running task is successfully pulled, false otherwise.
 */
bool __pull_running_task(int dst_cpu, bool ux)
{
    struct task_struct *curr = NULL;
    cpumask_t search_cpus = CPU_MASK_NONE;
    int order_idx = -1, walk_cnt = -1, idx;
    struct rq *src_rq = NULL;
    int src_cpu;
    bool ret = false;

    struct trans_lb_env env = {
        .migr_type = TICKPULL_MIGR_RUNNING,
        .curr_cls  = trans_topology_cluster_id(dst_cpu),
    };

    /*
    * Calculate order_idx and walk_cnt.
    */
    ret = calc_order_idx(&env, &order_idx, &walk_cnt);
    if (!ret) {
        return false;
    }

    ret = false;
    for (idx = 0; idx < walk_cnt; idx++) {
        cpumask_copy(&search_cpus, &cpu_array[order_idx][idx]);

        for_each_cpu(src_cpu, &search_cpus) {
            if (src_cpu == dst_cpu)
                continue;

            src_rq = cpu_rq(src_cpu);
            curr = src_rq->curr;

            trace_walk_each_cpu(curr, search_cpus, src_cpu);

            /* Only pull tasks from online and active CPUs */
            if (!cpu_online(src_cpu) || !cpu_active(src_cpu))
                continue;

            /* Skip CPUs that are halted or partially halted */
            if (uas_cpu_halted(src_cpu) || uas_cpu_partial_halted(src_cpu))
                continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
            /* Skip delayed tasks */
            if (curr->se.sched_delayed)
                continue;
#endif

            /*
             * The current task may be in the middle of exit or teardown.
             * Skip non-running tasks to avoid pulling unstable tasks.
             */
            if (task_state(curr) != TASK_RUNNING)
                continue;

            /*
             * The task may be migrating to another CPU.
             * Skip if the task is no longer bound to the source CPU.
             */
            if (task_cpu(curr) != src_cpu)
                continue;

            /*
             * The task may be in the middle of dequeue or migration.
             * Skip tasks that are not queued on the runqueue.
             */
            if (!task_on_rq_queued(curr))
                continue;

            /*
             * Migration of pipeline-related threads is not allowed.
             */
            if (trans_is_pipeline_task(curr))
                continue;

            /*
             * UX path:
             * Ignore UX timeout state and only check whether
             * the current task is a UX task.
             *
             * UX tasks marked with UX_FLAG_CPU_L are restricted
             * to little CPUs and should not participate in
             * load balancing.
             */
            if (ux && (!is_ux_task_base(curr, NULL)
                || (get_ux_tags(curr) & UX_FLAG_CPU_L)))
                continue;

            /*
             * RT path:
             * Only consider currently running RT tasks.
             */
            if (!ux && !is_rt_task(curr)
#ifndef CONFIG_LB_STRESS_TEST
                /*
                 * Skip this check in stress-test mode to relax
                 * constraints and increase issue reproduction
                 * probability.
                 */
                && !check_sf_target(curr)
#endif
                )
                continue;

            /*
             * Check CPU affinity: the task must be allowed to
             * run on the destination CPU.
             */
            if (!cpumask_test_cpu(dst_cpu, &curr->cpus_mask))
                continue;

            if (!cpumask_test_cpu(dst_cpu, curr->cpus_ptr))
                continue;

            /*
             * Request active balance on the source runqueue to
             * migrate the currently running task.
             */
            if (!trans_kick_active_balance(src_rq, curr, dst_cpu, ux))
                continue;

            /*
             * Wake up the CPU stopper on the source CPU to
             * perform active balance and migrate the running
             * task to the destination CPU.
             *
             * NOTE:
             * stop_one_cpu_nowait() is used here to queue
             * the stop work asynchronously. The caller does not
             * wait for the stopper to run and must ensure that
             * active_balance_work remains valid until the stop
             * function starts executing.
             */
            ret = stop_one_cpu_nowait(
                    src_cpu,
                    trans_active_load_balance_cpu_stop,
                    src_rq,
                    &src_rq->active_balance_work);
            if (ret) {
                trace_pull_running_task(src_cpu, dst_cpu, curr);
                trace_show_tick_pull_running_task(src_cpu, curr->pid);
                return true;
            }
        }
    }
    return ret;
}

/**
 * pull_running_ux - pull a running UX task to the destination CPU
 * @dst_cpu: destination CPU to pull the running UX task to
 *
 * Wrapper around __pull_running_task() to actively pull a currently
 * running UX task from a remote CPU to @dst_cpu via active balance.
 *
 * This function must NOT be called with any rq->lock held.
 *
 * Return: true if a running UX task is successfully pulled,
 *         false otherwise.
 */
bool pull_running_ux(int dst_cpu)
{
    return __pull_running_task(dst_cpu, true);
}

/**
 * pull_running_rt - pull a running RT task to the destination CPU
 * @dst_cpu: destination CPU to pull the running RT task to
 *
 * Wrapper around __pull_running_task() to actively pull a currently
 * running RT task from a remote CPU to @dst_cpu via active balance.
 *
 * This function must NOT be called with any rq->lock held.
 *
 * Return: true if a running RT task is successfully pulled,
 *         false otherwise.
 */
bool pull_running_rt(int dst_cpu)
{
    return __pull_running_task(dst_cpu, false);
}

/**
 * trans_task_is_runnable - check whether a task is runnable but not currently running
 * @tsk: task to be checked
 *
 * Determine whether @tsk is in a runnable state and enqueued on a runqueue,
 * but not currently executing on a CPU.
 *
 * Return: true if the task is runnable and eligible to be scheduled,
 *         false otherwise.
 */
bool trans_task_is_runnable(struct task_struct *tsk)
{
    bool runnable;

    if (!tsk)
        return false;

    if (task_state(tsk) != TASK_RUNNING)
        return false;

    runnable = tsk->on_rq && !tsk->on_cpu;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    runnable = runnable && !tsk->se.sched_delayed;
#endif
    return runnable;
}

/**
 * __pick_runnable_ux - pick a runnable UX task from a source CPU
 * @src_cpu: source CPU from which to pick the UX task
 * @dst_cpu: destination CPU the task is intended to run on
 *
 * Iterate over the UX task list of @src_cpu and pick a UX task that is
 * in runnable state, enqueued on the source runqueue, and allowed to run
 * on @dst_cpu.
 *
 * UX timeout state is intentionally ignored so that UX tasks that have
 * timed out on small cores can still be migrated to big cores.
 *
 * Return: a runnable UX task if found, or NULL otherwise.
 */
struct task_struct *__pick_runnable_ux(int src_cpu, int dst_cpu)
{
    struct rq *src_rq = cpu_rq(src_cpu);
    struct task_struct *tsk = NULL;
    struct trans_rq *trq = NULL;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct trans_task_struct *trans_ts = NULL;

    /*
     * If there is no runnable UX task on this CPU, skip further
     * processing and return immediately.
     */
    trq = get_trans_rq(src_rq);
    if (is_ux_list_free(&trq->ux_list))
        return NULL;

    /*
     * Traverse the UX task list and select a runnable UX task.
     */
    list_for_each_safe(pos, next, &trq->ux_list) {
        trans_ts = container_of(pos, struct trans_task_struct, ux_node);
        if (unlikely(IS_ERR_OR_NULL(trans_ts)))
            continue;

        tsk = transts_to_ts(trans_ts);
        if (unlikely(IS_ERR_OR_NULL(tsk)))
            continue;

        /* Task must be in runnable state */
        if (!trans_task_is_runnable(tsk))
            continue;

        /*
         * UX tasks marked with UX_FLAG_CPU_L are restricted
         * to little CPUs and should not participate in
         * load balancing.
         */
        if (get_ux_tags(tsk) & UX_FLAG_CPU_L)
            continue;

        /*
         * Do not migrate pipeline-related threads.
         */
        if (trans_is_pipeline_task(tsk))
            continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        /* Skip delayed tasks */
        if (tsk->se.sched_delayed)
            continue;
#endif

        /* Task must still be enqueued on the source runqueue */
        if (!task_on_rq_queued(tsk))
            continue;

        /* Task must belong to the source CPU */
        if (unlikely(task_cpu(tsk) != src_cpu))
            continue;

        /*
         * Check CPU affinity: the task must be allowed to
         * run on the destination CPU.
         */
        if (!cpumask_test_cpu(dst_cpu, &tsk->cpus_mask))
            continue;

        if (!cpumask_test_cpu(dst_cpu, tsk->cpus_ptr))
            continue;

        /*
         * NOTE:
         * Ignore UX timeout state so that UX tasks that have
         * timed out on small cores can still be migrated to
         * big cores.
         */
        if (unlikely(!is_ux_task_base(tsk, NULL)))
            continue;

        /*
         * TODO:
         * It may be more reasonable to select the UX task with
         * the longest timeout among multiple runnable UX tasks.
         */

        return tsk;
    }

    return NULL;
}

/**
 * __pick_runnable_rt - pick a runnable RT task from a source CPU
 * @src_cpu: source CPU from which to pick the RT task
 * @dst_cpu: destination CPU the task is intended to run on
 *
 * Iterate over the list of pushable RT tasks on @src_cpu and select
 * a task that is in runnable state and allowed to run on @dst_cpu.
 *
 * Only RT tasks that are enqueued, not currently running, and pass
 * migration constraints are considered.
 *
 * Return: a runnable RT task if found, or NULL otherwise.
 */
struct task_struct *__pick_runnable_rt(int src_cpu, int dst_cpu)
{
    struct rq *src_rq = cpu_rq(src_cpu);
    struct plist_head *head = &src_rq->rt.pushable_tasks;
    struct task_struct *tsk = NULL;

    /*
     * Fast-path check: if there is no runnable RT task on this CPU,
     * skip further processing.
     */
    if (!has_runnable_rt_tasks(src_cpu))
        return NULL;

    /*
     * Iterate over all pushable RT tasks on the source CPU and
     * pick a runnable one.
     */
    plist_for_each_entry(tsk, head, pushable_tasks) {
        /* Task must be in runnable state */
        if (!trans_task_is_runnable(tsk))
            continue;

#ifndef CONFIG_LB_STRESS_TEST
        /*
         * Skip this check in stress-test mode to relax constraints
         * and increase issue reproduction probability.
         */
        if (!check_sf_target(tsk))
            continue;
#endif

        /*
         * tasks marked with UX_FLAG_CPU_L are restricted
         * to little CPUs and should not participate in
         * load balancing.
         */
        if (get_ux_tags(tsk) & UX_FLAG_CPU_L)
            continue;

        /*
         * Do not migrate pipeline-related threads.
         */
        if (trans_is_pipeline_task(tsk))
            continue;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        /* Skip delayed tasks */
        if (tsk->se.sched_delayed)
            continue;
#endif

        /* Task must still be enqueued on the source runqueue */
        if (!task_on_rq_queued(tsk))
            continue;

        /* Task must belong to the source CPU */
        if (unlikely(task_cpu(tsk) != src_cpu))
            continue;

        /*
         * Check CPU affinity: the RT task must be allowed to
         * run on the destination CPU.
         */
        if (!cpumask_test_cpu(dst_cpu, &tsk->cpus_mask))
            continue;

        if (!cpumask_test_cpu(dst_cpu, tsk->cpus_ptr))
            continue;

        return tsk;
    }

    return NULL;
}

/**
 * __pull_runnable_task - pull a runnable task from a remote CPU
 * @dst_cpu: destination CPU to pull the runnable task to
 * @ux: true to pull a runnable UX task, false to pull a runnable RT task
 *
 * Attempt to pull a runnable (but not currently running) task from a
 * remote source CPU to @dst_cpu. Source CPUs are selected based on
 * topology ordering, and only online, active and non-halted CPUs are
 * considered.
 *
 * Depending on @ux, either a runnable UX task or a runnable RT task
 * is selected and migrated under the source runqueue lock.
 *
 * Return: true if a runnable task is successfully pulled, false otherwise.
 */
bool __pull_runnable_task(int dst_cpu, bool ux)
{
    struct task_struct *tsk  = NULL;
    cpumask_t search_cpus = CPU_MASK_NONE;
    struct rq *src_rq = NULL;
    struct rq *dst_rq = NULL;
    struct rq_flags rf;
    int order_idx = -1, walk_cnt = -1, idx;
    int src_cpu;
    bool ret = false;

    struct trans_lb_env env = {
        .migr_type = TICKPULL_MIGR_RUNNABLE,
        .curr_cls  = trans_topology_cluster_id(dst_cpu),
    };

    /*
    * Calculate order_idx and walk_cnt.
    */
    ret = calc_order_idx(&env, &order_idx, &walk_cnt);
    if (!ret) {
        return false;
    }

    for (idx = 0; idx < walk_cnt; idx++) {
        cpumask_copy(&search_cpus, &cpu_array[order_idx][idx]);

        for_each_cpu(src_cpu, &search_cpus) {
            if (src_cpu == dst_cpu)
                continue;

            src_rq = cpu_rq(src_cpu);
            dst_rq = cpu_rq(dst_cpu);

            trace_walk_each_cpu(current, search_cpus, src_cpu);

            /* Only pull tasks from online and active CPUs */
            if (!cpu_online(src_cpu) || !cpu_active(src_cpu))
                continue;

            /* Skip CPUs that are halted or partially halted */
            if (uas_cpu_halted(src_cpu) || uas_cpu_partial_halted(src_cpu))
                continue;

            /*
             * Lock the source runqueue to safely pick and
             * detach a runnable task.
             */
            rq_lock(src_rq, &rf);

            /*
             * Pick a runnable UX or RT task from the source CPU.
             */
            tsk = ux ? __pick_runnable_ux(src_cpu, dst_cpu)
                     : __pick_runnable_rt(src_cpu, dst_cpu);
            if (!tsk) {
                rq_unlock(src_rq, &rf);
                return false;
            }

            /*
             * Dequeue the runnable task from the source runqueue
             * and update its CPU affinity to the destination CPU.
             */
            deactivate_task(src_rq, tsk, 0);
            set_task_cpu(tsk, dst_cpu);
            rq_unlock(src_rq, &rf);

            /*
             * NOTE:
             * A CPU that has entered the idle state may be selected
             * in tick_balance, so you need to actively call preempt_curr
             * to send an ipi interrupt to wake it up.
             */
            attach_one_task(dst_rq, tsk);
            trace_pull_runnable_task(src_cpu, dst_cpu, tsk);
            trace_show_tick_pull_runnable_task(dst_cpu, tsk->pid);

            return true;
        }
    }

    return false;
}

/**
 * pull_runnable_ux - pull a runnable UX task to the destination CPU
 * @dst_cpu: destination CPU to pull the runnable UX task to
 *
 * Wrapper around __pull_runnable_task() to actively pull a runnable
 * (but not currently running) UX task from a remote CPU to @dst_cpu.
 *
 * This function must NOT be called with any rq->lock held.
 *
 * Return: true if a runnable UX task is successfully pulled,
 *         false otherwise.
 */
bool pull_runnable_ux(int dst_cpu)
{
    return __pull_runnable_task(dst_cpu, true);
}

/**
 * pull_runnable_rt - pull a runnable RT task to the destination CPU
 * @dst_cpu: destination CPU to pull the runnable RT task to
 *
 * Wrapper around __pull_runnable_task() to actively pull a runnable
 * (but not currently running) RT task from a remote CPU to @dst_cpu.
 *
 * This function must NOT be called with any rq->lock held.
 *
 * Return: true if a runnable RT task is successfully pulled,
 *         false otherwise.
 */
bool pull_runnable_rt(int dst_cpu)
{
    return __pull_runnable_task(dst_cpu, false);
}

/**
 * struct lb_statistic - load balance related statistics
 *
 * This structure records load balance statistics for tick-based
 * balance and newidle balance paths. All fields are atomic to allow
 * lockless updates from different CPUs.
 */
struct lb_statistic {
    /*
     * Statistics related to tick-based load balance.
     */
    atomic64_t tick_hit;
    atomic64_t tick_pull_running_ux;
    atomic64_t tick_pull_runnable_ux;
    atomic64_t tick_pull_running_rt;
    atomic64_t tick_pull_runnable_rt;
    atomic64_t tick_fail;

    /*
     * Statistics related to newidle balance.
     */
    atomic64_t newidle_hit;
    atomic64_t newidle_pull_runnable_ux;
    atomic64_t newidle_pull_runnable_rt;
    atomic64_t newidle_fail;

    /*
     * Statistics related to irq work balance.
     */
    atomic64_t irqwb_hit;
    atomic64_t irqwb_succ;
    atomic64_t irqwb_fail;
};
struct lb_statistic lb_stat;

enum pull_path {
    PULL_NONE = 0,
    TICK_PULL_RUNNING_UX = 1,
    TICK_PULL_RUNNABLE_UX = 2,
    TICK_PULL_RUNNING_RT = 3,
    TICK_PULL_RUNNABLE_RT = 4,

    NEWIDLE_PULL_RUNNABLE_UX = 5,
    NEWIDLE_PULL_RUNNABLE_RT = 6,
};

/**
 * __trans_tick_balance - perform tick-based active load balance
 * @data: unused private data
 * @rq: runqueue of the current CPU
 *
 * Perform tick-based load balance on the current CPU. When triggered,
 * this function attempts to pull critical tasks from remote CPUs in
 * the following priority order:
 *
 *   - running UX task
 *   - runnable UX task
 *   - running RT task
 *   - runnable RT task
 *
 * Each successful pull updates the corresponding statistic and stops
 * further attempts.
 *
 * Return: true if a task is successfully pulled, false otherwise.
 */
bool __trans_tick_balance(void *data, struct rq *rq)
{
    struct task_struct *curr = rq->curr;
    int this_cpu = rq->cpu;
    bool trigger = false;
    bool ret = false;
    enum pull_path path = PULL_NONE;
    u64 sc[2];

    if (!check_uas_feature(FEATURE_LOADBALANCE))
        return false;

    /*
     * Check whether tick-based pull balance should be triggered
     * on this runqueue.
     */
    trigger = trigger_tickpull_balance(rq, curr);
    if (!trigger)
        return false;

    sc[0] = sched_clock();
    atomic64_inc(&lb_stat.tick_hit);

    /*
     * Step 1: pull a currently running UX task.
     */
    if (pull_running_ux(this_cpu)) {
        ret = true;
        path = TICK_PULL_RUNNING_UX;
        atomic64_inc(&lb_stat.tick_pull_running_ux);
        goto out;
    }

    /*
     * Step 2: pull a runnable UX task.
     */
    if (pull_runnable_ux(this_cpu)) {
        ret = true;
        path = TICK_PULL_RUNNABLE_UX;
        atomic64_inc(&lb_stat.tick_pull_runnable_ux);
        goto out;
    }

    /*
     * Step 3: pull a currently running RT task.
     */
    if (pull_running_rt(this_cpu)) {
        ret = true;
        path = TICK_PULL_RUNNING_RT;
        atomic64_inc(&lb_stat.tick_pull_running_rt);
        goto out;
    }

    /*
     * Step 4: pull a runnable RT task.
     */
    if (pull_runnable_rt(this_cpu)) {
        ret = true;
        path = TICK_PULL_RUNNABLE_RT;
        atomic64_inc(&lb_stat.tick_pull_runnable_rt);
        goto out;
    }

    /*
     * Tick balance was triggered but no task was pulled.
     *
     * NOTE:
     * This is expected in most cases since UX and RT tasks
     * typically account for only a small fraction of all tasks.
     */
    atomic64_inc(&lb_stat.tick_fail);

out:
    sc[1] = sched_clock();

    trace_tick_pull_consume(ret, this_cpu, sc[1]-sc[0]);
    trace_tickpull_critical_task(rq, curr, trigger, path);
    return ret;
}

__maybe_unused void trans_tick_balance(void *data, struct rq *rq)
{
    __trans_tick_balance(data, rq);
}

/**
 * __newidle_pull_runnable_ux - pull a runnable UX task in newidle balance
 * @this_rq: runqueue of the destination (idle) CPU
 * @rf: unused rq_flags pointer (kept for interface consistency)
 * @pulled_task: set to 1 if a task is pulled
 * @done: set to 1 to stop further balance attempts
 * @order_idx: topology order index for CPU traversal
 * @walk_cnt: number of topology levels to walk
 *
 * Attempt to pull a runnable UX task from a remote CPU when the
 * destination CPU enters the idle state (newidle balance).
 *
 * The function prioritizes fast response of UX tasks and minimizes
 * lock contention by performing as many early-exit checks as possible
 * before acquiring both runqueue locks.
 *
 * Return: true if a runnable UX task is successfully pulled, false otherwise.
 */
static bool __newidle_pull_runnable_ux(
            struct rq *this_rq, struct rq_flags *rf,
            int *pulled_task, int *done,
            int order_idx, int walk_cnt)
{
    cpumask_t search_cpus = CPU_MASK_NONE;
    struct rq *src_rq = NULL;
    struct task_struct *ux_task = NULL;
    int this_cpu = cpu_of(this_rq);
    int src_cpu = -1;
    int idx;

    /*
     * Walk CPUs based on topology order and look for a runnable
     * UX task to pull.
     */
    for (idx = 0; idx < walk_cnt; idx++) {
        cpumask_copy(&search_cpus, &cpu_array[order_idx][idx]);

        for_each_cpu(src_cpu, &search_cpus) {
            src_rq = cpu_rq(src_cpu);

            /* Cannot migrate a task from the same CPU */
            if (src_cpu == this_cpu)
                continue;

            /*
             * Fast-path check without holding rq locks.
             * Skip if there is no UX task on this CPU.
             */
            if (!has_ux_tasks(src_cpu))
                continue;

            /* Only pull tasks from online and active CPUs */
            if (!cpu_online(src_cpu) || !cpu_active(src_cpu))
                continue;

            /* Skip CPUs that are halted or partially halted */
            if (uas_cpu_halted(src_cpu) || uas_cpu_partial_halted(src_cpu))
                continue;

            /*
             * double_lock_balance() acquires both runqueue
             * locks and is relatively expensive. Perform as
             * many early-exit checks as possible before
             * acquiring the locks to reduce contention.
             */
            double_lock_balance(this_rq, src_rq);

            /*
             * Re-check after taking locks, since the fast
             * check above was performed without synchronization.
             */
            if (!has_ux_tasks(src_cpu)) {
                double_unlock_balance(this_rq, src_rq);
                continue;
            }

            /*
             * Select a runnable UX task, focusing on fast
             * response in the newidle path.
             */
            ux_task = __pick_runnable_ux(src_cpu, this_cpu);
            if (!ux_task) {
                double_unlock_balance(this_rq, src_rq);
                continue;
            }

            /*
             * Migrate the runnable UX task to the idle CPU.
             */
            deactivate_task(src_rq, ux_task, 0);
            set_task_cpu(ux_task, this_cpu);
            activate_task(this_rq, ux_task, 0);

            /*
             * Only pull one task at a time.
             */
            *pulled_task = 1;
            *done = 1;

            /*
             * reset the idle time stamp if we pulled any task.
             */
            this_rq->idle_stamp = 0;

            double_unlock_balance(this_rq, src_rq);

            trace_newidle_pull_runnable_ux(ux_task, src_cpu, this_cpu);
            trace_show_newidle_pull_runnable_ux(src_cpu, ux_task->pid);

            return true;
        }
    }
    return false;
}

/**
 * __newidle_pull_runnable_rt - pull a runnable RT task in newidle balance
 * @this_rq: runqueue of the destination (idle) CPU
 * @rf: unused rq_flags pointer (kept for interface consistency)
 * @pulled_task: set to 1 if a task is pulled
 * @done: set to 1 to stop further balance attempts
 * @order_idx: topology order index for CPU traversal
 * @walk_cnt: number of topology levels to walk
 *
 * Attempt to pull a runnable RT task from a remote CPU when the
 * destination CPU enters the idle state (newidle balance).
 *
 * In contrast to UX tasks, the newidle RT balance focuses on improving
 * overall throughput. Therefore, only the runnable state and priority
 * of the RT task are considered, and any runnable-time or timeout
 * semantics are intentionally ignored.
 *
 * Return: true if a runnable RT task is successfully pulled, false otherwise.
 */
bool __newidle_pull_runnable_rt(
            struct rq *this_rq, struct rq_flags *rf,
            int *pulled_task, int *done,
            int order_idx, int walk_cnt)
{
    cpumask_t search_cpus = CPU_MASK_NONE;
    struct rq *src_rq = NULL;
    struct task_struct *rt_task = NULL;
    int this_cpu = cpu_of(this_rq);
    int src_cpu = -1;
    int idx;

    /*
     * Do not pull RT tasks if the destination CPU already has
     * a runnable RT task.
     */
    if (sched_rt_runnable(this_rq))
        return false;

    /*
     * Walk CPUs based on topology order and search for a runnable
     * RT task to pull.
     */
    for (idx = 0; idx < walk_cnt; idx++) {
        cpumask_copy(&search_cpus, &cpu_array[order_idx][idx]);

        for_each_cpu(src_cpu, &search_cpus) {
            src_rq = cpu_rq(src_cpu);

            /* Cannot migrate a task from the same CPU */
            if (src_cpu == this_cpu)
                continue;

            /*
             * Fast-path check without holding rq locks.
             * Skip if there is no runnable RT task on this CPU.
             */
            if (!has_runnable_rt_tasks(src_cpu))
                continue;

            /* Only pull tasks from online and active CPUs */
            if (!cpu_online(src_cpu) || !cpu_active(src_cpu))
                continue;

            /* Skip CPUs that are halted or partially halted */
            if (uas_cpu_halted(src_cpu) || uas_cpu_partial_halted(src_cpu))
                continue;

            /*
             * double_lock_balance() acquires both runqueue
             * locks and is relatively expensive. Perform as
             * many early-exit checks as possible before
             * acquiring the locks to reduce contention.
             */
            double_lock_balance(this_rq, src_rq);

            /*
             * Re-check after taking locks: the destination CPU
             * may have become non-idle and gained a runnable RT
             * task while waiting for the locks.
             */
            if (sched_rt_runnable(this_rq)) {
                double_unlock_balance(this_rq, src_rq);
                return false;
            }

            /*
             * Re-check the source runqueue after taking locks.
             */
            if (!has_runnable_rt_tasks(src_cpu)) {
                double_unlock_balance(this_rq, src_rq);
                continue;
            }

            /*
             * Pick the highest-priority runnable RT task that
             * can be migrated to this CPU.
             */
            rt_task = __pick_runnable_rt(src_cpu, this_cpu);
            if (!rt_task) {
                double_unlock_balance(this_rq, src_rq);
                continue;
            }

            /*
             * Migrate the runnable RT task to the idle CPU.
             */
            deactivate_task(src_rq, rt_task, 0);
            set_task_cpu(rt_task, this_cpu);
            activate_task(this_rq, rt_task, 0);

            /* Only pull one task per newidle balance */
            *pulled_task = 1;
            *done = 1;

            /* Reset idle timestamp after pulling a task */
            this_rq->idle_stamp = 0;


            double_unlock_balance(this_rq, src_rq);

            trace_newidle_pull_runnable_rt(rt_task, src_cpu, this_cpu);
            trace_show_newidle_pull_runnable_rt(src_cpu, rt_task->pid);

            return true;
        }
    }
    return false;
}

/**
 * __newidle_pull_task - perform newidle runnable task balance
 * @this_rq: runqueue of the destination (idle) CPU
 * @rf: rq_flags passed from caller
 * @pulled_task: indicates whether a task is pulled or runnable work exists
 * @done: indicates whether newidle balance should stop
 *
 * Perform newidle balance when the CPU becomes idle. This function
 * attempts to pull runnable tasks from remote CPUs in the following
 * order:
 *
 *   - runnable UX task (response-oriented)
 *   - runnable RT task (throughput-oriented)
 *
 * Even if no task is pulled from remote CPUs, the balance may still
 * be considered successful if runnable tasks are enqueued locally
 * by other active balance paths.
 *
 * Return: true if newidle balance is considered handled, false otherwise.
 */
static bool __newidle_pull_task(struct rq *this_rq,
                    struct rq_flags *rf, int *pulled_task, int *done)
{
    int this_cpu = cpu_of(this_rq);
    int order_idx = -1, walk_cnt = -1;
    bool ret = false;
    enum pull_path path = PULL_NONE;
    u64 sc[2];

    struct trans_lb_env env = {
        .migr_type = NEWIDLE_MIGR_RUNNABLE,
        .curr_cls  = trans_topology_cluster_id(this_cpu),
    };

    /*
     * Calculate topology traversal order for newidle balance.
     */
    if (!calc_order_idx(&env, &order_idx, &walk_cnt))
        return false;

    sc[0] = sched_clock();
    atomic64_inc(&lb_stat.newidle_hit);

    /*
     * Step 1: pull a runnable UX task.
     *
     * In the newidle path, UX tasks are prioritized to improve
     * responsiveness.
     */
    if (__newidle_pull_runnable_ux(this_rq, rf,
                pulled_task, done, order_idx, walk_cnt)) {
        ret = true;
        path = NEWIDLE_PULL_RUNNABLE_UX;
        atomic64_inc(&lb_stat.newidle_pull_runnable_ux);
        goto out;
    }

    /*
     * Step 2: pull a runnable RT task.
     *
     * This step focuses on throughput rather than response latency.
     */
    if (__newidle_pull_runnable_rt(this_rq, rf,
                pulled_task, done, order_idx, walk_cnt)) {
        ret = true;
        path = NEWIDLE_PULL_RUNNABLE_RT;
        atomic64_inc(&lb_stat.newidle_pull_runnable_rt);
        goto out;
    }

    atomic64_inc(&lb_stat.newidle_fail);

out:
    sc[1] = sched_clock();
    trace_newidle_pull_consume(ret, this_cpu, sc[1]-sc[0]);

    /*
     * WARNING:
     * Although we may failed to pull tasks from other CPUs through
     * newidle balance. But there is a possibility that other cpus
     * have successfully enqueue a cfs task to the runqueue of
     * this_cpu through the active balance logic. In this case,
     * the newidle balance is considered successful, and the variable
     * 'done' needs to be set to true. Because only in this way, the
     * logic of 'goto again' in pick_next_task_fair will be executed,
     * and a task will be re-selected from the runqueue of this_cpu
     * as next_task. Otherwise, the newly migrated task will be in
     * the runnable state and will not get a chance to run for a
     * long time.
     */
    if (this_rq->cfs.h_nr_running && !*pulled_task)
        *pulled_task = 1;

    /*
     * Is there a task of a high priority class?
     */
    if (this_rq->nr_running != this_rq->cfs.h_nr_running)
        *pulled_task = -1;

    if (*pulled_task)
        this_rq->idle_stamp = 0;

    if (*pulled_task != 0)
        *done = 1;

    /*
     * WARNING:
     * Skip the newidle logic of QCOM or MTK and pick the higher
     * priority task directly when there is a higher priority task.
     *
     * For example:
     * the following logic exists in Qualcomm's walt_newidle_balance
     * function (implemented in kernel\sched\walt\walt_lb.c).
     * The pulled_task variable will be cleared here. If this_cpu is
     * inactive at this time, the walt_newidle_balance function will
     * return directly, which results in The RETRY_TASK logic will
     * not be executed in pick_next_task_fair. This will causes high
     * priority tasks to fail to run.
     *
     *  * newly idle load balance is completely handled here, so
     *  * set done to skip the load balance by the caller.
     *
     * *done = 1;
     * *pulled_task = 0;
     *
     *
     *  * This CPU is about to enter idle, so clear the
     *  * misfit_task_load and mark the idle stamp.
     *
     * this_rq->misfit_task_load = 0;
     * this_rq->idle_stamp = rq_clock(this_rq);
     *
     * if (!cpu_active(this_cpu))
     *   return;
     *
     * Note:
     * The execution logic in the pause_cpus function (implemented in
     * \kernel\cpu.c), will first mark the cpu as inactive, and then
     * wake up the migration thread. This will also cause the active
     * status judgment result of this_cpu in walt_newidle_balance to
     * be false. However, there may be a high-priority migration task
     * in the runqueue of this_cpu.
     *
     *  for_each_cpu(cpu, cpus)
     *  set_cpu_active(cpu, false);
     *  err = __pause_drain_rq(cpus);
     */
    if (*done != 0)
        ret = true;

    return ret;
}

/**
 * __trans_newidle_balance - perform newidle load balance
 * @data: unused private data
 * @this_rq: runqueue of the destination CPU
 * @rf: rq_flags passed from the caller
 * @pulled_task: indicates whether a task is pulled or runnable work exists
 * @done: indicates whether newidle balance should stop
 *
 * Perform newidle balance when the current CPU becomes idle. The function
 * applies a series of fast-path checks to determine whether newidle balance
 * is necessary and safe, and then attempts to pull runnable tasks from
 * remote CPUs.
 *
 * Return: true if newidle balance is considered handled, false otherwise.
 */
bool __trans_newidle_balance(void *data, struct rq *this_rq,
                    struct rq_flags *rf, int *pulled_task, int *done)
{
    int this_cpu = cpu_of(this_rq);
    struct root_domain *rd = cpu_rq(this_cpu)->rd;
    bool ret = false;

    if (!check_uas_feature(FEATURE_LOADBALANCE))
        return false;

    /*
     * Do not pull tasks towards CPUs that are not available.
     */
    if (!cpu_online(this_cpu) || !cpu_active(this_cpu))
        return false;

    /*
     * Tasks must not be placed on a halted or partially halted CPU.
     */
    if (uas_cpu_halted(this_cpu) || uas_cpu_partial_halted(this_cpu))
        return false;

    /*
     * If there are runnable tasks on this CPU, newidle balance
     * is unnecessary. nr_running includes CFS, RT and DL tasks.
     */
    if (this_rq->nr_running)
        return false;

    /*
     * Skip newidle balance if no CPU in the root domain is overloaded.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
    if (!READ_ONCE(rd->overloaded))
        return false;
#else
    if (!READ_ONCE(rd->overload))
        return false;
#endif

    /*
     * TODO:
     * Consider adding granularity control to limit how frequently
     * newidle balance is triggered, in order to reduce overhead.
     */

    /*
     * Set idle_stamp _before_ calling newidle balance so that the
     * time spent in balance is accounted as idle time.
     */
    this_rq->idle_stamp = rq_clock(this_rq);

    /*
     * Temporarily unpin the rq lock before entering newidle balance.
     *
     * See commit 46f69fa33712a for background and rationale.
     */
    rq_unpin_lock(this_rq, rf);

    /*
     * Attempt to pull runnable tasks from other CPUs.
     */
    ret = __newidle_pull_task(this_rq, rf, pulled_task, done);

    /*
     * Re-pin the rq lock before returning to the caller.
     */
    rq_repin_lock(this_rq, rf);

    return ret;
}

__maybe_unused void trans_newidle_balance(void *data, struct rq *this_rq,
                    struct rq_flags *rf, int *pulled_task, int *done)
{
    __trans_newidle_balance(data, this_rq, rf, pulled_task, done);
}

/*************************** irq_work balance ***************************/

static struct irq_work irqwb_work;


/**
 * __irqwb_detach_task - pick and detach a task for irq_work-based migration
 * @env: load-balance environment describing the migration context
 *
 * Select a task from the source runqueue to be migrated by irq_work-based
 * active balancing.
 *
 * Despite the function name, the selected task is not limited to the
 * currently running task. The candidate may be either running or runnable.
 * However, a task that is actually running on the source CPU is preferred
 * and will be selected first if it is eligible for migration.
 *
 * The function first attempts to detach a preferred UX task, followed by
 * an RT task. The preferred task is identified by a PID recorded by the
 * irq_work balancer; if that task cannot be migrated, a suitable fallback
 * task may be selected instead.
 *
 * Return:
 *   the detached task on success, or NULL if no migratable task was found
 */
static struct task_struct *__irqwb_detach_task(struct lb_env *env)
{
    struct trans_rq *trq = get_trans_rq(env->src_rq);
    pid_t prefer_pid = trq->lb.irqwb_pid;
    struct task_struct *p = NULL;

    p = __detach_running_ux(env, prefer_pid);
    if (p)
        goto out;

    p = __detach_running_rt(env, prefer_pid);

out:
    if (p)
        trace_irqwb_detach_task(env->src_cpu, env->dst_cpu, prefer_pid, p);

    return p;
}

/**
 * irqwb_cpu_stop_migrate - migrate a critical task via irq_work balance
 * @data: source runqueue pointer (struct rq *) of the calling CPU
 *
 * This function is executed in CPU stop context to carry out irq_work-based
 * active balancing. It checks whether there is a pending request to migrate
 * a critical task from the current CPU to a target CPU, typically a bigger
 * core.
 *
 * The migration candidate may be in either running or runnable state, and
 * includes both UX and RT tasks. The balancer gives priority to a preferred
 * task selected by the irq_work logic; if that task cannot be migrated, a
 * suitable fallback task may be chosen instead.
 *
 * To ensure correctness, the function validates CPU activity and runqueue
 * state before attempting migration. The irq_work balance state is cleared
 * unconditionally to avoid reusing stale requests, regardless of whether
 * migration succeeds.
 *
 * Return:
 *   0 - migration attempt completed; success or failure is reported via
 *       tracepoints and statistics counters
 */
static int irqwb_cpu_stop_migrate(void *data)
{
    struct rq *this_rq = data;
    struct trans_rq *trq = get_trans_rq(this_rq);
    int this_cpu = cpu_of(this_rq);
    int target_cpu = trq->lb.irqwb_push_cpu;
    struct rq *target_rq = NULL;
    struct task_struct *p = NULL;
    struct rq_flags rf;
    enum migration_exit_reason reason = EXIT_NONE;
    struct lb_env env = {
        .src_cpu    = this_cpu,
        .src_rq     = this_rq,
    };
    u64 sc[2];

    sc[0] = sched_clock();
    atomic64_inc(&lb_stat.irqwb_hit);

    /* No pending irq_work balance, nothing to do */
    if (!is_valid_cpu(target_cpu)) {
        reason = EXIT_NO_REQUEST;
        goto out;
    }

    target_rq = cpu_rq(target_cpu);
    env.dst_cpu = target_cpu;
    env.dst_rq = target_rq;

    rq_lock_irq(this_rq, &rf);

    /*
     * Between queueing the stop-work and running it is a hole in which
     * CPUs can become inactive. We should not move tasks from or to
     * inactive CPUs.
     */
    if (!cpu_active(this_cpu) || !cpu_active(target_cpu)) {
        reason = EXIT_CPU_INACTIVE;
        goto out_unlock;
    }

    /*
     * Re-check whether higher-priority tasks (UX or RT) have been
     * enqueued on the target CPU. Between queueing and executing
     * the migration stop-thread, a higher-priority task may have
     * been scheduled on the target CPU. Continuing the balance
     * operation in such a case could violate priority or mutual
     * exclusion guarantees.
     */
    if (has_runnable_ux_tasks(target_cpu) ||
        has_runnable_rt_tasks(target_cpu)) {
        reason = EXIT_DST_BUSY;
        goto out_unlock;
    }

    /* Make sure the requested CPU hasn't gone down in the meantime: */
    if (unlikely(this_cpu != smp_processor_id() ||
             !this_rq->active_balance)) {
        reason = EXIT_BALANCE_ABORTED;
        goto out_unlock;
    }

    /*
     * At least one task must remain on the source runqueue;
     * otherwise there is no candidate to migrate.
     */
    if (this_rq->nr_running <= 1) {
        reason = EXIT_NO_TASK;
        goto out_unlock;
    }

    /*
     * Pick and detach a migration candidate. The selected task may
     * be either running or runnable.
     */
    p = __irqwb_detach_task(&env);
    if (!p)
        reason = EXIT_DETACH_FAILED;

out_unlock:
    /*
     * NOTE:
     * The tasks to be migrated may have been enqueued to
     * the runqueue of other CPUs when the migration thread
     * is awakened for execution, but anyway, trq->lb.pid
     * and trq->lb.irqwb_push_cpu needs to be cleared here.
     */
    trq->lb.irqwb_pid = INVALID_PID;
    trq->lb.irqwb_push_cpu = INVALID_CPU;

    this_rq->active_balance = 0;
    rq_unlock(this_rq, &rf);

    /*
     * Attach the migrated task to the destination runqueue after
     * releasing the source runqueue lock.
     */
    if (p)
        attach_one_task(target_rq, p);

    local_irq_enable();
    trace_irqwb_migration_exit_reason((int)reason);

out:
    if (p) {
        atomic64_inc(&lb_stat.irqwb_succ);
    } else {
        atomic64_inc(&lb_stat.irqwb_fail);
    }

    sc[1] = sched_clock();
    trace_irqwb_consume(!!p, this_cpu, sc[1]-sc[0]);

    return 0;
}

/**
 * trigger_irqwb - Determine whether irq_work-based balancing should be triggered
 * @src_cpu: source CPU from which a task may be pushed
 * @dst_cpu: destination CPU to which a task may be migrated
 *
 * This helper evaluates whether it is worthwhile to initiate an irq_work-based
 * active balance operation from @src_cpu to @dst_cpu.
 *
 * The decision is based on the following conditions:
 *   - The source runqueue must have more than one runnable task; otherwise,
 *     there is no migration candidate.
 *   - The source runqueue must contain at least one high-priority task (UX or RT)
 *     that may be pushed away.
 *   - No active balance operation should already be in progress on @src_cpu.
 *   - The destination CPU must not already have runnable high-priority tasks
 *     (UX or RT), in order to avoid violating priority or mutual exclusion
 *     guarantees.
 *
 * This function performs a lightweight, lockless check. Callers are expected
 * to revalidate all conditions under proper runqueue locking before performing
 * the actual migration.
 *
 * Return:
 *   true  - irq_work-based balancing should be triggered
 *   false - balancing should not be triggered
 */
bool trigger_irqwb(int src_cpu, int dst_cpu)
{
    struct rq *src_rq = cpu_rq(src_cpu);

    /* No task to migrate */
    if (src_rq->nr_running <= 1)
        return false;

    /* No UX or RT tasks to push */
    if (!has_ux_tasks(src_cpu) && !has_rt_tasks(src_cpu))
        return false;

    /* Active balance already requested */
    if (src_rq->active_balance)
        return false;

    /*
     * Do not trigger irq_work-based balancing if runnable UX or
     * RT tasks already exist on the destination CPU, as migrating
     * tasks in this case could violate mutual exclusion or
     * priority guarantees.
     */
    if (has_runnable_ux_tasks(dst_cpu) ||
        has_runnable_rt_tasks(dst_cpu))
        return false;

    return true;
}

/**
 * irqwb_workfn - irq_work callback to initiate active load balancing
 * @irq_work: irq_work structure queued on the target CPU
 *
 * This function is executed in irq_work context on the target CPU.
 * Its purpose is to initiate irq_work-based active balancing by
 * waking up the per-CPU migration thread.
 *
 * The function performs a double-check of trigger conditions:
 *   - A lockless check is done first to avoid unnecessary locking
 *     when balancing is clearly not needed.
 *   - The conditions are then revalidated under the runqueue lock
 *     to ensure correctness before setting the active_balance flag.
 *
 * Once active balancing is marked in progress, the per-CPU migration
 * thread is woken up asynchronously via stop_one_cpu_nowait(), which
 * will perform the actual push operation.
 *
 * This function must be lightweight and non-blocking, as it runs in
 * interrupt context.
 */
static void irqwb_workfn(struct irq_work *irq_work)
{
    int this_cpu = smp_processor_id();
    struct rq *this_rq = cpu_rq(this_cpu);
    struct trans_rq *trq = get_trans_rq(this_rq);
    int target_cpu = trq->lb.irqwb_push_cpu;
    struct rq *target_rq = NULL;
    bool ret = false;

    /* No pending irq_work balance, nothing to do */
    if (!is_valid_cpu(target_cpu))
        goto out;

    target_rq = cpu_rq(target_cpu);

    /*
     * First check without taking the runqueue lock to avoid
     * unnecessary locking when balancing is not needed.
     */
    if (!trigger_irqwb(this_cpu, target_cpu))
        goto out;

    /*
     * Recheck under runqueue lock to avoid races with concurrent
     * state changes before initiating active balancing.
     *
     * Consider whether both the source and destination
     * runqueue locks should be held here to further narrow the
     * race window.
     */
    double_rq_lock(this_rq, target_rq);
    if (!trigger_irqwb(this_cpu, target_cpu)) {
        double_rq_unlock(this_rq, target_rq);
        goto out;
    }

    this_rq->active_balance = 1;
    double_rq_unlock(this_rq, target_rq);

    /*
     * Wake up the per-CPU migration thread to perform
     * the actual push-based load balancing.
     */
    ret = stop_one_cpu_nowait(
            this_cpu,
            irqwb_cpu_stop_migrate,
            this_rq,
            &this_rq->active_balance_work);

out:
    trace_irqwb_workfn(this_cpu, ret, trq->lb.irqwb_pid, trq->lb.irqwb_push_cpu);

    if (!ret) {
        trq->lb.irqwb_pid = INVALID_PID;
        trq->lb.irqwb_push_cpu = INVALID_CPU;
    }

    return;
}

/**
 * wakeup_irqwb - Request irq_work-based load balance on a remote CPU
 * @prev: task previously running on the local CPU
 * @next: task selected to run next on the local CPU
 * @this_rq: runqueue of the local CPU
 *
 * This function is invoked from the context switch path on a big CPU.
 * When the local CPU transitions from executing a high-priority task
 * (UX or RT) to a normal task, it indicates that high-performance CPU
 * resources are being released.
 *
 * In this case, the local CPU proactively scans little CPUs to check
 * whether any of them are overloaded with runnable or running UX/RT
 * tasks. If such a CPU is found, an irq_work is queued on that remote
 * CPU to wake up its migration thread, which will then push eligible
 * tasks away.
 *
 * The scan of remote CPUs is performed without holding their runqueue
 * locks and is therefore best-effort. The actual migration decision
 * is revalidated under proper locking in the remote CPU's migration
 * context.
 */
void wakeup_irqwb(struct task_struct *prev,
            struct task_struct *next,
            struct rq *this_rq)
{
    struct rq *rq = NULL;
    struct trans_rq *trq = NULL;
    struct task_struct *curr = NULL;
    struct cpumask little_mask = { CPU_BITS_NONE };
    int this_cpu = cpu_of(this_rq);
    int cpu, target_cpu = -1;
    bool is_ux, is_sf;
    static atomic_t last_cpu_rr = ATOMIC_INIT(0);
    int start, nr_little;
    u32 ux_tags;

    if (!check_uas_feature(FEATURE_LOADBALANCE))
        return;

    /* Only trigger irq_work balance when releasing big CPU resources */
    if (is_little_cpu(this_cpu))
        return;

    /*
     * Only trigger irq_work-based balancing from CPUs that are online,
     * active, and not halted by platform power management.
     */
    if (!cpu_online(this_cpu) || !cpu_active(this_cpu))
        return;

    if (uas_cpu_halted(this_cpu) || uas_cpu_partial_halted(this_cpu))
        return;

    /*
     * If the local runqueue still has runnable UX or RT tasks,
     * do not initiate remote balancing.
     */
    if (has_runnable_ux_tasks(this_cpu) || has_runnable_rt_tasks(this_cpu))
        return;

    /*
     * A resource release is defined as a transition from executing
     * a UX or RT task to a non-critical task.
     *
     * Note:
     * is_ux_task() is used instead of is_ux_task_base(), allowing
     * UX tasks that have exceeded their runtime threshold on big
     * CPUs to still trigger irq_work balance.
     */
    if (!is_ux_task(prev, NULL) && !is_rt_task(prev))
        return;

    /*
     * If the next task is a critical one (UX, RT, DL, or stop),
     * avoid initiating irq_work-based balancing.
     */
    if (is_ux_task(next, NULL) ||
        is_rt_task(next) ||
        dl_task(next) ||
        stop_task(next, this_rq))
        return;

    /*
     * Scan little CPUs to find a candidate CPU that is currently
     * running or hosting runnable UX or RT tasks.
     *
     * This scan is performed without holding the remote runqueue
     * lock and serves only as a heuristic hint.
     */
    get_little_cpu_mask(&little_mask);
    nr_little = cpumask_weight(&little_mask);
    if (!nr_little)
        return;

    /*
     * Round-robin selection:
     * Pick a rotating start index to avoid all callers scanning
     * little CPUs from the same position, reducing contention
     * and repeated disturbances on a single CPU.
     */
    start = atomic_fetch_inc(&last_cpu_rr);
    start %= nr_little;

    /*
     * Note:
     * The following logic performs checks on the task currently
     * running on the target CPU. This does not imply that irq_work-based
     * balancing will only migrate running tasks.
     *
     * The purpose of this check is purely heuristic: to ensure that there
     * exists at least one task on the remote CPU that is eligible to run
     * on the local CPU. Only under this condition will an IPI be issued
     * to wake up the migration thread and attempt a push operation.
     *
     * If the critical task currently running on the remote CPU is bound
     * to that CPU due to affinity constraints and cannot run on the local
     * CPU, waking up the migration thread is pointless.
     *
     * Worse still, repeatedly waking the migration thread in such cases
     * may frequently interrupt the execution of the critical task,
     * leading to unnecessary scheduling disturbances and potential
     * performance degradation.
     */
    for_each_cpu_wrap(cpu, &little_mask, start) {

        rq = cpu_rq(cpu);
        trq = get_trans_rq(rq);
        curr = rq->curr;

        /* Skip if there is already a pending irq_work push request */
        if (trq->lb.irqwb_pid > 0)
            continue;

        is_ux = is_ux_task_base(curr, &ux_tags);

#ifdef CONFIG_LB_STRESS_TEST
        /*
         * Skip this check in stress-test mode to relax
         * constraints and increase issue reproduction
         * probability.
         */
        is_sf = is_rt_task(curr);
#else
        is_sf = is_rt_task(curr) && check_sf_target(curr);
#endif

        /* Skip tasks that are neither UX nor SF RT */
        if (!is_ux && !is_sf)
            continue;

        /* Skip tasks marked with UX_FLAG_CPU_L. */
        if (ux_tags & UX_FLAG_CPU_L)
            continue;

        /*
         * Skip tasks that are exiting. Tasks in PF_EXITING state
         * are not eligible for migration or load balancing.
         */
        if (READ_ONCE(curr->flags) & PF_EXITING)
            continue;

        /*
         * Defensive check: rq->curr is expected to be TASK_RUNNING,
         * but keep this check to guard against transient states.
         */
        if (task_state(curr) != TASK_RUNNING)
            continue;

        /*
         * Check whether the task currently running on the remote CPU
         * is allowed to migrate and execute on the local CPU.
         */
        if (!can_migrate_task(curr, this_cpu))
            continue;

        /* Aha! Got it! */
        target_cpu = cpu;

        /* Record the target CPU for task pushing in advance */
        trq->lb.irqwb_pid = curr->pid;
        trq->lb.irqwb_push_cpu = this_cpu;
        break;
    }

    if (target_cpu < 0)
        return;

    /*
     * Queue irq_work on the target CPU to wake up its migration
     * thread, which will perform the actual push balance.
     */
    irq_work_queue_on(&irqwb_work, target_cpu);
    trace_wakeup_irqwb(this_cpu, prev, next, target_cpu);
    trace_show_wakeup_irqwb(this_cpu, prev->pid);
}

void init_irqwb(void)
{
    init_irq_work(&irqwb_work, irqwb_workfn);
}

/**
 * proc_lb_stat_write - reset load balance statistics
 * @file: proc file
 * @buf: user buffer
 * @count: number of bytes written
 * @ppos: file position
 *
 * Writing any value to this proc entry clears all load balance
 * statistics maintained in @lb_stat.
 *
 * Return: number of bytes consumed on success, or -EFAULT on error.
 */
static ssize_t proc_lb_stat_write(struct file *file,
            const char __user *buf,
            size_t count, loff_t *ppos)
{
    char buffer[8] = {0};

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    /* Clear all load balance statistics */
    memset(&lb_stat, 0, sizeof(struct lb_statistic));

    return count;
}

/**
 * proc_lb_stat_read - dump load balance statistics
 * @file: proc file
 * @buf: user buffer
 * @count: number of bytes to read
 * @ppos: file position
 *
 * Dump tick-based and newidle load balance statistics in a
 * human-readable, fixed-width format.
 *
 * Return: number of bytes read on success.
 */
static ssize_t proc_lb_stat_read(struct file *file, char __user *buf,
        size_t count, loff_t *ppos)
{
    size_t len = 0;
    char buffer[1024];

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_hit:                    %10llu\n",
        atomic64_read(&lb_stat.tick_hit));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_pull_running_ux:        %10llu\n",
        atomic64_read(&lb_stat.tick_pull_running_ux));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_pull_runnable_ux:       %10llu\n",
        atomic64_read(&lb_stat.tick_pull_runnable_ux));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_pull_running_rt:        %10llu\n",
        atomic64_read(&lb_stat.tick_pull_running_rt));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_pull_runnable_rt:       %10llu\n",
        atomic64_read(&lb_stat.tick_pull_runnable_rt));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "tick_fail:                   %10llu\n",
        atomic64_read(&lb_stat.tick_fail));

    len += snprintf(buffer + len, sizeof(buffer) - len, "\n");

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "newidle_hit:                 %10llu\n",
        atomic64_read(&lb_stat.newidle_hit));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "newidle_pull_runnable_ux:    %10llu\n",
        atomic64_read(&lb_stat.newidle_pull_runnable_ux));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "newidle_pull_runnable_rt:    %10llu\n",
        atomic64_read(&lb_stat.newidle_pull_runnable_rt));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "newidle_fail:                %10llu\n",
        atomic64_read(&lb_stat.newidle_fail));

    len += snprintf(buffer + len, sizeof(buffer) - len, "\n");

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "irqwb_hit:                   %10llu\n",
        atomic64_read(&lb_stat.irqwb_hit));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "irqwb_succ:                  %10llu\n",
        atomic64_read(&lb_stat.irqwb_succ));

    len += snprintf(buffer + len, sizeof(buffer) - len,
        "irqwb_fail:                  %10llu\n",
        atomic64_read(&lb_stat.irqwb_fail));

    len += snprintf(buffer + len, sizeof(buffer) - len, "\n");

    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 136)
static const struct proc_ops proc_lb_stat_operations = {
    .proc_write = proc_lb_stat_write,
    .proc_read = proc_lb_stat_read,
};
#else
static const struct file_operations proc_lb_stat_operations = {
    .write = proc_lb_stat_write,
    .read  = proc_lb_stat_read,
};
#endif

struct proc_dir_entry *lb_stat_node = NULL;

struct proc_dir_entry *trans_lb_stat_proc_init(
            struct proc_dir_entry *pde)
{
    lb_stat_node = proc_create("lb_stat", 0666, pde, &proc_lb_stat_operations);
    if (!lb_stat_node) {
        trans_err("create /proc/trans_scheduler/lb_stat failed\n");
        goto err;
    }

    return lb_stat_node;
err:
    return NULL;
}

void trans_lb_stat_proc_deinit(void)
{
    if (lb_stat_node) {
        proc_remove(lb_stat_node);
        lb_stat_node = NULL;
    }
}

/*
 * Tracks CPUs that have been halted by vendor-specific modules
 * (e.g., MTK, QCOM, SPRD).
 */
struct cpumask *vendor_cpu_halt_mask = NULL;
struct cpumask *vendor_cpu_partial_halt_mask = NULL;
unsigned int *vendor_sched_boost_type_ptr = NULL;

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

typedef bool (*is_pipeline_task_t)(struct task_struct *tsk);
is_pipeline_task_t __vendor_is_pipeline_task = NULL;

__nocfi bool trans_is_pipeline_task(struct task_struct *tsk)
{
    if (!__vendor_is_pipeline_task)
        return false;

    return __vendor_is_pipeline_task(tsk);
}

bool trans_is_pipeline_cpu(int cpu)
{
    struct rq *rq = cpu_rq(cpu);

    if (trans_is_pipeline_task(rq->curr))
        return true;

    return false;
}

/**
 * trans_register_kprobe - Initialize pointers to vendor CPU halt masks.
 *
 * This function uses kprobes to dynamically locate the address of
 * kallsyms_lookup_name(), then uses it to resolve the addresses of
 * vendor-specific CPU halt masks (__cpu_halt_mask and
 * __cpu_partial_halt_mask). These masks can then be used to query
 * the halt state of a CPU at runtime.
 */
__nocfi void trans_register_kprobe(void)
{
    int ret;
    kallsyms_lookup_name_t __kallsyms_lookup_name;
    struct kprobe kp = {
        .symbol_name = "kallsyms_lookup_name"
    };

    /*
     * Register kprobe to find the address of kallsyms_lookup_name().
     * Once we have the address, unregister the kprobe immediately.
     */
    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_info("[UAS] register kallsyms_lookup_name failed! ret=%d\n", ret);
        return;
    }
    __kallsyms_lookup_name = (kallsyms_lookup_name_t) kp.addr;
    unregister_kprobe(&kp);

    /*
     * Use kallsyms_lookup_name() to resolve addresses of
     * __cpu_halt_mask and __cpu_partial_halt_mask.
     */
    vendor_cpu_halt_mask =
        (struct cpumask *) __kallsyms_lookup_name("__cpu_halt_mask");
    if (!vendor_cpu_halt_mask) {
        pr_info("[UAS] lookup_name __cpu_halt_mask failed!\n");
        return;
    }

    vendor_cpu_partial_halt_mask =
        (struct cpumask *) __kallsyms_lookup_name("__cpu_partial_halt_mask");
    if (!vendor_cpu_partial_halt_mask) {
        pr_info("[UAS] lookup_name __cpu_partial_halt_mask failed!\n");
        return;
    }

    vendor_sched_boost_type_ptr =
        (int *) __kallsyms_lookup_name("sched_boost_type");
    if (!vendor_sched_boost_type_ptr) {
        pr_info("[UAS] lookup_name sched_boost_type failed!\n");
    }

    __vendor_is_pipeline_task =
        (is_pipeline_task_t) __kallsyms_lookup_name("is_pipeline_task");
    if (!__vendor_is_pipeline_task) {
        pr_info("[UAS] lookup_name is_pipeline_task failed!\n");
    }

#ifdef DEBUG_UAS
    pr_info("[UAS] vendor_cpu_halt_mask: ptr=0x%llx, mask=[%*pbl] "
            "vendor_cpu_partial_halt_mask: ptr=0x%llx, mask=[%*pbl] "
            "vendor_sched_boost_type_ptr: ptr=0x%llx, val=%d "
            "__vendor_is_pipeline_task: ptr=0x%llx\n",
            (u64) vendor_cpu_halt_mask,
            cpumask_pr_args(vendor_cpu_halt_mask),
            (u64) vendor_cpu_partial_halt_mask,
            cpumask_pr_args(vendor_cpu_partial_halt_mask),
            (u64) vendor_sched_boost_type_ptr,
            *vendor_sched_boost_type_ptr,
            (u64) __vendor_is_pipeline_task
    );
#endif

    return;
}

/**
 * trans_unregister_kprobe - Clear vendor CPU halt mask pointers.
 *
 * This is typically called on module unload to ensure the global
 * pointers are NULL and not used accidentally.
 */
void trans_unregister_kprobe(void)
{
    vendor_cpu_halt_mask = NULL;
    vendor_cpu_partial_halt_mask = NULL;
    vendor_sched_boost_type_ptr = NULL;
    __vendor_is_pipeline_task = NULL;
}

