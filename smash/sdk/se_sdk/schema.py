from pathlib import Path
import re


_SOURCE_INCLUDE = Path(__file__).resolve().parents[1] / "include" / "se"
_WHEEL_INCLUDE = Path(__file__).resolve().parent / "include" / "se"
INCLUDE = _SOURCE_INCLUDE if _SOURCE_INCLUDE.is_dir() else _WHEEL_INCLUDE
HEADERS = [INCLUDE / "package.h", INCLUDE / "hooks.h"]


def _defines():
    found = {}
    for header in HEADERS:
        text = header.read_text(encoding="utf-8")
        for name, value in re.findall(
                r"^#define\s+(SE_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)u?",
                text, re.MULTILINE):
            found[name] = int(value, 0)
    return found


DEFINES = _defines()
SE_PACKAGE_MAGIC = DEFINES["SE_PACKAGE_MAGIC"]
SE_PACKAGE_VERSION = DEFINES["SE_PACKAGE_VERSION"]
SE_NOTE_PACKAGE = DEFINES["SE_NOTE_PACKAGE"]
SE_RELATION_OPTIONAL = DEFINES["SE_RELATION_OPTIONAL"]
SE_ORDER_BEFORE = DEFINES["SE_ORDER_BEFORE"]
SE_ORDER_AFTER = DEFINES["SE_ORDER_AFTER"]
SE_IMPORT_ABS32 = DEFINES["SE_IMPORT_ABS32"]
SE_IMPORT_ARM_VENEER = DEFINES["SE_IMPORT_ARM_VENEER"]
SE_HOOK_EXCLUSIVE = DEFINES["SE_HOOK_EXCLUSIVE"]
SE_HOOK_CHAINABLE = DEFINES["SE_HOOK_CHAINABLE"]

REGIONS = {"usa": 1, "eur": 2, "jpn": 3}
CAPABILITIES = {
    "log": 1 << 0,
    "build_identity": 1 << 1,
    "targets": 1 << 2,
    "alloc": 1 << 3,
    "files": 1 << 4,
    "diagnostics": 1 << 5,
    "lifecycle": 1 << 6,
    "hooks": 1 << 7,
    "resident_modules": 1 << 8,
}
