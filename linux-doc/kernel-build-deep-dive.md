# Linux 内核与外部模块构建原理

本文以 `vendor-module/kernel/hello` 为例，解释 `bzImage`、`.ko`、Kbuild、
`obj-y`/`obj-m` 以及构建后的校验方法。

## 阅读路线

请按下面顺序阅读，避免把“构建流程”和“底层实现细节”混在一起：

1. **先建立产物概念**：第 1～4 节说明 `bzImage`、`.config`、`vmlinux`、
   `System.map`、`Module.symvers` 和 `obj-y/obj-m`。
2. **再看 hello 主流程**：第 5～7 节说明内核配置、内核编译、模块编译、
   QEMU 启动和 AOSP 对应关系。
3. **再看构建细节**：第 8～13 节解释 `System.map`、`Module.symvers`、
   C 的预处理、编译、汇编、ELF 和链接。
4. **动手验证与排错**：第 14～19 节提供可复制的 hello 检查命令和错误处理。

主线可以先记成：

```text
配置 .config → 编译 vmlinux/bzImage → Kbuild 编译 hello.ko
→ VM 启动同一 bzImage → insmod hello.ko → dmesg 验证
```

## 1. `bzImage` 是什么

`bzImage` 是 x86 Linux 的可启动内核镜像（历史名称来自 boot protocol，
并不表示使用 bzip2 压缩）。它包含启动代码、解压/加载逻辑和压缩后的 Linux
内核，QEMU 或真实 bootloader 可以把它加载到内存并跳转到内核入口。

本项目使用：

```text
build/linux-out/arch/x86/boot/bzImage
```

QEMU 的 `-kernel bzImage` 只负责启动这个内核；它不会自动包含外部 `hello.ko`。
`.ko` 必须放入 rootfs，系统启动后再由 `insmod` 加载。

相关产物：

| 文件 | 用途 |
|---|---|
| `bzImage` | 启动内核，通常不含调试符号 |
| `vmlinux` | 未压缩 ELF，含符号，供 GDB/KGDB 使用 |
| `.config` | 本次构建的功能开关 |
| `System.map` | 内核符号地址表 |
| `Module.symvers` | 导出符号及 CRC，供外部模块检查依赖 |

## 2. 是否需要校验

需要。至少校验四件事：

```bash
test -s build/linux-out/arch/x86/boot/bzImage
file build/linux-out/arch/x86/boot/bzImage
make -C linux-source O=build/linux-out kernelrelease
modinfo build/vendor-module/hello/hello.ko | grep -E '^(name|vermagic|depends):'
```

运行环境还要再次确认：

```bash
uname -r
cat /proc/version
```

`.ko` 的 `vermagic` 应与 VM 的内核版本、SMP/抢占等关键配置匹配。宿主机
内核不匹配时会得到 `Invalid module format`，这是版本/配置校验失败，不是
`insmod` 命令本身的问题。

更严格的模块签名校验由 `CONFIG_MODULE_SIG*` 控制；启用强制签名后，未签名
的学习模块会被拒绝。项目默认用于 QEMU 学习环境，不依赖签名机制。

## 3. 内核和 `.ko` 是不是同一种编译

二者都使用同一套 Linux Kbuild、同一个编译器和内核配置，但产物链接方式不同：

```text
内核源码 + obj-y → .o → built-in.a → vmlinux → bzImage
外部模块 + obj-m → .o → .ko（可运行时加载）
```

编译外部模块时：

```bash
make -C linux-source \
  O=build/linux-out \
  M=build/vendor-module/hello modules
```

`-C` 指定内核源码树，`O=` 指定内核输出树，`M=` 指定模块目录。Kbuild 会
复用内核的头文件、编译选项、架构定义、`Module.symvers` 和 modpost 检查，
因此模块不能脱离对应的内核构建目录随意用 GCC 编译。

## 4. `obj-y` 与 `obj-m`

它们是 Kbuild 的链接策略：

```make
obj-y := foo.o   # 编译并链接进 vmlinux，启动时已存在
obj-m := foo.o   # 编译为 foo.ko，运行时 insmod
```

内核源码目录通常通过上层 `Makefile/Kconfig` 决定使用哪一种；外部模块的
`Kbuild` 直接写 `obj-m := hello.o`。选择 `obj-m` 的原因是 demo 需要演示
模块生命周期：

