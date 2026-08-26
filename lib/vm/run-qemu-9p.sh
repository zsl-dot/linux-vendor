#!/bin/bash
# 使用 9P 共享目录启动 QEMU — 避免每次改代码重新打包 rootfs
# 用法: ./lib/vm/run-qemu-9p.sh [shared_dir]
#
# VM 内挂载:
#   mount -t 9p -o trans=virtio hostshare /mnt
#
# 然后直接在 /mnt 中访问宿主编译产物。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../linux-learn/env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs

SHARE_DIR="${1:-$ROOT_DIR}"

echo "=== 9P 共享目录启动 QEMU ==="
echo "共享目录: $SHARE_DIR"
echo ""
echo "VM 内执行:"
echo "  mount -t 9p -o trans=virtio hostshare /mnt"
echo "  ls /mnt"
echo ""

# 创建临时 rootfs.img（仅用于启动，文件通过 9P 访问）
dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=128 status=none
mke2fs -q -d "$ROOTFS_DIR" "$ROOTFS_IMG" 2>/dev/null

qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -append "root=/dev/vda rw console=ttyS0 init=/init nokaslr" \
    -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
    -device virtio-blk-pci,drive=drive0 \
    -m 1G -smp 4 \
    -virtfs local,path="$SHARE_DIR",mount_tag=hostshare,security_model=none \
    -display none -serial mon:stdio \
    -no-reboot
