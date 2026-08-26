# 共享 Makefile 配置 — 所有 demo 的 Makefile 都 include 此文件
# 用法: include ../../common.mk   (从 linux-vendor-module/<demo>/ 中)

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)

KERNEL_SRC := $(ROOT_DIR)/linux-source
KERNEL_OUT := $(ROOT_DIR)/build

# 所有 demo 编译产物统一放到 build/linux-learn/ 下，不污染源码目录
LEARN_OUT := $(ROOT_DIR)/build/linux-learn
