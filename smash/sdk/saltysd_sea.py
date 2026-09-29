#!/usr/bin/env python3

import argparse
import os
import re
import struct
import sys

BASE = 0x100000

SEA_VERSION = "1.1"
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
NOTE_FEATURES = 6
NOTE_TARGETS = 7

FEATURE_CRO_TARGETS = 1

FIXUP_BRANCH = 1
FIXUP_ABS32 = 2
FIXUP_NAMES = {FIXUP_BRANCH: "branch", FIXUP_ABS32: "abs32"}
BRANCH_REACH = 0x800000

LIMITS_EPILOG = ("SEA 1.1 finds each patch by a signature in code.bin or a named CRO. "
                 "See README.md for every SEA 1.1 limit.")


class SeaError(ValueError):
    pass


class Target:
    def __init__(self, name, code, start, relocations=frozenset()):
        self.name = name
        self.code = code
        self.start = start
        self.end = start + len(code)
        self.relocations = frozenset(relocations)

    @property
    def label(self):
        return "code.bin" if self.name is None else f"CRO {self.name}"


def align4(n):
    return (n + 3) & ~3


def target_for_address(targets, addr, size):
    found = [i for i, target in enumerate(targets)
             if target.start <= addr and size <= target.end - addr]
    if len(found) > 1:
        raise SeaError(f"address 0x{addr:08X} belongs to more than one target")
    return found[0] if found else None


def read_c_string(data, at, what):
    if not 0 <= at < len(data):
        raise SeaError(f"{what} points outside the CRO")
    end = data.find(b"\0", at)
    if end < 0:
        raise SeaError(f"{what} is not NUL terminated")
    try:
        return data[at:end].decode("ascii")
    except UnicodeDecodeError:
        raise SeaError(f"{what} is not ASCII") from None


def cro_target(data, link_base, path="CRO"):
    if len(data) < 0x138 or data[0x80:0x84] != b"CRO0":
        raise SeaError(f"{path} is not an unfixed CRO0 file")
    name_at, = struct.unpack_from("<I", data, 0x84)
    code_at, code_size = struct.unpack_from("<II", data, 0xB0)
    name = read_c_string(data, name_at, f"{path} module name")
    if not name:
        raise SeaError(f"{path} has an empty module name")
    if code_at > len(data) or code_size > len(data) - code_at:
        raise SeaError(f"{path} code segment runs outside the file")

    table_at, table_count = struct.unpack_from("<II", data, 0xC8)
    if table_at > len(data) or table_count > (len(data) - table_at) // 12:
        raise SeaError(f"{path} segment table runs outside the file")
    segments = [struct.unpack_from("<III", data, table_at + i * 12)
                for i in range(table_count)]

    relocations = set()
    for offset_field, count_field in ((0xF8, 0xFC), (0x128, 0x12C), (0x130, 0x134)):
        reloc_at, reloc_count = struct.unpack_from("<II", data, offset_field)
        if reloc_at > len(data) or reloc_count > (len(data) - reloc_at) // 12:
            raise SeaError(f"{path} relocation table at 0x{offset_field:X} runs outside the file")
        for i in range(reloc_count):
            tag, = struct.unpack_from("<I", data, reloc_at + i * 12)
            segment, offset = tag & 0xF, tag >> 4
            if segment >= len(segments):
                raise SeaError(f"{path} relocation {i} names missing segment {segment}")
            segment_at, segment_size, _ = segments[segment]
            if offset > segment_size or 4 > segment_size - offset:
                raise SeaError(f"{path} relocation {i} runs outside segment {segment}")
            word = segment_at + offset
            if code_at <= word and word + 4 <= code_at + code_size:
                relocations.add(link_base + word)

    return Target(name, data[code_at:code_at + code_size], link_base + code_at, relocations)


def parse_cro_arg(spec):
    try:
        path, base_text = spec.rsplit("@", 1)
        link_base = int(base_text, 0)
    except (ValueError, TypeError):
        raise SeaError(f"--cro expects FILE@BASE, got {spec!r}") from None
    data = read_file(path, "CRO")
    return cro_target(data, link_base, path)


