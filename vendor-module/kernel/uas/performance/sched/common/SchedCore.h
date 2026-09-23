#ifndef _SCHED_CORE_H
#define _SCHED_CORE_H

#include <sys/types.h>

#define LOG_TAG     "uas-service"

#define TRANS_SCHED_DRV_PATH    "/dev/trans_sched"

#define UAS_CGROUP      "/dev/cpuctl/uas/tasks"
#define DEFAULT_CGROUP  "/dev/cpuctl/cgroup.procs"

#define THREAD_NAME_LEN (64)

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
/* define for sched state, keep same as the define in kernel space. */
#define FEATURE_NONE            (0x0)
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

/* define for ux_tags field, keep same as the define in kernel space. */
#define UX_TASK_PRIO_OFFSET   (0)
#define UX_TASK_PRIO3         (0x01 << (3 + UX_TASK_PRIO_OFFSET))
#define UX_TASK_PRIO4         (0x01 << (4 + UX_TASK_PRIO_OFFSET))
#define UX_TASK_PRIO5         (0x01 << (5 + UX_TASK_PRIO_OFFSET))
#define UX_TASK_PRIO6         (0x01 << (6 + UX_TASK_PRIO_OFFSET))
#define UX_TASK_PRIO7         (0x01 << (7 + UX_TASK_PRIO_OFFSET))
#define UX_TASK_PRIO_MASK     (0xFF << UX_TASK_PRIO_OFFSET)

#define UX_TASK_SLICE_OFFSET  (8)
#define UX_TASK_SLICE0        (0x0 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE1        (0x1 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE2        (0x2 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE3        (0x3 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE4        (0x4 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE5        (0x5 << UX_TASK_SLICE_OFFSET)
#define UX_TASK_SLICE6        (0x6 << UX_TASK_SLICE_OFFSET)
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


#define UX_FLAG_CPU_L       (UX_TASK_CORES0)
#define UX_FLAG_EXACT       (UX_TASK_FLAG0)
#define UX_FLAG_ABINDER     (UX_TASK_FLAG2)
#define UX_FLAG_RECKON      (UX_TASK_FLAG5)
#define UX_FLAG_UAS_SERVICE (UX_TASK_FLAG6)
#define UX_FLAG_FUTEX       (UX_TASK_FLAG7)

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

// ux_tags template with prefer cpu
#define UX_TASK_TAGS_EDROP2_L   (UX_TASK_TAGS_EDROP2 | UX_FLAG_CPU_L)

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

// Cancel value corresponding to ux_tags
#define get_unset_ux_tags_val(ux_tags) ((ux_tags) & (~(UX_TASK_TAGS_UNSET_MASK)))

/* define for sched scene, keep same as the define in kernel space. */
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
#define ignore_ssc_mask(scene) ((unsigned int)scene & (~SS_CANCEL_MASK))

#define DEFAULT_UX_TAG          UX_TASK_TAGS_DEFAULT

#define UXTAG_ID_REF_NONE       (0)

#define VERSION_PROPERTY        "ro.vendor.transched.version"

struct ux_data {
    pid_t pid;
    unsigned int ux_tags;
    int ref;
    char *thread_name;
};

struct ux_info {
    pid_t pid;
    char *info;
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
#define TRANS_SET_UX_PRIO       _IOW(TRANS_SCHED_MAGIC, 0x12, struct ux_prio)
#define TRANS_SET_UX_TAGS       _IOW(TRANS_SCHED_MAGIC, 0x13, struct ux_data)
#define TRANS_GET_UX_TAGS       _IOR(TRANS_SCHED_MAGIC, 0x14, struct ux_data)
#define TRANS_GET_UX_INFO       _IOR(TRANS_SCHED_MAGIC, 0x15, struct ux_info)
#define TRANS_DUMP_UX_LIST      _IO(TRANS_SCHED_MAGIC, 0x16)


int set_proc_ux_tags(pid_t pid, unsigned int ux_tags, char *thread_name);
int set_trans_sched_state(int state);
int get_trans_sched_state(int *state);
int set_trans_sched_scene(unsigned int scene);
int get_trans_sched_scene(unsigned int *scene);
int set_trans_sched_group(pid_t pid, bool is_uxgroup);
int set_trans_sched_ux_tags(pid_t pid, unsigned int ux_tags, int ref, char *thread_name);
int set_trans_sched_ux_prio(pid_t pid, unsigned int shift);
int get_trans_sched_ux_tags(pid_t pid, unsigned int *ux_tags);
int get_trans_sched_ux_info(pid_t pid, char *info, unsigned int info_len);
int dump_trans_sched_ux_info();

#endif /* _SCHED_CORE_H */
