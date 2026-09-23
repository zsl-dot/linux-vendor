# 仓库工作规范

## 项目结构与模块组织

- `linux-source/` 是 `zsl-dot/linux` 的 Git 子模块。只在其 `work` 分支上开发；`master` 分支仅用于镜像上游 Linux。
- `lib/` 存放共享工作流代码：Shell 模块、Python 配置、Git 同步逻辑，以及 `lib/vm/` 下的 VM 基础设施。
- `vendor-module/kernel/` 下的 demo 按学习域分层（basic/comm/ebpf/container/sched/android），复用同一个内核；`vendor-module/model/` 是用户态机制模拟。
- `linux-doc/` 是学习与工作流文档的唯一存放位置。
- `build/` 存放所有可再生成的内核、demo、VM 和日志产物；已被 Git 忽略，禁止提交。

## 构建、测试与开发命令

在仓库根目录执行：

```bash
./go.sh init      # 初始化子模块并切换到 linux-source/work
./go.sh check     # 校验仓库、远端与分支不变量
./go.sh deps      # 安装/检查宿主机构建依赖
./go.sh kernel    # 编译 Linux 到 build/linux-out/
./go.sh index     # 重新生成合并后的 clangd 数据库到 build/clangd/
./go.sh demo      # 编译 demo 并在 QEMU 中验证
./go.sh           # 依赖 + 内核 + 全部 demo 验证
./go.sh sync      # 上游 → master → work rebase，并更新子模块指针
./go.sh status    # 查看内核子模块 / work 分支状态
```

`./go.sh clean` 只清理生成的 demo、VM 和日志文件；它有意保留内核构建产物。

本项目只维护一份 `build/linux-out/` 内核（x86_64_defconfig 基线 + 项目特性，见 `lib/kernel.sh`），同时服务 demo 验证、GPU/驱动学习、调度学习和 clangd 跳转——不要随意缩小配置；修改配置列表后必须递增 `lib/kernel.sh` 里的 flavor 名，否则改动不会生效。`./go.sh index` 将真实编译命令与未编译文件的兜底条目合并为 `build/clangd/compile_commands.json`，供 clangd 扩展使用（Microsoft C/C++ IntelliSense 已通过 `.vscode/settings.json` 保持禁用）。

## 代码风格与命名规范

`linux-source/` 下的代码遵循 Linux 内核风格：Tab 缩进、K&R 大括号、小写加下划线命名，使用 `pr_info()`/`pr_err()` 而非裸 `printk()`。

Shell 脚本保持 Bash 兼容并使用 `set -euo pipefail`。共享路径统一放在 `lib/workflow_config.py`；新脚本中不要硬编码 `linux-source`、`build` 或 `/tmp` 输出路径。新的 VM 模板放 `lib/vm/`，新的可运行练习放 `vendor-module/kernel/<对应学习域>/`（demo 由 `./go.sh demo` 自动发现，无需登记），说明性 Markdown 放 `linux-doc/`。

学习与知识总结类文档用中文撰写。命令、API 名称、标识符和必要的英文技术术语保持原样。

## 测试指南

改动内核构建逻辑后，运行 `./go.sh kernel`。改动 demo、VM 脚本、模块或 BPF 程序后，运行 `./go.sh demo`；失败时查看 `build/logs/`。单个 demo 可在其目录下执行 `./run.sh build`。

## 提交与 Pull Request 规范

使用简洁的 Conventional Commit 风格标题，与现有历史一致：`feat:`、`fix:`、`docs:`、`refactor:`、`chore:`。内核改动先提交到子模块仓库，再在本仓库提交更新后的 `linux-source` 指针，例如 `chore: update Linux work revision`。

PR 应说明受影响的层次（kernel、workflow、demo 或 docs）、执行的命令以及相关 QEMU/日志证据。禁止提交 `build/` 产物，也不要直接修改 `linux-source/master`。
