#!/usr/bin/env python3

import os
import random
import shutil
import struct
import subprocess
import tempfile
import unittest

from saltysd_sea import (BASE, FEATURE_CRO_TARGETS, NOTE_FEATURES, NOTE_TARGETS, SEA_VERSION,
                         SeaError, build_plugin, convert, cro_target, describe, main,
                         parse_features, parse_locators, parse_targets, read_elf, resolve, window,
                         write_plugin)

READELF = shutil.which("arm-none-eabi-readelf") or shutil.which(
    "arm-none-eabi-readelf", path=os.path.join(os.environ.get("DEVKITARM", ""), "bin"))

CODE = random.Random(1).randbytes(0x4000)
SEGMENTS = [(BASE + 0x100, b"\x01\x02\x03\x04"), (BASE + 0x40, b"\xAA" * 6)]


def program_headers(data):
    phoff, = struct.unpack_from("<I", data, 28)
    phnum, = struct.unpack_from("<H", data, 44)
    return [struct.unpack_from("<IIIIIIII", data, phoff + i * 32) for i in range(phnum)]


def saltysd_notes(data):
    out = {}
    for p in program_headers(data):
        if p[0] == 4:
            namesz, descsz, kind = struct.unpack_from("<III", data, p[1])
            if data[p[1] + 12:p[1] + 12 + namesz] == b"SaltySD\0":
                out[kind] = data[p[1] + 20:p[1] + 20 + descsz]
    return out


def elf_header(phnum):
    return b"\x7fELF\x01\x01\x01" + b"\0" * 9 + struct.pack(
        "<HHIIIIIHHHHHH", 2, 40, 1, 0, 52, 0, 0x05000400, 52, 32, phnum, 0, 0, 0)


def legacy_elf(segments, code):
    segments = sorted(segments)
    original = b"".join(code[v - BASE:v - BASE + len(d)] for v, d in segments)
    note = struct.pack("<III", 8, len(original), 1) + b"SaltySD\0" + original
    note += b"\0" * (-len(note) % 4)
    phnum = len(segments) + 1
    at = 52 + phnum * 32
    phdrs = [struct.pack("<8I", 4, at, 0, 0, len(note), len(note), 4, 4)]
    body = bytearray(note)
    at += len(note)
    for v, d in segments:
        phdrs.append(struct.pack("<8I", 1, at, v, v, len(d), len(d), 5, 4))
        d += b"\0" * (-len(d) % 4)
        body += d
        at += len(d)
    return elf_header(phnum) + b"".join(phdrs) + bytes(body)


def plain_elf(segments, memsz_extra=0):
    at = 52 + len(segments) * 32
    phdrs, body = [], bytearray()
    for v, d in segments:
        phdrs.append(struct.pack("<8I", 1, at, v, v, len(d), len(d) + memsz_extra, 5, 4))
        body += d
        at += len(d)
    return elf_header(len(segments)) + b"".join(phdrs) + bytes(body)


def loads(data):
    return sorted((v, bytes(d)) for v, d in read_elf(data)[0])


def bl(site, target):
    return struct.pack("<I", 0xEB000000 | (((target - (site + 8)) >> 2) & 0xFFFFFF))


def shifted(code, at, count):
    return code[:at - BASE] + random.Random(2).randbytes(count) + code[at - BASE:]


MODULE_LINK = 0x00900000
MODULE_CODE = 0x180
MODULE_CODE_SIZE = 0x2000


def fake_module(name="module_a", relocations=()):
    data = bytearray(random.Random(7).randbytes(0x4000))
    data[0x80:0x84] = b"CRO0"
    struct.pack_into("<I", data, 0x84, 0x3000)
    struct.pack_into("<II", data, 0xB0, MODULE_CODE, MODULE_CODE_SIZE)
    struct.pack_into("<II", data, 0xC8, 0x3100, 1)
    struct.pack_into("<III", data, 0x3100, MODULE_CODE, MODULE_CODE_SIZE, 0)
    data[0x3000:0x3000 + len(name) + 1] = name.encode("ascii") + b"\0"
    struct.pack_into("<II", data, 0xF8, 0x3200, len(relocations))
    struct.pack_into("<II", data, 0x128, 0x3200 + 12 * len(relocations), 0)
    struct.pack_into("<II", data, 0x130, 0x3200 + 12 * len(relocations), 0)
    for i, file_offset in enumerate(relocations):
        struct.pack_into("<III", data, 0x3200 + 12 * i,
                         ((file_offset - MODULE_CODE) << 4), 0x102, 0)
    return bytes(data)


