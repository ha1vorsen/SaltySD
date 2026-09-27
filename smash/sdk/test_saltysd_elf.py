#!/usr/bin/env python3

import os
import shutil
import struct
import subprocess
import tempfile
import unittest

from saltysd_elf import BASE, build_plugin, write_plugin

READELF = shutil.which("arm-none-eabi-readelf") or shutil.which(
    "arm-none-eabi-readelf", path=os.path.join(os.environ.get("DEVKITARM", ""), "bin"))

CODE = bytes(range(256)) * 64
SEGMENTS = [(BASE + 0x100, b"\x01\x02\x03\x04"), (BASE + 0x40, b"\xAA" * 6)]


def program_headers(data):
    phoff, = struct.unpack_from("<I", data, 28)
    phnum, = struct.unpack_from("<H", data, 44)
    return [struct.unpack_from("<IIIIIIII", data, phoff + i * 32) for i in range(phnum)]


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
        notes = [p for p in program_headers(self.data) if p[0] == 4]
        self.assertEqual(len(notes), 1)
        off = notes[0][1]
        namesz, descsz, kind = struct.unpack_from("<III", self.data, off)
        self.assertEqual((namesz, kind), (8, 1))
        self.assertEqual(self.data[off + 12:off + 20], b"SaltySD\0")
        want = CODE[0x40:0x46] + CODE[0x100:0x104]
        self.assertEqual(self.data[off + 20:off + 20 + descsz], want)


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


@unittest.skipUnless(READELF, "no readelf")
class Readelf(unittest.TestCase):
    def test_standard_tools_read_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "sample.elf")
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
