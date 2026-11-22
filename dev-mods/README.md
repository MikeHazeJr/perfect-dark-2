# Dev Mods

Authored mods that ship with the project as **dev content** (not user-installed
mods). They are **git-tracked here** so a clean build can never erase them, and
they are **copied into `<install>/mods/` on every build** by
`devtools/build-headless.ps1` (the same idea as the ROM addin copy, but these
live inside the repo because we author them).

## Why this exists

- All builds are clean builds: the build directory is wiped every time. Mods
  placed directly in the install's `mods/` would be lost. Keeping the source
  here (under git) and re-copying on each build keeps dev mods permanent.
- The runtime scans `<exe>/mods/` (`$E/mods`), which is what the release zip
  ships, so copying into `$BuildDir/mods/` works for both isolated session
  builds and shipped installs.

## Layout

```
dev-mods/
  dev-mods.json        # manifest: which mods exist + default dev/release inclusion
  <mod-id>/            # one folder per dev mod (loose component folder or a .pdmod)
    ...
```

`dev-mods.json` lists each mod with a `path` (folder under `dev-mods/`) and two
default-inclusion flags:

- `dev`: include in normal dev builds.
- `release`: include in release packages.

## Selecting which mods to include

Per build (`build-headless.ps1` / `build-session.ps1`):

```
-DevMods ""        # default: every bundled sample plus every mod whose "dev": true
-DevMods all       # include every listed mod regardless of flags
-DevMods none      # include nothing (and drop the build's old bundled samples)
-DevMods needler   # include only the named mod(s), comma-separated
```

## Bundled samples (`"release": true`)

An entry with `"release": true` is a bundled sample: it ships with the game and
a fresh profile starts with it enabled. Today: `crystal_pistol`,
`mod.vehicle_maian_hover` (folder `vehicle_maian_hover`), `blank_room`,
`model_room` and `grid_two_rooms` (Mike, 2026-11-21).

`tools/stage_bundled_samples.py` stages them, the same way for a release
(`devtools/release.ps1`) and for a local build (`devtools/build-headless.ps1`):

- Each sample is packed from its `mod.json` and its top-level typed archives
  only (never build scripts, notes or authoring sources) into
  `<install>/mods/bundled/<id>.pdmod`, through the release verifier
  (`tools/verify_pdxxx_modder_workflow.py --release-package`). A folder with a
  typed archive below its top level is refused: that is authoring input, not a
  package.
- `<install>/bundled-mods.sha256` is written last, one `<sha256> *<id>.pdmod`
  line per sample in `dev-mods.json` order.
- A bundled sample is never also copied loose: one id twice in one install
  would be two mods.

With no saved mod choice (no `mods-enabled.json`, an empty pd.ini list) and no
`--moddir`, the mod manager enables each listed package while it is the only
mod with that id and its bytes still match
(`port/include/modmgr_bundled_samples.h`). A player's own copy of the same id
in their mods folder always wins and is never enabled for them; a saved choice
always takes over. The player's own mods live in
`%LOCALAPPDATA%\perfectdark\mods`; the release's `Open Mods Folder.lnk` opens
it.

`vehicle_maian_hover` is the hover vehicle exactly as the game's mod author
builds it from `maian_hover_vehicle/recipe.json`; pd-tests
`"[maian-vehicle][bundled]"` fails if the two differ. After changing the recipe
or its files, run that case with `PD2_TEST_ARTIFACT_DIR` set and copy
`<dir>/vehicle_maian_hover/` over `dev-mods/vehicle_maian_hover/`.

`crystal_pistol` is rebuilt from its source vault
(`D:\Game Dev\PD2-Assets\crystal-pistol`, `source/scripts/build_pdweapon.py`);
never hand-edit its archives. Its held view numbers are its own (posx 10.5,
posy -12.9, posz -24.5), not an extracted weapon's.

Some 3D models here were made with Tripo3D; the project's one credit line is in
the top-level README.

## Adding a dev mod

1. Create `dev-mods/<id>/` containing the mod (a loose component folder with its
   `mod.json` + typed `.pd*` archives, or a packaged `.pdmod`).
2. Add an entry to `dev-mods.json`.
3. Build. The mod appears in `<install>/mods/<id>/` and the in-game mod manager
   scans it.
