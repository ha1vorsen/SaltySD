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

## SEA v1 limits

SEA v1 is a stopgap format. SaltySD's plugin system is planned to be replaced.

- Byte patches only. Each segment overwrites part of `code.bin` (from `0x00100000` to its end) once, at boot, before the game's first instruction. There are no hooks, callbacks, imports or new memory. New code has to fit in space the plugin overwrites itself.
- CROs, RomFS files and the heap cannot be patched.
- Every signature must match exactly once. If one is missing or matches more than once, the whole plugin is skipped.
- SaltySD searches the game after writing its own patches and those of plugins that loaded earlier. A signature that covers bytes one of them changed will not be found. `--also` and `info --code` check against clean `code.bin` files, so they cannot catch this.
- Only ARM `B`/`BL` branches are re-aimed automatically. Absolute addresses in a segment (literal pools, pointer tables) and Thumb branches are not, so a segment holding them only works where those addresses did not move.
- At most 64 signatures per plugin, each 4 to 64 bytes.
- No segment may be empty, and each segment's file size must equal its memory size (no `.bss`).
- A `.sea` is at most 64 KiB. All plugins together hold at most 256 segments.
- Before writing anything, SaltySD checks that the game's bytes at each found address match the original bytes stored in the `.sea`. If any differ, the whole plugin is skipped.
- A plugin may not touch bytes that SaltySD patches itself or that an earlier plugin has claimed. Plugins load in folder name order, and a later one that collides is skipped.
- A plugin applies completely or not at all. Skipped plugins are not reported on screen.
- Every enabled plugin searches all of `code.bin` once at boot, which adds to the game's start-up time.
- Turning a plugin on or off takes effect the next time the game starts.
