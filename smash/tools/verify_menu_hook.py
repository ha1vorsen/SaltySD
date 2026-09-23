#!/usr/bin/env python3

import re
import struct
import sys

BASE = 0x100000
ISLAND_OFFS = 0x3C4
ISLAND_HOOKS = 0x220
MENU_TRAMP_OFFS = 0x0C
DISPLACED = 0xE5902040
TRAMPOLINE = struct.pack("<II", 0xE51FF004, 0x0700011C)
MENU_FUNCTION = bytes((
    0x14, 0x10, 0xA0, 0xE3, 0x00, 0x00, 0xA0, 0xE1,
    0x40, 0x20, 0x90, 0xE5, 0x02, 0x11, 0x80, 0xE7,
    0x01, 0x10, 0xA0, 0xE3, 0x44, 0x10, 0xC0, 0xE5,
    0x1E, 0xFF, 0x2F, 0xE1,
))


def symbol(path, name):
    source = open(path, encoding="latin-1").read()
    match = re.search(rf"^{re.escape(name)} equ \((0x[0-9a-fA-F]+)\)$", source, re.MULTILINE)
    if not match:
        raise SystemExit(f"{path}: no {name} symbol")
    return int(match.group(1), 16)


def arm_branch_target(site, word):
    if word & 0xFF000000 != 0xEA000000:
        raise SystemExit(f"menu hook at 0x{site:08X} is not an unconditional ARM B")
    displacement = word & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return site + 8 + displacement * 4


def verify_hook(pristine, patched, site, target, base=BASE):
    if len(pristine) != len(patched):
        raise ValueError("pristine and patched code.bin sizes differ")
    offset = site - base
    function_start = offset - 8
    function_end = function_start + len(MENU_FUNCTION)
    if pristine[function_start:function_end] != MENU_FUNCTION:
        raise ValueError("pristine menu export does not match the audited 1.1.7 function")

    original, = struct.unpack_from("<I", pristine, offset)
    if original != DISPLACED:
        raise ValueError(
            f"menu hook displaced 0x{original:08X}, expected ldr r2,[r0,#0x40]"
        )
    patched_word, = struct.unpack_from("<I", patched, offset)
    try:
        actual_target = arm_branch_target(site, patched_word)
    except SystemExit as error:
        raise ValueError(str(error))
    if actual_target != target:
        raise ValueError(
            f"menu hook targets 0x{actual_target:08X}, expected island trampoline 0x{target:08X}"
        )

    local_diffs = [
        index for index in range(function_start, function_end)
        if pristine[index] != patched[index]
    ]
    if not local_diffs or any(not offset <= index < offset + 4 for index in local_diffs):
        raise ValueError("menu hook changed bytes outside its single displaced instruction")

    target_offset = target - base
    if patched[target_offset:target_offset + len(TRAMPOLINE)] != TRAMPOLINE:
        raise ValueError(f"menu trampoline is not intact at 0x{target:08X}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: verify_menu_hook.py <pristine code.bin> <patched code.bin> <common.armips.asm>"
        )
    pristine = open(sys.argv[1], "rb").read()
    patched = open(sys.argv[2], "rb").read()
    site = symbol(sys.argv[3], "menu_hook_site")
    island = symbol(sys.argv[3], "cro_fighter_new") + ISLAND_OFFS
    target = island + ISLAND_HOOKS + MENU_TRAMP_OFFS
    try:
        verify_hook(pristine, patched, site, target)
    except ValueError as error:
        raise SystemExit(str(error))

    print(
        f"menu hook ok: one word at 0x{site:08X} -> 0x{target:08X}; "
        "displaced ldr and plugin entry are fixed"
    )


if __name__ == "__main__":
    main()
