# UAS vendor demo

详细的组件边界、依赖和验证策略见
[`linux-doc/uas-vendor-demo.md`](../../../linux-doc/uas-vendor-demo.md)。

`performance/` 是 Transsion Android vendor 的 UAS（调度性能）组件，包含：

- `kernel_module/`：厂商内核调度模块和 Kbuild 配置；
- `sched/`：HIDL/AIDL 服务、client 和 VTS 测试；
- `framework/`：Android framework Java 服务；
- `trankeythread/`：BPF 和用户态封装。

该组件依赖 Android vendor BSP、SoC scheduler 扩展和 Android 构建系统，不能
直接在本项目的通用 x86 Linux 内核或 QEMU 中加载。因此这里将它作为“源码结构
验证型” vendor demo：

```bash
./run.sh build       # 检查源码清单、接口层和 Kbuild 对象引用
./run.sh check       # 与 build 等价
./run.sh update      # 增量接入时重复结构检查
```

脚本会确认 Android.bp、AIDL/HIDL、framework Java、BPF 和内核模块源文件均在，
并解析 `kernel_module/Kbuild` 中的 `trans_sched-y` 对象列表。真正的 Android
设备编译应在对应 BSP 中执行 `m`/`mm`，不能用本仓库的 `./go.sh kernel` 替代。
