// SPDX-License-Identifier: GPL-2.0
/*
 * kgdb 练习模块: 在函数里故意加个断点友好的位置
 *
 * 编译成模块拉进 VM:
 *   make && ../../../lib/vm/run-qemu-9p.sh  (通过 9P 共享目录加载)
 *
 * VM 内: insmod /host/kgdb-demo/kgdb_test.ko
 *
 * 然后用 kgdb 启动 QEMU:
 *   ../../../lib/vm/virtme-ng/02-run-kgdb.sh
 *
 * 终端2 连 gdb:
 *   gdb ../../build/vmlinux
 *   (gdb) target remote :1234
 *   (gdb) break kgdb_test_func     ← 在模块函数上打断点
 *   (gdb) continue
 *
 * VM 里加载模块: insmod kgdb_test.ko
 * gdb 会停在 kgdb_test_func
 */
#include <linux/init.h>
#include <linux/module.h>

static int kgdb_test_func(int x)
{
	int y = x * 2;
	int z = y + 100;
	return z;               /* ← 在这里打断点，观察 x,y,z 的值 */
}

static int __init kgdb_test_init(void)
{
	int result;

	pr_info("kgdb_test: 调用 kgdb_test_func(42)...\n");
	result = kgdb_test_func(42);
	pr_info("kgdb_test: result = %d\n", result);
	return 0;
}

static void __exit kgdb_test_exit(void)
{
	pr_info("kgdb_test: goodbye\n");
}

module_init(kgdb_test_init);
module_exit(kgdb_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kgdb practice module");
