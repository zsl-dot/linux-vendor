# 用户空间与内核空间的通信方式

用户程序不能直接访问内核地址空间，也不能直接调用内核函数。两者必须通过内核提供、会检查权限和参数的接口交换数据或事件。

## 总览

| 方式 | 主要场景 | 用户空间入口 | 内核侧典型接口 |
|---|---|---|---|
| 系统调用 | 最基础的内核服务 | `open()`、`read()`、`write()`、`mmap()` | `SYSCALL_DEFINE*`、VFS、驱动回调 |
| 字符设备 | 与驱动交换数据和控制命令 | 操作 `/dev/<name>` | `file_operations` |
| `procfs` | 调试信息、运行状态 | `/proc/<name>` | `proc_create()`、`proc_ops` |
| `sysfs` | 设备属性、简单配置 | `/sys/...` | `device_attribute`、`sysfs_emit()` |
| `ioctl` | 复杂控制操作 | `ioctl(fd, cmd, arg)` | `.unlocked_ioctl` |
| `mmap` | 高性能共享缓冲区 | `mmap(fd, ...)` | `.mmap`、页映射接口 |
| `poll` / `epoll` | 等待设备或内核事件 | `poll()`、`epoll_wait()` | `.poll`、wait queue |
| Netlink | 内核与守护进程的结构化双向消息 | `AF_NETLINK` socket | Netlink / Generic Netlink |
| 信号 / `eventfd` | 异步、轻量通知 | `sigaction()`、`eventfd()` | `send_sig()`、`eventfd_signal()` |
| BPF ring buffer / perf buffer | 内核事件的高效上报 | `bpf()`、libbpf | BPF map、ring buffer、tracepoint |
| Binder | Android IPC | `/dev/binder` | Binder 驱动的 ioctl、mmap |

## 1. 系统调用：所有受控交互的基础

最常见的路径是用户程序发起系统调用，CPU 从用户态进入内核态；内核完成工作后再返回用户态。

```text
用户程序
  │ read(fd, buffer, length)
  ▼
系统调用入口
  ▼
VFS 或具体驱动
  ▼
内核数据、文件系统或硬件
  ▼
copy_to_user()
  ▼
用户缓冲区
```

例如：

```c
char buffer[128];
int fd = open("/tmp/example", O_RDONLY);
ssize_t count = read(fd, buffer, sizeof(buffer));
```

内核不会信任用户传来的指针。驱动向用户返回数据时通常使用 `copy_to_user()`；从用户读取输入时通常使用 `copy_from_user()`。这两个接口会处理用户地址可访问性，并在失败时返回未完成复制的字节数。

## 2. 字符设备：自定义驱动的通用接口

字符设备把内核模块或驱动暴露为 `/dev/<device>` 文件。用户态通过文件描述符操作它，内核模块实现对应回调。

```text
用户程序                         内核模块
----------                      -------------------------
open("/dev/mydemo")        →   .open
read(fd, ...)               →   .read
write(fd, ...)              →   .write
ioctl(fd, command, arg)     →   .unlocked_ioctl
poll(fd, ...)               →   .poll
mmap(fd, ...)               →   .mmap
```

典型骨架：

```c
static const struct file_operations my_fops = {
    .owner          = THIS_MODULE,
    .open           = my_open,
    .read           = my_read,
    .write          = my_write,
    .unlocked_ioctl = my_ioctl,
    .poll           = my_poll,
    .mmap           = my_mmap,
};
```

适合：自定义数据通道、驱动控制、阻塞读写、等待队列练习，以及用户空间工具与模块协作。

## 3. procfs 与 sysfs：把内核状态表示为文件

### procfs

`procfs` 通常用于内核状态、统计信息和调试接口：

```text
/proc/meminfo
/proc/modules
/proc/<my_demo>
```

用户可以像读普通文本文件一样访问：

```bash
cat /proc/my_demo
echo 1 | sudo tee /proc/my_demo
```

模块可通过 `proc_create()` 创建条目，并实现 `proc_ops`。它很适合学习模块、输出调试统计；但如果接口属于设备模型的稳定属性，通常更应选择 sysfs。

### sysfs

`sysfs` 面向设备模型与属性：

```text
/sys/class/
/sys/bus/
/sys/devices/
/sys/module/
```

每个文件应表达一个简单、短小、文本化的属性，例如模块参数或设备开关：

```bash
cat /sys/module/<module>/parameters/debug
```

经验法则：

- 需要调试、统计、过程信息：优先考虑 procfs。
- 设备状态、驱动属性或简单配置：优先考虑 sysfs。
- 大块数据、复杂协议：不要滥用 procfs/sysfs，应使用设备、Netlink 或 mmap。

## 4. ioctl：在一个 fd 上表达复杂控制操作

`read()` 和 `write()` 擅长传输字节流；当操作包含命令编号、方向、结构体参数和版本演进时，驱动经常使用 `ioctl()`：

