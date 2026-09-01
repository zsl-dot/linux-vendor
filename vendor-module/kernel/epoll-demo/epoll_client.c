#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

int main(void)
{
	struct epoll_event ev = {.events = EPOLLIN};
	char buf[128];
	int fd, ep, n;

	fd = open("/dev/epoll_demo", O_RDWR | O_NONBLOCK);
	if (fd < 0) { perror("open"); return 1; }
	ep = epoll_create1(0);
	if (ep < 0) { perror("epoll_create1"); return 1; }
	ev.data.fd = fd;
	if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) { perror("epoll_ctl"); return 1; }
	if (write(fd, "hello from epoll\n", 17) < 0) { perror("write"); return 1; }
	n = epoll_wait(ep, &ev, 1, 1000);
	if (n != 1) { fprintf(stderr, "epoll_wait returned %d\n", n); return 1; }
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0) { perror("read"); return 1; }
	buf[n] = '\0';
	printf("epoll event: EPOLLIN, data: %s", buf);
	close(ep);
	close(fd);
	return 0;
}
