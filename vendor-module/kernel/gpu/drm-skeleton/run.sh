#!/bin/bash
# DRM/KMS 驱动骨架验证 demo：
#   编译最小 DRM 驱动（drm_simple_display_pipe + GEM shmem），
#   在 QEMU 中加载并用 modetest 走完 模块加载→设备节点→资源枚举→modeset。
#
# 用法: ./run.sh build   全量验证
#       ./run.sh config  仅核查内核配置
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source
check_kernel_build
ensure_rootfs

LOG="$LOG_DIR/drm-skeleton.log"
GUEST_SCRIPT="drm-skeleton-check.sh"
CONFIG="$KERNEL_OUT/.config"

check_gpu_config() {
    local missing="" sym
    for sym in CONFIG_DRM CONFIG_DRM_GEM_SHMEM_HELPER CONFIG_DRM_KMS_HELPER; do
        grep -qE "^$sym=y" "$CONFIG" || missing="$missing $sym"
    done
    if [ -n "$missing" ]; then
        echo "内核缺少 DRM 配置:$missing"
        return 1
    fi
    echo "DRM 配置核查通过（DRM 核心 + KMS helper + GEM shmem）"
}

case "${1:-build}" in
    config)
        check_gpu_config
        ;;
    build|update)
        echo "=== drm-skeleton：DRM/KMS 驱动骨架验证 ==="

        echo "[1/5] 内核配置核查..."
        check_gpu_config

        echo "[2/5] 编译骨架模块..."
        make -C "$SCRIPT_DIR" clean > /dev/null 2>&1 || true
        make -C "$SCRIPT_DIR" > /dev/null

        echo "[3/5] 注入 modetest 与 guest 验证脚本..."
        "$PROJECT_ROOT/lib/vm/add-drm-tools.sh" > /dev/null 2>&1 || {
            echo "modetest 注入失败（宿主机需 libdrm-tests）"; exit 1; }
        cp "$SCRIPT_DIR/$GUEST_SCRIPT" "$ROOTFS_DIR/drm-skeleton-check"
        chmod +x "$ROOTFS_DIR/drm-skeleton-check"
        mkdir -p "$ROOTFS_DIR/root/modules"
        cp "$LEARN_OUT/drm-skeleton/skeleton.ko" "$ROOTFS_DIR/root/modules/"

        echo "[4/5] 重建 rootfs.img..."
        mke2fs -F -q -d "$ROOTFS_DIR" "$ROOTFS_IMG"

        echo "[5/5] QEMU 启动验证..."
        timeout 200 qemu-system-x86_64 $(qemu_accel_flags) \
            -kernel "$KERNEL_IMAGE" \
            -append "root=/dev/vda rw console=ttyS0 init=/drm-skeleton-check nokaslr" \
            -drive file="$ROOTFS_IMG",format=raw,if=none,id=drive0 \
            -device virtio-blk-pci,drive=drive0 \
            -m 1G -smp 2 \
            -display none -serial "file:$LOG" -no-reboot >/dev/null 2>&1 || true

        echo ""
        echo "--- 验证结果 ---"
        grep -aE "^(RESULT:|===)" "$LOG" | head -12

        pass_n=$(grep -ac "^RESULT: PASS" "$LOG" || true)
        fail_n=$(grep -ac "^RESULT: FAIL" "$LOG" || true)
        echo ""
        if [ "$fail_n" -eq 0 ] && [ "$pass_n" -ge 3 ]; then
            echo "[✓] drm-skeleton 验证通过（pass=$pass_n）"
        else
            echo "drm-skeleton 验证存在失败项（pass=$pass_n fail=$fail_n），日志：$LOG"
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
