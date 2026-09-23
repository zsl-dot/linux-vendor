// SPDX-License-Identifier: GPL-2.0
/* Netlink kernel endpoint: receive a request and unicast a reply. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <net/netlink.h>

#define NETLINK_DEMO_PROTOCOL NETLINK_USERSOCK

static struct sock *demo_socket;

static void demo_send_reply(u32 portid, const char *request)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char reply[128];
	int length;

	length = scnprintf(reply, sizeof(reply), "kernel reply: %s", request);
	skb = nlmsg_new(length + 1, GFP_KERNEL);
	if (!skb)
		return;

	nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, length + 1, 0);
	if (!nlh) {
		kfree_skb(skb);
		return;
	}

	memcpy(nlmsg_data(nlh), reply, length + 1);
	if (nlmsg_unicast(demo_socket, skb, portid) < 0)
		pr_info("netlink_demo: reply to port %u failed\n", portid);
}

static void demo_receive(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	char request[96];
	u32 portid;

	if (skb->len < nlmsg_total_size(0))
		return;

	nlh = nlmsg_hdr(skb);
	if (!nlmsg_ok(nlh, skb->len))
		return;

	portid = NETLINK_CB(skb).portid;
	strscpy(request, nlmsg_data(nlh), sizeof(request));
	pr_info("netlink_demo: request from port %u: %s\n", portid, request);
	demo_send_reply(portid, request);
}

static int __init netlink_demo_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = demo_receive,
	};

	demo_socket = netlink_kernel_create(&init_net, NETLINK_DEMO_PROTOCOL,
					    &cfg);
	if (!demo_socket)
		return -ENOMEM;

	pr_info("netlink_demo: ready (protocol %d)\n", NETLINK_DEMO_PROTOCOL);
	return 0;
}

static void __exit netlink_demo_exit(void)
{
	netlink_kernel_release(demo_socket);
	pr_info("netlink_demo: stopped\n");
}

module_init(netlink_demo_init);
module_exit(netlink_demo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Userspace-to-kernel Netlink request/reply demo");
