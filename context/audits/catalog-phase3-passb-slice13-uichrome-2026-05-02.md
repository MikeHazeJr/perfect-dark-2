# Phase 3 Pass B Slice 13 -- UI Chrome Migration Audit

> **Date**: 2026-05-02 PM (initial commit + same-day correction)
> **Session**: catalog-pass-b-slice13-uichrome (worktree)
> **Predecessors**: Pass B Slices 1/3/4/7/9 (`0983b47c` + `214518b9`); Pass B Slices 2/5/6/8/11 (`fb7331ce`).  Slice 13 is the largest remaining Pass B item.
> **Plan reference**: [`context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md`](../designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md) Slice 13.
> **Source-of-truth audit**: [`context/audits/rom-extraction-audit-2026-04-30.md`](rom-extraction-audit-2026-04-30.md) Section 3.
> **Mike's initial Slice 13 directive (superseded; see Decision corrected below)**: "Move `mods/base-ui/textures/*` to `data/ui/textures/*`. UI chrome is project-authored canonical content under `base/`, not under `mods/`. Update the catalog entry paths, the extraction destination, and verify the runtime loads from the new disk path with no ROM-direct fallback."
> **Mike's corrected Slice 13 directive (2026-05-02 PM, post initial commit)**: "UI chrome textures are EXTRACTED from the ROM, not project-authored. So extraction destination should be `data/ui/textures/`, NOT `data/ui/textures/`. The original ROM extraction audit recommendation (`data/ui/pd-original.pdui`-style BYOR location) was correct."

---

## Decision corrected (2026-05-02 PM, follow-up commit)

**The initial Slice 13 commit (`f54959d1`) misclassified UI chrome textures as
project-authored content and placed them under `data/ui/textures/`.** That was
wrong: the textures are extracted from the user-supplied ROM at first launch
by `pdguiThemeExtractRomTextures`. ROM-extracted content never ships with the
project and therefore belongs in the BYOR `data/` tier alongside the per-romid
segments populated by `romExtractAllSegments` (Pass A.2 / Slices 2/5/6/8/11).

The follow-up commit corrects this: all 14 extraction destinations + 13 catalog
entry paths + 13 existence-check paths + 3 fsCreateDir calls now target
`data/ui/textures/`. The `[uichrome]` test pin gains an explicit "no `base/ui/`
references" assertion so any future regression is caught at compile time.

**BYOR convention (canonical)**:

- `base/`  -- project-authored content that ships with releases (currently:
            `arenas.pdbase`, `bodies.pdbase`, `heads.pdbase`, `weapons.pdbase`).
            Code + tooling treat it as read-only at runtime.
- `data/`  -- BYOR-extracted runtime content populated on the user's machine
            at first launch from their ROM. Per-romid segments live under
            `data/<romid>/segs/`; UI chrome textures live under
            `data/ui/textures/`. Never shipped, never committed, never
            redistributed.
- `mods/`  -- user-installed overlay content (`.pdmod` archives, theme JSON,
            etc.). Catalog override layer.

The `data/ui/textures/` location follows the same BYOR-tier reasoning as
the segment extractor lane: nothing under `data/` is shipped, the whole
tree is regenerable from the user's ROM, and the catalog reads through
the FileProvider handle exactly the same way for both `base/*.pdbase` and
`data/<romid>/segs/*.bin`.

---

## A. Surfaces (where the legacy `mods/base-ui/textures/` path lived)

Two files, 49 string-literal hits total:

- [`port/fast3d/pdgui_theme.cpp`](../../port/fast3d/pdgui_theme.cpp) -- 47 refs.
- [`port/include/pdgui_theme.h`](../../port/include/pdgui_theme.h) -- 2 refs (header comments).

### A.1 Catalog entry paths (`k_UiTextures[]`, 13 rows)

[`pdgui_theme.cpp:1737-1749`](../../port/fast3d/pdgui_theme.cpp:1737).  Read by `pdguiThemeLateInit()` once per session to load TGAs from disk into the GL texture cache, register `ASSET_UI` catalog rows, and expose textures via `pdguiThemeGetTexture(catalog_id)`.

