#include <linux/delay.h>
#include <linux/workqueue.h>

#include "sched_common.h"
#include "trans_target.h"
#include "trans_sched_info.h"


static struct ux_target g_sf_targets[] = {
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF,
        .grp_leader = true,
        .task = NULL,
        .comm = "surfaceflinger",
        .comm_size = sizeof("surfaceflinger") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF,
        .grp_leader = false,
        .task = NULL,
        .comm = "RenderEngine",
        .comm_size = sizeof("RenderEngine") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF_RELATED,
        .grp_leader = false,
        .task = NULL,
        .comm = "OverlayEngine_0",
        .comm_size = sizeof("OverlayEngine_0") - 1,
        .affinity = 0,
    },
};

static struct ux_target g_sf_abinder_targets[] = {
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF_ABINDER,
        .grp_leader = false,
        .task = NULL,
        .comm = "surfaceflinger",
        .comm_size = sizeof("surfaceflinger") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF_ABINDER,
        .grp_leader = false,
        .task = NULL,
        .comm = "BckgrndExec HP",
        .comm_size = sizeof("BckgrndExec HP") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_SF_ABINDER,
        .grp_leader = false,
        .task = NULL,
        .comm = "BckgrndExec WL",
        .comm_size = sizeof("BckgrndExec WL") - 1,
        .affinity = 0,
    },
};

static struct ux_target g_apps_targets[] = {
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "mali-cpu-comman",
        .comm_size = sizeof("mali-cpu-comman") - 1,
        .affinity = UX_FLAG_CPU_L,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "mali-event-hand",
        .comm_size = sizeof("mali-event-hand") - 1,
        .affinity = UX_FLAG_CPU_L,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "mali-cmar-backe",
        .comm_size = sizeof("mali-cmar-backe") - 1,
        .affinity = UX_FLAG_CPU_L,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "hwuiTask",
        .comm_size = sizeof("hwuiTask") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "UxRenderAllocBu",
        .comm_size = sizeof("UxRenderAllocBu") - 1,
        .affinity = 0,
    },
    {
        .pid = -1,
        .ux_tags = UX_TASK_TAGS_NONE,
        .grp_leader = false,
        .task = NULL,
        .comm = "mali-compiler",
        .comm_size = sizeof("mali-compiler") - 1,
        .affinity = 0,
    },
};

#define SF_TARGETS_NUM         (ARRAY_SIZE(g_sf_targets))
#define SF_ABINDER_TARGETS_NUM (ARRAY_SIZE(g_sf_abinder_targets))

#define APPS_TARGETS_SLOTS     (2)
#define APPS_TARGETS_NUM       (ARRAY_SIZE(g_apps_targets))

struct ux_target_work g_sf_targets_work = {0};
struct ux_target_work g_sf_abinder_targets_work = {0};

static void ux_targets_work_func(struct work_struct *work);


void ux_targets_work_init(void)
{
    INIT_WORK(&g_sf_targets_work.work, ux_targets_work_func);
    INIT_WORK(&g_sf_abinder_targets_work.work, ux_targets_work_func);
}

void ux_targets_work_deinit(void)
{
    cancel_work_sync(&g_sf_targets_work.work);
    cancel_work_sync(&g_sf_abinder_targets_work.work);
}

void dump_sf_targets(void)
{
    int idx = 0;

    for (idx = 0; idx < SF_TARGETS_NUM; idx++) {
        if (g_sf_targets[idx].ux_tags == UX_TASK_TAGS_SF) {
            trans_info("sf targets: %s/%d\n", g_sf_targets[idx].comm, g_sf_targets[idx].pid);
        }
        else if (g_sf_targets[idx].ux_tags == UX_TASK_TAGS_SF_RELATED) {
            trans_info("sf related targets: %s/%d\n", g_sf_targets[idx].comm, g_sf_targets[idx].pid);
        }
    }

    for (idx = 0; idx < SF_ABINDER_TARGETS_NUM; idx++) {
        trans_info("sf abinder targets: %s/%d\n", g_sf_abinder_targets[idx].comm, g_sf_abinder_targets[idx].pid);
    }
}

bool check_sf_target(struct task_struct *task)
{
    if (unlikely(!task)) {
        return false;
    }

    if (check_ux_tags(task, UX_TASK_TAGS_SF)) {
        return true;
    }

    return false;
}

bool check_overlayengine_target(struct task_struct *task)
{
    if (unlikely(!task)) {
        return false;
    }

    if (check_ux_tags(task, UX_TASK_TAGS_SF_RELATED)) {
        return true;
    }

    return false;
}

