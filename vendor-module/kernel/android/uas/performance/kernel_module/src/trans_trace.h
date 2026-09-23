/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 transsion Inc.
 */
#if !defined(__TRANS_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __TRANS_TRACE_H__

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/kernel.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#include <linux/args.h>
#endif

#include "sched_common.h"
#include "trans_balance.h"
#include "trans_loading.h"


#undef TRACE_SYSTEM
#define TRACE_SYSTEM trans_sched

#define TRANS_DINFO_LEN    (1024)

TRACE_EVENT(dump_info,

    TP_PROTO(const unsigned long *store, int size,
        bool is_waker, struct task_struct *task, u64 delay_ms),

    TP_ARGS(store, size, is_waker, task, delay_ms),

    TP_STRUCT__entry(
        __dynamic_array(char, stack_info, TRANS_DINFO_LEN)
    ),

    TP_fast_assign(
        char *buf = __get_dynamic_array(stack_info);
        struct task_struct *tsk = is_waker ? current : task;
        int offset = 0;
        int idx;

        offset = snprintf(buf, TRANS_DINFO_LEN, "[%s: %s$%d, tgid=%d, prio=%d, ux=%#x",
                is_waker ? "waker" : "wakee", tsk->comm, tsk->pid, tsk->tgid, tsk->prio, get_ux_tags(tsk));

        if (!is_waker)
            offset += snprintf(buf + offset, TRANS_DINFO_LEN - offset, ", delay_ms=%llu", delay_ms);

        offset += snprintf(buf + offset, TRANS_DINFO_LEN - offset, "]");

        if (store != NULL && size > 0) {
            offset += snprintf(buf + offset, TRANS_DINFO_LEN - offset, ": ");
            for (idx = size - 1; idx >= 0; idx--) {
                if (offset < TRANS_DINFO_LEN)
                    offset += snprintf(buf + offset, TRANS_DINFO_LEN - offset, "%pS %s ", (void *)store[idx], idx ? "<-" : "\n");
            }
        }
    ),

    TP_printk("%s",(char *)__get_dynamic_array(stack_info))
);

TRACE_EVENT(walk_each_cpu,

    TP_PROTO(struct task_struct *p, cpumask_t search_cpus, int cpu),

    TP_ARGS(p, search_cpus, cpu),

    TP_STRUCT__entry(
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
        __field(u32,            ux_tags)
        __array(char,           cpus_mask, 32)
        __array(char,           search_cpus, 32)
        __field(int,            cpu)
        __field(int,            online)
        __field(int,            active)
        __field(int,            halted)
        __field(int,            partial_halted)
        __field(int,            has_rt)
        __field(int,            has_ux)
        __field(int,            is_pipeline)),

    TP_fast_assign(
        __entry->pid            = p->pid;
        __entry->prio           = p->prio;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        __entry->ux_tags        = get_ux_tags(p);
        scnprintf(__entry->cpus_mask, sizeof(__entry->cpus_mask),
                    "%*pbl", cpumask_pr_args(&p->cpus_mask));
        scnprintf(__entry->search_cpus, sizeof(__entry->search_cpus),
                    "%*pbl", cpumask_pr_args(&search_cpus));
        __entry->cpu            = cpu;
        __entry->online         = cpu_online(cpu);
        __entry->active         = cpu_active(cpu);
        __entry->halted         = uas_cpu_halted(cpu);
        __entry->partial_halted = uas_cpu_partial_halted(cpu);
        __entry->has_rt         = has_rt_tasks(cpu);
        __entry->has_ux         = has_ux_tasks(cpu);
        __entry->is_pipeline    = trans_is_pipeline_cpu(cpu);),

    TP_printk("task=%s$%d, prio=%d, ux_tags=0x%x, cpus_mask=%s, search_cpus=%s, "
            "cpu=%d, online=%d, active=%d, halted=%d, partial_halted=%d, "
            "has_rt=%d, has_ux=%d, is_pipeline=%d",
        __entry->comm, __entry->pid, __entry->prio,
        __entry->ux_tags,
        __entry->cpus_mask,
        __entry->search_cpus,
        __entry->cpu,
        __entry->online,
        __entry->active,
        __entry->halted,
        __entry->partial_halted,
        __entry->has_rt,
        __entry->has_ux,
        __entry->is_pipeline)
);


TRACE_EVENT(find_available_cpu,

    TP_PROTO(bool ret, int reason, struct task_struct *p,
                int ux_tags, int load_avg, int order_idx,
                int walk_cnt, int orig_cpu, int cpu),

    TP_ARGS(ret, reason, p, ux_tags, load_avg, order_idx, walk_cnt, orig_cpu, cpu),

    TP_STRUCT__entry(
        __field(bool,           succ)
        __field(int,            reason)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
        __array(char,           cpus, 32)
        __field(int,            scene)
        __field(int,            sched_boost_type)
        __field(int,            ux_tags)
        __field(int,            tag_cpumask)
        __field(int,            load_avg)
        __field(int,            need_boost)
        __field(int,            raw_util)
        __field(int,            order_idx)
        __field(int,            walk_cnt)
        __field(int,            orig_cpu)
        __field(int,            cpu)),

    TP_fast_assign(
        __entry->succ           = ret;
        __entry->reason         = reason;
        __entry->pid            = p->pid;
        __entry->prio           = p->prio;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        scnprintf(__entry->cpus, sizeof(__entry->cpus),
                    "%*pbl", cpumask_pr_args(&p->cpus_mask));
        __entry->scene          = g_sched_scene;
        __entry->sched_boost_type = get_sched_boost_type();
        __entry->ux_tags        = ux_tags;
        __entry->tag_cpumask    = ux_tags & UX_FLAG_CPU_MASK;
        __entry->load_avg       = load_avg;
        __entry->need_boost     = need_boost(load_avg);
        __entry->raw_util       = READ_ONCE(p->se.avg.util_avg);
        __entry->order_idx      = order_idx;
        __entry->walk_cnt       = walk_cnt;
        __entry->orig_cpu       = orig_cpu;
        __entry->cpu            = cpu;),

    TP_printk("[%s:%d]: task=%s$%d, prio=%d, ux_tags=0x%x, cpus_allowed=%s, "
            "scene=%d, sched_boost_type=%d, "
            "tag_cpumask=0x%x, load_avg=%d, raw_util=%d, need_boost=%d, "
            "order_idx=%d, walk_cnt=%d, orig_cpu=%d, cpu=%d",
        __entry->succ ? "SUCC" : __entry->reason == 1 ? "SKIP" : "FAIL",
        __entry->reason,
        __entry->comm, __entry->pid, __entry->prio,
        __entry->ux_tags,
        __entry->cpus,
        __entry->scene,
        __entry->sched_boost_type,
        __entry->tag_cpumask,
        __entry->load_avg,
        __entry->raw_util,
        __entry->need_boost,
        __entry->order_idx, __entry->walk_cnt,
        __entry->orig_cpu, __entry->cpu)
);


