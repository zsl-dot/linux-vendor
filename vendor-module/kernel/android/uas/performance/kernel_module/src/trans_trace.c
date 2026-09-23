/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 transsion Inc.
 */

#include <linux/version.h>
#include <linux/proc_fs.h>

#include "sched_common.h"
#include "trans_trace.h"

long g_systrace_mask = 0;
struct proc_dir_entry *systrace_mask_node = NULL;

/*
 * Generate string array for enum names
 */
#define GENERATE_STRING(name, value) [value] = #name,
static const char *trans_systrace_names[] = {
    TRANS_SYSTRACE_LIST(GENERATE_STRING)
};

static ssize_t proc_systrace_mask_write(struct file *file,
            const char __user *buf, size_t count, loff_t *ppos)
{
    int ret;
    long val;
    char buffer[64] = {0};

    if (count > sizeof(buffer) - 1) {
        count = sizeof(buffer) - 1;
    }

    if (copy_from_user(buffer, buf, count)) {
        return -EFAULT;
    }

    buffer[count] = '\0';
    ret = kstrtol(strstrip(buffer), 10, &val);
    if (ret) {
        return ret;
    }

    g_systrace_mask = val;

    return count;
}

#define TRANS_TRACE_MAX_BUFF_SIZE 2048
static ssize_t proc_systrace_mask_read(struct file *file,
            char __user *buf, size_t count, loff_t *ppos)
{
    int i;
    char *buffer = NULL;
    int pos = 0;
    ssize_t length = 0;

    buffer = kcalloc(TRANS_TRACE_MAX_BUFF_SIZE, sizeof(char), GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    pos += scnprintf(buffer + pos, TRANS_TRACE_MAX_BUFF_SIZE - pos,
            "    Currently enabled systrace:\n");

    for (i = 0; i < ARRAY_SIZE(trans_systrace_names) - 1; i++) {
        if (!trans_systrace_names[i])
            continue;

        pos += scnprintf(buffer + pos, TRANS_TRACE_MAX_BUFF_SIZE - pos,
                "    %*s ... %s\n", 25,
                trans_systrace_names[i],
                (g_systrace_mask & (1U << i)) ? "ON" : "OFF");
    }

    pos += scnprintf(buffer + pos, TRANS_TRACE_MAX_BUFF_SIZE - pos,
            "\n");

    length = simple_read_from_buffer(buf, count, ppos, buffer, pos);

    kfree(buffer);
    return length;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 136)
static const struct proc_ops proc_systrace_mask_fops = {
    .proc_write = proc_systrace_mask_write,
    .proc_read = proc_systrace_mask_read,
};
#else
static const struct file_operations proc_systrace_mask_fops = {
    .write = proc_systrace_mask_write,
    .read  = proc_systrace_mask_read,
};
#endif

struct proc_dir_entry * systrace_mask_proc_init(
        struct proc_dir_entry *pdir)
{
    systrace_mask_node = proc_create("systrace_mask",
            0666, pdir, &proc_systrace_mask_fops);
    if (!systrace_mask_node) {
        trans_err("create proc/trans_scheduler/systrace_mask failed\n");
        goto err;
    }

    return systrace_mask_node;
err:
    return NULL;
}

void systrace_mask_proc_deinit(void)
{
    if (systrace_mask_node) {
        proc_remove(systrace_mask_node);
        systrace_mask_node = NULL;
    }
}
