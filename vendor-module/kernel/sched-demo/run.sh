#!/bin/bash
# 调度学习环境验证 demo：
#   在 QEMU 自定义内核中验证 CPU 调度观测与控制设施：
#   sched tracepoint 记录、函数级 ftrace、cgroup v2 CPU 带宽限流（CFS_BANDWIDTH）、
#   sched_ext（SCHED_CLASS_EXT）可用性。
#
# 用法: ./run.sh build   全量验证（配置核查 + 注入 guest 脚本 + QEMU 运行）
#       ./run.sh config  仅核查内核配置
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs

LOG="$LOG_DIR/sched-demo.log"
GUEST_SCRIPT="sched-check.sh"
CONFIG="$KERNEL_OUT/.config"

# ---- 调度学习内核配置核查（必需项 + 建议项）----
check_sched_config() {
    local missing="" sym
    for sym in CONFIG_TRACEPOINTS CONFIG_FTRACE CONFIG_SCHEDSTATS \
               CONFIG_CGROUPS CONFIG_CGROUP_SCHED CONFIG_FAIR_GROUP_SCHED \
               CONFIG_CFS_BANDWIDTH CONFIG_DEBUG_INFO_BTF; do
        grep -qE "^$sym=y" "$CONFIG" || missing="$missing $sym"
    done
    if [ -n "$missing" ]; then
        echo "内核缺少调度学习必需配置:$missing"
        echo "请在 lib/kernel.sh 的配置组中补齐（并递增 flavor）后重新 ./go.sh kernel"
        return 1
    fi
    echo "调度必需配置核查通过（tracepoints/ftrace/cgroup-cpu/BTF）"
    for sym in CONFIG_FUNCTION_TRACER CONFIG_SCHED_CLASS_EXT CONFIG_HIST_TRIGGERS; do
        grep -qE "^$sym=y" "$CONFIG" || echo "  [提示] 未开启 $sym（建议开启，影响部分观测能力）"
    done
}

case "${1:-build}" in
    config)
        check_sched_config
        ;;
    build|update)
        echo "=== sched-demo：调度学习环境验证 ==="

        echo "[1/4] 内核配置核查..."
        check_sched_config

        echo "[2/4] 注入 guest 验证脚本..."
        cp "$SCRIPT_DIR/$GUEST_SCRIPT" "$ROOTFS_DIR/sched-check"
        chmod +x "$ROOTFS_DIR/sched-check"

        echo "[3/4] 重建 rootfs.img..."
        mke2fs -F -q -d "$ROOTFS_DIR" "$ROOTFS_IMG"

        echo "[4/4] QEMU 启动验证..."
        timeout 200 qemu-system-x86_64 $(qemu_accel_flags) \
            -kernel "$KERNEL_IMAGE" \
            -append "root=/dev/vda rw console=ttyS0 init=/sched-check nokaslr" \
            -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
            -device virtio-blk-pci,drive=drive0 \
            -m 2G -smp 4 \
            -display none -serial "file:$LOG" -no-reboot >/dev/null 2>&1 || true

        echo ""
        echo "--- 验证结果 ---"
        grep -aE "^(RESULT:|===)" "$LOG" | head -20

        pass_n=$(grep -ac "^RESULT: PASS" "$LOG" || true)
        fail_n=$(grep -ac "^RESULT: FAIL" "$LOG" || true)
        echo ""
        if [ "$fail_n" -eq 0 ] && [ "$pass_n" -ge 4 ]; then
            echo "[✓] 调度环境验证通过（pass=$pass_n）"
        else
            echo "调度环境验证存在失败项（pass=$pass_n fail=$fail_n），日志：$LOG"
            exit 1
        fi
        ;;
    -h|--help|help)
        sed -n '2,8p' "$0"
        ;;
    *)
        echo "用法: ./run.sh build|config"
        exit 1
        ;;
esac
