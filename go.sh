#!/bin/bash
# 项目唯一总入口：依赖 → 内核编译 → QEMU demo 验证。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
eval "$(python3 "$SCRIPT_DIR/workflow_config.py" shell)"
GREEN='\033[0;32m'; BLUE='\033[0;34m'; RED='\033[0;31m'; NC='\033[0m'
DEMO_DIR="$LINUX_LEARN_DIR/linux-vendor-module"
LOG_DIR="${LOG_DIR:-/tmp/linux-learn-logs}"
mkdir -p "$LOG_DIR"

source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/kernel.sh"
source "$SCRIPT_DIR/lib/demos.sh"

usage() {
    cat <<EOF
用法: $0 [all|deps|kernel|demo|sync|status|check|clean|--auto]
  all（默认）  安装依赖 → 编译内核 → QEMU 验证全部 demo
  deps         仅检查/安装构建依赖
  kernel       仅准备并编译内核（输出：$KERNEL_OUT）
  demo         仅编译并在 QEMU 中验证 demo
  sync         同步 kernel.org → Fork master → work rebase
  status       显示 Linux 子模块分支、远程与当前提交
  check        检查工作区结构、子模块与忽略规则
  clean        清理 demo、rootfs 和日志；保留内核编译产物
  --auto       all 的非交互版本
EOF
}

check_workspace() {
    [ -f "$SCRIPT_DIR/.gitmodules" ] || die "linux-source 尚未登记为 Git 子模块"
    [ -d "$KERNEL_SRC/.git" ] || die "linux-source 子模块未初始化；执行 git submodule update --init --recursive"
    python3 "$SCRIPT_DIR/linux_fork_workflow.py" status
    echo "根仓库状态："
    git -C "$SCRIPT_DIR" status --short --branch
}

case "${1:-all}" in
    all)       install_deps ""; prepare_kernel; verify_all_demos ;;
    --auto|-y) install_deps "--auto"; prepare_kernel; verify_all_demos ;;
    deps)      install_deps "" ;;
    kernel)    prepare_kernel ;;
    demo)      verify_all_demos ;;
    sync)      python3 "$SCRIPT_DIR/linux_fork_workflow.py" sync-upstream ;;
    status)    python3 "$SCRIPT_DIR/linux_fork_workflow.py" status ;;
    check)     check_workspace ;;
    clean)     do_clean ;;
    -h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
