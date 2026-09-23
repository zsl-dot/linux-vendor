# Docker 与 WSL 的实现原理

目标：把"容器"和"WSL"这两件常被混为一谈的东西，还原到内核机制层面解释清楚。
撰写时间：2026-09。

## 0. 一句话区别

- **Docker**：没有虚拟硬件。容器是宿主机上的一组进程，靠内核机制把"视图"隔离。
- **WSL2**：跑的是**真 Linux 内核**，放在 Hyper-V 轻量虚拟机里。
- **WSL1**：第三类，既不虚拟化也不容器化，而是做**系统调用翻译**。

---

## 1. Docker：namespace 隔离 + cgroups 限制 + overlayfs

### 1.1 容器是什么

容器 = `clone()` 出来的进程 + 一堆内核提供的隔离开关。`runc` 启动容器时做的事：
设置各 namespace → 加入 cgroup → 准备 rootfs 并 `pivot_root` → 裁剪 capabilities
→ 加载 seccomp 过滤器 → `execve()` 容器里的 init 进程。

### 1.2 命名空间：隔离"看得见什么"

| namespace | 隔离内容 | 效果 |
|---|---|---|
| `uts` | 主机名 | 容器内 `hostname` 独立 |
| `pid` | 进程号 | 容器内 PID 1 是自己的 init |
| `mount` | 挂载点 | 容器看不到宿主机挂载 |
| `net` | 网络栈 | 独立网卡、路由、iptables |
| `ipc` | System V / POSIX IPC | 独立消息队列、共享内存 |
| `user` | UID/GID 映射 | 容器内 root ≠ 宿主机 root（rootless 的基础） |
| `cgroup` | cgroup 视图 | 容器内看到的 cgroup 根是自己的 |

三个系统调用对应三种操作：

- `clone(CLONE_NEW*)`：创建时就带新 namespace
- `unshare()`：给当前进程"拆"出新 namespace
- `setns()`：加入已存在的 namespace（`docker exec` 走这条）

### 1.3 cgroups：限制"能用多少"

v2 统一层级下挂 `cpu`、`memory`、`io`、`pids` 控制器。超内存时由内核
OOM killer 杀掉**容器内**的进程，而不是整个宿主机。

### 1.4 文件系统：overlayfs

```
lowerdir = 镜像各层（只读，多个容器共享）
upperdir = 容器可写层
workdir  = overlayfs 内部工作目录
merged   = 容器看到的根
```

镜像分层共享，写时复制（copy-up）到 upperdir；最后用 `pivot_root` 切换根。

### 1.5 安全边界

| 机制 | 作用 |
|---|---|
| capabilities 裁剪 | 默认去掉 `CAP_SYS_ADMIN` 等 |
| seccomp-BPF | 默认 profile 屏蔽约 300 个系统调用（BPF 的一个典型用途） |
| AppArmor / SELinux | 强制访问控制 |
| user namespace + 映射 | rootless 容器，容器内 root 在宿主机是普通用户 |

### 1.6 网络

每容器一对 `veth`：一端在容器 netns 内，一端挂到 `docker0` 网桥；出网走
iptables/nftables 的 MASQUERADE；端口映射走 DNAT。

### 1.7 组件层次

```
dockerd      守护进程 / REST API / 构建
  └ containerd   生命周期与镜像管理（CRI 实现之一）
      └ runc     真正调用 namespace + cgroups + execve（遵循 OCI runtime spec）
```

**关键认知**：容器里没有第二个内核，所有容器共享宿主内核。因此容器逃逸
等价于内核漏洞；也解释了为什么 Linux 容器无法在 Windows 上原生运行。

---

## 2. WSL：两条完全不同的实现路线

### 2.1 WSL1 — 系统调用翻译层（无 VM、无真内核）

Windows 内核里加载 `lxss.sys` 与 `lxcore.sys`，把 Linux 系统调用翻译成
Windows NT 内核调用：

- ELF 格式由 Windows 内核直接加载执行
- Linux 的 fork / vfork / 线程 → 映射为 NT 进程 / 线程
- Linux VFS → 落到 NTFS（用扩展属性保存权限、符号链接等元数据）
- 没有自己的内核、没有 systemd、没有独立网络栈（直接复用 Windows 的）

**代价**：每次 syscall 都过翻译层，文件 I/O 尤其慢（大量小文件可慢数倍）。
**好处**：与 Windows 文件系统、进程完全互通，内存占用小。

### 2.2 WSL2 — 轻量 VM 里跑真内核

