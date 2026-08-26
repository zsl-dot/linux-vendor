#define _GNU_SOURCE
#include "bpf_loader.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef __NR_bpf
#define __NR_bpf 321
#endif

static int bpf_cmd(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size)
{
	return syscall(__NR_bpf, cmd, attr, size);
}

int bpf_load_prog(const void *insns, size_t insn_bytes,
		  enum bpf_prog_type prog_type)
{
	char log_buf[64 * 1024] = {};
	union bpf_attr attr = {
		.prog_type = prog_type,
		.insns = (unsigned long)insns,
		.insn_cnt = (unsigned int)(insn_bytes / 8),
		.license = (unsigned long)"GPL",
		.log_buf = (unsigned long)log_buf,
		.log_size = sizeof(log_buf),
		.log_level = 1,
	};
	int fd = bpf_cmd(BPF_PROG_LOAD, &attr, sizeof(attr));
	if (fd < 0) {
		fprintf(stderr, "BPF verifier error:\n%s\n", log_buf);
		perror("BPF_PROG_LOAD");
	}
	return fd;
}

int bpf_create_kprobe(const char *name, const char *function)
{
	int fd = open("/sys/kernel/debug/tracing/kprobe_events",
		      O_WRONLY | O_APPEND);
	if (fd < 0) {
		/* Mount debugfs if needed */
		mkdir("/sys/kernel/debug", 0755);
		(void)mount("debugfs", "/sys/kernel/debug", "debugfs", 0, "");
		fd = open("/sys/kernel/debug/tracing/kprobe_events",
			  O_WRONLY | O_APPEND);
		if (fd < 0) { perror("open kprobe_events"); return -1; }
	}

	char buf[256];
	int len = snprintf(buf, sizeof(buf), "p:%s %s\n", name, function);
	(void)write(fd, buf, (size_t)len);
	close(fd);

	char id_path[256];
	snprintf(id_path, sizeof(id_path),
		 "/sys/kernel/debug/tracing/events/kprobes/%s/id", name);

	for (int i = 0; i < 10; i++) {
		fd = open(id_path, O_RDONLY);
		if (fd >= 0) break;
		usleep(10000);
	}
	if (fd < 0) { perror("open kprobe id"); return -1; }

	memset(buf, 0, sizeof(buf));
	(void)read(fd, buf, sizeof(buf) - 1);
	close(fd);
	return atoi(buf);
}

void bpf_remove_kprobe(const char *name)
{
	int fd = open("/sys/kernel/debug/tracing/kprobe_events",
		      O_WRONLY | O_APPEND);
	if (fd < 0) return;
	char buf[256];
	int len = snprintf(buf, sizeof(buf), "-:%s\n", name);
	(void)write(fd, buf, (size_t)len);
	close(fd);
}

int bpf_attach_perf(int tracepoint_id, int prog_fd)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_TRACEPOINT,
		.size = sizeof(attr),
		.config = (unsigned)tracepoint_id,
		.sample_period = 1,
		.wakeup_events = 1,
		.disabled = 1,
	};
	int fd = syscall(__NR_perf_event_open, &attr,
			 -1, 0, -1, PERF_FLAG_FD_CLOEXEC);
	if (fd < 0) { perror("perf_event_open"); return -1; }
	if (ioctl(fd, PERF_EVENT_IOC_SET_BPF, prog_fd) < 0) {
		perror("PERF_EVENT_IOC_SET_BPF"); close(fd); return -1;
	}
	if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		perror("PERF_EVENT_IOC_ENABLE"); close(fd); return -1;
	}
	return fd;
}


static volatile int g_running = 1;
static void sig_handler(int s) { (void)s; g_running = 0; }

void bpf_read_trace_pipe(void)
{
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	int fd = open("/sys/kernel/debug/tracing/trace_pipe", O_RDONLY);
	if (fd < 0) { perror("open trace_pipe"); return; }

	int epfd = epoll_create1(0);
	if (epfd < 0) { perror("epoll_create1"); close(fd); return; }

	struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
	epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

	printf("[trace] Ctrl-C to stop\n");

	char buf[4096];
	while (g_running) {
		struct epoll_event events[1];
		int nfds = epoll_wait(epfd, events, 1, 100);
		if (nfds < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (nfds == 0) continue;

		ssize_t n = read(fd, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			fwrite(buf, 1, (size_t)n, stdout);
			fflush(stdout);
		} else if (n < 0 && errno != EINTR) {
			break;
		}
	}
	close(epfd);
	close(fd);
}
