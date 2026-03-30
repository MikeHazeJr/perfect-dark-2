# Asset Reference Audit

> Phase 1 of catalog activation initiative.
> Created: 2026-03-28, Session 74.
> Back to [index](../README.md) | Related: [catalog-loading-plan.md](../catalog-loading-plan.md) | [ADR-003-asset-catalog-core.md](../ADR-003-asset-catalog-core.md)

---

## Summary

| Metric | Count |
|--------|-------|
| **Distinct load entry points** | 16 |
| **Total call sites** (across all 4 gateway functions) | ~185+ |
| **Asset types covered** | 10 primary categories |
| **Catalog-compatible already** | 6 (abstract behind romdata layer) |
| **Routed through fileLoadToNew gateway** | 7 entry points — unlocked by C-4 |
| **Bypass file gateway (direct dmaExec/ROM segment)** | 3 (anims, audio, textures) — require C-5/C-6/C-7 |
| **Hardcoded / no file load at all** | 1 (scenarios — static C array) |
| **Has ordering dependencies** | Yes — full dependency chain documented below |
| **Hardcoded N64 assumptions still active** | 3 (stage array size, body/head array size, anim segment) |

### Call Site Breakdown (from grep — src/ only)

| Gateway Function | Occurrences | Files |
|-----------------|-------------|-------|
| `fileLoadToNew` / `fileLoadToAddr` / `fileLoadPartToAddr` | ~17 | file.c, setup.c, modeldef.c, tilesreset.c, bg.c, bondgun.c, langreset.c, lang.c |
| `texLoad` / `texDecompress` / `texGetData` | ~42 | texdecompress.c, texreset.c, tex.c, texselect.c, bg.c, menu.c, modeldef.c, bondgun.c |
| `animLoadFrame` / `animLoadHeader` / `g_Anims[]` | ~77 | anim.c, model.c, modelasm_c.c, chr.c, player.c, propobj.c |
| `sndStart` / `sndLoad*` | ~107 | snd.c, bondgun.c, prop.c, propobj.c, lv.c, menutick.c, and 17 others |
| `dmaExec` / `dmaExecWithAutoAlign` | ~58 | anim.c, snd.c, texinit.c, file.c, lang.c, and 12 others |

---

## Architecture Overview

The codebase uses a **three-layer asset system**:

**Layer 1 — ROM abstraction (port/src/romdata.c)**
Hides ROM version differences and file offset tables behind function calls.
`romdataFileGetData(filenum)` / `romdataFileLoad(filenum)` — no caller sees raw addresses.

**Layer 2 — Game gateways (src/game/file.c)**
`fileLoadToNew()`, `fileLoadToAddr()`, `fileLoadPartToAddr()` — game code calls these.
They call romdata, then trigger preprocessing (byteswap/relocation) based on LOADTYPE.
Three separate gateways exist for ROM segments: `dmaExec` (anims), `dmaExec` (audio tables), `dmaExec` (textures).

**Layer 3 — Asset Catalog (port/src/assetcatalog.c)**
NEW string-keyed hash table system (D3R-2, ADR-003). Currently:
- Registers stages, bodies, heads, arenas, weapons, gamemodes (from modelcatalog.c)
- Provides `catalogResolve("base:joanna_dark")` → `asset_entry_t*`
- **NOT yet intercepting the 4 gateway functions above** — still advisory only

---

## Load Entry Points by Asset Type

---

### 1. Level Tiles / Collision Geometry

#### LOAD-001: Tile File Load
- **File**: `src/game/tilesreset.c:22`
- **Function**: `tilesReset()`
- **What**: Floor/wall/ceiling collision tile data. Binary array of `struct geo` entries — GEOTYPE_TILE_I, GEOTYPE_TILE_F, GEOTYPE_BLOCK, GEOTYPE_CYL.
- **How**: `fileLoadToNew(g_Stages[index].tilefileid, FILELOADMETHOD_DEFAULT, LOADTYPE_TILES)` → romdata → zlib decompress → `preprocessTilesFile()` (byteswap, scale transform ×6+offset).
- **When**: Stage load, early. `tilesReset()` is called before character spawns.
- **Why**: Collision detection (`cdFindGroundInfoAtCyl`, capsule sweeps) reads `g_TileFileData` directly. Must exist before any movement tick.
- **Dependencies**: Needs `bgGetStageIndex(g_Vars.stagenum)` — depends on stage index being set. Output (`g_TileFileData`, `g_TileNumRooms`, `g_TileRooms`) must exist before `stageParseTiles()`, prop instantiation, and any collision check.
- **Catalog-compatible**: **PARTIAL** — hidden behind `fileLoadToNew` gateway. C-4 adds `catalogResolveFilenum(tilefileid)` before the load, enabling per-mod tile replacement.
- **Notes**: Tile file index comes from `g_Stages[g_StageIndex].tilefileid` — numeric. No error recovery if load fails (logs error, sets `g_TileNumRooms = 0`). Mod stages have their own tile files.

---

### 2. Stage Setup (Props, Intro, AI Lists, Pads)

