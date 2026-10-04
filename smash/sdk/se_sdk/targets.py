from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import tomllib


class TargetError(ValueError):
    pass


_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def _load(pathname):
    source = Path(pathname).resolve()
    try:
        with source.open("rb") as stream:
            return tomllib.load(stream), source
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise TargetError(f"cannot read {source}: {exc}") from exc


def generate_linker(registry_path, layout_path, code_path, build_id):
    registry, _ = _load(registry_path)
    layout, _ = _load(layout_path)
    code_source = Path(code_path).resolve()
    try:
        code = code_source.read_bytes()
    except OSError as exc:
        raise TargetError(f"cannot read {code_source}: {exc}") from exc
    digest = hashlib.sha256(code).hexdigest().upper()
    builds = [item for item in registry.get("builds", [])
              if int(item.get("id", -1)) == build_id]
    if len(builds) != 1 or builds[0].get("code_sha256", "").upper() != digest:
        raise TargetError(f"code.bin does not select registry build {build_id:#010x}")
    targets = {item.get("name"): item for item in registry.get("targets", [])
               if int(item.get("build", -1)) == build_id}
    segments = layout.get("segments")
    if not isinstance(segments, list) or not segments:
        raise TargetError("layout needs at least one [[segments]] table")

    resolved = []
    ranges = []
    for index, segment in enumerate(segments):
        name = segment.get("name")
        section = segment.get("section")
        target_name = segment.get("target")
        if not isinstance(name, str) or not _NAME.fullmatch(name):
            raise TargetError(f"segments[{index}].name is not a linker identifier")
        if not isinstance(section, str) or not section.startswith("."):
            raise TargetError(f"segments[{index}].section must start with '.'")
        if target_name not in targets:
            raise TargetError(f"segments[{index}] names unknown target {target_name!r}")
        target = targets[target_name]
        offset = int(segment.get("offset", 0))
        maximum = int(segment.get("max_size", 0))
        span = int(target["span"])
        if offset < 0 or maximum <= 0 or offset > span or maximum > span - offset:
            raise TargetError(f"segments[{index}] is outside {target_name}'s span")
        address = int(target["address"]) + offset
        end = address + maximum
        if any(address < other_end and other_start < end for other_start, other_end in ranges):
            raise TargetError(f"segments[{index}] overlaps another generated segment")
        ranges.append((address, end))
        resolved.append((name, section, address, maximum))

    phdrs = " ".join(f"{name} PT_LOAD;" for name, _, _, _ in resolved)
    lines = ["/* Generated from the named-target registry; do not hand-edit. */",
             f"PHDRS {{ {phdrs} }}", "", "SECTIONS", "{"]
    for name, section, address, maximum in resolved:
        lines.append(f"    {section} 0x{address:08X} : {{ KEEP(*({section})) }} :{name}")
        lines.append(f"    ASSERT(SIZEOF({section}) <= 0x{maximum:X}, \"{section} exceeds its registry span\")")
    lines.extend(["    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }", "}", ""])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", required=True)
    parser.add_argument("--layout", required=True)
    parser.add_argument("--code", required=True)
    parser.add_argument("--build-id", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)
    try:
        text = generate_linker(args.registry, args.layout, args.code, args.build_id)
        output = Path(args.out)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8", newline="\n")
    except TargetError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
