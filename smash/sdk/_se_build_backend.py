from __future__ import annotations

import base64
import csv
import gzip
import hashlib
import io
from pathlib import Path
import tarfile
import zipfile


NAME = "saltysd_sea"
VERSION = "2.0.0.dev0"
DIST_INFO = f"{NAME}-{VERSION}.dist-info"
WHEEL_NAME = f"{NAME}-{VERSION}-py3-none-any.whl"
SDIST_NAME = f"{NAME}-{VERSION}.tar.gz"
ROOT = Path(__file__).resolve().parent


def _metadata() -> bytes:
    return (
        "Metadata-Version: 2.1\n"
        "Name: saltysd-sea\n"
        f"Version: {VERSION}\n"
        "Summary: Build and inspect SALT Engine archives\n"
        "Requires-Python: >=3.11\n"
        "Requires-Dist: capstone==5.0.9\n"
        "Requires-Dist: pyelftools==0.33\n"
        "Provides-Extra: test\n"
        "Requires-Dist: hypothesis==6.155.6; extra == 'test'\n"
        "Requires-Dist: sortedcontainers==2.4.0; extra == 'test'\n"
        "\n"
    ).encode()


def _wheel_files():
    yield "saltysd_sea.py", ROOT / "saltysd_sea.py"
    for source in sorted((ROOT / "se_sdk").glob("*.py")):
        yield f"se_sdk/{source.name}", source
    for header in ("package.h", "hooks.h"):
        source = ROOT / "include" / "se" / header
        yield f"se_sdk/include/se/{header}", source


def _record_digest(data: bytes) -> str:
    digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=")
    return "sha256=" + digest.decode("ascii")


def get_requires_for_build_wheel(config_settings=None):
    return []


def get_requires_for_build_sdist(config_settings=None):
    return []


def prepare_metadata_for_build_wheel(metadata_directory, config_settings=None):
    destination = Path(metadata_directory) / DIST_INFO
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "METADATA").write_bytes(_metadata())
    (destination / "WHEEL").write_text(
        "Wheel-Version: 1.0\nGenerator: saltysd-sea first-party backend\n"
        "Root-Is-Purelib: true\nTag: py3-none-any\n",
        encoding="utf-8",
    )
    (destination / "entry_points.txt").write_text(
        "[console_scripts]\nsaltysd-sea = saltysd_sea:main\n", encoding="utf-8"
    )
    return DIST_INFO


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    destination = Path(wheel_directory)
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / WHEEL_NAME
    members = {}
    for archive_name, source in _wheel_files():
        members[archive_name] = source.read_bytes()
    members[f"{DIST_INFO}/METADATA"] = _metadata()
    members[f"{DIST_INFO}/WHEEL"] = (
        b"Wheel-Version: 1.0\nGenerator: saltysd-sea first-party backend\n"
        b"Root-Is-Purelib: true\nTag: py3-none-any\n"
    )
    members[f"{DIST_INFO}/entry_points.txt"] = (
        b"[console_scripts]\nsaltysd-sea = saltysd_sea:main\n"
    )
    rows = [(name, _record_digest(data), str(len(data)))
            for name, data in sorted(members.items())]
    record_name = f"{DIST_INFO}/RECORD"
    record = io.StringIO(newline="")
    writer = csv.writer(record, lineterminator="\n")
    writer.writerows(rows + [(record_name, "", "")])
    members[record_name] = record.getvalue().encode()
    timestamp = (1980, 1, 1, 0, 0, 0)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(members.items()):
            info = zipfile.ZipInfo(name, timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    return WHEEL_NAME


def _source_files():
    roots = [
        ROOT / "README.md",
        ROOT / "pyproject.toml",
        ROOT / "_se_build_backend.py",
        ROOT / "saltysd_sea.py",
        ROOT / "requirements.lock",
        ROOT / "requirements-test.lock",
    ]
    for folder in ("se_sdk", "include", "targets", "templates", "examples", "tests", "tools"):
        roots.extend(path for path in (ROOT / folder).rglob("*") if path.is_file())
    for source in sorted(set(roots)):
        if "__pycache__" not in source.parts and "build" not in source.parts:
            yield source


def build_sdist(sdist_directory, config_settings=None):
    destination = Path(sdist_directory)
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / SDIST_NAME
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for source in _source_files():
            data = source.read_bytes()
            name = f"{NAME}-{VERSION}/{source.relative_to(ROOT).as_posix()}"
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o644
            info.mtime = 0
            archive.addfile(info, io.BytesIO(data))
    with output.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
            compressed.write(buffer.getvalue())
    return SDIST_NAME
