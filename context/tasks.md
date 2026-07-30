# Tasks

> Live punch list only. Completed implementation narratives belong in
> [session-log.md](session-log.md). The Workbench is the durable live tracker.
> Historical card detail remains in the retired Kanban archive and bug ledger.

Last updated: 2026-07-30

**2026-07-30 CURRENT ACTIVE PROGRAM — Workbench `T-ASSETS-001`.** Re-audit
every asset family from extraction through public `.pdxxx`, catalog/provider
loading, production adapters, generated-cache boundaries, creator workflows,
and no-ROM-fallback enforcement. Parallel production-path audits cover
`T-MENUS-001` and `T-INPUT-004`; fixes remain `missing` until current-tree
evidence identifies and connects them. Required closure gates are `V-001`
through `V-004` and `P-001`. Workbench statuses are verified truth, not the
old board's aspirations.

**2026-07-07 FULL-PARITY EXTRACTION + UTILIZATION (historical completion evidence;
current claims are being re-audited under `T-ASSETS-001`).**
Mike directive: make `.pdxxx` a LOSSLESS, fully-utilized representation of the ROM so
mods = base content, nothing lost, no omitted functionality. Chose full parity (extract
+ utilize). Spec: `context/designs/modding/full-parity-extraction-utilization-2026-07-07.md`.
Gaps (per 2026-07-02 audit + live scope): pdtexture loses N64 format id + CI palette/TLUT
(82% of textures); pdmesh loses per-face collision binding; pdweapon loses public custom
meshes. **DESCOPE (Mike 2026-07-07): LODs OUT** -- no mip emission, no renderer mipmapping;
requirement reframed as hero-assets-always-used + LOD machinery never breaks, and PROVEN
(pdmain.c:426 forced-hero semantics verified; 1,099 meshes / 26,577 DISTANCE nodes scanned,
0 far-only groups; no second LOD mechanism; extractor preserves DISTANCE nodes for
round-trip). DONE: Phase 1a step 1 (schema-v2 manifest: n64_format/num_lods/has_alpha,
re-extracted 3,503, conformance 3503/3503, source-gate 36/36, commit 3c669893). DONE:
step 2 CI palette (8180fa04) -- 2,886 CI textures carry exact TLUT as palette.json
(accessible format; *.bin forbidden by contract), whole-tree conformance ok 9,066,
combat_sim 15/15. **pdtexture is now fully lossless** (format + LOD-count + palette;
LOD image data descoped by Mike). DONE: Phase 1b pdmesh collision (f3141fd5) -- type-0x19
quads (parts 0x65/0x66) emitted into nodes.json (schema v2) AND consumed by the compiler
(round-trip; 733/733 meshes, 24/24 quads real, inner body/head/weapon meshes have zero
type19 = nothing lost). DONE: Phase 1c pdweapon (a231ee44) -- fire models catalog-
addressable ("projectile_model" ref beside the raw int, loader resolves ref-first;
held meshes audited already-complete; 20/20 refs resolve to public meshes). **FULL-PARITY
PROJECT COMPLETE for that 2026-07-07 scope** (all four audited gaps closed; 2a mipmapping cancelled by descope;
2b = check whether base uses palette animation at all, likely documented no-op). Every
step verified: conformance 9,066 ok, source-gate 36/36, combat_sim 15/15, pd-tests
841/841.

**2026-07-04 backlog wave 5 -- reconciliation correction + tooling (committed).** The
"scope COMPLETE" note below was PREMATURE -- the reconciliation sweep under-counted
(c3844-deferred cards were no longer gated). Completed FOUR more: c074 (savemigrate
chain test), c064 (dead manifest-serializer removal), c069 (SP-9 guard -> unit-tested
helper), c070 (worktree-redirect -> unit-tested resolver); c066 moot (.pdbase retired).
Build/self-test verified (22 commits total). Remaining tractable tail is risky/open-
ended: c040 (worktree-DELETION cleanup), c065 (production netmanifest.c refactor),
c068/c041. Those + Mike-gated work (playtests, NAT, group_session.c) are the remainder.

**2026-07-04 backlog push -- AI-tractable scope COMPLETE (16 commits, boundary reached).**
Closed every inventory item completable + verifiable without Mike/network/playtest
(waves 1-3 fully; wave 4 c056+c042). c042 fork CI added (9c952b69). Remaining is all
Mike-gated: c058+relay live in Mike's in-flight group_session.c (must not touch);
c054/055/057/059/060 need real-network NAT verification; the 5 Wave-2 bugs need
playtest/ROM; Wave 5/6 are runtime/playtest-gated + large designs. See session-log
top entry for the precise per-item blocker list. Next session pairs with Mike on the
relay branch + a playtest pass.

**2026-07-04 backlog wave 4 -- connectivity (IN PROGRESS).** c056 Opus auto-detect
verified already-done (CMake pkg_check_modules); stale doc corrected (313d4fb1).
Remaining: c058 (kbps) + MP relay Gap A (p2p_turn.c) are writeable/loopback-testable
next; c054/c055/c057/c059/c060 are NAT-traversal wiring whose proof needs real
network / second endpoint (single-machine constraint) -- best in a dedicated
connectivity session with network/VPS access, not landed blind.

**2026-07-04 backlog wave 3 -- systemic crash-audit (committed to dev).** SP-8: 7
unguarded prop->chr null derefs in bot AI targeting fixed (botinv.c + bot.c,
c144/492dd809); SP-3 jump-height bound hardened to MAX_LOCAL_PLAYERS; SP-1/2/3/6
remaining-audit sites re-verified + cleared (stale refs already fixed). Client build
green. Next: Wave 4 connectivity wiring c054-c060.

**2026-07-04 backlog wave 2 -- open-bug triage (committed to dev).** Two AI-fixable
bugs closed: B-312 inputlayer SIGSEGV RESOLVED-VERIFIED + shutdown stale-pointer
hardening (c073, 94a821da); B-913 mod.json contents/assets documented as forward
affordances, not a bug (c143, 1bedb5e8). Five bugs confirmed Mike-gated and flagged
in bugs.md: B-249 (kill-attribution -> player 0; playtest-gated, B-249.DIAG in
place), B-919 (do-not-fix-blind, needs OG-ROM check), B-855 (code done, needs
gameplay validation), B-772 (needs .pdanim re-extract + animation playtest), B-769
(architectural mesh-render parity, needs on-screen ROM comparison). Next: Wave 3
systemic audits SP-1/2/3/6/8, then Wave 4 connectivity wiring c054-c060.

**2026-07-04 backlog wave 1 -- audit-finding correctness fixes (committed to dev).**
Cleared the four actionable findings from the 2026-07-03 Super Audit as focused
card-anchored commits (client + tests build SUCCESS): connect-code slot dedupe +
exhaustive round-trip test (c139, e351d1ac); older-save loud-fail to stop silent
identity loss (c141, c0393a81); NET_MAX_CLIENTS wire-ceiling `_Static_assert`
(c140, 3a95a257); `n64_padeffectobj` setup-segment stride mirror (c142, 2590a36c).
Part of a goal-driven multi-wave backlog push (excludes Forge + PD Studio). One
follow-up still open: the real v1->v2 save data-migration transform (c141). Next
waves: open bugs B-249/B-769/B-772/B-855/B-913/B-919/B-312, then systemic audits
SP-1/2/3/6/8, then connectivity wiring c054-c060.

**2026-07-02 asset-decode + tooling pass (committed to dev).** B-945 fixed the
extractor's native-texel decode at the root (RDP-parity I4/I8 alpha, IA16 byte
order, RGBA32 channel order via the shared `port/src/texture_decode_pure.{c,h}`)
-- this was the true cause of the credits opaque squares (the B-346 recurrence)
AND the "textures are black" class. B-946 killed a per-boot 87-scenario
re-extraction loop (strstr-on-binary-GLB clean-check + smoke-harness
install_state=current data deletion); warm boot to credits 44s+ -> ~6s.
`credits_alpha_smoke` PASS 8/8, full pd-tests 819/819. Build queue watchdog made
progress-aware (c068, was killing active builds at 60s). Dev Window v3 added
(c3818, `devtools/dev-window-v3/`). Asset pipeline audit consolidated
([audits/asset-pipeline-audit-2026-07-02.md](audits/asset-pipeline-audit-2026-07-02.md)):
remaining extraction gaps are fidelity/BYOR only, no actionable bloat,
utilization sound. Commits: 496b4119, 887df5aa, 3416efb8, 257ec624.

---

## Active Critical Path

| Card | Status | Purpose |
|------|--------|---------|
| `c3844` | In progress (MP relay) | Joanna render bug **ROOT-CAUSED + FIXED 2026-06-24** (degenerate fovy=0 -> projection P[0][0]=-32768 -> geometry 32768x off-screen; clamp in guPerspectiveF; magenta-isolate capture proves a clean Joanna silhouette dead-centre. See B-936/session-log). Remaining for textured-in-room: a SEPARATE fast3d render-state desync after the scene renderer + the pending B-934/B-935/B-938 colour work. Credits B-346 squares PATCHED + visually proved 2026-06-30 (shaped glyphs/masked trails; Mike playtest still welcome). MP relay (A+C): **Gap B** (onPairOpen honors relay addr, group_session.c) DONE/uncommitted; **NEXT = Gap A** relay forwarder in p2p_turn.c (recv ALLOC->ALLOC_ACK+bind; RELAY->forward w/ src/dst rewrite) + loopback-sim + VPS doc; then other-modes check (item d). |

**B-346 credits squares patch (2026-06-30):** diagnostics pin the
solid credits glyphs to a Fast3D/OpenGL state-cache desync at frame start.
`gfx_opengl_start_frame()` disables `GL_BLEND`, but Fast3D's cached
`rendering_state.alpha_blend` could still be true from the prior frame, so the
first translucent credits draw skipped re-enabling blending. The CI4 glyph data
and IA16 palette decode were already correct (`use_alpha=1`, shaped alpha rows);
the visible failure was stale blend state. Patch invalidates the Fast3D
alpha/modulate/additive cache after backend frame start and keeps the earlier
TLUT/cache-key hardening as a guard. Verified: focused static B-346 tests PASS
(27 assertions / 4 cases), `asset_native_source_guard.py` PASS, isolated build
PASS, and delayed credits capture
`.claude/smoke-verify-runs/screenshots/20260630T025340-credits_alpha_smoke_delayed_local/`
shows shaped glyphs and masked particle trails. Harness note: the smoke result
was 7/8 assertions with exit code 0, failing only the stale required log-line
pattern `LOAD: lv\.c entering stage load sequence for stagenum=0x5c`.