| catalog_id | legacy path | new path |
|---|---|---|
| `base:ui_bg_haze` | `mods/base-ui/textures/ui_bg_haze.tga` | `data/ui/textures/ui_bg_haze.tga` |
| `base:ui_particles` | `mods/base-ui/textures/ui_particles.tga` | `data/ui/textures/ui_particles.tga` |
| `base:ui_noise_sm` | `mods/base-ui/textures/ui_noise_sm.tga` | `data/ui/textures/ui_noise_sm.tga` |
| `base:ui_noise_lg` | same | data/ui/textures/ui_noise_lg.tga |
| `base:ui_grad_bar` | same | data/ui/textures/ui_grad_bar.tga |
| `base:ui_mirror_tile` | same | data/ui/textures/ui_mirror_tile.tga |
| `base:ui_dot_tile` | same | data/ui/textures/ui_dot_tile.tga |
| `base:ui_nuke` | same | data/ui/textures/ui_nuke.tga |
| `base:ui_bg_alt` | same | data/ui/textures/ui_bg_alt.tga |
| `base:ui_deco` | same | data/ui/textures/ui_deco.tga |
| `base:ui_icon_a` | same | data/ui/textures/ui_icon_a.tga |
| `base:ui_icon_b` | same | data/ui/textures/ui_icon_b.tga |
| `base:ui_icon_c` | same | data/ui/textures/ui_icon_c.tga |

### A.2 Extraction destination (`pdguiThemeExtractRomTextures`)

[`pdgui_theme.cpp:2534`](../../port/fast3d/pdgui_theme.cpp:2534) (TGA), `:2545` (PNG sidecar), `:2554` (9-slice JSON sidecar), `:2596` (procedural-fallback path).  Iterates `k_Extracts[]` (14 rows including `ui_stars`) and writes per-texture files.

### A.3 Existence-check list (`k_ExpectedBaseUiTgas[]`, 13 rows)

[`pdgui_theme.cpp:2724-2737`](../../port/fast3d/pdgui_theme.cpp:2724).  Read by `s_baseUiTexturesExist` and `s_countMissingBaseUiTgas` for the auto-extract trigger in `pdguiThemeCheckExtract`.

### A.4 Directory creation + auto-written `mod.json`

[`pdgui_theme.cpp:3221-3277`](../../port/fast3d/pdgui_theme.cpp:3221).  Pre-Slice-13 created `mods/`, `mods/base-ui/`, and `mods/base-ui/textures/` and wrote a hand-rolled `mods/base-ui/mod.json` so the mod manager would see the loose-file directory as a mod.  Audit Section J: this autogen is dropped at Slice 13 (base/ is not a mod).

### A.5 Header / log strings

[`pdgui_theme.h:9, 33, 118`](../../port/include/pdgui_theme.h) docblocks; [`pdgui_theme.cpp:505, 1717, 1729, 2162, 2565, 2725, 2748, 3192, 3205, 3215`](../../port/fast3d/pdgui_theme.cpp) comments / log-format strings.

### A.6 Asymmetry observed (no behavioural change)

- 14 textures written by extraction (`k_Extracts[]` includes `ui_stars`), but only 13 registered as `ASSET_UI` (`k_UiTextures[]` omits `ui_stars`).
- `ui_stars.tga` is generated for completeness but not surfaced through `pdguiThemeGetTexture`.  Pre-existing.  Slice 13 preserves the asymmetry; surfacing `ui_stars` is a separate decision belonging to the theme renderer.

---

## B. Decisions

