#!/bin/bash
# 编译并进入交互式 demo VM
# 用法: ./03-run-demos.sh [hello hello-proc netlink-demo]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../vendor-module/env.sh"
check_kernel_source
check_kernel_build

DEMO_ROOT="$VENDOR_MODULE_DIR/kernel"
DEFAULT_DEMOS=(hello hello-proc netlink-demo binder-demo)
if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    sed -n '2,3p' "$0"
    exit 0
fi
DEMOS=("${@:-${DEFAULT_DEMOS[@]}}")

for demo in "${DEMOS[@]}"; do
    case "$demo" in
        hello|hello-proc|netlink-demo|binder-demo) ;;
        *) echo "未知 demo: $demo"; echo "可选: hello hello-proc netlink-demo binder-demo"; exit 1 ;;
    esac
    echo "=== 编译 $demo ==="
    make -C "$DEMO_ROOT/$demo"
done

echo
echo "=== 进入交互式 VM ==="
echo "请等待出现 VM 提示符（主机名为 virtme-ng）后，再执行下列命令。"
echo "不要在当前宿主机终端执行 insmod。"
echo "模块位于 $LEARN_OUT/<demo>/（VM 共享宿主机文件系统）"
for demo in "${DEMOS[@]}"; do
    case "$demo" in
        hello)       echo "sudo insmod $LEARN_OUT/hello/hello.ko";;
        hello-proc)  echo "sudo insmod $LEARN_OUT/hello-proc/hello_module.ko";;
        netlink-demo) echo "sudo insmod $LEARN_OUT/netlink-demo/netlink_demo.ko";;
        binder-demo) echo "# binder-demo 使用 $LEARN_OUT/binder-demo/binder-server";;
    esac
done
echo "退出 VM: poweroff"
echo

export PATH="$HOME/.local/bin:$PATH"
command -v vng >/dev/null 2>&1 || { echo "错误: 未安装 virtme-ng（pip install virtme-ng）"; exit 1; }
vng --arch amd64 --cpus 4 --memory 1G --cwd "$PROJECT_ROOT" --run "$KERNEL_OUT"
