include ../../common.mk
BUILD_DIR := $(LEARN_OUT)/binder-demo

CFLAGS := -static -Wall -Wextra -I$(KERNEL_SRC)/include/uapi -I$(KERNEL_SRC)/include

all: $(BUILD_DIR)/binder-server $(BUILD_DIR)/binder-client

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/binder-server: binder-server.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/binder-client: binder-client.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
