import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

import _se_build_backend as backend


class BuildBackendTests(unittest.TestCase):
    def test_wheel_is_reproducible_and_self_contained(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            one = Path(first, backend.build_wheel(first)).read_bytes()
            two = Path(second, backend.build_wheel(second)).read_bytes()
            self.assertEqual(hashlib.sha256(one).digest(), hashlib.sha256(two).digest())
            with zipfile.ZipFile(Path(first, backend.WHEEL_NAME)) as wheel:
                names = set(wheel.namelist())
                self.assertIn("saltysd_sea.py", names)
                self.assertIn("se_sdk/schema.py", names)
                self.assertIn("se_sdk/include/se/package.h", names)
                self.assertIn(f"{backend.DIST_INFO}/RECORD", names)
                metadata = wheel.read(f"{backend.DIST_INFO}/METADATA").decode()
                self.assertIn("Requires-Dist: capstone==5.0.9", metadata)
                self.assertIn("Requires-Dist: pyelftools==0.33", metadata)

    def test_sdist_is_reproducible(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            one = Path(first, backend.build_sdist(first)).read_bytes()
            two = Path(second, backend.build_sdist(second)).read_bytes()
            self.assertEqual(hashlib.sha256(one).digest(), hashlib.sha256(two).digest())


if __name__ == "__main__":
    unittest.main()
