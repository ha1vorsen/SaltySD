# SALT Engine SDK

Requires Python 3.11 or newer and devkitARM. Package sources are 32-bit ARM
ELF files. C headers are in `include/se`.

## Setup

Install the versions listed in `requirements*.lock` into `sdk/.deps`:

```
python tools/bootstrap_deps.py
```

## Build

```
python saltysd_sea.py build <manifest>
python saltysd_sea.py check --matrix <manifest> <archive>
```

Examples are under `examples`. Pass the game executable as
`CODE=<path-to-code.bin>`.

## Install

Copy the archive to `sd:/luma/titles/smash/engine/<name>/<name>.sea`.
