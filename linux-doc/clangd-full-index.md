# VS Code 全量源码跳转（单内核方案）

本仓库**只有一个内核产物** `build/linux-out/`（allyesconfig，见 `lib/kernel.sh`）。
它既是 demo 验证/Vulkan/GPU 驱动学习的内核，也是 clangd 全源码跳转的真实编译来源。

分析日期：2026-09-19。环境排查相关问题见 [env-troubleshooting.md](env-troubleshooting.md)。

---

## 1. 一次性环境准备

1. VS Code 安装 `clangd` 扩展；宿主机安装 `clangd` 可执行文件（`sudo apt install clangd`）。
2. `.vscode/settings.json` 已配置：
   - `"clangd.path": "/usr/bin/clangd"`、`--compile-commands-dir=${workspaceFolder}/build/clangd`
   - `"C_Cpp.intelliSenseEngine": "disabled"`（避免与 clangd 抢 C/C++ 跳转）

## 2. 生成跳转数据库（`./go.sh index`）

单内核方案分两步（`lib/index.sh`）：

1. **真实条目**：`build/linux-out/compile_commands.json`——allyesconfig 下内核
   实际编译了大部分源码（GPU 驱动、drm_sched、调度器、文件系统……），
   这部分条目**精确**。
2. **兜底条目**：`lib/gen_index_db.py` 为未编译到的 `.c` 文件按 x86 内核风格
   合成编译命令（`-nostdinc` + 各 include 目录 + `-include autoconf.h` +
   `-D__KERNEL__`），使整棵源码树 100% 有条目。

输出：`build/clangd/compile_commands.json`（`.vscode/settings.json` 的
`--compile-commands-dir` 已指向该目录）。

```bash
./go.sh kernel      # 先完成内核编译（会顺带产出 compile_commands.json）
./go.sh index       # 合并真实条目与兜底条目
```

然后在 VS Code 里 **Developer: Reload Window**，等 clangd 索引完成即可跳转。

## 3. 覆盖率说明

| 类别 | 说明 |
|---|---|
| 真实构建条目 | allyesconfig 下覆盖大部分 `.c`（内核核心、主流驱动、文件系统等），**解析精确** |
| 兜底条目 | 未编译到的文件（如部分架构相关代码、`Documentation/`、`tools/` 用户态示例），能跳转但个别符号可能解析不准 |

实测（allyesconfig）：真实构建覆盖约 71%+（数据随内核版本变化），兜底补齐至 100%。
验证方式：

```bash
clangd --check=linux-source/fs/read_write.c --compile-commands-dir=build/clangd
```

## 4. 历史方案（已废弃）：独立的 allyesconfig 构建目录

早期实现曾用第二个构建目录 `build/linux-idx/`（`make allyesconfig -k`）生成真实条目，
再合并兜底条目。**已废弃**，原因：

- 项目定位为「单一内核产物」，两个内核目录（13GB）造成认知与维护负担
- `-k` 会吞掉链接失败（曾因缺 `gawk` 导致 `modules.builtin.ranges` 生成失败、
  vmlinux 链接错误被掩盖——`scripts/Makefile.vmlinux:148` 需要 `gawk`）
- `lib/kernel.sh` 改用 allyesconfig 后，唯一内核本身就是全量构建，
  无需第二个目录

历史遗留：`build/linux-idx/` 已删除；`lib/workflow_config.py` 中的 `INDEX_OUT` 已移除。

## 5. 排查清单

1. `clangd --version` 可用；`settings.json` 的 `clangd.path` 指向它
2. `build/clangd/compile_commands.json` 存在且条目数合理
3. 只启用一个 C/C++ 语言服务（clangd；cpptools 已 disabled）
4. 抽样验证：`clangd --check=<file> --compile-commands-dir=build/clangd`
5. 内核重新编译后无需重跑 `./go.sh index`（compile_commands.json 随构建更新，
   clangd 会自动感知）；只有源码树新增文件且未被编译时才需要
