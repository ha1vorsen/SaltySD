# SEA tool

`saltysd_sea.py` makes SALT Engine patches, emitted as SALT Engine Archives (SEAs). It needs Python 3.8 or newer and nothing else.

```
python saltysd_sea.py convert highpoly.elf --code usa_code.bin -o highpoly.sea
python saltysd_sea.py convert highpoly.elf --code usa_code.bin --also eur_code.bin --also jpn_code.bin
python saltysd_sea.py info highpoly.sea --code eur_code.bin
```

Install a plugin as `sd:/luma/titles/smash/engine/<name>/<name>.sea`, one `.sea` per folder. Turn it on or off in the Tetra Menu submenu under Plugins / Engine.

## Input

A 32-bit little-endian ARM ELF of type `EXEC`, linked at the addresses of the `code.bin` passed with `--code`. Each `PT_LOAD` segment is one patch. The game's original bytes come from the ELF's `SaltySD` note if it has one, as older SaltySD plugins do, and otherwise from `--code`. An older `.sea` can be passed as input too.

## How a patch is found

SaltySD does not write to fixed addresses. For every segment the tool picks a signature, a short run of the game's own instructions that occurs exactly once in `code.bin`, and stores where the segment sits relative to it. At boot SaltySD searches the game for each signature and patches at that offset, so one `.sea` can work across regions and update layouts where the code has moved.

- A segment that overwrites game code usually gets a signature covering its own original bytes.
- A segment placed in unused space (a run of zeros, which cannot be unique) is placed relative to the nearest unique code before it, up to 4 KiB away.
- `B` and `BL` instructions inside a segment that jump to another segment or into the game are re-aimed at load, so calls and returns still land after code has moved. `convert` lists every one it finds. A data word that happens to decode as a branch into `code.bin` is re-aimed too; pass `--no-branch-fixups` if a segment holds such data.
- The branch field of any `B`/`BL` inside a signature is ignored when matching (shown as `??` by `info`), because it changes whenever code moves.

`--also` checks the result against other `code.bin` files and reports how far each segment moved, or why it does not resolve there. `convert` exits with status 3 when any `--also` file fails, after still writing the `.sea`.

## Salt Engine 1.1 limits

SE 1.1 is the current stopgap format. Each SEA declares the loader features it requires. Future loaders retain support for existing features so a compatible SEA remains loadable without being rebuilt.

- Byte patches only. A segment overwrites part of `code.bin` once at boot or part of a named CRO's code each time that CRO loads. There are no callbacks, imports or new memory.
- RomFS files and the heap cannot be patched.
- Every signature must match exactly once within its target's code. If one is missing or matches more than once, that file's patches are refused.
- SaltySD searches the game after writing its own patches and those of plugins that loaded earlier. A signature that covers bytes one of them changed will not be found. `--also` and `info --code` check against clean `code.bin` files, so they cannot catch this.
- Only ARM `B`/`BL` branches are re-aimed automatically. A branch from a CRO may target the main binary or the same CRO. A branch from the main binary to a CRO, or from one CRO to another, is refused.
- At most 64 signatures per plugin, each 4 to 64 bytes.
- No segment may be empty, and each segment's file size must equal its memory size (no `.bss`).
- A `.sea` is at most 64 KiB. All plugins together hold at most 256 segments.
- Data needed for CRO patches is retained in the game's heap. SaltySD allocates only the CRO data used by enabled SEA files and reserves nothing when no CRO patches are installed. The practical limit is the available heap—about 1.75 MiB in the current build—not a fixed store size or file count.
- Before writing anything, SaltySD checks that the game's bytes at each found address match the original bytes stored in the `.sea`. If any differ, the whole plugin is skipped.
- A plugin may not touch bytes that SaltySD patches itself or that an earlier plugin has claimed. Plugins load in folder name order, and a later one that collides is skipped.
- At boot, validation and main-binary patching are all or nothing per file. If that fails, its CRO patches are discarded. Each time a CRO loads, that file's patches for the CRO apply completely or not at all.
- CRO signatures are searched on the first load. Later loads recheck the bytes at the cached offset before applying anything.
- Refused plugins are not reported on screen.
- Turning a plugin on or off takes effect the next time the game starts.
