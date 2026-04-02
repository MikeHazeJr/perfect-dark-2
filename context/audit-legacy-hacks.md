# Legacy Hacks & Infrastructure Replacement Audit

> **Generated**: 2026-04-02, Session S119+
> **Scope**: Read-only audit of `src/game/`, `src/lib/`, `port/src/`, `port/fast3d/`, `port/include/`
> **Purpose**: Find legacy patterns, workarounds, and special-case code replaceable by our modern infrastructure (catalog, ImGui, component mods). This is NOT a catalog-ID compliance check (see `audit-catalog-id-compliance.md`).

---

## Executive Summary

**35+ actionable findings** across 10 legacy pattern categories. The codebase retains significant technical debt from N64 decompilation: hardcoded asset conditionals, per-body special-case logic, and magic number asset IDs. The modern Asset Catalog is in place and functional — the remaining work is data-driven refactoring of switch statements and constants.

**Critical blockers for mod support:**
- `MPBODY_*`/`MPHEAD_*` integer defaults not going through catalog (related to invisible bot bugs)
- Per-body special-case logic (`BODY_CHICROB` beams, `BODY_DRCAROLL` sizing) not parameterizable
- Stage-specific behavior (language banks, surface types) hardcoded in switch statements — blocks stage mods

---

## Category 1: Hardcoded Asset Conditionals

Switch/if statements on `bodynum`, `headnum`, `stagenum` making gameplay behavior decisions.

### C1-1 — `bodyGetRace()` — Race lookup by bodynum

**File**: `src/game/body.c:143–158`
**Code**:
```c
u32 bodyGetRace(s32 bodynum)
{
    switch (bodynum) {
    case BODY_SKEDAR:
    case BODY_MINISKEDAR:
    case BODY_SKEDARKING:
        return RACE_SKEDAR;
    case BODY_DRCAROLL:
        return RACE_DRCAROLL;
    // ... more cases
    }
    return RACE_HUMAN;
}
```
**Modern solution**: Store `race` as a metadata field in catalog body entry. Replace with `catalogGetSafeBody(bodynum)->race`.
**Effort**: Small
**Priority**: Medium

---

### C1-2 — `langGetLangBankIndexFromStagenum()` — Language bank by stage (82-case switch)

**File**: `src/game/lang.c:144–230`
**Code**:
```c
u32 langGetLangBankIndexFromStagenum(s32 stagenum)
{
    u32 bank;
    switch (stagenum) {
    case STAGE_PELAGIC:       bank = LANGBANK_DAM; break;
    case STAGE_EXTRACTION:    bank = LANGBANK_ARK; break;
    // ... 80 more cases covering all stages
    }
    return bank;
}
```
**Impact**: 82-case function is brittle. Adding a new stage mod requires editing this switch statement. Language bank mapping belongs in catalog stage metadata.
**Modern solution**: Catalog stage entry includes `language_bank_id` field. Replace with `catalogGetStage(stagenum)->language_bank_id`.
**Effort**: Small
**Priority**: **High** — actively blocks stage mods without code edits

---

### C1-3 — `chrGetBloodColour()` — Blood color by bodynum

**File**: `src/game/chr.c:3253–3300+`
**Code**:
```c
void chrGetBloodColour(s16 bodynum, u8 *colour1, u32 *colour2)
{
    switch (bodynum) {
    case BODY_ELVIS1:
    case BODY_THEKING:
    case BODY_ELVISWAISTCOAT:
        if (colour1) { colour1[0] = 10; colour1[1] = 10; colour1[2] = 10; }
        // ... many per-body color customizations
    }
}
```
**Modern solution**: Store `blood_color_1` and `blood_color_2` in catalog body metadata.
**Effort**: Small
**Priority**: Medium (cosmetic, but clean metadata candidate)

---

### C1-4 — `ciIsChrBioUnlocked()` — Bio unlock by bodynum

**File**: `src/game/training.c:2315–2337`
**Code**:
```c
bool ciIsChrBioUnlocked(u32 bodynum)
{
    switch (bodynum) {
    case BODY_DARK_COMBAT:
    case BODY_CARRINGTON:
        return true;
    case BODY_CASSANDRA:
        return ciIsStageComplete(SOLOSTAGEINDEX_DEFECTION);
    case BODY_DRCAROLL:
        return ciIsStageComplete(SOLOSTAGEINDEX_INVESTIGATION);
    // ... 6 more cases, each with a different stage unlock condition
    }
    return false;
}
```
**Impact**: Per-body unlock conditions are hardcoded. Mods cannot change unlock prerequisites without editing this function.
**Modern solution**: Catalog body entry includes `required_stage_for_unlock` field (or NULL if always unlocked). Replace with metadata lookup.
**Effort**: Small
**Priority**: **High** — blocks custom character unlock logic

