#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <uapi/asm-generic/errno-base.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/rwsem.h>
#include <trace/events/task.h>
#endif

#include "sched_common.h"
#include "trans_fair.h"
#include "trans_proc.h"
#include "sched_main.h"
#include "trans_binder.h"
#include "trans_mutex.h"
#include "trans_rwsem.h"
#include "trans_sched_info.h"
#include "trans_target.h"
#include "trans_balance.h"
#include "trans_locking.h"
#include "trans_futex.h"
#include "trans_workqueue.h"
#include "trans_xmu.h"


#if IS_ENABLED(CONFIG_TRANSSION_BLK_UAS)
#include "low_latency_sched_interface.h"
#endif

#include <trans_sched_interface.h>

#define TRANS_MODNAME      "trans_sched"

u32 g_sched_scene = 0;
int g_trans_sched_debug = 0;
int g_trans_sched_enable = 0;

DEFINE_MUTEX(g_mutex_sched_state);
DEFINE_MUTEX(g_mutex_ux_scene);
struct ux_sched_domains g_ux_sched_domains;

DEFINE_STATIC_KEY_ARRAY_FALSE(uas_static_keys, __UAS_FEAT_NR);

/*
 * Set the UAS feature state and sync the per-feature static keys.
 * g_trans_sched_enable stays the authoritative bitmask (userspace ABI);
 * keys are toggled only on actual transitions so their use counts stay
 * balanced.
 */
