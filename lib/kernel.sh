#!/bin/bash
# 准备并编译内核 —— 项目唯一的内核产物（defconfig 基线 + 项目特性）。
#
# 配置策略：x86_64_defconfig 基线（保持 bzImage 可启动大小）+ 按学习域分组
# 显式开启的能力（与 vendor-module/kernel/<域>/ 的 demo 一一对应）：
#   - 基础/ebpf 域：调试信息 + BPF/BTF（DWARF5 是 BTF 前提，缺了会被静默丢弃）
#   - sched 域：SCHED_CLASS_EXT（eBPF 调度器）、函数级 ftrace、HIST_TRIGGERS
#   - container 域：overlayfs/veth/bridge/legacy iptables/MEMCG/CFS_BANDWIDTH/USER_NS
#   - android 域：Binder/BinderFS
#   - gpu 域：DRM VKMS / VirtIO-GPU / drm_sched KUnit
#   - virtme-ng 交互通道：9P 共享
# 一个内核同时服务：demo 的 QEMU 自动验证、GPU 驱动/调度器学习、clangd 跳转。
#
# 注意：clangd 的真实编译条目覆盖为 ~9%（defconfig 只编译部分源码），
# 其余由 lib/gen_index_db.py 的兜底条目补齐至 100%。
prepare_kernel() {
    # go.sh 的所有入口都先经过 require_ready_workspace，这里只做防御性检查；
    # 源码初始化统一由 ./go.sh init 完成。
    step "2/4" "检查内核源码..."
    [ -d "$KERNEL_SRC/include" ] || die "linux-source 内核源码不存在；先执行 ./go.sh init"
    step "3/4" "编译内核（defconfig 基线 + 项目特性）..."
    mkdir -p "$KERNEL_OUT"

    # flavor 标记：区分配置版本（不要用某个符号值判断——多个 flavor 都可能
    # 产生 CONFIG_DRM_SCHED=y，导致切换被跳过）。
    # 修改下面的 --enable 列表后，必须同步递增 flavor 名，否则会走"复用配置"分支。
    # 仅调整分组/顺序而不增删符号时无需递增（生成的 .config 完全相同）。
    local flavor="base4"
    local flavor_file="$KERNEL_OUT/.config.flavor"
    if [ ! -f "$KERNEL_OUT/.config" ] || [ "$(cat "$flavor_file" 2>/dev/null)" != "$flavor" ]; then
        make -C "$KERNEL_SRC" O="$KERNEL_OUT" x86_64_defconfig
        local cfg="$KERNEL_SRC/scripts/config --file $KERNEL_OUT/.config"
        # ---- 基础/ebpf 域：调试信息 + BPF/BTF ----
        $cfg --enable CONFIG_DEBUG_KERNEL \
             --enable CONFIG_DEBUG_INFO_DWARF5 --enable CONFIG_DEBUG_INFO_BTF \
             --enable CONFIG_DEBUG_INFO_BTF_MODULES \
             --enable CONFIG_BPF --enable CONFIG_BPF_SYSCALL \
             --enable CONFIG_BPF_JIT --enable CONFIG_BPF_JIT_DEFAULT_ON \
             --enable CONFIG_BPF_EVENTS
        # ---- sched 域：可编程调度器 + 函数级追踪 ----
        $cfg --enable CONFIG_SCHED_CLASS_EXT \
             --enable CONFIG_FUNCTION_TRACER --enable CONFIG_FUNCTION_GRAPH_TRACER \
             --enable CONFIG_DYNAMIC_FTRACE --enable CONFIG_HIST_TRIGGERS
        # ---- container 域：namespace/cgroup/overlayfs/容器网络 ----
        $cfg --enable CONFIG_OVERLAY_FS --enable CONFIG_VETH --enable CONFIG_BRIDGE \
             --enable CONFIG_NETFILTER_ADVANCED --enable CONFIG_BRIDGE_NETFILTER \
             --enable CONFIG_NETFILTER_XTABLES_LEGACY --enable CONFIG_IP_NF_IPTABLES_LEGACY \
             --enable CONFIG_NETFILTER_XT_MATCH_ADDRTYPE \
             --enable CONFIG_IP_NF_IPTABLES --enable CONFIG_IP_NF_FILTER \
             --enable CONFIG_IP_NF_NAT --enable CONFIG_IP_NF_TARGET_MASQUERADE \
             --enable CONFIG_MEMCG --enable CONFIG_CFS_BANDWIDTH \
             --enable CONFIG_FAIR_GROUP_SCHED --enable CONFIG_BLK_DEV_THROTTLING \
             --enable CONFIG_BLK_DEV_DM --enable CONFIG_DM_CRYPT \
             --enable CONFIG_USER_NS --enable CONFIG_CHECKPOINT_RESTORE \
             --enable CONFIG_VXLAN --enable CONFIG_IP_VS
        # ---- android 域：Binder IPC ----
        $cfg --enable CONFIG_ANDROID_BINDER_IPC --enable CONFIG_ANDROID_BINDERFS \
             --set-val CONFIG_ANDROID_BINDER_DEVICES "binder,hwbinder,vndbinder"
        # ---- gpu 域：DRM 软件显示 + GPU 调度器 KUnit ----
        $cfg --enable CONFIG_DRM_VKMS --enable CONFIG_DRM_VIRTIO_GPU \
             --enable CONFIG_KUNIT --enable CONFIG_DRM_SCHED_KUNIT_TEST
        # ---- virtme-ng 交互通道：9P 共享宿主目录 ----
        $cfg --enable CONFIG_9P_FS --enable CONFIG_NET_9P --enable CONFIG_NET_9P_VIRTIO
        # ---- 其他 ----
        $cfg --set-val CONFIG_FRAME_WARN 2048
        make -C "$KERNEL_SRC" O="$KERNEL_OUT" olddefconfig
        echo "$flavor" > "$flavor_file"
    else
        step "-" "复用已有 $flavor 配置 $KERNEL_OUT/.config"
    fi

    make -C "$KERNEL_SRC" O="$KERNEL_OUT" -j"$(nproc)"
    if [ ! -f "$KERNEL_OUT/arch/x86/boot/bzImage" ]; then
        die "内核编译失败：未产出 bzImage（日志：$LOG_DIR）"
    fi
    ok "内核编译完成: $KERNEL_OUT/arch/x86/boot/bzImage"
}
