/*
 * binder-server.c — Binder 服务端 (Context Manager)
 */
#include <errno.h>
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

	int fd = open(device, O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); return 1; }

	void *map = mmap(NULL, BINDER_MMAP_SIZE, PROT_READ,
			 MAP_PRIVATE, fd, 0);
	if (map == MAP_FAILED) { perror("mmap"); return 1; }
	printf("[server] fd=%d, mmap=%p\n", fd, map);

	int ret = ioctl(fd, BINDER_SET_CONTEXT_MGR, 0);
	if (ret < 0) { perror("set_context_mgr"); return 1; }
	printf("[server] Registered as Context Manager\n");

	/* BC_ENTER_LOOPER */
	uint32_t looper_cmd = BC_ENTER_LOOPER;
	struct binder_write_read looper_bwr = {
		.write_size = sizeof(looper_cmd),
		.write_buffer = (uintptr_t)&looper_cmd,
	};
	ret = ioctl(fd, BINDER_WRITE_READ, &looper_bwr);
	printf("[server] BC_ENTER_LOOPER returned %d\n", ret);

	uint8_t read_buf[1024];
	struct binder_write_read bwr = {
		.read_size = sizeof(read_buf),
		.read_buffer = (uintptr_t)read_buf,
	};

	printf("[server] Entering read loop...\n");

	int count = 0;
	while (count < 3) {
		memset(read_buf, 0, sizeof(read_buf));
		bwr.read_consumed = 0;

		int ret = ioctl(fd, BINDER_WRITE_READ, &bwr);
		if (ret < 0) {
			printf("[server] ioctl error: %d (%s)\n", errno, strerror(errno));
			break;
		}
		if (bwr.read_consumed == 0) {
			printf("[server] read_consumed=0, retry...\n");
			continue;
		}

		printf("[server] read_consumed=%zu\n", (size_t)bwr.read_consumed);

		uint8_t *ptr = read_buf;
		while (ptr < read_buf + bwr.read_consumed) {
			uint32_t rc = *(uint32_t *)ptr;
			ptr += sizeof(uint32_t);

			if (rc == BR_TRANSACTION) {
				struct binder_transaction_data *txn =
					(struct binder_transaction_data *)ptr;
				printf("[server] #%d Got: \"%s\" from PID %d\n",
				       ++count,
				       (char *)(uintptr_t)txn->data.ptr.buffer,
				       txn->sender_pid);
				ptr += sizeof(*txn);

				/* BC_FREE_BUFFER */
				struct {
					uint32_t cmd;
					uint64_t ptr;
				} __attribute__((packed)) fb = {
					.cmd = BC_FREE_BUFFER,
					.ptr = txn->data.ptr.buffer,
				};
				struct binder_write_read fw = {
					.write_size = sizeof(fb),
					.write_buffer = (uintptr_t)&fb,
				};
				ioctl(fd, BINDER_WRITE_READ, &fw);
			} else if (rc == BR_SPAWN_LOOPER) {
				printf("[server] BR_SPAWN_LOOPER\n");
			} else if (rc == BR_NOOP) {
				printf("[server] BR_NOOP\n");
			} else if (rc == BR_TRANSACTION_COMPLETE) {
				printf("[server] BR_TRANSACTION_COMPLETE\n");
			} else {
				printf("[server] cmd=0x%x\n", rc);
				ptr += sizeof(struct binder_transaction_data);
			}
		}
	}

	printf("[server] Done, got %d transactions\n", count);
	close(fd);
	return 0;
}
