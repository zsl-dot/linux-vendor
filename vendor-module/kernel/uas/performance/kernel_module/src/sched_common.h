#ifndef _TRANS_SCHED_COMMON_H
#define _TRANS_SCHED_COMMON_H

#include <linux/printk.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
#include <linux/tracepoint.h>
#endif

#include "trans_xmu.h"
#include "trans_pcb.h"

extern u32 g_sched_scene;
extern int g_trans_sched_debug;
extern struct ux_sched_domains g_ux_sched_domains;
extern struct mutex g_mutex_sched_state;
extern int g_ux_share_limit[];

enum {
	__FEATURE_DEFAULT		= 0,
	__FEATURE_UX_SHARE		= 1,
	__FEATURE_UX_TARGET		= 2,
	__FEATURE_SELECT_CPUS		= 3,
	__FEATURE_DINFO			= 4,
	__FEATURE_FUTEX			= 5,
	__FEATURE_LOCK_PROTECT		= 6,
	__FEATURE_LOAD_TRACK		= 7,
	__FEATURE_KWORKER		= 8,
	__FEATURE_LOADBALANCE		= 9,
	__FEATURE_XMU_CPULIMIT		= 10,
	__FEATURE_XMU_CPUFREQ_SET	= 11,
	__UAS_FEAT_NR,
};
/* define for sched state, keep same as the define in userspace. */
#define FEATURE_NONE            0
#define FEATURE_DEFAULT         (0x1 << __FEATURE_DEFAULT)
#define FEATURE_UX_SHARE        (0x1 << __FEATURE_UX_SHARE)
#define FEATURE_UX_TARGET       (0x1 << __FEATURE_UX_TARGET)
#define FEATURE_SELECT_CPUS     (0x1 << __FEATURE_SELECT_CPUS)
#define FEATURE_DINFO           (0x1 << __FEATURE_DINFO)
#define FEATURE_FUTEX           (0x1 << __FEATURE_FUTEX)
#define FEATURE_LOCK_PROTECT    (0x1 << __FEATURE_LOCK_PROTECT)
#define FEATURE_LOAD_TRACK      (0x1 << __FEATURE_LOAD_TRACK)
#define FEATURE_KWORKER         (0x1 << __FEATURE_KWORKER)
#define FEATURE_LOADBALANCE     (0x1 << __FEATURE_LOADBALANCE)
#define FEATURE_XMU_CPULIMIT    (0x1 << __FEATURE_XMU_CPULIMIT)
#define FEATURE_XMU_CPUFREQ_SET (0x1 << __FEATURE_XMU_CPUFREQ_SET)
#define FEATURE_ALL             ((0x1 << __UAS_FEAT_NR) - 1)

DECLARE_STATIC_KEY_FALSE(uas_static_keys[__UAS_FEAT_NR]);

#define check_uas_feature(feature) (static_branch_likely(&uas_static_keys[__##feature]))

void set_uas_feature_state(int state);

/* _T_SCHED's ASSIC */
#define TRANS_MAGIC_HEAD (0x5F545F5343484544U)
/* DEHCS_T_'s ASSIC */
#define TRANS_MAGIC_TAIL (0x44454843535F545FU)

#define DEBUG_KLOG             (0x1 << 0)
#define DEBUG_PICK             (0x1 << 1)
#define DEBUG_BALANCE          (0x1 << 2)
#define DEBUG_SHARE            (0x1 << 3)
#define DEBUG_FUTEX            (0x1 << 4)
#define DEBUG_MODIFY_UX        (0x1 << 5)
#define DEBUG_ALL              (DEBUG_KLOG       | \
                                DEBUG_PICK       | \
                                DEBUG_BALANCE    | \
                                DEBUG_SHARE      | \
                                DEBUG_FUTEX      | \
                                DEBUG_MODIFY_UX)


#define TRANS_NR_CPUS        (8)

#define TRANS_CPUS_CLUSTER_SINGLE (1)
#define TRANS_CPUS_CLUSTER_DUAL   (2)
#define TRANS_CPUS_CLUSTER_TRIPLE (3)

/* Time slice is 4ms */
#define UX_RUN_SLICE  (4000000U)

#define RUN_SLICE_1   (UX_RUN_SLICE << 0)
#define RUN_SLICE_2   (UX_RUN_SLICE << 1)
#define RUN_SLICE_4   (UX_RUN_SLICE << 2)
#define RUN_SLICE_8   (UX_RUN_SLICE << 3)
#define RUN_SLICE_16  (UX_RUN_SLICE << 4)
#define RUN_SLICE_32  (UX_RUN_SLICE << 5)
#define RUN_SLICE_64  (UX_RUN_SLICE << 6)

/* define for ux_tags field, keep same as the define in userspace. */
#define UX_TASK_PRIO_OFFSET   (0)
#define UX_TASK_DPRIO_BASE    (0x03 << UX_TASK_PRIO_OFFSET)
#define UX_TASK_DPRIO_MASK    (0x07 << UX_TASK_PRIO_OFFSET)

#define UX_TASK_SPRIO_OFFSET  (3)
#define UX_TASK_PRIO3         (0x01 << (0 + UX_TASK_SPRIO_OFFSET))
#define UX_TASK_PRIO4         (0x01 << (1 + UX_TASK_SPRIO_OFFSET))
#define UX_TASK_PRIO5         (0x01 << (2 + UX_TASK_SPRIO_OFFSET))
#define UX_TASK_PRIO6         (0x01 << (3 + UX_TASK_SPRIO_OFFSET))
#define UX_TASK_PRIO7         (0x01 << (4 + UX_TASK_SPRIO_OFFSET))
#define UX_TASK_SPRIO_MASK    (0xF8 << UX_TASK_PRIO_OFFSET)
#define UX_TASK_PRIO_MASK     (0xFF << UX_TASK_PRIO_OFFSET)

