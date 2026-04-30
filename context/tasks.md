# Tasks

> Razor-thin punch list of what is open right now. Per-slice "Done this slice" narratives belong in [session-log.md](session-log.md), not here. Completed lanes get archived per [retention.md](retention.md).
>
> When in doubt: shorter is better. If a lane has stalled for > 14 days, ask whether it is genuinely active.

---

## Current critical path (per Mike, 2026-04-30)

The queue has three lanes after the context rebuild lands. Lane order is sequential; do not start the next until the previous is at a stable stopping point.

### 1. Catalog - Weapons F11-F13 data move

**Status**: F1-F10 shipped (manager skeleton, accessor migration, EYESPY mutators, default fallbacks, ext.weapon I.2 drop, .pdbase loader scaffold). F11+ data move is the immediate priority.

**Scope**:

- F11 implements `loaderPdbaseScan` filesystem walk and `loaderPdbaseBuildWeaponManager` decoder.
- Move the 86 `invitem_*` struct definitions from [src/game/invitems.c:5700+](../../src/game/invitems.c) to a `base/weapons.pdbase` archive.
- Populate `ext.weapon.pdbase_path / pdbase_offset / pdbase_size` on catalog entries.
- Extend `catalogManagerGetWeaponByIndex` to serve from manager-owned data instead of `g_Weapons[]`.
- F12 retires Layer A: `g_AibotWeaponPreferences[]`, `invaimsettings_default`, `invnoisesettings_silent`, the const-cast violation at [src/game/game_0b0fd0.c:120](../../src/game/game_0b0fd0.c:120).
- F13 replaces shape-only [tests/test_loader_pdbase_scan.cpp](../../tests/test_loader_pdbase_scan.cpp) with behavioral coverage; round-trip test for `catalogManagerGetWeaponById("base:falcon2")`; zero-runtime_index loud-fail.

**Companion cleanups within F11-F13**:

- Fix [tests/test_catalog_mgr_weapons_api.cpp:15](../../tests/test_catalog_mgr_weapons_api.cpp:15) stale comment (says 89, asserts 86).
- Reconcile [port/include/loader_pdbase.h:49-51](../../port/include/loader_pdbase.h:49) header that lies to its `.c` body.

**Design ref**: `designs/catalog/catalog-full-pipeline-weapons.md`.
**Pillar ref**: [pillars/catalog.md](pillars/catalog.md).

### 2. Catalog - Gate 3 Migration

**Status**: queued behind F11-F13. Applies the proven Manager + .pdbase pattern from weapons to other asset types.

**Scope**: per asset type, define a `catalog_mgr_<type>.c` + `<type>.pdbase` archive. Order by impact / risk:

- Heads + bodies (high-traffic, high-risk; selectors and avatars).
- Arenas (medium; static metadata).
- Audio (medium; ASSET_AUDIO already has runtime activation; data move follows).
- Scenarios / game modes.
- Bot profiles + bot variants.

For each: design pass + audit + migrate + retire Layer A.

**Pillar ref**: [pillars/catalog.md](pillars/catalog.md).

### 3. Input - Controller Support (Branch 2 Cohorts 5-8)

**Status**: queued after Catalog Gate 3. Per [designs/input/input-universality-and-transitions.md](designs/input/input-universality-and-transitions.md), Cohorts 1-4 shipped (layer types + scene events, layer push/pop, IMC ownership migration, per-player cutscene state). Cohorts 5-8 cover full controller support, menu graph completion, remaining transitional shim retirement.

**Scope** (high-level; design doc has the detail):

- Wire `g_LayerGameplay.imc` and `g_LayerMenu.imc` so the layer stack owns those lifecycles.
- Migrate the 10 raw F-key handlers in [pdgui_backend.cpp:1288-1379](../../port/fast3d/pdgui_backend.cpp:1288) into the actionmap.
- Migrate gfx_sdl2.cpp Alt+Enter / F10 / backquote raw hotkeys.
- Audit and retire `inputKeyPressed()` controller polling in [input.c:1075-1099](../../port/src/input.c:1075).
- Replace `WantCaptureKeyboard` gate in [pdgui_spectator.cpp:162](../../port/fast3d/pdgui_spectator.cpp:162) with `gameplayInputSuppressed()`.
- Static test linking right-stick scroll runtime to its spec constants.
- Fix observer push/pop asymmetry in [inputlayer.c:157-183](../../port/src/inputlayer.c:157).
- Menu graph completion: 127 raw `menuPushDialog / menuPopDialog` calls in 15 files migrate to `menuGraphFire*`. Add a graph-completeness test.

**Pillar refs**: [pillars/input.md](pillars/input.md), [pillars/menus.md](pillars/menus.md).

---

## Active investigations / unresolved bugs

