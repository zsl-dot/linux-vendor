# ABI (Application Binary Interface) — 从 C 到 Rust 的全面分析

## 1. ABI 是什么

### API 与 ABI 的区别

```
API (Application Programming Interface)  →  编译时的约定 (头文件)
ABI (Application Binary Interface)       →  运行时的约定 (二进制层面)
```

API 解决"**编译时**能不能通过"。ABI 解决"**运行时**二进制能不能正确跑"。

两个 `.c` 文件可以共享同一个头文件（API 一致），但如果用不同调用约定编译（ABI 不一致），链接后运行结果不可测。

### ABI 具体规定哪些东西

| 规则项 | 例子 |
|---|---|
| 调用约定 | 前 6 个整数参数走寄存器，第 7 个起压栈 |
| 返回值约定 | 整数走 rax，浮点走 xmm0，大 struct 走隐藏指针 |
| 类型大小 | `long` 在 64 位 Linux 是 8 字节，在 64 位 Windows 是 4 字节 |
| 对齐规则 | struct 成员按自然大小对齐，struct 整体按最大成员对齐 |
| 符号命名 | C 用裸名，C++ 有 name mangling |
| 栈管理 | 栈指针 16 字节对齐，red zone 128 字节 (x86-64) |
| 系统调用 | `syscall` 指令，参数通过特定寄存器传递 |

### 一个具体例子

```c
long bar(long a, long b, long c, long d, long e, long f);
```

不同平台的 ABI 对同一个函数有完全不同的二进制约定：

```
x86-64 System V ABI (Linux):
  前 6 个整数参数 → rdi, rsi, rdx, rcx, r8, r9
  返回值           → rax

ARM64 ABI (AAPCS):
  前 8 个整数参数 → x0, x1, x2, x3, x4, x5, x6, x7
  返回值           → x0

Windows x64 ABI:
  前 4 个整数参数 → rcx, rdx, r8, r9
  额外要求: 调用者必须在栈上预留 32 字节 "shadow space"
  返回值           → rax
```

调用者按一种 ABI 生成 `call` 指令，被调用者按另一种 ABI 期望接收参数 → 结果完全不可测。

---

## 2. C 的 ABI: 平台的事实标准

### C 的 ABI 不是 C 标准定的

C 语言标准（ISO C）**从没提过 ABI**。标准只管语法语义，不管二进制。

真正定义 ABI 的是**平台厂商**:

| 平台 | ABI 规范 | 制定者 |
|---|---|---|
| x86-64 Linux | System V AMD64 ABI | Intel / AMD |
| ARM64 Linux | AArch64 AAPCS | ARM |
| RISC-V | RISC-V psABI | RISC-V 基金会 |
| Windows | Windows x64 ABI | Microsoft |
| MIPS | MIPS ABI (o32/n64) | MIPS Technologies |

### 为什么平台的 ABI 用 C 定义

因为 **操作系统本身就是 C 写的**:

```
┌───────────────────────────────────────┐
│  硬件平台 (x86-64 / ARM64 / RISC-V)    │
│                                       │
│  平台 ABI 文档                         │
│    "long 是 8 字节"                    │
│    "前 6 个整数参数走 rdi, rsi, ..."    │
│    ← 全部用 C 的类型概念来描述          │
│                                       │
│  操作系统                             │
│    内核是 C 写的 → syscall = C ABI     │
│    libc.so 是 C 写的 → 函数调用 = C ABI│
│                                       │
│  一切上层语言                          │
│    Python/Rust/Java/Go → FFI → C ABI   │
└───────────────────────────────────────┘
```

不是 C 决定了 ABI。是**平台**定义了 ABI，并因为操作系统的所有接口都是 C 写的，这份 ABI 天然就是"**C 语义的二进制表达**"。

### C 的概念直接映射到硬件