#define UX_TASK_SLICE_OFFSET  (8)
#define UX_TASK_SLICE0        (0x0 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE1        (0x1 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE2        (0x2 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE3        (0x3 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE4        (0x4 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE5        (0x5 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE6        (0x6 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE7        (0x7 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE_MAX     UX_TASK_SLICE7
#define UX_TASK_SLICE_MASK    (0xF << UX_TASK_SLICE_OFFSET)

#define UX_TASK_CORES_OFFSET  (12)
#define UX_TASK_CORES0        (0x1 << (0 + UX_TASK_CORES_OFFSET))
#define UX_TASK_CORES1        (0x1 << (1 + UX_TASK_CORES_OFFSET))
#define UX_TASK_CORES2        (0x1 << (2 + UX_TASK_CORES_OFFSET))
#define UX_TASK_CORES3        (0x1 << (3 + UX_TASK_CORES_OFFSET))
#define UX_TASK_CORES_MASK    (0xF << UX_TASK_CORES_OFFSET)

#define UX_TASK_SHARE_OFFSET  (16)
#define UX_TASK_SHARE_MASK    (0xF << UX_TASK_SHARE_OFFSET)

#define UX_TASK_FLAG_OFFSET   (20)
#define UX_TASK_FLAG0         (0x01 << (0 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG1         (0x01 << (1 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG2         (0x01 << (2 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG3         (0x01 << (3 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG4         (0x01 << (4 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG5         (0x01 << (5 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG6         (0x01 << (6 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG7         (0x01 << (7 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG8         (0x01 << (8 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG9         (0x01 << (9 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG10        (0x01 << (10 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG11        (0x01 << (11 + UX_TASK_FLAG_OFFSET))
#define UX_TASK_FLAG_MASK     (0xFFF << UX_TASK_FLAG_OFFSET)

#define UX_FLAG_CPU_L    (UX_TASK_CORES0)
#define UX_FLAG_CPU_M    (UX_TASK_CORES1)
#define UX_FLAG_CPU_B    (UX_TASK_CORES2)
#define UX_FLAG_CPU_ML   (UX_FLAG_CPU_M | UX_FLAG_CPU_L)
#define UX_FLAG_CPU_BM   (UX_FLAG_CPU_B | UX_FLAG_CPU_M)
#define UX_FLAG_CPU_BML  (UX_FLAG_CPU_B | UX_FLAG_CPU_M | UX_FLAG_CPU_L)
#define UX_FLAG_CPU_MASK (UX_FLAG_CPU_L | UX_FLAG_CPU_M | UX_FLAG_CPU_B)

#define UX_FLAG_EXACT       (UX_TASK_FLAG0)
#define UX_FLAG_SF          (UX_TASK_FLAG1)
#define UX_FLAG_ABINDER     (UX_TASK_FLAG2)
#define UX_FLAG_RELATED     (UX_TASK_FLAG3)
#define UX_FLAG_ONESHOT     (UX_TASK_FLAG4)
#define UX_FLAG_RECKON      (UX_TASK_FLAG5)
#define UX_FLAG_UAS_SERVICE (UX_TASK_FLAG6)
#define UX_FLAG_FUTEX       (UX_TASK_FLAG7)
#define UX_FLAG_KWORKER     (UX_TASK_FLAG9)

#define UX_TASK_TAGS_SF         ((UX_FLAG_SF) & (~(UX_TASK_SHARE_MASK | UX_TASK_SLICE_MASK | UX_TASK_PRIO_MASK)))
#define UX_TASK_TAGS_SF_ABINDER ((UX_FLAG_ABINDER) | (UX_TASK_TAGS_SF))
#define UX_TASK_TAGS_SF_RELATED ((UX_FLAG_RELATED) | (UX_TASK_TAGS_SF))

// ux_tags normal template
#define UX_TASK_TAGS_NONE       (0x00000000) // clear ux_tags
#define UX_TASK_TAGS_UNSET_MASK (UX_TASK_PRIO_MASK) // mask is used for clear ux_tags
#define UX_TASK_TAGS_EDROP2     ((UX_FLAG_EXACT | UX_TASK_SLICE4 | UX_TASK_PRIO3) & (~(UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_EDROP1     ((UX_FLAG_EXACT | UX_TASK_SLICE4 | UX_TASK_PRIO4) & (~(UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_EXACT      ((UX_FLAG_EXACT | UX_TASK_SLICE4 | UX_TASK_PRIO5) & (~(UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_EPROMOTE1  ((UX_FLAG_EXACT | UX_TASK_SLICE4 | UX_TASK_PRIO6) & (~(UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_EPROMOTE2  ((UX_FLAG_EXACT | UX_TASK_SLICE4 | UX_TASK_PRIO7) & (~(UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_DDROP2     ((UX_TASK_SLICE5 | UX_TASK_PRIO3) & (~(UX_TASK_FLAG_MASK | UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_DDROP1     ((UX_TASK_SLICE5 | UX_TASK_PRIO4) & (~(UX_TASK_FLAG_MASK | UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_DEFAULT    ((UX_TASK_SLICE5 | UX_TASK_PRIO5) & (~(UX_TASK_FLAG_MASK | UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_DPROMOTE1  ((UX_TASK_SLICE5 | UX_TASK_PRIO6) & (~(UX_TASK_FLAG_MASK | UX_TASK_SHARE_MASK)))
#define UX_TASK_TAGS_DPROMOTE2  ((UX_TASK_SLICE5 | UX_TASK_PRIO7) & (~(UX_TASK_FLAG_MASK | UX_TASK_SHARE_MASK)))

// for dynamic recognition
#define UX_TASK_TAGS_RDROP1     (UX_FLAG_RECKON | UX_TASK_TAGS_DDROP1)
#define UX_TASK_TAGS_RECKON     (UX_FLAG_RECKON | UX_TASK_TAGS_DEFAULT)
#define UX_TASK_TAGS_RPROMOTE1  (UX_FLAG_RECKON | UX_TASK_TAGS_DPROMOTE1)

// Only for futex owner
#define UX_TASK_TAGS_FUTEX          (UX_TASK_TAGS_EXACT | UX_FLAG_FUTEX)

