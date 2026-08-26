# Linux 内核学习实践

## 环境要求

- **宿主机**: Ubuntu 22.04+ / Debian 12+ (x86_64)
- **工具**: `gcc clang make git qemu-system-x86_64 busybox e2fsprogs`
- **磁盘**: ~5GB (内核源码 3GB + 编译产物 2GB)

## 共享路径配置

项目路径统一定义在 `lib/workflow_config.py`。所有 `linux-learn` 下的
运行脚本都会加载它；如需改变内核源码、编译输出或根文件系统的位置，只修改
此文件中的 `BUILD_ROOT`、`KERNEL_SRC`、`KERNEL_OUT`、`ROOTFS_DIR`、`ROOTFS_IMG`
和 `LOG_DIR`。

## 快速开始

### 1. 准备内核源码

```bash
# 在本目录的父目录下放置内核源码，结构如下:
#   <项目根目录>/
#   ├── linux-source/  ← 内核源码
#   ├── build/         ← 所有编译、QEMU、日志产物 (自动生成)
#   └── linux-learn/   ← 本目录

cd ..  # 到 linux-learn 的父目录

# 完整 clone GitHub Fork（含完整历史）
git clone git@github.com:mxd-dot/linux.git linux-source

# 编译内核
make -C linux-source O=../build/linux-out x86_64_defconfig
make -C linux-source O=../build/linux-out -j$(nproc)
```

### 2. 运行第一个 demo

```bash
cd linux-learn/linux-vendor-module/hello
./run.sh build
```

`run.sh` 支持两个子命令：
- `build` — 全量编译 + VM 验证
- `update` — 增量编译 + VM 验证

## 目录结构

```
<项目目录>/
├── linux-source/      ← 内核源码 (自行 clone)
├── build/             ← 所有可再生成产物
│   ├── linux-out/     ← 内核 bzImage、vmlinux、.config
│   ├── linux-learn/   ← 模块、BPF 和用户态 demo
│   ├── vm-rootfs/     ← QEMU 根文件系统目录
│   ├── vm-rootfs.img  ← QEMU 根文件系统镜像
│   └── logs/          ← QEMU 与 demo 运行日志
└── linux-learn/       ← 本目录
    ├── env.sh
    ├── README.md
    └── linux-vendor-module/
        ├── hello/          ← Demo 1
        ├── binder-demo/    ← Demo 2
        ├── netlink-demo/   ← Netlink 双向通信
        ├── bpflib/         ← eBPF 共享库
        ├── ebpf-demo1/     ← Demo 3
        └── ebpf-demo2/     ← Demo 4
```

项目根目录中的 `go.sh` 是唯一总入口，`lib/workflow_config.py` 和 `lib/` 是它加载的配置与功能模块。

`lib/vm/` 只保存受版本控制的 QEMU/rootfs 构建脚本与 `init` 模板；其生成结果统一写入 `build/vm-rootfs/`、`build/vm-rootfs.img` 和 `build/logs/`。

> `build/vm-rootfs/` 与 `build/vm-rootfs.img` 是编译产物，由 `lib/vm/mk-rootfs.sh` 首次运行 demo 时自动生成。

## Demo 列表

| # | 目录 | 类型 | 说明 |
|---|------|------|------|
| 1 | `linux-vendor-module/hello/` | 内核模块 | 最简 Hello World 模块，insmod/rmmod |
| 2 | `linux-vendor-module/binder-demo/` | 内核模块 + IPC | Binder server 注册服务，client 发送消息 |
| 3 | `linux-vendor-module/netlink-demo/` | Netlink | 用户进程与内核模块的请求/响应通信 |
| 4 | `linux-vendor-module/ebpf-demo1/` | eBPF kprobe | 追踪 `execve()` 系统调用 |
| 5 | `linux-vendor-module/ebpf-demo2/` | eBPF kprobe | 追踪 `clone()` 系统调用 |

## 验证流程

每个 demo 的 `run.sh` 自动完成：

1. 编译源码 → 2. 复制到 rootfs → 3. 写入测试 init → 4. 构建 rootfs.img → 5. 启动 QEMU 验证

VM 输出通过 grep 提取关键日志打印到终端。

## 安装依赖

```bash
sudo apt install -y build-essential clang llvm gcc-multilib \
    qemu-system-x86 qemu-utils busybox-static e2fsprogs \
    git make flex bison libssl-dev libelf-dev bc cpio
```