```
C 类型      →  硬件
────────────────────
int         →  4 字节通用寄存器
long        →  8 字节通用寄存器
pointer     →  地址总线宽度
struct      →  连续内存布局
function    →  call/ret 指令
char[]      →  连续字节 + null 终结符
```

C 是最接近硬件的语言，它的每个类型和操作都直接对应 CPU 指令。这才是它能定义 ABI 的根本原因。

### C ABI 为什么 50 年不变

换 ABI = 换所有二进制。Linux 内核的用户空间 ABI "永不破坏"（never break userspace）是铁律:

```
libc.so.6 是 10 年前编译的 → 系统上所有程序都链接它 → 调用 printf

今天新编译一个程序:
  → 链接同一个 libc.so.6
  → printf 的符号名还是 "printf" (纯文本)
  → x86-64 SysV ABI 没变
  → 参数类型布局没变
  → 栈对齐规则没变
  → 一切正常。不需要重新编译整个系统。
```

### C ABI 的统一性: 所有编译器共用同一个契约

```
                    C ABI (x86-64 SysV)
                   ┌─────────────────┐
    GCC 4.8  ──────┤                 ├────── 任何 C 编译器
    GCC 14   ──────┤  同一个二进制    ├────── 任何语言 (Python、Rust...)
    Clang 19 ──────┤  契约            ├────── 任何版本的 .so
                   └─────────────────┘
```

---

## 3. C 的 `extern` 关键字

### extern 的含义

`extern` 不是 C 特有的语法糖，它是**分离编译模型的产物**:

```c
/* sched.h — 头文件 (声明) */
extern void wake_up_q(struct wake_q_head *head);   // 告诉编译器: "这个函数存在，但在别处"

/* core.c — 源文件 (定义) */
void wake_up_q(struct wake_q_head *head)           // 真正的实现在这里
{
    // ... 实现代码
}
```

C 的编译模型是**分离编译 + 链接**: 每个 `.c` 文件独立编译成 `.o`，编译器看不到其他文件的内容。`extern` 就是告诉编译器"相信这个符号会由链接器在别处找到"。

### extern 对函数和对变量的区别

```c
/* 函数 — extern 是可选的 (函数声明默认就是 extern) */
extern long schedule_timeout(long timeout);   // 显式
       long schedule_timeout(long timeout);   // 等价

/* 变量 — extern 是必须的! */
int global_counter;         // 不带 extern = 定义一个新变量 (分配内存、占符号位)
extern int global_counter;  // 带 extern = 引用别处已定义的变量 (不分配内存)
```

### 纯文本符号的"危险"和"力量"

危险: 声明和定义不匹配，链接器无法检测:

```c
/* file_a.c */
extern void foo(int x);    // 声明: 接受 int

/* file_b.c */
void foo(long x) { ... }   // 定义: 实际接受 long (8字节 vs 4字节)
```

编译不报错，链接不报错，运行结果不可测——调用方 push 了 4 字节的 `int`，被调用方读了 8 字节。

力量: 纯文本符号让跨语言互操作成为可能:

```c
// C ABI 是唯一所有语言都能对话的公共接口

Python:  lib = ctypes.CDLL("./mylib.so");  lib.foo(42)
Rust:    extern "C" { fn foo(x: i32); }
Go:      // #include "foo.h" → CGo → C ABI 调用
Java:    native int foo(int x);  → JNI → C ABI 调用

// 全部都通过 C 的纯文本符号名 "foo" 来互操作
```

防御手段: 定义方必须 include 自己的头文件，让编译器在同一翻译单元内检查声明和定义是否一致:

```c
/* kernel/sched/core.c */
#include "sched/wake_q.h"       // 声明了 wake_up_q 的签名

void wake_up_q(struct wake_q_head *head)  // 编译器会检查签名是否和头文件一致
{
    ...
}
```

---

## 4. C++ 的 ABI: 有，但不统一

### C++ ABI：实际上是"每个编译器的实现细节"

```
                    平台 C++ "ABI" (不统一)
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
    GCC (Itanium     Clang (尽力        MSVC (完全
     C++ ABI 变体)   兼容 Itanium)      不兼容前两者)
```