// Only for uas-service
#define UX_TASK_TAGS_UAS_SERVICE   (UX_TASK_TAGS_EPROMOTE1 | UX_FLAG_UAS_SERVICE)

// for kworker
#define UX_TASK_TAGS_KWORKER    (UX_TASK_TAGS_DPROMOTE1 | UX_FLAG_KWORKER)

// for abinder tags
#define UX_TASK_TAGS_ABINDER      (UX_TASK_TAGS_EPROMOTE1 | UX_FLAG_ABINDER)
// for abinder bit
#define UX_TASK_TAGS_ABINDER_BIT  (UX_FLAG_ABINDER | UX_TASK_PRIO_MASK)

// for more time slices
#define UX_TASK_TAGS_E_S5     ((UX_TASK_TAGS_EXACT & (~(UX_TASK_SLICE_MASK))) | UX_TASK_SLICE5)
#define UX_TASK_TAGS_E_S6     ((UX_TASK_TAGS_EXACT & (~(UX_TASK_SLICE_MASK))) | UX_TASK_SLICE6)
#define UX_TASK_TAGS_EP1_S5   ((UX_TASK_TAGS_EPROMOTE1 & (~(UX_TASK_SLICE_MASK))) | UX_TASK_SLICE5)
#define UX_TASK_TAGS_EP1_S6   ((UX_TASK_TAGS_EPROMOTE1 & (~(UX_TASK_SLICE_MASK))) | UX_TASK_SLICE6)
#define UX_TASK_TAGS_DP1_S6   ((UX_TASK_TAGS_DPROMOTE1 & (~(UX_TASK_SLICE_MASK))) | UX_TASK_SLICE6)

#define init_ux_dprio(ux_tags)   ((ux_tags) | UX_TASK_DPRIO_BASE)
#define get_ux_dprio(ux_tags)    ((ux_tags) & UX_TASK_DPRIO_MASK)
#define raise_ux_dprio(ux_tags)   ((get_ux_dprio(ux_tags) == UX_TASK_DPRIO_MASK) ? (ux_tags) : (ux_tags + (1 << UX_TASK_PRIO_OFFSET)))
#define lower_ux_dprio(ux_tags)   ((!get_ux_dprio(ux_tags)) ? (ux_tags) : (ux_tags - (1 << UX_TASK_PRIO_OFFSET)))
#define ignore_ux_dprio(ux_tags) (((u32)(ux_tags)) & (~(UX_TASK_DPRIO_MASK)))
#define rebase_ux_dprio(ux_tags) (init_ux_dprio(ignore_ux_dprio(ux_tags)))

#define get_ux_slice(ux_tags)   (((u32)(ux_tags)) & UX_TASK_SLICE_MASK)
#define _inc_ux_slice(ux_tags)  (get_ux_slice(ux_tags) + (1 << UX_TASK_SLICE_OFFSET))
#define inc_ux_slice(ux_tags)   ((get_ux_slice(ux_tags) >= UX_TASK_SLICE_MAX) ? UX_TASK_SLICE_MAX : _inc_ux_slice(ux_tags))
#define ignore_ux_slice(ux_tags) (((u32)(ux_tags)) & (~(UX_TASK_SLICE_MASK)))
#define change_ux_slice(ux_tags, slice) (ignore_ux_slice(ux_tags) | ((slice) & UX_TASK_SLICE_MASK))

#define set_ux_flag(ux_tags, ux_flag)   ((ux_tags) | (ux_flag))
#define check_ux_flag(ux_tags, ux_flag)  (((ux_tags) & (ux_flag)) == (ux_flag))
#define clear_ux_flag(ux_tags, ux_flag)  ((ux_tags) & (~(ux_flag)))
#define ignore_ux_cpu_flag(ux_tags) (((u32)(ux_tags)) & (~(UX_FLAG_CPU_MASK)))

#define check_unset_ux_tags(ux_tags)    (((ux_tags) & UX_TASK_TAGS_UNSET_MASK) == UX_TASK_TAGS_NONE)
#define check_ux_same_type(ux_tagsa, ux_tagsb)  (((ux_tagsa) & (~(UX_TASK_TAGS_UNSET_MASK))) == ((ux_tagsb) & (~(UX_TASK_TAGS_UNSET_MASK))))

#define get_ux_prefer_cpu_state(ux_tags) (((u32)(ux_tags)) & (UX_FLAG_CPU_MASK))
#define clear_ux_prefer_cpu(ux_tags) (((u32)(ux_tags)) & (~(UX_FLAG_CPU_MASK)))
#define set_ux_prefer_cpu(ux_tags, flags) ((clear_ux_prefer_cpu(ux_tags)) | ((flags) & UX_FLAG_CPU_MASK))

#define get_ux_identity(ux_tags)  ((ux_tags) & (UX_FLAG_EXACT | UX_TASK_SPRIO_MASK))

/* define for sched scene, keep same as the define in userspace. */
#define SS_NONE             (0x0)      // clear all sched scene
#define SS_LAUNCH           (0x1 << 0)
#define SS_LAUNCHER         (0x1 << 1)
#define SS_ANIMATION        (0x1 << 2)
#define SS_UNLOCK           (0x1 << 3)
#define SS_TOUCH            (0x1 << 4)
#define SS_FLING            (0x1 << 5)
#define SS_BENCH            (0x1 << 6)
#define SS_ALL              (SS_LAUNCH | SS_LAUNCHER | SS_ANIMATION | SS_UNLOCK | SS_TOUCH | SS_FLING | SS_BENCH)
#define SS_CANCEL_MASK      (0x1 << 31)

#define set_ss(scene) ((u32)(scene) | g_sched_scene)
#define cancel_ss(scene) ((~((u32)(scene))) & g_sched_scene)
#define check_ssc_mask(scene) ((u32)(scene) & SS_CANCEL_MASK)
#define ignore_ssc_mask(scene) ((u32)(scene) & (~SS_CANCEL_MASK))

/* define for boost threshold */
#define UX_BOOST_THRESHOLD (51)

#define TRACE_BUFFER_LEN        (256)

