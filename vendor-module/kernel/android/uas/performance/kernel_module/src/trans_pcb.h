#ifndef _TRANS_PCB_H
#define _TRANS_PCB_H

#define TRANS_PCB_OFFSET (0)

#define TRANS_PCB_COMM_LEN (20)

struct trans_pcb_data {
    struct kmem_cache *cache;
    char name[TRANS_PCB_COMM_LEN];
};

int trans_pcb_init(void);
void trans_pcb_deinit(void);
int trans_pcb_alloc(struct task_struct *task);
void android_vh_free_task_hook(void *data, struct task_struct *task);

#endif /* _TRANS_PCB_H */