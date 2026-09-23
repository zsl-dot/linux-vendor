#!/bin/bash
# 统一的颜色与日志输出；go.sh 与各 lib 模块共用。
GREEN='\033[0;32m'; BLUE='\033[0;34m'; RED='\033[0;31m'; NC='\033[0m'
step() { echo -e "${BLUE}[$1]${NC} $2"; }
ok()   { echo -e "${GREEN}[✓]${NC} $1"; }
die()  { echo -e "${RED}[✗]${NC} $1"; exit 1; }

install_deps() {
    step "1/4" "检查系统依赖..."
    local missing=""
    # 检查的是"可执行文件存在"；libssl/libelf 等库依赖由 apt 安装列表兜底。
    # pahole(dwarves) 是 CONFIG_DEBUG_INFO_BTF 的硬依赖，缺失会被静默丢弃。
    for pkg in gcc clang make git qemu-system-x86_64 busybox \
               flex bison bc cpio pahole mke2fs; do
        command -v "$pkg" &>/dev/null || missing="$missing $pkg"
    done
    if [ -z "$missing" ]; then ok "依赖就绪"; return; fi
    echo "缺少:$missing"
    if [ "${1:-}" != "--auto" ]; then
        read -rp "是否安装? [Y/n] " answer
        [ "$answer" = "n" ] && die "请手动安装依赖"
    fi
    sudo apt update && sudo apt install -y build-essential clang llvm \
        gcc-multilib qemu-system-x86 qemu-utils busybox-static \
        e2fsprogs git make flex bison libssl-dev libelf-dev bc cpio dwarves gawk
    ok "依赖安装完成"
}
