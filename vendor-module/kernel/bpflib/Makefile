include ../../common.mk
BUILD_DIR := $(LEARN_OUT)/bpflib
GCC := gcc
AR  := ar

CFLAGS := -Wall -Wextra -Os

LIB_A := $(BUILD_DIR)/libbpfloader.a
TOOL  := $(BUILD_DIR)/elf-extract

OBJS := $(BUILD_DIR)/bpf_loader.o $(BUILD_DIR)/elf_extract_lib.o

all: $(LIB_A) $(TOOL)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/bpf_loader.o: bpf_loader.c bpf_loader.h | $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/elf_extract_lib.o: elf_extract.c elf_extract.h | $(BUILD_DIR)
	$(GCC) $(CFLAGS) -c -DNO_MAIN -o $@ $<

$(TOOL): elf_extract.c elf_extract.h | $(BUILD_DIR)
	$(GCC) $(CFLAGS) -o $@ $<

$(LIB_A): $(OBJS)
	$(AR) rcs $@ $^

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