def make_targets(code_bin, cros=()):
    targets = [Target(None, code_bin, BASE)] + list(cros)
    names = set()
    for target in targets[1:]:
        if target.name in names:
            raise SeaError(f"more than one --cro names {target.name}")
        names.add(target.name)
    for i, target in enumerate(targets):
        for other in targets[:i]:
            if target.start < other.end and other.start < target.end:
                raise SeaError(f"{target.label} overlaps {other.label} in the linked address space")
    return targets


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


def aligned_matches(code, pattern, mask, limit=2, base=BASE):
    found = []
    if all(m == 0xFF for m in mask):
        at = code.find(pattern)
        while at >= 0 and len(found) < limit:
            if at % 4 == 0:
                found.append(base + at)
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
            found.append(base + m.start())
            if len(found) >= limit:
                break
    return found


def window(code, start, length, base=BASE, relocations=frozenset()):
    at = start - base
    pattern = bytearray(code[at:at + length])
    mask = bytearray(b"\xFF" * length)
    for w in range(0, length, 4):
        word = struct.unpack_from("<I", pattern, w)[0]
        if is_branch(word):
            mask[w:w + 3] = b"\0\0\0"
            pattern[w:w + 3] = b"\0\0\0"
        if start + w in relocations:
            mask[w:w + 4] = b"\0\0\0\0"
            pattern[w:w + 4] = b"\0\0\0\0"
    return bytes(pattern), bytes(mask)


def usable(pattern, mask):
    if not any(mask[w:w + 4] == b"\xFF" * 4 for w in range(0, len(mask), 4)):
        return False
    return any(p & m for p, m in zip(pattern, mask))


def unique_at(code, start, length, base=BASE, relocations=frozenset()):
    if start < base or start + length > base + len(code):
        return None
    pattern, mask = window(code, start, length, base, relocations)
    if not usable(pattern, mask):
        return None
    if aligned_matches(code, pattern, mask, base=base) != [start]:
        return None
    return pattern, mask


class Layout:
    def __init__(self, code, base=BASE, target=0, relocations=frozenset(), shared=None):
        self.code = code
        self.base = base
        self.target = target
        self.relocations = relocations
        if shared is None:
            shared = ([], [], [])
        self.sigs, self.sig_at, self.sig_targets = shared

    def add(self, pattern, mask, at):
        for i, s in enumerate(self.sigs):
            if self.sig_targets[i] == self.target and s == (pattern, mask):
                return i
        if len(self.sigs) == SEA_SIGNATURES_MAX:
            raise SeaError(f"more than {SEA_SIGNATURES_MAX} signatures needed")
        self.sigs.append((pattern, mask))
        self.sig_at.append(at)
        self.sig_targets.append(self.target)
        return len(self.sigs) - 1

    def own(self, addr):
        word = addr & ~3
        for length in range(4, SIG_BYTES_MAX + 4, 4):
            for back in range(0, length, 4):
                found = unique_at(self.code, word - back, length, self.base, self.relocations)
                if found:
                    return self.add(*found, word - back), addr - (word - back)
        return None

    def reuse(self, addr):
        best = None
        for i, at in enumerate(self.sig_at):
            if (self.sig_targets[i] == self.target and at <= addr and addr - at <= ANCHOR_REACH and
                    (best is None or at > self.sig_at[best])):
                best = i
        if best is None:
            return None
        return best, addr - self.sig_at[best]

    def before(self, addr):
        word = addr & ~3
        for start in range(word - 4, max(self.base, word - ANCHOR_REACH) - 4, -4):
            if self.code[start - self.base:start - self.base + 4] == b"\0\0\0\0":
                continue
            for length in range(4, SIG_BYTES_MAX + 4, 4):
                found = unique_at(self.code, start, length, self.base, self.relocations)
                if found:
                    return self.add(*found, start), addr - start
        return None

    def locate(self, addr, what):
        if not self.base <= addr < self.base + len(self.code):
            raise SeaError(f"{what} at 0x{addr:08X} is outside its target code")
        found = self.own(addr) or self.reuse(addr) or self.before(addr)
        if not found:
            raise SeaError(f"no unique signature for {what} at 0x{addr:08X} within "
                           f"0x{ANCHOR_REACH:X} bytes; it may sit in a large run of repeated bytes")
        return found


