/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Transsion Inc.
 * Author: Hongyan Xia <hongyan.xia@transsion.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xmu_cpufreq

#if !defined(_TRAN_XMU_EVENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRAN_XMU_EVENTS_H

#include <linux/tracepoint.h>
#include <linux/types.h>

TRACE_EVENT(xmu_cpu_metrics,

	TP_PROTO(int cpu,
		 u64 xmu_cycle, u64 xmu_stall, u64 xmu_inst,
		 u64 xmu_l2_refill, u64 xmu_l3_refill),

	TP_ARGS(cpu,
		xmu_cycle, xmu_stall, xmu_inst,
		xmu_l2_refill, xmu_l3_refill),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(u64, xmu_cycle)
		__field(u64, xmu_stall)
		__field(u64, xmu_inst)
		__field(u64, xmu_l2_refill)
		__field(u64, xmu_l3_refill)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->xmu_cycle	= xmu_cycle;
		__entry->xmu_stall	= xmu_stall;
		__entry->xmu_inst	= xmu_inst;
		__entry->xmu_l2_refill	= xmu_l2_refill;
		__entry->xmu_l3_refill	= xmu_l3_refill;
	),

	TP_printk("cpu=%d xmu_cycle=%llu xmu_stall=%llu xmu_inst=%llu xmu_l2_refill=%llu xmu_l3_refill=%llu",
		  __entry->cpu,
		  __entry->xmu_cycle,
		  __entry->xmu_stall,
		  __entry->xmu_inst,
		  __entry->xmu_l2_refill,
		  __entry->xmu_l3_refill
	)
);

TRACE_EVENT(xmu_freqlimit,

	TP_PROTO(int cpu, u32 freq, u32 limit, u32 uncap),

	TP_ARGS(cpu, freq, limit, uncap),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(u32, freq)
		__field(u32, limit)
		__field(u32, uncap)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->freq		= freq;
		__entry->limit		= limit;
		__entry->uncap		= uncap;
	),

	TP_printk("cpu=%d freq=%u limit=%u uncap=%u",
		  __entry->cpu, __entry->freq, __entry->limit, __entry->uncap
	)
);

TRACE_EVENT(xmu_task_metrics,

	TP_PROTO(pid_t pid, u64 task_cycle, u64 task_stall,
		 u64 task_inst, u64 task_l2_refill, u64 task_l3_refill),

	TP_ARGS(pid, task_cycle, task_stall,
		task_inst, task_l2_refill, task_l3_refill),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(u64, task_cycle)
		__field(u64, task_stall)
		__field(u64, task_inst)
		__field(u64, task_l2_refill)
		__field(u64, task_l3_refill)
	),

	TP_fast_assign(
		__entry->pid		= pid;
		__entry->task_cycle	= task_cycle;
		__entry->task_stall	= task_stall;
		__entry->task_inst	= task_inst;
		__entry->task_l2_refill	= task_l2_refill;
		__entry->task_l3_refill	= task_l3_refill;
	),

	TP_printk("pid=%d task_cycle=%llu task_stall=%llu task_inst=%llu task_l2_refill=%llu task_l3_refill=%llu",
		  __entry->pid,
		  __entry->task_cycle,
		  __entry->task_stall,
		  __entry->task_inst,
		  __entry->task_l2_refill,
		  __entry->task_l3_refill
	)
);

#endif /* _TRAN_XMU_EVENTS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trans_xmu_events
#include <trace/define_trace.h>