def module_view(name="module_a", base=MODULE_LINK, relocations=()):
    return cro_target(fake_module(name, relocations), base, name)


def append_feature(data, feature):
    out = bytearray(data)
    headers = program_headers(out)
    feature_index = next(i for i, p in enumerate(headers)
                         if p[0] == 4 and struct.unpack_from("<I", out, p[1] + 8)[0] == NOTE_FEATURES)
    feature_header = headers[feature_index]
    insert_at = feature_header[1] + feature_header[4]
    out[insert_at:insert_at] = struct.pack("<I", feature)
    phoff, = struct.unpack_from("<I", out, 28)
    for i, header in enumerate(headers):
        entry = phoff + i * 32
        if header[1] >= insert_at:
            struct.pack_into("<I", out, entry + 4, header[1] + 4)
    entry = phoff + feature_index * 32
    struct.pack_into("<I", out, entry + 16, feature_header[4] + 4)
    struct.pack_into("<I", out, entry + 20, feature_header[5] + 4)
    struct.pack_into("<I", out, feature_header[1] + 4, 8)
    return bytes(out)


class Layout(unittest.TestCase):
    def setUp(self):
        self.data = build_plugin(SEGMENTS, CODE)

    def test_loads_sorted_at_their_addresses(self):
        loads = [(p[2], p[4]) for p in program_headers(self.data) if p[0] == 1]
        self.assertEqual(loads, [(BASE + 0x40, 6), (BASE + 0x100, 4)])

    def test_segment_bytes_are_the_patch(self):
        for p in program_headers(self.data):
            if p[0] == 1:
                want = dict(SEGMENTS)[p[2]]
                self.assertEqual(self.data[p[1]:p[1] + p[4]], want)

    def test_note_holds_original_bytes(self):
        want = CODE[0x40:0x46] + CODE[0x100:0x104]
        self.assertEqual(saltysd_notes(self.data)[1], want)

    def test_baseline_has_no_features_or_version_note(self):
        notes = [p for p in program_headers(self.data) if p[0] == 4]
        self.assertEqual(len(notes), 3)
        self.assertNotIn(2, saltysd_notes(self.data))
        self.assertNotIn(NOTE_FEATURES, saltysd_notes(self.data))
        self.assertEqual(SEA_VERSION, "1.1")

    def test_resolves_to_its_own_addresses(self):
        _, placed = resolve(self.data, CODE)
        self.assertEqual(sorted((a, b) for a, b, _ in placed), sorted(SEGMENTS))


class Signatures(unittest.TestCase):
    def test_follows_code_that_moved(self):
        sea = build_plugin(SEGMENTS, CODE)
        _, placed = resolve(sea, shifted(CODE, BASE + 0x10, 0x20))
        self.assertEqual(sorted(a for a, _, _ in placed), [BASE + 0x60, BASE + 0x120])

    def test_not_found(self):
        sea = build_plugin(SEGMENTS, CODE)
        other = bytes(b ^ 0x5A for b in CODE)
        with self.assertRaisesRegex(SeaError, "not found"):
            resolve(sea, other)

    def test_ambiguous(self):
        sea = build_plugin(SEGMENTS, CODE)
        with self.assertRaisesRegex(SeaError, "more than once"):
            resolve(sea, CODE + CODE)

    def test_cave_is_placed_after_an_anchor(self):
        code = bytearray(CODE)
        code[0x2000:0x2400] = bytes(0x400)
        code = bytes(code)
        sea = build_plugin([(BASE + 0x2100, b"\x11" * 8)], code)
        _, placed = resolve(sea, shifted(code, BASE + 0x1000, 0x40))
        self.assertEqual(placed[0][0], BASE + 0x2140)

    def test_branch_is_masked_in_signatures(self):
        code = bytearray(CODE)
        for at in range(0x200, 0x240, 4):
            code[at:at + 4] = bl(BASE + at, BASE + 0x3000)
        sea = build_plugin([(BASE + 0x220, b"\x22" * 4)], bytes(code))
        lines = describe(sea)
        self.assertTrue(any("??" in line for line in lines))


