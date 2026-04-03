# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Critical Blockers (v0.1.0 "Foundation")

> All critical blockers cleared. v0.1.0 pending QC pass and playtest of items in "Awaiting Build Test / Playtest" section.

> **UI Scaling** — DONE (S97). `videoGetUiScaleMult()`/`videoSetUiScaleMult()` added to `video.c`. `pdguiScaleFactor()` now multiplies by user setting. "UI Scale" slider (50–200%) in Video settings tab. Persists as `Video.UIScaleMult`.
> B-49 (toilet freeze): **VERIFIED FIXED S81**. B-38 (setupCreateProps crash): **CLOSED S80 — FALSE ALARM** (all hypotheses verified safe).

---

## Catalog Activation

| Step | Status |
|------|--------|
| **C-0** Wire assetCatalogInit + RegisterBaseGame + ScanComponents | **DONE** |
| **C-2-ext** source_filenum/texnum/animnum/soundnum in asset_entry_t | **DONE (S74)** |
| **catalogLoadInit** Reverse-index arrays + query functions | **DONE (S74)** |
| **C-4** `catalogGetFileOverride` intercept in `romdataFileLoad()` | **DONE (S74)** — needs playtest with a mod that declares bodyfile |
| **C-5** `catalogGetTextureOverride` intercept in `texLoad()` | **DONE (S92)** — confirmed: texLoad→modTextureLoad→catalogResolveTexture chain fully wired in texdecompress.c:2255 + mod.c |
| **C-6** `catalogGetAnimOverride` intercept in `animLoadFrame/Header()` | **DONE (S92)** — `modAnimationTryCatalogOverride()` added to mod.c; wired in animLoadFrame + animLoadHeader ROM paths in anim.c. Build clean. Needs playtest with anim override mod. |
| **C-7** `catalogGetSoundOverride` in `sndStart()` | **DONE (S80)** — `audioPlayFileSound()` via SDL_LoadWAV in audio.c, intercept wired in snd.c |
| **C-8** Re-wire `catalogLoadInit()` on mod enable/disable | **DONE (S80)** — `catalogLoadInit()` re-wired on toggle |
| **C-9** Stage diff | **DONE (S80)** — `catalogComputeStageDiff` implemented |

---

## Mod System (T-series) — ALL DONE

| Task | Status |
|------|--------|
| T-1 through T-10 | **ALL DONE (S80)** — base table expansion (anim 1207, tex 3503, audio 1545), size_bytes walker, thumbnail queue, sound intercept, mod.json content, stage reset, texture flush |

---

## Memory Modernization (D-MEM) — ALL DONE

| Task | Status |
|------|--------|
| MEM-1: Load state fields | **DONE** |
| MEM-2: assetCatalogLoad/Unload | **DONE** |
| MEM-3: ref_count + eviction | **DONE** |

---

## Recently Completed (S130 — 2026-04-02)

| Item | Status |
|------|--------|
| **Wire protocol v27 — all net_hash removed** (S130) | **DONE** — All net_hash u32 wire fields replaced with catalog ID strings across 7+ message types. ~30 call sites updated. manifestSerialize/Deserialize drop net_hash. |
| **SAVE-COMPAT branches stripped** (S130) | **DONE** — scenario_save.c and savefile.c: all integer fallback paths removed. Write/read paths use catalog ID strings only. |
| **Comprehensive bug audit** (S130) | **DONE** — 19 findings (2 CRITICAL, 3 HIGH, 8 MEDIUM, 6 LOW). 5 systemic patterns. See `audit-comprehensive-bugs.md`. |
| **B-73 ChrResync desync (CRITICAL)** (S130) | **FIXED** — NULL prop no longer skips buffer reads. All fields always consumed. |
| **B-74 Unbounded malloc (CRITICAL)** (S130) | **FIXED** — MAX_DISTRIB_ARCHIVE_BYTES (64MB) guard before malloc. |
| **B-75 SVC_PLAYER_MOVE OOB (HIGH)** (S130) | **FIXED** — Bounds check on network-received client ID. |
| **B-76 sprintf overflow (HIGH)** (S130) | **FIXED** — snprintf + strncat with size limits. |
| **Per-frame log spammers removed** (S130) | **DONE** — 5 bondwalk.c spammers removed, one-shot events preserved. |
| **Catalog-based defaults in mplayer.c** (S130) | **DONE** — MPBODY_*/MPHEAD_*/STAGE_* replaced with assetCatalogResolve. |
| **Engine modernization vision documented** (S130) | **DONE** — ROM as legacy provider → catalog as provider-agnostic bus → PBR/physics. |

