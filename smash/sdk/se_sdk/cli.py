import hashlib
from pathlib import Path

from .format import build_package_note, parse_package_note, repack_sea_v2
from .manifest import ManifestError, load, path
from . import schema


def _read(pathname, label):
    try:
        return Path(pathname).read_bytes()
    except OSError as exc:
        raise ManifestError(f"cannot read {label} {pathname}: {exc}") from exc


def _matrix_check(legacy, sea, manifest):
    segments, notes = legacy.read_elf(sea)
    metadata = parse_package_note(notes[schema.SE_NOTE_PACKAGE])
    declared = metadata["builds"]
    for index, build in enumerate(manifest["builds"]):
        code_path = path(manifest, build.get("code"), f"builds[{index}].code")
        code = _read(code_path, "code.bin")
        digest = hashlib.sha256(code).hexdigest().upper()
        matching = [item for item in declared if item["code_sha256"] == digest]
        if len(matching) != 1:
            raise ManifestError(f"{code_path} selects {len(matching)} SEA 2 build records")
        legacy.resolve(sea, code)
        print(f"build {matching[0]['id']:#x}: {code_path.name} resolves {len(segments)} segments")


def run_build(args, legacy):
    manifest = load(args.manifest)
    payload = manifest["payload"]
    elf_path = path(manifest, payload.get("elf"), "payload.elf")
    code_path = path(manifest, payload.get("code"), "payload.code")
    elf = _read(elf_path, "ELF")
    code = _read(code_path, "code.bin")
    sea12, report = legacy.convert(elf, code, not args.no_branch_fixups)
    segments, notes = legacy.read_elf(sea12)
    signatures, _, _ = legacy.parse_locators(notes, len(segments))
    package_note = build_package_note(manifest, len(segments), len(signatures))
    sea20 = repack_sea_v2(legacy, sea12, package_note)
    output = Path(args.output).resolve() if args.output else path(
        manifest, payload.get("output", elf_path.with_suffix(".sea").name), "payload.output")
    if output == elf_path:
        raise ManifestError("output would overwrite the input ELF")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(sea20)
    print(f"wrote {output}: SEA 2.0, {len(segments)} segments, {len(signatures)} signatures, "
          f"{len(sea20)} bytes")
    for line in report:
        print(f"  {line}")
    _matrix_check(legacy, sea20, manifest)
    return 0


def run_check(args, legacy):
    manifest = load(args.matrix)
    sea = _read(Path(args.input).resolve(), "SEA")
    _, notes = legacy.read_elf(sea)
    if legacy.parse_sea_version(notes) != "2.0" or schema.SE_NOTE_PACKAGE not in notes:
        raise ManifestError("matrix checking requires a SEA 2.0 package")
    _matrix_check(legacy, sea, manifest)
    wrong = manifest.get("wrong_build")
    if wrong:
        wrong_path = path(manifest, wrong.get("code"), "wrong_build.code")
        try:
            legacy.resolve(sea, _read(wrong_path, "wrong-build code.bin"))
        except legacy.SeaError:
            print(f"wrong build refused: {wrong_path.name}")
        else:
            raise ManifestError(f"wrong build unexpectedly resolves: {wrong_path}")
    return 0


def add_commands(commands, legacy):
    parser = commands.add_parser("build", help="build a SEA 2 package from sea.toml")
    parser.add_argument("manifest", help="SEA 2 TOML manifest")
    parser.add_argument("-o", "--output", help="override payload.output")
    parser.add_argument("--no-branch-fixups", action="store_true")
    parser.set_defaults(run=lambda args: run_build(args, legacy))

    parser = commands.add_parser("check", help="verify a SEA 2 package against a build matrix")
    parser.add_argument("--matrix", required=True, help="TOML manifest containing [[builds]]")
    parser.add_argument("input", help="SEA 2 package")
    parser.set_defaults(run=lambda args: run_check(args, legacy))


def describe(notes):
    if schema.SE_NOTE_PACKAGE not in notes:
        return []
    metadata = parse_package_note(notes[schema.SE_NOTE_PACKAGE])
    return [f"package: {metadata['display_name']} ({metadata['id']}) {metadata['version']}",
            f"author: {metadata['author'] or '(unspecified)'}",
            f"host ABI: {metadata['abi_min']} through {metadata['abi_max']}",
            f"build records: {len(metadata['builds'])}"]