#### LOAD-002: Stage Setup File
- **File**: `src/game/setup.c:1293` (`setupLoadFiles`)
- **Function**: `setupLoadFiles(stagenum)` at line 1325
- **What**: Serialized `struct stagesetup` containing:
  - `intro`: intro camera command list (INTROCMD_* opcodes)
  - `props`: array of prop spawn data (type, position, health, etc.)
  - `paths`: AI patrol path waypoints
  - `ailists`: AI command sequences for objectives/scripted events
- **How**: Selects `setupfileid` (SP) or `mpsetupfileid` (MP) based on `g_Vars.normmplayerisrunning`. Calls `fileLoadToNew(filenum, FILELOADMETHOD_DEFAULT, LOADTYPE_SETUP)`. Then relocates internal pointers: `g_StageSetup.intro = (s32*)((uintptr_t)setup + (uintptr_t)setup->intro)` etc. AI list pointers are also patched to absolute addresses.
- **When**: Stage load, after tiles. Called from `setupLoadFiles()`.
- **Why**: All prop entities, AI behaviour, objectives, and the intro sequence are encoded here. Nothing spawns without it.
- **Dependencies**: Needs stage index → `g_Stages[g_StageIndex].setupfileid`. Output required before prop instantiation, objective init, intro camera, bot pathing. Also triggers `langLoad(langGetLangBankIndexFromStagenum(stagenum))` internally — language load is tied to setup load.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. C-4 would intercept. MP vs SP setup file selection needs catalog-aware branching.
- **Notes**: Contains B-46 fix (intro pointer validation): if intro and props pointers are within 64 bytes (aliased), intro is nulled. For MP setups (`isMpSetup` flag), distance check is skipped — base-game MP setup files legitimately have intro close to props. Intro commands are NOT a separate file — embedded in setup binary. Mod intro replacement requires new file format.

#### LOAD-003: Pad / Spawn Point File
- **File**: `src/game/setup.c:1365`
- **Function**: `setupLoadFiles()` (same call site as LOAD-002)
- **What**: Spawn pad definitions — player/bot respawn positions, angles, pickup items.
- **How**: `fileLoadToNew(g_Stages[g_StageIndex].padsfileid, FILELOADMETHOD_DEFAULT, LOADTYPE_PADS)` → decompress → `preprocessPadsFile()` (byteswap pad structs). Result stored in `g_StageSetup.padfiledata`.
- **When**: Immediately after setup file load in `setupLoadFiles()`.
- **Why**: Bot spawn logic and player respawn (B-46/B-19 fixes) read `g_StageSetup.padfiledata` to populate `g_SpawnPoints[]`. Critical for MP mode.
- **Dependencies**: Loaded after setup file in same function. Must exist before `playerreset.c` populates spawn points. B-19 fix (playerreset.c) scans pad file to populate `g_SpawnPoints` when none are set.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. C-4 intercept.
- **Notes**: Pad file format is unversioned binary. No per-pad mod override currently possible — only full file replacement. Error is logged if load fails (but no recovery path).

#### LOAD-004: Briefing / Preview Setup Load (separate code path)
- **File**: `src/game/setup.c:1230`
- **Function**: `setupLoadBriefing()` (called from menu to show mission briefing)
- **What**: Loads setup file read-only into a temporary buffer to extract briefing text and objective names. Does NOT set `g_StageSetup`.
- **How**: `fileLoadToAddr(setupfilenum, FILELOADMETHOD_DEFAULT, buffer, bufferlen)` — loads into caller-provided buffer. Also calls `langLoadToAddr(langbank, langbuffer, langbufferlen)` for the stage language bank.
- **When**: From mission briefing menu screen (pre-game).
- **Why**: Mission briefing screen needs to show objective text and briefing strings without fully loading the stage.
- **Dependencies**: Needs pre-allocated `buffer` from caller. Doesn't set global state (read-only preview).
- **Catalog-compatible**: **PARTIAL** — fileLoadToAddr gateway. C-4 intercept.

---

### 3. Level Geometry (Background / BG)

#### LOAD-005: BG Geometry Streaming
- **File**: `src/game/bg.c:1243`
- **Function**: `bgLoadFile(void *memaddr, u32 offset, u32 len)`
- **What**: Streams a slice of the BG file (room geometry, portals, display lists, lighting data) into a provided memory address.
- **How**: `fileLoadPartToAddr(g_Stages[g_StageIndex].bgfileid, memaddr, offset, len)` — loads an arbitrary slice of the file WITHOUT inflation. BG data is NOT zlib-compressed (unlike tiles/setup/pads). If `var8007fc04` is set (preloaded BG in memory), it uses `bcopy` instead.
- **When**: During stage geometry load — called repeatedly for each room segment.
- **Why**: Level rendering requires BG room display lists, portal visibility data, and lighting. The fast3d renderer processes these.
- **Dependencies**: BG must load before room rendering, portal culling (`bgPortalCull`), or collision using BG tile geometry. Rooms must be known before props/characters can be placed in them.
- **Catalog-compatible**: **PARTIAL** — `fileLoadPartToAddr` gateway. Unique: BG uses the PART-load variant (streaming slices, not full file inflate). C-4 needs to handle this variant separately from fileLoadToNew. The catalog would need per-BG-segment metadata or the entry would cover the full file.
- **Notes**: `var8007fc04` flag enables memory-preloaded BG path (avoids ROM reads). This is an existing optimization hook. A catalog could use this to route to mod BG files without changing the streaming interface.

