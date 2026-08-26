# Repository Guidelines

## Project Structure & Module Organization

- `linux-source/` is a Git submodule of `zsl-dot/linux`. Work only on its `work` branch; `master` mirrors upstream Linux.
- `lib/` contains shared workflow code: shell modules, Python configuration, Git synchronization, and VM infrastructure in `lib/vm/`.
- `linux-learn/` contains runnable kernel-module, BPF, and userspace simulation demos.
- `linux-doc/` is the single location for learning and workflow documentation.
- `build/` contains all generated kernel, demo, VM, and log outputs. It is ignored and must not be committed.

## Build, Test, and Development Commands

Run commands from the repository root:

```bash
./go.sh init      # initialize submodules and select linux-source/work
./go.sh check     # verify repository, remote, and branch invariants
./go.sh deps      # install/check host build dependencies
./go.sh kernel    # build Linux into build/linux-out/
./go.sh demo      # build demos and verify them in QEMU
./go.sh           # dependencies + kernel + all demo verification
./go.sh sync      # upstream → master → work rebase; updates submodule pointer
```

Use `./go.sh clean` only for generated demo, VM, and log files; it intentionally preserves the kernel build output.

## Coding Style & Naming Conventions

Follow Linux kernel style for code under `linux-source/`: tabs for indentation, K&R braces, lower-case-with-underscores names, and `pr_info()`/`pr_err()` rather than raw `printk()`.

Keep shell scripts Bash-compatible with `set -euo pipefail`. Put shared paths in `lib/workflow_config.py`; do not hardcode `linux-source`, `build`, or `/tmp` output paths in new scripts. Place new VM templates in `lib/vm/`, new runnable exercises in `linux-learn/`, and explanatory Markdown in `linux-doc/`.

Write learning and knowledge-summary documentation in Chinese. Keep commands, API names, identifiers, and necessary English technical terms unchanged.

## Testing Guidelines

After changing kernel build logic, run `./go.sh kernel`. After changing a demo, VM script, module, or BPF program, run `./go.sh demo`; inspect `build/logs/` on failure. For an individual demo, run its `run.sh build` from its directory.

## Commit & Pull Request Guidelines

Use concise Conventional Commit-style subjects, as in existing history: `feat:`, `fix:`, `docs:`, `refactor:`, or `chore:`. Keep kernel changes in the submodule repository, then commit the updated `linux-source` pointer here, for example `chore: update Linux work revision`.

PRs should state the affected layer (kernel, workflow, demo, or docs), commands run, and relevant QEMU/log evidence. Do not commit `build/` outputs or modify `linux-source/master` directly.