#define get_ux_share_state(ux_tags) (((u32)(ux_tags)) & (UX_TASK_SHARE_MASK))
#define ignore_ux_share_state(ux_tags) (((u32)(ux_tags)) & (~(UX_TASK_SHARE_MASK)))
#define set_ux_share_type(ux_tags, share_type) (((u32)(ux_tags)) | (0x01 << (UX_TASK_SHARE_OFFSET + (share_type))))
#define clear_ux_share_type(ux_tags, share_type) (((u32)(ux_tags)) & (~(0x01 << (UX_TASK_SHARE_OFFSET + (share_type)))))
#define check_ux_share_type(ux_tags, share_type) (get_ux_share_state(ux_tags) == (0x01 << (UX_TASK_SHARE_OFFSET + (share_type))))

#define UX_SHARE_HOLD_INIT          (1)
#define UX_SHARE_HOLD_BINDER_LIMIT  (3)
#define UX_SHARE_HOLD_LOCK_LIMIT    (3)
#define UX_SHARE_BINDER_TIMES_LIMIT (3)
#define UX_SHARE_MUTEX_TIMES_LIMIT  (2)
#define UX_SHARE_RWSEM_TIMES_LIMIT  (2)

#define TASK_ON_RUNQUEUE   (1)

// sched prio
#define LOWER_DPRIO_SHIFT  (0x01 << 0)
#define RAISE_DPRIO_SHIFT  (0x01 << 1)
#define REBASE_DPRIO_SHIFT (0x01 << 2)
#define DPRIO_SHIFT_MASK   (0x0F)

// core prio
#define CAP_SHIFT_OFFSET   (4)
#define BML_CAP_SHIFT      (0x01 << (0 + CAP_SHIFT_OFFSET))
#define BM_CAP_SHIFT       (0x01 << (1 + CAP_SHIFT_OFFSET))
#define ML_CAP_SHIFT       (0x01 << (2 + CAP_SHIFT_OFFSET))
#define L_CAP_SHIFT        (0x01 << (3 + CAP_SHIFT_OFFSET))
#define REBASE_CAP_SHIFT   (0x01 << (4 + CAP_SHIFT_OFFSET))
#define CAP_SHIFT_MASK     (0x1F << CAP_SHIFT_OFFSET)


#define NSEC_TO_MSEC(val) ((val) / NSEC_PER_MSEC)
#define NSEC_TO_USEC(val) ((val) / NSEC_PER_USEC)
#define USEC_TO_MSEC(val) ((val) / USEC_PER_MSEC)
#define MSEC_TO_NSEC(val) ((val) * NSEC_PER_MSEC)
#define MSEC_TO_USEC(val) ((val) * USEC_PER_MSEC)

struct ux_share {
    u32 share_hold : 4;
    u32 binder_nr : 4;
    u32 mutex_nr : 4;
    u32 rwsem_nr : 4;
    u32 futex_nr : 4;
    u32 reserve  : 12;
};

struct ux_tags_field {
    u32 dprio : 3;
    u32 prio : 5;
    u32 slice : 4;
    u32 share : 4;
    u32 cores : 4;
    u32 flag : 10;
    u32 flag_res : 2;
};

enum ux_share_type {
    UX_SHARE_FAULT = -1,
    UX_SHARE_BINDER = 0,
    UX_SHARE_MUTEX,
    UX_SHARE_RWSEM,
    UX_SHARE_FUTEX,
    UX_SHARE_TYPE_MAX,
};

struct lock_queue {
    struct list_head lock_list;
    spinlock_t list_lock;
    int nr_tasks;
    bool enable;
};

#define INVALID_PID                     (-1)
#define INVALID_CPU                     (-1)
enum migrate_task_type {
    MIG_TASK_INVALID = 0,
    MIG_TASK_UX,
    MIG_TASK_RT,
};
struct trans_lb {
    /*
     * used for active_balance to record the running task.
     */
    pid_t pid;

    /*
     * Type of the task to be migrated.
     */
    enum migrate_task_type mig_type;

    /*
     * Used by irq_work-based balancing to record the target CPU
     * for task pushing
     */
    int irqwb_push_cpu;
    pid_t irqwb_pid;
};

struct trans_rq {
    struct list_head ux_list;
    struct lock_queue *lock_queue;

    /* for loadbalance */
    struct trans_lb lb;
    int cls_id;
};

struct lock_info {
    int depth;
    unsigned long lock_start;

    u64 dcounts;
    u64 dstart;
    u64 duration;
};

struct task_lock_info {
    /*
     * Record the duration this task is blocked under any type of lock
     */
    struct lock_info total;

    /*
     * Record the duration this task is blocked under a specific type of lock
     */
    struct lock_info mutex;
    struct lock_info rtmutex;
    struct lock_info rwsem;
    struct lock_info pcpurwsem;

    /*
     * Record which lock this task is currently waiting for
     */
    void *ops;
};

/*
 * used for load tracking
 */
struct task_load {
    spinlock_t lock;

    u64 enqueue_time;
    u64 snap_run_delay;
    unsigned long snap_pcount;

    u64 last_running_time;
    u64 last_update;
    u64 period_contrib;

    u64 load_sum;
    u64 load_avg;

    /*
     * used for estimating utilization
     */
    u64 last_load_avg;
    u64 load_est;
};

struct trans_task_struct {
    u64 trans_magic_head;
    struct task_struct *task;
    struct list_head ux_node;
    u64 total_run;
    u64 sum_exec_runtime;
    u32 ux_tags;
    union {
        u32 share;
        struct ux_share share_field;
    };
    u32 ux_pending;
    u64 ux_share_period;

    struct rq *rq;
    struct list_head lock_entry;
    struct task_lock_info lockinfo;

    /* used for load tracking */
    struct task_load ld;

    /* per-task XMU statistics, allocated only on XMU-capable SoCs */
    struct task_xmu_info *txmu;

    u64 trans_magic_tail;
} ____cacheline_aligned;

enum ux_task_state {
    UX_TASK_REAL = 0,
    UX_TASK_TIMEOUT,
    UX_TASK_NO_FEATURE,
    UX_TASK_NOT_FAIR,
    UX_TASK_NO_TAG,
    UX_TASK_EXITING,
    UX_KWORKER_DISABLE,
};

