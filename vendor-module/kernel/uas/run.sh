#!/bin/bash
# 在通用 Linux/QEMU 工作台中验证 UAS vendor 源码的结构完整性。
#
# UAS 是面向 Android vendor BSP 的调度组件，依赖 Android.bp、厂商内核
# 接口和特定 SoC 的 scheduler 扩展，不能直接作为 x86 QEMU 模块加载。本
# demo 因此验证源码清单、Kbuild 对象引用和 Android 接口层，而不是伪造一
# 次不具备代表性的 insmod 测试。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../env.sh"

PERFORMANCE_DIR="$SCRIPT_DIR/performance"
KERNEL_MODULE_DIR="$PERFORMANCE_DIR/kernel_module"
KBUILD_FILE="$KERNEL_MODULE_DIR/Kbuild"

usage() {
	cat <<'EOF'
用法: ./run.sh [build|check|update]

build/check/update 都执行 UAS vendor 源码结构验证。UAS 依赖 Android
vendor BSP，当前工作台不会尝试在通用 x86 Linux 内核上编译它。
EOF
}

die() {
	echo "UAS demo: $*" >&2
	exit 1
}

require_file() {
	local relative_path="$1"
	[ -f "$SCRIPT_DIR/$relative_path" ] \
		|| die "缺少必需文件: $relative_path"
}

check_kbuild_sources() {
	local object source relative_path count=0

	while IFS= read -r object; do
		[ -n "$object" ] || continue
		source="${object%.o}.c"
		relative_path="performance/kernel_module/$source"
		[ -f "$SCRIPT_DIR/$relative_path" ] \
			|| die "Kbuild 引用了不存在的源文件: $relative_path"
		count=$((count + 1))
	done < <(sed -n 's/^[[:space:]]*trans_sched-y[[:space:]]*+=[[:space:]]*//p' "$KBUILD_FILE")

	[ "$count" -gt 0 ] || die "Kbuild 未找到 trans_sched-y 对象"
	echo "Kbuild 对象引用: $count 个，全部可解析"
}

check_interfaces() {
	local aidl_count hal_count cpp_count java_count bpf_count

	aidl_count="$(find "$PERFORMANCE_DIR" -type f -name '*.aidl' | wc -l)"
	hal_count="$(find "$PERFORMANCE_DIR" -type f -name '*.hal' | wc -l)"
	cpp_count="$(find "$PERFORMANCE_DIR" -type f \( -name '*.cpp' -o -name '*.h' \) | wc -l)"
	java_count="$(find "$PERFORMANCE_DIR" -type f -name '*.java' | wc -l)"
	bpf_count="$(find "$PERFORMANCE_DIR" -type f \( -path '*/bpf/*' -o -name '*.bpf.c' \) | wc -l)"

	[ "$aidl_count" -gt 0 ] || die "未找到 AIDL 接口"
	[ "$hal_count" -gt 0 ] || die "未找到 HIDL 接口"
	[ "$cpp_count" -gt 0 ] || die "未找到 C++ 实现"
	[ "$java_count" -gt 0 ] || die "未找到 framework Java 实现"
	[ "$bpf_count" -gt 0 ] || die "未找到 BPF 实现"

	echo "接口层: AIDL=$aidl_count HIDL=$hal_count C++/头文件=$cpp_count Java=$java_count BPF=$bpf_count"
}

run_check() {
	local revision="unknown"

	[ -d "$PERFORMANCE_DIR" ] || die "未找到 performance 源码目录"
	[ -f "$KBUILD_FILE" ] || die "未找到 UAS 内核模块 Kbuild"

	for required in \
		performance/Android.bp \
		performance/kernel_module/Makefile \
		performance/kernel_module/include/trans_sched.h \
		performance/sched/aidl/Android.bp \
		performance/sched/1.0/ITransSched.hal \
		performance/framework/com/transsion/sched/service/TranSchedService.java \
		performance/trankeythread/bpf/tranKeyTask.c; do
		require_file "$required"
	done

	# UAS 原始目录曾是独立 Git 仓库；导入主仓库后不应误报主仓库提交号。
	if [ -d "$SCRIPT_DIR/.git" ] && git -C "$SCRIPT_DIR" rev-parse --short HEAD >/dev/null 2>&1; then
		revision="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD)"
	else
		revision="imported snapshot"
	fi

	grep -q 'hidl_package_root' "$PERFORMANCE_DIR/Android.bp" \
		|| die "Android.bp 缺少 HIDL package 声明"
	grep -q 'TRANS_SCHED_FEATURE' "$KBUILD_FILE" \
		|| die "Kbuild 缺少 TRANS_SCHED_FEATURE 配置入口"
	grep -q 'trans_sched-y' "$KBUILD_FILE" \
		|| die "Kbuild 缺少 trans_sched 对象列表"

	check_kbuild_sources
	check_interfaces

	echo "UAS 源码版本: $revision"
	echo "=== UAS vendor demo done ==="
}

case "${1:-build}" in
	build|check|update)
		echo "=== UAS vendor demo ==="
		run_check
		;;
	-h|--help|help)
		usage
		;;
	*)
		usage >&2
		exit 2
		;;
esac