---

### C1-5 — `ciGetChrBioByBodynum()` — Static bio array indexed by bodynum

**File**: `src/game/training.c:2342–2370+`
**Code**:
```c
struct chrbio *ciGetChrBioByBodynum(u32 bodynum)
{
    static struct chrbio bios[] = {
        /*0*/ { L_DISH_125, L_DISH_126, ... }, // Joanna Dark
        /*1*/ { L_DISH_129, L_DISH_130, ... }, // Jonathan
        /*2*/ { L_DISH_133, L_DISH_134, ... }, // Daniel Carrington
        // ... hardcoded index-to-bio mapping
    };
    // implicitly assumes bodynum index == bio array index
}
```
**Impact**: Bio data is statically indexed. Adding new bodies requires re-indexing this entire array and maintaining the `bodynum→array-index` correspondence.
**Modern solution**: Catalog body entry stores `bio_name_id`, `bio_race_id`, `bio_age_id`, `bio_profile_id` (language string IDs). Lookup via catalog.
**Effort**: Medium
**Priority**: **High** — directly blocks dynamic character additions

---

### C1-6 — `mplayer.c` — Texture surface-type by stagenum

**File**: `src/game/mplayer/mplayer.c:305–340+`
**Code**:
```c
switch (stagenum) {
case STAGE_TEST_SILO:
case STAGE_TEST_LAM:
case STAGE_TEST_MP8:
    g_SurfaceType = SURFACETYPE_XXXX;
    break;
    // ... many cases
}
```
**Modern solution**: Catalog stage entry includes `default_surface_type`. Replace entire switch with `g_SurfaceType = catalogGetStage(stagenum)->default_surface_type`.
**Effort**: Small
**Priority**: Medium

---

### C1-7 — `body.c:288` — Skedar height variation locked to `BODY_SKEDAR`

**File**: `src/game/body.c:287–292`
**Code**:
```c
} else if (bodymodeldef->skel == &g_SkelSkedar) {
    if (g_HeadsAndBodies[bodynum].canvaryheight && varyheight && bodynum == BODY_SKEDAR) {
        f32 frac = RANDOMFRAC();
        scale *= 2.0f * (0.1f * frac) - 0.1f + 0.75f;
    }
```
**Impact**: Height variation checks `bodynum == BODY_SKEDAR` explicitly. The `canvaryheight` flag (already in `g_HeadsAndBodies`) should be the only gate — the additional bodynum check is redundant and prevents mods using this skeleton from getting height variation.
**Modern solution**: Remove `bodynum == BODY_SKEDAR` check; rely solely on `canvaryheight` flag.
**Effort**: Trivial
**Priority**: Low

---

## Category 2: Magic Numbers as Asset Identifiers

Literal integers in array indexing or conditionals corresponding to ROM asset IDs.

### M2-1 — `mplayer.c:765–778` — Hardcoded `MPBODY_*`/`MPHEAD_*` defaults per player slot

**File**: `src/game/mplayer/mplayer.c:765–778`
**Code**:
```c
switch (playernum) {
case 0:
default:
    g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_DARK_COMBAT;
    break;
case 1:
    g_PlayerConfigsArray[playernum].base.mpbodynum = MPBODY_CASSANDRA;
    break;
// ... more hardcoded defaults
}
```
**Impact**: Defaults use raw integer constants, not catalog resolution. If catalog mapping shifts (mod replaces asset), these defaults silently point to wrong asset.
**Modern solution**: Define defaults as catalog ID strings (`"base:dark_combat"`, `"base:cassandra"`). Resolve to `mpbodynum` at runtime via catalog.
**Effort**: Medium
**Priority**: **High** — related to invisible bot bugs when assets are modded

---

### M2-2 — `mainmenu.c:3933–3935` — Hardcoded magic index `0x3e` for menu character preset

