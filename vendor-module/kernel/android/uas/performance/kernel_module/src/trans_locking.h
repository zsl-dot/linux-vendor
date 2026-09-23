/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 transsion Inc.
 */

#ifndef _TRANS_LOCKING_H_
#define _TRANS_LOCKING_H_

//#define TRANS_LOCKING_DEBUG

#ifdef TRANS_LOCKING_DEBUG
#define lock_debug_systrace(...) trans_sched_systrace_c(__VA_ARGS__)
#define lock_debug_traceprintk(...) trace_printk(__VA_ARGS__)
#else
#define lock_debug_systrace(...)
#define lock_debug_traceprintk(...)
#endif

struct lock_stats {
    /*
     * total
     */
    u64 duration_ns;
    u64 dcounts;

    u64 dcounts_1ms;        /*  0ms ~ 1ms   */
    u64 dcounts_5ms;        /*  1ms ~ 5ms   */
    u64 dcounts_10ms;       /*  5ms ~ 10ms  */
    u64 dcounts_20ms;       /* 10ms ~ 20ms  */
    u64 dcounts_50ms;       /* 20ms ~ 50ms  */
    u64 dcounts_100ms;      /* 50ms ~ 100ms */
    u64 dcounts_long;       /*       >100ms */
};


struct trans_lock_operations {
    const char *lock_type;

    /* D state statistics*/
    struct lock_stats stats;

    int (*get_depth)(struct trans_task_struct *tts);
    void (*inc_depth)(struct trans_task_struct *tts);
    void (*dec_depth)(struct trans_task_struct *tts);
    void (*set_depth)(struct trans_task_struct *tts, int val);

    unsigned long (*get_lock_start)(struct trans_task_struct *tts);
    void (*set_lock_start)(struct trans_task_struct *tts, unsigned long val);

    void (*inc_dcounts)(struct trans_task_struct *tts);
    u64 (*get_dcounts)(struct trans_task_struct *tts);

    void (*set_dstart)(struct trans_task_struct *tts, u64 val);
    u64 (*get_dstart)(struct trans_task_struct *tts);

    void (*set_duration)(struct trans_task_struct *tts, u64 val);
    void (*add_duration)(struct trans_task_struct *tts, u64 val);
    u64 (*get_duration)(struct trans_task_struct *tts);
};


#define DECLARE_LOCK_OPTIONS(type)                                              \
    static inline int                                                           \
    get_depth_##type(struct trans_task_struct *tts)                             \
    {                                                                           \
        return tts->lockinfo.type.depth;                                        \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    inc_depth_##type(struct trans_task_struct *tts)                             \
    {                                                                           \
        tts->lockinfo.type.depth++;                                             \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    dec_depth_##type(struct trans_task_struct *tts)                             \
    {                                                                           \
        tts->lockinfo.type.depth--;                                             \
        if (tts->lockinfo.type.depth < 0)                                       \
            tts->lockinfo.type.depth = 0;                                       \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    set_depth_##type(struct trans_task_struct *tts, int val)                    \
    {                                                                           \
        tts->lockinfo.type.depth = val;                                         \
    }                                                                           \
                                                                                \
    static inline unsigned long                                                 \
    get_lock_start_##type(struct trans_task_struct *tts)                        \
    {                                                                           \
        return tts->lockinfo.type.lock_start;                                   \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    set_lock_start_##type(struct trans_task_struct *tts, unsigned long val)     \
    {                                                                           \
        tts->lockinfo.type.lock_start = val;                                    \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    inc_dcounts_##type(struct trans_task_struct *tts)                           \
    {                                                                           \
        tts->lockinfo.type.dcounts++;                                           \
    }                                                                           \
                                                                                \
    static inline u64                                                           \
    get_dcounts_##type(struct trans_task_struct *tts)                           \
    {                                                                           \
        return tts->lockinfo.type.dcounts;                                      \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    set_dstart_##type(struct trans_task_struct *tts, u64 val)                   \
    {                                                                           \
        tts->lockinfo.type.dstart = val;                                        \
    }                                                                           \
                                                                                \
    static inline u64                                                           \
    get_dstart_##type(struct trans_task_struct *tts)                            \
    {                                                                           \
        return tts->lockinfo.type.dstart;                                       \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    set_duration_##type(struct trans_task_struct *tts, u64 val)                 \
    {                                                                           \
        tts->lockinfo.type.duration = val;                                      \
    }                                                                           \
                                                                                \
    static inline void                                                          \
    add_duration_##type(struct trans_task_struct *tts, u64 val)                 \
    {                                                                           \
        tts->lockinfo.type.duration += val;                                     \
    }                                                                           \
                                                                                \
    static inline u64                                                           \
    get_duration_##type(struct trans_task_struct *tts)                          \
    {                                                                           \
        return tts->lockinfo.type.duration;                                     \
    }                                                                           \
                                                                                \
    static struct trans_lock_operations type = {                                \
        .lock_type = #type,                                                     \
        .get_depth = get_depth_##type,                                          \
        .inc_depth = inc_depth_##type,                                          \
        .dec_depth = dec_depth_##type,                                          \
        .set_depth = set_depth_##type,                                          \
        .get_lock_start = get_lock_start_##type,                                \
        .set_lock_start = set_lock_start_##type,                                \
        .inc_dcounts = inc_dcounts_##type,                                      \
        .get_dcounts = get_dcounts_##type,                                      \
        .set_dstart = set_dstart_##type,                                        \
        .get_dstart = get_dstart_##type,                                        \
        .set_duration = set_duration_##type,                                    \
        .add_duration = add_duration_##type,                                    \
        .get_duration = get_duration_##type,                                    \
    };                                                                          \

enum cpu_load_state {
    LOW_LOAD,
    HIGH_LOAD
};

#define HIGH_LOAD_NR_RUNNING                2
#define LOW_LOAD_NR_RUNNING                 1
#define CONSECUTIVE_COUNT                   5

void update_cpu_load(struct rq *rq);
bool is_cpu_high_load(int cpu);
bool is_cpu_low_load(int cpu);


#define LOW_PRIORITY                        120
static inline bool task_low_prio(struct task_struct *tsk)
{
    return tsk->prio > LOW_PRIORITY;
}

int register_trans_locking_hook(void);
void unregister_trans_locking_hook(void);
bool task_inlock(struct task_struct *tsk);
bool task_inlock_protect(struct task_struct *tsk);
struct proc_dir_entry * lock_status_proc_init(struct proc_dir_entry *pdir);
void lock_status_proc_deinit(void);
void trans_set_next_task(struct task_struct *tsk);
void trans_pick_next_lock_task(struct rq *rq, struct task_struct **p,
            struct sched_entity **se, bool *repick, bool simple);
void dequeue_locktask_on_fork(struct trans_task_struct *tts);
void dequeue_locking_task(struct rq *rq, struct task_struct *tsk);
void enqueue_locking_task(struct rq *rq, struct task_struct *tsk);
void sched_switch_reset_lock_info(struct task_struct *prev,
            struct task_struct *next);

extern int lock_tuner;
#define TUNER_CONTINUOUS                    0x01
#define TUNER_PREEMPT                       0x02
#define TUNER_PICK_LOCK_TASK                0x04

#define TUNER_ENABLE_ON_HIGHLOAD            0x10
#define TUNE_CONSIDER_RUNNING_TIME          0x20


#endif /* _TRANS_LOCKING_H_ */