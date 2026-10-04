from dataclasses import dataclass
import hashlib
from pathlib import Path
import struct

from . import schema
from .manifest import ManifestError, integer, path, version


HEADER = struct.Struct("<IHH6I4H4H2I14I")
BUILD = struct.Struct("<II4HI32s")
VARIANT = struct.Struct("<9I")
DEPENDENCY = struct.Struct("<I4H4HI")
CONFLICT = struct.Struct("<I")
ORDER = struct.Struct("<II")
IMPORT = struct.Struct("<IIII")
HOOK = struct.Struct("<III IiI".replace(" ", ""))

assert HEADER.size == 112
assert BUILD.size == 52
assert VARIANT.size == 36
assert DEPENDENCY.size == 24
assert CONFLICT.size == 4
assert ORDER.size == 8
assert IMPORT.size == 16
assert HOOK.size == 24


def align4(value):
    return (value + 3) & ~3


class Strings:
    def __init__(self):
        self.data = bytearray(b"\0")
        self.offsets = {"": 0}

    def add(self, value, field):
        if not isinstance(value, str):
            raise ManifestError(f"{field} must be text")
        if "\0" in value:
            raise ManifestError(f"{field} cannot contain NUL")
        value.encode("utf-8")
        if value not in self.offsets:
            self.offsets[value] = len(self.data)
            self.data += value.encode("utf-8") + b"\0"
        return self.offsets[value]


def _sha256(pathname, field):
    try:
        return hashlib.sha256(Path(pathname).read_bytes()).digest()
    except OSError as exc:
        raise ManifestError(f"cannot read {field} {pathname}: {exc}") from exc


