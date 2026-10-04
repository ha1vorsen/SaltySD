from pathlib import Path
import subprocess
import sys

SDK = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SDK))
import saltysd_sea
from se_sdk.arm import disassemble
from se_sdk.format import parse_package_note
from se_sdk.schema import SE_NOTE_PACKAGE


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: verify.py PACKAGE.sea PAYLOAD.elf OBJDUMP")
    sea_path, elf_path = map(Path, sys.argv[1:3])
    segments, notes = saltysd_sea.read_elf(sea_path.read_bytes())
    if [address for address, _ in segments] != [0x00A2B800, 0x00A2B900]:
        raise SystemExit("generated named-target placement changed unexpectedly")
    metadata = parse_package_note(notes[SE_NOTE_PACKAGE])
    if metadata["id"] != "org.saltysd.examples.no-hitboxes":
        raise SystemExit("wrong package identity")
    symbols = subprocess.check_output([sys.argv[3], "-t", elf_path], text=True)
    if " no_hitboxes\n" not in symbols or " se_plugin_init\n" not in symbols:
        raise SystemExit("C entry symbols are missing")
    handler = disassemble(segments[1][1][:4], segments[1][0])
    if len(segments[1][1]) > 0x5C or len(handler) != 1 or (handler[0].mnemonic, handler[0].operands) != ("bx", "lr"):
        raise SystemExit("no_hitboxes is no longer the verified ARM 'bx lr' handler")
    print("verified C source symbols, named-target layout, hook declaration, and ARM return handler")


if __name__ == "__main__":
    main()