**File**: `src/game/mainmenu.c:3931–3936`
**Code**:
```c
if ((u32)wantindex == 0x3e) {
    g_Menus[g_MpPlayerNum].menumodel.newparams =
        MENUMODELPARAMS_SET_MP_HEADBODY(MPHEAD_DARK_FROCK, MPBODY_DARKLAB);
} else {
    g_Menus[g_MpPlayerNum].menumodel.newparams =
        MENUMODELPARAMS_SET_MP_HEADBODY(MPHEAD_DARK_COMBAT, MPBODY_DARK_AF1);
}
```
**Impact**: Specific menu indices (`0x3e`) hardcoded to specific asset combinations.
**Modern solution**: Preset definitions stored in catalog or ImGui menu config, not as magic hex numbers in game logic.
**Effort**: Medium
**Priority**: Medium (menu cosmetic, but blocks menu customization)

---

### M2-3 — `mpconfigs.c` — All bot default configs hardcoded to `MPBODY_DARK_COMBAT`

**File**: `src/game/mpconfigs.c:25–150+`
**Code**:
```c
// Repeated 40+ times:
{ BOTTYPE_GENERAL, MPHEAD_DARK_COMBAT, MPBODY_DARK_COMBAT, MPTEAM_1, { ... } },
{ BOTTYPE_GENERAL, MPHEAD_DARK_COMBAT, MPBODY_DARK_COMBAT, MPTEAM_1, { ... } },
```
**Impact**: Default bot configs all point to same hardcoded body/head pair. Inflexible; breaks with asset modding.
**Modern solution**: Bot configs reference catalog ID strings with fallback resolved at load time.
**Effort**: Small–Medium
**Priority**: **High** — blocks dynamic bot customization

---

## Category 3: Duplicated Lookup Logic

Manual iteration to find assets by name/property when catalog queries could replace this.

### D3-1 — Scattered string→stage / string→body loops (pattern, not exhaustively located)

**Pattern** (likely present in UI code):
```c
for (int i = 0; i < g_NumStages; i++) {
    if (strcmp(g_Stages[i].name, targetName) == 0) {
        stagenum = g_Stages[i].id;
        break;
    }
}
```
**Modern solution**: Use `assetCatalogResolve("base:defection")` to get stage directly by catalog ID string. The catalog already provides this.
**Effort**: Small per occurrence
**Priority**: Low (if it exists; may already be using catalog API — verify before acting)

---

## Category 4: Legacy Translation Layers

Conversion functions between index spaces.

### T4-1 — `mpGetBodyId()`, `mpGetHeadId()` — Integer-domain index conversions

**File**: Likely `src/game/mplayer/mplayer.c` or `port/src/modmgr.c`
**Pattern**:
```c
s32 mpGetBodyId(u8 bodynum)  { return modmgrGetBody(bodynum)->bodynum; }
s32 mpGetHeadId(u8 headnum)  { return modmgrGetHead(headnum)->headnum; }
```
**Impact**: Accept/return raw integers, not catalog IDs. These functions maintain the integer-domain assumption and tempt external code to use raw indices.
**Modern solution**: Phase out. Use `catalogGetSafeBody(bodynum)->catalogId` to get the string ID; reverse with `assetCatalogResolve(id)->runtime_index` only when legacy engine API requires it.
**Effort**: Medium (ripple through mplayer subsystem)
**Priority**: Medium — will be partially resolved by M2-1 fix

---

### T4-2 — `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx` — Exposed internals

**File**: `port/src/assetcatalog_api.c:371–404`
**Impact**: Internal conversion functions exist in the public API, tempting external code to depend on integer index conversions rather than catalog ID strings.
**Modern solution**: Restrict to catalog-internal use only. Document in header that catalog ID strings are the universal currency.
**Effort**: Trivial (header annotation + API review)
**Priority**: Low

---

## Category 5: Hardcoded ROM Assumptions

Code that assumes assets come from ROM or uses fixed file IDs.

### R5-1 — `floatToN64Depth()` — N64 16-bit Z-buffer format conversion

**File**: `src/game/artifact.c:109–123, 440`
**Code**:
```c
u16 floatToN64Depth(f32 arg0)
{
    /**
     * Method to convert a 32 bit floating point depth value to the
     * unsigned 16 bit integer format used by the zbuffer on the N64.
     * The argument arg0 represents z normalized to [0, 1] * 32704.0f.
     */
    // ...
}
// Used at:
artifact->expecteddepth = floatToN64Depth(f0) >> 2;
```
**Impact**: Converts to N64 16-bit depth format. The `>> 2` is an artifact preservation quirk ("the original game performs artifact depth comparison using the N64 depth values divided by 4"). On PC this works but is conceptually unnecessary.
**Modern solution**: Replace with platform-agnostic float depth comparison. Or document clearly as artifact preservation (the comparison is intentionally matching original behavior).
**Effort**: Small
**Priority**: Low (works correctly; no bug)