---

### 4. Models (Characters, Props, Weapons)

#### LOAD-006: Character Body / Head Model
- **File**: `src/game/modeldef.c:185`
- **Function**: `modeldefLoad(fileid, dst, size, texpool)`
- **What**: 3D character body or head model (geometry, display lists, rigging/skeleton reference).
- **How**: Sets `g_LoadType = LOADTYPE_MODEL`. If `dst` provided: `fileLoadToAddr(fileid, FILELOADMETHOD_EXTRAMEM, dst, size)`. Otherwise: `fileLoadToNew(fileid, FILELOADMETHOD_EXTRAMEM, LOADTYPE_MODEL)`. Then calls `modelPromoteTypeToPointer()`, `modelPromoteOffsetsToPointers()`, `modeldef0f1a7560()` to fixup internal pointers. Returns NULL on failure with a `CATALOG_CRITICAL` log line.
- **When**: Before character spawn or when switching character appearance in MP.
- **Why**: Renderer needs model geometry and display lists. Skeleton reference (`modeldef->skel`) is resolved against `g_Skeletons[]` table.
- **Dependencies**: Needs `g_Skeletons[]` initialized (from `animsInit`). Model fileid comes from `g_HeadsAndBodies[bodynum].fileid` or equivalent. Body must load before character can be drawn. Animations applied after model is loaded.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. C-4 intercept. `FILELOADMETHOD_EXTRAMEM` flag passes through. The catalog already has body/head entries (modelcatalog.c populates them), but the fileid lookup is still numeric.
- **Notes**: `FILELOADMETHOD_EXTRAMEM` allocates extra 0x8000 bytes — needed for model fixup scratch space. `CATALOG_CRITICAL` log on NULL is a sentinel for missing-file debug (added in prior session). `modeldefLoadToNew` and `modeldefLoadToAddr` are thin wrappers.

#### LOAD-007: Weapon Model (Gun File)
- **File**: `src/game/bondgun.c:3831`
- **Function**: Gun load path inside `bondgun.c` (player weapon draw)
- **What**: Weapon/gun 3D model file.
- **How**: `fileLoadToAddr(player->gunctrl.loadfilenum, FILELOADMETHOD_EXTRAMEM, ptr, loadsize)` — loads into pre-allocated ptr at known size.
- **When**: When player equips a weapon (on spawn with Starting Armed, or pickup during match).
- **Why**: Renderer needs weapon mesh to draw in hand view.
- **Dependencies**: Weapon filenum comes from `player->gunctrl.loadfilenum` — set by weapon assignment code. Must have player memory allocated (`ptr` caller-provided). Model fixup runs after load.
- **Catalog-compatible**: **PARTIAL** — fileLoadToAddr gateway. C-4 intercept.

---

### 5. Language / Strings

#### LOAD-008: Language Bank Load (stage-scoped)
- **File**: `src/game/lang.c:409,417,428`
- **Functions**: `langLoad(bank)`, `langLoadToAddr(bank, dst, size)`
- **What**: One of 7 language banks: GUN, MPMENU, PROPOBJ, MPWEAPONS, OPTIONS, MISC, TITLE. Each is a separate zlib-compressed file of human-readable strings.
- **How**: Three variants: (1) `fileLoadToAddr(langGetFileId(bank), ..., dst, len)` — into buffer; (2) `fileLoadToNew(file_id, FILELOADMETHOD_DEFAULT, LOADTYPE_LANG)` — heap alloc; (3) `fileLoadToAddr(file_id, ...)` — reuse existing buffer. `langGetFileId(bank)` maps bank enum → filenum.
- **When**: Stage load (`langLoad` called from `setupLoadFiles`), also at boot for global banks.
- **Why**: All UI text, HUD strings, prop names, weapon names displayed to player.
- **Dependencies**: Language selection (`g_GameFile.language`) determines which language bank filenum to use. Language banks load before any UI renders text.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew/fileLoadToAddr gateway. C-4 intercept. The catalog could route language banks to mod-provided language files (e.g., fan translations).
- **Notes**: Language change at runtime requires bank reload. `langGetFileId(bank)` returns a filenum that varies by language setting — the catalog would need to handle language as a resolution dimension.

#### LOAD-009: Global Language Banks (boot-time)
- **File**: `src/game/langreset.c:60–79`
- **Function**: `langReset()` (called at boot/lang change)
- **What**: All 7 language banks loaded at once (GUN, MPMENU, PROPOBJ, MPWEAPONS, OPTIONS, MISC, and conditionally TITLE).
- **How**: Repeated `fileLoadToNew(langGetFileId(LANGBANK_X), FILELOADMETHOD_DEFAULT, LOADTYPE_LANG)` calls, one per bank. Results stored in `g_LangBanks[LANGBANK_X]`.
- **When**: Boot, and when language setting changes.
- **Why**: Global UI text must be available before any menu renders.
- **Dependencies**: None. Independent of stage.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. C-4 intercept. 7 separate loads.

