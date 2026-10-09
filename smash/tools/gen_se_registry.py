#!/usr/bin/env python3

import argparse
import hashlib
from pathlib import Path
import re
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[1]
LOCAL_DEPS = ROOT / "sdk" / ".deps"
if LOCAL_DEPS.is_dir():
    sys.path.insert(0, str(LOCAL_DEPS))
sys.path.insert(0, str(ROOT / "sdk"))

from se_sdk.arm import ArmDecodeError, disassemble


BASE = 0x100000
KINDS = {
    "function_arm": "SE_TARGET_FUNCTION_ARM",
    "function_thumb": "SE_TARGET_FUNCTION_THUMB",
    "pointer_slot": "SE_TARGET_POINTER_SLOT",
    "table": "SE_TARGET_TABLE",
    "data": "SE_TARGET_DATA",
    "patch_site": "SE_TARGET_PATCH_SITE",
}
REGIONS = {"usa": 1, "eur": 2, "jpn": 3}


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


def hex_bytes(value, field):
    if not isinstance(value, str):
        raise SystemExit(f"{field} must be hexadecimal text")
    compact = re.sub(r"\s+", "", value)
    try:
        result = bytes.fromhex(compact)
    except ValueError as exc:
        raise SystemExit(f"{field} is not valid hexadecimal: {exc}") from exc
    if not result:
        raise SystemExit(f"{field} cannot be empty")
    return result


def target_build_ids(target, index):
    has_build = "build" in target
    has_builds = "builds" in target
    if has_build == has_builds:
        raise SystemExit(f"target {index} must define exactly one of build or builds")
    values = target["builds"] if has_builds else [target["build"]]
    if not isinstance(values, list) or not values:
        raise SystemExit(f"target {index} builds must be a non-empty list")
    result = [integer(value, f"target {index} build") for value in values]
    if len(result) != len(set(result)):
        raise SystemExit(f"target {index} names a build more than once")
    return result


def aligned_matches(image, pattern, mask):
    if all(byte == 0xFF for byte in mask):
        matches = []
        at = image.find(pattern)
        while at >= 0:
            if not (at & 3):
                matches.append(at + BASE)
            at = image.find(pattern, at + 1)
        return matches
    matches = []
    for offset in range(0, len(image) - len(pattern) + 1, 4):
        if all(not ((image[offset + i] ^ pattern[i]) & mask[i])
                   for i in range(len(pattern))):
            matches.append(offset + BASE)
    return matches


def c_bytes(value, size=None):
    data = list(value)
    if size is not None:
        data += [0] * (size - len(data))
    return "{ " + ", ".join(f"0x{byte:02X}" for byte in data) + " }"


def write_if_changed(path, text):
    encoded = text.encode("utf-8")
    if path.exists() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True)
    parser.add_argument("--title-id", required=True)
    parser.add_argument("--code", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    registry_path = Path(args.registry)
    registry = tomllib.loads(registry_path.read_text(encoding="utf-8"))
    image = Path(args.code).read_bytes()
    digest = hashlib.sha256(image).hexdigest().upper()
    title_id = integer(args.title_id, "title ID")
    builds = [item for item in registry.get("builds", [])
              if integer(item.get("title_id"), "build title_id") == title_id and
              item.get("code_sha256", "").upper() == digest]
    if len(builds) != 1:
        raise SystemExit(f"registry selected {len(builds)} builds for title 0x{title_id:08X} "
                         f"and SHA-256 {digest}")
    build = builds[0]
    build_id = integer(build.get("id"), "build id")
    game = build.get("game_version", "").split(".")
    if len(game) != 2 or any(not part.isdigit() for part in game):
        raise SystemExit("build game_version must contain major.minor")
    region = REGIONS.get(build.get("region"))
    if region is None:
        raise SystemExit("build region is unsupported")

    targets = [item for index, item in enumerate(registry.get("targets", []))
               if build_id in target_build_ids(item, index)]
    if not targets:
        raise SystemExit("selected build has no targets")
    seen_ids = set()
    seen_names = set()
    rows = []
    for index, target in enumerate(targets):
        target_id = integer(target.get("id"), f"target {index} id")
        name = target.get("name")
        if target_id in seen_ids or name in seen_names:
            raise SystemExit(f"duplicate target ID or name at target {index}")
        seen_ids.add(target_id)
        seen_names.add(name)
        if target.get("kind") not in KINDS:
            raise SystemExit(f"target {name} has unsupported kind")
        address = integer(target.get("address"), f"target {name} address")
        span = integer(target.get("span"), f"target {name} span")
        expected = hex_bytes(target.get("expected"), f"target {name} expected")
        signature = hex_bytes(target.get("signature"), f"target {name} signature")
        mask = hex_bytes(target.get("mask"), f"target {name} mask")
        if len(signature) != len(mask) or len(signature) % 4:
            raise SystemExit(f"target {name} signature and mask must have equal aligned sizes")
        offset = address - BASE
        if offset < 0 or span == 0 or span > len(image) - offset or len(expected) > span:
            raise SystemExit(f"target {name} is outside code.bin")
        if image[offset:offset + len(expected)] != expected:
            raise SystemExit(f"target {name} expected bytes differ at 0x{address:08X}")
        mode = target.get("mode")
        if mode in ("arm", "thumb"):
            try:
                disassemble(expected, address, mode)
            except ArmDecodeError as exc:
                raise SystemExit(f"target {name} has invalid {mode} expected bytes: {exc}") from exc
        matches = aligned_matches(image, signature, mask)
        if matches != [address]:
            raise SystemExit(f"target {name} signature resolves at " +
                             ", ".join(f"0x{match:08X}" for match in matches))
        rows.append((target_id, address, span, KINDS[target["kind"]], name, expected))

    sha = bytes.fromhex(digest)
    lines = ["/* Generated by tools/gen_se_registry.py; do not edit. */",
             "#ifndef SALTYSD_SE_TARGETS_GENERATED_H",
             "#define SALTYSD_SE_TARGETS_GENERATED_H", "",
             f"#define SE_RUNTIME_REGISTRY_REVISION {integer(registry.get('registry_revision'), 'registry revision')}u",
             f"#define SE_RUNTIME_BUILD_ID 0x{build_id:08X}u",
             f"#define SE_RUNTIME_TITLE_ID 0x{title_id:08X}u",
             f"#define SE_RUNTIME_GAME_MAJOR {int(game[0])}u",
             f"#define SE_RUNTIME_GAME_MINOR {int(game[1])}u",
             f"#define SE_RUNTIME_REGION {region}u",
             f"static const se_u8 se_runtime_code_sha256[32] = {c_bytes(sha)};", "",
             "static const se_registry_target se_registry_targets[] = {"]
    for target_id, address, span, kind, name, expected in sorted(rows):
        lines.append(f"    {{ 0x{target_id:08X}u, 0x{address:08X}u, 0x{span:X}u, {kind}, "
                     f"{len(expected)}u, {c_bytes(expected, 16)}, \"{name}\" }},")
    lines += ["};", "", "#endif", ""]
    write_if_changed(Path(args.out), "\n".join(lines))
    print(f"registry build 0x{build_id:08X}: {len(rows)} targets verified against {digest}")


if __name__ == "__main__":
    main()