---

### R5-2 — `bgGarbageCollectRooms()` — N64 stub function

**File**: `src/game/bg.c:3066–3069`
**Code**:
```c
void bgGarbageCollectRooms(s32 bytesneeded, bool desparate)
{
    // N64-only: rooms are allocated from heap on PC, no garbage collection needed
}
```
**Impact**: Empty stub. Call-sites invoke it unnecessarily.
**Modern solution**: Remove function and all call-sites.
**Effort**: Trivial
**Priority**: Low (dead code)

---

### R5-3 — `filenum` as primary model identity in `body.c:163–167`

**File**: `src/game/body.c:163–167`
**Code**:
```c
s32 filenum; /* SA-5a: catalog-resolved filenum */
if (!g_HeadsAndBodies[bodynum].modeldef) {
    filenum = catalogGetBodyFilenumByIndex(bodynum);
    g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNew(filenum);
```
**Impact**: `filenum` (ROM concept) is still the handoff point to the model loader. Catalog resolves it, so this is partially modernized, but the `filenum` integer still flows through.
**Modern solution**: Longer term — model loader should accept catalog ID string directly rather than ROM file number. Already a known migration target per SA-5.
**Effort**: Medium
**Priority**: Medium (SA-5 partially addressed this; remainder is architectural)

---

## Category 6: N64 Rendering Workarounds

Hacks and comments in `port/fast3d/` from N64-specific behavior.

### N6-1 — `gfx_opengl.cpp:320–321` — Z depth clamp hack for missing GL extension

**File**: `port/fast3d/gfx_opengl.cpp:319–322`
**Code**:
```c
if (!GLAD_GL_ARB_depth_clamp) {
    // HACK: workaround for no GL_DEPTH_CLAMP
    append_line(vs_buf, &vs_len, "    gl_Position.z *= 0.3f;");
}
```
**Impact**: If `GL_ARB_depth_clamp` is unavailable, shader multiplies Z by 0.3. This is a GPU feature compatibility workaround, not N64-specific.
**Modern solution**: Not really replaceable with catalog infrastructure — this is a GPU driver concern. Document clearly. The 0.3f magic constant should at minimum be a named constant.
**Effort**: Trivial (documentation)
**Priority**: Low

---

### N6-2 — `gfx_pc.cpp:869–873` — 32-bit LODed texture byte count doubling

**File**: `port/fast3d/gfx_pc.cpp:869–873`
**Code**:
```c
if (siz == G_IM_SIZ_32b) {
    // HACK: fixup 32-bit LODed texture height
    loaded_texture.size_bytes <<= 1;
    loaded_texture.full_size_bytes <<= 1;
}
```
**Impact**: Reason for byte count doubling on 32-bit LOD textures is unexplained. May be an N64 GBI quirk or decompilation artifact.
**Modern solution**: Expand comment to explain the N64 GBI spec reason. If it's a decompilation artifact, verify it's still needed.
**Effort**: Trivial (documentation + verification)
**Priority**: Low

---

### N6-3 — `gfx_pc.cpp:1573–1577` — `G_CCMUX_LOD_FRACTION` eyeballed approximation

**File**: `port/fast3d/gfx_pc.cpp:1573–1577`
**Code**:
```c
case G_CCMUX_LOD_FRACTION: {
    if (rdp.other_mode_h & G_TL_LOD) {
        // HACK: very roughly eyeballed based on the carpets in Defection
        // this is actually supposed to be calculated per pixel
        const float distance_frac = std::max(0.f, std::min(w / 1024.f, 1.f));
```
**Impact**: LOD_FRACTION is approximated (`w / 1024.f`) and eyeballed to match carpet appearance in one specific stage. Fragile; different stages may look wrong.
**Modern solution**: Investigate N64 GBI LOD spec; implement correctly per-pixel, or document as a known approximation.
**Effort**: Medium (requires GBI spec research)
**Priority**: Low (visual quality issue; workaround acceptable for now)

---

### N6-4 — `gfx_pc.cpp:1679` — Viewport aspect ratio assumption

