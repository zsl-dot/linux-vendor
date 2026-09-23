#ifndef __section
#define __section(S) __attribute__((section(S), used))
#endif

static long (*bpf_trace_printk)(const char *fmt, unsigned int fmt_size,
				...) = (void *)6;

__section("kprobe/__x64_sys_clone")
int trace_clone(void *ctx)
{
	const char msg[] = "eBPF: clone() called";
	bpf_trace_printk(msg, sizeof(msg));
	return 0;
}

__section("license")
char _license[] = "GPL";
