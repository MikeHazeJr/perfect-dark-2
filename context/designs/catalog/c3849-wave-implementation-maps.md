# c3849 Wave Implementation Maps (preserved 2026-06-10)

> Source-grounded plan fragments from mapping workflows wkdkezz7v + wmi19i7uk.
> Waves 1, 2a (soundnum), 2b (texnum) are SHIPPED; the fragments below are the
> remaining implementable waves. Every claim was file:line-verified at map time;
> re-verify anchors before editing (lines drift).


## Wave 2c -- animnum allocator

### Key facts

- src/lib/anim.c:48-94 animsInit(): DMAs ROM anim table; g_NumAnimations = g_NumRomAnimations = ptr[0] (RUNTIME value, 1207 on ntsc-final); g_Anims = g_RomAnims = &ptr[1] -- ONE allocation, both pointers alias it. Per-animnum side arrays sized by g_NumAnimations: g_AnimToHeaderSlot (u8, line 76), var8005f014 (s16, line 77, written by race.c:86), g_AnimReplacements (u8*, lines 87-88).
- src/include/types.h:5160-5167 struct animtableentry (12 bytes): u16 numframes; u16 bytesperframe; u32 data (segment offset, 0xffffffff = external-replacement sentinel); u16 headerlen; u8 framelen; u8 flags.
- src/lib/anim.c:283-342 animLoadFrame / 353-401 animLoadHeader: when g_Anims[n].data == 0xffffffff they call modAnimationLoadData(n) and point cache slots DIRECTLY into g_AnimReplacements[n] (lines 313-319, 380-385) -- the LRU byte slots sized by g_AnimMaxBytesPerFrame/g_AnimMaxHeaderLength (base-rows-only max calc, lines 63-74) are bypassed, so oversized custom clips are safe. Gate at line 311 reads g_Anims[n].bytesperframe BEFORE load -- custom row must be pre-seeded nonzero.
- Clip-install clamp #1: port/src/assetcatalog_load.c:1028-1048 s_catalogInstallAnimationClip() -- returns early if anim_id < 0 || anim_id >= g_NumAnimations; writes g_Anims[anim_id] = *compiled_row, .data = 0xffffffff, NULLs g_AnimReplacements[anim_id]. Compiled row comes from modAssetCompilerBuildAnimationClip (line 1077, outputs exact struct animtableentry + clip bytes). Unload restore mirror at 1605-1617 (same bound; note g_Anims==g_RomAnims so line 1612 'restore' is a self-copy no-op -- pre-existing).
- Clip-install clamp #2: port/src/mod.c:1014-1017 modAnimationLoadCatalogClip() -- 'if (compiled_entry && num < g_NumAnimations && g_Anims)' then installs row. Full lazy chain: animLoadFrame/Header -> modAnimationLoadData (mod.c:1037) or modAnimationTryCatalogOverride (mod.c:1094) -> catalogResolveAnim -> catalogLoadTypedAsset -> s_catalogLoadEntryAnimationPayload (assetcatalog_load.c:1050-1113, payload_kind ASSET_PAYLOAD_ANIMATION_CLIP).
- animnum->catalog reverse map: port/src/assetcatalog_load.c:43 '#define LOAD_MAX_ANIMS 2048' (file-private); s_AnimnumOverride[2048] keyed by entry->source_animnum (lines 128-131 in catalogLoadInit); catalogResolveAnim(animnum) at 316-346 bounds by LOAD_MAX_ANIMS. Custom slots 1207..1238 need ZERO changes here.
- name->animnum resolve: port/src/loader_pool.c:668-683 s_resolveAnimationCatalogOrEnumName() -- catalog-ID strings (contain ':') return assetCatalogResolve(name)->ext.anim.anim_id when >= 0, else loud RESOLVE_FAIL; plain names go to loaderEnumResolveAnimEnum. Once anim_id holds a custom slot, weapon-graph/AI JSON animation refs play with no change.
- Registration sites leaving custom anim_id = -1 (allocator wiring points): port/src/loader_walker_anim.c:74-108 (walker s_register; sets anim_id/source_animnum/runtime_index only when manifest source_index >= 0; manifest also carries frame_count/bytes_per_frame/header_len/framelen/flags at lines 40-45 -- enough to seed a playable row); port/src/assetcatalog_scanner.c:1833-1844 (INI anim_id default -1); port/src/net/netdistrib.c:1653-1665 (netdistrib mirror). Weapon precedent for scanner wiring: assetcatalog_scanner.c:1736-1748.
- Compile-time base count: generated enum ANIM_END = 0x4B7 = 1207 in Build/src/generated/<ROMID>/animations.h (on include path, CMakeLists.txt:226, tests too at :1016; ROMID fixed at configure, default ntsc-final). Precedent constant NUM_BASE_ANIM_ENTRIES 1207 at port/src/assetcatalog_base_extended.c:225. g_NumAnimations is runtime-derived from the user's ROM (BYOR) -- mismatch must be guarded loudly.
- Pattern files to mirror: port/src/assetcatalog_model_slots.c + port/include/assetcatalog_model_slots.h (B-911, dedup table s_CustomModelCatalogIds[MODEL_CUSTOM_COUNT][CATALOG_ID_LEN], s_allocate(), START+idx or -1 + CATALOG.MODEL.CUSTOM_SLOT_FAIL); constants precedent src/include/constants.h:2314-2316 (MODEL_CUSTOM_COUNT 0x20 / START=NUM_MODELS / END); CATALOG_ID_LEN 64 (port/include/assetcatalog.h:70); reset wiring port/src/assetcatalog.c:371-373, 382-384, 419-421 (assetCatalogClear + both assetCatalogClearMods paths); tests/test_model_slots.cpp (7 cases incl. range pin).
- Boot ordering: port/src/main.c boot phase calls catalogLoadInit() at line 584 (after walker phase 571-580) BEFORE the game thread's animsInit() (port/src/pdmain.c:417 inside mainInit, reached via mainProc at main.c:2409). Mod reload path re-runs catalogLoadInit at port/src/modmgr.c:2264 with g_Anims live. animsReset (src/lib/anim.c:117-122) runs every stage load (src/game/lv.c:684) and re-points g_Anims=g_RomAnims, g_NumAnimations=g_NumRomAnimations.
- RT cache does NOT bind for anims: port/src/assetcatalog_api.c:444 RT_CACHE_SIZE 1024, catalogIdByRuntime returns NULL for runtime_index >= 1024 (line 590) -- base animnums already reach 1206 and grep shows ZERO catalogIdByRuntime(ASSET_ANIMATION,...) callers; anim reverse-resolution is s_AnimnumOverride (2048). Cache writes are range-guarded (api.c:527-528) so slot values 1207+ are safely skipped, not corrupting.
- Wire exposure: NONE. port/src/net/netmsg.c carries no animnum -- chr/bot sync sends position + animation speed multipliers + action state only (netmsg.c:3260-3327; comment at 6737-6745: 'No asset references transmitted'). netdistrib ships the catalog row (anim_id INI key) and each peer allocates its own local slot from the catalog ID -- slots are process-local, no NET_PROTOCOL_VER bump. No save exposure.
- Bounds-check sites on animnum that must learn the custom range: src/lib/anim.c:131 animHasFrames (animnum < g_NumAnimations); port/src/main.c:1496 (debug probe); assetcatalog_load.c:1038 + 1610; mod.c:1014. All other readers (animGetNumFrames anim.c:124, animLoadFrame/Header, model.c:1728-1740 etc.) index g_Anims/g_AnimToHeaderSlot/g_AnimReplacements unchecked -- they work iff the table+side arrays are grown. animGetNumAnimations() (anim.c:134) feeds the chr.c:5151/5159 debug anim cycler -- keep it returning base.
- assetCatalogRegisterAnimation (port/src/assetcatalog.c:1117-1139) passes anim_id through to ext.anim.anim_id + source_animnum; runtime_index stays -1 (default from line 536). ext.anim fields: assetcatalog.h:335 (anim_id, name[64], frame_count, bytes_per_frame, header_len, framelen, flags, target_body[64]).

