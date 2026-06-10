# c3849 Wave Implementation Maps (preserved 2026-06-10)

> Source-grounded plan fragments. Waves 1-4 (telemetry, all four allocators,
> FONT consumer, texture emitter extraction) are SHIPPED; their maps below are
> historical. The Wave 5/6 maps (appended 2026-06-10, 8-agent mapping round +
> cross-map critique) are the live implementation contract. Every claim was
> file:line-verified at map time; re-verify anchors before editing (lines drift).


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

---

# WAVE 5/6 MAPS (8-agent round, 2026-06-10, this session)

> Eight mapping agents (guidance / surface / impact-trail-transition /
> entity-armed-trigger / entity-deployed / settings-presentation /
> meta-families / pdeffect) plus a completeness critic. The BINDING SPECS
> section below is the cross-map contract every implementation slice must
> follow; the per-cluster maps follow it. The dead-field census source is
> context/audits/migration-utilization-measurement-2026-06-10.md fact 6/7.

## BINDING SPECS (critic-reconciled, mandatory)

### B1. weaponTick custom arm ordering (propobj.c)

Verified OG chain: rocket/slayer arms -> :4876 B-912 contact arm -> :4888
TIMEDMINE -> :4902 REMOTEMINE -> proximity/nbomb arm closing :5044 -> :5045 BOLT.

1. OG rocket/slayer arms: untouched.
2. CUSTOM PROJECTILE ARM (replaces/extends the :4876 B-912 arm; owner: surface
   cluster). Entry: any projectile-record-scoped helper hit (wall-hugger ||
   contact-impact || fuse-timer), NOT bare graph presence. Internal priority,
   fixed: (a) wall-hugger state machine -> (b) contact-impact detonation
   (exptype via the B2 shared resolver) -> (c) fuse/flight timer decrement +
   expiry (surface timer module + impact U4 merged).
3. OG TIMEDMINE, 4. OG REMOTEMINE: untouched.
5. OG proximity/nbomb arm: untouched except entity-deployed's in-arm proxy
   widen (:4957-4988) for storm_activation_policy == proximity, graph-guarded.
6. CUSTOM ENTITY ARM (new else-if after :5044, before BOLT; owner:
   entity-armed cluster). Sub-branches: remote -> timed -> proxy -> storm ->
   shared detonation block. Entity-deployed's nbomb-storm arm lives HERE, not
   after the B-912 arm.
7. Mutual exclusivity is by IR record type; a graph authoring both projectile
   fuse AND entity timed records gets a parse-time LOG_WARNING; projectile arm
   wins (matches OG chain position).

### B2. Unified explosion/spark ref resolver

ONE shared parse-time resolver (weaponGraphResolveExplosionRef, port-side,
single table) serves impact_explosion_ref, entity explosion_ref, and
wall_explosion_ref. Token set = the explicit EXPLOSIONTYPE names
(base:explosion_rocket->13, base:explosion_huge->17, base:explosion_sdgrenade->21,
base:explosion_phoenix->22, base:explosion_dragonbombspy->23) plus aliases for
the impact cluster's spellings with deprecation LOG_WARNING. The pdeffect
class words (tiny->6, small->2, medium->11, large->13, huge->17, massive->25)
are legal ONLY inside .pdeffect graph bodies, never in *_explosion_ref fields.
base:-prefixed refs resolve at parse into derived s32 fields (impact_exptype /
armed_exptype). Non-base refs are stored and resolved at detonation via
effectGraphResolveExplosionType(ref, fallback) which returns the fallback when
the toggle is OFF / ref unresolved, so Wave-5 consumers ship the call before
the pdeffect runtime lands. Same pattern for spark refs via
effectGraphResolveSparkType.

### B3. Guard discipline (the systemic risk)

Base-emitted graphs DO carry wall_hugger/homing/fbw/entity records with empty
params. Every consumer guards on PARAM PRESENCE (has_* bits, value > 0,
sentinel != -1), never record presence, in addition to the custom-slot guard
(weaponnum >= WEAPON_CUSTOM_START) where the OG arm would double-handle.
Entity-deployed Step 0 sentinels (-1 pre-set for absent bools) are the
discipline for absent-vs-authored-0 ambiguity. All string policies latch to
s32 enums at parse/registration (impact_filter included - no per-event strcmp).
Acceptance gate for the weaponTick units: toggle-ON base-parity review across
grenade, nbomb, timed/remote/proxy mine, laptop.

### B4. Double-claim resolutions

- impact_stick_on_hit: SURFACE owns (absorbed into its stick-gate entry
  condition, gated !impact_consume_on_hit).
- timer_on_expire: SURFACE owns latch + arm; impact U4 semantics fold in.
- recover_weapon_ref + autogun recover: ENTITY-DEPLOYED's g_ThrownLaptopLatch
  sidecar wins (NO autogunobj struct growth - B-323 stride class; verify
  in-place access during implementation).
- bondgun.c:5294 laptop-gate widen: one edit, entity-deployed implements,
  impact U5 transition semantics layer on top in the same unit.
- weaponGetCustomProjectileGraph helper: build once (impact U1 text).

### B5. Implementation unit order

- Unit 0 (bug-fix class, NOT toggle-gated): 0a timed_timer_ticks60 zero-guard
  (bondgun.c:1968/:2018; base Timed Mine first-tick detonation with toggle ON);
  0b descriptor *_file alias fix (weapon_graph_archive.c:300-305; shared
  context silently dropped from every walker-path held IR today); 0c dangling
  targetprop fix (projectilesUnrefOwner).
- Unit 1 (shared substrate): 1a record-scoped helper family; 1b unified
  resolver + frozen pdeffect bridge signatures; 1c one registration-path pass
  (all derived/latched fields + sentinels for all clusters); 1d this spec
  committed (this document).
- Unit 2: settings/variables/presentation parse + accessors (5f-2, 5f-4).
- Unit 3: guidance (struct projectile fields + latches; homing gains;
  trajectory; fbw pre-wiring; AI launcher widen + wrong-owner fix last).
- Unit 4: surface (combined custom projectile arm per B1; stick-gate
  restructure incl. impact_stick_on_hit; :18206 pickup mirror).
- Unit 5: impact remainder (filter/hit_sound/exptype/spark via Unit-1
  resolvers; trail).
- Unit 6: entity-armed S2 (custom entity arm; objDamage/objFree; detonator
  provenance; clear sites propobj.c:21852 + setup.c:358).
- Unit 7: entity-deployed + folded impact U5/U6 (autogun completion; storm in
  Unit 6's arm; owner-cleanup; sticky-device after Unit 4; interaction +
  recover combined :17947 edit).
- Unit 8: pdeffect (nested-ingestion fix first; schema/opcodes; runtime;
  bridges; spark registry/pink). Parallel track once 1b freezes signatures.
- Unit 9: settings consumers (defaults layering, camera_effect, MPOPTION bit).
- Unit 10: meta-families (independent breather commit).

Parallel tracks after Unit 1: (3->4->5), (6->7), (2->9 / 8 / 10).

### B6. Decisions taken (tactical calls this session; flag to Mike in summary)

1. homing_lost_target_behavior "detonate" interpretation (guidance Slice F):
   DEFERRED; validate-only this wave.
2. trajectory_max_angle units: DEGREES (authored), converted via DTOR at latch.
3. Variables substitution syntax: $name, compile-time only, unresolved ref =
   loud compile error.