GCC 13 编译的 `.so` 和 GCC 8 编译的 `.so`，用 C++ STL 类型互相传递时，**可能不兼容**。

### 三个具体障碍

**1. 模板 — 每个实例化都是新符号**:

```cpp
template <typename T>
class vector { T* _M_start; T* _M_finish; T* _M_end; };

vector<int> a;      // _M_start 的偏移量取决于当前 STL 版本
                    // libstdc++ v6 和 v7 的内部布局可能不同
```

**2. 虚函数表 — vtable 布局没有跨编译器规范**:

```cpp
class Base {
    virtual void f();     // vtable[0]? vtable[-1]? RTTI 指针在哪?
    virtual void g();     // GCC 和 MSVC 对 vtable 结构没有共识
};

// GCC 编译的库抛出的对象 → MSVC 编译的代码看到的 vtable 布局完全不同
```

**3. 异常 — 机制不互通**:

```
GCC:   用 DWARF 格式的展开表 (_Unwind_RaiseException)
MSVC:  用 SEH (Structured Exception Handling) 机制

一个编译器编译的库抛出的异常 → 另一个编译器编译的代码无法 catch
```

### 对比表

| | C ABI | C++ ABI |
|---|---|---|
| 统一性 | **一个**平台 ABI | GCC ABI / MSVC ABI 分裂 |
| 跨编译器 | ✓ 任何编译器 | ✗ 通常不行 (GCC↔Clang 勉强) |
| 跨版本 | ✓ 所有版本 | ✗ 不保证 (STL 内部布局随版本变化) |
| 符号名 | `foo` (裸名，纯文本) | `_Z3fooii` (mangled，依赖类型) |
| 内核能用吗 | ✓ 内核就是 C | ✗ 模块和主线之间 ABI 不可调和 |

---

## 5. Rust 的 ABI：和 C++ 同样的命运

### Rust 官方态度: "没有稳定 ABI，也不会承诺有"

```
                Rust "ABI"
                     │
      ┌──────────────┼──────────────┐
      ▼              ▼              ▼
  rustc 1.70     rustc 1.80     rustc 1.85
  (符号名不同)   (布局可能不同)  (再变一次)
```

Rust 的语言特性同样排斥标准化二进制布局:

```rust
// 泛型 → 编译期单态化 → 实例的符号和布局随版本变化
fn foo<T>(x: T) { ... }

// trait object → vtable 结构是 rustc 实现细节
trait MyTrait { fn do_thing(&self); }
// dyn MyTrait 的 vtable: [drop_fn, size, align, method_1, ...]
// 这个布局没有 RFC 标准化
```

### Rust 的桥: `extern "C"` 和 `#[repr(C)]`

Rust 没有走"替代 C"的路线，而是**在 C ABI 的边界上包装**:

```rust
#[repr(C)]                       // ← 用 C 的 struct 布局
struct MyStruct {
    a: i32,
    b: u64,
}

#[no_mangle]                     // ← 不 mangle 符号名
pub extern "C" fn my_fn() {     // ← 用 C 的调用约定
    // 内部可以随意用 Rust 的所有特性
}
```

```
┌─────────────────────────────────────┐
│  Rust 驱动模块                       │
│                                     │
│  #[repr(C)]    ← 数据结构用 C 布局   │
│  extern "C"    ← 函数用 C 链接      │
│                                     │
│  内部: 所有权, trait, 泛型...       │
└───────────────┬─────────────────────┘
                │
    extern "C" 边界 (C ABI!)
                │
┌───────────────┴─────────────────────┐
│  内核 C 代码                         │
└─────────────────────────────────────┘
```

### 三语言对比总表

