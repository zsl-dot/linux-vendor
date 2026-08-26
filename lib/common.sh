#!/bin/bash
step() { echo -e "${BLUE}[$1]${NC} $2"; }
ok()   { echo -e "${GREEN}[✓]${NC} $1"; }
die()  { echo -e "${RED}[✗]${NC} $1"; exit 1; }

install_deps() {
    step "1/4" "检查系统依赖..."
    local missing=""
    for pkg in gcc clang make git qemu-system-x86_64 busybox; do
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
        e2fsprogs git make flex bison libssl-dev libelf-dev bc cpio dwarves
    ok "依赖安装完成"
}
