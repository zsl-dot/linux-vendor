#!/bin/bash
# virtme-ng: 在自定义内核上验证 Vulkan 栈（lavapipe 软渲染，不依赖 GPU 与窗口系统）
#
# 思路：virtme-ng 把宿主用户态共享给 guest，因此 guest 里可直接用宿主的 Mesa /
# lavapipe，而内核是自己编译的 build/linux-out。
#
# 依赖: sudo apt install virtme-ng vulkan-tools mesa-vulkan-drivers
#
# 产物（宿主机上可见，位于 build/vk-out/）:
#   kernel.txt      guest 内核版本 —— 证明跑的是自己的内核
#   vulkaninfo.txt   guest 里 Vulkan 实例与设备信息
#
# ---- virtme-ng 在非交互环境下的两个坑 ----
# 1. vng 的控制台是 stdio chardev：必须 </dev/null 并把输出重定向到文件，
#    否则 guest 输出全部丢失（表现为"命令没输出、也没有报错"）。
# 2. --rwdir 只接受宿主机上真实存在的路径（guest 内同路径可写），
#    不支持 guestpath=hostpath 形式；guest 路径不存在会导致挂载失败。
# 另外 vng 1.22 的 --arch 只接受交叉架构名（arm64/armhf/...），
# 本机 x86 直接省略该参数。
#
# ---- 已知未解决 ----
# guest 里跑 vkcube（X 窗口）能起来，但 Xvfb + scrot 截出来是黑屏（时序/窗口抓捕
# 问题，与内核无关）。宿主上用同样方法可以正常截到立方体。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../../../vendor-module/env.sh"
check_kernel_source
check_kernel_build

OUT_DIR="$PROJECT_ROOT/build/vk-out"
mkdir -p "$OUT_DIR"

export PATH="$HOME/.local/bin:$PATH"
if ! command -v vng &>/dev/null; then
    echo "错误: 未安装 virtme-ng"
    echo "安装: sudo apt install virtme-ng"
    exit 1
fi

echo "=== 在自定义内核上验证 Vulkan ==="
echo "内核: $KERNEL_OUT"
echo "产物: $OUT_DIR"
echo ""

vng --cpus 4 --memory 2G --rwdir="$OUT_DIR" --run "$KERNEL_OUT" -- bash -c "
uname -r > $OUT_DIR/kernel.txt
# 强制使用 lavapipe（CPU 版 Vulkan），不依赖任何 GPU
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
vulkaninfo --summary > $OUT_DIR/vulkaninfo.txt 2>&1 || true
" >"$LOG_DIR/vng-vulkan.log" 2>&1 </dev/null

echo "=== guest 结果 ==="
echo "内核版本: $(cat "$OUT_DIR/kernel.txt" 2>/dev/null)"
grep -E "deviceName|driverName|apiVersion|deviceType" "$OUT_DIR/vulkaninfo.txt" 2>/dev/null | head -6
echo "控制台日志: $LOG_DIR/vng-vulkan.log"
