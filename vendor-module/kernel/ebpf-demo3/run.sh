#!/bin/bash
# 一键运行 eBPF tracepoint demo 验证流程（sched:sched_switch）
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/ebpf-demo3-run.log"
CMD="${1:-build}"

echo "=== eBPF tracepoint demo 验证流程 ==="

# 1. 编译 bpflib
echo "[0/4] 编译 bpflib..."
make -C "$SCRIPT_DIR/../bpflib" > /dev/null 2>&1 || true

# 2. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/4] 全量编译 eBPF tracepoint demo..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 eBPF tracepoint demo..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 3. 复制到 rootfs
echo "[2/4] 复制到 rootfs..."
cp "$LEARN_OUT/ebpf-demo3/tp-loader" "$ROOTFS_DIR/bin/"
cp "$LEARN_OUT/ebpf-demo3/sched_switch_tp.bpf.o" "$ROOTFS_DIR/root/"

# 4. 注入测试 init（脚本退出时自动恢复原始 init）
inject_init_test << 'TESTEOF'

# Auto-run eBPF tracepoint demo (sched:sched_switch)
if [ -x /bin/tp-loader ] && [ -f /root/sched_switch_tp.bpf.o ]; then
	echo "=== eBPF tracepoint demo ==="
	mkdir -p /sys/kernel/debug
	mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null
	echo 0 > /proc/sys/kernel/unprivileged_bpf_disabled 2>/dev/null || true
	/bin/tp-loader /root/sched_switch_tp.bpf.o "tracepoint/sched_switch" sched sched_switch &
	LOADER_PID=$!
	sleep 2
	# 多任务系统本身持续触发 sched_switch，再补几次进程切换
	/bin/sh -c "true"
	/bin/sh -c "true"
	sleep 1
	kill $LOADER_PID 2>/dev/null
	wait $LOADER_PID 2>/dev/null
	umount /sys/kernel/debug 2>/dev/null
	echo "=== eBPF done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 5. 启动 VM（run_qemu 重建 rootfs.img 并运行）
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

echo ""
echo "--- VM 输出（加载器）---"
grep -aE '\[loader\]|\[trace\]' "$LOG" | head -8
echo ""
hits=$(grep -ac "eBPF: sched_switch" "$LOG" || true)
if [ "${hits:-0}" -ge 1 ] 2>/dev/null; then
    echo "[✓] tracepoint 捕获 ${hits} 条 sched_switch 事件"
else
    echo "[✗] 未捕获 tracepoint 事件，日志：$LOG"
    exit 1
fi
