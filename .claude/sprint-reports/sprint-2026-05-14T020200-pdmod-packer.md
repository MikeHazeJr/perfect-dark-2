# Sprint Report - In-Game .pdmod Packer UI

- Sprint ID: sprint-2026-05-14T020200-pdmod-packer
- Date: 2026-05-13
- Branch: dev
- Pillar / Card: modding / c3808
- Source: 2026-05-13 incompleteness audit, Sprint 4 (Track 3 finding 3.8)

## Goal

Expose `modpackPdmodFromFolder` in the in-game Modding Hub so end
users can pack a folder of `.pd<ext>` files into a valid `.pdmod`.
Today the helper is reachable only via `port/src/modmigrate.c:158`
(legacy auto-migration). End users had no UI path to author a
`.pdmod` archive from a folder. v1 scope: minimal two-text-input
form, no file-system browser, no `.pdpack` migration helper.

## Shipped

- `port/fast3d/pdgui_menu_moddinghub.cpp` -- added `#include
  "modpack_pdmod.h"` (line 45) and a new section at the end of
  `renderPackTool` titled "PACK .pdmod FROM FOLDER" (lines 1257-
  1337). The section adds four static buffers (`s_PdmodSrcFolder`
  default `mods/staging/`, `s_PdmodOutPath` default
  `mods/packed.pdmod`, `s_PdmodStatusMsg`, `s_PdmodStatusOk`), two
  `ImGui::InputText` controls (Folder + Output), and one
  `PdButton` "Pack .pdmod" that calls
  `modpackPdmodFromFolder(folder, output)`. On success: emits
  `LOG_NOTE "MODPACK.PDMOD: packed <folder> -> <output>"` and shows a
  green success line. On failure: maps the `MODPACK_PDMOD_ERR_*`
  return code to a human reason, emits `LOG_WARNING
  "MODPACK.PDMOD: failed folder=<...> output=<...> rc=<n>"`, and
  shows a red status line.
- `tools/kanban/state.json` -- new card c3808 inserted between
  c3807 and c127. Pillar `modding`, column `active`, order 25000,
  priority 3. Title: "Modding: in-game .pdmod packer UI (folder
  -> .pdmod)".
- `tools/kanban/state.json` -- new pillar `modding` registered in
  the top-level `pillars[]` array (id `modding`, name `Modding`,
  color `#f59e0b`). Required so the commit-msg hook resolves the
  `Modding` subject prefix to a registered pillar. No prior
  `modding`-pillar cards existed; existing mod-tooling work uses
  `mod-infrastructure`. The new `modding` pillar is reserved for
  pure mod-authoring UX work to keep `mod-infrastructure`
  (registration/loader plumbing) distinct.
- No header touched: `port/include/modpack_pdmod.h` already
  declared `s32 modpackPdmodFromFolder(const char *, const char *)`
  inside an `extern "C"` block, so no edit was required.
- `pdguiToastEnqueue` is not used: the moddinghub.cpp file does
  not include `pdgui_toast.h` and toast notifications aren't
  hooked up in this tool. The inline status-line pattern used by
  the rest of the Pack tool (status string + colored text) is the
  affordance for success/failure feedback. The task allowed for
  toast use only "if available in the file" -- it is not.

## Decisions

1. **Add `modding` pillar to the registry.** Task spec said the
   card pillar is `modding` and the commit subject is `Modding -
   c3808:`. The commit-msg hook validates the pillar token against
   `tools/kanban/state.json` `pillars[]`. Since no `modding`
   pillar existed, registered a new one. Did not co-opt the
   existing `mod-infrastructure` pillar to avoid mixing card
   semantics (mod-infrastructure = registration/loader/save plumbing;
   modding = end-user authoring UX).
2. **Use `--` instead of em-dash in one pre-existing status line.**
   Touched line 1254 `"Mod Pack -- export/import .pdpack files"`
   (was an em-dash). Project standing rule says "no em-dashes
   anywhere"; the single near-touch line was converted. Other
   em-dashes elsewhere in the file were left alone (outside this
   sprint's scope).
3. **Inline `MODPACK_PDMOD_ERR_*` translation in the .cpp.** The
   error codes are already exposed via `modpack_pdmod.h`; the
   switch in the click handler is short enough to live inline
   without warranting a wrapper helper.
4. **No `pdguiToastEnqueue`.** moddinghub.cpp does not include the
   toast header and the existing Mod Pack tool uses inline status
   lines. Adopted the file's local convention.

## Build verify

`pwsh devtools/build-headless.ps1 -Targets pd` PASS -- Client
target rebuilt cleanly from a fresh Build/ (smart-clean migration
forced), 47s, output `Build/PerfectDark.exe` 55.4 MB. Updater also
PASS (2s). Total 69s. The earlier failed `ninja -C Build pd` run
from bash was a ccache/PCH staleness issue unrelated to this work
(direct `c++ -c` invocation on the touched .cpp exited 0 with no
warnings); the canonical PowerShell build runner self-recovered via
smart-clean.

## Files touched

- `port/fast3d/pdgui_menu_moddinghub.cpp` (+85 lines, -1 line; new
  section + include + one em-dash fix)
- `tools/kanban/state.json` (+20 lines; new pillar `modding`,
  new card c3808)

## Commit

SHA TBD (committed by orchestrator after this report is in the
tree). Subject: `Modding - c3808: in-game .pdmod packer UI (folder
to .pdmod)`. Refs trailer references c3808.

## Follow-ups

- File-system folder browser (`pdgui_filebrowser`) is the
  obvious next step; deferred per v1 scope.
- `.pdpack` -> `.pdmod` migration helper is the second piece of
  the audit Track 3 finding 3.8; deferred.
- A future c-NNN card may want to also surface
  `modpackPdmodWriteSingle` (used today only by the theme
  editor) for users authoring a single-asset mod from in-game
  state. Not in scope here.

## Where to look

- Helper signature: `port/src/modpack_pdmod.c:248`
  `s32 modpackPdmodFromFolder(const char *src_folder, const char
  *out_path)`.
- Helper public decl: `port/include/modpack_pdmod.h:56`.
- New UI: `port/fast3d/pdgui_menu_moddinghub.cpp:1257-1337`.
- New card: `tools/kanban/state.json` c3808 (between c3807 and
  c127).
- New pillar: `tools/kanban/state.json` `pillars[]` entry
  `modding`.