TRACE_EVENT(filterout_on_wakeup_rt,

    TP_PROTO(bool ret, struct task_struct *p, int load_avg, int cpu),

    TP_ARGS(ret, p, load_avg, cpu),

    TP_STRUCT__entry(
        __field(bool,           filterout)
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
        __field(int,            load_avg)
        __field(int,            has_rt)
        __field(int,            has_ux)
        __field(int,            is_pipeline)
        __field(int,            scene)
        __field(int,            cpu)
        __field(int,            capacity)
        __field(int,            uclamp_max)
        __field(int,            uclamp_min)
        __field(int,            fits)),

    TP_fast_assign(
        __entry->filterout      = ret;
        __entry->pid            = p->pid;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        __entry->load_avg       = load_avg;
        __entry->has_rt         = has_rt_tasks(cpu);
        __entry->has_ux         = has_ux_tasks(cpu);
        __entry->is_pipeline    = trans_is_pipeline_cpu(cpu);
        __entry->scene          = g_sched_scene;
        __entry->cpu            = cpu;
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        __entry->capacity       = cpu_rq(cpu)->cpu_capacity;
        #else
        __entry->capacity       = cpu_rq(cpu)->cpu_capacity_orig;
        #endif
        __entry->uclamp_max     = uclamp_eff_value(p, UCLAMP_MAX);
        __entry->uclamp_min     = uclamp_eff_value(p, UCLAMP_MIN);
        __entry->fits           = __entry->capacity >= min(__entry->uclamp_min,
                                    __entry->uclamp_max);),

    TP_printk("[%s]: cpu%d, capacity=%d, task=%s$%d, "
            "uclamp_max=%d, uclamp_min=%d, fits=%d, "
            "scene=%d, has_ux=%d, has_rt=%d, "
            "is_pipeline=%d, "
            "load_avg=%d",
        __entry->filterout ? "FOT" : "RES",
        __entry->cpu, __entry->capacity,
        __entry->comm, __entry->pid,
        __entry->uclamp_max, __entry->uclamp_min, __entry->fits,
        __entry->scene,
        __entry->has_ux, __entry->has_rt,
        __entry->is_pipeline,
        __entry->load_avg)
);


TRACE_EVENT(adjust_rt_lowest_mask,

    TP_PROTO(struct task_struct *p,
        cpumask_t backup_lowest_mask, cpumask_t lowest_mask),

    TP_ARGS(p, backup_lowest_mask, lowest_mask),

    TP_STRUCT__entry(
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
        __array(char,           backup_lowest_mask, 32)
        __array(char,           lowest_mask, 32)
        __field(int,            emtpy)),

    TP_fast_assign(
        __entry->pid            = p->pid;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        scnprintf(__entry->backup_lowest_mask, sizeof(__entry->backup_lowest_mask),
                    "%*pbl", cpumask_pr_args(&backup_lowest_mask));
        scnprintf(__entry->lowest_mask, sizeof(__entry->lowest_mask),
                    "%*pbl", cpumask_pr_args(&lowest_mask));
        __entry->emtpy          = cpumask_empty(&lowest_mask);),

    TP_printk("task=%s$%d, backup_lowest_mask=%s, lowest_mask=%s, empty=%d",
        __entry->comm, __entry->pid,
        __entry->backup_lowest_mask,
        __entry->lowest_mask,
        __entry->emtpy)
);


TRACE_EVENT(trans_find_lowest_rq,

    TP_PROTO(struct task_struct *p, int load_avg, cpumask_t search_cpus, int target_cpu),

    TP_ARGS(p, load_avg, search_cpus, target_cpu),

    TP_STRUCT__entry(
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
        __field(int,            load_avg)
        __field(int,            scene)
        __array(char,           search_cpus, 32)
        __field(int,            emtpy)
        __field(int,            target_cpu)),

    TP_fast_assign(
        __entry->pid            = p->pid;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        __entry->load_avg       = load_avg;
        __entry->scene          = g_sched_scene;
        scnprintf(__entry->search_cpus, sizeof(__entry->search_cpus),
                    "%*pbl", cpumask_pr_args(&search_cpus));
        __entry->emtpy          = cpumask_empty(&search_cpus);
        __entry->target_cpu     = target_cpu;),

    TP_printk("task=%s$%d, scene=%d, load_avg=%d, target_cpu=%d, search_cpus=%s, empty=%d",
        __entry->comm, __entry->pid,
        __entry->scene,
        __entry->load_avg,
        __entry->target_cpu,
        __entry->search_cpus,
        __entry->emtpy)
);


TRACE_EVENT(android_rvh_find_lowest_rq_hook,

    TP_PROTO(struct task_struct *p,
        cpumask_t lowest_mask_backup, cpumask_t lowest_mask,
        int best_cpu_backup, int best_cpu),

    TP_ARGS(p, lowest_mask_backup, lowest_mask, best_cpu_backup, best_cpu),

    TP_STRUCT__entry(
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
        __array(char,           lowest_mask_backup, 32)
        __array(char,           lowest_mask, 32)
        __field(int,            best_cpu_backup)
        __field(int,            best_cpu)),

    TP_fast_assign(
        __entry->pid            = p->pid;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        scnprintf(__entry->lowest_mask_backup, sizeof(__entry->lowest_mask_backup),
                    "%*pbl", cpumask_pr_args(&lowest_mask_backup));
        scnprintf(__entry->lowest_mask, sizeof(__entry->lowest_mask),
                    "%*pbl", cpumask_pr_args(&lowest_mask));
        __entry->best_cpu_backup   = best_cpu_backup;
        __entry->best_cpu          = best_cpu;
    ),

    TP_printk("task=%s$%d, lowest_mask_backup=%s, lowest_mask=%s, best_cpu_backup=%d, best_cpu=%d",
        __entry->comm, __entry->pid,
        __entry->lowest_mask_backup,
        __entry->lowest_mask,
        __entry->best_cpu_backup,
        __entry->best_cpu)
);


