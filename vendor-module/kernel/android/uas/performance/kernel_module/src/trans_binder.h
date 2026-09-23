#ifndef _TRANS_BINDER_H
#define _TRANS_BINDER_H
#include <linux/types.h>
#include <linux/version.h>
#include <drivers/android/binder_internal.h>

struct ux_binder
{
    struct binder_node *node;
    struct task_struct *to_proc;
    struct task_struct *to_task;
    pid_t to_pid;
};

int abinder_set_ux_tags(struct task_struct *task, u32 ux_tags);

void android_vh_binder_restore_priority_hook(void *data, struct binder_transaction *reply_to, struct task_struct *task);
void android_vh_binder_wait_for_work_hook(void *data, bool do_proc_work, struct binder_thread *thread, struct binder_proc *proc);
void android_vh_sync_txn_recvd_hook(void *data, struct task_struct *to_task, struct task_struct *from_task);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
void android_vh_binder_proc_transaction_finish_hook(void *data, struct binder_proc *binder_proc, struct binder_transaction *t, \
        struct task_struct *thread_task, bool pending_async, bool sync);
#else
void android_vh_binder_proc_transaction_end_hook(void *data, struct task_struct *caller_task, struct task_struct *proc_task, \
		struct task_struct *thread_task, unsigned int code, bool pending_async, bool sync);
#endif /* KERNEL_VERSION(5, 15, 0) */

void binder_hook_init(void);

#endif /* _TRANS_BINDER_H */