def plan(patches, targets, branch_fixups=True):
    patches = sorted((vaddr, bytes(data), bytes(original), target)
                     for vaddr, data, original, target in patches)
    check_patches(patches, targets)
    shared = ([], [], [])
    layouts = [Layout(t.code, t.start, i, t.relocations, shared) for i, t in enumerate(targets)]
    placement = [layouts[target].locate(vaddr, "segment")
                 for vaddr, _, _, target in patches]

    fixups = []
    notes = []
    if branch_fixups:
        for i, (vaddr, data, _, source_target) in enumerate(patches):
            for at in range(-vaddr % 4, len(data) - 3, 4):
                word = struct.unpack_from("<I", data, at)[0]
                if not is_branch(word):
                    continue
                site = vaddr + at
                target = branch_target(word, site)
                if vaddr <= target < vaddr + len(data):
                    continue
                target_id = target_for_address(targets, target, 1)
                if target_id is None:
                    continue
                if source_target == 0 and target_id != 0:
                    raise SeaError(f"branch at 0x{site:08X} goes from code.bin to a CRO")
                if source_target != 0 and target_id not in (0, source_target):
                    raise SeaError(f"branch at 0x{site:08X} goes from one CRO to another")
                owner = next((j for j, (v, d, _, patch_target) in enumerate(patches)
                              if patch_target == target_id and v <= target < v + len(d)), None)
                if owner is not None:
                    sig, off = placement[owner][0], placement[owner][1] + target - patches[owner][0]
                    where = "segment"
                else:
                    sig, off = layouts[target_id].locate(target, f"branch target of 0x{site:08X}")
                    where = "game"
                fixups.append((i, at, FIXUP_BRANCH, sig, off))
                notes.append(f"branch at 0x{site:08X} -> 0x{target:08X} ({where})")
    return patches, shared[0], placement, fixups, notes, shared[2]


def check_patches(patches, targets=None):
    if not patches:
        raise SeaError("no PT_LOAD segments, so there is nothing to patch")
    if len(patches) > SEA_SEGMENTS_MAX:
        raise SeaError(f"{len(patches)} segments; SEA 1.1 allows at most {SEA_SEGMENTS_MAX}")
    last = {}
    for vaddr, data, original, target in patches:
        if not data:
            raise SeaError(f"empty segment at 0x{vaddr:08X}")
        if targets is not None:
            t = targets[target]
            if vaddr < t.start or vaddr + len(data) > t.end:
                raise SeaError(f"segment at 0x{vaddr:08X} runs outside {t.label}")
            if target and any(vaddr < r + 4 and r < vaddr + len(data) for r in t.relocations):
                raise SeaError(f"segment at 0x{vaddr:08X} overlaps a relocated CRO word")
        previous = last.get(target)
        if previous is not None and vaddr < previous[0] + len(previous[1]):
            raise SeaError(f"segment at 0x{vaddr:08X} overlaps the one at 0x{previous[0]:08X}")
        if len(original) != len(data):
            raise SeaError(f"segment at 0x{vaddr:08X} has {len(data)} bytes but "
                           f"{len(original)} original bytes")
        last[target] = (vaddr, data)


def make_note(kind, desc):
    note = struct.pack("<III", len(NOTE_OWNER), len(desc), kind) + NOTE_OWNER + desc
    return note + b"\0" * (align4(len(note)) - len(note))


def build_targets_note(names, sig_targets):
    if len(set(names)) != len(names):
        raise SeaError("CRO target names must be unique")
    desc = bytearray(struct.pack("<I", len(names)))
    for name in names:
        try:
            raw = name.encode("ascii")
        except UnicodeEncodeError:
            raise SeaError(f"CRO name {name!r} is not ASCII") from None
        if not raw or b"\0" in raw:
            raise SeaError(f"invalid CRO name {name!r}")
        desc += struct.pack("<I", len(raw)) + raw + b"\0" * (-len(raw) % 4)
    desc += b"".join(struct.pack("<I", target) for target in sig_targets)
    return bytes(desc)