TRACE_EVENT(should_honor_sync,

    TP_PROTO(int cpu, struct task_struct *waker, struct task_struct *wakee, bool raw_sync, bool sync),

    TP_ARGS(cpu, waker, wakee, raw_sync, sync),

    TP_STRUCT__entry(
        __field(int,            cpu)

        __field(int,            waker_pid)
        __field(int,            waker_prio)
        __array(char,           waker_comm, TASK_COMM_LEN)

        __field(int,            wakee_pid)
        __field(int,            wakee_prio)
        __array(char,           wakee_comm, TASK_COMM_LEN)

        __field(bool,           raw_sync)
        __field(bool,           sync)
    ),

    TP_fast_assign(
        __entry->cpu            = cpu;

        __entry->waker_pid      = waker->pid;
        __entry->waker_prio     = waker->prio;


        __entry->wakee_pid      = wakee->pid;
        __entry->wakee_prio     = wakee->prio;
        memcpy(__entry->waker_comm, waker->comm, TASK_COMM_LEN);
        memcpy(__entry->wakee_comm, wakee->comm, TASK_COMM_LEN);

        __entry->raw_sync       = raw_sync;
        __entry->sync           = sync;

    ),

    TP_printk("cpu=%d, waker=%s$%d$%d, wakee=%s$%d$%d, raw_sync=%d, sync=%d",
        __entry->cpu,
        __entry->waker_comm, __entry->waker_pid, __entry->waker_prio,
        __entry->wakee_comm, __entry->wakee_pid, __entry->wakee_prio,
        __entry->raw_sync, __entry->sync
    )
);


TRACE_EVENT(pull_runnable_task,

    TP_PROTO(int src_cpu, int dst_cpu, struct task_struct *tsk),

    TP_ARGS(src_cpu, dst_cpu, tsk),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu        = src_cpu;
        __entry->dst_cpu        = dst_cpu;
        __entry->pid            = tsk->pid;
        __entry->prio           = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
    ),

    TP_printk("src_cpu=%d, dst_cpu=%d, task=%s$%d, prio=%d",
        __entry->src_cpu,
        __entry->dst_cpu,
        __entry->comm, __entry->pid,
        __entry->prio
    )
);


TRACE_EVENT(pull_running_task,

    TP_PROTO(int src_cpu, int dst_cpu, struct task_struct *curr),

    TP_ARGS(src_cpu, dst_cpu, curr),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu        = src_cpu;
        __entry->dst_cpu        = dst_cpu;
        __entry->pid            = curr->pid;
        __entry->prio           = curr->prio;
        memcpy(__entry->comm, curr->comm, TASK_COMM_LEN);
    ),

    TP_printk("src_cpu=%d, dst_cpu=%d, curr=%s$%d, prio=%d",
        __entry->src_cpu,
        __entry->dst_cpu,
        __entry->comm, __entry->pid,
        __entry->prio
    )
);


TRACE_EVENT(migration_detach_task,

    TP_PROTO(int src_cpu, int dst_cpu, int target_pid, struct task_struct *picked),

    TP_ARGS(src_cpu, dst_cpu, target_pid, picked),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            target_pid)
        __field(bool,           is_target)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu        = src_cpu;
        __entry->dst_cpu        = dst_cpu;
        __entry->target_pid     = target_pid;
        __entry->is_target      = (picked->pid == target_pid);
        __entry->pid            = picked->pid;
        __entry->prio           = picked->prio;
        memcpy(__entry->comm, picked->comm, TASK_COMM_LEN);
    ),

    TP_printk("[%s] target_pid=%d, src_cpu=%d, dst_cpu=%d, picked=%s$%d, prio=%d",
        __entry->is_target ? "TARGET" : "FALLBK",
        __entry->target_pid,
        __entry->src_cpu,
        __entry->dst_cpu,
        __entry->comm, __entry->pid,
        __entry->prio
    )
);


TRACE_EVENT(migration_exit_reason,

    TP_PROTO(int reason),

    TP_ARGS(reason),

    TP_STRUCT__entry(
        __field(int, reason)
    ),

    TP_fast_assign(
        __entry->reason = reason;
    ),

    TP_printk("reason=%d",
        __entry->reason
    )
);


TRACE_EVENT(wakeup_irqwb,

    TP_PROTO(int this_cpu, struct task_struct *prev, struct task_struct *next, int target_cpu),

    TP_ARGS(this_cpu, prev, next, target_cpu),

    TP_STRUCT__entry(
        __field(int,            this_cpu)
        __field(int,            target_cpu)
        __field(int,            prev_pid)
        __field(int,            prev_prio)
        __array(char,           prev_comm, TASK_COMM_LEN)
        __field(int,            next_pid)
        __field(int,            next_prio)
        __array(char,           next_comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->this_cpu       = this_cpu;
        __entry->target_cpu     = target_cpu;
        __entry->prev_pid       = prev->pid;
        __entry->prev_prio      = prev->prio;
        __entry->next_pid       = next->pid;
        __entry->next_prio      = next->prio;
        memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
        memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
    ),

    TP_printk("this_cpu=%d, target_cpu=%d, prev=%s$%d, p_prio=%d, next=%s$%d, n_prio=%d",
        __entry->this_cpu,
        __entry->target_cpu,
        __entry->prev_comm,
        __entry->prev_pid,
        __entry->prev_prio,
        __entry->next_comm,
        __entry->next_pid,
        __entry->next_prio
    )
);


TRACE_EVENT(irqwb_detach_task,

    TP_PROTO(int src_cpu, int dst_cpu, int prefer_pid, struct task_struct *tsk),

    TP_ARGS(src_cpu, dst_cpu, prefer_pid, tsk),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            prefer_pid)
        __field(int,            prio)
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu       = src_cpu;
        __entry->dst_cpu       = dst_cpu;
        __entry->prefer_pid    = prefer_pid;
        __entry->pid           = tsk->pid;
        __entry->prio          = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
    ),

    TP_printk("src_cpu=%d, dst_cpu=%d, prefer_pid=%d, task=%s$%d, prio=%d",
        __entry->src_cpu,
        __entry->dst_cpu,
        __entry->prefer_pid,
        __entry->comm,
        __entry->pid,
        __entry->prio
    )
);


