DEVKITARM ?= C:/devkitPro/devkitARM
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
AS := $(PREFIX)gcc
LD := $(PREFIX)gcc
PYTHON ?= python

CODE ?= ../../../code.bin
SEA_TOOL ?= ../../saltysd_sea.py
BUILD := build

ARCH := -marm -march=armv6k -mfpu=vfpv3-d16 -mfloat-abi=hard
ASFLAGS := $(ARCH) -x assembler-with-cpp
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -Wl,-T,hittest.ld

.PHONY: all clean verify require-inputs

all: $(BUILD)/hittest.sea verify

$(BUILD):
	@mkdir -p $@

$(BUILD)/hittest.o: hittest.S | $(BUILD)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILD)/hittest.elf: $(BUILD)/hittest.o hittest.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/hittest.o

require-inputs:
	@test -f "$(CODE)" || (echo "CODE does not name a file: $(CODE)"; exit 2)
	@test -f "$(SEA_TOOL)" || (echo "SEA_TOOL does not name a file: $(SEA_TOOL)"; exit 2)

$(BUILD)/hittest.sea: require-inputs $(BUILD)/hittest.elf $(SEA_TOOL) sea.toml
	$(PYTHON) $(SEA_TOOL) build sea.toml -o $@

verify: $(BUILD)/hittest.sea
	$(PYTHON) $(SEA_TOOL) check --matrix sea.toml $(BUILD)/hittest.sea
	$(PYTHON) verify_layout.py $(BUILD)/hittest.sea $(CODE)

clean:
	@rm -rf $(BUILD)
