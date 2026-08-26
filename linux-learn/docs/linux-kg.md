---
name: linux-kg
description: Build personal knowledge graph for Linux kernel learning — [[wiki-links]], call chains, concept comparisons, exercises
---

# Linux 内核知识图谱

## 触发条件

当用户要求"记笔记"、"整理成笔记"、"追踪调用链"、"对比 X vs Y"、"出练习题"时触发。

## 笔记存放

```
linux-learn/notes/          # 从实践中沉淀的知识
├── 进程管理/
├── 内存管理/
├── 文件系统/
├── 网络栈/
├── 内核基础/               # 模块、printk、proc、内存分配
├── 锁与并发/
├── 调用链/                 # 系统调用 → 内核内部全路径
└── 概念对比/               # RCU vs rwlock 等
```

## 五种执行模式

| 模式 | 场景 | 要点 |
|------|------|------|
| A — 概念笔记 | 解释某段内核代码或机制 | 数据结构表 + 关键函数 + 源码位置 + 相关链接 |
| B — 调用链 | 追踪入口函数到实现 | Mermaid 序列图 + 逐层说明 |
| C — 概念对比 | RCU vs rwlock 等 | 对比表 + 各自适用场景 + 错误用法 |
| D — 练习题 | 验证理解 | 可运行的内核模块代码，放 `exercises/` |
| E — 审查链接 | 检查笔记质量 | 找孤立笔记、缺失反向链接 |

## 核心原则

1. **一砖一瓦**：每次只记一个概念，不铺太广
2. **双向链接**：每篇笔记至少 2 个 `[[链接]]`，至少被 1 篇引用
3. **三层深度**：
   - L1：核心数据结构（task_struct, mm_struct, page, inode, sk_buff）
   - L2：关键调用链（syscall → subsystem 5-7 层）
   - L3：概念对比（RCU vs rwlock, GFP_ATOMIC vs GFP_KERNEL）
4. **疑问驱动**：每篇笔记至少记录一个待验证的问题
5. **笔记即地图**：目的是快速回忆全貌，不记细节

## 黄金闭环

```
读源码 → linux-kernel-dev 动手验证 → linux-kg 沉淀笔记 → 从笔记出发继续深挖
```

笔记中的概念必须用实际代码验证。写了 [[内核模块机制]]，就去 `hello-proc/` 跑一遍；写了 `__schedule()` 调用链，就用 `record-trace.sh` 追踪一次调度切换。

## 交付物

每次操作后：
1. 创建/修改了哪些笔记（绝对路径）
2. 建立了哪些 [[链接]]
3. 建议下一步深挖方向（附可验证的动手实验）