def build_sea(patches, sigs, placement, fixups=(), target_names=(), sig_targets=None):
    normalized = []
    for patch in patches:
        if len(patch) == 3:
            normalized.append((*patch, 0))
        elif len(patch) == 4:
            normalized.append(tuple(patch))
        else:
            raise SeaError("each patch needs an address, replacement, original bytes and target")
    patches = normalized
    check_patches(patches)
    if not sigs or len(sigs) > SEA_SIGNATURES_MAX:
        raise SeaError(f"SEA 1.1 needs 1 to {SEA_SIGNATURES_MAX} signatures")
    if len(placement) != len(patches):
        raise SeaError("every segment needs a placement")
    if sig_targets is None:
        sig_targets = [0] * len(sigs)
    sig_targets = list(sig_targets)
    if len(sig_targets) != len(sigs):
        raise SeaError("every signature needs a target")
    if any(target < 0 or target > len(target_names) for target in sig_targets):
        raise SeaError("a signature names a CRO target that does not exist")
    uses_cro = any(sig_targets)
    if uses_cro != bool(target_names):
        raise SeaError("CRO target names and signature targets disagree")

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
    for patch, (sig, _) in zip(patches, placement):
        if patch[3] != sig_targets[sig]:
            raise SeaError("a segment and its placement signature name different targets")
    place_desc = b"".join(struct.pack("<Ii", sig, off) for sig, off in placement)

    for seg, at, kind, sig, _ in fixups:
        if seg >= len(patches) or sig >= len(sigs) or at % 4 or at + 4 > len(patches[seg][1]):
            raise SeaError("a fix-up names bytes that do not exist")
        source, dest = patches[seg][3], sig_targets[sig]
        if source == 0 and dest != 0:
            raise SeaError("a fix-up goes from code.bin to a CRO")
        if source != 0 and dest not in (0, source):
            raise SeaError("a fix-up goes from one CRO to another")
        if kind not in FIXUP_NAMES:
            raise SeaError(f"unknown fix-up kind {kind}")

    notes = [make_note(NOTE_ORIGINAL, b"".join(original for _, _, original, _ in patches)),
             make_note(NOTE_SIGNATURES, sig_desc),
             make_note(NOTE_PLACEMENT, place_desc)]
    if fixups:
        notes.append(make_note(NOTE_FIXUPS, struct.pack("<I", len(fixups)) + b"".join(
            struct.pack("<IIIIi", *f) for f in fixups)))
    if uses_cro:
        notes.append(make_note(NOTE_FEATURES, struct.pack("<I", FEATURE_CRO_TARGETS)))
        notes.append(make_note(NOTE_TARGETS, build_targets_note(target_names, sig_targets)))

    phnum = len(notes) + len(patches)
    at = EHDR_SIZE + phnum * PHDR_SIZE
    phdrs = []
    body = bytearray()
    for note in notes:
        phdrs.append(struct.pack("<IIIIIIII", PT_NOTE, at, 0, 0, len(note), len(note), PF_R, 4))
        body += note
        at += len(note)
    for vaddr, data, _, _ in patches:
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
        raise SeaError(f"the .sea would be {len(sea)} bytes; SEA 1.1 allows at most {SEA_BYTES_MAX}")
    return sea


def build_plugin(segments, code_bin, branch_fixups=True):
    targets = make_targets(code_bin)
    patches = [(vaddr, data, code_bin[vaddr - BASE:vaddr - BASE + len(data)], 0)
               for vaddr, data in segments]
    patches, sigs, placement, fixups, _, sig_targets = plan(patches, targets, branch_fixups)
    sea = build_sea(patches, sigs, placement, fixups, sig_targets=sig_targets)
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
                               "SEA 1.1 can only write bytes that are in the file")
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


def parse_features(notes, require_known=True):
    desc = notes.get(NOTE_FEATURES, b"")
    if len(desc) % 4:
        raise SeaError("the features note is damaged")
    features = [struct.unpack_from("<I", desc, at)[0] for at in range(0, len(desc), 4)]
    if len(set(features)) != len(features):
        raise SeaError("the features note contains a duplicate feature")
    unknown = [feature for feature in features if feature != FEATURE_CRO_TARGETS]
    if require_known and unknown:
        raise SeaError("unsupported SEA feature " + ", ".join(str(feature) for feature in unknown))
    return features


