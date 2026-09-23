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

inject_init_test << 'TESTEOF'

echo "=== Netlink request/reply test ==="
insmod /root/modules/netlink_demo.ko
/bin/netlink-client
dmesg | grep 'netlink_demo:'
rmmod netlink_demo
echo "=== Netlink done ==="
exec /bin/sh
TESTEOF

run_qemu "$LOG"

grep -E 'Netlink request|userspace received|netlink_demo:|Netlink done' "$LOG"
