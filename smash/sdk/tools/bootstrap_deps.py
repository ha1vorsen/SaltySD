#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile


SDK = Path(__file__).resolve().parents[1]
DESTINATION = SDK / ".deps"
PACKAGES = (
    {
        "name": "capstone",
        "version": "5.0.9",
        "filename": "capstone-5.0.9-py3-none-win_amd64.whl",
        "sha256": "732cedbbb56d42e723f14d7af6387f1454194a820b4b96b56d1e53f865ef85d0",
        "url": "https://files.pythonhosted.org/packages/50/e6/6f06fdb6a9ed32b2f7cd9c036b92d5324112c3ef7080f2c71efc367d40dd/capstone-5.0.9-py3-none-win_amd64.whl",
    },
    {
        "name": "pyelftools",
        "version": "0.33",
        "filename": "pyelftools-0.33-py3-none-any.whl",
        "sha256": "f215ad5f47d3f1373a21496a6c9e0707c622840d0622f23ff7ce08678b020036",
        "url": "https://files.pythonhosted.org/packages/46/2a/f9697576603dae937727827505a6126a066affb227034e77e6f9068910da/pyelftools-0.33-py3-none-any.whl",
    },
    {
        "name": "hypothesis",
        "version": "6.155.6",
        "filename": "hypothesis-6.155.6-py3-none-any.whl",
        "sha256": "a96d9a29f6bbc8ccac39dd84e140892da76765464929f401a4181b90c20c9ad1",
        "url": "https://files.pythonhosted.org/packages/9e/a9/4c17e962c2e9cbc314bb579ed2e2b2da45d7b6b942aab6948d14d85abfea/hypothesis-6.155.6-py3-none-any.whl",
    },
    {
        "name": "sortedcontainers",
        "version": "2.4.0",
        "filename": "sortedcontainers-2.4.0-py2.py3-none-any.whl",
        "sha256": "a163dcaede0f1c021485e957a39245190e74249897e2ae4b2aa38595db237ee0",
        "url": "https://files.pythonhosted.org/packages/32/46/9cb0e58b2deb7f82b84065f37f3bffeb12413f947f9388e4cac22c4621ce/sortedcontainers-2.4.0-py2.py3-none-any.whl",
    },
)


def _manifest():
    return {item["name"]: {"version": item["version"], "sha256": item["sha256"]}
            for item in PACKAGES}


def main():
    manifest_path = DESTINATION / ".se-deps.json"
    if manifest_path.is_file():
        if json.loads(manifest_path.read_text(encoding="utf-8")) == _manifest():
            print(f"SDK dependencies already match {manifest_path}")
            return 0
        raise SystemExit(f"{DESTINATION} contains another dependency set; remove it explicitly before upgrading")
    if DESTINATION.exists():
        raise SystemExit(f"refusing to overlay unrecognized directory {DESTINATION}")

    staging = SDK / ".deps-staging"
    if staging.exists():
        raise SystemExit(f"refusing to overlay stale staging directory {staging}")
    staging.mkdir()
    try:
        for item in PACKAGES:
            wheel = staging / item["filename"]
            print(f"fetching {item['name']} {item['version']}")
            with urllib.request.urlopen(item["url"], timeout=60) as response:
                wheel.write_bytes(response.read())
            digest = hashlib.sha256(wheel.read_bytes()).hexdigest()
            if digest != item["sha256"]:
                raise RuntimeError(f"SHA-256 mismatch for {item['filename']}: {digest}")
            with zipfile.ZipFile(wheel) as archive:
                archive.extractall(staging / "installed")
        installed = staging / "installed"
        (installed / ".se-deps.json").write_text(
            json.dumps(_manifest(), sort_keys=True, indent=2) + "\n", encoding="utf-8")
        installed.replace(DESTINATION)
        print(f"installed {len(PACKAGES)} pinned packages in {DESTINATION}")
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
