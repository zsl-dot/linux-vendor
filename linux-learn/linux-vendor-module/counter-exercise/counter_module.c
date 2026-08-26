// SPDX-License-Identifier: GPL-2.0
/*
 * 练习：计数器模块
 *
 * 目标：验证对内核模块机制和 /proc 文件系统的理解
 *
 * 任务：
 *   1. 补全 TODO 标记的代码，实现一个通过 /proc/counter 维护的计数器
 *   2. read:  返回当前计数值 "count = %u\n"
 *   3. write: 接受 "inc" 使计数+1，"dec" 使计数-1，其他无效
 *   4. 模块加载时通过参数 "start" 设置初始值
 *   5. 加载模块读 /proc/counter，echo inc > /proc/counter，确认计数正确
 *
 * 预期结果：
 *   $ sudo insmod counter_module.ko start=10
 *   $ cat /proc/counter
 *   count = 10
 *   $ echo inc | sudo tee /proc/counter
 *   $ echo inc | sudo tee /proc/counter
 *   $ echo dec | sudo tee /proc/counter
 *   $ cat /proc/counter
 *   count = 11
 *   $ echo bad | sudo tee /proc/counter && echo "OK" || echo "FAIL"
 *   FAIL
 *   $ sudo rmmod counter_module
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/string.h>

static unsigned int start = 0;
module_param(start, uint, 0644);
MODULE_PARM_DESC(start, "Initial counter value");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("Exercise: counter module via /proc");

/* ---- /proc/counter ---- */

/* TODO: 这个函数应该输出 "count = %u\n" */
/* ... */

/* TODO: 这个函数应该解析 "inc" 或 "dec"，修改 counter，其他返回 -EINVAL */
/* 提示：用 strncmp 或 strstrip + strcmp */
/* ... */

/* TODO: 填充 proc_ops 结构体 */
/* ... */

/* TODO: 在 init 中 proc_create("counter", ...) */
/* TODO: 在 exit 中 proc_remove(...) */
