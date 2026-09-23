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
           -fdata-sections -fno-builtin -ffreestanding -fno-strict-aliasing -I.. -Isource \
           -Isource/monocypher
ASFLAGS := $(ARCH) -x assembler-with-cpp -Wa,-I,..
CFLAGS  += -DSALTYSD_TITLE_ID=0x$(firstword $(TITLE_IDS))
ifeq ($(UPDATE_TEST),1)
CFLAGS  += -DSALTYSD_UPDATE_TEST
TARGET  := saltysd-TEST
endif
ifneq ($(VERSION),)
ifeq ($(shell echo "$(VERSION)" | grep -E "^[0-9]+\.[0-9]+\.[0-9]+$$"),)
$(error VERSION must look like X.Y.Z)
endif
endif
COMMIT  := $(shell git describe --always --dirty --abbrev=7)
# git describe names a commit, not a build: every build off one dirty tree
# shares that name, and the updater reads a shared name as "already running it".
# DIRTY_ID names the build, and the publish script computes one for all regions.
ifneq ($(DIRTY_ID),)
ifeq ($(shell echo "$(DIRTY_ID)" | grep -E "^[0-9a-z]{7}(-[0-9a-f]{8})?-[0-9]{6}-[0-9]{4}$$"),)
$(error DIRTY_ID must look like <commit7>[-<src8>]-YYMMDD-HHMM)
endif
COMMIT := $(DIRTY_ID)
endif
ifneq ($(BUILT_AT),)
ifeq ($(shell echo "$(BUILT_AT)" | grep -E "^[0-9]{14}$$"),)
$(error BUILT_AT must be 14 digits, YYYYMMDDHHMMSS)
endif
endif
# A bare make is a local build: it claims no build time, so it takes anything.
BUILT_AT ?= 00000000000000

ifneq ($(VERSION),)
IDENTITY := $(VERSION)
else
IDENTITY := $(COMMIT)
endif
CFLAGS  += -I$(BUILD)
LDFLAGS := $(ARCH) -T 3gx.ld -nostdlib -nostartfiles \
           -Wl,--gc-sections,--strip-discarded,-Map,$(BUILD)/$(TARGET).map

CFILES  := $(wildcard $(SOURCES)/*.c)
VENDOR  := $(wildcard $(SOURCES)/monocypher/*.c)
SFILES  := $(wildcard $(SOURCES)/*.s)
OFILES  := $(patsubst $(SOURCES)/%.s,$(BUILD)/%.o,$(SFILES)) \
           $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES)) \
           $(patsubst $(SOURCES)/monocypher/%.c,$(BUILD)/%.o,$(VENDOR))

.PHONY: all clean patches verify FORCE

all: $(TARGET).3gx

$(BUILD):
	@mkdir -p $(BUILD)

patches: $(SOURCES)/patches.h

$(SOURCES)/patches.h: $(CODEBIN) $(PATCHED) ../tools/gen_patches.py
	@test -f "$(CODEBIN)" -a -f "$(PATCHED)" || { \
	  echo ""; \
	  echo "  error: need both the pristine and the patched binary:"; \
	  echo "      $(CODEBIN)"; \
	  echo "      $(PATCHED)"; \
	  echo ""; \
	  echo "  Build from the directory above, which produces them."; \
	  echo ""; \
	  exit 1; }
	@python ../tools/gen_patches.py "$(CODEBIN)" "$(PATCHED)" $@

$(OFILES): | patches $(BUILD)

$(BUILD)/main.o: $(SOURCES)/patches.h

$(BUILD)/buildinfo.h: FORCE | $(BUILD)
ifneq ($(VERSION),)
	@echo '#define SALTYSD_VERSION "$(VERSION)"' > $@
else
	@test -n "$(COMMIT)" || { echo "no git commit to name this dirty build"; exit 1; }
	@echo '#define SALTYSD_COMMIT "$(COMMIT)"' > $@
endif
	@echo '#define SALTYSD_BUILT_AT "$(BUILT_AT)"' >> $@

$(BUILD)/host_menu.o $(BUILD)/update.o: $(BUILD)/buildinfo.h

$(BUILD)/%.o: $(SOURCES)/%.s
	@echo "as  $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/monocypher/%.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# A moved or resized entry table is caught here rather than on hardware.
define check_entry_table
	@start=`$(NM) $(TARGET).elf | grep -i " T _start$$" | cut -d' ' -f1`; \
	 after=`$(NM) $(TARGET).elf | grep -i " t plugin_start$$" | cut -d' ' -f1`; \
	 status=`$(NM) $(TARGET).elf | grep -i " [DdBb] saltysd_status$$" | cut -d' ' -f1`; \
	 test "$$start" = "07000100" || { echo "_start is at 0x$$start, expected 0x07000100"; exit 1; }; \
	 test "$$after" = "07000120" || { echo "the entry table is not eight words (plugin_start at 0x$$after)"; exit 1; }; \
	 echo "entry table 0x07000100-0x0700011F ok, status word at 0x$$status"
endef

$(TARGET).elf: $(OFILES) 3gx.ld
	@echo "ld  $@"
	@$(LD) $(LDFLAGS) $(OFILES) -o $@
	$(check_entry_table)
	@python ../tools/verify_plugin_abi.py $(TARGET).elf
	@python ../tools/verify_plugin.py $(TARGET).elf "$(CODEBIN)" "$(PATCHED)"

verify:
	$(check_entry_table)
	@python ../tools/verify_plugin_abi.py $(TARGET).elf
	@python ../tools/verify_plugin.py $(TARGET).elf "$(CODEBIN)" "$(PATCHED)"

$(TARGET).3gx: $(TARGET).elf plugin.json
	@python ../tools/mk3gx.py $(TARGET).elf $(BUILD)/plugin.json $@

$(BUILD)/plugin.json: plugin.json | $(BUILD)
	@python -c "import json; i=json.load(open('plugin.json')); i['targets']=['0x'+t for t in '$(TITLE_IDS)'.split()]; i['title']='SaltySD $(IDENTITY)'; json.dump(i,open('$(BUILD)/plugin.json','w'))"

$(TARGET).3gx: $(BUILD)/plugin.json

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).3gx $(SOURCES)/patches.h