```text
insmod hello.ko → hello_init()
rmmod hello     → hello_exit()
```

`obj-y` 没有独立 `.ko`，不能用 `rmmod` 卸载；它适合启动必需的核心代码。

## 5. `hello` 的完整链路

```text
hello.c + Kbuild
    ↓ make M=...
hello.ko
    ↓ 复制到 rootfs
/root/modules/hello.ko
    ↓ QEMU 启动 bzImage
自定义内核执行 /init
    ↓ insmod
hello_init() → dmesg
```

执行：

```bash
./go.sh kernel
cd vendor-module/kernel/hello
make
./run.sh build
```

手动交互时，必须在 `zslmxd@virtme-ng` 内执行：

```bash
sudo insmod ~/26work/linux-vendor/build/vendor-module/hello/hello.ko
dmesg | grep 'hello:'
sudo rmmod hello
```

## 6. 什么时候需要重新编译

- 只改 `hello.c`：重新执行 demo 的 `make` 即可。
- 改了内核头文件、配置或导出符号：先 `./go.sh kernel`，再编译模块。
- 改了架构、编译器或关键配置：建议删除对应输出目录后全量构建。
- 看到 `vermagic` 不匹配：确认模块使用的 `O=` 与正在运行的 `bzImage`
  来自同一次内核构建。

## 7. 与 AOSP 自定义内核的关系

当前工程和 AOSP 使用的是同一套 Linux 内核/Kbuild 基础模型：

```text
内核源码 + 配置 → 内核镜像
内核源码 + 同一配置 → .ko 模块
```

区别在于目标平台和工程封装：

| 当前工程 | AOSP/Android |
|---|---|
| x86_64 | 通常 ARM64 |
| `bzImage` | `Image`/`Image.gz`/`Image.lz4` |
| QEMU 直接加载内核 | Bootloader 加载 `boot.img`/GKI |
| BusyBox rootfs | system/vendor/product 分区 |
| `build/linux-out/.config` | Kleaf/Bazel 或厂商脚本生成的配置 |
| `.ko` 放入 rootfs 后 `insmod` | `.ko` 通常打包到 `vendor_dlkm`/`system_dlkm` |

Android 的 GKI（Generic Kernel Image）将通用内核与厂商驱动分离。厂商模块
仍然是 Linux `.ko`，但必须匹配内核版本、架构、配置、`Module.symvers`、
GKI KMI 和模块签名；否则会出现 `Invalid module format`、`Unknown symbol`
或 `Required key not available`。

因此当前 `hello.ko` 不能直接复制到 Android 设备：它面向本项目的 x86_64
内核。要在 Android 上运行，必须使用 Android 目标内核配置和 ARM64 工具链
重新编译，再放入 `/vendor_dlkm/lib/modules/` 等分区，并通过 `adb shell`
中的 `modprobe`/`insmod` 加载。

两者的验证链路可以对应为：

```text
本项目：hello.ko → rootfs → QEMU → virtme-ng → dmesg
Android：厂商 .ko → vendor_dlkm.img → boot/GKI → adb shell → dmesg/logcat
```

本项目适合先学习 Kbuild、模块生命周期、`file_operations`、等待队列、
`poll/epoll` 和 Netlink；进入 AOSP 后再增加交叉编译、Device Tree、U-Boot、
Android init、vendor_dlkm、GKI/KMI、ABI 检查和模块签名等工程约束。真实设备
上的外设时序、功耗、性能和 SELinux/安全策略仍需在目标硬件验证。

## 8. `System.map` 是什么

`System.map` 是本次内核链接后生成的“符号地址表”，每行通常包含地址、
符号类型和名称：

```text
ffffffff81000000 T _text
ffffffff81012340 T start_kernel
```

它记录的是 `vmlinux` 中函数和全局符号的链接地址，主要用途是：

- 人工阅读内核地址和符号关系；
- 分析早期启动日志、Oops、栈回溯中的地址；
- 辅助 `kallsyms`、崩溃分析和调试工具定位符号。

`System.map` 不是启动镜像，也不是模块加载清单。运行中的内核通常通过
`CONFIG_KALLSYMS` 将类似信息压缩进内核，导出到 `/proc/kallsyms`；因此
`System.map` 更像离线参考副本。开启 KASLR 后，运行时地址可能会整体偏移，
不能简单把 `System.map` 地址直接当作当前运行地址。

## 9. `Module.symvers` 是什么