static void ux_targets_work_func(struct work_struct *work)
{
    struct rq *rq = NULL;
    struct rq_flags flags = {0};
    struct task_struct *task = NULL;
    struct ux_target *target = NULL;
    struct ux_target_work *target_work = NULL;

    if (!work) {
        return;
    }

    target_work = container_of(work, struct ux_target_work, work);
    if (IS_ERR_OR_NULL(target_work)) {
        return;
    }

    target = target_work->target;
    if (IS_ERR_OR_NULL(target)) {
        return;
    }

    rcu_read_lock();
    task = find_task_by_vpid(target->pid);
    if (!task) {
        goto unlock_rcu;
    }
    get_task_struct(task);

    if (target->task != task) {
        goto put_task;
    }

    if (!trans_task_struct_create(task)) {
        goto put_task;
    }

    rq = task_rq_lock(task, &flags);
    if (!is_trans_task_struct_init(task)) {
        if (trans_task_struct_init(task)) {
            goto out;
        }
    }

    set_ux_tags(task, target->ux_tags);
    target_work->target = NULL;

out:
    task_rq_unlock(rq, task, &flags);
put_task:
    put_task_struct(task);
unlock_rcu:
    rcu_read_unlock();

    return;
}

static int sf_targets_init(struct task_struct *task, const char *comm)
{
    int idx = 0;

    for (idx = 0; idx < SF_TARGETS_NUM; idx++) {
        if (!strncmp(comm, g_sf_targets[idx].comm, g_sf_targets[idx].comm_size)) {
            if (g_sf_targets[idx].grp_leader) {
                if (task->pid != task->tgid) {
                    break;
                }
            }
            else {
                if (task->pid == task->tgid) {
                    break;
                }
            }

            g_sf_targets[idx].pid = task->pid;
            g_sf_targets[idx].task = task;

            WRITE_ONCE(g_sf_targets_work.target, (struct ux_target *)(&g_sf_targets[idx]));
            schedule_work(&g_sf_targets_work.work);
            return 0;
        }
    }

    return -ESRCH;
}

static int sf_abinder_targets_init(struct task_struct *task)
{
    int idx = 0;

    if (!is_rt_task(task)) {
        goto out;
    }

    for (idx = 0; idx < SF_ABINDER_TARGETS_NUM; idx++) {
        if (!strncmp(task->comm, g_sf_abinder_targets[idx].comm, g_sf_abinder_targets[idx].comm_size)) {
            if (g_sf_abinder_targets[idx].grp_leader) {
                if (task->pid != task->tgid) {
                    goto out;
                }
            }
            else {
                if (task->pid == task->tgid) {
                    goto out;
                }
            }

            g_sf_abinder_targets[idx].pid = task->pid;
            g_sf_abinder_targets[idx].task = task;

            WRITE_ONCE(g_sf_abinder_targets_work.target, (struct ux_target *)(&g_sf_abinder_targets[idx]));
            schedule_work(&g_sf_abinder_targets_work.work);
            return 0;
        }
    }

out:
    return -ESRCH;
}

bool check_sf_abinder_target(struct task_struct *task, bool simple)
{
    if (unlikely(!task)) {
        return false;
    }

    if (check_ux_tags(task, UX_TASK_TAGS_SF_ABINDER)) {
        return true;
    }

    if (simple) {
        return false;
    }

    if (check_ux_tags(task, UX_TASK_TAGS_SF)) {
        return false;
    }

    if (!sf_abinder_targets_init(task)) {
        return true;
    }

    return false;
}

static struct task_struct *get_task_grpleader(struct task_struct *task)
{
    struct task_struct *thread = task;

    do {
        if (task->tgid == thread->pid) {
            return thread;
        }
    } while_each_thread(task, thread);

    return NULL;
}

static void apps_targets_init_tags(struct task_struct *task, const char *comm)
{
    int idx = 0;
    enum ux_task_state state = 0;
    struct task_struct *grpleader = NULL;
    u32 target_tags = UX_TASK_TAGS_NONE;

    if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_TARGET)) {
        return;
    }

    if (!is_fair_task(task)) {
        return;
    }

    for (idx = 0; idx < APPS_TARGETS_NUM; idx++) {
        if (!strncmp(comm, g_apps_targets[idx].comm, g_apps_targets[idx].comm_size)) {
            if (g_apps_targets[idx].grp_leader) {
                if (task->pid != task->tgid) {
                    break;
                }
            }
            else {
                if (task->pid == task->tgid) {
                    break;
                }
            }

            grpleader = get_task_grpleader(task);
            if (!grpleader) {
                return;
            }

            if (is_ux_task(grpleader, &state) || (state == UX_TASK_TIMEOUT)) {
                target_tags = g_apps_targets[idx].ux_tags ? g_apps_targets[idx].ux_tags : get_ux_tags(grpleader);
                if (target_tags && g_apps_targets[idx].affinity) {
                    target_tags |= g_apps_targets[idx].affinity;
                }
                set_ux_tags_to_task(task, ignore_ux_dprio(target_tags), TARGET_THREAD_NAME, true);
                trans_debug(DEBUG_KLOG, "apps targets init: %s/%d/%d/%s, ux_tags=%#x\n", comm, task->pid, task->tgid, grpleader->comm, target_tags);
            }
            return;
        }
    }
}

