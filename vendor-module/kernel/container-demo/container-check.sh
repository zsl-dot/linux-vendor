#!/bin/sh
# 容器化内核能力验证（guest 内运行，root）。
# 由 run.sh 注入 rootfs 为 /container-check，QEMU 以 init=/container-check 启动。
# 每项测试输出一行 "RESULT: PASS/FAIL/SKIP <名称>"，供宿主机 grep 汇总。

PASS=0; FAIL=0; SKIP=0
pass() { echo "RESULT: PASS $1"; PASS=$((PASS+1)); }
fail() { echo "RESULT: FAIL $1"; FAIL=$((FAIL+1)); }
skip() { echo "RESULT: SKIP $1"; SKIP=$((SKIP+1)); }

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null

echo "=== 容器化内核能力验证 内核 $(uname -r) ==="

# ---------- T1 UTS namespace：子进程改主机名，父环境不受影响 ----------
h1=$(hostname)
if unshare -u sh -c 'hostname container-ns-test' 2>/dev/null; then
    if [ "$(hostname)" = "$h1" ]; then
        pass "uts-namespace"
    else
        fail "uts-namespace（宿主主机名被改）"
    fi
else
    skip "uts-namespace（unshare 不支持）"
fi

# ---------- T2 PID namespace：新命名空间内 PID 从 1 开始 ----------
pid_in_ns=$(unshare -p -f sh -c 'echo $$' 2>/dev/null)
if [ "$pid_in_ns" = "1" ]; then
    pass "pid-namespace"
else
    fail "pid-namespace（新 ns 内 pid=$pid_in_ns，期望 1）"
fi

# ---------- T3 network namespace：子进程看不到宿主网卡 ----------
ip link show eth0 >/dev/null 2>&1 && host_eth=yes || host_eth=no
unshare -n sh -c 'ip link show eth0' >/dev/null 2>&1 && ns_eth=yes || ns_eth=no
if [ "$host_eth" = "yes" ] && [ "$ns_eth" = "no" ]; then
    pass "net-namespace"
else
    fail "net-namespace（host_eth=$host_eth ns_eth=$ns_eth）"
fi

# ---------- T4 mount namespace：unshare -m 的挂载不影响父进程 ----------
mkdir -p /tmp/mns
if unshare -m sh -c 'mount -t tmpfs tmpfs /tmp/mns && touch /tmp/mns/probe' 2>/dev/null; then
    if [ ! -e /tmp/mns/probe ]; then
        pass "mount-namespace"
    else
        fail "mount-namespace（挂载泄漏到父环境）"
    fi
else
    skip "mount-namespace"
fi

# ---------- T5 overlayfs：lower 可见 + 写时复制落到 upper ----------
mkdir -p /tmp/ovl/l /tmp/ovl/u /tmp/ovl/w /tmp/ovl/m
echo base-content > /tmp/ovl/l/f1
if mount -t overlay overlay \
    -o lowerdir=/tmp/ovl/l,upperdir=/tmp/ovl/u,workdir=/tmp/ovl/w \
    /tmp/ovl/m 2>/dev/null; then
    ok=1
    [ "$(cat /tmp/ovl/m/f1 2>/dev/null)" = "base-content" ] || ok=0
    echo upper-content > /tmp/ovl/m/f2
    [ "$(cat /tmp/ovl/u/f2 2>/dev/null)" = "upper-content" ] || ok=0
    if [ "$ok" = "1" ]; then
        pass "overlayfs"
    else
        fail "overlayfs（copy-up 语义异常）"
    fi
    umount /tmp/ovl/m 2>/dev/null
else
    fail "overlayfs（挂载失败，CONFIG_OVERLAY_FS 未开？）"
fi

# ---------- T6 cgroups v2：可挂载，memory/cpu/pids 控制器可用 ----------
mkdir -p /tmp/cg2
if mount -t cgroup2 none /tmp/cg2 2>/dev/null; then
    ctrl=$(cat /tmp/cg2/cgroup.controllers 2>/dev/null)
    ok=1
    for c in memory cpu pids; do
        echo "$ctrl" | grep -qw "$c" || ok=0
    done
    mkdir /tmp/cg2/probe
    [ -d /tmp/cg2/probe ] || ok=0
    if [ "$ok" = "1" ]; then
        pass "cgroups-v2（controllers: $(echo $ctrl | tr ' ' ',')）"
    else
        fail "cgroups-v2（controllers=$ctrl）"
    fi
else
    fail "cgroups-v2（挂载失败）"
fi

# ---------- T7 user namespace（可选）：UID 映射 ----------
if unshare -U -r -f true 2>/dev/null; then
    umap=$(unshare -U -r -f sh -c 'cat /proc/self/uid_map' 2>/dev/null | awk 'NR==1{print $1" "$2" "$3}')
    [ -n "$umap" ] && pass "user-namespace（uid_map: $umap）" || fail "user-namespace"
else
    skip "user-namespace（unshare 不支持 -U）"
fi

echo "RESULT: SUMMARY pass=$PASS fail=$FAIL skip=$SKIP"
echo "=== 容器化验证结束，关机 ==="
poweroff -f