### Gotchas

- g_Anims is NOT a static array: it's a MEMPOOL_PERMANENT heap copy of the ROM table, and g_Anims == g_RomAnims alias the SAME memory (anim.c:58). Growth must extend that single allocation; do not create a second ROM copy or the assetcatalog_load.c:1612 'restore' semantics change. (That restore is already a self-copy no-op -- pre-existing latent bug, worth a bugs.md note, not in scope to fix.)
- Ordering trap: first-boot catalogLoadInit (main.c:584) runs BEFORE animsInit (pdmain.c:417), so seeding custom g_Anims rows inside catalogLoadInit alone never fires at first boot (g_Anims NULL). Seed from BOTH animsInit-end and catalogLoadInit-end via one helper guarded on g_Anims != NULL.
- animsReset() runs on every stage load (lv.c:684) and resets g_NumAnimations to g_NumRomAnimations -- do NOT implement growth by bumping g_NumAnimations/g_NumRomAnimations (it would also drag the max-bytes calc and population loops past base, violating the pattern). Keep g_NumAnimations = base; use an explicit total (base + ANIM_CUSTOM_COUNT) at the 5 bound sites.
- The custom row must be PRE-SEEDED before first playback: animLoadFrame gates on g_Anims[n].bytesperframe != 0 (anim.c:311) and routes to the replacement loader only when data == 0xffffffff (anim.c:313). A zero row silently yields garbage frame pointers, no error. Seed from the .pdanim manifest envelope fields; exact values are re-installed by s_catalogInstallAnimationClip at first activation.
- ANIM_CUSTOM_START anchor is compile-time ANIM_END (generated per-ROMID) but g_NumRomAnimations is runtime from the user's ROM (BYOR): a mismatched ROM makes base != ANIM_END. animsInit must loud-log the mismatch and allocate max(g_NumRomAnimations, ANIM_END) + ANIM_CUSTOM_COUNT rows so slot indices stay in-bounds either way.
- The program brief's 'slot must stay < RT_CACHE_SIZE=1024' rule CANNOT hold for anims -- base animnums already reach 1206. It is also irrelevant: no caller reverse-resolves ASSET_ANIMATION through catalogIdByRuntime; the anim reverse map is s_AnimnumOverride with LOAD_MAX_ANIMS=2048 (custom end 1239 fits). Flag this deviation in the slots header comment; do not grow RT_CACHE_SIZE.
- LOAD_MAX_ANIMS is private to assetcatalog_load.c:43 -- a _Static_assert(ANIM_CUSTOM_END <= 2048) in the allocator needs the macro hoisted to assetcatalog_load.h or a mirrored literal with a pointer comment.
- modAnimationLoadData sysFatalError()s when a clip fails to build (mod.c:1062, 1089) -- a custom slot whose GLTF fails to compile is a hard fatal at first playback. Acceptable (loud), but means seeded-but-broken customs crash mid-game rather than at load; the activation path already warns at install time if exercised earlier.
- constants.h cannot host ANIM_CUSTOM_* the way it hosts MODEL_CUSTOM_* unless it includes the generated animations.h; put the constants in the new port/include/assetcatalog_anim_slots.h (which includes animations.h) instead -- src/lib/anim.c may include port headers (it already includes mod.h).
- tests pin 3-site wiring for weapons (tests/test_menu_graph.cpp:873-875 checks walker+scanner+netdistrib mention the allocator) -- mirror all three wiring sites for anim or the pattern audit diverges; netdistrib.c:1653 must allocate too or net-distributed custom anims arrive with anim_id=-1 and are unplayable on receivers.

### Plan

DESIGN DECISION -- grow the loaded table, not a parallel custom store. g_Anims[animnum] is raw-indexed in dozens of sites (anim.c, model.c:1728-2407, player.c, chr.c) with no accessor seam, and the existing 0xffffffff-sentinel + g_AnimReplacements machinery already delivers custom payload bytes; a parallel row store would force accessor rewrites everywhere. Additive growth: base rows untouched, custom rows zero-init, population/max-calc loops stay at base count (pattern-conformant with B-911 g_ModelStates[MODEL_CUSTOM_END]).

1) NEW port/include/assetcatalog_anim_slots.h -- mirrors assetcatalog_model_slots.h. Includes "animations.h" (generated, on include path); defines ANIM_CUSTOM_COUNT 0x20, ANIM_CUSTOM_START ANIM_END (=0x4B7=1207 ntsc-final), ANIM_CUSTOM_END (ANIM_END + ANIM_CUSTOM_COUNT); declares assetCatalogResetCustomAnimSlots(void) and s32 assetCatalogResolveAnimPrivateSlot(const char *catalog_id); extern "C" guards; header comment flags the RT_CACHE_SIZE deviation (anim reverse map = s_AnimnumOverride/LOAD_MAX_ANIMS, base already > 1024) and the private-slot/migration-debt rule.

2) NEW port/src/assetcatalog_anim_slots.c -- byte-for-byte mirror of assetcatalog_model_slots.c: static char s_CustomAnimCatalogIds[ANIM_CUSTOM_COUNT][CATALOG_ID_LEN]; same s_allocate() dedup-or-claim; resolve returns ANIM_CUSTOM_START + idx or -1 with LOG_WARNING "CATALOG.ANIM.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom anim slots available (%d)"; _Static_assert(ANIM_CUSTOM_COUNT > 0) and _Static_assert(ANIM_CUSTOM_END <= 2048 /* LOAD_MAX_ANIMS, assetcatalog_load.c:43 */).

3) Reset wiring port/src/assetcatalog.c -- add assetCatalogResetCustomAnimSlots() beside assetCatalogResetCustomModelSlots() at the 3 sites: lines 371-373 (assetCatalogClear), 382-384 (assetCatalogClearMods early-out), 419-421 (assetCatalogClearMods tail).

4) Storage growth src/lib/anim.c animsInit() (include "assetcatalog_anim_slots.h"):
   - s32 total = g_NumRomAnimations + ANIM_CUSTOM_COUNT after the DMA; if (g_NumRomAnimations != ANIM_CUSTOM_START) loud sysLogPrintf(LOG_WARNING, "ANIM.CUSTOM: ROM anim count %d != ANIM_END %d; custom slots may collide") and use max(g_NumRomAnimations, ANIM_CUSTOM_START) as effective base for sizing.
   - Grow the table alloc (line 54): mempAlloc(ALIGN64(tablelen + ANIM_CUSTOM_COUNT * sizeof(struct animtableentry)), MEMPOOL_PERMANENT); after dmaExec, memset(&g_Anims[base], 0, ANIM_CUSTOM_COUNT * sizeof(struct animtableentry)). g_Anims = g_RomAnims = &ptr[1] unchanged (aliasing preserved so animsReset at lv.c:684 stays harmless).
   - Grow per-animnum arrays to `total`: g_AnimToHeaderSlot (line 76), var8005f014 (line 77), g_AnimReplacements (lines 87-88 incl. bzero); extend animsInitTables() first loop bound (line 100) to total. Max-calc loop (63-74) STAYS at base -- replacement-backed customs bypass the LRU byte slots (anim.c:313-319/380-385).
   - Keep g_NumAnimations/animGetNumAnimations() at base (chr.c:5151/5159 debug cycler). Add s32 animGetTotalCount(void) returning base + ANIM_CUSTOM_COUNT (declare in src/include/lib/anim.h); switch animHasFrames (anim.c:131) bound to it.