struct ux_sched_group {
    struct cpumask cpus;
    unsigned long capacity;
};

struct ux_sched_domains {
    int group_nr;
    struct ux_sched_group sched_group[TRANS_NR_CPUS];
};

enum ux_prefer_cpu {
    UX_PREFER_L = 1,
    UX_PREFER_M,
    UX_PREFER_B,
    UX_PREFER_ML,
    UX_PREFER_BM,
    UX_PREFER_BML,
    UX_PREFER_UNLIMIT,
    UX_PREFER_MAX,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
struct trace_points_table {
    const char *name;
    char name_size;
    void *func;
    struct tracepoint *tp;
};
#endif

#define TRANS_ASSERT(trans_struct, trans_struct_offset, kernel_struct)  \
    BUILD_BUG_ON((sizeof(trans_struct) + trans_struct_offset) > (sizeof(u64) *  \
        ARRAY_SIZE(((kernel_struct *)0)->android_oem_data1)))

#define trans_err(fmt, ...) \
    printk(KERN_ERR "[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define trans_warn(fmt, ...) \
    printk(KERN_WARNING "[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)
#define trans_info(fmt, ...) \
    printk(KERN_INFO "[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define trans_warn_deferred(fmt, ...) \
    printk_deferred("[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__)

#define trans_debug(debug, fmt, ...) \
    do { \
        if (unlikely(g_trans_sched_debug & debug)) { \
            printk(KERN_INFO "[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)

#define trans_debug_deferred(debug ,fmt, ...) \
    do { \
        if (unlikely(g_trans_sched_debug & debug)) { \
            printk_deferred("[trans_sched][%s:%d]"fmt, __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while (0)


static inline int is_available_state(int state)
{
    if (state & ~FEATURE_ALL) {
        return 0;
    }

    if (state && !(state & FEATURE_DEFAULT)) {
        return 0;
    }

    return 1;
}

static inline int is_available_ux_type(unsigned int ux_tags)
{
    if (check_unset_ux_tags(ux_tags))
        return 1;

    switch (ignore_ux_cpu_flag(ux_tags)) {
        case UX_TASK_TAGS_EDROP2:
        case UX_TASK_TAGS_EDROP1:
        case UX_TASK_TAGS_EXACT:
        case UX_TASK_TAGS_EPROMOTE1:
        case UX_TASK_TAGS_EPROMOTE2:
        case UX_TASK_TAGS_DDROP2:
        case UX_TASK_TAGS_DDROP1:
        case UX_TASK_TAGS_DEFAULT:
        case UX_TASK_TAGS_DPROMOTE1:
        case UX_TASK_TAGS_DPROMOTE2:
        case UX_TASK_TAGS_RDROP1:
        case UX_TASK_TAGS_RECKON:
        case UX_TASK_TAGS_RPROMOTE1:
        case UX_TASK_TAGS_UAS_SERVICE:
        case UX_TASK_TAGS_KWORKER:
        case UX_TASK_TAGS_FUTEX:
        case UX_TASK_TAGS_ABINDER:
        case UX_TASK_TAGS_ABINDER_BIT:
        case UX_TASK_TAGS_E_S5:
        case UX_TASK_TAGS_E_S6:
        case UX_TASK_TAGS_EP1_S5:
        case UX_TASK_TAGS_EP1_S6:
        case UX_TASK_TAGS_DP1_S6:
            return 1;
        default:
            return 0;
    }
}

static inline bool check_ux_tags_ok(unsigned int ux_tags)
{
    switch (get_ux_identity(ux_tags)) {
        case get_ux_identity(UX_TASK_TAGS_EDROP2):
        case get_ux_identity(UX_TASK_TAGS_EDROP1):
        case get_ux_identity(UX_TASK_TAGS_EXACT):
        case get_ux_identity(UX_TASK_TAGS_EPROMOTE1):
        case get_ux_identity(UX_TASK_TAGS_EPROMOTE2):
        case get_ux_identity(UX_TASK_TAGS_DDROP2):
        case get_ux_identity(UX_TASK_TAGS_DDROP1):
        case get_ux_identity(UX_TASK_TAGS_DEFAULT):
        case get_ux_identity(UX_TASK_TAGS_DPROMOTE1):
        case get_ux_identity(UX_TASK_TAGS_DPROMOTE2):
            return true;
        default:
            return false;
    }
}

static inline int is_available_scene(u32 scene)
{
    if (ignore_ssc_mask(scene) & (~SS_ALL)) {
        return 0;
    }

    return 1;
}

static inline bool check_sched_scene(u32 scene)
{
    return (g_sched_scene & scene) == scene;
}

static inline struct task_struct *transts_to_ts(struct trans_task_struct *trans_ts)
{
    return trans_ts->task;
}

static inline struct trans_task_struct *get_trans_task_struct(struct task_struct *task)
{
    // task->ANDROID_OEM_DATA_ARRAY(1, xx);
    return (struct trans_task_struct *) READ_ONCE(task->android_oem_data1[TRANS_PCB_OFFSET]);
}

static inline bool is_valid_cpu(int cpu)
{
    if (cpu < 0)
        return false;

    if (cpu >= TRANS_NR_CPUS)
        return false;

    return true;
}

static inline struct trans_rq *get_trans_rq(struct rq *rq)
{
    // rq->ANDROID_OEM_DATA_ARRAY(1, 16);
    return (struct trans_rq *) rq->android_oem_data1;
}

static inline struct trans_rq *get_trans_rq_cpu(int cpu)
{
    struct rq *rq = cpu_rq(cpu);

    return get_trans_rq(rq);
}

static inline void set_cpu_cluster_id(int cpu, int cls_id)
{
    struct trans_rq *trq;

    trq = get_trans_rq_cpu(cpu);
    if (likely(trq))
        trq->cls_id = cls_id;
}

static inline int get_cpu_cluster_id(int cpu)
{
    struct trans_rq *trq;

    trq = get_trans_rq_cpu(cpu);
    if (likely(trq))
        return READ_ONCE(trq->cls_id);

    return -1;
}

static inline bool is_fair_task(struct task_struct *task)
{
    BUG_ON(!task);
    if ((task->prio >= MAX_RT_PRIO) && (task->prio < MAX_PRIO)) {
        return true;
    }

    return false;
}

static inline bool is_rt_task(struct task_struct *task)
{
    BUG_ON(!task);
    if ((task->prio >= 0) && (task->prio < MAX_RT_PRIO)) {
        return true;
    }

    return false;
}

static inline bool is_rt_fair_task(struct task_struct *task)
{
    BUG_ON(!task);
    if ((task->prio >= 0) && (task->prio < MAX_PRIO)) {
        return true;
    }

    return false;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
static inline int task_state(struct task_struct *tsk)
{
    return READ_ONCE((tsk)->__state);
}

static inline raw_spinlock_t *trans_rq_lockp(struct rq *rq)
{
    return &rq->__lock;
}
#else
static inline int task_state(struct task_struct *tsk)
{
    return READ_ONCE((tsk)->state);
}

static inline raw_spinlock_t *trans_rq_lockp(struct rq *rq)
{
    return &rq->lock;
}
#endif

static inline bool check_task_on_rq(struct task_struct *task, struct rq *rq)
{
    BUG_ON(!task);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    if (task->se.sched_delayed)
        return false;
#endif

    if (unlikely(SCHED_WARN_ON(rq != task_rq(task)))) {
        trans_warn_deferred("task rq error:task=%#llx, rq=%llx\n", (u64)task, (u64)rq);
        return false;
    }

    if (task_state(task) != TASK_RUNNING)
        return false;

    return READ_ONCE(task->on_rq) == TASK_ON_RUNQUEUE;
}

/* Check whether "trans_task_struct->ux_node" has been initialized. */
static inline bool is_ux_node_uninit(struct list_head *list)
{
    BUG_ON(!list);
    return ((!list->prev) || (!list->next));
}

static inline bool is_ux_node_free(struct list_head *list)
{
    BUG_ON(!list);
    return list_empty(list) || is_ux_node_uninit(list);
}

static inline bool is_ux_list_free(struct list_head *list)
{
    BUG_ON(!list);
    return is_ux_node_free(list);
}

static inline bool trans_list_empty(struct list_head *list)
{
    return is_ux_list_free(list);
}

static inline bool is_trans_task_struct_init(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return false;
    }

    return ((trans_ts->trans_magic_head == TRANS_MAGIC_HEAD) && (trans_ts->trans_magic_tail == TRANS_MAGIC_TAIL));
}

static inline void clear_trans_magic(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    trans_ts->trans_magic_head = 0;
    trans_ts->trans_magic_tail = 0;
}

static inline u32 get_ux_tags(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return UX_TASK_TAGS_NONE;
    }

    return trans_ts->ux_tags;
}

static inline void set_ux_tags(struct task_struct *task, u32 ux_tags)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    trans_ts->ux_tags = ux_tags;
}

static inline u32 get_ux_pending(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return UX_TASK_TAGS_NONE;
    }

    return trans_ts->ux_pending;
}

static inline void set_ux_pending(struct task_struct *task, u32 ux_pending)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    trans_ts->ux_pending = ux_pending;
}

static inline struct list_head *get_ux_node(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return NULL;
    }

    return &trans_ts->ux_node;
}

static inline bool prio_higher(u32 ux_taga, u32 ux_tagb)
{
    return (ux_taga & UX_TASK_PRIO_MASK) > (ux_tagb & UX_TASK_PRIO_MASK) ? true : false;
}

static inline bool check_ux_tags(struct task_struct *task, unsigned int ux_tags)
{
    BUG_ON(!task);
    return get_ux_tags(task) == ux_tags;
}

static inline void set_ux_share_val(struct task_struct *task, u32 share)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    trans_ts->share = share;
}

static inline u32 get_ux_share_val(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return 0;
    }

    return trans_ts->share;
}

static inline u32 get_ux_share_type(struct task_struct *task)
{
    u32 ux_tags = get_ux_tags(task);

    if (check_ux_share_type(ux_tags, UX_SHARE_BINDER))
        return UX_SHARE_BINDER;
    else if (check_ux_share_type(ux_tags, UX_SHARE_MUTEX))
        return UX_SHARE_MUTEX;
    else if (check_ux_share_type(ux_tags, UX_SHARE_RWSEM))
        return UX_SHARE_RWSEM;
    else if (check_ux_share_type(ux_tags, UX_SHARE_FUTEX))
        return UX_SHARE_FUTEX;
    else
        return UX_SHARE_FAULT;
}

static inline void set_ux_share_period(struct task_struct *task, u64 period)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return;
    }

