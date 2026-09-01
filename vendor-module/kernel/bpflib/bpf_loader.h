/*
 * bpf_loader.h — Shared eBPF program loader & kprobe helpers
 */
#ifndef BPF_LOADER_H
#define BPF_LOADER_H

#include <linux/bpf.h>
#include <stddef.h>

/* Load raw BPF instructions. Returns prog_fd or -1 on error. */
int bpf_load_prog(const void *insns, size_t insn_bytes,
		  enum bpf_prog_type prog_type);

/* Create a kprobe, return its tracepoint event ID or -1 on error. */
int bpf_create_kprobe(const char *name, const char *function);

/* Remove a previously created kprobe. */
void bpf_remove_kprobe(const char *name);

/* Open perf event for a tracepoint, attach BPF prog, enable. */
int bpf_attach_perf(int tracepoint_id, int prog_fd);

/* Block reading trace_pipe, print to stdout. Signal-safe exit. */
void bpf_read_trace_pipe(void);

#endif
