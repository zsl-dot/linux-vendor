# Linux Demo 工作流

本项目由两个 GitHub 仓库和一个官方上游组成：

```text
github.com/torvalds/linux（官方镜像，与 kernel.org 逐字节一致）
        →  zsl-dot/linux:master  →  zsl-dot/linux:work
                                       ↓ submodule
                               zsl-dot/linux-vendor:main
```

> 上游拉取地址在 `lib/workflow_config.py` 的 `KERNEL_UPSTREAM`，默认使用
> GitHub 官方镜像；当前网络直连 git.kernel.org 会中断，如可直连可改回。

- `zsl-dot/linux`：内核源码仓库。`master` 只同步官方上游，`work` 只放自己的内核改动。
- `zsl-dot/linux-vendor`：本仓库。保存构建、QEMU 验证、学习 demo、文档与工作流规则。
- `linux-doc/`：唯一的知识文档目录；`vendor-module/` 只保留可运行 demo。
- `linux-source/`：本仓库的 Git 子模块，固定在 `zsl-dot/linux` 的某个 `work` 提交。
- `build/`：内核、demo、QEMU rootfs 和日志的唯一可再生成产物目录，禁止提交。
- `lib/vm/`：QEMU/rootfs 的脚本和模板源码，必须提交；不是产物目录。

## 日常开发

```bash
# 1. 修改内核并验证
cd linux-source
git switch work
# 编辑源码
cd ..
./go.sh kernel
./go.sh demo

# 2. 提交内核改动到 Linux Fork
cd linux-source
git add <文件>
git commit -m "说明"
git push

# 3. 更新 linux-demo 记录的子模块提交
cd ..
git add linux-source
git commit -m "chore: update Linux work revision"
git push
```

## 同步官方上游

仅通过总入口执行，避免手工混用分支：

```bash
./go.sh sync
```

它会执行：官方上游 → Fork `master` → 本地/远程 `work` rebase → 更新根仓库子模块指针。
如果产生冲突，先在 `linux-source/` 解决并完成 rebase，然后执行 `git push --force-with-lease`；最后回到根目录提交子模块指针。

## 从零复刻（删库重来也按此流程）

前提：Ubuntu 22.04+/Debian 12+（x86_64）、约 15GB 空闲磁盘、两台仓库的 SSH key
（`zsl-dot/linux` 与 `zsl-dot/linux-vendor`）；有 `/dev/kvm` 更佳（demo 自动启用，
无 KVM 时回退 TCG，启动慢但可跑）。

```bash
# 1. 克隆主仓库 + 内核子模块（子模块约 6GB 完整历史，供 rebase/bisect 使用）
git clone --recurse-submodules git@github.com:zsl-dot/linux-vendor.git
cd linux-vendor

# 2. 子模块切到 work 分支并关联远端
./go.sh init

# 3. 检查/安装依赖（pahole/flex/bison 等都在检查列表，BTF 依赖 pahole）
./go.sh deps

# 4. 编译内核（首次全量约 10 分钟；配置由 lib/kernel.sh 的 flavor 自动生成）
./go.sh kernel

# 5. 全部 demo 在 QEMU 中验证（应全部通过；日志在 build/logs/）
./go.sh demo

# 6.（可选）clangd 全源码跳转数据库
./go.sh index
```

验证复刻成功的标志：`./go.sh demo` 末尾输出 `全部 demo 验证通过！`，
且任意 demo 的串口日志（`build/logs/*-run.log`）开头可见
`linux-vendor: hello from the work branch`（work 分支自带的启动横幅）。

```bash
# 确认子模块状态正确
./go.sh status        # 应显示 work 分支、origin 指向 zsl-dot/linux
cd linux-source && git log --oneline -1   # 顶部是自己的提交，其下是上游 tip
```

## 不变量

1. 不直接向 `master` 提交自己的改动。
2. 内核代码只在 `linux-source` 的 `work` 分支提交。
3. 工作流、demo、文档只在本仓库的 `main` 分支提交。
4. 每次 `work` 变更后，都提交根仓库的 `linux-source` 子模块指针。