def _tables(manifest, segment_count, signature_count):
    package = manifest["package"]
    strings = Strings()
    package_id = strings.add(package["id"], "package.id")
    display_name = strings.add(package.get("display_name", package["id"]),
                               "package.display_name")
    author = strings.add(package.get("author", ""), "package.author")
    package_version = version(package.get("version", "0.0.0"), "package.version")
    abi_min = version(package.get("abi_min", "1.0"), "package.abi_min", 2)
    abi_max = version(package.get("abi_max", "1.0"), "package.abi_max", 2)

    capabilities = 0
    for capability in package.get("capabilities", []):
        try:
            capabilities |= schema.CAPABILITIES[capability]
        except KeyError as exc:
            raise ManifestError(f"unknown capability {capability!r}") from exc

    variants_in = manifest.get("variants") or [{"id": 1}]
    variant_ids = set()
    variants = bytearray()
    for index, item in enumerate(variants_in):
        variant_id = integer(item.get("id", index + 1), f"variants[{index}].id")
        if variant_id in variant_ids:
            raise ManifestError(f"variant ID {variant_id} is duplicated")
        variant_ids.add(variant_id)
        first_segment = integer(item.get("first_segment", 0),
                                f"variants[{index}].first_segment")
        segments = integer(item.get("segment_count", segment_count),
                           f"variants[{index}].segment_count")
        first_signature = integer(item.get("first_signature", 0),
                                  f"variants[{index}].first_signature")
        signatures = integer(item.get("signature_count", signature_count),
                             f"variants[{index}].signature_count")
        init_segment = integer(item.get("init_segment", 0xFFFFFFFF),
                               f"variants[{index}].init_segment")
        init_offset = integer(item.get("init_offset", 0), f"variants[{index}].init_offset")
        cro_segment = integer(item.get("cro_loaded_segment", 0xFFFFFFFF),
                              f"variants[{index}].cro_loaded_segment")
        cro_offset = integer(item.get("cro_loaded_offset", 0),
                             f"variants[{index}].cro_loaded_offset")
        if not segments or first_segment + segments > segment_count:
            raise ManifestError(f"variant {variant_id} segment range is outside the payload")
        if not signatures or first_signature + signatures > signature_count:
            raise ManifestError(f"variant {variant_id} signature range is outside the payload")
        variants += VARIANT.pack(variant_id, first_segment, segments, first_signature,
                                 signatures, init_segment, init_offset, cro_segment, cro_offset)

    builds = bytearray()
    for index, item in enumerate(manifest["builds"]):
        variant_id = integer(item.get("variant", 1), f"builds[{index}].variant")
        if variant_id not in variant_ids:
            raise ManifestError(f"build {index} selects unknown variant {variant_id}")
        region_name = item.get("region")
        if region_name not in schema.REGIONS:
            raise ManifestError(f"builds[{index}].region must be usa, eur, or jpn")
        game = version(item.get("game_version", "1.1"), f"builds[{index}].game_version", 2)
        code_path = path(manifest, item.get("code"), f"builds[{index}].code")
        builds += BUILD.pack(integer(item.get("id"), f"builds[{index}].id"),
                             integer(item.get("title_id"), f"builds[{index}].title_id"),
                             game[0], game[1], schema.REGIONS[region_name], 0, variant_id,
                             _sha256(code_path, f"builds[{index}].code"))

    dependencies = bytearray()
    for index, item in enumerate(manifest.get("dependencies", [])):
        target = strings.add(item.get("id"), f"dependencies[{index}].id")
        minimum = version(item.get("min", "0.0.0"), f"dependencies[{index}].min")
        maximum = version(item.get("max", "0.0.0"), f"dependencies[{index}].max")
        flags = schema.SE_RELATION_OPTIONAL if item.get("optional", False) else 0
        dependencies += DEPENDENCY.pack(target, *minimum, 0, *maximum, 0, flags)

    conflicts = bytearray()
    for index, target in enumerate(package.get("conflicts", [])):
        conflicts += CONFLICT.pack(strings.add(target, f"package.conflicts[{index}]"))
    ordering = bytearray()
    for kind_name, kind in (("before", schema.SE_ORDER_BEFORE),
                            ("after", schema.SE_ORDER_AFTER)):
        for index, target in enumerate(package.get(kind_name, [])):
            ordering += ORDER.pack(strings.add(target, f"package.{kind_name}[{index}]"), kind)

    imports = bytearray()
    for index, item in enumerate(manifest.get("imports", [])):
        kind = {"abs32": schema.SE_IMPORT_ABS32,
                "arm_veneer": schema.SE_IMPORT_ARM_VENEER}.get(item.get("kind", "abs32"))
        if kind is None:
            raise ManifestError(f"imports[{index}].kind is unsupported")
        imports += IMPORT.pack(integer(item.get("symbol"), f"imports[{index}].symbol"),
                               integer(item.get("segment"), f"imports[{index}].segment"),
                               integer(item.get("offset"), f"imports[{index}].offset"), kind)

    hooks = bytearray()
    for index, item in enumerate(manifest.get("hooks", [])):
        mode = item.get("mode", "exclusive")
        if mode not in ("exclusive", "chainable"):
            raise ManifestError(f"hooks[{index}].mode must be exclusive or chainable")
        flags = schema.SE_HOOK_EXCLUSIVE if mode == "exclusive" else schema.SE_HOOK_CHAINABLE
        priority = item.get("priority", 0)
        if not isinstance(priority, int) or not -0x80000000 <= priority <= 0x7FFFFFFF:
            raise ManifestError(f"hooks[{index}].priority must fit in signed 32 bits")
        hooks += HOOK.pack(integer(item.get("target"), f"hooks[{index}].target"),
                           integer(item.get("segment"), f"hooks[{index}].segment"),
                           integer(item.get("offset"), f"hooks[{index}].offset"),
                           flags, priority, 0)
    return (strings, package_id, display_name, author, package_version, abi_min, abi_max,
            capabilities, [builds, variants, dependencies, conflicts, ordering, imports, hooks])