    trans_ts->ux_share_period = period;
}

static inline u64 get_ux_share_period(struct task_struct *task)
{
    struct trans_task_struct *trans_ts = get_trans_task_struct(task);
    if (IS_ERR_OR_NULL(trans_ts)) {
        return 0;
    }

    return trans_ts->ux_share_period;
}

static inline void inc_ux_share_hold(u32 *share, enum ux_share_type share_type)
{
    struct ux_share *share_filed = (struct ux_share *)share;

    if (!share) {
        return;
    }

    switch (share_type) {
        case UX_SHARE_BINDER:
            if (share_filed->share_hold <= UX_SHARE_HOLD_BINDER_LIMIT) {
                share_filed->share_hold += 1;
            }
            break;
        case UX_SHARE_FUTEX:
            if (share_filed->share_hold <= UX_SHARE_HOLD_LOCK_LIMIT) {
                share_filed->share_hold += 1;
            }
            break;
        default:
            return;
    }

    return;
}

static inline void inc_ux_share(u32 *ux_tags, u32 *share, enum ux_share_type share_type)
{
    u32 ux_share_tags = 0;
    struct ux_share *share_filed = (struct ux_share *)share;

    if (!ux_tags || !share) {
        return;
    }

    ux_share_tags = *ux_tags;
    switch (share_type) {
        case UX_SHARE_BINDER:
            if (!share_filed->binder_nr) {
                *ux_tags = set_ux_share_type(ux_share_tags, share_type);
            }

            share_filed->binder_nr += 1;
            share_filed->share_hold = UX_SHARE_HOLD_INIT;
            break;
        case UX_SHARE_MUTEX:
            if (!share_filed->mutex_nr) {
                *ux_tags = set_ux_share_type(ux_share_tags, share_type);
            }

            share_filed->mutex_nr += 1;
            share_filed->share_hold = UX_SHARE_HOLD_INIT;
            break;
        case UX_SHARE_RWSEM:
            if (!share_filed->rwsem_nr) {
                *ux_tags = set_ux_share_type(ux_share_tags, share_type);
            }

            share_filed->rwsem_nr += 1;
            share_filed->share_hold = UX_SHARE_HOLD_INIT;
            break;
        case UX_SHARE_FUTEX:
            if (!share_filed->futex_nr) {
                *ux_tags = set_ux_share_type(ux_share_tags, share_type);
            }

            share_filed->futex_nr += 1;
            share_filed->share_hold = UX_SHARE_HOLD_INIT;
            break;
        default:
            return;
    }

    return;
}

