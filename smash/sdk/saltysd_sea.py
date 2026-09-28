#!/usr/bin/env python3

import argparse
import os
import re
import struct
import sys

BASE = 0x100000

SEA_VERSION = 1
SEA_BYTES_MAX = 0x10000
SEA_SEGMENTS_MAX = 256
SEA_SIGNATURES_MAX = 64
SIG_BYTES_MAX = 64
ANCHOR_REACH = 0x1000

EHDR_SIZE = 52
PHDR_SIZE = 32
NOTE_HEADER_SIZE = 12
ET_EXEC = 2
EM_ARM = 40
EF_ARM_EABI5_HARD = 0x05000400
PT_LOAD = 1
PT_NOTE = 4
PF_R = 4
PF_X = 1
NOTE_OWNER = b"SaltySD\0"
NOTE_ORIGINAL = 1
NOTE_SEA_VERSION = 2
NOTE_SIGNATURES = 3
NOTE_PLACEMENT = 4
NOTE_FIXUPS = 5

FIXUP_BRANCH = 1
FIXUP_ABS32 = 2
FIXUP_NAMES = {FIXUP_BRANCH: "branch", FIXUP_ABS32: "abs32"}
BRANCH_REACH = 0x800000

LIMITS_EPILOG = ("SEA v1 finds each patch in code.bin by a signature, patches it once at boot, "
                 "and holds at most 64 KiB. See README.md for every v1 limit.")


class SeaError(ValueError):
    pass


def align4(n):
    return (n + 3) & ~3


def is_branch(word):
    return (word >> 28) != 0xF and (word >> 25) & 7 == 5


def branch_target(word, site):
    disp = word & 0xFFFFFF
    if disp & 0x800000:
        disp -= 0x1000000
    return site + 8 + disp * 4


def encode_branch(word, site, target):
    if site & 3 or target & 3:
        raise SeaError(f"branch at 0x{site:08X} to 0x{target:08X} is not word aligned")
    disp = (target - (site + 8)) >> 2
    if not -BRANCH_REACH <= disp < BRANCH_REACH:
        raise SeaError(f"branch at 0x{site:08X} cannot reach 0x{target:08X}")
    return (word & 0xFF000000) | (disp & 0xFFFFFF)


def aligned_matches(code, pattern, mask, limit=2):
    found = []
    if all(m == 0xFF for m in mask):
        at = code.find(pattern)
        while at >= 0 and len(found) < limit:
            if at % 4 == 0:
                found.append(BASE + at)
            at = code.find(pattern, at + 1)
        return found

    parts = []
    for b, m in zip(pattern, mask):
        if m == 0xFF:
            parts.append(re.escape(bytes([b])))
        elif m == 0:
            parts.append(b".")
        else:
            options = bytes(v for v in range(256) if (v ^ b) & m == 0)
            parts.append(b"[" + b"".join(re.escape(bytes([v])) for v in options) + b"]")
    regex = re.compile(b"(?=" + b"".join(parts) + b")", re.DOTALL)
    for m in regex.finditer(code):
        if m.start() % 4 == 0:
            found.append(BASE + m.start())
            if len(found) >= limit:
                break
    return found


def window(code, start, length):
    at = start - BASE
    pattern = bytearray(code[at:at + length])
    mask = bytearray(b"\xFF" * length)
    for w in range(0, length, 4):
        word = struct.unpack_from("<I", pattern, w)[0]
        if is_branch(word):
            mask[w:w + 3] = b"\0\0\0"
            pattern[w:w + 3] = b"\0\0\0"
    return bytes(pattern), bytes(mask)


def usable(pattern, mask):
    if not any(mask[w:w + 4] == b"\xFF" * 4 for w in range(0, len(mask), 4)):
        return False
    return any(p & m for p, m in zip(pattern, mask))


def unique_at(code, start, length):
    if start < BASE or start + length > BASE + len(code):
        return None
    pattern, mask = window(code, start, length)
    if not usable(pattern, mask):
        return None
    if aligned_matches(code, pattern, mask) != [start]:
        return None
    return pattern, mask


