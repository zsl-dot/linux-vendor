#!/bin/bash
# 一键运行 eBPF kprobe demo 验证流程
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/ebpf-demo1-run.log"
CMD="${1:-build}"

echo "=== eBPF kprobe demo 验证流程 ==="

# 1. 编译 bpflib
echo "[0/4] 编译 bpflib..."
make -C "$SCRIPT_DIR/../bpflib" > /dev/null 2>&1 || true

# 2. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/4] 全量编译 eBPF demo..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 eBPF demo..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 3. 复制到 rootfs
echo "[2/4] 复制到 rootfs..."
cp "$LEARN_OUT/ebpf-demo1/ebpf-loader" "$ROOTFS_DIR/bin/"
cp "$LEARN_OUT/ebpf-demo1/kprobe_exec.bpf.o" "$ROOTFS_DIR/root/"

# 4. 注入测试 init（脚本退出时自动恢复原始 init）
inject_init_test << 'TESTEOF'

# Auto-run eBPF kprobe demo
if [ -x /bin/ebpf-loader ] && [ -f /root/kprobe_exec.bpf.o ]; then
	echo "=== eBPF kprobe demo ==="
	echo 0 > /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || true
	/bin/ebpf-loader /root/kprobe_exec.bpf.o "kprobe/__x64_sys_execve" __x64_sys_execve &
	LOADER_PID=$!
	sleep 2
	# Trigger execve events to make the kprobe fire
	/bin/sh -c "true" 2>/dev/null
	/bin/sh -c "true" 2>/dev/null
	sleep 1
	kill $LOADER_PID 2>/dev/null
	wait $LOADER_PID 2>/dev/null
	echo "=== eBPF done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 5. 启动 VM（run_qemu 重建 rootfs.img 并运行）
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

echo ""
echo "--- VM 输出 ---"
grep -E 'eBPF|ebpf|loader' "$LOG"
