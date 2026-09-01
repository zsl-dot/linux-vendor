/*
 * kprobe_exec.bpf.c — BPF program tracing __x64_sys_execve
 *
 * Uses bpf_trace_printk() (helper #1) to log every execve call.
 * Self-contained — no external BPF headers needed.
 */
#ifndef __section
#define __section(S) __attribute__((section(S), used))
#endif

/* bpf_trace_printk(fmt, fmt_size, ...) — helper #1 */
static long (*bpf_trace_printk)(const char *fmt, unsigned int fmt_size,
				...) = (void *)6;

__section("kprobe/__x64_sys_execve")
int trace_execve(void *ctx)
{
	const char msg[] = "eBPF: execve";
	bpf_trace_printk(msg, sizeof(msg));
	return 0;
}

__section("license")
char _license[] = "GPL";
