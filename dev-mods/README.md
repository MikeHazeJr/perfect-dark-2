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
-DevMods ""        # default: include every mod whose "dev": true
-DevMods all       # include every listed mod regardless of flags
-DevMods none      # include no dev mods
-DevMods needler   # include only the named mod(s), comma-separated
```

Release packaging includes mods whose `"release": true`, overridable by the
release script's mod flags. This lets you build or ship **with or without**
specific mods without editing the mods themselves.

## Adding a dev mod

1. Create `dev-mods/<id>/` containing the mod (a loose component folder with its
   `mod.json` + typed `.pd*` archives, or a packaged `.pdmod`).
2. Add an entry to `dev-mods.json`.
3. Build. The mod appears in `<install>/mods/<id>/` and the in-game mod manager
   scans it.
