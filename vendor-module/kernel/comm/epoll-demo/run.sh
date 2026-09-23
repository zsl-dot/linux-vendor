#!/bin/bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source; check_kernel_build; ensure_rootfs
LOG="$LOG_DIR/epoll-demo-run.log"
make -C "$SCRIPT_DIR" clean >/dev/null 2>&1 || true
make -C "$SCRIPT_DIR"
mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/epoll-demo/epoll_demo.ko" "$ROOTFS_DIR/root/modules/"
cp "$LEARN_OUT/epoll-demo/epoll-client" "$ROOTFS_DIR/bin/"
inject_init_test <<'EOF'
echo "=== epoll demo test ==="
insmod /root/modules/epoll_demo.ko
/bin/epoll-client
rmmod epoll_demo
echo "=== epoll demo done ==="
exec /bin/sh
EOF
run_qemu "$LOG"
grep -E 'epoll demo|epoll event|epoll_demo:' "$LOG"