5) Clip-install clamp changes (custom slot installs instead of clamping):
   - assetcatalog_load.c:1038: anim_id bound becomes animGetTotalCount() (extern via lib/anim.h); same at the unload restore 1610 (for anim_id >= g_NumAnimations skip the g_RomAnims self-copy and instead zero/re-seed the row, still NULL g_AnimReplacements[anim_id]).
   - mod.c:1014: 'num < animGetTotalCount()' instead of g_NumAnimations.
   - main.c:1496 debug probe: same total bound so --debug-probe-animation-source works on customs.

6) Allocator wiring at the 3 registration sites (mirror weapons, pinned by test_menu_graph.cpp:873-875):
   - loader_walker_anim.c s_register, in the e=assetCatalogGetMutable(id) block (lines 91-108): when source_index < 0, s32 slot = assetCatalogResolveAnimPrivateSlot(id); if (slot >= 0) { e->ext.anim.anim_id = slot; e->source_animnum = slot; e->runtime_index = slot; } (feeds s_AnimnumOverride + loader_pool name resolve with zero further changes).
   - assetcatalog_scanner.c:1834-1837: if iniGetInt anim_id < 0, same allocator call (pattern of weapons at 1736-1748), set anim_id/source_animnum/runtime_index.
   - netdistrib.c:1653-1656: identical mirror.

7) Row seeding so customs are playable pre-activation: new void catalogSeedCustomAnimRows(void) in assetcatalog_load.c (near catalogLoadInit) -- if (!g_Anims) return; for each occupied+enabled ASSET_ANIMATION entry with anim_id in [ANIM_CUSTOM_START, animGetTotalCount()): write g_Anims[anim_id] = { .numframes = ext.anim.frame_count, .bytesperframe = ext.anim.bytes_per_frame, .data = 0xffffffff, .headerlen = ext.anim.header_len, .framelen = ext.anim.framelen, .flags = ext.anim.flags } and g_AnimReplacements[anim_id] = NULL. Call at end of catalogLoadInit (covers modmgr.c:2264 reloads) AND end of animsInit (covers first boot, where catalogLoadInit at main.c:584 ran before g_Anims existed). Playback then flows: catalog-ID name -> loader_pool resolve -> chrTryStartAnim/modelSetAnimation -> animLoadHeader/Frame -> data==0xffffffff -> modAnimationLoadData -> catalogResolveAnim(slot) -> clip compile -> s_catalogInstallAnimationClip overwrites the seed with exact compiled values.

8) NEW tests/test_anim_slots.cpp mirroring tests/test_model_slots.cpp exactly (7 cases, tag [catalog][anim][slots][c3849]): in-range, dedup-same-id, distinct-ids-distinct-slots, empty/null rejected, exhaustion -1 + dedup-when-full + first-slot pin, reset-clears-reservations, range pin REQUIRE(ANIM_CUSTOM_START == ANIM_END) && REQUIRE(ANIM_CUSTOM_END == ANIM_END + ANIM_CUSTOM_COUNT) (+ pin ANIM_CUSTOM_END <= 2048). Include "assetcatalog_anim_slots.h" extern "C"; generated animations.h is on the tests include path (CMakeLists.txt:1016).

WIRE FLAG: animnum never crosses the wire -- netmsg.c syncs chr/bot animation via action state + speed multipliers only (netmsg.c:6743 'No asset references transmitted'); netdistrib ships catalog rows and each peer allocates its own local slot from the catalog ID. No NET_PROTOCOL_VER bump, no test_versions.cpp pin change. Private slots must never be serialized (same migration-debt rule as the model/body/head headers).

## Wave 2d -- stagenum allocator

### Key facts

- stageTableAppend: src/game/stagetable.c:138 — `s32 stageTableAppend(const struct stagetableentry *entry)`; realloc-grows heap g_Stages by 1, copies *entry, returns new stage-table index (g_NumStages++), -1 on realloc fail. ZERO callers (only decl at src/include/game/stagetable.h:11). Capacity unbounded (heap).
- Stage table: s_StagesInit static initializer has 87 rows (0x00..0x56) at stagetable.c:15-108; g_Stages/g_NumStages heap copies at :111-112 (stageTableInit :115). stageTableReset :154 discards appended rows; its ONLY caller is modmgrUnloadAllMods at port/src/modmgr.c:2063. stageGetIndex(stagenum) :183 linear-scans g_Stages[i].id; stageGetCurrent :169. No NUM_STAGES compile constant exists — bounds are runtime g_NumStages.
- Index domains (context/constraints.md:115-125): stage table index 0-86, solo index 0-20, stagenum = arbitrary logical ID. Base stagenums occupy 0x01..0x5f (src/include/constants.h:4026-4073, max = STAGE_EXTRA26 0x5f; STAGE_MP_RANDOM/MULTI/SOLO/GEX meta tokens = 0x01..0x04; 0 = universal 'none/fail' sentinel e.g. netmsg.c:5321).
- Scanner raw INI default -1: port/src/assetcatalog_scanner.c:1583 (ASSET_MAP `e->ext.map.stagenum = iniGetInt(ini, "stagenum", -1)`), :1667 (ASSET_ARENA), :1913 (ASSET_SCENARIO). Arena also parses scenario_id at :1668. Wire-delivery mirror populateExtFromIni: port/src/net/netdistrib.c:1435 (MAP), :1488 (ARENA), :1808 (SCENARIO) — must be wired identically (file header says 'field-for-field parity', netdistrib.c:1426-1428).
- What -1 breaks: (u8)-1 = 0xFF flows at netmsg.c:5321/:5960, port/src/net/matchsetup.c:99/:850-852, src/game/mplayer/mplayer.c:885 → stageGetIndex(0xFF) = -1 (stagetable.c:193 warning) → stage load dead. Random pool src/game/mplayer/setup.c:253 pools (s16)e->ext.arena.stagenum with NO >0 filter → -1 enters random pick. main.c:694-701 bootResolveStageIdToNum rejects (requires >0). assetCatalogActivateStage treats <=0 as deactivate (assetcatalog_resolve.c:219-230).
- WIRE: stagenum NEVER crosses the wire. CLC_LOBBY_START write explicitly voids it — netmsg.c:4883-4885 `(void)stagenum; /* stage identity comes from g_MatchConfig.stage_id */`; server read netmsg.c:5310-5331 reads stage_id string, resolves locally. SVC_STAGE_START write netmsg.c:1198-1242 writes session ref `catalogWriteAssetRef(dst, sessionCatalogGetId(g_MpSetup.stage_id))`; client read netmsg.c:1456-1472 catalogReadAssetRef → catalogResolveStageBySession → LOCAL sr.stagenum. Lobby-settings read netmsg.c:5957-5985 reads arena catalog ID string. Both ends mint independently; disagreement is harmless because only strings/session refs are serialized.
- SAVE: the one persistence leak is the MP-setup save file 7-bit stagenum field — src/game/mplayer/mplayer.c:4626 `savebufferOr(buffer, g_MpSetup.stagenum, 7)`, read :4523 `savebufferReadBits(buffer, 7)`, overview :4687. 7 bits = 0..0x7F: any stagenum >0x7F silently truncates; even in-range minted values are machine-local (allocation-order dependent) and go stale across mod-set changes. No stage_id string in the save format (active constraint — do not change format).
- Allocator pattern to mirror EXACTLY: port/src/assetcatalog_model_slots.c + port/include/assetcatalog_model_slots.h (B-911) — static char table[COUNT][CATALOG_ID_LEN]; s_allocate() dedup-by-id then first-free; resolve returns START+idx or -1 with LOG_WARNING `CATALOG.MODEL.CUSTOM_SLOT_FAIL`; reset = memset. Reset wired at the 3 assetcatalog.c sites: port/src/assetcatalog.c:371-373 (assetCatalogClear), :382-384 (ClearMods early-return), :419-421 (ClearMods normal), includes at :30-32. Constants pattern constants.h:2314-2316 (MODEL_CUSTOM_COUNT 0x20 / START / END).
- runtime_index for ASSET_MAP = stage-table INDEX, not stagenum: assetcatalog_base.c:474 sets base maps' runtime_index = g_Stages idx (registration via assetCatalogRegisterMap(id, g_Stages[idx].id /*logical*/, ...) at :460-464; server skips when g_NumStages==0 per :448-451). s_fillStageResult (assetcatalog_api.c:81-114) indexes g_Stages[e->runtime_index] for bgfileid/padsfileid/setupfileid/mpsetupfileid/tilefileid, only builds romProviderHandle when fileid > 0 (:100-104), and has a g_Stages==NULL server fallback (:105-113). catalogBuildRuntimeCaches pass-1 (api.c:523-531) caches by runtime_index < RT_CACHE_SIZE=1024 (api.c:444); appended rows land at index 87..118 — safe. catalogStageIdByStageTableIndex api.c:609-611; catalogStageIdByStagenum api.c:622-634 pool-scans ext.map.stagenum.
- Test template: tests/test_model_slots.cpp — 7 cases (in-range, dedup-same-id, distinct-ids, empty/null reject, exhaustion -1 + dedup-when-full, reset-reuse, range-relationship asserts). Sibling tests/test_body_head_slots.cpp. extern "C" include of constants.h + the slots header; catch.hpp tags `[catalog][stage][slots][c3849]`.
- Body/head precedent for non-constants.h constants: CATALOG_MGR_BODY_CUSTOM_COUNT 32 / _START = base count at port/include/catalog_mgr_bodies.h:58-60 — stage constants can live in constants.h next to the STAGE_* block (constants.h:4073).
- bootResolveStageIdToNum (port/src/main.c:673-706) accepts numeric stage IDs only when v > 0 && v < 0x100 — minted range must stay under 0x100; g_MpSetup.stagenum and g_MissionConfig.stagenum are u8.

