#!/bin/bash
# 本目录保存 rootfs 构建脚本和 init 模板；生成的 rootfs 一律写入 build/vm-rootfs/。
# 用法: ./lib/vm/mk-rootfs.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
eval "$(python3 "$SCRIPT_DIR/../workflow_config.py" shell)"

echo "=== 生成根文件系统: $ROOTFS_DIR ==="

# 如果已存在则跳过（增量模式）
if [ -f "$ROOTFS_DIR/bin/busybox" ]; then
    echo "[跳过] 根文件系统已存在"
    exit 0
fi

rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"/{bin,dev,proc,sys,etc,root,lib,mnt,tmp}

# ---- busybox ----
BUSYBOX=$(command -v busybox 2>/dev/null || echo "/usr/bin/busybox")
if [ ! -f "$BUSYBOX" ]; then
    echo "错误: 未找到 busybox，请安装: sudo apt install busybox-static"
    exit 1
fi
cp "$BUSYBOX" "$ROOTFS_DIR/bin/busybox"
chmod +x "$ROOTFS_DIR/bin/busybox"

# ---- 创建 busybox 常用命令的符号链接 ----
SYMLINKS="sh ls cat echo grep mount umount sleep ps kill cp mv rm mkdir rmdir \
    chmod chown ln readlink basename dirname true false test mknod \
    dmesg insmod rmmod lsmod modprobe modinfo depmod \
    date dd df du head tail wc find xargs sed sort uniq cut tr \
    stat sync pidof printf seq strings tee touch tty uname usleep vi \
    gzip gunzip tar xz xzcat zcat which who whoami yes \
    ping ping6 route ifconfig ip netstat nc wget telnet tftp \
    id passwd su login getty poweroff reboot halt \
    free top uptime killall fdisk swapon swapoff mountpoint \
    less more od xxd hexdump expr md5sum sha256sum \
    awk bc dc env time watch setsid unshare taskset \
    mke2fs mkdosfs mkswap blkdiscard blockdev \
    sysctl klogd syslogd logread logger \
    acpid crond crontab httpd telnetd udhcpc udhcpd \
    bunzip2 bzip2 bzcat cpio dpkg dpkg-deb rpm rpm2cpio \
    ar arp arping brctl chgrp chpasswd chroot chvt clear cmp \
    crc32 cttyhack deallocvt devmem diff dos2unix unix2dos \
    dumpleases dumpkmap ed egrep fgrep env expand factor fallocate \
    fatattr findfs fold freeramdisk fsfreeze fstrim ftpget ftpput \
    getopt groups hexdump hostid hostname hwclock i2cdetect \
    i2cdump i2cget i2cset i2ctransfer ifdown ifup ionice ipcalc \
    last link linux32 linux64 linuxrc loadfont loadkmap logname \
    losetup lsscsi lzcat lzma lzop mdev microcom mim mkpasswd mt \
    nameif nbd-client nl nologin nproc nsenter nslookup nuke \
    openvt partprobe paste patch pivot_root rdate realpath renice \
    reset resume rev run-init run-parts setkeycodes setpriv \
    sha1sum sha3sum sha512sum shred shuf ssl_client start-stop-daemon \
    static-sh strings stty sulogin svc svok switch_root \
    tac traceroute traceroute6 truncate ts \
    tunctl ubirename udhcpc6 uevent uncompress unexpand unlink unlzma \
    unzip uptime uudecode uuencode vconfig watch watchdog \
    wget xargs"

for cmd in $SYMLINKS; do
    [ ! -e "$ROOTFS_DIR/bin/$cmd" ] && ln -s /bin/busybox "$ROOTFS_DIR/bin/$cmd"
done

# ---- 基础文件 ----
cat > "$ROOTFS_DIR/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/sh
EOF

cat > "$ROOTFS_DIR/etc/group" << 'EOF'
root:x:0:
EOF

# ---- init 脚本 ----
cp "$SCRIPT_DIR/init" "$ROOTFS_DIR/init"
chmod +x "$ROOTFS_DIR/init"

# ---- poweroff 辅助脚本 ----
cat > "$ROOTFS_DIR/bin/off" << 'EOF'
#!/bin/sh
reboot -f
EOF
chmod +x "$ROOTFS_DIR/bin/off"

echo "[完成] 根文件系统已生成: $ROOTFS_DIR"
