#!/bin/sh
# 在 QEMU guest 中验证内核的 binder / binderfs 支持，验证完自动关机。
# 由 lib/vm/mk-rootfs.sh 复制到 rootfs 的 /binder-check，
# 启动方式：qemu ... -append "... init=/binder-check"
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

echo "== 内核版本 =="
uname -r

echo "== /proc/filesystems 中的 binder =="
grep binder /proc/filesystems || echo "(无 binderfs)"

echo "== binder.devices 默认创建的设备节点 =="
ls -l /dev/binder* 2>/dev/null || echo "(无)"

mkdir -p /mnt/bfs
if mount -t binder binder /mnt/bfs; then
    echo "== binderfs 挂载成功，初始内容 =="
    ls -l /mnt/bfs
    echo "== 支持的特性 =="
    ls /mnt/bfs/features 2>/dev/null || echo "(无 features 目录)"
else
    echo "== binderfs 挂载失败 =="
fi

reboot -f
