import tempfile
from pathlib import Path
import unittest

import saltysd_sea
from se_sdk import schema
from se_sdk.format import build_package_note, parse_package_note, repack_sea_v2

from .test_saltysd_sea import CODE, SEGMENTS


class Sea2Format(unittest.TestCase):
    def test_shared_payload_package_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "code.bin").write_bytes(CODE)
            manifest = {
                "_root": root,
                "package": {
                    "id": "org.saltysd.test",
                    "display_name": "Test package",
                    "author": "SALT team",
                    "version": "2.3.4",
                    "abi_min": "1.0",
                    "abi_max": "1.2",
                    "capabilities": ["log", "targets"],
                },
                "payload": {},
                "builds": [{
                    "id": 0x1170001,
                    "title_id": 0x000EDF00,
                    "game_version": "1.1",
                    "region": "usa",
                    "code": "code.bin",
                    "variant": 1,
                }],
                "variants": [{"id": 1}],
            }
            sea12 = saltysd_sea.build_plugin(SEGMENTS, CODE)
            segments, notes = saltysd_sea.read_elf(sea12)
            signatures, _, _ = saltysd_sea.parse_locators(notes, len(segments))
            package = build_package_note(manifest, len(segments), len(signatures))
            sea20 = repack_sea_v2(saltysd_sea, sea12, package)
            _, notes = saltysd_sea.read_elf(sea20)
            self.assertEqual(saltysd_sea.parse_sea_version(notes), "2.0")
            metadata = parse_package_note(notes[schema.SE_NOTE_PACKAGE])
            self.assertEqual(metadata["id"], "org.saltysd.test")
            self.assertEqual(metadata["version"], "2.3.4")
            self.assertEqual(metadata["builds"][0]["title_id"], 0x000EDF00)
            saltysd_sea.resolve(sea20, CODE)


if __name__ == "__main__":
    unittest.main()
