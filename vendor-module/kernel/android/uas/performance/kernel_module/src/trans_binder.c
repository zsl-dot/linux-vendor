#include <linux/version.h>
#include <uapi/linux/android/binder.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <trace/hooks/binder.h>
#endif

#include "sched_common.h"
#include "trans_binder.h"
#include "trans_target.h"

static struct ux_binder g_ux_abinder = {
    .node = NULL,
    .to_proc = NULL,
    .to_task = NULL,
    .to_pid = -1,
};
#define SYSTEM_UID     (1000)
#define APP_UID_MIN    (10000)
#define APP_UID_MAX    (10999)

enum ux_transaction_flags {
    TF_UX_BINDER_WORK       = 0x1000,
    TF_UX_ABINDER_WORK      = 0x2000,
    TF_UX_SF_ABINDER_WORK   = 0x4000,
};

enum {
    BINDER_LOOPER_STATE_REGISTERED      = 0x01,
    BINDER_LOOPER_STATE_ENTERED         = 0x02,
};

#define FIND_BINDER_THREADS_LIMIT   (150)
#define ABINDER_SET_THREADS_LIMIT   (15)
#define FIND_ABINDER_THREADS_LIMIT  (15)

#define BINDER_SETUX_ROUTE_RECVD              (1)
#define BINDER_SETUX_ROUTE_TRANSACTION        (2)
#define BINDER_SETUX_ROUTE_RECVD_ASYNC        (3)
#define BINDER_SETUX_ROUTE_TRANSACTION_INFER  (4)

static bool check_ux_abinder(u32 ux_tags)
{
    if (check_ux_flag(ux_tags, UX_FLAG_ABINDER))
        return true;

    return false;
}

int abinder_set_ux_tags(struct task_struct *task, u32 ux_tags)
{
    u32 ux_curr_tags = 0;
    struct rq *rq = NULL;
    struct rq_flags flags = {0};

    if (unlikely(!task))
        return -EINVAL;

    if (!check_ux_same_type(ux_tags, UX_TASK_TAGS_ABINDER_BIT))
        return -EACCES;

    rq = task_rq_lock(task, &flags);
    ux_curr_tags = get_ux_tags(task);

    // set
    if (ux_tags == UX_TASK_TAGS_ABINDER_BIT) {
        ux_curr_tags = set_ux_flag(ux_curr_tags, UX_FLAG_ABINDER);
        set_ux_tags(task, ux_curr_tags);
        trans_sched_systrace_c(task->tgid, ux_tags_align(ux_curr_tags), "uas_abinder-%d", task->pid);
        goto out;
    }

    // unset
    if (check_unset_ux_tags(ux_tags)) {
        ux_curr_tags = clear_ux_flag(ux_curr_tags, UX_FLAG_ABINDER);
        set_ux_tags(task, ux_curr_tags);
        trans_sched_systrace_c(task->tgid, ux_tags_align(ux_curr_tags), "uas_abinder-%d", task->pid);
    }

out:
    task_rq_unlock(rq, task, &flags);

    return 0;
}

