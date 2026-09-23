#!/bin/sh
# 调度学习环境验证（guest 内运行，root）。
# 由 run.sh 注入 rootfs 为 /sched-check，QEMU 以 init=/sched-check 启动。
# 验证：sched tracepoint 记录、函数级 ftrace、cgroup v2 CPU 带宽限流、sched_ext。
# 每项测试输出一行 "RESULT: PASS/FAIL/SKIP <名称>"，供宿主机 grep 汇总。

PASS=0; FAIL=0; SKIP=0
pass() { echo "RESULT: PASS $1"; PASS=$((PASS+1)); }
fail() { echo "RESULT: FAIL $1"; FAIL=$((FAIL+1)); }
skip() { echo "RESULT: SKIP $1"; SKIP=$((SKIP+1)); }

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null

echo "=== 调度学习环境验证 内核 $(uname -r) ==="

# ---- 定位并挂载 tracefs ----
TRACING=/sys/kernel/tracing
mkdir -p "$TRACING" 2>/dev/null
if ! mount -t tracefs tracefs "$TRACING" 2>/dev/null; then
    mkdir -p /sys/kernel/debug 2>/dev/null
    if mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null; then
        TRACING=/sys/kernel/debug/tracing
    fi
fi

# ---------- T1 tracefs 就绪 + sched tracepoint 存在 ----------
if [ -f "$TRACING/events/sched/sched_switch/id" ]; then
    pass "tracefs-sched-tracepoints（sched_switch id=$(cat "$TRACING/events/sched/sched_switch/id")）"
else
    fail "tracefs-sched-tracepoints（tracefs 未挂载或 sched 事件缺失）"
fi

# ---------- T2 sched_switch tracepoint 能捕获调度事件 ----------
if [ -f "$TRACING/events/sched/sched_switch/enable" ]; then
    echo 1 > "$TRACING/events/sched/sched_switch/enable"
    echo > "$TRACING/trace"
    end=$(( $(date +%s) + 1 ))
    while [ "$(date +%s)" -lt "$end" ]; do :; done &
    SPIN=$!
    wait "$SPIN" 2>/dev/null
    echo 0 > "$TRACING/events/sched/sched_switch/enable"
    n=$(grep -c "sched_switch" "$TRACING/trace" 2>/dev/null)
    if [ "${n:-0}" -gt 0 ] 2>/dev/null; then
        pass "sched-switch-trace（捕获 ${n} 条调度事件）"
    else
        fail "sched-switch-trace（未捕获到调度事件）"
    fi
fi

# ---------- T3 函数级 ftrace（CONFIG_FUNCTION_TRACER）----------
if grep -qw function "$TRACING/available_tracers" 2>/dev/null; then
    echo function > "$TRACING/current_tracer"
    echo > "$TRACING/trace"
    end=$(( $(date +%s) + 1 ))
    while [ "$(date +%s)" -lt "$end" ]; do :; done &
    SPIN=$!
    wait "$SPIN" 2>/dev/null
    fn=$(grep -c ' <-' "$TRACING/trace" 2>/dev/null)
    echo nop > "$TRACING/current_tracer"
    if [ "${fn:-0}" -gt 0 ] 2>/dev/null; then
        pass "function-tracer（捕获 ${fn} 条函数调用，如 $(grep -m1 ' <-' "$TRACING/trace" | sed 's/.*: //' | cut -c1-60)）"
    else
        fail "function-tracer（tracer 可用但未捕获函数调用）"
    fi
else
    skip "function-tracer（内核未启用 CONFIG_FUNCTION_TRACER）"
fi

# ---------- T4 cgroup v2 CPU 带宽限流（CFS_BANDWIDTH）----------
mkdir -p /tmp/cg
if mount -t cgroup2 none /tmp/cg 2>/dev/null; then
    echo "+cpu" > /tmp/cg/cgroup.subtree_control 2>/dev/null
    mkdir -p /tmp/cg/limited
    # period=100ms，quota=20ms → 20% CPU，跑 3 秒必然触发限流
    echo "2000 10000" > /tmp/cg/limited/cpu.max
    end=$(( $(date +%s) + 3 ))
    while [ "$(date +%s)" -lt "$end" ]; do :; done &
    SPIN=$!
    echo "$SPIN" > /tmp/cg/limited/cgroup.procs
    wait "$SPIN" 2>/dev/null
    nr_throttled=$(awk '/^nr_throttled/ {print $2}' /tmp/cg/limited/cpu.stat)
    throttled_us=$(awk '/^throttled_usec/ {print $2}' /tmp/cg/limited/cpu.stat)
    if [ "${nr_throttled:-0}" -gt 0 ] 2>/dev/null && [ "${throttled_us:-0}" -gt 0 ] 2>/dev/null; then
        pass "cfs-bandwidth-throttle（nr_throttled=${nr_throttled}，被限流 ${throttled_us}us）"
    else
        fail "cfs-bandwidth-throttle（nr_throttled=${nr_throttled:-?}，未观察到限流）"
    fi
else
    fail "cfs-bandwidth-throttle（cgroup2 挂载失败）"
fi

# ---------- T5 sched_ext（可编程调度器类，可选）----------
if [ -d /sys/kernel/sched_ext ]; then
    state=""
    [ -f /sys/kernel/sched_ext/state ] && state="state=$(cat /sys/kernel/sched_ext/state)"
    pass "sched-ext-available（/sys/kernel/sched_ext 存在${state:+，$state}）"
else
    skip "sched-ext-available（内核未启用 CONFIG_SCHED_CLASS_EXT）"
fi

echo "RESULT: SUMMARY pass=$PASS fail=$FAIL skip=$SKIP"
echo "=== 调度环境验证结束，关机 ==="
poweroff -f