Open per [bugs.md](bugs.md). Latest entries (B-280 through B-290) are all FIXED-PENDING-BUILD as of S575 (2026-04-28); promote to FIXED with commit SHA when the build verification clears.

Older still-open bugs:

- **B-179, B-182, B-183**: torn-modeldef class. Critical/high crash + geometry issues. Modeldef defensive guards landed (S312); root-cause fix not yet identified. Track in `bugs.md`.

---

## Build infrastructure follow-ups

Per [audits/infrastructure-pillars-status-2026-04-27.md](audits/infrastructure-pillars-status-2026-04-27.md) Section 7:

- **Investigate the `pd_headers` 600s stall.** Likely Python tool or Git probe blocking under the wrapper's launcher policy. Surface real progress (which generator script is running) in heartbeat.
- **Factor SP-9 truncation guard** out of inline `build-headless.ps1:763-804` into a callable function in `version-util.ps1` (or new `_build-safety.ps1`) so it can be unit-tested.
- **Centralize worktree-redirect** logic into one helper called by every script (currently duplicated in 3 places).
- Add a smoke test that the smart-clean heuristic correctly forces a clean on generator change.

These do not block the critical path; pick up when context allows.

---

## Test framework follow-ups

Per [pillars/tests.md](pillars/tests.md) Known Gaps:

- CI-time drift check for each `*_pure.c` (currently hand-synced with manual `@SYNC` comments).
- F10 `.pdbase` test moves from shape-only to behavioral when F11 lands (covered by lane 1 above).
- Pin right-stick scroll runtime to its spec constants (covered by lane 3 above).
- Domain coverage extension: forge serializer round-trip, social public-mod registry, updater HTTP error paths, theme decode, menu graph completeness.

---

## Modding follow-ups

Per [pillars/modding.md](pillars/modding.md) Known Gaps:

- **`.pdmod` cannot deliver INI-based components.** Extend `modmgrLoadMod` for archive mods to drive the INI scanner over archive contents (new helper `assetCatalogScanComponentsFromArchive`). Architectural gap (folder vs archive parity).
- **SHA-256 mismatch between archive and folder mods.** Unify both digests to `mod.json` bytes only.
- **NetDistrib hot-register skips `g_ModRegistry`.** Call `modmgrRescanDirectory` after `SVC_DISTRIB_END` so distributed mods appear in Mod Manager UI without a relaunch.
- **Audit and remove or wire `modmgrWriteManifest / modmgrReadManifest`** (dead legacy serializer at [modmgr.c:2456-2554](../../port/src/modmgr.c:2456)).
- Replace `manifest_pure.c` hand-sync with compile-boundary approach.

These are post-Catalog-Gate-3 candidates.

---

## Connectivity follow-ups

Per [pillars/connectivity.md](pillars/connectivity.md) Known Gaps. Not on the immediate critical path but tracked for the connectivity-focused phase that follows controller support:

- Wire `p2pIceAddPeerCandidate` from presence/invite delivery.
- Publish local STUN reflexive in presence pings.
- Replace placeholder kbps in `groupSessionRecomputeAuthority` with real measurement.
- Extend UPnP to map p2p probe ports (27102, 27103) in addition to ENet port.
- Add a configurable public TURN fallback when `bestRelayCand()` is NULL.
- Detect Opus via CMake `find_package` and auto-set `HAVE_OPUS`.
- Unify the hole-punch flow: `netStartClientWithHolePunch` should consume `p2pPairGetEndpoint`.

---

## Save / wire format follow-ups

Per [pillars/save-wire-format.md](pillars/save-wire-format.md) Known Gaps:

- Replace `tests/savebuffer_pure.c` and `tests/manifest_pure.c` hand-sync with compile-boundary approach.
- Add chained-migration behavioral test (set up `SAVE_VERSION = 4` fixture, register dummy 1->2, 2->3, 3->4 migrations, assert each ran in order).
- Co-locate version constants with cross-reference comment headers in each version-bearing file.

---

## Process / context follow-ups

- Phase 3B of the rebuild: move old context to `_old/` after Mike approves the readiness brief at Step 6. See [audits/context-rebuild-proposal-2026-04-30.md](audits/context-rebuild-proposal-2026-04-30.md) Section 6.
- Repoint `CLAUDE.md`, `AGENTS.md`, `.cursor/skills/context-session-start/` references to the new structure.
- Self-archive the cleanup plan + rebuild proposal once the rebuild is fully landed.

---

## Where to look

- For per-pillar live state and pillar-specific in-flight work: [pillars/](pillars/).
- For per-pillar active design references: [designs/](designs/).
- For active invariants that must be respected: [constraints.md](constraints.md).
- For build verify, git safety, isolated builds: [procedures.md](procedures.md).
- For the longer-term roadmap by gate / pillar / decision: [roadmap.md](roadmap.md).
