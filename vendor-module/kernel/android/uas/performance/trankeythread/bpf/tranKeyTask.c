#include <bpf_helpers.h>
#include <linux/bpf.h>
#include <string.h>
#include "include/tran_bpf_keytask.h"

DEFINE_BPF_MAP_GRW(dyn_enable_map, ARRAY, int, int, 1, AID_SYSTEM);

DEFINE_BPF_MAP_GRW(key_regex_cnt_map, ARRAY, int, int, 1, AID_SYSTEM);

DEFINE_BPF_MAP_GRW(key_comm_map, HASH, struct k_task_comm, int, 64, AID_SYSTEM);

DEFINE_BPF_MAP_GRW(key_task_map, HASH, int, struct k_task_info, 64, AID_SYSTEM);

DEFINE_BPF_MAP_GRW(report_task_map, HASH, int, int, 1024, AID_SYSTEM);

DEFINE_BPF_MAP_GRW(key_dep_list_map, HASH, int, struct k_task_dep_arr, 64, AID_SYSTEM);

DEFINE_BPF_RINGBUF_EXT(key_task_ringbuf, struct k_task_msg, 4096, AID_ROOT, AID_SYSTEM, 0660, "",
	DEFAULT_BPF_MAP_PIN_SUBDIR, PRIVATE, BPFLOADER_MIN_VER, BPFLOADER_MAX_VER,
	LOAD_ON_ENG, LOAD_ON_USER, LOAD_ON_USERDEBUG);

static unsigned long long (*bpf_get_current_task)(void) = (void *) BPF_FUNC_get_current_task;

#define BINDER_PERFIX       "binder:"
#define PF_KTHREAD		    0x00200000


static inline void read_comm(void *to, char *from)
{
    memset(to, 0, TASK_COMM_LEN);
    bpf_probe_read_str(to, TASK_COMM_LEN, from);
}

static inline bool is_black_comm(char *comm)
{
    if (!strncmp(comm, BINDER_PERFIX, 7)) {
        return true;
    }
    return false;
}


static inline bool current_is_kthread()
{
    uint32_t flags;
    char *task_addr = (char *)bpf_get_current_task();
    if (!task_addr) {
        return false;
    }

    bpf_probe_read(&flags, 4, task_addr + 68);
    return (flags & PF_KTHREAD) != 0;
}

static inline bool is_key_regex_comm_map_empty()
{
    int key = 0;
    int *cnt = bpf_key_regex_cnt_map_lookup_elem(&key);
    return (!cnt || *cnt == 0);
}

static inline bool is_dyn_enable()
{
    int key = 0;
    int *val;

    val = bpf_dyn_enable_map_lookup_elem(&key);
    if (!val) {
        return false;
    }
    return (*val) == 1;
}

void ringbuf_submit(struct k_task_info *k_task, struct k_task_info *wke_task, int action)
{
	struct k_task_msg *ring = bpf_key_task_ringbuf_reserve();
	if (!ring)
		return;
    ring->action = action;
    if (wke_task) {
        memcpy(&ring->wke_task, wke_task, sizeof(struct k_task_info));
    }
	memcpy(&ring->k_task, k_task, sizeof(struct k_task_info));
	bpf_key_task_ringbuf_submit(ring);
}

struct oom_score_adj_update_args {
    uint64_t ignore;
    int pid;
    char comm[TASK_COMM_LEN];
    short oom_score_adj;
};

DEFINE_BPF_PROG("tracepoint/oom/oom_score_adj_update", AID_ROOT, AID_SYSTEM, tp_oom_score_adj_update)
(struct oom_score_adj_update_args* args)
{
    int pid = args->pid;
    struct k_task_info task;

    /**
     * If the current thread is at the top,
     * then add this thread to the key_task_map.
     */
    if (is_top(args->oom_score_adj)) {
        task.pid = pid;
        task.tgid = pid;
        task.load = 0;
        read_comm(&task.comm, args->comm);
        bpf_key_task_map_update_elem(&pid, &task, BPF_NOEXIST);
        return 0;
    }

    /**
     * If a certain thread exits from the top,
     * then remove that thread from the key_task_map
     */
    bpf_key_task_map_delete_elem(&pid);
    bpf_key_dep_list_map_delete_elem(&pid);

    return 0;
}

