#!/bin/bash
# virtme-ng + kgdb: 启动 VM 并等待 gdb 连接
# 用法:
#   终端1: ./02-run-kgdb.sh
#   终端2: gdb <项目目录>/build/vmlinux
#          (gdb) target remote :1234
#          (gdb) break __schedule
#          (gdb) continue
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../linux-learn/env.sh"
check_kernel_source
check_kernel_build

export PATH="$HOME/.local/bin:$PATH"
if ! command -v vng &>/dev/null; then
    echo "错误: 未安装 virtme-ng"
    echo "安装: pip install virtme-ng"
    exit 1
fi

echo "=== virtme-ng kgdb 模式 ==="
echo ""
echo "终端2 执行:"
echo "  gdb $KERNEL_OUT/vmlinux"
echo "  (gdb) target remote :1234"
echo "  (gdb) break __schedule"
echo "  (gdb) continue"
echo ""

vng --arch amd64 --cpus 4 --memory 512M \
    --run "$KERNEL_OUT" --gdb
