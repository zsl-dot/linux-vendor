/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c)  2025 Transsion Inc.
 * Author: Hongyan Xia <hongyan.xia@transsion.com>
 */

#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
#include <asm/cputype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/arch_topology.h>
#include <linux/cpufreq.h>
#include <linux/math64.h>

#include "sched_common.h"
#include "trans_xmu.h"

#include "trans_sched_interface.h"

#define CREATE_TRACE_POINTS
#include "trans_xmu_events.h"

#define XMU_CYCLE_SCALE_SHIFT	32

static struct kobject *xmu_cpufreq_kobj;
static int limit_factor = 0;
static int runnable_boost_enable = 1;
static int cpufreq_set_enable = 0;
DEFINE_PER_CPU(struct cpu_xmu_info, percpu_windows);
static DEFINE_PER_CPU(struct kobject *, percpu_kobj);
static DEFINE_PER_CPU(u32, xmu_cycle_scale);

static bool xmu_active;
static struct kmem_cache *task_xmu_cache;

struct task_xmu_info *xmu_task_info_alloc(void)
{
	if (!READ_ONCE(xmu_active))
		return NULL;

	return kmem_cache_zalloc(task_xmu_cache, GFP_ATOMIC);
}

void xmu_task_info_free(struct trans_task_struct *tts)
{
	struct task_xmu_info *info;

	if (!tts || !task_xmu_cache)
		return;
	info = tts->txmu;
	if (!info)
		return;
	tts->txmu = NULL;
	kmem_cache_free(task_xmu_cache, info);
}

static struct xmu_events_struct {
	bool		is_amu[xmu_nr_events_full];
	unsigned int	nr;
} global_events;

static u64
amu_counters_read(enum xmu_events idx)
{
	if (idx == xmu_cycle_idx)
		return read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
	if (idx == xmu_mem_stall_idx)
		return read_sysreg_s(SYS_AMEVCNTR0_MEM_STALL);
	if (idx == xmu_inst_idx)
		return read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
	return 0;
}

static void
xmu_counters_read(u64 *new_readings)
{
	struct cpu_xmu_info *this_cpu = this_cpu_ptr(&percpu_windows);
	int idx;

	for (idx = 0; idx < global_events.nr; ++idx) {
		struct perf_event *event = this_cpu->events[idx].event;

		if (global_events.is_amu[idx]) {
			new_readings[idx] = amu_counters_read(idx);
			continue;
		}

		if (!event || !event->pmu) {
			new_readings[idx] = 0;
			continue;
		}

		if (event->state == PERF_EVENT_STATE_ACTIVE)
			event->pmu->read(event);
		new_readings[idx] = local64_read(&event->count);
	}
}

static unsigned long xmu_cycles_to_freq(u64 cycles, u64 ms)
{
	unsigned long freq = (unsigned long)(cycles / ms);

	return freq + (freq >> 2);
}

static unsigned int xmu_get_hw_max_freq(int cpu)
{
	struct cpufreq_policy *policy;
	unsigned int max_freq = 0;

	policy = cpufreq_cpu_get(cpu);
	if (!policy) {
		pr_warn("XMU failed to get cpufreq max freq for cpu%d, fallback to 0\n", cpu);
		return 0;
	}

	max_freq = policy->cpuinfo.max_freq;

	cpufreq_cpu_put(policy);

	return max_freq;
}

/*
 * Initialize cycle scales for all CPUs
 * scale[cpu] = capacity[cpu] / max_freq[cpu], in Q32 format.
 */
static void xmu_init_cycle_scales(void)
{
	unsigned long cap = 0;
	unsigned int max_freq = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		u32 scale;

		cap = arch_scale_cpu_capacity(cpu);
		max_freq = xmu_get_hw_max_freq(cpu);

		if (!cap || !max_freq)
			goto invalid_scale;

		scale = div_u64((u64)cap << XMU_CYCLE_SCALE_SHIFT, max_freq);

		if (!scale)
			goto invalid_scale;

		per_cpu(xmu_cycle_scale, cpu) = scale;

		pr_info("CPU %d xmu_cycle_scale %u\n", cpu, scale);
	}
	return;

