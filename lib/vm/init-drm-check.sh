#!/bin/sh
# 在 QEMU guest 中验证内核的 DRM / KMS 显示管线，验证完自动关机。
# 由 lib/vm/add-drm-tools.sh 复制到 rootfs 的 /drm-check。
# 启动方式：qemu ... -append "... init=/drm-check"
mount -t proc proc /proc
mount -t sysfs sysfs /sys

echo "== 内核版本 =="
uname -r

echo "== /dev/dri 设备节点 =="
ls -l /dev/dri 2>/dev/null || echo "(无 /dev/dri，DRM 驱动未加载)"

echo "== vkms 提供的显示资源（connectors / CRTCs / planes） =="
modetest -M vkms 2>&1

echo "== 尝试一次 modeset（atomic + 显示测试图案） =="
# connector id: Connectors 段下第一个数字行；mode: modes 段里 #0 那一行的分辨率
CONN=$(modetest -M vkms 2>/dev/null |
       awk '/^Connectors:/{f=1;next} /^CRTCs:/{f=0} f && $1 ~ /^[0-9]+$/ && NF>4 {print $1; exit}')
MODE=$(modetest -M vkms 2>/dev/null |
       awk '/^[[:space:]]*#0[[:space:]]/{print $2; exit}')
if [ -n "$CONN" ] && [ -n "$MODE" ]; then
    echo "connector=$CONN mode=$MODE"
    timeout 5 modetest -M vkms -s "$CONN:$MODE" 2>&1
else
    echo "(未能解析 connector/mode，仅完成资源枚举)"
fi

reboot -f
