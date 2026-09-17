#!/usr/bin/env python3


import struct
import sys

BASE = 0x100000


def read_elf(path):
    """Return (sections by VA, symbol table) for a little-endian 32-bit ARM ELF."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF")

    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, s_type, _flags, addr, offset, size, link, _info, _align, entsize = \
            struct.unpack_from("<IIIIIIIIII", d, off)
        sections.append({"name": name, "type": s_type, "addr": addr,
                         "off": offset, "size": size, "link": link,
                         "entsize": entsize})

    shstr = sections[e_shstrndx]

    def sname(s):
        start = shstr["off"] + s["name"]
        return d[start:d.index(b"\0", start)].decode()

    symbols = {}
    for s in sections:
        if s["type"] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[s["link"]]
        for off in range(s["off"], s["off"] + s["size"], s["entsize"]):
            st_name, st_value, st_size, _info, _other, _shndx = \
                struct.unpack_from("<IIIBBH", d, off)
            if not st_name:
                continue
            start = strtab["off"] + st_name
            symbols[d[start:d.index(b"\0", start)].decode()] = (st_value, st_size)

    loaded = [s for s in sections if s["addr"] and s["type"] == 1]  # SHT_PROGBITS
    return d, loaded, symbols, sname


def reader(image, loaded):
    def read(va, n):
        for s in loaded:
            if s["addr"] <= va < s["addr"] + s["size"]:
                off = s["off"] + (va - s["addr"])
                return image[off:off + n]
        raise SystemExit(f"address 0x{va:08X} is not in any loaded section")
    return read


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: verify_plugin.py <saltysd.elf> <pristine code.bin> <patched code.bin>')
    elf_path, pristine_path, patched_path = sys.argv[1:4]

    image, loaded, symbols, _sname = read_elf(elf_path)
    read = reader(image, loaded)

    if "saltysd_patches" not in symbols:
        raise SystemExit("the plugin has no saltysd_patches symbol")
    table_va, table_size = symbols["saltysd_patches"]
    if table_size % 16:
        raise SystemExit("the patch table is not a whole number of entries")

    pristine = bytearray(open(pristine_path, "rb").read())
    patched = open(patched_path, "rb").read()

    count = table_size // 16
    total = 0
    for i in range(count):
        entry = read(table_va + i * 16, 16)
        addr, length, want_va, orig_va = struct.unpack("<IIII", entry)
        want = read(want_va, length)
        orig = read(orig_va, length)

        off = addr - BASE
        if pristine[off:off + length] != orig:
            raise SystemExit(
                f"entry {i} at 0x{addr:06X}: the bytes it expects are not what "
                "the pristine binary holds"
            )
        pristine[off:off + length] = want
        total += length

    if bytes(pristine) != patched:
        raise SystemExit(
            "applying the plugin's own table does NOT reproduce code_saltysd.bin"
        )

    print(f"plugin table reproduces code_saltysd.bin exactly: "
          f"{count} runs, {total} bytes")


if __name__ == "__main__":
    main()
