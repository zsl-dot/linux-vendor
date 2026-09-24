# 写一个最小 DRM/KMS 驱动：drm-skeleton

`kernel/gpu/drm-skeleton/skeleton.c`（约 230 行）实现一个无硬件的虚拟显示设备，覆盖 DRM 驱动的完整骨架。本文解释每一层的职责与验证方式。

## 骨架层次（自下而上读 skeleton.c）

| 层 | 代码 | 职责 |
|---|---|---|
| 驱动身份 | `drm_driver skeleton_drm_driver` | 名字（`modetest -M skeleton` 按它匹配）、特性位（MODESET/GEM/ATOMIC）、fops；`DRM_GEM_SHMEM_DRIVER_OPS` 一行获得 dumb_create 等 GEM 实现 |
| 设备生命周期 | `devm_drm_dev_alloc` + `drm_dev_register` | 分配 `drm_device`（内嵌在自定义结构体），注册后生成 `/dev/dri/cardN`；devm 托管自动释放 |
| 虚拟硬件 | `platform_device_register_simple` | 无真实硬件时自己造一个 platform device 触发 probe；真实驱动由总线枚举触发 |
| KMS 简化层 | `drm_simple_display_pipe` | 把 CRTC + primary plane + encoder + connector 的三个 vtable 合并成 4 个回调（enable/disable/update + shadow-plane 宏） |
| 连接器 | `connector_funcs` + `get_modes` | 必须自己 `drm_connector_init`（pipe_init 只 attach encoder！）；无 EDID 时用 `drm_cvt_mode` 硬编码模式 |
| 帧缓冲 | GEM shmem | userspace `drmModeCreateDumb` 分配，`drm_gem_fb_create_with_dirty` 创建 framebuffer |

## 两个只有动手才踩得到的坑（7.3 内核）

1. **`drm_simple_display_pipe_init` 不初始化 connector**——它只做 `drm_connector_attach_encoder`。漏掉 `drm_connector_init` 的症状：encoder/CRTC/plane 都在 modetest 里可见，但 Connectors 列表为空、`/sys/class/drm/cardN/` 下没有 connector 条目。
2. **`drm_gem_shmem_vmap` 自 7.x 起只在 `EXPORTED_FOR_KUNIT_TESTING` 命名空间导出**——模块 CPU 访问 GEM 缓冲的路被上游有意收窄（modpost 报 namespace 错误）。骨架驱动因此只在回调里记录事件，不做像素填充。

## 验证链路（run.sh + drm-skeleton-check.sh）

```text
insmod skeleton.ko
  → /dev/dri/cardN 出现（与内置 vkms 并存，各占一个 minor）
  → modetest -M skeleton 枚举出 connector 和 1024x768@60 模式
  → modetest -M skeleton -s <conn>:<mode>  →  完整 atomic modeset
  → dmesg 出现 "pipe enabled: mode 1024x768@60"（我们的 enable 回调被调用）
```

四项各输出一行 `RESULT: PASS`，guest 自动关机。

## 下一步（向真实驱动生长）

- 对照 `drivers/gpu/drm/tiny/gm12u320.c`（同样基于 simple pipe 的 USB 显示设备）；
- 阅读 `drivers/gpu/drm/vkms/`（内核内置的虚拟 KMS 全功能版：writeback、多 plane、CRC）；
- 给骨架加 `damage` 处理（`drm_atomic_helper_damage_iter_on_old_master`）；
- 学习 `drm_sched`（GPU 调度器，本内核已带 KUnit 测试：`CONFIG_DRM_SCHED_KUNIT_TEST`，启动日志可见）。
