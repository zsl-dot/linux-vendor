#!/bin/bash
# 项目唯一总入口：依赖 → 内核编译 → QEMU demo 验证。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
eval "$(python3 "$SCRIPT_DIR/lib/workflow_config.py" shell)"
GREEN='\033[0;32m'; BLUE='\033[0;34m'; RED='\033[0;31m'; NC='\033[0m'
DEMO_DIR="$LINUX_LEARN_DIR/linux-vendor-module"
mkdir -p "$LOG_DIR"

source "$SCRIPT_DIR/lib/common.sh"
source "$SCRIPT_DIR/lib/kernel.sh"
source "$SCRIPT_DIR/lib/demos.sh"

usage() {
    cat <<EOF
用法: $0 [all|init|deps|kernel|demo|sync|status|check|clean|--auto]
  all（默认）  安装依赖 → 编译内核 → QEMU 验证全部 demo
  init         初始化子模块，并切换到 Linux work 分支
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
    [ -e "$KERNEL_SRC/.git" ] || die "linux-source 子模块未初始化；执行 ./go.sh init"
    python3 "$SCRIPT_DIR/lib/linux_fork_workflow.py" status
    [ "$(git -C "$KERNEL_SRC" branch --show-current)" = "$KERNEL_WORK_BRANCH" ] \
        || die "linux-source 必须位于 $KERNEL_WORK_BRANCH 分支；执行 ./go.sh init"
    echo "根仓库状态："
    git -C "$SCRIPT_DIR" status --short --branch
}

require_ready_workspace() {
    [ -e "$KERNEL_SRC/.git" ] || die "linux-source 子模块未初始化；执行 ./go.sh init"
    [ "$(git -C "$KERNEL_SRC" branch --show-current)" = "$KERNEL_WORK_BRANCH" ] \
        || die "linux-source 必须位于 $KERNEL_WORK_BRANCH 分支；执行 ./go.sh init"
}

init_workspace() {
    git -C "$SCRIPT_DIR" submodule update --init --recursive
    git -C "$KERNEL_SRC" switch "$KERNEL_WORK_BRANCH"
    git -C "$KERNEL_SRC" branch --set-upstream-to="origin/$KERNEL_WORK_BRANCH" "$KERNEL_WORK_BRANCH"
    check_workspace
}

sync_workspace() {
    git -C "$SCRIPT_DIR" diff --quiet || die "根仓库有未提交修改；先提交后再同步"
    git -C "$SCRIPT_DIR" diff --cached --quiet || die "根仓库暂存区有未提交修改；先提交后再同步"
    python3 "$SCRIPT_DIR/lib/linux_fork_workflow.py" sync-upstream
    git -C "$SCRIPT_DIR" add linux-source
    if ! git -C "$SCRIPT_DIR" diff --cached --quiet; then
        git -C "$SCRIPT_DIR" commit -m "chore: sync Linux work revision"
        if git -C "$SCRIPT_DIR" remote get-url origin >/dev/null 2>&1; then
            git -C "$SCRIPT_DIR" push origin main
        else
            echo "根仓库尚未配置 origin；子模块指针已提交，待配置远程后执行 git push -u origin main"
        fi
    else
        echo "Linux work 未产生新提交；根仓库无需更新。"
    fi
}

case "${1:-all}" in
    all)       require_ready_workspace; install_deps ""; prepare_kernel; verify_all_demos ;;
    --auto|-y) require_ready_workspace; install_deps "--auto"; prepare_kernel; verify_all_demos ;;
    init)      init_workspace ;;
    deps)      install_deps "" ;;
    kernel)    require_ready_workspace; prepare_kernel ;;
    demo)      require_ready_workspace; verify_all_demos ;;
    sync)      sync_workspace ;;
    status)    python3 "$SCRIPT_DIR/lib/linux_fork_workflow.py" status ;;
    check)     check_workspace ;;
    clean)     do_clean ;;
    -h|--help|help) usage ;;
    *) usage; exit 1 ;;
esac
