#!/usr/bin/env python3
"""合并 clangd 编译数据库：真实构建条目 + 未编译文件的兜底条目（单内核方案）。

真实条目来自 KERNEL_OUT/compile_commands.json（由 gen_compile_commands.py 生成，
只覆盖当前 .config 实际编译到的文件）。本脚本为其余 .c 文件按所在 arch 合成
一条内核风格的编译命令，让 clangd 对整棵源码树都能跳转。

用法: python3 lib/gen_index_db.py
"""

from __future__ import annotations

import json
import shlex
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from workflow_config import CLANGD_DIR, KERNEL_OUT, KERNEL_SRC  # noqa: E402

# 内核之外的用户态示例代码，用默认（不含 -nostdinc）命令兜底
USERSPACE_DIRS = ("Documentation", "tools", "scripts")


def arch_of(rel: Path) -> str:
    """推断文件所属 arch；非 arch/ 下的文件按索引构建所用 arch（x86）处理。"""
    parts = rel.parts
    if len(parts) > 2 and parts[0] == "arch":
        return parts[1]
    return "x86"


def kernel_flags(rel: Path, arch: str) -> list[str]:
    """构造内核风格的基础编译参数（不含 -W*，避免 clangd 报大量告警）。"""
    gen = KERNEL_OUT / "include" / "generated"
    flags = [
        "-nostdinc",
        f"-I{KERNEL_SRC / 'arch' / arch / 'include'}",
        f"-I{KERNEL_SRC / 'arch' / arch / 'include' / 'uapi'}",
        f"-I{KERNEL_SRC / 'include'}",
        f"-I{KERNEL_SRC / 'include' / 'uapi'}",
        f"-I{KERNEL_OUT / 'include'}",
        f"-I{gen}",
        f"-I{gen / 'uapi'}",
    ]
    # 只有真正构建过的 arch 才有 generated asm 头文件
    arch_gen = KERNEL_OUT / "arch" / arch / "include" / "generated"
    if arch_gen.is_dir():
        flags += [f"-I{arch_gen}", f"-I{arch_gen / 'uapi'}"]
    flags += [
        f"-include {KERNEL_SRC / 'include' / 'linux' / 'compiler-version.h'}",
        f"-include {KERNEL_SRC / 'include' / 'linux' / 'kconfig.h'}",
        f"-include {KERNEL_SRC / 'include' / 'linux' / 'compiler_types.h'}",
        "-D__KERNEL__",
        f"-include {gen / 'autoconf.h'}",
        "-std=gnu11",
        "-fms-extensions",
        "-fno-strict-aliasing",
        "-funsigned-char",
    ]
    modname = rel.stem
    flags += [
        f"-DKBUILD_MODFILE='\"{rel}\"'",
        f"-DKBUILD_BASENAME='\"{modname}\"'",
        f"-DKBUILD_MODNAME='\"{modname}\"'",
        f"-D__KBUILD_MODNAME={modname}",
    ]
    return flags


def synth_entry(path: Path, rel: Path) -> dict:
    if rel.parts[0] in USERSPACE_DIRS:
        # 用户态示例/工具：保留 gcc 默认系统头文件搜索路径
        command = f"gcc -std=gnu11 -c -o /dev/null {shlex.quote(str(path))}"
    else:
        flags = kernel_flags(rel, arch_of(rel))
        command = " ".join(["gcc", *flags, "-c -o /dev/null", shlex.quote(str(path))])
    return {"directory": str(KERNEL_OUT), "command": command, "file": str(path)}


def main() -> int:
    real_db_path = KERNEL_OUT / "compile_commands.json"
    if not real_db_path.is_file():
        raise SystemExit(f"缺少 {real_db_path}；先执行 ./go.sh kernel 完成内核编译")
    if not (KERNEL_OUT / "include" / "generated" / "autoconf.h").is_file():
        raise SystemExit(f"缺少 {KERNEL_OUT}/include/generated/autoconf.h；索引构建未完成")

    real_db = json.loads(real_db_path.read_text())
    known = {entry["file"] for entry in real_db}

    sources = sorted(p for p in KERNEL_SRC.rglob("*.c") if ".git" not in p.parts)
    source_set = {str(p) for p in sources}
    real_covered = len(source_set & known)
    missing = sorted(p for p in sources if str(p) not in known)

    merged = list(real_db)
    merged += [synth_entry(p, p.relative_to(KERNEL_SRC)) for p in missing]

    CLANGD_DIR.mkdir(parents=True, exist_ok=True)
    out = CLANGD_DIR / "compile_commands.json"
    out.write_text(json.dumps(merged, indent=2))

    print(f"源码 .c 文件总数      : {len(sources)}")
    print(f"真实构建覆盖          : {real_covered} ({real_covered / len(sources):.1%})")
    print(f"兜底条目              : {len(missing)}")
    print(f"合并后总条目          : {len(merged)}")
    print(f"输出                  : {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
