#!/usr/bin/env python3

import struct
import sys

from arm_stack import verify_routine

ROUTINES = (
    ("plugin_start", True),
    ("saltysd_menu_hook", True),
    ("saltysd_normload", False),
    ("saltysd_rf_hook", True),
    ("saltysd_threadload", False),
)


def read_elf(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x01\x01":
        raise SystemExit(f"{path}: expected a little-endian ELF32 file")

    section_offset, = struct.unpack_from("<I", data, 0x20)
    section_size, section_count, names_index = struct.unpack_from("<HHH", data, 0x2E)
    sections = []
    for index in range(section_count):
        offset = section_offset + index * section_size
        fields = struct.unpack_from("<IIIIIIIIII", data, offset)
        sections.append({
            "name_off": fields[0], "type": fields[1], "flags": fields[2],
            "addr": fields[3], "off": fields[4], "size": fields[5],
            "link": fields[6], "entsize": fields[9],
        })

    names = sections[names_index]
    for section in sections:
        start = names["off"] + section["name_off"]
        section["name"] = data[start:data.index(b"\0", start)].decode()

    symbols = {}
    for section in sections:
        if section["type"] != 2:
            continue
        strings = sections[section["link"]]
        start = section["off"]
        stop = start + section["size"]
        for offset in range(start, stop, section["entsize"]):
            fields = struct.unpack_from("<IIIBBH", data, offset)
            name_off, value, size, _info, _other, index = fields
            if not name_off:
                continue
            start = strings["off"] + name_off
            name = data[start:data.index(b"\0", start)].decode()
            symbols[name] = (value, size, index)
    return data, sections, symbols


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_plugin_abi.py <saltysd.elf>")
    data, sections, symbols = read_elf(sys.argv[1])
    total_calls = 0

    for name, balanced in ROUTINES:
        if name not in symbols:
            raise SystemExit(f"plugin ELF has no {name} symbol")
        start, _size, section_index = symbols[name]
        section = sections[section_index]
        end = section["addr"] + section["size"]

        def read_word(address, current=section):
            offset = current["off"] + address - current["addr"]
            if not current["addr"] <= address < current["addr"] + current["size"]:
                raise ValueError(f"{name}: address 0x{address:08X} left {current['name']}")
            return struct.unpack_from("<I", data, offset)[0]

        try:
            calls, _exits = verify_routine(
                read_word, start, end, name, balanced=balanced,
            )
        except ValueError as error:
            raise SystemExit(str(error))
        total_calls += calls

    print(f"plugin ARM ABI ok: {len(ROUTINES)} routines, {total_calls} reachable calls")


if __name__ == "__main__":
    main()
