#ifndef _TRAN_BPF_KEY_TASK_H_
#define _TRAN_BPF_KEY_TASK_H_

#include <stdint.h>
#include <stdbool.h>

#define MAX_DEP_ARR_SIZE    8
#define TASK_COMM_LEN       16
#define MAX_TASK_LOAD       ~0ULL
#define ENABLE_KEY          0
#define LOAD_THRESHOLD      120
#define KEY_COMM_TYPE_STATIC 0
#define KEY_COMM_TYPE_DYNAMIC 1
#define KEY_COMM_TYPE_REGEX 2

#define BPF_FS_DIR_PATH "/sys/fs/bpf/"



enum k_task_action {
    K_OVER_THR = 0,
    K_KEY_COMM,
    K_KEY_DEL,
    K_KEY_REGEX_COMM,
};

struct k_task_comm {
    uint64_t comm[2];
};

struct k_task_info {
    int pid;
    int tgid;
    uint64_t load;
    struct k_task_comm comm;
};

struct k_task_dep_arr {
    int size;
    int ins_idx;
    struct k_task_info arr[MAX_DEP_ARR_SIZE];
};

struct k_task_msg {
    int action;
    struct k_task_info wke_task;
    struct k_task_info k_task;
};

static inline bool is_top(short adj)
{
    return (adj >= 0 && adj < 100)||(adj == -1000);
}

#endif /* _TRAN_BPF_KEY_TASK_H_ */

