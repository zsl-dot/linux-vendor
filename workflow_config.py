#!/usr/bin/env python3
"""唯一公共配置：供 Python 导入，也可向 Shell 导出环境变量。"""

from __future__ import annotations

import shlex
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent
LINUX_LEARN_DIR = PROJECT_ROOT / "linux-learn"
KERNEL_SRC = PROJECT_ROOT / "linux-source"
KERNEL_OUT = PROJECT_ROOT / "build"
LEARN_OUT = KERNEL_OUT / "linux-learn"
ROOTFS_DIR = PROJECT_ROOT / "vm-rootfs"
ROOTFS_IMG = PROJECT_ROOT / "vm-rootfs.img"
GITHUB_REPOSITORY = "git@github.com:zsl-dot/linux.git"
DEMO_GITHUB_REPOSITORY = "git@github.com:zsl-dot/linux-demo.git"
KERNEL_UPSTREAM = "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git"
KERNEL_WORK_BRANCH = "work"
KERNEL_MASTER_BRANCH = "master"


def shell_exports() -> str:
    """Return safely quoted exports for: eval "$(python3 workflow_config.py shell)"."""
    values = {
        "PROJECT_ROOT": PROJECT_ROOT,
        "LINUX_LEARN_DIR": LINUX_LEARN_DIR,
        "KERNEL_SRC": KERNEL_SRC,
        "KERNEL_OUT": KERNEL_OUT,
        "LEARN_OUT": LEARN_OUT,
        "ROOTFS_DIR": ROOTFS_DIR,
        "ROOTFS_IMG": ROOTFS_IMG,
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
