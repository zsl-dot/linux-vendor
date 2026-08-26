# 编译产物输出到外部，不污染源码目录
BUILD_DIR := $(abspath ../../../build/linux-learn/simulations/wake_q_demo)

CFLAGS = -Wall -Wextra -O0 -g

TARGETS = 01_intrusive_list 02_ptr_to_ptr 03_wake_q_simulation \
          04_single_vs_double_ptr 05_lastp_visual \
          06_manual_step_by_step

all: $(addprefix $(BUILD_DIR)/,$(TARGETS))

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

run: all
	@for t in $(TARGETS); do \
		echo ""; \
		echo "==========================="; \
		echo " $$t"; \
		echo "==========================="; \
		$(BUILD_DIR)/$$t; \
	done

run-04: all
	@echo "==========================="
	@echo " 04: 一级指针 vs 二级指针"
	@echo "==========================="
	@$(BUILD_DIR)/04_single_vs_double_ptr

run-05: all
	@echo "==========================="
	@echo " 05: lastp 逐步可视化"
	@echo "==========================="
	@$(BUILD_DIR)/05_lastp_visual

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run clean