class Layout:
    def __init__(self, code):
        self.code = code
        self.sigs = []
        self.sig_at = []

    def add(self, pattern, mask, at):
        for i, s in enumerate(self.sigs):
            if s == (pattern, mask):
                return i
        if len(self.sigs) == SEA_SIGNATURES_MAX:
            raise SeaError(f"more than {SEA_SIGNATURES_MAX} signatures needed")
        self.sigs.append((pattern, mask))
        self.sig_at.append(at)
        return len(self.sigs) - 1

    def own(self, addr):
        word = addr & ~3
        for length in range(4, SIG_BYTES_MAX + 4, 4):
            for back in range(0, length, 4):
                found = unique_at(self.code, word - back, length)
                if found:
                    return self.add(*found, word - back), addr - (word - back)
        return None

    def reuse(self, addr):
        best = None
        for i, at in enumerate(self.sig_at):
            if at <= addr and addr - at <= ANCHOR_REACH and (best is None or at > self.sig_at[best]):
                best = i
        if best is None:
            return None
        return best, addr - self.sig_at[best]

    def before(self, addr):
        word = addr & ~3
        for start in range(word - 4, max(BASE, word - ANCHOR_REACH) - 4, -4):
            if self.code[start - BASE:start - BASE + 4] == b"\0\0\0\0":
                continue
            for length in range(4, SIG_BYTES_MAX + 4, 4):
                found = unique_at(self.code, start, length)
                if found:
                    return self.add(*found, start), addr - start
        return None

    def locate(self, addr, what):
        if not BASE <= addr < BASE + len(self.code):
            raise SeaError(f"{what} at 0x{addr:08X} is outside code.bin")
        found = self.own(addr) or self.reuse(addr) or self.before(addr)
        if not found:
            raise SeaError(f"no unique signature for {what} at 0x{addr:08X} within "
                           f"0x{ANCHOR_REACH:X} bytes; it may sit in a large run of repeated bytes")
        return found


def plan(patches, code, branch_fixups=True):
    patches = sorted((vaddr, bytes(data), bytes(original)) for vaddr, data, original in patches)
    check_patches(patches, BASE + len(code))
    layout = Layout(code)
    placement = [layout.locate(vaddr, "segment") for vaddr, _, _ in patches]

    fixups = []
    notes = []
    if branch_fixups:
        for i, (vaddr, data, _) in enumerate(patches):
            for at in range(-vaddr % 4, len(data) - 3, 4):
                word = struct.unpack_from("<I", data, at)[0]
                if not is_branch(word):
                    continue
                site = vaddr + at
                target = branch_target(word, site)
                if vaddr <= target < vaddr + len(data) or not BASE <= target < BASE + len(code):
                    continue
                owner = next((j for j, (v, d, _) in enumerate(patches) if v <= target < v + len(d)), None)
                if owner is not None:
                    sig, off = placement[owner][0], placement[owner][1] + target - patches[owner][0]
                    where = "segment"
                else:
                    sig, off = layout.locate(target, f"branch target of 0x{site:08X}")
                    where = "game"
                fixups.append((i, at, FIXUP_BRANCH, sig, off))
                notes.append(f"branch at 0x{site:08X} -> 0x{target:08X} ({where})")
    return patches, layout.sigs, placement, fixups, notes


def check_patches(patches, code_end=None):
    if not patches:
        raise SeaError("no PT_LOAD segments, so there is nothing to patch")
    if len(patches) > SEA_SEGMENTS_MAX:
        raise SeaError(f"{len(patches)} segments; SEA v1 allows at most {SEA_SEGMENTS_MAX}")
    last = None
    for vaddr, data, original in patches:
        if not data:
            raise SeaError(f"empty segment at 0x{vaddr:08X}")
        if vaddr < BASE:
            raise SeaError(f"segment at 0x{vaddr:08X} is below code.bin, which starts at 0x{BASE:08X}")
        if code_end is not None and vaddr + len(data) > code_end:
            raise SeaError(f"segment at 0x{vaddr:08X} runs past the end of code.bin (0x{code_end:08X})")
        if last is not None and vaddr < last[0] + len(last[1]):
            raise SeaError(f"segment at 0x{vaddr:08X} overlaps the one at 0x{last[0]:08X}")
        if len(original) != len(data):
            raise SeaError(f"segment at 0x{vaddr:08X} has {len(data)} bytes but "
                           f"{len(original)} original bytes")
        last = (vaddr, data)


