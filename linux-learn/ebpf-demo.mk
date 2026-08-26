# eBPF demo 共享模板
# 用法: include ../../common.mk
#       BPF_SRC    := kprobe_exec.bpf.c   # BPF 程序源码
#       LOADER_SRC := loader.c            # 用户态加载器源码
#       LOADER_BIN := ebpf-loader         # 最终加载器可执行文件名
#       include ../../ebpf-demo.mk
#
# 需要先编译 bpflib: make -C ../bpflib
# 编译产物输出到 $(LEARN_OUT)/<当前目录名>/

BUILD_DIR  := $(LEARN_OUT)/$(notdir $(CURDIR))
BPFLIB_DIR := $(LEARN_OUT)/bpflib

CLANG := clang
GCC   := gcc

BPF_CFLAGS := -target bpf -O2 -g \
              -I$(KERNEL_SRC)/arch/x86/include/uapi \
              -I$(KERNEL_OUT)/arch/x86/include/generated/uapi

BPF_O := $(BUILD_DIR)/$(BPF_SRC:.c=.o)

all: $(BPF_O) $(BUILD_DIR)/$(LOADER_BIN)

$(BPF_O): $(BPF_SRC) | $(BUILD_DIR)
	$(CLANG) $(BPF_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/$(LOADER_BIN): $(LOADER_SRC) $(BPFLIB_DIR)/libbpfloader.a | $(BUILD_DIR)
	$(GCC) -static -Wall -Wextra -Os -I../bpflib -o $@ $(LOADER_SRC) \
		$(BPFLIB_DIR)/libbpfloader.a

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
