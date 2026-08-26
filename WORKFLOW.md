# Linux Demo 工作流

本项目由两个 GitHub 仓库和一个官方上游组成：

```text
kernel.org/torvalds/linux  →  zsl-dot/linux:master  →  zsl-dot/linux:work
                                                          ↓ submodule
                                                   zsl-dot/linux-demo:main
```

- `zsl-dot/linux`：内核源码仓库。`master` 只同步官方上游，`work` 只放自己的内核改动。
- `zsl-dot/linux-demo`：本仓库。保存构建、QEMU 验证、学习 demo、文档与工作流规则。
- `linux-source/`：本仓库的 Git 子模块，固定在 `zsl-dot/linux` 的某个 `work` 提交。
- `build/`、`vm-rootfs/`、`vm-rootfs.img`：可再生成产物，禁止提交。

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

## 克隆到新机器

```bash
git clone --recurse-submodules git@github.com:zsl-dot/linux-demo.git
cd linux-demo
./go.sh check
./go.sh deps
./go.sh kernel
./go.sh demo
```

## 不变量

1. 不直接向 `master` 提交自己的改动。
2. 内核代码只在 `linux-source` 的 `work` 分支提交。
3. 工作流、demo、文档只在本仓库的 `main` 分支提交。
4. 每次 `work` 变更后，都提交根仓库的 `linux-source` 子模块指针。
