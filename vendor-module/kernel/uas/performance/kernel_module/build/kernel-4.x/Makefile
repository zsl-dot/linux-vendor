ccflags-y += \
	-I$(srctree)/ \
	-I$(srctree)/include/linux/ \
	-I$(srctree)/kernel/sched/trans_sched/ \
	-I$(srctree)/../vendor/transsion/spd/uas/performance/kernel_module/include/ \
	-I$(srctree)/../vendor/transsion/spd/uas/performance/kernel_module/src/

obj-$(CONFIG_TRANS_SCHED_FEATURE) += trans_sched.o

trans_sched-y += ../../src/sched_main.o
trans_sched-y += ../../src/trans_pcb.o
trans_sched-y += ../../src/sched_common.o
trans_sched-y += ../../src/trans_fair.o
trans_sched-y += ../../src/trans_proc.o
trans_sched-y += ../../src/trans_sched_info.o
trans_sched-y += ../../src/trans_binder.o
trans_sched-y += ../../src/trans_mutex.o
trans_sched-y += ../../src/trans_rwsem.o
trans_sched-y += ../../src/trans_target.o
trans_sched-y += ../../src/trans_balance.o
trans_sched-y += ../../src/trans_locking.o
trans_sched-y += ../../src/trans_loading.o
trans_sched-y += ../../src/trans_trace.o
trans_sched-y += ../../src/trans_futex.o
trans_sched-y += ../../src/trans_xmu.o
