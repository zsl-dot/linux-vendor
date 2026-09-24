#!/bin/sh
# 容器内 PID 1：把命名空间隔离的证据写到 bind mount 的 /out，
# 供 guest 宿主侧的 runc-check 读取判定。
{
	echo "IN_PID=$$"
	echo "IN_HOSTNAME=$(hostname)"
	echo "IN_MOUNTS=$(wc -l < /proc/self/mountinfo)"
} > /out/container-result.txt
sleep 300
