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
BUILD   := ../build/plugin
SOURCES := source
SE_SOURCES := source/se
TARGET_ELF = $(BUILD)/$(TARGET).elf
TARGET_3GX = $(BUILD)/$(TARGET).3gx

TITLE_IDS ?= 000EDF00

ifneq ($(words $(TITLE_IDS)),1)
$(error Build one resident 3GX per verified title ID; TITLE_IDS must contain exactly one value)
endif

CODEBIN  ?= $(CURDIR)/../code.bin
PATCHED  ?= $(CURDIR)/../build/code_saltysd.bin
LEGACY   ?= $(firstword $(TITLE_IDS)) "$(CODEBIN)"

ARCH    := -marm -march=armv6k -mtune=mpcore -mfloat-abi=hard
# -O2 is what resource_mod has always been built with.
CFLAGS  := $(ARCH) -std=gnu11 -O2 -Wall -fomit-frame-pointer -ffunction-sections \
           -fdata-sections -fno-builtin -ffreestanding -fno-strict-aliasing -I$(BUILD) -I../build -I.. -Isource \
           -Isource/monocypher -I../sdk/include
ASFLAGS := $(ARCH) -x assembler-with-cpp -I../build -Wa,-I,.. -Wa,-I,../build
CFLAGS  += -DSALTYSD_TITLE_ID=0x$(firstword $(TITLE_IDS))
ifeq ($(UPDATE_TEST),1)
CFLAGS  += -DSALTYSD_UPDATE_TEST
TARGET  := saltysd-TEST
endif
VERSION := 2.1.0
ifneq ($(BUILT_AT),)
ifeq ($(shell echo "$(BUILT_AT)" | grep -E "^[0-9]{14}$$"),)
$(error BUILT_AT must be 14 digits, YYYYMMDDHHMMSS)
endif
endif
BUILT_AT ?= 00000000000000

ifneq ($(strip $(DIRTY_ID)),)
IDENTITY := $(DIRTY_ID)
IDENTITY_DEFINE := SALTYSD_COMMIT
else
IDENTITY := $(VERSION)
IDENTITY_DEFINE := SALTYSD_VERSION
endif
LDFLAGS := $(ARCH) -T 3gx.ld -nostdlib -nostartfiles \
           -Wl,--gc-sections,--strip-discarded,-Map,$(BUILD)/$(TARGET).map