TRACE_EVENT(irqwb_workfn,

    TP_PROTO(int cpu, bool kick, int irqwb_pid, int irqwb_push_cpu),

    TP_ARGS(cpu, kick, irqwb_pid, irqwb_push_cpu),

    TP_STRUCT__entry(
        __field(int,            cpu)
        __field(bool,           kick)
        __field(int,            nr_running)
        __field(int,            irqwb_pid)
        __field(int,            irqwb_push_cpu)
        __field(int,            has_ux)
        __field(int,            has_rt)
        __field(int,            active_balance)
    ),

    TP_fast_assign(
        __entry->cpu            = cpu;
        __entry->kick           = kick;
        __entry->irqwb_pid      = irqwb_pid;
        __entry->irqwb_push_cpu = irqwb_push_cpu;
        __entry->nr_running     = cpu_rq(cpu)->nr_running;
        __entry->has_ux         = has_ux_tasks(cpu);
        __entry->has_rt         = has_rt_tasks(cpu);
        __entry->active_balance = cpu_rq(cpu)->active_balance;

    ),

    TP_printk("[%s] cpu=%d, nr_running=%d, has_ux=%d, has_rt=%d, active_balance=%d, irqwb_pid=%d, irqwb_push_cpu=%d",
        __entry->kick ? "KICK" : "FAIL",
        __entry->cpu,
        __entry->nr_running,
        __entry->has_ux,
        __entry->has_rt,
        __entry->active_balance,
        __entry->irqwb_pid,
        __entry->irqwb_push_cpu
    )
);


TRACE_EVENT(irqwb_migration_exit_reason,

    TP_PROTO(int reason),

    TP_ARGS(reason),

    TP_STRUCT__entry(
        __field(int, reason)
    ),

    TP_fast_assign(
        __entry->reason = reason;
    ),

    TP_printk("reason=%d",
        __entry->reason
    )
);


TRACE_EVENT(tickpull_critical_task,

    TP_PROTO(struct rq *rq, struct task_struct *curr, bool trigger, int path),

    TP_ARGS(rq, curr, trigger, path),

    TP_STRUCT__entry(
        __field(int,            this_cpu)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
        __field(bool,           is_ux)
        __field(bool,           is_rt)
        __field(bool,           is_pipeline)
        __field(bool,           has_ux)
        __field(bool,           has_rt)
        __field(bool,           trigger)
        __field(int,            path)
    ),

    TP_fast_assign(
        __entry->this_cpu       = rq->cpu;
        __entry->pid            = curr->pid;
        __entry->prio           = curr->prio;
        memcpy(__entry->comm, curr->comm, TASK_COMM_LEN);
        __entry->is_ux          = is_ux_task(curr, NULL);
        __entry->is_rt          = is_rt_task(curr);
        __entry->is_pipeline    = trans_is_pipeline_task(curr);
        __entry->has_ux         = has_runnable_ux_tasks(rq->cpu);
        __entry->has_rt         = has_runnable_rt_tasks(rq->cpu);
        __entry->trigger        = trigger;
        __entry->path           = path;
    ),

    TP_printk("this_cpu=%d, curr=%s$%d, prio=%d, is_ux=%d, is_rt=%d, is_pipeline=%d, has_ux=%d, has_rt=%d, trigger=%d, path=%d",
        __entry->this_cpu,
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->is_ux,
        __entry->is_rt,
        __entry->is_pipeline,
        __entry->has_ux,
        __entry->has_rt,
        __entry->trigger,
        __entry->path
    )
);


TRACE_EVENT(newidle_pull_runnable_ux,

    TP_PROTO(struct task_struct *tsk, int src_cpu, int dst_cpu),

    TP_ARGS(tsk, src_cpu, dst_cpu),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu        = src_cpu;
        __entry->dst_cpu        = dst_cpu;
        __entry->pid            = tsk->pid;
        __entry->prio           = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
    ),

    TP_printk("tsk=%s$%d, prio=%d, src_cpu=%d, dst_cpu=%d",
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->src_cpu,
        __entry->dst_cpu
    )
);


TRACE_EVENT(newidle_pull_runnable_rt,

    TP_PROTO(struct task_struct *tsk, int src_cpu, int dst_cpu),

    TP_ARGS(tsk, src_cpu, dst_cpu),

    TP_STRUCT__entry(
        __field(int,            src_cpu)
        __field(int,            dst_cpu)
        __field(int,            pid)
        __field(int,            prio)
        __array(char,           comm, TASK_COMM_LEN)
    ),

    TP_fast_assign(
        __entry->src_cpu        = src_cpu;
        __entry->dst_cpu        = dst_cpu;
        __entry->pid            = tsk->pid;
        __entry->prio           = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
    ),

    TP_printk("tsk=%s$%d, prio=%d, src_cpu=%d, dst_cpu=%d",
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->src_cpu,
        __entry->dst_cpu
    )
);


TRACE_EVENT(calc_order_idx,

    TP_PROTO(int migr_type, int task_type, int curr_cls, int order_idx, int walk_cnt),

    TP_ARGS(migr_type, task_type, curr_cls, order_idx, walk_cnt),

    TP_STRUCT__entry(
        __field(int,            migr_type)
        __field(int,            task_type)
        __field(int,            curr_cls)
        __field(int,            order_idx)
        __field(int,            walk_cnt)
    ),

    TP_fast_assign(
        __entry->migr_type      = migr_type;
        __entry->task_type      = task_type;
        __entry->curr_cls       = curr_cls;
        __entry->order_idx      = order_idx;
        __entry->walk_cnt       = walk_cnt;
    ),

    TP_printk("migr_type=%d, task_type=%d, curr_cls=%d, order_idx=%d, walk_cnt=%d",
        __entry->migr_type,
        __entry->task_type,
        __entry->curr_cls,
        __entry->order_idx,
        __entry->walk_cnt
    )
);


