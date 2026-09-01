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
    echo "[1/4] 全量编译 hello..."
    make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
    make -C "$SCRIPT_DIR"
elif [ "$CMD" = "update" ]; then
    echo "[1/4] 增量编译 hello..."
    make -C "$SCRIPT_DIR"
else
    echo "用法: ./run.sh build|update"
    exit 1
fi

# 2. 复制 .ko 到 rootfs
echo "[2/4] 复制 hello.ko 到 rootfs..."
mkdir -p "$ROOTFS_DIR/root/modules"
cp "$LEARN_OUT/hello/hello.ko" "$ROOTFS_DIR/root/modules/"

# 3. 写入测试用 init
echo "[3/4] 准备测试 init..."
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
# 4. 启动 VM
echo "[4/4] 启动 VM 验证..."
run_qemu "$LOG"

# 恢复原始 init
mv "$ROOTFS_DIR/init.bak" "$ROOTFS_DIR/init"

echo ""
echo "--- VM 输出 ---"
grep -E 'Hello module|hello:|Hello done' "$LOG"