**B-346 recurrence RESOLVED AT ROOT 2026-07-02 (B-945 + B-946).** Mike reported
the credits motes/font still rendered as solid opaque rectangles after the
blend-cache patch. Root cause was in EXTRACTION, not render state: the
native-texel decode wrote I4/I8 textures with forced-opaque alpha (RDP
replicates intensity into alpha -- the motes are I-format soft blobs), swapped
IA16's big-endian I/A byte pair (transparent glow sprites decoded as opaque
BLACK -- Mike's "some textures are black"), and scrambled RGBA32 channel order.
Fixed via the shared pure decoder `port/src/texture_decode_pure.{c,h}` with
fast3d bit-replication parity, stored-alpha-only alphaMode classification,
explicit `"alphaMode":"OPAQUE"` emission plus a renderer guard so regenerated
scenes never re-promote, and `_iafix_b945` cache-kind bumps forcing a one-time
re-extract. B-946 additionally killed a per-boot 87-scenario re-extraction loop
(strstr-on-binary-GLB clean-check + unconditional texCoord probe + smoke
harness deleting the data tree for install_state=current): warm boot now
reaches the credits in ~6s and `credits_alpha_smoke` PASSES 8/8 with captures
showing shaped glyphs, soft fog planes, and round motes. Full pd-tests
819/819. Details in bugs.md B-945/B-946.

**CI menu render (c3844, 2026-06-23):** the glass-table opaque-black bug is fixed
universally -- the `.pdscenario` extract now classifies glTF alphaMode from the
opaque/xlu block split (B-941) and the scenario scene renderer honors it per
material (B-940). Verified on screen: glass translucent, menu fonts crisp,
CITRAINING classifies opaque:63/mask:3/blend:15 (was 0 BLEND / 12 MASK). Commits
`1472789c`, `91124983`.

**B-942 chr-body skeletal scramble -- RESOLVED 2026-06-25 (`07676f76`).** Joanna's
held-pose leg/limb scramble was per-FACE matrix capture collapsing N64
weighted-vertex skinning (225/601 seam tris mis-bound; the bent CI hold anim 1157
exposed it). Fixed by a per-VERTEX matrix channel in `.pdmesh` extract+consume --
RENDER-VERIFIED, her whole figure (head/torso/arms/legs/feet) connects cleanly on
black and in-scene. Port-wide (all generated chr bodies; systemic SP-17). Diagnosis
`922d8217`; conformance-pin unblock `a3019c57` (scene.glb v11->v12, also clears the
standing 8/804 test drift). REMAINING: a translucent box around her legs IN-SCENE is
a SCENE element (gone under `--debug-hide-scene`, NOT the chr) -- untraced (likely
the desk/table); plus the deferred chr backlog (BG room lights, anim channels B-772,
pdtexture fidelity).

## Joanna render bug -- ROOT-CAUSED + FIXED 2026-06-24 (degenerate fovy=0 projection)

The "menu Joanna renders ZERO pixels" bug is solved at the root and the fix is
committed. It was NOT colour/combine/cycle (B-934/B-935/B-938 all wrong layer) and
NOT framing. Prior "in-frame" was VIEW-space only; CLIP-space instrumentation proved
her vertices project to garbage (clip x/y in the millions, `onscreen=0`) because the
world projection `P[0][0]=-32768` -- the CI intro-cutscene camera feeds `fovy=0` into
`viSetFovY`, `guPerspectiveF` blows `cot(0/2)` to +inf, and `guMtxF2L` overflows the
x/y scale to INT_MIN. **Fix:** clamp degenerate fovy in `src/lib/ultra/gu/perspective.c`
(the same guard `scenario_scene_renderer.cpp:1018` already has). Verified: forced-magenta
isolate capture shows a clean Joanna HUMAN SILHOUETTE dead-centre
(`.claude/smoke-verify-runs/screenshots/20260624T092726-main_menu_joanna_isolate/iso2_a_full.png`).
Full breadcrumb in **B-936**.

