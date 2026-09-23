/*
 * loader.c — 静态 tracepoint 版加载器（与 ebpf-demo1 的 kprobe 加载器对照）
 *
 * 差异只在第 3 步：kprobe 需要向 kprobe_events 动态注册探针拿事件 ID；
 * 静态 tracepoint 的事件 ID 直接读
 * /sys/kernel/debug/tracing/events/<category>/<event>/id。
 */
#define _GNU_SOURCE
#include "../bpflib/bpf_loader.h"
#include "../bpflib/elf_extract.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int tracepoint_id(const char *category, const char *event)
{
	char id_path[256];
	int fd;

	snprintf(id_path, sizeof(id_path),
		 "/sys/kernel/debug/tracing/events/%s/%s/id", category, event);
	fd = open(id_path, O_RDONLY);
	if (fd < 0) {
		/* debugfs 未挂载则先挂载（与 bpflib 的 kprobe 路径一致） */
		mkdir("/sys/kernel/debug", 0755);
		if (mount("debugfs", "/sys/kernel/debug", "debugfs", 0, "") < 0)
			perror("mount debugfs");
		fd = open(id_path, O_RDONLY);
	}
	if (fd < 0) {
		perror("open tracepoint id");
		return -1;
	}

	char buf[16];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	return atoi(buf);
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr,
			"Usage: %s <bpf_elf> <section> <category> <event>\n",
			argv[0]);
		return 1;
	}

	/* 1. 从 ELF 提取 BPF 指令 */
	size_t insn_bytes;
	void *insns = elf_extract_section(argv[1], argv[2], &insn_bytes);
	if (!insns)
		return 1;
	printf("[loader] Section '%s': %zu bytes (%u insns)\n",
	       argv[2], insn_bytes, (unsigned)(insn_bytes / 8));

	/* 2. 加载 BPF 程序（tracepoint 类型） */
	int prog_fd = bpf_load_prog(insns, insn_bytes, BPF_PROG_TYPE_TRACEPOINT);
	free(insns);
	if (prog_fd < 0)
		return 1;
	printf("[loader] BPF program loaded, fd=%d\n", prog_fd);

	/* 3. 读取静态 tracepoint 事件 ID */
	int tp_id = tracepoint_id(argv[3], argv[4]);
	if (tp_id < 0)
		return 1;
	printf("[loader] Tracepoint %s:%s, id=%d\n", argv[3], argv[4], tp_id);

	/* 4. 经 perf_event 挂载 */
	int perf_fd = bpf_attach_perf(tp_id, prog_fd);
	if (perf_fd < 0)
		return 1;
	printf("[loader] Attached and enabled\n");

	/* 5. 读 trace_pipe（Ctrl-C 退出） */
	bpf_read_trace_pipe();

	close(perf_fd);
	close(prog_fd);
	return 0;
}