static void binder_set_ux_share(struct task_struct *to_proc, struct task_struct *to_task, struct task_struct *from_task, bool sync, int route)
{
    u32 ux_share_tags = 0;
    u32 ux_share_data = 0;
    enum ux_task_state state = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely(!to_proc || !to_task))
        return;

    if (!from_task) {
        if (sync || (route != BINDER_SETUX_ROUTE_RECVD_ASYNC))
            return;

        if(is_ux_task(to_task, &state))
            return;

        if (state == UX_TASK_NO_TAG) {
            ux_share_tags = get_ux_tags(to_proc);
            if (unlikely(!check_ux_tags_ok(ux_share_tags)))
                ux_share_tags = init_ux_dprio(UX_TASK_TAGS_DEFAULT);

            if (check_ux_flag(ux_share_tags, UX_FLAG_ABINDER))
                ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ABINDER);

            set_ux_share(to_task, UX_SHARE_BINDER, 0, ux_share_tags);

            trans_debug(DEBUG_SHARE, "to:%s/%d/%d/%s, ux_tags:%#x:%#x, slice=%llu\n", \
                to_task->comm, to_task->pid, to_task->tgid, to_proc->comm, \
                get_ux_tags(to_task), get_ux_share_val(to_task), ux_task_run_limit(to_task));
        }

        return;
    }

    if (is_ux_task(from_task, NULL)) {
        if(is_ux_task(to_task, &state)) {
            if (route == BINDER_SETUX_ROUTE_RECVD)
                return;

            if(check_ux_share_type(get_ux_tags(to_task), UX_SHARE_BINDER))
                update_ux_share_hold(to_task, UX_SHARE_BINDER);

            return;
        }

        if (state == UX_TASK_NOT_FAIR) {
            if (check_ux_tags_ok(get_ux_tags(to_task)))
                return;
        }

        if ((state == UX_TASK_NO_TAG) || (state == UX_TASK_NOT_FAIR)) {
            ux_share_tags = get_ux_tags(from_task);

            if (get_ux_share_state(ux_share_tags)) {
                if (!check_ux_share_type(ux_share_tags, UX_SHARE_BINDER))
                    return;

                ux_share_data = get_ux_share_val(from_task);
                if (share_filed->binder_nr >= g_ux_share_limit[UX_SHARE_BINDER])
                    return;
            }

            if (check_sched_scene(SS_LAUNCH) && sync && (from_task->pid == from_task->tgid)) {
                ux_share_tags = change_ux_slice(ux_share_tags, UX_TASK_SLICE7); // 128ms
                if (get_ux_dprio(ux_share_tags) == UX_TASK_DPRIO_BASE)
                    ux_share_tags = raise_ux_dprio(ux_share_tags);
            }

            if (check_ux_flag(ux_share_tags, UX_FLAG_ABINDER))
                ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ABINDER);

            set_ux_share(to_task, UX_SHARE_BINDER, ux_share_data, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[%s]:%s/%d/%d, to:%s/%d/%d, %#x:%#x =>> %#x:%#x, state=%d, slice=%llu, route=%d\n", (sync ? "b" : "ab"), \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, \
                get_ux_tags(from_task), get_ux_share_val(from_task), get_ux_tags(to_task), get_ux_share_val(to_task), state, ux_task_run_limit(to_task), route);
        }

        return;
    }

    if (is_rt_task(from_task)) {
        if (is_ux_task(to_task, &state)) {
            if (route == BINDER_SETUX_ROUTE_RECVD)
                return;

            if(check_ux_share_type(get_ux_tags(to_task), UX_SHARE_BINDER))
                update_ux_share_hold(to_task, UX_SHARE_BINDER);

            return;
        }

        if (state == UX_TASK_NO_TAG) {
            ux_share_tags = init_ux_dprio(UX_TASK_TAGS_DEFAULT);

            if (!sync && check_ux_abinder(get_ux_tags(from_task))) {
                ux_share_tags = get_ux_tags(to_proc);
                if (unlikely(!check_ux_tags_ok(ux_share_tags))) {
                    if (check_sf_abinder_target(from_task, true))
                        return;

                    ux_share_tags = init_ux_dprio(UX_TASK_TAGS_DEFAULT);
                }

                if (check_sf_abinder_target(from_task, true))
                    ux_share_tags = raise_ux_dprio(ux_share_tags);
            }

            if (check_ux_flag(ux_share_tags, UX_FLAG_ABINDER))
                ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ABINDER);

            set_ux_share(to_task, UX_SHARE_BINDER, 0, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from[%s]:%s/%d/%d, to:%s/%d/%d, ux_tags=%#x:%#x, state=%d, route=%d\n", (sync ? "b" : "ab"), \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, \
                get_ux_tags(to_task), get_ux_share_val(to_task), state, route);
        }

        return;
    }
}

static void binder_unset_ux_share(struct task_struct *task)
{
    u32 ux_share_data = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (unlikely(!task))
        return;

    ux_share_data = get_ux_share_val(task);
    if (!share_filed->binder_nr)
        return;

    unset_ux_share(task, UX_SHARE_BINDER);
}

void android_vh_binder_restore_priority_hook(void *data, struct binder_transaction *reply_to, struct task_struct *task)
{
    if (reply_to)
        binder_unset_ux_share(task);
}

void android_vh_binder_wait_for_work_hook(void *data, bool do_proc_work, struct binder_thread *thread, struct binder_proc *proc)
{
    if (!thread)
        return;

    if (do_proc_work)
        binder_unset_ux_share(thread->task);
}

