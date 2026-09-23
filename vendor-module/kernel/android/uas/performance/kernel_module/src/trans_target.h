#ifndef _TRANS_TARGET_H
#define _TRANS_TARGET_H

#define TARGET_THREAD_NAME    "K-target"

#define SYSTEM_SERVER_UID (1000)

struct ux_target {
    pid_t pid;
    u32 ux_tags;
    bool grp_leader;
    struct task_struct *task;
    char comm[TASK_COMM_LEN];
    char comm_size;
    u32 affinity;
};

struct ux_target_work {
    struct ux_target *target;
    struct work_struct work;
};

void ux_targets_work_init(void);
void ux_targets_work_deinit(void);
void dump_sf_targets(void);
bool check_sf_target(struct task_struct *task);
bool check_overlayengine_target(struct task_struct *task);
bool check_sf_abinder_target(struct task_struct *task, bool simple);
void set_ux_target_tags(pid_t pid, u32 ux_tags);
void task_rename_hook(void *data, struct task_struct *task, const char *comm);
#endif /* _TRANS_TARGET_H */