```
Windows
 └─ WSL 服务（wslservice）
     └─ Hyper-V Utility VM（极简，无完整 BIOS/设备模拟）
         └─ 微软维护的 Linux 内核（microsoft/WSL2-Linux-Kernel）
             ├─ ext4 根文件系统（放在 ext4.vhdx 虚拟磁盘里）
             ├─ /mnt/c  ← 9P 文件服务器（drvfs）跨 VM 访问 NTFS
             └─ systemd / WSLg / 用户 shell
```

| 机制 | 作用 |
|---|---|
| Hyper-V utility VM | 极简 VM + virtio 设备，秒级启动，内存动态回收 |
| 9P / Plan 9 协议 | Windows ↔ Linux 文件系统互访（`/mnt/c`、`\\wsl$\`） |
| vsock | Windows 与 Linux 之间的控制/通信通道（`wsl.exe`、interop） |
| interop | 在 Linux 里执行 `*.exe`；在 Windows 里用 `wsl` 执行 Linux 命令 |
| 网络 | 默认 NAT；新版 **mirrored 镜像模式**共享 Windows 网络栈，支持 IPv6 与 `localhost` 双向直达 |
| WSLg | 图形界面（Wayland + RDP 传输） |
| 设备直通 | GPU 半虚拟化（CUDA/DirectML）、USB 走 USBIP |

`.wslconfig` 控制 VM 的内存/CPU/交换；`wsl.conf` 控制发行版内部的挂载行为、
是否启用 systemd。

### 2.3 WSL1 vs WSL2

| | WSL1 | WSL2 |
|---|---|---|
| 内核 | 无（翻译层） | 真 Linux 内核 |
| 跨系统文件性能 | 快（同一文件系统） | 慢（走 9P，大量小文件尤甚） |
| Linux 内部文件性能 | 慢 | 快（ext4 VHD 本地） |
| 系统调用兼容 | 不完整（无 systemd） | 完整（可跑 Docker、可加载内核模块） |
| 内存 / 启动 | 小 / 快 | 较大 / 秒级 |

---

## 3. 两者的交汇点

**在 Windows 上使用 Docker Desktop，背后就是 WSL2**：Docker Desktop 把
`dockerd` 跑在一个专用 WSL2 发行版（`docker-desktop`）里，容器实际是
"WSL2 虚拟机里的 Linux 容器"。

所以同样是 `docker run`：

- Linux 主机：容器直接跑在宿主内核上（一层隔离）
- Windows 主机：Hyper-V VM → 真 Linux 内核 → 容器（VM + 容器两层）

---

## 4. 与本项目知识点的对应

| 本仓库方向 | 对应机制 |
|---|---|
| 进程管理 | `pid` namespace（容器里的 PID 1）、cgroup 的 CPU 分配 |
| 用户态 ↔ 内核态通信 | 9P 是典型的用户态文件服务器（FUSE 同类）；vsock 与 netlink 一样解决"跨边界通信" |
| eBPF | Docker 默认 seccomp profile 就是 seccomp-BPF |
| Binder / UAS | 与 9P、vsock 一样，是"跨边界通信"问题的不同解法 |

---

## 5. 本机可验证的命令

```bash
# 手搓一个 mini 容器：新 pid/mount/user namespace
unshare --map-root-user --pid --mount-proc --fork bash

# 在 mini 容器里确认隔离生效
hostname mini && hostname          # uts（需要 --uts）
ps aux                             # 只看到自己的进程树（pid ns）
cat /proc/self/uid_map             # 看 user namespace 的 UID 映射
mount | grep proc

# 观察当前进程所属的各个 namespace
ls -l /proc/self/ns/

# 看 cgroup 层级与当前进程受到的约束
cat /proc/self/cgroup
cat /sys/fs/cgroup/memory.max

# 观察 overlayfs 挂载（docker 运行时）
mount -t overlay
```

在 WSL 里确认内核来源：

```bash
cat /proc/version          # 含 microsoft 字样即 WSL2 的内核
cat /proc/sys/kernel/osrelease
mount -t 9p                # /mnt/c 的跨 VM 文件通道
mount | grep drvfs
```

Windows 侧（PowerShell）：

```powershell
wsl --status               # 默认版本、内核版本
wsl --list --verbose       # 各发行版跑在 WSL1 还是 WSL2
```

## 6. 参考

- [Docker 架构与 runc](https://docs.docker.com/engine/architecture/)
- [Microsoft Learn：WSL 网络（含 mirrored 模式）](https://learn.microsoft.com/zh-cn/windows/wsl/networking)
- [WSL2-Linux-Kernel 源码](https://github.com/microsoft/WSL2-Linux-Kernel)
- 内核文档：`Documentation/admin-guide/namespaces/`、`Documentation/admin-guide/cgroup-v2.rst`