---

### 6. Animations

#### LOAD-010: Animation Table Init (boot)
- **File**: `src/lib/anim.c:48`
- **Function**: `animsInit()`
- **What**: Animation metadata table (`struct animtableentry[]` — num frames, bytes-per-frame, header length for each animation). NOT the frame data itself.
- **How**: `dmaExec(ptr, _animationsTableRomStart, tablelen)` — direct ROM DMA from a fixed ROM segment. Results in `g_Anims = g_RomAnims = &ptr[1]`, `g_NumAnimations = ptr[0]`. Also allocates LRU caches for frame data and headers.
- **When**: Boot, once. `animsReset()` resets pointers back to ROM table (mod animations clear on stage reset).
- **Why**: All animation system queries go through `g_Anims[animnum]` — need frame count, byte size, etc. before any animation can be streamed.
- **Dependencies**: Must run before any character loads or animation queries. `mempAlloc(MEMPOOL_PERMANENT)` — must run while permanent pool is open.
- **Catalog-compatible**: **NO — direct dmaExec to ROM segment.** C-6 would intercept, letting the catalog register mod animations into `g_Anims` (and expanding the array). Currently, `animsReset()` discards any mod additions.
- **Notes**: `g_AnimReplacements` (allocated here) is the existing mod override array — a parallel system to the catalog. C-6 would consolidate.

#### LOAD-011: Animation Frame / Header Data (on-demand)
- **File**: `src/lib/anim.c:141` (`animDma`)
- **Function**: `animDma(dst, segoffset, len)` — called by `animLoadFrame` / `animLoadHeader`
- **What**: Per-frame bone transformation data and animation header (frame remapping table).
- **How**: `dmaExecWithAutoAlign(dst, _animationsSegmentRomStart + segoffset, len)` — direct ROM DMA from animation segment. If `g_AnimHostEnabled`, uses `bcopy` from `g_AnimHostSegment` instead (dev/mod override hook).
- **When**: On-demand: first time a character plays a given animation frame (LRU cache of 32 frames + 40 headers).
- **Why**: Characters need bone transform data each frame to drive the skeleton.
- **Dependencies**: `animsInit()` must have run (needs `g_Anims[animnum]` metadata to know offset/size). Model with skeleton must be loaded.
- **Catalog-compatible**: **NO — direct dmaExec.** C-6 would add `catalogResolveAnimation(animnum)` before the DMA, enabling mod animation file loading via filesystem instead of ROM segment.
- **Notes**: `g_AnimHostSegment` and `g_AnimHostEnabled` are an existing override hook (used in dev tooling). The catalog would formalize this path.

#### LOAD-012: Mod Animation Replacement (parallel path)
- **File**: `src/lib/anim.c` — `g_AnimReplacements[]` array
- **What**: Mod-provided animation data that replaces specific ROM animations by index.
- **How**: Mod system (mod.c) populates `g_AnimReplacements[animnum] = ptr` at startup. `animLoadFrame/Header` check this array before doing the dmaExec.
- **When**: Checked on every animation load (hot path).
- **Why**: Allows mods to replace animations without modifying ROM data.
- **Catalog-compatible**: **NO — parallel system.** C-6 would consolidate into catalog so `catalogResolveAnimation(animnum)` returns mod override if available, then falls through to ROM. Eliminates the separate array.

---

### 7. Textures

#### LOAD-013: Texture Table Init (boot)
- **File**: `src/game/texinit.c:1` (`texInit`)
- **What**: Texture metadata table (`struct texture[]` — format, dimensions, ROM offset for each texture). NOT the pixel data.
- **How**: `dmaExec` from texture ROM segment to populate `g_Textures[]` and related config arrays (`g_TexWords`, `g_TextureConfigSegment`).
- **When**: Boot.
- **Why**: All texture lookups go through `g_Textures[texnum]` — need format and offset before pixel data can be streamed.
- **Dependencies**: Must run before any texture is decompressed. `MEMPOOL_PERMANENT`.
- **Catalog-compatible**: **NO — direct dmaExec.** C-5 would intercept texture resolution, allowing mods to register texture overrides.

