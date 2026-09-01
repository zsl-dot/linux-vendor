# 共享环境配置 — 所有 demo 的 run.sh 都会 source 此文件
# 用法: source "$(dirname "$0")/../../env.sh"

set -euo pipefail

# 目录结构:
#   ROOT_DIR/               ← 项目根目录（vendor-module 的父目录）
#   ├── linux-source/       ← 内核源码
#   ├── build/              ← 内核编译产物
#   └── vendor-module/        ← 本目录
#       ├── env.sh           ← 本文件
#       ├── demo/              ← 可运行学习 demo
#       └── vm/

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

  请先在 <项目目录>/build/linux-out/ 中编译内核:

    cd <项目目录>/linux-source
    make O=../build/linux-out x86_64_defconfig
    make O=../build/linux-out -j$(nproc)

  完整环境搭建请参考 README.md
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

# ---- 统一 QEMU 验证入口 ----
# 调用方需先准备 ROOTFS_DIR/init 和 ROOTFS_IMG；日志路径作为第一个参数，
# 可选第二个参数覆盖超时时间。
run_qemu() {
    local log="$1" timeout_sec="${2:-12}"
    mkdir -p "$(dirname "$log")"
    dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=128 status=none
    mke2fs -q -d "$ROOTFS_DIR" "$ROOTFS_IMG" 2>/dev/null
    timeout "$timeout_sec" qemu-system-x86_64 \
        -kernel "$KERNEL_IMAGE" \
        -append "root=/dev/vda rw console=ttyS0 init=/init nokaslr" \
        -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
        -device virtio-blk-pci,drive=drive0 \
        -m 1G -smp 2 -display none -serial "file:$log" -no-reboot 2>&1 || true
}