DECLARE_EVENT_CLASS(loadbalance_consume_time_template,

    TP_PROTO(bool succ, int cpu, u64 consume),

    TP_ARGS(succ, cpu, consume),

    TP_STRUCT__entry(
        __field(bool,           succ)
        __field(int,            cpu)
        __field(u64,            consume)),

    TP_fast_assign(
        __entry->succ           = succ;
        __entry->cpu            = cpu;
        __entry->consume        = consume;),

    TP_printk("[%s] cpu=%d, consume=%llu(ns), WARN=%d",
        __entry->succ ? "SUCC" : "FAIL",
        __entry->cpu,
        __entry->consume,
        __entry->consume > 500000ULL)
);

DEFINE_EVENT(loadbalance_consume_time_template, newidle_pull_consume,
    TP_PROTO(bool succ, int cpu, u64 consume),
    TP_ARGS(succ, cpu, consume)
);

DEFINE_EVENT(loadbalance_consume_time_template, tick_pull_consume,
    TP_PROTO(bool succ, int cpu, u64 consume),
    TP_ARGS(succ, cpu, consume)
);

DEFINE_EVENT(loadbalance_consume_time_template, irqwb_consume,
    TP_PROTO(bool succ, int cpu, u64 consume),
    TP_ARGS(succ, cpu, consume)
);

DECLARE_EVENT_CLASS(consume_time_template,

    TP_PROTO(struct task_struct *p, u64 consume),

    TP_ARGS(p, consume),

    TP_STRUCT__entry(
        __field(int,            pid)
        __array(char,           comm, TASK_COMM_LEN)
        __field(u64,            consume)),

    TP_fast_assign(
        __entry->pid            = p->pid;
        memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
        __entry->consume        = consume;),

    TP_printk("task=%s$%d, consume=%llu(ns), WARN=%d",
        __entry->comm, __entry->pid,
        __entry->consume,
        __entry->consume > 500000ULL)
);

