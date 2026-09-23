#include <linux/plist.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <linux/uaccess.h>
#include <linux/futex.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <trace/hooks/futex.h>
#endif

#include "sched_common.h"
#include "trans_futex.h"

#define FUTEX_WAITER_TIMEOUT  (200000000)

struct futex_q {
    struct plist_node list;

    struct task_struct *task;
    spinlock_t *lock_ptr;
    union futex_key key;
    void *pi_state;
    void *rt_waiter;
    union futex_key *requeue_pi_key;
    u32 bitset;
} __randomize_layout;


static void futex_set_ux_share(struct task_struct *task, u32 ux_tags)
{
    u32 ux_share_tags = 0;
    enum ux_task_state state = 0;
    u32 ux_task_slice = UX_TASK_SLICE3;

    if(is_ux_task(task, &state)) {
        if(check_ux_share_type(get_ux_tags(task), UX_SHARE_FUTEX))
            update_ux_share_hold(task, UX_SHARE_FUTEX);

        return;
    }

    if ((state == UX_TASK_NO_TAG) || (state == UX_TASK_NOT_FAIR)) {
        ux_tags = raise_ux_dprio(init_ux_dprio(ux_tags));
        ux_share_tags = change_ux_slice(ux_tags, ux_task_slice);

        set_ux_share(task, UX_SHARE_FUTEX, 0, ux_share_tags);
    }

    return;
}

static void futex_unset_ux_share(struct task_struct *task)
{
    u32 ux_share_data = 0;
    struct ux_share *share_filed = (struct ux_share *)&ux_share_data;

    ux_share_data = get_ux_share_val(task);
    if (!share_filed->futex_nr)
        return;

    unset_ux_share(task, UX_SHARE_FUTEX);
}

int futex_set_ux_tags(struct task_struct *task, u32 ux_tags)
{
    if (unlikely(!task))
        return -EINVAL;

    if (!check_ux_same_type(ux_tags, UX_TASK_TAGS_FUTEX))
        return -EACCES;

    // set
    if (ux_tags == UX_TASK_TAGS_FUTEX) {
        futex_set_ux_share(task, ux_tags);
        return 0;
    }

    // unset
    if (check_unset_ux_tags(ux_tags))
        futex_unset_ux_share(task);

    return 0;
}

static inline int trans_futex_match(union futex_key *key1, union futex_key *key2)
{
    return (key1 && key2
        && key1->both.word == key2->both.word
        && key1->both.ptr == key2->both.ptr
        && key1->both.offset == key2->both.offset);
}

static struct futex_q *get_top_fair_waiter(struct plist_head *head, union futex_key *key)
{
    struct futex_q *this = NULL;
    struct futex_q *next = NULL;

    plist_for_each_entry_safe(this, next, head, list) {
        if (trans_futex_match(&this->key, key)) {
            if (this->list.prio == MAX_RT_PRIO)
                return this;
        }
    }

    return NULL;
}

static u64 get_rq_clock(struct task_struct *task)
{
    u64 rq_time = 0;
    struct rq *rq = NULL;

    rq = task_rq(task);
    if (!IS_ERR_OR_NULL(rq))
        rq_time = rq->clock;

    return rq_time;
}

static bool check_waiter_timeout(struct task_struct *task, u64 timeout, u64 *value)
{
    u64 delta = 0;
    u64 sleep_start = 0;
    struct sched_statistics *stats = NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    stats = __schedstats_from_se(&task->se);
#else
    stats = &task->se.statistics;
#endif

    sleep_start = schedstat_val(stats->sleep_start);

    if (sleep_start) {
        delta = get_rq_clock(task) - sleep_start;

        if ((s64)delta < 0)
            delta = 0;

        if (unlikely(delta > timeout)) {
            *value = delta;
            return true;
        }
    }

    return false;
}

static void dump_futex_list(struct plist_head *head, union futex_key *key)
{
    bool key_match = false;
    unsigned int cnt = 0;
    struct futex_q *this = NULL;
    struct futex_q *next = NULL;
    struct task_struct *task = NULL;

    plist_for_each_entry_safe(this, next, head, list) {
        key_match = trans_futex_match(&this->key, key);

        task = this->task;
        if (IS_ERR_OR_NULL(task))
            task = current;

        trans_debug(DEBUG_FUTEX, "waiter[%d][%d]:%s/%d/%d, tags=%d/%d/%#x\n", key_match, cnt++, task->comm, \
            task->pid, task->tgid, this->list.prio, task->prio, get_ux_tags(task));
    }
}

void trans_plist_add(struct plist_node *node, struct plist_head *head)
{
    struct plist_node *first, *iter, *prev = NULL;
    struct list_head *node_next = &head->node_list;

    WARN_ON(!plist_node_empty(node));
    WARN_ON(!list_empty(&node->prio_list));

    if (plist_head_empty(head))
        goto ins_node;

    first = iter = plist_first(head);

    do {
        if (node->prio < iter->prio) {
            node_next = &iter->node_list;
            break;
        }

        prev = iter;
        iter = list_entry(iter->prio_list.next, struct plist_node, prio_list);
    } while (iter != first);

    if (!prev || prev->prio != node->prio)
        list_add_tail(&node->prio_list, &iter->prio_list);

ins_node:
    list_add_tail(&node->node_list, node_next);
}

static void android_vh_alter_futex_plist_add_hook(void *data, struct plist_node *node, struct plist_head *head, bool *already_on_hb)
{
    u64 value = 0;
    struct futex_q *waiter = NULL;
    struct futex_q *fair_waiter = NULL;
    struct task_struct *curr = current;
    struct task_struct *task = NULL;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_FUTEX))
        return;

    if (unlikely(!node || !head || *already_on_hb))
        return;

    if (!is_ux_task(curr, NULL))
        return;

    waiter = container_of(node, struct futex_q, list);
    if (IS_ERR_OR_NULL(waiter))
        return;

    fair_waiter = get_top_fair_waiter(head, &waiter->key);
    if (fair_waiter) {
        task = fair_waiter->task;
        if (task && check_waiter_timeout(task, FUTEX_WAITER_TIMEOUT, &value)) {
            trans_debug(DEBUG_FUTEX, "timeout:%s/%d/%d, tags=%d/%d/%#x, value=%llu\n", task->comm, task->pid, task->tgid, fair_waiter->list.prio, task->prio, get_ux_tags(task), value);
            return;
        }
    }

    plist_node_init(node, min(curr->normal_prio, MAX_RT_PRIO - 1));
    trans_plist_add(node, head);
    *already_on_hb = true;

    if (g_trans_sched_debug)
        dump_futex_list(head, &waiter->key);
}

void futex_hook_init(void)
{
    int ret = 0;

    ret = register_trace_android_vh_alter_futex_plist_add(android_vh_alter_futex_plist_add_hook, NULL);
    if (ret)
        trans_warn("register trace_android_vh_alter_futex_plist_add failed!\n");

    return;
}
