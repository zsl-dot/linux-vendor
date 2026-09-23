#!/bin/bash
# 自动发现 $DEMO_DIR 下所有带 run.sh 的 demo；新增 demo 无需再改本文件。
all_kernel_demos() {
    local d
    for d in "$DEMO_DIR"/*/run.sh; do
        [ -f "$d" ] || continue
        basename "$(dirname "$d")"
    done | sort
}

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
    for demo in $(all_kernel_demos); do
        run_demo "$demo" || failed="$failed $demo"
    done
    [ -z "$failed" ] || die "失败:$failed（日志：$LOG_DIR）"
    ok "全部 demo 验证通过！"
}

do_clean() {
    echo "清理 demo 与 QEMU 编译产物..."
    local mk
    for mk in "$DEMO_DIR"/*/Makefile "$VENDOR_MODULE_DIR"/model/*/Makefile; do
        [ -f "$mk" ] || continue
        make -C "$(dirname "$mk")" clean > /dev/null 2>&1 || true
    done
    rm -rf "$LEARN_OUT" "$ROOTFS_DIR" "$ROOTFS_IMG" "$LOG_DIR"
    ok "清理完成（不删除内核编译目录：$KERNEL_OUT）"
}
