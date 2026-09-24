#!/bin/bash
# 构建 Debian 最小发行版 rootfs —— L3 第三通道。
#
# 与 busybox rootfs（mk-rootfs.sh，自动化 demo 验证用）互补：本通道提供
# 真实发行版用户态（glibc/coreutils/runc），供容器运行时等需要完整用户态
# 的实验使用。内核仍是同一个 build/linux-out/bzImage。
#
# 产物（见 workflow_config.py）：
#   DISTRO_ROOTFS_DIR  目录树（debootstrap 产物，可增量复用）
#   DISTRO_ROOTFS_IMG  ext4 镜像（QEMU 直接启动，init=<脚本> 无人值守）
#
# 用法: ./lib/vm/mk-distro-rootfs.sh [suite] [mirror]
#   suite  默认 bookworm；mirror 默认清华 TUNA（国内可达）。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
eval "$(python3 "$SCRIPT_DIR/../workflow_config.py" shell)"

SUITE="${1:-bookworm}"
MIRROR="${2:-https://mirrors.tuna.tsinghua.edu.cn/debian}"

command -v debootstrap >/dev/null 2>&1 || {
    echo "错误: 未找到 debootstrap，请安装: sudo apt install debootstrap"
    exit 1
}

# debootstrap 需要真实 root（创建设备节点/属主）。两种运行方式：
#   交互：./lib/vm/mk-distro-rootfs.sh（内部 sudo 会提示密码）
#   无终端：echo <密码> | sudo -S bash lib/vm/mk-distro-rootfs.sh
# 以 root 整体运行时，结束时把产物属主归还调用用户，便于后续 demo 注入文件。
if [ -x "$DISTRO_ROOTFS_DIR/usr/bin/runc" ]; then
    echo "[跳过] 发行版 rootfs 已存在且含 runc: $DISTRO_ROOTFS_DIR"
else
    echo "=== debootstrap $SUITE (minbase + runc)，镜像: $MIRROR ==="
    echo "（首次构建约需 5~15 分钟，取决于镜像速度）"
    sudo rm -rf "$DISTRO_ROOTFS_DIR"
    mkdir -p "$DISTRO_ROOTFS_DIR"
    sudo debootstrap --arch=amd64 --variant=minbase --include=runc \
        "$SUITE" "$DISTRO_ROOTFS_DIR" "$MIRROR"
    if [ -n "${SUDO_USER:-}" ]; then
        sudo chown -R "$SUDO_USER:$SUDO_USER" "$DISTRO_ROOTFS_DIR" || true
    fi
fi

echo "=== 生成 ext4 镜像: $DISTRO_ROOTFS_IMG ==="
rm -f "$DISTRO_ROOTFS_IMG"
truncate -s 800M "$DISTRO_ROOTFS_IMG"
mke2fs -F -q -d "$DISTRO_ROOTFS_DIR" "$DISTRO_ROOTFS_IMG"

echo "[✓] 发行版 rootfs 就绪: $DISTRO_ROOTFS_IMG（800M，含 Debian $SUITE minbase + runc）"
