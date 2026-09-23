// SPDX-License-Identifier: GPL-2.0
/*
 * hello_module.c — 一个简单的运行时内核模块示例
 *
 * 演示: module_init/exit、模块参数、printk、/proc 文件创建
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

static char *name = "world";
module_param(name, charp, 0644);
MODULE_PARM_DESC(name, "名字，显示在 /proc/hello_module 中");

static unsigned int count = 1;
module_param(count, uint, 0644);
MODULE_PARM_DESC(count, "计数初值");

static unsigned int value = 0;

/* ---- /proc/hello_module 的 show 函数 ---- */
static int hello_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Hello, %s!\n", name);
	seq_printf(m, "Module loaded, IRQ disabled count = %u\n", value);
	return 0;
}

static int hello_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, hello_proc_show, NULL);
}

static ssize_t hello_proc_write(struct file *file, const char __user *buf,
				size_t len, loff_t *off)
{
	char kbuf[64];

	if (len >= sizeof(kbuf))
		len = sizeof(kbuf) - 1;
	if (copy_from_user(kbuf, buf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	if (kstrtouint(kbuf, 0, &value))
		return -EINVAL;
	pr_info("value set to %u\n", value);
	return len;
}

static const struct proc_ops hello_proc_ops = {
	.proc_open    = hello_proc_open,
	.proc_read    = seq_read,
	.proc_write   = hello_proc_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static struct proc_dir_entry *proc_entry;

/* ---- 模块初始化和退出 ---- */
static int __init hello_init(void)
{
	pr_info("Hello, %s! (count=%u)\n", name, count);

	proc_entry = proc_create("hello_module", 0666, NULL, &hello_proc_ops);
	if (!proc_entry) {
		pr_err("failed to create /proc/hello_module\n");
		return -ENOMEM;
	}

	return 0;
}

static void __exit hello_exit(void)
{
	proc_remove(proc_entry);
	pr_info("Goodbye, %s!\n", name);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Demo");
MODULE_DESCRIPTION("A simple runtime kernel module example");