`Module.symvers` 是 Kbuild 在构建内核或模块时维护的导出符号数据库。它至少
包含符号名、CRC（当启用 `CONFIG_MODVERSIONS` 时）和导出类型，例如：

```text
0x12345678  some_kernel_api  vmlinux  EXPORT_SYMBOL
```

编译外部模块时，Kbuild 使用内核输出目录中的 `Module.symvers`：

```text
hello.ko
    ├── 需要调用的内核符号
    └── 与 Module.symvers 中的导出符号/CRC 对照
```

它解决两个问题：

1. `modpost` 检查模块引用的符号是否由内核导出，避免拼写错误或调用私有符号；
2. 使用符号 CRC 检查模块与内核的数据结构/API 版本是否兼容。

因此，`Module.symvers` 不是运行时必须被用户直接读取的文件，但它是“针对
哪个内核编译模块”的重要依据。只复制 `vmlinux` 或 `.config` 而没有对应的
`Module.symvers`，外部模块可能出现 `Unknown symbol` 或版本不一致。

模块自身构建目录也会生成一个 `Module.symvers`，记录该模块向其他模块导出
的符号；单个 `hello` 没有导出符号时通常内容很少。多个外部模块共享符号时，
可通过 `KBUILD_EXTRA_SYMBOLS` 额外提供其他模块的 `Module.symvers`。

## 10. 三种编译方式的共同点

普通 C 程序、Linux 内核和外部 `.ko` 最终都经历：

```text
C 源文件 → 预处理 → 编译 → 汇编 → ELF 目标文件 → 链接
```

都会使用 GCC/Clang、汇编器、链接器和 ELF 格式，也都可能包含多个 `.c` 文件
和头文件。但“链接目标”和“可用运行环境”不同，决定了它们不能互相替代。

## 11. 普通 C 程序 vs 内核 vs `.ko`

| 项目 | 普通 C 程序 | Linux 内核 | 外部 `.ko` |
|---|---|---|---|
| 入口 | `main()` | `start_kernel()` 等内核入口 | `module_init()` 注册的初始化函数 |
| 链接目标 | 用户态 ELF，可执行文件 | `vmlinux`，再生成 `bzImage` | 可重定位内核模块 ELF |
| 运行位置 | 用户态、受 MMU/系统调用保护 | 特权态、直接管理硬件 | 被加载后运行在内核态 |
| libc | 通常链接 glibc/musl | 不能使用 libc，使用内核 API | 同样不能使用 libc |
| 内存 | `malloc/free` | `kmalloc/kfree`、页分配器 | `kmalloc/kfree` 等内核分配器 |
| 错误处理 | 返回值、`errno`、异常（C++） | 返回负 errno，不能抛异常 | 返回负 errno，加载失败回滚 |
| 符号解析 | 链接器解析 libc/共享库 | 内核链接阶段解析 | 加载时解析内核导出符号 |
| 生命周期 | 进程启动到退出 | 启动后持续运行 | `insmod` 到 `rmmod` |
| 调试 | `gdb program` | `gdb vmlinux` + KGDB/QEMU | `gdb vmlinux` + 模块符号 |

普通程序的链接结果通常可以直接执行：

```bash
gcc -Wall -O2 hello.c -o hello
./hello
```

它由 ELF loader 映射到用户空间，通过动态链接器加载 libc。内核不能这样
编译，因为内核启动时还没有用户空间、文件系统和 libc。

## 12. 内核本体与 `.ko` 的链接差异

内核本体需要把大量对象合并成一个可启动映像：

```text
多个 .o
  ↓ 各目录 built-in.a
vmlinux（完整内核 ELF）
  ↓ 压缩、加入启动头
bzImage（x86 启动镜像）
```

内核链接脚本会安排 `.text`、`.rodata`、`.data`、`.bss`、初始化段等位置，
并生成 `System.map`、`kallsyms` 等符号信息。

模块则必须保持可重定位，不能假设自己固定在某个地址：

```text
hello.o + hello.mod.o
  ↓ modpost 检查符号和 CRC
hello.ko
  ↓ insmod/modprobe 加载
内核模块加载器分配地址并完成重定位
```

因此 `.ko` 不是一个可以直接执行的普通 ELF 程序，也不能用 `./hello.ko`
启动。它由内核模块加载器映射到内核地址空间，并调用注册的初始化函数。

