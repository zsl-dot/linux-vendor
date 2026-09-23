#ifndef _TRANS_FUTEX_H
#define _TRANS_FUTEX_H

void futex_hook_init(void);
int futex_set_ux_tags(struct task_struct *task, u32 ux_tags);

#endif /* _TRANS_FUTEX_H */