import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import saltysd_sea


PATCH_ADDRESSES = {
    0x0020DA64, 0x0020DB3C, 0x00230488, 0x002412D0, 0x00241478,
    0x00247C28, 0x002481B4, 0x005000BC, 0x0095912C, 0x00B6110C,
    0x00B612A0, 0x00B615A0, 0x00B61600, 0x00B61680, 0x00B61700,
    0x00B61800, 0x00B61880, 0x00B61A80, 0x00B61B00,
}


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} SEA_PATH CODE_PATH")
    sea_path, code_path = map(Path, sys.argv[1:])
    segments, _ = saltysd_sea.read_elf(sea_path.read_bytes())
    actual = {address for address, _ in segments}
    if actual != PATCH_ADDRESSES:
        raise SystemExit(f"unexpected patch addresses: {sorted(actual)!r}")
    saltysd_sea.resolve(sea_path.read_bytes(), code_path.read_bytes())
    print(f"layout: {len(segments)} segments, {sum(len(data) for _, data in segments)} patched bytes")


if __name__ == "__main__":
    main()
