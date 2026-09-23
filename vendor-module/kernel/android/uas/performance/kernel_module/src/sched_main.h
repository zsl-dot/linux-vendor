#ifndef _TRANS_SCHED_MAIN_H
#define _TRANS_SCHED_MAIN_H

#include <linux/types.h>


struct ux_data {
    pid_t pid;
    u32 ux_tags;
    int ref;
    char *thread_name;
};

struct ux_info {
    pid_t pid;
    char __user *info;
    unsigned int info_len;
};

struct ux_prio {
    pid_t pid;
    unsigned int shift;
};

#define TRANS_SCHED_MAGIC    'T'

#define TRANS_GET_SCHED_STATE   _IOR(TRANS_SCHED_MAGIC, 0x01, int)
#define TRANS_SET_SCHED_STATE   _IOW(TRANS_SCHED_MAGIC, 0x02, int)
#define TRANS_SET_PROC_UX_TAGS  _IOW(TRANS_SCHED_MAGIC, 0x03, struct ux_data)
#define TRANS_SET_SCHED_SCENE   _IOW(TRANS_SCHED_MAGIC, 0x10, unsigned int)
#define TRANS_GET_SCHED_SCENE   _IOR(TRANS_SCHED_MAGIC, 0x11, unsigned int)
#define TRANS_SET_UX_PRIO      _IOW(TRANS_SCHED_MAGIC, 0x12, struct ux_prio)
#define TRANS_SET_UX_TAGS       _IOW(TRANS_SCHED_MAGIC, 0x13, struct ux_data)
#define TRANS_GET_UX_TAGS       _IOR(TRANS_SCHED_MAGIC, 0x14, struct ux_data)
#define TRANS_GET_UX_INFO       _IOR(TRANS_SCHED_MAGIC, 0x15, struct ux_info)
#define TRANS_DUMP_UX_LIST      _IO(TRANS_SCHED_MAGIC, 0x16)

#endif /* _TRANS_SCHED_MAIN_H */