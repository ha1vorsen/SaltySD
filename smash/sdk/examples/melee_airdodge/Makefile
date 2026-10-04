DEVKITARM ?= C:/devkitPro/devkitARM
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
AS := $(PREFIX)gcc
CC := $(PREFIX)gcc
LD := $(PREFIX)gcc
OBJDUMP := $(PREFIX)objdump
PYTHON ?= python

CODE ?=
SEA_TOOL ?= ../../saltysd_sea.py
BUILD := build

ARCH := -marm -march=armv6k -mfpu=vfpv3-d16 -mfloat-abi=hard
ASFLAGS := $(ARCH) -x assembler-with-cpp -I.
CFLAGS := $(ARCH) -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
          -fno-pic -fno-pie -fno-stack-protector
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -Wl,-T,melee_airdodge.ld

.PHONY: all clean arm11 parity require-code

all: $(BUILD)/melee_airdodge.sea arm11 parity

$(BUILD):
	@mkdir -p $@

$(BUILD)/hooks.o: hooks.S | $(BUILD)
	$(AS) $(ASFLAGS) -c -o $@ $<


$(BUILD)/core_bridges.o: core_bridges.S melee_airdodge.h | $(BUILD)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILD)/blend.o: blend.S | $(BUILD)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILD)/melee_airdodge.o: melee_airdodge.c melee_airdodge.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/melee_airdodge.elf: $(BUILD)/hooks.o $(BUILD)/core_bridges.o $(BUILD)/blend.o $(BUILD)/melee_airdodge.o melee_airdodge.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/hooks.o $(BUILD)/core_bridges.o $(BUILD)/blend.o $(BUILD)/melee_airdodge.o

require-code:
	@test -n "$(CODE)" || (echo "Set CODE to the clean USA 1.1.7 code.bin path."; exit 2)
	@test -f "$(CODE)" || (echo "CODE does not name a file: $(CODE)"; exit 2)

$(BUILD)/melee_airdodge.sea: require-code $(BUILD)/melee_airdodge.elf $(CODE) $(SEA_TOOL)
	$(PYTHON) $(SEA_TOOL) convert $(BUILD)/melee_airdodge.elf --code $(CODE) -o $@

arm11: $(BUILD)/melee_airdodge.elf
	@! $(OBJDUMP) -d $< | grep -E '\\<(movw|movt)\\>'

parity: $(BUILD)/melee_airdodge.sea
	$(PYTHON) verify_layout.py $(BUILD)/melee_airdodge.sea $(CODE)

clean:
	@rm -rf $(BUILD)
