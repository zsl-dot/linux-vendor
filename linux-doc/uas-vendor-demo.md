# UAS vendor 组件分析与验证

`vendor-module/kernel/uas/performance/` 是 Transsion Android vendor 的 UAS
（调度性能）组件快照，当前快照来自 `tran_master_main` 的提交
`59411ff93c8f20cd16e553f463d973f102183b2f`。它不是面向通用 Linux 的独立
内核模块，而是 Android BSP 的一部分。

## 组件边界

| 目录 | 内容 | 依赖 |
| --- | --- | --- |
| `kernel_module/` | `trans_sched` 内核模块、Kconfig 和多版本 Kbuild | 厂商 scheduler 扩展、SoC 内核头文件 |
| `sched/` | HIDL/AIDL 接口、服务、client 和 VTS 测试 | Android Soong、Binder、vendor 分区 |
| `framework/` | framework Java 服务 | Android framework 类库 |
| `trankeythread/` | BPF 程序和用户态封装 | Android BPF/libbpf、BTF 和 tracepoint |

顶层 `Kbuild` 引用了 `TRANS_SCHED_FEATURE`、`drivers/unisoc_platform`、
MediaTek UFS/scheduler 路径以及 Android vendor 目录。这些路径在本项目的
通用 x86 `linux-source/work` 中不存在，因此不能用普通 demo 的 Makefile 模板
编译，也不能在 QEMU 中直接 `insmod`。

## 工作台中的 demo 形式

`vendor-module/kernel/uas/run.sh` 提供结构验证入口：

```bash
./vendor-module/kernel/uas/run.sh build
./vendor-module/kernel/uas/run.sh check
./vendor-module/kernel/uas/run.sh update
```

脚本检查以下内容：

1. Android.bp、AIDL/HIDL、framework Java、BPF 和内核模块入口文件存在；
2. `kernel_module/Kbuild` 声明了 `TRANS_SCHED_FEATURE`；
3. `trans_sched-y` 中的 17 个 C 源文件都能解析到本地路径；
4. 接口层和实现层文件数量非零。

它属于“源码结构验证型 vendor demo”，结果会被 `./go.sh demo` 纳入统一
验证，但不会把 Android 专用代码伪装成可在 QEMU 加载的 x86 模块。

## 在真实 BSP 中编译

应在匹配的 Android BSP 根目录执行 Soong/内核构建，例如使用 `m` 或 `mm`
构建对应模块，并按 `performance/kernel_module/build/README.md` 注册
`ko_order_table.csv` 或 `ext_modules.list`。需要使用目标 SoC 的内核配置、
vendor 头文件和 BTF 环境；本项目的 `./go.sh kernel` 不能替代该流程。