## Recently Completed (S109 — 2026-04-01)

| Item | Status |
|------|--------|
| **Credits/storyboard removal** (S109, f5cf0da) | **DONE** |
| **Updater download freeze fix** (S109, 3b593ed) | **DONE** |
| **Mission select UX redesign** (S109) | **DONE** |
| **v0.0.22 released to GitHub** (S109) | **DONE** |
| **B-56 / B-57 / B-58 fixed** (S109) | **DONE** |

---

## Manifest Lifecycle Sprint

> **Goal**: Remove numeric alias bloat, make manifest speak human-readable catalog IDs natively.
> **Entry point**: Catalog investigation in S109 revealed ~7k real assets + ~7k alias duplicates = 14k entries. Aliases are waste.

| Phase | Task | Status |
|-------|------|--------|
| **Phase 0** | Remove numeric alias entries from catalog; manifest uses human-readable IDs ("base:falcon2" not "weapon_46") | **DONE (S110, commit 4476d00)** |
| **Phase 1** | Manifest-diff transitions — diff old vs new manifest, load delta, unload stale | **DONE (S110, commit dd04701)** — catalogLoadAsset/catalogUnloadAsset wired in manifestApplyDiff + manifestEnsureLoaded; manifestMPTransition() added; mainChangeToStage() routes SP vs MP. Needs SP playtest (two consecutive missions) + MP playtest (match load → verify MANIFEST-MP: log lines). |
| **Phase 2** | Dependency graph — character → body + head + anims + textures | **DONE (S111, commit 2c761f1)** — flat dep table module (assetcatalog_deps.c/.h), scanner populates from INI "deps" and anim "target_body" fields, manifestBuild + manifestBuildMission expand deps at all 6 body/head add sites. Both targets build clean. Needs playtest with a mod body that declares deps. |
| **Phase 3** | Language bank manifesting — menu screens declare lang dependencies | **DONE (S112, commit 5d449cd)** — ASSET_LANG type, 68 base lang banks registered, langmanifest.h/c module, langManifestEnsureId() API, langreset.c + setup.c wired, scanner type mappings. Build clean. |
| **Phase 4** | Pre-validation pass — verify all entries exist before committing | **DONE (S113, commit 98aa2ec)** — `manifestValidate()` validates to_load entries (catalog presence, enabled state, lang bank type-check, dep chain warnings) before `manifestApplyDiff`. Wired in both SP + MP paths. 529/529 clean. |
| **Phase 5** | Proper unload/cleanup — targeted ref-counted unloads | **DONE (S114, commit 8af6919)** — `catalogUnloadAsset` now logs "freed/retained" with ref deltas, cascades dep unloads when parent hits ref=0, safe idempotent with manifest direct dep calls. `manifestApplyDiff` loads before unloads (primary crash fix for 56-asset match→menu transition 0xc0000005). Needs playtest: SP transition → verify log shows ref=N->M (freed/retained) lines. |
| **Phase 6** | Menu/UI asset manifesting — screens register mini-manifests | **DONE (S115, commit d624022)** — `screenmfst.h/c` module: `screenManifestRegister(dialogdef*, ids[], types[], count)` + `screenManifestTick()` (enter/leave detection via per-frame dialogdef* set diff) + `screenManifestShutdown()`. Wired into `pdguiHotswapRenderQueued` + `pdguiHotswapShutdown`. Example registrations: Agent Select (body+head+lang_misc), Match Setup (lang_mpmenu+lang_mpweapons), Network (lang_mpmenu+lang_misc). Base-game bundled assets are no-op retains; full lifecycle active for non-bundled mod assets. 721/721 clean. |
| **Post-sprint fixes** | Audit bugs from v0.0.22 + hash mismatch | **DONE (S116, commit 93a2a26)** — langSafe() at 9 unsafe langGet() sites (solomission + agentselect), manifest dedup hash fixed (FNV-1a→CRC32 via e->net_hash), synthetic body_N/head_N fallbacks removed from manifestBuild + manifestBuildMission, "LANG" added to s_type_names[]. 530/530 clean. |