class Fixups(unittest.TestCase):
    def setUp(self):
        self.code = bytearray(CODE)
        self.code[0x2000:0x2400] = bytes(0x400)
        self.code = bytes(self.code)
        hook = BASE + 0x100
        cave = BASE + 0x2100
        game = BASE + 0x3000
        self.segments = [(hook, bl(hook, cave)), (cave, bl(cave, game) + b"\x1E\xFF\x2F\xE1")]
        self.sea = build_plugin(self.segments, self.code)

    def test_branches_are_re_aimed(self):
        other = shifted(shifted(self.code, BASE + 0x2800, 0x100), BASE + 0x1000, 0x40)
        _, placed = resolve(self.sea, other)
        by_home = {home: (addr, body) for addr, body, home in placed}
        hook_at, hook = by_home[BASE + 0x100]
        cave_at, cave = by_home[BASE + 0x2100]
        self.assertEqual((hook_at, cave_at), (BASE + 0x100, BASE + 0x2140))
        self.assertEqual(hook, bl(hook_at, cave_at))
        self.assertEqual(cave[:4], bl(cave_at, BASE + 0x3140))

    def test_listed_by_convert(self):
        _, report = convert(legacy_elf(self.segments, self.code), self.code)
        self.assertEqual(len(report), 2)
        self.assertIn("(segment)", report[0])
        self.assertIn("(game)", report[1])

    def test_can_be_turned_off(self):
        sea, report = convert(legacy_elf(self.segments, self.code), self.code, branch_fixups=False)
        self.assertEqual(report, [])
        self.assertNotIn(5, saltysd_notes(sea))


class CroTargets(unittest.TestCase):
    def setUp(self):
        self.module = module_view()
        self.site = MODULE_LINK + 0x400
        self.patch = b"\x00\x00\xA0\xE1"

    def make_sea(self):
        return convert(plain_elf([(self.site, self.patch)]), CODE, cros=[self.module])[0]

    def test_writes_feature_and_target_notes(self):
        sea = self.make_sea()
        _, notes = read_elf(sea)
        sigs, _, _ = parse_locators(notes, 1)
        features = parse_features(notes)
        names, targets = parse_targets(notes, len(sigs), features)
        self.assertEqual(features, [FEATURE_CRO_TARGETS])
        self.assertEqual(names, ["module_a"])
        self.assertTrue(targets and all(target == 1 for target in targets))
        self.assertIn(NOTE_TARGETS, notes)
        self.assertNotIn(2, notes)

    def test_resolves_in_named_cro(self):
        sea = self.make_sea()
        _, placed = resolve(sea, CODE, [self.module])
        self.assertEqual(placed, [(self.site, self.patch, self.site)])
        lines = describe(sea, CODE, [self.module])
        self.assertTrue(any("module_a:0x00900400" in line for line in lines))
        self.assertTrue(any("features: CRO targets" in line for line in lines))

    def test_relocated_words_are_masked_in_signature_windows(self):
        module = module_view(relocations=(0x400,))
        pattern, mask = window(module.code, MODULE_LINK + 0x400, 8,
                               module.start, module.relocations)
        self.assertEqual(pattern[:4], bytes(4))
        self.assertEqual(mask[:4], bytes(4))
        self.assertEqual(mask[4:], b"\xFF" * 4)

    def test_segment_cannot_overlap_relocation(self):
        module = module_view(relocations=(0x400,))
        with self.assertRaisesRegex(SeaError, "relocated CRO word"):
            convert(plain_elf([(self.site, self.patch)]), CODE, cros=[module])

    def test_cro_can_branch_to_main_binary(self):
        sea, report = convert(plain_elf([(self.site, bl(self.site, BASE + 0x100))]),
                              CODE, cros=[self.module])
        self.assertEqual(len(report), 1)
        _, notes = read_elf(sea)
        sigs, _, fixups = parse_locators(notes, 1)
        _, targets = parse_targets(notes, len(sigs), parse_features(notes))
        self.assertEqual(len(fixups), 1)
        self.assertEqual(targets[fixups[0][3]], 0)

    def test_main_binary_cannot_branch_to_cro(self):
        main_site = BASE + 0x100
        data = plain_elf([(main_site, bl(main_site, self.site)), (self.site, self.patch)])
        with self.assertRaisesRegex(SeaError, "code.bin to a CRO"):
            convert(data, CODE, cros=[self.module])

    def test_one_cro_cannot_branch_to_another(self):
        other = module_view("module_b", MODULE_LINK + 0x100000)
        other_site = other.start + 0x300
        data = plain_elf([(self.site, bl(self.site, other_site)),
                          (other_site, b"\x00\x00\xA0\xE1")])
        with self.assertRaisesRegex(SeaError, "one CRO to another"):
            convert(data, CODE, cros=[self.module, other])

    def test_unknown_feature_is_refused(self):
        sea = append_feature(self.make_sea(), 99)
        with self.assertRaisesRegex(SeaError, "unsupported SEA feature 99"):
            resolve(sea, CODE, [self.module])


