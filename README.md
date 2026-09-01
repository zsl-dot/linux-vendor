# Linux 内核学习与验证工作台

本项目围绕 Linux 内核源码、可运行学习 demo 和 QEMU 自动验证流程组织。内核源码通过 `linux-source/` Git 子模块引入，工作分支固定为 `work`；根仓库保存工作流、文档和 demo，不直接修改上游 `master`。

## 目录结构

- `linux-source/`：`zsl-dot/linux` 子模块，进行内核代码修改与编译。
- `vendor-module/`：统一管理内核模块、用户态程序及机制 demo（如 `wake_q_demo`、`wait_queue_demo`、`epoll-demo`）。
- `linux-doc/`：中文学习与开发文档的唯一入口。
- `lib/`：共享 Shell/Python 工作流代码；`lib/vm/` 存放 QEMU 和 rootfs 模板。
- `build/`：内核、demo、rootfs、镜像和日志等可再生成产物，已被 Git 忽略。
- `go.sh`：项目统一命令入口。

更完整的 QEMU/virtme-ng 原理、交互方式和适用场景见
[`linux-doc/linux-kernel-dev.md`](linux-doc/linux-kernel-dev.md)。

## VS Code 开发

使用 VS Code 打开仓库根目录即可浏览 `linux-source/`。建议安装
`clangd` 或 Microsoft C/C++ 扩展，然后运行命令面板中的
`Kernel: generate compile_commands`，即可启用内核头文件和符号跳转。
常用构建任务位于 `.vscode/tasks.json`。

## 快速开始

宿主机建议使用 Ubuntu 22.04+/Debian 12+（x86_64），并准备 GCC/Clang、Make、QEMU、BusyBox、e2fsprogs、Flex、Bison、OpenSSL 和 ELF 开发库。

```bash
git clone --recurse-submodules <仓库地址>
cd linux-vendor
./go.sh init       # 初始化子模块并切换 linux-source/work
./go.sh deps       # 检查或安装依赖
./go.sh            # 编译内核并在 QEMU 验证全部 demo
```

常用命令：

```bash
./go.sh kernel     # 仅编译内核，输出到 build/linux-out/
./go.sh demo       # 编译并验证全部 demo
./go.sh check      # 检查分支、子模块和仓库不变量
./go.sh status     # 查看内核子模块状态
./go.sh sync       # 同步上游并 rebase work（需干净工作区）
./go.sh clean      # 清理 demo、rootfs 和日志，保留内核产物
```

单个 demo 可在其目录执行 `./run.sh build`；运行日志位于 `build/logs/`。

## 验证与交互自定义内核

修改 `linux-source/work` 中的代码后，重新编译并验证：

```bash
cd linux-source && git switch work
# 编辑内核源码
cd ..
./go.sh kernel     # 生成 build/linux-out/arch/x86/boot/bzImage
./go.sh demo       # 在 QEMU 中验证全部 demo
```

每个 demo 的 `run.sh build` 会编译模块、制作 rootfs、启动 QEMU，并检查 `dmesg`。例如：

```bash
cd vendor-module/kernel/hello
./run.sh build
```

需要手动进入运行中的自定义内核时，使用 virtme-ng：

```bash
./lib/vm/virtme-ng/01-run.sh
```

在 VM 中可执行 `uname -a`、`dmesg`、`lsmod`、`insmod module.ko` 和 `rmmod module_name`；执行 `poweroff` 退出。Netlink 等用户态/内核态交互 demo 也可在对应目录运行 `./run.sh build`，日志统一写入 `build/logs/`。

若要一次准备多个模块并交互测试，可使用：

```bash
./lib/vm/virtme-ng/03-run-demos.sh hello hello-proc netlink-demo
```

脚本会先编译指定 demo，再进入 VM 并打印模块路径。等待 VM 提示符出现后，确认 `uname -r` 输出为项目自定义内核版本，再执行 `sudo insmod`；不要在宿主机执行该命令。默认不带参数时准备 `hello`、`hello-proc`、`netlink-demo` 和 `binder-demo`。

调试内核可使用 KGDB。终端一启动：

```bash
./lib/vm/virtme-ng/02-run-kgdb.sh
```

终端二连接：

```bash
gdb build/linux-out/vmlinux
(gdb) target remote :1234
(gdb) break __schedule
(gdb) continue
```

## 开发约定

内核代码遵循 Linux kernel coding style（Tab 缩进、K&R 花括号、`snake_case` 命名）；Shell 使用 Bash `set -euo pipefail`，共享路径统一放在 `lib/workflow_config.py`。内核提交在 `linux-source/work` 完成，根仓库随后提交子模块指针。

提交信息采用 Conventional Commit 风格，例如 `feat:`、`fix:`、`docs:`、`refactor:`、`chore:`。提交或 PR 应说明影响层（内核、工作流、demo 或文档）、执行的验证命令及相关 QEMU/日志结果；不要提交 `build/` 产物。
