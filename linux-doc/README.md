# Linux 内核学习文档

本目录是项目唯一的知识文档入口；`vendor-module/` 仅保留可编译、可运行的 demo 与其最小使用说明。

| 学习域 | 文档 | 内容 |
|---|---|---|
| 基础 | [hello-end-to-end.md](hello-end-to-end.md) | 以 hello 为例的内核、模块和 QEMU 完整编译验证流程 |
| 基础 | [hello-build-chain.html](hello-build-chain.html) | 以 HTML 流程图说明 hello 编译、匹配与验证 |
| 基础 | [abi.md](abi.md) | API、ABI 与内核接口 |
| 基础 | [linux-kernel-dev.md](linux-kernel-dev.md) | 编译、QEMU、virtme-ng 调试流程 |
| 基础 | [kernel-build-deep-dive.md](kernel-build-deep-dive.md) | bzImage、Kbuild、.ko、obj-y/obj-m 与 AOSP/GKI 对照 |
| 基础 | [clangd-full-index.md](clangd-full-index.md) | VS Code 全量源码跳转的排查过程与 `./go.sh index` 实现 |
| 基础 | [env-troubleshooting.md](env-troubleshooting.md) | 宿主环境排查：dpkg 坏包、Docker 占端口、SVG 中文渲染 |
| 通信 | [user-kernel-communication.md](user-kernel-communication.md) | 用户空间与内核空间的通信方式 |
| 通信 | [netlink-user-kernel-demo.md](netlink-user-kernel-demo.md) | Netlink 用户进程与内核模块双向通信示例 |
| 调度 | [process-management.md](process-management.md) | 调度、睡眠唤醒与进程管理 |
| 调度 | [sched-and-tracing-demo.md](sched-and-tracing-demo.md) | 调度学习环境：ftrace/cgroup 带宽/sched_ext 配置与 sched-demo |
| 容器 | [docker-wsl-principles.md](docker-wsl-principles.md) | Docker（namespace/cgroup/overlayfs）与 WSL1/WSL2 的实现原理 |
| 容器 | [container-isolation-landscape.md](container-isolation-landscape.md) | 业界容器化全景：runc、Kata、gVisor、Firecracker、机密容器 |
| 容器 | [runc-on-custom-kernel.md](runc-on-custom-kernel.md) | L3 发行版通道：runc 真容器验证与 CGROUP_BPF 踩坑 |
| GPU | [drm-skeleton.md](drm-skeleton.md) | 最小 DRM/KMS 驱动骨架：层次结构与两个 7.3 实战坑 |
| Android | [uas-vendor-demo.md](uas-vendor-demo.md) | UAS Android vendor 组件边界与结构验证 |
| Android | [surfaceflinger-hwc 相关](../../knowledge-base/android/composer/README.md) | SF↔HWC/composer 分析已迁至知识库 `knowledge-base/android/composer/` |
| 方法论 | [linux-kg.md](linux-kg.md) | 内核知识图谱与学习路线 |
| 方法论 | [learning-by-simulation.md](learning-by-simulation.md) | 用户态模拟 demo 的编写约定 |
| 方法论 | [cpp-go-kernel-skill.md](cpp-go-kernel-skill.md) | 用 C++/Go 面向对象模型理解内核 C 实现 |
| 方法论 | [skill-knowledge.md](skill-knowledge.md) | 开发辅助能力说明 |
| 方法论 | [auto-sync-docs.md](auto-sync-docs.md) | 学习文档同步约定 |

学习域与代码分层对应：`vendor-module/kernel/<域>/` 的 demo、`lib/kernel.sh` 里按域分组的配置、本表的文档——三者同名对应。
构建和运行产物不放在本目录，统一位于 `../build/`。