invalid_scale:
	pr_warn("XMU invalid cycle scale on cpu%d: cap=%lu max_freq=%u; set all to 0\n",
		cpu, cap, max_freq);

	for_each_possible_cpu(cpu)
		per_cpu(xmu_cycle_scale, cpu) = 0;
}

/*
 * Convert task_cycle_est from old_cpu's cycle domain to new_cpu's
 * cycle domain:
 * new_cycles = old_cycles * scale[old_cpu] / scale[new_cpu]
 */
static u64 xmu_cycle_convert_cpu(u64 cycles, u32 old_scale, u32 new_scale)
{
	if (!cycles || old_scale == new_scale || !old_scale || !new_scale)
		return cycles;

	return div_u64(cycles * old_scale, new_scale);
}

/*
 * Recompute the per-CPU cached frequency-limit ratio from the current
 * cycle and memory-stall rolling-window averages.  Called when xmu_avg
 * changes (window roll).
 *
 * The ratio is stored as a SCHED_CAPACITY_SCALE (1 << SHIFT) fixed-point
 * value:  limited_freq = (freq * limit_ratio) >> SHIFT.
 *
 * When no valid data is available or limiting is effectively disabled,
 * limit_ratio is set to SCHED_CAPACITY_SCALE (i.e. no reduction).
 */
static void
xmu_update_limit_ratio(struct cpu_xmu_info *cpuptr)
{
	u64 total_stalls = cpuptr->stats[xmu_mem_stall_idx].xmu_avg;
	u64 total_cycles = cpuptr->stats[xmu_cycle_idx].xmu_avg;
	int factor = READ_ONCE(limit_factor);
	u64 stall_sum;

	if (!total_cycles || !factor || !cpuptr->mem_stall_factor) {
		cpuptr->limit_ratio = SCHED_CAPACITY_SCALE;
		return;
	}

	stall_sum = total_stalls;
	stall_sum *= cpuptr->mem_stall_factor;
	stall_sum *= factor;
	stall_sum >>= SCHED_CAPACITY_SHIFT;
	stall_sum /= total_cycles;
	stall_sum = min(stall_sum, TRANS_FREQLIMIT_MAX_LIMIT_SCALE);
	cpuptr->limit_ratio = SCHED_CAPACITY_SCALE - stall_sum;
}

static void
xmu_counters_delta_get(u64 *delta_readings, struct cpu_xmu_info *info)
{
	int i;

	/* First, read the latest counters. */
	xmu_counters_read(delta_readings);

	/* Then, calculate the delta and update last_readings. */
	for (i = 0; i < global_events.nr; ++i) {
		u64 prev_reading = info->events[i].last_reading;

		if (delta_readings[i] > prev_reading) {
			info->events[i].last_reading = delta_readings[i];
			delta_readings[i] -= prev_reading;
		} else
			delta_readings[i] = 0;
	}
}

static void
cpu_windows_update_move(u64 *deltas, struct cpu_xmu_info *this_cpu, u64 now,
			    enum xmu_action action)
{
	int i, nr_move;

	if (action == xmu_action_tick) {
		this_cpu->last_update_time = now;
		this_cpu->last_window_start = now;
		nr_move = 1;
	} else {
		this_cpu->last_update_time = now;
		nr_move = (now - this_cpu->last_window_start) / WIN_DURATION_NS;
		this_cpu->last_window_start += nr_move * WIN_DURATION_NS;
	}

	for (i = 0; i < global_events.nr; ++i)
		this_cpu->stats[i].xmu_curr += deltas[i];

	if (!nr_move)
		return;

	for (i = 0; i < global_events.nr; ++i) {
		struct xmu_stat *this_stat = &this_cpu->stats[i];

		this_stat->xmu_avg = (this_stat->xmu_avg + this_stat->xmu_curr) >> min(nr_move, 63);
		this_stat->xmu_curr = 0;
	}
	xmu_update_limit_ratio(this_cpu);

	if (trace_xmu_cpu_metrics_enabled())
		trace_xmu_cpu_metrics(smp_processor_id(),
				      this_cpu->stats[xmu_cycle_idx].xmu_avg,
				      this_cpu->stats[xmu_mem_stall_idx].xmu_avg,
				      this_cpu->stats[xmu_inst_idx].xmu_avg,
				      this_cpu->stats[xmu_l2_refill_idx].xmu_avg,
				      this_cpu->stats[xmu_l3_refill_idx].xmu_avg);
}

