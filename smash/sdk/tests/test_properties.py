import unittest

import saltysd_sea  # dependency path setup
from hypothesis import given, settings, strategies as st

from se_sdk.format import parse_package_note
from se_sdk.manifest import ManifestError


PROPERTY_SETTINGS = settings(max_examples=200, derandomize=True, database=None, deadline=None)


class ParserProperties(unittest.TestCase):
    @PROPERTY_SETTINGS
    @given(st.binary(max_size=512))
    def test_package_note_rejects_arbitrary_bytes_cleanly(self, data):
        try:
            parse_package_note(data)
        except ManifestError:
            pass

    @PROPERTY_SETTINGS
    @given(st.binary(max_size=512))
    def test_elf_reader_rejects_arbitrary_bytes_cleanly(self, data):
        try:
            saltysd_sea.read_elf(data)
        except saltysd_sea.SeaError:
            pass


if __name__ == "__main__":
    unittest.main()
