#include <asm/atomic.h>

#include "sched_common.h"
#include "trans_target.h"
#include "trans_rwsem.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#define TRANS_RWSEM_WRITER_MASK         (1UL << 0)
#define TRANS_RWSEM_READER_OWNED        (1UL << 0)
#define TRANS_RWSEM_OWNER_FLAGS_MASK    (0x07)
#define TRANS_RWSEM_FLAG_HANDOFF        (1UL << 2)
#else
#define TRANS_RWSEM_ANONYMOUSLY_OWNED   (1UL << 0)
#define TRANS_RWSEM_READER_OWNED        ((struct task_struct *)TRANS_RWSEM_ANONYMOUSLY_OWNED)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;
	bool handoff_set;
};
#endif

#define get_rwsem_first_waiter(sem) \
    list_first_entry(&sem->wait_list, struct rwsem_waiter, list)


#define WAITERS_UX_LIMIT     (50)
#define RWSEM_WAITER_TIMEOUT (1)

#define RWSEM_SETUX_ROUTE_NORMAL   (0)
#define RWSEM_SETUX_ROUTE_HANDOFF  (1)
#define RWSEM_SETUX_ROUTE_TIMEOUT  (2)
#define RWSEM_SETUX_ROUTE_READER   (3)
#define RWSEM_SETUX_ROUTE_WRITER   (4)


static void rwsem_set_ux_share(struct task_struct *to_task, struct task_struct *from_task, int route)
{
    u32 ux_share_tags = 0;
    u32 ux_share_data = 0;
    enum ux_task_state state = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;
    u32 ux_task_slice = route ? UX_TASK_SLICE1 : UX_TASK_SLICE2;

    if (unlikely(!to_task || !from_task)) {
        return;
    }

    if (is_ux_task(from_task, NULL) && (!is_ux_task(to_task, &state))) {
        if (state == UX_TASK_NO_TAG) {
            ux_share_tags = get_ux_tags(from_task);

            if (get_ux_share_state(ux_share_tags)) {
                if (check_ux_share_type(ux_share_tags, UX_SHARE_RWSEM)) {
                    ux_share_data = get_ux_share_val(from_task);
                    if (share_filed->rwsem_nr >= g_ux_share_limit[UX_SHARE_RWSEM]) {
                        return;
                    }
                }
                else if (check_ux_share_type(ux_share_tags, UX_SHARE_BINDER)) {
                    ux_share_tags = ignore_ux_share_state(ux_share_tags);
                }
                else {
                    return;
                }
            }

            ux_share_tags = change_ux_slice(ux_share_tags, ux_task_slice);
            set_ux_share(to_task, UX_SHARE_RWSEM, ux_share_data, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[r][%d]:%s/%d/%d, to:%s/%d/%d, %#x:%#x =>> %#x:%#x, state=%d\n", route, \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, \
                get_ux_tags(from_task), get_ux_share_val(from_task), get_ux_tags(to_task), get_ux_share_val(to_task), state);
        }

        return;
    }

    if (is_rt_task(from_task) && (!is_ux_task(to_task, &state))) {
        if (state == UX_TASK_NO_TAG) {
            if (!(check_sf_target(from_task) || check_overlayengine_target(from_task))) {
                return;
            }

            ux_share_tags = change_ux_slice(init_ux_dprio(UX_TASK_TAGS_DEFAULT), ux_task_slice);
            set_ux_share(to_task, UX_SHARE_RWSEM, 0, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[rr][%d]:%s/%d/%d, to:%s/%d/%d, ux_tags=%#x:%#x, state=%d\n", route, \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, \
                get_ux_tags(to_task), get_ux_share_val(to_task), state);
        }

        return;
    }
}

static void rwsem_unset_ux_share(struct task_struct *task)
{
    u32 ux_share_data = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely(!task)) {
        return;
    }

    ux_share_data = get_ux_share_val(task);
    if (!share_filed->rwsem_nr) {
        return;
    }

    unset_ux_share(task, UX_SHARE_RWSEM);
}

