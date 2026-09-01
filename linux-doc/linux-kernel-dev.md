---
name: linux-kernel-dev
description: Linux 内核开发流程：使用 QEMU/virtme-ng 编译、测试、调试与追踪
---

# Linux 内核开发环境

## 项目入口

所有路径从 `vendor-module/` 出发，通过 `env.sh` 自动检测内核源码和编译产物位置。

| 操作 | 入口 |
|------|------|
| 一键搭建+验证全部 demo | `../go.sh` |
| 单个 demo 编译+VM验证 | `<name>/run.sh build` |
| busybox VM 启动 | 各 demo 的 `run.sh` 自动启动 QEMU |
| virtme-ng 交互调试 | `../lib/vm/virtme-ng/01-run.sh` |
| 9P 免打包启动 | `../lib/vm/run-qemu-9p.sh` |

## 两大 VM 环境

- **busybox QEMU**：轻量，3 秒启动，适合自动化验证。`../go.sh demo` 批量跑全部
- **virtme-ng**：完整 Ubuntu，宿主文件系统直达。适合 perf/gdb/strace 交互调试

## virtme-ng 的工作原理

`virtme-ng` 启动的是 QEMU/KVM 虚拟机，不是容器。QEMU 加载项目编译的
`build/linux-out/arch/x86/boot/bzImage`，然后启动一个完整的用户空间 Shell。
因此宿主机和 VM 具有独立的内核、进程、`/proc`、`/sys` 和模块环境：

```text
宿主机 Ubuntu 7.0.x
        │ QEMU/KVM
        ▼
VM：项目自定义内核 7.2.0-g66498c75b4f8
        │
        └── Shell：zslmxd@virtme-ng
```

宿主机文件系统通过 virtio-9p 映射到 VM，源码和 `.ko` 可以直接访问，默认
只读挂载。共享的是文件，不是内核；所以项目模块必须在 `virtme-ng` 提示符
内加载，不能在宿主机执行 `insmod`。

## 适用的开发验证场景

这种方案适合快速、可重复且不依赖真机的内核工作：

- 内核编译结果和启动参数验证；
- 外部模块的加载、卸载及 `/proc`、sysfs、Netlink 接口测试；
- eBPF、ftrace、perf、strace 等观测和追踪实验；
- 使用带调试信息的 `vmlinux` 配合 GDB/KGDB 定位执行路径；
- KUnit、kselftest、LTP 和自动化回归测试；
- KASAN、lockdep、kmemleak 等检测配置的快速复现；
- 在没有 ARM/RISC-V 真机时，使用对应 QEMU 架构做早期 bring-up。

它不能替代真机验证：真实外设、BootROM/U-Boot、时钟/电源管理、硬件中断
时序、性能功耗和驱动电气特性仍需在目标板上测试。

## 开发闭环

```bash
# 1. 改代码
vim hello/hello.c

# 2. 编译 + VM 验证（一条命令）
./run.sh build

# 3. 或用 virtme-ng 快速迭代（免打包 rootfs）
../../../lib/vm/virtme-ng/01-run.sh
# VM 内直接 insmod <宿主路径>/hello.ko
```

## ftrace 录制 → Perfetto 可视化

```bash
# 拖入 https://ui.perfetto.dev 查看
```

## 重点约定

- 编译产物统一位于项目根目录的 `build/`（内核为 `build/linux-out/`，QEMU rootfs 为 `build/vm-rootfs/`）
- `vendor-module/` 是纯源码+脚本，可以直接分享
- `env.sh` 和 `common.mk` 是所有脚本的单一配置来源

## 相关技能

- [[linux-kg]]：知识图谱笔记——学完就记，记完再学