4. Canonical schema spellings: pd.weapon_settings.v1 / pd.weapon_variables.v1
   (base emitter's current strings); needler spellings accepted with
   LOG_WARNING; needler builder converges same wave. Effect graphs: canonical
   pd.effect_graph.v1, base emitter updated, legacy pd2.effect.graph.v1
   accepted with LOG_WARNING.
5. MPOPTION_WEAPONGRAPH 0x20000000: rides existing g_MpSetup.options u32 (no
   wire bump, BOTJUMP precedent), MASKED at the MP-setup save site (transient
   debug semantics; not persisted).
6. fire_cadence unit: rpm; recorded in weapon-graph-module-parameters.md.
7. wall_post_fall_timer60: DEFERRED (no OG backend; needs weaponobj growth).
8. coop/anti/self_attached/owner_slot_source non-empty vocabularies: DEFERRED
   (parity-default only).
9. Pink-spark custom-row registry: IN SCOPE for Unit 8 slice 1 (the proving
   asset payoff; only sparks.c touch).

### B7. Unclaimed-field dispositions (coverage ledger)

- trajectory_aim_source / trajectory_solve_velocity: subsumed by the
  FUNCFLAG_CALCULATETRAJECTORY flag wiring; validate-only beyond that.
- sticky_embed_policy: surface owns; DEFERRED this wave (embedment is
  structurally chr-model-only via objEmbed).
- sticky_disable_policy: entity-deployed owns ("shootable" skips invincibility
  flags at the landing arm; other values deferred).
- proxy_on_trigger: entity-armed; "detonate" consumed via the shared
  detonation block; other values deferred.
- wall_explosion_ref: SURFACE owns the consume site (wall expiry calls
  effectGraphResolveExplosionType(wall_explosion_ref, EXPLOSIONTYPE_ROCKET),
  fallback-shimmed from day one).
- sticky_on_attach: single owner = entity-armed ledger entry (timed_starts
  on_attach class); deferred this wave.

### B8. Incidental findings to record in bugs.md

- bondgun.c:1968-1970/:2018-2020 timed_timer zero-guard miss (Unit 0a).
- weapon_graph_archive.c:300-305 descriptor key drift dropping
  shared-context.json on the walker path (Unit 0b).
- propobj.c:7928 duplicated operand (WEAPON_COMBATKNIFE || WEAPON_COMBATKNIFE;
  context suggests one should be WEAPON_BOLT). Pre-existing decomp quirk; log
  only, do not fix in this wave.
- assetcatalog_load.c:1612 anim restore self-copy no-op (recorded earlier).
- Modding Hub presentation template authors zoom_fovs[] array but the compiler
  consumes scalar zoom_fov (editor-authored zoom dead on arrival).

## Wave 5a - guidance (homing detail, fly-by-wire numerics, trajectory)

### Key facts
- Toggle: Debug.WeaponGraphRuntime default 0 (weapon_graph_runtime.c:42, registered :148-152); every *ForGameplay accessor returns NULL when off (:674-695). Projectile runtime records are zero-initialized at alloc (projectileRuntimeAlloc :601-617), so unset numeric params read 0.0 - "0 means unset, fall back to OG literal" is the dormant-safety convention.
- IR population: projectileRuntimeFromNode - trajectory at :2134-2143 (has_trajectory_correction, aim_source, solve_velocity parsed as BOOL, max_angle), homing at :2144-2162 (five strings + steering_gain/damping), fly-by-wire at :2164-2181 (three strings + six numerics).
- Base emitter authors ONLY strings for these modules: homing {target_source:"current_lock", runtime_constants:"extract_in_runtime_adapter"} and fbw {control_source:"owner", bot_route_policy:"runtime_existing"} (romextract_pdweapon.c:1783-1784); no base graph authors steering gains, fly numerics, or a trajectory_correction node (only calculate_trajectory in motion :1755). Numeric consumption is custom-only by data even with the toggle ON.
- calculate_trajectory (motion module) IS consumed: bgunGetProjectileFlagsFromGraph maps it to FUNCFLAG_CALCULATETRAJECTORY (bondgun.c:1854-1856); thrown path checks directly (:5455-5456). The trajectory_correction MODULE's four fields have zero consumers.
- OG trajectory backends: thrown clamp 20 deg = 0.34901026f and solve speed 21.666666f (bondgun.c:5465-5499 in bgunCreateThrownProjectile :5363); fired clamp 10 deg = 0.17450513f with traveldist-derived solve speed (:5754-5777 in bgunCreateFiredProjectile :5653). Aim source = crosshair dot (propFindAimingAt + hand->dotpos, :5458-5463 / :5747-5752). projectilegraph already in scope at both sites (:5456, :5711).
- OG homing steering: projectileTick PD controller (propobj.c:7283-7294): statics kkg=3, kkd=20, kkp=120 + mainOverrideVariable hooks; formula tmp = ((kkd/100 * preverr/dt) + (kkp/100 * err*dt)) * (kkg/100) at :7292; prev-error var80069bc4 is a single shared static (:7283, :7294) - cross-couples all simultaneous homing projectiles. Steering gate (B-914 widened) :7222-7230; target use guarded only by non-NULL projectile->targetprop :7237.
- B-914 latch precedent: projectileApplyGraphRuntime (propobj.c:1202-1256) sets PROJECTILEFLAG_HOMING :1222-1224; 5 spawn sites: bondgun.c:5345, :5634, :5894+:5975, chraction.c:10484. At :5894 it runs BEFORE playerLaunchSlayerRocket (:5900-5902).
- Targetprop assignment (current_lock OG backend): player trackedprops[0].prop (bondgun.c:5875, :5956); AI chrGetTargetProp(chr) (chraction.c:10486).
- Slayer fbw player path: player.c:4981-5305; OG constants: stick-to-turn 0.00025f (:5172-5173), speed targets 1/12 (:5238-5250), accel ramp 0.05f per tick60 (:5255, :5261), out-of-bounds detonate TICKS(120) (:5032). Entered via visionmode == VISIONMODE_SLAYERROCKET set by playerLaunchSlayerRocket (player.c:4631).
- Slayer fbw bot path: rocketTickFbw (propobj.c:6479-6660), reached ONLY via weaponnum == WEAPON_SKROCKET gate (:6742), spawned only by botactCreateSlayerRocket (botact.c:521-569), invoked only for WEAPON_SLAYER (bot.c:4618-4623). OG constants: turn tween 0.01875f (:6534-6535), accel 0.0018f/tick60 (:6555), lost-target speed 0.10f (:6560), smoke interval smoketimer240 = TICKS(24) (:6595-6596), max altitude 10000.0f (:6603), enemy proximity 250 units squared (:6621), lost-target timeout 8 * TICKS(240) (:6647), owner-death ownerprop = NULL (:6655-6657). Route: botactFindRocketRoute depth 6 (botact.c:467-495).
- Rocket smoke in projectileTick: ROCKETTAIL every tick while powered (:7892), HOMINGTAIL every tick (:7899); only interval-based smoke is smoketimer240 in rocketTickFbw. fly_smoke_interval_ticks60's sole OG backend is :6596.
- AI launcher branch: weaponnum allowlist chraction.c:10281-10286; graph fetch :10301-10312; flag widen :10328-10338; spawn dispatch :10341-10372 - the else at :10369-10371 passes g_Vars.currentplayer->prop->chr as owner (wrong-owner bug), marked "Unreachable"; func->unk50 deref at :10483 unconditional.
- Dangling targetprop: projectilesUnrefOwner (propobj.c:1057-1067) clears only ownerprop; called from objFree (:2952) and chrRemove (chr.c:1637). Nothing clears g_Projectiles[i].targetprop (only init :1096); assignments: bondgun.c:5875,5956, chraction.c:10486, propobj.c:1096 only.
- struct projectile (types.h:1369-1410): mempAlloc'd pool (setup.c:408), never ROM-read - appending PC fields is stride-safe. projectileReset (:1069-1108) resets fields individually, NOT memset; slots reused (projectileAllocate :1110-1149).
- MP wire: SVC_PROP_MOVE serializes projectile->flags u32, targetprop, speed (netmsg.c:2349-2364, read :2426-2444). New struct fields do not cross the wire. bgunCreateFiredProjectile early-outs on NETMODE_CLIENT (:5693-5695).
- gsetHasFunctionFlags is graph-aware (game_0b0fd0.c:648-663). WEAPON_CUSTOM_START 0x56 (constants.h:4611). weaponGraphTicks60To240 helper (propobj.c:1194-1200). Static-pin guard block: test_mod_external_archive_static.cpp:3128-3206 (B-912/914 pins :3175-3180); projectile registration test pins homing fields :2894-2897.

### Gotchas
- Base graphs DO carry homing/fbw records (strings only). Consumers keyed on record presence double-handle base weapons toggle-ON. Numeric fields safe via 0-means-unset; string policies validate-only where authored value names the OG behavior.
- OG steering expression stays verbatim in the unset branch, including mainOverrideVariable("kkg"/"kkd"/"kkp") hooks and the shared var80069bc4 update.
- Custom-gain path must NOT write var80069bc4 (would perturb base rockets same-frame); state in comment.
- projectileReset is not a memset; every new field MUST reset there or values leak across slot reuse.
- Spawned rockets mutate identity (player launcher spawns WEAPON_ROCKET/HOMINGROCKET weaponobjs, bondgun.c:5816-5826) - spawn-time latching mandatory; per-tick lookups silently fail for them.
- Hot path: consume only latched fields in projectileTick/rocketTickFbw/player fbw tick.
- MP: new f32 fields not in SVC_PROP_MOVE; remote clients use OG constants for custom projectiles between updates (visual-only divergence, accepted posture).
- rocketTickFbw unreachable for custom weapons (SKROCKET gate + Slayer-only bot path); fbw numerics there are dormant pre-wiring until a bot-side widen.
- AI widen NULL-safety: gate on weapondef && weapondef->functions[gset.weaponfunc].
- Dangling-targetprop fix is bug-fix class, not toggle-gated. Propagation check: player-prop removal path (MP disconnect) beyond objFree/chrRemove.
- trajectory_solve_velocity parses as bool; OG always velocity-solves; only true/unset matches OG.

### Plan
1. Slice A (first shippable): per-weapon homing steering gains. types.h append f32 hominggain/homingdamping/homingpreverr to struct projectile; projectileReset zeros; projectileApplyGraphRuntime latch when has_homing and > 0; projectileTick :7283-7294 branch: gain_eff = hominggain > 0 ? hominggain : 0.036f, damping_eff = homingdamping > 0 ? homingdamping : 0.006f (OG composites (kkp/100)*(kkg/100), (kkd/100)*(kkg/100)); update projectile->homingpreverr, do not touch var80069bc4. Else branch: OG verbatim.
2. Slice B: trajectory closure. bgunGetProjectileFlagsFromGraph ORs FUNCFLAG_CALCULATETRAJECTORY when has_trajectory_correction; hoist clamp literals to locals at bondgun.c:5470/:5478 and :5758/:5766, override with DTOR(trajectory_max_angle) when authored > 0. aim_source/solve_velocity validate-only.
3. Slice C: AI launcher widen + wrong-owner fix. chrGsetCustomProjectileGraph static helper (custom-slot guarded); widen allowlist at :10281-10286 gated on weapondef functions presence; fix :10371 to pass chr.
4. Slice D: fbw numerics. types.h append f32 flyturnrate/flyaccel/flyproxradius/flymaxaltitude + s32 flylosttimeout240/flysmokeinterval240; reset; latch (ticks via weaponGraphTicks60To240); consume player.c:5172-5173/:5255/:5261 and rocketTickFbw :6534/:6555/:6596/:6603/:6621/:6647 with OG literals as 0-fallbacks. Strings validate-only.
5. Slice E: dangling-targetprop hardening (NOT toggle-gated): projectilesUnrefOwner also clears matching targetprop; propagation check MP disconnect path.
6. Slice F: DEFERRED per B6 decision 1.

### Test plan
- Static pins (new sibling case [modding][pdxxx][weapon_graph][c3849][static]): propobj "projectile->hominggain", "homingpreverr", keep-OG pin "mainOverrideVariable(\"kkg\"", "flysmokeinterval240", unref pin targetprop clear; bondgun "has_trajectory_correction", "trajectory_max_angle"; chraction "WEAPON_CUSTOM_START" + corrected owner call; player.c "flyturnrate"/"flyaccel"; types.h ordering pin.
- Port unit tests: register projectile graph authoring gains + fbw numerics + trajectory node; REQUIRE capture; REQUIRE base-shaped graphs (strings only) leave numerics 0.

### Size verdict
MEDIUM. First shippable: Slice A.

### Deferred
homing_target_filter / retarget_policy / target_source (no OG backend; validate-only); homing_lost_target_behavior (B6.1); homing_runtime_constants (doc marker, satisfied by Slice A); fly strings; bot fbw widen for customs; fly_lost player-path analog; MP prediction of custom gains.

## Wave 5b - surface (wall-hugger, sticky-attach, bounce-slide, timer)

### Key facts
- IR fields: wall-hugger weapon_graph_runtime.h:313-319, sticky :321-328, bounce :330-335, timer :337-340. Parse: weapon_graph_runtime.c:2183-2198 (wall), 2199-2213 (sticky), 2214-2224 (bounce), 2225-2233 (timer). Param keys: stick_timer_ticks60, fall_threshold (f32 no units), post_fall_timer60|post_fall_timer_ticks60, first_bounce_boost, rest_speed, slide_friction, randomize_rotation, timer|timer_ticks60, timer_starts, on_expire.
- Already consumed: has_bounce_slide + bounce_first_boost at propobj.c:1236-1237 (feeds projectile->unk08c); timer_ticks60 at :1245-1246, bondgun.c:1950-1951, chraction.c:10317-10319. Dead: bounce_limit/rest_speed/slide_friction/randomize_rotation, all 7 wall fields, all 8 sticky fields, timer_starts, timer_on_expire.
- OG wall-hugger (Devastator = WEAPON_GRENADEROUND FUNC_SECONDARY): stick latch projectileTick :7417-7424 (on stick timer240 = TICKS(480) :7422; post-fall sentinel timer240 == 1 forces stick=false; timer240=0 :7418-7420). On-wall countdown + fall: weaponTick :4709-4765 (fall threshold literal timer240 < 8 :4716; fall vector literal {0,-10,0} :4718; fall sets timer240=1, projectileSetSticky, speed=direction :4746-4752 after func0f0685e4(prop) re-arms :4722, defined :1155-1176). Detonation: falls into normal-grenade else (:4766-4779), propExplode(EXPLOSIONTYPE_ROCKET) :4776.
- OG sticky/embedment: sticky collision mode via PROJECTILEFLAG_STICKY (:7370-7374); stick decision :7382-7426: autogun = background only (:7397-7401), weaponnum list OR gsetHasFunctionFlags(FUNCFLAG_STICKTOWALL) (:7405-7414); vetoes knife/bolt-vs-weapon :7436-7445, sliding/shield :7447-7480; stick executes objLand at :7667 which FREES the projectile (:4543-4546) and embeds via objEmbed (:4490-4536, called :4596). FUNCFLAG_STICKTOWALL = 0x100 (constants.h:1078), already graph-aware.
- OG bounce (projectileTick): restitution f0 *= -(unk08c + 1.0f) :7789-7799; random rotation gated by (flags & PROJECTILEFLAG_00000100) == 0 :7822-7825 (flag read-only in OG, set nowhere); bouncecount++ :7827; limit literal bouncecount >= 6 :7835; rest-speed literal 2.2222223f :7840-7842; first-bounce boost = (flags & PROJECTILEFLAG_00000002) && bouncecount == 1 (:7841-7842, OG setters bondgun.c:5342-5343, 5560-5561); slide friction = projectile->unk098 linear decel (:7991-8011, OG setters bondgun.c:5893/5974, chraction.c:10483).
- OG timer expiry: weaponTick arm chain :4705 grenade/grenade-round -> :4802 nbomb -> :4847 rockets -> :4876 B-912 custom contact arm (checks timer240 == 0 only, never decrements) -> :4888 timed mine -> :4902 remote -> :4957 proximity. timer240 is s16 with sentinels -1/1/0 (types.h:1606-1618).
- Spawn sites (latch points): bondgun.c:5345, 5634, 5894, 5975; chraction.c:10484. ForGameplay gating weapon_graph_runtime.c:674-702.
- Net: projectile->flags replicates in SVC_PROP_MOVE (netmsg.c:2351/:2428); unk08c/unk098 replicate only for hover types (:2358-2364).
- Base emitter emits a wall_hugger node for every FUNCFLAG_STICKTOWALL base weapon with stick_surface_filter="background", post_fall_timer60=360 (romextract_pdweapon.c:1785); never emits sticky_attach/bounce_slide/projectile.timer nodes (:1723-1792). Custom-slot guard mandatory.
- Existing static pin block: test_mod_external_archive_static.cpp:3169-3180; wall-field parse test :2899-2900.

### Gotchas
- timer240 is s16 but weaponGraphTicks60To240 returns s32 (assigned :1254); authored timers > ~136s silently overflow. Add clamp; same sink at bondgun.c:2031.
- Devastator base graphs carry has_wall_hugger; custom-slot guard everywhere. Same for mine weapons in the stick list.
- PROJECTILEFLAG_STICKY is set for EVERY launched projectile (bgun0f09ebcc bondgun.c:5208-5214); never use as "this weapon sticks".
- The :4709-4711 wall-arm gate is nested in the :4705-4707 outer gate keyed to WEAPON_GRENADE/GRENADEROUND; customs never enter. Custom path = separate arm at the B-912 position per B1, preserving chain order.
- A weapon with both impact_consume_on_hit and has_timer/has_wall_hugger hits only the FIRST matching weaponTick arm: ONE combined arm with internal priority (wall > contact > fuse), not three else-ifs.
- Post-fall state cannot live on struct projectile: objLand frees it on landing - wall_post_fall_timer60 deferred (B6.7).
- gsetHasFunctionFlags graph-awareness means the :7414 stick gate is toggle-sensitive for base weapons (emitter writes same literal = parity-by-construction; pin flags equality).
- String policies latch at registration (timer_starts/timer_on_expire/wall_fall_vector/filters) via exported pure helpers for unit-testability.
- weaponTick already resolves graph/entitygraph at top (:4698-4702); reuse.

### Plan
1. Registration-time latching: derived fields on weapon_graph_projectile_runtime_t: s32 timer_start_policy (0 on_spawn, 1 on_impact, 2 on_attach), s32 timer_expire_policy (0 explode, 1 delete), f32 wall_fall_vec[3] (default {0,-10,0}; accept "down" and "x,y,z"), s32 wall_stick_bg_only. Pure helpers weaponGraphParseTimerStartPolicy/ParseExpirePolicy/ParseVector. fall_threshold_ticks60 alias key. Unknown strings: defaults + loud registration warning.
2. Shared resolver weaponGetCustomProjectileGraph (impact U1 text); weaponGetContactImpactGraph reimplemented on top.
3. Bounce-slide: latches in projectileApplyGraphRuntime (slide_friction > 0 -> unk098; first_boost > 0 -> also PROJECTILEFLAG_00000002; randomize_rotation == 0 -> PROJECTILEFLAG_00000100); event consumers at :7835/:7840: bounce_limit > 0 ? limit : 6; bounce_rest_speed > 0 ? v : 2.2222223f, resolved once at top of the bounce-collision scope.
4. Timer: only set timer240 when on_spawn; s16 clamps both sinks; combined custom arm per B1 (wall branch -> contact verbatim -> fuse: decrement, propUnsetDangerous, explode HUGEEXP?HUGE17:ROCKET / delete OBJHFLAG_DELETING, slayerrocket cleanup loop mirror :4785-4794); on_impact start at the :7901-7907 BG-hit chain (set timer instead of detonating when policy = on_impact and timer240 == -1).
5. Wall-hugger: stick latch widen :7417-7424 custom else-if (sentinel structure; stick timer = authored > 0 ? clamped : TICKS(480); wall_stick_bg_only forces stick=0 on props); weaponTick wall branch replicating :4712-4762 verbatim-shaped with latched vec/threshold; expiry ROCKET class.
6. Sticky-attach: custom-only clause after OG stick computation, before :7436 vetoes (allow_background/char/obj by hit class); impact_stick_on_hit absorbed here per B4 (additional entry condition, gated !impact_consume_on_hit); timer_starts == on_attach seeds timer240 at the stick execution site before objLand.

### Test plan
Static pins (propobj block): weaponGetCustomProjectileGraph, bounce_rest_speed, bounce_limit, PROJECTILEFLAG_00000100, PROJECTILEFLAG_00000002, wall_stick_timer_ticks60, wall_fall_vec, sticky_allow_background, timer_expire_policy, impact_stick_on_hit; runtime-file pin for parse helpers. Unit tests: policy/vector parsers (on_spawn/on_impact/on_attach, explode/delete, down/0,-10,0/garbage -> defaults); registration-derived fields via compile+register+Get; clamp pins textual in both sinks. Toggle-off parity via existing SetEnabled(0) pattern.

### Size verdict
M-L. Slice 1 = bounce + timer + parse layer + clamps (~150 lines); slice 2 = wall-hugger machine + sticky (~200 lines, riskiest = :4712-4762 replication).

### Deferred
wall_post_fall_timer60 (B6.7); wall_explosion_ref consume-site shim per B7 (fallback ROCKET, lights up at Unit 8); sticky_surface_filter beyond background / sticky_prop_filter / sticky_embed_policy (no OG backend); sticky_on_attach -> entity-armed ledger; timer_on_expire storm/fall values; bounce_first_boost unk08c semantic mismatch cleanup (emitter+schema follow-up).

## Wave 5c - impact-trail-transition (impact remainder, trail, transition, pickup)

### Key facts
- Fields: impact weapon_graph_runtime.h:342-348 (impact_filter[64], explosion_ref, spark_ref, impact_hit_sound s32, consume_on_hit, stick_on_hit); trail :350-352; transition :354-360; pickup :362-367. Parser: weapon_graph_runtime.c:2234-2277. impact_hit_sound and pickup_sound are ALREADY registration-resolved soundnums via heldResolveSfxParam (:1879-1921, catalog refs through catalogResolveAudio, rejects sound_id=-1); zeroed-struct default means > 0 is the presence check.
- weaponGraphRuntimeGetEntityForProjectile :732-740 (toggle-gated, reads projectile->entity_ref); ZERO production callers - the natural transition seam.
- B-912 state: weaponGetContactImpactGraph propobj.c:1267-1285; 4 sites: weaponTick :4876-4887 (hardcoded PHOENIX, checks timer240 == 0, never decrements), projectileLaunch :6694-6695, prop-hit consume :7484-7576 (graph damage :7559-7563), BG-hit :7901-7907 (collision -> timer240 = 0, no else).
- projectileApplyGraphRuntime :1202-1256: pickup_timer_ticks60 -> pickuptimer240 ALREADY consumed (:1240-1243); timer60 -> timer240 (:1245-1255). 5 spawn sites (bondgun.c:5345 after pickuptimer240 = TICKS(240) :5344 so graph wins; :5634; :5894; :5975; chraction.c:10484).
- OG trail: projectileTick weaponnum chain :7857-7924 - rocket ROCKETTAIL :7892, homing HOMINGTAIL :7899, grenade-round GRENADETAIL :7922; B-912 clause at :7901 sits in this chain with NO trail else-branch. Cadenced variant: smoketimer240 + TICKS(24) in rocketTickFbw :6595-6600; smoketimer240 otherwise unused (reset :1095) - free for reuse. SMOKETYPE constants.h:3907-3916.
- OG hit sound: collision psCreate block :7926-7941 - knife SFX_808B, grenade-secondary random array, default SFX_EYESPYHIT; debounced by projectile->unk0a4 frame guard :7927.
- OG sparks: sparksCreate(prop->rooms[0], prop, &sp5e8, &dir, &sp5f4, SPARKTYPE_PAINT/PROJECTILE) at :7658-7662 (bolt/knife stick arm; paintball via chrIsUsingPaintball). sp5e8/sp5f4 in scope throughout the collision block. SPARKTYPE constants.h:3954-3973.
- OG laptop transition: bgunCreateThrownProjectile2 (bondgun.c:5253), gate gset->weaponnum == WEAPON_LAPTOPGUN -> laptopDeploy at :5294-5299. laptopDeploy (propobj.c:19142-19299) already consumes the ENTITY record via held chain (:19153-19170 modelnum, ammo cap 254; aim :19235-19237; team :19289-19293) and does OG ammo transfer (:19252-19277). Deployed = OBJTYPE_AUTOGUN + OBJFLAG_THROWNLAPTOP (:19298), sticks BG-only via :7397-7401.
- OG pickup: objTestForPickup :18151 (THROWNLAPTOP -> TICKOP_NONE :18174; pickuptimer240/pickupby window :18184-18194; armed-weapon blocks weaponnum-keyed :18206-18235 - customs NOT covered). Laptop recover: propobjInteract :16841-16863 (hardcoded invGiveSingleWeapon(WEAPON_LAPTOPGUN), weaponPlayPickupSound, ammo merge :16859-16861). Generic weapon pickup sound: propPickupByPlayer :17947-17949; weaponPlayPickupSound :17521-17553 gives every weaponnum > WEAPON_PSYCHOSISGUN SFX_PICKUP_KEYCARD (:17546-17547).
- Explosion classes: PHOENIX 22 / ROCKET 13 / HUGE17 17 / LAPTOP 3 (constants.h:922-937). catalogResolveWeapon(id, out) -> out.weapon_num (assetcatalog.h:1119-1127, 1169).
- Producer reality: base emitter authors ONLY spawn_state/motion/homing/fbw/wall_hugger (:1723-1792); NO base impact/trail/transition/pickup nodes. Producers today: Needler (build_needler_mod.py:483-489 impact_filter any, explosion/spark refs, consume true, stick false; motion.timer60:180 :481) and typed examples (build_typed_pdxxx_examples.py:2020 transition when at_rest, transfer_owner true, transfer_position true).
- Wire: projectile->flags u32 replicates (:2351/:2428/:2574), pickuptimer240 replicates S16 (:2576); new struct fields do NOT replicate (protocol freeze).

### Gotchas
- Do NOT widen weaponGetContactImpactGraph itself (4 sites depend on consume semantics). Broader sibling helper per B4.
- Commit 381bee64 message claims base graphs carry impact records - they do NOT (emitter verified). Guard stays as defense-in-depth.
- timer240 overloading: new expiry decrement clamps to 0 (rocket-style == 0 arm) so the existing propExplode arm fires.
- The :7857-7924 chain is else-if; trail emission must live in the SAME custom clause as the impact arm (mirror rocket arm shape :7862-7894).
- impact_filter latches to s32 enum at parse per B3 (no per-event strcmp); trail_type/pickup_allowed_owner strcmp once at spawn; recover_weapon_ref/explosion_ref resolve at REGISTRATION.
- New struct projectile fields are host-local: clients never run projectileApplyGraphRuntime (spawn server-gated bondgun.c:5396) - custom trails will not render on remote clients this wave. Record; do not bump protocol.
- Clamp graph pickuptimer240 to S16 (netmsg :2576 writes S16).
- Helper key pattern: guard weapon->weaponnum, fetch by weapon->gset.weaponnum/weaponfunc (differ for some base weapons).
- Precedence: consume_on_hit wins over stick_on_hit (document in helper comment).
- WEAPON_GRAPH_RUNTIME_MAX_WEAPONS 96 vs custom start 86: only 10 custom held slots.

### Plan
- U1 helper refactor: weaponGetCustomProjectileGraph (no impact precondition); weaponGetContactImpactGraph on top. (Unit 1.)
- U2 impact remainder: impact_filter (s32 enum: any/background/props/chr) honored at the three consume sites; impact_hit_sound else-if BEFORE the default SFX_EYESPYHIT arm :7935 (knife/grenade arms first verbatim); impact_stick_on_hit -> surface cluster per B4; impact_explosion_ref via derived s32 impact_exptype resolved at parse through the B2 unified resolver, consumed at the B-912 arm :4883 (exptype >= 0 ? exptype : PHOENIX, HUGEEXP override preserved), WAVE6-EFFECT-HANDOFF markers at both sites; impact_spark_ref presence-based sparksCreate mirror in the :7926 block (debounced by unk0a4), Wave 6 owns visual selection.
- U3 trail: s32 graphtrailsmoketype (-1) + s32 graphtrailinterval240 (0) on struct projectile; reset; latch (rocket/homing/grenade map -> SMOKETYPE_*, none/"" -> -1, unknown -> ROCKETTAIL + one-time warning; interval via weaponGraphTicks60To240, <= 0 = every tick); consume by widening the :7901 clause (collision -> timer240 = 0 only when consume_on_hit; else cadence on smoketimer240, emit smokeCreateSimple(&prop->pos, prop->rooms, type), mirroring :6595-6600).
- U4 flight-timer expiry: merged into surface's combined arm per B4 (delete -> OBJHFLAG_DELETING rides along); objTestForPickup custom clause mirroring :18206-18213 (custom graph weapon with has_timer || consume_on_hit and timer240 >= 0 -> TICKOP_NONE). Fixes the live Needler gap (authored timer60:180 never counted).
- U5 transition: widen laptop gate :5294 (OG first verbatim) for custom + has_transition_to_entity + GetEntityForProjectile yields has_autogun entity -> laptopDeploy; laptopDeploy falls back to projectile-record chain when held chain NULL; transition_when any -> OG early instantiation (sanctioned by module spec); transfer_owner true -> OG owner bits + targetteam; transfer_ammo true -> OG block, false -> skip carrier deduction, ammoquantity = min(reserve, 254); transfer_position true == OG; delete_carrier no-op documented. Implemented inside Unit 7 with entity-deployed owning the :5294 edit per B4.
- U6 pickup: pickup_allowed_owner spawn latch ("owner"/"owner_only" -> projectile->pickupby = ownerprop; ""/any -> NULL); recover_weapon_ref -> s32 recover_weaponnum at registration via catalogResolveWeapon, stored in entity-deployed's g_ThrownLaptopLatch per B4 (NO autogunobj growth); recover site :16841-16863 custom branch (latch-keyed weaponnum replaces the three WEAPON_LAPTOPGUN literals); recover_ammo_policy ""/merge/merge_reserve -> OG merge, none -> skip; pickup_sound at recover (playerSndStart mirror :17552) and generic :17947 via a has_pickup_recover helper (custom-slot guarded; today customs get SFX_PICKUP_KEYCARD).
- U7 MP toggle parity: host-authoritative flags replicate regardless of client toggle; per-tick graph clauses evaluate the LOCAL toggle -> prediction divergence host-ON/client-OFF, corrected by resync; trails host-local. Wave 7 shape: host advertises toggle in session rules (5f-6 MPOPTION bit is the no-wire-change vehicle).

### Test plan
Static pins: weaponGetCustomProjectileGraph, impact enum latch, impact_hit_sound, graphtrailsmoketype, impact_exptype, WAVE6-EFFECT-HANDOFF, pickup_allowed_owner, recover_weaponnum; bondgun pin block for GetEntityForProjectile + has_transition_to_entity; weaponTick expiry pin. Unit tests: all four node kinds registered; impact_exptype == 13 for base:explosion_rocket, -1 for mod refs; recover_weaponnum -1 when unresolvable; trail/pickup latches; GetEntityForProjectile toggle gate.

### Size verdict
Medium-large (~350-450 LOC). Sub-slices: (a) U1+U2+U4, (b) U3, (c) U5+U6 (inside Unit 7).

### Deferred
transfer_owner/position = false (no OG backend); non-autogun transition archetypes; AI deploy widening (botact.c:309, chraicommands.c:7386); timer_on_expire fall/create_storm; true effect visuals (Unit 8); client trail rendering + toggle negotiation (Wave 7); weaponPlayPickupSound keycard default arm.

## Wave 5d - entity-armed-trigger (armed, proxy, remote, timed)

### Key facts
- Record: weapon_graph_entity_runtime_t (weapon_graph_runtime.h:370-459): armed :391-397, proxy :399-405, remote :407-413, timed :415-419. Accessors gate on weaponGraphRuntimeEnabled(): GetEntityForGameplay :710-715, GetEntityForHeldFunction :717-730 (entity_ref then payload_ref), GetEntityForProjectile :732-740.
- Parser entityRuntimeFromNode :2283-2376: has_* set FROM NODE OPCODE; armed :2323-2337, proxy :2338-2350, remote :2351-2365, timed :2366-2376. Multiple nodes fold into one record.
- Consumed already: proxy_radius at propobj.c:4976-4981 (> 0.0f guard, 250.0f fallback); arm_delay_ticks60 at bondgun.c:1971-1974 + 2021-2024 (> 0 guarded); timed_timer_ticks60 at bondgun.c:1968-1970 + 2018-2020 (NOT zero-guarded = the Unit 0a bug).
- OG backends: weaponTick entitygraph fetch at top :4698-4702. Timed mine arm :4888-4901 (decrements only when gunfunc == FUNC_PRIMARY :4890; expiry propExplode(HUGEEXP?HUGE17:ROCKET) + DELETING :4894-4897; else-branch empty :4899-4901 = the only OG pause). Remote arm :4902-4956: signal mask g_PlayersDetonatingMines (decl :135, data.h:201); self-attached guard :4906-4910; coop/anti ownerplayernum == 2 mask :4911-4933; timer240 = 0 -> explode :4944-4955. Signal producers: playerActivateRemoteMineDetonator (:18865-18872) from handTickAttack HANDATTACKTYPE_DETONATE (prop.c:1511-1513) and quick-detonate (bondmove.c:2944-2948, gate bgunGetWeaponNum(HAND_RIGHT) == WEAPON_REMOTEMINE :154). Mask reset: alarmTick tail propobj.c:21852 AND src/game/setup.c:358. Detonator weapon side already graph-capable: graph->specialfunc feeds hand->attacktype (bondgun.c:2850, 2861) - authored specialfunc = HANDATTACKTYPE_DETONATE (constants.h:1307) reaches prop.c:1511 today.
- Proxy arm :4957-5044 (weaponnum list PROXIMITYMINE / DRAGON-sec / GRENADE-sec / NBOMB-sec); arm countdown timer240 >= 2 -> weaponRegisterProxy :4962-4969; active state checks ONLY g_Vars.currentplayer radius :4970-4983; detonation :4986-5043 (NBOMB -> nbombCreateStorm_hack with normmplayerisrunning owner lookup :4990-4999; Dragon -> DRAGONBOMBSPY :5035-5037). Chr/object triggers: coordTriggerProxies(pos, bool) :18939-18967 (250*250, Dragon doubles, grenade requires arg1); callers: chrsTriggerProxies :18989 (CHRH2FLAG_CONSIDERPROXIES, runs at alarmTick tail :21850), func0f069c70 :2208, bondeyespy.c:1271, training.c:1819. Unregister: objFree weaponnum-gated :2832-2847. g_Proxies[MAX_PROXIES 120]. NO OG LOS check anywhere in the proxy path.
- Damage-triggered detonation: objDamage OBJTYPE_WEAPON :16012-16039 (explosive weaponnum list zeroes timer240 :16024-16036, homing-vs-remote exception :16034, returns :16039). objTakeGunfire honors OBJFLAG2_IMMUNETOGUNFIRE :15958-15963. Server-authoritative with SVC relay :15968-15979.
- Spawn/owner: bgunCreateThrownProjectile2 :5253-5353 (activatetime60 :5284-5285; timer240 :5307-5311 then bgunApplyEntityGraphToWeapon :5324; owner slot = hidden bits << 28 :5331-5339; weaponCreateProjectileFromGset inits timer240 = -1 :19369). Bots route through the same function (botact.c:417).
- Base entity graphs author NO policy params (kind map romextract_pdweapon.c:1859-1864, common params only :1876-1885): has_* TRUE with empty strings / zero numerics for base mines toggle-ON.
- Vocabulary pinned by fixtures (test:2808-2836): detonation_policy explode_once, target_filter hostile_chr, team_filter enemy_only, line_of_sight true, on_trigger detonate, detonator_ref catalog id, owner_slot_source owner_inventory, on_remote_signal detonate, starts_when armed, on_expire detonate.
- LOS primitive: cdTestLos05(pos, rooms, pos2, rooms2, cdtypes, geoflags) (collision.h:59; canonical chraction.c:6585 GEOFLAG_BLOCK_SHOOT).

### Gotchas
- Every consumer needs custom-slot guard OR empty-equals-OG fallthrough (B3). proxy_radius survives only via its > 0.0f guard.
- timer240 priority for combined customs: timed countdown wins; proxy uses arm -> 1; remote signal sets 0 from any state; no-op at -1 (pickup safety).
- Detonate signal is identity-blind: detonator_ref pairing needs provenance (g_PlayersDetonatingWeaponnum, reset at BOTH sites :21852 + setup.c:358, same frame schedule).
- objFree custom unregister clause mandatory (g_Proxies dangle = use-after-free in coordTriggerProxies).
- coordTriggerProxies has no source identity; filters/LOS on the weaponTick currentplayer path only this wave (4-caller signature widen deferred).
- B-912 arm precedes the entity arm: a custom with both impact and entity records resolves to the impact arm (acceptable; document).
- Latch policy strings at parse (B3); reuse the fetched entitygraph in weaponTick.
- Quick-detonate weaponnum gate (bondmove.c:154) means custom mines get no quick-detonate input; graph detonator route works today.
- MP: copy verbatim the normmplayerisrunning owner lookups and coop/anti ownerplayernum == 2 logic.

### Plan
- S1 (Unit 0a + Unit 1c): timed_timer > 0 guard fix at bondgun.c:1968-1970/2018-2020 (mirrors arm_delay guard; bugs.md entry). Parse-time latch: armed_damage_response_mode (0 detonate, 1 ignore), armed_exptype (via B2 resolver, -1 unresolved), proxy_on_trigger_mode (0 detonate, 1 storm), proxy_target/team/owner_filter_modes (0 all = OG), remote_signal_mode (0 detonate), timed_on_expire_mode (0 detonate, 1 storm, 2 delete), timed_starts_mode (0 thrown/armed). Unknown strings latch 0 + recorded.
- S2 (Unit 6): helper weaponGetEntityGraphForGameplay (custom-slot guard + held lookup -> GetEntityForHeldFunction). New weaponTick CUSTOM ENTITY ARM after :5044 per B1: remote (verbatim coop/anti/self-attached semantics + provenance match when detonator_ref authored: assetCatalogResolve must be ASSET_WEAPON whose runtime_index equals the signaling weaponnum; lazy resolve at signal time); timed (decrement; on_expire detonate -> shared block, storm -> nbombCreateStorm_hack + verbatim owner lookup + propUnsetDangerous + DELETING, delete -> propUnsetDangerous + DELETING); proxy (only when !has_timed_detonatable; arm countdown -> weaponRegisterProxy mirror; radius + filter modes + proxy_line_of_sight via cdTestLos05 on radius-pass only); storm sub-branch hosts entity-deployed Step 2; shared detonation block (storm modes -> nbomb path; else propExplode(armed_exptype >= 0 ? armed_exptype : HUGEEXP?HUGE17:ROCKET); timer240 = -1; DELETING). objDamage custom else-if after the verbatim OG list: custom + has_armed_explosive + mode != IGNORE -> timer240 = 0. objFree: custom-slot weaponUnregisterProxy clause. Provenance: g_PlayersDetonatingWeaponnum[MAX_PLAYERS] beside the mask; playerActivateRemoteMineDetonator(playernum, weaponnum) (prop.c:1512 passes the in-scope weaponnum local :1473; bondmove.c:2947 passes bgunGetWeaponNum(HAND_RIGHT)); -1 wildcard; reset at both mask reset sites.
- S3: tests + context per standing orders.

### Test plan
Static pins: weaponGetEntityGraphForGameplay, armed_damage_response_mode, g_PlayersDetonatingWeaponnum, custom weaponUnregisterProxy clause, playerActivateRemoteMineDetonator new signature at prop.c + bondmove.c, timed_timer_ticks60 > 0 guard, cdTestLos05 + proxy_line_of_sight. Unit tests: armed_exptype base:explosion_phoenix -> 22, unknown -> -1; mode latches from fixture strings; empty-policy graph latches all modes 0; toggle-OFF NULL (existing pin). Pure predicate weaponGraphEntityRemoteSignalMatches(entity, weaponnum) unit-tested (wildcard/-1, empty-ref, mismatch).

### Size verdict
Medium-large (~6 files; propobj ~150 lines). S1 and S2 separately buildable.

### Deferred
detonation_policy beyond explode_once; delete_on_detonate false; armed_owner_filter; timed_pause_policy; timed_starts on_attach; non-empty coop/anti/self/owner_slot vocabularies (B6.8); source-filtered coordTriggerProxies widen; quick-detonate widen; MP toggle parity (Wave 7).

## Wave 5e - entity-deployed (nbomb-storm, autogun, sticky-device, owner-cleanup, interaction)

### Key facts
- Dead fields: nbomb :421-425, autogun extras :430-437, sticky-device :440-445, owner-cleanup :447-451, interaction :453-458 of weapon_graph_runtime.h. Parse: nbomb :2377-2388, autogun :2389-2411, sticky :2412-2425, owner :2426-2436, interaction :2437-2448. interaction_sound already parse-resolved via heldResolveSfxParam (:2445). heldParam* helpers assign only-if-present - absent params stay memset-zero (absent-vs-authored-0 ambiguity for the bool-like ints).
- Already-consumed autogun fields (pattern to mirror) in laptopDeploy (propobj.c:19142): graph fetch :19153-19156; ammo_reserve :19162-19169 (> 0 guard, clamp 254); aim_distance :19235-19237 (> 0.0f guard, else 5000); team_policy "any" :19289-19293. CORRECTION: autogun_target_filter is parsed (:2391-2392) but has NO src/game consumer.
- bgunApplyEntityGraphToWeapon (bondgun.c:2008-2033) called at thrown deploy :5324. Thrown routing into laptopDeploy hard-keyed gset->weaponnum == WEAPON_LAPTOPGUN (:5294).
- OG nbomb storm: nbombCreateStorm(pos, ownerprop) (nbomb.c:670-705, owner :705); triggers: weaponTick primary expiry :4802-4845 (storm :4821, owner extraction (obj->hidden & 0xf0000000) >> 28 :4810-4819, carrier delete :4824); proxy arm :5001; BG-rest projectileTick :7908-7920; nbombCreateStorm_hack macro :4680.
- OG autogun: autogunTick :9020 (client early-returns :9024-9026/:9137-9139 = the net_authority backend; targeting :9144-9224; turn speed = autogun->maxspeed consumed :9407-9408, seeded PALUPF(0.0697f) at deploy :19287). autogunTickShoot :9562 (client return :9567-9569; fireleft = (firecount % 2) == 0 :9589; alternate muzzle (firecount % 2) == 1 gated on modelGetPart(MODELPART_AUTOGUN_FLASHLEFT) :9591-9593; beam (firecount % 4) == 0 :9602; friendly-fire suppression :9711-9717; MP damage halving :9707-9709; ammo decrement :9853-9857).
- OG pickup/interact: propobjInteract thrown-laptop arm :16841-16863 (owner check laptop == &g_ThrownLaptops[playernum] :16853; invGiveSingleWeapon(WEAPON_LAPTOPGUN) :16855; hudmsg; weaponPlayPickupSound; ammo return :16859-16862). objTestForInteract :16633-16715 (OBJFLAG3_INTERACTABLE :16643); objTestForPickup :18151 (THROWNLAPTOP excluded :18174-18176; armed sticky devices non-pickable while timer240 >= 0 :18215-18226). propPickupByPlayer :17852 (weapon arm :17911-18047; sound :17947-17949; textoverride :17951-17968). Pickup text = lang-id textoverride->pickuptext via langGet (inv.c:1095-1115; match key obj pointer :926-938).
- OG sticky devices: landing-flags arm :4550-4560 (ECM/comms/tracer/amplifier get OBJFLAG_INVINCIBLE | OBJFLAG_FORCENOBOUNCE | OBJFLAG2_IMMUNETOGUNFIRE); mission hook objectiveCheckThrowInRoom :4562; stick decision :7382-7426 (weaponnum list :7405-7413, graph-aware FUNCFLAG_STICKTOWALL :7414, autogun background-only :7397-7401).
- OG owner cleanup: laptop replace-on-redeploy :19183-19190 (explosionCreateSimple(EXPLOSIONTYPE_LAPTOP) + objFreePermanently); playerDie/playerDieByShooter (player.c:6569-6654, NETMODE_CLIENT guard :6574-6576, isdead transition :6629); net disconnect kills the chr (net.c:1700-1707) so the death hook covers disconnect; g_PlayersDetonatingMines is the OG-native "react to owner event per weapon" mechanism.
- g_ThrownLaptops index = player index, max 12 (setup.c:440-446); pointer arithmetic laptop - g_ThrownLaptops recovers the index.
- Base graphs carry these records with empty params (kind map romextract_pdweapon.c:1859-1864): guards per B3.
- Fixture pins exist: autogun_ammo_reserve == 400 :2931, ffsuppression == 1 :2932, max_active_per_owner == 1 :2934, transfer_payload == "base:laptopgun" :2936, storm_delete_carrier == 1 :2929.

### Gotchas
- Do NOT grow struct autogunobj or weaponobj: setupCreateAutogun reinterprets setup-segment bytes in place (setup.c:1228-1247, maxspeed :1232) - B-323 stride class. Use the g_ThrownLaptopLatch[12] sidecar.
- Sentinel-initialize alternate_muzzles/ffsuppression/pickup_recover/storm_delete_carrier to -1 in their parse cases (absent = OG default).
- autogunobj has no gset/weaponnum: per-tick accessor lookup impossible in autogunTick* - latch at laptopDeploy.
- fire_cadence has no defined unit anywhere: rpm decided (B6.6); record in module-parameters doc same merge. Cadence converts to integer fire-interval once at deploy.
- timer240 pickup safety (-1 inert) in any storm arm.
- Strings strcmp at deploy/landing/expiry transitions only.
- Latch lifecycle must be specced: free/reset on objFree of the deployed laptop and on redeploy slot reuse (critic risk item).
- No wire changes: autogun fires server-side only; pickups replicate via netmsgSvcPropPickupWrite (:18108-18110).

### Plan
- Step 0 (Unit 1c): parse sentinels + exported pure helper weaponGraphAutogunFireInterval(f32 rpm) (1800 rpm baseline = 1, clamp >= 1).
- Step 1 (Unit 7): autogun completion: laptopDeploy turn_speed (> 0 -> PALUPF), target_filter players_only -> targetteam = 0 (team_policy clause first verbatim); static g_ThrownLaptopLatch[12] {valid, fireinterval, alternate, beaminterval, ffsuppress, pickuprecover, recoverweaponnum, recoverammopolicy, pickupsound, interactanyone} written unconditionally on every deploy (valid = 0 when no graph), recoverweaponnum from impact U6's registration-resolved field / transfer_payload via catalogResolveWeapon. autogunTickShoot: cadence gate firecount % (2 * fireinterval) when valid && fireinterval > 1; alternate == 0 forces single muzzle; beam literal 4 -> latched; ffsuppress == 0 skips suppression block. propobjInteract: pickuprecover == 0 skips recover; recoverweaponnum replaces the three WEAPON_LAPTOPGUN literals; interact anyone relaxes owner equality. bgunCreateThrownProjectile2 :5294 routing widen (custom + has_autogun + archetype == "autogun"); ammo-transfer literals :19253/:19259/:19273 -> gset->weaponnum (bit-identical for base). Impact U5 transition semantics land in this same unit.
- Step 2 (inside Unit 6's entity arm per B1): nbomb-storm sub-branch: timer countdown mirroring :4802-4845; storm_owner_transfer none -> NULL else OG extraction; nbombCreateStorm_hack; delete carrier unless storm_delete_carrier == 0. Proxy-arm widen (:4957-4960, :4988) for storm_activation_policy == proximity; BG-rest widen :7908.
- Step 3 (Unit 7): owner-cleanup: laptopDeploy replace policies (empty/explode_existing OG verbatim; delete_existing -> objFreePermanently no explosion; deny -> return NULL; max_active_per_owner == 0 -> deny). g_PlayersOwnerCleanupPending mask set in playerDieByShooter isdead transition; consumed in weaponTick by custom-slot weapons with has_owner_cleanup whose owner bits match: explode -> timer240 = 0; delete -> DELETING; empty/persist -> no-op. Thrown-laptop slot handled via :19185-19189 routines. Clear the mask at the same sites as g_PlayersDetonatingMines (propobj.c:21852, setup.c:358).
- Step 4 (Unit 7, after Unit 4): sticky-device: weaponGetStickyDeviceGraph helper; landing-flags arm :4550-4560 widen ("shootable" skips invincibility flags); stick decision :7405-7414 helper clause (background_only requires hitprop == NULL; projectile sticky_attach record wins); armed-pickup :18215 widen ("none" blocked while deployed); sticky_visible_state verify-first (OBJFLAG2_INVISIBLE render consumer must be confirmed, else defer).
- Step 5 (Unit 7): interaction: interaction_sound at propPickupByPlayer :17947 (custom-slot, has_interaction, > 0 -> playerSndStart mirror :17865) + laptop latch at :16857; transfer_payload via recover latch; interaction_action none skips OBJFLAG3_INTERACTABLE at deploy; prompt_ref deferred (lang-id based, validation only).

### Test plan
Static pins: weaponGetNbombStormGraph, storm_activation_policy, autogun_turn_speed, weaponGraphAutogunFireInterval, autogun_beam_interval_ticks60, autogun_friendly_fire_suppression, g_ThrownLaptopLatch, weaponGetStickyDeviceGraph, sticky_attachment_filter, owner_death_behavior, g_PlayersOwnerCleanupPending, interaction_sound, recoverweaponnum; bondgun routing clause pin; player.c mask-set pin. Unit tests: fire-interval conversion table (1800 -> 1, 900 -> 2, 0/neg -> 1); sentinel defaults on minimal fixture (-1 x4); rich-fixture pins keep passing.

### Size verdict
LARGE; 3 sub-slices: (5e-1) autogun + interaction recover; (5e-2) storm + owner-cleanup; (5e-3) sticky-device + remaining interaction.

### Deferred
storm_ref visuals (Unit 8 records only); mission_behavior_ref (scenario-owned per spec); prompt_ref; max_active > 1 (one slot per player); owner_lost distinct from death; net_authority non-server (protocol work); sticky owner_only persistent; bot laptop recovery propagation; AI deploy path.

## Wave 5f - settings-presentation (settings/variables, presentation, camera_effect, MP bit)

### Key facts
- Authored contract: behavior/settings.json + behavior/variables.json per weapon-archive-clean-format.md:68 + :95. Base emits EMPTY tunables (romextract_pdweapon.c:2271-2283 settings = schema/asset_id/dependency_closure only; :2285-2297 variables = empty array). Needler authors real values (build_needler_mod.py:567-575 fire_cadence {value 8, unit centiseconds}, ammo_clip 20 / ammo_reserve 100) and the bindings trio (:583-595 material-slots.json, grip-sockets.json, presentation.json {crosshair: "default"}).
- The compiler parses NONE of it: weapon_graph_runtime.c has zero settings/variables matches; composeWeaponGraphFromAuthoringFiles (:1404-1511) and weaponGraphCompileWeaponSourceJson (:1513-1594) compose only primary + secondary + shared-context; only shared_context reaches IR (:950 -> ir->contexts[16]).
- Catalog checks presence only: assetcatalog_load.c:927-936 refuses held registration if settings_file/variables_file EMPTY; :946-951 loads only primary/secondary/shared. ext.weapon fields exist for settings/variables (assetcatalog.h:308-310); NO fields for material_slots/grip_sockets/presentation (scanner path-qualifies :1018-1020 and drops). Mirror sites: scanner :1820-1829, walker :128-139, netdistrib :1656-1665.
- bindings/ trio has zero engine readers (only the moddinghub editor reads animations/audio manifests; base archives contain NO trio). Conformance requires the trio for mod-shaped archives (asset_archive_conformance.py:546-575) and documents intended consumers + fallbacks (:891-908).
- Held presentation census: sight consumed game_0b0fd0.c:716-717; zoom_fov :203-204; reticle_ref/overlay_ref/camera_effect captured (:2677-2682) with ZERO consumers. Native camera backend: bgunTick vision arm bondgun.c:9435-9480 (DEVICE_XRAYSCANNER / WEAPON_FARSIGHT -> VISIONMODE_XRAY while aiming).
- LATENT BUG (Unit 0b): weapon_graph_archive.c:300-305 matches keys shared_context/settings/variables but every real archive writes *_file spellings (romextract_pdweapon.c:2441-2443; needler :556-558): desc->shared_context empty on the walker path -> shared-context.json silently dropped from every base/mod held IR. Test fixture masks it (authors the non-_file spelling :2197). model/model_file already have alias pairs (the precedent).
- Schema string drift: base pd.weapon_settings.v1 / pd.weapon_variables.v1 vs needler pd.weapon.settings.v1 / pd.weapon.variables.v1; shape drift: base variables array vs needler flat object. B6.4: canonical = base spellings; accept both; converge needler same wave.
- MP: zero net references to the toggle. g_MpSetup.options u32 already on the wire (SVC_STAGE_START write netmsg.c:1271, read :1547) and saved full-width (mplayer.c:4641 / read :4544). Bits 0x20000000/0x40000000/0x80000000 free (constants.h:2963-2998); MPOPTION_BOTJUMP 0x10000000 precedent.
- Redundancy: variables.ammo_clip/reserve duplicate pool manifest ammos[].clipsize (loader_pool.c:1140-1155, 1756-1758 -> bondgun.c:14628).

### Gotchas
- Editor drift: moddinghub presentation template authors zoom_fovs [array] (:1059) but the compiler consumes scalar zoom_fov (:2673).
- Bit-identical-when-OFF and toggle-ON-base proofs: base settings carry zero tunables so the defaults layer is empty for base; assert in tests.
- Save leak: mask MPOPTION_WEAPONGRAPH at the save site (B6.5); verify exactly one serialization path; stage-end + disconnect restores in the same commit.
- Lifecycle: settings records cleared inside weaponGraphRuntimeClearWeapon; registration also runs dependency registration (:2807, :2935-2947) - insert after the existing clear.
- Extended RegisterWeaponSourceJson signature: update the 2 static pins that name it (test_mod_external_archive_static.cpp:3201, test_asset_native_source_contract.cpp:10848).

### Plan
- 5f-1 (Unit 0b): descriptor alias arms (first-set-wins like model/model_file); bugs.md entry.
- 5f-2 (Unit 2): parse settings/variables into bounded weapon_graph_weapon_settings_t (key/type/i/f/string/unit, caps ~32+32, reusing weapon_graph_ir_param_t shape) per weaponnum beside s_held_functions; cleared in ClearWeapon; accept both spellings/shapes; unit enforcement on time-like keys; loud unknown keys at LOG_NOTE; accessor pair weaponGraphRuntimeGetWeaponSettings / ForGameplay (toggle gate); loose path passes ext.weapon.settings_file/variables_file through extended RegisterWeaponSourceJson.
- 5f-3 (Unit 9): defaults layering after held param capture (:2683): fill has_* == 0 fields from settings via fixed key map (fire_cadence -> rpm-based cadence fields, spread, zoom_fov, sight); node params win. $name variable substitution at compile (B6.3); unresolved = loud compile error.
- 5f-4 (Unit 2): ext.weapon.presentation_file[128] + 3 mirror sites; parse sight/zoom_fov into the defaults layer; crosshair default no-op, others loud-logged; fix moddinghub scalar zoom_fov.
- 5f-5 (Unit 9): camera_effect consumer in the bgunTick vision arm (xray while aiming; OG clauses first verbatim; enum none/xray at compile).
- 5f-6 (Unit 9): MPOPTION_WEAPONGRAPH 0x20000000; host ORs at SVC_STAGE_START write when weaponGraphRuntimeEnabled(); client latches + restores at stage end/disconnect; masked at mplayer.c:4641 save site.

### Test plan
[settings] compiler tests: REAL *_file spellings fixture (kills the drift); both schema spellings; array + flat shapes; unit rejection; defaults layering precedence; $name success + unresolved loud failure; toggle gates; base-shaped settings yield zero layered defaults. [presentation] pins: vision-arm clause + OG verbatim-first; moddinghub scalar; presentation_file at 3 mirror sites. [netparity]: MPOPTION value pin + masked-at-save pin + roundtrip latch/restore. Updated signature pins x2.

### Size verdict
M overall (5f-1 XS; 5f-2/5f-3 core M; 5f-4 S; 5f-5 S; 5f-6 S own commit).

### Deferred
material-slots/grip-sockets engine consumers (no native backend; meta-family wave + .pdmaterial parser; optional validation-only parse); reticle_ref/overlay_ref imagery (no .pdui reticle runtime); camera_effect slayer_rocket; ammo variables bridge (redundant); editor settings UI; strict MP mismatch refusal (Wave 7 protocol bump + test_versions pin).

## Wave 6a - meta-families (assetRuntimeFind* consumers)

### Key facts
- API: assetRuntimeFind / FindByTypeAndId / FindByTypeKind / FindByTarget / LoadPrimaryFile / Count (asset_runtime.h:78-89; impl asset_runtime.c:187-231, 677-712). Record asset_runtime_binding_t (asset_runtime.h:12-71): generic id/primary_path/authored_file/deps/target_id/display_name/runtime_id/kind/value0/params[4] + family blocks (gamemode :51-57, botprofile :58-62, scenario :23-36, ui :40-48, lang :37-39).
- Activation: assetRuntimeActivateCatalogEntry (asset_runtime.c:268-658) copies ext.* per family + validates source presence. Call sites: assetcatalog_load.c:749 (lang) + :1246-1247 (metadata path, LOG_WARNING refusal :1252-1255). Bundled meta preloaded at boot (catalogLoadInit :140-146 -> :977-987). All 11 families covered by s_catalogTypeUsesMetadataRuntimePayload (:795-821).
- Zero game-code consumers confirmed (no asset_runtime.h include in src/game).
- botprofile (18 base): records mirror g_BotProfiles[] exactly (registration assetcatalog_base_extended.c:1039-1045, copy asset_runtime.c:495-502). Native reads: mpCreateBotFromProfile mplayer.c:3677-3678 (type/difficulty), :3732/:3743 (body); simulant menu setup.c:3357-3359. Difficulty indexes g_BotDifficulties[] (bot.c:138,159,2245-2277). Mod profiles get real authored values from botprofile.ini (scanner :2267-2284) but are selector-invisible (picker resolves via mp_index, setup.c:3288-3298).
- gamemode (6 base): records mirror s_BaseGameModes[] (base_extended.c:326-365; team_based matches g_MpScenarioOverviews[].teamonly scenarios.c:257-265). Native reads: scenarioCtxAccepts reads ext.gamemode.team_based (scenarios.c:352-362); min/max_players have NO native read site.
- theme (7 builtin): full native consumer pdgui_theme_loader.cpp resolves paths itself from entry->source.primary (register_catalog_theme_entry :1612-1666, apply :1823+); never touches the runtime record.
- Deferred families with reasons: character (no read site left; body/head closed), mission (subsystem rewrite; briefing.json stub), hud (empty slots + procedural Gfx), skin/material (blocked on material parser, LARGE), vehicle (stub physics), prop (invented health values; would change base behavior).
- ID bridge: catalogIdByRuntime (assetcatalog_api.c:584-607), catalogGameModeIdByScenarioIndex (:636-658).

### Gotchas
- Binding presence not guaranteed: preload requires fileProvider primary; pdmeta extraction client-only. Every consumer needs logged native-mirror fallback, never hard dependence.
- Warn once per site (static flag), not per call (scenarioCtxAccepts and the picker run per menu frame).
- Theme ordering unknown (loader registers entries possibly AFTER catalogLoadInit preload): consumer tolerates missing bindings with LOG_WARNING; verify ordering during implementation and record.
- Ungated consumers acceptable ONLY because value-identical to native mirrors; demand value-identity assertion tests in the same commit (critic ruling).

### Plan (Unit 10)
1. botprofile: mpBotProfileRuntimeBinding(profilenum) helper (catalogIdByRuntime -> assetRuntimeFindByTypeAndId; once-per-session CATALOG.BOTPROFILE.RUNTIME_MISS warn; NULL -> native). Consume at mplayer.c:3677-3678 (type/difficulty), :3732/:3743 (body), setup.c:3357-3359.
2. gamemode: scenarioCtxAccepts uses binding->gamemode_team_based when present else ext fallback + once-per-session CATALOG.GAMEMODE.RUNTIME_MISS. min/max_players NOT wired (net-new enforcement).
3. theme: register_catalog_theme_entry prefers binding paths when non-empty + warns when an enabled entry lacks a binding; verify activation ordering and record in the catalog pillar.

### Test plan
Static contract blocks (test_asset_native_source_contract.cpp functionBlock pattern :12404): mpCreateBotFromProfile contains assetRuntimeFindByTypeAndId + bot_profile_type; scenarioCtxAccepts contains gamemode_team_based; register_catalog_theme_entry contains assetRuntimeFindByTypeAndId. Value-identity unit tests in test_asset_runtime_adapters.cpp (extend :1135-1178). Runtime proof: zero RUNTIME_MISS on stock boot; delete one .pdbotprofile -> exactly one loud warning + unchanged gameplay.

### Size verdict
SMALL-to-MEDIUM (~80-120 lines + tests). Leverage rank: botprofile > gamemode > theme > character > mission > hud > skin > material > vehicle > prop.

## Wave 6b - pdeffect (.pdeffect compiler/runtime + OG executor bridges)

### Key facts
- Family contract: asset_archive_conformance.py:790-799 (effect.ini + effect.graph.json|timeline.json); slot contract :1096-1102; validator hook :5022-5023.
- Base emitter: romextract_pdmeta.c s_emitEffect :1314-1388 writes schema "pd2.effect.graph.v1" (:1345) with ONE node {id: apply, kind: effect.<type_key>, params: {shader, intensity}} (:1350). Type keys (s_effectTypeKey :861-871): tint, glow, shimmer, darken, screen, particle. Six base archives (s_BaseEffects[] base_extended.c:294-303, 799-826). Real archive verified (base_effect_particle.pdeffect): nodes as above, edges [].
- Walker binds ASSET_EFFECT (loader_walker_meta.c:620; ext :515-540). Runtime binding copies effect fields (asset_runtime.c:356-371). Netdistrib ingests received .pdeffect (netdistrib.c:2058-2080). Ext struct assetcatalog.h:443-452; EFFECT_TYPE_*/TARGET_* :134-147.
- Pattern to mirror: weapon_graph_runtime toggle :148-152; ForGameplay accessors :674-740; type-keyed compiler weaponGraphSchemaForType :405-413 (returns NULL for ASSET_EFFECT today; compile rejects :1243-1250); s_modules[] :80-110; registration slots + owner-bit lifecycle :503-664; activation hook s_catalogTypeUsesWeaponGraphRuntime (assetcatalog_load.c:839-842) -> s_catalogActivateWeaponGraphRuntime (:990-1055) from the metadata path :1237-1244.
- NESTED INGESTION GAP: weapon_graph_archive.c typeForNestedArchiveName (:124-129) returns a type ONLY for .pdprojectile/.pdentity; nested .pdeffect falls to ASSET_NONE and is DROPPED - the needler proving asset's effect never registers. Effects key by asset_id string (no runtime_index slot needed) so ingestion is not blocked by allocators.
- OG executors: explosiontype struct types.h:4415-4429 (rangeh/rangev/changerates/innersize/blastradius/damageradius/duration/propagationrate/flarespeed/smoketype/sound/damage); g_ExplosionTypes[26] explosions.c:44-85; entries explosionCreateSimple/Complex/Create :87/:92/:238; all 18 table reads inside explosions.c. sparktype types.h:3553-3568 with unk1c/unk20 RGBA consumed at render (sparks.c:403-413); g_SparkTypes[27] :31-93; sparksCreate :178 indexes :181; table already runtime-mutated for blood/paint recolor :192-202; only 3 read sites (sparks.c:181, 399, sparkstick.c:23). smoketype types.h:4462-4476 with per-type r/g/b applied at render smoke.c:216-222; dual PAL/NTSC g_SmokeTypes :24-83; creates :264/:329/:371/:413/:435; 28 direct index sites. nbombCreateStorm takes NO tuning params (nbomb.c:670-705).
- Color reality: explosion COLOR is NOT per-type (render writes one vertex color 0xffffffff + debug-only ecol override, explosions.c:1345-1352; textures fixed g_ExplosionTexturePairs[15] :1354-1361) - pink fireball impossible by table parameterization (renderer work, deferred). Spark COLOR is per-type RGBA and render-consumed - PINK IS ACHIEVABLE via a runtime-extended spark row. Smoke color per-type but 28 sites -> custom rows deferred.
- Needler pink graph (build_needler_mod.py:357-403): nodes effect.explosion (explosion_class "small", tint [1.0,0.4,0.8,1.0]) + effect.spark (tint [1.0,0.5,0.85,1.0]), schema "pd.effect_graph.v1", marked ILLUSTRATIVE pending this runtime. Needler Q2: explosion tint hard-white, pink carried by spark; explosion_ref selects a small OG class for blast/damage parity.
- No wire impact: explosions never type-replicated (netmsg.c:3038-3065); .pdeffect already distributes.

### Gotchas
- Schema split: canonical pd.effect_graph.v1 per B6.4; base emitter updated; legacy pd2.effect.graph.v1 accepted with LOG_WARNING. Existing-archive early-out (romextract_pdmeta.c:1319-1322) checks entry presence only - stale schemas persist; the accept-legacy path covers them.
- Both node-kind families must compile (6 presentation kinds gameplay-inert + gameplay kinds explosion/spark/smoke) or activation of all 6 base archives loud-fails.
- g_SparkTypes rows runtime-mutated by OG code: custom-row registry append-only, never assume const base table.
- Explosion type is s16 plumbed propExplode(prop, s32) -> explosionCreate(s16); slice 1 maps refs to EXISTING indices only (no index growth).
- Bridge header needs extern "C" + C-safe types.
- Per-client toggle divergence inherited (accepted risk, Wave 7).

### Plan (Unit 8)
1. Nested ingestion fix FIRST (bug-class): .pdeffect -> ASSET_EFFECT in typeForNestedArchiveName; route nested effect graph bytes to effectGraphRuntimeRegisterGraphJson.
2. Compiler: ASSET_EFFECT in weaponGraphSchemaForType (canonical + legacy alias); WEAPON_GRAPH_OP_EFFECT_* opcode block (700 range: tint/glow/shimmer/darken/screen/particle + explosion/spark/smoke) in s_modules[]; s_emitEffect canonical schema.
3. New port/src/effect_graph_runtime.c + port/include/effect_graph_runtime.h mirroring weapon_graph_runtime layout: effect_graph_runtime_t {valid, asset_id, source/ir sha, has_explosion + explosion_class[16] + explosion_type s16 + explosion_tint[4], has_spark + spark_type + spark_color1/2, has_smoke + smoke_type, has_sound + soundnum, has_intensity + intensity}; Register{GraphJson,Archive}, ClearAsset/All, Get, GetForGameplay (gated on weaponGraphRuntimeEnabled()).
4. Class -> OG mapping (graph-body-only vocabulary per B2): tiny->6, small->2, medium->11, large->13(ROCKET), huge->17(HUGE17), massive->25.
5. Bridges (frozen at Unit 1b): s32 effectGraphResolveExplosionType(ref, fallback) / ResolveSparkType / ResolveSmokeType / ResolveSound - return fallback when toggle OFF / ref empty / unresolved.
6. Registration plumbing: activation hook beside the weapon-graph hook (ASSET_EFFECT -> RegisterArchive/GraphJson, loud-fail); clear hook beside s_catalogClearWeaponGraphRuntime (:1057-1074).
7. Pink spark delivery (B6.9 in scope): sparks.c runtime spark-row registry: sparkTypeFor(typenum) helper replacing the 3 direct reads; sparksRegisterCustomType(row) -> index >= 27; effect.spark tint compiles to colors; registration allocates the row, stores index in spark_type. Base indices < 27 untouched.
8. Smoke refs map to NEAREST existing SMOKETYPE_* slice 1.

### Test plan
[modding][pdxxx][effect_graph][c3849]: compile all 6 base fixtures (both schemas) + needler pink fixture; deterministic ir_sha256; reject unknown/script-like kinds + wrong-type schema; explosion_class small -> 2; spark tint -> custom row >= 27 with expected RGBA; smoke nearest-type; unresolved -> fallback verbatim; toggle OFF -> GetForGameplay NULL + every bridge returns fallback; static gate pin in effect_graph_runtime.c.

### Size verdict
MEDIUM (~700 lines new runtime mostly mirrored; ~60 compiler; ~50 hooks; ~50 sparks; ~250 tests).

### Deferred
Per-explosion fireball tint (renderer slice 2); custom explosiontype rows (18 sites + s16 space); custom smoke rows (28 sites); nbomb storm parameterization (no knobs); timeline.json; presentation kinds driving the renderer (meta-family work); toggle retirement (Wave 7).

---

Sentinel: end of c3849 wave implementation maps. Waves 1-4 shipped; Wave 5/6 maps above are the live contract; Wave 7 (live-gated flips) remains staged to B-801.
