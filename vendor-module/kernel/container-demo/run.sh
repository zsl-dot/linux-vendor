#!/bin/bash
# 容器化内核能力验证 demo：
#   在 QEMU 自定义内核中验证 namespace / cgroups v2 / overlayfs 等
#   容器运行所需的内核特性（本项目内核配置的"容器化组"）。
#
# 用法: ./run.sh build   全量验证（配置核查 + 注入 guest 脚本 + QEMU 运行）
#       ./run.sh config  仅核查内核配置
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs

LOG="$LOG_DIR/container-demo.log"
GUEST_SCRIPT="container-check.sh"
CONFIG="$KERNEL_OUT/.config"

# ---- 容器化内核配置核查（Docker 运行的前提）----
check_container_config() {
    local missing=""
    for sym in CONFIG_NAMESPACES CONFIG_NET_NS CONFIG_PID_NS CONFIG_UTS_NS \
               CONFIG_IPC_NS CONFIG_CGROUPS CONFIG_MEMCG CONFIG_CGROUP_SCHED \
               CONFIG_CGROUP_DEVICE CONFIG_CGROUP_PIDS CONFIG_CFS_BANDWIDTH \
               CONFIG_OVERLAY_FS CONFIG_VETH CONFIG_BRIDGE CONFIG_BRIDGE_NETFILTER \
               CONFIG_IP_NF_IPTABLES CONFIG_IP_NF_FILTER CONFIG_IP_NF_NAT \
               CONFIG_IP_NF_TARGET_MASQUERADE CONFIG_SECCOMP CONFIG_SECCOMP_FILTER \
               CONFIG_USER_NS; do
        grep -qE "^$sym=(y|m)" "$CONFIG" || missing="$missing $sym"
    done
    if [ -n "$missing" ]; then
        echo "内核缺少容器化配置:$missing"
        echo "请在 lib/kernel.sh 的配置组中补齐后重新 ./go.sh kernel"
        return 1
    fi
    echo "内核容器化配置核查通过（namespaces/cgroups/overlayfs/veth/netfilter）"
}

case "${1:-build}" in
    config)
        check_container_config
        ;;
    build|update)
        echo "=== container-demo：容器化内核能力验证 ==="

        echo "[1/4] 内核配置核查..."
        check_container_config

        echo "[2/4] 注入 guest 验证脚本..."
        cp "$SCRIPT_DIR/$GUEST_SCRIPT" "$ROOTFS_DIR/container-check"
        chmod +x "$ROOTFS_DIR/container-check"

        echo "[3/4] 重建 rootfs.img..."
        mke2fs -F -q -d "$ROOTFS_DIR" "$ROOTFS_IMG"

        echo "[4/4] QEMU 启动验证..."
        timeout 200 qemu-system-x86_64 $(qemu_accel_flags) \
            -kernel "$KERNEL_IMAGE" \
            -append "root=/dev/vda rw console=ttyS0 init=/container-check nokaslr" \
            -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
            -device virtio-blk-pci,drive=drive0 \
            -m 2G -smp 4 \
            -display none -serial "file:$LOG" -no-reboot >/dev/null 2>&1 || true

        echo ""
        echo "--- 验证结果 ---"
        grep -aE "^(RESULT:|===|   )" "$LOG" | head -30

        pass_n=$(grep -ac "^RESULT: PASS" "$LOG" || true)
        fail_n=$(grep -ac "^RESULT: FAIL" "$LOG" || true)
        echo ""
        if [ "$fail_n" -eq 0 ] && [ "$pass_n" -ge 5 ]; then
            echo "[✓] 容器化验证全部通过（pass=$pass_n）"
        else
            echo "容器化验证存在失败项（pass=$pass_n fail=$fail_n），日志：$LOG"
            exit 1
        fi
        ;;
    -h|--help|help)
        sed -n '2,6p' "$0"
        ;;
    *)
        echo "用法: ./run.sh build|config"
        exit 1
        ;;
esac