def parse_targets(notes, num_sigs, features):
    desc = notes.get(NOTE_TARGETS)
    uses_cro = FEATURE_CRO_TARGETS in features
    if not uses_cro:
        if desc is not None:
            raise SeaError("the targets note requires the CRO-targets feature")
        return [], [0] * num_sigs
    if desc is None or len(desc) < 4:
        raise SeaError("the CRO-targets feature needs a targets note")

    count, = struct.unpack_from("<I", desc)
    names, at = [], 4
    for _ in range(count):
        if at + 4 > len(desc):
            raise SeaError("the targets note is damaged")
        length, = struct.unpack_from("<I", desc, at)
        at += 4
        if not length or at + length > len(desc):
            raise SeaError("the targets note is damaged")
        raw = desc[at:at + length]
        if b"\0" in raw:
            raise SeaError("the targets note contains an invalid CRO name")
        try:
            names.append(raw.decode("ascii"))
        except UnicodeDecodeError:
            raise SeaError("the targets note contains a non-ASCII CRO name") from None
        at += align4(length)
    if len(desc) - at != 4 * num_sigs:
        raise SeaError("the targets note does not match the signatures")
    targets = [struct.unpack_from("<I", desc, at + 4 * i)[0] for i in range(num_sigs)]
    if any(target > len(names) for target in targets):
        raise SeaError("a signature names a CRO target that does not exist")
    if not any(targets):
        raise SeaError("the CRO-targets feature is not used")
    return names, targets


def parse_locators(notes, num_segments):
    if NOTE_SIGNATURES not in notes or NOTE_PLACEMENT not in notes:
        raise SeaError("no signatures; this file was made by an older tool, rebuild it with "
                       "convert --code")
    desc = notes[NOTE_SIGNATURES]
    if len(desc) < 4:
        raise SeaError("the signatures note is damaged")
    count, = struct.unpack_from("<I", desc, 0)
    if not 1 <= count <= SEA_SIGNATURES_MAX:
        raise SeaError("the signatures note has an invalid count")
    sigs, at = [], 4
    for _ in range(count):
        if at + 4 > len(desc):
            raise SeaError("the signatures note is damaged")
        n, = struct.unpack_from("<I", desc, at)
        if not 4 <= n <= SIG_BYTES_MAX or n % 4 or at + 4 + 2 * n > len(desc):
            raise SeaError("the signatures note is damaged")
        sigs.append((desc[at + 4:at + 4 + n], desc[at + 4 + n:at + 4 + 2 * n]))
        at += align4(4 + 2 * n)
    if at != len(desc):
        raise SeaError("the signatures note is damaged")
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


def resolve(data, code, cros=()):
    segments, notes = read_elf(data)
    sigs, placement, fixups = parse_locators(notes, len(segments))
    features = parse_features(notes)
    target_names, sig_targets = parse_targets(notes, len(sigs), features)
    supplied = make_targets(code, cros)
    by_name = {target.name: target for target in supplied[1:]}
    targets = [supplied[0]]
    for name in target_names:
        if name not in by_name:
            raise SeaError(f"CRO target {name} was not supplied")
        targets.append(by_name[name])

    matches = []
    for i, ((pattern, mask), target_id) in enumerate(zip(sigs, sig_targets)):
        target = targets[target_id]
        found = aligned_matches(target.code, pattern, mask, base=target.start)
        if not found:
            raise SeaError(f"signature {i} is not found")
        if len(found) > 1:
            raise SeaError(f"signature {i} matches more than once")
        matches.append(found[0])

    placed = []
    segment_targets = []
    for (vaddr, seg), (sig, off) in zip(segments, placement):
        target_id = sig_targets[sig]
        target = targets[target_id]
        addr = matches[sig] + off
        if addr < target.start or addr + len(seg) > target.end:
            raise SeaError(f"segment 0x{vaddr:08X} lands outside {target.label}")
        placed.append([addr, bytearray(seg), vaddr])
        segment_targets.append(target_id)

    for seg, at, kind, sig, off in fixups:
        if seg >= len(placed) or sig >= len(matches) or at % 4 or at + 4 > len(placed[seg][1]):
            raise SeaError("the fix-up note is damaged")
        source_target, dest_target = segment_targets[seg], sig_targets[sig]
        if source_target == 0 and dest_target != 0:
            raise SeaError("a fix-up goes from code.bin to a CRO")
        if source_target != 0 and dest_target not in (0, source_target):
            raise SeaError("a fix-up goes from one CRO to another")
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
    for (addr, body, vaddr), target_id in zip(placed, segment_targets):
        want = original[used:used + len(body)]
        used += len(body)
        target = targets[target_id]
        if target.code[addr - target.start:addr - target.start + len(body)] != want:
            raise SeaError(f"segment 0x{vaddr:08X} lands at 0x{addr:08X}, where the target bytes "
                           "differ from the stored original bytes")
    if used != len(original):
        raise SeaError("the original-bytes note does not match the segments")
    return matches, [(addr, bytes(body), vaddr) for addr, body, vaddr in placed]


