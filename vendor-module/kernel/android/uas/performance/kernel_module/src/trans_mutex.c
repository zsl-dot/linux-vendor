#include <asm/atomic.h>

#include "sched_common.h"
#include "trans_target.h"
#include "trans_mutex.h"

#define TRANS_MUTEX_FLAGS           (0x07)
#define TRANS_MUTEX_FLAG_HANDOFF    (0x02)


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
struct mutex_waiter {
    struct list_head    list;
    struct task_struct  *task;
    struct ww_acquire_ctx   *ww_ctx;
#ifdef CONFIG_DEBUG_MUTEXES
    void    *magic;
#endif
};
#endif /* KERNEL_VERSION(5, 15, 0) */

#define MUTEX_SETUX_ROUTE_NORMAL   (0)
#define MUTEX_SETUX_ROUTE_HANDOFF  (1)

static void mutex_set_ux_share(struct task_struct *to_task, struct task_struct *from_task, int route)
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
                if (check_ux_share_type(ux_share_tags, UX_SHARE_MUTEX)) {
                    ux_share_data = get_ux_share_val(from_task);
                    if (share_filed->mutex_nr >= g_ux_share_limit[UX_SHARE_MUTEX]) {
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
            set_ux_share(to_task, UX_SHARE_MUTEX, ux_share_data, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[m][%d]:%s/%d/%d, to:%s/%d/%d, %#x:%#x =>> %#x:%#x, state=%d\n", route, \
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
            set_ux_share(to_task, UX_SHARE_MUTEX, 0, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[mr][%d]:%s/%d/%d, to:%s/%d/%d, ux_tags=%#x:%#x, state=%d\n", route, \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, \
                get_ux_tags(to_task), get_ux_share_val(to_task), state);
        }

        return;
    }
}

static void mutex_unset_ux_share(struct task_struct *task)
{
    u32 ux_share_data = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely(!task)) {
        return;
    }

    ux_share_data = get_ux_share_val(task);
    if (!share_filed->mutex_nr) {
        return;
    }

    unset_ux_share(task, UX_SHARE_MUTEX);
}

static void mutex_list_insert_ux(struct task_struct *task, struct list_head *waiter_node, struct list_head *wait_list, bool *already_on_list, bool has_handoff)
{
    enum ux_task_state state = 0;
    struct list_head *pos = NULL;
    struct list_head *next = NULL;
    struct mutex_waiter *waiter = NULL;

    if (!task || !waiter_node || !wait_list || !already_on_list) {
        return;
    }

    list_for_each_safe(pos, next, wait_list) {
        waiter = list_entry(pos, struct mutex_waiter, list);
        if (IS_ERR_OR_NULL(waiter)) {
            return;
        }

        if (unlikely(IS_ERR_OR_NULL(waiter->task))) {
            continue;
        }

        if (has_handoff && list_is_first(pos, wait_list)) {
            mutex_set_ux_share(waiter->task, task, MUTEX_SETUX_ROUTE_HANDOFF);
            continue;
        }

        if (!is_ux_task(waiter->task, &state)) {
            if (unlikely(state == UX_TASK_NOT_FAIR)) {
                continue;
            }

            list_add(waiter_node, waiter->list.prev);
            *already_on_list = true;
            return;
        }
    }
}

static void dump_mutex_list(struct list_head *wait_list)
{
    unsigned int cnt = 0;
    struct list_head *pos = NULL;
    struct mutex_waiter *waiter = NULL;
    struct task_struct *task = NULL;

    if (IS_ERR_OR_NULL(wait_list)) {
        return;
    }

    list_for_each(pos, wait_list) {
        waiter = list_entry(pos, struct mutex_waiter, list);
        if (IS_ERR_OR_NULL(waiter)) {
            continue;
        }

        task = waiter->task;
        if (IS_ERR_OR_NULL(task)) {
            task = current;
        }

        trans_debug(DEBUG_SHARE, "waiter[%d]:%s/%d/%d, tags=%d/%#x\n", cnt++, task->comm, task->pid, task->tgid, task->prio, get_ux_tags(task));
    }
}

void android_vh_mutex_wait_start_hook(void *data, struct mutex *lock)
{
    struct task_struct *task = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

    if (!lock) {
        return;
    }

    task = (struct task_struct *)(atomic_long_read(&lock->owner) & ~TRANS_MUTEX_FLAGS);
    if (task) {
        mutex_set_ux_share(task, current, MUTEX_SETUX_ROUTE_NORMAL);
    }
}

void android_vh_mutex_unlock_slowpath_hook(void *data, struct mutex *lock)
{
    mutex_unset_ux_share(current);
}
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
void android_vh_record_mutex_lock_starttime_hook(void *data, struct task_struct *task, unsigned long settime_jiffies)
{
    if (!settime_jiffies) {
        mutex_unset_ux_share(task);
    }
}
#else
void android_vh_record_mutex_lock_starttime_hook(void *data, struct mutex *lock, unsigned long settime_jiffies)
{
    struct task_struct *task = current;
    if (!settime_jiffies) {
        mutex_unset_ux_share(task);
    }
}
#endif

void android_vh_alter_mutex_list_add_hook(void *data, struct mutex *lock, struct mutex_waiter *waiter,
    struct list_head *list, bool *already_on_list)
{
    bool has_handoff = false;
    struct task_struct *task = current;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

    if (!waiter || !list || !lock) {
        return;
    }

    if (list_empty(list)) {
        return;
    }

    if (!(is_ux_task(task, NULL) || check_sf_target(task) || check_overlayengine_target(task))) {
        return;
    }

    has_handoff = !!(atomic_long_read(&lock->owner) & TRANS_MUTEX_FLAG_HANDOFF);

    mutex_list_insert_ux(task, &waiter->list, list, already_on_list, has_handoff);
    if (g_trans_sched_debug) {
        if (*already_on_list) {
            dump_mutex_list(list);
        }
    }
}

void android_vh_mutex_can_spin_on_owner_hook(void *data, struct mutex *lock, int *retval)
{
    struct task_struct *task = NULL;
    struct task_struct *curr = current;
    struct mutex_waiter *waiter = NULL;

    if (!lock || !retval || !(*retval) || !curr) {
        return;
    }

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE)) {
        return;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    raw_spin_lock(&lock->wait_lock);
#else
    spin_lock(&lock->wait_lock);
#endif

    if (list_empty(&lock->wait_list)) {
        goto out;
    }

    waiter = list_first_entry(&lock->wait_list, struct mutex_waiter, list);
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

        *retval = 0;
    }

out:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    raw_spin_unlock(&lock->wait_lock);
#else
    spin_unlock(&lock->wait_lock);
#endif
}
