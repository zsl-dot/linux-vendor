#!/bin/bash
# Build the Netlink module/client and verify request/reply in QEMU.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs

LOG="$LOG_DIR/netlink-demo-run.log"
CMD="${1:-build}"

if [ "$CMD" = "build" ]; then
	make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
	make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
	make -C "$SCRIPT_DIR"
else
	echo "Usage: $0 [build|update]"
	exit 1
fi

mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/netlink-demo/netlink_demo.ko" "$ROOTFS_DIR/root/modules/"
cp "$LEARN_OUT/netlink-demo/netlink-client" "$ROOTFS_DIR/bin/"

cp "$ROOTFS_DIR/init" "$ROOTFS_DIR/init.bak"
sed -i '/^exec \/bin\/sh$/d' "$ROOTFS_DIR/init"
cat >> "$ROOTFS_DIR/init" << 'TESTEOF'

echo "=== Netlink request/reply test ==="
insmod /root/modules/netlink_demo.ko
/bin/netlink-client
dmesg | grep 'netlink_demo:'
rmmod netlink_demo
echo "=== Netlink done ==="
exec /bin/sh
TESTEOF

dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=128 status=none
mke2fs -q -d "$ROOTFS_DIR" "$ROOTFS_IMG" 2>/dev/null
timeout 12 qemu-system-x86_64 \
	-kernel "$KERNEL_IMAGE" \
	-append "root=/dev/vda rw console=ttyS0 init=/init nokaslr" \
	-drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
	-device virtio-blk-pci,drive=drive0 \
	-m 1G -smp 2 -display none -serial file:"$LOG" -no-reboot 2>&1 || true

mv "$ROOTFS_DIR/init.bak" "$ROOTFS_DIR/init"
grep -E 'Netlink request|userspace received|netlink_demo:|Netlink done' "$LOG"
