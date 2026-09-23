#!/bin/bash
# 验证 kgdb 测试模块
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/kgdb-demo-run.log"
CMD="${1:-build}"

echo "=== kgdb-demo 验证流程 ==="

# 1. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/4] 全量编译 kgdb-test..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 kgdb-test..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 2. 复制 .ko 到 rootfs
echo "[2/4] 复制 kgdb-test.ko 到 rootfs..."
mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/kgdb-demo/kgdb-test.ko" "$ROOTFS_DIR/root/modules/"

# 3. 注入测试 init（脚本退出时自动恢复原始 init）
inject_init_test << 'TESTEOF'

# Auto-load kgdb-test module if available
if [ -f /root/modules/kgdb-test.ko ]; then
	echo "=== kgdb-test module test ==="
	insmod /root/modules/kgdb-test.ko
	dmesg | grep 'kgdb_test:'
	rmmod kgdb-test
	echo "=== kgdb-test done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 4. 启动 VM（run_qemu 重建 rootfs.img 并运行）
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

echo ""
echo "--- VM 输出 ---"
grep -E 'kgdb-test module|kgdb_test:|kgdb-test done' "$LOG"
