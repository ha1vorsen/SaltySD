DEVKITARM ?= C:/devkitPro/devkitARM
PREFIX := $(DEVKITARM)/bin/arm-none-eabi-
AS := $(PREFIX)gcc
CC := $(PREFIX)gcc
LD := $(PREFIX)gcc
OBJDUMP := $(PREFIX)objdump
HOSTCC ?= gcc
PYTHON ?= python

CODE ?= ../../../code.bin
SEA_TOOL ?= ../../saltysd_sea.py
BUILD := build

ARCH := -marm -march=armv6k -mfpu=vfpv3-d16 -mfloat-abi=hard
ASFLAGS := $(ARCH) -x assembler-with-cpp
CFLAGS := $(ARCH) -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
	-fno-builtin -fno-pic -fno-pie -fno-stack-protector
LDFLAGS := $(ARCH) -nostdlib -nostartfiles -Wl,-T,skip_c08_h08_textures.ld

.PHONY: all clean verify require-inputs host-test

all: $(BUILD)/skip_c08_h08_textures.sea verify host-test

$(BUILD):
	@mkdir -p $@

require-inputs:
	@test -f "$(CODE)" || (echo "CODE does not name a file: $(CODE)"; exit 2)

$(BUILD)/hook.o: hook.S | $(BUILD)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILD)/texture_filter.o: texture_filter.c texture_filter.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/skip_c08_h08_textures.elf: $(BUILD)/hook.o $(BUILD)/texture_filter.o skip_c08_h08_textures.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/hook.o $(BUILD)/texture_filter.o

$(BUILD)/skip_c08_h08_textures.sea: require-inputs $(BUILD)/skip_c08_h08_textures.elf sea.toml $(SEA_TOOL)
	$(PYTHON) $(SEA_TOOL) build sea.toml -o $@

verify: $(BUILD)/skip_c08_h08_textures.sea
	$(PYTHON) $(SEA_TOOL) check --matrix sea.toml $<
	$(PYTHON) verify.py $< $(BUILD)/skip_c08_h08_textures.elf $(CODE) $(OBJDUMP)

$(BUILD)/test_filter.exe: test_filter.c texture_filter.c texture_filter.h | $(BUILD)
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ test_filter.c texture_filter.c

host-test: $(BUILD)/test_filter.exe
	$(BUILD)/test_filter.exe

clean:
	@rm -rf $(BUILD)
