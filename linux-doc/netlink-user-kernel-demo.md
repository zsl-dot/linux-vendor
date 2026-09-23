# Netlink：用户进程与内核模块双向通信示例

本项目的 `vendor-module/kernel/netlink-demo/` 演示用户进程与内核模块如何通过 Netlink 进行“请求—响应”通信。

```text
netlink-client                 netlink_demo.ko
      │ sendmsg()                    │ .input callback
      ├──── NETLINK_USERSOCK ───────►│ 接收 "hello netlink"
      │                              │ nlmsg_unicast()
      │◄──── NETLINK_USERSOCK ───────┤ 返回 "kernel reply: ..."
      │ recv()
```

## 关键接口

内核模块通过 `netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg)` 创建一个内核 Netlink socket，并将 `demo_receive()` 作为消息到达回调。回调从 `NETLINK_CB(skb).portid` 取得发送进程的端口 ID，再调用 `nlmsg_unicast()` 将响应精确发回该进程。

用户程序创建 `socket(AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK)`，以 `getpid()` 作为本地 `nl_pid` 并 `bind()`；目标地址的 `nl_pid = 0` 代表内核。它用 `sendmsg()` 发送 `nlmsghdr`，再通过 `recv()` 接收模块的响应。

## 原始 Netlink 与 Generic Netlink

### 层次关系

Generic Netlink 不是与 Netlink 完全并列的另一种通信机制，而是建立在 Netlink 之上的通用协议层：

```text
用户程序 / 内核模块
        │
        │ family、command、attribute、policy
        ▼
Generic Netlink
        │
        │ Netlink 消息、socket、port ID、skb
        ▼
Netlink 传输层
```

可以把它类比为 HTTP 与 TCP 的关系：TCP 提供传输能力，HTTP 在 TCP 之上定义请求方法、字段和响应语义；Netlink 提供用户空间与内核之间的消息传输能力，Generic Netlink 在其上定义更规范的命令和属性组织方式。

当前示例使用的是较底层的 Netlink：

```c
#define NETLINK_DEMO_PROTOCOL NETLINK_USERSOCK
```

用户程序和内核模块直接使用同一个协议号、自行构造 `struct nlmsghdr`，并把字符串放入消息 payload：

```text
Netlink 消息头
└── "hello netlink"
```

内核收到消息后，通过一个 `.input` 回调统一处理：

```text
demo_receive()
    ├── 检查 skb 和 nlmsghdr
    ├── 取得发送方 port ID
    ├── 解析 payload
    └── nlmsg_unicast() 发送回复
```

这类原始 Netlink 用法代码少、便于理解底层机制，但消息类型、payload 格式、参数校验、命令分派和协议版本都需要应用和内核自行约定。

### Generic Netlink 增加了什么

Generic Netlink 通常把一个正式接口组织成一个 family，family 下包含多个 command，每个 command 携带一个或多个 attribute，并可以使用 policy 对 attribute 做校验：

```text
family: MY_MODULE
├── command: GET_STATUS
├── command: SET_VALUE
└── command: RESET

attribute:
├── VALUE  (u32)
├── NAME   (string)
└── FLAGS  (u32)
```

例如，设置一个值的请求可以表达为：

```text
family: MY_MODULE
command: SET_VALUE
attribute: VALUE = 42
```

内核处理过程是：

```text
收到 Generic Netlink 消息
        ↓
根据 family 找到接口
        ↓
根据 command 分派处理函数
        ↓
解析 attribute
        ↓
根据 policy 检查类型、长度和值范围
        ↓
执行操作并返回结构化响应
```

其中：

- `family`：一组相关内核用户接口的名称空间，例如 `MY_MODULE`。
- `command`：具体操作，例如 `GET_STATUS`、`SET_VALUE` 或 `RESET`。
- `attribute`：带类型和长度的数据字段，例如 `u32`、字符串或嵌套属性。
- `policy`：属性校验规则，例如类型、最大长度和数值范围。

Generic Netlink 使用通用的 `NETLINK_GENERIC` 传输协议，并通过 family ID、command ID 和 attribute type 区分不同接口和字段。用户空间可以直接使用 Netlink API，也可以使用 `libnl`、`libmnl` 等库减少消息封装和解析代码。

### 两种方式的对比

| 对比项 | 原始 Netlink | Generic Netlink |
|---|---|---|
| 所处层次 | 直接使用 Netlink 协议 | 建立在 Netlink 之上的协议层 |
| 接口组织 | 自行定义消息类型和 payload | family、command、attribute |
| 数据格式 | 字符串或自定义二进制 | 结构化 Netlink attributes |
| 参数校验 | 模块自行实现 | 可通过 attribute policy 统一校验 |
| 命令分派 | 自己在接收回调中解析 | 通过 command 对应处理函数 |
| 协议扩展 | 需要自行设计兼容规则 | 增加可选 attribute 或 command 较自然 |
| 维护成本 | 小型 demo 较低，复杂项目较高 | 初始代码较多，但长期维护更清晰 |
| 适合场景 | 学习、实验、已有专用协议 | 新建正式、可扩展的内核用户接口 |

原始 Netlink 并不意味着不能传输二进制数据，也不意味着 Generic Netlink 自动解决所有安全问题。两者都必须检查消息长度、权限、network namespace、并发和对象生命周期；Generic Netlink 主要提供更统一的协议组织和属性解析基础。