def check_resolves_home(sea, code, cros=()):
    segments, _ = read_elf(sea)
    _, placed = resolve(sea, code, cros)
    for (vaddr, seg), (addr, body, _) in zip(segments, placed):
        if addr != vaddr or body != seg:
            raise SeaError(f"segment 0x{vaddr:08X} would land at 0x{addr:08X} in its own code.bin")


def convert(data, code_bin, branch_fixups=True, cros=()):
    segments, notes = read_elf(data)
    if not segments:
        raise SeaError("no PT_LOAD segments, so there is nothing to patch")
    targets = make_targets(code_bin, cros)
    segment_targets = []
    from_code = []
    for vaddr, seg in segments:
        target = target_for_address(targets, vaddr, len(seg))
        if target is None:
            raise SeaError(f"segment at 0x{vaddr:08X} is outside code.bin and every supplied CRO")
        segment_targets.append(target)
        view = targets[target]
        from_code.append(view.code[vaddr - view.start:vaddr - view.start + len(seg)])
    stored = notes.get(NOTE_ORIGINAL)
    if stored is not None:
        if len(stored) != sum(len(seg) for _, seg in segments):
            raise SeaError(f"the original-bytes note holds {len(stored)} bytes, but the segments "
                           f"hold {sum(len(seg) for _, seg in segments)}")
        at = 0
        for (vaddr, seg), theirs in zip(segments, from_code):
            if stored[at:at + len(seg)] != theirs:
                raise SeaError(f"at 0x{vaddr:08X} the ELF's original bytes differ from the "
                               "supplied target; it was made for another game version or region")
            at += len(seg)

    patches, sigs, placement, fixups, report, sig_targets = plan(
        [(vaddr, seg, orig, target)
         for (vaddr, seg), orig, target in zip(segments, from_code, segment_targets)],
        targets, branch_fixups)
    used = sorted(set(sig_targets) - {0})
    remap = {old: new for new, old in enumerate(used, 1)}
    file_targets = [targets[i] for i in used]
    file_sig_targets = [0 if target == 0 else remap[target] for target in sig_targets]
    sea = build_sea(patches, sigs, placement, fixups,
                    [target.name for target in file_targets], file_sig_targets)
    check_resolves_home(sea, code_bin, file_targets)
    return sea, report


def pattern_text(pattern, mask):
    return " ".join(f"{p:02X}" if m == 0xFF else "??" for p, m in zip(pattern, mask))


