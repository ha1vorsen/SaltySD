#---------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error Please set DEVKITARM in your environment)
endif

PREFIX  := arm-none-eabi-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc
LD      := $(PREFIX)gcc
NM      := $(PREFIX)nm

TARGET  := saltysd
BUILD   := build
SOURCES := source

TITLE_IDS ?= 000EDF00

CODEBIN  ?= $(CURDIR)/../code.bin
PATCHED  ?= $(CURDIR)/../code_saltysd.bin

ARCH    := -marm -march=armv6k -mtune=mpcore -mfloat-abi=hard
# -O2 is what resource_mod has always been built with.
CFLAGS  := $(ARCH) -std=gnu11 -O2 -Wall -fomit-frame-pointer -ffunction-sections \
           -fdata-sections -fno-builtin -ffreestanding -fno-strict-aliasing -I.. -Isource
ASFLAGS := $(ARCH) -x assembler-with-cpp -Wa,-I,..
LDFLAGS := $(ARCH) -T 3gx.ld -nostdlib -nostartfiles \
           -Wl,--gc-sections,--strip-discarded,-Map,$(BUILD)/$(TARGET).map

CFILES  := $(wildcard $(SOURCES)/*.c)
SFILES  := $(wildcard $(SOURCES)/*.s)
OFILES  := $(patsubst $(SOURCES)/%.s,$(BUILD)/%.o,$(SFILES)) \
           $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES))

.PHONY: all clean patches verify

all: $(TARGET).3gx

$(BUILD):
	@mkdir -p $(BUILD)

patches:
	@test -f "$(CODEBIN)" -a -f "$(PATCHED)" || { \
	  echo ""; \
	  echo "  error: need both the pristine and the patched binary:"; \
	  echo "      $(CODEBIN)"; \
	  echo "      $(PATCHED)"; \
	  echo ""; \
	  echo "  Build from the directory above, which produces them."; \
	  echo ""; \
	  exit 1; }
	@python ../tools/gen_patches.py "$(CODEBIN)" "$(PATCHED)" $(SOURCES)/patches.h

$(OFILES): | patches $(BUILD)

$(BUILD)/%.o: $(SOURCES)/%.s
	@echo "as  $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# A moved or resized entry table is caught here rather than on hardware.
define check_entry_table
	@start=`$(NM) $(TARGET).elf | grep -i " T _start$$" | cut -d' ' -f1`; \
	 after=`$(NM) $(TARGET).elf | grep -i " t plugin_start$$" | cut -d' ' -f1`; \
	 status=`$(NM) $(TARGET).elf | grep -i " [DdBb] saltysd_status$$" | cut -d' ' -f1`; \
	 test "$$start" = "07000100" || { echo "_start is at 0x$$start, expected 0x07000100"; exit 1; }; \
	 test "$$after" = "07000118" || { echo "the entry table is not six words (plugin_start at 0x$$after)"; exit 1; }; \
	 echo "entry table 0x07000100-0x07000117 ok, status word at 0x$$status"
endef

$(TARGET).elf: $(OFILES) 3gx.ld
	@echo "ld  $@"
	@$(LD) $(LDFLAGS) $(OFILES) -o $@
	$(check_entry_table)
	@python ../tools/verify_plugin.py $(TARGET).elf "$(CODEBIN)" "$(PATCHED)"

verify:
	$(check_entry_table)

$(TARGET).3gx: $(TARGET).elf plugin.json
	@python ../tools/mk3gx.py $(TARGET).elf $(BUILD)/plugin.json $@

$(BUILD)/plugin.json: plugin.json | $(BUILD)
	@python -c "import json; i=json.load(open('plugin.json')); i['targets']=['0x'+t for t in '$(TITLE_IDS)'.split()]; json.dump(i,open('$(BUILD)/plugin.json','w'))"

$(TARGET).3gx: $(BUILD)/plugin.json

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3gx $(SOURCES)/patches.h
