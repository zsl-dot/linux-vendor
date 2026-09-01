include ../../common.mk

BUILD_DIR := $(LEARN_OUT)/netlink-demo
CLIENT := $(BUILD_DIR)/netlink-client

all: $(CLIENT) module

$(BUILD_DIR):
	mkdir -p $@

$(CLIENT): netlink_client.c | $(BUILD_DIR)
	$(CC) -static -Wall -Wextra -o $@ $<

module: | $(BUILD_DIR)
	cp netlink_demo.c Kbuild $(BUILD_DIR)/
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNEL_OUT) M=$(BUILD_DIR) modules

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all module clean
