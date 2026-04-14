# Manifest Gap Report — 2026-04-13

## Summary

Two distinct classes of manifest gaps exist, both rooted in the same architectural problem: the manifest system was designed for MP match startup and never extended to cover non-match stage types.

---

## Gap 1: Menu/Title Stage — No Manifest At All

**Affected stage**: `STAGE_TITLE` (0x5a)

**Root cause**: `mainChangeToStage()` (pdmain.c:760) gates manifest building behind `STAGE_IS_GAMEPLAY(stagenum)`. STAGE_TITLE is classified as `STAGE_IS_SYSTEM()` (constants.h:4113), so the `else` branch simply calls `manifestClear(&g_ClientManifest)` — no `manifestBuildMission()` and no `manifestSPTransition()`.

**Impact**: The Skin Editor (pdgui_skin_editor.cpp) runs on the title stage. It calls `assetCatalogIterateByType(ASSET_BODY)` to enumerate all characters, then calls `pdguiCharPreviewRequest(head_id, body_id)` with catalog string IDs. The preview pipeline resolves these to `mp_index` via `assetCatalogResolve()` and packs them into `menumodel.newparams` for the GBI render.

For **bundled base-game models**, loading works regardless of manifest state because `bodyLoad()` → `catalogGetBodyModeldef(bodynum)` → `modeldefLoadToNew(filenum)` loads directly from ROM data. The manifest is irrelevant for bundled assets — they're always ROM-resident.

For **mod character models**, the manifest lifecycle system (catalogLoadAsset/catalogUnloadAsset) governs whether mod files are loaded to memory. Without a manifest on the title stage, mod character models loaded during a previous match may have been unloaded when transitioning back to menu, and no mechanism reloads them for the Skin Editor.

**Current mitigation**: None. Mod characters are silently missing from the Skin Editor preview. Base-game characters work by accident (ROM-resident).

**Severity**: HIGH for mod workflow. The Skin Editor is the primary use case for character skin modding, and it cannot preview mod characters.

---

## Gap 2: SP Manifest Pre-Scan Timing — g_StageSetup.props NULL

**Affected stages**: ALL gameplay stages via SP path, but most critically STAGE_CITRAINING (0x26) — the CI intro cutscene.

**Root cause**: Call ordering violation in `mainChangeToStage()`:

1. Line 764: `manifestSPTransition(stagenum)` — builds the SP manifest
2. Line 770: `g_MainChangeToStageNum = stagenum` — deferred stage load begins
3. Later: `lvInit()` → `setupLoadFiles()` populates `g_StageSetup.props`

`manifestBuildMission()` (netmanifest.c:877) checks `if (g_StageSetup.props)` — but it's always NULL at call time because setup files haven't loaded yet. The code's own comment (line 875) states this: "manifestSPTransition() should be called after setupLoadFiles() returns so that both passes are meaningful."

**What the manifest captures pre-load** (with g_StageSetup.props == NULL):
- Stage map entry (from catalog by stagenum)
- Joanna Dark body/head (hardcoded `"base:dark_combat"` / `"base:head_dark_combat"`)
- Counter-op body/head (if antiplayernum >= 0 — not at pre-load time)

**What it misses**:
- All CHR entries from the setup spawn list (NPC characters, enemies, allies)
- All prop model entries (OBJTYPE_DOOR, OBJTYPE_WEAPON, etc.)
- All counter-op body/head (g_Vars.antiplayernum not set at pre-load time)

**Safety net**: `manifestEnsureLoaded()` in `bodyAllocateChr()` / `bodyAllocateModel()` / `setuputils.c` catches late-added assets at spawn time. But this is reactive — it fires per-spawn, not preventive. For bundled assets this works (ROM-resident, no actual load needed). For mod assets, this could race with the render pipeline.

**B-118 specific**: CI Training intro cutscene spawns ~56 characters not in the manifest. All are base-game (bundled), so they load from ROM via `manifestEnsureLoaded()`. The crash is likely caused by the volume of late-adds happening in a single frame during cutscene init, combined with the manifest grow/realloc overhead. The pre-scan would spread this work across the diff phase.

---

## Gap 3: Cutscene Cinema Models — No Dedicated Scan

**Affected stages**: Any stage with cinematics (intro, outro, inter-mission cutscenes)

**Root cause**: `manifestBuildMission()` only scans `g_StageSetup.props` for CHR/prop entries. Cinema scripts can spawn characters that aren't in the setup list — they're created dynamically by AI commands (AICMD_CHR_SPAWN, etc.) or by cinema script functions.

**Impact**: Some cinema characters are never in the manifest at all, caught only by `manifestEnsureLoaded()` at spawn time.

**Current mitigation**: `manifestEnsureLoaded()` safety net.

---

## Stage Type Coverage Matrix

| Stage Type | Example | Manifest Builder | Gap |
|---|---|---|---|
| **MP arena** | STAGE_MP_FELICITY | `manifestBuild()` / `manifestBuildForHost()` | None — players + bots + weapons covered |
| **SP mission** | STAGE_DEFECTION (0x00) | `manifestBuildMission()` via `manifestSPTransition()` | props scan NULL (timing); counter-op body at wrong time |
| **CI Training** | STAGE_CITRAINING (0x26) | `manifestBuildMission()` via `manifestSPTransition()` | 56+ characters missed (props NULL). B-118. |
| **Title/Menu** | STAGE_TITLE (0x5a) | None — `manifestClear()` only | No manifest at all. Skin Editor broken for mods. |
| **Credits** | STAGE_CREDITS (0x5c) | None — `manifestClear()` only | No characters — not an issue |
| **Boot pak menu** | STAGE_BOOTPAKMENU (0x5b) | None | Not relevant |
| **4MB menu** | STAGE_4MBMENU (0x5d) | None | Removed constraint (no 4MB mode) |
| **Challenges** | STAGE_DUEL etc. | `manifestBuildMission()` via `manifestSPTransition()` | Same timing gap as SP missions |

---

## Fix Plan

### Fix 1: Menu-Stage Manifest — `manifestBuildForMenu()`

Add a new builder function `manifestBuildForMenu()` that pre-populates a manifest with **all registered ASSET_BODY and ASSET_HEAD entries** from the catalog. The title stage is the hub — it must be able to render any character the user might select in any menu (Skin Editor, Agent Select, Modding Hub, Bot Setup).

Call `manifestBuildForMenu()` from the `!STAGE_IS_GAMEPLAY` branch in `mainChangeToStage()`, replacing the bare `manifestClear(&g_ClientManifest)`.

### Fix 2: SP Manifest Timing — Post-Load Re-Scan

Move the setup-props scan phase of `manifestBuildMission()` to run AFTER `setupLoadFiles()`. Two options:

**Option A**: Split `manifestBuildMission()` into two phases. Phase 1 (pre-load) adds stage + Joanna. Phase 2 (post-load) scans `g_StageSetup.props` and adds CHR/prop entries. Call Phase 2 from `lvInit()` after `setupLoadFiles()`.

**Option B**: Move the entire `manifestSPTransition()` call from `mainChangeToStage()` to `lvInit()` after `setupLoadFiles()`. Simpler but changes the lifecycle contract.

**Recommended**: Option A — keeps the pre-load phase for early diff-based unloading of the prior stage's mod assets, while adding a post-load scan to catch all setup characters.