static void
task_windows_update_move(u64 *deltas, struct task_struct *p, u64 now,
			 enum xmu_action action)
{
	struct task_xmu_info *txmu;
	int i, nr_move = 0;

	if (!p)
		return;

	txmu = task_xmu_get(p);
	if (!txmu)
		return;

	if (now <= txmu->last_update_time)
		return;

	if (action == xmu_action_tick) {
		txmu->last_update_time = now;
		txmu->last_window_start = now;
		nr_move = 1;
	} else {
		txmu->last_update_time = now;
		nr_move = (now - txmu->last_window_start) / WIN_DURATION_NS;
		txmu->last_window_start += nr_move * WIN_DURATION_NS;
	}

	if (action != xmu_action_set_next)
		for (i = 0; i < global_events.nr; ++i)
			txmu->stats[i].xmu_curr += deltas[i];

	if (!nr_move)
		return;

	for (i = 0; i < global_events.nr; ++i) {
		struct xmu_stat *this_stat = &txmu->stats[i];

		this_stat->xmu_avg = (this_stat->xmu_avg + this_stat->xmu_curr) >> min(nr_move, 63);
		this_stat->xmu_curr = 0;
	}

	if (trace_xmu_task_metrics_enabled())
		trace_xmu_task_metrics(p->pid,
				       txmu->stats[xmu_cycle_idx].xmu_avg,
				       txmu->stats[xmu_mem_stall_idx].xmu_avg,
				       txmu->stats[xmu_inst_idx].xmu_avg,
				       txmu->stats[xmu_l2_refill_idx].xmu_avg,
				       txmu->stats[xmu_l3_refill_idx].xmu_avg);
}

void
xmu_tick_entry(struct rq *rq)
{
	struct cpu_xmu_info *this_cpu = this_cpu_ptr(&percpu_windows);
	u64 cpu_xmu_deltas[xmu_nr_events_full];
	struct task_struct *curr = rq->curr;
	u64 now = ktime_get_ns();

	if (now <= this_cpu->last_update_time)
		return;

	xmu_counters_delta_get(cpu_xmu_deltas, this_cpu);
	cpu_windows_update_move(cpu_xmu_deltas, this_cpu, now, xmu_action_tick);
	if (curr && !is_idle_task(curr))
		task_windows_update_move(cpu_xmu_deltas, curr, now, xmu_action_tick);
}

void
xmu_schedule(struct task_struct *prev, struct task_struct *next, struct rq *rq)
{
	struct cpu_xmu_info *this_cpu = this_cpu_ptr(&percpu_windows);
	u64 cpu_xmu_deltas[xmu_nr_events_full];
	u64 now = ktime_get_ns();

	if (prev == next)
		return;

	if (now <= this_cpu->last_update_time)
		return;

	xmu_counters_delta_get(cpu_xmu_deltas, this_cpu);
	cpu_windows_update_move(cpu_xmu_deltas, this_cpu, now, xmu_action_put_prev);
	if (prev && !is_idle_task(prev))
		task_windows_update_move(cpu_xmu_deltas, prev, now, xmu_action_put_prev);
	if (next && !is_idle_task(next))
		task_windows_update_move(cpu_xmu_deltas, next, now, xmu_action_set_next);
}

void xmu_task_enqueue(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_xmu_info *info = task_xmu_get(p);
	int cpu = cpu_of(rq), old_cpu;

	if (!info)
		return;
	if (info->on_rq)
		return;

	old_cpu = info->cycle_cpu;
	if (old_cpu < 0 || old_cpu >= nr_cpu_ids)
		return;

	if (old_cpu != cpu) {
		u32 old_scale = per_cpu(xmu_cycle_scale, old_cpu);
		u32 scale = per_cpu(xmu_cycle_scale, cpu);

		info->task_cycle_est =
			xmu_cycle_convert_cpu(info->task_cycle_est, old_scale, scale);
		info->stats[xmu_cycle_idx].xmu_avg =
			xmu_cycle_convert_cpu(info->stats[xmu_cycle_idx].xmu_avg, old_scale, scale);
		info->stats[xmu_cycle_idx].xmu_curr =
			xmu_cycle_convert_cpu(info->stats[xmu_cycle_idx].xmu_curr, old_scale, scale);

		info->cycle_cpu = cpu;
	}
	info->on_rq = true;
	per_cpu_ptr(&percpu_windows, cpu)->cpu_cycle_est += info->task_cycle_est;
}

