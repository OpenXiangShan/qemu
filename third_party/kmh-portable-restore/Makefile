CROSS_COMPILE ?= riscv64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
OBJCOPY := $(CROSS_COMPILE)objcopy
BUILD_DIR := build
ELF := $(BUILD_DIR)/kmh-portable-restore.elf
BIN := $(BUILD_DIR)/kmh-portable-restore.bin

.PHONY: all clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

$(ELF): kmh-portable-restore.S kmh-portable-restore.ld | $(BUILD_DIR)
	$(CC) -nostdlib -nostartfiles -static -mcmodel=medany \
		-march=rv64gcv -mabi=lp64d -Wl,--build-id=none \
		-Wl,-T,kmh-portable-restore.ld kmh-portable-restore.S -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)

NATIVE_FLAGS = -nostdlib -nostartfiles -static -fno-pic -fno-pie \
	-ffreestanding -fno-builtin -fno-stack-protector -msmall-data-limit=0 \
	-mcmodel=medany -march=rv64imac_zicsr_zifencei -mabi=lp64 -Os \
	-Wall -Wextra -Werror -Wl,--build-id=none

$(BUILD_DIR)/native-entry.bin: native-entry.S native-entry.ld | $(BUILD_DIR)
	$(CC) $(NATIVE_FLAGS) -Wl,-T,native-entry.ld native-entry.S -o $(BUILD_DIR)/native-entry.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/native-entry.elf $@

$(BUILD_DIR)/native-core.o: native.c native-abi.h | $(BUILD_DIR)
	$(CC) $(NATIVE_FLAGS) -c native.c -o $@

$(BUILD_DIR)/native-start.o: native-start.S | $(BUILD_DIR)
	$(CC) $(NATIVE_FLAGS) -c native-start.S -o $@

$(BUILD_DIR)/native-return.o: native-return.S | $(BUILD_DIR)
	$(CC) $(NATIVE_FLAGS) -march=rv64gcv -c native-return.S -o $@

$(BUILD_DIR)/native.bin: $(BUILD_DIR)/native-core.o $(BUILD_DIR)/native-start.o $(BUILD_DIR)/native-return.o native.ld
	$(CC) $(NATIVE_FLAGS) -march=rv64gcv -Wl,-T,native.ld \
		-Wl,--no-warn-rwx-segments $(filter %.o,$^) -o $(BUILD_DIR)/native.elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/native.elf $@

.PHONY: native-resources
native-resources: $(BUILD_DIR)/native-entry.bin $(BUILD_DIR)/native.bin
	python3 embed-native.py
