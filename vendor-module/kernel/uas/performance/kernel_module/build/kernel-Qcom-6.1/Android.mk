LOCAL_PATH := $(call my-dir)

DLKM_Q_DIR := device/qcom/common/dlkm
EXPECTED_DLKM_Q_PATH := $(TOP)/device/qcom/common/dlkm/Build_external_kernelmodule.mk
ACTUAL_DLKM_PATH := $(abspath $(DLKM_Q_DIR)/Build_external_kernelmodule.mk)
SCHED_BLD_DIR := $(TOP)/$(LOCAL_PATH)

LOCAL_MODULE_DDK_BUILD := true

$(info DLKM_Q_DIR = $(DLKM_Q_DIR))
$(info ACTUAL_DLKM_PATH = $(ACTUAL_DLKM_PATH))
$(info EXPECTED_DLKM_Q_PATH = $(EXPECTED_DLKM_Q_PATH))
$(info TOP = $(TOP))

ifneq ($(wildcard $(DLKM_Q_DIR)/Build_external_kernelmodule.mk),)
include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE      := trans_sched-module-symvers
LOCAL_MODULE_STEM         := Module.symvers
LOCAL_MODULE_KBUILD_NAME  := Module.symvers
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_Q_DIR)/Build_external_kernelmodule.mk

include $(CLEAR_VARS)
LOCAL_SRC_FILES   := $(wildcard $(LOCAL_PATH)/**/*) $(wildcard $(LOCAL_PATH)/*)
LOCAL_MODULE      := trans_sched.ko
#LOCAL_EXPORT_KO_INCLUDE_DIRS    := $(LOCAL_PATH)/include/linux
LOCAL_MODULE_KBUILD_NAME := trans_sched.ko
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
KBUILD_OPTIONS += SCHED_ROOT=$(SCHED_BLD_DIR)
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
include $(DLKM_Q_DIR)/Build_external_kernelmodule.mk

else
    $(warning "Q platform dependencies not found: $(DLKM_Q_DIR). Skipping Q platform compilation.")
endif
