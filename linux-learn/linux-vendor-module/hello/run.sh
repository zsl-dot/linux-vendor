#!/bin/bash
# 一键运行 hello 内核模块验证流程
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/hello-run.log"
CMD="${1:-build}"

echo "=== hello 模块验证流程 ==="

# 1. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/5] 全量编译 hello..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/5] 增量编译 hello..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 2. 复制 .ko 到 rootfs
echo "[2/5] 复制 hello.ko 到 rootfs..."
mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/hello/hello.ko" "$ROOTFS_DIR/root/modules/"

# 3. 写入测试用 init
echo "[3/5] 准备测试 init..."
cp "$ROOTFS_DIR/init" "$ROOTFS_DIR/init.bak"
sed -i '/^exec \/bin\/sh$/d' "$ROOTFS_DIR/init"
cat >> "$ROOTFS_DIR/init" << 'TESTEOF'

# Auto-load hello module if available
if [ -f /root/modules/hello.ko ]; then
	echo "=== Hello module test ==="
	insmod /root/modules/hello.ko
	dmesg | grep 'hello:'
	echo "=== Hello done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 4. 创建 rootfs.img
echo "[4/5] 创建 rootfs.img..."
dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=128 status=none
mke2fs -q -d "$ROOTFS_DIR" "$ROOTFS_IMG" 2>/dev/null

# 5. 启动 VM
echo "[5/5] 启动 VM 验证..."
timeout 12 qemu-system-x86_64 \
    -kernel "$KERNEL_IMAGE" \
    -append "root=/dev/vda rw console=ttyS0 init=/init nokaslr" \
    -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
    -device virtio-blk-pci,drive=drive0 \
    -m 1G -smp 2 \
    -netdev user,id=net0 -device virtio-net-pci,netdev=net0 \
    -display none -serial file:"$LOG" \
    -no-reboot 2>&1 || true

# 恢复原始 init
mv "$ROOTFS_DIR/init.bak" "$ROOTFS_DIR/init"

echo ""
echo "--- VM 输出 ---"
grep -E 'Hello module|hello:|Hello done' "$LOG"
