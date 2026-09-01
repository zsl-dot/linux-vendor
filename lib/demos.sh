#!/bin/bash
run_demo() {
    local name="$1" dir="$DEMO_DIR/$1" log="$LOG_DIR/$1.log"
    echo ""; echo -e "${BLUE}--- $name ---${NC}"
    [ -f "$dir/run.sh" ] || { echo "  跳过（无 run.sh）"; return; }
    if (cd "$dir" && ./run.sh build) > "$log" 2>&1; then
        ok "$name — 通过"; grep -E '===.*===' "$log" | head -3 || true
    else
        echo -e "${RED}[✗] $name — 失败${NC}"; echo "  日志: $log"; tail -20 "$log"; return 1
    fi
}

verify_all_demos() {
    step "4/4" "验证全部 demo..."
    local failed="" demo
    for demo in hello hello-proc binder-demo netlink-demo ebpf-demo1 ebpf-demo2 kgdb-demo; do
        run_demo "$demo" || failed="$failed $demo"
    done
    [ -z "$failed" ] || die "失败:$failed（日志：$LOG_DIR）"
    ok "全部 demo 验证通过！"
}

do_clean() {
    echo "清理 demo 与 QEMU 编译产物..."
    local demo
    for demo in hello hello-proc binder-demo netlink-demo ebpf-demo1 ebpf-demo2 kgdb-demo bpflib; do
        make -C "$DEMO_DIR/$demo" clean > /dev/null 2>&1 || true
    done
    make -C "$LINUX_LEARN_DIR/demo/wake_q_demo" clean > /dev/null 2>&1 || true
    make -C "$LINUX_LEARN_DIR/demo/wait_queue_demo" clean > /dev/null 2>&1 || true
    rm -rf "$LEARN_OUT" "$ROOTFS_DIR" "$ROOTFS_IMG" "$LOG_DIR"
    ok "清理完成（不删除内核编译目录：$KERNEL_OUT）"
}
