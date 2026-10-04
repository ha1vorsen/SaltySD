from pathlib import Path
import re
import tomllib


class ManifestError(ValueError):
    pass


PACKAGE_ID = re.compile(r"^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]*[a-z0-9])?)+$")


def load(path):
    source = Path(path).resolve()
    try:
        with source.open("rb") as stream:
            data = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise ManifestError(f"cannot read manifest {source}: {exc}") from exc
    if not isinstance(data.get("package"), dict):
        raise ManifestError("manifest needs a [package] table")
    if not isinstance(data.get("payload"), dict):
        raise ManifestError("manifest needs a [payload] table")
    if not isinstance(data.get("builds"), list) or not data["builds"]:
        raise ManifestError("manifest needs at least one [[builds]] table")
    package_id = data["package"].get("id")
    if not isinstance(package_id, str) or not PACKAGE_ID.fullmatch(package_id):
        raise ManifestError("package.id must be a lowercase reverse-DNS identifier")
    data["_source"] = source
    data["_root"] = source.parent
    return data


def path(manifest, value, field):
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{field} must name a file")
    return (manifest["_root"] / value).resolve()


def version(value, field, components=3):
    if not isinstance(value, str):
        raise ManifestError(f"{field} must be a dotted version string")
    pieces = value.split(".")
    if len(pieces) > components or any(not part.isdigit() for part in pieces):
        raise ManifestError(f"{field} must contain at most {components} numeric components")
    numbers = tuple(int(part) for part in pieces) + (0,) * (components - len(pieces))
    if any(number > 0xFFFF for number in numbers):
        raise ManifestError(f"{field} components must fit in 16 bits")
    return numbers


def integer(value, field):
    if isinstance(value, int):
        number = value
    elif isinstance(value, str):
        try:
            number = int(value, 0)
        except ValueError as exc:
            raise ManifestError(f"{field} must be an integer") from exc
    else:
        raise ManifestError(f"{field} must be an integer")
    if not 0 <= number <= 0xFFFFFFFF:
        raise ManifestError(f"{field} must fit in 32 bits")
    return number
