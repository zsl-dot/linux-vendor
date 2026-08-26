# 内核模块 demo 共享模板
# 用法: include ../../common.mk
#       MODULE_SRC := hello.c        # 模块源码文件名
#       include ../../kernel-module.mk
#
# 编译产物输出到 $(LEARN_OUT)/<当前目录名>/

BUILD_DIR := $(LEARN_OUT)/$(notdir $(CURDIR))

all:
	mkdir -p $(BUILD_DIR)
	cp $(MODULE_SRC) Kbuild $(BUILD_DIR)/
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNEL_OUT) M=$(BUILD_DIR) modules

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
