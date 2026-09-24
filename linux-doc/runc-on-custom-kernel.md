# 在自定义内核上跑真容器：runc 与 L3 发行版通道

本文说明 `kernel/container/runc-demo/` 与 `lib/vm/mk-distro-rootfs.sh`（L3 第三通道）的关系，以及第一次打通时踩到的两个真实内核配置问题。

## 三条 VM 通道的分工

| 通道 | rootfs | 用户态 | 适用 |
|---|---|---|---|
| QEMU + busybox（`mk-rootfs.sh`） | ~10MB | busybox 单二进制 | ko/BPF demo 的无人值守自动验证 |
| virtme-ng（`lib/vm/virtme-ng/`） | 无（9P 共享宿主） | 宿主机完整用户态 | 交互实验：Vulkan、perf、bpftrace |
| **发行版（`mk-distro-rootfs.sh`）** | ~800MB | Debian bookworm + runc | **容器运行时、需要 glibc/真实 userland 的实验** |

内核始终是同一个 `build/linux-out/bzImage`——三条通道只是换"内核之上的用户态"。

## runc-demo 验证了什么

`container-demo` 逐项验证内核能力（unshare/mount/cgroup2）；`runc-demo` 在此之上走完**真实运行时**的链路：

1. `runc run -b /bundle -d demo-container`：按 OCI 规范（`bundle/config.json`）创建容器；
2. 容器内 PID 1 是我们自己的 `/init.sh`（pid namespace 证据：`IN_PID=1`）；
3. uts namespace：容器内 hostname=demo-container，guest 宿主不受影响；
4. mount namespace：容器内挂载树是 runc 按规范拼装的最小集，远小于宿主；
5. `runc exec`：向运行中容器的 pid/mount ns 注入进程；
6. cgroup v2：`cgroupsPath: demo-cg` → `/sys/fs/cgroup/demo-cg` 被创建。

容器根文件系统就是 busybox——与通道一的复用，说明"容器 rootfs"只是个目录树。

## 踩坑记录（都已修进 kernel.sh 配置组）

1. **`CONFIG_CGROUP_BPF` 未开**：cgroup v2 没有独立的设备控制器，设备访问控制由挂在 cgroup 上的 BPF 程序（`BPF_PROG_TYPE_CGROUP_DEVICE`）实现。runc 启动容器时 `bpf_prog_query(BPF_CGROUP_DEVICE)` 直接 `EINVAL`。修复：container 配置组加 `--enable CONFIG_CGROUP_BPF`（flavor base5）。
2. **OCI `process.env` 是字符串数组**（`"PATH=/bin"`），不是 name/value 对象——写错时 runc 报 `json: cannot unmarshal object`。

## 扩展实验方向

- bundle 里加 `linux.resources.memory` / `cpu`，对照 `cpu.stat` 观察限流（与 sched-demo 的 T4 呼应）；
- `namespaces` 增删 `network`，观察 veth/bridge 的组网需求；
- 把 rootfs 换成完整 Debian（debootstrap 目录直接当容器根），就是"发行版容器"的最小复刻；
- 对照 `container-isolation-landscape.md` 理解 runc 之下的内核机制分层。