def build_package_note(manifest, segment_count, signature_count):
    (strings, package_id, display_name, author, package_version, abi_min, abi_max,
     capabilities, tables) = _tables(manifest, segment_count, signature_count)
    offsets_counts = []
    body = bytearray(b"\0" * HEADER.size)
    for table, record in zip(tables, (BUILD, VARIANT, DEPENDENCY, CONFLICT, ORDER, IMPORT, HOOK)):
        if not table:
            offsets_counts.extend((0, 0))
            continue
        while len(body) % 4:
            body.append(0)
        offsets_counts.extend((len(body), len(table) // record.size))
        body += table
    while len(body) % 4:
        body.append(0)
    strings_offset = len(body)
    body += strings.data
    header = HEADER.pack(schema.SE_PACKAGE_MAGIC, HEADER.size, 0, len(body),
                         strings_offset, len(strings.data), package_id, display_name, author,
                         *package_version, 0, *abi_min, *abi_max,
                         capabilities, 0, *offsets_counts)
    body[:HEADER.size] = header
    return bytes(body)


def parse_package_note(data):
    if len(data) < HEADER.size:
        raise ManifestError("SEA 2 package note is truncated")
    fields = HEADER.unpack_from(data)
    if fields[0] != schema.SE_PACKAGE_MAGIC or fields[1] != HEADER.size or fields[3] != len(data):
        raise ManifestError("SEA 2 package note header is invalid")
    strings_offset, strings_size = fields[4:6]
    if strings_offset > len(data) or strings_size > len(data) - strings_offset:
        raise ManifestError("SEA 2 string table is outside the note")
    string_data = data[strings_offset:strings_offset + strings_size]

    def text(offset):
        if offset >= len(string_data):
            raise ManifestError("SEA 2 string offset is outside the table")
        end = string_data.find(b"\0", offset)
        if end < 0:
            raise ManifestError("SEA 2 string is not terminated")
        return string_data[offset:end].decode("utf-8")

    table_values = fields[-14:]
    builds = []
    build_offset, build_count = table_values[:2]
    if build_offset > len(data) or build_count > (len(data) - build_offset) // BUILD.size:
        raise ManifestError("SEA 2 build table is invalid")
    for index in range(build_count):
        record = BUILD.unpack_from(data, build_offset + index * BUILD.size)
        builds.append({"id": record[0], "title_id": record[1],
                       "game_version": f"{record[2]}.{record[3]}", "region": record[4],
                       "variant": record[6], "code_sha256": record[7].hex().upper()})
    return {"id": text(fields[6]), "display_name": text(fields[7]),
            "author": text(fields[8]), "version": ".".join(map(str, fields[9:12])),
            "abi_min": f"{fields[13]}.{fields[14]}",
            "abi_max": f"{fields[15]}.{fields[16]}", "capabilities": fields[17],
            "builds": builds}


def repack_sea_v2(legacy, sea, package_note):
    segments, notes = legacy.read_elf(sea)
    notes[legacy.NOTE_SEA_VERSION] = struct.pack("<I", schema.SE_PACKAGE_VERSION)
    notes[schema.SE_NOTE_PACKAGE] = package_note
    note_blobs = [legacy.make_note(kind, notes[kind]) for kind in sorted(notes)]
    phnum = len(note_blobs) + len(segments)
    at = legacy.EHDR_SIZE + phnum * legacy.PHDR_SIZE
    phdrs = []
    body = bytearray()
    for note in note_blobs:
        phdrs.append(struct.pack("<IIIIIIII", legacy.PT_NOTE, at, 0, 0, len(note), len(note),
                                 legacy.PF_R, 4))
        body += note
        at += len(note)
    for address, data in segments:
        phdrs.append(struct.pack("<IIIIIIII", legacy.PT_LOAD, at, address, address,
                                 len(data), len(data), legacy.PF_R | legacy.PF_X, 4))
        body += data + b"\0" * (align4(len(data)) - len(data))
        at += align4(len(data))
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = ident + struct.pack("<HHIIIIIHHHHHH", legacy.ET_EXEC, legacy.EM_ARM, 1, 0,
                                 legacy.EHDR_SIZE, 0, legacy.EF_ARM_EABI5_HARD,
                                 legacy.EHDR_SIZE, legacy.PHDR_SIZE, phnum, 0, 0, 0)
    result = header + b"".join(phdrs) + body
    if len(result) > legacy.SEA_BYTES_MAX:
        raise ManifestError(f"SEA 2 package is {len(result)} bytes; limit is {legacy.SEA_BYTES_MAX}")
    return result