**File**: `port/fast3d/gfx_pc.cpp:1678–1681`
**Code**:
```c
static void gfx_adjust_viewport_or_scissor(XYWidthHeight* area, bool preserve_aspect = false) {
    // HACK: assume all target framebuffers have the same aspect
    area->width *= RATIO_X;
    area->x *= RATIO_X;
```
**Impact**: Assumes all framebuffers share the same aspect ratio (global `RATIO_X`). Breaks if multiple render targets with different aspects are used.
**Modern solution**: Pass aspect ratio as parameter instead of using a global. Clean up when adding split-screen or picture-in-picture rendering.
**Effort**: Small–Medium
**Priority**: Medium (potential rendering bug; not triggered yet)

---

### N6-5 — `gfx_pc.cpp:1797–1814` — Texture format correction hacks (CI vs RGBA)

**File**: `port/fast3d/gfx_pc.cpp:1807–1814`
**Code**:
```c
if (fmt == G_IM_FMT_RGBA && siz < G_IM_SIZ_16b) {
    // HACK: sometimes the game will submit G_IM_FMT_RGBA, G_IM_SIZ_8b/4b,
    // intending it to read as CI8/CI4 with RGBA16 palette
    fmt = G_IM_FMT_CI;
} else if (fmt == G_IM_FMT_IA && siz == G_IM_SIZ_32b) {
    // HACK: ... and sometimes it submits this, apparently intending it to be I8
    fmt = G_IM_FMT_I;
    siz = G_IM_SIZ_8b;
}
```
**Impact**: Game submits incorrect texture format metadata; renderer corrects on-the-fly. Likely decompilation artifacts or original game bugs in the display list.
**Modern solution**: Document as known format quirks in the ROM. Consider logging a warning when these corrections fire (to detect regressions or mod assets that have real format bugs).
**Effort**: Trivial (documentation + optional logging)
**Priority**: Low (workaround handles it correctly)

---

## Category 7: Memory Management Hacks

Fixed-size static arrays or pre-allocated buffers sized to specific ROM asset counts.

### M7-1 — `g_HeadsAndBodies[]` static array with hardcoded size

