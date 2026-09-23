#!/bin/bash
# 生成 clangd 全源码跳转数据库（单内核方案）。
#
# 项目只有一个内核产物 build/linux-out（x86_64_defconfig 基线 + 项目特性，见 lib/kernel.sh）：
#   1) 真实条目 = linux-out/compile_commands.json（defconfig 下只覆盖实际编译到的文件，~9%）
#   2) 兜底条目 = 为未编译到的 .c 文件按 x86 内核风格合成编译命令
# 合并输出 build/clangd/compile_commands.json（.vscode 的 clangd 已指向该文件）。

build_index_db() {
    mkdir -p "$CLANGD_DIR"

    step "1/2" "确认内核编译数据库..."
    if [ ! -f "$KERNEL_OUT/compile_commands.json" ]; then
        make -C "$KERNEL_SRC" O="$KERNEL_OUT" compile_commands.json
    fi

    step "2/2" "合并真实条目与兜底条目..."
    python3 "$SCRIPT_DIR/lib/gen_index_db.py"

    ok "clangd 编译数据库已生成: $CLANGD_DIR/compile_commands.json"
}
