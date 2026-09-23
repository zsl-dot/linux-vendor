# 共享环境配置 — 所有 demo 的 run.sh 都会 source 此文件
# 用法: source "$(dirname "$0")/../../env.sh"

set -euo pipefail

# 目录结构:
#   ROOT_DIR/               ← 项目根目录（vendor-module 的父目录）
#   ├── linux-source/       ← 内核源码
#   ├── build/              ← 所有可再生成产物（内核、demo、rootfs、日志）
#   └── vendor-module/      ← 本目录
#       ├── env.sh          ← 本文件
#       ├── kernel/         ← 内核模块 / eBPF / 容器等 QEMU 验证 demo
#       └── model/          ← 用户态机制模拟 demo

# SCRIPT_DIR 由调用方设置（demo 的 run.sh 所在目录）
CONFIG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
eval "$(python3 "$CONFIG_ROOT/lib/workflow_config.py" shell)"
LEARN_DIR="$VENDOR_MODULE_DIR"
ROOT_DIR="$PROJECT_ROOT"
mkdir -p "$LOG_DIR"

# ---- 内核源码检查 ----
check_kernel_source() {
    if [ ! -d "$KERNEL_SRC/include" ]; then
        cat << 'EOF'
========================================
  错误: 未找到内核源码

  请将内核源码放在: <项目目录>/linux-source/

  获取方式:
    python3 lib/linux_fork_workflow.py init

  完整环境搭建请参考 README.md
========================================
EOF
        exit 1
    fi
}

# ---- 所有 demo 编译产物统一输出目录 ----
# ---- 内核编译产物检查 ----
KERNEL_IMAGE="$KERNEL_OUT/arch/x86/boot/bzImage"

check_kernel_build() {
    if [ ! -f "$KERNEL_IMAGE" ]; then
        cat << 'EOF'
========================================
  错误: 未找到编译好的内核 (bzImage)

  请在项目根目录执行:

    ./go.sh kernel

  内核配置由 lib/kernel.sh 维护；手动 make x86_64_defconfig
  会缺少项目特性（BTF/sched_ext/容器化/DRM 等）导致 demo 失败。
========================================
EOF
        exit 1
    fi
}

# ---- rootfs 路径（编译产物，在 vendor-module 外部） ----
ROOTFS_MKSCRIPT="$PROJECT_ROOT/lib/vm/mk-rootfs.sh"

ensure_rootfs() {
    if [ ! -f "$ROOTFS_DIR/bin/busybox" ]; then
        echo "[env] 根文件系统不存在，自动生成..."
        "$ROOTFS_MKSCRIPT"
    fi
}

# ---- QEMU 加速参数：宿主机有 /dev/kvm 就用 KVM，否则回退 TCG ----
# 新内核（ftrace/BTF/sched_ext + 启动期 KUnit）在 TCG 下启动要几十秒，
# KVM 下 2~4 秒；无 KVM 的环境（容器/CI）靠各 demo 的超时余量兜底。
qemu_accel_flags() {
    if [ -w /dev/kvm ]; then
        echo "-enable-kvm -cpu host"
    fi
}

# ---- init 测试注入（各 demo 共享，替代 run.sh 里的三段样板）----
# inject_init_test 从 stdin 读取测试块追加到 rootfs 的 init（去掉结尾的
# exec /bin/sh，由测试块自行补上）。备份原 init 并注册退出自动恢复，
# run.sh 中途失败也不会把测试 init 留在 rootfs 里。
inject_init_test() {
    cp "$ROOTFS_DIR/init" "$ROOTFS_DIR/init.bak"
    trap restore_init EXIT
    sed -i '/^exec \/bin\/sh$/d' "$ROOTFS_DIR/init"
    cat >> "$ROOTFS_DIR/init"
}

restore_init() {
    if [ -f "$ROOTFS_DIR/init.bak" ]; then
        mv "$ROOTFS_DIR/init.bak" "$ROOTFS_DIR/init"
    fi
}

# ---- 统一 QEMU 验证入口 ----
# 调用方需先准备 ROOTFS_DIR/init 和 ROOTFS_IMG；日志路径作为第一个参数，
# 可选第二个参数覆盖超时时间。
run_qemu() {
    local log="$1" timeout_sec="${2:-120}"
    mkdir -p "$(dirname "$log")"
    dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=128 status=none
    mke2fs -q -d "$ROOTFS_DIR" "$ROOTFS_IMG" 2>/dev/null
    timeout "$timeout_sec" qemu-system-x86_64 $(qemu_accel_flags) \
        -kernel "$KERNEL_IMAGE" \
        -append "root=/dev/vda rw console=ttyS0 init=/init nokaslr" \
        -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
        -device virtio-blk-pci,drive=drive0 \
        -m 1G -smp 2 -display none -serial "file:$log" -no-reboot 2>&1 || true
}
