from pathlib import Path
import subprocess
import sys


SDK = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SDK))

import saltysd_sea
from se_sdk.arm import disassemble
from se_sdk.format import parse_package_note
from se_sdk.schema import SE_NOTE_PACKAGE


HOOK = 0x0017C230
CAVE = 0x00B6110C
PATH_STR = 0x00181854
DATA_SIZE = 0x0016F0D0
CONTINUE = 0x0017C234


def branch_target(instruction):
    if not instruction.operands.startswith("#0x"):
        raise SystemExit(f"instruction has no immediate target: {instruction}")
    return int(instruction.operands[1:], 16)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: verify.py PACKAGE.sea PAYLOAD.elf CODE.bin OBJDUMP")
    sea_path, elf_path, code_path = map(Path, sys.argv[1:4])
    objdump = sys.argv[4]

    segments, notes = saltysd_sea.read_elf(sea_path.read_bytes())
    if [address for address, _ in segments] != [HOOK, CAVE]:
        raise SystemExit("generated hook/cave layout changed")
    metadata = parse_package_note(notes[SE_NOTE_PACKAGE])
    if metadata["id"] != "org.saltysd.examples.skip-c08-h08-textures":
        raise SystemExit("wrong package identity")

    hook = disassemble(segments[0][1], HOOK)
    if len(hook) != 1 or hook[0].mnemonic != "b" or branch_target(hook[0]) != CAVE:
        raise SystemExit("BCH loader no longer branches to the filter cave")
    cave = disassemble(segments[1][1], CAVE)
    targets = {(item.mnemonic, branch_target(item)) for item in cave
               if item.mnemonic in ("b", "bl") and item.operands.startswith("#0x")}
    if (("bl", PATH_STR) not in targets or ("bl", DATA_SIZE) not in targets or
            ("b", CONTINUE) not in targets):
        raise SystemExit("filter cave lost a Resource call or the retail continuation")
    if len(segments[1][1]) > 0xef4:
        raise SystemExit("filter cave exceeds its audited range")

    code = code_path.read_bytes()
    if code[HOOK - saltysd_sea.BASE:HOOK - saltysd_sea.BASE + 4] != bytes.fromhex("00 40 A0 E1"):
        raise SystemExit("BCH loader hook differs from the audited ARM mov r4, r0")
    if any(code[CAVE - saltysd_sea.BASE:CAVE - saltysd_sea.BASE + len(segments[1][1])]):
        raise SystemExit("filter cave is not zero-filled in the selected executable")

    symbols = subprocess.check_output([objdump, "-t", elf_path], text=True)
    for name in ("bch_texture_object_patch", "bch_texture_object_hook",
                 "suppress_c08_h08_bch_textures"):
        if f" {name}\n" not in symbols:
            raise SystemExit(f"missing source symbol {name}")
    print("verified SEA 2 BCH texture-object hook, retail continuation, cave, and source symbols")


if __name__ == "__main__":
    main()
