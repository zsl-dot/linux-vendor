#ifndef _TRANS_MUTEX_H
#define _TRANS_MUTEX_H
#include <linux/mutex.h>

struct mutex_waiter;
void android_vh_mutex_wait_start_hook(void *data, struct mutex *lock);
void android_vh_mutex_unlock_slowpath_hook(void *data, struct mutex *lock);
void android_vh_alter_mutex_list_add_hook(void *data, struct mutex *lock, struct mutex_waiter *waiter, struct list_head *list, bool *already_on_list);
void android_vh_mutex_can_spin_on_owner_hook(void *data, struct mutex *lock, int *retval);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
void android_vh_record_mutex_lock_starttime_hook(void *data, struct task_struct *task, unsigned long settime_jiffies);
#else
void android_vh_record_mutex_lock_starttime_hook(void *data, struct mutex *lock, unsigned long settime_jiffies);
#endif

#endif /* _TRANS_MUTEX_H */