#### LOAD-014: Texture Decompression / Load (on-demand)
- **File**: `src/game/texdecompress.c:146` (`texInflateZlib`) and `src/game/tex.c` (`texLoad`, `texLoadSmall`, `texLoadCompressed`)
- **What**: Individual texture pixel data (paletted zlib-compressed or raw). LRU cache of 150 textures.
- **How**: `texnum` → `g_Textures[texnum]` → get ROM offset → `dmaExec` raw data → `rzipInflate` or direct copy. Cache in `g_TexCacheItems[150]`.
- **When**: During render — first time a texture is needed for a surface. LRU eviction when cache is full.
- **Why**: Fast3d renderer needs texture data in GPU-accessible memory for draw calls.
- **Dependencies**: `texInit()` must have run. Texture config arrays (`g_TexWallhitConfigs`, etc.) populated at stage load.
- **Catalog-compatible**: **NO — direct ROM segment access.** C-5 would add `catalogResolveTexture(texnum)` before the dmaExec, allowing mod texture files to be loaded from filesystem instead.
- **Notes**: 42 call sites across 8 files. Cache (150 slots) is internal — transparent to catalog. No mod texture override path currently exists.

---

### 8. Audio (SFX, Voice, Music / Sequences)

#### LOAD-015: Audio System Init (boot)
- **File**: `src/lib/snd.c:944` (`sndLoadSfxCtl`)
- **What**: SFX control tables — ALBank, ALEnvelope, ALKeyMap, ALWaveTable, ALADPCMBook, ALADPCMloop structures. Defines all available sound effects by index.
- **How**: Reads from ROM audio control segment (`sfxctl`/`sfxtbl`) via dmaExec. Populates `g_SndCache` structure. `g_ALSoundRomOffsets[]` maps soundnum → ROM address of PCM/ADPCM data.
- **When**: Boot. Also called on audio config change.
- **Why**: Without the SFX control table, no sounds can be triggered.
- **Dependencies**: Audio driver (`ALHeap g_SndHeap`) must be initialized first.
- **Catalog-compatible**: **NO — direct dmaExec.** C-7 would intercept.

#### LOAD-016: SFX Playback (on-demand, 107 call sites)
- **File**: `src/lib/snd.c:1311` (`sndLoadSound`) — called by `sndStart` throughout codebase
- **What**: PCM/ADPCM audio sample data for a specific sound effect or voice line.
- **How**: `soundnum` → `g_SndCache.indexes[sfxnum]` → if cached, reuse; if not, find LRU slot → `g_ALSoundRomOffsets[sfxnum]` → dmaExec PCM data into audio heap. 45 concurrent cache slots.
- **When**: On-demand: whenever game plays a sound (weapon fire, footstep, voice, ambient, UI click). 107 call sites across 22 files.
- **Why**: Audio driver needs PCM data to mix and output to speakers.
- **Dependencies**: `sndLoadSfxCtl()` must have run (needs `g_ALSoundRomOffsets[]`). Audio driver initialized.
- **Catalog-compatible**: **NO — direct ROM offset lookup.** C-7 would add `catalogResolveSound(soundnum)` before the ROM lookup, enabling mod audio files (WAV/OGG) to be played instead. `modSoundLoad` (in mod.c) is an existing parallel path — C-7 would consolidate.
- **Notes**: Voice lines (MP3) go through a separate path (`g_SndCurMp3`) using the MP3 decoder. The `lib/mp3/` subsystem reads ROM offsets for voice MP3 data. C-7 would need to handle both PCM and MP3 paths.

#### LOAD-017: Sequence / Music Load
- **File**: `src/lib/snd.c` (sequence player) + `src/game/stagemusic.c`
- **What**: Music sequences (background BGM for stages, menu themes). Stored as N64 audio sequence format.
- **How**: `g_SeqRomAddrs[seqnum]` → ROM address → audio driver loads sequence. `g_SeqTable` populated from `seqtbl`/`seqctl` ROM segments at boot.
- **When**: At stage load (`stagemusic.c` maps stagenum → sequenceid) and menu transitions.
- **Why**: Background music for gameplay and menus.
- **Dependencies**: Audio driver initialized. `g_SeqTable` populated at boot (before any stage load).
- **Catalog-compatible**: **NO — direct ROM address.** No C-phase defined for music yet. Would require audio format conversion (N64 sequences → OGG/MIDI) for mod music.
- **Notes**: `g_SeqInstances[3]` — max 3 concurrent sequences. Stage music crossfades on stage transitions.

---

### 9. Multiplayer Setup / Scenarios

#### LOAD-018: MP Setup File
- **File**: `src/game/setup.c:1315–1319` (inside `setupLoadFiles`)
- **What**: MP-specific setup file (separate from SP setup file). Contains minimal intro data and MP-specific prop layout.
- **How**: When `g_Vars.normmplayerisrunning`, uses `g_Stages[g_StageIndex].mpsetupfileid` instead of `setupfileid`. Same `fileLoadToNew(filenum, FILELOADMETHOD_DEFAULT, LOADTYPE_SETUP)` call as LOAD-002.
- **When**: Stage load in MP mode.
- **Why**: MP stages have different prop layouts and a minimal intro (no cutscene).
- **Dependencies**: Same as LOAD-002.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. C-4 intercept.
- **Notes**: Same load path as SP setup (LOAD-002) — filenum selection is the only difference.

