#ifndef _TRANS_RWSEM_H
#define _TRANS_RWSEM_H
#include <linux/rwsem.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};
struct rwsem_waiter;
#endif

void android_vh_rwsem_wake_hook(void *data, struct rw_semaphore *sem);
void android_vh_rwsem_wake_finish_hook(void *data, struct rw_semaphore *sem);
void android_vh_alter_rwsem_list_add_hook(void *data, struct rwsem_waiter *waiter, struct rw_semaphore *sem, bool *already_on_list);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
void android_vh_rwsem_can_spin_on_owner_hook(void *data, struct rw_semaphore *sem, bool *ret);
 #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
 void android_vh_record_rwsem_lock_starttime_hook(void *data, struct rw_semaphore *sem, unsigned long settime_jiffies);
 #else
 void android_vh_record_rwsem_lock_starttime_hook(void *data, struct task_struct *task, unsigned long settime_jiffies);
 #endif
#else
void android_vh_rwsem_can_spin_on_owner_hook(void *data, struct rw_semaphore *sem, bool *ret, bool wlock);
void android_vh_rwsem_up_read_end_hook(void *data, struct rw_semaphore *sem);
void android_vh_rwsem_up_write_end_hook(void *data, struct rw_semaphore *sem);
#endif

#endif /* _TRANS_RWSEM_H */