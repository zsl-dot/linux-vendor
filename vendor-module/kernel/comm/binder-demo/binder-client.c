/*
 * binder-client.c — Binder 客户端 (one-way, 读取 ack 确保事务刷新)
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/android/binder.h>

#define BINDER_MMAP_SIZE (1024 * 1024)

int main(int argc, char **argv)
{
	const char *device = (argc > 1) ? argv[1] : "/dev/binder";
	const char *msg = "Hello from client!";

	int fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); return 1; }

	if (mmap(NULL, BINDER_MMAP_SIZE, PROT_READ,
		 MAP_PRIVATE, fd, 0) == MAP_FAILED) {
		perror("mmap"); return 1;
	}
	printf("[client] fd=%d\n", fd);

	struct {
		uint32_t cmd;
		struct binder_transaction_data txn;
	} __attribute__((packed)) send_buf = {
		.cmd = BC_TRANSACTION,
		.txn = {
			.target = { .handle = 0 },
			.code = 1,
			.flags = TF_ONE_WAY,
			.data_size = strlen(msg) + 1,
			.data.ptr.buffer = (uintptr_t)msg,
		},
	};

	uint8_t read_buf[256];
	struct binder_write_read bwr = {
		.write_size = sizeof(send_buf),
		.write_buffer = (uintptr_t)&send_buf,
		.read_size = sizeof(read_buf),
		.read_buffer = (uintptr_t)read_buf,
	};

	printf("[client] Sending: \"%s\"\n", msg);
	if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
		perror("ioctl"); close(fd); return 1;
	}

	/* 读取 BR_TRANSACTION_COMPLETE */
	uint8_t *ptr = read_buf;
	while (ptr < read_buf + bwr.read_consumed) {
		uint32_t rc = *(uint32_t *)ptr;
		if (rc == BR_TRANSACTION_COMPLETE)
			printf("[client] Got ack: BR_TRANSACTION_COMPLETE\n");
		else if (rc == BR_FAILED_REPLY)
			printf("[client] Failed!\n");
		ptr += sizeof(uint32_t);
		if (rc > 0x80000000) /* 有数据的命令，跳过 data */
			ptr += sizeof(struct binder_transaction_data);
	}

	printf("[client] Done\n");
	close(fd);
	return 0;
}