def describe(data, code=None, cros=()):
    segments, notes = read_elf(data)
    features = parse_features(notes, require_known=False)
    lines = [f"SEA {SEA_VERSION}",
             "features: " + (", ".join("CRO targets" if f == FEATURE_CRO_TARGETS else str(f)
                                        for f in features) if features else "baseline"),
             f"{len(data)} bytes, {len(segments)} segments, "
             f"{sum(len(seg) for _, seg in segments)} bytes patched",
             "original bytes: " + ("stored" if NOTE_ORIGINAL in notes else "missing")]
    try:
        sigs, placement, fixups = parse_locators(notes, len(segments))
        target_names, sig_targets = parse_targets(notes, len(sigs), features)
    except SeaError as e:
        lines.append(str(e))
        for vaddr, seg in sorted(segments):
            lines.append(f"  0x{vaddr:08X}  {len(seg):#x} bytes")
        return lines

    resolved = None
    if code is not None:
        try:
            resolved = resolve(data, code, cros)
        except SeaError as e:
            lines.append(f"does not resolve in this code.bin: {e}")
    lines.append("segments:")
    for i, ((vaddr, seg), (sig, off)) in enumerate(zip(segments, placement)):
        at = f" -> 0x{resolved[1][i][0]:08X}" if resolved else ""
        target = "code.bin" if sig_targets[sig] == 0 else target_names[sig_targets[sig] - 1]
        lines.append(f"  {target}:0x{vaddr:08X}  {len(seg):#x} bytes  "
                     f"signature {sig} {off:+#x}{at}")
    lines.append("signatures:")
    for i, (pattern, mask) in enumerate(sigs):
        at = f"  found at 0x{resolved[0][i]:08X}" if resolved else ""
        target = "code.bin" if sig_targets[i] == 0 else target_names[sig_targets[i] - 1]
        lines.append(f"  {i} [{target}]: {pattern_text(pattern, mask)}{at}")
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
    cros = [parse_cro_arg(spec) for spec in args.cro]
    out = args.output or os.path.splitext(args.input)[0] + ".sea"
    if os.path.abspath(out) == os.path.abspath(args.input):
        raise SeaError("the output would overwrite the input; pass -o")
    sea, report = convert(data, code_bin, not args.no_branch_fixups, cros)
    with open(out, "wb") as f:
        f.write(sea)
    segments, notes = read_elf(sea)
    sigs, _, fixups = parse_locators(notes, len(segments))
    print(f"wrote {out}: SEA {SEA_VERSION}, {len(segments)} segments, {len(sigs)} signatures, "
          f"{len(fixups)} fix-ups, {len(sea)} bytes")
    for line in report:
        print(f"  {line}")

    failed = 0
    for path in args.also:
        other = read_file(path, "code.bin")
        try:
            _, placed = resolve(sea, other, cros)
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
    cros = [parse_cro_arg(spec) for spec in args.cro]
    for line in describe(read_file(args.input, "file"), code, cros):
        print(line)
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="saltysd_sea.py",
        description="Make and inspect SALT Engine Archive (.sea) plugins for SaltySD.",
        epilog=LIMITS_EPILOG)
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("convert", help="turn an ARM ELF into a SEA 1.1 plugin",
                            description="Turn an ARM ELF into a SEA 1.1 plugin.", epilog=LIMITS_EPILOG)
    p.add_argument("input", help="the ELF (or older .sea) to convert")
    p.add_argument("-o", "--output", help="where to write the .sea (default: input name with .sea)")
    p.add_argument("--code", metavar="CODE_BIN", required=True,
                   help="the decompressed code.bin the ELF was made for; signatures are "
                        "chosen so they are unique in it")
    p.add_argument("--also", metavar="CODE_BIN", action="append", default=[],
                   help="another code.bin (region or update layout) the .sea should also work "
                        "on; reports where it lands or why it does not")
    p.add_argument("--cro", metavar="FILE@BASE", action="append", default=[],
                   help="an unfixed CRO0 and the address its file offset was linked at; repeatable")
    p.add_argument("--no-branch-fixups", action="store_true",
                   help="leave B/BL instructions as they are instead of re-aiming them at load")
    p.set_defaults(run=run_convert)

    p = commands.add_parser("info", help="show what a .sea or plugin ELF contains",
                            description="Show what a .sea or plugin ELF contains.")
    p.add_argument("input", help="the file to inspect")
    p.add_argument("--code", metavar="CODE_BIN", help="also show where it lands in this code.bin")
    p.add_argument("--cro", metavar="FILE@BASE", action="append", default=[],
                   help="a CRO target needed to resolve the file; repeatable")
    p.set_defaults(run=run_info)

    args = parser.parse_args(argv)
    try:
        return args.run(args)
    except SeaError as e:
        print(f"error: {args.input}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