class Refusals(unittest.TestCase):
    def test_overlap(self):
        with self.assertRaises(ValueError):
            build_plugin([(BASE, b"\0" * 8), (BASE + 4, b"\0" * 4)], CODE)

    def test_past_end(self):
        with self.assertRaises(ValueError):
            build_plugin([(BASE + len(CODE) - 2, b"\0" * 4)], CODE)

    def test_below_base(self):
        with self.assertRaises(ValueError):
            build_plugin([(BASE - 4, b"\0" * 4)], CODE)

    def test_empty(self):
        with self.assertRaises(ValueError):
            build_plugin([(BASE, b"")], CODE)

    def test_no_unique_signature(self):
        with self.assertRaisesRegex(SeaError, "no unique signature"):
            build_plugin([(BASE + 0x2000, b"\1" * 4)], bytes(0x4000))


class Convert(unittest.TestCase):
    def test_legacy_elf_round_trips(self):
        sea, _ = convert(legacy_elf(SEGMENTS, CODE), CODE)
        self.assertEqual(loads(sea), sorted(SEGMENTS))
        self.assertEqual(saltysd_notes(sea)[1], CODE[0x40:0x46] + CODE[0x100:0x104])
        self.assertNotIn(2, saltysd_notes(sea))

    def test_same_bytes_as_building_directly(self):
        self.assertEqual(convert(legacy_elf(SEGMENTS, CODE), CODE)[0], build_plugin(SEGMENTS, CODE))

    def test_sea_converts_to_itself(self):
        sea = build_plugin(SEGMENTS, CODE)
        self.assertEqual(convert(sea, CODE)[0], sea)

    def test_plain_elf_takes_original_bytes_from_code(self):
        self.assertEqual(convert(plain_elf(SEGMENTS), CODE)[0], build_plugin(SEGMENTS, CODE))

    def test_code_bin_disagrees_with_note(self):
        other = bytes(b ^ 0xFF for b in CODE)
        with self.assertRaisesRegex(SeaError, "another game version"):
            convert(legacy_elf(SEGMENTS, CODE), other)

    def test_bss(self):
        with self.assertRaisesRegex(SeaError, "bss"):
            convert(plain_elf(SEGMENTS, memsz_extra=4), CODE)

    def test_too_big(self):
        big = random.Random(3).randbytes(0x40000)
        with self.assertRaisesRegex(SeaError, "at most 65536"):
            convert(plain_elf([(BASE, b"\0" * 0x10000)]), big)

    def test_overlap(self):
        with self.assertRaisesRegex(SeaError, "overlaps"):
            convert(plain_elf([(BASE, b"\0" * 8), (BASE + 4, b"\0" * 4)]), CODE)

    def test_note_does_not_match_segments(self):
        data = bytearray(legacy_elf(SEGMENTS, CODE))
        struct.pack_into("<I", data, 52 + 32 * 2 + 16, 3)
        struct.pack_into("<I", data, 52 + 32 * 2 + 20, 3)
        with self.assertRaisesRegex(SeaError, "original-bytes note"):
            convert(bytes(data), CODE)

    def test_version_note_is_not_required(self):
        sea = build_plugin(SEGMENTS, CODE)
        self.assertNotIn(2, saltysd_notes(sea))
        self.assertEqual(convert(sea, CODE)[0], sea)

    def test_object_file_is_refused(self):
        data = bytearray(plain_elf(SEGMENTS))
        struct.pack_into("<H", data, 16, 1)
        with self.assertRaisesRegex(SeaError, "ET_EXEC"):
            convert(bytes(data), CODE)

    def test_describe(self):
        lines = describe(build_plugin(SEGMENTS, CODE), CODE)
        self.assertEqual(lines[0], "SEA 1.1")
        self.assertTrue(any("0x00100040" in line and "-> 0x00100040" in line for line in lines))

    def test_describe_file_without_signatures(self):
        lines = describe(legacy_elf(SEGMENTS, CODE))
        self.assertTrue(any("older tool" in line for line in lines))


