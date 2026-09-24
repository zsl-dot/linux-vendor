#!/bin/sh
# runc 容器运行时验证（L3 发行版 rootfs，guest 内 root，init=/runc-check）。
# 在自定义内核 + 真实 Debian 用户态上，用 runc 启动一个 busybox 容器，
# 验证 pid/uts/mount 命名空间隔离、exec、cgroup v2 集成。
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /dev/shm
mount -t devpts devpts /dev/pts 2>/dev/null || true

PASS=0; FAIL=0
pass() { echo "RESULT: PASS $1"; PASS=$((PASS+1)); }
fail() { echo "RESULT: FAIL $1"; FAIL=$((FAIL+1)); }

echo "=== runc 容器验证（$(/usr/bin/runc --version 2>/dev/null | head -1)）内核 $(uname -r) ==="

# cgroup v2（runc cgroupfs 模式需要）
mkdir -p /sys/fs/cgroup
mount -t cgroup2 none /sys/fs/cgroup 2>/dev/null || true

# ---------- T1 runc 二进制可用 ----------
if /usr/bin/runc --version >/dev/null 2>&1; then
	pass "runc-binary（$(/usr/bin/runc --version | head -1)）"
else
	fail "runc-binary（Debian rootfs 缺 runc）"
fi

# ---------- T2 启动容器（detach）----------
rm -f /out/container-result.txt
if /usr/bin/runc run -b /bundle -d demo-container 2>/tmp/runc-err; then
	sleep 2
	if /usr/bin/runc state demo-container 2>/dev/null | grep -q 'running'; then
		pass "container-running（runc run -d 成功）"
	else
		fail "container-running（state 非 running）"
	fi
else
	echo "--- runc 错误详情 ---"
	head -5 /tmp/runc-err 2>/dev/null
	fail "container-running（详见上方错误详情）"
fi

# ---------- T3 命名空间隔离证据（容器内 PID 1 写出）----------
if [ -f /out/container-result.txt ]; then
	in_pid=$(sed -n 's/^IN_PID=//p' /out/container-result.txt)
	in_host=$(sed -n 's/^IN_HOSTNAME=//p' /out/container-result.txt)
	in_mounts=$(sed -n 's/^IN_MOUNTS=//p' /out/container-result.txt)
	out_mounts=$(wc -l < /proc/self/mountinfo)

	if [ "$in_pid" = "1" ]; then
		pass "pid-namespace（容器内 init 是 PID 1）"
	else
		fail "pid-namespace（容器内 PID=$in_pid，期望 1）"
	fi

	if [ "$in_host" = "demo-container" ] && [ "$(hostname)" != "demo-container" ]; then
		pass "uts-namespace（容器 hostname=$in_host，宿主=$(hostname)）"
	else
		fail "uts-namespace（in=$in_host host=$(hostname)）"
	fi

	if [ -n "$in_mounts" ] && [ "$in_mounts" != "$out_mounts" ]; then
		pass "mount-namespace（容器挂载树 $in_mounts 项 ≠ 宿主 $out_mounts 项）"
	else
		fail "mount-namespace（in=$in_mounts out=$out_mounts，挂载树相同？）"
	fi
else
	echo "--- 容器内诊断 ---"
	/usr/bin/runc exec demo-container /bin/sh -c 'id; ls /out' 2>&1 | head -4
	fail "namespace-evidence（容器未写出 /out/container-result.txt）"
fi

# ---------- T4 exec 进入运行中的容器 ----------
if /usr/bin/runc exec demo-container /bin/echo exec-ok 2>/dev/null | grep -q exec-ok; then
	pass "runc-exec（在运行容器内执行命令）"
else
	fail "runc-exec"
fi

# ---------- T5 cgroup v2 集成 ----------
if [ -d /sys/fs/cgroup/demo-cg ]; then
	pass "cgroup-v2（/sys/fs/cgroup/demo-cg 已创建）"
else
	fail "cgroup-v2（未找到容器 cgroup）"
fi

# 清理
/usr/bin/runc kill demo-container KILL 2>/dev/null || true
sleep 1
/usr/bin/runc delete demo-container 2>/dev/null || true

echo "RESULT: SUMMARY pass=$PASS fail=$FAIL"
echo "=== runc 容器验证结束，关机 ==="
poweroff -f
