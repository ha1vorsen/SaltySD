#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules
export LD	:=	$(CC)
ARMIPS ?= $(DEVKITARM)/bin/armips

TITLE_IDS ?= 000EDF00

.PHONY: assemble all clean test-guards

all:
	@mkdir -p bin
	@python scan.py code.bin
	@$(AS) "resource_mod/hookresource.asm"
	@$(OBJCOPY) -O binary a.out "bin/hookresource.bin"
	@rm a.out
	@$(AS) "resource_mod/incalloc.asm"
	@$(OBJCOPY) -O binary a.out "bin/incalloc.bin"
	@rm a.out
	@$(AS) "hookthread.asm"
	@$(OBJCOPY) -O binary a.out "bin/hookthread.bin"
	@rm a.out
	@$(AS) "hooknorm.asm"
	@$(OBJCOPY) -O binary a.out "bin/hooknorm.bin"
	@rm a.out
	@$(AS) "island_sdbgm.s"
	@$(OBJCOPY) -O binary a.out "bin/island_sdbgm.bin"
	@rm a.out
	@python patch.py code.bin
	@$(ARMIPS) -sym2 bin/cro_redir.sym cro_redir.asm
	@python tools/verify_cro_abi.py code_saltysd.bin bin/cro_redir.sym
	@python tools/verify_fighter_data_fallback.py code.bin code_saltysd.bin common.armips.asm bin/cro_redir.sym
	@python tools/verify_menu_hook.py code.bin code_saltysd.bin common.armips.asm
	@python tools/check_island.py code.bin code_saltysd.bin
	@$(MAKE) --no-print-directory -C plugin TITLE_IDS="$(TITLE_IDS)"

test-guards:
	@python tools/test_build_guards.py

clean:
	@rm -R bin
	@$(MAKE) --no-print-directory -C plugin clean
