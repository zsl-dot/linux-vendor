#!/usr/bin/env python3
"""Manage a full Linux checkout with GitHub master/work branches.

GitHub keeps the full Linux history in the fork. This program clones that full
history so that logs, bisect, and rebases have complete ancestry available.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from workflow_config import (
    GITHUB_REPOSITORY,
    KERNEL_MASTER_BRANCH,
    KERNEL_SRC,
    KERNEL_UPSTREAM,
    KERNEL_WORK_BRANCH,
)


def run(*args: str, cwd: Path | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, check=True)


def output(*args: str, cwd: Path) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def require_clean(repo: Path) -> None:
    if output("git", "status", "--porcelain", cwd=repo):
        raise SystemExit("工作区或暂存区有未提交修改；请先提交或 stash 后再同步。")


def verify_checkout(repo: Path) -> None:
    if not (repo / ".git").exists():
        raise SystemExit(f"不是有效的 Linux Git 检出：{repo}")
    origin = output("git", "remote", "get-url", "origin", cwd=repo)
    if origin != GITHUB_REPOSITORY:
        raise SystemExit(f"origin 必须为 {GITHUB_REPOSITORY}，当前是 {origin}")


def status(args: argparse.Namespace) -> None:
    repo = args.directory.resolve()
    verify_checkout(repo)
    print(f"Linux checkout: {repo}")
    print(output("git", "status", "--short", "--branch", cwd=repo))
    print("origin:", output("git", "remote", "get-url", "origin", cwd=repo))
    print("HEAD:", output("git", "log", "-1", "--oneline", cwd=repo))


def init(args: argparse.Namespace) -> None:
    repo = args.directory.resolve()
    if repo.exists() and not repo.is_dir():
        raise SystemExit(f"目标不是目录，拒绝覆盖：{repo}")
    if repo.exists() and any(repo.iterdir()):
        raise SystemExit(f"目标目录不为空，拒绝覆盖：{repo}")
    run("git", "clone", "--progress", args.repository, str(repo))
    remote_work = output("git", "ls-remote", "--heads", "origin", "work", cwd=repo)
    if remote_work:
        run("git", "switch", "--track", "origin/work", cwd=repo)
    else:
        run("git", "switch", "-c", "work", "master", cwd=repo)
    run("git", "push", "--progress", "-u", "origin", "work", cwd=repo)
    print(f"完成：当前目录 {repo}，当前分支 work。")


def update_master_from_fork(repo: Path) -> None:
    run("git", "fetch", "--progress", "origin", cwd=repo)
    run("git", "switch", KERNEL_MASTER_BRANCH, cwd=repo)
    run("git", "merge", "--ff-only", f"origin/{KERNEL_MASTER_BRANCH}", cwd=repo)


def sync(args: argparse.Namespace) -> None:
    repo = args.directory.resolve()
    verify_checkout(repo)
    require_clean(repo)
    update_master_from_fork(repo)
    run("git", "switch", KERNEL_WORK_BRANCH, cwd=repo)
    run("git", "rebase", KERNEL_MASTER_BRANCH, cwd=repo)
    run("git", "push", "--progress", "--force-with-lease", "origin", KERNEL_WORK_BRANCH, cwd=repo)
    print("完成：work 已基于最新 master rebase 并推送。")


def sync_upstream(args: argparse.Namespace) -> None:
    """Fetch current kernel.org tip and update the GitHub fork's master."""
    repo = args.directory.resolve()
    verify_checkout(repo)
    require_clean(repo)
    remotes = output("git", "remote", cwd=repo).splitlines()
    if "upstream" not in remotes:
        run("git", "remote", "add", "upstream", args.upstream, cwd=repo)
    else:
        run("git", "remote", "set-url", "upstream", args.upstream, cwd=repo)
    run("git", "fetch", "--progress", "upstream", KERNEL_MASTER_BRANCH, cwd=repo)
    # GitHub already owns the full ancestry of this fork, so this is an
    # ordinary fast-forward update.
    run(
        "git", "push", "--progress", "origin",
        f"upstream/{KERNEL_MASTER_BRANCH}:{KERNEL_MASTER_BRANCH}", cwd=repo,
    )
    sync(args)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--directory", type=Path, default=KERNEL_SRC, help="本地仓库目录（默认：配置中的 linux-source）")
    p.add_argument("--repository", default=GITHUB_REPOSITORY, help="GitHub Fork SSH 地址")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("init", help="首次完整克隆，并创建/切换到 work")
    sub.add_parser("sync", help="拉取 GitHub master，将 work rebase 到 master")
    sub.add_parser("status", help="检查 Linux 子模块、远程和当前提交")
    upstream = sub.add_parser("sync-upstream", help="从 kernel.org 更新 GitHub master，再 rebase work")
    upstream.add_argument("--upstream", default=KERNEL_UPSTREAM, help="上游 Linux 地址")
    return p


def main() -> None:
    args = parser().parse_args()
    try:
        {"init": init, "sync": sync, "status": status, "sync-upstream": sync_upstream}[args.command](args)
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from error


if __name__ == "__main__":
    main()