def make_note(kind, desc):
    note = struct.pack("<III", len(NOTE_OWNER), len(desc), kind) + NOTE_OWNER + desc
    return note + b"\0" * (align4(len(note)) - len(note))


def build_sea(patches, sigs, placement, fixups=()):
    patches = list(patches)
    check_patches(patches)
    if not sigs or len(sigs) > SEA_SIGNATURES_MAX:
        raise SeaError(f"SEA v1 needs 1 to {SEA_SIGNATURES_MAX} signatures")
    if len(placement) != len(patches):
        raise SeaError("every segment needs a placement")

    sig_desc = struct.pack("<I", len(sigs))
    for pattern, mask in sigs:
        if len(pattern) != len(mask) or not 4 <= len(pattern) <= SIG_BYTES_MAX or len(pattern) % 4:
            raise SeaError("signatures are 4 to 64 bytes, a multiple of 4, with one mask byte each")
        if not usable(pattern, mask):
            raise SeaError("every signature needs at least one fully matched word")
        body = struct.pack("<I", len(pattern)) + pattern + mask
        sig_desc += body + b"\0" * (align4(len(body)) - len(body))
    for sig, _ in placement:
        if not 0 <= sig < len(sigs):
            raise SeaError(f"placement names signature {sig}, which does not exist")
    place_desc = b"".join(struct.pack("<Ii", sig, off) for sig, off in placement)

    notes = [make_note(NOTE_SEA_VERSION, struct.pack("<I", SEA_VERSION)),
             make_note(NOTE_ORIGINAL, b"".join(original for _, _, original in patches)),
             make_note(NOTE_SIGNATURES, sig_desc),
             make_note(NOTE_PLACEMENT, place_desc)]
    if fixups:
        notes.append(make_note(NOTE_FIXUPS, struct.pack("<I", len(fixups)) + b"".join(
            struct.pack("<IIIIi", *f) for f in fixups)))

    phnum = len(notes) + len(patches)
    at = EHDR_SIZE + phnum * PHDR_SIZE
    phdrs = []
    body = bytearray()
    for note in notes:
        phdrs.append(struct.pack("<IIIIIIII", PT_NOTE, at, 0, 0, len(note), len(note), PF_R, 4))
        body += note
        at += len(note)
    for vaddr, data, _ in patches:
        phdrs.append(struct.pack("<IIIIIIII", PT_LOAD, at, vaddr, vaddr, len(data), len(data),
                                 PF_R | PF_X, 4))
        padded = data + b"\0" * (align4(len(data)) - len(data))
        body += padded
        at += len(padded)

    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    ehdr = ident + struct.pack("<HHIIIIIHHHHHH", ET_EXEC, EM_ARM, 1, 0, EHDR_SIZE, 0,
                               EF_ARM_EABI5_HARD, EHDR_SIZE, PHDR_SIZE, phnum, 0, 0, 0)
    sea = ehdr + b"".join(phdrs) + bytes(body)
    if len(sea) > SEA_BYTES_MAX:
        raise SeaError(f"the .sea would be {len(sea)} bytes; SEA v1 allows at most {SEA_BYTES_MAX}")
    return sea


def build_plugin(segments, code_bin, branch_fixups=True):
    patches = [(vaddr, data, code_bin[vaddr - BASE:vaddr - BASE + len(data)])
               for vaddr, data in segments]
    patches, sigs, placement, fixups, _ = plan(patches, code_bin, branch_fixups)
    sea = build_sea(patches, sigs, placement, fixups)
    check_resolves_home(sea, code_bin)
    return sea


def write_plugin(path, segments, code_bin):
    data = build_plugin(segments, code_bin)
    with open(path, "wb") as f:
        f.write(data)
    return data