CFILES  := $(filter-out $(SOURCES)/home_swap.c,$(wildcard $(SOURCES)/*.c))
SE_CFILES := $(wildcard $(SE_SOURCES)/*.c)
VENDOR  := $(wildcard $(SOURCES)/monocypher/*.c)
# distinct output stem; no C/assembly object collision
SFILES  := $(filter-out $(SOURCES)/home_swap.s,$(wildcard $(SOURCES)/*.s))
OFILES  := $(patsubst $(SOURCES)/%.s,$(BUILD)/%.o,$(SFILES)) \
           $(patsubst $(SOURCES)/%.c,$(BUILD)/%.o,$(CFILES)) \
           $(patsubst $(SE_SOURCES)/%.c,$(BUILD)/se/%.o,$(SE_CFILES)) \
           $(BUILD)/home_events.o \
           $(patsubst $(SOURCES)/monocypher/%.c,$(BUILD)/%.o,$(VENDOR))

.PHONY: all clean patches verify FORCE

all: $(TARGET_3GX)

$(BUILD):
	@mkdir -p $(BUILD)

patches: $(BUILD)/patches.h

$(BUILD)/input-identity.txt: FORCE ../tools/write_input_identity.py | $(BUILD)
	@python ../tools/write_input_identity.py $@ "$(CODEBIN)" "$(PATCHED)" $(wildcard ../legacy/*.ips)

$(BUILD)/build-config.txt: FORCE ../tools/write_text_if_changed.py | $(BUILD)
	@python ../tools/write_text_if_changed.py $@ "TITLE_IDS=$(TITLE_IDS)" "IDENTITY=$(IDENTITY)" "IDENTITY_DEFINE=$(IDENTITY_DEFINE)" "BUILT_AT=$(BUILT_AT)"

$(BUILD)/se_targets.generated.h: $(BUILD)/input-identity.txt $(BUILD)/build-config.txt ../sdk/targets/smash-1.1.7.toml ../tools/gen_se_registry.py | $(BUILD)
	@python ../tools/gen_se_registry.py --registry ../sdk/targets/smash-1.1.7.toml --title-id 0x$(firstword $(TITLE_IDS)) --code "$(CODEBIN)" --out $@

$(BUILD)/se/targets.o: $(BUILD)/se_targets.generated.h

$(BUILD)/patches.h: $(BUILD)/input-identity.txt $(CODEBIN) $(PATCHED) ../tools/gen_patches.py | $(BUILD)
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

$(BUILD)/legacy.h: $(BUILD)/input-identity.txt $(BUILD)/build-config.txt $(CODEBIN) ../tools/gen_legacy.py $(wildcard ../legacy/*.ips) | $(BUILD)
	@python ../tools/gen_legacy.py ../legacy $@ $(LEGACY)

$(BUILD)/main.o: $(BUILD)/patches.h $(BUILD)/legacy.h

$(BUILD)/buildinfo.h: FORCE | $(BUILD)
	@echo '#define $(IDENTITY_DEFINE) "$(IDENTITY)"' > $@
	@echo '#define SALTYSD_BUILT_AT "$(BUILT_AT)"' >> $@

$(BUILD)/resource_mod.o $(BUILD)/tetra_menu.o $(BUILD)/update.o: $(BUILD)/buildinfo.h
$(BUILD)/resource_mod.o $(BUILD)/tetra_menu.o $(BUILD)/boot_log.o: $(SOURCES)/boot_log.h
$(BUILD)/resource_mod.o $(BUILD)/tetra_menu.o $(BUILD)/boot_log.o $(BUILD)/plgldr.o $(BUILD)/update.o: $(SOURCES)/plgldr.h

$(BUILD)/%.o: $(SOURCES)/%.s
	@echo "as  $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

# code input switch invalidates address literals
$(BUILD)/normload.o $(BUILD)/threadload.o: ../build/common.asm

$(BUILD)/%.o: $(SOURCES)/%.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/se/%.o: $(SE_SOURCES)/%.c
	@mkdir -p $(@D)
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/home_events.o: $(SOURCES)/home_swap.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/monocypher/%.c
	@echo "cc  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# A moved or resized entry table gets caught here rather than on a build
define check_entry_table
	@start=`$(NM) $(TARGET_ELF) | grep -i " T _start$$" | cut -d' ' -f1`; \
	 after=`$(NM) $(TARGET_ELF) | grep -i " t plugin_start$$" | cut -d' ' -f1`; \
	 status=`$(NM) $(TARGET_ELF) | grep -i " [DdBb] saltysd_status$$" | cut -d' ' -f1`; \
	 test "$$start" = "07000100" || { echo "_start is at 0x$$start, expected 0x07000100"; exit 1; }; \
	 test "$$after" = "07000124" || { echo "the entry table is not nine words (plugin_start at 0x$$after)"; exit 1; }; \
	 echo "entry table 0x07000100-0x07000123 ok, public se_get_host slot at 0x07000120, status word at 0x$$status"
endef

$(TARGET_ELF): $(OFILES) 3gx.ld
	@echo "ld  $@"
	@$(LD) $(LDFLAGS) $(OFILES) -o $@
	$(check_entry_table)
	@python ../tools/verify_plugin_abi.py $(TARGET_ELF)
	@python ../tools/verify_plugin.py $(TARGET_ELF) "$(CODEBIN)" "$(PATCHED)"

verify:
	$(check_entry_table)
	@python ../tools/verify_plugin_abi.py $(TARGET_ELF)
	@python ../tools/verify_plugin.py $(TARGET_ELF) "$(CODEBIN)" "$(PATCHED)"

$(TARGET_3GX): $(TARGET_ELF) plugin.json
	@python ../tools/mk3gx.py $(TARGET_ELF) $(BUILD)/plugin.json $@

$(BUILD)/plugin.json: plugin.json $(BUILD)/build-config.txt | $(BUILD)
	@python -c "import json; i=json.load(open('plugin.json')); i['targets']=['0x'+t for t in '$(TITLE_IDS)'.split()]; i['title']='SaltySD $(IDENTITY)'; json.dump(i,open('$(BUILD)/plugin.json','w'))"

$(TARGET_3GX): $(BUILD)/plugin.json

clean:
	@rm -rf $(BUILD)
