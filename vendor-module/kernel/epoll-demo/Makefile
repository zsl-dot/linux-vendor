include ../../common.mk

BUILD_DIR := $(LEARN_OUT)/epoll-demo

all: $(BUILD_DIR)/epoll-client module

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/epoll-client: epoll_client.c | $(BUILD_DIR)
	$(CC) -static -Wall -Wextra -o $@ $<

module: | $(BUILD_DIR)
	cp epoll_demo.c Kbuild $(BUILD_DIR)/
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNEL_OUT) M=$(BUILD_DIR) modules

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all module clean
