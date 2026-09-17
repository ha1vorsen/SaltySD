#!/usr/bin/env python3


import json
import struct
import sys

# ---------------------------------------------------------------- constants

MAGIC = b"3GX$0002"
HEADER_SIZE = 0x94  # verified against the reference file

# Luma maps the plugin at 0x07000000, behind a 0x100-byte header.
PLUGIN_CODE_VA = 0x07000100

# infos.flags bitfield (3gx.h _3gx_Infos)
F_EMBEDDED_EXE_LOAD = 1 << 0
F_EMBEDDED_SWAP     = 1 << 1
F_MEMREGION_SHIFT   = 2      # 2 bits -> memRegionSizes[] = {5MiB, 2MiB, 10MiB, 5MiB}
F_COMPAT_SHIFT      = 4      # 2 bits -> 0=console, 1=emulator, 2=console+emulator
F_EVENTS_SELF_MGD   = 1 << 6
F_SWAP_NOT_NEEDED   = 1 << 7
F_USE_PRIVATE_MEM   = 1 << 8
F_ALLOW_HOMEBREW    = 1 << 9

COMPAT = {"console": 0, "emulator": 1, "both": 2}
MEMREGION = {"5MiB": 0, "2MiB": 1, "10MiB": 2}

# Sums the image; Luma runs this and compares against exeLoadChecksum.
EXE_LOAD_FUNC = [
    0xE92D40C0, 0xE3A07000, 0xE4906004, 0xE0877006,
    0xE1500001, 0x1AFFFFFB, 0xE1A00007, 0xE8BD80C0,
    0xE320F000,
]

# Luma swaps the plugin out on HOME; this just lets it checksum the block.
SWAP_FUNC = EXE_LOAD_FUNC


# --------------------------------------------------------------------- ELF

def read_elf_segments(path):
    """Return (code, rodata, data, bss_size) from the ELF's PT_LOAD headers."""
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit(f"{path}: not an ELF")
    if d[4] != 1 or d[5] != 1:
        raise SystemExit(f"{path}: expected 32-bit little-endian ARM ELF")

    e_phoff, = struct.unpack_from("<I", d, 0x1C)
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2A)

    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_flags, _align = \
            struct.unpack_from("<IIIIIIII", d, off)
        # An empty PT_LOAD sorts to the front and breaks the contiguity check.
        if p_type == 1 and p_memsz != 0:
            loads.append({
                "off": p_offset, "vaddr": p_vaddr,
                "filesz": p_filesz, "memsz": p_memsz, "flags": p_flags,
                "bytes": d[p_offset:p_offset + p_filesz],
            })

    if not loads:
        raise SystemExit(f"{path}: no PT_LOAD segments")

    loads.sort(key=lambda s: s["vaddr"])

    if loads[0]["vaddr"] != PLUGIN_CODE_VA:
        raise SystemExit(
            f"{path}: first PT_LOAD is at 0x{loads[0]['vaddr']:08X}, expected "
            f"0x{PLUGIN_CODE_VA:08X}. Link with __start__ = 0x07000100."
        )

    # Luma reads the three sizes as one block, so they must tile without gaps.
    code = rodata = data = b""
    bss = 0
    expect_va = loads[0]["vaddr"]
    for i, s in enumerate(loads):
        if s["vaddr"] != expect_va:
            raise SystemExit(
                f"{path}: PT_LOAD {i} starts at 0x{s['vaddr']:08X} but the previous "
                f"segment ends at 0x{expect_va:08X}. Segments must be contiguous."
            )
        x = s["flags"] & 0x7  # PF_X=1 PF_W=2 PF_R=4
        if x & 1:
            code += s["bytes"]
        elif x & 2:
            data += s["bytes"]
        else:
            rodata += s["bytes"]
        bss += s["memsz"] - s["filesz"]
        expect_va = s["vaddr"] + s["memsz"]

    return code, rodata, data, bss


# ------------------------------------------------------------------- build

def u32sum(blob):
    """Mirror EXE_LOAD_FUNC: sum of little-endian u32 words, mod 2^32."""
    if len(blob) % 4:
        raise SystemExit("image is not a multiple of 4 bytes")
    total = 0
    for (w,) in struct.iter_unpack("<I", blob):
        total = (total + w) & 0xFFFFFFFF
    return total


