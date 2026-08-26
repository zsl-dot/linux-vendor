---
name: linux-kernel-dev
description: Linux 内核开发流程：使用 QEMU/virtme-ng 编译、测试、调试与追踪
---

# Linux 内核开发环境

## 项目入口

所有路径从 `linux-learn/` 出发，通过 `env.sh` 自动检测内核源码和编译产物位置。

| 操作 | 入口 |
|------|------|
| 一键搭建+验证全部 demo | `../go.sh` |
| 单个 demo 编译+VM验证 | `linux-vendor-module/<name>/run.sh build` |
| busybox VM 启动 | 各 demo 的 `run.sh` 自动启动 QEMU |
| virtme-ng 交互调试 | `../lib/vm/virtme-ng/01-run.sh` |
| 9P 免打包启动 | `../lib/vm/run-qemu-9p.sh` |

## 两大 VM 环境

- **busybox QEMU**：轻量，3 秒启动，适合自动化验证。`../go.sh demo` 批量跑全部
- **virtme-ng**：完整 Ubuntu，宿主文件系统直达。适合 perf/gdb/strace 交互调试

## 开发闭环

```bash
# 1. 改代码
vim linux-vendor-module/hello/hello.c

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
- `linux-learn/` 是纯源码+脚本，可以直接分享
- `env.sh` 和 `common.mk` 是所有脚本的单一配置来源

## 相关技能

- [[linux-kg]]：知识图谱笔记——学完就记，记完再学