static __always_inline bool
comm_equal(const char *a, const char *b)
{
    bool same = true;

#pragma unroll
    for (int i = 0; i < TASK_COMM_LEN; i++) {
        if (a[i] != b[i]) {
            same = false;
            break;
        }
    }

    return same;
}

struct task_rename_args {
	uint64_t ignore;
	int pid;
	char oldcomm[TASK_COMM_LEN];
	char newcomm[TASK_COMM_LEN];
	short oom_score_adj;
};

DEFINE_BPF_PROG("tracepoint/task/task_rename", AID_ROOT, AID_SYSTEM, tp_task_rename)
(struct task_rename_args *args)
{
    int *k_comm_val;
    int *old_k_comm_val;
    uint64_t cur_pid_tgid;
    int pid = args->pid, cur_pid, cur_tgid;
    struct k_task_info new_task, *key_task;
    struct k_task_comm *comm = &new_task.comm;
    struct k_task_info old_task;
    struct k_task_comm *old_comm = &old_task.comm;

    key_task = bpf_key_task_map_lookup_elem(&pid);
    /**
     * If this thread is in the key_task_map,
     * then update its comm.
     */
    if (key_task) {
        read_comm(&key_task->comm, args->newcomm);
        return 0;
    }

    cur_pid_tgid = bpf_get_current_pid_tgid();
    cur_pid      = cur_pid_tgid;
    if (pid != cur_pid) {
        return 0;
    }

    /**
     * If the key_task_map does not contain this thread,
     * but the new comm is in the key_comm_map, it is
     * necessary to determine whether this thread is at
     * the top before deciding whether to add it to the
     * key_task_map, and notify the user space that the
     * thread stored in the key_comm_map has been created.
     */
    read_comm(comm, args->newcomm);
    k_comm_val = bpf_key_comm_map_lookup_elem(comm);

    read_comm(old_comm, args->oldcomm);
    old_k_comm_val = bpf_key_comm_map_lookup_elem(old_comm);
    if (old_k_comm_val && bpf_report_task_map_lookup_elem(&pid)) {
        if (comm_equal((const char *)new_task.comm.comm,
                       (const char *)old_task.comm.comm)) {
            return 0;
        }
        // Read old comm into old_task
        read_comm(&old_task.comm, args->oldcomm);
        old_task.pid = pid;
        old_task.tgid = cur_pid_tgid >> 32;
        old_task.load = 0;

        // Notify user space to delete oldcomm from key_comm_map
        ringbuf_submit(&old_task, NULL, K_KEY_DEL);
        // Delete from report map
        bpf_report_task_map_delete_elem(&pid);
    }

    if (k_comm_val && !bpf_report_task_map_lookup_elem(&pid)) {
        cur_tgid = cur_pid_tgid >> 32;
        new_task.pid = pid;
        new_task.tgid = cur_tgid;
        new_task.load = 0;
        ringbuf_submit(&new_task, NULL, K_KEY_COMM);
        bpf_report_task_map_update_elem(&pid, &cur_tgid, BPF_ANY);
        if (*k_comm_val) {
            bpf_key_task_map_update_elem(&pid, &new_task, BPF_NOEXIST);
        }
    }
    if (!is_top(args->oom_score_adj)) {
        return 0;
    }
    if (!is_key_regex_comm_map_empty()) {
        cur_tgid = cur_pid_tgid >> 32;
        new_task.pid = pid;
        new_task.tgid = cur_tgid;
        new_task.load = 0;
        ringbuf_submit(&new_task, NULL, K_KEY_REGEX_COMM);
    }

	return 0;
}

struct sched_waking_args {
    uint64_t ignore;
    char comm[TASK_COMM_LEN];
    int pid;
    int prio;
    int tgt_cpu;
};

