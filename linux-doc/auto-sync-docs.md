---
name: auto-sync-docs
description: 讨论 Linux 内核概念后，自动更新相关学习文档
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

分析 Linux 内核代码或概念后，如果用户确认已理解，应主动更新相应的学习文档，无需再次询问。

**原因：** 本项目要构建相互关联的知识库，需要将分析结论自动沉淀到文档中。

**执行方式：**

1. 解释概念并确认用户理解后，判断主题应归入哪篇文档。
2. 将结论插入对应文档；保留原有章节编号，必要时重新编号。
3. 在合适位置补充与相关文档的交叉引用。
4. 简要说明本次更新了哪篇文档。

**文档归属：**

| 主题 | 记录位置 |
|---|---|
| 内核模块基础（init/exit、printk） | `vendor-module/kernel/hello/`；demo 本身是实践说明 |
| /proc 文件系统、模块参数 | `vendor-module/kernel/hello-proc/` |
| Binder IPC（ioctl、服务管理） | `vendor-module/kernel/binder-demo/` |
| Netlink 用户态/内核通信 | `linux-doc/netlink-user-kernel-demo.md` 与 `vendor-module/kernel/netlink-demo/` |
| eBPF kprobe、perf_event、BPF 系统调用 | `vendor-module/kernel/ebpf-demo1/` 或 `ebpf-demo2/` |
| ftrace、tracefs、Perfetto | `linux-doc/linux-kernel-dev.md` |
| kgdb、内核调试 | `vendor-module/kernel/kgdb-demo/` |
| 编码风格、checkpatch、sparse | 项目根目录 `CLAUDE.md` |
| 内核配置（BPF、BTF、ftrace、9P） | `lib/kernel.sh` 与 `lib/workflow_config.py` |
