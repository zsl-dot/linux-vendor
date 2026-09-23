#!/bin/bash
# 验证 hello_module（/proc 交互模块）
# 用法: ./run.sh build  全量编译并验证
#       ./run.sh update 增量编译并验证
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs
LOG="$LOG_DIR/hello-proc-run.log"
CMD="${1:-build}"

echo "=== hello-proc 验证流程 ==="

# 1. 编译
if [ "$CMD" = "build" ]; then
    echo "[1/4] 全量编译 hello_module..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 hello_module..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 2. 复制 .ko 到 rootfs
echo "[2/4] 复制 hello_module.ko 到 rootfs..."
mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/hello-proc/hello_module.ko" "$ROOTFS_DIR/root/modules/"

# 3. 注入测试 init（脚本退出时自动恢复原始 init）
inject_init_test << 'TESTEOF'

# Auto-load hello_module if available
if [ -f /root/modules/hello_module.ko ]; then
	echo "=== hello_module test ==="
	insmod /root/modules/hello_module.ko
	dmesg | grep 'hello_module:'
	cat /proc/hello_module
	echo "42" > /proc/hello_module
	cat /proc/hello_module
	rmmod hello_module
	echo "=== hello_module done ==="
	echo ""
fi

exec /bin/sh
TESTEOF

# 4. 启动 VM（run_qemu 重建 rootfs.img 并运行）
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

echo ""
echo "--- VM 输出 ---"
grep -E 'hello_module module|hello_module:|hello_module done' "$LOG"
