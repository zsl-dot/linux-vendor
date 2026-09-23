# Linux 内核学习文档

本目录是项目唯一的知识文档入口；`vendor-module/` 仅保留可编译、可运行的 demo 与其最小使用说明。

| 文档 | 内容 |
|---|---|
| [user-kernel-communication.md](user-kernel-communication.md) | 用户空间与内核空间的通信方式 |
| [netlink-user-kernel-demo.md](netlink-user-kernel-demo.md) | Netlink 用户进程与内核模块双向通信示例 |
| [process-management.md](process-management.md) | 调度、睡眠唤醒与进程管理 |
| [sched-and-tracing-demo.md](sched-and-tracing-demo.md) | 调度学习环境：ftrace/cgroup 带宽/sched_ext 配置与 sched-demo、ebpf-demo3 |
| [abi.md](abi.md) | API、ABI 与内核接口 |
| [linux-kernel-dev.md](linux-kernel-dev.md) | 编译、QEMU、virtme-ng 调试流程 |
| [kernel-build-deep-dive.md](kernel-build-deep-dive.md) | bzImage、Kbuild、.ko、obj-y/obj-m 与 AOSP/GKI 对照 |
| [clangd-full-index.md](clangd-full-index.md) | VS Code 全量源码跳转的排查过程与 `./go.sh index` 实现 |
| [docker-wsl-principles.md](docker-wsl-principles.md) | Docker（namespace/cgroup/overlayfs）与 WSL1/WSL2 的实现原理 |
| [container-isolation-landscape.md](container-isolation-landscape.md) | 业界容器化全景：runc、Kata、gVisor、Firecracker、机密容器 |
| [surfaceflinger-hwc 相关](../../knowledge-base/android/composer/README.md) | SF↔HWC/composer 分析已迁至知识库 `knowledge-base/android/composer/` |
| [env-troubleshooting.md](env-troubleshooting.md) | 宿主环境排查：dpkg 坏包、Docker 占端口、SVG 中文渲染 |
| [hello-end-to-end.md](hello-end-to-end.md) | 以 hello 为例的内核、模块和 QEMU 完整编译验证流程 |
| [hello-build-chain.html](hello-build-chain.html) | 以 HTML 流程图说明 hello 编译、匹配与验证 |
| [linux-kg.md](linux-kg.md) | 内核知识图谱与学习路线 |
| [learning-by-simulation.md](learning-by-simulation.md) | 用户态模拟 demo 的编写约定 |
| [skill-knowledge.md](skill-knowledge.md) | 开发辅助能力说明 |
| [cpp-go-kernel-skill.md](cpp-go-kernel-skill.md) | 用 C++/Go 面向对象模型理解内核 C 实现 |
| [auto-sync-docs.md](auto-sync-docs.md) | 学习文档同步约定 |
| [uas-vendor-demo.md](uas-vendor-demo.md) | UAS Android vendor 组件边界与结构验证 |

构建和运行产物不放在本目录，统一位于 `../build/`。