static inline void dec_ux_share(u32 *ux_tags, u32 *share, enum ux_share_type share_type)
{
    u32 ux_share_tags = 0;
    struct ux_share *share_filed = (struct ux_share *)share;

    if (!ux_tags || !share) {
        return;
    }

    ux_share_tags = *ux_tags;
    switch (share_type) {
        case UX_SHARE_BINDER:
            if (share_filed->share_hold > 0) {
                share_filed->share_hold -= 1;
            }

            if (!share_filed->share_hold) {
                share_filed->binder_nr = 0;
                *ux_tags = clear_ux_share_type(ux_share_tags, share_type);
            }

            break;
        case UX_SHARE_MUTEX:
            if (share_filed->share_hold > 0) {
                share_filed->share_hold -= 1;
            }

            if (!share_filed->share_hold) {
                share_filed->mutex_nr = 0;
                *ux_tags = clear_ux_share_type(ux_share_tags, share_type);
            }

            break;
        case UX_SHARE_RWSEM:
            if (share_filed->share_hold > 0) {
                share_filed->share_hold -= 1;
            }

            if (!share_filed->share_hold) {
                share_filed->rwsem_nr = 0;
                *ux_tags = clear_ux_share_type(ux_share_tags, share_type);
            }

            break;
        case UX_SHARE_FUTEX:
            if (share_filed->share_hold > 0) {
                share_filed->share_hold -= 1;
            }

            if (!share_filed->share_hold) {
                share_filed->futex_nr = 0;
                *ux_tags = clear_ux_share_type(ux_share_tags, share_type);
            }

            break;
        default:
            return;
    }

    return;
}

static inline int is_available_shift(u32 shift)
{
    u32 ux_shift = 0;

    if (shift & (REBASE_DPRIO_SHIFT | REBASE_CAP_SHIFT))
        return 1;

    ux_shift = shift & DPRIO_SHIFT_MASK;
    if (ux_shift) {
        switch (ux_shift & (~REBASE_DPRIO_SHIFT)) {
            case LOWER_DPRIO_SHIFT:
            case RAISE_DPRIO_SHIFT:
                return 1;
            default:
                return 0;
        }
    }

    ux_shift = shift & CAP_SHIFT_MASK;
    if (ux_shift) {
        switch (ux_shift & (~REBASE_CAP_SHIFT)) {
            case BML_CAP_SHIFT:
            case BM_CAP_SHIFT:
            case ML_CAP_SHIFT:
            case L_CAP_SHIFT:
                return 1;
            default:
                return 0;
        }
    }

    return 0;
}

static inline u32 set_ux_prio(u32 ux_tags, u32 shift)
{
    switch (shift) {
        case LOWER_DPRIO_SHIFT:
            return lower_ux_dprio(ux_tags);
        case RAISE_DPRIO_SHIFT:
            return raise_ux_dprio(ux_tags);
        case REBASE_DPRIO_SHIFT:
            return rebase_ux_dprio(ux_tags);
        case BML_CAP_SHIFT:
            return set_ux_prefer_cpu(ux_tags, UX_FLAG_CPU_BML); // Allow ux to use all cores.
        case BM_CAP_SHIFT:
            return set_ux_prefer_cpu(ux_tags, UX_FLAG_CPU_BM); // Allow ux to use BM cores.
        case ML_CAP_SHIFT:
            return set_ux_prefer_cpu(ux_tags, UX_FLAG_CPU_ML); // Allow ux to use ML cores.
        case L_CAP_SHIFT:
            return set_ux_prefer_cpu(ux_tags, UX_FLAG_CPU_L); // Allow ux to use L cores.
        case REBASE_CAP_SHIFT:
            return clear_ux_prefer_cpu(ux_tags);
        default:
            return ux_tags;
    }
}

static inline u32 adjust_ux_prio(u32 ux_tags, u32 shift)
{
    u32 ux_shift = 0;
    u32 ux_tags_new = ux_tags;

    ux_shift = shift & DPRIO_SHIFT_MASK;
    if (ux_shift) {
        ux_shift = (ux_shift & REBASE_DPRIO_SHIFT) ? REBASE_DPRIO_SHIFT : (ux_shift & (~REBASE_DPRIO_SHIFT));
        ux_tags_new = set_ux_prio(ux_tags_new, ux_shift);
    }

    ux_shift = shift & CAP_SHIFT_MASK;
    if (ux_shift) {
        ux_shift = (ux_shift & REBASE_CAP_SHIFT) ? REBASE_CAP_SHIFT : (ux_shift & (~REBASE_CAP_SHIFT));
        ux_tags_new = set_ux_prio(ux_tags_new, ux_shift);
    }

    return ux_tags_new;
}

