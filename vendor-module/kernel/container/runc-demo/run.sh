#!/bin/bash
# runc 容器运行时验证 demo —— L3 第三通道（发行版 rootfs）的首个实验。
#
# 与 container-demo（内核能力逐项验证，busybox rootfs）互补：本 demo 在
# 真实 Debian 用户态上用 runc 启动一个完整 OCI 容器（busybox 容器根文件
# 系统），验证"容器 = 内核命名空间/cgroup + 运行时拼装"的完整链路。
#
# 依赖: lib/vm/mk-distro-rootfs.sh 生成的发行版 rootfs（Debian + runc），
#       首次使用请先执行: ./lib/vm/mk-distro-rootfs.sh（需 sudo，约 5~15 分钟）
#
# 用法: ./run.sh build
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../env.sh"
check_kernel_source
check_kernel_build

LOG="$LOG_DIR/runc-demo.log"
RUN_IMG="$BUILD_ROOT/vm-distro-run.img"   # demo 专用镜像，不动 800M 基线
CONFIG="$KERNEL_OUT/.config"

check_container_config() {
    local missing="" sym
    for sym in CONFIG_NAMESPACES CONFIG_PID_NS CONFIG_UTS_NS CONFIG_IPC_NS \
               CONFIG_CGROUPS CONFIG_MEMCG; do
        grep -qE "^$sym=y" "$CONFIG" || missing="$missing $sym"
    done
    [ -n "$missing" ] && { echo "内核缺少容器配置:$missing"; return 1; }
    echo "容器配置核查通过（namespaces + cgroups）"
}

ensure_distro_rootfs() {
    if [ -x "$DISTRO_ROOTFS_DIR/usr/bin/runc" ]; then
        return
    fi
    echo "发行版 rootfs 不存在（L3 通道未构建）。执行一次（需 sudo 密码）:"
    echo "    ./lib/vm/mk-distro-rootfs.sh"
    exit 1
}

build_bundle() {
    local bundle="$DISTRO_ROOTFS_DIR/bundle"
    echo "[2/5] 组装 OCI bundle（busybox 容器根文件系统）..."
    rm -rf "$bundle"
    mkdir -p "$bundle/rootfs"/{bin,etc,proc,sys,dev,tmp,root,out}
    cp "$(command -v busybox)" "$bundle/rootfs/bin/busybox"
    for app in sh echo cat sleep hostname mount ps grep ls wc; do
        ln -sf busybox "$bundle/rootfs/bin/$app"
    done
    cp "$SCRIPT_DIR/container-init.sh" "$bundle/rootfs/init.sh"
    chmod +x "$bundle/rootfs/init.sh"
    cp "$SCRIPT_DIR/bundle-config.json" "$bundle/config.json"
}

case "${1:-build}" in
    build|update)
        echo "=== runc-demo：runc 容器运行时验证（L3 发行版通道）==="

        echo "[1/5] 配置与 rootfs 检查..."
        check_container_config
        ensure_distro_rootfs
        build_bundle

        echo "[3/5] 注入 guest 验证脚本..."
        cp "$SCRIPT_DIR/runc-check.sh" "$DISTRO_ROOTFS_DIR/runc-check"
        chmod +x "$DISTRO_ROOTFS_DIR/runc-check"
        mkdir -p "$DISTRO_ROOTFS_DIR/out"

        echo "[4/5] 生成运行镜像..."
        rm -f "$RUN_IMG"
        truncate -s 800M "$RUN_IMG"
        mke2fs -F -q -d "$DISTRO_ROOTFS_DIR" "$RUN_IMG"

        echo "[5/5] QEMU 启动验证（Debian 用户态 + 自定义内核）..."
        timeout 240 qemu-system-x86_64 $(qemu_accel_flags) \
            -kernel "$KERNEL_IMAGE" \
            -append "root=/dev/vda rw console=ttyS0 init=/runc-check nokaslr" \
            -drive file="$RUN_IMG",format=raw,if=none,id=drive0 \
            -device virtio-blk-pci,drive=drive0 \
            -m 2G -smp 2 \
            -display none -serial "file:$LOG" -no-reboot >/dev/null 2>&1 || true

        echo ""
        echo "--- 验证结果 ---"
        grep -aE "^(RESULT:|===)" "$LOG" | head -16

        pass_n=$(grep -ac "^RESULT: PASS" "$LOG" || true)
        fail_n=$(grep -ac "^RESULT: FAIL" "$LOG" || true)
        echo ""
        if [ "$fail_n" -eq 0 ] && [ "$pass_n" -ge 5 ]; then
            echo "[✓] runc 容器验证通过（pass=$pass_n）"
        else
            echo "runc 验证存在失败项（pass=$pass_n fail=$fail_n），日志：$LOG"
            exit 1
        fi
        ;;
    -h|--help|help)
        sed -n '2,11p' "$0"
        ;;
    *)
        echo "用法: ./run.sh build"
        exit 1
        ;;
esac
