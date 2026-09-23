#!/usr/bin/env python3


import sys

from arm_stack import read_armips_equ, read_armips_symbols

BASE = 0x100000
LIBPNG_LO, LIBPNG_HI = 0xA33000, 0xA37000

# Must match patch.py.
ISLAND_OFFS = 0x3C4
ISLAND_SIZE = 0x400
ISLAND_SDBGM = 0x1E0
ISLAND_HOOKS = 0x220


def island_bounds():
    symbols = read_armips_symbols("build/bin/cro_redir.sym")
    if "saltysd_hook_end" not in symbols:
        raise SystemExit("missing armips symbol: saltysd_hook_end")
    try:
        start = read_armips_equ("build/common.armips.asm", "cro_fighter_new") + ISLAND_OFFS
    except ValueError as error:
        raise SystemExit(str(error))
    end = symbols["saltysd_hook_end"]
    return start, end


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: check_island.py <pristine code.bin> <patched code.bin>')
    pristine = open(sys.argv[1], "rb").read()
    patched = open(sys.argv[2], "rb").read()
    sdbgm = open("build/bin/island_sdbgm.bin", "rb").read()

    island, hook_end = island_bounds()
    at = island + ISLAND_SDBGM - BASE

    if patched[at:at + len(sdbgm)] != sdbgm:
        raise SystemExit(
            f"the BGM payload is not intact at 0x{island + ISLAND_SDBGM:06X}. "
            "The island helpers in cro_redir.asm have grown past "
            f"0x{ISLAND_SDBGM:X} and overwritten it."
        )

    if ISLAND_SDBGM + len(sdbgm) > ISLAND_HOOKS:
        raise SystemExit("the BGM payload runs into the CRO hook stubs.")

    lo = island + ISLAND_SDBGM + len(sdbgm) - BASE
    hi = island + ISLAND_HOOKS - BASE
    if pristine[lo:hi] != patched[lo:hi]:
        raise SystemExit("something was written between the BGM payload and the hook stubs.")

    end = hook_end - island
    if end < ISLAND_HOOKS:
        raise SystemExit("the CRO hook stubs end before their reserved area.")
    if end > ISLAND_SIZE:
        raise SystemExit("the island is full.")

    lo = island + end - BASE
    hi = island + ISLAND_SIZE - BASE
    if pristine[lo:hi] != patched[lo:hi]:
        raise SystemExit("something was written past the end of the island.")

    lo, hi = LIBPNG_LO - BASE, LIBPNG_HI - BASE
    if pristine[lo:hi] != patched[lo:hi]:
        raise SystemExit(
            "code.bin was written inside the live libpng range. The payloads "
            "belong in the plugin."
        )

    print(f"island 0x{island:06X}: helpers + BGM payload + hook stubs ok, "
          f"{ISLAND_SIZE - end} bytes spare; libpng untouched")


if __name__ == "__main__":
    main()
