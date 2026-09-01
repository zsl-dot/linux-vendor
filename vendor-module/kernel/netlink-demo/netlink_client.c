// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NETLINK_DEMO_PROTOCOL NETLINK_USERSOCK

int main(void)
{
	char buffer[NLMSG_SPACE(128)] = {};
	struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
	struct sockaddr_nl local = {
		.nl_family = AF_NETLINK,
		.nl_pid = getpid(),
	};
	struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;
	struct iovec iov = { .iov_base = nlh, .iov_len = sizeof(buffer) };
	struct msghdr message = { .msg_name = &kernel, .msg_namelen = sizeof(kernel),
		.msg_iov = &iov, .msg_iovlen = 1 };
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_DEMO_PROTOCOL);
	if (fd < 0) {
		perror("socket");
		return EXIT_FAILURE;
	}
	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("bind");
		return EXIT_FAILURE;
	}

	nlh->nlmsg_len = NLMSG_LENGTH(strlen("hello netlink") + 1);
	nlh->nlmsg_pid = local.nl_pid;
	strcpy(NLMSG_DATA(nlh), "hello netlink");
	if (sendmsg(fd, &message, 0) < 0) {
		perror("sendmsg (is netlink_demo.ko loaded?)");
		return EXIT_FAILURE;
	}

	memset(buffer, 0, sizeof(buffer));
	if (recv(fd, nlh, sizeof(buffer), 0) < 0) {
		perror("recv");
		return EXIT_FAILURE;
	}
	printf("userspace received: %s\n", (char *)NLMSG_DATA(nlh));
	close(fd);
	return EXIT_SUCCESS;
}