#### LOAD-019: Scenarios (Game Mode Definitions)
- **File**: `src/game/mplayer/scenarios.c` + `src/game/mplayer/scenarios/`
- **What**: Game mode rules — Combat Sim, Capture the Case, Team Elimination, etc. Scoring, win conditions, game options.
- **How**: **Not loaded from ROM at runtime.** Compiled as static data in C source files. No `fileLoadToNew` call.
- **When**: Accessed at scenario selection and match start.
- **Why**: Game logic needs to know the rules for the active mode.
- **Dependencies**: None — available immediately after executable loads.
- **Catalog-compatible**: **NO — not an asset load.** Would require extracting scenario definitions into external files (JSON/INI) and loading them through the catalog. Significant redesign. Enables mod-defined game modes.
- **Notes**: `scenario_save.c` (S71) added save/load of scenario *configurations* (settings), but the scenario *definitions* remain compiled-in.

---

### 10. GE Credits / Title Data

#### LOAD-020: GE Credits Data
- **File**: `src/game/setup.c:1325`
- **Function**: Inside `setupLoadFiles()` — special case for GE credits stage
- **What**: Credits sequence data (text, timing, commands) for the "GE Credits" stage.
- **How**: `g_GeCreditsData = (u8*)fileLoadToNew(filenum, FILELOADMETHOD_DEFAULT, LOADTYPE_SETUP)` — same LOADTYPE_SETUP as regular setup. `setup = (struct stagesetup*)g_GeCreditsData`.
- **When**: When GE Credits stage loads.
- **Why**: Credits sequence renderer needs the credits text/command data.
- **Dependencies**: Same as LOAD-002.
- **Catalog-compatible**: **PARTIAL** — fileLoadToNew gateway. Same as regular setup.

---

### 11. Port-Side Preprocessing (Transparent to Catalog)

These are internal to the load pipeline and not separate load points — they run automatically after decompression.

| File | Function | What it does |
|------|----------|-------------|
| `port/src/preprocess/filetiles.c` | `preprocessTilesFile()` | Byteswap tile geometry (big→little endian), scale transform (×6+offset) |
| `port/src/preprocess/filesetup.c` | `preprocessSetupFile()` | Byteswap setup struct fields (props, paths, ailists, intro commands) |
| `port/src/preprocess/filemodel.c` | `preprocessModelFile()` | Byteswap model display list commands, validate node types |
| `port/src/preprocess/filelang.c` | `preprocessLangFile()` | Byteswap string offset table, validate string pointers |
| `port/src/preprocess/filepads.c` | `preprocessPadsFile()` | Byteswap pad struct array |
| `port/src/preprocess/filebg.c` | `preprocessBgFile()` (if exists) | BG loaded in slices via `fileLoadPartToAddr` — NOT preprocessed (raw big-endian assumed handled by fast3d) |
| `port/src/preprocess/common.c` | Shared helpers | rzip/zlib inflate, error handling, LOADTYPE dispatch |
| `port/src/preprocess/segaudio.c` | Audio segment preprocessing | Byteswap audio control tables after DMA |
| `port/src/preprocess/segfonts.c` | Font segment preprocessing | Byteswap font glyph data |

**These are catalog-transparent** — they run inside the load pipeline and don't need modification for catalog integration. Mod files in external formats (PNG, OGG, etc.) would bypass preprocessing entirely.

---

## Ordering Dependencies Graph

```
BOOT (permanent pool open)
  │
  ├─ animsInit()            ─── LOAD-010: Animation table DMA from ROM segment
  │                              → g_Anims[], g_NumAnimations, LRU caches
  │
  ├─ texInit()              ─── LOAD-013: Texture table DMA from ROM segment
  │                              → g_Textures[], g_TextureConfigSegment
  │
  ├─ sndLoadSfxCtl()        ─── LOAD-015: Audio tables DMA from ROM segment
  │                              → g_SndCache, g_ALSoundRomOffsets[], g_SeqTable
  │
  ├─ langReset()            ─── LOAD-009: Language banks (7 × fileLoadToNew)
  │                              → g_LangBanks[] (all 7 banks)
  │
  └─ catalogInit() (D3R-2)  ─── Asset catalog hash table initialized

STAGE LOAD (lv.c → setupLoadFiles)
  │
  ├─ tilesReset()           ─── LOAD-001: Tile file (fileLoadToNew → LOADTYPE_TILES)
  │    └─ stageParseTiles()          → g_TileFileData, g_TileNumRooms, g_TileRooms
  │    [MUST complete before any collision check or character spawn]
  │
  ├─ bgLoadFile()           ─── LOAD-005: BG geometry streaming (fileLoadPartToAddr)
  │    [multiple calls per stage — one per room segment]         → rooms, portals, display lists
  │    [MUST complete before room rendering or portal culling]
  │
  ├─ setupLoadFiles()
  │    ├─ LOAD-002/LOAD-018: Setup file (fileLoadToNew → LOADTYPE_SETUP)
  │    │      → g_StageSetup.intro, .props, .paths, .ailists
  │    │      [MUST complete before prop instantiation, objective init]
  │    │
  │    ├─ LOAD-003: Pad file (fileLoadToNew → LOADTYPE_PADS)
  │    │      → g_StageSetup.padfiledata
  │    │      [MUST complete before player/bot spawn point selection]
  │    │
  │    └─ langLoad()        ─── LOAD-008: Stage language bank (fileLoadToNew/Addr)
  │           → g_LangBanks[stagebank]
  │
  ├─ Prop instantiation
  │    └─ modeldefLoad()    ─── LOAD-006: Prop / char models (fileLoadToNew → LOADTYPE_MODEL)
  │                                 → per-prop modeldef in g_ModelStates[]
  │
  ├─ Player / bot spawn
  │    └─ modeldefLoad()    ─── LOAD-006: Player body/head (fileLoadToNew → LOADTYPE_MODEL)
  │                                 → player->modeldef
  │
  └─ Match start
       └─ bondgunLoad()     ─── LOAD-007: Weapon model (fileLoadToAddr → LOADTYPE_MODEL)
                                      → player->gunctrl model

RUNTIME (per-frame, on-demand)
  ├─ animDma()              ─── LOAD-011: Animation frame/header (dmaExec)
  │                              [cached LRU: 32 frames, 40 headers]
  ├─ texLoad()              ─── LOAD-014: Texture pixel data (dmaExec)
  │                              [cached LRU: 150 textures]
  └─ sndStart()             ─── LOAD-016: SFX PCM data (dmaExec)
                                 [cached LRU: 45 sounds]
```