---

## Recently Completed (S106 — 2026-04-01)

| Item | Status |
|------|--------|
| **Solo Campaign mission select redesign + NULL crash fix** (S106) | **BUILD VERIFIED** — Tree-based chapter/mission hierarchy, all 21 missions with A/S/P completion badges, per-diff checkpoint rows, reward tooltip stubs. Fixed 11 `langGet()→langSafe()` crash sites (0xc0000005 on mission select). Needs playtest. |

---

## Recently Completed (S105 — 2026-04-01)

| Item | Status |
|------|--------|
| **Level Editor tab foundation** (S105) | **BUILD VERIFIED** — Tab 3 "Level Editor" in room screen. Catalog browser (type/search), spawned object list, property editor (scale/collision/interaction/texture), floating overlay, "Launch Level Editor" footer button. Needs playtest. Free-fly camera + empty level load are stubs. |

---

## Recently Completed (S103 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 6 Training Mode dialogs** | **BUILD VERIFIED** — `pdgui_menu_training.cpp`: 12 ImGui renderers (FR difficulty/info/stats, bio text, DT result, HT list+result, now-safe) + 10 NULL (3D model/GBI screens). Zero warnings. Needs playtest. |

---

## Recently Completed (S100 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 2 End Screens — registration wired** (9a376eb) | **DONE** — `pdguiMenuEndscreenRegister` now called from `pdguiMenusRegisterAll`. SP solo/2P/MP end screens active. `pdgui_menu_endscreen.cpp` already in 795ff96. Needs playtest. |

---

## Recently Completed (S99 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 1 Solo Mission Flow** (30a1d9e) | **DONE** — 8 ImGui replacements + 3 legacy-preserved. `pdgui_menu_solomission.cpp`. Needs playtest. |

---

## Recently Completed (S97-S98 — 2026-04-01)

| Item | Status |
|------|--------|
| **Kill plane void death** (9ff6daa) | **DONE** — adaptive Y threshold from `g_WorldMesh` bounds; falls back to Y < -10000. Force-kills + normal respawn. `src/game/player.c`. |
| **Catalog Settings tab** (1aa0c93) | **DONE** — Settings > Catalog: summary (entry counts by type, loaded/mod counts), entry browser (type filter + search), stage manifest view. `port/fast3d/pdgui_menu_mainmenu.cpp`. |
| **Universal numeric aliases** (1c801a3, 8f6de5e, 554759e) | **DONE** — `body_%d`, `head_%d`, `weapon_%d`, `stage_0x%02x`, `arena_%d`, `prop_%d`, `gamemode_%d`, `hud_%d`, `model_%d`, `anim_%d` (1207), `tex_%d` (3503), `sfx_%d` (1545). Mods can now override any asset by numeric ID. |
| **Arena list from catalog** (9a698c3) | **DONE** — GEX stages removed. "Mods" catch-all group added. Mod arenas auto-appear in Combat Sim dropdown. |
| **Boot crash / match start crash** (c7bfa43, c5486ee, f355ff6) | **DONE** — runtime_index space mismatch fixed; `sessionCatalogGetId` direct; stage_0x alias + hash backfill. |
| **Manifest chokepoints** (990d512, 6e1addc) | **DONE** — `manifestEnsureLoaded` wired in `bodyAllocateModel` (all spawn paths) and `setupLoadModeldef` (all model loads). Obj1 crash fixed. |
| **SA-2/SA-3/SA-4** (4945ff3, af6036b, 574f7b6) | **DONE** — modular catalog API, wire protocol migration, persistence migration. All completed in unlogged sessions (S91–S92). |

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **SA-6/S97 SP manifest completeness** | **BUILD VERIFIED (S97)** — needs SP playtest: (1) two consecutive missions, verify Joanna stays in `to_keep`; (2) Counter-Op mode, check log for `MANIFEST-SP:` lines showing anti-player body/head added. |
| **T-7 mod.json body/head/arena catalog registration** (S77) | **CODED (S77)** — needs playtest: enable a mod with `content.bodies/heads/arenas` in mod.json; verify entries appear in character/arena pickers in-game. |
| **T-8/T-9 Stage table restore + texture cache flush on reload** (S78) | **BUILD VERIFIED (S78)** — needs playtest: toggle a mod on/off, confirm no stale stages or textures after reload. |
| **B-46 Void spawn on MP stages** (S74) | **FIXED (S74)** — needs Felicity playtest confirmation. |
| **B-47 Exit freeze on window close** (S74) | **FIXED (S74)** — needs test: close window during match, should exit within 1s. |
| **Combat Sim scenario save/load** (S71) | **CODED (S71)** — needs build + smoke test. |

