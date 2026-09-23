# TODO
# Add ddk module definition for frpc-trusted driver

load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "ddk_headers",
    "ddk_module",
    "kernel_module",
    "kernel_modules_install",
)

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    ddk_module(
        name = "{}_trans_sched".format(kernel_build_variant),
        srcs = [
            "src/sched_main.c",
            "src/trans_pcb.c",
            "src/sched_common.c",
            "src/trans_fair.c",
            "src/trans_proc.c",
            "src/trans_sched_info.c",
            "src/trans_binder.c",
            "src/trans_mutex.c",
            "src/trans_rwsem.c",
            "src/trans_target.c",
            "src/trans_balance.c",
            "src/trans_locking.c",
            "src/trans_loading.c",
            "src/trans_trace.c",
            "src/trans_futex.c",
            "src/trans_workqueue.c",
            "src/trans_xmu.c",
        ],

        out = "trans_sched.ko",
        kconfig = "Kconfig",
        kernel_build = "//msm-kernel:{}".format(kernel_build_variant),
        deps = [
            ":uas_headers",
            "//msm-kernel:all_headers",
            "//msm-kernel:sched_headers",
            "//msm-kernel:trans_sched_headers",
            "//msm-kernel:low_latency_sched_header",
        ]
    )

    copy_to_dist_dir(
        name = "{}_modules_dist".format(kernel_build_variant),
        data = [":{}_trans_sched".format(kernel_build_variant)],
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )
