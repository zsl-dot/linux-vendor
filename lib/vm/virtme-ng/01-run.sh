#!/bin/bash
# virtme-ng: 启动完整 Ubuntu 环境（共享宿主文件系统）
# 优势: 可直接使用 perf, strace, gdb, trace-cmd 等完整工具
# 退出: Ctrl+D 或 poweroff
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

# virtme-ng 直接指向 O= 构建目录（build/），不需要源码树内有 bzImage
echo "=== virtme-ng 启动 ==="
echo "内核: $KERNEL_OUT"
echo "文件系统: 宿主 Ubuntu（共享）"
echo "可用工具: perf, strace, gdb, trace-cmd"
echo "退出: Ctrl+D 或 poweroff"
echo ""

vng --arch x86_64 --cpus 4 --memory 512M --run "$KERNEL_OUT"
