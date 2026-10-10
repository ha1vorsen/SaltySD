#!/usr/bin/env python3

import argparse
import hashlib
from pathlib import Path
import tomllib


def integer(value, field):
    if isinstance(value, int):
        result = value
    elif isinstance(value, str):
        result = int(value, 0)
    else:
        raise SystemExit(f"{field} must be an integer")
    if not 0 <= result <= 0xFFFFFFFF:
        raise SystemExit(f"{field} is outside u32")
    return result


def select_build(registry, digest):
    matches = [build for build in registry.get("builds", [])
               if build.get("code_sha256", "").upper() == digest.upper()]
    if len(matches) != 1:
        raise SystemExit(f"registry selected {len(matches)} builds for SHA-256 {digest.upper()}")
    build = matches[0]
    title_id = integer(build.get("title_id"), "build title_id")
    values = build.get("legacy_title_ids")
    if not isinstance(values, list) or not values:
        raise SystemExit("build legacy_title_ids must be a non-empty list")
    legacy_title_ids = [integer(value, "build legacy title ID") for value in values]
    if legacy_title_ids[0] != title_id:
        raise SystemExit("build legacy_title_ids must start with its title_id")
    if len(legacy_title_ids) != len(set(legacy_title_ids)):
        raise SystemExit("build legacy_title_ids contains a duplicate")
    return title_id, legacy_title_ids


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True)
    parser.add_argument("--code", required=True)
    args = parser.parse_args()
    registry = tomllib.loads(Path(args.registry).read_text(encoding="utf-8"))
    code_path = Path(args.code)
    if not code_path.is_file():
        raise SystemExit(f"CODE does not name a file: {args.code}")
    image = code_path.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    title_id, legacy_title_ids = select_build(registry, digest)
    print(" ".join(f"{value:08X}" for value in legacy_title_ids))


if __name__ == "__main__":
    main()