### Gotchas

- Stage family diverges from the model/body/head pattern in two ways: (1) _START is NOT 'base count' — stagenum is a sparse logical-ID space, so START must clear the base logical max 0x5f, not the 87-row table size; (2) there is no fixed storage array to grow — g_Stages is heap-realloc'd, so stageTableAppend IS the storage-grow step.
- 7-bit save field (mplayer.c:4626) caps the usable mint range at 0x7F. STAGENUM_CUSTOM_START 0x60 + COUNT 0x20 = END 0x80 exactly fills the headroom. Anything above 0x7F silently truncates on save; even in-range saved values are machine-local → guard the load site (mplayer.c:4523), not the format.
- One logical stage = up to three catalog IDs (map / arena / scenario). Naive dedup-by-own-id mints DIFFERENT stagenums for an arena and its scenario, breaking assetCatalogFindModMapByStagenum pairing (assetcatalog_resolve.c:189-195). Mint key for ASSET_ARENA must be ext.arena.scenario_id when non-empty, else own id.
- Only mint on the default-(-1) path. An explicit INI stagenum pointing at a base stage (how all dev-mods author today) must pass through untouched — minting there would orphan the content from its base-stage geometry.
- Dedicated server: stageTableInit never runs, g_NumStages==0, g_Stages==NULL (assetcatalog_base.c:448-451, s_fillStageResult :105-113). The stageTableAppend wiring must be guarded `if (g_Stages != NULL)`; the allocator itself (pure dedup table) is server-safe.
- Reset desync: allocator reset lives at the 3 assetcatalog.c sites, but stage-table truncation lives in modmgrUnloadAllMods (modmgr.c:2063). assetCatalogClearMods can run without stageTableReset → duplicate appended rows. Make the append idempotent: only append when stageGetIndex(minted_stagenum) < 0, else reuse the existing row index.
- Do not set out-of-band runtime_index before the table row exists: s_fillStageResult dereferences g_Stages[e->runtime_index] unchecked against g_NumStages (api.c:91-97) — runtime_index must be exactly the index stageTableAppend returned (or -1).
- Appended row file IDs must be -1, not copied from the template base row — s_fillStageResult only builds ROM handles when fileid > 0; copied Skedar fileids would load Skedar bg/tiles instead of routing to the scenario-source path (scenarioSourceLoad*ForStage).
- netdistrib.c populateExtFromIni is a deliberate field-for-field mirror of the scanner — wiring only the scanner regresses wire-delivered mods (B-class parity bug).
- Random pool (setup.c:253) has no stagenum>0 filter: today -1 pollutes it; after minting, custom arenas become random-pick candidates. Decide explicitly (likely desired) and add the >0 filter regardless as the -1 backstop.
- Never mint 0x00 (fail sentinel) or 0x01-0x04 (STAGE_MP_RANDOM* meta tokens) — guaranteed by START=0x60, assert it in tests.
- Commit message format: '<Pillar> - c3849: <summary>' per .githooks/commit-msg; build verify is full link via `ninja -C Build pd pd-tests` (pd-server deprecated).

### Plan

## Wave 2d — stagenum private-slot allocator (mirrors B-911 model slots)

### 1. Constants — src/include/constants.h (insert after STAGE_EXTRA26, line 4073)
```c
/* c3849: private custom-stage stagenum range. Base logical IDs occupy
 * 0x01..0x5f (STAGE_EXTRA26); 0x00 is the fail sentinel and 0x01..0x04 are
 * the STAGE_MP_RANDOM* meta tokens. The mpsetup save file stores stagenum
 * in 7 bits (mplayer.c savebufferOr(...,7)), so the range must end at 0x80. */
#define STAGENUM_CUSTOM_START 0x60
#define STAGENUM_CUSTOM_COUNT 0x20
#define STAGENUM_CUSTOM_END   (STAGENUM_CUSTOM_START + STAGENUM_CUSTOM_COUNT) /* 0x80 */
```

### 2. New header — port/include/assetcatalog_stage_slots.h
Mirror assetcatalog_model_slots.h verbatim structure (extern "C" guards, doc block). Doc block must state: minted stagenum is a machine-local runtime bridge ONLY; wire identity is the catalog ID string / session ref (netmsg.c:4883-4885, :1227-1242); it must never be serialized to wire, manifest, or the 7-bit mpsetup save field. Declares:
```c
void assetCatalogResetCustomStageSlots(void);
s32  assetCatalogResolveStagenumPrivateSlot(const char *catalog_id);
```

