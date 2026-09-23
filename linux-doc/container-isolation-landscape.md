# 业界容器化与隔离方案全景

回答一个问题：云服务的容器化，是不是都用 `namespace + cgroup` 那一套？
撰写时间：2026-09。阅读前建议先看
[docker-wsl-principles.md](docker-wsl-principles.md)。

## 0. 结论

**地基是同一套，但云厂商几乎不会"裸用"它。**

`namespace + cgroup + overlayfs` 是所有容器方案的**共同内核地基**。区别在：
共享宿主内核这件事，在多租户云上**不被接受**，所以各家在上面叠了不同强度的隔离层。

一句话记忆：

> namespace / cgroup 决定"这是容器"；
> VM / TEE / 用户态内核 决定"这个容器敢不敢跑别人的代码"。

---

## 1. 分层看业界的实际栈

```
编排/交付层   Kubernetes（事实标准，2026 已进入 1.3x）
              └ Serverless 容器：Fargate / Cloud Run / 阿里云 ECI / 腾讯云 TKE Serverless
运行时代理层   containerd、CRI-O（Kubernetes CRI 的实现）
运行时层       runc（默认，即 namespace + cgroup）
              ├ Kata Containers          → 轻量 VM
              ├ gVisor                   → 用户态内核
              ├ Firecracker              → microVM
              └ Confidential Containers  → TEE 加密 VM
内核层         namespace / cgroups / overlayfs / seccomp / capabilities   ← 地基不变
```

关键点：**接口统一（OCI 镜像规范 + CRI 运行时接口），底层可替换**。
所以 `kubectl run` 不用改，换一个 RuntimeClass 就从 runc 切到 Kata。

---

## 2. 为什么不都用 runc

runc 容器的隔离边界是**内核软件逻辑**，不是硬件。逃逸等价于一个内核漏洞打穿整台机器。
云上跑的是互不信任的租户，于是有了这些加固路线：

| 方案 | 隔离边界在哪 | 开销 | 典型使用者 |
|---|---|---|---|
| **runc**（原生容器） | 内核 namespace | 几乎为零 | 自建集群、可信负载，仍是主流默认 |
| **Kata Containers** | 每个 Pod 一个轻量 VM（QEMU / cloud-hypervisor + 裁剪 guest kernel） | 内存略增，启动百毫秒级 | 腾讯云 TKE 安全容器、OpenStack、蚂蚁/阿里系 |
| **gVisor** | 用户态内核（Sentry 自行实现 syscall），不把 syscall 交给宿主内核 | syscall 密集负载有损耗 | Google Cloud Run、GAE、GKE Sandbox |
| **Firecracker** | microVM，砍掉全部无用设备，启动 <125ms | 极低 | AWS Lambda、Fargate 的底座 |
| **Confidential Containers (CoCo)** | VM + TEE（Intel TDX / AMD SEV-SNP），内存加密 + 远程证明 | 较大 | CNCF 项目，**2026-07-08 晋升 Incubating** |

### Serverless 容器的真相

对外是"容器接口"，对内是 **VM 级隔离**：

- AWS Fargate → microVM（Firecracker 系）
- Google Cloud Run → gVisor
- 阿里云 ECI、腾讯云 TKE Serverless → 安全沙箱（Kata 系）

账单按 Pod 计费，但租户之间必须 VM 级隔开——这是 Serverless 容器几乎都走
安全容器的根本原因。

---

## 3. 这套方案里正在变化的部件

### 3.1 镜像/存储：overlayfs 不再是唯一

containerd 的 snapshotter 是可插拔的：`overlayfs`、`native`、`devmapper`、`blockfile`。
大规模集群更关键的是**懒加载**：Nydus / eStargz 把镜像按需拉取，冷启动从
"拉完整个镜像"变成"只拉用到的块"，秒级降到百毫秒级。

### 3.2 网络：iptables 正在被 eBPF 取代

`kube-proxy` 的 iptables 模式在上万 service 时规则数爆炸、更新是 O(n)。
Cilium / Calico 的 eBPF 数据面把转发逻辑塞进内核里的 eBPF 程序，
已成为新建集群的常见默认。这是 eBPF 在云上最大的战场。

### 3.3 新增需求：AI Agent 代码执行沙箱（2026 最热）

让 LLM 生成的代码可安全执行，microVM（Firecracker / Kata）是目前生产环境
被广泛认可的方案，gVisor 作为更轻的次选。这一方向的讨论量在 2026 增长很快。

---

## 4. 什么时候不用容器

| 场景 | 实际做法 |
|---|---|
| 大模型训练 | 多为裸金属 / GPU VM，容器只做环境封装，调度靠 Slurm 或 K8s |
| Windows 容器 | 完全不同的一套（Host Compute Service），不是 Linux namespace |
| macOS / Windows 开发机 | Docker Desktop = 虚拟机里跑容器 |
| 边缘 / 极短任务 | WASM 运行时（WasmEdge、Spin）在抢占 |
| 强合规数据 | 机密容器（TEE）或物理隔离 |

---

## 5. 对本仓库学习路径的启示

无论 Kata / gVisor / Firecracker 怎么包装，**guest 里面照样是
namespace + cgroup + overlayfs**。差别只在"边界放在哪一层"：

```
runc   : 边界在内核软件逻辑
Kata   : 边界在 VMM / 硬件虚拟化
gVisor : 边界在用户态内核
CoCo   : 边界在 CPU 的内存加密引擎
```

所以进程管理、cgroup、eBPF 这三块到哪都通用；多出来的只是"边界在哪"的取舍。

### 可做的验证实验（本机）

```bash
# 1) 手搓 mini 容器，确认 namespace 隔离
unshare --map-root-user --pid --mount-proc --uts --fork bash

# 2) 同一份负载在 runc 与安全容器下的启动开销对比（需装 kata-containers）
#    kubectl apply 一个 RuntimeClass=kata 的 Pod，对比 time kubectl get -w

# 3) 观察容器网络是 iptables 还是 eBPF
iptables -t nat -L | wc -l          # 大规模集群里这个数字会很惊人
bpftool prog list | grep -i cilium  # eBPF 方案下这里能看到转发程序
```

---

## 6. 参考

- [CNCF Confidential Containers](https://www.cncf.io/projects/confidential-containers/)（2026-07 晋升 Incubating）
- [Kata Containers](https://katacontainers.io/)
- [gVisor](https://gvisor.dev/)、[Firecracker](https://firecracker-microvm.github.io/)
- [腾讯云 TKE 安全容器（Kata）文档](https://cloud.tencent.com/document/product/457/129421)
- [阿里云弹性容器实例 ECI](https://www.aliyun.com/product/eci)
- 内核文档：`Documentation/admin-guide/cgroup-v2.rst`、`Documentation/networking/`（eBPF 相关）