## 13. 为什么不能用普通 GCC 编译 `.ko`

下面的命令只能生成用户态程序：

```bash
gcc hello.c -o hello
```

即使把输出文件命名成 `hello.ko`，它仍然缺少：

- 内核头文件和架构定义；
- `__KERNEL__` 等内核编译宏；
- 内核专用编译选项和栈保护策略；
- `Module.symvers` 符号 CRC；
- `modpost` 未定义符号检查；
- 模块元数据（`vermagic`、license、alias）；
- 可被内核加载器处理的模块段和重定位信息。

正确方式是复用目标内核的 Kbuild：

```bash
make -C linux-source \
  O=build/linux-out \
  M=build/vendor-module/hello \
  modules
```

这样模块使用与内核一致的头文件、配置、工具链和符号数据库。

## 14. 实践：用 `hello` 逐项校验（可直接复制）

下面假设当前目录是项目根目录。每一步都对应一个明确问题。

### 14.1 校验内核配置是否支持模块

```bash
grep '^CONFIG_MODULES=' build/linux-out/.config
```

必须输出：

```text
CONFIG_MODULES=y
```

如果没有输出或值为 `n`，内核没有模块加载能力；`hello.ko` 即使编译成功也
无法 `insmod`。

### 14.2 校验启动镜像确实存在

```bash
test -s build/linux-out/arch/x86/boot/bzImage && \
file build/linux-out/arch/x86/boot/bzImage
```

`test -s` 确认文件非空，`file` 应识别为 x86 Linux kernel 启动镜像。它是
QEMU 启动的文件，不是模块，也不能用 `insmod`。

### 14.3 校验内核发布版本

```bash
make -C linux-source O=build/linux-out kernelrelease
```

例如输出：

```text
7.2.0-g66498c75b4f8
```

这个字符串稍后必须与 VM 内 `uname -r` 一致。

### 14.4 用内核 Kbuild 编译 `hello.ko`

```bash
make -C vendor-module/kernel/hello clean
make -C vendor-module/kernel/hello
test -s build/vendor-module/hello/hello.ko
```

这里的 `make` 不是普通用户态 GCC 编译，而是执行：

```bash
make -C linux-source O=build/linux-out \
     M=build/vendor-module/hello modules
```

确认模块元数据：

```bash
modinfo build/vendor-module/hello/hello.ko | \
    grep -E '^(filename|name|vermagic|depends|license):'
```

应看到 `name: hello`、`license: GPL`，并看到类似：

```text
vermagic: 7.2.0-g66498c75b4f8 SMP preempt mod_unload
```

### 14.5 在正确的自定义内核 VM 中检查版本

从宿主机启动：

```bash
./lib/vm/virtme-ng/01-run.sh
```

进入 `zslmxd@virtme-ng` 后执行：

```bash
uname -r
cat /proc/version
```

`uname -r` 必须等于 14.3 的 `kernelrelease`。如果显示宿主机版本（例如
`7.0.0-28-generic`），不要加载模块，否则会得到 `Invalid module format`。

### 14.6 在 VM 内加载并验证 `hello`

```bash
sudo insmod ~/26work/linux-vendor/build/vendor-module/hello/hello.ko && \
    dmesg | grep 'hello:'
```

成功时应看到：

```text
hello: loading out-of-tree module taints kernel.
hello: module loaded
```

第一行是正常的“树外模块”提示，第二行说明 `hello_init()` 已执行。

确认模块确实存在于当前 VM 内核：

```bash
lsmod | grep '^hello '
```

最后验证退出路径：

```bash
sudo rmmod hello && dmesg | grep 'hello:'
```

应新增：

```text
hello: module unloaded
```

这说明 `hello_exit()` 已执行，模块生命周期完整闭环。

### 14.7 `System.map` 和 `Module.symvers` 在这个例子中的位置

```bash
grep -w 'start_kernel' build/linux-out/System.map
grep -w 'module_layout' build/linux-out/Module.symvers
```

第一条是在 `System.map` 中查找内核启动符号，用于离线地址分析；第二条是在
`Module.symvers` 中查找模块加载所需的内核模块基础符号。它们不需要复制到
rootfs，但必须来自编译 `bzImage` 的同一个 `build/linux-out`。

最终的可靠条件是：