### 3. New source — port/src/assetcatalog_stage_slots.c
Mirror assetcatalog_model_slots.c exactly: `static char s_CustomStageCatalogIds[STAGENUM_CUSTOM_COUNT][CATALOG_ID_LEN];`, copy of s_allocate() (dedup-by-id then first-free), resolve returns `STAGENUM_CUSTOM_START + idx` or -1 with `sysLogPrintf(LOG_WARNING, "CATALOG.STAGE.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom stage slots available (%d)", ...)`. Add:
```c
_Static_assert(STAGENUM_CUSTOM_COUNT > 0, "...");
_Static_assert(STAGENUM_CUSTOM_START > STAGE_EXTRA26, "must mint outside base logical range");
_Static_assert(STAGENUM_CUSTOM_END <= 0x80, "must fit the 7-bit mpsetup save field");
```

### 4. Reset wiring — port/src/assetcatalog.c
Add `#include "assetcatalog_stage_slots.h"` beside lines 30-32; add `assetCatalogResetCustomStageSlots();` beside the existing three resets at :371-373, :382-384, :419-421 (all three sites, same position pattern).

### 5. Mint + stageTableAppend consumer — port/src/assetcatalog_scanner.c
Add a static helper (top of file, near other helpers):
```c
/* c3849: mint a private stagenum for a custom stage component that authored
 * no INI stagenum, and (client only) ensure a g_Stages row exists so
 * stageGetIndex/stageGetCurrent/s_fillStageResult resolve it. Returns the
 * minted stagenum or -1 (already loudly logged). */
static s32 s_mintCustomStagenum(asset_entry_t *e, const char *mint_key)
{
    s32 stagenum = assetCatalogResolveStagenumPrivateSlot(mint_key);
    if (stagenum < 0) return -1;
    if (g_Stages != NULL) {                      /* dedicated server: skip */
        s32 idx = stageGetIndex(stagenum);       /* idempotent re-scan guard */
        if (idx < 0) {
            struct stagetableentry tmpl = *stageGetEntry(stageGetIndex(STAGE_MP_SKEDAR));
            tmpl.id = stagenum;
            tmpl.bgfileid = tmpl.tilefileid = tmpl.padsfileid = -1;
            tmpl.setupfileid = tmpl.mpsetupfileid = -1;  /* force scenario-source path */
            idx = stageTableAppend(&tmpl);
        }
        if (idx >= 0) e->runtime_index = idx;    /* 87..118 < RT_CACHE_SIZE=1024 */
    }
    return stagenum;
}
```
(include src/include/game/stagetable.h; verify exact stagetableentry member names against types.h before writing — s_fillStageResult at assetcatalog_api.c:93-97 confirms bgfileid/padsfileid/setupfileid/mpsetupfileid/tilefileid.)

Wire the three parse sites — mint ONLY when INI omitted stagenum (default -1):
- :1583 ASSET_MAP: `if (e->ext.map.stagenum < 0) e->ext.map.stagenum = s_mintCustomStagenum(e, idbuf);`
- :1667 ASSET_ARENA (after scenario_id strncpy at :1668-1669 — move the stagenum line below it): `if (e->ext.arena.stagenum < 0) e->ext.arena.stagenum = s_mintCustomStagenum(e, e->ext.arena.scenario_id[0] ? e->ext.arena.scenario_id : idbuf);` — keys arena to its scenario so both share one stagenum.
- :1913 ASSET_SCENARIO: `if (e->ext.scenario.stagenum < 0) e->ext.scenario.stagenum = s_mintCustomStagenum(e, idbuf);`
On allocator exhaustion the value stays -1 — existing <=0 guards keep it inert and the allocator already logged CATALOG.STAGE.CUSTOM_SLOT_FAIL.

### 6. Wire-delivery parity — port/src/net/netdistrib.c populateExtFromIni
Identical minting at :1435 (MAP), :1488 (ARENA, scenario_id-keyed), :1808 (SCENARIO). Either export the helper from the scanner (non-static, declared in a scanner header) or duplicate the 15-line helper with a parity comment — match the file's existing mirroring convention. g_Stages guard inside the helper covers the dedicated server.

### 7. -1 backstop — src/game/mplayer/setup.c:253
In randomPoolCollect, skip entries with `e->ext.arena.stagenum <= 0` before pooling (today -1 enters the random pool unfiltered). Minted customs (>0) remain eligible — intended.

### 8. Save guards (the ONLY persistence exposure) — src/game/mplayer/mplayer.c
- Read :4523: after `g_MpSetup.stagenum = savebufferReadBits(buffer, 7);` add: `if (g_MpSetup.stagenum >= STAGENUM_CUSTOM_START) { sysLogPrintf(LOG_WARNING, "MPSETUP: saved stagenum 0x%02x is a machine-local custom slot — resetting to default", ...); g_MpSetup.stagenum = STAGE_MP_SKEDAR; g_MpSetup.stage_id[0] = '\0'; }` (slot↔stage mapping is allocation-order dependent; a saved slot is meaningless on reload).
- Write :4626: leave the 7-bit format untouched (active save-format constraint; range END<=0x80 guarantees no truncation); add a LOG_WARNING when writing a custom-range stagenum so the staleness is visible.
Do NOT touch the wire: netmsg.c already carries stage identity as stage_id string (CLC_LOBBY_START :4883-4885, :5310-5331) and session ref (SVC_STAGE_START write :1227-1242 / read :1456-1472) — no change needed, verified.

### 9. Tests — tests/test_stage_slots.cpp (mirror tests/test_model_slots.cpp, tags [catalog][stage][slots][c3849])
Seven cases: in-range [START,END); dedup same id; distinct ids distinct slots; empty/null reject (-1); exhaustion after STAGENUM_CUSTOM_COUNT distinct ids loud-fails -1 + previously-seen id still dedups; reset clears (new id reuses first slot); range-relationship: `REQUIRE(STAGENUM_CUSTOM_START == 0x60); REQUIRE(STAGENUM_CUSTOM_START > STAGE_EXTRA26); REQUIRE(STAGENUM_CUSTOM_END <= 0x80);` plus first mint == STAGENUM_CUSTOM_START. Pure-allocator only (no g_Stages dependency) so it links in pd-tests like the model test.

### 10. Verify + context
`ninja -C Build pd pd-tests` (full link; no pd-server). Update context/tasks.md (Wave 2d), context/pillars/modding.md or catalog pillar (stagenum bridge closed: 8/13 → integer-bridge row), and the migration-utilization audit's fact #4 (stagenum allocator now exists; stageTableAppend has its first caller). Commit `Modding - c3849: stagenum private-slot allocator (Wave 2d)`.

## Wave 3 -- FONT consumer (emitter repair + runtime compiler)

### Key facts