def read_elf(data):
    if len(data) < EHDR_SIZE or data[:4] != b"\x7fELF":
        raise SeaError("not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise SeaError("not a 32-bit little-endian ELF")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    if e_machine != EM_ARM:
        raise SeaError("not an ARM ELF")
    if e_type != ET_EXEC:
        raise SeaError("not an executable ELF; link it (ET_EXEC) instead of passing an object file")
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    if phentsize != PHDR_SIZE or phoff + phnum * PHDR_SIZE > len(data):
        raise SeaError("program headers are missing or damaged")

    segments = []
    notes = {}
    for i in range(phnum):
        p_type, offset, vaddr, _, filesz, memsz, _, _ = struct.unpack_from(
            "<IIIIIIII", data, phoff + i * PHDR_SIZE)
        if offset + filesz > len(data):
            raise SeaError(f"segment {i} runs past the end of the file")
        if p_type == PT_LOAD:
            if filesz != memsz:
                raise SeaError(f"segment at 0x{vaddr:08X} has {memsz - filesz} bytes of .bss; "
                               "SEA v1 can only write bytes that are in the file")
            segments.append((vaddr, data[offset:offset + filesz]))
        elif p_type == PT_NOTE:
            at, end = offset, offset + filesz
            while at + NOTE_HEADER_SIZE <= end:
                namesz, descsz, kind = struct.unpack_from("<III", data, at)
                name_at = at + NOTE_HEADER_SIZE
                desc_at = name_at + align4(namesz)
                if desc_at + descsz > end:
                    raise SeaError(f"note in segment {i} is damaged")
                if data[name_at:name_at + namesz] == NOTE_OWNER:
                    if kind in notes:
                        raise SeaError(f"more than one SaltySD note of type {kind}")
                    notes[kind] = data[desc_at:desc_at + descsz]
                at = desc_at + align4(descsz)
    return segments, notes


def sea_version(notes):
    desc = notes.get(NOTE_SEA_VERSION)
    if desc is None:
        return None
    if len(desc) != 4:
        raise SeaError("the SEA version note is damaged")
    return struct.unpack("<I", desc)[0]


def parse_locators(notes, num_segments):
    if NOTE_SIGNATURES not in notes or NOTE_PLACEMENT not in notes:
        raise SeaError("no signatures; this file was made by an older tool, rebuild it with "
                       "convert --code")
    desc = notes[NOTE_SIGNATURES]
    count, = struct.unpack_from("<I", desc, 0)
    sigs, at = [], 4
    for _ in range(count):
        n, = struct.unpack_from("<I", desc, at)
        sigs.append((desc[at + 4:at + 4 + n], desc[at + 4 + n:at + 4 + 2 * n]))
        at += align4(4 + 2 * n)
    place = notes[NOTE_PLACEMENT]
    if len(place) != 8 * num_segments:
        raise SeaError("the placement note does not match the segments")
    placement = [struct.unpack_from("<Ii", place, 8 * i) for i in range(num_segments)]
    fixups = []
    if NOTE_FIXUPS in notes:
        fdesc = notes[NOTE_FIXUPS]
        n, = struct.unpack_from("<I", fdesc, 0)
        if len(fdesc) != 4 + 20 * n:
            raise SeaError("the fix-up note is damaged")
        fixups = [struct.unpack_from("<IIIIi", fdesc, 4 + 20 * i) for i in range(n)]
    return sigs, placement, fixups


def resolve(data, code):
    segments, notes = read_elf(data)
    if sea_version(notes) != SEA_VERSION:
        raise SeaError(f"not a SEA v{SEA_VERSION} file")
    sigs, placement, fixups = parse_locators(notes, len(segments))

    matches = []
    for i, (pattern, mask) in enumerate(sigs):
        found = aligned_matches(code, pattern, mask)
        if not found:
            raise SeaError(f"signature {i} is not found")
        if len(found) > 1:
            raise SeaError(f"signature {i} matches more than once")
        matches.append(found[0])

    placed = []
    for (vaddr, seg), (sig, off) in zip(segments, placement):
        addr = matches[sig] + off
        if addr < BASE or addr + len(seg) > BASE + len(code):
            raise SeaError(f"segment 0x{vaddr:08X} lands outside code.bin")
        placed.append([addr, bytearray(seg), vaddr])

    for seg, at, kind, sig, off in fixups:
        addr, body, _ = placed[seg]
        target = matches[sig] + off
        word = struct.unpack_from("<I", body, at)[0]
        if kind == FIXUP_ABS32:
            struct.pack_into("<I", body, at, target)
        elif kind == FIXUP_BRANCH and is_branch(word):
            struct.pack_into("<I", body, at, encode_branch(word, addr + at, target))
        else:
            raise SeaError(f"fix-up at 0x{addr + at:08X} is not a branch")

    original = notes.get(NOTE_ORIGINAL, b"")
    used = 0
    for addr, body, vaddr in placed:
        want = original[used:used + len(body)]
        used += len(body)
        if code[addr - BASE:addr - BASE + len(body)] != want:
            raise SeaError(f"segment 0x{vaddr:08X} lands at 0x{addr:08X}, where the game's bytes "
                           "differ from the stored original bytes")
    return matches, [(addr, bytes(body), vaddr) for addr, body, vaddr in placed]


def check_resolves_home(sea, code):
    segments, _ = read_elf(sea)
    _, placed = resolve(sea, code)
    for (vaddr, seg), (addr, body, _) in zip(segments, placed):
        if addr != vaddr or body != seg:
            raise SeaError(f"segment 0x{vaddr:08X} would land at 0x{addr:08X} in its own code.bin")


def convert(data, code_bin, branch_fixups=True):
    segments, notes = read_elf(data)
    version = sea_version(notes)
    if version is not None and version != SEA_VERSION:
        raise SeaError(f"this is a SEA v{version} file; this tool makes SEA v{SEA_VERSION}")
    if not segments:
        raise SeaError("no PT_LOAD segments, so there is nothing to patch")
    for vaddr, seg in segments:
        if vaddr < BASE:
            raise SeaError(f"segment at 0x{vaddr:08X} is below code.bin, which starts at 0x{BASE:08X}")
        if vaddr - BASE + len(seg) > len(code_bin):
            raise SeaError(f"segment at 0x{vaddr:08X} runs past the end of code.bin "
                           f"(0x{BASE + len(code_bin):08X})")

    from_code = [code_bin[vaddr - BASE:vaddr - BASE + len(seg)] for vaddr, seg in segments]
    stored = notes.get(NOTE_ORIGINAL)
    if stored is not None:
        if len(stored) != sum(len(seg) for _, seg in segments):
            raise SeaError(f"the original-bytes note holds {len(stored)} bytes, but the segments "
                           f"hold {sum(len(seg) for _, seg in segments)}")
        at = 0
        for (vaddr, seg), theirs in zip(segments, from_code):
            if stored[at:at + len(seg)] != theirs:
                raise SeaError(f"at 0x{vaddr:08X} the ELF's original bytes differ from this "
                               "code.bin; it was made for another game version or region")
            at += len(seg)

    patches, sigs, placement, fixups, report = plan(
        [(vaddr, seg, orig) for (vaddr, seg), orig in zip(segments, from_code)], code_bin, branch_fixups)
    sea = build_sea(patches, sigs, placement, fixups)
    check_resolves_home(sea, code_bin)
    return sea, report


def pattern_text(pattern, mask):
    return " ".join(f"{p:02X}" if m == 0xFF else "??" for p, m in zip(pattern, mask))


def describe(data, code=None):
    segments, notes = read_elf(data)
    version = sea_version(notes)
    lines = [f"SEA v{version}" if version is not None else "not a SEA (no version note)",
             f"{len(data)} bytes, {len(segments)} segments, "
             f"{sum(len(seg) for _, seg in segments)} bytes patched",
             "original bytes: " + ("stored" if NOTE_ORIGINAL in notes else "missing")]
    try:
        sigs, placement, fixups = parse_locators(notes, len(segments))
    except SeaError as e:
        lines.append(str(e))
        for vaddr, seg in sorted(segments):
            lines.append(f"  0x{vaddr:08X}  {len(seg):#x} bytes")
        return lines

    resolved = None
    if code is not None:
        try:
            resolved = resolve(data, code)
        except SeaError as e:
            lines.append(f"does not resolve in this code.bin: {e}")
    lines.append("segments:")
    for i, ((vaddr, seg), (sig, off)) in enumerate(zip(segments, placement)):
        at = f" -> 0x{resolved[1][i][0]:08X}" if resolved else ""
        lines.append(f"  0x{vaddr:08X}  {len(seg):#x} bytes  signature {sig} {off:+#x}{at}")
    lines.append("signatures:")
    for i, (pattern, mask) in enumerate(sigs):
        at = f"  found at 0x{resolved[0][i]:08X}" if resolved else ""
        lines.append(f"  {i}: {pattern_text(pattern, mask)}{at}")
    if fixups:
        lines.append("fix-ups:")
        for seg, at, kind, sig, off in fixups:
            lines.append(f"  segment 0x{segments[seg][0]:08X} {at:+#x}: {FIXUP_NAMES.get(kind, kind)} "
                         f"to signature {sig} {off:+#x}")
    return lines


def read_file(path, what):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        raise SeaError(f"cannot read {what} {path}: {e.strerror}")


def run_convert(args):
    data = read_file(args.input, "ELF")
    code_bin = read_file(args.code, "code.bin")
    out = args.output or os.path.splitext(args.input)[0] + ".sea"
    if os.path.abspath(out) == os.path.abspath(args.input):
        raise SeaError("the output would overwrite the input; pass -o")
    sea, report = convert(data, code_bin, not args.no_branch_fixups)
    with open(out, "wb") as f:
        f.write(sea)
    segments, notes = read_elf(sea)
    sigs, _, fixups = parse_locators(notes, len(segments))
    print(f"wrote {out}: SEA v{SEA_VERSION}, {len(segments)} segments, {len(sigs)} signatures, "
          f"{len(fixups)} fix-ups, {len(sea)} bytes")
    for line in report:
        print(f"  {line}")

    failed = 0
    for path in args.also:
        other = read_file(path, "code.bin")
        try:
            _, placed = resolve(sea, other)
        except SeaError as e:
            print(f"warning: does not resolve in {path}: {e}", file=sys.stderr)
            failed += 1
            continue
        moved = sorted({addr - vaddr for addr, _, vaddr in placed})
        shift = ", ".join(f"{m:+#x}" for m in moved)
        print(f"resolves in {path}: segments moved by {shift}")
    return 3 if failed else 0


def run_info(args):
    code = read_file(args.code, "code.bin") if args.code else None
    for line in describe(read_file(args.input, "file"), code):
        print(line)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="saltysd_sea.py",
        description="Make and inspect SALT Engine Archive (.sea) plugins for SaltySD.",
        epilog=LIMITS_EPILOG)
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("convert", help="turn an ARM ELF into a SEA v1 plugin",
                            description="Turn an ARM ELF into a SEA v1 plugin.", epilog=LIMITS_EPILOG)
    p.add_argument("input", help="the ELF (or older .sea) to convert")
    p.add_argument("-o", "--output", help="where to write the .sea (default: input name with .sea)")
    p.add_argument("--code", metavar="CODE_BIN", required=True,
                   help="the decompressed code.bin the ELF was made for; signatures are "
                        "chosen so they are unique in it")
    p.add_argument("--also", metavar="CODE_BIN", action="append", default=[],
                   help="another code.bin (region or update layout) the .sea should also work "
                        "on; reports where it lands or why it does not")
    p.add_argument("--no-branch-fixups", action="store_true",
                   help="leave B/BL instructions as they are instead of re-aiming them at load")
    p.set_defaults(run=run_convert)

    p = commands.add_parser("info", help="show what a .sea or plugin ELF contains",
                            description="Show what a .sea or plugin ELF contains.")
    p.add_argument("input", help="the file to inspect")
    p.add_argument("--code", metavar="CODE_BIN", help="also show where it lands in this code.bin")
    p.set_defaults(run=run_info)

    args = parser.parse_args(argv)
    try:
        return args.run(args)
    except SeaError as e:
        print(f"error: {args.input}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
