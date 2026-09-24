#!/bin/sh
# drm-skeleton 驱动验证（guest 内运行，root）。
# 由 run.sh 注入 rootfs 为 /drm-skeleton-check，QEMU 以 init=/drm-skeleton-check 启动。
# 验证：模块加载→/dev/dri 节点→modetest 枚举→完整 modeset（enable 回调）。
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null

PASS=0; FAIL=0
pass() { echo "RESULT: PASS $1"; PASS=$((PASS+1)); }
fail() { echo "RESULT: FAIL $1"; FAIL=$((FAIL+1)); }

echo "=== drm-skeleton 验证 内核 $(uname -r) ==="

# ---------- T1 模块加载与设备注册 ----------
if insmod /root/modules/skeleton.ko 2>/dev/null; then
	if dmesg | grep -q 'drm-skeleton initialized'; then
		pass "module-loaded（probe 成功，设备已注册）"
	else
		pass "module-loaded（insmod 成功）"
	fi
else
	fail "module-loaded（insmod 失败：$(dmesg | tail -2 | head -1 | cut -c1-70)）"
fi

# ---------- T2 /dev/dri 设备节点 ----------
if ls /dev/dri/card* >/dev/null 2>&1; then
	pass "dri-device-node（$(ls /dev/dri | tr '\n' ' ')）"
else
	fail "dri-device-node（无 /dev/dri/card*）"
fi

# ---------- T3 modetest 枚举 connector 与模式 ----------
echo "--- modetest 原始输出（诊断用）---"
modetest -M skeleton 2>&1 | head -25
echo "--- /sys/class/drm ---"
ls /sys/class/drm 2>/dev/null
CONN=$(modetest -M skeleton 2>/dev/null |
	awk '/^Connectors:/{f=1;next} /^CRTCs:/{f=0} f && $1 ~ /^[0-9]+$/ && NF>4 {print $1; exit}')
MODE=$(modetest -M skeleton 2>/dev/null |
	awk '/^[[:space:]]*#0[[:space:]]/{print $2; exit}')
if [ -n "$CONN" ] && [ -n "$MODE" ]; then
	pass "connector-modes（connector=$CONN mode=$MODE）"
else
	fail "connector-modes（modetest 未发现资源）"
fi

# ---------- T4 完整 modeset（atomic commit → pipe enable 回调）----------
if [ -n "$CONN" ] && [ -n "$MODE" ]; then
	timeout 5 modetest -M skeleton -s "$CONN:$MODE" >/dev/null 2>&1
	if dmesg | grep -q 'pipe enabled'; then
		pass "modeset-callback（$(dmesg | grep 'pipe enabled' | tail -1 | sed 's/.*] //' | cut -c1-60)）"
	else
		fail "modeset-callback（modeset 执行但 enable 回调未记录）"
	fi
else
	fail "modeset-callback（无 connector/mode）"
fi

echo "RESULT: SUMMARY pass=$PASS fail=$FAIL"
echo "=== drm-skeleton 验证结束，关机 ==="
poweroff -f
