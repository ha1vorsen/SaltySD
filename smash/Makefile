#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules
export LD	:=	$(CC)
ARMIPS ?= armips

TITLE_IDS ?= 000EDF00
CODE ?= code.bin

ifneq ($(words $(TITLE_IDS)),1)
$(error Build one resident 3GX per verified title ID; TITLE_IDS must contain exactly one value)
endif
BUILD := build
PAYLOADS := $(BUILD)/bin

.PHONY: assemble all clean test-guards test-host test-sdk

all: require-code
	@mkdir -p $(PAYLOADS)
	@python scan.py "$(CODE)"
	@$(AS) "resource_mod/hookresource.asm"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/hookresource.bin"
	@rm a.out
	@$(AS) "resource_mod/incalloc.asm"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/incalloc.bin"
	@rm a.out
	@$(AS) "hookthread.asm"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/hookthread.bin"
	@rm a.out
	@$(AS) "hooknorm.asm"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/hooknorm.bin"
	@rm a.out
	@$(AS) "island_sdbgm.s"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/island_sdbgm.bin"
	@rm a.out
	@$(AS) "island_home.s"
	@$(OBJCOPY) -O binary a.out "$(PAYLOADS)/island_home.bin"
	@rm a.out
	@python patch.py "$(CODE)"
	@$(ARMIPS) -sym2 $(PAYLOADS)/cro_redir.sym cro_redir.asm
	@python tools/verify_cro_abi.py $(BUILD)/code_saltysd.bin $(PAYLOADS)/cro_redir.sym
	@python tools/verify_fighter_data_fallback.py "$(CODE)" $(BUILD)/code_saltysd.bin $(BUILD)/common.armips.asm $(PAYLOADS)/cro_redir.sym
	@python tools/verify_menu_hook.py "$(CODE)" $(BUILD)/code_saltysd.bin $(BUILD)/common.armips.asm
	@python tools/check_island.py "$(CODE)" $(BUILD)/code_saltysd.bin
	@$(MAKE) --no-print-directory -C plugin TITLE_IDS="$(TITLE_IDS)" CODEBIN="$(abspath $(CODE))" PATCHED="$(abspath $(BUILD)/code_saltysd.bin)"

.PHONY: require-code
require-code:
	@test -f "$(CODE)" || (echo "CODE does not name a clean, labeled code.bin: $(CODE)"; exit 2)

test-guards:
	@python tools/test_build_guards.py

test-host:
	@mkdir -p $(BUILD)
	@gcc -std=c11 -Wall -Wextra -Werror -Iplugin/source tests/test_se_claims.c plugin/source/se/claims.c -o $(BUILD)/test_se_claims.exe
	@$(BUILD)/test_se_claims.exe
	@gcc -std=c11 -Wall -Wextra -Werror -Iplugin/source tests/test_se_errors.c plugin/source/se/errors.c -o $(BUILD)/test_se_errors.exe
	@$(BUILD)/test_se_errors.exe
	@gcc -std=c11 -Wall -Wextra -Werror -Iplugin/source tests/test_se_plan.c plugin/source/se/plan.c plugin/source/se/errors.c -o $(BUILD)/test_se_plan.exe
	@$(BUILD)/test_se_plan.exe
	@gcc -std=c11 -Wall -Wextra -Werror -Iplugin/source -Isdk/include tests/test_se_format.c plugin/source/se/format.c plugin/source/se/errors.c -o $(BUILD)/test_se_format.exe
	@$(BUILD)/test_se_format.exe
	@gcc -std=c11 -Wall -Wextra -Werror -Iplugin/source -Isdk/include tests/test_se_hooks.c plugin/source/se/hooks.c plugin/source/se/targets.c plugin/source/se/errors.c -Ibuild/plugin -o $(BUILD)/test_se_hooks.exe
	@$(BUILD)/test_se_hooks.exe

test-sdk:
	@python -m unittest discover -s sdk/tests -t sdk -p 'test_*.py'

clean:
	@rm -rf $(PAYLOADS) $(BUILD)/code_saltysd.bin $(BUILD)/common.asm $(BUILD)/common.armips.asm $(BUILD)/common.h
	@$(MAKE) --no-print-directory -C plugin clean
