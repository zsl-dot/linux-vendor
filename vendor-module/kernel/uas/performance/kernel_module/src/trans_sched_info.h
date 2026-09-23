#ifndef _TRANS_SCHED_INFO_H
#define _TRANS_SCHED_INFO_H
#include <linux/version.h>
#include "trans_trace.h"

#define RECORD_UX       (0x01)
#define RE_RECORD_UX    (0x10)
#define UNRECORD_UX     (0x11)

#define THREAD_NAME_LEN (64)

#define UX_EXACT_HEAD      "ux_"
#define UX_EXACT_HEAD_LEN  (3)

void ux_account_init(void);
void ux_account_deinit(void);
int get_accent_book_ux_nr(void);
int record_ux(struct task_struct *task, char *thread_name);
int delete_ux_record(struct task_struct *task);
int get_threadid_ref(char *thread_name, int *ref);
void update_ux_account(struct task_struct *task, char *thread_name, int record_cmd);
void ux_account_book_audit(void);
void dump_ux_record(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
void sched_stat_blocked_hook(void *data, struct task_struct *task, u64 delay_ns);
static inline bool uas_trace_enabled(void)
{
    return trace_dump_info_enabled();
}
#else
static inline bool __maybe_unused uas_trace_enabled(void) { return true; }
#endif

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)))
int trace_points_init(void);
void trace_points_deinit(void);
void update_stats_enqueue_rt(struct rq *rq, struct task_struct *task, int flags);
void update_stats_dequeue_rt(struct rq *rq, struct task_struct *task, int flags);
#else
static int __maybe_unused trace_points_init(void) { return 0; }
static void __maybe_unused trace_points_deinit(void) {}
static void __maybe_unused update_stats_enqueue_rt(struct rq *rq, struct task_struct *task, int flags) {}
static void __maybe_unused update_stats_dequeue_rt(struct rq *rq, struct task_struct *task, int flags) {}
#endif

#endif /* _TRANS_SCHED_INFO_H */