- WHICH WORLD: neither (a) nor (b) exactly — .pdfont embeds DECODED public source: glyphs.pgm (8-bit grayscale atlas, value = CI4 nibble*17) + font.metrics.json (per-glyph index/baseline/height/width/kerning_index/atlas x,y + full 13x13 kerning table) per romextract_pdfont.c:14-17, 222-345. No font.otf, no native payload member. The native struct font is LOSSLESSLY reconstructible from these two members with a ~300-line bitmap compiler (PGM read + JSON parse + CI4 nibble repack v/17) — no OTF rasterizer, no generated-cache bridge needed. The walker's 'font.otf' default (loader_walker_font.c:36) is a dead fallback for future modder vector fonts; base manifests carry 'glyphs' (manifest key at romextract_pdfont.c:432-436).
- Native format the renderer consumes: struct font { s32 kerning[169]; struct fontchar chars[94/135] } (types.h:5623-5626); fontchar = {u8 index, s8 baseline, u8 height, u8 width, s32 kerningindex, u8 *pixeldata} (types.h:5606-5621; JPN variant u16/s16). pixeldata is raw CI4, fixed 8-byte row stride, consumed directly as a CI texture via gDPSetTextureImage (game_1531a0.c:1546, 1793, 1908, 2315). textLoadFont mempPCAllocs len bytes, dmaExec-copies the (already PC-converted) segment, then patches chars[i].pixeldata += (uintptr_t)font (:309-326), applies JPN baseline++/monospace/PAL pipe tweaks (:330-352, :383-391), and caches by romstart in a persistent process-lifetime cache (:249-258, :361-365).
- Segment->identity mapping: ALL textLoadFont call sites are inside textReset (game_1531a0.c:460-503) using _font<seg>SegmentRomStart symbols; textLoadFont already has a romstart->name if-chain (:287-305) covering all six NTSC faces (handelgothic sm/xs/md/lg, numeric, tahoma) — this is the natural place to derive the catalog id 'base:font_<face>' (id convention at romextract_pdfont.c:375). The walker records face only in category 'font:<face>' (loader_walker_font.c:56-61) and binds primary file 'data/<romid>/fonts/<id>.pdfont::glyphs.pgm' + ext.font.metrics_file '...::font.metrics.json' (loader_walker_font.c:38-50; assetcatalog.h:485-486). fsFileLoad natively supports 'archive::member' chains (fs.c:83-168). Precedent for catalog-resolved public-source runtime compile: langManifestLoadBankFromCatalog (port/src/langmanifest.c:556-568, called from lang.c:408).
- Boot ordering is already correct for catalog-first fonts: segs extracted then ROM released (main.c:449-459), BOOT_PHASE_EMIT_FONT at main.c:554-556 runs BEFORE the walker (main.c:571-580) and caches build (:582-587); textReset runs at stage load, long after catalog readiness.
- JPN glyph path (lang.c:350/364) dma's raw codepoint-indexed pixel banks (0x60/0x80-byte glyphs) from fontjpnsingle/fontjpnmulti segments — these have NO kerning/char-table layout and cannot use the struct-font .pdfont shape; fontjpnmulti isn't even in the emitter face list. Keep on segment path this slice.
- ROMSEG_LIST (romdata.c:122-149) contains exactly: fonttahoma, fontnumeric, fonthandelgothic{sm,xs,md,lg}, fontjpnsingle, fontjpnmulti, fontjpn. bankgothic/zurich/ocramd/ocralg fonts have no segments in the port at all (textReset's externs for them at :416-425 are declared but never referenced).

### Gotchas

- BUG 1 — the emitter currently yields ZERO valid .pdfont files: s_emitOneFont resolves segments by FACE name (romExtractSegmentRelPath(face) -> 'segs/handelgothicsm.bin', romextract_pdfont.c:366) but Pass B writes SEGMENT names ('segs/fonthandelgothicsm.bin', romextract.c:893 + ROMSEG_LIST). Every NTSC face misses silently (return 0 on fsFileSize<=0, :369-371). Only 'fontjpnsingle' matched. Proof: .claude/smoke-verify-cache/ntsc-final/fonts contains ONLY base_font_fontjpnsingle.pdfont while segs/ has all 7 font .bins.
- BUG 2 — even with names fixed, s_buildFontExports parses the WRONG byte layout: it assumes RAW N64 big-endian (s_readBe32, 12-byte chars at offset 676; romextract_pdfont.c:92-95,180-181,251-257), but segs/*.bin for fonts is POST-preprocess PC-native (romdataInitSegment runs preprocessFont at romdata.c:534-545 before romExtractAllSegments dumps seg->data at romextract.c:985, and EMIT_FONT runs after romdataReleaseRom). Actual layout per segfonts.c:42-95: LE kerning s32[169] at 0, fontchar[n] at PD_ALIGN(676,8)=680, 16-byte fontchar, pixeldata field = LE buffer-relative offset. The lone existing .pdfont is a garbage misparse of a raw JPN glyph bank.
- Fast-cache + existing-archive skips will fossilize the garbage: bump PDFONT_FAST_CACHE_KIND 'pdfont_metrics_json_v1'->v2 (romextract_pdfont.c:50, :609-617) AND make the s_existingArchiveHasEntry early-skip (:390-394) require the new schema (manifest pd_schema_version=2), else existing installs never regenerate; remove/stop emitting base_font_fontjpnsingle.pdfont.
- struct fontchar is build-variant (JPN u16 index/s16 kerningindex) and pointer-size dependent — the runtime compiler must fill struct fields, never memcpy fixed offsets.
- Post-load mutation order must be preserved when adding the catalog source: pixeldata base patch (:326) -> JPN baseline++ (:330) -> monospace clamp (:338-352) -> cache registration (:361-365) -> integrity checksum (:374-380) -> PAL pipe baseline (:383-391). Catalog path should only replace the dmaExec byte-source, reusing everything else, or the PAL/JPN tweaks silently diverge.
- PGM round-trip: emitter wrote nibble*17 so v/17 is exact for base archives, but modder-edited PGMs need round-to-nearest ((v+8)/17, clamp 15); raw width is inherently <=16 px (8-byte row stride) — reject metrics declaring wider glyphs loudly.
- Persistent mempPC font cache keyed by romstart means fonts compile once per process; .pdfont edits need restart (same behavior as today — acceptable, but log source=catalog per face so utilization is provable).
- Walker sets category 'base' then overwrites with 'font:<face>' (loader_walker_font.c:27 vs 56-61) — resolve rows by id, not category.

### Plan

Slice: catalog-first font bytes for the six NTSC faces, loud fallback to segment bytes.

Stage 1 — emitter repair (port/src/romextract_pdfont.c):
(1) Replace k_FontFaces with a {face, segname} table: tahoma->fonttahoma, numeric->fontnumeric, handelgothic{xs,sm,md,lg}->fonthandelgothic{xs,sm,md,lg}; DROP bankgothic/zurich/ocramd/ocralg (no segments exist, romdata.c:122-149) and fontjpn/fontjpnsingle (raw glyph banks, wrong shape). Resolve segments via romExtractSegmentRelPath(segname).
(2) Rewrite s_buildFontExports to parse the actual post-preprocess PC-native .bin (per segfonts.c:42-95): LE kerning[169] at 0, fontchar[s_fontNumChars] at PD_ALIGN(676,8), 16-byte fontchar, pixeldata = LE buffer-relative offset to CI4 8-byte-row glyph pixels. Output glyphs.pgm + font.metrics.json unchanged in shape; set manifest/ini pd_schema_version=2.
(3) Bump PDFONT_FAST_CACHE_KIND to v2 and require schema v2 in the existing-archive skip so installs regenerate; delete stale base_font_fontjpnsingle.pdfont.

Stage 2 — runtime compiler + textLoadFont integration:
(4) New port/src/fontcatalog.c: s32 fontCatalogBuildFace(const char *face, u8 **out_payload, u32 *out_len) — assetCatalogResolve("base:font_<face>") (type ASSET_FONT, enabled); fsFileLoad(entry->ext.font.font_file /*::glyphs.pgm*/) + ext.font.metrics_file (::font.metrics.json); parse PGM + metrics JSON; emit a buffer laid out exactly like the preprocessed segment (kerning, struct fontchar[n] with buffer-relative pixeldata offsets, CI4 repack (v+8)/17 clamp 15). Loud sysLoudFailf/LOG_WARNING on every miss (no row, member load, JSON/PGM parse, atlas bounds). Model: langManifestLoadBankFromCatalog (langmanifest.c:556).
(5) textLoadFont (game_1531a0.c:228): extend the existing romstart if-chain (:287-305) to yield the face string; after the persistent-cache check, attempt fontCatalogBuildFace and memcpy the payload into the mempPCAlloc(len,tag) block in place of dmaExec; on failure log "FONT.CATALOG: <face> falling back to ROM segment bytes" and keep the dmaExec path. All downstream fixups (:325-391) unchanged and shared. LOG_NOTE source=catalog per face for utilization proof.

