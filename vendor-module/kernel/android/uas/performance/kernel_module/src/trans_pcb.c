#include <linux/slab.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <trace/hooks/sched.h>
#endif

#include "sched_common.h"
#include "trans_xmu.h"
#include "trans_locking.h"
#include "trans_loading.h"
#include "trans_sched_info.h"
#include "trans_workqueue.h"


static struct trans_pcb_data g_trans_pcb_data = {
    .cache = NULL,
    .name = "trans_task_struct",
};

int trans_pcb_alloc(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = NULL;

    if (!task) {
        return -EFAULT;
    }

    trans_ts = kmem_cache_alloc(g_trans_pcb_data.cache, GFP_ATOMIC);
    if (!trans_ts) {
        return -ENOMEM;
    }

    WRITE_ONCE(task->android_oem_data1[TRANS_PCB_OFFSET], (u64)trans_ts);
    trans_debug_deferred(DEBUG_KLOG, "pcb alloc: %s/%d/%d, trans_ts=%p\n", task->comm, task->pid, task->tgid, trans_ts);

    return 0;
}

void android_vh_dup_task_struct_hook(void *unused,
        struct task_struct *tsk, struct task_struct *orig)
{
    struct trans_task_struct *tts = NULL;

    if (!tsk || !orig)
        return;

    /* The required space has been allocated */
    if (!IS_ERR_OR_NULL((void *)READ_ONCE(tsk->android_oem_data1[TRANS_PCB_OFFSET])))
        return;

    tts = kmem_cache_alloc(g_trans_pcb_data.cache, GFP_ATOMIC);
    if (IS_ERR_OR_NULL(tts)) {
        trans_debug_deferred(DEBUG_KLOG, "dup_task failed:%s/%d/%d, tts=0x%llx\n", tsk->comm, tsk->pid, tsk->tgid, (u64)tts);
        return;
    }

    /*
     * If the task is still in the lock_list, remove it from the list.
     */
    dequeue_locktask_on_fork(tts);

    memset(tts, 0, sizeof(struct trans_task_struct));
    tts->task = tsk;

    INIT_LIST_HEAD(&tts->lock_entry);
    smp_mb();

    init_task_load(&tts->ld);

    tts->txmu = xmu_task_info_alloc();

    WRITE_ONCE(tsk->android_oem_data1[TRANS_PCB_OFFSET], (u64) tts);
}

void android_vh_free_task_hook(void *data, struct task_struct *task)
{
    struct trans_task_struct *trans_ts = NULL;

    if (!task)
        return;

    trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts))
        return;

    dec_ux_kworker_count(task, FREE_TASK_DEC_UX_KWORKER_CNT);

    if (unlikely((trans_ts->ux_tags) && (!get_ux_share_state(trans_ts->ux_tags)))) {
        delete_ux_record(task);
    }

    WRITE_ONCE(task->android_oem_data1[TRANS_PCB_OFFSET], 0);

    /*
     * If the task is still in the lock_list, remove it from the list.
     */
    dequeue_locktask_on_fork(trans_ts);

    xmu_task_info_free(trans_ts);

    memset(trans_ts, 0, sizeof(struct trans_task_struct));

    INIT_LIST_HEAD(&trans_ts->lock_entry);
    smp_mb();

    init_task_load(&trans_ts->ld);

    kmem_cache_free(g_trans_pcb_data.cache, trans_ts);
}

static void alloc_tts_for_all_threads(void)
{
    struct task_struct *p, *g;
    u32 iter_cpu;

    read_lock(&tasklist_lock);
    for_each_process_thread(g, p) {
        struct trans_task_struct *tts = NULL;

        tts = (struct trans_task_struct *) READ_ONCE(p->android_oem_data1[TRANS_PCB_OFFSET]);
        if (IS_ERR_OR_NULL(tts)) {
            tts = kmem_cache_alloc(g_trans_pcb_data.cache, GFP_ATOMIC);

            if (!IS_ERR_OR_NULL(tts)) {
                tts->task = p;
                smp_mb();

                WRITE_ONCE(p->android_oem_data1[TRANS_PCB_OFFSET], (u64) tts);
            }
        }
    }
    for_each_possible_cpu(iter_cpu) {
        struct trans_task_struct *tts = NULL;

        p = cpu_rq(iter_cpu)->idle;
        tts = (struct trans_task_struct *) READ_ONCE(p->android_oem_data1[TRANS_PCB_OFFSET]);
        if (IS_ERR_OR_NULL(tts)) {
            tts = kmem_cache_alloc(g_trans_pcb_data.cache, GFP_ATOMIC);
            if (!IS_ERR_OR_NULL(tts)) {
                tts->task = p;
                smp_mb();

                WRITE_ONCE(p->android_oem_data1[TRANS_PCB_OFFSET], (u64) tts);
            }
        }
    }
    read_unlock(&tasklist_lock);
}

static void init_trans_task_struct(void *ptr)
{
    struct trans_task_struct *tts = ptr;

    memset(tts, 0, sizeof(struct trans_task_struct));
}

int trans_pcb_init(void)
{
    int ret = 0;

    g_trans_pcb_data.cache = kmem_cache_create(g_trans_pcb_data.name,
                        sizeof(struct trans_task_struct), 0, \
                        SLAB_PANIC|SLAB_ACCOUNT, init_trans_task_struct);
    if (!g_trans_pcb_data.cache) {
        return -ENOMEM;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    /*
     * alloc trans_task_struct for all tasks.
     */
    alloc_tts_for_all_threads();

    ret = register_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_hook, NULL);
    if (ret) {
        trans_err("register android_vh_dup_task_struct_handler failed!\n");
    }

    ret = register_trace_android_vh_free_task(android_vh_free_task_hook, NULL);
    if (ret) {
        trans_err("register android_vh_free_task_hook failed!\n");
    }
#endif

    return ret;
}

void trans_pcb_deinit(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    unregister_trace_android_vh_dup_task_struct(android_vh_dup_task_struct_hook, NULL);
    unregister_trace_android_vh_free_task(android_vh_free_task_hook, NULL);
#endif

    kmem_cache_destroy(g_trans_pcb_data.cache);
}
