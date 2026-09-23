### Smash dt/ls SD Redirect v2.0

These edits redirect the romfs:/dt and romfs:/ls files to load straight from the SD card, allowing for modifications and additions of any file without the need to repack or alter existing archives. SD loaded files take first priority, with update files next followed by the original content. All addresses are found automatically based on the code.bin and the payloads adjusted accordingly. If you are creating a modified update CIA or HANS codebin you must make sure your code.bin is decompressed.

**Smash's SaltySD now ships as a Luma3DS plugin**

Nothing is installed as a `code.ips` any more. Luma applies one IPS per title, so an IPS build meant it was SaltySD or game engine patches, never both. The plugin writes the same changes into the game itself at startup, before the game's first instruction, and the IPS slot stays free.

It also keeps the payloads out of the game's binary. They used to be written over live libpng code; they now live in the plugin.

**Patching Instructions**

 * Use a code.bin of your version of Smash to patch and place it at `/smash/code.bin`, alongside the Makefile.
 * Grab the latest armips from [here](https://buildbot.orphis.net/armips/) and make sure your terminal can call it (i.e. add it to PATH).
 * Build with the Makefile provided, naming the region you are building for: `make TITLE_IDS=000EDF00` (USA), `000EE000` (EUR) or `000B8B00` (JPN). The code.bin is scanned and patched to `build/code_saltysd.bin`. `build/plugin/saltysd.3gx` is built afterwards. 
 * Install the plugin at `sd:/luma/plugins/<full title ID>/saltysd.3gx`, ie `sd:/luma/plugins/00040000000EDF00/saltysd.3gx`, and enable the plugin loader in Rosalina.
 * Remove any older SaltySD `code.ips` or `code.bin`. If the game is already patched by one, you will have issues. SaltySD v2 breaks other IPS mods, like the older high quality model IPS. 

**Override Layout**

 * Mod folders are self contained and can be named anything. They belong at `sd:/saltysd/smash/`, mirroring the game's own directory tree, i.e. `sd:/saltysd/smash/MeleeFox/model/fighter/fox/body/h00` would hold a model swap for Fox. You can place up to 64 other mods alongside it, like `sd:/saltysd/smash/NewMusic/bgm/...`
 * Loose files mirror the game's own tree and are scanned from `sd:/luma/titles/smash/`, ie `param/fighter/fighter_param_common.bin`. This is most equivalent to the SaltySD v1.2 and below patching method, and is now legacy. Anything here will have priority over what's in `/saltysd/smash/*`.
 * `revoke*.txt` lists are read from the legacy location only, i.e. `sd:/luma/titles/smash/revoke-effects.txt`. A revoke prevents the entire game from using that resource, even if a custom mod folder uses it.

**CRO and BGM Override**

 * CROs are stored in the same heirarchy as they are in `cro.sarc`/`, i.e. `fighter/falco` is overridden as `cro/fighter/falco`.
 * BGM is stored under `sound/bgm/`, named as the game names it, ie `sound/bgm/snd_bgm_menu.nus3bank`.
 * Both work in either layout, ie `sd:/luma/titles/smash/cro/fighter/falco` or `sd:/saltysd/smash/MyMod/cro/fighter/falco`.

**Caching**

This helps load times for smaller modpacks on consoles, and eliminates load times for larger packs under emulation, but has diminishing returns for packs that increase lots of files on a real console. If Smash Run isn't loading the final battle, please select `Build Mod Index` in SaltySD's submenu by going to the main menu of Smash and pressing Y. This also fixes Classic Mode.


**Notes**

 * SaltySD v2's layout is a critical change. Content left directly under `sd:/saltysd/smash/` by an older SaltySD will no longer work properly. Use Mod Moon to handle this automatically, or move everything inside a new folder inside of `/saltysd/smash/`.
 * Non-update versions (Demo, 1.0.1) have not been tested with SaltySD and are unlikely to work yet, versions past those but under 1.1.3 may not work, but are more likely to work. In addition to this, the Smash Demo does not have SDMC access in its exheader, so SaltySD would never work with the Demo without a modified version to grant permissions.
 * Creating modified CIAs is not advised, as Citra and Luma CFW both support code.bin override and Luma CFW has support for IPS patching.
 * The game will flash a blue or green screen after you start running Smash. This is normal and means that you have patched the game properly, congrats!
