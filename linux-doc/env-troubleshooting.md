# 宿主环境排查笔记

记录在本机（Ubuntu 24.04）折腾内核/图形环境时踩到的系统层问题与排查手法。
这些不是内核知识，但**每次装依赖都会撞上**，所以值得留档。

撰写时间：2026-09-19。

---

## 1. dpkg / apt 状态速查

`dpkg -l` 前两列是状态码，格式为「期望状态 + 当前状态 + 错误标记」：

| 码 | 含义 | 要不要处理 |
|---|---|---|
| `ii` | 已安装且已配置 | 正常 |
| `rc` | 已卸载，**配置文件残留** | 无害，可批量清 |
| `iF` | 安装后**配置失败** | **必须处理**，会卡住一切 apt 操作 |
| `iU` | 已解包但未配置 | 同上 |
| `ic` | 已卸载，仅剩配置（purge 未跑完） | 同 `rc` |

列出非正常状态的包：

```bash
dpkg -l | awk '$1!="ii"'          # 全部（含 rc 残留）
dpkg -l | awk '/^rc/{print $2}'   # 只列 rc
sudo dpkg --audit                 # 官方审计，需 root
```

---

## 2. 排查流程

```
sudo dpkg --audit                        # ① 看哪些包卡住
sudo dpkg --configure -a                 # ② 尝试配置（会再次暴露根因）
                                         #    失败时看 postinst 的具体报错
apt-get -s purge -y <包名...>            # ③ 清包前先干跑，确认不牵连其他包
sudo apt-get purge -y <包名...>          # ④ 真正执行
sudo apt-get check                       # ⑤ 验证
```

**关键习惯**：`purge` 前一定先 `-s`（simulate）干跑一次，看输出里是否出现
「升级/安装/保留」——如果只有「卸载」，说明确实只清残留，安全。

---

## 3. 案例一：镜像不可达导致 postinst 永久失败

**症状**：任何 `apt install` 最后都报

```
在处理时有错误发生：google-android-platform-tools-installer
E: Sub-process /usr/bin/dpkg returned an error code (1)
```

**根因**：这个包（`google-android-platform-tools-installer`）的 postinst 要去
`mirrors.neusoft.edu.cn` 下载 platform-tools，而该镜像在本机不可达 ——
包本身是「下载器」，下载失败 → 配置失败 → **之后每次 apt 操作都会重跑一遍并再次失败**。

**处理**：直接 purge。但**先确认依赖它的东西不受影响**：

```bash
which adb fastboot                    # → /home/<user>/bin/adb（自备副本，不归 dpkg 管）
for b in adb fastboot; do
  dpkg -S $(readlink -f $(which $b))  # 确认不是这个包提供的
done
sudo apt-get purge -y google-android-platform-tools-installer
```

**教训**：报错信息里的包名不一定是「问题根源」，也可能是**上一个坏包拖出来的连带错误**。

---

## 4. 案例二：服务被 Docker 容器占端口，导致包配置失败

**症状**：`dpkg --configure mysql-server-8.0` 报

```
Job for mysql.service failed because the control process exited with error code.
Error: 98 (地址已在使用)
```

**排查过程**（这四步能定位几乎所有「服务起不来」）：

```bash
sudo ss -ltnp | grep 3306          # ① 谁占着端口
ps -o pid,ppid,user,args -p <PID>  # ② 进程是谁、父进程是谁
readlink /proc/<PID>/exe           # ③ 真实可执行文件
readlink /proc/<PID>/ns/pid        # ④ 和 self 对比：namespace 不同 → 它在容器里
```

本例中父进程是 `containerd-shim-runc-v2 -namespace moby`，
PID namespace 与主机不同 —— 说明是 **Docker 容器里的 mysqld**，
这解释了两个诡异现象：

- `ps` 显示该进程的 user 是 `dnsmasq`（容器内 UID 999 在宿主机被解析成了另一个用户）
- 宿主机上 `mysql.service` 永远起不来（端口被容器占着）

**处理**：

```bash
docker ps                                             # 找到容器（本例 mysql:latest 9.7）
docker inspect mysql --format '{{.HostConfig.RestartPolicy.Name}}'   # → always
docker stop mysql
docker update --restart=no mysql                      # ★ 必须改，否则开机会再抢
sudo dpkg --configure -a                              # 这次一次通过
```

**教训**：

1. 宿主机服务和 Docker 容器**共用端口空间**，安装服务前先确认端口没被容器占
2. `restart=always` 的容器会在开机后自动抢端口，排查完一定要改掉策略
3. 容器内 UID 在宿主机可能映射成同名/错名用户，**别用 `ps` 的 user 列下结论**

---

## 5. 案例三：SVG 导出中文丢失（ImageMagick vs librsvg）

**症状**：用 HTML + 内联 SVG 画时序图，导出 PNG 时中文全部丢失，只有 ASCII 显示。

**误判**：以为缺中文字体。实际 `fc-list :lang=zh` 显示 Noto Sans CJK 齐全。

**真因**：**ImageMagick 内置的 SVG 渲染器**不做 fontconfig 字形回退，遇到 CJK 直接丢字形。

**处理**：换 librsvg 渲染

```bash
sudo apt install librsvg2-bin
rsvg-convert -w 1800 diagram.svg -o diagram.png    # 中文正常
```

**结论**：SVG → PNG 一律用 `rsvg-convert`，别用 `convert`。
（浏览器打开 HTML 原文件不受影响，浏览器本身有字体回退。）

---

## 6. 常用命令清单

```bash
# ---- dpkg / apt 体检 ----
sudo dpkg --audit                     # 官方审计
dpkg -l | awk '$1!="ii"'              # 非正常状态包
sudo apt-get check                    # 依赖健康
apt-get -s purge -y <pkg>             # 清包前干跑

# ---- 端口 / 进程归属 ----
sudo ss -ltnp | grep <port>           # 谁占端口
readlink /proc/<PID>/ns/pid           # 是否在容器/命名空间里
docker ps --format 'table {{.Names}}\t{{.Image}}\t{{.Ports}}'
docker update --restart=no <name>     # 关掉自动重启

# ---- 字体 / 图形 ----
fc-list :lang=zh | head               # 有哪些中文字体
rsvg-convert -w 1800 in.svg -o out.png

# ---- 磁盘 ----
df -h /boot                           # 旧内核堆积常在这里
```

---

## 7. 与本仓库的关系

- 内核构建依赖（flex/bison/libssl-dev 等）都走 apt，坏包会让 `./go.sh deps` 失败
- 图形相关文档的 SVG 图（现位于知识库 `knowledge-base/android/composer/*.html`）
  导出 PNG 时走上面第 5 节的方案
- QEMU / virtme-ng 的虚拟机基础设施依赖宿主机端口与 Docker 共存，注意端口冲突
