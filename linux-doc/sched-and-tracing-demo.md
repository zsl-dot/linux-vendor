# 调度与追踪学习环境（sched-demo / ebpf-demo3）

本文说明 CPU 调度方向的两个可运行 demo 及其依赖的内核配置组。背景评估结论：eBPF 机制、容器原语、GPU 驱动三方面原有环境基本齐备，CPU 调度是最薄弱的方向——函数级 ftrace、sched_ext 未开，且 BTF 实际未生效（`CONFIG_DEBUG_INFO_NONE=y`，`DEBUG_INFO_BTF` 被静默丢弃）。

## 内核配置组（lib/kernel.sh，flavor `base2`）

| 配置 | 用途 |
|---|---|
| `CONFIG_DEBUG_INFO_DWARF5` + `CONFIG_DEBUG_INFO_BTF` | BTF 的前提。只开 BTF 不开 DWARF5 时，`olddefconfig` 会静默丢弃 BTF（pahole 已由 `./go.sh deps` 检查） |
| `CONFIG_SCHED_CLASS_EXT` | sched_ext：用 eBPF 程序实现调度器类，调度与 eBPF 的交叉点 |
| `CONFIG_FUNCTION_TRACER` / `CONFIG_FUNCTION_GRAPH_TRACER` / `CONFIG_DYNAMIC_FTRACE` | 函数级 ftrace，观察调度器内部调用路径 |
| `CONFIG_HIST_TRIGGERS` | tracefs 直方图触发器（`CONFIG_SCHED_DEBUG` 已被上游移除，无需再开） |

**流程要点**：修改 `lib/kernel.sh` 的 `--enable` 列表后必须递增 flavor 名（如 `base` → `base2`），否则 `prepare_kernel` 走"复用配置"分支，修改不生效。

## sched-demo（vendor-module/kernel/sched-demo/）

QEMU 内以 `init=/sched-check` 无人值守验证五项能力：

1. **tracefs + sched tracepoint**：`/sys/kernel/tracing/events/sched/sched_switch/id` 存在；
2. **sched_switch 事件记录**：启用事件后跑负载，从 trace 缓冲区统计捕获条数；
3. **函数级 ftrace**：`current_tracer` 切到 `function`，确认捕获函数调用（未开 `FUNCTION_TRACER` 时 SKIP）；
4. **CFS 带宽限流**：cgroup v2 下设 `cpu.max = 2000 100000`（20% CPU），跑满 3 秒后读 `cpu.stat`，要求 `nr_throttled > 0`；
5. **sched_ext 可用性**：`/sys/kernel/sched_ext` 是否存在（未开时 SKIP）。

## ebpf-demo3（vendor-module/kernel/ebpf-demo3/）

与 ebpf-demo1（kprobe）对照的**静态 tracepoint** demo：挂 `sched:sched_switch`，每次调度切换打印一条 `eBPF: sched_switch`。

与 kprobe 的机制差异：kprobe 需向 `kprobe_events` 动态注册探针换取 perf 事件 ID；静态 tracepoint 的 ID 直接读 `events/sched/sched_switch/id`，两者经同一个 `perf_event_open(PERF_TYPE_TRACEPOINT)` + `PERF_EVENT_IOC_SET_BPF` 通道挂载 BPF 程序。

## 建议的下一步实验

- 用 KGDB 在 `__schedule` / `pick_next_task` 断点，结合 `perf sched` / `trace-cmd record -e sched:sched_switch` 对照源码理解选核逻辑；
- BTF 生效后，尝试 libbpf CO-RE 程序与 `fentry` 挂载（按 BTF ID 附着，无需 kprobe 注册）；
- 编译内核树 `tools/sched_ext/` 下的 `scx_simple` / `scx_qmap`，在 virtme-ng 中加载 eBPF 调度器观察行为。
