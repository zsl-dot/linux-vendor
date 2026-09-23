#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "epoll_demo"
#define EVENT_MAX 128

static DECLARE_WAIT_QUEUE_HEAD(event_wait);
static DEFINE_MUTEX(event_lock);
static char event_buf[EVENT_MAX];
static size_t event_len;

static ssize_t epoll_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	ssize_t ret;

	if (mutex_lock_interruptible(&event_lock))
		return -ERESTARTSYS;
	if (!event_len) {
		mutex_unlock(&event_lock);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(event_wait, READ_ONCE(event_len)))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&event_lock))
			return -ERESTARTSYS;
	}
	if (len < event_len) {
		ret = -EINVAL;
		goto out;
	}
	if (copy_to_user(buf, event_buf, event_len)) {
		ret = -EFAULT;
		goto out;
	}
	ret = event_len;
	event_len = 0;
out:
	mutex_unlock(&event_lock);
	return ret;
}

static ssize_t epoll_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	size_t n = min(len, (size_t)EVENT_MAX - 1);

	if (mutex_lock_interruptible(&event_lock))
		return -ERESTARTSYS;
	if (copy_from_user(event_buf, buf, n)) {
		mutex_unlock(&event_lock);
		return -EFAULT;
	}
	event_buf[n] = '\0';
	event_len = n;
	mutex_unlock(&event_lock);
	wake_up_interruptible(&event_wait);
	pr_info("epoll_demo: event queued (%zu bytes)\n", n);
	return len;
}

static __poll_t epoll_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(file, &event_wait, wait);
	if (READ_ONCE(event_len))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations epoll_fops = {
	.owner = THIS_MODULE,
	.read = epoll_read,
	.write = epoll_write,
	.poll = epoll_poll,
};

static struct miscdevice epoll_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DEVICE_NAME,
	.fops = &epoll_fops,
	.mode = 0666,
};

static int __init epoll_init(void)
{
	int ret = misc_register(&epoll_misc);
	if (!ret)
		pr_info("epoll_demo: registered /dev/%s\n", DEVICE_NAME);
	return ret;
}

static void __exit epoll_exit(void)
{
	misc_deregister(&epoll_misc);
	pr_info("epoll_demo: unloaded\n");
}

module_init(epoll_init);
module_exit(epoll_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("poll/epoll wait queue demonstration");
