import sys
from pathlib import Path

SDK = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SDK))
import saltysd_sea


PATCHES = {
    0x00A2B800,
    0x00C2D214,
    0x00C2D26C,
    0x00C2D278,
    0x00C2D280,
    0x00C2D288,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} SEA CODE_BIN")
    sea_path, code_path = map(Path, sys.argv[1:])
    sea = sea_path.read_bytes()
    segments, _ = saltysd_sea.read_elf(sea)
    actual = {address for address, _ in segments}
    if actual != PATCHES:
        raise SystemExit(f"unexpected patch addresses: {sorted(actual)!r}")
    saltysd_sea.resolve(sea, code_path.read_bytes())
    print(f"layout resolves: {len(segments)} segments, {sum(len(data) for _, data in segments)} bytes")


if __name__ == "__main__":
    main()