**Next for FULL textured-in-room visibility (two separate items, deferred):**
1. **fast3d render-state desync after `scenarioSceneRendererRender`** -- with the room
   visible, even no-Z (GL_ALWAYS) magenta is hidden; the raw-GL scene renderer leaves
   GL state fast3d's `rendering_state` cache doesn't reflect (HUD fonts re-sync, the
   first world draw doesn't). A depth/shader/texture cache-invalidate did NOT fix it ->
   blocker is blend/VAO/framebuffer/other state. Probe with
   `--debug-show-only-mesh combat --debug-force-chr-prim` (scene ON) -> still invisible.
2. **B-934/B-935/B-938 colour/combine** -- so she renders textured, not dark.

## Uncommitted working-tree candidates (2026-06-24)

- `port/src/net/group_session.c` -- MP relay **Gap B** (onPairOpen honors `relay_ipv4/port`
  for `P2P_EP_RELAYED`). ALWAYS-active; NEEDED for the relay; pending build-verify with
  Gap A (the forwarder). LEFT UNCOMMITTED (unrelated to the B-936 fix commit).

Committed with the B-936 fix (2026-06-24): the fovy clamp (`perspective.c`), the
`--debug-track-chr` tracker (`model.c`), the `--debug-force-chr-prim` / `--debug-hide-scene`
/ CLIPDBG diagnostics (`gfx_pc.cpp`, `modasset_compiler.c`), and the
`main_menu_joanna_*` capture scenarios. The modasset_compiler cycle-type markers
(REFUTED) + `--debug-mesh-matclass` rode along in that file (byte-neutral / flag-gated).

**c3849 status (2026-06-17):** Waves 1-7 are SHIPPED and verified for the current
tree. Waves 1-6 delivered telemetry, the four private runtime allocators, FONT
consumer, texture emitter/source binding, Wave-5 weapon/projectile/entity graph
consumers, Wave-6a metadata consumers, and Wave-6b `.pdeffect` runtime. Wave 7 is
now complete: weapon graph runtime is product-default ON, the old
`Debug.WeaponGraphRuntime` user setting and `MPOPTION_WEAPONGRAPH` transient MP
option are retired, normal-play fallback cutover is fatal for the selected
source-owned families, and `NET_PROTOCOL_VER` is v51 so mixed v50/v51 peers are
rejected at auth. The fresh live proof is
`.claude/smoke-verify-runs/results-20260617T193054Z.json`:
`needler_graph_runtime_visual_smoke` passed 40/40, proving an installed Needler
`.pdmod` loads `.pdweapon` held source, nested projectile/effect/mesh archives,
first-person `.pdmesh::model.gltf`, and live `MODASSET.RENDER` without the old
toggle or ROM fallback. Maps + binding specs remain in
`context/designs/catalog/c3849-wave-implementation-maps.md`.

`c3844` was closed on 2026-06-12 as **Asset Pipeline: 100% source/runtime parity
closure**. This is no longer a broad archive-format migration. The final runtime,
retained-output currentness, private bridge, and tool workflow gates are green
for the current tree.

Needler visual proof refresh, 2026-06-17T23:58Z: the held-model source/runtime
path is green by logs, assertions, and retained screenshots, and B-933's
null-`rwdata` crash is fixed. The latest
`needler_graph_runtime_visual_smoke` passed 43/43 in
`.claude\smoke-verify-runs\results-20260617T235837Z.json`, with
`BONDGUN.SOURCE` and `MODASSET.RENDER` for `mod_needler:needler_model` at
300 vertices / 100 tris. The two retained BMP screenshots under
`.claude\smoke-verify-runs\screenshots\20260617T195640-needler_graph_runtime_visual_smoke\`
show the source-render proof overlay, so the screenshot-inspection gap is now
closed for this proof slice.

The 100% target means every asset family can be extracted into a public typed
archive, inspected or edited as accessible source, re-imported or packaged by
the mod tools, distributed when required, and consumed by the game through the
same catalog/provider path as base content. Renderer/GPU/collision/audio-codec/
animation/graph/runtime products are allowed only as source-hashed rebuildable
cache. Public TSV, public `.bin` dumps, raw native table dumps, numeric asset
identity, and runtime ROM/RomProvider fallback after extraction are failures.
Private integer-only handoffs may remain only as catalog-owned bridge debt while
the current engine still requires them.

`c3848` (opened 2026-06-10 from the Needler proof-of-need) is now closed for the
current custom weapon mesh gap. The embedded `.pdweapon` -> `.pdmesh` catalog
ingestion path allocates private custom model slots, maps them to private source
filenums, preserves archive-member source paths through `.pdmod::needler.pdweapon`
VFS roots, and the 2026-06-17 Needler smoke proves live first-person render from
`weapon.pdmesh::model.gltf` at filenum 2016.

---

## c3844 Completion Gates

1. **B-801 safe live visual/audio proof**
   - Build or use bounded CPU-safe probes first because prior live tests made the PC unresponsive.
   - Then run narrow live proof for level/Scenario visuals, character/body/head visuals, weapons/props/projectiles/entities, textures/materials/skins/effects/UI/fonts/lang, animations, SFX/voice/music, and gameplay-authored data that affects those surfaces.
   - Enable only relevant logging and inspect the logs after each run.
   - 2026-06-09: Scenario CPU pre-flight is now repeatable. `build-session.ps1 -Target probe` builds `scenario-scene-probe`; `devtools/scenario-scene-probe-sweep.ps1 -Session <id>` probes every `.pdscenario` (87/87 green on the fresh tree) with no window/GPU/audio. Remaining CPU pre-flight gaps before any live pass: standalone anim/mesh/audio decode probes for the non-scenario families. The narrow live pass itself remains the only live-blocked step.
   - 2026-06-10: Audio CPU pre-flight is now repeatable. `tools/verify_audio_sources.py` validates `.pdsfx`, `.pdvoice`, and `.pdsong` archives without launching the game, checking WAV timing metadata, MP3/Vorbis headers, sequence event counts, loop bounds, and effective pitch buckets. Current retained install and Build audio trees both pass: 2,108 archives, including 104 MP3 voices and 119 sequence songs.
   - 2026-06-11: Animation CPU pre-flight is current. `tools/verify_pdanim_sources.py` validates checked-in examples plus retained and Build generated animation trees without launching the game. Current retained and Build animation trees both pass: 1,060 archives, including 950 character GLTF clips and 110 weapon command animations.
   - 2026-06-11: Mesh CPU pre-flight is current. `tools/verify_pdmesh_sources.py` validates `.pdmesh` OBJ/GLTF/GLB source geometry, integer-native coordinate/UV quantization, semantic hierarchy JSON, face-to-render-command coverage, and declared manifest/descriptor counts without launching the game. Current proof passes checked-in examples, retained and Build generated mesh trees with 733 archives each, plus Build nested character meshes with 5 archives.
   - 2026-06-11: Custom body/head assembly CPU/static proof is current. Static coverage ties private body/head slot allocation, walker forced-slot parsing, catalog reverse lookup, public typed archive loading, source-backed modeldef conversion, slot-zero fallback refusal, and generated-modeldef instantiation together before the live character render proof.
   - 2026-06-11: Scoped live-proof logging support is ready. The smoke harness accepts named masks such as `game,catalog,render`, and the Scenario/weapon source-gate smokes no longer enable log-all.
   - 2026-06-11: First bounded no-sound weapon/hand live proof is green. `weapon_match_source_gate_smoke` passed 45/45 after the mask was corrected to include the existing `MATCHSETUP` network-channel classification. The run proves the DY357 held model, first-person hand model, and weapon command animation are loading from public typed archive source and reaching live render diagnostics.
   - 2026-06-11: Automated live smokes now suppress and reap native Windows application-error popups. Root cause was two-part: the child process originally lacked `SEM_NOGPFAULTERRORBOX`, and the smoke runner still disabled the in-game crash handler by default with `--no-crash-handler`. `crashInit()` now sets `SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX`; `tools/smoke-verify/run.ps1` keeps the crash handler enabled unless `PD_SMOKE_DISABLE_CRASH_HANDLER=1` is set, and reaps smoke-owned `PerfectDark.exe` / `PerfectDarkServer.exe` / `WerFault.exe` processes before launch, after each run, and at final teardown. Verified by focused `[logging][crash][static][b927]`, parser check, source guard, and bounded `boot_smoke` 14/14 with no lingering fault processes. Follow-up B-928 is also fixed: `videoShutdown()` now frees only owned display-mode heap storage, and a fresh `boot_smoke` exits with OS code 0 and no `exit-code override`.
   - 2026-06-11: Fallback telemetry classification is closed for the extraction-bootstrap false positives. Root cause: first-launch extraction intentionally reads ROM as bootstrap input before per-file cache exists, but `romdataFileLoad()` was counting those reads as post-extraction runtime fallback. `romExtractIsBootstrapping()` now suppresses `LOUDFAIL.FALLBACK` / `ASSET.FALLBACK` only while extraction/verification is bootstrapping; true runtime fallback after extraction remains loud and counted. Verified with a clean `weapon_match_source_gate_smoke`: extraction ran, the smoke passed 45/45, and fallback count was zero for both signatures.
   - 2026-06-11: Scenario-level live source-render proof is green. `scenario_pads_source_gate_smoke` passed 154/154 against the isolated `b801scenario` client. The log shows `SCENARIO.RENDER` activated `base:scenario_chicago` from source scene data with 19,992 vertices, 92 groups, 93 materials, 98 images, 39 alpha textures, and 4,697 mesh-collision triangles; it accepted the camera frame and rendered the native source scene. Log review found zero `LOUDFAIL.FALLBACK`, zero `ASSET.FALLBACK`, zero `ASSET.SOURCE_ONLY`, zero Scenario render rejects/skips, and zero fatal/access-violation signatures. Historical non-blocking notes: that run saw the shutdown OS exit-code override later fixed as B-928, firewall-rule setup was denied under `--no-net`, and unrelated startup `CATALOG.WEAPON.CUSTOM_SLOT_FAIL` / `WEAPONGRAPH.MESH.SCAN` warning noise remains outside this Scenario proof.
   - 2026-06-11: All-family non-Scenario source activation proof is green after smoke harness cleanup. `all_family_source_gate_smoke` passed 36/36 against the isolated `b801allfam` client, and `archive_walker_source_gate_smoke` passed 19/19 after replacing the stale removed font ID with `base:font_handelgothicsm` and extending the clean-extraction dwell to 40 seconds. The scoped logs showed representative source-only catalog loads for weapon, character, head, body, arena, mission, model, animation, texture, material, skin, effect, prop, vehicle, SFX, voice, song, UI, font, lang, gamemode, botprofile, HUD, and theme, with zero `LOUDFAIL.FALLBACK`, `ASSET.FALLBACK`, `ASSET.SOURCE_ONLY`, missing/failed debug loads, fatal errors, or access-violation signatures. This is broad activation proof, not live audible playback or proof that every rendered non-Scenario surface is visually correct on screen.
   - 2026-06-11: The first bounded `custom_body_live_render_smoke` found a real remaining character-render bridge failure. The debug match setup accepted `example:tri_body` and `example:tri_head`, but live bot allocation later resolved `bodynum=-1` / `headnum=-1`; `BODY.IDENTITY` correctly refused the old slot-0 visual fallback, no `LOADER.WALKER.BODY.CUSTOM_SLOT` / `LOADER.WALKER.HEAD.CUSTOM_SLOT` logs appeared for the staged mod, and `MODASSET.RENDER` never fired for `example:tri_body`. Next fix: mirror the body/head private-slot allocator and forced-slot loader-pool parse path into local/mod scan, network distribution, and any mod registration path that can register `.pdbody` / `.pdhead` outside the base walker.
   - 2026-06-11 follow-up: a first scanner/distribution bridge pass was implemented and verified CPU-side (`asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]`, `[catalog][bodyhead][slots][c3844]`, and isolated `b801bodylive` all-target build passed), but the rerun of `custom_body_live_render_smoke` still failed. The log now proves source activation reaches the compiler for `example:tri_body` / `example:tri_head` from `.pdbody::mesh.pdmesh::model.gltf`, but match/bot allocation still receives `bodynum=-1` / `headnum=-1`. Treat the remaining break as a registration/selection-row overwrite or bypassed direct typed-archive/mod registration path, not as missing extraction or missing model source.
   - 2026-06-11 direct-registration bridge patch: direct `assetCatalogRegisterBody` / `assetCatalogRegisterHead` now resolve declared or private body/head slots and write `runtime_index`, while the `mod.json` body/head registration path no longer requires public numeric slots or overwrites runtime indices with sequential offsets. Scanner/distribution nested `.pdmesh` source detection now accepts `model.obj`, `model.gltf`, or `model.glb`. Verified CPU/build-side with `asset_native_source_guard.py`, focused `[catalog][bodyhead][slots][c3844]`, focused `[modding][pdxxx][c3844][source][static]`, combined focused selectors, and isolated `b801bodyreg` / `b801bodyreg2` all-target build/log scans.
   - 2026-06-11 live rerun after the direct-registration bridge patch failed earlier than the old custom-body render proof. `custom_body_live_render_smoke` stopped at stage-load catalog health with `CATALOG-MISS: catalogGetBodyIsComplete bodynum=92 in-range but unregistered` before the scripted custom-body render assertions. Root cause was the sparse loader pool claiming unparsed base body/head slots were populated once any custom body/head was parsed. `loaderPoolGetBody` / `loaderPoolGetHead` now return NULL for active-but-unpopulated sparse slots so catalog managers fall back to authored base records. Verified with `asset_native_source_guard.py`, focused `[catalog][checked]` tests, and isolated `b92pool` all-target build.
   - 2026-06-11 B-920 archive/compile/allocation fix: checked-in typed examples now write nested `.pdbody` / `.pdhead` mesh archives with the full editable `.pdmesh` sidecar set (`model.gltf`, `model.mtl`, hierarchy/parts/faces/render JSON) and descriptor/manifest keys for those members. Body/head loader walkers now resolve `model.obj`, `model.gltf`, or `model.glb` from nested mesh archives instead of assuming OBJ. The hierarchy compiler now treats group `"-"` as an explicit wildcard, generated body modeldefs get `g_SkelChr` plus `MODELNODETYPE_CHRINFO` root defaults, and generated-source body recognition no longer depends on zero parts. Verified with example regeneration, all-family example conformance, native-source guard, focused archive/provider/source static tests, and isolated `b920sidecar` all-target build.
   - 2026-06-11 bounded `custom_body_live_render_smoke` rerun after the B-920 fix is not green yet, but the failure point moved forward. The log now proves `example:tri_body` / `example:tri_head` custom slots are registered, debug bot appearance is applied, `example:tri_body` compiles from public source with `vertices=3`, `tris=1`, and `skeleton=SKEL_CHR`, the catalog activates the external body modeldef, and bot allocation receives non-null custom modeldefs. Remaining blocker: during the 48-second smoke window render diagnostics still only see `base:dark_combat`; no `MODASSET.RENDER` line appears for `example:tri_body`. Treat this as a final scene-visibility/render-audit handoff issue or a final handoff gap after allocation, not as missing extraction, missing sidecars, or missing model source.
   - 2026-06-11 manager-lifetime follow-up: the custom body/head `reason=unpopulated` failure is fixed. Root cause was boot order: external mod/component scan registered `example:tri_body` / `example:tri_head` into the body/head managers, then `catalogManagerHeadInit()` / `catalogManagerBodyInit()` ran afterward and cleared the custom slots. Manager init now happens before external component scan, and custom manager slots are preserved outside active loader-pool fallback. Verified with `asset_native_source_guard.py`, focused `[catalog][checked]`, focused c3844 source/static tests, and isolated `c3844life2` all-target build. The bounded `custom_body_live_render_smoke` rerun still is not green, but it now proves slot 152 survives through scanner registration, match setup, and `CHR.DIAG: botAlloc`; `example:tri_body` compiles/activates from public `.pdbody::mesh.pdmesh::model.gltf`. A later placement-hook attempt moved the debug bot placement to after normal prop/scenario ticks and before prop sorting, forced the bot prop enabled/onscreen for 180 frames, and passed the focused static pins plus isolated `c3844bodyrender` all-target build. The follow-up smoke still failed 26/32: the hook logged `--debug-place-bot-near-player consumed` in player room 57 and normal generated model render diagnostics fired for base props, but no `MODASSET.RENDER` line appeared for `example:tri_body` and the run ended before scripted exit. Remaining blocker at that point: prove whether the slot-152 bot enters `propsSort` / `propsRender` / `chrRender` and whether the generated body modeldef reaches `modelRender`.
   - 2026-06-11 trace-instrumentation follow-up: a scoped `BOT.RENDER.AUDIT` trace was added and build-verified for the debug-placed bot at `placed`, `propsSort`, `propsRender`, room assignment, `chrRender`, and `modelRender` handoff points. Verification passed `asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]`, and isolated `c3844trace` all-target build. The rerun of `custom_body_live_render_smoke` failed before it could prove that render path: the smoke harness parsed the named mask as `channel_mask=0x0000`, clean-install fallback warnings reappeared, and the staged example mod did not register (`MANIFEST-SP: load failed 'example_typed_pdxxx_basic' -- asset missing`); no `example:tri_body` custom-slot, debug-appearance, `BOT.RENDER.AUDIT`, or `MODASSET.RENDER` lines appeared. Treat this as a smoke/mod staging/log-mask regression first, not new evidence that the render handoff is still missing.
   - 2026-06-11 smoke-runner correction: `tools/smoke-verify/run.ps1 -Build` now binds the smoke install to the freshly built isolated session binary instead of falling back to stale shared `Build/PerfectDark.exe`; focused static coverage pins that behavior. Verification passed `asset_native_source_guard.py`, PowerShell parser check for `run.ps1`, and focused `[modding][pdxxx][c3844][source][static][b801]`.
   - 2026-06-11 earlier bounded `custom_body_live_render_smoke` rerun: the fixture/staging/log-mask regression was cleared. The runner used `.claude/session-builds/c3844smoke/PerfectDark.exe`, the log mask was `0x0B87`, `example_typed_pdxxx_basic` loaded, `example:tri_body` / `example:tri_head` registered at slot 152, match setup and bot allocation used body/head 152, and the body compiled from public `.pdbody::mesh.pdmesh::model.gltf` source with 3 vertices / 1 triangle / `SKEL_CHR`. The smoke was still red at that point: it failed 28/36 and exited with `0xC0000005`. `BOT.RENDER.AUDIT` proved the placed bot reached `propsSort-included`, `propsRender-call`, and `chrRender-modelRender`, but by render time the audit showed `head=-104`, `hidden=0x100000`, an earlier `chrRender-alpha-skip`, and no `MODASSET.RENDER` line for `example:tri_body`. That failure is historical after the later head-width, lifecycle, and matrix-prep fixes.
   - 2026-06-11 head-slot overflow follow-up: the `head=-104` mutation is fixed. Root cause was `struct chrdata.headnum` being signed 8-bit while the private custom head slot is 152. `chrdata.headnum` is now `s16`, focused body/head static coverage pins that it cannot narrow back to `s8`, `asset_native_source_guard.py` passed, focused `[modding][pdxxx][c3844][source][static][bodyhead]` passed, and isolated `c3844head16` all-target build passed. A later bounded `custom_body_live_render_smoke` still failed 30/36 with `0xC0000005`, but `BOT.RENDER.AUDIT` then reported `head=152` through placement, `propsSort`, `propsRender`, `chrRender-enter`, `chrRender-shade`, and `chrRender-modelRender`. That failure is historical after the later lifecycle and matrix-prep fixes.
   - 2026-06-11 latest log review update: the `custom_body_live_render_smoke` expectations now need cleanup, but the real failure point is clearer. `CUSTOM_SLOT` scanner assertions were looking for archive-file paths even though current logs put folder paths on `CUSTOM_SLOT` and archive paths on `LOADER_SLOT`, and the old `MODELDEF.SOURCE` expectation is superseded by `MODASSET.COMPILER: built hierarchy modeldef ...`. The actual runtime break is that the legacy stage-category diff runs during an MP/client-manifest-owned match load and releases match assets on the base stage: `MANIFEST: unload 'example:tri_body'`, `example:tri_head`, and `example:tri_weapon` all drop to zero and are freed after `CATALOG: stage 0x1d diff -- load:0 unload:3`. The body/head manager can then retain stale modeldef pointers after catalog unload. Fix the ownership/lifetime boundary first: skip the old stage diff while `g_ClientManifest` owns match assets, and clear body/head manager cached modeldefs before freeing catalog-owned generated modeldefs.
   - 2026-06-11 lifecycle patch follow-up: the MP manifest ownership/lifecycle failure is patched and verified CPU/build-side. `lvReset()` now skips the older stage-category diff while `g_ClientManifest` owns MP match assets, generated body/head modeldef unload clears body/head manager cached modeldefs before free, and `custom_body_live_render_smoke` now forbids the old stage-diff unload plus specific `MANIFEST: unload 'example:tri_*'` regressions. Verification passed `asset_native_source_guard.py`, diff check, focused `[modding][pdxxx][c3844][source][static]` (935 assertions / 10 cases), `[catalog][checked]` (113 assertions / 19 cases), `[modding][pdmod][static][c3809]` (1165 assertions / 16 cases), and isolated `c3844lifefix` all-target build. The bounded live smoke improved to 36/39 and no longer fails on the lifecycle assertions, but still exits `0xC0000005` before scripted exit and never emits `MODASSET.RENDER` for `example:tri_body`.
   - 2026-06-11 generated-body render handoff fix: the remaining crash was matrix-prep timing, not extraction/source loss. The debug-placed bot can reach `chrRender` in the same frame before normal character matrix allocation has populated `model->matrices`; generated render audit then crashed before `MODASSET.RENDER`. `chrRender` now prepares matrices on demand before `modelRender` when needed and logs the narrow `chrRender-matrix-ondemand` audit for the debug bot. The smoke fixture now requires that handoff plus one `MODASSET.RENDER` for `example:tri_body`. Verification passed `asset_native_source_guard.py`, diff check, focused `[modding][pdxxx][c3844][source][static]` (935 assertions / 10 cases), isolated `c3844mat` all-target build, and bounded `custom_body_live_render_smoke` 41/41 with exit code 0 and scripted exit.
   - 2026-06-11 live SFX/voice/music playback proof is green. `audio_live_playback_source_smoke` passed 21/21 against the fresh isolated `c3844audio2` client. The log used scoped `channel_mask=0x028A` (`catalog,audio,game,system`) and proved source-only playback starts for `base:sfx_alarm_2` (SFX), `base:voice_cover_me_aiw` (voice), and `base:song_sequence_a` (music sequence with public `sequence.mid` plus `sequence.json` source). The reviewed log has zero `ASSET.SOURCE_ONLY`, `LOUDFAIL.FALLBACK`, `ASSET.FALLBACK`, missing/fail/category-mismatch, fatal, access-violation, `RomProvider:filenum`, or `--no-sound` signatures. Historical non-blocking notes: that run saw the shutdown OS exit-code override later fixed as B-928, firewall-rule setup can be denied outside admin, and unrelated startup warning noise remains outside this audio proof.
   - 2026-06-11: Scenario normal-play fallback posture is closed for the stage-load payloads that still had legacy runtime fallback after public source failure. `setupLoadFiles` now fail-closes if level graph activation fails, or if setup/pads source compilation fails; `tilesReset` fail-closes after tile source compile failure; and `bgReset` fail-closes after Scenario source background renderer activation failure. The shared fatal message names the stage, payload, legacy id, and public `.pdscenario` source, and states that runtime ROM/RomProvider fallback after extraction is an asset-chain failure. Guard coverage now rejects the old setup/pads/tiles/background ROM fallback signatures. Verification passed `python tools\asset_native_source_guard.py`, Python compile for the guard, focused `[modding][pdxxx][c3844][source][static]` (985 assertions / 11 cases), and isolated `c3844normal` test build after increasing the compile watchdog.
   - 2026-06-11/12 B-801 final fallback/fatal-cutover audit: static/source audit found no new direct runtime ROM/RomProvider fallback that needs a narrow code patch. Live paths are either eliminated, catalog/provider-owned private bridges, or loud/fatal under source-only enforcement. The prior B-929 smoke blocker is resolved: base source probes now defer until extraction/source emit is complete, animation probes wait for `g_Anims` before catalog activation, proof-only audio/animation probes exit through the smoke harness, `.pdui` repair waits for boot completion, and generated animation cache names use short private digest keys to avoid Win32 path overflow. Verification passed `python tools\asset_native_source_guard.py`, focused fallback/static selectors (19,675 assertions / 79 cases), focused c3842 source/static tests (878 assertions / 8 cases), external archive compile coverage (339 assertions / 1 case), isolated `b929` all-target build, and grouped smokes `all_family_source_gate_smoke` 36/36, `audio_live_playback_source_smoke` 21/21, and `base_animation_source_probe_smoke` 19/19 in `.claude/smoke-verify-runs/results-20260612T025658Z.json`. This was the readiness gate for the later c3849 Wave 7 cutover, now implemented and verified on 2026-06-17.
   - 2026-06-12 final all-family regression sweep: `c3844final` is green across the requested final gates. Offline validators passed: `asset_native_source_guard.py`; `verify_pdxxx_modder_workflow.py` (27 families, 59 archives, 31 nested, 229 public sources); conformance selftest (16 parity cases + recursion); checked-in example conformance (28 root / 59 checked archives across all 27 families); retained `Build\data\ntsc-final` conformance (8,065 root / 9,066 checked archives across all 26 retained families); audio CPU validator (2,111 archives); mesh CPU validator (734 archives); animation CPU validator (1,062 archives). Focused static tests passed 10,341 assertions / 60 cases. Final live smoke matrix `.claude/smoke-verify-runs/results-20260612T035120Z.json` passed 9/9 tests and 426/426 assertions: Scenario 154/154, weapon 45/45, custom body/head 41/41, archive walker 19/19, all-family 36/36, audio 21/21, animation 19/19, pdxxx workflow 58/58, Public Mods `.pdmod` install 33/33. Isolated all-target build `c3844final` passed client and updater. Board JSON parsed cleanly with 154 cards, 1 active (`c3844`), 0 unresolved decision requests, 4 columns, schema 2, semantic 0.4.0. Blocking sweep regressions fixed: B-929 and B-930.
   - 2026-06-12 closeout: `c3844` is moved to Done. Keep every non-`c3844` card deferred unless Mike explicitly reopens it or promotes it.
   - 2026-06-17 c3849 Wave 7 cutover: implemented after the B-801 readiness gate. Verification passed full `pd-tests` (41,191 assertions / 804 cases), focused c3849 Wave 7 tests (937 assertions / 24 cases), isolated `wave7` all-target build, `asset_native_source_guard.py`, Needler archive conformance (12 checked archives across `.pdeffect`, `.pdmaterial`, `.pdmesh`, `.pdprojectile`, `.pdtexture`, `.pdweapon`), and `needler_graph_runtime_visual_smoke` 40/40 in `.claude/smoke-verify-runs/results-20260617T193054Z.json`.

2. **Custom body/head full runtime equivalence**
   - B-904, B-905, and B-906 removed wrong slot-zero and selector-cache failures.
   - Remaining body/head work is the final slot-indexed runtime boundary: add direct catalog-ID consumers or catalog-owned private body/head runtime allocation.
   - Do not expose numeric body/head slots to modders.
   - 2026-06-09: FULLY DESIGNED. See `context/designs/modding/body-head-private-slot-allocator.md`.
   - 2026-06-10 (B-909): IMPLEMENTED per Option B. `CATALOG_MGR_{BODY,HEAD}_CUSTOM_*`/`_TOTAL` constants (manager + pure headers, `_Static_assert` lockstep); arrays/bounds grown to TOTAL while population/random-gender loops stay at base 152; new `assetcatalog_body_head_slots.c` allocator (dedup by id, loud `CATALOG.{BODY,HEAD}.CUSTOM_SLOT_FAIL` on exhaustion); `loaderPoolParse{Body,Head}JsonForSlot` forced-slot parse; walker wiring (allocate slot + set `runtime_index` + forced-slot parse); reset beside the weapon-slot reset. Verified: client `-Target all` PASS (26s); `[catalog][bodyhead][slots]` + manager bound tests PASS (438 assertions / 51 cases); `[catalog][provider][static]` no regression; guard + examples conformance PASS.
   - 2026-06-11: CPU/static character assembly proof added and verified for the base walker/assembly chain. The live custom-body proof then exposed that mod/local-scanned `.pdbody` and `.pdhead` entries are not yet receiving that same private runtime-slot bridge. This is now the concrete implementation gap folded into B-801.
   - 2026-06-11 follow-up: the scanner/distribution bridge was started and static/build verified, but live smoke still showed `bodynum=-1` / `headnum=-1` for the debug-selected mod body/head. A subsequent direct-registration patch now assigns private slots inside `assetCatalogRegisterBody` / `assetCatalogRegisterHead` and stops `mod.json` registration from overwriting them; this is CPU/build verified. The later slot-92 sparse-loader-pool failure is also fixed and verified. The B-920 nested sidecar/source and generated-body modeldef compile issues are now fixed and verified. The checked accessor OOB failure for custom private slots is fixed and verified. The custom field-record lifetime loss is also fixed by initializing body/head managers before external component scan and preserving durable custom manager records outside active loader-pool fallback. The placement timing probe and deeper render handoff trace are implemented and static/build verified. The smoke runner now uses the fresh isolated build, the staged example mod and scoped log mask are restored, and live proof reaches `chrRender-modelRender`. The `head=-104` signed overflow is fixed by widening `chrdata.headnum` to `s16`. The MP manifest/stage-diff lifecycle bug is also now fixed and verified CPU/build-side. The final generated-body render crash is fixed by on-demand character matrix preparation before `modelRender`; bounded `custom_body_live_render_smoke` now passes 41/41 with `MODASSET.RENDER` for `example:tri_body` and scripted exit. Custom body/head runtime equivalence can move out of the active blocker path; remaining c3844 work is live audio and the non-body parity gates.

3. **Scenario AI graph/runtime fallback closure**
   - Continue retiring remaining Scenario AI command graph/fallback debt module by module.
   - Public Scenario JSON/GLB source remains the runtime source.
   - Fallback to native/ROM behavior after extraction must fail loudly.
   - 2026-06-09: producer side is fully wired (all `scenarioSourceAiGraphExecute*` reach the loud-fail guard). Hardened `scenarioSourceAiGraphExecuteChrDoAnimation` to handle the require-node tri-state explicitly (was correct only by accident of a downstream NULL-resolve).
   - 2026-06-09 (VERIFIED correction of the gate audit): the objectives and trigger-volume consumers are ALREADY at the correct enforced-under-debug posture. `objectiveCheckGraphSource` routes every mismatch through `scenarioSourceObjectiveGraphReportRuntimeMismatch` -> `s_missionObjectiveGraphRuntimeFailure`, which `sysFatalError`s under `ASSET_MISSION` source-only enforcement; `s_levelGraphVolumeRuntimeFailure` likewise `sysFatalError`s under `ASSET_SCENARIO`. So under enforcement the OG fallback never runs (it fataled first). The OG fallback in `objectiveCheck`/`objectiveCheckRoomEntered`/`objectiveCheckThrowInRoom` only executes in NORMAL play, where it is the SAFE parity behavior; flipping it to always-fatal there would brick campaign stages whose graph node set is not yet proven complete. Do NOT make that flip without a per-stage graph-completeness proof. Genuine remaining gate-3 work is graph node-set completeness across all campaign stages so the normal-play flip is safe.
   - 2026-06-11: the first active-runtime level graph gap is closed. `lvTick` now records the active level tick through `scenarioSourceLevelGraphRecordTick("lvTick.start")`, which asserts the active public `.pdscenario::level.graph.json` global-settings source, scenario identity, and kind before logging `backend=graph.global.settings+level.tick`. The Scenario source matrix now expects that active-stage log. Verification passed `python tools\asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]` (936 assertions / 10 cases), and isolated `c3844s107` client build. Remaining `c3844-s107` work is graph node-set completeness across stages before any normal-play fallback flip.
   - 2026-06-11 follow-up: the Scenario AI extraction flattening gap is closed at the source/validator layer. `s_aiOpcodeName` now names every opcode declared in `chraicommands.h` instead of falling through to generic `"command"`, `asset_native_source_guard.py` fails if any declared opcode lacks a semantic extraction name, and strict conformance rejects `.pdscenario::ai/ailists.json` rows that still carry `"opcode_name": "command"`. A targeted Chicago retained-archive check reported 1,651 stale flattened rows before regeneration. Verification passed Python compile, `python tools\asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]` (936 assertions / 10 cases), and isolated `c3844s107b` client build.
   - 2026-06-11 regeneration follow-up: Scenario stale-output regeneration is now closed. Added `--extract-assets-only` so the normal catalog/extraction/walker/cache boot path can regenerate archives while skipping audio/network and exiting before scheduler/stage/gameplay init; raised the config registry cap to avoid the old 512-setting boot stop; and fixed Scenario-only conformance so real base body/head/weapon/model catalog IDs are known when validating a single Scenario folder. Follow-up hardening now also skips window/UI/input startup and full gameplay/window teardown for extractor-only runs, so the safe regeneration path no longer requires OpenGL and exits 0. Fresh extractor-only run exited 0, regenerated 87 `.pdscenario` archives, and durable `Build\data\ntsc-final\scenarios` validates 87/87 with 75,920 AI rows, 75,920 `scenario.ai.command` nodes, 75,920 links from `scenario.ai.lists`, 0 generic `opcode_name = "command"` rows, 0 missing/unlinked command graph rows, and 0 missing `ai/ailists.json`. Verification passed `asset_native_source_guard.py`, checked-in example conformance, Python compile, focused `[modding][pdxxx][c3844][source][static]` (985 assertions / 11 cases), isolated all-target build, extractor-only runtime run exit 0, strict Scenario conformance before cleanup on the session tree, and strict Scenario conformance on durable `Build\data\ntsc-final\scenarios`.

4. **Regenerate and validate stale extracted output**
   - Done for the current retained generated install as of 2026-06-11T16:31:33-04:00.
   - Scenario `.pdscenario` retained output is current as of 2026-06-11: 87/87 generated archives validate with 75,920 AI rows, 75,920 command graph nodes, 75,920 command links, no generic command rows, no missing/unlinked command graph rows, and no missing AI lists.
   - `.pdui` extraction-only output is now current. Root cause was extractor-only UI decode running before `catalogLoadInit()`, so texture source resolution had no reverse-index and could not find public `.pdtexture::texture.png` rows even though extraction had emitted them. `modTextureLoadRgba32Source()` now falls back to a direct enabled texture catalog scan and still fails closed if public source is missing or invalid.
   - Durable `Build\data\ntsc-final` was refreshed from the verified extractor output. Whole-tree strict conformance now passes: 8,066 root archives / 9,067 checked archives across all 26 public families, with 15 `.pdui`, 14 `.pdmission`, 3,503 `.pdtexture`, and 87 `.pdscenario` archives present.
   - CPU family verifiers now pass on the retained tree: audio 2,108 archives, animation 1,060 archives, mesh 738 archives. The clean extractor log has no searched `ASSET.SOURCE_ONLY`, `LOUDFAIL.FALLBACK`, `ASSET.FALLBACK`, fatal, exception, access-violation, or `RomProvider:filenum` signatures.
   - 2026-06-12 final retained-data regression pass is current for `Build\data\ntsc-final`: strict conformance passed 8,065 root / 9,066 checked archives across all 26 retained families; audio verifier passed 2,111 archives; mesh verifier passed 734 archives; animation verifier passed 1,062 archives.
   - Next gate: private integer bridge closure audit.

5. **Private integer bridge closure audit**
   - Audit remaining modelnums, filenums, sound slots, texture numbers, body/head slots, music slots, and weapon slots.
   - Each bridge must be catalog-owned and loud-failing, or replaced with direct catalog-ID runtime consumption.
   - Integer bridge details remain private migration debt, never public archive identity.
   - 2026-06-09 (B-907): closed a conformance gap where the JSON ref scanner accepted numeric/legacy values in catalog-ID keys (`model_catalog_id`, etc.) that the delimited scanner already rejected; the public-archive numeric-leak guard is now symmetric across JSON and delimited source. `tools/asset_archive_conformance.py --selftest` pins it.
   - 2026-06-09 (B-908): closed the write-only `g_CatalogFailure` flag. The catalog `*ByIndex`/modelnum load helpers set it on a miss but nothing consumed it (silent default substitution). Added `catalogAssertHealthy()` (consumer, fatal-under-enforcement via pure `catalogHealthShouldFatal`), wired at `lv.c` stage-load entry.
   - 2026-06-10 (B-908 follow-up DONE): the 10 in-range-unregistered body/head field accessors now route through `s_bodyFieldRecordChecked`/`s_headFieldRecordChecked` and set `g_CatalogFailure` on a genuine miss (discriminator `filenum!=0 OR catalog_id set`, so B-909 custom assets are not mis-flagged). Verified: client build PASS; `[catalog][checked]` PASS. Remaining gate-5: weapon/music slot exhaustion already loud + leak no identity (acceptable private debt); fatal-under-enforcement wants a source-gate smoke confirm.
   - 2026-06-11: public descriptor bridge cleanup is closed for the remaining generated descriptor leaks found by a zip-level audit. Checked-in examples were already clean, but retained base `.pdtexture`, `.pdlang`, and MP3 `.pdvoice` archives exposed private bridge/provenance fields in public INI descriptors (`empty_rom_slot`, `source_filenum`). Emitters now keep those out of public descriptors while preserving needed provenance under `_meta/manifest.json`; conformance rejects reintroducing them in public INI members; durable `Build\data\ntsc-final` was repaired and strict conformance now passes 8,066 root / 9,067 checked archives. Audio verification still passes with 2,111 archives including the 104 MP3 voices, proving the runtime bridge remains intact.
   - 2026-06-11 follow-up: `.pdmesh` had the same descriptor/provenance leak pattern via public `mesh.ini` `source_filenum_symbol`. Extraction now keeps that FILE_* provenance only in `_meta/manifest.json`, `loader_walker_mesh.c` continues to consume it from the manifest, conformance rejects `source_filenum_symbol` in public INI descriptors, and the retained tree was repaired. One stale `*.pdcharacter extracted` inspection folder was removed from `Build\data\ntsc-final`; strict retained conformance now passes 8,065 root / 9,066 checked archives, with zero public descriptor bridge-field hits across retained output and checked examples.
   - 2026-06-11 batched runtime bridge follow-up: MP head preview and weapon menu preview no longer submit raw legacy file numbers if catalog/provider resolution fails. They now clear the preview and log `CATALOG.MISS`, while `asset_native_source_guard.py` and static provider/source tests reject the old `catalogGetHeadFilenumByIndex(headnum)` / `weaponGetFileNum(weaponnum)` preview fallback signatures. Verification passed source guard, focused `[catalog][provider][static]` plus c3844/c3842 source tests (13,384 assertions / 59 cases), diff-check, and isolated `c3844batch` all-target build.
   - 2026-06-11: The weapon source-gate smoke confirms one catalog-owned private weapon bridge path under live render. Extraction-bootstrap fallback telemetry is now excluded from runtime fallback counts, so any future `ASSET.FALLBACK` after extraction should be treated as a real asset-chain failure to investigate.
   - 2026-06-11 s109 bridge-field cleanup: public mod templates no longer emit legacy numeric bridge fields (`headnum`, `bodynum`, `anim_id`, `sound_id`, or `load_mode = 0`). Local scanner and network-distributed mod scanner now ignore public numeric bridge inputs for custom map/arena/scenario/body/head/animation/texture/SFX/voice assets and mint catalog-owned private runtime slots instead; custom weapons preserve existing base-row overrides when applicable but new custom weapons still use private catalog-owned slots. Music remains catalog-ID source plus private virtual sequence slot where the old sequencer still needs an integer. Verified with source sweeps, focused `[modding][pdmod][static][c3809]` (1188 assertions / 16 cases), focused `[modding][pdxxx][c3844][source][static]` (985 assertions / 11 cases), private slot selectors (322 assertions / 28 cases), source guard, conformance selftest, checked-in all-family example conformance, diff-check, and isolated `c3844s109` all-target build.

6. **End-to-end modder workflow proof**
   - Prove user-created external geometry, audio, animation, materials, and gameplay assets can import, validate, package, distribute, load, render/play/animate, and be edited again.
   - Base content and mods must use the same public archive contract and runtime rules.
   - Custom geometry enters through mod tools as authored OBJ/GLTF/GLB source, gets quantized/validated into the current integer-native runtime boundary, and links required gameplay data by catalog ID: prop placement, spawn pads/zones, music, volumes, triggers, collision/nav, and behavior/mission graph references.
   - 2026-06-11: A developer-only typed archive asset browser/extractor is now available from Dev Window v2's Assets tab and `devtools/pdxxx-asset-tool.ps1`. It crawls data folders for `.pdxxx` archives, filters by archive type, lists source hints, extracts selected assets to ignored `.pdxxx-dev-extracts/`, and opens the extraction location. It preserves stored archive bytes and does not translate or regenerate mesh/material data; nested typed archives are kept as `.pdxxx` files and expanded beside them only for inspection.
   - 2026-06-11: First s110 offline all-family workflow proof is current. `tools/verify_pdxxx_modder_workflow.py` validates the checked-in `examples\modding\typed-pdxxx-basic` modder source folder, requires all 27 typed families, rejects public `.bin` and `.tsv`, counts editable source files, packages the folder as `.pdmod`, extracts it, and verifies `mod.json` plus every `.pdxxx` archive hash round-trips unchanged. Current proof: 59 archives, 31 nested archives, 229 public source entries, 45 standard source entries, and 184 semantic source entries. Runtime proof still comes from the existing representative source-gate and live smokes; this closes the all-family package/extract/editability proof slice, not the entire 100% completion audit.
   - 2026-06-11 s110 transport boundary closure: `.pdmod` folder packing, in-memory single-entry packing, and direct archive registration now reject loose public `.tsv` payloads the same way they reject authored `.bin` payloads. Descriptor source refs inside folders and archives cannot point at public TSV files, and direct `.pdmod` registration release-validates nested typed `.pdxxx` archives so invalid public payloads cannot bypass the packer. Verification passed source guard, all-family workflow verifier, all-family example conformance, focused pdmod/source tests, diff-check, and isolated `s110tsv` all-target build.
   - 2026-06-11 production packer closure: the shared Modding Hub/social `.pdmod` packer now packages the checked-in all-family example through `modpackPdmodFromFolder()` and preserves every typed archive byte-for-byte. The fix removed stale packer assumptions that `.pdarena` must expose loose geometry instead of `scenario_archive`, and that `.pdsong` must use SFX/voice-style `file_path` instead of `music_file`/`midi_file`/`track_file`. Verification passed focused production-packer coverage, focused pdmod/source selectors, source guard, all-family workflow verifier, all-family conformance, and isolated `s110prod` all-target build.
   - 2026-06-11 import/install workflow closure: Modding Hub now exposes the strict `.pdmod` pack/import path instead of the legacy `.pdpack` UI, Hub import installs through shared `modmgrInstallArchiveFile()`, and received Public Mods validate through shared `modmgrValidateArchiveFile()` before install. That shared gate checks root `mod.json`, rejects public `.bin` / `.tsv`, and release-validates nested typed `.pdxxx` archives. Verification passed source guard, all-family workflow verifier, all-family conformance, focused pdmod/Public Mods tests, diff-check, board JSON parse, decision-request check, and isolated `s110import` all-target build.
   - 2026-06-11/12 runtime installed `.pdmod` workflow proof is green. `pdxxx_modder_workflow_smoke` packages `examples\modding\typed-pdxxx-basic` as `mods/installed/example_typed_pdxxx_basic.pdmod`, enables it, and proves representative custom geometry (`example:tri_mesh`), texture/material, animation, arena/scenario/mission gameplay data, weapon, SFX, voice, and song assets load or register through source-only catalog/provider paths from the installed archive. The smoke passed 58/58 with zero `ASSET.SOURCE_ONLY`, `ASSET.FALLBACK`, `LOUDFAIL.FALLBACK`, `RomProvider:filenum`, `result=FAIL`, public `.tsv`, authored `.bin`, fatal, or access-violation signatures. Runtime fixes landed for installed `.pdmod` audio section typing, VFS-backed nested `.pdweapon` archive reads/dependency scans, relative-first weapon runtime registration, one-shot post-catalog debug probes, and smoke-only exit after catalog probes. Verification also passed the all-family workflow verifier, all-family example conformance, native-source guard, focused `[modding][pdmod][static][c3809]`, and isolated `s110flow` all-target build. This closes the representative end-to-end custom-created asset workflow proof; remaining `c3844` work is final closure audit/routing, not this workflow slice.

---

## Deferred From Active

The following cards were removed from the Active lane on 2026-06-09 and tightened
again on 2026-06-11 because they are not standalone active fronts for the current
all-asset 100% completion path. They remain on the board.

Moved to Done because their subtasks were complete:

- `c3836` - Kanban: remote phone board and card sessions
- `c3816` - Input: custom/accessibility controller class + glyph/social foundation
- `c3819` - Input: Settings Input tab rebuild and controller profile assignment
- `c133` - B-345: campaign mission starts to black screen after menu preview
- `c134` - B-346: credits particles/text and fog planes render as solid squares
- `c3829` - B-365: Infiltration robot attack exception
- `c3830` - B-366: Falcon 2 OBJ and in-game barrel stretch
- `c3831` - B-368: Match restart exception after leaving

Moved to Backlog / deferred:

- `c3845` - Connectivity: MP co-op drop-in/out and Combat Sim start (not part of current all-asset parity closure)
- `c3846` - Mod-infrastructure: Mod Studio rebuild + versioning + controller + dev-mod pipeline (belongs after source/runtime parity proof)
- `c3847` - Modding: Needler custom weapon mod (implementation verified; remaining live proof is B-801-gated under c3844)
- `c3848` - Catalog: custom-model runtime-slot allocator for embedded weapon meshes (B-911) (implementation verified; remaining live proof is B-801-gated under c3844)
- `c3849` - Catalog: 100% utilization program (Waves 1-7 shipped; keep as history unless Mike opens a new utilization wave)

- `c3840` - Weapon Graph: modularize OG behavior routines after parity
- `c3815` - B-356: Combat Sim post-match screen visible and interactive
- `c136` - B-350: airborne side-entry through overhead collision blockers
- `c083` - B-316: Combat Sim start crash in botJumpDecide
- `c3823` - Startup extraction fast-cache and centered progress modal
- `cmpbhfdir2lxo` - Full Campaign Auto-Runner with Unlocks and Social Syncing
- `c3746` - Player init architectural fixes
- `c029` - Benchmarking: speed tune + B-307 256-bot crash
- `c3807` - GPU swarm benchmark debug tab (Phase 2)
- `c3738` - Skedar surface-normal locomotion (Slices 4-5)
- `c028` - Pre-existing test failure triage
- `c031` - Benchmarking: GPU bot behavior pipeline
- `c3826` - B-361: Main Menu Esc/title X root close reopens
- `c3827` - B-362: Falcon 2 mission-start beam stretches to center

Kanban cleanup rule: do not delete deferred cards unless Mike explicitly asks
for historical deletion. Keep them in Backlog or Done with notes, and keep only
`c3844` plus the current `c3844` proof subtask in the Active lane until the
all-asset parity closure changes state. As of 2026-06-11, non-`c3844` attention
flags and stale attention timestamps have also been cleared so old starred/watch
cards do not compete with the active parity closure. The 2026-06-11 board
refresh also added explicit deferral notes to every non-`c3844` Backlog card and
recorded the rule in `tools/kanban/state.json` under `x_special_notes` and
`x_board_cleanup`: those cards are parked for routing until Mike changes
priority or the work is explicitly folded into `c3844`. Completed card
workspaces under `x_card_task_context.cards` are historical handoff notes only;
they must not route future sessions toward `c3840`, `c3834`, `c3835`, or any
other non-`c3844` lane while this parity closure remains active. Custom body/head
equivalence is now Done because the live custom-body smoke proves the runtime
bridge. `c3844-s105` is now green through live SFX/voice/music playback; the
active proof route is `c3844-s110` end-to-end modder workflow proof.

Latest board hygiene check, 2026-06-11T04:31:50-04:00: live Kanban now has 1 Active card
(`c3844`), 49 Backlog cards explicitly deferred, 0 Blocked cards, 104 Done cards,
and exactly 1 attention flag (`c3844`). Empty historical card workspaces such as
`c3820` and `c3823` are marked deferred, and the preserved `c3824` asset-decision
workspace is marked completed/historical, so none of them can be mistaken for live
handoffs.

Follow-up cleanup, 2026-06-11T04:50:04-04:00: preserved Kanban archive-decision
metadata was corrected so old "OBJ/TSV/setup source" wording now points at
OBJ/GLTF/JSON/setup source and explicitly says public TSV is not a valid final
source contract. This did not reopen `c3824`; the decision workspace remains
completed/historical and `c3844` remains the only active routing target.

Current verification, 2026-06-11T04:58:04-04:00: the live Kanban still parses
cleanly with exactly 1 Active card (`c3844`), 49 Backlog cards, 0 Blocked cards,
104 Done cards, and exactly 1 attention flag (`c3844`). All non-`c3844`
Backlog cards already carry explicit 2026-06-11 deferral notes, no non-`c3844`
card-specific workspace is marked active/not-started, and there are 0 unresolved
decision requests. No additional card moves were needed.

Strict deferral refresh, 2026-06-11T05:59:20-04:00: all 49 non-`c3844`
Backlog cards now have Kanban priority `5` (Someday/deferred) plus
`x_deferred` metadata preserving the original priority and stating that they
must not route work until Mike explicitly reopens them or folds them into
`c3844`. Done cards are marked history-only in metadata. The separate parked
thread file was also reset to manual resume behind the `c3844` parity closure,
so old date-based resume triggers cannot reintroduce Forge, AllInOne cleanup, or
friend-play work while the all-asset closure is the sole active lane.
Verified at 2026-06-11T06:19:14-04:00: board JSON parses; Active is exactly
`c3844`; Backlog is 49/49 priority-5 deferred; Done is 104/104 history-only;
the only card flag is `c3844`; parked entries are manual-resume only; and
decision requests have 0 unresolved questions. No additional card moves were
needed.

Final context-system refresh, 2026-06-11T06:33:55-04:00: the board was checked
again after the requested cleanup. It still has exactly one Active card
(`c3844`), zero Blocked cards, 49/49 non-`c3844` Backlog cards with priority-5
`x_deferred` metadata, 104/104 Done cards marked history-only, and only the
`c3844` attention flag. The context index, task ledger, active session log, and
Kanban root metadata all agree that unrelated cards are parked until Mike
explicitly reopens them or folds their proof into `c3844`.

Fresh verification, 2026-06-11T08:26:09-04:00: no additional card moves were
needed. The live board still parses with exactly one Active card (`c3844`), 49/49
Backlog cards priority-5 deferred, 104/104 Done cards history-only, zero Blocked
cards, one attention flag (`c3844`), zero unresolved decision requests, and no
non-`c3844` live card workspaces. `c3844` remains the only routing target.

Context refresh, 2026-06-11T08:47:25-04:00: no new cards were removed or moved
because the Kanban structure was already correct: `c3844` is the only Active
card, all 49 non-`c3844` Backlog cards are priority-5 deferred with
`x_deferred` metadata, all 104 Done cards are history-only, there are zero
Blocked cards, exactly one attention flag (`c3844`), zero unresolved decision
requests, and no non-`c3844` live card workspaces. The stale board/context
guidance that still pointed at the old smoke staging/log-mask regression was
updated to the then-open issue: slot-152 custom-body render reaches
`chrRender-modelRender` but render-time state shows `head=-104`, hidden
alpha-skip, no `MODASSET.RENDER` for `example:tri_body`, and a `0xC0000005`
exit. Keep unrelated cards parked unless Mike explicitly reopens them or folds
their proof into `c3844`.

Historical context/Kanban refresh, 2026-06-11T09:00:39-04:00: the board structure
still needs no card moves: `c3844` is the only Active card, all 49 non-`c3844`
Backlog cards remain priority-5 deferred, all 104 Done cards remain
history-only, there are zero Blocked cards, exactly one attention flag
(`c3844`), zero unresolved decision requests, and no non-`c3844` live
workspaces. The routing text was refreshed to the latest custom-body evidence:
`head=-104` is no longer current after widening `chrdata.headnum` to `s16`.
That smoke reached `chrRender-modelRender` with `head=152` but still
failed 30/36 with `0xC0000005`, no `MODASSET.RENDER` for `example:tri_body`, no
scripted exit, missing custom-slot scanner / `MODELDEF.SOURCE` expectation
lines, and a late `MANIFEST-SP` missing/unload warning that must be inspected
before chasing the renderer further.

Full context-system refresh, 2026-06-11T09:14:10-04:00: no new cards were
removed or moved because the board structure was already correct. Verification
now confirms exactly one Active card (`c3844`), 49/49 Backlog cards priority-5
deferred with `x_deferred` metadata, 104/104 Done cards history-only, zero
Blocked cards, one attention flag (`c3844`), zero unresolved decision requests,
and no non-`c3844` live Card Decisions workspace. Preserved non-`c3844`
workspaces now carry explicit `historical` or `deferred` status instead of blank
status fields. The active audit folder was also cleaned so only
`migration-utilization-measurement-2026-06-10.md` remains in `context/audits/`;
older audits are archived under `context/_old/audits/2026/`. Older same-day
custom-body breadcrumbs are historical; the current blocker remains the latest
slot-152 run: no `MODASSET.RENDER` or scripted exit, missing custom-slot scanner
/ `MODELDEF.SOURCE` expectation lines, late `MANIFEST-SP` missing/unload warning,
and `0xC0000005` exit after reaching `chrRender-modelRender`.

Requested context-system refresh, 2026-06-11T09:22:47-04:00: the board was
verified again for Mike's cleanup request. No additional card moves were needed:
Active is exactly `c3844`, Backlog is 49/49 priority-5 deferred with
`x_deferred` metadata, Blocked is 0, Done is 104/104 history-only, the only
attention flag is `c3844`, unresolved decision requests are 0, parked threads
are manual-resume only, `context/audits/` contains only the current c3849
measurement audit, and there are no live non-`c3844` Card Decisions workspaces.
The routing rule remains strict: unrelated cards stay parked until Mike
explicitly reopens them or folds their proof into `c3844`.

Latest requested context-system refresh, 2026-06-11T09:32:04-04:00: the live
board was verified through the actual `column` field. No card deletion or
additional moves were needed. Active is exactly `c3844`; Backlog is 49/49
priority-5 deferred with `x_deferred` metadata; Blocked is 0; Done is 104/104
history-only; the only attention flag is `c3844`; unresolved decision requests
remain 0; parked threads are manual-resume only; and `context/audits/` still
contains only the current c3849 measurement audit. This is the current routing
source of truth: all non-`c3844` cards are parked unless Mike explicitly reopens
them or folds their proof into `c3844`.

Final context/Kanban verification, 2026-06-11T09:36:59-04:00: the board was
rechecked against the actual evaluator field for decision requests. The only
apparent unresolved question was the historical `c121` smoke-test question on a
Done tooling card; it already had an answer but lacked the evaluator's
`answered_at` field. That field is now populated, and the verified routing state
is: Active exactly `c3844`, Backlog 49/49 priority-5 deferred with
`x_deferred`, Blocked 0, Done 104/104 history-only, only `c3844` flagged,
unresolved decision requests 0, parked threads manual-resume only, and no live
non-`c3844` Card Decisions workspace. No additional cards were deleted or moved.

Current context/Kanban verification, 2026-06-11T09:44:04-04:00: this request was
verified directly against `tools/kanban/state.json` and the decision-request
evaluator. Active is exactly `c3844`; Backlog is 49/49 priority-5 deferred with
`x_deferred`; Blocked is 0; Done is 104/104 history-only; the only card flag is
`c3844`; unresolved decision requests are 0; parked threads remain manual-resume
only; `context/audits/` contains only the current c3849 measurement audit; and
there are no live non-`c3844` Card Decisions workspaces. No additional cards
needed deletion or movement.

Canonical context note, 2026-06-11T09:51:54-04:00: the repository `context/`
tree is the source of truth for future sessions. Parent-level briefing files are
treated as convenience mirrors only and must not override `context/` unless they
are explicitly synced from this directory. The live board was rechecked with the
same result: exactly one Active card (`c3844`), all 49 Backlog cards deferred at
priority 5, all 104 Done cards history-only, zero Blocked cards, only `c3844`
flagged, zero unresolved decision requests, and only the current c3849 audit in
`context/audits/`.

Latest context/Kanban refresh, 2026-06-11T10:32:19-04:00: no card moves or
deletions were needed. Direct JSON and decision-evaluator checks still show
exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred with
`x_deferred`, zero Blocked cards, 104/104 Done cards history-only, only the
`c3844` attention flag, zero unresolved decision requests, no live
non-`c3844` Card Decisions workspaces, and only the current c3849 audit in
`context/audits/`. The active handoff was refreshed to the latest verified
state at that time: the MP manifest/stage-diff lifecycle blocker was patched and
pinned by static/build checks, while the bounded live custom-body smoke remained
red later in the render path with `0xC0000005`, no scripted exit, and no
`MODASSET.RENDER` for `example:tri_body`. This note is superseded by the
2026-06-11T11:15:13-04:00 refresh below.

Current context/Kanban refresh, 2026-06-11T11:25:25-04:00: the board still
needs no card deletion or movement. Direct JSON and decision-evaluator checks
show exactly one Active card (`c3844`), 49/49 non-`c3844` Backlog cards
priority-5 deferred with `x_deferred`, zero Blocked cards, 104/104 Done cards
history-only, only the `c3844` attention flag, zero unresolved decision
requests, parked threads manual-resume only, no live non-`c3844` Card Decisions
workspace, and only the current c3849 measurement audit in `context/audits/`.
This refresh supersedes the 10:32 custom-body blocker note and re-verifies Mike's
cleanup request: custom body/head live render proof is green, `c3844-s106` is
Done, and the active `c3844-s105` proof is live SFX/voice/music playback with
scoped logging. No additional card moves were needed because all irrelevant
cards were already deferred or marked history-only.

Latest strict board/context verification, 2026-06-11T11:34:53-04:00: the actual
Kanban JSON still has exactly one Active card (`c3844`), 49/49 Backlog cards
priority-5 deferred with `x_deferred`, zero Blocked cards, 104/104 Done cards
history-only, and exactly one attention flag (`c3844`). The old `c121`
decision-request smoke-test question already had an answer and resolution
timestamp, but now also has explicit `resolved: true` so direct JSON checks and
the board cleanup metadata both report zero unresolved decision requests. No
additional card moves, deletes, or reopenings were needed.

Latest requested cleanup verification, 2026-06-11T11:50:04-04:00: the board was
checked again from the list-based Kanban card records. No structural cleanup was
left to perform: Active is exactly `c3844`, Backlog is 49/49 priority-5
deferred with `x_deferred`, Blocked is 0, Done is 104/104 history-only, the only
card flag is `c3844`, unresolved decision requests are 0, parked threads are
manual-resume only, and no non-`c3844` Card Decisions workspace is live. The
context index, task ledger, active session log, and Kanban root metadata now
carry this latest verification stamp.

Current requested cleanup verification, 2026-06-11T12:02:04-04:00: the board
was rechecked directly from `tools/kanban/state.json`,
`tools/kanban/parked.json`, the decision-request evaluator, and the active
audit folder. No additional cards should be deleted, moved, or reopened: Active
is exactly `c3844`, Backlog is 49/49 priority-5 deferred with `x_deferred`,
Blocked is 0, Done is 104/104 history-only, the only card flag is `c3844`,
unresolved decision requests are 0, parked threads remain manual-resume only,
`context/audits/` contains only the current c3849 measurement audit, and no
non-`c3844` Card Decisions workspace is live. This stamp is superseded by the
2026-06-11T12:27:12-04:00 refresh below because `c3844-s105` is now done.

Follow-up requested cleanup verification, 2026-06-11T12:27:12-04:00: no
additional Kanban card deletion or movement is warranted. Direct board checks
show exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred
with `x_deferred`, zero Blocked cards, 104/104 Done cards history-only, and the
only attention flag on `c3844`. Decision-request checks report zero unresolved
questions, parked threads remain manual-resume only, and no non-`c3844` Card
Decisions workspace is live. `c3844-s105` is complete through live
SFX/voice/music proof; the current active proof subtask is `c3844-s107`
Scenario AI graph/runtime fallback closure.

Latest requested cleanup verification, 2026-06-11T12:42:13-04:00: the context
system and board were refreshed again after Mike asked to update everything and
remove/defer irrelevant cards. No deletion or movement was needed: Active is
exactly `c3844`; Backlog is 49/49 priority-5 deferred with `x_deferred`;
Blocked is 0; Done is 104/104 history-only; the only active/routing marker is
`c3844`; unresolved decision requests are 0; parked threads are manual-resume
only; and no non-`c3844` Card Decisions workspace is live. The parked AllInOne
thread still points at the archived audit path instead of the active audit
folder. This route note is superseded by the 2026-06-11T13:19:35-04:00 refresh
below.

Current cleanup verification, 2026-06-11T13:19:35-04:00: direct board, parked
thread, decision-request, and audit-folder checks still show no irrelevant card
left in Active. Active is exactly `c3844`; Backlog is 49/49 priority-5 deferred
with `x_deferred`; Blocked is 0; Done is 104/104 history-only; the only card
star/routing marker is `c3844`; unresolved decision requests are 0; parked
threads are manual-resume only; no non-`c3844` Card Decisions workspace is live;
and `context/audits/` contains only the current c3849 measurement audit. No
additional cards should be deleted or moved. The routing handoff is now current
to the latest `c3844-s107` state: level graph tick proof is fixed, declared
Scenario AI opcodes no longer extract as generic `"command"`, regenerated
`.pdscenario` archives validate 87/87 with 75,920 AI rows and no generic command
rows, and graph node-set completeness remains the next proof before any
normal-play fallback flip.

Current requested cleanup refresh, 2026-06-11T14:00:45-04:00: direct checks of
`tools/kanban/state.json`, `tools/kanban/parked.json`,
`tools/kanban_evaluator.py list-decision-requests`, and `context/audits/`
confirm the board still needs no deletion or movement. Active is exactly
`c3844`; Backlog is 49/49 priority-5 deferred with `x_deferred`; Blocked is 0;
Done is 104/104 history-only; the only active/routing marker is `c3844`;
unresolved decision requests are 0; parked threads remain manual-resume only;
there are no live non-`c3844` Card Decisions workspaces; and the active audit
folder contains only the current c3849 measurement audit. The strict routing
rule remains: unrelated cards stay parked until Mike explicitly reopens them or
folds their proof into `c3844`. This handoff was superseded later the same day
by the command graph completeness proof recorded below.

Current c3844 handoff update, 2026-06-11T14:32:57-04:00: Scenario graph
node-set completeness is now proven across regenerated retained output. The
durable `Build\data\ntsc-final\scenarios` set validates 87/87 with 75,920 AI
rows, 75,920 `scenario.ai.command` nodes, 75,920 links from
`scenario.ai.lists`, 0 generic command rows, and 0 missing/unlinked command
graph rows. The board cleanup state is unchanged: `c3844` remains the only
Active card, all non-`c3844` backlog cards remain explicitly deferred, and
parked threads remain manual-resume only. The next active work is `c3844-s107`
Scenario normal-play fallback posture, followed by whole-tree retained-output
currentness, private integer bridge audit, and end-to-end modder workflow proof.

Latest context/Kanban cleanup refresh, 2026-06-11T15:37:36-04:00: direct checks
of `tools/kanban/state.json`, `tools/kanban/parked.json`, the decision-request
evaluator, and `context/audits/` confirmed no additional card deletion or
movement is warranted. Active is exactly `c3844`; Backlog is 49/49 priority-5
deferred with `x_deferred`; Blocked is 0; Done is 104/104 history-only; the
only active/routing marker is `c3844`; unresolved decision requests are 0;
parked threads remain manual-resume only; and no non-`c3844` Card Decisions
workspace is live. `c3844-s107` is now done, so the current handoff is
`c3844-s108`: whole-tree retained-output currentness. Current retained-output
finding: Scenario archives are current and audio/animation/mesh verifier passes
are green, but whole-tree strict conformance still finds stale `.pdmission`
`scenario_graph_cache` metadata and missing `.pdui` output in the fresh
extractor tree.

Current context/Kanban cleanup refresh, 2026-06-11T16:11:45-04:00: direct board
and parked-thread checks still show no irrelevant card left to remove or move.
Active is exactly `c3844`; Backlog is 49/49 priority-5 deferred with
`x_deferred`; Blocked is 0; Done is 104/104 history-only; unresolved decision
requests are 0; parked threads are manual-resume only; `context/audits/` still
contains only the current c3849 measurement audit; and no non-`c3844` Card
Decisions workspace is live. This handoff was superseded by the
2026-06-11T16:31:33-04:00 retained-output closure below.

Retained-output closure refresh, 2026-06-11T16:31:33-04:00: `c3844-s108` is
now done for the current retained generated tree. Fresh extractor-only output
validated across all families, `Build\data\ntsc-final` was refreshed from that
verified output, whole-tree conformance passes 8,066 root archives / 9,067
checked archives across all 26 families, and audio/animation/mesh CPU verifiers
pass on the retained tree. The fresh extractor log scan found no source-only,
fallback, fatal, exception, access-violation, or `RomProvider:filenum`
signatures. Keep `c3844` as the only Active card; move the active proof route to
`c3844-s110` end-to-end modder workflow proof.

Public Mods runtime proof refresh, 2026-06-12T02:08Z: the received `.pdmod`
path now validates through `modmgrValidateArchiveFile()`, installs through the
shared strict `modmgrInstallArchiveFile()` contract, queues/accepts the received
enable prompt, and loads the installed archive through the same catalog/provider
runtime path as local content. `public_mods_pdmod_install_smoke` passed 34/34
against the isolated `s110pub` client, proving the inbox archive installs into
`mods/installed`, scans 4 component descriptors, and loads `tri_head`,
`tri_arena`, and `fixture:character_skeletal` from public typed archive source.
Focused static coverage also pins negative proof for public `.bin`, public
`.tsv`, and invalid nested typed archives before install. Verification passed
focused Public Mods tests, `asset_native_source_guard.py`, diff-check, scoped
smoke logging, isolated `s110pub` all-target build, and process cleanup with no
lingering `PerfectDark`, `PerfectDarkServer`, or `WerFault` processes. Keep
`c3844-s110` active for the final 100% closure audit unless Mike folds another
proof into this route.

Final all-family regression refresh, 2026-06-17T21:55Z: c3844 remains closed
after a current-tree re-sweep. The refresh found and fixed two blockers owned by
the proof route: custom `.pdweapon` held-model lookup for first-person custom
weapon IDs, and an under-timed weapon archive source smoke. Current proof:
native-source guard PASS; modder workflow verifier PASS with 27 families / 59
archives / 31 nested archives / 229 public sources; conformance PASS for 8,093
root / 9,125 checked archives including retained `Build\data\ntsc-final`;
audio/mesh/animation CPU verifiers PASS with 2,111 / 734 / 1,062 archives;
focused Public Mods / `.pdmod` / c3844 / c3842 / weapon-graph tests PASS with
10,782 assertions / 77 cases; Needler source-render smoke PASS 40/40 in
`.claude\smoke-verify-runs\results-20260617T214449Z.json`; full c3844 smoke
matrix PASS 9/9 and 421/421 in
`.claude\smoke-verify-runs\results-20260617T215555Z.json`; isolated
`final3844fix` all-target build PASS; decision-request check reports 1 request,
0 unresolved questions, 1 answered question, 0 needing interpretation, and 0
needing confirmation; board JSON parse PASS; final cleanup found no lingering
`PerfectDark`, `PerfectDarkServer`, or `WerFault` processes and removed the
session build directory.

Needler screenshot proof follow-up, 2026-06-17T22:49Z: B-933 is fixed and
verified. Current focused proof is native-source guard PASS; Needler conformance
PASS with 1 root / 12 checked archives across `.pdeffect`, `.pdmaterial`,
`.pdmesh`, `.pdprojectile`, `.pdtexture`, and `.pdweapon`; isolated
`needlervis` all-target build PASS; and
`needler_graph_runtime_visual_smoke` PASS 40/40 in
`.claude\smoke-verify-runs\results-20260617T224749Z.json` with exit 0 and no
access-violation signatures. The two captured BMP screenshots are still not
readable enough to close a visual-inspection requirement.

Live visual correctness audit refresh, 2026-06-12T03:00Z: the c3844 rendered
asset audit is green for the current tree under scoped logging. Live smokes
passed for Scenario/level geometry and textures (`scenario_pads_source_gate_smoke`
154/154), weapons/hands (`weapon_match_source_gate_smoke` 45/45), custom
characters/body/head (`custom_body_live_render_smoke` 41/41), props/projectiles/
entities/vehicles/materials/skins/effects/UI/HUD/fonts/lang/theme plus audio
and animation coverage (`all_family_source_gate_smoke` 36/36,
`archive_walker_source_gate_smoke` 19/19, `audio_live_playback_source_smoke`
21/21, `base_animation_source_probe_smoke` 19/19). Real failures found during
the audit were fixed: `.pdui` extraction now emits `.pdtexture` first, generated
cache writes create parent directories and use shorter private cache filenames,
base source-only catalog probes wait until base emit/walk, and public `.pdsong`
sequences whose loops close at track end compile without legacy music fallback.
Final verification passed focused c3844/c3809/static tests, native-source guard,
all-family conformance across 8,093 root / 9,125 checked archives, audio/mesh/
animation source verifiers, isolated `c3844vis` all-target build, and process
cleanup. The smoke harness now has a screenshot artifact channel for targeted
smokes, but the current Needler captures are not yet readable enough to close a
human visual-inspection requirement.

---

## Required Verification Pattern

For code changes under `c3844`:

1. Run `python tools\asset_native_source_guard.py`.
2. Run focused tests for the touched asset family or runtime surface.
3. Run relevant conformance or smoke/matrix checks.
4. Run an isolated build with `.\devtools\build-session.ps1 -Session <id> -Target all`.
5. Remove the isolated build directory with `.\devtools\build-session.ps1 -Remove -Session <id>`.
6. Update this file, the relevant pillar doc, `context/session-log.md`, and `UNRELEASED.md` if behavior changed.

For context/Kanban-only changes, JSON parse plus targeted diff/consistency checks are sufficient.

---

## Current Guardrails

- Public typed archives are the game-facing source.
- Base content and mods use the same contract.
- No public TSV files.
- No public `.bin` or native dump payloads as modder-facing source.
- No numeric asset identity in public archive fields, manifests, saves, config, wire, UI, or mod tools.
- Runtime ROM/RomProvider fallback after extraction is an asset-chain failure.
- Private renderer/GPU/audio/collision/animation/graph products are source-hashed cache only.
- Integer-only runtime limitations are private migration debt and must not leak into authored source.
- Replacing the original renderer is feasible later and should make rendering from strict public archives more direct, but it does not remove the need to preserve gameplay, collision, animation, material, room/portal, trigger, and catalog-link data in the source archive.
