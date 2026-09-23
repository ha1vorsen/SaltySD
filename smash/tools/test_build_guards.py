#!/usr/bin/env python3

import struct
import unittest

from arm_stack import bytes_reader, verify_routine
from verify_fighter_data_fallback import verify_dispatch
from verify_menu_hook import MENU_FUNCTION, TRAMPOLINE, verify_hook


class ArmStackTests(unittest.TestCase):
    def verify(self, words):
        image = struct.pack("<%dI" % len(words), *words)
        return verify_routine(bytes_reader(image, 0x1000), 0x1000,
                              0x1000 + len(image), "synthetic", balanced=True)

    def test_aligned_call_passes(self):
        self.verify((0xE92D4010, 0xEB000000, 0xE8BD8010))

    def test_four_byte_call_misalignment_fails(self):
        with self.assertRaisesRegex(ValueError, "sp alignment 4"):
            self.verify((0xE92D4000, 0xEB000000, 0xE8BD8000))


class MenuHookTests(unittest.TestCase):
    BASE = 0x1000
    SITE = 0x1040
    TARGET = 0x1100

    def images(self):
        pristine = bytearray(0x200)
        patched = bytearray(pristine)
        function_start = self.SITE - self.BASE - 8
        pristine[function_start:function_start + len(MENU_FUNCTION)] = MENU_FUNCTION
        patched[:] = pristine
        displacement = (self.TARGET - (self.SITE + 8)) >> 2
        struct.pack_into("<I", patched, self.SITE - self.BASE,
                         0xEA000000 | (displacement & 0xFFFFFF))
        patched[self.TARGET - self.BASE:self.TARGET - self.BASE + 8] = TRAMPOLINE
        return pristine, patched

    def test_exact_one_word_hook_passes(self):
        verify_hook(*self.images(), self.SITE, self.TARGET, self.BASE)

    def test_neighbor_write_fails(self):
        pristine, patched = self.images()
        patched[self.SITE - self.BASE + 4] ^= 1
        with self.assertRaisesRegex(ValueError, "outside its single"):
            verify_hook(pristine, patched, self.SITE, self.TARGET, self.BASE)

    def test_wrong_target_fails(self):
        pristine, patched = self.images()
        struct.pack_into("<I", patched, self.SITE - self.BASE, 0xEA000000)
        with self.assertRaisesRegex(ValueError, "expected island trampoline"):
            verify_hook(pristine, patched, self.SITE, self.TARGET, self.BASE)

    def test_wrong_displaced_instruction_fails(self):
        pristine, patched = self.images()
        struct.pack_into("<I", pristine, self.SITE - self.BASE, 0xE1A00000)
        with self.assertRaisesRegex(ValueError, "audited 1.1.7 function"):
            verify_hook(pristine, patched, self.SITE, self.TARGET, self.BASE)

    def test_corrupt_trampoline_fails(self):
        pristine, patched = self.images()
        patched[self.TARGET - self.BASE] ^= 1
        with self.assertRaisesRegex(ValueError, "not intact"):
            verify_hook(pristine, patched, self.SITE, self.TARGET, self.BASE)


class FighterDataFallbackTests(unittest.TestCase):
    BASE = 0x1000
    START = 0x1040
    CASES = START + 0x110
    DEFAULT = START + 0x318
    LOOKUP = 0x10A0
    FALLBACK = 0x10C0

    @staticmethod
    def branch(site, target, condition):
        displacement = target - (site + 8)
        return (condition << 28) | 0x0A000000 | ((displacement >> 2) & 0xFFFFFF)

    def images(self):
        pristine = bytearray(0x500)
        struct.pack_into("<III", pristine, self.START - self.BASE,
                         0xE3510041, 0x379FF101,
                         self.branch(self.START + 8, self.DEFAULT, 0xE))
        for fighter_id in range(0x41):
            struct.pack_into("<I", pristine,
                             self.START - self.BASE + 0xC + fighter_id * 4,
                             self.CASES + fighter_id * 8)
        patched = bytearray(pristine)
        struct.pack_into("<II", patched, self.START - self.BASE,
                         0xE3510041,
                         self.branch(self.START + 4, self.DEFAULT, 0x2))
        struct.pack_into("<II", patched, self.LOOKUP - self.BASE,
                         0xE3530000,
                         self.branch(self.LOOKUP + 4, self.FALLBACK, 0x0))
        struct.pack_into("<6I", patched, self.FALLBACK - self.BASE,
                         0xE28DD020, 0xE8BD43FE, 0xE59FC004,
                         0xE08CC181, 0xE12FFF1C, self.CASES)
        return pristine, patched

    def verify(self, images):
        verify_dispatch(*images, self.START, self.CASES, self.DEFAULT,
                        self.FALLBACK, self.LOOKUP, self.BASE)

    def test_stock_fallbacks_pass(self):
        self.verify(self.images())

    def test_wrong_default_target_fails(self):
        pristine, patched = self.images()
        struct.pack_into("<I", patched, self.START - self.BASE + 4, 0x2A000000)
        with self.assertRaisesRegex(ValueError, "stock default"):
            self.verify((pristine, patched))

    def test_missing_export_fault_sentinel_fails(self):
        pristine, patched = self.images()
        struct.pack_into("<I", patched, self.START - self.BASE + 0xE0, 0x01234567)
        with self.assertRaisesRegex(ValueError, "deliberate"):
            self.verify((pristine, patched))

    def test_modified_stock_case_stub_fails(self):
        pristine, patched = self.images()
        patched[self.CASES - self.BASE] ^= 1
        with self.assertRaisesRegex(ValueError, "case stubs"):
            self.verify((pristine, patched))


if __name__ == "__main__":
    unittest.main()
