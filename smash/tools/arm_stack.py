#!/usr/bin/env python3

import re
import struct


def read_armips_symbols(path):
    symbols = {}
    with open(path, encoding="utf-8") as source:
        for line in source:
            fields = line.split(maxsplit=1)
            if len(fields) != 2:
                continue
            try:
                symbols[fields[1].strip()] = int(fields[0], 16)
            except ValueError:
                pass
    return symbols


def read_armips_equ(path, name):
    source = open(path, encoding="latin-1").read()
    pattern = rf"^{re.escape(name)} equ \((0x[0-9a-fA-F]+)\)$"
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError(f"{path}: no {name} symbol")
    return int(match.group(1), 16)


def _ror32(value, shift):
    shift &= 31
    if not shift:
        return value
    return ((value >> shift) | (value << (32 - shift))) & 0xFFFFFFFF


def arm_immediate(word):
    return _ror32(word & 0xFF, ((word >> 8) & 0xF) * 2)


def branch_target(pc, word):
    displacement = word & 0x00FFFFFF
    if displacement & 0x00800000:
        displacement -= 0x01000000
    return pc + 8 + displacement * 4


def stack_change(word):
    # PUSH and POP
    if word & 0x0FFF0000 == 0x092D0000:
        return 4 * (word & 0xFFFF).bit_count()
    if word & 0x0FFF0000 == 0x08BD0000:
        return -4 * (word & 0xFFFF).bit_count()

    # ADD/SUB sp, sp, #imm
    opcode = word & 0x0FEFF000
    if opcode == 0x024DD000:
        return arm_immediate(word)
    if opcode == 0x028DD000:
        return -arm_immediate(word)
    return 0


def verify_routine(read_word, start, end, name, entry_mod=0, balanced=False):
    if start & 3 or end & 3 or start >= end:
        raise ValueError(f"{name}: invalid routine bounds 0x{start:X}-0x{end:X}")

    pending = [(start, 0)]
    seen = {}
    calls = 0
    exits = 0

    while pending:
        pc, delta = pending.pop()
        if not start <= pc < end:
            if balanced and delta:
                raise ValueError(f"{name}: branch exits with stack delta {delta} at 0x{pc:08X}")
            exits += 1
            continue

        old = seen.get(pc)
        if old is not None:
            if old != delta:
                raise ValueError(
                    f"{name}: 0x{pc:08X} is reachable with stack deltas {old} and {delta}"
                )
            continue
        seen[pc] = delta

        word = read_word(pc)
        delta += stack_change(word)
        cond = word >> 28

        is_bl = word & 0x0F000000 == 0x0B000000
        is_blx_imm = cond == 0xF and word & 0x0E000000 == 0x0A000000
        is_blx_reg = word & 0x0FFFFFF0 == 0x012FFF30
        if is_bl or is_blx_imm or is_blx_reg:
            calls += 1
            if (entry_mod + delta) & 7:
                raise ValueError(
                    f"{name}: call at 0x{pc:08X} has sp alignment "
                    f"{(entry_mod + delta) & 7} (local stack delta {delta})"
                )
            pending.append((pc + 4, delta))
            continue

        if word & 0x0F000000 == 0x0A000000:
            pending.append((branch_target(pc, word), delta))
            if cond != 0xE:
                pending.append((pc + 4, delta))
            continue

        pop_pc = word & 0x0FFF8000 == 0x08BD8000
        bx = word & 0x0FFFFFF0 == 0x012FFF10
        writes_pc = ((word >> 12) & 0xF) == 0xF
        if pop_pc or bx or writes_pc:
            if balanced and delta:
                raise ValueError(f"{name}: exit at 0x{pc:08X} has stack delta {delta}")
            exits += 1
            continue

        pending.append((pc + 4, delta))

    if not calls:
        raise ValueError(f"{name}: verifier reached no calls")
    if not exits:
        raise ValueError(f"{name}: verifier reached no exits")
    return calls, exits


def bytes_reader(data, base):
    def read_word(address):
        offset = address - base
        if offset < 0 or offset + 4 > len(data):
            raise ValueError(f"address 0x{address:08X} is outside the image")
        return struct.unpack_from("<I", data, offset)[0]
    return read_word