void set_uas_feature_state(int state)
{
    int old_state, i;

    mutex_lock(&g_mutex_sched_state);
    old_state = g_trans_sched_enable;
    g_trans_sched_enable = state;
    for (i = 0; i < __UAS_FEAT_NR; i++) {
        int bit = 1U << i;
        bool was_on = (old_state & bit) == bit;
        bool now_on = (state & bit) == bit;

        if (now_on == was_on)
            continue;
        if (now_on)
            static_branch_enable(&uas_static_keys[i]);
        else
            static_branch_disable(&uas_static_keys[i]);
    }
    mutex_unlock(&g_mutex_sched_state);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static void trans_hook_init(void)
{
    int ret;

    ret = register_trace_android_rvh_sched_fork(android_rvh_sched_fork_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_sched_fork failed!\n");
    }

    ret = register_trace_android_vh_build_sched_domains(android_vh_build_sched_domains_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_build_sched_domains failed!\n");
    }

    ret = register_trace_android_vh_scheduler_tick(android_vh_scheduler_tick_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_scheduler_tick failed!\n");
    }

    /*
     * In theory, the hook implementation below should be integrated into the
     * chip vendor’s hook. However, implementing this change would require a
     * considerable amount of work.
     * Therefore, the hook will not be registered on the QCOM platform for
     * now; for other platforms, whether to make such changes will be decided
     * later based on actual needs.
     */
#if (!IS_ENABLED(CONFIG_PLATFORM_QCOM))
    ret = register_trace_android_rvh_replace_next_task_fair(android_rvh_replace_next_task_fair_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_replace_next_task_fair failed!\n");
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    ret = register_trace_android_rvh_check_preempt_wakeup(android_rvh_check_preempt_wakeup_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_check_preempt_wakeup failed!\n");
    }
#else
    ret = register_trace_android_rvh_check_preempt_wakeup_fair(android_rvh_check_preempt_wakeup_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_check_preempt_wakeup failed!\n");
    }
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0) */
#endif /* CONFIG_PLATFORM_QCOM */

#if (IS_ENABLED(CONFIG_UNISOC_SCHED))
    ret = register_trace_android_rvh_enqueue_task(android_rvh_enqueue_task_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_enqueue_task failed!\n");
    }

    ret = register_trace_android_rvh_dequeue_task(android_rvh_dequeue_task_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_dequeue_task failed!\n");
    }

#endif /* CONFIG_UNISOC_SCHED */

    ret = register_trace_sched_stat_blocked(sched_stat_blocked_hook, NULL);
    if (ret) {
        trans_warn("register trace_sched_stat_blocked failed!\n");
    }

    ret = register_trace_android_vh_mutex_wait_start(android_vh_mutex_wait_start_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_mutex_wait_start failed!\n");
    }

    ret = register_trace_android_vh_record_mutex_lock_starttime(android_vh_record_mutex_lock_starttime_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_record_mutex_lock_starttime failed!\n");
    }

#if (!IS_ENABLED(CONFIG_PLATFORM_QCOM))
    /*
     * Currently, the following hook is registered only on Qualcomm platforms.
     * Therefore, it should not be registered again on Qualcomm platforms.
     */
    ret = register_trace_android_vh_alter_mutex_list_add(android_vh_alter_mutex_list_add_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_alter_mutex_list_add failed!\n");
    }
#endif /* CONFIG_PLATFORM_QCOM */

    ret = register_trace_android_vh_mutex_can_spin_on_owner(android_vh_mutex_can_spin_on_owner_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_mutex_can_spin_on_owner failed!\n");
    }

    ret = register_trace_android_vh_rwsem_can_spin_on_owner(android_vh_rwsem_can_spin_on_owner_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_rwsem_can_spin_on_owner failed!\n");
    }

    ret = register_trace_android_vh_rwsem_wake(android_vh_rwsem_wake_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_rwsem_wake failed!\n");
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    ret = register_trace_android_vh_record_rwsem_lock_starttime(android_vh_record_rwsem_lock_starttime_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_record_rwsem_lock_starttime failed!\n");
    }
#else
    ret = register_trace_android_vh_rwsem_up_read_end(android_vh_rwsem_up_read_end_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_rwsem_up_read_end failed!\n");
    }

    ret = register_trace_android_vh_rwsem_up_write_end(android_vh_rwsem_up_write_end_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_rwsem_up_write_end failed!\n");
    }
#endif

#if (!IS_ENABLED(CONFIG_MTK_TASK_TURBO))
    ret = register_trace_android_vh_alter_rwsem_list_add(android_vh_alter_rwsem_list_add_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_vh_alter_rwsem_list_add failed!\n");
    }
#endif

    ret = register_trace_task_rename(task_rename_hook, NULL);
    if (ret) {
        trans_warn("register trace_task_rename failed!\n");
    }

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 15, 0)
    /*
     * As the SDK evolves, vendor projects currently only avoid using
     * this hook in kernel 5.10 builds. Therefore, it is valid to
     * register the following rvh-type hook only for kernel-5.10
     * projects. For other kernel versions, this hook must NOT be
     * registered to prevent conflicts.
     */
    ret = register_trace_android_rvh_find_lowest_rq(android_rvh_find_lowest_rq_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_find_lowest_rq failed!\n");
    }
#endif

    ret = register_trace_prio_sched_switch(sched_switch_next_hook, NULL, 9);
    if (ret) {
        trans_warn("register trace_prio_sched_switch next failed!\n");
    }

    ret = register_trace_prio_sched_switch(sched_switch_prev_hook, NULL, 11);
    if (ret) {
        trans_warn("register trace_prio_sched_switch prev failed!\n");
    }

    ret = register_trace_android_rvh_schedule(android_rvh_schedule_hook, NULL);
    if (ret) {
        trans_warn("register trace_android_rvh_schedule failed!\n");
    }

    ret = register_trace_sched_switch(trans_sched_switch, NULL);
    if (ret != 0) {
        trans_warn("register trans_sched_switch failed!\n");
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    ret = register_trace_android_rvh_dequeue_entity_delayed(android_rvh_dequeue_entity_delayed_hook, NULL);
    if (ret) {
        trans_warn("register android_rvh_dequeue_entity_delayed failed!\n");
    }
#endif

    register_trans_locking_hook();
    binder_hook_init();
    futex_hook_init();

    init_irqwb();
}
#else
static void trans_hook_init(void)
{
    return;
}
#endif

static void register_trans_sched_interface(void)
{
    g_trans_sched_interface.android_rvh_enqueue_task_hook = &android_rvh_enqueue_task_hook;
    g_trans_sched_interface.android_rvh_dequeue_task_hook = &android_rvh_dequeue_task_hook;
    g_trans_sched_interface.android_rvh_sched_fork_hook = &android_rvh_sched_fork_hook;
    g_trans_sched_interface.android_vh_build_sched_domains_hook = &android_vh_build_sched_domains_hook;
    g_trans_sched_interface.android_vh_scheduler_tick_hook = &android_vh_scheduler_tick_hook;
    g_trans_sched_interface.android_rvh_replace_next_task_fair_hook = &android_rvh_replace_next_task_fair_hook;
    g_trans_sched_interface.android_rvh_check_preempt_wakeup_hook = &android_rvh_check_preempt_wakeup_hook;
    g_trans_sched_interface.android_vh_binder_restore_priority_hook = &android_vh_binder_restore_priority_hook;
    g_trans_sched_interface.android_vh_binder_wait_for_work_hook = &android_vh_binder_wait_for_work_hook;
    g_trans_sched_interface.android_vh_sync_txn_recvd_hook = &android_vh_sync_txn_recvd_hook;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    g_trans_sched_interface.android_vh_binder_proc_transaction_finish_hook = &android_vh_binder_proc_transaction_finish_hook;
#else
    g_trans_sched_interface.android_vh_binder_proc_transaction_end_hook = &android_vh_binder_proc_transaction_end_hook;
#endif /* KERNEL_VERSION(5, 15, 0) */
    g_trans_sched_interface.android_vh_mutex_wait_start_hook = &android_vh_mutex_wait_start_hook;
    g_trans_sched_interface.android_vh_mutex_unlock_slowpath_hook = &android_vh_mutex_unlock_slowpath_hook;
    g_trans_sched_interface.android_vh_alter_mutex_list_add_hook = &android_vh_alter_mutex_list_add_hook;
    g_trans_sched_interface.android_vh_rwsem_wake_hook = &android_vh_rwsem_wake_hook;
    g_trans_sched_interface.android_vh_rwsem_wake_finish_hook = &android_vh_rwsem_wake_finish_hook;
    g_trans_sched_interface.android_vh_alter_rwsem_list_add_hook = &android_vh_alter_rwsem_list_add_hook;
    g_trans_sched_interface.android_vh_free_task_hook = &android_vh_free_task_hook;
    g_trans_sched_interface.task_rename_hook = &task_rename_hook;
    g_trans_sched_interface.sched_switch_prev_hook = &sched_switch_prev_hook;
    g_trans_sched_interface.sched_switch_next_hook = &sched_switch_next_hook;
    g_trans_sched_interface.check_ux_task = &tran_is_ux_task;
    g_trans_sched_interface.balance_on_wakeup_ux = &balance_on_wakeup_ux;
    g_trans_sched_interface.trans_should_honor_sync = &trans_should_honor_sync;
    g_trans_sched_interface.android_rvh_find_lowest_rq_hook = &android_rvh_find_lowest_rq_hook;
    g_trans_sched_interface.adjust_rt_lowest_mask = &adjust_rt_lowest_mask;
    g_trans_sched_interface.filterout_on_wakeup_rt = &filterout_on_wakeup_rt;
    g_trans_sched_interface.trans_select_task_rq_rt = &trans_select_task_rq_rt;
    g_trans_sched_interface.android_vh_scheduler_tick = &__trans_tick_balance;
    g_trans_sched_interface.android_rvh_sched_newidle_balance = &__trans_newidle_balance;
    g_trans_sched_interface.schedule = &xmu_schedule;
    g_trans_sched_interface.scheduler_tick_entry = &xmu_tick_entry;
    g_trans_sched_interface.xmu_cpufreq_limit = &xmu_cpufreq_limit;
    g_trans_sched_interface.is_runnable_boost_enabled = &xmu_is_runnable_boost_enabled;
    g_trans_sched_interface.xmu_cpufreq_set = &xmu_cpufreq_set;
}

static void unregister_trans_sched_interface(void)
{
    g_trans_sched_interface.android_rvh_enqueue_task_hook = NULL;
    g_trans_sched_interface.android_rvh_dequeue_task_hook = NULL;
    g_trans_sched_interface.android_rvh_sched_fork_hook = NULL;
    g_trans_sched_interface.android_vh_build_sched_domains_hook = NULL;
    g_trans_sched_interface.android_vh_scheduler_tick_hook = NULL;
    g_trans_sched_interface.android_rvh_replace_next_task_fair_hook = NULL;
    g_trans_sched_interface.android_rvh_check_preempt_wakeup_hook = NULL;
#if IS_ENABLED(CONFIG_TRANSSION_BLK_UAS)
    tran_low_latency_sched_interface.check_ux_task = NULL;
#endif
    g_trans_sched_interface.android_vh_binder_restore_priority_hook = NULL;
    g_trans_sched_interface.android_vh_binder_wait_for_work_hook = NULL;
    g_trans_sched_interface.android_vh_sync_txn_recvd_hook = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    g_trans_sched_interface.android_vh_binder_proc_transaction_finish_hook = NULL;
#else
    g_trans_sched_interface.android_vh_binder_proc_transaction_end_hook = NULL;
#endif /* KERNEL_VERSION(5, 15, 0) */
    g_trans_sched_interface.android_vh_mutex_wait_start_hook = NULL;
    g_trans_sched_interface.android_vh_mutex_unlock_slowpath_hook = NULL;
    g_trans_sched_interface.android_vh_alter_mutex_list_add_hook = NULL;
    g_trans_sched_interface.android_vh_rwsem_wake_hook = NULL;
    g_trans_sched_interface.android_vh_rwsem_wake_finish_hook = NULL;
    g_trans_sched_interface.android_vh_alter_rwsem_list_add_hook = NULL;
    g_trans_sched_interface.android_vh_free_task_hook = NULL;
    g_trans_sched_interface.task_rename_hook = NULL;
    g_trans_sched_interface.sched_switch_prev_hook = NULL;
    g_trans_sched_interface.sched_switch_next_hook = NULL;
    g_trans_sched_interface.check_ux_task = NULL;
    g_trans_sched_interface.balance_on_wakeup_ux = NULL;
    g_trans_sched_interface.trans_should_honor_sync = NULL;
    g_trans_sched_interface.android_rvh_find_lowest_rq_hook = NULL;
    g_trans_sched_interface.adjust_rt_lowest_mask = NULL;
    g_trans_sched_interface.filterout_on_wakeup_rt = NULL;
    g_trans_sched_interface.android_vh_scheduler_tick = NULL;
    g_trans_sched_interface.android_rvh_sched_newidle_balance = NULL;
    g_trans_sched_interface.schedule = NULL;
    g_trans_sched_interface.scheduler_tick_entry = NULL;
    g_trans_sched_interface.xmu_cpufreq_limit = NULL;
    g_trans_sched_interface.is_runnable_boost_enabled = NULL;
    g_trans_sched_interface.xmu_cpufreq_set = NULL;
}

static long trans_sched_ioctl(struct file *file, unsigned int cmd, unsigned long parm)
{
    int ret = 0;
    int ref = 0;
    u32 ux_tags = 0;
    int sched_state = 0;
    u32 sched_scene = 0;
    char buffer[256] = {0};
    unsigned int info_len = 0;
    struct ux_data ux_data = {0};
    struct ux_info ux_info = {0};
    struct task_struct *task = NULL;
    struct task_struct *thread = NULL;
    char thread_name[THREAD_NAME_LEN] = {0};
    struct trans_task_struct *trans_ts = NULL;
    struct ux_prio ux_prio = {0};

    switch (cmd) {
    case TRANS_SET_SCHED_STATE:
        if (copy_from_user(&sched_state, (int *)parm, sizeof(sched_state))) {
            trans_err("receive sched_state failed!\n");
            return -EFAULT;
        }

        if (!is_available_state(sched_state)) {
            trans_err("sched_state not available!\n");
            return -EINVAL;
        }

#if (!IS_ENABLED(CONFIG_TRANS_SCHED_SHARE))
        sched_state = sched_state ? (FEATURE_DEFAULT | FEATURE_DINFO) : FEATURE_NONE;
#endif

        set_uas_feature_state(sched_state);

        break;
    case TRANS_GET_SCHED_STATE:
        if (copy_to_user((int *)parm, &g_trans_sched_enable, sizeof(g_trans_sched_enable))) {
            trans_err("send trans_sched state failed!\n");
            return -EFAULT;
        }

        break;
    case TRANS_SET_PROC_UX_TAGS:
        if (!check_uas_feature(FEATURE_DEFAULT)) {
            return 0;
        }

        if (copy_from_user(&ux_data, (struct ux_data *)parm, sizeof(struct ux_data))) {
            trans_err("receive ux_data failed!\n");
            return -EFAULT;
        }

        if (ux_data.thread_name) {
            if (copy_from_user(thread_name, (char *)ux_data.thread_name, sizeof(thread_name)-1)) {
                trans_err("receive ux_data.thread_name failed!\n");
                return -EFAULT;
            }
            thread_name[sizeof(thread_name) - 1] = '\0';
        }

        if (ux_data.pid <= 0 || ux_data.pid > PID_MAX_DEFAULT) {
            trans_err("%d is not available pid!\n", ux_data.pid);
            return -EINVAL;
        }

        rcu_read_lock();
        task = find_task_by_vpid(ux_data.pid);
        if (task) {
            get_task_struct(task);

            thread = task;
            do {
                set_ux_tags_by_pid(thread, thread->pid, ux_data.ux_tags, thread_name);
            } while_each_thread(task, thread);

            put_task_struct(task);
        }
        rcu_read_unlock();

        break;
    case TRANS_SET_SCHED_SCENE:
        if (!check_uas_feature(FEATURE_DEFAULT)) {
            return 0;
        }

        if (copy_from_user(&sched_scene, (u32 *)parm, sizeof(sched_scene))) {
            trans_err("receive sched_scene failed!\n");
            return -EFAULT;
        }

        if (!is_available_scene(sched_scene)) {
            trans_err("sched_scene not available!\n");
            return -EINVAL;
        }

        mutex_lock(&g_mutex_ux_scene);
        if (check_ssc_mask(sched_scene)) {
            g_sched_scene = cancel_ss(ignore_ssc_mask(sched_scene));
        }
        else if (!sched_scene) {
            g_sched_scene = 0;
        }
        else {
            g_sched_scene = set_ss(ignore_ssc_mask(sched_scene));
        }
        mutex_unlock(&g_mutex_ux_scene);
        trans_sched_systrace_c(TRANS_SYSTRACE_POS, g_sched_scene, "uas_scene");

        break;
    case TRANS_GET_SCHED_SCENE:
        if (copy_to_user((u32 *)parm, &g_sched_scene, sizeof(g_sched_scene))) {
            trans_err("send sched_scene failed!\n");
            return -EFAULT;
        }
        break;
    case TRANS_SET_UX_TAGS:
        if (!check_uas_feature(FEATURE_DEFAULT)) {
            return 0;
        }

        if (copy_from_user(&ux_data, (struct ux_data *)parm, sizeof(struct ux_data))) {
            trans_err("receive ux_data failed!\n");
            return -EFAULT;
        }

        if (ux_data.thread_name) {
            if (copy_from_user(thread_name, (char *)ux_data.thread_name, sizeof(thread_name)-1)) {
                trans_err("receive ux_data.thread_name failed!\n");
                return -EFAULT;
            }
            thread_name[sizeof(thread_name) - 1] = '\0';
        }

        if (ux_data.ref) {
            if (!get_threadid_ref(thread_name, &ref)) {
                if (ref >= ux_data.ref) {
                    trans_warn("id_ref overflow: pid=%d, ux_tags=%#x, ref=%#x/%#x, id=%s\n", ux_data.pid, ux_data.ux_tags, ref, ux_data.ref, thread_name);
                    return -EACCES;
                }
            }
        }

        ret = set_ux_tags_by_pid(NULL, ux_data.pid, ux_data.ux_tags, thread_name);
        if (ret) {
            trans_err("set ux tags failed[%d], pid=%d, ux_tags=%#x, id=%s!\n", ret, ux_data.pid, ux_data.ux_tags, thread_name);
            return ret;
        }

        set_ux_target_tags(ux_data.pid, ux_data.ux_tags);
        break;
    case TRANS_SET_UX_PRIO:
        if (!check_uas_feature(FEATURE_DEFAULT)) {
            return 0;
        }

        if (copy_from_user(&ux_prio, (struct ux_prio *)parm, sizeof(struct ux_prio))) {
            trans_err("receive ux_prio failed!\n");
            return -EFAULT;
        }

        ret = convert_ux_prio(ux_prio.pid, ux_prio.shift);
        if (ret) {
            trans_err("convert ux_prio failed[%d], pid=%d, shift=%d!\n", ret, ux_prio.pid, ux_prio.shift);
            return ret;
        }

        break;
    case TRANS_GET_UX_TAGS:
        if (copy_from_user(&ux_data, (struct ux_data *)parm, sizeof(struct ux_data))) {
            trans_err("receive ux_data failed!\n");
            return -EFAULT;
        }

        if (ux_data.pid <= 0 || ux_data.pid > PID_MAX_DEFAULT) {
            trans_err("%d is not available pid!\n", ux_data.pid);
            return -EINVAL;
        }

        rcu_read_lock();
        task = find_task_by_vpid(ux_data.pid);
        if (task) {
            get_task_struct(task);
            ux_tags = get_ux_tags(task);
            put_task_struct(task);
        }
        rcu_read_unlock();

        if (copy_to_user(&((struct ux_data *)parm)->ux_tags, &ux_tags, sizeof(ux_data.ux_tags))) {
            trans_err("send ux_tags failed!\n");
            return -EFAULT;
        }
        break;
    case TRANS_GET_UX_INFO:
        if (copy_from_user(&ux_info, (struct ux_info *)parm, sizeof(struct ux_info))) {
            trans_err("receive ux_info failed!\n");
            return -EFAULT;
        }

        if (ux_info.pid <= 0 || ux_info.pid > PID_MAX_DEFAULT) {
            trans_err("%d is not available pid!\n", ux_info.pid);
            return -EINVAL;
        }

        if (!ux_info.info) {
            trans_err("ux_info.info is NULL!\n");
            return -EINVAL;
        }

        rcu_read_lock();
        task = find_task_by_vpid(ux_info.pid);
        if (task) {
            get_task_struct(task);
            trans_ts = get_trans_task_struct(task);
            if (!IS_ERR_OR_NULL(trans_ts)) {
                snprintf(buffer, sizeof(buffer), "%s/%d/%d, run_limit=%llu, ux_tags=%#x:%#x", \
                        task->comm, task->pid, task->tgid, ux_task_run_limit(task), trans_ts->ux_tags, trans_ts->share);
            }
            else {
                snprintf(buffer, sizeof(buffer), "%s/%d/%d is not init.", task->comm, task->pid, task->tgid);
            }
            put_task_struct(task);
        }
        rcu_read_unlock();

        info_len = ux_info.info_len > sizeof(buffer) ? sizeof(buffer) : ux_info.info_len;
        if (copy_to_user(ux_info.info, buffer, info_len)) {
            trans_err("send ux_info.info failed!\n");
            return -EFAULT;
        }
        break;
    case TRANS_DUMP_UX_LIST:
        dump_ux_record();
        dump_sf_targets();
        dump_ux_sched_domains();
        ut_dump_cpu_array();
        ut_calc_order_idx();
        break;
    default:
        break;
    }

    return 0;
}

static const struct file_operations trans_sched_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = trans_sched_ioctl,
};

static struct miscdevice trans_sched_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = TRANS_MODNAME,
    .fops = &trans_sched_fops,
};

