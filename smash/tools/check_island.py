#!/usr/bin/env python3


import sys
import struct

from arm_stack import read_armips_equ, read_armips_symbols

BASE = 0x100000
LIBPNG_LO, LIBPNG_HI = 0xA33000, 0xA37000

# Must match patch.py.
ISLAND_OFFS = 0x3C4
ISLAND_SIZE = 0x400
ISLAND_SDBGM = 0x1E0
ISLAND_HOOKS = 0x220
ISLAND_HOME = 0x360
ISLAND_HOME_GATE = 0x3FC


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
    home = open("build/bin/island_home.bin", "rb").read()

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

    if ISLAND_HOME + len(home) > ISLAND_HOME_GATE:
        raise SystemExit("the HOME relay overlaps its resident gate word.")
    normal = bytes([0x05, 0x20, 0xA0, 0xE1, 0x07, 0x10, 0xA0, 0xE1,
                    0x06, 0x00, 0xA0, 0xE1, 0x03, 0x00, 0x00, 0x9A])
    normal_site = pristine.find(normal)
    if normal_site < 0:
        raise SystemExit("normal loader signature missing while checking HOME relay")
    def fill(marker, value):
        nonlocal home
        needle = struct.pack("<I", marker)
        if home.count(needle) != 1:
            raise SystemExit("HOME relay marker is malformed")
        home = home.replace(needle, struct.pack("<I", value))
    fill(0x11111111, island + ISLAND_HOME_GATE)
    fill(0x22222222, BASE + normal_site + 0xC)
    fill(0x33333333, BASE + normal_site + 0x1C)
    at = island + ISLAND_HOME - BASE
    if patched[at:at + len(home)] != home:
        raise SystemExit("the HOME relay is not intact in the island.")
    gate_at = island + ISLAND_HOME_GATE - BASE
    if patched[gate_at:gate_at + 4] != b"\0\0\0\0":
        raise SystemExit("the HOME relay gate is not initialized to zero.")

    end = max(end, ISLAND_HOME_GATE + 4)
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

    print(f"island 0x{island:06X}: helpers + BGM payload + hook stubs + HOME relay ok, "
          f"{ISLAND_SIZE - end} bytes spare; libpng untouched")


if __name__ == "__main__":
    main()
