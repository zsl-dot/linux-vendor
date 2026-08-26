#define _GNU_SOURCE
#include "../bpflib/bpf_loader.h"
#include "../bpflib/elf_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <bpf_elf> [section] [kprobe_func]\n",
			argv[0]);
		return 1;
	}

	const char *section = argc > 2 ? argv[2] : "kprobe/__x64_sys_clone";
	const char *kprobe_func = argc > 3 ? argv[3] : "__x64_sys_clone";

	/* 1. Extract BPF insns from ELF section */
	size_t insn_bytes;
	void *insns = elf_extract_section(argv[1], section, &insn_bytes);
	if (!insns) return 1;
	printf("[loader] Section '%s': %zu bytes (%u insns)\n",
	       section, insn_bytes, (unsigned)(insn_bytes / 8));

	/* 2. Load BPF program */
	int prog_fd = bpf_load_prog(insns, insn_bytes, BPF_PROG_TYPE_KPROBE);
	free(insns);
	if (prog_fd < 0) return 1;
	printf("[loader] BPF program loaded, fd=%d\n", prog_fd);

	/* 3. Create kprobe (bpflib mounts debugfs if needed) */
	int event_id = bpf_create_kprobe("ebpf_probe", kprobe_func);
	if (event_id < 0) return 1;
	printf("[loader] Kprobe: %s, event_id=%d\n", kprobe_func, event_id);

	/* 4. Attach via perf_event */
	int perf_fd = bpf_attach_perf(event_id, prog_fd);
	if (perf_fd < 0) return 1;
	printf("[loader] Attached and enabled\n");

	/* 5. Read trace_pipe (blocking, Ctrl-C to stop) */
	bpf_read_trace_pipe();

	bpf_remove_kprobe("ebpf_probe");
	close(perf_fd);
	close(prog_fd);
	return 0;
}