DEFINE_EVENT(consume_time_template, find_available_cpu_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

DEFINE_EVENT(consume_time_template, filterout_on_wakeup_rt_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

DEFINE_EVENT(consume_time_template, adjust_rt_lowest_mask_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

DEFINE_EVENT(consume_time_template, trans_find_lowest_rq_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

DEFINE_EVENT(consume_time_template, android_rvh_find_lowest_rq_hook_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

DEFINE_EVENT(consume_time_template, update_task_load_consume,
    TP_PROTO(struct task_struct *p, u64 consume),
    TP_ARGS(p, consume));

TRACE_EVENT(enqueue_lock_thread,

    TP_PROTO(struct rq *rq, struct task_struct *tsk),

    TP_ARGS(rq, tsk),

    TP_STRUCT__entry(
        __field(int, cpu)
        __field(int, nr_tasks)
        __field(int, pid)
        __field(int, prio)
        __array(char, comm, TASK_COMM_LEN)
        ),

    TP_fast_assign(
        __entry->cpu = cpu_of(rq);
        __entry->nr_tasks = get_trans_rq(rq)->lock_queue->nr_tasks;
        __entry->pid = tsk->pid;
        __entry->prio = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        ),

    TP_printk("task=%s$%d, prio=%d, cpu=%d, nr_tasks=%d",
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->cpu,
        __entry->nr_tasks)
);

TRACE_EVENT(dequeue_lock_thread,

    TP_PROTO(struct rq *rq, struct task_struct *tsk),

    TP_ARGS(rq, tsk),

    TP_STRUCT__entry(
        __field(int, cpu)
        __field(int, nr_tasks)
        __field(int, pid)
        __field(int, prio)
        __array(char, comm, TASK_COMM_LEN)
        ),

    TP_fast_assign(
        __entry->cpu = cpu_of(rq);
        __entry->nr_tasks = get_trans_rq(rq)->lock_queue->nr_tasks;
        __entry->pid = tsk->pid;
        __entry->prio = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        ),

    TP_printk("task=%s$%d, prio=%d, cpu=%d, nr_tasks=%d",
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->cpu,
        __entry->nr_tasks)
);


TRACE_EVENT(pick_lock_thread,

    TP_PROTO(struct rq *rq, struct task_struct *tsk),

    TP_ARGS(rq, tsk),

    TP_STRUCT__entry(
        __field(int, cpu)
        __field(int, nr_tasks)
        __field(int, pid)
        __field(int, prio)
        __array(char, comm, TASK_COMM_LEN)
        ),

    TP_fast_assign(
        __entry->cpu = cpu_of(rq);
        __entry->nr_tasks = get_trans_rq(rq)->lock_queue->nr_tasks;
        __entry->pid = tsk->pid;
        __entry->prio = tsk->prio;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        ),

    TP_printk("task=%s$%d, prio=%d, cpu=%d, nr_tasks=%d",
        __entry->comm, __entry->pid,
        __entry->prio,
        __entry->cpu,
        __entry->nr_tasks)
);


TRACE_EVENT(task_inlock_protect,

    TP_PROTO(struct task_struct *tsk, int depth, u64 lock_start,
                u64 running_time, int timeout),

    TP_ARGS(tsk, depth, lock_start, running_time, timeout),

    TP_STRUCT__entry(
        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)
        __field(int, depth)
        __field(u64, lock_start)
        __field(u64, now)
        __field(u64, running_time)
        __field(int, timeout)
        ),

    TP_fast_assign(
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;
        __entry->depth = depth;
        __entry->lock_start = lock_start;
        __entry->now = jiffies;
        __entry->running_time = running_time;
        __entry->timeout = timeout;
        ),

    TP_printk("task=%s$%d, depth=%d, lock_start=%llu, now=%llu, "
                "running_time=%llu, timeout=%d",
        __entry->comm, __entry->pid,
        __entry->depth,
        __entry->lock_start,
        __entry->now,
        __entry->running_time,
        __entry->timeout)
);


TRACE_EVENT(get_task_time,

    TP_PROTO(int cpath, int scene, struct task_struct *tsk,
        struct sched_info *si, u64 running_time, u64 runnable_time,
        u64 max_running, u64 max_runnable),

    TP_ARGS(cpath, scene, tsk, si, running_time, runnable_time, max_running, max_runnable),

    TP_STRUCT__entry(
        __field(int, cpath)
        __field(int, scene)
        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)
        __field(int, state)
        __field(int, on_cpu)
        __field(int, on_rq)
        __field(int, runnnig_on_cpu)
        __field(int, runnable_on_runqueue)
        __field(u64, now)
        __field(u64, pcount)
        __field(u64, run_delay)
        __field(u64, last_arrival)
        __field(u64, last_queued)
        __field(u64, enqueue_time)
        __field(u64, snap_run_delay)
        __field(u64, snap_pcount)
        __field(u64, running_time)
        __field(u64, runnable_time)
        __field(u64, max_running)
        __field(u64, max_runnable)
        ),

    TP_fast_assign(
        struct rq *rq = cpu_rq(task_cpu(tsk));
        struct trans_task_struct *tts = get_trans_task_struct(tsk);
        u64 now = rq->clock;

        __entry->cpath = cpath;
        __entry->scene = scene;
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;
        __entry->state = task_state(tsk);
        __entry->on_cpu = tsk->on_cpu;
        __entry->on_rq = tsk->on_rq;
        __entry->runnnig_on_cpu = task_is_runnnig_on_cpu(tsk);
        __entry->runnable_on_runqueue = task_is_runnable_on_runqueue(tsk);
        __entry->now = now;
        __entry->pcount = si->pcount;
        __entry->run_delay = si->run_delay;
        __entry->last_arrival = si->last_arrival;
        __entry->last_queued = si->last_queued;
        __entry->enqueue_time = tts->ld.enqueue_time;
        __entry->snap_run_delay = tts->ld.snap_run_delay;
        __entry->snap_pcount = tts->ld.snap_pcount;
        __entry->running_time = running_time;
        __entry->runnable_time = runnable_time;
        __entry->max_running = max_running;
        __entry->max_runnable = max_runnable;
        ),

    TP_printk("CALLPATH=%d, SCENE=%d, "
            "task=%s$%d, state=0x%x, on_cpu=%d, on_rq=%d, RN=%d-%d, "
            "pcount=%llu, run_delay=%llu, last_arrival=%llu, last_queued=%llu, "
            "enqueue_time=%llu, snap_run_delay=%llu, snap_pcount=%llu, "
            "now=%llu, running_time=%llu, runnable_time=%llu "
            "max_running=%llu, max_runnable=%llu "
            "flag=%d-%d-%d-%d-%d",
        __entry->cpath, __entry->scene,
        __entry->comm, __entry->pid,
        __entry->state, __entry->on_cpu, __entry->on_rq,
        __entry->runnnig_on_cpu, __entry->runnable_on_runqueue,
        __entry->pcount, __entry->run_delay, __entry->last_arrival, __entry->last_queued,
        __entry->enqueue_time, __entry->snap_run_delay, __entry->snap_pcount,
        __entry->now, __entry->running_time, __entry->runnable_time,
        __entry->max_running, __entry->max_runnable,
        (s64)__entry->running_time < 0,
        (s64)__entry->runnable_time < 0,
        __entry->runnable_time == __entry->now,
        __entry->running_time > 80000000000,
        __entry->runnable_time > 80000000000
    )
);

TRACE_EVENT(accumulate_load,

    TP_PROTO(struct load_env *env),

    TP_ARGS(env),

    TP_STRUCT__entry(
        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)
        __field(int, state)
        __field(int, on_cpu)
        __field(int, on_rq)
        __field(u64, now)
        __field(u64, raw_contrib)
        __field(u64, scale_contrib)
        __field(u64, s_last_running_time)
        __field(u64, s_period_contrib)
        __field(u64, s_last_update)
        __field(u64, s_load_sum)
        __field(u64, s_load_avg)
        __field(u64, raw_delta)
        __field(u64, scale_delta)
        __field(u64, delta_contrib)
        __field(u64, delta)
        __field(u64, periods)
        __field(u64, u)
        __field(u64, d1)
        __field(u64, c1)
        __field(u64, c2)
        __field(u64, d3)
        __field(u64, c3)
        __field(u64, load_sum)
        ),

    TP_fast_assign(
        struct task_struct *tsk = env->tsk;
        struct task_load *ld = env->ld;

        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;
        __entry->state = task_state(tsk);
        __entry->on_cpu = tsk->on_cpu;
        __entry->on_rq = tsk->on_rq;
        __entry->now = env->now;
        __entry->raw_contrib = env->raw_contrib;
        __entry->scale_contrib = env->scale_contrib;
        __entry->s_last_running_time = ld->last_running_time;
        __entry->s_period_contrib = ld->period_contrib;
        __entry->s_last_update = ld->last_update;
        __entry->s_load_sum = ld->load_sum;
        __entry->s_load_avg = ld->load_avg;
        __entry->raw_delta = env->raw_delta;
        __entry->scale_delta = env->scale_delta;
        __entry->delta_contrib = env->delta_contrib;
        __entry->delta = env->delta;
        __entry->periods = env->periods;
        __entry->u = env->u;
        __entry->d1 = env->d1;
        __entry->c1 = env->c1;
        __entry->c2 = env->c2;
        __entry->d3 = env->d3;
        __entry->c3 = env->c3;
        __entry->load_sum = env->load_sum;
        ),

    TP_printk("task=%s$%d, state=0x%x, on_cpu=%d, on_rq=%d, "
            "now=%llu, raw_contrib=%llu, scale_contrib=%llu, "
            "last_running_time=%llu, period_contrib=%llu, "
            "last_update=%llu, ld.load_sum=%llu, load_avg=%llu, "
            "raw_delta=%llu, scale_delta=%llu, "
            "delta_contrib=%llu, env.delta=%llu, periods=%llu, "
            "u=%llu, d1=%llu, c1=%llu, c2=%llu, d3=%llu, c3=%llu, "
            "env.load_sum=%llu",
        __entry->comm, __entry->pid,
        __entry->state, __entry->on_cpu, __entry->on_rq,
        __entry->now, __entry->raw_contrib, __entry->scale_contrib,
        __entry->s_last_running_time, __entry->s_period_contrib,
        __entry->s_last_update, __entry->s_load_sum, __entry->s_load_avg,
        __entry->raw_delta, __entry->scale_delta,
        __entry->delta_contrib, __entry->delta, __entry->periods,
        __entry->u, __entry->d1, __entry->c1, __entry->c2, __entry->d3, __entry->c3,
        __entry->load_sum
    )
);

TRACE_EVENT(update_task_load,

    TP_PROTO(int cpath, struct task_struct *tsk,
        struct task_load *sld, u64 now, u64 delta, u64 periods),

    TP_ARGS(cpath, tsk, sld, now, delta, periods),

    TP_STRUCT__entry(
        __field(int, cpath)

        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)
        __field(int, state)
        __field(int, on_cpu)
        __field(int, on_rq)

        __field(u64, now)
        __field(u64, delta)
        __field(u64, periods)

        __field(u64, s_last_running_time)
        __field(u64, s_last_carry)
        __field(u64, s_last_update)
        __field(u64, s_load_sum)
        __field(u64, s_load_avg)

        __field(u64, last_running_time)
        __field(u64, period_contrib)
        __field(u64, last_update)
        __field(u64, load_sum)
        __field(u64, load_avg)

        __field(u64, util)
        ),

    TP_fast_assign(
        struct trans_task_struct *tts = get_trans_task_struct(tsk);
        struct task_load *ld = &tts->ld;

        __entry->cpath = cpath;

        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;
        __entry->state = task_state(tsk);
        __entry->on_cpu = tsk->on_cpu;
        __entry->on_rq = tsk->on_rq;

        __entry->now = now;
        __entry->delta = delta;
        __entry->periods = periods;

        __entry->s_last_running_time = sld->last_running_time;
        __entry->s_last_carry = sld->period_contrib;
        __entry->s_last_update = sld->last_update;
        __entry->s_load_sum = sld->load_sum;
        __entry->s_load_avg = sld->load_avg;

        __entry->last_running_time = ld->last_running_time;
        __entry->period_contrib = ld->period_contrib;
        __entry->last_update = ld->last_update;
        __entry->load_sum = ld->load_sum;
        __entry->load_avg = ld->load_avg;

        __entry->util = tsk->se.avg.util_avg;
        ),

    TP_printk("CALLPATH=%d, "
            "task=%s$%d, state=0x%x, on_cpu=%d, on_rq=%d, "
            "now=%llu, delta=%llu, periods=%llu, "
            "s_last_running_time=%llu, s_last_carry=%llu, "
            "s_last_update=%llu, s_load_sum=%llu, s_load_avg=%llu, "
            "last_running_time=%llu, period_contrib=%llu, "
            "last_update=%llu, load_sum=%llu, load_avg=%llu, util=%llu",
        __entry->cpath,
        __entry->comm, __entry->pid,
        __entry->state, __entry->on_cpu, __entry->on_rq,
        __entry->now, __entry->delta, __entry->periods,
        __entry->s_last_running_time, __entry->s_last_carry,
        __entry->s_last_update, __entry->s_load_sum, __entry->s_load_avg,
        __entry->last_running_time, __entry->period_contrib,
        __entry->last_update, __entry->load_sum, __entry->load_avg, __entry->util
    )
);


TRACE_EVENT(trans_task_load,

    TP_PROTO(struct task_struct *tsk, u64 load_avg),

    TP_ARGS(tsk, load_avg),

    TP_STRUCT__entry(
        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)

        __field(u64, load_avg)

        __field(unsigned long, util)
        __field(unsigned long, util_est)

        __field(u64, ld_load_avg)
        __field(u64, ld_load_est)

        __field(unsigned long, min)
        __field(unsigned long, max)
        ),

    TP_fast_assign(
        struct trans_task_struct *tts = get_trans_task_struct(tsk);
        struct task_load *ld = &tts->ld;

        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;

        __entry->load_avg = load_avg;
        __entry->util = task_util(tsk);
        __entry->util_est = _task_util_est(tsk);
        __entry->ld_load_avg = ld->load_avg;
        __entry->ld_load_est = ld->load_est;
        __entry->min = uclamp_eff_value(tsk, UCLAMP_MIN);
        __entry->max = uclamp_eff_value(tsk, UCLAMP_MAX);
        ),

    TP_printk("task=%s$%d, load_avg=%llu, util=%lu, util_est=%lu, "
                "ld.load_avg=%llu, ld.load_est=%llu, "
                "clamp_min=%lu, clamp_max=%lu",
        __entry->comm, __entry->pid,
        __entry->load_avg, __entry->util, __entry->util_est,
        __entry->ld_load_avg, __entry->ld_load_est,
        __entry->min, __entry->max
    )
);


