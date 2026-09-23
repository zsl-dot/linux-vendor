#include <linux/version.h>
#include <linux/proc_fs.h>
#include <uapi/asm-generic/errno-base.h>

#include "sched_common.h"
#include "trans_balance.h"
#include "trans_locking.h"

#define TRANS_PROC_DIR    "trans_scheduler"

struct proc_dir_entry *debug_node = NULL;
struct proc_dir_entry *enable_node = NULL;
struct proc_dir_entry *d_trans_sched = NULL;

extern int g_trans_sched_enable;
extern int g_trans_sched_debug;

static void enable_usage(void)
{
    trans_info("proc/trans_scheduler/enable: only accept values >> [0, 1, 3, 5, 7, 9, 11, 13, 15].\n");
}

static ssize_t proc_enable_write(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos)
{
    int ret, val;
    char buffer[8] = {0};

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    buffer[count] = '\0';
    ret = kstrtoint(strstrip(buffer), 10, &val);
    if (ret) {
        enable_usage();
        return ret;
    }

    if (!is_available_state(val)) {
        enable_usage();
        return -EINVAL;
    }

#if (!IS_ENABLED(CONFIG_TRANS_SCHED_SHARE))
    val = val ? (FEATURE_DEFAULT | FEATURE_DINFO) : FEATURE_NONE;
#endif

    set_uas_feature_state(val);

    return count;
}

static ssize_t proc_enable_read(struct file *file, char __user *buf,
        size_t count, loff_t *ppos)
{
    size_t len = 0;
    char buffer[16];

    len = snprintf(buffer, sizeof(buffer), "enable=%#x\n", g_trans_sched_enable);

    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static void debug_usage(void)
{
    trans_info("usage:\n \
        \techo 0 > proc/trans_scheduler/debug  --- disable debug mode.\n \
        \techo 1 > proc/trans_scheduler/debug  --- enable klog.\n \
        \techo 2 > proc/trans_scheduler/debug  --- enable pick.\n \
        \techo 4 > proc/trans_scheduler/debug  --- enable balance.\n \
        \techo 8 > proc/trans_scheduler/debug  --- enable share.\n \
        \techo 16 > proc/trans_scheduler/debug  --- enable futex.\n \
        \techo 32 > proc/trans_scheduler/debug  --- enable modify ux.\n \
    ");
}

static ssize_t proc_debug_write(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos)
{
    int ret;
    int val = 0;
    char buffer[8] = {0};

    memset(buffer, 0, sizeof(buffer));

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    buffer[count] = '\0';
    ret = kstrtoint(strstrip(buffer), 10, &val);
    if (ret) {
        debug_usage();
        return ret;
    }

    if (val & ~DEBUG_ALL) {
        debug_usage();
        return -EINVAL;
    }

    g_trans_sched_debug = val;

    return count;
}

static ssize_t proc_debug_read(struct file *file, char __user *buf,
        size_t count, loff_t *ppos)
{
    size_t len = 0;
    char buffer[16];

    len = snprintf(buffer, sizeof(buffer), "debug mode=%d\n", g_trans_sched_debug);

    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 136)
static const struct proc_ops proc_enable_fops = {
    .proc_write     = proc_enable_write,
    .proc_read      = proc_enable_read,
};

static const struct proc_ops proc_debug_fops = {
    .proc_write     = proc_debug_write,
    .proc_read      = proc_debug_read,
};
#else
static const struct file_operations proc_enable_fops = {
    .write      = proc_enable_write,
    .read       = proc_enable_read,
};

static const struct file_operations proc_debug_fops = {
    .write      = proc_debug_write,
    .read       = proc_debug_read,
};
#endif

extern struct proc_dir_entry * systrace_mask_proc_init(struct proc_dir_entry *pdir);
extern void systrace_mask_proc_deinit(void);

int trans_proc_init(void)
{
    d_trans_sched = proc_mkdir(TRANS_PROC_DIR, NULL);
    if (!d_trans_sched) {
        trans_err("create proc/trans_scheduler dir failed\n");
        goto out;
    }

    enable_node = proc_create("enable", 0666, d_trans_sched, &proc_enable_fops);
    if (!enable_node) {
        trans_err("create proc/trans_scheduler/enable failed\n");
        goto out1;
    }

    debug_node = proc_create("debug", 0666, d_trans_sched, &proc_debug_fops);
    if (!debug_node) {
        trans_err("create proc/trans_scheduler/debug failed\n");
        goto out2;
    }

    if (!systrace_mask_proc_init(d_trans_sched)) {
        goto out3;
    }

    if (!lock_status_proc_init(d_trans_sched)) {
        goto out4;
    }

    if (!trans_lb_stat_proc_init(d_trans_sched)) {
        goto out5;
    }

    return 0;

out5:
    lock_status_proc_deinit();
out4:
    systrace_mask_proc_deinit();
out3:
    proc_remove(debug_node);
out2:
    proc_remove(enable_node);
out1:
    proc_remove(d_trans_sched);
out:
    return -ENOMEM;
}

void trans_proc_deinit(void)
{
    trans_lb_stat_proc_deinit();
    lock_status_proc_deinit();
    systrace_mask_proc_deinit();

    if (debug_node) {
        proc_remove(debug_node);
        debug_node = NULL;
    }

    if (enable_node) {
        proc_remove(enable_node);
        enable_node = NULL;
    }

    if (d_trans_sched) {
        proc_remove(d_trans_sched);
        d_trans_sched = NULL;
    }
}