```c
struct my_config config = { .enable = 1 };
ioctl(fd, MY_DEVICE_SET_CONFIG, &config);
```

内核实现 `.unlocked_ioctl`，仍需使用 `copy_from_user()` / `copy_to_user()` 安全地处理参数。设计 ioctl 时应明确命令含义、数据大小和兼容性，避免直接把内核指针暴露给用户空间。

## 5. poll / epoll 与等待队列：等待内核事件

若用户调用 `read()` 时没有数据，驱动可以阻塞该调用；也可以实现 `.poll`，使应用使用 `poll()` 或 `epoll()` 同时等待多个 fd。

```text
用户态                                  内核
poll/epoll_wait(fd)  ───────────────→   .poll 注册 wait queue
                                          数据到达
poll/epoll_wait()    ←───────────────   wake_up_interruptible()
read(fd, ...)        ───────────────→   取出数据
```

这是字符设备与异步事件模型的关键组合。驱动的数据状态变化时唤醒等待队列，用户态就不会忙循环查询。

## 6. mmap：减少复制的共享数据路径

`mmap()` 可以把驱动管理的页或缓冲区映射到用户进程地址空间。用户与内核随后通过同一组物理页读取或写入数据，避免每次事件经过 `read()` 与复制。

```text
用户态                             驱动/内核
mmap(fd)       ───────────────→   .mmap 建立映射
读写映射内存                        填充或消费环形缓冲区
poll/epoll      ←───────────────   数据就绪通知
```

常见于网卡、显卡、DMA、perf、BPF ring buffer 和 Binder。共享内存不等于没有同步：仍需要明确所有权、读写位置、内存屏障和生命周期。

## 7. Netlink：结构化、双向、可异步的消息通道

Netlink 是内核和用户态守护进程之间的 socket 通信机制。它适合配置请求，也允许内核主动发送事件。

```text
用户态守护进程                       内核子系统
bind(AF_NETLINK)          ←──────  multicast / unicast 事件
recvmsg()                 ←──────  Netlink 消息
sendmsg()                 ──────→  配置或查询请求
```

网络工具是典型例子：`ip link`、`ip addr`、`ip route` 通常通过 `NETLINK_ROUTE` 与网络栈通信。新的自定义协议通常应优先考虑 Generic Netlink，因为它有族、命令、属性和版本化机制。

## 8. 信号、eventfd 与 futex：通知和同步

- 信号适合较简单的异步通知，例如进程终止、定时器、异步 I/O 事件。
- `eventfd` 是可通过 fd 读取和写入的 64 位计数器，适合和 `poll` / `epoll` 结合，也常用于用户态与内核/虚拟化组件之间的事件通知。
- futex 让常规无竞争同步留在用户态；只有竞争、阻塞或唤醒时才进入内核，因此是 pthread mutex/condition 等实现的重要基础。

## 9. BPF、perf 与 tracing：内核向观测程序输出事件

eBPF 程序通过 `bpf()` 系统调用加载，用户态通常使用 libbpf 管理 map、程序和事件缓冲区。内核事件可写入 perf buffer 或 BPF ring buffer，用户态批量读取。

```text
tracepoint / kprobe / syscall
            │
            ▼
        BPF 程序
            │
            ▼
       ring buffer
            │
            ▼
       用户态 loader
```

这尤其适合追踪和观测；本项目的 `vendor-module/kernel/ebpf-demo1` 与 `ebpf-demo2` 是对应实践。

## 10. Binder：字符设备、ioctl 与 mmap 的组合

Android Binder 是一个有代表性的综合案例：

```text
用户进程 A ─ ioctl → Binder 驱动 ─ ioctl → 用户进程 B
                   │
                   └─ mmap 建立高效缓冲区
```

应用通过 `/dev/binder` 与 Binder 驱动交互；命令主要经 ioctl 提交，传输缓冲区通过 mmap 高效共享。项目中的 `vendor-module/kernel/binder-demo` 可用于理解这个组合。

## 选择建议

| 需求 | 建议方式 |
|---|---|
| 学习模块、导出简单调试信息 | procfs |
| 表示设备属性或简单参数 | sysfs |
| 自定义数据读写或控制设备 | 字符设备 + `read` / `write` / `ioctl` |
| 等待数据或状态变化 | `.poll` + wait queue + `poll` / `epoll` |
| 内核主动通知用户态守护进程 | Generic Netlink |
| 高频或大块数据 | `mmap` + 环形缓冲区 / DMA + 事件通知 |
| 内核事件追踪 | eBPF ring buffer 或 perf buffer |
| Android IPC | Binder |

## 核心原则

1. 用户指针必须通过 `copy_from_user()`、`copy_to_user()` 或受控映射处理。
2. 选择稳定、符合语义的接口，而不是为了方便把所有内容塞进 procfs 或 ioctl。
3. 高频路径优先减少复制与系统调用次数，但同时必须设计好并发和生命周期。
4. 所有用户输入都是不可信的：检查长度、范围、权限、对象状态和兼容性。