static inline enum ux_prefer_cpu get_ux_prefer_cpu(struct task_struct *task)
{
    u32 ux_tags = get_ux_tags(task);

    switch (get_ux_prefer_cpu_state(ux_tags)) {
        case UX_FLAG_CPU_L:
            return UX_PREFER_L;
        case UX_FLAG_CPU_M:
            return UX_PREFER_M;
        case UX_FLAG_CPU_B:
            return UX_PREFER_B;
        case UX_FLAG_CPU_ML:
            return UX_PREFER_ML;
        case UX_FLAG_CPU_BM:
            return UX_PREFER_BM;
        case UX_FLAG_CPU_BML:
            return UX_PREFER_BML;
        default:
            return UX_PREFER_UNLIMIT;
    }
}

static inline u32 ux_tags_align(u32 ux_tags)
{
    u32 tags = ux_tags;
    struct ux_tags_field *field = (struct ux_tags_field *) (&tags);

    field->flag_res = 0;

    return tags;
}

static inline struct task_xmu_info *task_xmu_get(struct task_struct *task)
{
	struct trans_task_struct *tts = get_trans_task_struct(task);

	if (IS_ERR_OR_NULL(tts))
		return NULL;

	return tts->txmu;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 15, 0)
#if IS_ENABLED(CONFIG_FAIR_GROUP_SCHED)
static inline struct task_struct *task_of(struct sched_entity *se)
{
    SCHED_WARN_ON(!entity_is_task(se));
    return container_of(se, struct task_struct, se);
}
#else /* !CONFIG_FAIR_GROUP_SCHED */
static inline struct task_struct *task_of(struct sched_entity *se)
{
    return container_of(se, struct task_struct, se);
}
#endif
#endif

struct trans_task_struct *trans_task_struct_create(struct task_struct *task);
int trans_task_struct_init(struct task_struct *task);
u64 ux_task_run_limit(struct task_struct *task);
bool is_ux_task(struct task_struct *task, enum ux_task_state *state);
bool is_ux_task_base(struct task_struct *task, enum ux_task_state *state);
bool tran_is_ux_task(struct task_struct *task);
int set_ux_tags_to_task(struct task_struct *task, u32 ux_tags, char *thread_name, bool record_ux);
int set_ux_tags_by_pid(struct task_struct *target_task, pid_t pid, u32 ux_tags, char *thread_name);
int convert_ux_prio(pid_t pid, u32 shift);
void update_ux_runtime(struct rq *rq, struct task_struct *task);

void trans_rqs_init(void);

void build_ux_sched_domains(void);
void dump_ux_sched_domains(void);

void android_rvh_enqueue_task_hook(void *data, struct rq *rq, struct task_struct *task, int flags);
void android_rvh_dequeue_task_hook(void *data, struct rq *rq, struct task_struct *task, int flags);

void android_vh_build_sched_domains_hook(void *data, bool has_asym);
void android_vh_scheduler_tick_hook(void *data, struct rq *rq);
void android_rvh_sched_fork_hook(void *data, struct task_struct *task);

void update_ux_share_hold(struct task_struct *task, enum ux_share_type share_type);
void set_ux_share(struct task_struct *task, enum ux_share_type share_type, u32 share, u32 ux_tags);
void unset_ux_share(struct task_struct *task, enum ux_share_type share_type);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
void sched_switch_prev_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state);
void sched_switch_next_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state);
#else
void sched_switch_prev_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next);
void sched_switch_next_hook(void *data, bool preempt, struct task_struct *prev, struct task_struct *next);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0))
void android_rvh_schedule_hook(void *unused, unsigned int sched_mode, struct task_struct *prev, struct task_struct *next, struct rq *rq);
#else
void android_rvh_schedule_hook(void *unused, struct task_struct *prev, struct task_struct *next, struct rq *rq);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
void android_rvh_dequeue_entity_delayed_hook(void *unused,
            struct cfs_rq *cfs_rq, struct sched_entity *se, bool *delay);
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
void trans_sched_switch(void *unused,
            bool preempt, struct task_struct *prev, struct task_struct *next,
            unsigned int prev_state);
#else
void trans_sched_switch(void *unused,
            bool preempt, struct task_struct *prev, struct task_struct *next);
#endif



#define TRANS_SYSTRACE_POS    99999
#define INVALID_SYSTRACE_IDX  (-1ULL)

/*
 * Some systrace information can be resource-intensive, making it
 * unsuitable for long-term enabling. Use the following switch to
 * enable it only when necessary.
 */
#define TRANS_SYSTRACE_LIST(macro)          \
    macro(NR_RUNNING, 0)                    \
    macro(CPU_LOADSTATES, 1)                \
    macro(LK_ALLOC_LOCK, 2)                 \
    macro(LK_DSTATE_DURATION, 3)            \
    macro(LK_LOCK_PROTECT, 4)               \
    macro(LK_LOCK_TRIGGER, 5)               \
    macro(LK_DUMP_LOCKTASK, 6)              \
    macro(LD_RUNNING_TIME, 7)               \
    macro(PIPELINE, 8)                      \
    macro(UXTAGS, 9)                        \
    macro(PULL_RUNNING_TASK, 10)            \
    macro(PULL_RUNNABLE_TASK, 11)           \
    macro(NEWIDLE_PULL_RUNNABLE_UX, 12)     \
    macro(NEWIDLE_PULL_RUNNABLE_RT, 13)     \
    macro(WAKEUP_IRQWB, 14)                 \
    macro(MAX, 15)

/*
 * Generate enum values
 */
#define GENERATE_ENUM(name, value) TRANS_TRACE_##name = BIT(value),
enum trans_systrace {
    TRANS_SYSTRACE_LIST(GENERATE_ENUM)
};

noinline int tracing_mark_write(const char *buf);
void trans_sched_systrace_c(pid_t pid, u64 val, const char *fmt, ...);
void trans_sched_systrace_b(pid_t tgid, const char *fmt, ...);
void trans_sched_systrace_e(void);
void scene_systrace_c(void);
void ux_state_systrace_c(unsigned int cpu, struct task_struct *p);


#endif /* _TRANS_SCHED_COMMON_H */