void xmu_task_dequeue(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_xmu_info *info = task_xmu_get(p);
	u64 p_cycle_xmu_avg, p_cycle_est;

	if (!info)
		return;
	if (!info->on_rq)
		return;

	p_cycle_est = info->task_cycle_est;
	p_cycle_xmu_avg = info->stats[xmu_cycle_idx].xmu_avg;
	per_cpu_ptr(&percpu_windows, cpu_of(rq))->cpu_cycle_est -= p_cycle_est;
	info->on_rq = false;
	/* Copy mainline UTIL_EST behavior. */
	if (p_cycle_est <= p_cycle_xmu_avg)
		info->task_cycle_est = p_cycle_xmu_avg;
	else {
		u64 diff = p_cycle_est - p_cycle_xmu_avg;

		p_cycle_est <<= UTIL_EST_WEIGHT_SHIFT;
		p_cycle_est -= (diff + (diff >> 1));
		p_cycle_est >>= UTIL_EST_WEIGHT_SHIFT;
		info->task_cycle_est = p_cycle_est;
	}
}

u64
xmu_cpufreq_limit(int cpu, unsigned long *freq, unsigned long max)
{
	struct cpu_xmu_info *cpuptr = per_cpu_ptr(&percpu_windows, cpu);
	unsigned long limit, freq_ori;

	if (!check_uas_feature(FEATURE_XMU_CPULIMIT))
		return 0;
	if (!READ_ONCE(limit_factor))
		return 0;
	if (!freq)
		return cpuptr->mem_stall_factor;

	freq_ori = min(*freq, max);
	limit = (freq_ori * cpuptr->limit_ratio) >> SCHED_CAPACITY_SHIFT;

	if (trace_xmu_freqlimit_enabled())
		trace_xmu_freqlimit(cpu, freq_ori, limit, 0);

	*freq = limit;
	return cpuptr->mem_stall_factor;
}

/**
 * Calculate the target frequency from max(win_freq, est_freq).
 */
unsigned long xmu_cpufreq_set(int cpu)
{
	struct cpu_xmu_info *cpuptr;
	u64 cycle_xmu;

	if (!check_uas_feature(FEATURE_XMU_CPUFREQ_SET))
		return 0;
	if (!READ_ONCE(cpufreq_set_enable))
		return 0;
	if (cpu == -1)
		return 1;

	cpuptr = per_cpu_ptr(&percpu_windows, cpu);
	cycle_xmu = cpuptr->stats[xmu_cycle_idx].xmu_avg;
	cycle_xmu = max(cycle_xmu, cpuptr->cpu_cycle_est);

	return xmu_cycles_to_freq(cycle_xmu, WIN_DURATION_MS);
}

bool xmu_is_runnable_boost_enabled(void)
{
	if (!check_uas_feature(FEATURE_XMU_CPULIMIT))
		return true;

	return runnable_boost_enable;
}

static ssize_t limit_factor_show(struct kobject *kobj, struct kobj_attribute *kattr,
				 char *buf)
{
	return sprintf(buf, "%d\n", READ_ONCE(limit_factor));
}
static ssize_t limit_factor_store(struct kobject *kobj, struct kobj_attribute *kattr,
				  const char *buf, size_t count)
{
	int val;

	sscanf(buf, "%d", &val);
	WRITE_ONCE(limit_factor, val);
	smp_mb();

	return count;
}
static struct kobj_attribute limit_factor_attribute =
	__ATTR(limit_factor, 0664, limit_factor_show, limit_factor_store);

static ssize_t runnable_boost_enable_show(struct kobject *kobj, struct kobj_attribute *kattr,
					  char *buf)
{
	return sprintf(buf, "%d\n", READ_ONCE(runnable_boost_enable));
}
static ssize_t runnable_boost_enable_store(struct kobject *kobj, struct kobj_attribute *kattr,
					   const char *buf, size_t count)
{
	int val;

	sscanf(buf, "%d", &val);
	WRITE_ONCE(runnable_boost_enable, val);
	smp_mb();

	return count;
}
static struct kobj_attribute runnable_boost_enable_attribute =
	__ATTR(runnable_boost_enable, 0664, runnable_boost_enable_show,
	       runnable_boost_enable_store);

