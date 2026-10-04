#!/usr/bin/env python3

import os
import sys
import tempfile


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: write_text_if_changed.py <output> <line> [<line> ...]")
    output = os.path.abspath(sys.argv[1])
    content = ("\n".join(sys.argv[2:]) + "\n").encode("utf-8")
    try:
        with open(output, "rb") as current:
            if current.read() == content:
                return
    except FileNotFoundError:
        pass
    directory = os.path.dirname(output)
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".config-", dir=directory)
    try:
        with os.fdopen(fd, "wb") as target:
            target.write(content)
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == "__main__":
    main()