static void rwsem_list_insert_ux(struct task_struct *task, struct list_head *waiter_node, enum rwsem_waiter_type type, struct list_head *wait_list, bool *already_on_list, bool has_handoff)
{
    int setux_cnt = 0;
    bool waiter_timeout = false;
    bool ux_reader_ahead = false;
    enum ux_task_state state = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct rwsem_waiter *waiter = NULL;
    struct rwsem_waiter *insert_pos = NULL;

    if (!task || !waiter_node || !wait_list || !already_on_list) {
        return;
    }

    if (!((type == RWSEM_WAITING_FOR_READ) || (type == RWSEM_WAITING_FOR_WRITE))) {
        return;
    }

    list_for_each_safe(pos, next, wait_list) {
        waiter = list_entry(pos, struct rwsem_waiter, list);
        if (unlikely(IS_ERR_OR_NULL(waiter) || IS_ERR_OR_NULL(waiter->task))) {
            return;
        }

        if (has_handoff && list_is_first(pos, wait_list)) {
            if (setux_cnt < WAITERS_UX_LIMIT) {
                rwsem_set_ux_share(waiter->task, task, RWSEM_SETUX_ROUTE_HANDOFF);
                setux_cnt ++;
            }
        }

        if (is_ux_task(waiter->task, &state) || (state == UX_TASK_NOT_FAIR)) {
            if (waiter->type == RWSEM_WAITING_FOR_READ) {
                ux_reader_ahead = true;
            }
            continue;
        }

        if (type == RWSEM_WAITING_FOR_READ) {
            if (!ux_reader_ahead) {
                if ((waiter->type == RWSEM_WAITING_FOR_WRITE) && (setux_cnt < WAITERS_UX_LIMIT)) {
                    rwsem_set_ux_share(waiter->task, task, RWSEM_SETUX_ROUTE_READER);
                    setux_cnt ++;
                    continue;
                }
            }

            insert_pos = waiter;
            break;
        }

        if (type == RWSEM_WAITING_FOR_WRITE) {
            if (ux_reader_ahead) {
                if ((waiter->type == RWSEM_WAITING_FOR_READ) && (setux_cnt < WAITERS_UX_LIMIT)) {
                    rwsem_set_ux_share(waiter->task, task, RWSEM_SETUX_ROUTE_WRITER);
                    setux_cnt ++;
                    continue;
                }
            }

            if (!insert_pos) {
                waiter_timeout = time_after(jiffies, (waiter->timeout + RWSEM_WAITER_TIMEOUT));
                if (waiter_timeout && (setux_cnt < WAITERS_UX_LIMIT)) {
                    rwsem_set_ux_share(waiter->task, task, RWSEM_SETUX_ROUTE_TIMEOUT);
                    setux_cnt ++;
                }

                insert_pos = waiter;
            }
        }
    }

    if (insert_pos) {
        list_add(waiter_node, insert_pos->list.prev);
    }
    else {
        list_add_tail(waiter_node, wait_list);
    }

    *already_on_list = true;
}