static ssize_t cpufreq_set_enable_show(struct kobject *kobj, struct kobj_attribute *kattr,
				      char *buf)
{
	return sprintf(buf, "%d\n", READ_ONCE(cpufreq_set_enable));
}
static ssize_t cpufreq_set_enable_store(struct kobject *kobj, struct kobj_attribute *kattr,
				      const char *buf, size_t count)
{
	int val;

	sscanf(buf, "%d", &val);
	WRITE_ONCE(cpufreq_set_enable, val);
	smp_mb();

	return count;
}
static struct kobj_attribute cpufreq_set_enable_attribute =
	__ATTR(cpufreq_set_enable, 0664, cpufreq_set_enable_show, cpufreq_set_enable_store);

static ssize_t last_update_time_show(struct kobject *kobj, struct kobj_attribute *kattr, char *buf)
{
	const char *name = kobject_name(kobj);
	struct cpu_xmu_info *cpuptr;
	int cpu, ret;

	ret = kstrtoint(name, 10, &cpu);
	if (ret)
		return ret;

	cpuptr = per_cpu_ptr(&percpu_windows, cpu);

	return scnprintf(buf, PAGE_SIZE, "%d: %llu\n", cpu, cpuptr->last_update_time);
}
static struct kobj_attribute last_update_time_attribute = __ATTR_RO(last_update_time);

