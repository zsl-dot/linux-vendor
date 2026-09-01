# Netlink：用户进程与内核模块双向通信示例

本项目的 `vendor-module/kernel/netlink-demo/` 演示用户进程与内核模块如何通过 Netlink 进行“请求—响应”通信。

```text
netlink-client                 netlink_demo.ko
      │ sendmsg()                    │ .input callback
      ├──── NETLINK_USERSOCK ───────►│ 接收 "hello netlink"
      │                              │ nlmsg_unicast()
      │◄──── NETLINK_USERSOCK ───────┤ 返回 "kernel reply: ..."
      │ recv()
```

## 关键接口

内核模块通过 `netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg)` 创建一个内核 Netlink socket，并将 `demo_receive()` 作为消息到达回调。回调从 `NETLINK_CB(skb).portid` 取得发送进程的端口 ID，再调用 `nlmsg_unicast()` 将响应精确发回该进程。

用户程序创建 `socket(AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK)`，以 `getpid()` 作为本地 `nl_pid` 并 `bind()`；目标地址的 `nl_pid = 0` 代表内核。它用 `sendmsg()` 发送 `nlmsghdr`，再通过 `recv()` 接收模块的响应。

## 运行

```bash
cd vendor-module/kernel/netlink-demo
./run.sh build
```

或运行所有验证：

```bash
./go.sh demo
```

成功输出包含：

```text
userspace received: kernel reply: hello netlink
netlink_demo: request from port ...: hello netlink
```

`NETLINK_USERSOCK` 仅适合这个隔离的学习示例。实际产品接口通常应使用 Generic Netlink：定义 family、command 和 attribute，获得更清晰的协议、扩展性和权限控制。