TRACE_EVENT(update_load_est,

    TP_PROTO(struct task_struct *tsk, struct task_load *sld, struct task_load *ld),

    TP_ARGS(tsk, sld, ld),

    TP_STRUCT__entry(
        __array(char, comm, TASK_COMM_LEN)
        __field(int, pid)

        /* snap */
        __field(u64, load_avg_t)
        __field(u64, load_avg_t_1)
        __field(u64, load_est_t_1)

        __field(long, last_avg_diff)
        __field(long, last_est_diff)

        __field(u64, load_est_t)
        ),

    TP_fast_assign(
        memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
        __entry->pid = tsk->pid;

        __entry->load_avg_t = sld->load_avg;
        __entry->load_avg_t_1 = sld->last_load_avg;
        __entry->load_est_t_1 = sld->load_est;

        __entry->last_avg_diff = sld->load_avg - sld->last_load_avg;
        __entry->last_est_diff = ld->load_avg - sld->load_est;

        __entry->load_est_t = ld->load_est;
        ),

    TP_printk("task=%s$%d, load_avg(t)=%llu, load_avg(t-1)=%llu, load_est(t-1)=%llu, "
        "last_avg_diff=%ld, last_est_diff=%ld, "
        "load_est(t)=%llu",
        __entry->comm, __entry->pid,
        __entry->load_avg_t, __entry->load_avg_t_1, __entry->load_est_t_1,
        __entry->last_avg_diff, __entry->last_est_diff,
        __entry->load_est_t
    )
);


#define MAX_TRACE_NAME_LEN    64
TRACE_EVENT(tracing_mark_write,
    TP_PROTO(const char *name, u64 idx, u64 pos, u64 val),

    TP_ARGS(name, idx, pos, val),

    TP_STRUCT__entry(
        __dynamic_array(char, trace_buf, MAX_TRACE_NAME_LEN)
        __field(u64, idx)
        __field(u64, pos)
        __field(u64, val)

    ),

    TP_fast_assign(
        char *buf = __get_dynamic_array(trace_buf);
        int offset = 0;

        __entry->idx = idx;
        __entry->pos = pos;
        __entry->val = val;

        offset = snprintf(buf, MAX_TRACE_NAME_LEN, "C|%llu|%s", __entry->pos, name);

        if (idx != INVALID_SYSTRACE_IDX)
            offset += snprintf(buf + offset, MAX_TRACE_NAME_LEN - offset, "[%llu]", idx);

        offset += snprintf(buf + offset, MAX_TRACE_NAME_LEN - offset, "|%llu", __entry->val);

    ),

    TP_printk("%s", (char *)__get_dynamic_array(trace_buf))
)

