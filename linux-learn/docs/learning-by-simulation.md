---
name: learning-by-simulation
description: User learns Linux kernel structs by building userspace C demo directories with step-by-step simulations
metadata:
  node_type: memory
  type: feedback
  originSessionId: 8d523dcd-ccb9-4c0a-8b4f-186e392ce1e2
---

When explaining a Linux kernel data structure or mechanism, create a self-contained demo directory with runnable userspace C programs that simulate the kernel code.

**Why:** User confirmed this is their preferred way to learn — "用来学习linux相关结构体的设计和知识学习". The wake_q_node demos (wake_q_demo/) and wait_queue demos (wait_queue_demo/) were well received.

**How to apply:**
1. Create a new directory (e.g., `<topic>_demo/`) with a Makefile and numbered C files (`01_*.c`, `02_*.c`, ...)
2. Each C file should compile and run standalone — produce visible output showing what happens at each step
3. Progression: start simple, build up complexity, explicitly state "this corresponds to kernel code at path/to/file.c"
4. Include ASCII diagrams in printf output showing memory layout and pointer relationships
5. Use `container_of`, `cmpxchg`, and other kernel macros in the simulation so the user sees them in action
6. Execute `make && make run` (or individual targets) to verify before reporting completion
7. Prefer tab-indented, C89-style code (matching kernel coding style from CLAUDE.md)

**Directory pattern:**
```
<topic>_demo/
├── Makefile           # make run, make clean, individual targets
├── 01_basics.c        # simplest concept first
├── 02_intermediate.c  # add complexity
├── 03_full_sim.c      # complete kernel-like simulation
└── ...
```