> The following were confirmed working in S81 networked playtest and removed from this list: **B-44/B-26** (bot names + player name), **B-43** (first-tick crash), **B-39** (jump crash), **B-40/41** (timelimit+options), **B-42** (bot cap).

---

## Awaiting Build Test / Playtest (Pre-S68)

| Item | Status |
|------|--------|
| **Room interior UX + Match Start** (S57–S60) | **MILESTONE: MATCH RUNS** (confirmed S68: 7 bots, Jungle, 25s gameplay). Full playtest: Leave Room → social lobby, Start Match → match loads. |
| **netSend audit + CRIT fixes** (S61) | **BUILD VERIFIED (S61)** — 3 critical bugs fixed. Desync recovery now functional. |
| **SP-6 Null Guard Audit** (S64) | **CODED (S64)** — 14 HIGH/CRITICAL PLAYERCOUNT() loops guarded across 8 files. Needs full client build + playtest. |
| **SP-8 Prop/Obj Null Guard Audit 2 of 4** (S65) | **CODED (S65)** — 7 CRITICAL/HIGH fixes. Needs build + playtest. |
| **Bot/AI/Simulant null-guard audit 4 of 4** (S66) | **CODED (S66)** — 28 CRITICAL/HIGH bugs fixed across 6 files. Needs build + dedicated server playtest. |
| **B-36 Client crash after skyReset** (S63) | **CODED (S63+S64)** — needs full client build + playtest on stage with ambient music. |
| **2-player Combat Sim match** (S54) | Build client + server. Connect → lobby → Combat Simulator → verify match loads + both players spawn. |
| **SPF-1/3**: Hub, rooms, identity, lobby, join-by-code (S47d–S49) | Run `.\devtools\build-headless.ps1 -Target server`, then end-to-end join test (J-1) |
| **Update tab — cross-session staged version** (S50) | Build client. Download a version, close without restarting. Reopen Update tab → Switch button should appear. |
| **Player Stats** (playerstats.c) (S49) | Needs client build test. |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **B-13**: Prop scale fix (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open (Pre-Audit)

| Bug | Severity | Status |
|-----|----------|--------|
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Needs investigation. |
| B-19 Bot spawn stacking on Skedar Ruins | MEDIUM | **PARTIAL FIX (S54)** — Phase F.1 adds anti-repeat (S125). Needs test. |
| B-21 Menu double-press / hierarchy issues | MEDIUM | **LIKELY FIXED (S124 Phase E)** — full-stack duplicate rejection in menuPush(). Needs playtest. |
| B-60 Stray 'g'+'s' visible behind Video/Audio tabs | LOW | OPEN |
| B-72 SVC_LOBBY_STATE raw stagenum | LOW | OPEN — display-only, does not block v0.1.0 |

> B-56/B-57/B-58 all **FIXED (S109)**. B-50 **FIXED (S81)**. See bugs.md for full history.
> B-73 through B-89 are from the S130 comprehensive audit — tracked in "Comprehensive Bug Audit" section above.

---

## Catalog Universality Migration — CRITICAL (Blocks All Other Feature Work)

> **Governing spec**: `PD2_Catalog_Universality_Spec_v1.0.docx` (root of repo)
> **Root cause**: Playtest (April 1, 2026) confirmed catalog type=16 failures (B-63/B-64), server catalog gap (B-65), and menu input state gaps (B-66–B-69). The catalog still has raw-index call sites that bypass type-safe resolution. This migration makes the catalog universal and hermetic.

| Phase | Task | Priority | Status |
|-------|------|----------|--------|
| **Phase A** | **Catalog Universality Audit** — full codebase audit of every raw-index asset reference. 47 raw-index sites mapped (9 CRITICAL, 8 HIGH, 18 MEDIUM, 12 LOW). See `context/audit-catalog-universality.md`. | CRITICAL | **DONE (S120, commit 23a35c7)** |
| **Phase B** | **Catalog API Hardening + Human-Readable IDs** — FIX-24 (SP-only head registration), FIX-7/13/11/12/14/10/15 (index domain fixes), B.3 (typed error logging), Part 2 (arena human-readable IDs). New API: `catalogResolveBodyByMpIndex`, `catalogResolveHeadByMpIndex`, `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`. | CRITICAL | **DONE (S121, commit b13a6b5) — needs playtest: confirm zero CATALOG-ASSERT type=16** |
| **Phase C** | **Systematic Catalog Conversion** — convert every subsystem to catalog-only resolution: bot allocation, SVC_STAGE_START bot config, weapon spawn, arena selection, stage loading. System by system, tested individually after each. | CRITICAL | **DONE (S122) — needs playtest: CLC_LOBBY_START stage net_hash, weapon net_hash, bot mpbodynum/mpheadnum conversion** |
| **Phase D** | **Server Manifest Model** — server receives match manifest from host (catalog IDs, not raw indices). No server-side catalog. Mod distribution with SHA-256 validation. Protocol version bump. Fixes B-65 (SVC_STARTGAME stage gap). | CRITICAL | **DONE (S123, commit e517633)** — manifestBuildForHost, manifestSerialize/Deserialize, SHA-256 in modinfo_t, CLC_LOBBY_START embeds host manifest, SVC_MATCH_MANIFEST uses serialize helpers, server supplements player body/head, D.5 stage validation, protocol v26. Needs playtest: CLC_LOBBY_START host manifest flow in real MP match. |
| **Phase E** | **Menu Stack Architecture** — push/pop menu stack, input context stack, duplicate rejection on push, theme/tint separation (tint cleared on any menu pop back to root), Esc fix. Fixes B-68/B-69/B-21. | HIGH | **DONE (S124, commit 5eab8d3)** — menuPush full-stack duplicate rejection; endscreen releases mouse on IsWindowAppearing(); menumgr restores SDL mouse on stack-empty; main menu enforces palette 1; endscreen save/restore palette. Needs playtest: post-mission buttons, MP lobby→gameplay mouse, green tint. |
| **Phase F** | **Spawn System Hardening** — randomization verified, navmesh fallback, floor fallback, stuck detection with 1/4 damage relocation for 2 seconds. Fixes B-71, hardens B-19. Also wire `inputSetMode()` on match start and post-mission transition. Fixes B-66/B-67. | HIGH | **DONE (S125, commit 27b1e08)** — F.1 anti-repeat (s_LastSpawnPad), F.5 stuck detection (bot.c, 180-frame snapshot, 300u relo, 25% penalty), F.6 B-70 MPOPTION_SPAWNWITHWEAPON default + spawnWeaponNum resolution, B-66 inputLockMouse(1) after menuStop(). F.2/F.3/F.4 already done in playerReset(). Needs playtest: spawn variety, bot unstick, spawn weapons. |
| **Phase G** | **Full Verification Pass** — all game modes, clean logs, mod testing, spawn testing, menu transition testing. Success criteria: zero CATALOG-ASSERT warnings, zero type=16 in any log, all MP game modes run to completion with bots, all menu transitions clean (no tint bleed, no duplicate instances). | HIGH | **CODE COMPLETE (S126–S130)** — S127: catalog-ID-native data model (8 files). S128: stage_id + bridge API + catalog defaults (6 files). S129: UI picker conversion (2 files). S130: wire protocol v27 (all net_hash → catalog ID strings), SAVE-COMPAT stripped, comprehensive bug audit (19 findings), C-01/C-02/H-01/H-02 critical fixes. **PLAYTEST VERIFICATION PENDING.** |

---

## Comprehensive Bug Audit — Remaining Fixes (S130)

> **Source**: `context/audit-comprehensive-bugs.md` — 19 findings total
> **Fixed (S130)**: C-01 (ChrResync desync), C-02 (unbounded malloc), H-01 (OOB array), H-02 (sprintf overflow)

### Tier 2 — Fix Before Public Release

| Finding | Description | File | Effort | Status |
|---------|-------------|------|--------|--------|
| **M-2** | Chat rebroadcast without rate limiting — DoS amplification | netmsg.c | S | OPEN |
| **M-3** | Chunk ordering ignored in mod distribution — silent data corruption | netdistrib.c | M | OPEN |
| **M-4** | archive_bytes not validated at BEGIN time (companion to C-02) | netdistrib.c | XS | OPEN |
| **M-7** | JSON tokenizer unbounded recursion on deep nesting — crafted save crash | savefile.c | S–M | OPEN |
| **H-3** | fread return unchecked in savefile load — silent save corruption | savefile.c | S | OPEN |

### Tier 3 — Quality Pass

| Finding | Description | File | Effort | Status |
|---------|-------------|------|--------|--------|
| **M-1** | Dead `tmp[1024]` variable in chat handler | netmsg.c | XS | OPEN |
| **M-5** | pdsched.c TODO — scheduling investigation | pdsched.c | M | OPEN |
| **M-6** | Audio sample rate 22020 Hz (should be 22050?) | audio.c | S | OPEN |
| **M-8** | Incomplete shutdown sequence on quit | main.c | M | OPEN |
| **L-1** | buildArchiveDir stale pointer on realloc failure | netdistrib.c | S | OPEN |
| **L-2** | enet_peer_send return value unchecked | netdistrib.c | XS | OPEN |
| **L-3** | strcpy → strncpy in input.c VK names | input.c | XS | OPEN |
| **L-4** | strcpy → strncpy in mpsetups.c | mpsetups.c | XS | OPEN |
| **L-5** | strcpy → strncpy in fs.c homeDir | fs.c | XS | OPEN |

### Systemic Sweeps (Cleanup Sprint)

| Pattern | Scope | Status |
|---------|-------|--------|
| sprintf → snprintf | 350+ instances across src/ and port/ | OPEN |
| Network array bounds checks | All netbufReadU8/U16 used as array indices | **DONE (S131)** — 1 unguarded site fixed in netmsgSvcAuthRead |
| fread/fwrite return checks | All file I/O in port/src/ | OPEN |
| strcpy → strncpy | 20+ instances in port/src/ | OPEN |
| malloc/realloc NULL checks | Selective — smaller allocations in game code | OPEN |

---

## Active Work Tracks

---

### SESSION CATALOG + MODULAR API — COMPLETE

> **Design doc**: [`context/designs/session-catalog-and-modular-api.md`](designs/session-catalog-and-modular-api.md)
> **Principle**: The catalog replaces the **entire** legacy loading pipeline, not just networking. All asset references at interface boundaries (wire protocol, save files, public APIs) must use catalog IDs — never raw N64 indices.
> **Status**: ALL DONE (S91–S97). Wire protocol fully migrated to catalog ID strings in S130 (v27). This track is complete.

| Phase | Task | Details |
|-------|------|---------|
| SA-1 | **Session catalog infrastructure** | `sessioncatalog.h/c`: build from manifest, broadcast SVC_SESSION_CATALOG (0x67), receive + resolve, teardown. **DONE (S91)** — both targets build clean. |
| SA-2 | **Modular catalog API** | Per-type resolution functions (`catalogResolveBody/Head/Stage/Weapon`) + wire helper structs. **DONE (S91–S92, commit 4945ff3)** — completed in unlogged session between S91 and S92. |
| SA-3 | **Network session catalog wire migration** | Replaced raw N64 indices in SVC_*/CLC_* with u16 session IDs. ~180 call sites, ~20 message types. **DONE (S91–S92, commit af6036b)** |
| SA-4 | **Persistence migration** | Session IDs in savefile, identity, and scenario save. **DONE (S91–S92, commit 574f7b6)** — NOTE: scenario save weapon persistence (B-57) and type=16 assert (B-58) identified as follow-up bugs. |
| SA-5 | **Load path migration + deprecation pass** | SA-5a through SA-5f all complete. All `g_HeadsAndBodies[].filenum/.scale` and `g_Stages[].bgfileid` etc. load-path accesses migrated. `catalogGetBodyScaleByIndex` legacy fallback fixed. Deprecated-attribute audit confirmed zero external callers on 3 old modelcatalog functions. **DONE (S92-S93)**. |
| SA-6 | **SP load manifest (diff-based lifecycle)** | Full implementation + S96 follow-up: counter-op body/head added to `manifestBuildMission`; `manifestEnsureLoaded()` runtime safety net added; `bodyAllocateChr` wired. **DONE (S94/S96) — needs SP playtest: two consecutive missions (verify Joanna stays in to_keep) + Counter-Op mode (verify anti-player body/head in manifest log).** |
| SA-7 | **Consolidation cleanup** | modelcatalog.c kept (unique VEH/validation logic). g_ModBodies/Heads/Arenas confirmed absent. Removed dead accessors: `catalogGetEntry`, `catalogGetBodyByMpIndex`, `catalogGetHeadByMpIndex`. All 6 audits passed clean. port/CLAUDE.md updated with catalog-first rules. **DONE (S95) — 711/711 clean.** |

> **Status**: **ALL DONE (S97)**. SA-1 (S91), SA-2/3/4 (unlogged S91–S92), SA-5 (S93), SA-6 (S94/S96), SA-7 (S95). The full SA catalog migration track is complete.
> **Follow-up bugs**: B-57 (scenario save weapon persistence), B-58 (catalogResolveByRuntimeIndex type=16 assert on save path).
> **Dependencies**: Match Startup Pipeline (Phases B–F) consumed SA-2. R-series room sync depends on SA-1 (done). These dependencies are now unblocked.

---

### Join Flow (J-series) — Next Steps

| Phase | Task | Details |
|-------|------|---------|
| J-1 | **Verify end-to-end join** | **DONE (S81)** — full join cycle verified: connect code → CLSTATE_LOBBY → match loads → match runs → match ends. |
| J-2 | **Server GUI connect code** | **DONE (S84)** — IP waterfall UPnP→STUN→empty in `server_gui.cpp:~684`. Shows "discovering..." while UPnP/STUN working; "LAN only" only when both settled. `pdgui_bridge.c`: `netGetPublicIP()` checks STUN between UPnP and HTTP fallback. |
| J-3 | **SVC_ROOM_LIST protocol** | Broadcast room state from server to clients so lobby UI shows real room data. |
| J-4 | **Server history UI** | **DONE (S80/S84)** — serverhistory.json + Recent Servers panel. S84: `fmtRelTime` lambda ("5s ago", "12m ago") in subtitle "ABC-DEF · 5m ago"; `net.c` changed `lastresponse = g_NetTick` → `lastresponse = (u32)time(NULL)` for unix timestamps; expanded config `Net.RecentServer.N.Host` + `.Time`. |
| J-5 | **Lobby handoff polish** | **DONE (S81)** — `menuStop()` + `pdguiMainMenuReset()` called on SVC_AUTH; menu stack cleared on lobby join. |

See [join-flow-plan.md](join-flow-plan.md) for full audit.

---

### Room Architecture (R-series) — See [room-architecture-plan.md](room-architecture-plan.md)

| Phase | Task | Details |
|-------|------|---------|
| R-1 | **Foundation (no protocol change)** | Hub slot pool stubs, `g_NetLocalClient = NULL` for dedicated server, IP scrub (B-28/29/30). **DONE (S52)** — needs server build test. |
| R-2 | **Room lifecycle** | Expand `HUB_MAX_ROOMS=16`, `HUB_MAX_CLIENTS=32`. Add `leader_client_id`, `room_id`. On-demand room creation. Remove permanent room 0. |
| R-3 | **Room sync (protocol)** | `SVC_ROOM_LIST 0x75`, `SVC_ROOM_UPDATE 0x76`, `SVC_ROOM_ASSIGN 0x77`. `CLC_ROOM_JOIN 0x0A`, `CLC_ROOM_LEAVE 0x0B`. |
| R-4 | **Match start (room-scoped)** | `CLC_ROOM_SETTINGS 0x0C`, `CLC_ROOM_KICK 0x0D`, `CLC_ROOM_TRANSFER 0x0E`, `CLC_ROOM_START 0x0F`. |
| R-5 | **Server GUI redesign** | New Players + Rooms panel layout. No raw IP anywhere. Replace Hub tab with Rooms panel. |

---

### Lobby / Room / Match UX Flow (L-series) — See [lobby-flow-plan.md](lobby-flow-plan.md)

> L-series = client-facing UI only. Depends on R-2 + R-3 complete before L-1/L-2 can be wired.

| Phase | Task | Details |
|-------|------|---------|
| L-1 | **Social Lobby** | Rewrite `pdgui_menu_lobby.cpp`: strip game mode selection, add room list with Join buttons and Create Room. Depends on R-3. |
| L-2 | **Room Create/Join** | Wire Create Room + Join Room buttons to `CLC_ROOM_JOIN`. Handle `SVC_ROOM_ASSIGN`. Depends on R-3. |
| L-3 | **Room Interior + Mode Selection** | New `pdgui_menu_room.cpp`. Leader: mode buttons + room player list. Non-leader: read-only. Depends on R-4. |
| L-4 | **Combat Sim Setup** | Extend `pdgui_menu_matchsetup.cpp` with network path. Depends on R-4. |
| L-5 | **Campaign + Counter-Op Setup** | New `pdgui_menu_campaign_setup.cpp`. Depends on R-4. |
| L-6 | **Drop-In / Drop-Out** | Allow joining ROOM_STATE_MATCH rooms. Depends on L-4/L-5. |

---

### Match Startup Pipeline — See [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md)

> Unified design merging B-12 Phase 3, R-2/R-3, J-3, C-series, and mod distribution. 8-phase pipeline: Gather→Manifest→Check→Catalog→Transfer→Ready Gate→Load→Sync.

| Phase | Task | Details |
|-------|------|---------|
| A | **Protocol messages** | **DONE (S84)** — `SVC_MATCH_MANIFEST (0x62)`, `CLC_MANIFEST_STATUS (0x0E)`, `SVC_MATCH_COUNTDOWN (0x63)` opcodes + structs + read/write stubs in netmsg.c; `match_manifest_t` in netmanifest.h; `ROOM_STATE_PREPARING`/`CLSTATE_PREPARING` added; NET_PROTOCOL_VER=24 |
| B | **Server side manifest build** | **DONE (S85)** — `port/src/net/netmanifest.c`: `manifestBuild()`, `manifestComputeHash()`, `manifestClear()`, `manifestAddEntry()`, `manifestLog()`. Wired into `netmsgClcLobbyStartRead` before `netServerStageStart()`. Logs manifest on every match start. Both targets build clean. |
| C | **Client manifest processing** | **DONE (S86)** — `manifestCheck()` in netmanifest.c; `g_ClientManifest` global; `netmsgSvcMatchManifestRead()` stores + checks; server broadcasts manifest + logs; both dispatches wired in net.c |
| C.5 | **Full game catalog registration** | **PARTIAL (S87)** — SP bodies/heads from g_HeadsAndBodies[152] registered in assetcatalog_base.c (~12 new entries). Stages already covered by s_BaseStages[]. **Remaining**: navmesh spawn fallback for SP maps, UI grouping (MP/SP/Unlockable categories in character picker), MPFEATURE_* unlock gating in selection UI. |
| D | **Mod transfer** | **DONE (S88)** — `netmsgClcManifestStatusRead()` resolves missing hashes via catalog, queues via `netDistribServerHandleDiff`. No-op when all clients READY. |
| E | **Ready gate** | **DONE (S88)** — `s_ReadyGate` bitmask tracker, `CLSTATE_PREPARING` transitions, 30s timeout, per-client status handling (READY/NEED_ASSETS/DECLINE), broadcasts SVC_MATCH_COUNTDOWN on state changes. Replac