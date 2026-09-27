#!/usr/bin/env python3

import struct

BASE = 0x100000

EHDR_SIZE = 52
PHDR_SIZE = 32
ET_EXEC = 2
EM_ARM = 40
EF_ARM_EABI5_HARD = 0x05000400
PT_LOAD = 1
PT_NOTE = 4
PF_R = 4
PF_X = 1
NOTE_OWNER = b"SaltySD\0"
NOTE_ORIGINAL = 1


def align4(n):
    return (n + 3) & ~3


def check_segments(segments, code_bin):
    end = BASE + len(code_bin)
    last = BASE
    for vaddr, data in segments:
        if not data:
            raise ValueError(f"empty segment at 0x{vaddr:08X}")
        if vaddr < last:
            raise ValueError(f"segment at 0x{vaddr:08X} overlaps the one before it")
        if vaddr + len(data) > end:
            raise ValueError(f"segment at 0x{vaddr:08X} runs past the end of code.bin")
        last = vaddr + len(data)


def build_plugin(segments, code_bin):
    segments = sorted((vaddr, bytes(data)) for vaddr, data in segments)
    check_segments(segments, code_bin)

    original = b"".join(code_bin[vaddr - BASE:vaddr - BASE + len(data)] for vaddr, data in segments)
    note = struct.pack("<III", len(NOTE_OWNER), len(original), NOTE_ORIGINAL) + NOTE_OWNER + original
    note += b"\0" * (align4(len(note)) - len(note))

    phnum = len(segments) + 1
    at = EHDR_SIZE + phnum * PHDR_SIZE
    note_off = at
    at += len(note)

    phdrs = [struct.pack("<IIIIIIII", PT_NOTE, note_off, 0, 0, len(note), len(note), PF_R, 4)]
    body = bytearray(note)
    for vaddr, data in segments:
        phdrs.append(struct.pack("<IIIIIIII", PT_LOAD, at, vaddr, vaddr, len(data), len(data),
                                 PF_R | PF_X, 4))
        padded = data + b"\0" * (align4(len(data)) - len(data))
        body += padded
        at += len(padded)

    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    ehdr = ident + struct.pack("<HHIIIIIHHHHHH", ET_EXEC, EM_ARM, 1, 0, EHDR_SIZE, 0,
                               EF_ARM_EABI5_HARD, EHDR_SIZE, PHDR_SIZE, phnum, 0, 0, 0)
    return ehdr + b"".join(phdrs) + bytes(body)


def write_plugin(path, segments, code_bin):
    data = build_plugin(segments, code_bin)
    with open(path, "wb") as f:
        f.write(data)
    return data