static int __init trans_sched_init(void)
{
    int ret;

    TRANS_ASSERT(struct trans_task_struct *, TRANS_PCB_OFFSET, struct task_struct);
    TRANS_ASSERT(struct trans_rq, 0, struct rq);

    ret = trans_pcb_init();
    if (ret) {
        trans_err("trans pcb init failed!\n");
        return ret;
    }

    ret = trans_proc_init();
    if (ret) {
        trans_err("trans proc init failed!\n");
        return ret;
    }

    ux_targets_work_init();
    ux_account_init();
    trans_rqs_init();
    build_ux_sched_domains();
    xmu_cpufreq_init();
    register_trans_sched_interface();
    trans_register_kprobe();
    trans_hook_init();
    trace_points_init();
    register_workqueue_tracepoints();
#if IS_ENABLED(CONFIG_TRANSSION_BLK_UAS)
    tran_low_latency_sched_interface.check_ux_task = &tran_is_ux_task;
#endif
    set_uas_feature_state(FEATURE_DEFAULT);
    return misc_register(&trans_sched_miscdev);
}

// Please don't uninstall this module
static void __exit trans_sched_exit(void)
{
    unregister_workqueue_tracepoints();
    trace_points_deinit();
    trans_proc_deinit();
    ux_account_deinit();
    ux_targets_work_deinit();
    trans_unregister_kprobe();
    unregister_trans_sched_interface();
    xmu_cpufreq_exit();
#if IS_ENABLED(CONFIG_TRANSSION_BLK_UAS)
    tran_low_latency_sched_interface.check_ux_task = NULL;
#endif
    misc_deregister(&trans_sched_miscdev);
    trans_pcb_deinit();
}

module_init(trans_sched_init);
module_exit(trans_sched_exit);

MODULE_DESCRIPTION("Transsion Scheduler Driver.");
MODULE_AUTHOR("zhiyuan.wang@transsion.com");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL v2");