def cstr(s):
    return s.encode("utf-8") + b"\0"


def build(elf_path, info, out_path):
    code, rodata, data, bss = read_elf_segments(elf_path)
    image = code + rodata + data

    flags = F_EMBEDDED_EXE_LOAD
    flags |= MEMREGION[info.get("memoryRegion", "2MiB")] << F_MEMREGION_SHIFT
    flags |= COMPAT[info.get("compatibility", "both")] << F_COMPAT_SHIFT
    swap_needed = not info.get("swapNotNeeded", True)
    if swap_needed:
        flags |= F_EMBEDDED_SWAP
    else:
        flags |= F_SWAP_NOT_NEEDED
    if info.get("eventsSelfManaged", False):
        flags |= F_EVENTS_SELF_MGD
    if info.get("usePrivateMemory", False):
        flags |= F_USE_PRIVATE_MEM
    if info.get("allowHomebrewLoad", False):
        flags |= F_ALLOW_HOMEBREW

    author = cstr(info["author"])
    title = cstr(info["title"])
    summary = cstr(info.get("summary", ""))
    description = cstr(info.get("description", ""))
    targets = [int(str(t), 0) for t in info.get("targets", [])]

    # --- lay out the file -------------------------------------------------
    blob = bytearray()

    def place(payload, align=1):
        while len(blob) % align:
            blob.append(0)
        off = HEADER_SIZE + len(blob)
        blob.extend(payload)
        return off

    title_off = place(title)
    author_off = place(author)
    summary_off = place(summary)
    description_off = place(description)
    exe_load_off = place(struct.pack(f"<{len(EXE_LOAD_FUNC)}I", *EXE_LOAD_FUNC), 4)
    swap_off = place(struct.pack(f"<{len(SWAP_FUNC)}I", *SWAP_FUNC), 4) if swap_needed else 0
    targets_off = place(struct.pack(f"<{len(targets)}I", *targets), 4) if targets else 0
    code_off = place(image, 4)

    rodata_off = code_off + len(code)
    data_off = rodata_off + len(rodata)

    # --- header -----------------------------------------------------------
    h = bytearray()
    h += MAGIC
    h += struct.pack("<II", info.get("version", 0), 0)          # version, reserved
    h += struct.pack("<II", len(author), author_off)
    h += struct.pack("<II", len(title), title_off)
    h += struct.pack("<II", len(summary), summary_off)
    h += struct.pack("<II", len(description), description_off)
    h += struct.pack("<II", flags, u32sum(image))               # flags, exeLoadChecksum
    h += struct.pack("<4I", 0, 0, 0, 0)                         # builtInLoadExeArgs
    h += struct.pack("<4I", 0, 0, 0, 0)                         # builtInSwapSaveLoadArgs
    h += struct.pack("<III", code_off, rodata_off, data_off)
    h += struct.pack("<IIII", len(code), len(rodata), len(data), bss)
    h += struct.pack("<III", exe_load_off, swap_off, swap_off)  # exe/swapSave/swapLoad
    h += struct.pack("<II", len(targets), targets_off)
    h += struct.pack("<III", 0, 0, 0)                           # symtable (unused)

    assert len(h) == HEADER_SIZE, f"header is 0x{len(h):X}, expected 0x{HEADER_SIZE:X}"

    open(out_path, "wb").write(bytes(h) + bytes(blob))

    print(f"wrote {out_path}")
    print(f"  code   0x{len(code):06X} @ file 0x{code_off:06X} -> VA 0x{PLUGIN_CODE_VA:08X}")
    print(f"  rodata 0x{len(rodata):06X} @ file 0x{rodata_off:06X}")
    print(f"  data   0x{len(data):06X} @ file 0x{data_off:06X}")
    print(f"  bss    0x{bss:06X}")
    print(f"  flags  0x{flags:08X}  checksum 0x{u32sum(image):08X}")
    print(f"  swap   {'yes, functions at file 0x%06X' % swap_off if swap_needed else 'not needed'}")
    print(f"  targets {[f'0x{t:08X}' for t in targets] or '(any)'}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: mk3gx.py <input.elf> <info.json> <output.3gx>')
    with open(sys.argv[2], "r", encoding="utf-8") as f:
        info = json.load(f)
    build(sys.argv[1], info, sys.argv[3])


if __name__ == "__main__":
    main()