```text
build/linux-out/.config 的 CONFIG_MODULES=y
build/linux-out/bzImage 能启动
hello.ko 的 vermagic = VM 的 uname -r
hello.ko 由同一 build/linux-out 的 Kbuild 生成
insmod 后 dmesg 出现 module loaded
```

## 15. 附录：从 `hello.c` 到 `hello.ko` 的 C 编译细节

第 3 节给出了内核与模块的整体关系；本节只展开编译器和链接器的内部阶段。
如果你的目标是先跑通 demo，可以跳过本节，直接阅读第 19 节排错。

无论是普通 C 程序、Linux 内核还是外部模块，底层都经过类似阶段：

```text
.c/.h → 预处理 → .i → 编译 → .s → 汇编 → .o → 链接 → 最终 ELF
```

### 15.1 预处理：展开头文件和宏

```bash
gcc -E hello.c -o hello.i
```

预处理器处理 `#include`、`#define`、`#if`，删除注释并展开头文件。`.h`
不会独立生成 `.o`；它会被复制进每个引用它的 `.c`。内核构建还会加入
`__KERNEL__`、架构宏和由 `.config` 生成的配置头文件。

### 15.2 编译：生成汇编

```bash
gcc -S hello.i -o hello.s
```

编译器检查 C 语法和类型，执行优化，将 C 转换为目标架构汇编。此时外部
函数可以只有声明，函数地址留到链接或模块加载阶段解决。

### 15.3 汇编：生成 ELF `.o`

```bash
gcc -c hello.c -o hello.o
```

`.o` 是 ELF relocatable 文件，不是可执行文件，包含 `.text`、`.rodata`、
`.data`、`.bss`、符号表和重定位表。可以查看：

```bash
file hello.o
readelf -S hello.o
nm hello.o
readelf -r hello.o
```

符号 `U foo` 表示本文件引用但未定义 `foo`；符号 `T func` 表示本文件在
代码段定义了 `func`。

### 15.4 链接：解决多个文件之间的引用

普通 C 程序示例：

```bash
gcc -c main.c -o main.o
gcc -c counter.c -o counter.o
gcc main.o counter.o -o app
```

链接器会合并多个 `.o` 的节，查找每个未定义符号的唯一实现，应用重定位，
安排最终地址和入口。如果只有声明没有实现，会报 `undefined reference`；
多个文件都定义同名全局符号，会报 `multiple definition`。

头文件依赖关系是：

```text
main.c ──include──> counter.h
counter.c ──include─> counter.h
```

实际编译为：

```text
main.c + counter.h → main.o
counter.c + counter.h → counter.o
main.o + counter.o → app
```

修改 `counter.c` 通常只需重编 `counter.o`；修改 `counter.h` 则所有包含它的
`.c` 都应重编。Make/Kbuild 通过 `.d` 依赖文件和时间戳实现增量编译。

## 16. 三种目标的链接区别

### 16.1 普通 C 程序

```text
main.c/other.c → main.o/other.o → 链接 libc 和启动文件 → app
```

最终是用户态可执行 ELF，有 `main()` 入口，由 ELF loader 映射到用户地址空间，
通常还会由动态链接器加载 `libc.so`。可以直接运行：

```bash
./app
```

### 16.2 Linux 内核

```text
各目录 .c → .o（obj-y）→ built-in.a → vmlinux → bzImage
```

`vmlinux` 是完整内核 ELF，使用内核专用链接脚本安排代码段、数据段和启动
入口；`bzImage` 是 x86 启动封装后的镜像，QEMU/bootloader 负责加载它。内核
不链接 glibc，也没有用户态 `main()`。

### 16.3 外部 `hello.ko`

```text
hello.c → hello.o → hello.mod.o → modpost → hello.ko
```

`hello.ko` 仍是 ELF，但保持可重定位，包含模块元数据和初始化/退出入口。它
不能直接执行，必须由内核模块加载器完成地址分配、重定位和内核符号解析：

```text
insmod hello.ko → hello_init()
rmmod hello     → hello_exit()
```

## 17. `hello` 的多个文件依赖如何处理

当前 `hello` 只有一个实现文件和一个 Kbuild 文件：

```text
hello.c
  └── Kbuild: obj-m := hello.o
```

如果拆成多个源文件，可写成：

```make
obj-m := hello.o
hello-y := hello_main.o hello_proc.o
```

