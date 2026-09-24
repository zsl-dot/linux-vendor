#!/usr/bin/env python3
"""唯一公共配置：供 Python 导入，也可向 Shell 导出环境变量。"""

from __future__ import annotations

import shlex
import sys
from pathlib import Path


# 本文件位于 <project>/lib/，项目根目录是 lib 的父目录。
PROJECT_ROOT = Path(__file__).resolve().parent.parent
VENDOR_MODULE_DIR = PROJECT_ROOT / "vendor-module"
KERNEL_SRC = PROJECT_ROOT / "linux-source"
BUILD_ROOT = PROJECT_ROOT / "build"
KERNEL_OUT = BUILD_ROOT / "linux-out"
# 合并后的 clangd 编译数据库目录（真实构建条目 + 未编译文件兜底条目）
CLANGD_DIR = BUILD_ROOT / "clangd"
LEARN_OUT = BUILD_ROOT / "vendor-module"
ROOTFS_DIR = BUILD_ROOT / "vm-rootfs"
ROOTFS_IMG = BUILD_ROOT / "vm-rootfs.img"
# L3 第三通道：发行版 rootfs（真实用户态 + runc，容器运行时实验用）
DISTRO_ROOTFS_DIR = BUILD_ROOT / "vm-distro-rootfs"
DISTRO_ROOTFS_IMG = BUILD_ROOT / "vm-distro-rootfs.img"
LOG_DIR = BUILD_ROOT / "logs"
GITHUB_REPOSITORY = "git@github.com:zsl-dot/linux.git"
DEMO_GITHUB_REPOSITORY = "git@github.com:zsl-dot/linux-vendor.git"
# torvalds 官方镜像：与 git.kernel.org 逐字节一致；当前网络直连 kernel.org 会中断
KERNEL_UPSTREAM = "https://github.com/torvalds/linux.git"
KERNEL_WORK_BRANCH = "work"
KERNEL_MASTER_BRANCH = "master"


def shell_exports() -> str:
    """Return safely quoted exports for: eval "$(python3 workflow_config.py shell)"."""
    values = {
        "PROJECT_ROOT": PROJECT_ROOT,
        "VENDOR_MODULE_DIR": VENDOR_MODULE_DIR,
        "KERNEL_SRC": KERNEL_SRC,
        "BUILD_ROOT": BUILD_ROOT,
        "KERNEL_OUT": KERNEL_OUT,
        "CLANGD_DIR": CLANGD_DIR,
        "LEARN_OUT": LEARN_OUT,
        "ROOTFS_DIR": ROOTFS_DIR,
        "ROOTFS_IMG": ROOTFS_IMG,
        "DISTRO_ROOTFS_DIR": DISTRO_ROOTFS_DIR,
        "DISTRO_ROOTFS_IMG": DISTRO_ROOTFS_IMG,
        "LOG_DIR": LOG_DIR,
        "LINUX_GITHUB_REPOSITORY": GITHUB_REPOSITORY,
        "DEMO_GITHUB_REPOSITORY": DEMO_GITHUB_REPOSITORY,
        "KERNEL_UPSTREAM": KERNEL_UPSTREAM,
        "KERNEL_WORK_BRANCH": KERNEL_WORK_BRANCH,
        "KERNEL_MASTER_BRANCH": KERNEL_MASTER_BRANCH,
    }
    return "\n".join(f"export {name}={shlex.quote(str(value))}" for name, value in values.items())


if __name__ == "__main__":
    if sys.argv[1:] == ["shell"]:
        print(shell_exports())
    else:
        raise SystemExit("用法: python3 workflow_config.py shell")
