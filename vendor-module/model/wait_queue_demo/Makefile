# 编译产物输出到外部，不污染源码目录
BUILD_DIR := $(abspath ../../../build/vendor-module/model/wait_queue_demo)

CFLAGS = -Wall -Wextra -O0 -g -pthread
LDLIBS = -pthread

TARGETS = 01_basic_wait_queue 02_wake_q_internal 03_combined_demo

all: $(addprefix $(BUILD_DIR)/,$(TARGETS))

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

run: all
	@for t in $(TARGETS); do \
		echo ""; \
		echo "============================================================"; \
		echo "  $$t"; \
		echo "============================================================"; \
		$(BUILD_DIR)/$$t; \
	done

run-01: all
	$(BUILD_DIR)/01_basic_wait_queue

run-02: all
	$(BUILD_DIR)/02_wake_q_internal

run-03: all
	$(BUILD_DIR)/03_combined_demo

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run run-01 run-02 run-03 clean
