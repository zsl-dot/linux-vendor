# wait_queue — 睡眠唤醒机制与内存屏障协议

## 1. 典型流程

```c
/* 进程 A — 睡眠方 */
for (;;) {
    set_current_state(TASK_UNINTERRUPTIBLE);    // ① 写状态 + 内存屏障
    if (CONDITION)                              // ② 检查条件
        break;
    schedule();                                 // ③ 让出 CPU
}
__set_current_state(TASK_RUNNING);              // ④ 醒来后恢复

/* 进程 B — 唤醒方 */
CONDITION = 1;                                  // ⑤ 修改条件
wake_up_process(A);                             // ⑥ 唤醒进程 A
```

## 2. 核心问题：为什么需要内存屏障

### 如果不加屏障，可能丢失唤醒

```
危险时序 (CPU 乱序，无屏障):

  进程 A:  检查 CONDITION → false
  进程 B:  CONDITION = 1
  进程 B:  wake_up_process(A)  → 但 A 的状态还是 RUNNING，什么都不做
  进程 A:  set_current_state(SLEEPING)
  进程 A:  schedule()
           → 永久睡眠! 没人会再唤醒它了
```

**丢失唤醒**的根本原因：进程 A 写自己的睡眠状态和检查 CONDITION 之间，有一个窗口。如果进程 B 恰好在这个窗口内同时设置了 CONDITION 并尝试唤醒，就会错过。

### 内存屏障的解决方案

```
正确时序 (有屏障配对):

  进程 A                         进程 B
  ────────                       ────────
  set_current_state(SLEEPING)
    │ smp_store_mb():
    │   WRITE_ONCE(state)         CONDITION = 1
    │ + 完整屏障 ──────────────┐    │
    │                         │    ▼
    ▼                         │  try_to_wake_up(A)
  if (CONDITION)              │    │ smp_mb() ← 完整屏障
    → 真: break               │    ▼
    → 假: schedule()          │  读 p->state
                               │    │
                               └── 配对 ─┘

  保证: 要么 A 看到 CONDITION=1 → 直接退出 (不睡)
        要么 B 看到 state=SLEEPING → 执行唤醒
        不会丢失!
```

## 3. 两个关键函数的注释解读

### `set_current_state` vs `__set_current_state` (`include/linux/sched.h`)

```c
/* set_current_state() — 带内存屏障 */
#define set_current_state(state_value)              \
    do {                                            \
        smp_store_mb(current->__state, (state_value)); \
    } while (0)

/* __set_current_state() — 无内存屏障 */
#define __set_current_state(state_value)            \
    do {                                            \
        WRITE_ONCE(current->__state, (state_value)); \
    } while (0)
```

| | `set_current_state` | `__set_current_state` |
|---|---|---|
| 写入方式 | `smp_store_mb` (写+屏障) | `WRITE_ONCE` (纯写) |
| 使用位置 | 准备睡眠的循环头 | 醒来后恢复 RUNNING |
| 为什么 | 必须和 `try_to_wake_up` 的屏障配对 | 已经确认被唤醒，不需要防丢失 |

注释原文的核心：唤醒方 `try_to_wake_up` 在访问 `p->state` 之前发送完整屏障，睡眠方 `set_current_state` 发送完整屏障。两边的屏障配对，确保不会丢失唤醒。

注释还提到一种边缘情况：唤醒方的 `TASK_RUNNING` 写可能与睡眠方的 `__set_current_state(TASK_RUNNING)` 冲突，但那无所谓——反正在 `schedule()` 外面，多写一次不影响正确性。

### `try_to_wake_up` (`kernel/sched/core.c`)

```c
/**
 * try_to_wake_up - wake up a thread
 *
 * Conceptually does:
 *   If (@state & @p->state) @p->state = TASK_RUNNING.
 *
 * This function is atomic against schedule() which would dequeue the task.
 *
 * It issues a full memory barrier before accessing @p->state, see the comment
 * with set_current_state().
 *
 * Tries really hard to only take one task_rq(p)->lock for performance.
 *
 * As a consequence we race really badly with just about everything. See the
 * many memory barriers and their comments for details.
 */
```

关键解读：

1. **"atomic against schedule()"** — `try_to_wake_up` 和 `schedule()` 在逻辑上互斥。`schedule()` 把 task 从运行队列摘除，`try_to_wake_up` 把它放回去。

2. **"full memory barrier before accessing @p->state"** — 与 `set_current_state` 的 `smp_store_mb` 配对。

3. **"tries really hard to only take one rq lock"** — 为了性能，尽可能只拿一次运行队列锁。

4. **"race really badly with just about everything"** — 正因为只拿一次锁，与几乎所有调度相关操作都有竞态。这也是为什么函数里有大量内存屏障和 `WRITE_ONCE`/`READ_ONCE`。

## 4. 完整的内存屏障配对图

```
进程 A (睡眠方)                    进程 B (唤醒方)
                                  CONDITION = 1;              ⑤ 修改条件

set_current_state(SLEEPING)       try_to_wake_up(A, state, ..)
  │                                 │
  │ smp_store_mb():                │ smp_mb()  ← 完整屏障
  │   WRITE_ONCE(state, SLEEPING)   │
  │ + 完整屏障                      ├─ 读 p->state
  │                                 │   如果 state 匹配:
  │                                 │     p->state = TASK_RUNNING
  │                                 │     放回运行队列
  ▼                                 │
if (CONDITION)                      │
  → 真: break (不睡)                │
  → 假: schedule()                  │
  │                                 │
  ▼                                 ▼
__set_current_state(RUNNING)    // task 被唤醒或从未睡眠
```

**屏障配对的保证**：两边都执行了完整屏障，CPU 无法把"检查 CONDITION"移到"写 SLEEPING"之前，也无法把"读 state"移到"写 CONDITION"之前。因此绝对不会丢失唤醒。

## 5. `wake_q_node` 与 `try_to_wake_up` 的衔接

在 wait_queue_demo 的 [02_wake_q_internal.c](02_wake_q_internal.c) 和 [03_combined_demo.c](03_combined_demo.c) 中，我们演示了 `wake_up_interruptible` 的调用链最终落到 `try_to_wake_up`。

wake_q_node 是一个**互补的优化**——它不替代 wait_queue，而是在 waker 持锁时把 `try_to_wake_up` 的调用延迟到解锁之后：

```
持锁时 (不能直接唤醒):
  spin_lock(&lock);
  wake_q_add(&wq, task);    ← 只记录到链表 (轻量, O(1))
  spin_unlock(&lock);

解锁后 (安全了):
  wake_up_q(&wq);           ← 批量调用 wake_up_process → try_to_wake_up
```

## 6. demo 文件导航

| 文件 | 内容 |
|---|---|
| `01_basic_wait_queue.c` | 模拟 `wait_event_interruptible` + `wake_up_interruptible` 用法 |
| `02_wake_q_internal.c` | 从 wait_queue 内部追踪到 wake_q_node 的桥接 |
| `03_combined_demo.c` | 完整三层架构：驱动 API → wait_queue → wake_q_node |

### 运行

```bash
make          # 编译全部
make run      # 运行全部
```

## 7. 相关资源

- [wake_q_demo/](../wake_q_demo/) — 底层: wake_q_node 链表机制与延迟唤醒优化
- 内核源码: `include/linux/sched.h`, `kernel/sched/core.c`