**File**: `src/include/data.h` (or equivalent declaration)
**Pattern**: `struct headbody g_HeadsAndBodies[NUM_HEADS_AND_BODIES]` where `NUM_HEADS_AND_BODIES` is the fixed ROM asset count.
**Impact**: Array is fixed size. Adding mod characters beyond the ROM count requires recompilation and risks OOB access if the catalog doesn't enforce the bound.
**Modern solution**: The catalog already dynamically manages entries. `g_HeadsAndBodies` could be converted to a dynamically-grown array (or the catalog's internal array used as the sole backing store). This is a significant refactor.
**Effort**: Large (array indexed by `bodynum` throughout game code)
**Priority**: Medium (blocked by catalog universality migration; low urgency until Phase A–C complete)

---

### M7-2 — Static per-stage lookup tables (language banks, surface types)

**Files**: `src/game/lang.c`, `src/game/mplayer/mplayer.c`
**Pattern**: Large switch statements or static arrays mapping `stagenum → property`.
**Impact**: Each table is a potential off-by-one or missing-case for mod stages.
**Modern solution**: Consolidate into catalog stage metadata. One pass at catalog registration time, no runtime lookups.
**Effort**: Small per table
**Priority**: Medium

---

## Category 8: Per-Asset Special-Case Game Logic

Per-asset special behavior embedded in game logic.

### S8-1 — `body.c:509–512` — `BODY_DRCAROLL` gets hardcoded height/radius

**File**: `src/game/body.c:509–512`
**Code**:
```c
if (bodynum == BODY_DRCAROLL) {
    chr->drcarollimage_left = 0;
    chr->drcarollimage_right = 0;
    chr->height = 185;
    chr->radius = 30;
}
```
**Impact**: Dr. Caroll's collision capsule is hardcoded. Cannot be parameterized per-mod.
**Modern solution**: Store `default_height`, `default_radius` in catalog body metadata.
**Effort**: Small
**Priority**: Medium

---

### S8-2 — `body.c:514–520` — `BODY_CHICROB` gets hardcoded beam slot allocation

**File**: `src/game/body.c:514–520`
**Code**:
```c
} else if (bodynum == BODY_CHICROB) {
    chr->unk348[0] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
    chr->unk348[1] = mempAlloc(sizeof(struct fireslotthing), MEMPOOL_STAGE);
    chr->unk348[0]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
    chr->unk348[1]->beam = mempAlloc(ALIGN16(sizeof(struct beam)), MEMPOOL_STAGE);
    chr->unk348[0]->beam->age = -1;
    chr->unk348[1]->beam->age = -1;
}
```
**Impact**: Robot character allocates special beam structures. Cannot be used for custom robots without code edit.
**Modern solution**: Add `has_beam_slots` capability flag in catalog body metadata. `bodyInitChar()` calls `bodyInitializeBeams(chr)` conditionally on this flag.
**Effort**: Medium
**Priority**: **High** — blocks custom robot creation

---

### S8-3 — `chr.c:3253+` — Per-body blood and visual effects variants

Related to C1-3. Blood color is one example; likely similar per-body variants exist for:
- Spark effects on hit
- Impact sound variants
- Death animation selection

**Modern solution**: All visual/audio effect properties belong in catalog body metadata. Single lookup at spawn time; no per-body switch cases in runtime code.
**Effort**: Medium (audit all chr.c switch statements for body-specific behavior)
**Priority**: Medium

---

## Category 9: Dead Code / Unreachable Paths

### D9-1 — `body.c:294` — Decompiler artifact `if(1);`

**File**: `src/game/body.c:294`
**Code**: `if (1);` (empty statement — does nothing)
**Modern solution**: Remove.
**Effort**: Trivial
**Priority**: Low

---

### D9-2 — `bg.c:476, 487, 962, 1179` — Multiple `if(1);` decompiler artifacts

**File**: `src/game/bg.c`
**Pattern**: Multiple instances of `if(1);` throughout file.
**Modern solution**: Bulk remove all instances.
**Effort**: Trivial
**Priority**: Low

---

### D9-3 — `weather.c:3111` — `if(0);` decompiler artifact

**File**: `src/game/weather.c:3111`
**Code**: `if (0);` inside a loop (dead statement).
**Modern solution**: Remove.
**Effort**: Trivial
**Priority**: Low

---

### D9-4 — `pak.c:2268–2274` — `if(false)` in version-conditional block

**File**: `src/game/pak.c:2268–2274`
**Code**:
```c
#if VERSION >= VERSION_NTSC_1_0
    if (offset + header.filelen >= g_Paks[device].pdnumbytes)
#else
    if (false)
#endif
```
**Impact**: Non-NTSC-1.0 branch is dead. The `#else if(false)` path can never execute on PC.
**Modern solution**: Remove the `#else` branch; leave only the active path with a comment noting it's unconditional on PC.
**Effort**: Trivial
**Priority**: Low

---

### D9-5 — `activemenu.c` — Multiple `IS4MB()` checks (always false on PC)

**File**: `src/game/activemenu.c:769, 782, 835, 887, 982, 1598` (and others throughout codebase)
**Pattern**: `if (IS4MB() && ...)` — `IS4MB()` is compile-time `0`.
**Impact**: These branches are dead. Compiler eliminates them, but they clutter the code.
**Modern solution**: Remove `IS4MB()` checks — constraint removed 2026-03-01.
**Effort**: Small (grep + bulk removal)
**Priority**: Low (cosmetic — compiler already eliminates)

---

### D9-6 — `bgGarbageCollectRooms()` call-sites — Dead function still invoked

**File**: `src/game/bg.c` and callers
**Impact**: Function is an empty stub (see R5-2). Its call-sites run but accomplish nothing.
**Modern solution**: Remove function and call-sites together.
**Effort**: Trivial
**Priority**: Low

---

## Category 10: Workaround Comments (Self-Documented Tech Debt)

### W10-1 — `audiomgr.c:262` — Hardcoded audio frame size hack

**File**: `src/lib/audiomgr.c:262–264`
**Code**:
```c
// HACK: only allow small frames if really needed
if (somevalue < 1100) {
    somevalue = 248;
```
**Impact**: Audio buffer sizing has a hardcoded hack. The comment is self-documenting but reason is unclear.
**Modern solution**: Investigate whether this can be replaced with proper dynamic audio buffer sizing. Likely safe to leave for now.
**Effort**: Medium
**Priority**: Low

---

### W10-2 — `frcommands.h:114` — Score limit workaround in firing range scripts

**File**: `src/include/firingrange/frcommands.h:114–115`
**Comment**:
```
Because the script data is made up of single bytes, it would normally be
impossible to set goal scores above 255. This command is a workaround to
allow higher goal scores.
```
**Impact**: Firing range script format limitation. Documented but unresolved.
**Modern solution**: Extend script format to support 16-bit or 32-bit integers for scores. Significant effort; low priority.
**Effort**: Large
**Priority**: Low

---

### W10-3 — `gfx_pc.cpp` — 5+ TODO/HACK comments in renderer

**File**: `port/fast3d/gfx_pc.cpp:502, 603, 1366, 1471, 1575`
**Examples**:
```c
// TODO discard if alpha is 0?
// TODO: this trips in some places with a garbage size in full_image_line_size_bytes
// TODO: fix this; for now just ignore smaller mips
// HACK: very roughly eyeballed based on the carpets in Defection
```
**Impact**: Multiple rendering edge cases unresolved (alpha discard, garbage texture sizes, incomplete mip handling, LOD approximation).
**Modern solution**: Address case-by-case. Alpha discard and mip handling are highest visual impact.
**Effort**: Medium (case-by-case)
**Priority**: Medium (some are visual quality issues)

---

### W10-4 — `gfx_pc.cpp:1797` — `OTRTODO` assertion commented out

**File**: `port/fast3d/gfx_pc.cpp:1797`
**Code**:
```c
// OTRTODO:
// SUPPORT_CHECK(tmem == 0 || tmem == 256);
static uint32_t max_tmem = 0;
```
**Impact**: Texture memory assertion was disabled. Whether this is still valid is unknown.
**Modern solution**: Investigate whether the assertion should be re-enabled, or remove the dead comment.
**Effort**: Trivial
**Priority**: Low

---

## Summary Table

| ID | Category | Pattern | Files | Effort | Priority |
|----|----------|---------|-------|--------|----------|
| C1-1 | Asset conditional | `bodyGetRace()` switch | `body.c:143` | Small | Medium |
| C1-2 | Asset conditional | `langGetLangBankIndex()` 82-case switch | `lang.c:144` | Small | **High** |
| C1-3 | Asset conditional | `chrGetBloodColour()` switch | `chr.c:3253` | Small | Medium |
| C1-4 | Asset conditional | `ciIsChrBioUnlocked()` unlock logic | `training.c:2315` | Small | **High** |
| C1-5 | Asset conditional | `ciGetChrBioByBodynum()` static array | `training.c:2342` | Medium | **High** |
| C1-6 | Asset conditional | Surface type switch by stagenum | `mplayer.c:305` | Small | Medium |
| C1-7 | Asset conditional | Skedar height tied to `BODY_SKEDAR` | `body.c:288` | Trivial | Low |
| M2-1 | Magic number | Player slot defaults `MPBODY_*` | `mplayer.c:765` | Medium | **High** |
| M2-2 | Magic number | Menu preset `0x3e` magic index | `mainmenu.c:3931` | Medium | Medium |
| M2-3 | Magic number | Bot configs all `MPBODY_DARK_COMBAT` | `mpconfigs.c:25` | Small–Med | **High** |
| D3-1 | Lookup duplication | String→stage manual loops | (scattered) | Small | Low |
| T4-1 | Translation layer | `mpGetBodyId()`/`mpGetHeadId()` | `mplayer.c` | Medium | Medium |
| T4-2 | Translation layer | `catalogBodynumToMpBodyIdx` exposed | `assetcatalog_api.c:371` | Trivial | Low |
| R5-1 | ROM assumption | `floatToN64Depth()` Z-buffer format | `artifact.c:109` | Small | Low |
| R5-2 | ROM assumption | `bgGarbageCollectRooms()` stub | `bg.c:3066` | Trivial | Low |
| R5-3 | ROM assumption | `filenum` as model identity | `body.c:163` | Medium | Medium |
| N6-1 | N64 render hack | Depth clamp Z *= 0.3f fallback | `gfx_opengl.cpp:320` | Trivial | Low |
| N6-2 | N64 render hack | 32-bit LOD byte doubling | `gfx_pc.cpp:869` | Trivial | Low |
| N6-3 | N64 render hack | LOD_FRACTION eyeballed `w/1024` | `gfx_pc.cpp:1573` | Medium | Low |
| N6-4 | N64 render hack | Framebuffer aspect RATIO_X global | `gfx_pc.cpp:1679` | Small–Med | Medium |
| N6-5 | N64 render hack | Texture format auto-correction | `gfx_pc.cpp:1807` | Trivial | Low |
| M7-1 | Memory hack | `g_HeadsAndBodies[]` fixed array | `data.h` | Large | Medium |
| M7-2 | Memory hack | Static per-stage lookup tables | `lang.c`, `mplayer.c` | Small | Medium |
| S8-1 | Per-asset logic | `BODY_DRCAROLL` height/radius | `body.c:509` | Small | Medium |
| S8-2 | Per-asset logic | `BODY_CHICROB` beam allocation | `body.c:514` | Medium | **High** |
| S8-3 | Per-asset logic | Per-body effect variants in `chr.c` | `chr.c:3253+` | Medium | Medium |
| D9-1 | Dead code | `if(1);` in `body.c` | `body.c:294` | Trivial | Low |
| D9-2 | Dead code | `if(1);` in `bg.c` (×4) | `bg.c:476,487,962,1179` | Trivial | Low |
| D9-3 | Dead code | `if(0);` in `weather.c` | `weather.c:3111` | Trivial | Low |
| D9-4 | Dead code | `if(false)` version block | `pak.c:2268` | Trivial | Low |
| D9-5 | Dead code | `IS4MB()` guards (always false) | `activemenu.c` + others | Small | Low |
| D9-6 | Dead code | `bgGarbageCollectRooms()` call-sites | `bg.c` + callers | Trivial | Low |
| W10-1 | Tech debt comment | Audio frame size HACK | `audiomgr.c:262` | Medium | Low |
| W10-2 | Tech debt comment | Firing range score workaround | `frcommands.h:114` | Large | Low |
| W10-3 | Tech debt comment | 5× TODO/HACK in renderer | `gfx_pc.cpp` | Medium | Medium |
| W10-4 | Tech debt comment | OTRTODO assertion commented out | `gfx_pc.cpp:1797` | Trivial | Low |

---

## Action Plan

### Immediate (High Priority — Blocks Mods / Bugs)

1. **C1-2** `langGetLangBankIndexFromStagenum()` — add `language_bank_id` to catalog stage entry; replace 82-case switch. **Blocks all stage mods.**
2. **C1-4** `ciIsChrBioUnlocked()` — add `unlock_requirement` to catalog body entry; replace switch. **Blocks character unlock logic.**
3. **C1-5** `ciGetChrBioByBodynum()` — replace static bio array with catalog metadata fields. **Blocks dynamic character additions.**
4. **M2-1** Player slot defaults — define as catalog ID strings, resolve at init. **Related to invisible-bot bugs.**
5. **M2-3** Bot configs in `mpconfigs.c` — catalog ID strings, resolve at load. **Blocks dynamic bot customization.**
6. **S8-2** `BODY_CHICROB` beam allocation — `has_beam_slots` flag in catalog body. **Blocks custom robot creation.**

### Medium Priority (Tech Debt)

7. **C1-1**, **C1-3**, **C1-6** — Move race, blood color, surface type to catalog body/stage metadata (small effort each; good mod-extensibility wins).
8. **S8-1**, **S8-3** — Move `BODY_DRCAROLL` sizing and per-body effect variants to catalog.
9. **N6-4** — Fix `gfx_adjust_viewport_or_scissor` framebuffer aspect assumption before split-screen work.
10. **R5-3** — Long-term: model loader accepts catalog ID string directly.
11. **W10-3** — Address renderer TODOs for alpha discard, mip handling.

### Low Priority (Cleanup)

12. **D9-1 through D9-6** — Bulk pass: remove all `if(1);`, `if(0);`, `IS4MB()` guards, stub call-sites.
13. **R5-1**, **R5-2** — Remove `bgGarbageCollectRooms` stub and document `floatToN64Depth` as artifact preservation.
14. **N6-1 through N6-5** — Expand renderer hack comments; address LOD_FRACTION properly as a stretch goal.
15. **T4-2** — Hide conversion functions from public API.

---

## Notes on Scope

- **Catalog registration code** (`assetcatalog_base.c`, `assetcatalog_api.c`) was excluded as modern infrastructure, not legacy hacks.
- **Deep animation/audio/collision math** excluded unless a clear hack was present.
- **Catalog-ID compliance** (wire protocol, save files, matchslot) excluded — that audit is in `audit-catalog-id-compliance.md`.
- Several findings (C1-4, C1-5, M2-1) are closely related to the catalog universality migration (Phases A–G). They should be scheduled within that sprint.