class CommandLine(unittest.TestCase):
    def run_main(self, tmp, name, data, *extra):
        src = os.path.join(tmp, name)
        with open(src, "wb") as f:
            f.write(data)
        code = os.path.join(tmp, "code.bin")
        with open(code, "wb") as f:
            f.write(CODE)
        return src, main(["convert", src, "--code", code, *extra])

    def test_default_output_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, status = self.run_main(tmp, "highpoly.elf", legacy_elf(SEGMENTS, CODE))
            self.assertEqual(status, 0)
            with open(os.path.join(tmp, "highpoly.sea"), "rb") as f:
                self.assertEqual(f.read(), build_plugin(SEGMENTS, CODE))

    def test_also_reports_other_layouts(self):
        with tempfile.TemporaryDirectory() as tmp:
            moved = os.path.join(tmp, "moved.bin")
            with open(moved, "wb") as f:
                f.write(shifted(CODE, BASE + 0x10, 0x20))
            broken = os.path.join(tmp, "broken.bin")
            with open(broken, "wb") as f:
                f.write(bytes(b ^ 1 for b in CODE))
            _, status = self.run_main(tmp, "p.elf", legacy_elf(SEGMENTS, CODE), "--also", moved)
            self.assertEqual(status, 0)
            _, status = self.run_main(tmp, "p.elf", legacy_elf(SEGMENTS, CODE), "--also", broken)
            self.assertEqual(status, 3)

    def test_cro_argument(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "module_a.elf")
            site = MODULE_LINK + 0x400
            with open(src, "wb") as f:
                f.write(plain_elf([(site, b"\x00\x00\xA0\xE1")]))
            code = os.path.join(tmp, "code.bin")
            with open(code, "wb") as f:
                f.write(CODE)
            cro = os.path.join(tmp, "module_a.cro")
            with open(cro, "wb") as f:
                f.write(fake_module())
            self.assertEqual(main(["convert", src, "--code", code,
                                   "--cro", f"{cro}@0x{MODULE_LINK:X}"]), 0)
            with open(os.path.join(tmp, "module_a.sea"), "rb") as f:
                _, notes = read_elf(f.read())
            self.assertIn(NOTE_FEATURES, notes)
            self.assertIn(NOTE_TARGETS, notes)

    def test_error_is_a_message_not_a_traceback(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = bytearray(plain_elf(SEGMENTS))
            struct.pack_into("<H", data, 16, 1)
            _, status = self.run_main(tmp, "bad.elf", bytes(data))
            self.assertEqual(status, 1)
            self.assertFalse(os.path.exists(os.path.join(tmp, "bad.sea")))


@unittest.skipUnless(READELF, "no readelf")
class Readelf(unittest.TestCase):
    def test_standard_tools_read_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "sample.sea")
            write_plugin(path, SEGMENTS, CODE)
            out = subprocess.run([READELF, "-h", "-l", "-n", "-W", path],
                                 capture_output=True, text=True, check=True).stdout
        self.assertIn("EXEC (Executable file)", out)
        self.assertIn("ARM", out)
        self.assertIn("0x00100040", out)
        self.assertIn("0x00100100", out)
        self.assertIn("SaltySD", out)


if __name__ == "__main__":
    unittest.main()