void android_vh_sync_txn_recvd_hook(void *data, struct task_struct *to_task, struct task_struct *from_task)
{
    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE))
        return;

    binder_set_ux_share(to_task/* ignore */, to_task, from_task, true, BINDER_SETUX_ROUTE_RECVD);
}

__maybe_unused static void update_ux_abinder_node(struct task_struct *to_proc, struct task_struct *to_task, struct binder_transaction *bt)
{
    struct binder_node *node = NULL;

    if (!to_task || !to_proc || !bt || !(bt->buffer))
        return;

    node = bt->buffer->target_node;
    if ((g_ux_abinder.node == node) && (g_ux_abinder.to_proc == to_proc) && (g_ux_abinder.to_task == to_task))
        return;

    g_ux_abinder.node = node;
    g_ux_abinder.to_proc = to_proc;
    g_ux_abinder.to_task = to_task;
    g_ux_abinder.to_pid = to_task->pid;

    trans_debug(DEBUG_SHARE, "%s/%d/%d/%s, node=%#llx\n", to_task->comm, to_task->pid, to_task->tgid, to_proc->comm, (u64)(g_ux_abinder.node));
}

__maybe_unused static struct task_struct *find_abinder_thread_task(struct binder_proc *proc, struct binder_transaction *bt)
{
    int cnt = 0;
    enum ux_task_state state = 0;
    struct task_struct *task = NULL;
    struct binder_node *node = NULL;
    struct binder_thread *thread = NULL;
    struct rb_node *parent = NULL;
    struct rb_node **p_node = NULL;

    if (!bt || !(bt->buffer) || !proc || (proc->max_threads <= 0))
        return NULL;

    node = bt->buffer->target_node;
    if ((!node) || (node != g_ux_abinder.node) || (proc->tsk != g_ux_abinder.to_proc))
        return NULL;

    p_node = &proc->threads.rb_node;
    while (*p_node) {
        parent = *p_node;
        thread = rb_entry(parent, struct binder_thread, rb_node);
        if (!thread)
            break;

        if (g_ux_abinder.to_pid < thread->pid)
            p_node = &(*p_node)->rb_left;
        else if (g_ux_abinder.to_pid > thread->pid)
            p_node = &(*p_node)->rb_right;
        else {
            if (thread->task == g_ux_abinder.to_task)
                task = thread->task;

            break;
        }

        if (++cnt > FIND_ABINDER_THREADS_LIMIT)
            break;
    }

    if (task) {
        trans_debug(DEBUG_SHARE, "%s/%d/%d, ux_tags=%#x, node=%#llx, cnt=%d\n", \
            task->comm, task->pid, task->tgid, get_ux_tags(task), (u64)(node), cnt);

        if (is_ux_task(task, &state) || (state != UX_TASK_NO_TAG))
            return NULL;
    }

    return task;
}

static inline bool check_ux_transaction_flags(struct binder_transaction *bt, u32 flags)
{
    if (!bt)
        return false;

    return !!((bt->flags) & (flags));
}

__maybe_unused static void android_vh_binder_transaction_received_hook(void *data, struct binder_transaction *bt, \
    struct binder_proc *binder_proc, struct binder_thread *thread, uint32_t cmd)
{
    struct task_struct *to_proc = NULL;
    struct task_struct *to_task = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE))
        return;

    if ((!bt) || (!(bt->flags & TF_ONE_WAY)))
        return;

    if ((!binder_proc) || (!binder_proc->tsk) || (!thread) || (!thread->task))
        return;

    if (!check_ux_transaction_flags(bt, TF_UX_SF_ABINDER_WORK | TF_UX_ABINDER_WORK))
        return;

    to_proc = binder_proc->tsk;
    to_task = thread->task;
    binder_set_ux_share(to_proc, to_task, NULL, false, BINDER_SETUX_ROUTE_RECVD_ASYNC);

    if (check_ux_transaction_flags(bt, TF_UX_SF_ABINDER_WORK) && is_ux_task(to_proc, NULL))
        update_ux_abinder_node(to_proc, to_task, bt);
}

