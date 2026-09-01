---
name: cpp-go-kernel-modeling
description: 用 C++/Go 面向对象模型理解 Linux 内核 C 设计，并通过 QEMU 对照验证
metadata:
  node_type: skill
  type: learning-workflow
---

# C++/Go 面向对象映射内核 C 实现

## 目标

先用熟悉的 C++/Go 表达内核机制的对象、接口、并发和生命周期，再回到 Linux C 源码理解真实实现。模型代码用于学习，不编译进内核，也不能替代 QEMU/真机验证。

## 标准学习闭环

```text
Go 并发模型 → C++ 对象/接口模型 → Linux C 实现 → QEMU 验证 → KGDB/ftrace 观察
```

每个主题都回答：数据结构是什么？接口如何分派？对象谁创建/释放？错误如何回滚？并发由谁同步？

## 概念映射

| Linux C | C++ | Go |
|---|---|---|
| `struct file_operations` | 抽象类/虚函数表 | interface |
| `wait_queue_head_t` | 条件变量/事件对象 | channel |
| `poll_wait()` | 注册观察者 | 订阅 channel |
| `wake_up_interruptible()` | `notify_one/all()` | 发送事件 |
| `kmalloc/kfree` | RAII/智能指针的手动版本 | GC 管理的堆 |
| `mutex_lock` | `std::mutex` | `sync.Mutex` |
| `list_head` | intrusive list | 链表或切片 |
| `kref` | `shared_ptr` | 引用由 GC 管理 |

## 推荐目录

为一个机制建立并行实现：

```text
<demo>/
├── <demo>.c          # Linux 内核真实实现
├── <demo>_client.c   # 用户态真实调用
├── cpp-model/        # C++ 类、虚函数和 RAII 模型
├── go-model/         # Go interface、goroutine、channel 模型
└── README.md         # 三种实现的逐项映射
```

当前项目可按以下顺序学习：`hello`（模块生命周期）→ `hello-proc`（回调接口）→ `epoll-demo`（`file_operations`、等待队列）→ `netlink-demo`（异步消息）→ `wake_q_demo`/`wait_queue_demo`（链表与唤醒）→ `kgdb-demo`（调用栈和生命周期）。

## 实践要求

1. 先写最小 Go 模型，验证事件和并发语义。
2. 再写 C++ 模型，显式展示类、接口、所有权和清理路径。
3. 对照 Linux C，标出函数指针、锁、引用计数和错误码位置。
4. 使用 `./run.sh build` 或 `./go.sh demo` 在自定义内核 QEMU 中验证。
5. 使用 `uname -r` 确认处于项目内核 VM；不要在宿主机加载项目 `.ko`。

## 典型对应

`epoll-demo` 中，C++ 的虚函数 `poll()` 对应 `file_operations.poll`，Go 的 `<-events` 对应 `wait_queue` 被唤醒后 `epoll_wait()` 返回；最终以 `/dev/epoll_demo` 的真实 `EPOLLIN` 输出作为验证证据。
