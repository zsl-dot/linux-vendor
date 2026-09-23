/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Transsion Inc.
 * Author: Hongyan Xia <hongyan.xia@transsion.com>
 */
#ifndef __TRANSSION_XMU_CPUFREQ_H__
#define __TRANSSION_XMU_CPUFREQ_H__

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/sched/cputime.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
#include <linux/perf_event.h>
#include <linux/perf/arm_pmuv3.h>

/* The duration of one window is 4ms. */
#define WIN_DURATION_MS 4ULL
#define WIN_DURATION_NS (WIN_DURATION_MS * NSEC_PER_MSEC)

#define TRANS_FREQLIMIT_MAX_LIMIT_SCALE ((u64)192)

#define MODEL_NAME_D8400	"MT6899Z_E/ZA"
#define MODEL_NAME_D7400	"MT6878V/TZA"
#define MODEL_NAME_D7400E	"MT6878V_E/TZA"

enum xmu_events {
	xmu_cycle_idx = 0,
	xmu_mem_stall_idx,
	xmu_nr_events_pmu,
	xmu_inst_idx = xmu_nr_events_pmu,
	xmu_nr_events_amu,
	xmu_l2_refill_idx = xmu_nr_events_amu,
	xmu_l3_refill_idx,
	xmu_nr_events_full,
};

enum xmu_soc_type {
	xmu_soc_default = 0,
	xmu_soc_D8400,
	xmu_soc_D7400,
};

enum xmu_action {
	xmu_action_tick,
	xmu_action_put_prev,
	xmu_action_set_next,
};

struct xmu_stat {
	u64	xmu_avg;
	u64	xmu_curr;
};

struct cpu_xmu_event {
	struct perf_event	*event;
	u64			last_reading;
};

/* Per-CPU structure keeping track of last WIN_NR windows of XMU statistics for all XMU events */
struct cpu_xmu_info {
	u64			last_window_start;
	u64			last_update_time;
	u64			mem_stall_factor;
	u64			cpu_cycle_est;
	u64			limit_ratio;
	struct cpu_xmu_event	events[xmu_nr_events_full];
	struct xmu_stat		stats[xmu_nr_events_full];
};

/* Per-task structure keeping track of XMU statistics */
struct task_xmu_info {
	u64			last_window_start;
	u64			last_update_time;
	u64			task_cycle_est:63;
	bool			on_rq:1;
	int 			cycle_cpu;
	struct xmu_stat		stats[xmu_nr_events_full];
};

DECLARE_PER_CPU(struct cpu_xmu_info, percpu_windows);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0) */

int xmu_cpufreq_init(void);
void xmu_cpufreq_exit(void);

void xmu_schedule(struct task_struct *prev, struct task_struct *next, struct rq *rq);
void xmu_tick_entry(struct rq *rq);
u64 xmu_cpufreq_limit(int cpu, unsigned long *freq, unsigned long max);
bool xmu_is_runnable_boost_enabled(void);

void xmu_task_enqueue(struct rq *rq, struct task_struct *p, int flags);
void xmu_task_dequeue(struct rq *rq, struct task_struct *p, int flags);

unsigned long xmu_cpufreq_set(int cpu);

struct trans_task_struct;
struct task_xmu_info *xmu_task_info_alloc(void);
void xmu_task_info_free(struct trans_task_struct *tts);
#endif
