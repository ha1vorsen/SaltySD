#!/usr/bin/env python3


import re
import sys
import struct

from arm_stack import bytes_reader, read_armips_equ, read_armips_symbols, verify_routine

BASE = 0x100000
LIBPNG_LO, LIBPNG_HI = 0xA33000, 0xA37000

# Must match patch.py.
ISLAND_OFFS = 0x3C4
ISLAND_SIZE = 0x800
ISLAND_SDBGM = 0x1E0
ISLAND_HOOKS = 0x220
ISLAND_HOME = 0x360
ISLAND_HOME_THREAD = 0x400
ISLAND_HOME_GATE = 0x4FC
ISLAND_GATED = 0x500
GATED_SE_TABLE = 0x2C
SE_HOST_HEADER_SIZE = 0x14
SE_HOST_FUNCTIONS = 11
PLUGIN_LO, PLUGIN_HI = 0x07000000, 0x08000000
RF_SIG = bytes([0x02, 0x10, 0xD0, 0xE5, 0x04, 0x00, 0x51, 0xE3, 0x05, 0x00, 0x00, 0x3A,
                0x0C, 0x10, 0x90, 0xE5, 0x04, 0x00, 0x90, 0xE5, 0x00, 0x10, 0x41, 0xE0])


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


def branch_target(site, word):
    displacement = word & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return site + 8 + displacement * 4


def stub_array_end(pristine, function):
    at = function - BASE
    for offset in range(0, 0x40, 4):
        word = struct.unpack_from("<I", pristine, at + offset)[0]
        if word != 0x379FF100:
            continue
        compare = struct.unpack_from("<I", pristine, at + offset - 4)[0]
        if compare & 0xFFFFFF00 != 0xE3500000:
            break
        count = compare & 0xFF
        table = at + offset + 8
        last = max(struct.unpack_from("<I", pristine, table + 4 * i)[0] for i in range(count))
        for stub in range(last, last + 0x40, 4):
            if struct.unpack_from("<I", pristine, stub - BASE)[0] >> 24 == 0xEA:
                return stub + 4
        break
    raise SystemExit("cro_fighter_new no longer has the stub jump table the island lives in.")