void task_rename_hook(void *data, struct task_struct *task, const char *comm)
{
    int ret;

    if (!task || !comm) {
        return;
    }

    if (!task->mm) {
        return;
    }

    if (unlikely(task_uid(task).val == SYSTEM_SERVER_UID)) {
        ret = sf_targets_init(task, comm);
        if (!ret) {
            return;
        }
    }

    apps_targets_init_tags(task, comm);
}

static int update_apps_targets(struct task_struct *task, struct ux_target *target, int slots)
{
    int slot = 0;
    struct task_struct *thread = task;

    if (unlikely(!task || !target || (slots <= 0))) {
        return 0;
    }

    do {
        if (!strncmp(thread->comm, target[slot].comm, target[slot].comm_size)) {
            target[slot].pid = thread->pid;
            target[slot].task = thread;
            slot += 1;

            if (slot >= slots)
                return slot;
        }
    } while_each_thread(task, thread);

    return slot;
}

static void set_apps_targets_tags(struct task_struct *task, u32 ux_tags)
{
    int idx = 0;
    int slot = 0;
    int slots = 0;
    u32 target_tags = ux_tags;
    struct ux_target apps_targets[APPS_TARGETS_NUM + 1][APPS_TARGETS_SLOTS + 1] = {{0}, {0}};

    if (check_ux_tags_ok(ux_tags)) {
        if (!check_uas_feature(FEATURE_DEFAULT) || !check_uas_feature(FEATURE_UX_TARGET)) {
            return;
        }
    }

    if (unlikely(!task)) {
        return;
    }

    for (idx = 0; idx < APPS_TARGETS_NUM; idx++) {
        for (slot = 0; slot < APPS_TARGETS_SLOTS; slot++) {
            memcpy(&apps_targets[idx][slot], &g_apps_targets[idx], sizeof(struct ux_target));
        }
    }

    for (idx = 0; idx < APPS_TARGETS_NUM; idx++) {
        slots = update_apps_targets(task, &apps_targets[idx][0], APPS_TARGETS_SLOTS);

        for (slot = 0; slot < slots; slot++) {
            if ((apps_targets[idx][slot].pid <= 0) || (!apps_targets[idx][slot].task)) {
                continue;
            }

            if (target_tags && apps_targets[idx][slot].ux_tags) {
                target_tags = apps_targets[idx][slot].ux_tags;
            }

            if (target_tags && g_apps_targets[idx].affinity) {
                target_tags = target_tags | apps_targets[idx][slot].affinity;
            }

            trans_debug(DEBUG_KLOG, "apps targets[%d][%d]:[%s]: task:%s/%d, target:%s/%d/%#x\n", idx, slot, target_tags ? "set" : "unset", \
                task->comm, task->pid, apps_targets[idx][slot].comm, apps_targets[idx][slot].pid, target_tags);
            set_ux_tags_by_pid(apps_targets[idx][slot].task, apps_targets[idx][slot].pid, target_tags, TARGET_THREAD_NAME);
        }
    }
}

void set_ux_target_tags(pid_t pid, u32 ux_tags)
{
    struct task_struct *task = NULL;

    if (check_ux_same_type(ux_tags, UX_TASK_TAGS_FUTEX))
        return;

    if (check_ux_same_type(ux_tags, UX_TASK_TAGS_ABINDER_BIT))
        return;

    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        goto unlock_rcu;
    }

    get_task_struct(task);

    if (task->pid != task->tgid) {
        goto out;
    }

    if ((!check_unset_ux_tags(ux_tags)) && (!is_fair_task(task))) {
        goto out;
    }

    set_apps_targets_tags(task, ux_tags);

    if (check_unset_ux_tags(ux_tags)) {
        ux_account_book_audit();
    }

out:
    put_task_struct(task);
unlock_rcu:
    rcu_read_unlock();
}