static inline bool check_user_app(struct task_struct *task)
{
    unsigned int uid = 0;

    if (unlikely(!task))
        return false;

    uid = (unsigned int)(task_uid(task).val);
    if ((uid > APP_UID_MIN) && (uid < APP_UID_MAX))
        return true;

    return false;
}

static inline bool check_system(struct task_struct *task)
{
    unsigned int uid = 0;

    if (unlikely(!task))
        return false;

    uid = (unsigned int)(task_uid(task).val);
    if (uid == SYSTEM_UID)
        return true;

    return false;
}


__maybe_unused static void set_uxbinder_transaction(struct task_struct *from_task, struct task_struct *to_proc, struct binder_transaction *bt)
{
    bool sync = false;

    if (!to_proc || !bt || !from_task)
        return;

    sync = !(bt->flags & TF_ONE_WAY);

    if (sync) {
        if (is_ux_task(from_task, NULL) || is_rt_task(from_task))
            bt->flags |= TF_UX_BINDER_WORK;

        goto out;
    }

    if (check_sf_abinder_target(from_task, false)) {
        if (is_ux_task(to_proc, NULL) || check_system(to_proc))
            bt->flags |= TF_UX_SF_ABINDER_WORK;

        goto out;
    }

    if (check_ux_abinder(get_ux_tags(from_task))) {
        if (check_user_app(to_proc))
            bt->flags |= TF_UX_ABINDER_WORK;

        goto out;
    }

out:
    if (check_ux_transaction_flags(bt, TF_UX_SF_ABINDER_WORK | TF_UX_ABINDER_WORK | TF_UX_BINDER_WORK))
        trans_debug(DEBUG_SHARE, "from:%s/%d/%d, to_proc:%s/%d/%d, ux_tags=%#x, flags=%#x\n",  \
            from_task->comm, from_task->pid, from_task->tgid, to_proc->comm, to_proc->pid, to_proc->tgid, \
            get_ux_tags(from_task), bt->flags);

    return;
}

__maybe_unused static void abinder_set_threads_ux_share(struct task_struct *from_task, struct binder_proc *proc, struct binder_transaction *bt)
{
    int cnt = 0;
    int uxcnt = 0;
    u32 ux_share_tags = 0;
    u32 ux_share_data = 0;
    enum ux_task_state state = 0;
    struct rb_node *rbnode = NULL;
    struct task_struct *to_task = NULL;
    struct binder_thread *thread = NULL;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    if (!bt || !proc || !from_task)
        return;

    if (check_ux_transaction_flags(bt, TF_UX_SF_ABINDER_WORK)) {
        ux_share_tags = get_ux_tags(proc->tsk);
        if (unlikely(!check_ux_tags_ok(ux_share_tags)))
            return;

        ux_share_tags = raise_ux_dprio(ux_share_tags);
        ux_share_tags = change_ux_slice(ux_share_tags, UX_TASK_SLICE0);
    }
    else {
        ux_share_tags = get_ux_tags(from_task);
        if (unlikely(!check_ux_tags_ok(ux_share_tags)))
            return;

        if (get_ux_share_state(ux_share_tags)) {
            if (!check_ux_share_type(ux_share_tags, UX_SHARE_BINDER))
                return;

            ux_share_data = get_ux_share_val(from_task);
            if (share_filed->binder_nr >= g_ux_share_limit[UX_SHARE_BINDER])
                return;
        }

        ux_share_tags = change_ux_slice(ux_share_tags, UX_TASK_SLICE1);
    }

    if (check_ux_flag(ux_share_tags, UX_FLAG_ABINDER))
        ux_share_tags = clear_ux_flag(ux_share_tags, UX_FLAG_ABINDER);

    for (rbnode = rb_first(&proc->threads); rbnode != NULL; rbnode = rb_next(rbnode)) {
        thread = rb_entry(rbnode, struct binder_thread, rb_node);
        if (thread && (thread->looper & (BINDER_LOOPER_STATE_ENTERED | BINDER_LOOPER_STATE_REGISTERED))) {
            to_task = thread->task;
            if (IS_ERR_OR_NULL(to_task))
                continue;

            if(is_ux_task(to_task, &state))
                continue;

            if (state != UX_TASK_NO_TAG)
                continue;

            set_ux_share(to_task, UX_SHARE_BINDER, ux_share_data, ux_share_tags);

            trans_debug(DEBUG_SHARE, "from:%s/%d/%d, to:%s/%d/%d/%s, %#x =>> %#x:%#x, slice=%llu, cnt=%d:%d\n", \
                from_task->comm, from_task->pid, from_task->tgid, to_task->comm, to_task->pid, to_task->tgid, proc->tsk->comm, \
                get_ux_tags(from_task), get_ux_tags(to_task), get_ux_share_val(to_task), ux_task_run_limit(to_task), uxcnt, cnt);

            if (++uxcnt >= ABINDER_SET_THREADS_LIMIT)
                break;
        }

        if (++cnt > FIND_BINDER_THREADS_LIMIT)
            break;
    }
}

