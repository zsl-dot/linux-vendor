# hello：从内核编译到模块验证的完整流程

本文只围绕一个 demo：`vendor-module/kernel/hello/`。目标是回答：内核如何配置和编译，`hello.ko` 如何使用同一套 Kbuild 编译，以及它如何在 QEMU 自定义内核中加载。

## 0. 先理解四个目录

```text
linux-source/                         # 内核源码（输入）
build/linux-out/                     # 内核输出（.config、vmlinux、bzImage）
vendor-module/kernel/hello/          # hello 源码和 Kbuild（输入）
build/vendor-module/hello/           # hello.ko（输出）
```

`linux-source` 和 `vendor-module/kernel/hello` 是源码；`build/` 下的内容都可删除后重新生成。

## 1. 生成内核配置

```bash
mkdir -p build/linux-out
make -C linux-source O=build/linux-out x86_64_defconfig
```

`x86_64_defconfig` 根据 x86_64 默认选项生成 `build/linux-out/.config`。项目脚本随后用 `scripts/config` 打开模块、BPF、BTF 和 9P 等选项，再执行：

```bash
make -C linux-source O=build/linux-out olddefconfig
```

`olddefconfig` 会补齐配置依赖。确认模块能力：

```bash
grep '^CONFIG_MODULES=' build/linux-out/.config
# CONFIG_MODULES=y
```

## 2. 编译自定义内核

```bash
make -C linux-source O=build/linux-out -j"$(nproc)"
```

主要产物：

```text
build/linux-out/vmlinux                         # 完整 ELF，供 GDB 调试
build/linux-out/arch/x86/boot/bzImage           # QEMU 启动的 x86 内核镜像
build/linux-out/.config                         # 实际使用的配置
build/linux-out/System.map                      # 内核符号地址表
build/linux-out/Module.symvers                  # 导出符号和 CRC 数据库
```

`bzImage` 不是模块，它是可启动内核；QEMU 用 `-kernel bzImage` 将它加载到内存并启动。`vmlinux` 不直接用于启动，但包含调试符号。`System.map` 用于离线地址分析，`Module.symvers` 供外部模块的 `modpost` 检查符号和版本。

## 3. 查看 `hello` 的构建配置

`vendor-module/kernel/hello/Makefile`：

```make
include ../../common.mk
MODULE_SRC := hello.c
include ../../kernel-module.mk
```

`Kbuild`：

```make
obj-m := hello.o
```

`obj-m` 表示生成可运行时加载的 `hello.ko`；如果是 `obj-y`，代码会链接进 `vmlinux`，启动时已存在，不能用 `rmmod` 卸载。

执行：

```bash
make -C vendor-module/kernel/hello clean
make -C vendor-module/kernel/hello
```

内部等价于：

```bash
make -C linux-source \
  O=build/linux-out \
  M=build/vendor-module/hello \
  modules
```

`-C` 进入内核源码树，`O=` 指向已经配置和编译过的内核输出，`M=` 指向外部模块目录。Kbuild 复用内核头文件、架构选项、编译器参数、`Module.symvers` 和 `modpost` 检查，因此不能用普通 `gcc hello.c -o hello` 代替。

检查结果：

```bash
test -s build/vendor-module/hello/hello.ko
modinfo build/vendor-module/hello/hello.ko | grep -E '^(name|vermagic|license|depends):'
```

`vermagic` 必须和运行中的自定义内核匹配；否则加载时会报 `Invalid module format`。

## 4. 自动 QEMU 验证

```bash
cd vendor-module/kernel/hello
./run.sh build
```

脚本执行：

```text
编译 hello.ko
  ↓
复制到 build/vm-rootfs/root/modules/hello.ko
  ↓
修改临时 /init，加入 insmod 和 dmesg
  ↓
制作 build/vm-rootfs.img
  ↓
QEMU 加载 bzImage 和 rootfs.img
  ↓
/init 执行 insmod /root/modules/hello.ko
  ↓
hello_init() 输出 dmesg
```

日志保存于：

```text
build/logs/hello-run.log
```

## 5. 手动进入自定义内核验证

宿主机执行：

```bash
./lib/vm/virtme-ng/01-run.sh
```

看到 `zslmxd@virtme-ng` 后，确认：

```bash
uname -r
# 应与 make -C linux-source O=build/linux-out kernelrelease 相同
```

然后在 VM 内执行：

```bash
sudo insmod ~/26work/linux-vendor/build/vendor-module/hello/hello.ko
dmesg | grep 'hello:'
sudo rmmod hello
dmesg | grep 'hello:'
```

预期看到 `hello: module loaded` 和 `hello: module unloaded`。宿主机的 `uname -r` 与 VM 不同，不能在宿主机加载该 `.ko`。

## 6. 什么时候重新编译

```text
只改 hello.c                         → 重新 make 模块
修改内核头文件或导出符号              → 重新 ./go.sh kernel，再 make 模块
修改 .config、架构或编译器            → 重新生成配置并全量编译内核和模块
更换正在启动的 bzImage                → 模块必须用该 bzImage 对应的 build/linux-out 编译
```