**Critical ordering constraints:**
1. `tilesReset()` → must complete before ANY collision check. No recovery if it fails.
2. `bgLoadFile()` → must complete before room visibility test or portal traversal.
3. `setupLoadFiles()` → must complete before prop/chr instantiation or objective init.
4. `padfiledata` → must exist before `playerreset.c` populates `g_SpawnPoints[]`.
5. `animsInit()` must run before `modeldefLoad()` (model fixup checks `g_Skeletons[]` which needs anim metadata).
6. `sndLoadSfxCtl()` must run before any `sndStart()` call.
7. Language banks should be loaded before any UI renders (but no hard crash if missing — empty strings shown).

---

## Catalog Integration Assessment

### Current State

The catalog (D3R-2) is initialized and registers stages, bodies, heads, arenas, weapons, gamemodes. But the 4 core gateway functions — `fileLoadToNew`, `texLoad`, `animLoadFrame`, `sndStart` — do NOT check the catalog before accessing ROM. The catalog is advisory-only.

### What Each C-Phase Unlocks

| Phase | Gateway | Asset Types Unlocked | Call Sites | Effort |
|-------|---------|---------------------|-----------|--------|
| **C-4** | `fileLoadToNew` / `fileLoadToAddr` / `fileLoadPartToAddr` | Tiles, Setup, Pads, Language, Models, Weapons, BG | ~17 | Medium |
| **C-5** | `texLoad*` / `texInflateZlib` | Textures (all) | ~42 | Medium |
| **C-6** | `animDma` / `animLoadFrame` / `animLoadHeader` | Animations (all) | ~77 (anim-related) | Medium |
| **C-7** | `sndStart` / `sndLoadSound` | SFX, Voice lines | ~107 | Medium-Large |

**C-4 is the critical unblocking step.** Once fileLoadToNew checks the catalog, 7 asset types (tiles, setup, pads, lang, models, weapons, BG) automatically become mod-overridable with no additional per-call-site changes. The other ~16 direct call sites in file.c gateway functions automatically benefit.

### Per-Asset-Type Assessment

| Asset Type | Load Points | Current Gate | C-Phase | What Changes |
|------------|------------|--------------|---------|-------------|
| **Tile geometry** | LOAD-001 | fileLoadToNew | C-4 | Mod can provide per-stage tile file |
| **Stage setup** | LOAD-002/018 | fileLoadToNew | C-4 | Mod can override setup (props, AI, intro) |
| **Pad/spawn data** | LOAD-003 | fileLoadToNew | C-4 | Mod can provide custom spawn points |
| **Briefing preview** | LOAD-004 | fileLoadToAddr | C-4 | Same as setup |
| **BG geometry** | LOAD-005 | fileLoadPartToAddr | C-4+ | Needs special handling — partial reads, not full file |
| **Char/prop models** | LOAD-006 | fileLoadToNew | C-4 | Mod can provide custom body/head/prop models |
| **Weapon models** | LOAD-007 | fileLoadToAddr | C-4 | Mod can provide custom weapon mesh |
| **Language banks** | LOAD-008/009 | fileLoadToNew/Addr | C-4 | Mod can provide fan translations |
| **Anim table** | LOAD-010 | dmaExec (ROM seg) | C-6 | Catalog expands g_Anims with mod entries |
| **Anim frames** | LOAD-011 | dmaExec (ROM seg) | C-6 | Catalog routes to mod file instead of ROM |
| **Anim replacements** | LOAD-012 | parallel array | C-6 | Consolidate into catalog, remove g_AnimReplacements |
| **Texture table** | LOAD-013 | dmaExec (ROM seg) | C-5 | Catalog expands g_Textures with mod entries |
| **Texture pixels** | LOAD-014 | dmaExec (ROM seg) | C-5 | Catalog routes to mod texture file |
| **Audio tables** | LOAD-015 | dmaExec (ROM seg) | C-7 | Catalog expands sound table with mod sounds |
| **SFX playback** | LOAD-016 | dmaExec via snd cache | C-7 | Catalog routes to mod audio file |
| **Music sequences** | LOAD-017 | dmaExec via seq table | (future) | Requires audio format design decision |
| **MP setup** | LOAD-018 | fileLoadToNew | C-4 | Same as setup |
| **GE credits** | LOAD-020 | fileLoadToNew | C-4 | Same as setup |
| **Scenarios** | LOAD-019 | static C data | (redesign) | Extract to files; enable mod game modes |

