#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules
export LD	:=	$(CC)

TITLE_IDS ?= 000EDF00

.PHONY: assemble all clean

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
	@armips cro_redir.asm
	@python tools/check_island.py code.bin code_saltysd.bin
	@$(MAKE) --no-print-directory -C plugin TITLE_IDS="$(TITLE_IDS)"

clean:
	@rm -R bin
	@$(MAKE) --no-print-directory -C plugin clean