def common_define(name):
    source = open("build/common.h", encoding="latin-1").read()
    match = re.search(rf"^#define {re.escape(name)} (0x[0-9a-fA-F]+)$", source, re.MULTILINE)
    if not match:
        raise SystemExit(f"build/common.h has no {name}")
    return int(match.group(1), 16)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: check_island.py <pristine code.bin> <patched code.bin>')
    pristine = open(sys.argv[1], "rb").read()
    patched = open(sys.argv[2], "rb").read()
    sdbgm = open("build/bin/island_sdbgm.bin", "rb").read()
    home = open("build/bin/island_home.bin", "rb").read()
    home_thread = open("build/bin/island_home_thread.bin", "rb").read()
    gated = open("build/bin/island_gate.bin", "rb").read()

    island, hook_end = island_bounds()
    gated_at = island + ISLAND_GATED
    sdbgm = sdbgm.replace(struct.pack("<I", 0x77777777), struct.pack("<I", gated_at + 0x1C))
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
    if end > ISLAND_HOME:
        raise SystemExit("the CRO hook stubs run into the HOME relay.")

    if ISLAND_HOME + len(home) > ISLAND_HOME_THREAD:
        raise SystemExit("the HOME relay overlaps the HOME thread relay.")
    if ISLAND_HOME_THREAD + len(home_thread) > ISLAND_HOME_GATE:
        raise SystemExit("the HOME thread relay overlaps its resident gate word.")
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
    thread = bytes([0x08, 0xD0, 0x4D, 0xE2, 0x00, 0x60, 0xA0, 0xE1,
                    0x04, 0x00, 0x92, 0xE5])
    thread_site = pristine.find(thread)
    if thread_site < 0:
        raise SystemExit("thread loader signature missing while checking HOME relay")
    home_thread = home_thread.replace(
        struct.pack("<I", 0x11111111),
        struct.pack("<I", island + ISLAND_HOME_GATE))
    home_thread = home_thread.replace(
        struct.pack("<I", 0x44444444),
        struct.pack("<I", BASE + thread_site + 0xC))
    at = island + ISLAND_HOME_THREAD - BASE
    if patched[at:at + len(home_thread)] != home_thread:
        raise SystemExit("the HOME thread relay is not intact in the island.")
    branch = struct.unpack_from("<I", patched, thread_site)[0]
    target = BASE + thread_site + 8 + ((branch & 0xFFFFFF) << 2)
    if branch & 0x00800000:
        target -= 0x04000000
    if branch >> 24 != 0xEA or target != island + ISLAND_HOME_THREAD:
        raise SystemExit("the thread loader does not branch through its HOME relay.")
    gate_at = island + ISLAND_HOME_GATE - BASE
    if patched[gate_at:gate_at + 4] != b"\0\0\0\0":
        raise SystemExit("the HOME relay gate is not initialized to zero.")

    if ISLAND_GATED + len(gated) > ISLAND_SIZE:
        raise SystemExit("the gated relays do not fit in the island.")
    if island + ISLAND_SIZE > stub_array_end(pristine, island - ISLAND_OFFS):
        raise SystemExit("the island runs past the dead stub array.")
    rf_site = pristine.find(RF_SIG)
    if rf_site < 0:
        raise SystemExit("resource hook signature missing while checking the gated relays")
    menu_site = read_armips_equ("build/common.armips.asm", "menu_hook_site")
    expected = bytearray(gated)
    for marker, value in ((0x11111111, island + ISLAND_HOME_GATE),
                          (0x55555555, BASE + rf_site + 0xC),
                          (0x56565656, BASE + rf_site + 0x24),
                          (0x66666666, menu_site + 0x4)):
        needle = struct.pack("<I", marker)
        if expected.count(needle) != 1:
            raise SystemExit(f"the gated relays have an unexpected 0x{marker:08X} marker count")
        expected = bytearray(bytes(expected).replace(needle, struct.pack("<I", value)))
    for i in range(SE_HOST_FUNCTIONS):
        at = GATED_SE_TABLE + SE_HOST_HEADER_SIZE + i * 4
        offset = struct.unpack_from("<I", expected, at)[0]
        veneer = struct.unpack_from("<I", expected, offset)[0]
        if veneer != 0xE3A0C000 | (i * 4):
            raise SystemExit(f"SE host table entry {i} does not reach its veneer")
        struct.pack_into("<I", expected, at, gated_at + offset)
    at = gated_at - BASE
    if patched[at:at + len(expected)] != bytes(expected):
        raise SystemExit("the gated relays are not intact in the island.")

    read_word = bytes_reader(patched, BASE)
    entries = [branch_target(gated_at + slot, read_word(gated_at + slot))
               for slot in (0x04, 0x0C, 0x10, 0x14, 0x1C, 0x20, 0x24)]
    entries += [struct.unpack_from("<I", expected, GATED_SE_TABLE + SE_HOST_HEADER_SIZE + i * 4)[0]
                for i in range(SE_HOST_FUNCTIONS)]
    for entry in entries:
        try:
            verify_routine(read_word, entry, gated_at + len(gated),
                           f"gated relay 0x{entry:08X}", balanced=True)
        except ValueError as error:
            raise SystemExit(str(error))

    branch = struct.unpack_from("<I", patched, rf_site)[0]
    if branch >> 24 != 0xEA or branch_target(BASE + rf_site, branch) != gated_at + 0xC:
        raise SystemExit("the resource hook does not branch through its gated relay.")
    if patched[rf_site + 4:rf_site + 0xC] != pristine[rf_site + 4:rf_site + 0xC]:
        raise SystemExit("the resource hook changed more than its first word.")

    for name, value in (("saltysd_home_gate_ADDR", island + ISLAND_HOME_GATE),
                        ("saltysd_menu_exit_ADDR", gated_at + 0x14),
                        ("saltysd_se_get_host_ADDR", gated_at + 0x18),
                        ("saltysd_se_host_real_ADDR", gated_at + 0x28),
                        ("saltysd_se_host_ADDR", gated_at + 0x2C)):
        if common_define(name) != value:
            raise SystemExit(f"build/common.h {name} is not 0x{value:08X}; scan.py and patch.py disagree.")

    gated_words = range(gated_at - BASE, gated_at - BASE + len(gated))
    relay_words = (range(island + ISLAND_HOME - BASE, island + ISLAND_HOME_GATE - BASE),
                   gated_words)
    for offset in range(0, min(len(pristine), len(patched)) - 3, 4):
        if patched[offset:offset + 4] == pristine[offset:offset + 4]:
            continue
        word = struct.unpack_from("<I", patched, offset)[0]
        if PLUGIN_LO <= word < PLUGIN_HI and not any(offset in r for r in relay_words):
            raise SystemExit(
                f"0x{BASE + offset:08X} holds plugin address 0x{word:08X} outside the gated relays."
            )

    end = max(end, ISLAND_GATED + len(gated))
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
