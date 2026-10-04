DEVKITARM ?= C:/devkitPro/devkitARM
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
CC := $(PREFIX)gcc
LD := $(PREFIX)gcc
OBJDUMP := $(PREFIX)objdump
PYTHON ?= python

CODE ?= ../../../code.bin
SEA_TOOL ?= ../../saltysd_sea.py
REGISTRY ?= ../../targets/smash-1.1.7.toml
BUILD := build

ARCH := -marm -march=armv6k -mfpu=vfpv3-d16 -mfloat-abi=hard
CFLAGS := $(ARCH) -std=c11 -Os -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -fno-pic -fno-pie -fno-stack-protector -I../../include
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -Wl,-T,$(BUILD)/no_hitboxes.ld

.PHONY: all clean verify require-inputs

all: $(BUILD)/no_hitboxes.sea verify

$(BUILD):
	@mkdir -p $@

require-inputs:
	@test -f "$(CODE)" || (echo "CODE does not name a file: $(CODE)"; exit 2)

$(BUILD)/no_hitboxes.ld: layout.toml $(REGISTRY) ../../se_sdk/targets.py require-inputs | $(BUILD)
	$(PYTHON) ../../se_sdk/targets.py --registry $(REGISTRY) --layout layout.toml \
		--code $(CODE) --build-id 0x01170001 --out $@

$(BUILD)/no_hitboxes.o: no_hitboxes.c ../../include/se/host.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/no_hitboxes.elf: $(BUILD)/no_hitboxes.o $(BUILD)/no_hitboxes.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/no_hitboxes.o

$(BUILD)/no_hitboxes.sea: $(BUILD)/no_hitboxes.elf sea.toml $(SEA_TOOL) require-inputs
	$(PYTHON) $(SEA_TOOL) build sea.toml -o $@

verify: $(BUILD)/no_hitboxes.sea
	$(PYTHON) $(SEA_TOOL) check --matrix sea.toml $<
	$(PYTHON) verify.py $< $(BUILD)/no_hitboxes.elf $(OBJDUMP)

clean:
	@rm -rf $(BUILD)
