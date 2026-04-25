# PD2ModPropHandler

Windows Shell property handler for `.pdmod` archives.

When registered, Windows Explorer shows mod manifest fields (Title, Author,
Comment / description, Version, Keywords / id) directly in the Properties
dialog and the column-view of `mods/installed/`. The handler reads `mod.json`
inside the archive on demand; no extraction to disk.

This is **optional**. The defensive zip-comment mirror written by every
`.pdmod` saver lets non-Windows tools and unregistered Windows installs
see the same headline fields via 7-Zip / file-manager hover-tooltips.

## Build

The DLL is built alongside the engine. From the repo root:

```bash
source devtools/build-env.sh
cmake -B Build -G Ninja
ninja -C Build PD2ModPropHandler
```

Output: `Build/PD2ModPropHandler.dll`.

## Install

Run the registration script as Administrator:

```powershell
PowerShell -ExecutionPolicy Bypass -File tools\pdmod_prophandler\install\register.ps1 `
    -DllPath "C:\Path\To\Build\PD2ModPropHandler.dll"
```

The script writes the registry entries described in design Section 4.5.2
(see `context/designs/pdmod-unified-mod-format.md`).

After registration, restart Explorer:

```powershell
taskkill /f /im explorer.exe; start explorer.exe
```

Then right-click any `.pdmod` and check **Properties → Details**. You
should see Title, Authors, Comment, Version, and Keywords populated from
the archive's `mod.json`.

To uninstall:

```powershell
PowerShell -ExecutionPolicy Bypass -File tools\pdmod_prophandler\install\unregister.ps1
```

## Property mapping

| Property                          | Source field in `mod.json` |
|---|---|
| `System.Title`                    | `name`                     |
| `System.Author`                   | `creator` (falls back to `author`) |
| `System.Comment`                  | `description`              |
| `System.Software.ProductVersion`  | `version`                  |
| `System.Keywords`                 | `id` (Phase 1; full `tags` array deferred to a follow-up that adds the `.propdesc` schema) |

Custom `System.Mod.*` properties from design Section 4.5.1 are deferred
to a future revision; they require shipping a `.propdesc` XML schema and
registering it via `psr.exe`. The standard properties listed above
satisfy the design's Phase 1 surface.

## Safety

- **Read-only.** `IPropertyStore::SetValue` returns `STG_E_ACCESSDENIED`.
  The handler will never write back to the archive.
- **Bounded reads.** Archives over 256 MiB are refused so Explorer cannot
  be memory-ballooned by a malicious file.
- **Path traversal refused.** Entries with `..` segments or absolute
  paths are rejected before the inflate pass; `mod.json` lookup compares
  by exact path so injection via crafted entry names cannot redirect.

## Source layout

```
tools/pdmod_prophandler/
  CMakeLists.txt                # CMake target (linked from top level)
  PD2ModPropHandler.cpp         # COM glue: IInitializeWithStream + IPropertyStore
  pdmod_zip_inflate.{h,cpp}     # Minimal zip reader from in-memory buffer (zlib)
  pdmod_json_min.{h,cpp}        # Top-level "key": "string" extractor
  install/
    register.ps1                # Registry setup (run as Admin)
    unregister.ps1              # Registry teardown (run as Admin)
  README.md                     # This file
```

The DLL has zero engine dependencies -- it could be shipped as a
standalone install on machines without the game (useful for sysadmins
who manage `.pdmod` archives in a shared mods library).