static inline int search_dep_update(struct k_task_dep_arr *dep_arr, struct k_task_info *wke_task, struct k_task_info *wkr_task)
{
    int i;
    struct k_task_info *pos_tsk;

    for (i = 0; i < MAX_DEP_ARR_SIZE; ++i) {
        if (i >= dep_arr->size) break;
        pos_tsk = &dep_arr->arr[i];
        if (pos_tsk->pid == wkr_task->pid
                && pos_tsk->tgid == wkr_task->tgid) {
            pos_tsk->load++;
            if (pos_tsk->load >= LOAD_THRESHOLD
                    && !bpf_report_task_map_lookup_elem(&pos_tsk->pid)) {
                /**
                 * If the current thread’s wake-up count exceeds the
                 * threshold within a short period of time and this
                 * thread has not been reported before, trigger dynamic
                 * identification to report this thread.
                 */
                ringbuf_submit(pos_tsk, wke_task, K_OVER_THR);
                bpf_report_task_map_update_elem(&pos_tsk->pid, &pos_tsk->tgid, BPF_ANY);
            }
            return i;
        }
    }
    return -1;
}

static inline void insert_task(struct k_task_info *wkr_task, struct k_task_dep_arr *dep_arr)
{
    struct k_task_info *ins_task;

    int size = dep_arr->size;
    int ins_idx = dep_arr->ins_idx;
    if (ins_idx < 0 || ins_idx >= MAX_DEP_ARR_SIZE) {
        return;
    }
    ins_task = &dep_arr->arr[ins_idx];
    memcpy(ins_task, wkr_task, sizeof(struct k_task_info));
    ins_task->load = 1;
    dep_arr->ins_idx = (ins_idx + 1) & (MAX_DEP_ARR_SIZE - 1);
    dep_arr->size = size >= MAX_DEP_ARR_SIZE ? MAX_DEP_ARR_SIZE : size + 1;
}

static inline void update_dep(struct k_task_dep_arr *dep_arr, struct k_task_info *wke_task, struct k_task_info *wkr_task)
{

    struct k_task_dep_arr new_dep_arr;

    if (!dep_arr) {
        memset(&new_dep_arr, 0, sizeof(struct k_task_dep_arr));
        insert_task(wkr_task, &new_dep_arr);
        bpf_key_dep_list_map_update_elem(&wke_task->pid, &new_dep_arr, BPF_NOEXIST);
        return;
    }


    if (search_dep_update(dep_arr, wke_task, wkr_task) != -1) {
        return;
    }

    insert_task(wkr_task, dep_arr);
}

DEFINE_BPF_PROG("tracepoint/sched/sched_waking", AID_ROOT, AID_SYSTEM, tp_sched_waking)
(struct sched_waking_args *args)
{
    uint64_t wkr_pid_tgid;
    int wke_pid = args->pid;
    struct k_task_dep_arr *dep_arr;
    struct k_task_info *wke_task, wkr_task;
    char *wkr_comm = (char *)&wkr_task.comm;

    if (!is_dyn_enable()) {
        return 0;
    }

    if (current_is_kthread()) {
        return 0;
    }

    wke_task = bpf_key_task_map_lookup_elem(&wke_pid);
    if (!wke_task) {
        return 0;
    }

    wkr_pid_tgid = bpf_get_current_pid_tgid();
    wkr_task.pid = wkr_pid_tgid;
    wkr_task.tgid = wkr_pid_tgid >> 32;

    if (wkr_task.tgid != wke_task->tgid) {
        return 0;
    }

    bpf_get_current_comm(wkr_comm, TASK_COMM_LEN);
    if (is_black_comm(wkr_comm)) {
        return 0;
    }

    dep_arr = bpf_key_dep_list_map_lookup_elem(&wke_pid);
    update_dep(dep_arr, wke_task, &wkr_task);

	return 0;
}

struct sched_process_free_args {
    uint64_t ignore;
    char comm[TASK_COMM_LEN];
    pid_t pid;
    int prio;
};

DEFINE_BPF_PROG("tracepoint/sched/sched_process_free", AID_ROOT, AID_SYSTEM, tp_sched_process_free)
(struct sched_process_free_args* args)
{
    int *tgid;
    int pid = args->pid;
    struct k_task_info task;


    bpf_key_task_map_delete_elem(&pid);
    bpf_key_dep_list_map_delete_elem(&pid);
    tgid = bpf_report_task_map_lookup_elem(&pid);
    if (tgid) {
        task.pid = pid;
        task.tgid = *tgid;
        task.load = 0;
        ringbuf_submit(&task, NULL, K_KEY_DEL);
        bpf_report_task_map_delete_elem(&pid);
    }

    return 0;
}

LICENSE("GPL");
