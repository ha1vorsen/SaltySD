#!/usr/bin/env python3

import struct
import sys

from arm_stack import read_armips_equ, read_armips_symbols

BASE = 0x100000
FIGHTER_COUNT = 0x41
SENTINEL = struct.pack("<I", 0x01234567)


def read_word(image, address, base=BASE):
    return struct.unpack_from("<I", image, address - base)[0]


def branch_target(address, word):
    displacement = word & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return address + 8 + displacement * 4


def verify_dispatch(pristine, patched, start, cases, default, fallback, lookup, base=BASE):
    if len(pristine) != len(patched):
        raise ValueError("pristine and patched code.bin sizes differ")

    if read_word(pristine, start, base) != 0xE3510041:
        raise ValueError("stock fighter-data dispatcher lost its 0x41 range check")
    if read_word(pristine, start + 4, base) != 0x379FF101:
        raise ValueError("stock fighter-data dispatcher lost its conditional case-table load")
    stock_default = read_word(pristine, start + 8, base)
    if (stock_default & 0xFF000000 != 0xEA000000 or
            branch_target(start + 8, stock_default) != default):
        raise ValueError("stock fighter-data default branch changed")
    for fighter_id in range(FIGHTER_COUNT):
        expected = cases + fighter_id * 8
        if read_word(pristine, start + 0xC + fighter_id * 4, base) != expected:
            raise ValueError("stock fighter-data case table changed")

    if read_word(patched, start, base) != 0xE3510041:
        raise ValueError("patched fighter-data entry does not check ID 0x41")
    early = read_word(patched, start + 4, base)
    if early & 0xFF000000 != 0x2A000000 or branch_target(start + 4, early) != default:
        raise ValueError("out-of-domain fighter IDs do not tail into the stock default")

    if read_word(patched, start + 8, base) != 0xE92D43FE:
        raise ValueError("fighter-data entry no longer saves r1-r9 and lr")
    reserve = read_word(patched, start + 0xC, base)
    if reserve & 0xFFFFFF00 != 0xE24DD000:
        raise ValueError("fighter-data entry no longer reserves its locals")
    frame = reserve & 0xFF
    for index in range(3):
        spill = read_word(patched, start + 0x10 + index * 4, base)
        if spill & 0xFFFF0000 != 0xE58D0000 or (spill >> 12) & 0xF != index:
            raise ValueError(f"fighter-data entry no longer spills r{index}")
        if spill & 0xFFF >= frame:
            raise ValueError(f"fighter-data r{index} spill overwrites the saved registers")

    if read_word(patched, lookup, base) != 0xE3530000:
        raise ValueError("fighter-data export-result check changed")
    miss = read_word(patched, lookup + 4, base)
    if miss & 0xFF000000 != 0x0A000000 or branch_target(lookup + 4, miss) != fallback:
        raise ValueError("a missing fighter-data export does not reach the stock fallback")

    expected_fallback = (0xE28DD020, 0xE8BD43FE, None, 0xE08CC181, 0xE12FFF1C)
    for index, expected in enumerate(expected_fallback):
        word = read_word(patched, fallback + index * 4, base)
        if expected is not None and word != expected:
            raise ValueError(f"stock fallback instruction {index} changed")
    literal_load = read_word(patched, fallback + 8, base)
    if literal_load & 0xFFFFF000 != 0xE59FC000:
        raise ValueError("stock fallback no longer loads its case-stub base")
    literal_address = fallback + 16 + (literal_load & 0xFFF)
    if read_word(patched, literal_address, base) != cases:
        raise ValueError("stock fallback case-stub base is wrong")

    lo, hi = cases - base, default - base
    if patched[lo:hi] != pristine[lo:hi]:
        raise ValueError("SaltySD modified the retail fighter-data case stubs")
    if SENTINEL in patched[start - base:cases - base]:
        raise ValueError("the deliberate 0x01234567 fighter-data fault remains")


def main():
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: verify_fighter_data_fallback.py <pristine> <patched> "
            "<common.armips.asm> <cro_redir.sym>"
        )
    pristine = open(sys.argv[1], "rb").read()
    patched = open(sys.argv[2], "rb").read()
    symbols = read_armips_symbols(sys.argv[4])
    required = ("fighter_data_stock_case", "fighter_data_lookup_result")
    missing = [name for name in required if name not in symbols]
    if missing:
        raise SystemExit(f"missing armips symbol(s): {', '.join(missing)}")
    try:
        verify_dispatch(
            pristine,
            patched,
            read_armips_equ(sys.argv[3], "get_fighter_data"),
            read_armips_equ(sys.argv[3], "get_fighter_data_stock_cases"),
            read_armips_equ(sys.argv[3], "get_fighter_data_stock_default"),
            symbols["fighter_data_stock_case"],
            symbols["fighter_data_lookup_result"],
        )
    except ValueError as error:
        raise SystemExit(str(error))
    print(
        "fighter-data fallback ok: IDs >= 0x41 use retail default; "
        "missing exports use intact retail case stubs; sentinel removed"
    )


if __name__ == "__main__":
    main()
