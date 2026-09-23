/*
 * sched_switch_tp.bpf.c — BPF program on the sched:sched_switch tracepoint
 *
 * 与 kprobe demo 的区别：静态 tracepoint 由内核源码中的
 * trace_sched_switch() 钩子点产生（include/trace/events/sched.h），
 * 不需要在 kprobe_events 里动态注册探针；挂载走同一个 perf_event 通道，
 * 但事件 ID 直接读 events/sched/sched_switch/id。
 * 自包含，无需外部 BPF 头文件。
 */
#ifndef __section
#define __section(S) __attribute__((section(S), used))
#endif

/* bpf_trace_printk(fmt, fmt_size, ...) — helper id 6 */
static long (*bpf_trace_printk)(const char *fmt, unsigned int fmt_size,
				...) = (void *)6;

__section("tracepoint/sched_switch")
int on_sched_switch(void *ctx)
{
	const char msg[] = "eBPF: sched_switch";
	bpf_trace_printk(msg, sizeof(msg));
	return 0;
}

__section("license")
char _license[] = "GPL";