含义是先分别编译 `hello_main.c`、`hello_proc.c`，再把它们组合成一个
`hello.o`，最后生成 `hello.ko`。这与普通程序的 `main.o + counter.o` 类似，
但最终链接由内核 Kbuild 完成，且会经过 `modpost`。

模块引用内核函数时，链接器不会像普通程序那样链接 `libc`；Kbuild 使用
`Module.symvers` 检查符号，模块加载器在 `insmod` 时把引用绑定到内核导出的
地址。找不到导出符号会得到 `Unknown symbol`。

## 18. 用工具观察真实产物

```bash
file build/vendor-module/hello/hello.ko
readelf -h build/vendor-module/hello/hello.ko
readelf -S build/vendor-module/hello/hello.ko
nm build/vendor-module/hello/hello.ko | grep hello
readelf -r build/vendor-module/hello/hello.ko
```

对照普通程序：

```bash
gcc -Wall -O2 hello.c -o /tmp/hello-user
file /tmp/hello-user
readelf -h /tmp/hello-user
```

普通程序有用户态程序入口和 libc 依赖；`hello.ko` 有模块入口、`vermagic` 和
内核重定位信息。两者都使用 ELF，但运行加载者不同：前者由用户态 ELF loader
加载，后者由 Linux 内核模块加载器加载。

## 19. 排错：常见失败与处理方法（以 hello 为例）

### 内核配置/编译失败

- `No rule to make target`、`include/generated/... not found`：输出目录没有配置完整。重新执行 `make -C linux-source O=build/linux-out x86_64_defconfig` 和 `olddefconfig`。
- BTF、pahole 或编译器缺失：运行 `./go.sh deps`，检查 `command -v gcc make pahole`。
- 编译被 OOM 杀死：降低并行度，例如 `make -C linux-source O=build/linux-out -j2`。
- 磁盘不足：`df -h build`；使用 `./go.sh clean` 清理 demo/rootfs。

### 普通 C 程序失败

- `fatal error: xxx.h`：头文件目录错误，检查 `-I`；用 `gcc -E -H` 查看实际包含路径。
- `undefined reference to foo`：漏了实现文件或库；用 `nm foo.o` 检查定义，并把库放在使用它的目标文件之后。
- `multiple definition of foo`：多个文件定义同名全局符号；头文件只放声明，实现放一个 `.c`，内部符号使用 `static`。
- `implicit declaration`：调用前缺少头文件；使用 `-Wall -Wextra -Werror` 提前发现。

### hello.ko 编译失败

- `linux/module.h not found`：错误地用普通 GCC 编译。必须执行 `make -C vendor-module/kernel/hello`，让 Kbuild 提供内核头文件和宏。
- `modpost: "foo" undefined`：模块引用了未导出的内核符号。检查 `grep -w foo build/linux-out/Module.symvers`；必要时使用 `EXPORT_SYMBOL(foo)` 后重新编译内核。
- `disagrees about version of symbol`：模块使用的 `.config`、`Module.symvers` 或头文件与内核不一致。执行 `./go.sh kernel`，再执行 `make -C vendor-module/kernel/hello clean all`。

### insmod 加载失败

- `Invalid module format`：通常是在宿主机加载，或 `vermagic` 与 VM 不同。比较 `modinfo build/vendor-module/hello/hello.ko | grep vermagic` 和 VM 内 `uname -r`。
- `Unknown symbol`：模块依赖的函数未导出，或运行的 `bzImage` 不是同一 `build/linux-out` 生成的版本。
- `Required key not available`：内核强制模块签名；检查 `CONFIG_MODULE_SIG*`，产品环境应使用正确密钥签名。
- `Operation not permitted`：当前 Shell 没有权限，确认已进入 `zslmxd@virtme-ng` 并使用 `sudo insmod`。

### QEMU 启动/日志失败

- `could not load kernel`：检查 `test -s build/linux-out/arch/x86/boot/bzImage` 和 `file .../bzImage`。
- 找不到模块：确认 `build/vm-rootfs/root/modules/hello.ko` 存在，并重新执行 `./run.sh build` 制作镜像。
- 没有 `hello: module loaded`：先看 `dmesg | tail -30` 的 `insmod` 错误，再检查 `CONFIG_MODULES=y`、`vermagic` 和运行内核版本。

排查原则：先确定失败层级（配置 → 编译 → 链接 → modpost → 模块加载 → QEMU），再处理第一条真实错误；最后的 `make: Error 2` 只是汇总。
