#!/bin/bash
# 把宿主机的 modetest（libdrm-tests）及其动态库注入 rootfs，供 guest 内验证 DRM/KMS。
# 用法: ./lib/vm/add-drm-tools.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
eval "$(python3 "$SCRIPT_DIR/../workflow_config.py" shell)"

MODETEST=$(command -v modetest 2>/dev/null || true)
if [ -z "$MODETEST" ]; then
    echo "错误: 未找到 modetest，请安装: sudo apt install libdrm-tests"
    exit 1
fi

echo "=== 注入 modetest 到 $ROOTFS_DIR ==="
mkdir -p "$ROOTFS_DIR"/lib64
cp "$MODETEST" "$ROOTFS_DIR/bin/modetest"
chmod +x "$ROOTFS_DIR/bin/modetest"

# 复制动态库依赖（跟随符号链接指向的实体文件）
for lib in $(ldd "$MODETEST" | awk '{print $3}' | grep '^/'); do
    cp -L "$lib" "$ROOTFS_DIR/lib/" 2>/dev/null || true
done
cp -L /lib64/ld-linux-x86-64.so.2 "$ROOTFS_DIR/lib64/" 2>/dev/null || true

# 拷贝 guest 内的验证脚本
cp "$SCRIPT_DIR/init-drm-check.sh" "$ROOTFS_DIR/drm-check"
chmod +x "$ROOTFS_DIR/drm-check"

echo "[完成] rootfs 已包含 modetest 与 /drm-check"
echo "启动: qemu ... -append '... init=/drm-check'"
