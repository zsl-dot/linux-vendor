#!/bin/bash
prepare_kernel() {
    step "2/4" "准备内核源码..."
    if [ ! -d "$KERNEL_SRC/include" ]; then
        echo "克隆 GitHub Fork 的完整 Linux 历史（约 6GB）..."
        git clone --progress "$LINUX_GITHUB_REPOSITORY" "$KERNEL_SRC"
    fi
    ok "内核源码已就绪: $KERNEL_SRC"
    step "3/4" "编译内核..."
    mkdir -p "$KERNEL_OUT"
    if [ ! -f "$KERNEL_OUT/.config" ]; then
        make -C "$KERNEL_SRC" O="$KERNEL_OUT" x86_64_defconfig
        "$KERNEL_SRC/scripts/config" --file "$KERNEL_OUT/.config" \
            --enable CONFIG_BPF --enable CONFIG_BPF_SYSCALL \
            --enable CONFIG_BPF_JIT --enable CONFIG_BPF_JIT_DEFAULT_ON \
            --enable CONFIG_BPF_EVENTS --enable CONFIG_DEBUG_INFO_BTF \
            --enable CONFIG_DEBUG_INFO_BTF_MODULES --enable CONFIG_9P_FS \
            --enable CONFIG_NET_9P --enable CONFIG_NET_9P_VIRTIO \
            --set-val CONFIG_FRAME_WARN 2048
        make -C "$KERNEL_SRC" O="$KERNEL_OUT" olddefconfig
    fi
    make -C "$KERNEL_SRC" O="$KERNEL_OUT" -j"$(nproc)"
    ok "内核编译完成: $KERNEL_OUT/arch/x86/boot/bzImage"
}
