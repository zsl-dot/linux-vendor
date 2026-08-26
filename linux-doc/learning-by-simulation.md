---
name: learning-by-simulation
description: 通过逐步运行的用户态 C 模拟程序学习 Linux 内核结构与机制
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

解释 Linux 内核数据结构或机制时，应创建独立的 demo 目录，以可运行的用户态 C 程序模拟内核代码。

**原因：** 用户确认这是偏好的学习方式：通过模拟程序理解 Linux 相关结构体设计与机制。`wake_q_demo/` 与 `wait_queue_demo/` 是已验证的示例。

**执行方式：**

1. 创建新目录，例如 `<topic>_demo/`，包含 Makefile 与按序编号的 C 文件（`01_*.c`、`02_*.c`）。
2. 每个 C 文件必须可独立编译运行，并输出每一步发生的行为。
3. 由简单到复杂推进，明确说明“这里对应内核中的 `path/to/file.c`”。
4. 用 `printf` 输出 ASCII 图，展示内存布局和指针关系。
5. 尽量使用 `container_of`、`cmpxchg` 等内核宏，使模拟与真实代码建立联系。
6. 报告完成前执行 `make && make run` 或对应单目标验证。
7. 采用 Tab 缩进和 C89 风格，与 `CLAUDE.md` 中的内核风格保持一致。

**目录模式：**
```
<topic>_demo/
├── Makefile           # make run、make clean 与单独目标
├── 01_basics.c        # 最简单的概念
├── 02_intermediate.c  # 增加复杂度
├── 03_full_sim.c      # 完整的类内核模拟
└── ...
```