| # | Decision |
|---|---|
| B.1 | Destination is `data/ui/textures/` (BYOR tier).  Per Mike's corrected Slice 13 directive (2026-05-02 PM): UI chrome textures are extracted from the user-supplied ROM at first launch, never ship with the project, and therefore live in the BYOR `data/` tier alongside per-romid segments populated by `romExtractAllSegments` (Pass A.2 / Slices 2/5/6/8/11).  Aligns with the original rom-extraction-audit-2026-04-30 recommendation. |
| B.2 | No `.pdui` archive in this slice.  Loose TGA + PNG + 9-slice JSON files under `data/ui/textures/`.  The `.pdXXX` taxonomy work (rom-extraction-audit Section 3) remains a separate later track that may package these as `data/ui/pd-original.pdui` once the archive format ships. |
| B.3 | Drop the auto-written `mods/base-ui/mod.json`.  data/ is not a mod tier; the catalog ID -> path mapping in `k_UiTextures[]` is the single source of truth.  Mod-manager scanning of `mods/base-ui/` is not part of the texture pipeline. |
| B.4 | No physical file move.  `mods/base-ui/textures/` does not exist on a clean checkout (extraction creates it on first launch); on a populated checkout, the new code writes to `data/ui/textures/` and the legacy path becomes inert.  Existing users will trigger one extra extraction on first post-Slice-13 launch.  Acceptable. |
| B.5 | Preserve the procedural fallback chain.  `pdguiThemeLateInit` still falls back to procedural generation when on-disk TGAs are missing AND the extraction pass has not yet populated them.  The extraction pass is the bootstrap; procedural fills the gap during the same boot. |
| B.6 | "No ROM-direct fallback for UI chrome" verified: the runtime `pdguiThemeLateInit` only reads TGAs via `s_loadTgaTexture` (disk read).  The only ROM access is in `pdguiThemeExtractRomTextures` which writes to disk and is the extraction step, not a runtime fallback.  Boot ordering: extraction runs at frame 0 if any TGAs missing, then theme reload picks up the new files. |
| B.7 | Test pin via static-text grep.  `tests/test_uichrome_paths_pin.cpp` locks the new `data/ui/textures/` path string and forbids reintroduction of EITHER `mods/base-ui/textures/` (legacy mod tier) OR `base/ui/textures/` (interim misclassification).  Mirrors the heads / bodies / arenas grep-guard discipline. |

---

## C. Migration delta (single commit)

| File | Change |
|---|---|
| `port/fast3d/pdgui_theme.cpp` | All `mods/base-ui/textures/` -> `data/ui/textures/` (29 hits in path strings).  `fsCreateDir` triple changed to `data/`, `data/ui/`, `data/ui/textures/` (corrected from the initial `base/` misclassification).  `mods/base-ui/mod.json` autogen block deleted (~50 lines).  Comments + log messages updated; `mod_path` field renamed to `disk_path` in `k_UiTextures[]` for clarity.  Two em-dashes replaced with hyphens in log strings I touched. |
| `port/include/pdgui_theme.h` | Three docblock comments updated to reflect `data/ui/textures/` path. |
| `tests/test_uichrome_paths_pin.cpp` | New file.  Static-text pins for catalog paths + extraction dest + dir creation + absence of `mods/base-ui/` patterns + absence of `mod.json` autogen. |
| `context/audits/catalog-phase3-passb-slice13-uichrome-2026-05-02.md` | This audit. |
| `CMakeLists.txt` | Add the new test source to `SRC_TESTS`. |

---

## D. Boundary checks

- **Mod system interaction.**  `mods/base-ui.pdmod` (the legacy archive) and `mods/base-ui.legacy_backup/` (legacy directory) remain untouched on disk; they hold themes (`.json`) that the mod manager scans independently.  This slice does not touch the themes path.  The `mods/base-ui/` directory previously created by extraction is no longer auto-created; that directory was a side-channel never required by the mod manager.
- **Server build.**  `pdgui_theme.cpp` is `pd`-only (auto-discovered under `port/fast3d/` for the client target).  `pd-server` does not link the theme module and is unaffected.
- **`fsCreateDir` semantics.**  `fsCreateDir("data/ui/textures")` succeeds when intermediate `data/` and `data/ui/` already exist.  We create all three explicitly (mirrors the legacy 3-call pattern) so the order is deterministic regardless of fsCreateDir intermediate-creation behaviour.
- **VFS modvfs.c interaction.**  `port/src/modvfs.c` intercepts `fsFileLoad` for paths matching mod archive contents.  Loose `data/ui/textures/*.tga` paths do not match any archive prefix, so VFS pass-through resolves to the disk file directly.  No VFS code change required.

---

## E. Stop conditions checked

- Catalog ID format unchanged (`base:ui_*`).  No mod / save / wire surface change.
- Procedural-fallback chain preserved; first-launch behaviour identical (extraction populates disk, theme reloads, on-disk TGAs win).
- No protocol bump, no save migration, no test surface deletion.

Phase 2 commit follows immediately.
