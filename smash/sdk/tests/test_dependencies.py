import unittest
from importlib.metadata import version

import saltysd_sea  # dependency path setup
import capstone
import elftools

from se_sdk.arm import ArmDecodeError, disassemble


class DependencyIntegrationTests(unittest.TestCase):
    def test_dependency_versions_are_locked(self):
        self.assertEqual(version("capstone"), "5.0.9")
        self.assertEqual(elftools.__version__, "0.33")

    def test_capstone_decodes_arm_return(self):
        decoded = disassemble(b"\x1e\xff\x2f\xe1", 0x00A2B900)
        self.assertEqual([(item.mnemonic, item.operands) for item in decoded], [("bx", "lr")])

    def test_partial_arm_instruction_is_rejected(self):
        with self.assertRaises(ArmDecodeError):
            disassemble(b"\x1e\xff\x2f", 0x00A2B900)


if __name__ == "__main__":
    unittest.main()