static void dump_rwsem_list(struct list_head *wait_list)
{
    unsigned int cnt = 0;
    unsigned long timeout = 0;
    struct list_head *pos = NULL;
    struct rwsem_waiter *waiter = NULL;
    struct task_struct *task = NULL;

    if (IS_ERR_OR_NULL(wait_list)) {
        return;
    }

    list_for_each(pos, wait_list) {
        waiter = list_entry(pos, struct rwsem_waiter, list);
        if (IS_ERR_OR_NULL(waiter)) {
            continue;
        }

        task = waiter->task;
        if (IS_ERR_OR_NULL(task)) {
            continue;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
        timeout = (jiffies > waiter->timeout) ? (jiffies - waiter->timeout) : 0;
#endif

        trans_debug(DEBUG_SHARE, "waiter[%d][%s]:%s/%d/%d, timeout=%lu, tags=%d/%#x\n", cnt++, \
            (waiter->type == RWSEM_WAITING_FOR_READ) ? "R" : "W", task->comm, task->pid, task->tgid, \
            timeout, task->prio, get_ux_tags(task));
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
static inline bool is_rwsem_reader_owned(struct rw_semaphore *sem)
{
#ifdef CONFIG_DEBUG_RWSEMS
    if (atomic_long_read(&sem->count) & TRANS_RWSEM_WRITER_MASK) {
        return false;
    }
#endif

    return (atomic_long_read(&sem->owner) & TRANS_RWSEM_READER_OWNED);
}
#else
static inline bool is_rwsem_reader_owned(struct rw_semaphore *sem)
{
    struct task_struct *owner = NULL;

    owner = READ_ONCE(sem->owner);
    return owner == TRANS_RWSEM_READER_OWNED;
}
#endif

void android_vh_rwsem_wake_hook(void *data, struct rw_semaphore *sem)
{
    struct task_struct *task = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

    if (!sem) {
        return;
    }

    if (is_rwsem_reader_owned(sem)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    task = (struct task_struct *)(atomic_long_read(&sem->owner) & ~TRANS_RWSEM_OWNER_FLAGS_MASK);
#else
    task = READ_ONCE(sem->owner);
#endif
    if (task) {
        rwsem_set_ux_share(task, current, RWSEM_SETUX_ROUTE_NORMAL);
    }
}

void android_vh_rwsem_wake_finish_hook(void *data, struct rw_semaphore *sem)
{
    rwsem_unset_ux_share(current);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
void android_vh_record_rwsem_lock_starttime_hook(void *data, struct rw_semaphore *sem, unsigned long settime_jiffies)
{
    struct task_struct *task = current;
    if (!settime_jiffies) {
        rwsem_unset_ux_share(task);
    }
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
void android_vh_record_rwsem_lock_starttime_hook(void *data, struct task_struct *task, unsigned long settime_jiffies)
{
    if (!settime_jiffies) {
        rwsem_unset_ux_share(task);
    }
}
#else
void android_vh_rwsem_up_read_end_hook(void *data, struct rw_semaphore *sem)
{
    struct task_struct *task = current;

    rwsem_unset_ux_share(task);
}

void android_vh_rwsem_up_write_end_hook(void *data, struct rw_semaphore *sem)
{
    struct task_struct *task = current;

    rwsem_unset_ux_share(task);
}
#endif

void android_vh_alter_rwsem_list_add_hook(void *data, struct rwsem_waiter *waiter, struct rw_semaphore *sem, bool *already_on_list)
{
    bool has_handoff = false;
    struct rwsem_waiter *first_waiter = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

    if (!waiter || (!waiter->task) || !sem || !already_on_list) {
        return;
    }

    if (list_empty(&sem->wait_list)) {
        return;
    }

    if (!is_ux_task(waiter->task, NULL)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
    has_handoff = !!(atomic_long_read(&sem->count) & TRANS_RWSEM_FLAG_HANDOFF);

    if (!has_handoff) {
        first_waiter = get_rwsem_first_waiter(sem);
        if (likely(!IS_ERR_OR_NULL(first_waiter))) {
            if (time_after(jiffies, first_waiter->timeout)) {
                has_handoff = true;
            }
        }
    }
#endif

    rwsem_list_insert_ux(waiter->task, &waiter->list, waiter->type, &sem->wait_list, already_on_list, has_handoff);

    if (g_trans_sched_debug) {
        if (*already_on_list) {
            dump_rwsem_list(&sem->wait_list);
        }
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
void android_vh_rwsem_can_spin_on_owner_hook(void *data, struct rw_semaphore *sem, bool *ret)
#else
void android_vh_rwsem_can_spin_on_owner_hook(void *data, struct rw_semaphore *sem, bool *ret, bool wlock)
#endif
{
    struct task_struct *task = NULL;
    struct task_struct *curr = current;
    struct rwsem_waiter *waiter = NULL;

    if (!sem || !ret || !(*ret) || !curr) {
        return;
    }

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

    raw_spin_lock_irq(&sem->wait_lock);

    if (list_empty(&sem->wait_list)) {
        goto out;
    }

    waiter = list_first_entry(&sem->wait_list, struct rwsem_waiter, list);
    if (IS_ERR_OR_NULL(waiter) || IS_ERR_OR_NULL(waiter->task)) {
        goto out;
    }

    task = waiter->task;
    if (is_ux_task(task, NULL) || check_sf_target(task) || check_overlayengine_target(task)) {
        if (is_ux_task(curr, NULL) || is_rt_task(curr)) {
            goto out;
        }

        trans_debug(DEBUG_SHARE, "thief:%s/%d/%d, tags=%d/%#x; waiter:%s/%d/%d, tags=%d/%#x\n", \
            curr->comm, curr->pid, curr->tgid, curr->prio, get_ux_tags(curr), \
            task->comm, task->pid, task->tgid, task->prio, get_ux_tags(task));

        *ret = 0;
    }

out:
    raw_spin_unlock_irq(&sem->wait_lock);
}
