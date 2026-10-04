#!/usr/bin/env python3

import hashlib
import os
import sys
import tempfile


def digest(path):
    value = hashlib.sha256()
    with open(path, "rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            value.update(block)
    return value.hexdigest().upper()


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: write_input_identity.py <output> <input> [<input> ...]")
    output = os.path.abspath(sys.argv[1])
    inputs = [os.path.abspath(path) for path in sys.argv[2:]]
    lines = []
    for path in inputs:
        if not os.path.isfile(path):
            raise SystemExit(f"input does not name a file: {path}")
        lines.append(f"{digest(path)}  {path}\n")
    content = "".join(lines).encode("utf-8")
    try:
        with open(output, "rb") as current:
            if current.read() == content:
                return
    except FileNotFoundError:
        pass

    directory = os.path.dirname(output)
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".identity-", dir=directory)
    try:
        with os.fdopen(fd, "wb") as target:
            target.write(content)
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == "__main__":
    main()
