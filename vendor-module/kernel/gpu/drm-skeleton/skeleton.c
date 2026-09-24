// SPDX-License-Identifier: GPL-2.0+
/*
 * drm-skeleton — 最小 DRM/KMS 驱动骨架（gpu 学习域）
 *
 * 一个无真实硬件的虚拟显示设备，展示 DRM 驱动的完整骨架：
 *
 *   drm_driver              驱动身份（名字/特性/文件操作）；modetest -M skeleton
 *                           按这里的 .name 匹配驱动
 *   drm_device              设备实例，注册后生成 /dev/dri/cardN
 *   drm_simple_display_pipe CRTC + encoder + connector 三合一的简化层，
 *                           免去手写三个对象的完整 vtable
 *   connector.get_modes     硬编码一个 1024x768@60 模式（真实驱动来自 EDID）
 *   GEM(shmem)              帧缓冲内存：DRM_GEM_SHMEM_DRIVER_OPS 提供
 *                           dumb_create，userspace 用 drmModeCreateDumb 分配
 *   pipe.enable/update      模式设置与页面翻转事件；7.3 起 drm_gem_shmem_vmap
 *                           仅限 KUNIT 命名空间，模块不能直接 CPU 访问 GEM，
 *                           故骨架在回调中记录事件而非填充像素
 *
 * 对照阅读：drivers/gpu/drm/tiny/gm12u320.c（同样基于 simple pipe）、
 * drivers/gpu/drm/vkms/（虚拟 KMS 的全功能实现，内核已内置）。
 */
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define DRIVER_NAME "skeleton"

struct skeleton_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector conn;
};

static const uint32_t skeleton_formats[] = {
	DRM_FORMAT_XRGB8888,
};

/*
 * "扫描输出"回调。真实硬件在 enable 里配置 DMA/寄存器让显示引擎从 GEM
 * 缓冲取数；骨架驱动只记录到达的模式。7.3 起 drm_gem_shmem_vmap 只在
 * EXPORTED_FOR_KUNIT_TESTING 命名空间导出，模块侧不应 CPU 访问 GEM。
 */
static void skeleton_pipe_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *crtc_state,
				 struct drm_plane_state *plane_state)
{
	struct drm_display_mode *mode = &crtc_state->mode;

	drm_info(pipe->crtc.dev, "pipe enabled: mode %ux%u@%u, fb %ux%u\n",
		 mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode),
		 plane_state->fb->width, plane_state->fb->height);
}

static void skeleton_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	drm_info(pipe->crtc.dev, "pipe disabled\n");
}

static void skeleton_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;

	/* 页面翻转（damage 提交）到达这里；记录新帧缓冲模拟一次扫描输出 */
	if (state->fb)
		drm_info(pipe->crtc.dev, "page flip: fb %ux%u\n",
			 state->fb->width, state->fb->height);
}

static const struct drm_simple_display_pipe_funcs skeleton_pipe_funcs = {
	.enable  = skeleton_pipe_enable,
	.disable = skeleton_pipe_disable,
	.update  = skeleton_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static int skeleton_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	/* 无 EDID：直接构造一个标准 CVT 模式；真实驱动解析显示器 EDID */
	mode = drm_cvt_mode(connector->dev, 1024, 768, 60,
			    false, false, false);
	if (!mode)
		return 0;

	drm_mode_probed_add(connector, mode);
	return 1;
}

static const struct drm_connector_helper_funcs skeleton_conn_helper = {
	.get_modes = skeleton_get_modes,
};

static const struct drm_connector_funcs skeleton_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs skeleton_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(skeleton_fops);

static const struct drm_driver skeleton_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.name = DRIVER_NAME,
	.desc = "drm-skeleton learning driver",
	.major = 1,
	.minor = 0,

	.fops = &skeleton_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
};

static int skeleton_probe(struct platform_device *pdev)
{
	struct skeleton_device *sdev;
	struct drm_device *drm;
	int ret;

	sdev = devm_drm_dev_alloc(&pdev->dev, &skeleton_drm_driver,
				  struct skeleton_device, drm);
	if (IS_ERR(sdev))
		return PTR_ERR(sdev);
	drm = &sdev->drm;
	platform_set_drvdata(pdev, sdev);

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 16;
	drm->mode_config.max_width = 4096;
	drm->mode_config.min_height = 16;
	drm->mode_config.max_height = 4096;
	drm->mode_config.funcs = &skeleton_mode_config_funcs;

	/* connector 必须由驱动自己初始化（pipe_init 只负责 attach encoder）；
	 * 虚拟显示设备用 DRM_MODE_CONNECTOR_VIRTUAL（与 vkms 一致）
	 */
	ret = drm_connector_init(drm, &sdev->conn, &skeleton_conn_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	/* simple pipe = CRTC + primary plane + encoder + connector 一次到位 */
	ret = drm_simple_display_pipe_init(drm, &sdev->pipe,
					   &skeleton_pipe_funcs,
					   skeleton_formats,
					   ARRAY_SIZE(skeleton_formats),
					   NULL, &sdev->conn);
	if (ret)
		return ret;

	drm_connector_helper_add(&sdev->conn, &skeleton_conn_helper);
	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	drm_client_setup(drm, NULL);

	drm_info(drm, "drm-skeleton initialized\n");
	return 0;
}

static void skeleton_remove(struct platform_device *pdev)
{
	struct skeleton_device *sdev = platform_get_drvdata(pdev);
	struct drm_device *drm = &sdev->drm;

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
}

static struct platform_device *skeleton_pdev;

static struct platform_driver skeleton_platform_driver = {
	.probe = skeleton_probe,
	.remove = skeleton_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

static int __init skeleton_init(void)
{
	int ret;

	ret = platform_driver_register(&skeleton_platform_driver);
	if (ret)
		return ret;

	/* 虚拟设备：自己注册一个 platform device 触发 probe（真实驱动由
	 * 总线枚举硬件触发）
	 */
	skeleton_pdev = platform_device_register_simple(DRIVER_NAME, PLATFORM_DEVID_NONE,
							NULL, 0);
	if (IS_ERR(skeleton_pdev)) {
		platform_driver_unregister(&skeleton_platform_driver);
		return PTR_ERR(skeleton_pdev);
	}
	return 0;
}

static void __exit skeleton_exit(void)
{
	platform_device_unregister(skeleton_pdev);
	platform_driver_unregister(&skeleton_platform_driver);
}

module_init(skeleton_init);
module_exit(skeleton_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Minimal DRM/KMS driver skeleton for GPU driver learning");