Stage 3 — proof + pins:
(6) Debug one-shot equivalence: on first catalog load also dmaExec the segment into a temp buffer and memcmp; LOG_WARNING on drift.
(7) Tests: pin face->segname table, walker primary '::glyphs.pgm', textLoadFont catalog-first call + fallback string (test_asset_native_source_contract pattern).
(8) Context: bugs.md (two emitter defects), modding/catalog pillar .pdfont utilization 0%->consumed; constraints note that segs/font*.bin becomes fallback-only (generated product) per c3842.

Follow-ups (out of slice): JPN glyph banks need a codepoint-indexed .pdfont shape for lang.c:350/364; OTF vector font mods (walker font.otf fallback) remain unimplemented.

### Size verdict
MEDIUM — ~600-800 lines across an emitter parse rewrite, one new ~350-line runtime compiler, ~60 lines in textLoadFont, plus tests/context; no schema redesign (members stay glyphs.pgm + font.metrics.json), no wire/protocol impact. NOT the LARGE world: no OTF rasterizer needed because .pdfont already carries losslessly-recompilable bitmap source. First shippable slice = Stages 1+2 (six NTSC faces catalog-first with loud fallback + cache-kind bump); Stage 3 equivalence check ideally same merge. Prerequisite reality: two emitter bugs (face/segname mismatch, BE-raw vs PC-native parse) mean today's .pdfont output is invalid, so the emitter fix must land in the same slice or the runtime path would consume garbage.

## Wave 4 -- base-texture public-source emitter

### Key facts

- PREMISE STALE: the base-texture image emitter ALREADY EXISTS. port/src/romextract_pdmeta.c:1167-1270 s_emitTexture emits one .pdtexture ZIP per bundled ASSET_TEXTURE row (texture.ini + texture.png + _meta/manifest.json via s_openWriter/assetArchiveWriterAdd*); dispatched at romextract_pdmeta.c:1888-1890 inside the all-bundled-entries loop at 1987-1995 (romExtractAllPdmeta). Added 2026-05-27 (commit window aec2e736). 3,503 archives verified on disk at .claude/smoke-verify-install/data/ntsc-final/textures/ (count confirmed = 3503).
- Boot wiring exists: port/src/main.c:566-568 BOOT_PHASE_EMIT_META calls romExtractAllPdmeta(0) as the last emit phase before BOOT_PHASE_WALKER; emit-before-walker ordering rationale documented in the B-325 comment block at main.c:~500-510.
- Native decode runs at extraction time with no GBI/render dependency: romextract_pdarena.c:5422-5513 romExtractDecodeTextureImages(texnum) reads romdataSegGetData("textureslist")/("texturesdata"), inflates via the game's own texInflateZlib/texInflateNonZlib (src/game/texdecompress.c) into a synthetic 256KB calloc'd texpool, converts via s_decodeTexToRgba (handles RGBA16/IA16 CI4/CI8 palettes), and PNG-encodes with stbi_write_png_to_mem. PNG writer = port/external/stb_image_write.h (already used by romextract_pdarena.c and pdgui_skin_editor.cpp). Empty ROM slots (romExtractTextureSlotIsEmpty, :5515) emit a 1x1 transparent PNG with empty_rom_slot=true in texture.ini and source_state=empty_rom_slot in the manifest (romextract_pdmeta.c:1190-1208).
- Catalog binding is REGISTRATION-time, not walker-time: port/src/assetcatalog_base_extended.c:686-706 registers all NUM_TEXTURES (3503 NTSC / 3511 JPN) rows, builds primary path 'data/<romid>/textures/<slug>.pdtexture::texture.png' via s_buildArchiveMemberPath (:120-130, format "%s/%s%s::%s") and calls catalogSetPrimaryFile (:700). assetCatalogRegisterTexture sets source_texnum (port/src/assetcatalog.c:1148-1151), catalogLoadInit indexes the texnum reverse map (port/src/assetcatalog_load.c:122-125), catalogResolveTexture (:284-314) returns the FileProvider path. ASSET_TEXTURE is deliberately ABSENT from the walker's s_MetaFamilies table (port/src/loader_walker_meta.c:608-623) — no loader_walker_texture.c exists.
- Runtime consumption is live: src/game/texdecompress.c:2300-2330 texLoad calls modTextureLoadRgba32Source (port/src/mod_texture_source.c:66-187; stb_image decode of png/tga/jpg/bmp; 255x255 header cap at :140-144) then texLoadPublicRgba32Source (texdecompress.c:2153) installs RGBA32 into the texpool; legacy segment dmaExec path (:2354) survives only as fallback for non-resolved texnums. fsFileLoad supports nested 'archive::member' chains (port/src/fs.c:83-168). FileProvider intern pool is PC-scale (16384 paths / 1MB, port/src/assetprovider_file.c:32-33) so all 3503 paths intern.
- THE ACTUAL GAP (skip-if-current): romExtractAllPdmeta WRITES the textures fast-cache stamp (romextract_pdmeta.c:2022-2023) but NEVER reads it — no romExtractPdFastCacheCanSkip call anywhere in pdmeta, while all 11 other family emitters gate on it (mechanism: port/src/romextract_pd_cache.c:107-145, dir fingerprint = count+total_bytes+latest_mtime under schema 'pdasset-fast-v1-20260521'; example consumer romextract_pdmesh.c:2921-2929 + write at :2960-2963). Result: every warm boot re-runs per-entry checks = fsFileSize + 2 modArchiveOpen ZIP opens per texture (romextract_pdmeta.c:1172-1176) ~= 7,000 ZIP opens/boot for textures alone.
- Conformance contract: tools/asset_archive_conformance.py:623-626 — .pdtexture requires texture.ini, require_any of texture.png/.tga/.jpg/.jpeg, allowed list closed; manifest must carry texture_file (B-854 fix, session-log.md:574-580 — conformance rejects descriptor/manifest drift). Family taxonomy rows: port/src/asset_archive_policy.c:51 and asset_mod_utility_contract.c:31. Needler reference archive shape: tools/build_needler_mod.py build_body_texture (:281-297).
- Family already PROVEN end-to-end under c3844: run-all-family-source-matrix.ps1 -Family texture selected all 3,503 .pdtexture archives into 71 source-only batches, all passed (context/session-log.md:17409); public .pdtexture::texture.png loading as RGBA32 runtime textures proven in weapon-match smoke (session-log.md:1160-1166); networked rows get source_texnum parity (session-log.md:18435). Strict conformance over the smoke install: root_archives=7616 / checked=8617 PASS.