## 正式项目的选型建议

### 新建正式接口：优先 Generic Netlink

如果正在为一个新内核模块设计长期使用的用户空间控制接口，通常应优先选择 Generic Netlink。典型设计如下：

```text
family: MY_MODULE
commands:
  GET_STATUS
  SET_CONFIG
  START
  STOP

attributes:
  STATUS
  VALUE
  FLAGS
```

它适合：

- 用户程序查询内核模块状态。
- 用户程序修改配置或下发控制命令。
- 内核向用户空间发送事件通知。
- 需要持续增加命令和字段的接口。
- 需要清晰的参数类型、范围检查和权限控制的接口。

相比当前 demo 直接传递字符串：

```text
"hello netlink"
```

正式接口可以明确表示：

```text
command = SET_VALUE
attribute VALUE = 42
```

这样更容易进行参数校验、日志分析和版本演进。

### 对接已有内核子系统：遵循已有协议

如果用户程序是对接已有内核子系统，应使用该子系统定义的协议，而不是自行选择协议。例如：

```bash
ip link
ip addr
ip route
```

这些工具通过网络子系统规定的 Netlink 接口与内核通信，相关协议包括 `NETLINK_ROUTE`。对接已有接口时，重点是遵循已有的 family、command、attribute 和兼容性约定。

### 学习或隔离实验：原始 Netlink 足够

本目录的 `NETLINK_USERSOCK` 示例适合学习：

- `AF_NETLINK` socket 如何创建和绑定。
- `nl_pid` / port ID 如何定位通信端点。
- `struct sk_buff` 如何承载 Netlink 消息。
- `struct nlmsghdr` 如何描述消息。
- `sendmsg()`、`recv()` 与内核回调如何配合。
- `nlmsg_new()`、`nlmsg_put()` 和 `nlmsg_unicast()` 如何发送消息。

它的重点是展示传输路径，而不是提供一个可长期演进的协议。`NETLINK_USERSOCK` 在这里是隔离学习示例的选择；新设计的复杂产品接口不应只靠它加自定义字符串格式来解决协议建模问题。

### 大块数据：考虑其他接口

Netlink 更适合控制命令、配置和事件通知。如果数据是大块数据或持续高速数据流，应该评估字符设备、`mmap`、`eventfd`、BPF ring buffer 或 perf buffer 等方案。可以让 Netlink 负责控制面，让其他机制负责数据面：

```text
Generic Netlink：配置、启动、停止、状态和事件
        │
        └── mmap / 字符设备 / ring buffer：大块或持续数据
```

## Generic Netlink 的典型设计模型

设计一个正式接口时，可以先定义协议表，而不是直接拼接字符串：

| 项目 | 示例 | 作用 |
|---|---|---|
| Family | `MY_MODULE` | 标识一组相关操作 |
| Command | `MY_CMD_SET_VALUE` | 标识具体操作 |
| Attribute | `MY_ATTR_VALUE` | 携带一个参数或返回值 |
| Type | `NLA_U32` | 规定属性数据类型 |
| Policy | `min=0, max=100` | 校验属性是否合法 |

用户空间发送 `MY_CMD_SET_VALUE` 时，可以携带 `MY_ATTR_VALUE = 42`；内核解析完成后，返回一个包含状态或错误信息的 Generic Netlink 消息。新增字段时，可以把它设计为可选 attribute，使新旧用户程序更容易共存；真正改变语义或不兼容时，则应增加 command 或版本约定。

在内核侧，Generic Netlink 的实现通常包含以下工作：

```text
定义 attribute 类型和 policy
        ↓
定义 command 与处理函数
        ↓
注册 genl family
        ↓
在处理函数中解析属性并执行操作
        ↓
构造属性并发送响应或事件
```

因此，Generic Netlink 的开发成本比当前原始 demo 高一些，但接口边界、错误处理和扩展方式更加明确。

## 选择结论

可以按下面的规则选择：

```text
需要用户空间与内核通信
        │
        ├── 对接已有内核子系统？
        │       └── 使用该子系统规定的 Netlink 协议
        │
        ├── 新建正式且可扩展的控制接口？
        │       └── 优先使用 Generic Netlink
        │
        ├── 只是学习底层收发或做隔离实验？
        │       └── 原始 Netlink 可以满足需求
        │
        └── 传输大块或持续数据？
                └── 评估字符设备、mmap 或 ring buffer
```

一句话总结：原始 Netlink 解决“消息如何在用户空间和内核之间传输”，Generic Netlink 进一步解决“正式协议如何组织、解析、校验和扩展”。新设计的正式内核—用户空间控制接口通常优先使用 Generic Netlink；当前 `NETLINK_USERSOCK` demo 则用于理解底层 Netlink 收发机制。

## 运行

```bash
cd vendor-module/kernel/netlink-demo
./run.sh build
```

或运行所有验证：

```bash
./go.sh demo
```

成功输出包含：

```text
userspace received: kernel reply: hello netlink
netlink_demo: request from port ...: hello netlink
```

`NETLINK_USERSOCK` 仅适合这个隔离的学习示例。实际产品接口通常应使用 Generic Netlink：定义 family、command 和 attribute，获得更清晰的协议、扩展性和权限控制。
