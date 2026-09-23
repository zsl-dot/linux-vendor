#!/bin/bash
# 一键运行 binder-demo 验证流程
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/binder-demo-run.log"
CMD="${1:-build}"

echo "=== binder-demo 验证流程 ==="

# 1. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/4] 全量编译 binder-demo..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 binder-demo..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 2. 复制二进制到 rootfs
echo "[2/4] 复制二进制到 rootfs..."
cp "$LEARN_OUT"/binder-demo/binder-{server,client} "$ROOTFS_DIR/bin/"

# 3. 注入测试 init（脚本退出时自动恢复原始 init）
inject_init_test << 'TESTEOF'

# Auto-run binder-demo test if available
if [ -x /bin/binder-server ] && [ -x /bin/binder-client ]; then
	echo "=== Binder demo test ==="
	binder-server &
	sleep 1
	binder-client
	kill %1 2>/dev/null
	echo "=== Test done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 4. 启动 VM（run_qemu 重建 rootfs.img 并运行）
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

echo ""
echo "--- VM 输出 ---"
grep -E 'Binder demo|server|client|Test done' "$LOG"