### Hardcoded N64 Assumptions Still Active

| Assumption | Location | Impact on Mods | Effort to Remove |
|------------|----------|---------------|-----------------|
| `g_Stages[87]` fixed array | `stagetable.c`, `setup.c` | Mod stages limited to indices 61–86 (25 slots) | Medium: dynamic array + catalog-driven stage registration |
| `g_HeadsAndBodies[152]` fixed array | `modelcatalog.h`, `modelcatalog.c` | Mod bodies/heads limited to 152 total | Medium: dynamic array, part of D3R-11 |
| `_animationsSegmentRomStart` ROM segment | `anim.c` | Mod animations must use `g_AnimReplacements[]` (index-based, limited to ROM count) | C-6: catalog extends `g_Anims[]` dynamically |
| `_animationsTableRomStart` ROM table | `anim.c` | Animation count fixed at ROM table size | C-6: append mod entries after ROM table |
| Texture count from ROM segment | `texinit.c` | Mod textures need new indices beyond ROM count | C-5: catalog assigns new texnum above ROM range |
| Sound count from `sfxctl` ROM segment | `snd.c` | Mod sounds need new indices beyond ROM count | C-7: catalog assigns new soundnum |

### Not N64 Assumptions (Already PC-clean)
- `dmaExec` on PC reads from ROM file (mmap'd or fread'd), not actual DMA hardware
- `romdataFileLoad` hides ROM version differences (ntsc-final, pal-final, etc.)
- `zlib`/`rzip` decompression on PC is full-speed, no hardware RSP needed
- `MEMPOOL_PERMANENT` / `MEMPOOL_STAGE` are heap allocators (no N64 memory fragmentation constraints)
- `FILELOADMETHOD_EXTRAMEM` is just malloc extra bytes (not DMA alignment requirement)

---

## Key Files Reference

| File | Role | Load Types Handled |
|------|------|-------------------|
| `src/game/file.c` | **Master gateway** — fileLoadToNew/Addr/PartToAddr definitions | All LOADTYPE_* |
| `src/game/tilesreset.c` | Tile file caller | LOADTYPE_TILES |
| `src/game/setup.c` | Setup + pad + credits caller | LOADTYPE_SETUP, LOADTYPE_PADS |
| `src/game/lang.c` + `langreset.c` | Language bank callers | LOADTYPE_LANG |
| `src/game/modeldef.c` | Model file caller (all model types) | LOADTYPE_MODEL |
| `src/game/bondgun.c` | Weapon model caller | LOADTYPE_MODEL |
| `src/game/bg.c` | BG streaming caller | `fileLoadPartToAddr` (no LOADTYPE) |
| `src/lib/anim.c` | Animation table + frame/header caller | `dmaExec` (ROM segment) |
| `src/game/texdecompress.c` + `tex.c` | Texture caller | `dmaExec` (ROM segment) |
| `src/lib/snd.c` | Audio SFX + sequence caller | `dmaExec` (ROM segment) |
| `port/src/romdata.c` | ROM abstraction layer | All (called by file.c) |
| `port/src/assetcatalog.c` | **Catalog** — currently advisory only | (all, post C-4 through C-7) |
| `port/src/modelcatalog.c` | Numeric catalog (bodies/heads) — will be absorbed | ASSET_CHARACTER |

---

## Recommended C-Phase Entry Points (for catalog activation plan)

When implementing C-4, the three functions to modify in `src/game/file.c` are:

1. **`fileLoadToNew(s32 filenum, u32 method, u32 loadtype)` at line 218** — add `catalogResolveFilenum(filenum)` call before `romdataFileLoad`. If catalog returns a mod path, load from filesystem instead.

2. **`fileLoadToAddr(s32 filenum, s32 method, u8 *ptr, u32 size)` at line 269** — same intercept.

3. **`fileLoadPartToAddr(u16 filenum, void *memaddr, s32 offset, u32 len)` at line 163** — BG streaming variant. Intercept needs to support partial reads from mod BG files (seeking to offset within file).

For C-5 through C-7: intercept points are in `texdecompress.c` (`texLoad`), `anim.c` (`animDma`), and `snd.c` (`sndLoadSound` + `sndLoadSfxCtl`).

---

*Created: Session 74, 2026-03-28*
*Based on: direct source audit of src/ + port/src/, cross-referenced with catalog-loading-plan.md and ADR-003*