bool check_uas_service(struct task_struct *task)
{
    if (get_ux_tags(task) & UX_FLAG_UAS_SERVICE)
        return true;

    return false;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
void android_vh_binder_proc_transaction_finish_hook(void *data, struct binder_proc *binder_proc, struct binder_transaction *bt, \
        struct task_struct *thread_task, bool pending_async, bool sync)
{
    struct task_struct *to_proc = NULL;
    struct task_struct *to_task = thread_task;
    struct task_struct *caller_task = current;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE))
        return;

    if (!binder_proc)
        return;

    to_proc = binder_proc->tsk;
    if (!to_proc)
        return;

    set_uxbinder_transaction(caller_task, to_proc, bt);
    if (check_uas_service(to_proc)) {
        if (to_task && (!check_uas_service(to_task)) && (to_proc->pid == to_task->tgid)) {
            set_ux_tags_to_task(to_task, UX_TASK_TAGS_UAS_SERVICE, "uas-service", true);

            return;
        }
    }

    if (!sync) {
        if (!check_ux_transaction_flags(bt, TF_UX_SF_ABINDER_WORK | TF_UX_ABINDER_WORK))
            return;

        if (!to_task) {
            to_task = find_abinder_thread_task(binder_proc, bt);
            if (to_task)
                binder_set_ux_share(to_proc, to_task, caller_task, sync, BINDER_SETUX_ROUTE_TRANSACTION_INFER);

            abinder_set_threads_ux_share(caller_task, binder_proc, bt);
            return;
        }
    }

    binder_set_ux_share(to_proc, to_task, caller_task, sync, BINDER_SETUX_ROUTE_TRANSACTION);
}
#else
void android_vh_binder_proc_transaction_end_hook(void *data, struct task_struct *caller_task, struct task_struct *proc_task, \
        struct task_struct *thread_task, unsigned int code, bool pending_async, bool sync)
{
    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_SHARE))
        return;

    if (!caller_task || !thread_task || !proc_task)
        return;

    if (!sync) {
        if (check_sf_abinder_target(caller_task, false) && (!is_ux_task(to_proc, NULL)))
            return;

        if (!check_ux_abinder(get_ux_tags(caller_task)))
            return;
    }

    binder_set_ux_share(proc_task, thread_task, caller_task, sync, BINDER_SETUX_ROUTE_TRANSACTION);
}
#endif /* KERNEL_VERSION(5, 15, 0) */

void binder_hook_init(void)
{
    int ret = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    ret = register_trace_android_vh_binder_proc_transaction_finish(android_vh_binder_proc_transaction_finish_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_binder_proc_transaction_finish failed!\n");
#else
    ret = register_trace_android_vh_binder_proc_transaction_end(android_vh_binder_proc_transaction_end_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_binder_proc_transaction_end failed!\n");
#endif /* KERNEL_VERSION(5, 15, 0) */

    ret = register_trace_android_vh_sync_txn_recvd(android_vh_sync_txn_recvd_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_sync_txn_recvd failed!\n");

    ret = register_trace_android_vh_binder_wait_for_work(android_vh_binder_wait_for_work_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_binder_wait_for_work failed!\n");

    ret = register_trace_android_vh_binder_restore_priority(android_vh_binder_restore_priority_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_binder_restore_priority failed!\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
    ret = register_trace_android_vh_binder_transaction_received(android_vh_binder_transaction_received_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_binder_transaction_received failed!\n");
#endif /* KERNEL_VERSION(5, 15, 0) */

    return;
}