| | C | C++ | Rust |
|---|---|---|---|
| ABI 稳定性 | ✓ 平台标准，50 年不变 | ✗ 编译器内部细节 | ✗ 编译器内部细节 |
| ABI 制定者 | 平台厂商 (Intel/ARM/...) | 无统一标准 | 无统一标准 |
| 符号名 | 裸名，纯文本 | name mangling | name mangling |
| 跨编译器兼容 | ✓ | ✗ | ✗ |
| 跨版本 `.so` 兼容 | ✓ | ✗ | ✗ |
| 内核能用吗 | ✓ 主线语言 | ✗ 拒绝 | ✓ 接受 (通过 `extern "C"` 边界) |
| 为什么能/不能 | 类型直接映射到硬件 | 模板/虚表无硬件映射 | 和 C ABI 互为补充 |

---

## 6. ABI 的历史演变

```
1972: C 诞生 — 没有 ABI 这个概念
1973: Unix V4 用 C 重写 — 系统调用接口天然 = C 的函数签名
1980s: SunOS, HP-UX, AIX — 各自用 C 编译器，碰巧兼容
1990s: Linux, Windows NT — C ABI 已是全行业的事实标准
2001: x86-64 System V ABI — 正式的 ABI 规范文档诞生
2010s: ARM64 AAPCS — ARM 生态的 ABI 标准化
今天:   所有语言通过 C ABI 互操作 — 50 年的惯性无法动摇
```

C ABI 不是被"设计"出来的，是 50 年逐渐**演化成的**——Unix 的内核是 C 写的，系统调用的接口就是 C 函数的接口。一代一代的硬件平台为了兼容这套操作系统生态，用自己的 ABI 规范表达了"怎么用本平台的机器码实现 C 的函数调用"。

---

## 7. 为什么内核拒绝 C++ 而接纳 Rust (但两者都依赖 C ABI)

### Linux 内核的态度

| 语言 | 主线态度 | 原因 |
|---|---|---|
| C | ✓ 永远的主线 | ABI 稳定，硬件映射清晰，50 年历史 |
| C++ | ✗ 拒绝 | ABI 不稳定 + 模板膨胀 + 隐式代码生成不可控 |
| Rust | ✓ 在 merge | ABI 也不稳定，但通过 `extern "C"` + `#[repr(C)]` 隔离在 C ABI 边界 |

### Rust 和 C++ 的关键区别

C++ 和 Rust 都没有自己的稳定 ABI。但 Rust **承认这一点**，设计上就提供了 `extern "C"` 作为一等公民的互操作机制。C++ 则倾向于假设"整个程序用同一个编译器编译"。

内核对 Rust 的接纳不是因为它有稳定 ABI，而是因为它的设计哲学能和 C ABI 干净地互补：

```
C ABI 是操作系统的公共接口语言。
C 负责这个接口本身。
Rust 负责接口背后的复杂逻辑 (内存安全, 零开销抽象)。
它们不互相替代, 各司其职。
```

---

## 8. 内核源码中的相关定义

```c
/* include/linux/sched.h — extern 函数声明 */
extern long schedule_timeout(long timeout);
extern void wake_q_add(struct wake_q_head *head, struct task_struct *task);
extern void wake_up_q(struct wake_q_head *head);

/* include/linux/sched/wake_q.h — 纯 C ABI 结构体 */
struct wake_q_head {
    struct wake_q_node *first;         // 指针 = C 的 ABI 概念
    struct wake_q_node **lastp;        // 二级指针 = C 的 ABI 概念
};

/* kernel/sched/core.c — 函数定义 */
void wake_up_q(struct wake_q_head *head)
{
    struct wake_q_node *node = head->first;
    while (node != WAKE_Q_TAIL) {
        struct task_struct *task;
        task = container_of(node, struct task_struct, wake_q);
        node = node->next;
        WRITE_ONCE(task->wake_q.next, NULL);
        wake_up_process(task);
        put_task_struct(task);
    }
}
```

所有这些类型和函数调用都在 C ABI 的框架内运作。没有这个 ABI，内核 C 代码、汇编代码、Rust 代码之间无法互操作。