### Gotchas

- Do NOT write a from-scratch romextract_pdtexture.c emitter — the family is mined, wired, and matrix-verified. Writing a parallel emitter would create duplicate-source ownership of data/<romid>/textures/. The correct Wave-4 card is extraction+hardening of the existing s_emitTexture, not new mining.
- Emit-on-demand is NOT viable: texLoad's public-source path sysFatalErrors when the bound archive is missing or unreadable (mod_texture_source.c:56-64,115-126 modTextureFatalPublicSourceFailure) — there is no graceful per-texture fallback once the FileProvider primary is bound at registration. Safety today rests entirely on emit-before-walker boot order (B-325). Keep emit-all-at-boot.
- Slug duplication hazard: s_idToFilenameSlug (romextract_pdmeta.c:~70-88, used by emitter output names) and s_catalogIdToFilenameSlug (assetcatalog_base_extended.c:107-118, used by the bound primary path) are two private copies that must produce identical slugs; drift = boot-completes-fine then fatal at first texLoad of the divergent texture, with zero boot-time detection. No test pins this today.
- Source-text test pins will break on file moves: tests/test_asset_native_source_contract.cpp:1095 and :13856 readTextFile("port/src/romextract_pdmeta.c") and OR-check '.pdtexture' across base/meta/header (:13869-13886); c3843 static coverage also pins the texture.ini adjacent-string-literal form (the format-string varargs crash fix, session-log.md:3542). Re-point pins in the same merge as any code move.
- Do not casually add ASSET_TEXTURE to the walker s_MetaFamilies: loaderWalkerScanKind opens every archive's envelope — 3,503 extra ZIP opens at BOOT_PHASE_WALKER. Registration-time binding was the deliberate cheap path; if bind verification is wanted, do a lightweight fsFileSize pass on the archive half of the '::' path instead.
- History: enabling texnum overrides for base rows once routed raw PNG bytes into texLoad's legacy compressed decoder and crashed during body model texture expansion (session-log.md:18194, then fixed per :1160-1166 via texLoadPublicRgba32Source). Any change must keep modTextureLoadRgba32Source as the only entry into texLoad for public sources.
- The fast-cache stamp is mtime/count-based, not content-hashed (PDEXTRACT_CACHE_SCHEMA pdasset-fast-v1). c3842's 'source-hashed rebuildable cache' constraint applies to ENGINE-READY products — for textures there is none (PNG is decoded into the in-memory texpool at load), so the stamp is a skip heuristic over the SOURCE archives; re-emit invalidation is via the kind string (bump like pdmesh's v23 label) plus per-entry member checks.
- romExtractAllPdmeta is compiled out under PD_SERVER (romextract_pdmeta.c:1918-1920); pd-server is deprecated (do not build it in verify), but a split-out file should mirror the guard or rely on server_stubs.c for symbol parity.
- JPN ROM has 3511 textures (NUM_TEXTURES is per-romid compile constant) — never hardcode 3503 in stamps, tests, or progress totals; use the bundled-ASSET_TEXTURE count from the catalog loop.

### Plan

romextract_pdtexture.c plan (refactor + harden, not new mining):

SLICE A — family extraction + dead-stamp fix (first shippable):
1. Create port/src/romextract_pdtexture.c. Move s_emitTexture (romextract_pdmeta.c:1167-1270) verbatim plus per-file copies of s_archiveRelPath / s_existingArchiveHasEntry / k_Transparent1x1Png (per-file private helpers are the established convention). Keep decode via romExtractDecodeTextureImages + romExtractTextureSlotIsEmpty (already public in port/include/romextract_pd.h:421-426).
2. romExtractAllPdtexture(s32 force_rewrite): fsDataDirEnsure; build textures_dir = data/<romid>/textures; fsCreateDir; enumerate assetCatalogGetByIndex(0..count) filtering e->occupied && e->bundled && e->type==ASSET_TEXTURE (same predicate as romExtractAllPdmeta:1987-1990); define ROMEXTRACT_PDTEXTURE_FAST_CACHE_KIND "pdtexture_png_v1_<traits>"; early-out via romExtractPdFastCacheCanSkip(kind, textures_dir, ".pdtexture", force_rewrite) with bootProgressUpdate(n,n) — this is the piece pdmeta never had; else per-entry emit (existing per-archive member checks remain the stale-stamp fallback); on failed==0 romExtractPdFastCacheWrite. Log "romextract pdtexture: written/skipped/failed" in family format.
3. Boot wiring: add BOOT_PHASE_EMIT_TEXTURE to port/include/boot_progress.h enum (after BOOT_PHASE_EMIT_UI:53) + its weight row; in port/src/main.c insert bootProgressBeginPhase(BOOT_PHASE_EMIT_TEXTURE); (void)romExtractAllPdtexture(0); bootProgressEndPhase(); immediately before BOOT_PHASE_EMIT_META (:566). Remove the ASSET_TEXTURE case from s_emitEntryArchive (:1888-1890) and the textures stamp write (:2022-2023) from pdmeta. Prototype in port/include/romextract_pd.h beside romExtractAllPdmeta.
4. Re-point source-text pins: tests/test_asset_native_source_contract.cpp:1095/:13856 family OR-checks and the c3843 texture.ini literal pin must include the new file.

SLICE B — bind verification (Gate-5 style, mirrors B-908): after emit (or end of romExtractAllPdtexture), iterate bundled ASSET_TEXTURE rows, split primary path at "::", fsFileSize the archive half; loud-log/flag misses at boot instead of deferring to texLoad-time sysFatalError. Add a static test pinning byte-identical slug logic between s_idToFilenameSlug and s_catalogIdToFilenameSlug (or unify into one shared helper).

SLICE C (optional, measured): collect-then-fan-out threading for first-run extraction (3,503 decode+PNG encodes), matching the 12 existing emitter fan-outs from Engine Startup Phase 4; only if cold-boot timing shows the serial loop matters.

CONFORMANCE GATES per merge: python tools/asset_archive_conformance.py (strict, .pdtexture schema :623-626 — texture.ini required, texture.png|tga|jpg|jpeg any, manifest texture_file parity per B-854); python tools/asset_native_source_guard.py; focused [modding][pdxxx][c3842] + [c3843] static suites; runtime proof via tools/smoke-verify/run-all-family-source-matrix.ps1 -Family texture (3,503 archives / 71 batches) against the patched binary; fresh-install boot must still report 3,503 (NTSC) emitted and zero texLoad fatals.

### Size verdict
SMALL-to-MEDIUM — and a re-scope: the "largest unmined family" is already mined, wired, and matrix-verified (3,503 .pdtexture archives emit at BOOT_PHASE_EMIT_META, bind via registration-time FileProvider primaries, and load as RGBA32 through texLoad). Wave 4 collapses to a hardening card. First shippable slice = SLICE A: split the texture family out of romextract_pdmeta.c into romextract_pdtexture.c and wire the never-read fast-cache stamp through romExtractPdFastCacheCanSkip — one new C file, three touched files (main.c, boot_progress.h, romextract_pd.h) plus pdmeta deletion and test re-pins, eliminating ~7,000 warm-boot ZIP opens. SLICE B (boot bind verification + slug-parity pin) is the follow-up; SLICE C (emit fan-out) only if cold-boot timing justifies it. Emit-all at boot is the only safe model — emit-on-demand would race texLoad's fatal-on-missing-source contract.