extern long g_systrace_mask;

#define trans_systrace_c(mask, fmt, ...)                                    \
        do {                                                                \
            if (unlikely(g_systrace_mask & (mask)))                         \
                trace_tracing_mark_write(fmt, ##__VA_ARGS__);               \
        } while (0)

#define trace_sched_switch_ux(cpu, ux_state)                                \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trace_tracing_mark_write("ux_state",                            \
                    cpu, TRANS_SYSTRACE_POS, ux_state);                     \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_nr_running(cpu, nr_running)                              \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_NR_RUNNING,                        \
                "nr_running", cpu, TRANS_SYSTRACE_POS, nr_running);         \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_tick_pull_running_task(cpu, pid)                         \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_PULL_RUNNING_TASK,                 \
                "tick_pull_running_task", cpu, TRANS_SYSTRACE_POS, pid);    \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_tick_pull_runnable_task(cpu, pid)                        \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_PULL_RUNNABLE_TASK,                \
                "tick_pull_runnable_task", cpu, TRANS_SYSTRACE_POS, pid);   \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_newidle_pull_runnable_ux(cpu, pid)                       \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_NEWIDLE_PULL_RUNNABLE_UX,          \
                "newidle_pull_runnable_ux", cpu, TRANS_SYSTRACE_POS, pid);  \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_newidle_pull_runnable_rt(cpu, pid)                       \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_NEWIDLE_PULL_RUNNABLE_RT,          \
                "newidle_pull_runnable_rt", cpu, TRANS_SYSTRACE_POS, pid);  \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_wakeup_irqwb(cpu, prev_pid)                              \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_WAKEUP_IRQWB,                      \
                "wakeup_irqwb", cpu, TRANS_SYSTRACE_POS, prev_pid);         \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_pipeline(cpu, pipeline)                                  \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_PIPELINE,                          \
                "pipeline", cpu, TRANS_SYSTRACE_POS, pipeline);             \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_uxtags(cpu, uxtags)                                      \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_UXTAGS,                            \
                "uxtags", cpu, TRANS_SYSTRACE_POS, uxtags);                 \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_loadstates(cpu, states)                                  \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_CPU_LOADSTATES,                    \
                "cpu_loadstates", cpu, TRANS_SYSTRACE_POS, states);         \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_alloc_lock(lock_type, tsk, lock_dpeth, total_depth)           \
        do {                                                                \
            char __trace_buffer[64];                                        \
            int tid = task_pid_nr(tsk);                                     \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_depth_%s", tid, lock_type);                             \
            trans_systrace_c(TRANS_TRACE_LK_ALLOC_LOCK,                     \
                __trace_buffer, tid, TRANS_SYSTRACE_POS, lock_dpeth);       \
                                                                            \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_depth_%s", tid, "total");                               \
            trans_systrace_c(TRANS_TRACE_LK_ALLOC_LOCK,                     \
                __trace_buffer, tid, TRANS_SYSTRACE_POS, total_depth);      \
                                                                            \
            if (USEC_TO_MSEC(total_depth) > 10)                             \
                lock_dump_stack(tsk);                                       \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_show_dstate_duration(lock_type, tsk, duration)                \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%s_dend", lock_type);                                      \
            trans_systrace_c(TRANS_TRACE_LK_DSTATE_DURATION,                \
                    __trace_buffer, task_pid_nr(tsk),                       \
                    TRANS_SYSTRACE_POS, duration);                          \
            if (USEC_TO_MSEC(duration) > 20)                                \
                lock_dump_stack(tsk);                                       \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_lock_protect(type, cpu, tid)                                  \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "lock_protect_%s", type);                                   \
            trans_systrace_c(TRANS_TRACE_LK_LOCK_PROTECT,                   \
                __trace_buffer, cpu, TRANS_SYSTRACE_POS, tid);              \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

extern u64 lock_trigger;
#define trace_lock_trigger(tid)                                             \
        do {                                                                \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            trans_systrace_c(TRANS_TRACE_LK_LOCK_TRIGGER,                   \
                "lock_trigger", -1, TRANS_SYSTRACE_POS, tid);               \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_last_running(tid, time)                                       \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_last_running", tid);                                    \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, time);              \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_running_time(tid, time)                                       \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_running_time", tid);                                    \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, time);              \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_runnable_time(tid, time)                                      \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_runnable_time", tid);                                   \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, time);              \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_contrib(tid, contrib)                                         \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_contrib", tid);                                         \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, contrib);           \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_weight(tid, time)                                             \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_weight", tid);                                          \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, time);              \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_trans_load_sum(tid, load_sum)                                 \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_trans_load_sum", tid);                                  \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, load_sum);          \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_trans_load_avg(tid, load_avg)                                 \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_trans_load_avg", tid);                                  \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, load_avg);          \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_trans_load_est(tid, load_est)                                 \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_trans_load_est", tid);                                  \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, load_est);          \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_util_avg(tid, util_avg)                                       \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_util_avg", tid);                                        \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, util_avg);          \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_util_est(tid, util_est)                                       \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_util_est", tid);                                        \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, util_est);          \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)

#define trace_runnable_avg(tid, runnable_avg)                               \
        do {                                                                \
            char __trace_buffer[64];                                        \
            if (!trace_tracing_mark_write_enabled())                        \
                goto CONCATENATE(__trace_exit_, __LINE__);                  \
            snprintf(__trace_buffer, sizeof(__trace_buffer),                \
                "%d_runnable_avg", tid);                                    \
            trans_systrace_c(TRANS_TRACE_LD_RUNNING_TIME,                   \
                __trace_buffer, -1, TRANS_SYSTRACE_POS, runnable_avg);      \
            CONCATENATE(__trace_exit_, __LINE__): ;                         \
        } while (0)


#endif /*__TRANS_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define TRACE_INCLUDE_PATH ../../../vendor/transsion/kernel_modules/uas/src/
#else
#define TRACE_INCLUDE_PATH .
#endif

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trans_trace

/* This part must be outside protection */
#include <trace/define_trace.h>