#define XMU_SYSFS(event)						\
static ssize_t event##_show(struct kobject *kobj,			\
			    struct kobj_attribute *kattr,		\
			    char *buf)					\
{									\
	const char *name = kobject_name(kobj);				\
	struct cpu_xmu_info *cpuptr;					\
	int cpu, ret;							\
									\
	ret = kstrtoint(name, 10, &cpu);				\
	if (ret)							\
		return ret;						\
									\
	cpuptr = per_cpu_ptr(&percpu_windows, cpu);			\
									\
	return scnprintf(buf, PAGE_SIZE,				\
			 "CPU %d:\n"					\
			 "\tlast_reading: %llu\n"			\
			 "\txmu_avg: %llu\n"				\
			 "\txmu_curr: %llu\n",				\
			 cpu, cpuptr->events[event##_idx].last_reading,	\
			 cpuptr->stats[event##_idx].xmu_avg,		\
			 cpuptr->stats[event##_idx].xmu_curr);		\
}									\
static struct kobj_attribute event##_attribute = __ATTR_RO(event);

XMU_SYSFS(xmu_cycle)
XMU_SYSFS(xmu_mem_stall)
XMU_SYSFS(xmu_inst)
XMU_SYSFS(xmu_l2_refill)
XMU_SYSFS(xmu_l3_refill)

static struct attribute *xmu_attrs[] = {
	&last_update_time_attribute.attr,
	&xmu_cycle_attribute.attr,
	&xmu_mem_stall_attribute.attr,
	&xmu_inst_attribute.attr,
	&xmu_l2_refill_attribute.attr,
	&xmu_l3_refill_attribute.attr,
	NULL,
};

static struct attribute_group xmu_attr_group = {
	.attrs = xmu_attrs,
};

static void
xmu_cpufreq_sysfs_cleanup(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct kobject *this_obj = per_cpu(percpu_kobj, cpu);

		if (this_obj)
			kobject_put(this_obj);
	}
	kobject_put(xmu_cpufreq_kobj);
}

static int
xmu_cpufreq_sysfs_init(void)
{
#define DIR_SIZE 8
	char cpu_dir[DIR_SIZE];
	int cpu, ret = 0;

	xmu_cpufreq_kobj = kobject_create_and_add("tran_xmu_cpufreq", kernel_kobj);
	if (!xmu_cpufreq_kobj)
		return -ENOMEM;

	ret = sysfs_create_file(xmu_cpufreq_kobj, &limit_factor_attribute.attr);
	if (ret)
		return ret;

	ret = sysfs_create_file(xmu_cpufreq_kobj, &runnable_boost_enable_attribute.attr);
	if (ret)
		return ret;

	ret = sysfs_create_file(xmu_cpufreq_kobj, &cpufreq_set_enable_attribute.attr);
	if (ret)
		return ret;

	for_each_possible_cpu(cpu) {
		struct kobject **this_obj = per_cpu_ptr(&percpu_kobj, cpu), *temp;

		snprintf(cpu_dir, DIR_SIZE, "%d", cpu);
		temp = kobject_create_and_add(cpu_dir, xmu_cpufreq_kobj);
		if (!temp)
			return -ENOMEM;
		*this_obj = temp;

		ret = sysfs_create_group(temp, &xmu_attr_group);
		if (ret)
			return ret;
	}

	return 0;
}

static struct perf_event_attr *alloc_attr(int event_id)
{
	struct perf_event_attr *attr;

	attr = kzalloc(sizeof(struct perf_event_attr), GFP_KERNEL);
	if (!attr)
		return ERR_PTR(-ENOMEM);

	attr->type = PERF_TYPE_RAW;
	attr->config = (u64)event_id;
	attr->size = sizeof(struct perf_event_attr);
	attr->pinned = (u64)1;
	attr->disabled = 1;

	return attr;
}

static int xmu_idx_to_id(enum xmu_events id)
{
	if (id == xmu_cycle_idx)
		return ARMV8_PMUV3_PERFCTR_CPU_CYCLES;
	if (id == xmu_mem_stall_idx)
		return ARMV8_AMU_PERFCTR_STALL_BACKEND_MEM;
	if (id == xmu_inst_idx)
		return ARMV8_PMUV3_PERFCTR_INST_RETIRED;
	if (id == xmu_l2_refill_idx)
		return ARMV8_PMUV3_PERFCTR_L2D_CACHE_REFILL;
	if (id == xmu_l3_refill_idx)
		return ARMV8_PMUV3_PERFCTR_L3D_CACHE_REFILL;
	return 0;
}

static int pmu_create_cpu_counters(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_xmu_info *cpuptr = per_cpu_ptr(&percpu_windows, cpu);
		int i;

		for (i = 0; i < global_events.nr; ++i) {
			int event_id = xmu_idx_to_id(i);
			struct perf_event_attr *attr;
			struct perf_event *event;

			if (global_events.is_amu[i])
				continue;

			attr = alloc_attr(event_id);
			if (IS_ERR(attr))
				return PTR_ERR(attr);

			event = perf_event_create_kernel_counter(attr, cpu, NULL, NULL, NULL);
			if (IS_ERR(event)) {
				pr_err("unable to create perf event (cpu:%i-type:%d-config:0x%llx) : %ld",
				       cpu, attr->type, attr->config, PTR_ERR(event));
				kfree(attr);
				return PTR_ERR(event);
			}

			perf_event_enable(event);
			if (event->hw.idx == -1) {
				pr_err("pinned event unable to get onto hardware, perf event (cpu:%i-type:%d-config:0x%llx)",
				       cpu, attr->type, attr->config);
				kfree(attr);
				return -EINVAL;
			}
			cpuptr->events[i].event = event;
			kfree(attr);
		}
	}

	return 0;
}

static void pmu_release_cpu_counters(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu_xmu_info *cpuptr = per_cpu_ptr(&percpu_windows, cpu);
		int i;

		for (i = 0; i < global_events.nr; ++i) {
			if (cpuptr->events[i].event == NULL)
				continue;

			perf_event_release_kernel(cpuptr->events[i].event);
			cpuptr->events[i].event = NULL;
		}
	}
}

static const u32 soc_stall_factors[][8] = {
	[xmu_soc_default] = {0, 0, 0, 0, 0, 0, 0, 0},
	[xmu_soc_D8400] = {375U, 375U, 375U, 375U, 375U, 375U, 375U, 375U},
	[xmu_soc_D7400] = {0, 0, 0, 0, 415U, 415U, 415U, 415U},
};

static enum xmu_soc_type
probe_soc(void)
{
	struct device_node *root = of_find_node_by_path("/");
	enum xmu_soc_type ret = xmu_soc_default;
	struct cpu_xmu_info *cpuptr;
	const char *model_name;
	const u32 *factors;
	int cpu;

	if (!root)
		return ret;

	if (of_property_read_string(root, "model-part-name", &model_name) != 0)
		goto out_put;

	if (strncmp(model_name, MODEL_NAME_D8400, strlen(MODEL_NAME_D8400)) == 0)
		ret = xmu_soc_D8400;
	else if (strncmp(model_name, MODEL_NAME_D7400, strlen(MODEL_NAME_D7400)) == 0 ||
		 strncmp(model_name, MODEL_NAME_D7400E, strlen(MODEL_NAME_D7400E)) == 0)
		ret = xmu_soc_D7400;
	else
		goto out_put;

	factors = soc_stall_factors[ret];
	pr_info("XMU detected SoC %s\n", model_name);
	for_each_possible_cpu(cpu) {
		cpuptr = per_cpu_ptr(&percpu_windows, cpu);
		BUG_ON(cpu >= ARRAY_SIZE(soc_stall_factors[0]));
		cpuptr->mem_stall_factor = factors[cpu];
		cpuptr->limit_ratio = SCHED_CAPACITY_SCALE;
		pr_info("CPU %d stall_factor %u\n", cpu, factors[cpu]);
	}
	xmu_init_cycle_scales();

out_put:
	of_node_put(root);
	return ret;
}

int __init xmu_cpufreq_init(void)
{
	enum xmu_soc_type soc;
	int ret;

	pr_info("XMU init start.\n");

	runnable_boost_enable = 1;

	soc = probe_soc();
	if (soc == xmu_soc_D7400) {
		global_events.is_amu[xmu_cycle_idx] = false;
		global_events.is_amu[xmu_mem_stall_idx] = false;
		global_events.nr = xmu_nr_events_pmu;
		pr_info("ARMv8 XMU populated\n");
	} else if (soc == xmu_soc_D8400) {
		global_events.is_amu[xmu_cycle_idx] = true;
		global_events.is_amu[xmu_mem_stall_idx] = true;
		global_events.is_amu[xmu_inst_idx] = true;
		global_events.nr = xmu_nr_events_amu;
		pr_info("ARMv9 XMU populated\n");
	} else {
		pr_info("No suitable XMU CPU\n");
		return 0;
	}

	ret = pmu_create_cpu_counters();
	if (ret)
		goto error;
	pr_info("XMU PMU init done.\n");

	ret = xmu_cpufreq_sysfs_init();
	if (ret)
		goto error;

	task_xmu_cache = kmem_cache_create("task_xmu_info",
					   sizeof(struct task_xmu_info),
					   0, SLAB_ACCOUNT, NULL);
	if (!task_xmu_cache) {
		ret = -ENOMEM;
		goto error;
	}
	WRITE_ONCE(xmu_active, true);
	pr_info("Transsion XMU init done.\n");

	return 0;
error:
	pr_err("Transsion XMU cpufreq driver init failed with %d\n", ret);
	xmu_cpufreq_sysfs_cleanup();
	pmu_release_cpu_counters();
	if (task_xmu_cache) {
		kmem_cache_destroy(task_xmu_cache);
		task_xmu_cache = NULL;
	}
	return ret;
}

void __exit xmu_cpufreq_exit(void)
{
	WRITE_ONCE(xmu_active, false);
	xmu_cpufreq_sysfs_cleanup();
	pmu_release_cpu_counters();
	if (task_xmu_cache) {
		kmem_cache_destroy(task_xmu_cache);
		task_xmu_cache = NULL;
	}
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0) */
#include "trans_xmu.h"

struct task_xmu_info *xmu_task_info_alloc(void) { return NULL; }
void xmu_task_info_free(struct trans_task_struct *tts) { }

int __init xmu_cpufreq_init(void)
{
	return 0;
}

void __exit xmu_cpufreq_exit(void)
{
}

void xmu_schedule(struct task_struct *prev, struct task_struct *next, struct rq *rq)
{
}

void xmu_tick_entry(struct rq *rq)
{
}

u64 xmu_cpufreq_limit(int cpu, unsigned long *freq, unsigned long max)
{
	return 0;
}

bool xmu_is_runnable_boost_enabled(void)
{
	return true;
}

void xmu_task_enqueue(struct rq *rq, struct task_struct *p, int flags)
{
}

void xmu_task_dequeue(struct rq *rq, struct task_struct *p, int flags)
{
}

unsigned long xmu_cpufreq_set(int cpu)
{
	return 0;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0) */
