# Session Log (Active)

## 2026-07-04 - Backlog wave 5: reconciliation under-count CORRECTED

The "0 OPEN-AI-TRACTABLE" from the backlog reconciliation sweep was an UNDER-COUNT:
it blanket-classified cards "deferred pending c3844" as Mike-gated, but c3844 is
closed, so several are now doable. Verified + completed FOUR (c074, c064, c069, c070) and found one moot:
- **c074** (17e50fe2): behavioural test for the REAL `savemigrate.c` chain
  framework -- 6 cases (ascending-order chain with out-of-order registration, type
  isolation, no-op, fail-closed on missing step, downgrade refusal). `[save]` 15
  cases / 63 assertions green. Complements the c141 loud-fail fix.
- **c064** (fa9cde55): removed the dead legacy manifest serializer API
  (`modmgrGetManifestHash` / `modmgrWriteManifest` / `modmgrReadManifest`, ~3.4KB,
  zero live callers, superseded by `match_manifest_t`). Client links clean.
- **c069** (70a28384): factored the SP-9 truncation guard in build-headless.ps1 into
  a pure, unit-tested helper (`Get-Sp9FlagsFromNumstat`) + 4 `-SelfTest` checks
  (sp9Ok=True). Turned untested critical guard logic into tested code.
- **c070** (7bc3978d): extracted the worktree-redirect guard into the pure,
  unit-tested `Resolve-RealProjectRoot` helper (wtOk=True). The "3-script
  duplication" premise is mostly stale -- only deprecated dev-window-v2/_dev-window
  still inline it.
- **c066** MOOT: the `.pdbase` loader was RETIRED 2026-05-03 (catalog universality
  Step 5); "add loader coverage" is obsolete -- retirement is guarded by
  `test_pdbase_retired_audit.cpp`. Closed as superseded.

c069 and c070 turned out CLEANER than my "too risky" first read: single-file pure
helper extractions with self-test coverage, build-verified. Remaining tractable
cards: c040 (dev-window worktree-aware cleanup); c065 (pure-mirror compile-boundary
-- a multi-file *production* netmanifest.c refactor, genuinely risky); c068
(pd_headers watchdog investigation, open-ended); c041 (audit grooming, vague).
c065/c068/c041 are the higher-risk / open-ended tail best suited to a focused fresh
session. Lesson: don't trust a reconciliation agent's "gated" label wholesale, AND
don't over-fear a refactor before scoping it -- the c3844-deferral reason was stale
and c069/c070 were clean.

Continued through the rest of the tail: **c068** stale-done (the pd_headers watchdog
STALL was already fixed by the progress-aware watchdog 887df5aa); **c041** archived
the two aged-out weekly super-audits (06-12, 06-19) to `_old/audits/2026/` per
retention.md, keeping the within-window + tasks.md/design-cited ones; **c040** added
a worktree-count label to the dev-window-v3 header (read-only, `git worktree list`
-backed, parse + count-logic verified). Then **c065** too, once scoped properly --
my "no safe slice" read was wrong. The card's STATED problem is "drift is not enforced
by CI", so I added `tests/test_pure_mirror_sync.cpp`: a drift guard asserting every
@SYNC production-file reference in the mirrors is live (20 refs / 17 mirrors, `[sync]`
green, runs in the c042 CI). That closes the enforcement gap; the full mirror-
ELIMINATION refactor (globals-free shared TUs) is now an optional maintainability
optimization, not a correctness gap.

**18 cards resolved this push** (c139-c144, c073, c143, c056, c042, c074, c064, c069,
c070, c068, c041, c040, c065) + c066 moot + systemic audits SP-1/2/3/6/8 + 5 flagged
bugs. Final full suite: **829 cases / 46,551 assertions green**. EVERY AI-tractable
backlog card is now resolved; the entire remainder is genuinely Mike-gated -- ~19
stale-done cards (need a playtest to formally close), the NAT tier + c058 (network
verification + the protected group_session.c), the 5 Wave-2 bugs (playtest/ROM), and
the Wave-6 designs (Forge/Studio excluded per Mike; the rest playtest/design-decision
gated). Lesson reinforced: scope every card before declaring it too risky -- c065,
c069, c070, c040 all turned out to have clean, verifiable slices.

## 2026-07-04 - Backlog push: partial (goal boundary, superseded by wave 5 above)

Goal was "complete all listed inventory items except Forge + PD Studio." Closed
every item AI-completable + verifiable without Mike / network / playtest -- 16
commits across waves 1-4 (per-wave detail below). Reached the genuine boundary
where each remaining item needs Mike specifically.

DONE (committed, built, tested green -- 822 cases / 46,494 assertions):
- Wave 1: c139-c142 (connect-code dedupe + exhaustive test, save loud-fail, net
  wire-ceiling assert, pdeffect N64 stride mirror)
- Wave 2: c073 (B-312 verified-gone + shutdown hardening), c143 (B-913 non-bug doc)
- Wave 3: c144 (SP-8 x7 null-chr crash guards in bot AI, SP-3 hardening, SP-1/2/3/6
  audits cleared)
- Wave 4: c056 (Opus auto-detect verified already-done), c042 (fork CI workflow)

REMAINING -- each blocked on something only Mike can provide:
- **c058 kbps + MP relay Gap A**: the kbps placeholder + relay wiring live in
  `port/src/net/group_session.c`, Mike's IN-FLIGHT uncommitted file -- must not be
  edited/staged. Do these WITH his relay branch.
- **c054/c055/c057/c059/c060** (ICE / STUN reflexive / UPnP probe ports / TURN
  fallback / hole-punch unification): NAT-traversal; verification needs a real
  second endpoint / NAT (single-test-machine constraint).
- **5 Wave-2 bugs**: B-249 (4-bot no-fire playtest -> `B-249.DIAG` pinpoints it),
  B-919 (OG-ROM bolt-vs-knife check), B-855 (authored-weapon playtest), B-772
  (.pdanim re-extract + animation playtest), B-769 (architectural mesh-render
  parity, needs on-screen ROM comparison).
- **Wave 5** (physics slope/ceiling, combat-sim, benchmarking, flaky
  auto_campaign): runtime/playtest-gated -- e.g. stabilising the flaky smoke needs
  ~20 game runs to confirm a pass-rate fix.
- **Wave 6** (GPU-swarm AI, Skedar slices 4-5, rigging-aware body/head, Catch2
  cohort): large designs, most playtest-gated; interest-management needs Mike's
  go/no-go per its design doc.

Deliberately did NOT pad the count with unverifiable NAT code, edits to Mike's
protected file, or fabricated playtest results. Next session: pair with Mike on the
relay branch + a playtest pass.

**Reconciliation sweep (systematic, evidence-backed) confirms the boundary.** A full
pass over all 45 backlog cards vs bugs.md + code found **0 OPEN-AI-TRACTABLE cards** --
nothing remains that an AI can complete AND verify without Mike. Breakdown:
- **19 STALE-DONE** (code complete + build/test-verified, mostly May 2026, awaiting
  Mike's PLAYTEST gate to formally close): c029, c031, c032, c033, c071, c080, c083
  (B-316), c136 (B-350), c3738, c3807, c3808, c3815 (B-356), c3823, c3826 (B-361),
  c3827 (B-362), c3840, c3845 (B-910), cmpbhfdir2lxo. These are the highest-value
  "close on next playtest" set.
- **26 MIKE-GATED** (playtest / ROM check / real-network / design decision / edits to
  the protected group_session.c): the c054-c060 net tier, c028/c035/c037/c039/c040/
  c041/c064-c070/c074/c081/c138/c3746/c3846 (deferred pending the now-closed c3844),
  c3847 (gameplay authoring), c3848 (B-801 live render), cmpbhaqofaqj1.
Did NOT mass-close the stale-done cards: FIXED-PENDING-PLAYTEST is Mike's gate, and
one classification (c054 "ICE shipped") conflicts with the pillar sweep ("ICE not
wired") -- verify before closing. Mutating the board on unverified agent output would
misrepresent state.

## 2026-07-04 - Backlog wave 4: connectivity (goal-driven, IN PROGRESS)

- **c056** (Opus auto-detect) RESOLVED-VERIFIED (313d4fb1): CMake already
  auto-detects libopus via `pkg_check_modules` (CMakeLists.txt:376-394) and sets
  `HAVE_OPUS`; voice.c gates every path on it. The pillar's "build script doesn't
  auto-detect" note was stale (2026-04-27) -- corrected. `find_package(Opus)` is an
  optional pkg-config-less refinement.
- **Remaining c054/c055/c057/c058/c059/c060** split two ways: **c058** (kbps
  measurement) and the **MP relay Gap A forwarder** (p2p_turn.c, has a loopback-sim
  path) are writeable + statically/loopback-verifiable in a focused session;
  **c054** (ICE candidate exchange), **c055** (STUN reflexive publish), **c057**
  (UPnP probe-port mapping), **c059** (TURN public fallback), **c060** (hole-punch /
  tier-machine unification) are genuine NAT-traversal wiring whose VERIFICATION
  needs real network/NAT or a second endpoint -- limited by the SINGLE-test-machine
  constraint (project_mp_track). These are the same "code-can-land-but-proof-needs-
  Mike/network" gating class as the Wave-2 bugs. Recommend a dedicated connectivity
  session with network/VPS test access rather than landing unverifiable NAT code.

## 2026-07-04 - Backlog wave 3: systemic crash-audit SP-1/2/3/6/8 (goal-driven)

Cleared the systemic array-index / null-deref audit debt (crash-class patterns):
- **SP-8** (null prop->chr): 7 unguarded `chrGetTargetProp(chr)->chr` / `target->chr`
  derefs in bot AI targeting FIXED + adversarially verified -- botinv.c (chrsinsight
  x2 @540/557, chrdistances @890, crossbow blur @589, tranq blur @603) + bot.c
  `botGetTargetsWeaponNum` @1276. A bot's target prop can be a PROPTYPE_PLAYER whose
  chr is NULL during MP load / late-join / cleanup; `botGetWeaponNum(NULL)` crashed
  at `chr->aibot`. `chrGetTargetProp` is non-NULL for target != -1, so only `->chr`
  needed guarding; guards are behaviour-neutral when chr is set. (c144, 492dd809)
- **SP-3**: jump-height read (bondwalk.c) hardened `MAX_PLAYERS` -> `MAX_LOCAL_PLAYERS`.
- **SP-1/2/3/6 remaining-audit CLEARED** (stale line refs already fixed): player.c:5094
  is a per-player action map (not a MAX_PLAYERS index); no live `% MAX_PLAYERS` alias;
  player.c/mplayer.c extcfg already MAX_LOCAL-bounded; mplayer/* player loops all
  null-guarded (mpspawn_orchestrate.c, scenarios.c) or g_MpNumChrs-bounded.

Client build green throughout; systemic-bugs.md updated with every clearance. Next:
Wave 4 (connectivity wiring c054-c060: ICE / STUN / TURN / UPnP / Opus / kbps /
hole-punch unification). NOTE the SINGLE-test-machine constraint limits live NAT
verification -- I'll do the code + static/unit verification and flag runtime checks
for Mike.

## 2026-07-04 - Backlog wave 2: open-bug triage (goal-driven)

Triaged the seven "genuinely open" bugs from the inventory (scoped by an Explore
pass over bugs.md + source). Two were AI-completable and are now fixed + verified
+ committed; the other five are genuinely Mike-gated (playtest / ROM check /
gameplay authoring) and are flagged in bugs.md:

- **B-312** RESOLVED-VERIFIED (c073, 94a821da): the cross-test `[inputlayer]`
  SIGSEGV no longer reproduces (full pd-tests 822 cases / 46494 assertions +
  `[inputlayer]` 24 cases both green). Added defense-in-depth: `inputLayerShutdown`
  now memsets `s_Stack` symmetric with `inputLayerInit` (mirrored into the
  `inputlayer_pure.c` @SYNC copy) so the stale-def-pointer class cannot resurface.
- **B-913** RESOLVED-DOCUMENTED (c143, 1bedb5e8): not a functional bug -- the
  mod.json parser correctly skips the forward-looking `contents`/`assets` keys.
  Documented inline in `modmgr.c` + the modding pillar doc so nobody "fixes" the
  non-bug; wiring point marked for the future mod-manager subtype UI.

Mike-gated (code cannot be safely completed by an AI alone; details in bugs.md):
- **B-249** (kill attribution -> player 0): re-audited; `mpstatsRecordDeath` is
  correct, the defect is an upstream `aplayernum` default a static trace can't
  locate. The `B-249.DIAG` warning is in place -- needs a 4-bot no-fire playtest to
  pinpoint. A blind fix risks regressing real player-0 kills.
- **B-919** (propobj.c duplicate `WEAPON_COMBATKNIFE` operand): one-liner but
  explicitly "do not fix blind" -- needs OG-ROM verification of bolt-vs-knife
  hit-sound behaviour.
- **B-855** (custom .pdweapon runtime): code already landed + unit-tested; only
  gameplay validation with an authored weapon remains.
- **B-772** (.pdanim v4 GLTF): code applied; needs base-tree re-extraction + an
  animation playtest.
- **B-769** (source-only .pdmesh render parity): architectural, multi-file, needs
  on-screen comparison to native ROM geometry -- effectively its own project.

Wave 2's AI-completable code is DONE. Next: Wave 3 (systemic array-index /
null-check audits SP-1/2/3/6/8), then Wave 4 (connectivity wiring c054-c060).

## 2026-07-04 - Backlog wave 1: audit-finding correctness fixes (goal-driven)

Started a multi-wave push to clear the outstanding inventory (goal: complete all
listed incomplete items EXCEPT Forge + PD Studio). Wave 1 resolved the four
actionable findings from the 2026-07-03 Super Audit, each a focused card-anchored
commit, built (client + tests SUCCESS) and tested:

- **c139 / CONNECT-CODE-DUPLICATES** (e351d1ac): removed all 14 duplicate words
  across the `connectcode.c` slot tables (audit said 13; actual 14 -- "the
  colosseum" appeared 3x). The decoder maps a repeated word to its lowest index,
  so higher-index dupes never round-tripped. Added `_Static_assert` size guards
  plus an EXHAUSTIVE test round-tripping all 256 values in every byte position
  (IP + port paths); the old sampled test never landed on the dup indices.
  `[connectcode]` 5263 assertions green.
- **c141 / SAVE-MIGRATE-NOT-WIRED** (c0393a81): `saveCheckFileVersion` now
  loud-fails older-format saves (SAVE_VERSION=2, no v1->v2 migration registered)
  instead of silently loading them under the v2 string-ID loader and dropping
  character/scenario identity. File is preserved for later migration. Stale
  "SAVE_VERSION = 1" comments in `savemigrate.c` corrected. `[save]` 45 green.
  STILL OPEN follow-up: write + register the real v1->v2 data transform for
  upgrade-on-load (needs the old integer-ID schema).
- **c140 / NET-WIRE-CLIENTID-CEILING** (3a95a257): `_Static_assert(NET_MAX_CLIENTS
  < NET_NULL_CLIENT)` in `net.h` so a future bump past the 254 u8-wire ceiling
  fails the build.
- **c142 / PDEFFECT-N64-STRIDE-LATENT** (2590a36c): added `struct n64_padeffectobj`
  and strided the `OBJTYPE_PADEFFECT` setup-segment read on it (every other type
  already uses an n64_ mirror). Latent-safe today, robust to future PC-struct
  growth.

Cards c139-c142 created and moved to done. Next: Wave 2 (genuinely-open bugs
B-249/B-769/B-772/B-855/B-913/B-919/B-312), then systemic audits SP-1/2/3/6/8,
then connectivity wiring c054-c060.

## 2026-07-03 - Weekly Super Audit (scheduled) on dev at d3cc67ac

Scheduled weekly Super Audit landed. Report:
[audits/2026-07-03-full.md](audits/2026-07-03-full.md). Delta window
`2e86ad99..d3cc67ac` (9 commits, 35 files, ~2k insertions) is small and
remedial — dominated by B-945/B-946 asset-decode + boot re-extract
loop, c068 progress-aware watchdog, c3818 Dev Window v3.

Pillar integrity is BETTER than at 06-26: Catalog SOT strengthened
(`catalogDebugRomModeldefHandle` closes the last direct
`romProviderHandle()` call from game code), Grid consent-before-
dispatch fixed in `social_share.c`, mod-native parity intact, Phase
2.5 discipline held (GASBOTTLE/SAFE stride fix is exemplary).

**Two elevated findings** worth acting on:
- **CONNECT-CODE-DUPLICATES** (High, Confirmed): the four connect-code
  word tables in `port/src/connectcode.c` contain 13 duplicate entries
  across `s_Adjectives/Nouns/Actions/Places`. The decoder returns the
  first match on ties, so ~4.4% of random IPv4 addresses cannot
  round-trip through connect codes — the sole join mechanism per
  `constraints.md:62`. Silent misroute is a privacy concern too.
  Recommend: replace duplicates + add uniqueness `_Static_assert`.
- **SAVE-MIGRATE-NOT-WIRED** (High, Confirmed): `SAVE_VERSION=2` at
  `port/include/savefile.h:41`, `saveMigrateFile()` defined at
  `port/src/savemigrate.c:145` but never called from anywhere.
  `port/src/savefile.c:230-244` `saveCheckFileVersion` only logs
  `LOG_NOTE` for `fileVersion < SAVE_VERSION`. v1 saves silently
  consumed as v2 → character/scenario identity lost.
  Recommend: wire the migration hook OR switch to loud-fail.

**New scaling / Phase 2.5 findings** (both Medium):
- **NET-WIRE-CLIENTID-CEILING**: `netclient.id` widened to u32
  in-memory but still `netbufWriteU8` on wire. Effective 254-client
  ceiling; no `_Static_assert`. Recommend
  `_Static_assert(NET_MAX_CLIENTS < NET_NULL_CLIENT)` in `net.h:224`.
- **PDEFFECT-N64-STRIDE-LATENT**: `OBJTYPE_PADEFFECT` in
  `filesetup.c:97` returns PC-struct `sizeof` with no
  `n64_padeffectobj` sibling. Safe today by structural coincidence;
  any future PC edit adds a pointer → silent corruption. Recommend
  adding `n64_padeffectobj` mirror for symmetry.

Carry-over status: **2 FIXED** (Grid consent, Catalog SOT last-caller),
**1 PARTIAL** (MODASSET overflow guard), **1 ELEVATED** (connect-code
duplicates confirmed), **12 UNCHANGED**. SMOKE-COMPILED-IN, mod
distribution signing, save migration, STUN entropy, per-IP bans all
remain open.

**Scorecard**: Design 7.5, Code 7.5, Security 6.0, Arch Discipline
**8.5 (new high)**. Total budget to remediate Critical/High:
~2.5–4.5 weeks; new-in-delta findings ~3–5 days.

## 2026-06-30 - B-346 credits solid squares visually fixed

Closed the reopened credits solid-block layer with a frame-start Fast3D/OpenGL
state-cache fix. Diagnostics proved the CI4 glyph path was decoding correctly:
the credits font palette was IA16, `use_alpha=1`, palette alpha entries had the
expected transparent/opaque values, and uploaded glyph rows had shaped alpha
masks. The visible white/cyan rectangles were stale blend state instead:
`gfx_opengl_start_frame()` disables `GL_BLEND`, while Fast3D's cached
`rendering_state.alpha_blend` could still be true from the previous frame, so
the first translucent credits draw skipped re-enabling blending.

Fix: after backend frame start, invalidate Fast3D's alpha/modulate/additive blend
cache so the first translucent draw must call `set_use_alpha()` again. Kept the
related hardening from the investigation: TLUT fallback stays local to palette
loading, CI texture cache keys include palette format, and focused static tests
pin those cases plus the frame-start cache invalidation.

Verification: isolated `b346tlut` all-target build passed with logs inspected
for compile failures; focused
`.claude\session-builds\b346tlut\pd-tests.exe "[rendering][credits][texture][static][b346]"`
passed 27 assertions / 4 cases; `python tools\asset_native_source_guard.py`
passed; no temporary `B346DBG`/`s_b346` probes remain. Delayed visual smoke
captured four frames in
`.claude\smoke-verify-runs\screenshots\20260630T025340-credits_alpha_smoke_delayed_local\`
showing shaped Handel Gothic glyphs and masked blue particle trails instead of
solid quads. Harness caveat: `results-20260630T065537Z.json` is 7/8 assertions
with exit code 0 because only the stale required-line pattern
`LOAD: lv\.c entering stage load sequence for stagenum=0x5c` was missing.

## 2026-06-25 (cont.) - B-942 RESOLVED: Joanna's whole figure renders clean (per-vertex skinning)

**The multi-session chr-body leg/limb scramble (B-942) is FIXED + render-verified.** Per Mike: "chase the 1157 menu-foot -- it needs fixed or our task isn't complete."

**Root cause (systemic SP-17 -- per-vertex matrix seam collapse):** the generated dark_combat body uses N64 weighted-vertex skinning -- a single limb seam triangle's 3 verts bind DIFFERENT bone matrices, but `.pdmesh` recorded ONE matrix per FACE, mis-binding 225/601 seam tris (106 leg seams). Straight legs hid it; the bent CI hold (anim 1157) flew the mis-bound verts off-body. The earlier joint-flag (type_hi) fix computed the helper matrices but the seam verts stayed mis-bound -- a needed precursor, insufficient alone.

**The impasse broke when two instrumentation blind spots were caught:** (1) the combat-sim AI (`chrChooseStandAnimation`) re-set the anim every tick AFTER the gallery force, so prior "run vs 1157" compares were anim 106 vs itself; (2) the STITCH/DESYNC seam probes only covered `G_TRI1`, but this body emits `G_TRI4` -> a false 0 made the per-face capture look byte-faithful.

**Fix (`07676f76`; diagnosis `922d8217`):** per-VERTEX bind matrix through extract+consume -- extractor writes `model.faces.json` vtx_matrix triplets + G_TRI4 probes; consumer rebuilds the N64 multi-batch gSPMatrix/gSPVertex interleave. Single-matrix tris collapse to 1 batch = byte-identical (no prop/head regression). Port-wide: all generated chr bodies' weighted joints.

**RENDER-VERIFIED on pixels:** re-extract (601 triplets, 225 seams) -> CI-menu Joanna's whole figure (head/torso/arms/legs/feet) connects cleanly on black (`glshot/VTX_fullfig.png`) AND in-scene (`MONEY_VTX_joanna.png`).

**Prerequisite unblock (`a3019c57`):** the commit hit the asset-archive conformance guard, which (+ verify script + 2 contract tests) still pinned scene.glb at v11 while the live exporter ships v12 (`..._dualtex_alphablend`, per `1472789c`). Bumped all 4 v11->v12 -- also clears the standing 8/804 test drift.

**REMAINING (separate, minor):** a translucent box around her lower legs IN-SCENE is a SCENE element (gone under `--debug-hide-scene`, NOT the chr) -- untraced; likely the desk/table or a scene mesh (Mike's earlier "trace from draw data" ask). Deferred chr backlog (BG room lights, anim channels B-772, pdtexture fidelity) still waits.

**NEXT:** Mike's call -- trace+fix the in-scene box for a pristine menu, or move to the deferred backlog.

## 2026-06-24 (cont.) - B-936 Joanna CRACKED end-to-end: she renders at her terminal in the CI room

**The months-long "Joanna not appearing" bug is fully cracked -- 4 render root causes, all fixed on dev.** Continuing the same day from the fovy fix below, drove her from on-screen-but-dark to textured + lit + composited in the real CI menu backdrop.

**Layers fixed this session (each instrumented, not guessed):**
1. **Lighting** (`6da0e831`): generated chr bodies (mcount==3) cleared `G_LIGHTING` and fed per-vertex NORMALS as raw colours into MODULATEIA -> near-black. Keep `G_LIGHTING` on so fast3d lights the normals.
2. **Env-lift / darkness** (`23ac8e6d`): traced the stock chr render (`--debug-chr-env`, chr.c:3766) -- the suit lift is the per-chr room shade carried in FOG under 2-cycle `G_CC_CUSTOM_17/18`, not a bright env. Replicated with a DERIVED env (160) + mid shade_alpha (140) keeping the stock combine. CRITICAL: stock `FOG_PRIM_A` washes her to pure BLACK when the room-shade fog is near-black -> made generated chr bodies fog-independent opaque. She reads textured isolated (tan skin, blue-grey suit).
3. **Scene-desync** (`bc86441f`): she was invisible whenever the room rendered. Instrumented the GL state the raw-GL scene renderer leaves + her actual draw state; ruled out depth/scissor/colormask/blend/cull/FBO/array-buffer WITH DATA. Root cause: **core-profile build** -- fast3d binds its VAO/VBO once at init, the scene renderer leaves `vao=0`, and drawing with VAO 0 in core profile reads no attrib pointers (her 220-tri batches rasterised nothing despite a correct CPU clip). Fix: rebind `opengl_vao`+`opengl_vbo` per draw in `gfx_opengl_draw_triangles`.
4. **Backdrop / money shot** (`08d32a45`): with fast3d drawing again, the world's `skyRender` G_CYC_FILL (env sky colour = blue, sky.c:266) covered the scene room. Gated the `skyRender` call (lv.c:1658) on `!scenarioSceneRendererIsActive()`. RESULT (`main_menu_joanna_moneyshot/002-shot_55.png`): tiled CI room backdrop + green-monitor desk PC + Joanna textured/lit at her terminal.

**HONEST CAVEAT -> new bug B-942:** her held POSE is skeletally DISTORTED (an off-body spike-limb + contorted torso, frozen by the menu's cutscene hold -- all frames identical). The MODEL + TEXTURES are correct (zoom verified: it IS Joanna), but the pose is a bone-weight/transform bug in the generated chr body, newly visible now that she renders. A clean standing hero frame is blocked until B-942.

**Caveats banked:** env/shade-alpha derived (not stock); fog disabled for generated chr bodies (distant ones won't fade into scene fog); the VAO/VBO rebind affects all fast3d draws (a correctness win -- self-sufficient draws). Diagnostics added (all gated, off by default): `--debug-chr-env`, `--debug-scene-glstate`, `--debug-firstdraw-glstate`, `--debug-clear-depth-after-scene`, `--debug-texsample`.

**NEXT:** B-942 skeletal pose (instrument the 19 bone matrices / vertex weights of the generated chr body vs stock). Build-verify lesson reinforced: the wrapper reports SUCCESS even when ninja halts on a compile error -- verify a diagnostic string in the exe OR the binary relink mtime before trusting a run.

## 2026-06-24 - c3844 Joanna render bug ROOT-CAUSED + FIXED (degenerate fovy=0 projection)

**The Joanna-invisible bug is solved at the root.** It was NEVER colour/combine/
cycle (B-934/B-935/B-938 were all the wrong layer) and NOT framing. The prior
"in-frame" claim was VIEW-space only -- the TRACK reads the modelview
(`model->matrices[0]` = sane view `(3,7.8,-157)`). I instrumented fast3d's CLIP
space (a `--debug-force-chr-prim` clip log in `gfx_sp_vertex`) and proved her
vertices project to GARBAGE: clip x/y in the MILLIONS, negative w, `onscreen=0`.
A one-shot matrix dump pinned it: the world projection `rsp.P_matrix[0][0] =
-32768` (= -2^15) vs the normal ~1.3 perspective x-scale, z/w sane.

**Root cause:** the CI intro-cutscene camera feeds `fovy = animGetCameraValue(...)`
= **0.0** into `viSetFovY` (player.c:3549/3592). `guPerspectiveF` then computes
`cot = cos(0)/sin(0) = +inf` for `mf[0][0]=cot/aspect` + `mf[1][1]=cot`, and
`guMtxF2L` overflows those to INT_MIN (0x80000000) -> fast3d reads -32768. Every
world vertex projects ~32768x out of frame -> giant off-screen triangles, zero
recognizable pixels (the desk PC + sofa + furniture vanish WITH her -- one shared
cause, matching the long-standing "missing PC + missing Joanna" lead). The scene
renderer (room) was immune because `scenario_scene_renderer.cpp:1018` ALREADY
guards `fovy <= 1.0` and uses its own VP.

**Fix (`src/lib/ultra/gu/perspective.c`):** clamp a degenerate fovy (<=0.5 or
>=179, or non-finite) to 60.0 in `guPerspectiveF` with a rate-limited warning --
the SAME guard the scene renderer already has, and a no-op for every valid view
(incl. weapon zoom, smallest legit fov >> 0.5). **VERIFIED:** post-clamp CLIPDBG
flips to `onscreen=1` at screen-centre for all samples, and a forced-magenta
isolate capture renders a clean, recognizable Joanna HUMAN SILHOUETTE dead-centre
(arm reaching toward the desk PC). Screenshot:
`.claude/smoke-verify-runs/screenshots/20260624T092726-main_menu_joanna_isolate/iso2_a_full.png`.

**Diagnostic tooling added (flag-gated, off by default):** `--debug-force-chr-prim`
(forces every generated body DL to constant opaque-magenta PRIMITIVE in 1-cycle,
no-Z -- the "does it draw at all / where" probe; modasset_compiler.c + the
gfx_pc.cpp clip-space CLIPDBG log) and `--debug-hide-scene` (skips
`scenarioSceneRendererRender` so a forced body shows against a bare framebuffer;
gfx_pc.cpp). Scenarios `main_menu_joanna_forceprim.json` + `_isolate.json`.

**REMAINING (separate bug, NOT the projection):** with the room visible, even
no-Z (GL_ALWAYS) magenta is hidden -> a fast3d render-state desync after
`scenarioSceneRendererRender` (the raw-GL room renderer leaves GL state that
fast3d's `rendering_state` cache doesn't reflect; HUD fonts re-sync, the first
world draw doesn't). A depth/shader/texture cache-invalidate after the scene did
NOT fix it (so the blocker is blend/VAO/framebuffer/other state) -- design-
uncertain, reverted, deferred as a focused follow-up. Full TEXTURED-in-room
visibility also still rides on the pending B-934/B-935/B-938 colour/combine work.
Committed the fovy fix + tooling; left the unrelated `group_session.c` MP-relay
candidate untouched.

## 2026-06-24 - c3844 CI render saga (Joanna PARKED, credits residual) + MP relay (A+C) started

**Joanna menu render -- PARKED (framing SOLVED, render bug CONFIRMED).** The
`var8009dfc0` submission fix (`44bf45cc`) restores Joanna+PC+furniture+camera
(submitted, PASS 3/3). Built a per-frame body-origin tracker (`--debug-track-chr`,
src/lib/model.c, gate `nummatrices>1`; UNCOMMITTED -- initial `>20` wrongly excluded
her, her generated modeldef has `nummatrices=19`). It proves she settles dead-center
IN-FRAME at view `(3.0,7.8,-156.9)` through the whole capture window (the user's
spatial fact confirmed: she stands at the desk edge typing on the PC). So framing was
a RED HERRING -- the `--debug-cam-look-chr` prop-aim is ~313u off her body AND the
one-time audit matrix sampled at 34s/intro (she was at `(-24,65,-313)` then), so every
prior zoom hit the wrong spot. YET she renders ZERO pixels at her confirmed position
-> a real render bug (NOT framing/submission). Cycle-type candidate
(`gDPSetCycleType(G_CYC_1CYCLE)` in modasset_compiler.c markers; UNCOMMITTED) tested
AT her real position + REFUTED. MATCLASS: 35 mats, all opaque, 34 textured (not XLU).
Open candidates: occlusion in the headless held-cam, or a combiner/draw bug. Tool now
works for before/after verification at her real position. Real-play proof = user's
rebuild (live terminal cam, not replicable headless). Full breadcrumb: B-936, tasks.md.
Uncommitted candidates/tooling: cycle-type marker edit, `--debug-mesh-matclass`,
`--debug-track-chr` (all src/lib/model.c + port/src/modasset_compiler.c).

**Credits B-346 squares -- REOPENED (playtest FAILED).** Added `--launch-credits`
(`85e13f01`, port/src/main.c bootApplyLaunchCredits) + the credits_alpha_smoke
scenario. Visual confirm: Handel Gothic glyphs render as SOLID WHITE BLOCKS (texture
alpha cutout lost), particles as cyan/blue bars. B-346's fog-alpha fix IS in the tree
(gfx_pc.cpp:1704-1748 + both static tests) but does NOT close the credits squares.
Focused follow-up: re-examine the additive-fog branch gfx_pc.cpp:1704/1747-1754 for the
credits XLU glyph/particle case (SHADER_OPT_ALPHA_FROM_FOG wrongly set, or the CI4 glyph
texture-state reset in text0f153628 not taking).

**MP relay (A+C) -- SURVEYED + Gap B fixed; Gap A (forwarder) is the next build.** The
NAT stack (ICE/STUN/TURN/hole-punch: p2p_ice/stun/turn/lan.c, p2p.c, netstun.c,
netholepunch.c) is built. Two gaps for the symmetric-NAT relay: **(A) the relay
FORWARDER (server) is MISSING** -- `p2pTurnPoll` (port/src/net/p2p_turn.c:257) only
handles `KIND_ALLOC_ACK` (client receiving its relay addr); the SERVER side (recv
`KIND_ALLOC` -> create binding {init_handle,src_addr} + reply `ALLOC_ACK` w/ relay's own
public addr; recv `KIND_RELAY` -> look up dst binding + `sendto` payload with src/dst
rewrite; `KIND_HANGUP` -> drop binding) is TODO -- add a bindings table beside s_Cands.
**(B) onPairOpen** (group_session.c:293) used `ep->ipv4/port` (==0 for relayed
endpoints), ignoring `ep->relay_ipv4/relay_port` -> **FIXED** (honors `P2P_EP_RELAYED`,
dials the relay as the peer's proxy; UNCOMMITTED, pending build-verify with the
forwarder). NEXT: build Gap A forwarder + loopback-sim test (forwarder + 2 clients on
127.0.0.1, assert a RELAY packet forwards rewritten) + document the VPS deploy
(forwarder binds `P2P_TURN_PORT 27104`; register its public IP via
`p2pTurnRegisterRelayCandidate`). Then **other-modes check (item d)**: verify the 5
c3845 listen-host fixes (roomsInit, ready-gate, null-peer netSend, host self-disconnect,
no-free-slots) cover co-op/counter-op/etc. Protocol: KIND ALLOC=0/ALLOC_ACK=1/RELAY=2/
HANGUP=3, magic `PDTRN`, hdr 24B, port 27104.

## 2026-06-23 - c3845 Tests/Connectivity: two-process END-TO-END match-lifecycle loopback smoke

Authored (NOT built/run -- Mike compiles) the `listen_host_match_smoke.json`
regression that extends the existing `listen_host_peer_smoke` two-process
loopback from the join handshake to the full match lifecycle on 127.0.0.1:
start -> spawn(both players) -> play -> score -> end -> endscreen.

Integration approach (the riskiest part was the auto-start entry point):
- **Host auto-start** drives the SAME high-level lobby-leader path the Room
  "Start Match" button uses -- `netLobbyRequestStartWithSims(GAMEMODE_MP, ...)`
  in `port/fast3d/pdgui_bridge.c:1086`, which on a listen host replays
  `CLC_LOBBY_START` through the server handler locally. That performs the full
  room-assign / manifest-broadcast / participant-playernum / ready-gate setup
  that `netServerStageStart()` depends on. We deliberately do NOT call
  `netServerStageStart()` raw (it presupposes that setup). New `--host-autostart`
  flag + `bootHostAutostartTick()` (port/src/main.c) fires it EXACTLY ONCE once a
  remote client reaches `CLSTATE_LOBBY` + ~2s settle. The client auto-follows:
  `netmsgSvcStageStartRead` already calls `mpStartMatch()` and sends
  `CLC_STAGE_READY`, and the client answers the manifest handshake READY so the
  ready gate fires its 3s countdown (not the 30s timeout) -- NO `--auto-ready`
  flag needed.
- **Deterministic end:** the wire/config time limit (`g_MpSetup.timelimit`) is
  MINUTES-only (6-bit; `mpApplyLimits` does `(tl+1)*60` s, 1-min floor). Chose
  option (a): new `--match-timelimit-sec <n>` flag forces a seconds-granularity
  `g_MpTimeLimit60 = SECSTOTIME60(n)` override at the `src/game/lv.c` MP
  time-limit gate (the comparison reads `g_MpTimeLimit60` in 60Hz frame-units).
  Match ends at ~35s. Cleaner than a 1-min real limit + longer budget.
- **`MATCH:` log channel** at six seams (see tests.md harness-extensions list).
  The load-bearing one is the player-spawn line in `lv.c`'s per-local-player
  `playerSpawn()` loop: slot derived from `g_Vars.currentplayer->client->id`
  (the wire client id == g_NetClients[] index), which is robust against
  `netPlayersAllocate`'s client-side local-player->index-0 playernum swap
  (net.c:2366-2372). Host's local player -> client id 0 -> `slot=0`; client's
  local player -> client id 1 -> `slot=1`. `netPlayersAllocate` runs in
  `playermgrAllocatePlayers` (src/lib/main.c:985) BEFORE `lvReset` (line 1012),
  so `->client` is populated when the spawn loop runs.

Seams that cooperated cleanly: 0 bots is valid (2 humans meet base:combat's
min-players=2; the CLC_LOBBY_START handler builds player participants from
connected clients, not from bot slots). `base:arena_mp_felicity` + `base:combat`
verified to exist (arenadata_authored.c:67, assetcatalog_base_extended.c:328).

Files: port/src/main.c (2 flags + tick + accessor), port/src/pdmain.c (tick
wiring), src/game/lv.c (timelimit override + stage-end log + spawn log),
port/src/net/net.c (server-stage-start + scores logs), port/src/net/netmsg.c
(client-stage-start log), src/game/mplayer/mplayer.c (endscreen log),
tools/smoke-verify/tests/listen_host_match_smoke.json (new).

NOT yet build-verified or run. `timeout_seconds=240`, scripted exit at 200000ms.

## 2026-06-23 - c3844 CI menu glass: universal alpha-mode fix

Root-caused and fixed the Carrington Institute menu glass table rendering as an
opaque black slab that occluded the live scene. Two layers:

- **B-940 (immediate win):** the scenario scene renderer
  (`port/fast3d/scenario_scene_renderer.cpp`) drew the whole CITRAINING
  scene.glb in one opaque pass with `GL_BLEND` disabled. Added a two-pass blend
  (opaque groups, then alpha groups with blend + depth-write off). Glass became
  translucent (confirmed on screen).

- **B-941 (universal root fix):** the `.pdscenario` extract
  (`port/src/romextract_pdarena.c`) blanket-tagged every alpha-texture material
  `alphaMode=MASK`, so translucent glass was a hard cutout, never blended. The
  room's translucency actually lives in the opaque-vs-xlu block split (like the
  `.pdmesh` render-class capture). Rewrote the scene walk to traverse
  `gfx->opablocks` and `gfx->xlublocks` separately (following `block->next` +
  `PARENT->child` exactly like `bgGetNextGdlInBlock`), stamping `is_xlu` onto
  each material: xlu -> BLEND, opaque+alpha -> MASK cutout, else OPAQUE. The
  renderer was generalized from the texture-alpha `uses_alpha` heuristic to
  honor the glTF `alphaMode` per material (BLEND in the blend pass; MASK/OPAQUE
  in the opaque pass, MASK alpha-testing via a new `u_AlphaCutoff` uniform).
  Extract/cache versions bumped to force re-extraction.

  CITRAINING now classifies **opaque:63 / mask:3 / blend:15** (was 0 BLEND / 12
  MASK). The glass table renders translucent teal with the floor and pillars
  visible through it. Fonts and particles render through the separate
  render-mode-aware fast3d GBI path (`gfx_pc.cpp`: CVG_X_ALPHA cutout,
  G_AC_THRESHOLD, blender translucency) and were already correct -- no change.

Also added `--debug-cam-look-chr` (`src/game/player.c`), a gated aid that aims
the camera at the player chrbody prop (cam_look written as a direction vector,
since `playerAllocateMatrices` treats it as a forward vector). It frames the
prop area but not Joanna's body directly -- the intro cutscene poses her body
offset from the prop and at greater view depth, so a clean close portrait
remains a follow-up. Her body provably renders every run (`MODASSET.RENDER`
base:dark_combat 1803 verts / 601 tris).

Commits: `1472789c` (universal alpha-mode fix), `91124983` (cam-aim aid). Bugs
B-940, B-941 recorded in `context/bugs.md`.

## 2026-06-12 - c3844 live visual correctness audit closure

Closed the c3844 live rendered-asset audit for the current tree without
broadening to unrelated cards. Scoped source-gate smokes now cover Scenario
geometry/textures, weapons/hands, custom body/head, props/projectiles/entities/
vehicles, materials/skins/effects, UI/HUD/fonts/lang/theme, audio, and
animation. The passing evidence is retained in smoke result JSON/logs:
`scenario_pads_source_gate_smoke` 154/154, `weapon_match_source_gate_smoke`
45/45, `custom_body_live_render_smoke` 41/41,
`archive_walker_source_gate_smoke` 19/19, `all_family_source_gate_smoke` 36/36,
`audio_live_playback_source_smoke` 21/21, and
`base_animation_source_probe_smoke` 19/19. The current harness does not emit
screenshot artifacts, so the durable evidence is logs and result JSON.

Fixed real audit failures found by the live runs: source-only `.pdui` startup
now extracts `.pdtexture` first, generated runtime-cache writes create missing
parent directories and use shorter private filenames for Windows paths, base
source-only catalog probes are deferred until base extraction/walking/runtime
cache is ready, scoped FSPATH tracing no longer widens normal smoke logs, and
public `.pdsong` sequences whose loops naturally close at track end now compile
instead of falling back to legacy ROM music.

Verification passed focused c3844/c3809/static pd-tests, `python
tools\asset_native_source_guard.py`, all-family archive conformance across
8,093 root / 9,125 checked archives, audio/mesh/animation source verifiers,
the live smokes listed above, and isolated `c3844vis` all-target build. Session
build directories were removed and no `PerfectDark`, `PerfectDarkServer`, or
`WerFault` processes remained after cleanup.

## 2026-06-12 - c3844-s110 Public Mods runtime delivery proof

Closed the received Public Mods runtime proof for strict `.pdmod` assets. The
file-transfer received-mod path now delegates validation to
`modmgrValidateArchiveFile()` and installation to `modmgrInstallArchiveFile()`,
so inbox delivery, Modding Hub import, and installed archive registration share
the same strict contract. Non-friend received mods install disabled, queue the
received-mod enable prompt, and enable through the same apply-changes path when
accepted.

Fixed a runtime source-binding gap found by the smoke: live `.pdmod` scanner
registration for head/arena descriptors without nested archives now binds
`model_file` / `geometry_file` as the primary public source. The Public Mods
smoke now stages a `.pdmod` under `social/inbox/mods`, installs it through the
received path, accepts the enable prompt, and proves `tri_head`, `tri_arena`,
and `fixture:character_skeletal` load from the installed archive through
catalog/VFS.

Verification passed focused Public Mods tests (297 assertions / 9 cases),
`python tools\asset_native_source_guard.py`, `git diff --check`, the scoped
`public_mods_pdmod_install_smoke` (34/34), and isolated `s110pub` all-target
build. Process checks were clean after smoke/build, and the session build
directory cleanup completed.

## 2026-06-11 - c3844/B-801 source-only fallback audit and smoke gate

Audited the remaining c3844/B-801 source-only fallback/fatal-cutover surface
across runtime load, render, audio, animation, collision, catalog/provider, and
c3849 Wave 7 readiness. No new narrow direct runtime ROM/RomProvider fallback
site required a code patch: the reviewed paths are eliminated, catalog/provider
owned as private bridge debt, or loud/fatal under source-only enforcement. The
c3849 Wave 7 items remain gated on live B-801 proof: no weapon graph default
flip, no per-family fatal cutover, no strict MP mismatch refusal/protocol bump,
and no toggle retirement yet.

Made narrow readiness fixes only. Generated mod-asset cache writes now log the
expanded path and errno when opening a private cache file fails, the three
source-gate smoke fixtures wait longer for cold extraction before scripted exit,
and the pd-tests stubs now include an inert `fsFileLoad` so weapon-graph archive
VFS fallback coverage links in focused tests.

Verification passed `python tools\asset_native_source_guard.py`, focused
fallback/static selectors (19,675 assertions / 79 cases), and isolated
`c3844audit` all-target builds. The live smoke gate is not closed:
`all_family_source_gate_smoke`, `base_animation_source_probe_smoke`, and
`audio_live_playback_source_smoke` still exit before scripted sentinel/debug
proof while bootstrap/extraction is active; latest results are
`.claude/smoke-verify-runs/results-20260612T015000Z.json`. A prior audio run
also exposed generated animation normalized-cache write failures; the new cache
diagnostics have not re-exercised that phase because the latest retained run
exited earlier. Tracked as B-929.

## 2026-06-11 - c3844-s110 pdmod import/install workflow closure

Closed the next user-facing s110 workflow gap in a batched pass. The strict
`.pdmod` production packer existed, but Modding Hub still exposed the old
`.pdpack` import/export tool, and received Public Mods validated only a narrow
authored `.bin` case before copying into `mods/installed`.

Added shared `modmgrValidateArchiveFile()` and `modmgrInstallArchiveFile()`
helpers for strict `.pdmod` install/import validation. The validator checks root
`mod.json`, rejects public `.bin` / `.tsv` payloads, and release-validates nested
typed `.pdxxx` archives through the same public-source contract used by direct
mod registration. Modding Hub now presents pack/import `.pdmod` workflows only,
installs imports through the shared mod-manager helper, and removes the active
legacy `.pdpack` UI. Received Public Mods now call the same shared validator
before install, so transfer, Hub import, and registry scan fail for the same
payload-shape problems.

Verification passed `python tools\asset_native_source_guard.py`,
`python tools\verify_pdxxx_modder_workflow.py`, checked-in all-family
conformance, focused pdmod/Public Mods static tests (2,127 assertions / 26
cases), scoped diff-check, board JSON parse, decision-request check, isolated
`s110import` all-target build, and session-build cleanup.

## 2026-06-11 - c3844-s110 production pdmod packer proof

Closed the next s110 workflow gap: the all-family workflow verifier was proving
`.pdmod` round trip with a Python zip writer, but the actual user-facing
Modding Hub/shared packer path still rejected the checked-in all-family example.
The production packer had stale validation assumptions: `.pdarena` was still
treated as a loose geometry/map archive requiring `geometry_file`, and `.pdsong`
was validated like SFX/voice with a required `file_path`.

Updated `modpack_pdmod.c` so `.pdarena` accepts the current
`scenario_archive`-backed typed dependency shape, while preserving geometry keys
for compatible layouts. Split `.pdsong` validation from SFX/voice so music
archives can use `music_file`, `midi_file`, or `track_file` as valid public
source. Replaced the test target's no-op INI stubs with a lightweight parser so
packer tests exercise descriptor source refs, then added a production packer
round-trip test for `examples\modding\typed-pdxxx-basic` that packages with
`modpackPdmodFromFolder()` and verifies every typed archive is preserved
byte-for-byte inside the resulting `.pdmod`.

Verification passed focused production-packer test (174 assertions / 1 case),
focused pdmod/source selectors (3,193 assertions / 37 cases), source guard,
all-family workflow verifier, checked-in all-family conformance, diff-check,
isolated `s110prod` all-target build, session-build cleanup, and process scan
with no lingering `PerfectDark`, `PerfectDarkServer`, or `WerFault` processes.

## 2026-06-11 - c3844-s110 pdmod transport TSV closure

Closed a real s110 workflow gap at the `.pdmod` transport boundary. Typed
archives and the all-family workflow verifier already rejected public `.tsv`,
but `.pdmod` folder packing and direct installed archives still primarily
checked for authored `.bin` payloads. That meant a loose public TSV file could
enter through the transport/load path even though TSV is not a valid final
modder-facing source format.

Updated `.pdmod` packing and loading so folder packaging, in-memory single-entry
writing, descriptor source refs, archive-internal descriptor refs, and direct
archive registration reject public `.tsv` payloads alongside `.bin`. Direct
`.pdmod` registration now also release-validates nested typed `.pdxxx` archive
bytes before accepting them. Modding Hub packer guidance now says `no
.bin/.tsv`, and the external-format `.pdmod` contract records that public TSV
tables are invalid final source.

Verification passed `python tools\asset_native_source_guard.py`,
`python tools\verify_pdxxx_modder_workflow.py` (27 families, 59 archives, 31
nested archives, 229 public source entries), checked-in all-family conformance,
focused pdmod/source tests (3,016 assertions / 36 cases), diff-check, isolated
`s110tsv` all-target build, session-build cleanup, and process scan with no
lingering `PerfectDark`, `PerfectDarkServer`, or `WerFault` processes.

## 2026-06-11 - c3844-s110 all-family modder workflow verifier

Moved from the s109 bridge audit into the first s110 end-to-end workflow proof.
Existing smokes already covered representative folder, `.pdmod`, runtime load,
live body render, audio playback, and source activation paths, but there was no
all-family proof that the checked-in modder example folder could be packaged as
`.pdmod` transport, unpacked again for editing, and retain every typed archive
unchanged.

Added `tools/verify_pdxxx_modder_workflow.py`. It validates
`examples\modding\typed-pdxxx-basic`, requires all 27 public typed archive
families, rejects public `.bin` and `.tsv`, counts editable standard and
semantic source members, packages the folder as `.pdmod`, extracts it, and
verifies `mod.json` plus every `.pdxxx` archive hash round-trips unchanged.
Also corrected a stale static pin so public `weapon_id` INI reads remain
forbidden while private preserved weapon slots are still allowed.

Verification passed Python compile, the new workflow verifier (27 families, 59
archives, 31 nested archives, 229 public source entries, 45 standard source
entries, 184 semantic source entries), all-family example conformance, source
guard, asset tool selftest, example audio/mesh verifiers, focused c3844/c3842
static tests (8,880 assertions / 37 cases), diff-check, isolated `s110flow`
all-target build, session-build cleanup, and process scan with no lingering
`PerfectDark`, `PerfectDarkServer`, or `WerFault` processes.

## 2026-06-11 - c3844-s109 batched model bridge fallback cleanup

Batched the remaining modelnum/filenum bridge audit by failure pattern instead
of one call site at a time. The scan found two real menu-preview holdouts:
MP head preview in `src/game/mplayer/setup.c` and weapon menu preview in
`src/game/mainmenu.c` still submitted raw legacy file numbers when catalog
resolution failed. Both now clear the preview and log a `CATALOG.MISS` warning
instead of asking `menuRenderModel()` to resolve a raw filenum.

The rest of the audited model/audio bridge calls are either catalog-owned
helpers, source-only guarded runtime consumers, diagnostic labels, or already
covered by fallback telemetry. Added guard and static-test pins so the removed
menu-preview fallback signatures cannot return. Verification passed Python
compile for the touched tools, `python tools\asset_native_source_guard.py`,
diff-check, focused `[catalog][provider][static]` plus c3844/c3842 source
contract tests (13,384 assertions / 59 cases), and isolated `c3844batch`
all-target build. The session build directory was removed after verification.

## 2026-06-11 - c3844-s109 mesh descriptor provenance cleanup

Continued the private integer bridge audit from current tree state. A direct
zip-level scan found another public descriptor leak: generated `.pdmesh`
archives still wrote `source_filenum_symbol` into public `mesh.ini` while the
runtime loader already reads that provenance from `_meta/manifest.json`.

Removed `source_filenum_symbol` from public `mesh.ini` emission while keeping it
in `_meta/manifest.json`, added conformance rejection for putting the field back
into public INI descriptors, and tightened the MP3 audio verifier so
`source_filenum` provenance must live in `_meta/manifest.json` rather than being
accepted from `voice.ini`. Repaired the retained `Build\data\ntsc-final`
archives and removed one stale `*.pdcharacter extracted` inspection folder that
contained old `.zip` inspection artifacts rather than canonical typed archives.

Verification passed Python compile for the touched tools, conformance selftest,
checked-in all-family example conformance, strict retained-tree conformance
(8,065 root / 9,066 checked archives after removing the stale inspection
artifact), audio source verification (2,111 archives including 104 MP3 voices),
mesh source verification (734 archives), zero public descriptor bridge-field
hits across checked examples plus retained output, `python
tools\asset_native_source_guard.py`, focused c3844/c3842 static tests (1,801
assertions / 19 cases), diff-check, and isolated `c3844sym` all-target build.

## 2026-06-11 - c3844-s109 public descriptor bridge cleanup

Continued the private integer bridge audit after the smoke cleanup recheck. A
zip-level scan found the checked-in modder examples clean, but retained base
output still exposed private bridge/provenance fields in public descriptors:
`texture.ini` had `empty_rom_slot`, and generated `.pdlang` / MP3 `.pdvoice`
descriptors had `source_filenum`. Runtime already reads the required voice
file-number bridge from `_meta/manifest.json`, and language loading uses
`source_bank` plus `strings.json`, so those fields did not need to remain in
the authored INI surface.

Removed the public descriptor fields from the emitters while preserving `_meta`
provenance, added a conformance gate that rejects `source_filenum` and
`empty_rom_slot` in public INI descriptors, and repaired the retained
`Build\data\ntsc-final` archives so the generated tree matches the contract.
Verification passed conformance selftest, checked-in all-family example
conformance, strict retained-tree conformance (8,066 root / 9,067 checked
archives), audio source verification (2,111 archives including 104 MP3 voices
and 120 sequence songs), `python tools\asset_native_source_guard.py`, and
focused `[modding][pdxxx][c3844][source][static]` (985 assertions / 11 cases).

## 2026-06-11 - B-927/B-928 smoke cleanup recheck

Mike noted another session may have fixed the lingering `PerfectDark.exe`
application-error dialogs. Rechecked the current checkout instead of assuming:
no `PerfectDark`, `PerfectDarkServer`, or `WerFault` processes were already
running; the focused B-927/B-928 static regressions passed; and a fresh
`boot_smoke` using the isolated `crash928` client passed 14/14 with OS exit 0,
no `exit-code override`, and no lingering game or fault-reporting processes.

Follow-up cleanup: `Add-SmokeFirewallAllowRule` now treats firewall permission
denial as a terminating operation inside its existing `try/catch`, so
non-admin runs produce the controlled warning instead of a raw red PowerShell
error while continuing the smoke. Updated the stale smoke-runner static pin that
still expected the old default `--no-crash-handler` behavior. Verification
passed focused tests for the crash-dialog, shutdown, and smoke-runner static
contracts (42 assertions / 3 cases), the fresh `boot_smoke` above, and
`python tools\asset_native_source_guard.py`.

## 2026-06-11 - B-928 smoke shutdown heap-corruption fix

Mike noted another session may have fixed the lingering test error. A fresh
`boot_smoke` proved the popup cleanup held, but B-928 still reproduced: the
smoke passed 14/14 and then Windows returned `-1073740940` / `0xC0000374`
after the smoke sentinel. The log stopped after `STATS: shutdown`, narrowing
the failure to final teardown.

Root cause was `videoShutdown()` freeing `vidModes` unconditionally even though
the fallback value is static `vidModeDefault` when display-mode enumeration
does not allocate a heap list. The fix adds explicit display-mode ownership,
frees only owned heap storage, resets shutdown state back to the static fallback,
and preserves cleanup if shrink `realloc` fails.

Verification passed focused `[video][shutdown][static][b928]` with 10
assertions / 1 case, bounded `boot_smoke` with the freshly built isolated
`b928fix` client (PASS 14/14, OS exit code 0, no `exit-code override`), a
post-run process scan with no lingering `PerfectDark`, `PerfectDarkServer`, or
`WerFault`, and `python tools\asset_native_source_guard.py`. Removed the
`b928check` and `b928fix` session build directories afterward.

## 2026-06-11 - Smoke crash-dialog cleanup hardening

Mike reported lingering `PerfectDark.exe - Application Error` dialogs during
testing. Current source already had the first half of the fix: `crashInit()`
sets `SEM_NOGPFAULTERRORBOX`, and static coverage existed for that contract.
The remaining gap was in the smoke runner. Harness-mode smoke launches still
passed `--no-crash-handler` by default, which bypassed the child process crash
handler that suppresses modal Windows fault UI. The runner also did not
proactively reap smoke-owned `PerfectDark.exe`, `PerfectDarkServer.exe`, or
`WerFault.exe` processes left behind by older faulting runs.

Updated `tools/smoke-verify/run.ps1` so single-process and multi-process smoke
runs keep the in-game crash handler enabled by default, with raw crash behavior
available only through `PD_SMOKE_DISABLE_CRASH_HANDLER=1`. Added scoped cleanup
for smoke-owned fault processes before launch, after each launched process exits
or is watchdog-killed, and in final teardown. The cleanup is scoped to known
smoke process IDs, `--smoke` command lines, `.claude\smoke-verify` paths, and
matching `WerFault` instances so it does not target normal manual game sessions.

Verification passed the PowerShell parser check, focused
`[logging][crash][static][b927]` with 13 assertions / 1 case,
`python tools\asset_native_source_guard.py`, and a bounded
`boot_smoke` using the freshly built isolated `crashdlgsmoke` binary. The smoke
passed 14/14 and a post-run process scan found no lingering `PerfectDark`,
`PerfectDarkServer`, or `WerFault`. The smoke still reported `OS exit
-1073740940` after the harness sentinel, so B-928 records that as a separate
shutdown heap-corruption follow-up instead of hiding it behind the dialog fix.

## 2026-06-11 - c3844-s109 public numeric bridge cleanup

Continued the private integer bridge closure audit. The failure class was not
new asset extraction loss; public mod templates and external scan paths still
exposed or accepted legacy numeric bridge fields after the project decision that
base content and mods must use the same accessible archive contract. Removed
`headnum`, `bodynum`, `anim_id`, `sound_id`, and `load_mode = 0` from the
modder-facing templates, and stopped local scan plus network-distributed scan
from consuming public numeric bridge inputs for custom map/arena/scenario,
body/head, animation, texture, SFX, and voice assets. Those assets now receive
catalog-owned private runtime slots where the current engine still requires an
integer. Music remains catalog-ID source plus a private virtual sequence slot
only for the old sequencer bridge; new custom weapons stay on private catalog
weapon/MP slots while existing base-row overrides can preserve their row.

Verification passed source sweeps for the removed public numeric reads, diff
check, `python tools\asset_native_source_guard.py`, conformance selftest,
checked-in all-family example conformance, focused `[modding][pdmod][static][c3809]`
with 1188 assertions / 16 cases, focused
`[modding][pdxxx][c3844][source][static]` with 985 assertions / 11 cases,
private slot selectors with 322 assertions / 28 cases, and isolated
`c3844s109` all-target build. The board remains intentionally narrow: `c3844`
is the only Active/routable card, non-`c3844` cards stay deferred or
history-only, and the next c3844 route is finishing s109 then moving to s110
end-to-end modder workflow proof.

## 2026-06-11 - c3844-s108 retained-output currentness closure

Closed the current retained-output gate. The fresh extractor-only run writes all
expected public archive families, including 15 `.pdui`, 14 `.pdmission`, 3,503
`.pdtexture`, and 87 `.pdscenario` archives. The `.pdui` failure point was not
bad UI source data; extractor-only UI texture decode ran before
`catalogLoadInit()`, so texture source resolution could not use the normal
reverse index and fell through toward legacy texture loading. The texture source
loader now falls back to scanning enabled public `.pdtexture` catalog rows
directly and still fail-closes if the public image source is absent or invalid.

Refreshed durable `Build\data\ntsc-final` from the verified extractor output.
Strict whole-tree conformance now passes on the retained tree with 8,066 root
archives / 9,067 checked archives across all 26 public families. CPU validators
also pass on retained output: audio 2,108 archives, animation 1,060 archives,
and mesh 738 archives. The clean extractor log scan found no searched
`ASSET.SOURCE_ONLY`, `LOUDFAIL.FALLBACK`, `ASSET.FALLBACK`, fatal, exception,
access-violation, or `RomProvider:filenum` signatures. Focused
`[modding][pdxxx][c3844][source][static]` tests pass with 985 assertions / 11
test cases.

Board/context state after this closure: `c3844` remains the only Active card.
`c3844-s108` can be treated as done for the current retained generated install.
The next active route is `c3844-s109` private integer bridge closure audit,
followed by `c3844-s110` end-to-end modder workflow proof. All unrelated Kanban
cards stay deferred or history-only unless Mike explicitly reopens them or folds
their proof into `c3844`.

## 2026-06-11 - Context/Kanban refresh for c3844-s108 retained-output currentness

Mike asked to update the context system fully and remove or defer irrelevant
Kanban cards. The live board was rechecked directly from
`tools/kanban/state.json`, parked threads, the decision-request evaluator, and
the active audit folder. No additional card deletion or movement was needed:
`c3844` is still the only Active card, all 49 Backlog cards are priority-5
deferred with `x_deferred`, all 104 Done cards are history-only, Blocked is
empty, the only active/routing marker is `c3844`, unresolved decision
requests are zero, parked threads are manual-resume only, and no non-`c3844`
Card Decisions workspace is live.

The context handoff was refreshed to the current `c3844-s108` state. Scenario
archives are current, and CPU verifier passes for audio, animation, and mesh
retained/generated trees are green. Whole-tree strict conformance is not green
yet: the current failure is stale `.pdmission` `scenario_graph_cache` metadata
and missing `.pdui` output in the fresh extractor tree. Continue with the
`.pdmission`/`.pdui` retained-output fix before moving to the private integer
bridge audit and end-to-end modder workflow proof.

Reverified at 2026-06-11T16:11:45-04:00 with the same result: no card deletion
or movement is warranted. The board, parked-thread file, decision-request
evaluator, and audit folder agree that `c3844` is the only active/routable card,
all unrelated cards are deferred or history-only, and parked threads are manual
resume only.

## 2026-06-11 - c3844-s107 Scenario normal-play fallback closure and board refresh

Closed the remaining normal-play Scenario stage-load fallback posture after the
command graph node set was proven. Public `.pdscenario` source failure now
fail-closes instead of continuing into legacy ROM payloads for level graph
activation, setup source, pads source, tiles source, and background source
renderer activation. The shared fatal includes the stage id, payload, legacy id,
and resolved public Scenario source, and states that runtime ROM/RomProvider
fallback after extraction is an asset-chain failure.

Added native-source guard coverage and focused static pins rejecting the old
setup/pads/tiles/background fallback signatures. Verification passed
`python tools\asset_native_source_guard.py`, Python compile for the guard, an
isolated `c3844normal` tests build with a longer compile watchdog, and direct
focused `[modding][pdxxx][c3844][source][static]` execution: 985 assertions /
11 cases passed. The first test wrapper run hit the default 60-second compile
watchdog while still compiling tests; the error log was empty and the rerun with
a longer watchdog passed.

Kanban/context routing remains strict: `c3844` is still the only Active card,
all non-`c3844` Backlog cards remain priority-5 deferred, Blocked is empty, Done
cards are history-only, and no non-`c3844` card is routable. `c3844-s107` can be
treated as done; the next active subtask is whole-tree retained-output
currentness, followed by private integer bridge audit and end-to-end modder
workflow proof.

## 2026-06-11 - c3844-s107 Scenario command graph completeness proof

Closed the Scenario AI command graph node-set gap for regenerated base output.
The extractor now emits one explicit `scenario.ai.command` graph node per
`ai/ailists.json` row, with source/list/offset/opcode/opcode-name/semantic-kind
metadata, and links each command node from `scenario.ai.lists`. Strict
conformance now requires `counts.ai_commands`, command-node existence, copied
row fields, semantic kind, and the list-to-command link.

The safe regeneration path was also hardened: `--extract-assets-only` now skips
window/UI/input startup and skips full gameplay/window teardown after extraction,
so the run does not require an OpenGL window and exits cleanly after catalog
extraction, archive walking, and runtime-cache generation. Verified
extractor-only run exited 0 from the isolated `c3844graphnodes` client.

Verification passed `python tools\asset_native_source_guard.py`, Python compile
for archive/source/example tools, checked-in all-family example conformance,
focused `[modding][pdxxx][c3844][source][static]` (985 assertions / 11 cases),
isolated all-target build, extractor-only runtime run exit 0, strict Scenario
conformance on session output and durable `Build\data\ntsc-final\scenarios`,
and direct command graph counts: 87 archives, 75,920 AI rows, 75,920 command
nodes, 75,920 command links, 0 generic command rows, 0 missing/unlinked command
rows. Durable `Build\data\ntsc-final\scenarios` was refreshed from the verified
session output.

Kanban cleanup remains strict: `c3844` is still the only Active card and no
non-`c3844` card was reopened. Remaining c3844 route: Scenario normal-play
fallback posture now that graph node-set completeness is proven, whole-tree
retained-output currentness, private integer bridge audit, and end-to-end modder
workflow proof.

## 2026-06-11 - Context/Kanban strict cleanup refresh

Mike asked to update the context system fully and remove or defer irrelevant
Kanban cards. The live board was checked directly from the actual JSON card
records, parked-thread file, decision-request evaluator, and active audit
folder. No additional card deletion or movement was needed: `c3844` is the only
Active card, all 49 Backlog cards are priority-5 deferred with `x_deferred`, all
104 Done cards are history-only, Blocked is empty, the only card star/routing
marker is `c3844`, unresolved decision requests are zero, parked threads are
manual-resume only, and no non-`c3844` Card Decisions workspace is live.

Refreshed the cold-start index, task ledger, modding pillar, Kanban root
metadata, and parked-thread metadata to the same 2026-06-11T14:00:45-04:00
stamp. This cleanup handoff was superseded later the same day by the command
graph completeness proof above; the current c3844 route is Scenario normal-play
fallback posture, whole-tree retained-output currentness, private integer bridge
audit, and end-to-end modder workflow proof.

## 2026-06-11 - c3844-s107 safe Scenario regeneration proof

Closed the stale Scenario archive regeneration blocker for the active c3844
parity route. Added `--extract-assets-only` so the normal catalog extraction,
walker, and runtime-cache boot path can regenerate retained archives while
skipping audio/network and exiting before scheduler, stage, gameplay, and render
startup. The safe run exited 0 and logged the expected audio/net skips plus the
early extractor-only completion path. The old 512-entry config registry cap was
raised to 2048 because it stopped this current boot path before extraction.

Scenario-only conformance now seeds known catalog IDs from authored base source
tables and sibling typed archives, so validating only the Scenario folder no
longer rejects real base body/head/weapon/model references. The regenerated
Scenario set contains 87 `.pdscenario` archives, 75,920 AI rows, zero generic
`opcode_name = "command"` rows, and zero missing `ai/ailists.json` members.
`Build\data\ntsc-final\scenarios` was refreshed from the verified session output
and passes strict Scenario conformance 87/87. The isolated session build
directory was removed after the durable Build/data refresh.

Verification passed `python tools\asset_native_source_guard.py`,
`python tools\asset_archive_conformance.py --selftest`, Python compile for the
conformance/guard tools, focused
`[modding][pdxxx][c3844][source][static]` (978 assertions / 11 cases), isolated
client build, extractor-only runtime run, strict Scenario conformance before
cleanup on the session output, and strict Scenario conformance on durable
`Build\data\ntsc-final\scenarios`.

Kanban cleanup remains unchanged: `c3844` is the only Active card and
`c3844-s107` is the only active proof subtask. No unrelated card should be
reopened. This handoff was superseded by the command graph completeness proof
above; the remaining route is Scenario normal-play fallback posture, then
whole-tree retained-output currentness, private integer bridge audit, and
end-to-end modder workflow proof.

## 2026-06-11 - Context/Kanban refresh after Scenario opcode fix

Mike asked to update the context system fully and remove or defer irrelevant
Kanban cards. The live board was checked from `tools/kanban/state.json`, parked
threads, the decision-request evaluator, and the active audit folder. No
additional deletion or movement was needed: `c3844` is the only Active card,
all 49 Backlog cards are priority-5 deferred with `x_deferred`, all 104 Done
cards are history-only, Blocked is empty, the only attention flag is `c3844`,
unresolved decision requests are zero, parked threads are manual-resume only,
and no non-`c3844` Card Decisions workspace is live.

The canonical context handoff and Kanban metadata were refreshed to the latest
`c3844-s107` state. Level graph tick proof is fixed, Scenario AI opcode
extraction no longer flattens declared commands to generic `"command"`, and
retained/generated `.pdscenario` archives were known stale at that point. This
handoff is superseded by the later safe Scenario regeneration and command graph
completeness proofs above; the current route is Scenario normal-play fallback
posture, then whole-tree retained-output currentness, private integer bridge
audit, and end-to-end modder workflow proof.

## 2026-06-11 - c3844-s107 Scenario level graph active-runtime hook

Closed the first confirmed Scenario AI graph/runtime fallback gap. The active
stage tick path now calls `scenarioSourceLevelGraphRecordTick("lvTick.start")`
beside the existing mission-phase record. That runtime hook only activates when
a public Scenario level graph is active; it asserts the loaded
`.pdscenario::level.graph.json` path, the required global-settings source node,
scenario identity, and source kind, then logs
`backend=graph.global.settings+level.tick` once for the active stage.

The Scenario source smoke matrix now expects this active-stage proof line, and
the static asset-source contract pins the header, runtime log/failure strings,
and `lvTick` callsite. Verification passed `python tools\asset_native_source_guard.py`,
focused `[modding][pdxxx][c3844][source][static]` (936 assertions / 10 cases),
and isolated `c3844s107` client build. The session build directory was removed.

`c3844-s107` remains active because the broader normal-play fallback flip still
depends on graph node-set completeness across campaign stages. The next Scenario
slice should prove that completeness before changing normal-play fallback
behavior.

## 2026-06-11 - Context/Kanban cleanup refresh to c3844-s107

Mike asked to update the context system fully and remove or defer irrelevant
Kanban cards. The board was checked directly from the live card records, parked
thread file, and decision-request evaluator. No additional deletion or movement
was needed: `c3844` is the only Active card, all 49 Backlog cards are
priority-5 deferred with `x_deferred`, all 104 Done cards are history-only,
Blocked is empty, the only attention flag is `c3844`, unresolved decision
requests are zero, parked threads are manual-resume only, and no non-`c3844`
Card Decisions workspace is live.

The context handoff was refreshed so future sessions route to `c3844-s107`.
`c3844-s105` is done through the live SFX/voice/music proof, custom body/head
live render proof is already green, and the remaining active route is Scenario
AI graph/runtime fallback closure, then stale generated-output regeneration,
private integer bridge audit, and end-to-end modder workflow proof.

Follow-up verification at 2026-06-11T12:34:40-04:00 refreshed the canonical
context files, Kanban root metadata, and parked-thread metadata. The board still
needs no deletion or movement: `c3844` is the only Active card, `c3844-s107` is
the only active subtask, all 49 Backlog cards are priority-5 deferred, all 104
Done cards are history-only, Blocked is empty, the only flag is `c3844`,
decision requests have zero unresolved questions, and parked threads are
manual-resume only. The parked AllInOne context reference was corrected to the
archived audit path so active audits stay clean.

Final verification at 2026-06-11T12:42:13-04:00 rechecked the actual card
`flag` field as well as the card-task context marker. The board structure is
unchanged and clean: one Active card (`c3844`), only the `c3844` card star, 49/49
Backlog cards priority-5 deferred with `x_deferred`, zero Blocked cards, 104/104
Done cards history-only, zero unresolved decision requests, parked threads
manual-resume only, and no live non-`c3844` Card Decisions workspace.

## 2026-06-11 - c3844 live audio playback proof green

Finished the next B-801 live proof slice. The stale local
`sndSetSfxVolume(s32)` declaration in [main.c](../port/src/main.c) conflicted
with the real `snd.h` signature and blocked the fresh client build; it is now
removed, with static coverage preventing that declaration from returning.

Verification passed: `python tools\asset_native_source_guard.py`; JSON parse for
the new audio smoke fixture and Kanban files; `git diff --check` for touched
runtime/test/context files; focused
`[modding][pdxxx][c3844][source][static]` (936 assertions / 10 cases); isolated
`c3844audio2` client build with `PerfectDark.exe` present and no `FAILED` or
stderr build markers; and bounded `audio_live_playback_source_smoke` against
that fresh client, passing 21/21 in 41.2s.

The smoke used scoped `catalog,audio,game,system` logging
(`channel_mask=0x028A`) and proved source-only playback for
`base:sfx_alarm_2`, `base:voice_cover_me_aiw`, and `base:song_sequence_a`. The
song path logged public `sequence.mid` plus `sequence.json` source before
playback. Reviewed failures/fallbacks were absent: no `ASSET.SOURCE_ONLY`,
`LOUDFAIL.FALLBACK`, `ASSET.FALLBACK`, missing/fail/category mismatch, fatal,
access violation, `RomProvider:filenum`, or `--no-sound`. Non-blocking notes:
the harness still trusted the scripted-exit sentinel over a shutdown OS
exit-code override, firewall-rule setup may be denied without admin rights, and
unrelated startup warning noise remains outside this audio proof.

`c3844-s105` is no longer the next route. Continue with `c3844-s107`: Scenario
AI graph/runtime fallback closure, then stale generated-output regeneration,
private integer bridge audit, and end-to-end modder workflow proof.

## 2026-06-11 - Context/Kanban refresh after custom-body proof

Requested cleanup refresh at 2026-06-11T12:02:04-04:00 rechecked the live board
and context system. No card deletion or movement was warranted: `c3844` is the
only Active card, all 49 Backlog cards are priority-5 deferred with
`x_deferred`, all 104 Done cards are history-only, Blocked is empty, the only
attention flag is `c3844`, unresolved decision requests are zero, parked threads
are manual-resume only, `context/audits/` contains only the current c3849
measurement audit, and no non-`c3844` Card Decisions workspace is live. The
Kanban root cleanup metadata, context index, and task ledger now carry this
verification stamp. This note was superseded after `c3844-s105` completed; the
current active proof subtask is `c3844-s107`.

Mike asked to update the context system fully and remove or defer irrelevant
Kanban cards. The board already had the structural routing correct, so no card
deletion or movement was needed: `c3844` remains the only Active card and the
only attention flag; all 49 non-`c3844` Backlog cards remain priority-5 deferred
with `x_deferred`; all 104 Done cards remain history-only; Blocked is empty; the
decision-request evaluator reports zero unresolved questions; parked threads are
manual-resume only; and non-`c3844` Card Decisions workspaces are historical or
deferred.

Updated the context index, task ledger, and modding pillar so they all agree with
the current c3844 handoff. Custom body/head live render proof is green:
`custom_body_live_render_smoke` passes 41/41 with scripted exit and
`MODASSET.RENDER` for source-backed `example:tri_body`. The active proof is now
live SFX/voice/music playback with scoped logging, followed by Scenario AI
graph/runtime fallback closure, stale generated-output regeneration, private
integer bridge audit, and end-to-end modder workflow proof.

Follow-up verification at 2026-06-11T11:25:25-04:00 checked the actual list-based
card structure, parked-thread metadata, decision-request evaluator, active audit
folder, and preserved Card Decisions workspaces. No additional cards needed to
move or delete. The modding pillar wording was tightened so custom body/head
runtime equivalence is no longer listed as remaining work.

Strict JSON recheck at 2026-06-11T11:34:53-04:00 found the same board structure:
one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred, zero Blocked,
104/104 Done history-only, and only the `c3844` attention flag. The only mismatch
was historical metadata: the answered `c121` decision-request smoke-test question
had `resolved_at` but not an explicit `resolved: true` field. That marker is now
set, so direct JSON checks and the Kanban cleanup metadata agree that unresolved
decision requests are zero.

Requested cleanup verification at 2026-06-11T11:50:04-04:00 checked the live
list-based Kanban records again. No structural board changes were needed:
`c3844` is still the only Active card, all 49 Backlog cards remain priority-5
deferred, all 104 Done cards remain history-only, Blocked is empty, the only
flag is `c3844`, unresolved decision requests are zero, parked threads remain
manual-resume only, and no non-`c3844` Card Decisions workspace is live. The
context index, task ledger, and Kanban root metadata were refreshed to this
latest verification stamp.

## 2026-06-11 - c3844 custom body live render proof green

Finished the B-923 generated custom-body render handoff. The failure was not lost mesh source or a bad archive: the debug-placed bot could reach `chrRender` before normal character matrix allocation populated `model->matrices`, so generated render audit crashed before `MODASSET.RENDER`. `chrRender` now prepares matrices on demand before `modelRender` when needed and logs the narrow `chrRender-matrix-ondemand` audit for the debug bot.

Verification passed: `python tools\asset_native_source_guard.py`; `git diff --check` for the touched runtime/test/smoke files; focused `[modding][pdxxx][c3844][source][static]` (935 assertions / 10 cases); isolated `c3844mat` all-target build; and bounded `custom_body_live_render_smoke` against the isolated client, passing 41/41 with exit code 0, scripted exit, and `MODASSET.RENDER` for `example:tri_body`.

Kanban routing was rechecked after the fix: `c3844` is still the only Active card and the only attention flag, all 49 non-`c3844` Backlog cards remain priority-5 deferred, and no additional card moves or deletions were needed. Next c3844 proof is live SFX/voice/music playback with only relevant logging, followed by the remaining Scenario AI graph/runtime fallback, stale generated-output, private integer bridge, and end-to-end modder workflow gates.

## 2026-06-11 - Dev Window typed archive asset browser/extractor

Added a developer-only typed `.pdxxx` asset browser/extractor for the current c3844 modder-workflow proof. The reusable PowerShell module [pdxxx-asset-tool.psm1](../devtools/pdxxx-asset-tool.psm1) crawls a data folder, lists zip-openable typed archives by type, reports source hints such as `model.obj`, `model.gltf`, `scene.glb`, image counts, and nested `.pdmesh` archives, and extracts selected assets into the ignored `.pdxxx-dev-extracts/` folder. The wrapper [pdxxx-asset-tool.ps1](../devtools/pdxxx-asset-tool.ps1) exposes List, Extract, Open, and SelfTest modes.

Dev Window v2 now has an Assets tab in [dev-window-v2.ps1](../devtools/dev-window-v2/dev-window-v2.ps1) with data-root selection, type filtering, search, multi-row selection, Extract Selected, and Open Output. Per Mike's direction, extraction preserves archive contents as stored. The tool does not translate, convert, normalize, or regenerate mesh/material data. Nested typed archives are kept as their original `.pdxxx` files and also expanded into adjacent `*.pdxxx extracted/` folders only for inspection and Blender visibility testing.

Verification passed: `powershell -NoProfile -ExecutionPolicy Bypass -File devtools\pdxxx-asset-tool.ps1 -Mode SelfTest`; PowerShell parser checks for the module, wrapper, and Dev Window v2 script; listing real Build `.pdmesh` archives filtered by `pdmesh` and `falcon`; extracting `examples\modding\typed-pdxxx-basic\bodies\tri_body.pdbody` with nested `mesh.pdmesh` preserved and `mesh.pdmesh extracted\model.gltf` present; `python tools\asset_native_source_guard.py`; `git diff --check` for touched files; and isolated `.\devtools\build-session.ps1 -Session pdxxxasset -Target all`, followed by `-Remove`.

Immediate follow-up after Mike opened the tab: the grid showed one `System.Object[]` row because `Get-PdxxxAssetList` returned the whole ArrayList as one object for WPF binding. The module now returns one row object per asset. The Assets tab crawl now runs on the background runspace pool and disables Refresh/Extract while scanning, so type changes and Refresh no longer freeze the WPF UI. Verified against `Build\data\ntsc-final\meshes`: `pdmesh` filter returns 733 rows with real `Type`, `Id`, and `RelativePath` values.

## 2026-06-11 - Context/Kanban retention cleanup and c3844 handoff refresh

Mike asked to update the context system fully and remove/defer irrelevant Kanban cards. The live board already had the important routing correct: only `c3844` is Active and `c3844-s105` is the active proof subtask. This cleanup keeps that routing explicit, preserves historical cards in Backlog/Done, and removes old session/audit material from the hot path instead of letting new sessions start from stale context.

Latest requested refresh at 2026-06-11T10:32:19-04:00: the board still needs no structural card moves. Direct JSON and evaluator checks show exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred with `x_deferred`, zero Blocked cards, 104/104 Done cards history-only, only the `c3844` attention flag, zero unresolved decision requests, no live non-`c3844` Card Decisions workspaces, and only the current c3849 measurement audit in `context/audits/`. The active `c3844` handoff was updated to the latest verified state: the MP manifest/stage-diff lifecycle bug is patched and CPU/build verified (`asset_native_source_guard.py`, focused c3844/static tests, `[catalog][checked]`, `[modding][pdmod][static][c3809]`, and isolated `c3844lifefix` all-target build). The bounded `custom_body_live_render_smoke` improved to 36/39 and no longer fails on lifecycle assertions, but remains red with `0xC0000005`, no scripted exit, and no `MODASSET.RENDER` for `example:tri_body`. Next work is generated-body `modelRender` crash/handoff investigation, not card routing or the fixed lifecycle bug.

Superseded requested refresh at 2026-06-11T10:03:00-04:00: the board still needed no structural card moves. Direct JSON and evaluator checks showed exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred with `x_deferred`, zero Blocked cards, 104/104 Done cards history-only, only the `c3844` attention flag, zero unresolved decision requests, no live non-`c3844` Card Decisions workspaces, and only the current c3849 measurement audit in `context/audits/`. This older note captured the now-fixed lifecycle diagnosis: legacy stage diff unloading MP manifest-owned custom body/head/weapon assets. The 10:32 refresh above is the current routing source.

Final context-system refresh at 2026-06-11T09:14:10-04:00: the board still needs no structural card moves. It verifies as exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred, zero Blocked cards, 104/104 Done cards history-only, one attention flag (`c3844`), zero unresolved decision requests, and no live non-`c3844` Card Decisions workspace. Preserved non-`c3844` Card Decisions workspaces now have explicit historical/deferred status fields, and the active audit folder now contains only `migration-utilization-measurement-2026-06-10.md`; older audits are archived under `context/_old/audits/2026/`. Current `c3844` handoff supersedes older same-day breadcrumbs: custom body/head reaches `chrRender-modelRender` with body/head 152, but no `MODASSET.RENDER` or scripted exit appears, custom-slot scanner / `MODELDEF.SOURCE` expectation lines are missing, a late `MANIFEST-SP` missing/unload warning appears, and the smoke exits `0xC0000005`.

Requested verification at 2026-06-11T09:22:47-04:00: the board was checked again for Mike's cleanup request. No additional card moves were needed. Active remains exactly `c3844`; all 49 non-`c3844` Backlog cards are priority-5 deferred with `x_deferred` metadata; Blocked is empty; all 104 Done cards are history-only; the only attention flag is `c3844`; decision requests have zero unresolved questions; parked threads are manual-resume only; and no non-`c3844` Card Decisions workspace is live. `context/README.md`, `context/tasks.md`, and `tools/kanban/state.json` were refreshed to preserve that routing.

Latest requested verification at 2026-06-11T09:32:04-04:00: the board was checked through the real `column` field after the quick display table made blank status fields look suspicious. The structure is still correct: exactly one Active card (`c3844`), 49 Backlog cards all priority-5 deferred with `x_deferred`, zero Blocked cards, 104 Done cards all history-only, the only flag on `c3844`, zero unresolved decision requests, parked threads set to manual resume, and only the current c3849 measurement audit in `context/audits/`. No extra cards needed to move or delete; the context/board metadata was refreshed so this is the latest routing record.

Final evaluator-aligned verification at 2026-06-11T09:36:59-04:00: a direct decision-request scan found one stale `c121` smoke-test question on a Done tooling card because the historical answer used `answered_date` but not the evaluator's `answered_at` field. The question is now closed with `answered_at`, and the board metadata was refreshed. Verified routing state remains one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred, zero Blocked cards, 104/104 Done cards history-only, only `c3844` flagged, zero unresolved decision requests, parked threads manual-resume only, no live non-`c3844` Card Decisions workspace, and only the current c3849 measurement audit in `context/audits/`. No cards were deleted or moved in this final pass.

Current verification at 2026-06-11T09:44:04-04:00: the board and context system were checked again for Mike's cleanup request. Direct JSON parse shows exactly one Active card (`c3844`), 49 Backlog cards all priority-5 deferred with `x_deferred`, zero Blocked cards, 104 Done cards all history-only, and the only flag on `c3844`. The decision-request evaluator reports zero unresolved questions, parked threads are manual-resume only, and `context/audits/` contains only the current c3849 measurement audit. No additional cards needed deletion or movement; the context index, task ledger, session log, and Kanban root metadata now carry this verification stamp.

Canonical context follow-up at 2026-06-11T09:51:54-04:00: the live `context/` tree was rechecked and remains authoritative. Parent-level briefing files exist but are treated as stale convenience mirrors unless explicitly synced from `context/`. Board verification is unchanged: `c3844` is the only Active card, all 49 Backlog cards are priority-5 deferred, zero cards are Blocked, all 104 Done cards are history-only, only `c3844` is flagged, decision requests have zero unresolved questions, and `context/audits/` contains only the current c3849 measurement audit.

Strict deferral follow-up at 2026-06-11T05:59:20-04:00: all 49 non-`c3844` Backlog cards are now priority `5` / Someday and carry `x_deferred` metadata with their prior priority preserved. Done cards are marked history-only, `x_special_notes` and `x_board_cleanup` state that only `c3844` routes work, and the separate parked-thread resume triggers were reset to manual resume after the `c3844` parity closure or an explicit Mike priority change. This means old connectivity, Forge, AllInOne cleanup, Needler, c3848/c3849, benchmarking, tooling, and input items should not wake up future sessions by date or priority drift. Final verification at 2026-06-11T06:00:46-04:00: board JSON parses; Active is exactly `c3844`; Backlog is 49/49 priority-5 deferred; Done is 104/104 history-only; only `c3844` is flagged; parked entries are manual-resume only; and decision requests have 0 unresolved questions.

Final context-system refresh at 2026-06-11T06:33:55-04:00: no further card moves were needed. The board still parses with exactly one Active card (`c3844`), zero Blocked cards, 49/49 non-`c3844` Backlog cards carrying priority-5 `x_deferred` metadata, 104/104 Done cards marked history-only, and only the `c3844` attention flag. Context routing remains narrowed to `c3844` until Mike explicitly reopens another card or folds that work into the parity closure.

Fresh requested board/context verification at 2026-06-11T08:26:09-04:00: the live Kanban still parses cleanly with exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred, zero Blocked cards, 104/104 Done cards history-only, only the `c3844` attention flag, zero unresolved decision requests, and no non-`c3844` card workspaces marked active/not-started/in-progress. No additional cards needed to move; the context system and board still agree that unrelated work is parked until Mike reopens it or folds it into `c3844`.

Requested context-system refresh at 2026-06-11T08:47:25-04:00: the board needed no new structural moves. It still has exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred with `x_deferred` metadata, zero Blocked cards, 104/104 Done cards history-only, only the `c3844` attention flag, zero unresolved decision requests, and no non-`c3844` live workspaces. The cleanup corrected stale routing text in `tools/kanban/state.json` and the rendering pillar that still pointed to the older smoke staging/log-mask regression. Current handoff is now consistent everywhere: staging and log mask are restored; `custom_body_live_render_smoke` reaches slot-152 `chrRender-modelRender`, but render-time state shows `head=-104`, `hidden=0x100000`, `chrRender-alpha-skip`, no `MODASSET.RENDER` for `example:tri_body`, and a `0xC0000005` exit. Continue the final character render-state/modelRender handoff before moving to live SFX/voice/music proof.

Latest context/Kanban refresh at 2026-06-11T09:00:39-04:00: the board again needed no structural moves. It still has exactly one Active card (`c3844`), 49/49 Backlog cards priority-5 deferred, zero Blocked cards, 104/104 Done cards history-only, only the `c3844` attention flag, zero unresolved decision requests, and no non-`c3844` live workspaces. The live handoff was corrected to the newest evidence from the in-flight head-width fix: `head=-104` is no longer current. Root cause was `struct chrdata.headnum` being signed 8-bit while the private custom head slot is 152; it is now `s16`, with static coverage, `asset_native_source_guard.py`, focused body/head tests, and isolated `c3844head16` all-target build passing. The latest `custom_body_live_render_smoke` is still red: it fails 30/36 with `0xC0000005`, no `MODASSET.RENDER` for `example:tri_body`, no scripted exit, missing custom-slot scanner / `MODELDEF.SOURCE` expectation lines, and a late `MANIFEST-SP: load failed 'example_typed_pdxxx_basic' -- asset missing` plus unloads in the log. `BOT.RENDER.AUDIT` now keeps `head=152` through `chrRender-modelRender`, so the next session should inspect the manifest/source lifetime or generated-body `modelRender` handoff rather than chasing the fixed signed overflow.

Latest c3844/B-801 handoff correction: the direct body/head registration bridge was build-verified, then `custom_body_live_render_smoke` exposed a separate stage-load catalog-health failure at `catalogGetBodyIsComplete bodynum=92`. That slot-92 failure is now fixed: the loader pool had sparse custom body/head slots but `loaderPoolGetBody` / `loaderPoolGetHead` returned zeroed active records for unparsed base slots instead of NULL, preventing the manager fallback to authored base body/head data. Per-slot populated flags now guard those getters, and the fix passed `asset_native_source_guard.py`, focused `[catalog][checked]`, and isolated `b92pool` all-target build.

B-920 archive/compile/allocation follow-up is now fixed and verified. Checked-in typed body/head examples now embed nested `.pdmesh` archives with `model.gltf`, `model.mtl`, hierarchy/parts/faces/render JSON, and matching descriptor/manifest keys. Body/head loader walkers resolve OBJ, GLTF, or GLB nested mesh source. The generated modeldef compiler now handles explicit wildcard group `"-"`, applies character skeleton/root defaults for body modeldefs, and generated-source body recognition no longer depends on zero parts. Verification passed example regeneration, all-family example conformance, native-source guard, focused archive/provider/source static tests, and isolated `b920sidecar` all-target build.

The latest bounded `custom_body_live_render_smoke` still is not green, but the failure point moved past extraction and compile. The log proves custom body/head slots register, debug bot appearance is applied, `example:tri_body` compiles from public source with 3 vertices / 1 triangle / `SKEL_CHR`, the catalog activates the external body modeldef, and bot allocation receives non-null custom modeldefs. Remaining active blocker: render diagnostics still only see `base:dark_combat` during the smoke window and no `MODASSET.RENDER` line appears for `example:tri_body`. Next work should adjust or trace the smoke so a custom body is actually in the audited render path, then move to live SFX/voice/music playback proof.

Latest manager-lifetime follow-up in this same handoff: the custom body/head `reason=unpopulated` failure is fixed. Root cause was boot order: external mod/component scan registered custom body/head manager records, then `catalogManagerHeadInit()` / `catalogManagerBodyInit()` ran afterward and cleared the custom slots. Manager init now runs before external component scan, and custom manager slots no longer fall through to authored fallback when the loader pool is inactive. Verification passed `asset_native_source_guard.py`, `[catalog][checked]`, `[modding][pdxxx][c3844][source][static]`, and isolated `c3844life2` all-target build.

The bounded `custom_body_live_render_smoke` is still red, but the failure moved forward: no catalog miss/unpopulated/fallback/source-only/fatal/AV signatures remain; `example:tri_body` / `example:tri_head` register at slot 152, match setup and `CHR.DIAG: botAlloc` use body/head 152, and `example:tri_body` compiles/activates from public GLTF source. Remaining active blocker is final render-audit handoff: diagnostics still report `base:dark_combat` and never emit `MODASSET.RENDER` for `example:tri_body`.

Follow-up placement timing probe at 2026-06-11T06:19:14-04:00: the debug bot placement hook was moved to after normal prop/scenario ticks and before `propsSort`, forces the bot prop enabled/onscreen for 180 frames, and is pinned by focused static tests. Verification passed `asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]`, and isolated `c3844bodyrender` all-target build. The live `custom_body_live_render_smoke` still failed 26/32 and exited before scripted exit: the log proves the hook consumed in player room 57 and normal generated model render diagnostics fired for base props, but no `MODASSET.RENDER` line appeared for `example:tri_body`. Next session should instrument or trace `propsSort` -> `propsRender` -> `chrRender` -> `modelRender` for the slot-152 bot; the failure is now render visibility/audit handoff, not extraction, sidecar, slot, or source compilation.

Render handoff trace follow-up at 2026-06-11T08:16:36-04:00: a scoped `BOT.RENDER.AUDIT` trace was added around the debug-placed bot so future live runs can prove `placed`, `propsSort`, `propsRender`, room assignment, `chrRender`, and `modelRender` handoff stages without enabling broad logging. Verification passed `asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]`, and isolated `c3844trace` all-target build.

Smoke-runner/staging correction follow-up: the earlier `channel_mask=0x0000` / missing staged mod run was caused by the smoke runner using stale shared binary discovery after a queued isolated build. `tools/smoke-verify/run.ps1 -Build` now binds the install to `.claude/session-builds/<session>/PerfectDark.exe` when no explicit source binary is provided, and the focused B-801 static test pins that behavior. Verification passed `asset_native_source_guard.py`, a PowerShell parser check for `run.ps1`, focused `[modding][pdxxx][c3844][source][static][b801]`, and a fresh bounded `custom_body_live_render_smoke` build/run using `.claude/session-builds/c3844smoke/PerfectDark.exe`.

The latest `custom_body_live_render_smoke` still is not green, but it now reaches the real render handoff. The log has `channel_mask=0x0B87`, the staged `example_typed_pdxxx_basic` mod loads, `example:tri_body` / `example:tri_head` register at slot 152, debug bot appearance is applied, bot allocation starts with body/head 152, and `example:tri_body` compiles from public `.pdbody::mesh.pdmesh::model.gltf` source with 3 vertices / 1 triangle / `SKEL_CHR`. The failure is later: the smoke failed 28/36 and exited with `0xC0000005`; `BOT.RENDER.AUDIT` proves the placed bot reaches `propsSort-included`, `propsRender-call`, and `chrRender-modelRender`, but render-time state shows `head=-104`, `hidden=0x100000`, an earlier `chrRender-alpha-skip`, and no `MODASSET.RENDER` for `example:tri_body`. Next session should trace why the allocated custom head/body state mutates before render and fix the character render-state/modelRender handoff before moving to live audio proof.

Retention cleanup: older session-log entries before 2026-06-07 were moved to `_old/session-log/sessions-before-2026-06-07-archived-2026-06-11.md`. Active context now starts from the current asset-parity push instead of a multi-megabyte mixed historical log.

Kanban cleanup rule remains: do not route sessions to `c3840`, `c3845`, `c3846`, `c3847`, `c3848`, `c3849`, or older gameplay/tooling cards while the all-asset parity closure remains active. Their history is retained, but the only active front is `c3844`.

Follow-up board refresh in this session: all 49 non-`c3844` Backlog cards now carry an explicit deferral note, no non-`c3844` card has an attention flag, and `tools/kanban/state.json` records the routing rule in both `x_special_notes` and `x_board_cleanup`. No cards had to move because the board already had exactly one Active card: `c3844`.

Final board/context hygiene pass: the live board was rechecked at 2026-06-11T03:26:28-04:00 and still has 1 Active (`c3844`), 49 Backlog/deferred, 0 Blocked, 104 Done, and only the `c3844` attention flag. The root board note now states that non-`c3844` cards, including c3848/c3849/Needler/networking/tooling/input/benchmarking, must not route sessions unless Mike changes priority or folds the proof into c3844. Empty card-specific workspaces (`c3820`, `c3823`) were marked deferred to prevent stale placeholder context from looking like live work.

Consistency cleanup after the refresh: older audits were moved out of the active audit directory into `context/_old/audits/2026/`, pre-2026-06-07 session history was archived under `context/_old/session-log/`, and `B-920` in `context/bugs.md` was corrected from an open nested-sidecar blocker to a fixed bug with the remaining live render proof tracked under `c3844-s105`.

Follow-up context-system pass: the preserved `x_card_task_context.cards.c3824` archive-decision workspace no longer reports `not_started`; it is marked completed/historical with no next action beyond continuing `c3844`. This keeps approved archive decisions available without letting a stale card workspace become a routing target.

Final requested board/context refresh at 2026-06-11T04:31:50-04:00: `tools/kanban/state.json` parses cleanly and still has exactly 1 Active card (`c3844`), 49 Backlog cards explicitly deferred, 0 Blocked cards, 104 Done cards, and only the `c3844` attention flag. No additional cards needed to move. The context index, task ledger, modding pillar, and board root notes all agree that non-`c3844` cards are parked unless Mike changes priority or folds their proof into `c3844`.

Final metadata correction at 2026-06-11T04:50:04-04:00: preserved archive-decision text in `tools/kanban/state.json` no longer describes Scenario source as `OBJ/TSV/setup`; it now uses OBJ/GLTF/JSON/setup wording and explicitly records that public TSV is not a valid final source contract. This keeps the historical `c3824` decision workspace available without contradicting the current c3842/c3844 no-public-TSV rule. Board counts and routing remain unchanged: only `c3844` is Active and flagged.

Requested refresh verified at 2026-06-11T04:58:04-04:00: the board still has exactly 1 Active card (`c3844`), 49 Backlog cards, 0 Blocked cards, 104 Done cards, and only the `c3844` star. Every non-`c3844` Backlog card already carries an explicit 2026-06-11 deferral note, no non-`c3844` card task workspace is marked active/not-started, and decision requests have 0 unresolved questions. No additional cards needed to move; the context and Kanban source of truth now agree that all other work is parked unless Mike changes priority or folds it into `c3844`.

---
## 2026-06-10 - c3849 Waves 5+6 COMPLETE; utilization program at 100% of pre-live scope

Final session entry for the ultracode "finish asset extraction and utility to 100%" directive. Everything implementable before the B-801 live gate is now shipped, build-verified, and committed:

- Units 2+9 (1e7d0c01): settings/variables/presentation parse (dual spelling/shape, unit enforcement), $name variable substitution at compile (loud unresolved), defaults layering onto held records (fire_cadence rpm -> max_rpm ONLY for auto functions / recoverytime for others - the has_max_rpm bit classifies automatics at bondgun.c:2068, layering it blindly would flip single-shots to auto), presentation_file at all three mirror sites, moddinghub scalar zoom_fov fix (B8), camera_effect xray consumer in the bgunTick vision arm (parse-latched mode, no per-tick strcmp), MPOPTION_WEAPONGRAPH 0x20000000 (host ORs the WIRE COPY only when toggle on; client latches + restores at stage end AND netDisconnect; masked at the single mplayer.c save site - verified the only options serialization path). Needler regenerated with canonical schemas.
- Unit 8 (20532a64): full .pdeffect runtime per the Wave 6b map. Needler nesting evidence: the pink effect is at level TWO (weapon -> projectile -> effect), so the typeForNestedArchiveName fix alone was insufficient; s_registerEmbeddedEffectDeps second-level scan added (mesh-ingest precedent). Spark finding: g_SparkTypes is one 27-row compile-time PAL/NTSC table, runtime-mutated by OG recolor code - registry is append-only, custom rows >= 27, SPARKTYPE_BASE_COUNT pinned by _Static_assert.
- Wave 7 staging recorded (kanban s7 + catalog pillar): toggle default flip, per-family fatal cutover, strict MP refusal (protocol bump + test_versions pin), toggle retirement. NOTHING further is implementable pre-live.

Cross-cutting facts for future sessions: binding specs B1-B8 in c3849-wave-implementation-maps.md are the weapon-graph consumer contract (arm ordering, unified explosion/spark vocabulary, param-presence guards - base graphs carry records with EMPTY params); the pre-existing weapon_id=-1 scanner pin failure (c028) is the only standing red in [weapon_graph]; [c3849] closed at 985 assertions / 52 cases green, [effect_graph] 235/8, [net][lifecycle] 417/18.

Tactical decisions taken this session (B6 in the maps doc; flag to Mike): $name syntax, canonical schema spellings (base wins; needler converged), MPOPTION masked-at-save (transient), fire_cadence unit rpm, trajectory_max_angle degrees, wall_post_fall_timer60 + lost-target detonate + non-parity policy vocabularies deferred, pink-spark in scope.

NEXT SESSION: Wave 7 at Mike's live-test time (B-801): enable Debug.WeaponGraphRuntime, run the Needler full-chain proof (render via B-911 chain, fire, homing track, pink contact burst via Unit 8), then the staged flips. The c3844 umbrella gates 1 (live proofs) and 6 (end-to-end modder workflow proof) remain the program-level closers.

## 2026-06-10 - c3849 Wave 5 dead-IR consumers: Units 0-7 SHIPPED (checkpoint)

Ultracode session continuing c3849 to 100%. 8-agent mapping round + cross-map critic produced binding specs B1-B8 (weaponTick custom arm ordering, unified explosion/spark ref resolver, param-presence guard discipline, 10-unit order) preserved in context/designs/catalog/c3849-wave-implementation-maps.md (b32833a3). Landed, each build-verified (client -Target all PASS, [c3849] green, only the pre-existing weapon_id=-1 scanner pin failing in [weapon_graph]):

- Unit 0 (34f3a6e5): B-915 timed-timer zero-guard (base Timed Mine first-tick detonation toggle-ON), B-916 descriptor *_file alias drift (shared-context.json silently dropped from every walker-path held IR), B-917 dangling projectile targetprop (+ propagation to prop.c TICKOP_FREE and forge exit-play). B-919 recorded open (propobj.c:7928 decomp quirk).
- Phase I parallel (06e3cac1): Wave 6a meta-family consumers (botprofile/gamemode/theme - first assetRuntimeFind* gameplay consumers, value-identical, RUNTIME_MISS fallbacks; theme ordering finding recorded) + Wave 4 Slice B (TEXTURE.BIND stat-only boot verification, slug unification into assetcatalog_slug.h, B-918 font/lang pin repair).
- Units 1+3 (c061a4d2): substrate (custom-slot helper family, weaponGraphResolveExplosionRef vocabulary, effect_graph_runtime.h bridge stubs, ALL parse-time derived/latched fields + sentinels, s16 ticks240 clamps) + guidance (per-projectile homing gains with OG PD-controller verbatim fallback, trajectory_max_angle + FUNCFLAG_CALCULATETRAJECTORY closure, fbw numerics at player.c + rocketTickFbw, AI launcher widen + chraction.c wrong-owner fix).
- Units 4+5 (bb0c58ad): combined custom projectile arm per B1 (wall-hugger state machine -> contact-impact -> fuse timer), stick-gate restructure (sticky_allow_*, impact_stick_on_hit absorbed, wall-hugger latch widen), timer_starts on_impact/on_attach seeds, bounce limit/rest/friction/rotation, impact_filter_mode/hit_sound/spark_ref/explosion_ref with WAVE6-EFFECT-HANDOFF seams, graph trails on the free smoketimer240 cadence.
- Unit 6 (96925f3a): custom entity arm (remote w/ verbatim coop/anti masks + detonator provenance via g_PlayersDetonatingWeaponnum, timed w/ on_expire modes, proxy w/ filter modes + cdTestLos05 LOS on radius-pass, storm w/ owner transfer), objDamage damage_response, objFree custom unregister (dangle fix), weaponGraphEntityRemoteSignalMatches pure predicate.
- Unit 7 (ee058887): g_ThrownLaptopLatch sidecar (B-323-safe, no autogunobj growth - setup-segment reinterpretation confirmed), autogun cadence/alternate/beam/ffsuppress, transition-to-entity (first GetEntityForProjectile production caller; bondgun laptop-gate widen; ammo literals -> carrier weaponnum), owner-cleanup (g_PlayersOwnerCleanupPending set at playerDieByShooter; replace policies; alarmTick laptop pass), sticky-device (landing flags, sticky_visible_state wired to OBJFLAG2_INVISIBLE - render consumer verified, pickup policy), interaction (sounds, recover weaponnum/ammo policy, interact filter).

All consumers dormant behind Debug.WeaponGraphRuntime via the *ForGameplay accessors + custom-slot guards; base behavior bit-identical both toggle states (param-presence discipline per B3). NEXT: Unit 8 .pdeffect runtime (completes the bridge stubs + pink spark registry), Units 2+9 settings/variables/presentation + camera_effect + MPOPTION_WEAPONGRAPH bit, then Wave 7 staging + session close.

## 2026-06-10 - c3849 Wave 4 TEXTURE emitter SHIPPED (audit premise corrected)

Agent-implemented Slice A, independently re-verified (client PASS empty err; 11,532/40; guard ok). The measurement audit's "no base texture emitter exists" was STALE: s_emitTexture lived inside romextract_pdmeta.c and 3,503 installs already carry texture.png. The real gaps were structural: extracted it to romextract_pdtexture.c with its own BOOT_PHASE_EMIT_TEXTURE and a WORKING fast-cache stamp (pdmeta wrote a stamp it never read; the new kind is deliberately <64 chars because romextract_pd_cache.c's reader fscanf %63s truncates -- pdmeta's own 66-char kind can never round-trip, flagged). Agent evidence: conformance 3,503/3,503 PASS; fresh boot written=3503 failed=0; warm boot fast-cache skip removes ~7,000 zip opens; boot smoke shows live texture intercepts (tex=427 queries). c3843 static pins re-pointed to the new file + negative pin on pdmeta. Slices B/C remain as map follow-ups.

## 2026-06-10 - c3849 Wave 3 FONT SHIPPED: the 0%-utilized family now consumes public source

Agent-implemented from the preserved map (context/designs/catalog/c3849-wave-implementation-maps.md), independently re-verified (client PASS, empty err; [catalog][provider][static]+telemetry+stage-slots 11,584/49; guard ok). Stage 1 repaired the doubly-broken emitter (face->segname table fixes the zero-NTSC-archives bug; the export parser now reads the POST-preprocess PC-native segment layout field-by-field instead of raw N64 BE; cache-kind v2 + schema-2 gate force regeneration; stale jpn garbage archives deleted at emit). Stage 2 added fontcatalog.{c,h} (PGM + metrics JSON -> segment-shaped buffer; build-variant-safe struct fontchar fill; CI4 (v+8)/17 repack; +16-byte tail pad for the LoadBlock over-read; width>16 rejected only when real pixels overflow -- handelgothiclg's W advance=17 falsified the blanket rule) and made textLoadFont catalog-first with ALL post-load mutations unchanged; miss = LOUDFAIL + assetFallbackRecord(ASSET_FONT). The agent also proved a lossless round-trip of all six real font segments and fixed two latent Wave-2c fresh-build breaks (guard-less generated-header include in anim_slots.h; missing lib/anim.h in mod.c). JPN glyph banks deliberately stay on the segment path.

REMAINING: Wave 4 base-texture emitter, Wave 5 dead-IR consumers, Wave 6 meta/.pdeffect, Wave 7 live flips (B-801). Plans preserved in-repo.

## 2026-06-10 - c3849 100% utilization program: Waves 1 + 2 (ALL FOUR ALLOCATORS) SHIPPED

Waves 1 and 2 are complete and build-verified (commits 421ba04c telemetry, 94ede922 soundnum, 361ee0be texnum, a9ccf25a animnum, 3b4de682 stagenum). Net-new custom content is now reachable at runtime in all four previously-impossible families, each via the proven catalog-owned private-slot pattern with loud CATALOG.<FAM>.CUSTOM_SLOT_FAIL, reset wiring (all 3 assetcatalog.c clusters now cover all 7 allocator families -- a latent early-out-cluster gap was found and fixed in 3b4de682), allocator unit tests, and zero wire/save identity exposure. Key family-specific facts: sound = no native bank growth (file playback; 11-bit soundnum ceiling); texture = g_Textures +0x40 zero-init rows + 5 gates raised (the texLoadFromConfigs relocation-else was the corruption hazard; 12-bit ceiling); anim = table+arrays grown, catalogSeedCustomAnimRows seeds custom rows with the 0xffffffff sentinel (boot via animsInit, reloads via catalogLoadInit), clip-install/restore bounds on new animGetTotalCount(); stage = mint 0x60..0x80 (7-bit save field), arena keyed to scenario_id, idempotent stageTableAppend rows with fileids=0 (u16! -1 wraps past the >0 handle guard), save-load guard resets machine-local stagenums.

NEXT (plans preserved in context/designs/catalog/c3849-wave-implementation-maps.md): Wave 3 FONT (two-stage: emitter repair -- wrong segment names AND wrong byte layout today -- then a ~300-line PGM+metrics->struct-font compiler at textLoadFont), Wave 4 base-texture emitter, Wave 5 dead-IR consumers, Wave 6 meta/.pdeffect, Wave 7 live-gated flips (B-801).

## 2026-06-10 - c3849 100% utilization program: Waves 1 + 2a SHIPPED

Mike's ultracode directive: use the measurement audit (context/audits/migration-utilization-measurement-2026-06-10.md) to drive to 100%. Program card c3849 opened (7 waves; Wave 7 = the live-gated flips staged to B-801). Mapping workflows: wkdkezz7v (Wave 1 telemetry + 4 allocators, 5 agents), wmi19i7uk (Wave 3 font + Wave 4 texture emitter, 2 agents -- KEY FINDING: the .pdfont emitter is doubly broken today: wrong segment names (face vs fontface -> zero NTSC archives) AND wrong byte layout (parses raw N64 BE but segs/*.bin are post-preprocess PC-native), so Wave 3 = emitter repair + a ~300-line PGM+metrics->struct-font compiler at textLoadFont; full plan in the workflow output).

SHIPPED Wave 1 (`421ba04c`): asset_fallback_telemetry.{c,h} -- O(1) per-family counters, first-offender snapshot, quiet-when-zero ASSET.FALLBACK aggregate consumed at the lv.c stage-load checkpoint beside catalogAssertHealthy. 15 sites instrumented across mod.c/snd.c/setup.c/tilesreset.c/bg.c/bondgun.c/romdata.c -- abnormal branches ONLY (healthy Pass-C cache reads + uncataloged base routing deliberately uncounted; scenario records gated on scenarioSourceFindEntryForStage != NULL); two formerly-silent sites upgraded to LOUDFAIL.FALLBACK (MP3 raw-ROM, extracted-cache-missing). Client PASS; [catalog][fallback][telemetry] 14/4.

SHIPPED Wave 2a soundnum (`94ede922`): assetcatalog_sound_slots.{c,h} (SND_CUSTOM_* = 0x60A + 0x40, 11-bit soundnum ceiling _Static_assert); allocation at scanner + netdistrib ASSET_AUDIO ingest (gate: sound_id<0 && !MUSIC && file primary; MUSIC excluded because sound_id doubles as tracknum); slot -> ext.audio.sound_id + source_soundnum so the existing reverse index + file-playback chain works with NO native bank growth; snd.c bank-overlap guard; heldResolveSfxParam now rejects sound_id<0 (was silently playing SFX_0000). Client PASS; [catalog][sound][slots] green (168/16 combined run).

REMAINING per the maps (all plans in the wkdkezz7v/wmi19i7uk outputs, file:line-anchored): Wave 2b texnum, 2c animnum (needs custom anim-table row store), 2d stagenum (stageTableAppend has zero callers; mind the Index Domain Warning + wire analysis), Wave 3 font (two-stage: emitter repair + runtime compiler), Wave 4 base-texture emitter (decode via texdecompress at extract time), Wave 5 dead-IR consumers, Wave 6 meta/effects, Wave 7 live flips (B-801).

## 2026-06-10 - BUILD VERIFIED: the full Needler/B-911/B-912/B-914 stack is green

Mike granted the build-session wrapper permission (the blocker was a session-scoped PowerShell deny the auto-mode classifier honored; his settings files never had one -- the unblock was explicit allow grants via /permissions). Full verification of the six stacked commits:

- Client `-Target all` PASS (24s full; 6s incremental re-verify), empty error log -- the additive `g_ModelStates` growth, allocator, ingest consumer, homing widen, and impact slice all compile and link.
- ONE compile fix needed: `weapon_graph_runtime.c` never called `sysLogPrintf` before the B-911/B-912 ingest, and the client PCH masked the missing `system.h` include; pd-tests (no PCH) caught it. One-line include added.
- `[catalog][model][slots]` + `[modding][pdxxx][model_slots]`: PASS 119 assertions / 10 cases, with the loud-fail channels (CATALOG.MODEL.CUSTOM_SLOT_FAIL, WEAPONGRAPH.MESH.SCAN namespace/no-id skips) visibly firing in the run output.
- Broad regression band (`[modding][pdxxx]`, `[catalog][bodyhead][slots]`, `[catalog][provider][static]`, `[catalog][checked]`, `[net][lifecycle]`): 173/178 cases, 19750/19755 assertions. The 5 failures are PRE-EXISTING static source pins (romextract_pdweapon.c, scanner, scenario renderer, fonts) on files last changed in `23914afb` (2026-06-09, before this session) -- zero regressions from this stack; they belong to the existing pre-existing-failure triage card.

Remaining for the Needler/parity arc: ONLY live verification (B-801-gated, Mike's call): enable `Debug.WeaponGraphRuntime`, equip the Needler, confirm render (B-911 chain), fire (spawn bridge), tracking (B-914 + tracktype 3), and contact-burst (B-912 + EXPLOSIONTYPE_PHOENIX). Build procedure recorded in permanent memory so no future session re-litigates the broken paths.

## 2026-06-10 - Needler gameplay halves: homing widen + impact first slice (c3847, B-912/B-914)

Continued the goal after B-911. A 2-agent trace workflow (wv26yix9i) grounded both gameplay halves end-to-end; headline discovery: the Needler could not FIRE at all on the player path -- `gsetGetWeaponFunction` returned NULL (the custom pool weapondef had no `functions`), and the whole graph gameplay surface is behind `Debug.WeaponGraphRuntime` (default OFF, `weapon_graph_runtime.c:41/149`). Implemented three units (all build-pending; all dormant until the toggle is enabled, so base content is bit-identical):

1. Spawn-path DATA bridge: the Needler weapon manifest now authors a `functions` pair-array (`weaponfunc_shootprojectile`, type 513, ammoindex -1 -- dispatch skeleton only; the behavior graphs override every ballistic value via the FromGraph helpers) + `aimsettings.tracktype = 3` (latched OG-launcher lock-on so `bondgun.c:5868`'s generic targetprop assignment hands the needle a live target). Builder rebuilt deterministically; mod v0.3.0; conformance 1 root -> 12 archives green; guard green.
2. B-914 homing widen (Q1): new `PROJECTILEFLAG_HOMING` latched from `runtime->has_homing` in `projectileApplyGraphRuntime` (all 5 spawn sites); `projectileTick` steering gate honors OG `WEAPON_HOMINGROCKET` (first, verbatim) OR the flag OR `gsetHasFunctionFlags(FUNCFLAG_HOMINGROCKET)`. Deferred + recorded: AI launcher-branch widen (must also fix the wrong-owner else at `chraction.c:10371`), per-weapon steering gains (parsed, unconsumed; shared PD-controller statics), dangling targetprop (inherited OG).
3. B-912 impact first slice: `weaponGetContactImpactGraph` (custom-slot guarded -- prevents base-emitted graphs double-handling OG arms) consumed at 4 `propobj.c` sites mirroring the rocket machinery: weaponTick detonation (`EXPLOSIONTYPE_PHOENIX`, the small OG class per Q2; safe for pickups, timer240 inits -1 -- verified in `weaponCreateProjectileFromGset`), prop-hit consume (graph `damage` when authored), BG-hit consume, point-blank parity. Per the cutover plan's closure rule (feed existing OG routines). Deferred: stick_on_hit, spark_ref, custom flight-timer expiry, effect-graph explosion_ref, MP toggle parity.

Static pins added to the propobj guard block (`weaponGetContactImpactGraph`, `impact_consume_on_hit`, `PROJECTILEFLAG_HOMING`, the gset homing clause). Also fixed Mike-authorized: the dispatch-state-freshness hook false-positives (checker session-attribution strict+symmetric; hook-order correction so the same-Stop turn-end marker is not used as this turn's boundary) -- scripts in `~/.claude/scripts/`, no repo commit.

## 2026-06-10 - B-911 ingest consumer: embedded meshes resolve to custom slots (c3848 Slices 2-3)

Continued the autonomous goal after the foundation commit. A 3-agent API-map workflow (w6cid0dyo) grounded the consumer; its headline finding DISSOLVED the materialization fork: `fs.c::fsExtractNestedArchiveChain` (verified by direct read, fs.c:129-240) recurses on every `::` for loose on-disk archives -- the "single-level `::`" limit is modvfs-MOUNT-only (zipped .pdmod resolution), and the dev-mod `.pdweapon` is a loose tier file. So the doubly-nested needle mesh needs NO disk extraction: bind `<weapon>::<projectile>::<mesh>::<geometry>` directly (the shipping `.pdbody::mesh.pdmesh::model.obj` chain is the 2-level precedent). Source-only guard passes (FileProvider handle, dev-mods/ path); `.gltf` routes through the external-source modeldef compiler.

Implemented (BUILD-PENDING, no build access this session): (1) pure scan `weaponGraphArchiveScanEmbeddedMeshesBytes` in `weapon_graph_archive.c` -- enumerates a nested payload's in-memory `.pdmesh` members via `modArchiveMemForEachEntry`/`modArchiveExtractMemAlloc`, reads declared `catalog_id` + `model_file` from `mesh.ini` (`[mesh]`-gated local parser; ids never derived), parent-namespace gate, dedup, loud-skip channel `WEAPONGRAPH.MESH.SCAN:`. (2) thin wiring `s_registerEmbeddedMeshDeps` in `weapon_graph_runtime.c`'s dependency walk, called per-payload after byte-extract and BEFORE the payload IR compiles/registers (so `projectileRuntimeFromNode` -> `heldResolveProjectileModelRef` -> `catalogResolveModel` sees the populated `runtime_index`): register ASSET_MODEL row (fresh entries default to the mod profile; category inherited from the parent weapon's row -- deliberately NOT `loaderWalkerMarkBaseArchiveEntry`, which would mismark base/bundled and survive `assetCatalogClearMods`), `runtime_index = assetCatalogResolveModelPrivateSlot(id)`, `catalogSetPrimaryFile` with the multi-level chain, all failures loud-skip (`WEAPONGRAPH.MESH.INGEST:`).

Testing reality discovered and worked around: pd-tests structurally STUBS the catalog (`stubs.c assetCatalogResolve -> NULL` is load-bearing for netmanifest tests; base model refs in the existing weapon-graph tests resolve via the hardcoded alias table in `heldResolveProjectileModelRef`, NOT the catalog). So the full `has_projectile_modelnum`-in-custom-range chain cannot be asserted in pd-tests. Pinned the testable seam instead: 3 new TEST_CASEs (`[modding][pdxxx][model_slots][c3848]`) for scan discovery, geometry default, namespace gate, no-declared-id skip, dedup-by-id, and non-archive robustness; added `assetCatalogRegister`->NULL + `catalogSetPrimaryFile` no-op stubs so the existing walk tests exercise the ingest inert. Allocator behavior remains pinned by `test_model_slots.cpp`. Full chain = review + client build + the B-801-gated live render (s4, the only c3848 step left after Mike's compile).

## 2026-06-10 - B-911 custom-model slot allocator: foundation (c3848, under c3844)

Mike set an autonomous `/goal` ("complete that, expand scope as necessary") then "ultracode complete the goal" after the Needler verification surfaced B-911 (custom embedded mesh/material/texture never reach the catalog -> custom needle does not render). Two design workflows (wp36kfcp7 root-cause, wf3hlqwrf implementation-map) established: the root cause is a MISSING catalog-owned custom-model runtime-slot allocator (catalogResolveModel returns out.modelnum = runtime_index; a fresh embedded mesh gets runtime_index = -1; no allocator exists -- the C52 bridge bodies/heads got in B-909, models never got). Mike chose the B-909 pattern. New card c3848 under the c3844 100%-parity umbrella.

FOUNDATION IMPLEMENTED (build-pending -- the PowerShell build wrapper is denied by the auto-mode classifier this session, so this is delivered for Mike to compile per the AI-cannot-compile constraint; self-reviewed against the full diff):
- `constants.h`: `MODEL_CUSTOM_COUNT 0x20` / `MODEL_CUSTOM_START NUM_MODELS` / `MODEL_CUSTOM_END (NUM_MODELS + MODEL_CUSTOM_COUNT)` (derived from NUM_MODELS so the JPN ternary is tracked).
- New `port/src/assetcatalog_model_slots.c` + `port/include/assetcatalog_model_slots.h`: mirrors `assetcatalog_body_head_slots.c` (dedup-by-id `s_allocate`, loud `CATALOG.MODEL.CUSTOM_SLOT_FAIL` on exhaustion, reset). Globals-free, added to the pd-tests block (constants.h is already in that target via test_mod_external_archive_static.cpp).
- `g_ModelStates` grown additively to `MODEL_CUSTOM_END` at `data.h:338`, `general.c:404` (base init rows unchanged; 32 trailing slots zero-init), `server_stubs.c:100`. Runtime sites that index by a custom slot grow to TOTAL: `setup.c:1735` stage-init NULL-clear (REQUIRED -- else a custom slot's stage-scoped modeldef pointer survives a stage change into a recycled pool = UAF) and `prop.c:1678` bound. All `romextract_*` + base-registration loops correctly stay at NUM_MODELS; dead `g_GexModelStates`/`g_Goldfinger64ModelStates` untouched.
- Reset wired at all 3 `assetcatalog.c` sites beside the weapon/body-head resets. `tests/test_model_slots.cpp` (mirror of the body/head test) + CMake entry.

The foundation alone is additive headroom + a dead-but-tested allocator (no caller yet, so zero behavior change for base content). The ingest consumer (s2: mesh-first two-pass peel of the doubly-nested embedded mesh + materialization to a public source handle, since the model loader + source-only guard demand a public FileProvider path and modvfs `::` is single-level) is the next slice.

## 2026-06-10 - Needler authoring complete + adversarial runtime verification (c3847)

Resumed the multi-phase `/goal` after Mike's PC updated mid-work. The in-flight unit was the Needler weapon mod (phase 4). `tools/build_needler_mod.py` was already written and run (uncommitted); confirmed it is deterministic (stable SHA-256 across rebuilds), strict conformance passes (`--root dev-mods/needler`: 1 root -> 12 archives across `.pdweapon/.pdprojectile/.pdmesh/.pdmaterial/.pdtexture/.pdeffect`), and the native-source guard is clean.

Conformance only proves the archive SCHEMA, so I ran a 4-way adversarial static verification (subagents reading the live C source, since live boot is B-801-gated) of the builder's runtime-alignment claims. RESULT: the authoring/parse layer is fully sound -- every weapon + projectile node-kind is registered in `weapon_graph_runtime.c` `s_modules`; every authored param key is read (`heldFunctionFromNode` / `projectileRuntimeFromNode`); `shoot_projectile` -> `INVENTORYFUNCTYPE_SHOOT_PROJECTILE`; the impact op reads the UNPREFIXED `explosion_ref`/`spark_ref` JSON keys (the builder's comment was right -- not dropped); a custom `.pdweapon` binds to a private `WEAPON_CUSTOM_START`/`MPWEAPON_CUSTOM_START` slot; the loose dev-mod folder is discovered. Homing gap (Q1) and effect-graph gap (Q2) confirmed real + accurately declared.

The verification's payoff was three runtime gaps the prior design note did NOT list, now recorded: **B-911** (the weapon-graph archive walker's `typeForNestedArchiveName` ingests only `.pdprojectile`/`.pdentity`, so embedded `.pdmesh`/`.pdmaterial`/`.pdtexture` never reach the catalog -> the projectile `model_ref = mod_needler:needle` resolves to nothing, graceful; c3844-class self-contained-closure parity gap affecting all custom weapons), **B-912** (`weaponGraphRuntimeGetProjectileForGameplay` is test-only -> projectile.impact gameplay execution is unconsumed; the secondary's explode-on-contact is data-only at the gameplay level, broader weapon-graph-runtime-cutover), and **B-913** (`modmgr.c` reads only singular `content`; the authored `contents`/`assets` arrays are skipped -- decorative today). Directly confirmed B-911 by reading `weapon_graph_archive.c:123-128,671`.

Committed the Needler authoring unit (builder + archive + mod.json) with the captured findings (design note + bugs.md B-911/912/913 + kanban: c3847 s2-s5 done, s8 added for B-911). Authoring is correct + conformant; B-911 is the most contained next runtime slice (decide fix locus -- general catalog-scanner closure ingestion vs weapon-graph-walker extension -- via an embedded-asset-ingestion investigation before touching the render-sensitive path).

## 2026-06-10 - Goal phases kickoff: dev-mods pipeline + MP start fix (B-910)

Ran the `goal-phase-design` ultracode workflow (7 agents) to produce source-grounded, file-anchored plans for Mike's multi-phase `/goal` (finish migrations, rebuild mod tooling, fix MP co-op/Combat Sim, build the Needler mod). Key verified finding: the Combat Sim "dead Start button" is a SINGLE bug, and three input plans collapse into one delivery spine (the per-family contract table already exists as `port/src/asset_mod_utility_contract.c`, so the Mod Studio design supersedes the inventory plan and subsumes the gate-6 modder-workflow item). Created Kanban cards c3845 (networking: MP co-op + Combat Sim), c3846 (mod-infrastructure: Mod Studio rebuild + dev-mod pipeline), c3847 (modding: Needler mod).

Dev-mod build pipeline (c3846): per Mike, dev mods must survive clean builds and be copied into the build like the ROM, with build/release include/exclude. Added a git-tracked `dev-mods/` source tree (`dev-mods.json` manifest + `README.md` + `needler/mod.json` placeholder) copied into `<install>/mods/` by `build-headless.ps1` on every client build, selected via a new `-DevMods ""|all|none|id1,id2` flag (default = manifest `dev:true`), passed through `build-session.ps1`. Source is git-tracked so a clean build cannot erase it; the copy re-populates the wiped install each build. Verified: `-Target all` build landed `needler/mod.json` in the install's `mods/`.

MP listen-host start fix (B-910, c3845, rank 1): `netLobbyRequestStartWithSims` (`pdgui_bridge.c`) rejected every mode except `NETMODE_CLIENT`, so the in-client listen host's "Start Match" button was dead -- the single blocker for Combat Sim + co-op + Counter-Op start. Widened the guard to accept the listen host and replay `CLC_LOBBY_START` through the server handler locally (mirroring `netSendRoomSettingsUpdate` at `netmsg.c:8334-8346`); remote-client `netSend` path preserved. Added a static guard test. Verified: client `-Target all` PASS (26s); `[net][lifecycle]` PASS 417 assertions / 18 cases incl. the new listen-host-start test. Live match-start is B-801-gated.

Gate 5 follow-up (c3844, B-908 follow-up, ranks 3-4): the workflow's rank-3 plan used `filenum==0` as the unregistered discriminator, but my B-909 custom bodies/heads use public mesh source with `filenum==0`, so that would false-flag them. Corrected to `filenum!=0 OR catalog_id set`. Routed the 10 body/head field accessors through `s_bodyFieldRecordChecked`/`s_headFieldRecordChecked` (set `g_CatalogFailure` on a genuine in-range-unregistered miss; benign manager-NULL stays unflagged). Added pure + static tests. Verified: client `-Target all` PASS (25s); `[catalog][checked],[catalog][provider][static]` PASS 11,507 assertions / 57 cases; guard PASS. This closes the last named B-908 follow-up; the fatal-under-enforcement path wants a source-gate smoke confirm.

Also: started keeping the Kanban actively current per Mike's directive -- flipped landed subtasks done (c3844 body/head equivalence, c3845 listen-host start + guard, c3846 dev-mods pipeline, c3847 folder reserved) and normalized the new cards' subtasks; will keep it fresh per slice. Using ultracode workflows where appropriate (the goal-phase design fan-out; upcoming Needler authoring + Mod Studio family flows).

## 2026-06-10 - B-909 body/head private-slot allocator (Gate 2 implemented)

Mike set a multi-phase `/goal`: (1) finish c3844 migrations to 100%, (2) rebuild the in-game mod tooling from scratch with versioning + comprehensive controller support, (3) fix MP co-op drop-in/out + Combat Sim connectivity, (4) build a Halo-Needler-style custom weapon mod (secondary fire = non-tracking explosive pink needles). Working autonomously through the phases under a session-scoped goal hook.

Phase 1, item 1: implemented the body/head private-slot allocator (Gate 2 headline) per the design note, closing the last actionable migration gap. A fully-new custom `.pdbody`/`.pdhead` with no legacy bodynum/headnum now gets a catalog-owned private runtime slot above the 152 base ceiling, so the existing integer render path (`body0f02ce8c` -> `s_Bodies[slot]`) assembles it from public mesh source. Option B (separate `_TOTAL` for arrays/bounds; base 152 retained for population + `s_pickRandomByGender` loops to avoid random-gender pool pollution). New `assetcatalog_body_head_slots.c` mirrors `assetcatalog_weapon_slots.c`; `loaderPoolParse{Body,Head}JsonForSlot` injects the allocated slot; walkers set `runtime_index` so `catalogIdByRuntime`'s pool scan reverse-resolves it; reset wired beside the weapon-slot reset; `_Static_assert` lockstep guards keep the pure/manager constants in sync. 14 source files + 2 new + 3 test files.

Verified: client `-Target all` PASS (26s, empty error log); `[catalog][bodyhead][slots],[catalog-mgr-body],[catalog-mgr-head],[catalog][checked]` PASS (438 assertions / 51 cases, incl. dedup/range/exhaustion-loud-fail/reset); `[catalog][provider][static]` no regression (part of an 11,784-assertion run); native-source guard PASS; examples conformance 28/59 (no regression). The private slot is allocated at runtime and never written to any archive, so no new public-boundary leak. Only the live custom-body render is B-801-blocked (part of the B-801 gate). Build access via the PowerShell wrapper (Mike's allow rules + dontAsk).

## 2026-06-09 - c3844 gate audit + B-907 conformance JSON ref parity

Recovered live c3844 state and ran a source-grounded 8-agent gate-audit workflow to turn the session-log narrative into verified ground truth. Confirmed against `tools/kanban/state.json`: c3844 is the sole Active card (104/110 subtasks done); the 6 open subtasks are exactly the named gates. Verified findings: Gate 2 (body/head) has NO private-slot allocator analogous to weapons (the real remaining slice); Gate 3 producer side is fully wired and the remaining work is 3 consumer-side silent-OG fallbacks (objectives first); Gate 4's fresh install tree is clean but the retained prefilled-seed cache `.claude/smoke-verify-cache/ntsc-final` is a wholesale-older generation (37,698 conformance errors) needing full re-extraction, blocked on B-801; Gate 5's `g_CatalogFailure` is a write-only flag (9 setters, 0 consumers); Gate 1 has only the scenario-scene-probe as a CPU-safe probe.

Closed the cold-trace finding B-907 first (highest-ROI, CPU-safe, no build): `tools/asset_archive_conformance.py`'s JSON ref scanner accepted numeric/legacy values in catalog-ID keys outside the narrow `FORBIDDEN_NUMERIC_ASSET_REF_KEYS` set (`model_catalog_id`, `*_catalog_id`, `stage_id`) because `validate_catalog_id_reference` early-returns on non-catalog-shaped values, while the delimited scanner already rejected them. JSON is the primary public format, so the weaker gate was the one that mattered. Ported the numeric/legacy reject to the JSON path on the same `is_delimited_catalog_ref_column` hard-ref selector (excludes generic `pad_ref`/`room_ref`). Deliberately did NOT port the stricter "must be catalog-ID shaped" half: it over-rejected legitimate intra-archive paths (`.pdui texture=texture.png`, `.pdweapon material=dependencies/.../default.pdmaterial`) that are JSON-only and never flowed through CSV. Added a `--selftest` mode pinning JSON<->delimited parity.

Verified: `--selftest` PASS (16 parity cases + recursion); native-source guard PASS; example conformance 28 root / 59 checked (no regression, matches baseline); fresh extracted-tree conformance 8060 root / 9061 checked (no regression). Pure-Python tooling change, no C/C++ surface. Next best slices per the audit ranking: objectives-module OG-fallback gating (Gate 3, small/testable), `g_CatalogFailure` consumer checkpoint (Gate 5, medium), scenario-scene-probe sweep runner (Gate 1 CPU pre-flight, small), then the body/head private-slot allocator (Gate 2, large headline closure).

Build access note: this session initially had NO C/C++ build path (the PowerShell build wrapper was auto-denied by the permission classifier, and the MinGW compiler cannot spawn `cc1/cc1plus` from the Bash tool so even ccache misses fail). Mike added `Bash(powershell -File devtools/build-session.ps1:*)` and `run-pd-tests.ps1` allow rules to user settings and set `dontAsk` mode, which unblocked the wrapper. The scenario-scene-probe-sweep runner is also runnable. Subsequent C slices build via `build-session.ps1 -Session <id> -Target all|tests`.

Body/head allocator (Gate 2, rank 6) -- SCOPED + DESIGNED, not implemented this session. Verified the blast radius is contained but Option B (separate `_TOTAL` for arrays/bounds, base 152 for population loops) is required because growing the raw count would pollute `s_pickRandomByGender`'s random-gender candidate pool with empty custom slots. Reverse-map confirmed feasible (`catalogIdByRuntime` falls back to a pool scan by `runtime_index`; `RT_CACHE_SIZE`=1024). `catalogManagerBodyCount/GetBodyAt` have no external consumers. Full one-pass implementation plan written to `context/designs/modding/body-head-private-slot-allocator.md` (constants, per-site TOTAL-vs-base table, new `assetcatalog_body_head_slots.c` mirroring the weapon allocator, walker wiring, tests). Held to a dedicated focused pass: it is a ~12-file data-layout change on the most render-sensitive path, live-unverifiable under B-801, and the context was saturated -- exactly the rushed-data-layout class the project discipline (struct-stride / macro-collapse) warns against.

Gate 4 (stale cache regen) and Gate 1 full live proof remain genuinely live-blocked under B-801: the retained prefilled-seed cache `.claude/smoke-verify-cache/ntsc-final` is a wholesale-older generation (37,698 conformance errors per the audit) needing full re-extraction, and the non-scenario family CPU probes + the narrow live pass need a live boot. Neither is doable without a live action on Mike's PC (the thing that previously made it unresponsive). Narrow next actions recorded under each gate in tasks.md.

Gate 3 objectives/trigger enforcement (rank 2/7) -- VERIFIED, no code change: read the actual runtime-failure helpers rather than trusting the audit narrative. `s_missionObjectiveGraphRuntimeFailure` (objectives) and `s_levelGraphVolumeRuntimeFailure` (trigger volumes) already `sysFatalError` under source-only enforcement (`ASSET_MISSION`/`ASSET_SCENARIO`), and every `objectiveCheckGraphSource` / pad-room mismatch routes through them. So under enforcement the legacy OG fallback never runs (it fataled first); the OG fallback only executes in NORMAL play, where it is the SAFE parity behavior. The audit's recommended "gate the else on graph-active" flip would, applied to normal play, brick campaign stages whose graph node set is not yet proven complete, and adds nothing under enforcement. Declined the flip per the architectural-review/rabbit-hole discipline; recorded the genuine remaining gate-3 work (per-stage graph-completeness proof + graph-sourced `lvTick` global behavior, both large and live-dependent) in tasks.md.

g_CatalogFailure consumer (Gate 5, B-908, rank 3): the catalog load-site helpers set the write-only `g_CatalogFailure` flag on a miss + substituted a default, but nothing consumed it (silent slot-0/filenum-0/scale-1.0 substitution after a one-time log). Added `catalogAssertHealthy(checkpoint)` + `catalogClearHealth()`; the escalation decision is the pure, testable `catalogHealthShouldFatal(failure, enforcement_active)` in `catalog_checked.c`. The consumer loud-reports `CATALOG.HEALTH: FAIL at <checkpoint>` and, under source-only enforcement, `sysFatalError`s; normal play stays report + clear (no live brick before per-family gates close). Wired at `lv.c` stage-load entry. Verified: client `-Target all` PASS (12s); `[catalog][checked]` PASS 64 assertions / 15 cases; `[catalog][provider][static],[modding][pdxxx][c3844][source][static]` PASS 12,180 / 48 (no regression); guard PASS. Follow-up: the in-range-unregistered body/head field accessors still return zeros silently; fatal-under-enforcement wants a source-gate smoke run to confirm no pre-existing benign miss.

Require-node hardening (Gate 3, rank 5): `scenarioSourceAiGraphExecuteChrDoAnimation` tested the tri-state require-node helper with `if (!s_aiGraphRequireAnimationNode(...))`, but the helper returns -1 on loud-fail and `!(-1)==0`, so the loud-fail did not fire from the guard; it was absorbed only by the downstream `s_aiGraphResolveAnimationCatalogId` returning NULL (correct by accident). Now handles the tri-state explicitly: 0 (inactive) falls back to OG, <0 (active but source missing, already loud-reported) owns the result, >0 proceeds. No behavior change in normal play (the resolve path returned 1 on the missing node anyway). `scenario_source_runtime.c` is not compiled into pd-tests, so verified by client build + review. Verified: `-Target all` PASS (CLIENT 32s, UPDATER 1s, empty error log); native-source guard PASS.

Probe slice (Gate 1 CPU pre-flight): added a first-class `probe` build target (`build-headless.ps1` + `build-session.ps1` ValidateSet/target maps, mapping to the existing `scenario-scene-probe` CMake target) and `devtools/scenario-scene-probe-sweep.ps1`, which probes every `.pdscenario` in a data tree asserting `ok=1` + `images>0` + `vertices>0` with non-zero exit on any miss. This converts the prior manual 87/87 sweep into a repeatable, window/GPU/audio-free pre-flight and guards against a regeneration silently dropping scene content. Verified: probe builds via `build-session.ps1 -Session probe1 -Target probe` (18s, SUCCESS, empty error log); sweep PASS 87/87 on the fresh `ntsc-final` tree via both the runner and a direct-exe bash loop.

## 2026-06-08 - B-902 menu model handle sentinel and weapon visual handle

Continued the all-asset parity goal on CPU-safe mesh usage inspection after B-901. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as the mesh integer-native runtime boundary, the `.pdsong` private integer-slot bridge, and the broader rule that remaining numeric runtime bridges are private migration debt, not public archive identity or modder-authored fields.

The confirmed failure point is a private runtime handoff, not extraction. Source-backed menu model rows can be valid catalog/provider assets without a legacy file number, but `menuSetModelFileHandle(..., -1, handle)` encoded the request through `MENUMODELPARAMS_SET_FILENUM()`. The low 16 bits became `0xffff`, which `menuRenderModel()` treats as the character body/head sentinel, so custom weapon/vehicle/prop previews could enter the wrong load branch or fail the pending handle match. Weapon resolution also exposed the split weapon `primary_graph` as `catalog_weapon_result_t.handle` even though current preview/spawn-pad callers use that result as a visual model handle.

The fix centralizes a private `MENUMODEL_HANDLE_SENTINEL_FILENUM = 0xfffe`, remaps invalid or colliding provider-backed menu file numbers to that safe key, updates the char-preview path to use the shared sentinel, and makes weapon resolution return `model_file` / model-source handles for model-loading consumers while leaving split graph activation as the weapon runtime source path. The FileProvider handle construction needed for the resolver is exposed through a catalog-owned helper so raw provider construction stays behind the catalog/provider boundary.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known `context/bugs.md` line-ending warning, isolated `menuhandle` tests build, focused `[catalog][provider][static]` with 11,371 assertions / 39 cases after one expected red/green boundary correction, focused `[modding][pdxxx][c3844][source][static]` with 744 assertions / 8 cases, and isolated `menuhandle` all-target build. No live renderer/audio smoke was run.

## 2026-06-08 - B-901 Training/Hangar source-backed preview handoff

Continued the all-asset parity goal on CPU-safe mesh usage inspection after B-900. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md`, `context/pillars/modding.md`, and `UNRELEASED.md`; the limitation is private runtime/cache migration debt, not public archive identity or a modder-authored numeric slot requirement.

The confirmed failure point was the Training-family preview bridge. Device Training and Holo Training resolved the current weapon to `weaponGetFileNum()`, Hangar Holograph used a hardcoded native vehicle file table, and the ImGui preview helper submitted raw `pdguiCharPreviewRequestFilenum()` requests. That could bypass catalog/source identity for extracted or custom model sources even after the shared preview path learned how to use provider handles.

Training/Hangar previews now resolve current weapon/vehicle selections back to catalog IDs where possible, ask the catalog for provider handles tied to the legacy model source file number, submit provider-backed previews before raw file-number fallback, and keep numeric file numbers as private base-content fallback only.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, isolated `trainprevsrc` all-target build, isolated `trainprevsrc` tests build, focused `[catalog][provider][static]` with 11,355 assertions / 39 cases, and focused `[modding][pdxxx][c3844][source][static]` with 744 assertions / 8 cases. No live renderer/audio smoke was run.

## 2026-06-08 - B-900 Map Import source-layout staging

Continued the all-asset parity goal on CPU-safe custom geometry/import inspection after B-899. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The confirmed failure point was the exposed Modding Hub Map Import path. It still described itself as a PD-format/native map importer, searched for `.bg` / `.bin`, parsed raw BG/pad headers, copied top-level files only, and emitted a staged `.bg` payload. That made user-created map imports depend on inaccessible native geometry/setup bytes and flattened delivery, which is the same class of mistake that broke mesh/level rendering after extraction.

Map Import now recursively scans source folders for editable geometry/source units (`scene.glb`, `scene.gltf`, `geometry.obj`, `.pdarena`, `.pdscenario`, `arena.ini`, `scenario.ini`), rejects native `.bg` / `.bin` / `.pad` / `.setup` payloads, stages source layouts recursively under `mods/imported_<name>/`, generates missing `scenario.ini` / `arena.ini` descriptors for bare scene/geometry folders, and validates the staged output with the same source-only scan. The Modding Hub UI now tells users to import editable source layouts or typed map archives instead of native map dumps.

Verification passed `python tools\asset_native_source_guard.py`, stale native Map Import string checks, scoped diff check with only the known bug-ledger line-ending warning, isolated `mapimportsrc` all-target build, isolated `mapimportsrc` tests build, focused `[modding][pdxxx][mapimport][source][static]` with 23 assertions / 1 case, focused `[modding][pdmod][static][c3809]` with 1,092 assertions / 16 cases, and focused `[modding][pdxxx][c3844][source][static]` with 744 assertions / 8 cases. The isolated build directory was removed. No live renderer/audio smoke was run.

## 2026-06-08 - B-899 source-backed non-character preview handles

Continued the all-asset parity goal on CPU-safe mesh/tool usage inspection after B-898. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The confirmed failure point was the shared ImGui model preview path for non-character mesh previews. Character previews already resolve catalog body/head IDs, but weapon, vehicle, and prop previews still required `asset_entry.source_filenum > 0` and then submitted only the encoded legacy file number. A custom source-backed row can have a valid FileProvider handle and public mesh/model source without occupying a ROM file slot, so the UI could show a placeholder even though the asset was valid.

Non-character preview requests now keep the legacy source-filenum path for base rows, but when no real file number exists they submit the catalog effective provider handle with a private preview sentinel. `menuRenderModel()` already honors a matching pending handle before resolving by file number, so the preview now reaches the same public modeldef source compiler path without exposing any numeric slot to modders.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, isolated `previewsrc` all-target build, isolated `previewsrc` tests build, focused `[catalog][provider][static]` with 11,330 assertions / 38 cases, and focused `[modding][pdxxx][c3844][source][static]` with 721 assertions / 7 cases. The isolated build directory was removed. No live renderer/audio smoke was run.

## 2026-06-08 - B-898 Modding Hub mesh scale source alignment

Continued the all-asset parity goal on CPU-safe mesh/source inspection after B-897. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as mesh integer-native runtime semantics, the `.pdsong` private integer-slot bridge, and the broader rule that current numeric runtime bridges are private migration debt, not public archive identity or modder-authored fields.

The confirmed failure point was the Modding Hub Model Scale Tool. Extraction already writes public `.pdmesh::mesh.ini` with `model_scale`, and runtime compilation reads that source metadata. The UI still bypassed that path: it opened the selected model path directly, read/wrote the big-endian scale float at byte offset `0x10`, and resolved body paths through legacy `source_filenum` lookup.

The scale tool now derives character/body scale sources from the public body/mesh archive chain (`.pdbody::mesh.pdmesh::mesh.ini`), reads `model_scale` through the shared INI parser and archive member loader, and removes the binary bake path instead of mutating hidden native bytes. Descriptor edits remain source-file edits.

Verification passed `python tools\asset_native_source_guard.py`, stale binary-scale string check, scoped diff check with only the known bug-ledger line-ending warning, isolated `scaleini` all-target build, isolated `scaleini` tests build, focused `[catalog][provider][static]` with 11,318 assertions / 37 cases, and focused `[modding][pdxxx][c3844][source][static]` with 721 assertions / 7 cases. The isolated build directory was removed. No live renderer/audio smoke was run.

## 2026-06-08 - B-897 `.pdprop` walker primary source alignment

Continued the all-asset parity goal on CPU-safe primary-member inspection after B-896. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The confirmed prop failure point was the same source-handle drift pattern as B-896. Scanner, network delivery, and runtime activation already treat `behavior_graph` as dependency metadata for `.pdprop`: `model_file` is preferred, `prop_file` is the fallback source, and behavior alone cannot activate the prop. Boot metadata walking still searched `behavior_graph` before `prop_file`, so a walked archive could expose behavior as the selected provider primary when direct model source was absent.

Metadata walking now searches `model_file`, then `prop_file`, then `behavior_graph`, preserving behavior source without letting it replace the physical/archetype/model source handle.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, isolated `propwalk` client/updater build, isolated `propwalk` tests build, focused `[catalog][provider][static]` with 11,301 assertions / 36 cases, and focused `[modding][pdxxx][c3844][source][static]` with 721 assertions / 7 cases. No live renderer/audio smoke was run.

## 2026-06-08 - B-896 `.pdvehicle` base/walker primary source alignment

Continued the all-asset parity goal on CPU-safe vehicle source inspection after B-895. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as the mesh integer-native runtime boundary, the `.pdsong` private integer-slot bridge, and the broader rule that remaining numeric runtime bridges are private migration debt, not modder-authored archive identity.

The confirmed vehicle failure point was source-handle selection drift. Local mod scans and network delivery already selected `model_file` first and `behavior_graph` second for `.pdvehicle`, but base vehicle catalog registration still selected `physics.json` as the provider primary, and boot metadata walking searched `physics_file` before model/behavior source.

Base vehicle registration now selects `model_file` first and `behavior_graph` second for the provider primary. Metadata walking uses the same order. `physics_file` is still preserved as required vehicle data, but it is no longer allowed to masquerade as the selected vehicle source handle.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, isolated `vehbase` client/updater build, isolated `vehbase` tests build, focused `[catalog][provider][static]` with 11,295 assertions / 36 cases, and focused `[modding][pdxxx][c3844][source][static]` with 719 assertions / 7 cases. No live renderer/audio smoke was run.

## 2026-06-08 - B-895 `.pdanim` source-only ROM DMA fallback refusal

Continued the all-asset parity goal on CPU-safe animation inspection after B-894. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as the mesh integer-native runtime boundary, the `.pdsong` private integer-slot bridge, and the broader rule that remaining numeric runtime bridges are private migration debt, not modder-authored archive identity.

The confirmed animation failure point was ROM-backed animation fallback. `modAnimationTryCatalogOverride()` could see a cataloged public animation source, fail to compile a GLTF/GLB runtime clip, return `NULL`, and then let `animLoadFrame()` / `animLoadHeader()` continue to `animDma()`. A selected public source that was not editable GLTF/GLB animation source could also fall through to raw file loading outside source-only checks.

`ASSET_ANIMATION` source-only mode now fails closed when a public animation source is missing, is not editable GLTF/GLB source, or fails runtime clip compilation. `modAnimationTryCatalogOverride()` now uses `catalogResolveAnim()` directly so source-only classification is honored before the old ROM DMA fallback point.

Verification passed `python tools\asset_native_source_guard.py`, focused `[catalog][provider][static]` with 11,279 assertions / 35 cases, focused `[modding][pdxxx][c3844][source][static]` with 717 assertions / 7 cases, isolated `animsrc` all-target build, and build-session cleanup. No live renderer/audio smoke was run.

## 2026-06-08 - B-894 `.pdtexture` source-only non-image fallback refusal

Continued the all-asset parity goal on CPU-safe texture inspection after B-893. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as the mesh integer-native runtime boundary, the `.pdsong` private integer-slot bridge, and the broader rule that remaining numeric runtime bridges are private migration debt, not modder-authored archive identity.

The confirmed texture failure point was source-only fallback classification. `modTextureLoadRgba32Source()` correctly refused missing public texture source, but when a selected public FileProvider path was present and not an editable image member, it returned "not handled". `texLoad()` could then continue to the legacy compressed texture path, and `modTextureLoad()` could load those public non-image bytes as a compressed-texture override.

`ASSET_TEXTURE` source-only mode now fails closed when a resolved public source path is not PNG/TGA/JPG/JPEG/BMP image source. The older compressed-texture override path also fatals in texture source-only mode before it can consume non-image public source bytes. Normal non-source-only behavior remains unchanged.

Verification passed `python tools\asset_native_source_guard.py`, focused `[catalog][provider][static]` with 11,259 assertions / 34 cases, focused `[modding][pdxxx][c3844][source][static]` with 717 assertions / 7 cases, isolated `texsrc` client/updater build, and isolated `texsrc` tests build. No live renderer smoke was run.

## 2026-06-08 - B-893 `.pdsong` source-only resolver classification

Continued the all-asset parity goal on CPU-safe audio inspection after B-892. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The confirmed failure point was source-only classification in the music resolver. `catalogResolveMusicSequence()` marked `source_only_blocked` whenever `ASSET_AUDIO` source-only mode was enabled, even after it had resolved a valid public FileProvider `.pdsong` source path. That mixed two different states: no public source exists versus public source exists but sequence compile or stream playback failed.

`catalogResolveMusicSequence()` now uses the shared source-only helper so `source_only_blocked` means the selected music row has no public FileProvider source. `modSequenceLoad()` still tries public `sequence.mid` plus `sequence.json` first, then fails closed in audio source-only mode if compilation fails. `modSequencePlayAudioSource()` also fails closed in source-only mode if public stream playback fails. The guard and static tests now pin this distinction.

Mike asked to be sure the integer-only limitation was recorded. It remains recorded in `context/constraints.md` as mesh integer-native geometry/material semantics, the `.pdsong` private integer-slot bridge, and the broader rule that remaining numeric slots are private runtime/cache migration debt, not public archive identity or modder-authored fields.

Verification passed `python tools\asset_native_source_guard.py`, focused `[catalog][provider][static]` with 11,255 assertions / 34 cases, focused `[modding][pdxxx][c3844][source][static]` with 717 assertions / 7 cases, and isolated `songsrc` all-target build. No live audio smoke was run.

## 2026-06-08 - B-892 Scenario source-renderer shader failure diagnostics

Continued the all-asset parity goal on CPU-safe Scenario render inspection after B-891. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed failure point was renderer observability. The public Scenario `scene.glb` source can now build valid CPU scene data and preserve DCC/runtime UVs, material extras, colors, and alpha metadata, but the source renderer still created and linked its OpenGL shader program without checking compile or link status. If the shader failed, the live symptom would still look like broken geometry/materials while the log lacked the actual failure point.

`scenario_scene_renderer` now checks shader creation, shader compile status, and program link status. It logs `SCENARIO.RENDER: shader compile failed` with the stage and GL info log, logs `SCENARIO.RENDER: shader link failed` with the link log, marks the scene shader-failed after the first failure, and refuses GPU upload/draw for that source scene instead of continuing with an invalid program.

Verification passed `python tools\asset_native_source_guard.py`, isolated `shaderdiag` all-target build, isolated `shaderdiag` tests build, focused `[modding][pdxxx][c3844][source][static]` with 717 assertions / 7 cases, and build-session cleanup. No live renderer smoke was run.

## 2026-06-08 - B-891 Scenario source-renderer dual-UV material delivery

Continued the all-asset parity goal on CPU-safe Scenario render inspection after B-890. Mike asked again to ensure the integer-only limitation was recorded; `context/constraints.md` still records mesh integer-native geometry/material boundaries and private integer runtime bridges.

The next confirmed failure point was runtime delivery rather than extraction. Generated Scenario GLBs already carry DCC/editor UVs on `TEXCOORD_0`, runtime-repeat UVs on `TEXCOORD_1`, and `extras.pd2_material.secondaryTexture.texCoord`, but the source renderer flattened each vertex to one UV and sampled both texture layers from that same coordinate channel.

The source renderer now preserves both UV sets through CPU scene build, VBO upload, and shader sampling. PD-authored Scenario materials with `pd2_material` extras keep primary sampling on runtime UV 1 for native parity; standard/custom GLB materials keep their declared base texture coordinate; secondary textures now honor their archived coordinate binding; and single-UV custom meshes duplicate the available channel into the missing runtime/editor UV slot.

Static coverage was also tightened so the body/head scanner and runtime-binding checks measure the actual `ASSET_BODY`/`ASSET_HEAD`/runtime activation cases instead of accidentally including adjacent `ASSET_MODEL` or support-switch code. Verification passed focused `[modding][pdxxx][c3844][source][static]` with 717 assertions / 7 cases, `python tools\asset_native_source_guard.py`, isolated `assetuv` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-890 `.pdanim` command-source scan/delivery parity

Continued the all-asset parity goal on CPU-safe animation/runtime inspection after closing B-889. Mike asked to be sure the integer-only limitation was recorded; `context/constraints.md` and the parent mirror both record it as private runtime/cache migration debt, not public asset identity or modder-authored numeric data.

The next confirmed source-chain failure point was command-backed `.pdanim`. Base/generated archive walking parsed `commands.json` into the weapon animation pool, but local mod scanning and network delivery only selected `commands_file` as the catalog primary. That meant mod/distributed command animations could look source-backed while never installing the command rows that weapon animation resolution expects.

Local scan and network delivery now load the public `commands_file`, parse it through `loaderPoolParseAnimationSourceJson()`, and rerun loader-pool finalization so command rows and include/random fixups are available through the same path as base archives. GLTF/GLB character animation clip rebuilding remains unchanged.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdmod][static][c3809],[modding][pdxxx][c3842],[modding][pdxxx][runtime][c3838][files]` with 2,086 assertions / 28 cases, isolated `animcmdsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-889 arena Scenario-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection after closing B-888. Mike asked to be sure the integer-only limitation was recorded; `context/constraints.md` and the parent mirror both record it as private runtime/cache migration debt, not public asset identity or modder-authored numeric data.

The next confirmed source-chain failure point was `.pdarena`. Strict arena archives are wrappers around Scenario source and require `scenario_archive`, but the generic runtime adapter could still activate an arena from a caller-selected primary path when `scenario_archive` was absent. Local scan and network distribution also still accepted legacy `geometry_file` / `geometry` as an arena source fallback.

Arena scan and distribution now select only `scenario_archive`, and runtime activation requires that Scenario archive member. Arena catalog ID, stagenum, load mode, unlock, name-langid, and Scenario target metadata remain preserved, but legacy geometry paths and bare primary paths cannot make an incomplete arena active.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[catalog][provider][static]` with 14,042 assertions / 68 cases, isolated `arenastrict` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-888 Scenario full-source runtime activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection after closing B-887. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdscenario`. Strict Scenario archives require the full public source bundle: `scene.glb` / `scene.gltf`, portals, pads, spawns, volumes, objects, setup fields, AI lists, objectives, navigation files, and `level.graph.json`. The generic runtime adapter could still mark a Scenario active from a caller-selected primary path or any one Scenario source member, which made partial source rows look runtime-ready while missing geometry, navigation, setup, or graph data.

Scenario runtime activation now uses `scene_file` as authored source and requires the full strict source bundle. `rooms_file` remains compatibility metadata and `collision_file` remains optional collision override data. Focused adapter coverage rejects primary-only, scene-only, and missing-graph Scenario rows, and static coverage pins the no-`primary_path` runtime rule.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[catalog][provider][static]` with 13,884 assertions / 65 cases, isolated `scenariosrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-887 prop source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection after closing B-886. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdprop`. Strict prop archives require prop/model/mesh source such as `prop.json`, `model.gltf`, `model.glb`, `model.obj`, `mesh.pdmesh`, or an embedded typed model dependency. Scanner and network delivery still selected `behavior_graph` as the primary source if model/prop source was missing, and runtime activation accepted a behavior-only row or caller-selected primary path as enough source proof.

Prop scan and distribution no longer select `behavior_graph` as primary. Runtime activation requires `model_file` or `prop_file`; behavior graph remains dependency metadata for future behavior importers but cannot activate a prop by itself. Focused adapter coverage rejects primary-only and behavior-only props, and static coverage pins scanner/distribution/runtime behavior.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[modding][pdxxx][weapon_graph][archive][c3814],[catalog][provider][static]` after one expected red/green correction with final 13,902 assertions / 66 cases, isolated `propsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-886 weapon split-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection after closing B-885. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdweapon`. Strict weapon archives require split held behavior source: `primary_graph`, `secondary_graph`, `shared_context_file`, `settings_file`, and `variables_file`. Scanner and network delivery still selected `model_file` as the primary source, the generic runtime adapter accepted model/primary/partial graph rows, `assetCatalogRegisterWeapon()` assigned model primary handles even though it cannot receive graph source, and loose graph lifecycle activation still accepted legacy single `behavior_graph`.

Weapon scan and distribution now select `primary_graph` as the primary source. Generic runtime activation requires the full split source bundle, while `model_file` is preserved as dependency metadata through `weapon_model_file` and `dependency_e`. Catalog helper registration no longer marks model-only weapons source-complete. Loose graph lifecycle activation compiles only split weapon graph source and requires shared-context/settings/variables metadata before registering.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[modding][pdxxx][weapon_graph][archive][c3814],[catalog][provider][static]` with 13,896 assertions / 66 cases, isolated `weapsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-885 character/body/head source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection after closing B-884. Live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdbody`, `.pdhead`, and `.pdcharacter`. Strict body/head archives require `mesh.pdmesh`, and strict character archives require a body archive. Scan and network delivery could still use legacy body/head `model_file` fallback as a primary source, while runtime activation could accept caller-selected primary paths, body hand meshes, character portraits, or other optional dependencies as enough source proof.

Body/head scan and distribution now select only `mesh_archive` as the primary source. Runtime body/head activation requires `mesh_archive`; hand meshes remain dependencies only. Character activation now requires `bodyfile`, with head and portrait preserved as dependency metadata. Focused adapter coverage rejects primary-only character/body/head rows, hand-only bodies, and portrait-only characters. Static coverage pins the scanner/distribution/runtime source requirement and rejects legacy body/head model fallback.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[modding][pdxxx][weapon_graph][archive][c3814]` with 2,641 assertions / 32 cases, isolated `chmesh` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-884 projectile/entity behavior-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to ensure the integer-only limitation was recorded; `context/constraints.md` and the parent mirror already record both the mesh/fixed-point boundary and the broader private integer bridge/cache rule.

The next confirmed source-chain failure point was `.pdprojectile` and `.pdentity`. Strict conformance requires `behavior.graph.json`, but scanner and network delivery could still choose `model_file` as primary if the graph was missing. The graph archive helper also still treated projectile behavior graphs as optional, and runtime activation accepted either a caller-selected primary path or model-only dependency as proof of an active projectile/entity.

Projectile/entity scan and distribution now select only `behavior_graph` as the primary source. Graph archive validation requires projectile behavior graphs as well as weapon/entity graphs. Runtime activation requires `behavior_graph` while keeping `model_file` as visual dependency metadata. Focused adapter coverage rejects primary-only and model-only projectile/entity rows, and static guards pin scanner, distribution, graph-helper, and runtime behavior.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[modding][pdxxx][weapon_graph][archive][c3814]` with 2,631 assertions / 32 cases, isolated `graphsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-883 mission full-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdmission`. Strict mission archives require `mission.graph.json`, embedded Scenario source, `objectives.json`, and `briefing.json`, but scanner and network delivery could still choose Scenario/objectives/briefing as primary if the graph was missing. Runtime activation also accepted any selected primary path or any one of those dependency members as proof of an active mission.

Mission scan and distribution now select only `mission_graph_file` as the primary source. Runtime activation requires the full mission source bundle: graph, Scenario dependency, objectives, and briefing. Focused adapter coverage rejects primary-only, graph-only, and missing-briefing missions, and static guards pin the local/network/runtime rule.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][c3842]` with 1,560 assertions / 16 cases, isolated `missionsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-882 font/lang authored-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdfont` and `.pdlang` activation. Scanner, network delivery, and base walking preserve font and language source fields, but runtime activation still accepted caller-selected primary paths as source proof. Font activation also counted `metrics_file` by itself, which could make a bitmap font row active without glyph source.

Font runtime activation now requires `font_file`; bitmap `.pgm` fonts also require `metrics_file`, while vector `font.ttf` / `font.otf` remains valid as a single authored source. Language activation now requires `strings_file` plus a real `bank_id` / `source_bank` target, so a bare archive path or bankless strings source cannot rebuild a native language bank silently.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842]` with 1,407 assertions / 13 cases, isolated `fontlang` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-881 UI texture source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` as both the mesh/fixed-point boundary and the broader private runtime bridge/cache rule.

The next confirmed source-chain failure point was `.pdui`. Strict conformance requires a UI texture source member, while `layout.json` and `nineslice.ini` are optional metadata/dependency members. Local scan and network delivery could still select `layout_file` as primary when no texture existed, and runtime activation accepted primary/layout/nine-slice paths as proof of an active UI asset.

UI scan and distribution now select only texture source as the primary path. Runtime activation requires `texture_file`, while layout and nine-slice members remain attached to the binding for tooling and render metadata. Focused adapter coverage rejects layout-only, nine-slice-only, and primary-path-only UI rows, and static guards pin the scanner/distribution/runtime shape.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,478 assertions / 29 cases, isolated `uisrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-880 gamemode/bot-profile authored-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdgamemode` and `.pdbotprofile` activation. Strict archives and scanner/distribution require `rules_file = rules.json` and `profile_file = profile.json`, but the runtime adapter still accepted a caller-supplied primary path when those named source members were missing. That allowed an outer archive path to mark a gamemode or bot profile active even though the editable rules/profile source was not available to the runtime binding.

Runtime activation now requires `rules_file` for gamemodes and `profile_file` for bot profiles. Focused adapter coverage rejects primary-path-only mode/profile rows, and static source-contract coverage pins that those runtime cases do not use `binding->primary_path` as source proof.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842]` with 1,387 assertions / 13 cases, and isolated `modeprof` all-target build. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-879 vehicle physics/source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdvehicle`. Strict vehicle archives require `physics.json` plus model or behavior source, but local scanning and network delivery could still select `physics_file` as the primary source when no model or behavior source existed. Runtime activation also accepted any one of primary/model/physics/behavior, so physics-only rows and model-without-physics rows could become active even though the archive was not base-equivalent.

Vehicle scan and distribution now select only model or behavior source as the primary path. Runtime activation requires `physics_file` plus either `model_file` or `behavior_graph`, and focused/static coverage rejects physics-only vehicles, model-only vehicles, and the old physics-primary fallback.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 2,288 assertions / 28 cases, and isolated `vehsrc` all-target build. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-878 effect authored-source activation requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` as both the mesh/fixed-point boundary and the broader private runtime bridge/cache rule.

The next confirmed source-chain failure point was `.pdeffect` activation. B-872 stopped shader metadata from counting as effect source, but the runtime adapter still accepted any caller-selected primary path when both `effect_file` and `timeline_file` were empty. That left a loophole where the outer `.pdeffect` path could mark an incomplete effect active even though no editable graph or timeline source was available.

Effect activation now requires `effect_file` or `timeline_file`; bare primary paths remain selected-path metadata only and cannot activate incomplete effect rows. Focused adapter coverage rejects primary-path-only effects, and static source-contract coverage pins that the effect activation case does not use `binding->primary_path` as proof of source.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842]` with 1,373 assertions / 13 cases, and isolated `effectsrc2` all-target build. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-875 material source activation requirement

Continued the all-asset parity goal on CPU-safe source-chain inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdmaterial`. Strict conformance now requires `material.json`, but the surrounding paths still allowed dependency archives to stand in for material source: checked-in examples lacked `material.json`, local scanning and network delivery could select `texture_archive` / `texture_file` as the primary source, and runtime activation accepted texture/effect dependencies as activation proof when `material_file` was missing.

Material dependencies remain runtime-visible source members, but they no longer stand in for authored material source. Local scan and network delivery select only `material_file` / `file_path` as the primary source, runtime activation requires the authored material source, checked-in material examples and their nested skin/weapon copies now carry `material.json`, and conformance requires descriptor/manifest agreement for `material_file = material.json`. Focused adapter coverage rejects dependency-only materials while still exposing texture/effect dependencies once the material source exists.

Verification passed `python tools\asset_native_source_guard.py`, checked-in example conformance with 28 root / 59 checked archives, retained-install conformance with 8,060 root / 9,061 checked archives, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters]` with 620 assertions / 5 cases, focused `[modding][pdxxx][c3842]` with 739 assertions / 8 cases, focused `[modding][pdxxx][runtime][c3838]` with 895 assertions / 11 cases, and focused `[modding][pdxxx][examples][static][c3812]` with 1,765 assertions / 3 cases. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-874 theme source activation requirement

Continued the all-asset parity goal on CPU-safe source-chain inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed source-chain failure point was `.pdtheme`. Strict conformance requires `theme.json`, but local scanning, network delivery, and runtime activation could still promote dependency archives such as UI/font/audio/music/effect as the primary file or activation proof when `theme_file` was missing.

Theme dependencies remain runtime-visible source members, but they no longer stand in for authored theme source. Local scan and network delivery select only `theme_file` / `file_path` as the primary source, and runtime activation requires the theme source member while keeping dependency archives distinct on the binding. Focused adapter coverage rejects dependency-only themes and static source-contract coverage pins the local/network/runtime rule.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters]` with 612 assertions / 5 cases, focused `[modding][pdxxx][c3842]` with 739 assertions / 8 cases, focused `[modding][pdxxx][runtime][c3838]` with 887 assertions / 11 cases, isolated `themesrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-873 selector metadata runtime parity

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed adapter failure point was metadata flattening in the generic runtime binding. `.pdarena`, `.pdbody`, `.pdhead`, `.pdprop`, `.pdweapon`, `.pdprojectile`, and `.pdentity` catalog rows already preserve names, language-name IDs, or unlock feature metadata through the public archive/catalog path, but runtime bindings only exposed source files plus generic runtime/kind slots.

Runtime bindings now expose `display_name`, `name_langid`, and `requirefeature` where those source-shaped fields exist on the catalog row. Arena, body, head, prop, weapon, projectile, and entity activation fill the named fields without making their legacy runtime integers public identity. Focused adapter coverage checks the activated bindings, and static source-contract coverage pins the field copies.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters]` with 604 assertions / 5 cases, focused `[modding][pdxxx][c3842]` with 739 assertions / 8 cases, focused `[modding][pdxxx][runtime][c3838]` with 879 assertions / 11 cases, isolated `selmeta` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-872 effect runtime source requirement

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

The next confirmed adapter failure point was `.pdeffect`. `assetRuntimeActivateCatalogEntry()` copied `effect_file`, `timeline_file`, and `shader_id`, but the activation check also treated `shader_id` as proof that the effect had an accessible source file. That could mark a shader-only effect row active even when the public archive/source member needed to rebuild or inspect effect behavior was missing. `shader_id` is now metadata only; effect activation requires a primary FileProvider path, `effect_file`, or `timeline_file`.

Focused runtime adapter coverage now rejects a shader-only effect and proves the same effect activates once `effect.graph.json` is present, while keeping the shader metadata visible on the binding. Static source-contract coverage pins that `shader_id` cannot be used as the file-presence argument.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters]` with 593 assertions / 5 cases, focused `[modding][pdxxx][c3842]` with 739 assertions / 8 cases, focused `[modding][pdxxx][runtime][c3838]` with 868 assertions / 11 cases, isolated `effectsrc` all-target build, and build-session cleanup. Live renderer/audio smoke remains paused under B-801.

## 2026-06-08 - B-871 gamemode/bot-profile runtime metadata parity

Continued the all-asset parity goal on CPU-safe metadata/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. The integer-only limitation remains recorded as a private runtime bridge/cache rule; this slice did not add public numeric selectors.

The next confirmed metadata failure point was the runtime adapter boundary for `.pdgamemode` and `.pdbotprofile`. Previous slices made descriptors, manifests, local scan, network delivery, and boot walking preserve selection metadata, but `assetRuntimeActivateCatalogEntry()` still exposed only the rules/profile source plus a few generic fields. That meant public archives could carry gamemode name, description, max-player bound, unlock feature, bot profile name-langid, and bot unlock feature, while runtime consumers had no named binding fields for them.

Runtime bindings now expose gamemode name, description, min/max players, team flag, and unlock feature, plus bot profile type, difficulty, body, name-langid, and unlock feature. Focused adapter coverage exercises real catalog entries for those fields, and static source-contract coverage pins the named runtime handoff so later archive work cannot regress into generic-slot flattening.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[modding][pdxxx][runtime][c3838][adapters]` with 586 assertions / 5 cases, focused `[modding][pdxxx][c3842]` with 739 assertions / 8 cases, focused `[modding][pdxxx][runtime][c3838]` with 861 assertions / 11 cases, isolated `rtmmeta` all-target build, and build-session cleanup. No live game, renderer, or audio smoke was run.

## 2026-06-08 - B-870 OGG source audio decoded-length parity

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` as both the mesh/fixed-point boundary and the broader private runtime bridge/cache rule for current integer-only subsystems.

After the recent WAV/MP3 source-backed audio fixes, the next audio failure point was in the shared standard-audio decoder used by file-backed SFX/voice playback and mod music helpers. `stb_vorbis_decode_memory()` and `stb_vorbis_decode_filename()` return decoded sample counts per channel, but the OGG loader treated that value as the total interleaved sample count. For stereo OGG sources, only half of the decoded PCM bytes were passed into `SDL_ConvertAudio()` or reported through `outLen` when conversion was unnecessary.

The OGG loader now names the value `samplesPerChannel`, rejects invalid channel counts, multiplies by `channels` before computing raw source bytes, and reports the same interleaved count when the source is already in the target format. Static audio coverage pins this exact per-channel-to-interleaved conversion and rejects the old total-sample calculation. This fixes a source translation bug for custom/user-created OGG audio; it does not add public numeric audio IDs or change the existing catalog-owned runtime bridge rules.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, focused `[catalog][provider][static]` with 11,246 assertions / 34 cases, isolated `oggaudio` all-target build, and build-session cleanup. No live audio smoke was run.

## 2026-06-08 - B-869 catalog dependency graph capacity contract

Continued the all-asset parity goal on CPU-safe dependency-closure inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness.

After B-868 removed the fixed runtime binding cap, the next shared scale surface was the catalog dependency graph used by manifest expansion. The implementation had already been changed to grow dynamically, but the public header still called the 256-entry starting size a hard cap and documented pair drops when the table was full. At the same time, `pd-tests` still supplied an empty `catalogDepForEach()` stub, so the test binary could not prove that dependency closure beyond the old first allocation survived.

The dependency header now describes 256 as the initial allocation only, and the implementation uses `CATALOG_INITIAL_DEP_PAIRS` to make that boundary explicit. `pd-tests` now links the real dependency graph instead of the empty stub. New focused coverage registers 320 dependency pairs, verifies first and last owners are iterable, proves duplicate pairs do not inflate the count, and keeps the existing bundled-owner behavior where base pairs are skipped during manifest expansion.

Verification passed focused `[modding][pdxxx][deps][c3844][source][static]` with 11 assertions / 2 cases, `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, and isolated `depgraph` all-target build. No live renderer or audio smoke was run.

## 2026-06-08 - B-868 asset runtime binding capacity

Continued the all-asset parity goal on CPU-safe runtime adapter inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` as both the mesh/fixed-point boundary and the broader private runtime bridge/cache rule for current integer-only subsystems.

The confirmed failure point was the shared runtime binding surface, not one extractor family. `asset_runtime` stored active bindings in a fixed `ASSET_RUNTIME_MAX_BINDINGS = 256` table. Retained generated installs and large custom packs can contain far more than 256 public source-backed archives, so valid catalog rows could scan, share, and activate through family-specific paths while generic runtime binding consumers stopped seeing later assets. That creates the same user-facing symptom class as missing extraction data: archives exist and gameplay can continue, but source-backed render/audio/metadata consumers can silently miss assets past the cap.

The binding table now grows dynamically from the old 256-entry initial capacity instead of treating 256 as a hard limit. `assetRuntimeReset()` preserves the allocated table for reuse, `s_allocBinding()` doubles capacity as needed, and focused adapter coverage activates 320 runtime bindings to prove large public archive sets no longer truncate at the old table size. This keeps public archive identity catalog-ID/source based; no public numeric slot or compatibility shape was added.

Verification passed focused `[modding][pdxxx][runtime][c3838][adapters]` with 575 assertions / 5 cases, `python tools\asset_native_source_guard.py`, scoped diff check with only the known bug-ledger line-ending warning, and isolated `runtimecap` all-target build. No live renderer or audio smoke was run.

## 2026-06-08 - B-867 Scenario source-renderer draw-target/state bridge

Continued the all-asset parity goal on CPU-safe Scenario rendering investigation because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` as both the mesh/fixed-point boundary and the broader private runtime bridge/cache rule for current integer-only subsystems.

B-801 proved the retained extracted `scene.glb` payloads are not empty or missing textures: all 87 `.pdscenario` archives build source-renderer CPU scene data with nonzero image counts. The next runtime integration failure point was the draw target/state bridge. `gfx_run()` rendered the source scene with outer window dimensions even though the active draw target can be the game draw area/framebuffer, and `scenarioSceneRendererRender()` changed the raw GL viewport without restoring it before the translated display-list renderer continued. A valid public source scene could therefore be scaled or placed against the wrong target and could leak viewport state into later props, sky, or UI display-list work while gameplay simulation still ran normally.

`scenarioSceneRendererRender()` is now called with `gfx_current_dimensions` so the source scene matches the active game draw area, and the renderer saves/restores `GL_VIEWPORT` around the source-scene draw. Static coverage now pins both requirements. Verification passed focused `[modding][pdxxx][c3844][source][static]` with 568 assertions / 5 cases, `python tools\asset_native_source_guard.py`, and isolated session-build cleanup. No live renderer smoke was run.

## 2026-06-08 - B-866 retained generated-install manifest repair

Continued the all-asset parity goal on CPU-safe retained-install validation because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike also asked to make sure the integer-only limitation was recorded; the canonical `context/constraints.md` and modding pillar now record it as private runtime bridge/cache debt, not a public archive-format choice.

The retained generated install initially failed strict conformance after the earlier audio/Scenario work. The first failure point was stale character `.pdanim` output: 950 archives still used the v3 GLTF generator, including 56 zero-frame clips with invalid accessor/sampler shape. Added `tools/upgrade_pdanim_v3_archives.py` as an offline repair path for retained installs. It only accepts archives whose public `animation.gltf` declares the old v3 generator, preserves non-zero GLTF channel data while restamping to v4, and rewrites zero-frame clips into the current v4 no-op placeholder shape. After applying it, `verify_pdanim_sources.py` passed across 1,060 animation archives.

The next conformance pass exposed stale metadata and weapon manifests. The public descriptors already contained the readable fields the runtime/tooling needs (`type_key`, `difficulty_key`, gamemode player/team fields, mission objectives/briefing members, and weapon graph/settings/shared-context members), but `_meta/manifest.json` lagged behind. Added `tools/upgrade_generated_manifest_archives.py` to repair retained generated manifests from public descriptors and to rename stale weapon descriptor keys (`shared_context`, `settings`, `variables`) to the current `*_file` names. It also recurses into weapon-embedded `.pdprojectile` / `.pdentity` archives so their graph/binding source members are present in nested manifests.

That second pass found a real producer contract bug: model-less base weapons such as `base_choppergun`, `base_hammer`, and `base_nothing` advertised `model_file = dependencies/assets/models/held_hi.pdmesh` even though no held mesh member existed. `romextract_pdweapon.c` now emits and requires `model_file` only when the weapon row has a held high model, and conformance treats a missing model file as valid only when the archive does not declare or embed one. This prevents fake public mesh source while still requiring model-bearing weapons to declare and embed their held mesh.

Full retained-install conformance now passes across 8,060 root archives / 9,061 checked archives and all first-class families. Verification also passed Python compile for the repair/conformance tools, examples conformance across 28 root / 59 checked archives, `python tools\asset_native_source_guard.py`, focused `[modding][pdxxx][c3844][source][static]` with 568 assertions / 5 cases, scoped diff check, isolated `manifestfix` all-target build, and build-session cleanup. A broad conformance run over `tests/fixtures` still finds intentionally stale fixture archives and folder-shaped fixtures; that was not counted as a live content failure.

## 2026-06-08 - B-865 MP3-backed `.pdvoice` source playback

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice targeted Mike's earlier report that some extracted audio sounded too fast or broken by checking the retained generated audio archives and the real source-backed playback path.

Generated audio conformance passed for all 2,108 retained `.pdsfx`, `.pdvoice`, and `.pdsong` archives. The remaining gap was not WAV metadata: the generated set contains 1,768 WAV `.pdsfx`, 117 WAV `.pdvoice`, 104 MP3-backed `.pdvoice`, and 119 sequence-backed `.pdsong` archives. Those 104 MP3 voice archives are valid public typed archives with `sample.mp3` source, but `sndStart()` routes catalog voice/SFX overrides through `audioStartFileSound()`, and that loader only accepted WAV through `SDL_LoadWAV`. A public `.pdvoice::sample.mp3` could therefore pass conformance while source-backed gameplay voice playback failed or fell back to ROM/static audio.

The file-backed SFX/voice path now reuses the standard audio decoder already used by mod music for WAV, MP3, and OGG. It converts to S16 stereo at 22050 Hz, reports the original source rate for loop-sample scaling, and keeps playback inside the SFX/voice state machine so native-style pitch, base pitch, sample pan/volume, key-volume, loop, envelope, FX mix, and FX bus behavior remain applied. This does not route voice through music playback and does not add a public numeric audio slot.

Verification passed `python tools\asset_native_source_guard.py`; generated audio conformance across 2,108 archives; scoped diff check; focused `[catalog][provider][static],[modding][pdxxx][c3842]` with 11,979 assertions / 42 cases; and isolated `audstd` all-target build. No live audio playback was run.

## 2026-06-08 - B-801 Scenario source-renderer CPU probe/material parser

Continued the all-asset parity goal on CPU-safe Scenario rendering investigation because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike also asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` and the parent mirror as both the mesh/runtime geometry boundary and the broader private runtime bridge rule.

This slice added a standalone `scenario-scene-probe` executable plus `scenarioSceneRendererProbeSource()` so real `.pdscenario::scene.glb` files can run through the same CPU scene build used by renderer activation without opening a window, uploading GPU resources, touching global renderer state, or launching the game. The probe reports vertex, draw-group, material, image, alpha-texture, alpha-material, and secondary-material counts, and has opt-in debug logging for the source-renderer build path.

The probe found the real failure point for extracted Airbase/Chicago was not missing geometry or texture source. File load and GLB JSON parse completed quickly, then the probe stalled at material parsing. `parseMaterials()` used generic `crude_json` value-level member helpers for every material/texture/sampler lookup, which became pathologically slow on real extracted material tables. The material parser now reads directly from parsed JSON objects for `pbrMetallicRoughness`, texture bindings, sampler wrap modes, and `extras.pd2_material.secondaryTexture`.

After the fix, the CPU probe passed all 87 extracted `.pdscenario` archives with no timeouts and no zero-image scenes (`vertices=1,209,192`, `groups=2,682`, `materials=2,772`, `images=3,174`). Representative Chicago now builds from public source with `vertices=19,992`, `groups=92`, `materials=93`, `images=98`, `alpha_textures=39`, and `alpha_materials=38`. This proves the retained extracted Scenario source payloads can build renderer CPU data; remaining broken live visuals after this point should be investigated in camera delivery, GPU upload, draw ordering, or render-state use rather than assuming the archive is empty.

Verification passed `python tools\asset_native_source_guard.py`; scoped diff check; focused `[modding][pdxxx][c3844][source][static],[modding][pdxxx][c3842]` with 1,273 assertions / 11 cases; explicit `scenario-scene-probe` target build; isolated `scenprobe` all-target build; and the 87/87 extracted Scenario CPU-probe sweep. No live game, renderer, or audio smoke was run.

## 2026-06-08 - B-864 `.pdanim` GLTF payload currentness gate

Continued the all-asset parity goal on CPU-safe animation/source inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice re-ran the strict archive gates over the retained extracted install to distinguish stale generated data from a current producer/runtime bug.

The extracted install still fails strict archive conformance in `.pdanim`: 950 character animation archives use the v3 GLTF generator, and the known zero-sample character clips still expose old invalid sampler/accessor output. Current code would regenerate those specific archives because their manifests are schema v3, so the retained install is stale data until a safe extraction pass runs. The producer weakness found here was narrower: `s_existingArchiveHasAnimPayloads()` decided whether to skip existing character `.pdanim` archives from archive membership and manifest metadata only. A mixed archive with a repaired v4 manifest but stale v3 `animation.gltf` could therefore be accepted as current.

Character animation extraction now uses a single `PDANIM_CHR_GENERATOR` constant for the generated GLTF payload and the currentness check. Existing archives are skipped only when `animation.gltf` itself declares the v4 generator, so public source payload currentness cannot be faked by manifest metadata alone.

Verification passed `python tools\asset_native_source_guard.py`; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\verify_pdanim_sources.py examples\modding\typed-pdxxx-basic --require-both-categories`; focused `[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 1,644 assertions / 23 cases; isolated `b864anim` all-target build; and isolated build cleanup. The retained smoke-install animation set remains stale v3 until B-801 permits safe regeneration or a non-live extraction path is added.

## 2026-06-08 - B-863 `.pdmesh` unresolved part-row hierarchy parity

Continued the all-asset parity goal on CPU-safe mesh/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice stayed on the broad B-769 broken-geometry class, but validated the extracted archive/runtime rebuild handoff instead of launching the game.

The 733 extracted `.pdmesh` archives now carry the expected hierarchy bundle: `model.nodes.json`, `model.parts.json`, `model.faces.json`, and `model.render.json` alongside OBJ/MTL source. That ruled out a blanket sidecar extraction miss. The concrete failure point was narrower: extraction preserves native part-table rows that do not resolve to a model node as `node = -1` with `node_unresolved = true`, but `generatedModeldefReadParts()` treated any negative node as fatal. Meshes such as `base_model_pdone`, `base_model_pdfour`, and `base_model_nlogo3` could therefore carry valid public hierarchy/render metadata yet fail the hierarchy-preserving generated modeldef rebuild.

The generated modeldef compiler now skips explicitly unresolved native part rows when constructing the runtime part table while leaving those rows in public metadata. This preserves the editable source record without inventing fake node bindings. The `.pdmesh` conformance gate now validates hierarchy sidecar coherence across nodes, parts, faces, render commands, OBJ face count, matrix bindings, and face draw coverage; duplicate native part numbers are allowed because current base archives contain them; explicit unresolved rows are allowed only with `node_unresolved = true`.

Verification passed Python compile for conformance; extracted mesh conformance across all 733 `.pdmesh` archives; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 9,136 assertions / 43 cases; isolated `b863mesh` all-target build; and isolated build cleanup. No live renderer smoke was run.

## 2026-06-08 - B-855 custom `.pdweapon` private slot lifecycle

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice followed the remaining custom weapon private-slot bridge through catalog clear/rebuild lifecycle rather than treating successful allocation as enough.

The confirmed failure point was stale bridge ownership. `assetCatalogClearMods()` removes non-bundled custom `.pdweapon` rows during mod rebuilds, but the private custom weapon slot map and `g_MpWeapons[]` custom bridge rows were not cleared with those rows. A removed custom weapon could therefore keep a private MP/runtime slot reserved, and a later scan could inherit stale slot/default state even though public source identity had changed.

`assetCatalogClear()` and `assetCatalogClearMods()` now reset the catalog-owned custom weapon slot bridge. This keeps integer slots private runtime migration debt while tying their lifetime to the catalog rows that selected the public `.pdweapon` source. Static coverage now pins both clear hooks.

Verification passed `python tools\asset_native_source_guard.py`; scoped diff check for the touched files; and focused `[input][menu_graph][room][static][c3814],[catalog-mgr-weapon],[spawn-weapon][pin],[spawn-weapon][catalog][pin],[random-pool],[matchsetup][spawn-weapon][b263]` with 1,138 assertions / 53 cases. No live game or renderer smoke was run.

## 2026-06-08 - B-855 custom `.pdweapon` private slot bridge

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice followed fully custom `.pdweapon` archives through the real held-weapon usage path instead of treating archive extraction alone as sufficient.

The confirmed failure point was the integer-only runtime bridge. Public `.pdweapon` archives can carry model source, held graph source, and preserved defaults, but the current weapon runtime still needs private `WEAPON_*` and MP weapon slots for loader-pool installation, graph registration, match selection, spawn selection, wire validation, bot state, and bgun equip paths. Asking modders to author numeric `weapon_id` values would violate the public-source/catolog-ID archive rule, so the fix had to keep those integers catalog-owned.

A staged bridge now reserves catalog-owned custom MP slots `0x29..0x32` and runtime slots `0x56..0x5f`. `assetcatalog_weapon_slots` allocates those slots from catalog IDs across data-root walking, local scanning, and network delivery; the loader pool can parse a custom weapon JSON payload into the assigned private runtime slot; base weapon static counts distinguish base MP rows from custom bridge rows; and net/bot/bgun validity checks accept the private range while preserving existing droppable/death restrictions.

This slice then followed the remaining MP spawn/default path. The confirmed follow-up failure point was the private MP row: the allocator created a usable catalog-owned slot, but it only filled `weaponnum`, `hasweapon`, and `extrascale`. Player and bot spawn paths still ask catalog wrappers for `g_MpWeapons[]` ammo and model defaults, so custom weapons with parsed function/ammo source could still spawn with empty/default ammo metadata.

The private bridge now refreshes custom MP row defaults when the loader pool parses a custom `.pdweapon` into its assigned private runtime slot. Primary and secondary ammo type/quantity derive from the parsed weapon function ammo index and inventory ammo records, with parsed bot preference target ammo used when present; the row model derives from the parsed high model; and the integer row remains a private catalog-owned cache.

The next failure point was source-present but runtime-invalid graph content in the checked-in custom weapon example. `tri_weapon.pdweapon` carried graph files, but those files used stale preview-only node kinds (`spawn.projectile`, `spawn.deployed_entity`), UI-style socket edge endpoints (`trigger.exec`), and exported trigger nodes instead of the held runtime action nodes the game can bind. The example generator now writes `spawn.fired_projectile` and `spawn.thrown_physical` action nodes, uses compiler-facing node-id edges, exports those action nodes, and keeps projectile/entity references as catalog IDs.

Static coverage now opens the actual checked-in `tri_weapon.pdweapon`, checks that the stale node kinds are gone, compiles the archive with the weapon graph compiler, registers it through the held runtime at `WEAPON_CUSTOM_START`, and verifies primary/secondary gameplay functions resolve to the expected projectile/entity opcodes. This prevents "archive has graph files" from being mistaken for "the held runtime can use this custom weapon."

This is not full custom-weapon base equivalence yet. Remaining work is direct catalog-ID consumers or richer authored gameplay/default payload coverage for graph/runtime paths that still assume base weapon rows. Public `.pdweapon` identity remains catalog IDs and source members, not modder-authored numeric slots.

Verification passed focused `[catalog-mgr-weapon],[spawn-weapon][pin],[spawn-weapon][catalog][pin],[random-pool],[matchsetup][spawn-weapon][b263],[input][menu_graph][room][static][c3814]` with 1,135 assertions / 53 cases; focused checked-in graph/archive/runtime tests with 800 assertions / 12 cases; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; scoped `git diff --check` with only the known `context/bugs.md` CRLF warning; isolated `b855defs` and `b855graph` all-target builds; detailed build-log error scans; and isolated build cleanup. The all-target verification initially exposed an unrelated `modasset_compiler.c` client compile blocker where `floor` / `ceil` were used without local prototypes despite the file's existing math declarations; the local declaration block now includes them, and the rerun log is clean. No live game or renderer smoke was run.

## 2026-06-08 - B-859 Scenario/Weapon manifest source-closure parity

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice followed the residual descriptor/manifest drift after B-858 instead of treating the audit output as automatically actionable.

Scenario drift was a verifier gap, not an immediate runtime-loss bug. `loader_walker_scenario.c` consumes loader-facing manifest keys such as `runtime_source`, `ai_lists`, `objectives`, `navigation`, `waypoints`, `paths`, and `level_graph`; the checked-in Scenario example already publishes those keys. Conformance only checked `scenario.ini`, though, so a stale or edited manifest could pass while boot walking restored different source members than the descriptor declared.

Weapon optional source/dependency closure was a real checked-in/custom archive gap. The sample `weapon.ini` declared `material_slots_file`, `grip_sockets_file`, `presentation_file`, `primary_projectile_archive`, `deployed_entity_archive`, `fire_sound_archive`, `idle_animation_archive`, and `reticle_archive`, but `_meta/manifest.json` only carried the core model/graph/settings fields. That left public files present in the archive without a manifest-walkable source closure.

Scenario conformance now requires descriptor/manifest agreement for the loader-facing source bundle. The weapon example generator now writes the optional dependency/source closure into `_meta/manifest.json`, conformance rejects weapon descriptor/manifest drift for those members, and static coverage pins both Scenario and Weapon contracts. No fake runtime fields were added where the current game has no consumer; the archive now delivers the data explicitly for manifest-driven walking and future/runtime consumers.

Verification passed direct archive inspection for Scenario source keys and Weapon optional dependency/source keys; Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 13,222 assertions / 64 cases; isolated `scenweaponmeta` all-target build; and isolated build cleanup. Full generated install conformance is still blocked by known stale `.pdanim` v3 archives while B-801 pauses live/smoke regeneration.

## 2026-06-08 - B-858 `.pdanim` manifest source-member parity

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. After B-857, the descriptor/manifest drift audit still showed `.pdanim` examples declaring editable GLTF source only in `animation.ini`.

The confirmed failure point was the checked-in/custom animation archive contract. `loader_walker_anim.c` consumes manifest keys such as `animation`, `runtime_source`, `command_source`, `commands_file`, `category`, and frame metadata. The example `.pdanim` archives contained valid `animation.gltf` and declared `animation_file = animation.gltf`, but published generic manifests. Native character animation extraction already emits loader-facing `animation = animation.gltf`, so this was not an extractor rewrite; it was an example/validation gap that could hide editable GLTF source from manifest-driven walking and tools.

The example generator now writes `.pdanim` manifest `category`, `frame_count`, `animation`, `runtime_source`, and `animation_file`, including the nested animation copy embedded in `tri_weapon.pdweapon`. Conformance now requires manifest category/source agreement for GLTF-backed animation archives and command-source agreement for command-backed animation archives. Static coverage pins the top-level and nested animation manifests.

Verification passed direct archive inspection for top-level and nested animation manifests; Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\verify_pdanim_sources.py examples\modding\typed-pdxxx-basic --require-both-categories`; `python tools\asset_native_source_guard.py`; residual drift audit no longer reporting `.pdanim`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 13,222 assertions / 64 cases; isolated `animmanifest` all-target build; and isolated build cleanup. Full generated install conformance is still blocked by known stale `.pdanim` v3 archives while B-801 pauses live/smoke regeneration.

## 2026-06-08 - B-857 `.pdprojectile` / `.pdentity` manifest source-member parity

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice followed the next descriptor/manifest drift hit after the `.pdmesh` fix through the projectile/entity graph source path.

The confirmed failure point was generated and checked-in projectile/entity manifest metadata. Local scan and network delivery read `projectile.ini` / `entity.ini`, but generated archive walking and source-facing tooling use `_meta/manifest.json` to restore public source members. A projectile/entity archive could therefore contain valid `behavior.graph.json`, `bindings.json`, and `composition.json` files while publishing a generic manifest that hid those files from runtime-visible graph/binding/composition metadata.

The shared typed archive writer now emits projectile/entity source-member manifest fields when those public entries are present, without stripping the dependency records it already generates. The checked-in example normalizer now rewrites top-level `.pdprojectile` / `.pdentity` samples before embedding them into `.pdweapon`, conformance rejects descriptor/manifest drift for those fields, and static coverage pins both writer and sample behavior.

Verification passed direct archive inspection for top-level and nested projectile/entity manifests; Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; a targeted source-member drift audit; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdxxx][writer][c3838]` with 13,309 assertions / 67 cases; isolated `promentmanifest` all-target build; and isolated build cleanup. Full generated install conformance is still blocked by known stale `.pdanim` v3 archives while B-801 pauses live/smoke regeneration.

## 2026-06-08 - B-856 `.pdmesh` manifest source-member parity

Continued the all-asset parity goal on CPU-safe mesh/archive inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. This slice used a descriptor/manifest drift audit across checked-in typed archives, then traced the strongest rendering-adjacent hit through the real base mesh registration path before patching.

The confirmed failure point was editable/custom `.pdmesh` manifest metadata. `loader_walker_mesh.c` restores the model source from `_meta/manifest.json` key `geometry`, defaulting to `model.obj` when the field is absent. The checked-in editable sample contained only `model.gltf` and declared `model_file = model.gltf` in `mesh.ini`, but published a generic manifest. That meant a manifest-driven registration path could point at missing `model.obj` even though valid GLTF source was present.

The sample generator now writes `.pdmesh` manifest `geometry = model.gltf` and `model_file = model.gltf`, the checked-in `tri_mesh.pdmesh` archive was regenerated, conformance rejects `.pdmesh` descriptor/manifest source drift, and static coverage pins the sample manifest. Native extracted meshes already emit `geometry = model.obj`, so generated base meshes stay aligned with the same manifest key.

Verification passed direct archive inspection; Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; generated mesh conformance across 733 `.pdmesh` archives; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 13,222 assertions / 64 cases; isolated `meshmanifest` all-target build; and isolated build cleanup. No live game, renderer, or audio smoke was run.

## 2026-06-08 - B-854 `.pdtexture` manifest source-member parity

Continued the all-asset parity goal on CPU-safe archive/runtime inspection because live renderer, smoke, and audio launches remain paused under B-801 after prior PC unresponsiveness. Mike asked to make sure the integer-only limitation was recorded; it remains recorded in `context/constraints.md` and the parent mirror as both the mesh/runtime geometry boundary and the broader private runtime bridge for current integer slots.

First traced fully custom `.pdweapon` archives through the held-weapon runtime handoff. Public archive source can carry graph/model/default metadata, but the current runtime still needs private `WEAPON_*` / MP weapon slots for graph registration, match selection, spawn selection, wire validation, and manager lookup. That is not a public archive-format problem, so it was recorded as B-855 instead of adding numeric fields to modder-authored weapon archives.

The patchable failure point was the editable/custom texture archive shape. Native texture extraction already emits manifest `texture_file`, but the checked-in `.pdtexture` example only named `texture.png` in `texture.ini` and published a generic manifest. Registration and tooling that rely on manifest metadata could therefore see a zip-openable archive while losing the actual public image source member. The sample generator now writes `texture_file` into the texture manifest, the checked-in `tri_texture.pdtexture` archive was regenerated, conformance rejects descriptor/manifest drift for `.pdtexture`, and static sample coverage pins the manifest field.

Verification passed Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 13,222 assertions / 64 cases; isolated `texmanifest` all-target build; and isolated build cleanup. No live game, renderer, or audio smoke was run.

## 2026-06-08 - B-853 `.pdhud` manifest source-member parity

Continued the all-asset parity goal on CPU-safe HUD archive inspection because live renderer/smoke launches remain paused under B-801. The integer-only limitation remains recorded in `context/constraints.md` and the parent mirror; this slice targeted a separate metadata/manifest contract hole.

The confirmed failure point was the editable/custom HUD archive shape. Native HUD extraction already emits `layout_file` in `_meta/manifest.json`, but the checked-in `.pdhud` example declared `texture_file = texture.png` and `layout_file = layout.json` only in `hud.ini` while publishing a generic manifest. `loader_walker_meta.c` restores HUD source members from the manifest, not the descriptor, so a user-created or sample-derived HUD archive could be structurally valid and still lose texture/layout visibility at boot-time registration.

The sample generator now writes `texture_file` and `layout_file` into the HUD manifest, the checked-in `tri_hud.pdhud` archive was regenerated, conformance now rejects HUD descriptor/manifest drift for layout and optional texture sources, and static coverage pins the sample manifest fields. Verification passed Python compile for source/conformance/example/animation verifier tools; checked-in all-family conformance across 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 13,222 assertions / 64 cases; isolated `hudmanifest` all-target build; and isolated build cleanup. No live game or renderer smoke was run.

## 2026-06-08 - B-847 custom typed-archive distribution scaling

Continued the all-asset parity goal on CPU-safe networking/distribution inspection because live renderer/smoke launches remain paused under B-801 after prior PC unresponsiveness. After the all-family catalog advertisement work, the next question was whether valid user-created assets could still be lost when a host shares a large custom pack.

The confirmed failure point was fixed legacy count limits in the distribution path. `SVC_CATALOG_INFO` advertised every relevant family but collected entries into a 256-row buffer. Client catalog diff and manifest ready-gate paths also used fixed 256-row missing lists, with `CLC_MANIFEST_STATUS` carrying only a `u8` missing count. The server transfer queue was fixed at 64 entries. That meant a large custom set of meshes, arenas, characters, weapons, audio, or other typed archives could exceed the host advertisement, missing-list, or transfer queue limit and arrive incomplete on clients even though local archive extraction/loading was valid.

The distribution wire shape now batches `SVC_CATALOG_INFO` with total/offset/count metadata, reads catalog diff and manifest-status missing lists from heap-backed `u16` counts, and grows the server transfer queue instead of dropping after 64 queued assets. `NET_PROTOCOL_VER` is bumped to v50 so mixed v49/v50 clients reject each other instead of misreading the new packet shape. Source-only typed archives remain catalog-ID based; this change only removes stale fixed integer count caps from the sharing path.

Verification passed `python tools\asset_native_source_guard.py`; Python compile for source/conformance tools; checked-in all-family example conformance across 28 root / 59 checked archives; a stale fixed-cap scan with only deliberate negative static-test assertions remaining; scoped diff check with only the known `context/bugs.md` CRLF warning; focused `[modding][pdmod][static][c3809],[net][lifecycle][manifest][security][static],[versions]` tests with 1,113 assertions / 21 cases; isolated `distribscale` all-target build; and isolated build cleanup. No live renderer/smoke was run.

## 2026-06-08 - B-846 `.pdmission` manifest source-member parity

Continued the all-asset parity goal on CPU-safe mission archive inspection because live renderer/smoke launches remain paused under B-801. The next gap came from comparing the actual extractor output shape with the boot-time metadata walker: `.pdmission` archives carried public `mission.graph.json`, `objectives.json`, and `briefing.json`, but only the descriptor declared the full source bundle.

The confirmed failure point was `port/src/romextract_pdmeta.c` manifest emission. `_meta/manifest.json` declared the Scenario archive and mission graph, but omitted `objectives_file` and `briefing_file`. `loader_walker_meta.c` restores mission source members from those manifest keys during boot-time public-source registration, so extracted base missions could become graph-visible while objectives and briefing source were left as archive-only files. This was the same class as earlier metadata-family flattening bugs: the source files existed, but the runtime-facing registration contract did not keep all of them visible.

Extraction now writes complete mission manifest metadata for catalog ID, Scenario archive/cache, mission graph, objectives, and briefing. `tools\asset_archive_conformance.py` now has a mission source contract that checks `mission.ini`, `_meta/manifest.json`, `mission.graph.json`, `objectives.json`, and `briefing.json` agree, and validates `briefing.json` as semantic `pd2.mission.briefing.v1` JSON rather than only accepting a filename. Verification passed Python compile for conformance/example/source-guard tools; checked-in all-family conformance across 28 root / 59 checked archives; an in-memory malformed `.pdmission` manifest rejection for missing `objectives_file` and `briefing_file`; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 9,083 assertions / 43 cases; isolated `missionmeta` all-target build; and isolated build cleanup. No live renderer/smoke was run because B-801 remains paused after prior PC unresponsiveness.

## 2026-06-08 - B-845 direct prop/vehicle model-source integer boundary

Continued the all-asset parity goal on CPU-safe mesh/custom-geometry inspection because live renderer/smoke launches remain paused under B-801. Mike asked to make sure the integer-only limitation was recorded; the active constraints already record the broader integer/fixed-point runtime boundary, and this slice tightened the enforcement note so direct `.pdprop` / `.pdvehicle` model sources are covered beside `.pdmesh`.

The confirmed failure point was propagation of the B-844 validator. Body/head/weapon/projectile/entity visual geometry routes through `.pdmesh` dependencies, but prop and vehicle archives can carry `model.obj`, `model.gltf`, or `model.glb` directly. Those direct model members could pass conformance without the native signed-16-bit coordinate, 1:32 UV, GLTF-buffer, and collapse checks that `.pdmesh` now receives. Prop metadata also assumed `model.gltf`, while vehicle metadata did not require the model/physics/behavior bundle to be declared consistently.

Conformance now validates every direct model source member through the same native-boundary checker for `.pdmesh`, `.pdprop`, and `.pdvehicle`; prop and vehicle descriptors/manifests must name the actual model member, and vehicles must declare `physics.json` plus `behavior.graph.json`. The checked-in prop and vehicle examples now use the valid triangle GLTF source instead of placeholder empty geometry. Verification passed Python compile for conformance/example tools; checked-in all-family conformance across 28 root / 59 checked archives; generated prop conformance across 8 `.pdprop` archives; generated vehicle conformance across 1 vehicle root / 2 checked archives; in-memory malformed prop GLTF and vehicle OBJ rejection proofs; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 9,077 assertions / 43 cases; isolated `modelsrc` all-target build; and isolated build cleanup. No live renderer/smoke was run because B-801 remains paused after prior PC unresponsiveness.

## 2026-06-07 - B-844 `.pdmesh` integer-native import boundary

Continued the all-asset parity goal on CPU-safe mesh/custom-geometry inspection because live renderer/smoke launches remain paused under B-801. Mike asked to ensure the integer-only mesh limitation was recorded; it already lived in `context/constraints.md`, so this slice checked whether the runtime compiler and public archive validator actually enforced that boundary for user-created geometry.

The confirmed failure point was the generated modeldef compiler boundary. `.pdmesh` OBJ/GLTF source parsed authoring coordinates and UVs as floats, then `fillGeneratedVertex()` clamped them directly into native `Vtx` s16 coordinate and 1:32 UV fields. That made DCC-friendly source usable, but it also meant a custom mesh could silently overflow or collapse after native quantization and reach the renderer as different geometry. Added an explicit integer-native validation step before generated modeldef conversion: non-finite or out-of-range coordinates/UVs now reject with a clear compiler warning, while native-quantized collapsed triangles log a warning so authored scale/collapse issues are visible. Collapse remains a warning rather than a hard error because 30 current extracted base meshes contain at least one collapsed native triangle and must still load as base-equivalent content. Public `.pdmesh` conformance now mirrors the hard overflow boundary for `model.obj`, `model.gltf`, and `model.glb`, including rejection of GLTF external binary buffers.

Verification passed `python -m py_compile tools\asset_archive_conformance.py`; checked-in all-family conformance across 28 root / 59 checked archives; generated mesh conformance across 733 `.pdmesh` archives; in-memory malformed `.pdmesh::model.gltf` external-buffer rejection; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 9,077 assertions / 43 cases; isolated `meshgltf` all-target build; and isolated build cleanup. No live renderer/smoke was run because B-801 remains paused after prior PC unresponsiveness.

## 2026-06-07 - B-843 source-backed audio FX send/return parity

Continued the all-asset parity goal on CPU-safe audio runtime inspection because live game/audio launches remain paused under B-801. After B-842, archive metadata and WAV headers matched, and prior slices already preserved pitch, pan, volume, key-volume, loop, envelope, and FX state. The remaining audio risk was whether preserved FX state actually affected source-backed playback.

The confirmed failure point was `port/src/audio.c::audioMixFileSoundsInto()`. `sndStart()` passed `fxmix`, `fxbus`, and key-derived FX mix offset into `audioStartFileSound()`, and file-backed handles updated `effective_fxmix` on FX events, but the mixer only used volume, pan, pitch, loop, and envelope. Public WAV-backed SFX/voice therefore had no audible aux/reverb-style return even when native `n_sndplayer` would route the voice through an FX bus. Added a bounded two-bus file-sound FX send/return path: dry samples are sent according to `effective_fxmix`, routed by `state.fxbus`, accumulated into a small delay/feedback buffer, returned into the combined frame, and kept alive while tails decay after the dry file sound releases.

Verification passed `python tools\asset_native_source_guard.py` and focused `[catalog][provider][static],[modding][pdxxx][c3842],[modding][pdxxx][c3844]` with 19,364 assertions / 62 cases. No live audio playback was run. This is a bounded parity step that makes FX state audible for source-backed WAVs; exact N64 aux/reverb tuning remains an audible verification target for a bounded harness or cautious playtest.

## 2026-06-07 - B-842 `.pdsfx` / `.pdvoice` WAV metadata/header parity

Continued the all-asset parity goal on CPU-safe audio inspection because live game/audio launches remain paused under B-801. The target was Mike's earlier observation that some extracted audio played too fast or sounded broken. Recent slices had fixed runtime pitch, pan, volume, key-volume, and FX state, so this pass checked the public archive boundary for stale or contradictory WAV source metadata.

The confirmed gap was that strict conformance required readable `sample_rate_hz` and `decoded_sample_count` metadata, but did not compare those fields to the actual `sample.wav` RIFF/WAVE header. Since runtime playback uses `SDL_LoadWAV` while catalog duration and loop scaling use archive metadata, a stale or hand-edited `.pdsfx` / `.pdvoice` archive could claim one sample rate or decoded frame count and play another. Conformance now parses `sample.wav`, requires PCM16 mono source for native SFX/voice parity, validates block alignment and byte rate, and rejects descriptor or manifest sample-rate/count mismatches. The checked-in example generator now writes deterministic valid PCM16 WAV source instead of preserving placeholder sample bytes.

Verification passed `python -m py_compile tools\build_typed_pdxxx_examples.py tools\asset_archive_conformance.py`; `python tools\build_typed_pdxxx_examples.py`; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families --max-errors 20`; `python tools\asset_archive_conformance.py --root .claude\smoke-verify-install\data\ntsc-final\audio --max-errors 20` with 2,108 checked audio archives; `python tools\asset_native_source_guard.py`; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,185 assertions / 28 cases. No live game or audio playback test was run. Remaining audio risk is audible N64 aux/reverb synthesis and subtle mixer behavior that still needs a bounded harness or cautious playtest.

## 2026-06-07 - B-841 `.pdanim` sampler/accessor source parity

Continued the all-asset parity goal on CPU-safe animation inspection because live game/smoke launches remain paused under B-801. The `.pdanim` path already had semantic glTF source, v4 zero-frame placeholder extraction, command-source validation, and runtime compiler support for `STEP` versus `LINEAR`, but the public verification layer was weaker than the runtime compiler.

Tightened `tools\verify_pdanim_sources.py` and `tools\asset_archive_conformance.py` so public `.pdanim` glTF source must use sampler interpolation accepted by the compiler (`LINEAR` or `STEP`), float `SCALAR` input time accessors, and output accessors matching the channel path: float `VEC3` for translation/scale and float `VEC4` for rotation. Static tests now pin those verifier strings alongside the stale-generator and zero-frame placeholder checks.

Verification passed `python -m py_compile tools\asset_archive_conformance.py tools\verify_pdanim_sources.py tools\asset_native_source_guard.py`; `python tools\verify_pdanim_sources.py examples\modding\typed-pdxxx-basic --require-both-categories --max-errors 20`; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families --max-errors 20`; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,180 assertions / 28 cases; isolated `animshape` all-target build; and isolated build cleanup. The local `.claude\smoke-verify-install\data\ntsc-final\animations` tree intentionally fails the new verifier with 950 stale v3 generator archives plus zero-sample accessor failures; refreshing that tree requires the next safe extraction/client boot path, so no live game or smoke launch was run.

## 2026-06-07 - B-840 Scenario material metadata completeness

Continued the all-asset parity goal on CPU-safe Scenario material inspection because live game/smoke launches remain paused under B-801 after prior PC unresponsiveness. The Chicago screenshot symptom still looked like broken level rendering, but the Scenario source path audit showed a more specific material-contract risk: extraction and the source renderer preserve and consume `extras.pd2_material`, while the non-visual verifier did not require every native material field the runtime expects.

Tightened Scenario `scene.glb` conformance so stale or hand-edited GLBs cannot pass after dropping material semantics. The validator now requires complete `pd2_material` blocks, named wrap modes, integer native fields for `offset`, `shift_s`, `shift_t`, `min_lod`, and `tile_flag`, secondary texture bindings on runtime UV set 1, and no orphan `secondary_image` values without a `secondaryTexture` object. The standalone checker now reports complete material counts. Mike's integer-only mesh limitation remains recorded as an active constraint in `context/constraints.md`: DCC-friendly source is allowed, but extraction/import must preserve or reproducibly quantize into the integer/fixed-point runtime domain.

Verification passed `python -m py_compile tools\asset_archive_conformance.py tools\verify_scene_glb_texture_contract.py tools\asset_native_source_guard.py`; `python tools\verify_scene_glb_texture_contract.py .claude\smoke-verify-install\data\ntsc-final --require-secondary` with 132 checked scene GLBs, 4,446/4,446 complete material blocks, and 10 secondary texture bindings; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,174 assertions / 28 cases; isolated `sceneextra` all-target build; and isolated build cleanup. No live game or smoke launch was run because B-801 remains paused.

## 2026-06-07 - B-838 `.pdprojectile` / `.pdentity` runtime binding parity

Continued the all-asset parity goal on CPU-safe runtime-binding inspection because live game/smoke/audio launches remain paused under B-801. The confirmed gap was not extraction, distribution, or graph compilation: projectile and entity archives already preserve `behavior.graph.json`, model source, projectile transition `entity_ref`, and entity `archetype` through local scan and network delivery, and catalog metadata activation already compiles their behavior graphs with `weaponGraphRuntimeRegisterBehaviorGraphJson()`. The missing handoff was the shared runtime adapter surface. `assetRuntimeSupportsType()` omitted `ASSET_PROJECTILE` and `ASSET_ENTITY`, so behavior assets could become graph-runtime active while remaining invisible to consumers that inspect public source members and semantic metadata through `asset_runtime`.

Added `ASSET_PROJECTILE` and `ASSET_ENTITY` support to `asset_runtime`. Projectile bindings now expose the behavior graph as authored source, the model file as a dependency member, and `entity_ref` as the transition target. Entity bindings now expose the behavior graph, model file, and archetype. Focused adapter coverage now counts and inspects both families, and the broader native-source contract pins the new runtime cases.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842]` with 893 assertions / 11 cases; isolated `projentbind` all-target build; scoped `git diff --check` with only the known `context/bugs.md` line-ending warning; and isolated build cleanup. No live game, smoke, or audio playback test was run.

## 2026-06-07 - B-837 `.pdlang` runtime binding parity

Continued the all-asset parity goal on CPU-safe runtime-binding inspection because live game/smoke/audio launches remain paused under B-801. The confirmed gap was not extraction or catalog registration: `.pdlang` archives already preserved `strings.json`, `source_bank` / `bank_id`, locale, category, and string count through scan, network delivery, and boot walking, and `s_catalogLoadEntryLangPayload()` compiled the language bank with `langManifestEnsureId()`. The missing handoff was the generic runtime adapter surface. `assetRuntimeSupportsType()` did not include `ASSET_LANG`, so a language row could become runtime-active while still being invisible to runtime binding consumers that inspect public source members and metadata.

Added `ASSET_LANG` support to `asset_runtime`, including explicit binding fields for locale, category, and string count. The binding now carries the public `strings_file` as the authored source, the language bank id as `runtime_id`, and the semantic language metadata beside it. Language payload activation now registers this binding after the language bank compile succeeds and rolls back the catalog activation if the public strings source is missing.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `langbind` tests build; focused `[modding][pdxxx][runtime][c3838][adapters],[catalog][provider][static],[modding][pdxxx][c3842]` with 12,048 assertions / 45 cases; isolated `langbind` all-target build; and isolated build cleanup. No live game, smoke, or audio playback test was run.

## 2026-06-07 - B-836 `.pdsong` source-before-ROM sequence playback

Continued the all-asset parity goal on CPU-safe music/runtime inspection because live game/smoke/audio launches remain paused under B-801. The confirmed gap was source-path ordering in `seqPlay()`: the function checked `g_SeqRomAddrs[seq->tracknum] < 0x10000` before trying public `.pdsong` stream playback or editable sequence source. That made source-backed songs depend on a valid legacy ROM sequence slot before the public source loader had a chance to run, and the fallback branch did not explicitly guard table/count bounds before reading legacy sequence metadata.

Moved the public source attempts ahead of the legacy ROM-address gate. `seqPlay()` now calls `modSequencePlayAudioSource()` and `modSequenceLoad()` first, then validates `g_SeqTable`, `g_SeqRomAddrs`, `seq->tracknum`, table bounds, and the legacy ROM address only when falling back to ROM sequence bytes. Oversized external sequence buffers are freed before returning failure. This preserves the current source-backed stream behavior and makes editable sequence source authoritative before legacy fallback checks.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `songguard` tests build; focused `[catalog][provider][static],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 12,768 assertions / 57 cases; isolated `songguard` all-target build; and isolated build cleanup. No live game, smoke, or audio playback test was run. Fully custom user-created sequence songs still need runtime slot/import handling so they are not constrained to existing sequence slots.

## 2026-06-07 - B-835 source-backed audio key-volume table parity

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live game/smoke launches remain paused under B-801. The confirmed gap was another runtime translation mismatch in source-backed SFX/voice handles. Native `n_sndplayer` keeps a per-key volume table in `var8009c334`, exposes it through `func00033ec4()`, and updates native sound-player states when `sndSetSfxVolume()` posts the key-volume event. File-backed public WAV handles were not on that native state list and did not consult the key-volume table during mix, so global/key-channel volume changes could diverge from native ALSound playback.

Updated `sndStart()` to pass `key_min & 0x1f` into `audioStartFileSound()`. File-backed sound handles now store that key-volume index, compute effective mixer volume from raw gameplay volume, sample volume, and `func00033ec4(index)`, and recompute the effective value during mix. The key-volume helper returns the native table value directly so a real zero volume remains silent instead of falling back to full volume.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `audkeyvol` tests build; focused `[catalog][provider][static],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 12,764 assertions / 57 cases; isolated `audkeyvol` all-target build; and isolated build cleanup. No live game, smoke, or audio playback test was run; remaining audio risk is audible mixer/aux/reverb parity that still needs a bounded harness or cautious playtest.

## 2026-06-07 - B-834 source-backed audio pan/volume sample-metadata parity

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live game/smoke launches remain paused under B-801. The confirmed gap was another runtime translation mismatch in source-backed SFX/voice handles. Native `n_sndplayer` stores gameplay pan and volume separately from `sound->samplePan` and `sound->sampleVolume`, then reapplies the sample metadata when pan or volume events arrive. The file-backed bridge folded sample pan/volume into the start pan/volume, so later `AL_SNDP_PAN_EVT` or `AL_SNDP_VOL_EVT` updates could drop the native sample metadata.

Added separate `sample_pan` and `sample_volume` storage to file-backed sound handles, extended `audioStartFileSound()` to receive raw gameplay pan/volume plus sample metadata, kept `sndstate.pan` / `sndstate.vol` as the gameplay values, and made the mixer use recomputed effective pan/volume at start and on later events. The source guard and static tests now pin this representation beside the existing base-pitch and FX state checks.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `audpan` tests build; and focused `[catalog][provider][static],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 12,746 assertions / 57 cases. The isolated build directory was removed. No live game, smoke, or audio playback test was run; remaining audio risk is audible source-backed mixer parity that still needs a bounded harness or cautious playtest.

## 2026-06-07 - B-833 source-backed audio repitch base-pitch parity

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live game/smoke launches remain paused under B-801. The confirmed gap was a runtime translation mismatch in source-backed SFX/voice handles. Native `n_sndplayer` stores gameplay pitch in `state->pitch`, keymap pitch in `state->basepitch`, and sends `state->pitch * state->basepitch` to the synthesizer. The file-backed bridge applied keymap base pitch at start, but only stored the combined step, so later `AL_SNDP_PITCH_EVT` events replaced the step with gameplay pitch and dropped keymap base pitch.

Added separate `base_pitch` storage to file-backed sound handles, extended `audioStartFileSound()` to receive gameplay pitch plus base pitch, initialized `sndstate.basepitch`, and made both initial playback and later pitch events use `pitch * base_pitch`. The source guard and static tests now pin this representation instead of the older combined `filepitch` contract.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `audpitch` tests build; and focused `[catalog][provider][static],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 12,723 assertions / 57 cases. The isolated build directory was removed. No live game, smoke, or audio playback test was run; remaining audio risk is audible source-backed mixer parity that still needs a bounded harness or cautious playtest.

## 2026-06-07 - B-832 audio archive duration preservation

Continued the all-asset parity goal on CPU-safe audio/runtime inspection because live game/smoke launches remain paused under B-801 after prior PC unresponsiveness. The integer-only mesh limitation is recorded in `context/constraints.md`: mesh archives may be user-editable and DCC-openable, but import/extraction must preserve or reproducibly quantize into integer-native runtime geometry.

The confirmed audio failure point in this slice was metadata flattening during archive walking. Extracted `.pdsfx` and `.pdvoice` archives already carry public `sample_rate_hz` and `decoded_sample_count`, but `loader_walker_sfx.c` and `loader_walker_voice.c` registered `duration_ms = 0`. `loader_walker_song.c` also re-registered base music rows with `duration_ms = 0`, wiping the base catalog duration while overlaying the extracted `.pdsong` source path. SFX/voice walking now derives duration from sample metadata, and song walking preserves the existing base music duration before registering the public source overlay.

Verification passed `python tools\asset_native_source_guard.py`; `python -m py_compile tools\asset_native_source_guard.py`; scoped `git diff --check`; isolated `audparity` tests build; and focused `[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812],[catalog][provider][static]` with 12,714 assertions / 57 cases. The isolated build directory was removed. No live game, smoke, or audio playback test was run; remaining audio risk is audible source-backed mixer parity, including any still-fast or garbled playback edge cases that need a bounded harness or cautious playtest.

## 2026-06-07 - B-831 `.pdweapon` held graph lifecycle activation

Continued the all-asset parity goal on CPU-safe runtime lifecycle inspection because live game/smoke launches remain paused under B-801 after prior PC unresponsiveness. The confirmed failure point was not the base `.pdweapon` walker: `loader_walker_weapon.c` already reads the runtime `WEAPON_*` slot and calls `weaponGraphRuntimeRegisterWeaponArchive()` for base archive walking. The gap was the generic catalog metadata lifecycle in `assetcatalog_load.c`, which treated projectile/entity graph JSON as runtime-active but omitted held `.pdweapon` archive graph registration.

Added `ASSET_WEAPON` to the graph lifecycle path, separated held weapon archive registration from projectile/entity JSON graph registration, stripped `archive.pdweapon::member` source paths back to the owning archive before compiling, and released held graph functions with `weaponGraphRuntimeClearWeapon()` instead of the projectile/entity asset-id clear path. Archive-backed weapons with no runtime weapon slot now fail loudly because the held-weapon runtime is still slot-indexed; custom user-created weapons need a catalog-owned runtime slot allocator before they can function exactly like base weapons. Mike's mesh integer-only limitation is already recorded in `context/constraints.md`: public mesh sources can be DCC-friendly, but import/extraction must preserve or reproducibly quantize to integer-native runtime geometry.

Verification passed `python tools\asset_native_source_guard.py`; scoped `git diff --check` with only the known `context/bugs.md` line-ending warning; isolated `weaponlife` tests build; focused held weapon lifecycle test with 53 assertions / 1 case; `[modding][pdxxx][weapon_graph][runtime]` with 223 assertions / 6 cases; `[modding][pdxxx][c3842][source][static]` with 91 assertions / 1 case; and `[modding][pdxxx][runtime][c3838][adapters]` with 161 assertions / 3 cases. No live game or smoke launch was run because B-801 remains paused after prior PC unresponsiveness.

## 2026-06-07 - B-830 strict typed-archive source path qualification

Continued the all-asset parity goal on CPU-safe scanner validation because live game/smoke launches remain paused under B-801. The confirmed failure point was the source-path qualifier registry in `assetcatalog_scanner.c`: direct scans of zip-openable `.pdxxx` archives parsed the inner descriptor and then qualified only the known source keys to `archive.pdxxx::member` paths. Many strict source fields added by the recent parity sweep were missing from that registry, so the data could exist in the archive and still be registered as a loose relative file path.

Expanded the folder, direct typed-archive, and `.pdmod` archive descriptor qualifier lists to include strict source/dependency fields for Scenario links and navigation JSON, mesh/body/head archive members, weapon graph/settings/dependency members, prop/entity/projectile behavior graphs, UI layout and nine-slice files, theme dependencies, font glyphs, effect timelines, and related source members. Added static coverage to keep all three qualifier surfaces in sync. Mike's integer-only mesh limitation is recorded in constraints and tasks: these path fixes make source members reachable, but mesh extraction/import still must preserve or explicitly quantize into integer-native geometry semantics. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; `python tools\asset_native_source_guard.py`; `git diff --check` with only the known `context/bugs.md` line-ending warning; focused `[modding][pdxxx][c3842][source][static],[modding][pdmod][static][c3809]` tests with 1,137 assertions / 17 cases; broader `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3844][source][static]` tests with 1,547 assertions / 21 cases; and isolated `qualkeys` build cleanup. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-829 `.pdgamemode`/`.pdbotprofile` metadata source propagation

Continued the all-asset parity goal on CPU-safe gamemode/botprofile archive validation because live game/smoke launches remain paused under B-801. The confirmed flattening point was boot metadata walking: the strict archives had `gamemode.ini` / `botprofile.ini` source fields, but `_meta/manifest.json` was generic and `loader_walker_meta.c` restored only `rules_file` or `profile_file`. That could leave gamemode player/team/unlock metadata and bot type/difficulty/body/target metadata behind when strict archives were discovered from public source.

Added rich example manifests for gamemode and bot-profile archives, restored gamemode and bot-profile metadata in the metadata walker from readable keys plus numeric tuning fields, and tightened conformance so missing manifest declarations fail. Static/runtime tests now pin scanner, network delivery, metadata walking, runtime activation, and the existing audio primary-file behavior discovered while rerunning focused tests. Verification passed: example regeneration; direct gamemode/botprofile archive inspection; Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; malformed manifest rejection proof for missing `rules_file`/`max_players` and `target_body`/`profile_file`; `python tools\asset_native_source_guard.py`; isolated `botmode` focused tests with 13,888 assertions / 77 cases; isolated build cleanup; and no leftover game/test/build process. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-828 `.pdarena` Scenario source propagation

Continued the all-asset parity goal on CPU-safe arena archive validation because live game/smoke launches remain paused under B-801. The confirmed `.pdarena` flattening point was the Scenario wrapper boundary: the clean archive and checked-in example already declare `scenario = example:tri_scenario` and embed `dependencies/assets/scenarios/tri_scenario.pdscenario`, but `asset_entry_t.ext.arena` and the runtime adapter only preserved stage/load metadata.

Added `scenario_id` and `scenario_archive` to arena catalog rows, made local scan and network distribution read and prefer `scenario_archive` as the arena primary source, added `.pdarena` to metadata-family walking, and added an `ASSET_ARENA` runtime binding that exposes both the target Scenario catalog ID and embedded Scenario archive. Regenerated `tri_arena.pdarena` so `arena.ini` and `_meta/manifest.json` declare the Scenario dependency, and added arena conformance so stale archives missing those declarations fail. Verification passed: example regeneration; direct arena archive inspection; Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; malformed arena rejection proof for missing manifest `scenario` / `scenario_archive`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `arenasrc` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[modding][pdxxx][c3844][source][static]` with 3,236 assertions / 46 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-827 `.pdbody`/`.pdhead` mesh and hand source propagation

Continued the all-asset parity goal on CPU-safe body/head archive validation because live game/smoke launches remain paused under B-801. The confirmed `.pdbody` / `.pdhead` flattening point was the strict mesh source boundary: the clean archives and checked-in examples already contain `mesh.pdmesh`, and body archives also contain `hand.pdmesh`, but `asset_entry_t.ext.body` and `asset_entry_t.ext.head` did not retain `mesh_archive` / `hand_archive` as first-class source members. Local scan, network distribution, boot-time metadata walking, `.pdmod` validation, and runtime adapter activation could therefore leave those meshes as archive-only data after extraction or sharing.

Added body/head mesh archive fields to catalog rows, made local scan and network distribution read and prefer strict `mesh_archive` / `hand_archive` fields, added `.pdbody` and `.pdhead` to metadata-family walking, accepted the strict fields in `.pdmod` source validation, and added `ASSET_BODY` / `ASSET_HEAD` runtime bindings that expose mesh and hand source separately. Regenerated `tri_body.pdbody` and `tri_head.pdhead` so descriptors and `_meta/manifest.json` declare mesh and hand archive members, and added body/head conformance so stale archives missing those declarations fail. Verification passed: example regeneration; direct body/head archive inspection; Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; malformed body/head rejection proof for missing `mesh_archive` / `hand_archive` manifest fields; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `bodyheadsrc` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,752 assertions / 43 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-826 `.pdcharacter` body/head/portrait source preservation

Continued the all-asset parity goal on CPU-safe character archive validation because live game/smoke launches remain paused under B-801. The confirmed `.pdcharacter` flattening point was the strict field boundary: the clean archive contract and checked-in example use `body_archive`, `head_archive`, and `portrait_file`, but local scan, network distribution, `.pdmod` folder validation, and runtime adapter activation still treated characters as legacy `bodyfile` / `headfile` only. The checked-in character manifest also omitted the embedded body, head, and portrait members.

Added `portrait_file` to character catalog rows, made local scan and network distribution accept `body_archive` / `head_archive` aliases, preserved portrait in metadata walking, accepted strict character fields in `.pdmod` folder validation, and added an `ASSET_CHARACTER` runtime binding that exposes portrait, body, and head as distinct source members. Regenerated `tri_character.pdcharacter` so `character.ini` and `_meta/manifest.json` declare body/head/portrait members, and added character-specific conformance so stale archives missing those declarations fail. Verification passed: example regeneration; direct `tri_character.pdcharacter` archive inspection; Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; malformed character rejection proof for missing `body_archive`, `head_archive`, and `portrait_file`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `chardeps` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,738 assertions / 43 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-825 `.pdtheme` dependency source preservation

Continued the all-asset parity goal on CPU-safe theme archive validation because live game/smoke launches remain paused under B-801. The confirmed `.pdtheme` flattening point was dependency propagation: the clean archive contract already allows themes to compose `.pdui`, `.pdfont`, `.pdsfx`, `.pdsong`, and `.pdeffect` assets, but `asset_entry_t.ext.theme`, local scan, network delivery, boot walking, package path handling, and runtime activation only preserved UI and font dependencies.

Added `audio_archive`, `music_archive`, and `effect_archive` to theme catalog rows and runtime bindings, widened the metadata walker theme key set, preserved those fields through local scanning, network distribution, and `.pdmod` path qualification, and regenerated `tri_theme.pdtheme` so `theme.ini` and `_meta/manifest.json` declare UI/font/audio/music/effect dependency archives. Added theme-specific conformance so embedded theme dependency archives must be declared by both descriptor and manifest metadata. Verification passed: example regeneration; direct `tri_theme.pdtheme` archive inspection; Python compile for archive/source tools; checked-in all-family example conformance with 28 root / 59 checked archives; malformed theme rejection proof for missing `audio_archive`, `music_archive`, and `effect_archive`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `themedeps` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,732 assertions / 43 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-824 `.pdmaterial` effect dependency source proof

Continued the all-asset parity goal on CPU-safe material archive proof because live game/smoke launches remain paused under B-801. The confirmed material-family weakness was not the runtime adapter branch, which already copied `entry->ext.material.effect_archive`; it was the proof surface around that branch. The checked-in `.pdmaterial` example only embedded a texture dependency, conformance did not reject an embedded effect dependency missing descriptor/manifest `effect_archive`, and metadata walking did not include `effect_archive` in the material primary-member key list.

Regenerated `tri_material.pdmaterial` so it embeds both `tri_texture.pdtexture` and `tri_effect.pdeffect`, with matching `texture_archive` and `effect_archive` fields in `material.ini` and `_meta/manifest.json`. Added material-specific conformance so any embedded material texture/effect archive must be declared in both descriptor and manifest metadata, and added a static guard for the metadata walker material key list. Verification passed: Python compile for archive/source tools; direct `tri_material.pdmaterial` inspection; checked-in all-family example conformance with 28 root / 56 checked archives; malformed material rejection proof for missing `effect_archive`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `materialfx` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,725 assertions / 43 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-823 `.pdeffect` timeline source preservation

Continued the all-asset parity goal on CPU-safe effect source validation because live game/smoke launches remain paused under B-801. The confirmed `.pdeffect` flattening point was timeline propagation: the clean archive contract allows `timeline.json` beside `effect.graph.json`, but `asset_entry_t.ext.effect`, local scan, network delivery, metadata walking, and runtime activation only preserved one `effect_file`. A public effect timeline could therefore exist in the archive but disappear from the game-facing catalog/runtime view.

Added `ext.effect.timeline_file`, wired it through local scanning, network distribution, base `.pdeffect` walking, and runtime adapter activation, and regenerated `tri_effect.pdeffect` so `effect.ini` and `_meta/manifest.json` declare both `effect_file` and `timeline_file`. Tightened `.pdeffect` conformance so descriptor and manifest metadata must declare those source members when the files exist. Verification passed: Python compile for archive/source tools; direct `tri_effect.pdeffect` archive inspection; checked-in all-family example conformance; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `effectline` tests build; and focused `[modding][pdxxx][runtime][c3838][files],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,623 assertions / 42 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - `.pdprop` behavior graph source parity

Continued the all-asset parity goal on CPU-safe prop source validation because live game/smoke launches remain paused under B-801. The confirmed `.pdprop` flattening point was behavior graph propagation: `behavior.graph.json` is part of the public archive contract and the checked-in `tri_prop.pdprop` example, but `asset_entry_t.ext.prop` had no behavior graph field. Local scanning, network delivery, boot-time metadata walking, and runtime activation therefore carried the visual model source while dropping the behavior graph from the game-facing catalog/runtime view.

Added `ext.prop.behavior_graph`, wired it through local scan, network distribution, base `.pdprop` walking, and runtime adapter activation, and regenerated `tri_prop.pdprop` so `_meta/manifest.json` declares both `model_file` and `behavior_graph`. Tightened `.pdprop` conformance so descriptor and manifest metadata must declare those source members when the files exist. Verification passed: Python compile for archive/source tools; direct `tri_prop.pdprop` archive inspection; checked-in all-family example conformance; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `propgraph` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdmod][static][c3809],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 2,665 assertions / 41 cases. The isolated build directory was removed. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - `.pdui` layout/nine-slice metadata parity

Continued the all-asset parity goal on CPU-safe UI source validation because live game/smoke launches remain paused under B-801. The confirmed `.pdui` flattening point was the catalog/runtime metadata boundary: base extraction writes texture dimensions and nine-slice metadata into `ui.ini` and `_meta/manifest.json`, and the clean archive format allows optional `layout.json` and `nineslice.ini`, but `asset_entry_t` had no UI-specific ext fields. Local scanning, network delivery, boot-time `.pdui` walking, and runtime activation therefore carried only one primary texture source path.

Added UI catalog fields for texture, layout, nine-slice members, texture dimensions/data size, texture name, and nine-slice inset/mode metadata. Wired those fields through local scan, network distribution, base `.pdui` walking, and runtime adapter activation. Regenerated `tri_reticle.pdui` as a richer UI source sample with `texture.png`, `layout.json`, `nineslice.ini`, descriptor keys, and manifest metadata. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `uimeta` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdmod][static][c3809],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 2,662 assertions / 41 cases. The isolated build directory was removed. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - `.pdlang` source-bank metadata parity

Continued the all-asset parity goal on CPU-safe language source validation because live game/smoke launches and broad regeneration remain paused under B-801. The next confirmed `.pdlang` parity gap was a base/mod delivery mismatch: extraction writes `source_bank`, `locale`, `category`, and `string_count` into base language descriptors/manifests, while local scan and network delivery read only `bank_id` and `strings_file`. An extracted language archive could therefore keep public `strings.json` but lose the runtime bank target when scanned or shared through descriptor parsing.

Added locale/category/string-count fields to language catalog rows, made local scan and network delivery accept `source_bank` as the extractor-style fallback for `bank_id`, and made the base walker preserve locale, category, and string counts from `_meta/manifest.json`. Tightened `.pdlang` conformance so descriptors/manifests must carry locale, category, positive string count, positive source bank, and `strings.json`, and so `strings.json` must be a non-empty `language_strings` table with text rows. Regenerated `tri_lang.pdlang` into that strict source shape. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; a temporary malformed `.pdlang` rejection proof for missing source-bank metadata; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `langmeta` tests build; and focused `[modding][pdxxx][c3844][lang][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 2,546 assertions / 39 cases. The isolated build directory was removed. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - Bitmap `.pdfont` glyph/metrics preservation

Continued the all-asset parity goal on CPU-safe font source validation because live game/smoke launches and broad regeneration remain paused under B-801. The next confirmed font parity gap was bitmap `.pdfont` metadata preservation. Base extraction writes a two-part editable bitmap font source (`glyphs.pgm` plus `font.metrics.json`), but the catalog entry shape had no font-specific source fields and runtime bindings only exposed the selected primary source path. That meant glyph metrics and kerning could become archive-only data after local scan, network delivery, boot-time base walking, or runtime activation.

Added `ext.font.font_file` and `ext.font.metrics_file` to catalog rows, then wired both through local scan, network distribution, base font archive walking, and runtime adapter activation. Tightened `.pdfont` conformance so bitmap archives must declare `font_file = glyphs.pgm`, `metrics_file = font.metrics.json`, positive character counts in descriptor/manifest metadata, and a non-empty `font.metrics.json` glyph list. Regenerated `tri_font.pdfont` as a bitmap glyph/metrics example instead of a vector-only example. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; a temporary malformed `.pdfont` rejection proof for missing `font.metrics.json`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `fontmeta` tests build; and focused `[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812],[modding][pdxxx][runtime][c3838][adapters]` with 1,604 assertions / 25 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - Sequence-backed `.pdsong` source binding

Continued the all-asset parity goal on CPU-safe music source validation because live game/smoke launches and broad regeneration remain paused under B-801. The next confirmed music parity gap was the sequence-backed `.pdsong` path: extraction writes editable `sequence.mid`, semantic `sequence.json`, and descriptor fields such as `music_file`, `midi_file`, `events_file`, `division`, and `event_count`, and the runtime compiler expects those sibling files. Local scan and network distribution, however, only treated `file_path` as the audio primary source, so authored or delivered sequence-backed songs could pass broad archive shape without getting a FileProvider primary source for `catalogResolveMusicSequence()`.

Updated `assetcatalog_scanner.c` and `netdistrib.c` so `music_file` / `midi_file` become the primary public source when no streaming `file_path` is declared. The streaming `ext.audio.file_path` field remains reserved for direct audio tracks, so MIDI sequence sources are not mistaken for WAV/MP3 streams. Tightened `.pdsong` conformance so sequence-backed archives must carry descriptor and manifest timing/source fields and a non-empty `sequence.json` event list. Regenerated `tri_song.pdsong` as a sequence-backed example with `sequence.mid` plus `sequence.json`. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; a temporary malformed `.pdsong` rejection proof for missing `sequence.json`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `songseq` tests build; and focused `[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 1,490 assertions / 22 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - WAV-backed audio playback metadata conformance

Continued the all-asset parity goal on CPU-safe audio source validation because live game/smoke launches and broad regeneration remain paused under B-801. The audio extraction/runtime path already carries native keymap, loop, envelope, pan, and volume metadata for WAV-backed `.pdsfx` and `.pdvoice`, but the standard conformance gate still accepted stale or hand-authored WAV archives that only contained `sample.wav` with a minimal descriptor.

Updated `tools\asset_archive_conformance.py` so WAV-backed `.pdsfx` / `.pdvoice` archives must include native playback metadata in both the descriptor and `_meta/manifest.json`. MP3-only `.pdvoice` aliases remain a separate source shape and are not forced through the WAV metadata contract. Regenerated checked-in audio examples so `tri_click.pdsfx`, `tri_voice.pdvoice`, and the nested weapon `fire.pdsfx` dependency carry the full playback fields. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; a temporary malformed `.pdsfx` rejection proof for missing `key_base`; `python tools\asset_native_source_guard.py`; isolated `audconf` tests build; and focused `[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 1,487 assertions / 22 cases. The isolated build directory was removed. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - `.pdanim` semantic conformance gate

Continued the all-asset parity goal on CPU-safe animation validation because live game/smoke launches and regeneration remain paused under B-801. The previous standalone `.pdanim` verifier made stale extracted animation output readable, but the standard all-family archive conformance gate still accepted `.pdanim` at the broad slot level as long as `animation.gltf`, `animation.glb`, or `commands.json` existed.

Updated `tools\asset_archive_conformance.py` so `.pdanim` validation now checks the actual public animation source semantics. Character GLTF/GLB archives reject stale `Perfect Dark 2 pdanim_chr` generator stamps, missing animations or target nodes, zero-count sampler accessors, invalid sampler/channel indices, and unsupported target paths. Weapon command archives require non-empty `commands.json`, catalog-ID animation/sound references, and matching descriptor command counts. The conformance CLI now also supports `--max-errors` so a stale extracted install fails with bounded output instead of flooding the terminal. A retained stale `base_animation_character_yb.pdanim` now fails standard conformance for the v3 generator and zero-sample accessors, while checked-in examples still pass. Verification passed: Python compile for archive/source tools; checked-in all-family example conformance; compact stale-archive conformance rejection; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `animconf` tests build; and focused `[modding][pdxxx][c3842],[B-772]` with 622 assertions / 9 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - `.pdanim` stale-output verifier diagnostics

Continued the all-asset parity goal on CPU-safe animation verification because live game/smoke launches and regeneration remain paused under B-801. The retained `.claude\smoke-verify-install\data\ntsc-final` animation tree still fails `tools\verify_pdanim_sources.py`: 950 character `.pdanim` archives are stale generator v3, and the stale `base_animation_character_yb` archive still has zero-count sampler accessors.

Updated the verifier so stale-generator failures are grouped by generator with capped examples, and general detailed error output is capped by `--max-errors` while keeping stale generated data as a hard failure. This makes full-tree animation source audits actionable instead of flooding the console. Verification passed: Python compile for `tools\verify_pdanim_sources.py`; checked-in example `.pdanim` verification; compact retained-install failure proof; `python tools\asset_native_source_guard.py`; checked-in example archive conformance; scoped diff check; isolated `animdiag` tests build; and focused `[modding][pdxxx][c3842],[B-772]` with 613 assertions / 9 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-822 Scenario authoring template source shape

Continued the all-asset parity goal on CPU-safe Scenario authoring/template coverage because live game/smoke launches remain paused under B-801. After Scenario navigation submember propagation, the next confirmed failure point was the loose-folder `.pdmod` packer template path: it generated `pads.json` as TSV-shaped text, omitted required Scenario sidecars such as `portals.json`, `setup.fields.json`, `ai/ailists.json`, and `navigation/*.json`, and the `scenario.ini` template omitted explicit navigation submember declarations.

Updated the packer templates so new Scenario folders get semantic public JSON for pads, portals, setup fields, AI lists, waypoint/waygroup/cover/path navigation members, and expanded `navigation.ini` declarations. The packer now creates nested sidecar directories before writing generated templates, so `ai/` and `navigation/` members work on a fresh folder. The scanner `scenario.ini` template now declares `waypoints_file`, `waygroups_file`, `covers_file`, and `paths_file` beside `navigation_file`. Static coverage pins the semantic templates, nested sidecar paths, generated-cache declaration, and absence of the old TSV-shaped `pads.json` header. Verification passed: scoped diff check with only the known `context/bugs.md` CRLF warning; `python tools\asset_native_source_guard.py`; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families`; isolated `scenpack` tests build; and focused `[modding][pdmod][static][c3809],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 2,471 assertions / 38 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-821 Scenario navigation submember source propagation

Continued the all-asset parity goal on CPU-safe Scenario catalog/runtime propagation because live game/smoke launches remain paused under B-801. After portal propagation, the next confirmed Scenario flattening point was navigation submembers: public `.pdscenario` descriptors declare `waypoints_file`, `waygroups_file`, `covers_file`, and `paths_file`, and runtime proof consumes those four JSON members independently, but the catalog and runtime binding shapes preserved only the aggregate `navigation_file`.

Added explicit Scenario catalog and runtime binding fields for navigation waypoint, waygroup, cover, and path sources. Local scanning, network distribution, boot-time base archive walking, `.pdmod` path qualification, runtime adapter activation, and direct navigation runtime fallbacks now preserve those declared members before falling back to the canonical strict archive names. Verification passed: scoped diff check with only the known `context/bugs.md` CRLF warning; `python tools\asset_native_source_guard.py`; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families`; isolated `navsub` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812],[modding][pdmod][static][c3809]` with 2,551 assertions / 41 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-820 Scenario portal source propagation

Continued the all-asset parity goal on CPU-safe catalog/runtime propagation because live game/smoke launches remain paused under B-801. The next confirmed Scenario data-loss point was the portal table source. Public `.pdscenario` examples and conformance require `portals_file = portals.json`, and runtime source compilation consumes `portals.json`, but `asset_entry_t.ext.scenario` had no field for the portal member. Scanner, network delivery, base archive walking, path qualification, runtime activation, and portal loading could only infer it from another member path.

Added `portals_file` to Scenario catalog metadata and propagated it through local scan, network distribution, `.pdmod` source-path qualification, base Scenario archive walking, runtime binding activation, and `scenarioSourceLoadPortalsForStage()`. The portal loader now prefers the explicit catalog field before falling back to the derived archive-member path. Verification passed: Python compile for `tools\asset_archive_conformance.py`, `tools\build_typed_pdxxx_examples.py`, and `tools\asset_native_source_guard.py`; scoped diff check; `python tools\asset_native_source_guard.py`; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families`; isolated `portalbind` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812],[modding][pdmod][static][c3809]` with 2,547 assertions / 41 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-819 Scenario runtime binding source preservation

Continued the all-asset parity goal on CPU-safe runtime adapter coverage because live game/smoke launches remain paused under B-801. After the metadata and mission example fixes, the next confirmed flattening point was Scenario runtime binding activation: `asset_entry_t.ext.scenario` preserved the full source bundle, but `assetRuntimeActivateCatalogEntry()` copied only scene, collision, and level graph into the generic runtime binding.

Added explicit Scenario source fields to `asset_runtime_binding_t` for rooms, pads, spawns, volumes, objects, setup fields, AI lists, objectives, and navigation, and wired them from `entry->ext.scenario` during activation. The adapter test now constructs a Scenario with the full public source set and asserts every source member survives activation. While recording the fix, stale catalog/design context sections that still described active Scenario TSV source were corrected to the current semantic JSON contract. Verification passed: `python tools\asset_native_source_guard.py`; scoped diff check; no active `pads.tsv`/`objects.tsv`/`volumes.tsv`/`setup.fields.tsv`/`ai/ailists.tsv` wording in the corrected context docs; isolated `scenbind` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 1,566 assertions / 25 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-818 stale TSV manifest references rejected

Continued the all-asset parity goal on CPU-safe archive-shape checks because live game/smoke launches remain paused under B-801. After the no-public-TSV cleanup, the checked-in mission example still had a metadata-only leak: `.pdmission::objectives.json` and `.pdmission::briefing.json` existed, but `_meta/manifest.json` still named `objectives.tsv` and `briefing.tsv`.

Regenerated the typed examples so `tri_mission.pdmission` writes JSON manifest keys for objectives and briefing, then tightened archive conformance so public text entries and `_meta/manifest.json` cannot retain stale `.tsv` references. Static coverage now reads the checked-in mission manifest and asserts JSON-only objective/briefing metadata. Verification passed: `python tools\build_typed_pdxxx_examples.py`; Python compile for `tools\asset_archive_conformance.py`, `tools\build_typed_pdxxx_examples.py`, and `tools\asset_native_source_guard.py`; direct checked-in example scan with zero `.tsv` references; `python tools\asset_archive_conformance.py --root examples\modding\typed-pdxxx-basic --require-all-families`; `python tools\asset_native_source_guard.py`; scoped diff check; isolated `missionmeta` tests build; and focused `[modding][pdxxx][policy][c3824],[modding][pdmod][static][c3809],[modding][pdxxx][c3842],[modding][pdxxx][base][static][c3812]` with 2,511 assertions / 39 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-817 `.pdskin` material/texture dependency preservation

Continued the all-asset parity goal on CPU-safe archive/catalog/runtime coverage because live game/smoke launches remain paused under B-801. After B-816 preserved skin swatches, the next confirmed `.pdskin` loss point was typed dependency closure: the strict archive contract allows `dependencies/assets/{material,materials,texture,textures}/*.pdxxx`, the checked-in example declared `material_archive`, and generated examples can embed the texture archive, but `asset_entry_t` had no skin fields for those typed dependencies. Local scanning, network delivery, metadata walking, `.pdmod` path handling, and runtime adapter binding therefore dropped material/texture dependency archives even when the public `.pdskin` file contained them.

Updated the skin catalog ext data with `material_archive` and `texture_archive`, then wired both through local scan, network distribution, metadata walking, `.pdmod` path validation/path handling, runtime bindings, and the checked-in typed `.pdskin` example. Runtime bindings now use distinct dependency slots for texture file, swatches, material archive, and texture archive instead of collapsing them. Verification passed: `python tools\asset_native_source_guard.py`; scoped diff check; isolated `skindep` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][skin],[modding][pdmod][static][c3809],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 2,563 assertions / 42 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - Integer-native mesh boundary recorded

Recorded Mike's integer-only mesh limitation as an active asset-pipeline constraint and mirrored it in the modding pillar plus clean archive format contract. The current model/scenario runtime still needs integer/fixed-point vertex, UV/tile, room/portal, part, matrix, and render-command semantics, so extraction must preserve those semantics and custom geometry import must explicitly quantize into documented integer native units before runtime cache/modeldef use. DCC-openable GLTF/GLB/OBJ remains the authoring shape, but float-only visual geometry is not a complete game-facing source until the renderer/runtime boundary is replaced or this constraint is explicitly retired. This does not change the no-integer-asset-reference rule.

## 2026-06-07 - B-816 `.pdskin` swatch source preservation

Continued the all-asset parity goal on CPU-safe archive/catalog/runtime coverage because live game/smoke launches remain paused under B-801. The next confirmed data-loss point was `.pdskin`: the strict archive shape and base extractor include `swatches.json`, and generated `skin.ini` declares `swatches_file = swatches.json`, but the catalog entry shape had no `swatches_file` field. Local scanning, network delivery, metadata walking, runtime adapter binding, and package path handling therefore treated the swatch file as archive-only data instead of a game-facing public source member.

Updated the skin catalog ext data with `swatches_file`, then wired it through local scan, network distribution, metadata walking, runtime bindings, base default skin registration, `.pdmod` path qualification, and extractor manifests. Base `.pdskin` archives without `swatches.json` now regenerate instead of keeping the older flattened shape. The checked-in typed `.pdskin` example now includes `swatches.json` plus `swatches_file`, and tests pin scanner/delivery/walker/runtime/example behavior. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `skinwatch` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][runtime][c3838][skin],[modding][pdmod][static][c3809],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 2,549 assertions / 42 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-815 runtime adapter multi-member binding

Continued the all-asset parity goal on CPU-safe runtime adapter coverage because live game/smoke launches remain paused under B-801. After B-814 fixed boot-time metadata walking, the next failure point was catalog activation into `asset_runtime_binding_t`: some families still collapsed distinct public archive members into a single authored/dependency slot.

Updated `asset_runtime.c` / `asset_runtime.h` so runtime bindings keep skin source and texture, HUD texture and layout, and mission scenario/objectives/briefing members separately. The adapter now uses `dependency_c` for the third mission dependency instead of choosing objectives or briefing, and focused adapter tests assert the distinct fields after activation. Static source-contract coverage now pins the runtime adapter against this specific flattening regression. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `runtimebind` tests build; and focused `[modding][pdxxx][runtime][c3838][adapters],[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,014 assertions / 30 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-814 multi-member metadata archive walking

Continued the all-asset parity goal on the same CPU-safe metadata walker path because live game/smoke launches remain paused under B-801. After the character body/head split, the broader failure point was the same single-primary-member flattening across other metadata families: local scanning and network delivery preserve distinct authored source/dependency fields, but boot-time `_meta/manifest.json` walking selected one primary archive member and did not repopulate every runtime-facing ext field.

Updated `loader_walker_meta.c` so vehicle metadata resolves model, physics, and behavior members independently; mission metadata resolves scenario, objectives, briefing, and graph members independently; HUD metadata resolves texture and layout members independently; material metadata resolves material, texture, and effect members independently; and theme metadata resolves theme, UI, and font members independently. Missing optional members are cleared instead of inheriting stale paths. Static coverage now pins those fields in both the external-archive and native-source contract tests. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `metawalk` tests build; and focused `[modding][pdmod][static][c3809],[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,912 assertions / 43 cases. The isolated build directory was removed and no game/test/build process remained. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-813 character body/head source-walking split

Continued the all-asset parity goal on a CPU-safe metadata walker path because live game/smoke launches remain paused under B-801. The failure point was `.pdcharacter` boot-time metadata walking: local `character.ini` scanning preserved separate `bodyfile` and `headfile`, but `_meta/manifest.json` walking selected one primary archive member and wrote both body and head fields from that same `source_path`.

Updated `loader_walker_meta.c` so character metadata resolves `body_archive`/`bodyfile` and `head_archive`/`headfile` independently through archive-member paths. Missing or `null` head source now clears `entry->ext.character.headfile` instead of inheriting the body path. Static coverage pins the body/head split and forbids copying the head source from the selected primary path. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `charwalk` tests build; and focused `[modding][pdmod][static][c3809]` with 960 assertions / 16 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-812 mod pack and authoring UI all-family enumeration

Continued the all-asset parity goal on a CPU-safe propagation pass after the enable-state and network-advertisement fixes. The next failure point was another set of hard-coded family lists: `modpack.c` could not find or map several first-class public/custom archive families during export-by-catalog-ID and hot registration, while Mod Manager and Modding Hub omitted some of the same families from user-facing type enumeration.

Updated the packer, Mod Manager, and Modding Hub lists so user-manageable typed archive families include arenas, bodies, heads, meshes/models, animations, and tools where each surface supports them. Descriptor lookup now covers `arena.ini`, `body.ini`, `head.ini`, `mesh.ini`/`model.ini`, and `animation.ini`, and static coverage pins the lists. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `modlists` tests build; and focused `[modding][pdmod][static][c3809]` with 954 assertions / 16 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-811 network catalog-info all-family advertisement

Continued the all-asset parity goal on a CPU-safe network distribution boundary. The manifest format already has generic typed-asset support (`MANIFEST_TYPE_ASSET` carrying `asset_type_e`), so the failure point was not the manifest constants themselves. The gap was the host catalog-info advertisement list in `netmsgSvcCatalogInfoWrite()`.

That writer collected enabled non-bundled rows through an older hard-coded asset-type list. It skipped `ASSET_ARENA`, `ASSET_BODY`, `ASSET_HEAD`, `ASSET_MODEL`, `ASSET_ANIMATION`, and `ASSET_TOOL`, so those custom assets could be registered and enabled locally but omitted from the catalog-info packet used for distribution visibility.

Updated the network catalog-info collection list to include the omitted user-manageable families and added static coverage that pins the full advertised family set. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `netadv` tests build; and focused `[modding][pdmod][static][c3809]` with 807 assertions / 15 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-810 component enable-state all-family persistence

Continued the all-asset parity goal on a CPU-safe Mod Manager path because live game/smoke launches remain paused under B-801. The concrete failure point was `modmgrSaveComponentState()`: disabled component IDs reload generically through `assetCatalogSetEnabled(line, 0)`, but the save path only iterated a hard-coded subset of asset types.

That subset skipped first-class public/custom families including `ASSET_ARENA`, `ASSET_BODY`, `ASSET_HEAD`, `ASSET_MODEL`, and `ASSET_ANIMATION`. Those assets could be disabled in-session but would not be written to `mods/.modstate`, so they could re-enable after restart even though other mod components persisted correctly.

Updated the save-side type list to include the omitted catalog families and added static coverage that pins every user-manageable type in the component-state save list. Verification passed: `python tools\asset_native_source_guard.py`; diff check with only the existing `context/bugs.md` CRLF warning; isolated `modstate` tests build; and focused `[modding][pdmod][static][c3809]` with 772 assertions / 14 cases. No live game or smoke launch was run because B-801 remains open.

## 2026-06-07 - B-803 mesh MTL texture-map parity

Continued the all-asset parity goal on a CPU-safe mesh/material boundary because live renderer smoke remains paused under B-801. The concrete custom-content failure point was in `modasset_compiler.c`: extracted base `.pdmesh::model.mtl` files carried project-specific `pd_texture_catalog` hints, but ordinary DCC-authored OBJ/MTL source uses standard `map_Kd`. That meant a custom `.pdmesh` could carry valid typed `.pdtexture` dependencies and still compile untextured because the importer ignored the standard material texture binding.

Updated `generatedModeldefLoadMaterialMetadata()` so `map_Kd` resolves through the existing catalog/provider texture layer. It accepts catalog IDs directly or archive-relative paths that match registered `ASSET_TEXTURE` rows, including `.pdtexture::texture.png` dependency members, then uses the same generated texture-marker path as extracted base meshes. Unresolved `map_Kd` entries warn and do not create loose texture sidecars, preserving the strict typed archive contract.

Verification passed: `python tools\asset_native_source_guard.py`; Python compile for `tools\asset_archive_conformance.py` and `tools\asset_native_source_guard.py`; isolated `meshmtl` tests build; focused `[modding][pdmod][static][c3809]` with 727 assertions / 13 cases; and focused `[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842],[catalog][provider][static]` with 12,559 assertions / 55 cases. No live renderer proof was run because B-801 still pauses live launches.

## 2026-06-07 - B-802 audio fx-state parity

Continued the all-asset parity goal on the CPU-safe audio path because live game/renderer smoke remains paused under B-801. The remaining audio wording from B-794 was too broad: native `func00033820()` initializes `sndstate.fxmix` and `sndstate.fxbus`, folds the low nibble of `ALKeyMap.keyMax` into the effective fxmix while preserving the high/surround bit, and later updates that state through `AL_SNDP_FX_EVT`, `AL_SNDP_4000_EVT`, and `AL_SNDP_FXBUS_EVT`. The source-backed file handle path preserved pitch, pan, loop, and envelope, but ignored these fx-state fields and events.

Updated `sndStart()` to pass `fxmix`, `fxbus`, and the keymap-derived fxmix offset into `audioStartFileSound()`. The file-backed audio bridge now initializes `sndstate.fxmix` / `sndstate.fxbus`, records the computed effective fxmix metadata for the source handle, and updates that state when native FX/fxbus events are posted. This keeps source-backed `.pdsfx` / `.pdvoice` handles aligned with the native sound-player state boundary without claiming full N64 aux/reverb synthesis inside SDL WAV playback.

Verification passed: `python tools\asset_native_source_guard.py`; Python compile for `tools\asset_native_source_guard.py`, `tools\asset_archive_conformance.py`, `tools\verify_pdanim_sources.py`, and `tools\verify_scene_glb_texture_contract.py`; isolated `audiofx` tests build; and focused `[catalog][provider][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 12,559 assertions / 55 cases. No live audio/game smoke was run because B-801 still pauses live launches.

## 2026-06-07 - All-family configured audio and UI source-load parity

Continued Mike's all-asset parity sweep from runtime evidence instead of extraction shape alone. The current all-family source-gate smoke first exposed two concrete problems: configured MP3-backed speech had typed `.pdvoice` archives but still used the wrong audio-config row at playback, and base `.pdui` textures were visibly loaded by the theme while direct typed catalog loads reported `base:ui_bg_alt` missing.

Fixed `sndStartMp3()` so MP3-backed configured aliases resolve `g_AudioRussMappings[sp24.confignum].audioconfig_index` before applying configured volume, pan, offensive filtering, and respond-hello flags. The alias row remains the route to the typed `.pdvoice::sample.mp3` source, but audio behavior now comes from the intended `g_AudioConfigs` row instead of the alias table index.

Fixed the UI failure in `pdgui_theme.cpp`: catalog UI apply was iterating `ASSET_UI` entries, loading their texture, then calling the loose-mod registration helper, which re-registered the same catalog row and wiped its public FileProvider archive-member source. Catalog-owned UI apply now only uploads the texture into the theme cache; loose mod texture imports remain the path that registers new UI catalog rows.

Verification passed scoped diff checks, `python tools\asset_native_source_guard.py`, focused `[catalog][provider][static],[modding][pdxxx][c3844]` with 18,438 assertions / 53 cases, isolated `mp3cfg-client2` client build, and exact-client `all_family_source_gate_smoke` 36/36 at `.claude\smoke-verify-runs\results-20260607T150046Z.json`. The smoke log proves `base:voice_cover_me_aiw` and `base:song_sequence_a` load from typed audio source, `romextract pdvoice: 104 configured MP3 alias archive(s) checked`, and `BOOT: --debug-load-catalog-assets result type=ui id='base:ui_bg_alt' result=OK`. The run still printed the known Windows firewall-rule access-denied warning after the pass summary; it did not fail the harness.

## 2026-06-07 - Configured MP3 speech alias extraction

Continued the all-asset parity investigation through the audio extraction/runtime path. The boot smoke warnings were not missing normal `.pdsfx` bank entries; configured SFX aliases such as Carrington speech resolve through `g_AudioRussMappings`, and when the mapped target has `mp3priority != 0`, `mapped.id` is a packed MP3 file number rather than an ALSound bank leaf.

Fixed `romextract_pdsfx.c::s_emitConfiguredAliasSounds()` so configured aliases mapped to MP3/file-backed speech are validated against the extracted file source path from `romExtractRelPathForFilenum()` instead of being treated as out-of-range sound-bank indices. Those aliases now skip `.pdsfx` bank archive emission, fail only when the mapped MP3 source file is missing, and log one compact summary instead of one line per alias.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check, isolated `audalias2` tests/client builds, focused `[modding][pdxxx][base][static][c3812]` with 846 assertions / 13 cases, focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 7,883 assertions / 27 cases, and `boot_smoke` 14/14 at `.claude\smoke-verify-runs\results-20260607T121938Z.json`. The smoke log reports `romextract pdsfx: 104 configured MP3 alias source(s) checked; no .pdsfx bank archives emitted`, `failed=0`, and no `LOUDFAIL.EXTRACT.PDSFX` / `maps outside sound bank`. Follow-up B-796 now packages those MP3 sources as typed `.pdvoice::sample.mp3` archives instead of loose `data/ntsc-final/files/*.bin` bridge data.

## 2026-06-07 - Scenario source-scene camera delivery

Continued the all-asset parity investigation through the live Chicago source-scene render path. The extracted `.pdscenario::scene.glb` was already activating with geometry, materials, textures, source room tables, and source collision; the failure point was runtime delivery. `scenarioSceneRendererSetCameraFrame()` received valid camera position/look/up vectors but rejected the frame because `viGetFovY()` was `0.000`, so `scenarioSceneRendererRender()` never drew the native source scene even though gameplay continued.

Updated `bgRenderScene()` to feed the source renderer sanitized projection inputs from the live player camera path, falling back from VI FOV to player FOV, zoom FOV, and finally 60 degrees only when the current value is unusable. Added one-shot source-renderer camera diagnostics and tightened the Chicago smoke so it requires accepted camera proof plus a rendered native source-scene line while forbidding rejected/missing-camera paths.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check, isolated `scenecam` tests/client builds, focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 7,883 assertions / 27 cases, and `scenario_pads_source_gate_smoke` 154/154 at `.claude\smoke-verify-runs\results-20260607T120158Z.json`. This closes the specific dropped Scenario source-scene draw caused by zero FOV delivery; broader all-family parity remains open for material/combiner/fog fidelity, animation timing/skeleton proof, audio edge cases, and other representative mesh/model users.

## 2026-06-07 - No-public-TSV guard suffix closure

Mike restated the archive-source rule plainly: TSV files are effectively opaque ripped row data and do not fit the accessible-files concept. The existing archive conformance checker already rejects public `.tsv` members globally while retaining explicit stale TSV names as old-archive fingerprints.

Found one remaining policy leak in `tools/asset_native_source_guard.py`: `.tsv` still appeared in `PUBLIC_TEXT_ENTRY_SUFFIXES`, meaning the guard still treated TSV like an ordinary public text source format for asset-reference scanning. Removed that suffix while keeping explicit forbidden TSV entry names for stale archive rejection. Added static coverage in `tests/test_asset_native_source_contract.cpp` so the guard and conformance checker both keep TSV out of public text-source suffix handling while conformance still forbids `*.tsv`.

Verification passed Python compile for the guard/conformance tools, `python tools\asset_native_source_guard.py`, scoped diff check for the touched source/context files, isolated `tsvguard` tests build, and focused `[modding][pdxxx][c3842]` with 540 assertions / 7 cases.

## 2026-06-07 - B-769 generated mesh hit-test pointer parity follow-up

Continued the all-asset parity investigation from the live mesh render/runtime path. Current `.pdmesh` extraction is no longer the old flat OBJ-only shape: archives emit `model.obj`, `model.mtl`, `model.nodes.json`, `model.parts.json`, `model.faces.json`, and `model.render.json`, and the compiler rebuilds hierarchy modeldefs from those public source members.

Found the next concrete runtime failure point in `src/game/propobj.c::func0f06bea0`. This character/object hit-test walker still treated generated source-backed display-list pointers like native segmented model bytes, even though the nearby object hit-test path had already been updated. For generated modeldefs it now uses the real `rwdata->gdl` and `rodata->dl.xlugdl` pointers; native modeldefs keep the existing `UNSEGADDR(...)+rodata->colours` path.

Added static B-769 coverage in `tests/test_asset_native_source_contract.cpp` so the hit walker must keep generated modeldefs on the direct pointer path before the native segmented fallback.

Verification passed `python tools\asset_native_source_guard.py`, scoped diff check for `src\game\propobj.c` and `tests\test_asset_native_source_contract.cpp`, isolated `b769hit` tests build, exact focused B-769 static test with 17 assertions / 1 case, focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 7,861 assertions / 27 cases, isolated `b769hitclient` client build, and `weapon_match_source_gate_smoke` against `.claude\session-builds\b769hitclient\PerfectDark.exe` with generated mesh render audit enabled.

Runtime smoke notes: the mesh render proof loaded Chicago and logged generated source rendering for DY-357, combat hands, and source-loaded world models without source fallback or fatal patterns. The run still printed the known firewall-rule access-denied warning, three no-triangle logo mesh warnings, configured SFX alias warnings, a spawn-with-weapon fallback warning, and the separate Scenario camera-matrix warning; those remain outside this hit-test pointer fix.

Remaining all-family parity work: broader material/combiner/fog parity, animation timing/skeleton verification, audio edge cases, source Scenario camera matrices, and representative body/head/title/prop proof beyond this specific generated display-list pointer walker.

## 2026-06-07 - B-794 audio envelope/release metadata parity

Continued the all-asset parity audit on the audio path after B-793 fixed loop/live-handle behavior. The next concrete failure point was another extraction/runtime flattening issue: native `ALSound` playback carries `ALEnvelope` attack/decay/release timing and volume targets, plus `ALKeyMap` velocity fields, but public `.pdsfx` / `.pdvoice` archives only preserved pitch, pan, volume, loop, and sample data. That meant source-backed audio could play and loop, but still did not have enough metadata to fade or release like native ALSound handles.

Updated `romextract_pdsfx.c` so both `.pdsfx` and `.pdvoice` archives emit readable `velocity_min`, `velocity_max`, `has_envelope`, `attack_time_us`, `decay_time_us`, `release_time_us`, `attack_volume`, and `decay_volume` fields in `sound.ini` / `voice.ini` and `_meta/manifest.json`. Stale audio archives missing `attack_time_us` or `velocity_max` now regenerate instead of keeping the flattened old form. The same fields flow through base archive walkers, external INI scanning, and network distribution, so base content and custom/mod audio use one archive/runtime path.

Updated the file-backed audio bridge so `sndStart()` passes envelope data into `audioStartFileSound()`. Source WAV handles now apply attack/decay scaling during mix and enter `AL_STOPPING` with release fade when the gameplay audio API stops them, instead of cutting immediately. Stale released handles no longer appear owned after the bridge frees them. This does not claim full native synthesis parity yet; fxmix/high-bit keymap chaining and any audible edge cases remain separate playtest-backed audit targets.

Verification passed `python tools\asset_native_source_guard.py`, `python -m py_compile tools\asset_native_source_guard.py`, scoped `git diff --check`, isolated `audioenv` client/updater build, isolated `audioenv` test build, and focused `[catalog][provider][static],[modding][pdxxx][base][static][c3812]` with 11,922 assertions / 46 cases.

## 2026-06-07 - B-793 audio source loop handles

Investigated the reported audio parity issue after the earlier keymap/pitch fix. The concrete failure point was loop/handle behavior: generated `.pdsfx` / `.pdvoice` archives carried `loop_start_samples`, `loop_end_samples`, `loop_count`, and `has_loop`, and an audit of the smoke install found 122 base audio archives with infinite loop metadata. The catalog walkers/scanners preserved keymap pitch/pan/volume but discarded the loop fields, and `sndStart()` queued file WAV playback once through SDL and returned `NULL`, so source-backed alarms, hums, engines, doors, and weapon loops could not be stopped, repanned, repitched, or looped like native `sndstate` handles.

Fixed the source audio runtime path by carrying loop fields through `asset_entry_t`, base archive walkers, external scanner, and net distribution. `sndStart()` now calls `audioStartFileSound()` with pitch, pan, volume, loop metadata, and the caller's handle pointer. `port/src/audio.c` now owns a small file-backed sound-handle pool, decodes public WAV source to 22050 Hz stereo once, mixes active file SFX into the normal audio frame, honors loop points/counts, and lets `sndGetState()`, `audioStop()`, and `audioPostEvent()` operate on those file-backed handles through the existing gameplay API.

Verification passed `python tools\asset_native_source_guard.py`, `python -m py_compile tools\asset_native_source_guard.py`, scoped `git diff --check`, isolated `audioloop` client build, isolated `audioloop` tests build, and focused `[catalog][provider][static],[modding][pdxxx][base][static][c3812]` with 11,913 assertions / 46 cases. Remaining audio parity risk is envelope/release/fx behavior; this fix closes the obvious source-loop/handle break without claiming full native ALSound synthesis parity.

## 2026-06-07 - Scenario source scene material alpha parity

Continued the all-asset parity investigation from the Chicago screenshot where source geometry loaded but textures looked dark, gridded, or wrong. The failure point was material alpha handling, not missing geometry: extracted `scene.glb` PNG images already carried alpha, but the GLB exporter did not mark alpha-bearing materials and `scenario_scene_renderer.cpp` globally rendered `texture(u_Tex, v_Uv) * v_Color` with blending disabled and no alpha discard. Transparent texels therefore drew as solid dark pixels in the native source-scene renderer.

Updated `.pdscenario::scene.glb` extraction so decoded textures report non-opaque alpha while RGBA pixels are still available, alpha-bearing materials emit `alphaMode:"MASK"` plus `alphaCutoff:0.01`, and Scenario GLB/cache stamps were bumped to force regeneration. Updated the native source renderer to scan loaded images for non-opaque alpha, count alpha textures/materials on activation, and discard fully transparent output pixels in the shader before writing color.

Verification passed Python compile for archive/example tools, scoped diff check, isolated `matalpha` all-target build, focused source/static tests with 1,426 assertions / 21 cases, shared `Build` rebuild, `python tools\asset_native_source_guard.py`, and Chicago source smoke 150/150 at `.claude\smoke-verify-runs\results-20260607T103922Z.json`. The smoke log proves source `scene.glb` activation with `vertices=19992 groups=92 materials=93 images=98 alpha_textures=39 alpha_materials=38 uv=TEXCOORD_1 alpha=mask`, source-built room tables, scripted exit, and no `BG.ROOMS` warning.

## 2026-06-07 - Chicago source scene room-list validation

Continued the Scenario visual/runtime parity investigation after the Chicago source smoke loaded `scene.glb`, `collision.obj`, `portals.json`, and JSON Scenario graph tables but still logged repeated `BG.ROOMS: entered room list missing terminator` warnings during gameplay.

Traced the normal room-list contract through player/object/smoke callers: they use eight-slot `RoomNum rooms[8]` lists and pass `maxlen=7` so the last slot is reserved for `-1`. The failure point was in `bgFindEnteredRooms()`, which correctly writes the terminator to `rooms[maxlen]` but only checked slots before `maxlen` when deciding whether the incoming list was malformed. Source-built Chicago can legitimately fill seven room entries, so a valid full list was reported as broken every frame.

Fixed the scan to include the reserved terminator slot before warning, added static coverage for that contract, and made `scenario_pads_source_gate_smoke` forbid the warning. Verification passed Python compile for archive/source tools, `python tools\asset_native_source_guard.py`, focused source/static tests with 1,425 assertions / 21 cases, isolated `bgrooms` all-target build, and Chicago source smoke 149/149 at `.claude\smoke-verify-runs\results-20260607T102059Z.json`. The smoke log proves `portals.json` native portal tables, `scene.glb` native background room tables, scripted exit, and no `BG.ROOMS` terminator warning.

## 2026-06-07 - No-public-TSV cleanup follow-up

Mike clarified that TSV files should be treated like opaque ripped binary-style dumps, not accessible modder source. Kept the strict no-public-TSV invariant and audited the current active hits for actual emitters/acceptors versus stale-archive detection.

Removed the dead Scenario collision/material TSV diagnostic buffers from `romextract_pdarena.c`: tile conversion now emits OBJ/collision and visual mesh data without building `tiles_tsv`, and BG material finalization no longer builds a `materials_tsv` ledger. Stale `.tsv` archive-entry checks remain in place because they force old archives to regenerate instead of being accepted as current source.

Updated the Chicago `scenario_pads_source_gate_smoke` proof so required runtime log lines name `portals.json`, `pads.json`, `setup.fields.json`, `ai/ailists.json`, navigation JSON members, and mission `objectives.json` rather than the removed TSV paths. Static coverage now pins that the smoke has no `.tsv` expectations and that Scenario extraction has no TSV scratch ledgers while still allowing stale-entry rejection strings.

## 2026-06-07 - Weapon animation command source-use proof

Continued the all-family asset parity sweep after the Scenario animation source-use proof. The next failure point was the weapon/inventory `.pdanim` path: extraction wrote editable `commands.json`, but the animation walker was still feeding `loader_pool` from the duplicated command list inside `_meta/manifest.json`. That meant external edits to `commands.json` could be ignored by the runtime.

Changed `loader_walker_anim.c` to load `.pdanim::commands.json` through the FileProvider archive-member path and feed those bytes into `loaderPoolParseAnimationSourceJson()`. `loader_pool` now accepts the public `name` field when parsing command source, records the source member path with each compiled `guncmd` array, and exposes that path to `bondgun.c`. `bgunStartAnimation()` now logs `BGUN.ANIM.SOURCE` for source-backed command arrays, proving the same public command script reaches the normal first-person weapon animation path.

Verification passed: Python compile for `tools\asset_native_source_guard.py` and `tools\asset_archive_conformance.py`; `python tools\asset_native_source_guard.py`; scoped diff check; focused `[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3844][source][static],[modding][pdxxx][c3844][weapon][static]` with 1,311 assertions / 16 cases; isolated `animcmdsrc` all-target build; and `weapon_match_source_gate_smoke` against `.claude\session-builds\animcmdsrc\PerfectDark.exe`. The smoke log proves `LOADER.POOL.ANIMATION.SOURCE` loaded `base_invanim_dy357_shoot.pdanim::commands.json` and `BGUN.ANIM.SOURCE` used that same member at runtime before scripted exit.

## 2026-06-07 - Scenario animation source-use proof

Investigated the remaining `.pdanim` runtime proof gap after the public source-shape fixes. The failure point was not extraction shape this time: Scenario animation actions resolved catalog IDs, but a catalog ID alone did not prove the public `.pdanim` payload had compiled before the action used the native animation number.

Hardened `scenario_source_runtime.c` so `chr_do_animation`, `set_camera_animation`, `object_do_animation`, `do_preset_animation`, `if_natural_anim`, and special-death animation resolution require a compiled public animation clip through `catalogLoadTypedAsset(ASSET_ANIMATION, ...)` / `catalogGetLoadedAnimationClip()` before applying or comparing the animation. Action logs now include `anim_source` and `clip_bytes`; Scenario source-matrix/static coverage requires `.pdanim::animation.gltf` proof for representative live camera, character, and object animation paths.

Fixed adjacent client-build blockers in the same runtime file: JSON helpers now have forward declarations before earlier use, and stale `ASSET_ID_LEN` local buffers now use `CATALOG_ID_LEN`.

Verification passed: `python -m py_compile tools\asset_native_source_guard.py tools\asset_archive_conformance.py`; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][c3844][static],[modding][pdxxx][c3844][scenario][smoke][static],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 8,643 assertions / 39 cases; and isolated `animsrc` all-target build producing `PerfectDark.exe`. The isolated build directory was removed.

Attempted live CI Training/Rescue Scenario source-matrix smoke did not reach the game because the shared `Build\data\ntsc-final\scenarios` cache is stale and missing current JSON members such as `navigation/waypoints.json`. Refresh extracted Scenario archives before using that smoke as runtime evidence. Remaining `.pdanim` proof is weapon/inventory command playback through representative weapon users and broader animation parity beyond these Scenario action consumers.

## 2026-06-07 - B-792 shared no-public-TSV release gate

Strengthened Mike's archive-source rule from per-family cleanup into a shared release gate. Public typed archives may not contain TSV files; required data must be delivered as semantic JSON/INI/graph or standard editable media/source files, with runtime/cache products rebuilt from that source.

Updated `asset_archive_policy` so release-mode `assetArchiveValidateFile()` and `assetArchiveValidateBytes()` reject any `.tsv` archive member while migration mode can still inspect stale archives. Strict conformance now forbids arbitrary `*.tsv` entries and no longer keeps TSV row readers; the typed-example normalizer now requires existing JSON source members instead of reconstructing them from old TSV examples. Scenario generated-nav smoke assertions expect JSON source/log paths, stale portal runtime logs say `portals.json`, and the Scenario extractor/header comments no longer describe TSV public payloads. Remaining TSV strings are stale-archive detection lists, migration fixture rewrites, guard forbidden-name pins, stale-entry removal filters, or deliberate bad-archive tests.

Verification passed: Python compile for `asset_archive_conformance.py`, `asset_native_source_guard.py`, and the generated-nav fixture builder; checked-in typed example conformance across all 27 families; PowerShell parser check for `run-scenario-generated-nav-fixture.ps1`; `python tools\asset_native_source_guard.py`; focused `[modding][pdxxx][policy][c3824],[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3842]` with 1,409 assertions / 21 cases; and isolated `b792notsv` all-target build. The isolated build directory was removed.

Current state: no-public-TSV is now a shared release invariant. Remaining asset-pipeline work returns to broader runtime parity proof across families, especially animation usage, audio edge cases, Scenario visual/material fidelity, and any fresh source-only/runtime gaps found by smokes.

## 2026-06-07 - B-791 weapon animation command refs and SFX alias archives

Closed the known `.pdanim` command-source conformance follow-up from B-778/B-790. Weapon/inventory `commands.json` extraction now writes include/random animation targets as catalog IDs (`base:invanim_*`) instead of local `invanim_*` symbols, and old command archives with local-symbol animation refs are treated as stale so extraction regenerates them.

Configured weapon command sounds now have public source identities, not only runtime catalog aliases. The SFX extractor emits configured alias `.pdsfx` archives for named high-bit sound refs, while the weapon extractor embeds those alias archives in `.pdweapon` dependency closure and points JSON audio bindings/manifests at the same configured catalog ID the command source uses.

Verification passed: Python compile for archive/source guards; isolated `b791pdanim` tests build; focused `[modding][pdxxx][base][static][c3812],[modding][pdxxx][c3844],[modding][pdxxx][c3842]` with 8,627 assertions / 39 cases; `python tools\asset_native_source_guard.py`; and isolated `b791pdanim` all-target build. The isolated build directory was removed.

Current state: no-public-TSV work is audit-discovered only, and the previously tracked `.pdanim` command-source include/random/configured-SFX archive-shape gap is closed. Remaining asset-pipeline work is broader runtime parity proof across families, especially representative animation users and any future extraction/runtime gaps found by source-only or visual/audio smokes.

## 2026-06-07 - B-789 Scenario AI lists semantic JSON source

Continued the no-public-TSV cleanup with the Scenario AI command source surface. Replaced `.pdscenario::ai/ailists.tsv` with semantic `ai/ailists.json` using schema `pd2.scenario.ai.lists.v1`; rows carry AI list refs, list ids, graph nodes, command index/order, byte offsets, opcodes, opcode names, operand arrays, and catalog patch fields.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `ai/ailists.json`, bumps Scenario/parent cache keys to v94/v91, points `scenario.ini`, `_meta/manifest.json`, `level.graph.json`, archive member declarations, and graph AI operands at the JSON path, and rejects stale archives that still contain `ai/ailists.tsv`. `scenario_source_runtime.c` now parses JSON through the same Scenario provider path before rebuilding native AI command bytes and applying catalog operand patches.

Updated tooling and validation. Strict conformance, native-source guard, scanner/walker defaults, network distribution, checked-in typed examples, generated-nav fixture construction, and static tests all require `ai/ailists.json` while forbidding the old TSV member. The generated-nav fixture also drops inherited public TSV members and stale TSV hash sidecars from the source Scenario archive.

Verification passed: Python compile for archive/source/example/fixture tools; PowerShell parser checks for both Scenario smoke runners; regenerated typed examples; strict checked-in example conformance; direct example archive inspection proving `ai/ailists.json=True`, `ai/ailists.tsv=False`, schema `pd2.scenario.ai.lists.v1`, and JSON operand arrays; generated-nav fixture inspection proving 23 JSON AI rows, no public TSV members, and no stale TSV hash sidecars; `python tools\asset_native_source_guard.py`; isolated `b789aijson` all-target build; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,534 assertions / 39 cases.

Current state: Scenario objectives, spawns, volumes, pads, navigation paths, waypoint/waygroup/cover tables, portals, objects, setup fields, and AI lists are now semantic JSON public source. Remaining no-public-TSV debt is `.pdanim` command-source conformance follow-up and any further public TSV bridge found by audit.

## 2026-06-07 - B-788 Scenario setup fields semantic JSON source

Continued the no-public-TSV cleanup with the Scenario setup-field source surface. Replaced `.pdscenario::setup.fields.tsv` with semantic `setup.fields.json` using schema `pd2.scenario.setup.fields.v1`; rows carry record id, kind, field name, type, value, catalog id, and referenced setup record id.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `setup.fields.json`, bumps Scenario/parent cache keys to v93, points `scenario.ini`, `_meta/manifest.json`, `level.graph.json`, archive member declarations, and graph setup-table operands at the JSON path, and rejects stale archives that still contain `setup.fields.tsv`. `scenario_source_runtime.c` now parses the JSON through the same Scenario provider path before rebuilding native setup records and validating setup behavior links.

Updated tooling and validation. Strict conformance, native-source guard, checked-in typed examples, generated-nav fixture construction, scanner/walker defaults, Scenario source-matrix setup-link proof parsing, and static tests all require `setup.fields.json` while forbidding the old TSV member. Regenerated checked-in typed examples now contain `setup.fields.json` and no `setup.fields.tsv`.

Verification passed: Python compile for archive/source/example/fixture tools; Scenario matrix parser syntax check; regenerated typed examples; strict checked-in example conformance; direct example archive inspection proving `setup.fields.json=True`, `setup.fields.tsv=False`, schema `pd2.scenario.setup.fields.v1`, graph binding, and row content; `python tools\asset_native_source_guard.py`; isolated `b788setupjson` all-target build; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,533 assertions / 39 cases. The isolated build directory was removed.

Current state: Scenario objectives, spawns, volumes, pads, navigation paths, waypoint/waygroup/cover tables, portals, objects, and setup fields are now semantic JSON public source. Remaining no-public-TSV debt is Scenario AI public command source plus the `.pdanim` command-source conformance follow-up.

## 2026-06-07 - B-787 Scenario objects semantic JSON source

Continued the no-public-TSV cleanup with the Scenario setup object source surface. Replaced `.pdscenario::objects.tsv` with semantic `objects.json` using schema `pd2.scenario.objects.v1`; rows carry record id, kind, pad ref, model/weapon/body/head catalog IDs, character/vehicle AI list refs, and flags.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `objects.json`, bumps Scenario/parent cache keys to v92, points `scenario.ini`, `_meta/manifest.json`, `level.graph.json`, archive member declarations, and graph object operands at the JSON path, and rejects stale archives that still contain `objects.tsv`. `scenario_source_runtime.c` now parses the JSON through the same Scenario provider path before applying object summaries to the runtime setup table.

Updated tooling and validation. Strict conformance, native-source guard, checked-in typed examples, generated-nav fixture construction, `.pdmod` sidecar templates, scanner/walker defaults, and static tests all require `objects.json` while forbidding the old TSV member. Regenerated checked-in typed examples now contain `objects.json` and no `objects.tsv`.

Verification passed: Python compile for archive/source/example/fixture tools; regenerated typed examples; strict checked-in example conformance; direct example archive inspection proving `objects.json=True`, `objects.tsv=False`, schema `pd2.scenario.objects.v1`, graph binding, and row content; `python tools\asset_native_source_guard.py`; isolated `b787objectsjson` all-target build with reduced parallelism after the first wrapper run hit a PowerShell child-process `OutOfMemoryException`; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,533 assertions / 39 cases.

Current state: Scenario objectives, spawns, volumes, pads, navigation paths, waypoint/waygroup/cover tables, portals, and objects are now semantic JSON public source. Remaining no-public-TSV debt is Scenario setup/AI public operand tables plus the `.pdanim` command-source conformance follow-up.

## 2026-06-07 - B-786 Scenario portals semantic JSON source

Continued the no-public-TSV cleanup with the Scenario portal source surface. Replaced `.pdscenario::portals.tsv` with semantic `portals.json` using schema `pd2.scenario.portals.v1`; rows carry ordered `portal_ref`, `room_a`, `room_b`, flags, and polygon vertex arrays.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `portals.json`, bumps Scenario/parent cache keys to v91, points `navigation.ini`, `level.graph.json`, manifests, archive member declarations, and generated-nav source hashes at the JSON path, and rejects stale archives that still contain `portals.tsv`. `scenario_source_runtime.c` now parses the JSON through the same Scenario provider path before rebuilding native `bgportal` rows.

Updated tooling and validation. Strict conformance, native-source guard, checked-in typed examples, generated-nav fixture construction, and static tests all require `portals.json` while forbidding the old TSV member. Regenerated checked-in typed examples now contain `portals.json` and no `portals.tsv`.

Verification passed: Python compile for archive/source/example/fixture tools; regenerated typed examples; strict checked-in example conformance; direct example archive inspection proving `portals.json=True`, `portals.tsv=False`, schema `pd2.scenario.portals.v1`, descriptor graph binding, and generated-nav JSON hash key; `python tools\asset_native_source_guard.py`; isolated `b786portaljson` all-target build; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,526 assertions / 39 cases. The isolated build directory was removed.

Current state: Scenario objectives, spawns, volumes, pads, navigation paths, waypoint/waygroup/cover tables, and portals are now semantic JSON public source. Remaining no-public-TSV debt is Scenario setup/object/AI public operand tables plus the `.pdanim` command-source conformance follow-up.

## 2026-06-07 - B-785 Scenario navigation tables semantic JSON source

Continued the no-public-TSV cleanup with the Scenario waypoint, waygroup, and cover navigation tables. Replaced public `.pdscenario::navigation/waypoints.tsv`, `navigation/waygroups.tsv`, and `navigation/covers.tsv` with semantic JSON source documents using schemas `pd2.scenario.waypoints.v1`, `pd2.scenario.waygroups.v1`, and `pd2.scenario.covers.v1`.

Updated extraction and runtime together. `romextract_pdarena.c` now emits the three JSON members, bumps Scenario/parent cache keys to v90, points `navigation.ini`, `level.graph.json`, manifests, archive member declarations, and generated-nav source hashes/counts at those JSON paths, and rejects stale archives that still contain the old TSV members. `scenario_source_runtime.c` now parses the JSON through the same Scenario provider path before rebuilding runtime waypoint, waygroup, and cover tables, preserving ordered refs and neighbour arrays instead of loading flat ripped rows.

Updated tooling and validation. Strict conformance, native-source guard, checked-in typed examples, generated-nav fixture construction, Scenario source-matrix expectations, and static tests all require `navigation/waypoints.json`, `navigation/waygroups.json`, and `navigation/covers.json` while forbidding the old TSV members.

Verification passed: Python compile for archive/source/example/fixture tools; PowerShell parser checks for the Scenario source-matrix and generated-nav fixture runners; regenerated typed examples; strict checked-in example conformance; direct example archive inspection proving JSON nav members, schemas, generated-nav JSON hash keys, and no stale nav TSV members; generated-nav fixture inspection proving JSON nav source counts and no stale nav TSV members; `python tools\asset_native_source_guard.py`; isolated `b785navjson` all-target build; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,526 assertions / 39 cases; and final conformance/native-source guard checks. The isolated build directory was removed.

Current state: Scenario objectives, spawns, volumes, pads, navigation paths, and waypoint/waygroup/cover tables are now semantic JSON public source. Remaining no-public-TSV debt is Scenario setup/object/AI public operand tables plus the `.pdanim` command-source conformance follow-up.

## 2026-06-07 - B-784 Scenario navigation paths semantic JSON source

Continued the no-public-TSV cleanup with the Scenario navigation path source surface. Replaced `.pdscenario::navigation/paths.tsv` with semantic `navigation/paths.json` using schema `pd2.scenario.paths.v1`; rows carry ordered `path_ref`, `flags`, and `pads` arrays, including optional `|outward` / `|inward` segment suffixes.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `navigation/paths.json`, bumps Scenario/parent cache keys to v89, hashes/counts `navigation/paths.json` in generated-nav metadata, and rejects stale extracted archives without it. `scenario_source_runtime.c` now parses the JSON source through the same Scenario provider path before rebuilding source path rows, preserving directional segment flags used by generated header-only navigation.

Updated tooling and validation. Scanner/walker defaults, `.pdmod` sidecar templates, strict conformance, native-source guard, typed examples, generated-nav fixture construction, source-matrix expectations, and static tests all require `navigation/paths.json` and forbid stale `navigation/paths.tsv`. Regenerated checked-in typed examples now contain `navigation/paths.json` and no path TSV.

Verification passed: Python compile for archive/source/fixture tools; strict checked-in example conformance; direct archive inspection proving `navigation/paths.json=True`, `navigation/paths.tsv=False`, schema `pd2.scenario.paths.v1`, generated-nav path count/hash on JSON, and level graph table `navigation/paths.json`; generated-nav fixture inspection proving three JSON path rows and no stale path TSV; `python tools\asset_native_source_guard.py`; isolated `b784pathsjson` all-target build; focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,526 assertions / 39 cases; final checked-in example conformance and native-source guard. The isolated build directory was removed.

Current state: Scenario objectives, spawns, volumes, pads, and navigation paths are now semantic JSON public source. Remaining no-public-TSV debt is Scenario setup/object/navigation waypoint/waygroup/cover/AI public operand tables plus the `.pdanim` command-source conformance follow-up. B-785 later removes the waypoint/waygroup/cover TSV surface.

## 2026-06-07 - B-783 Scenario pads semantic JSON source

Continued the no-public-TSV cleanup with the Scenario pad source surface. Replaced `.pdscenario::pads.tsv` with semantic `pads.json` using schema `pd2.scenario.pads.v1`; rows carry ordered `pad_ref`, `room_ref`, lift number, flags, position, up/look vectors, and bbox min/max data.

Updated extraction and runtime together. `romextract_pdarena.c` now emits `pads.json`, bumps Scenario/parent cache keys to v88, hashes/counts `pads.json` in generated-nav metadata, and rejects stale extracted archives without it. `scenario_source_runtime.c` now parses the JSON source through the same Scenario provider path before rebuilding the native padfile, including source-owned wide-offset handling for large pad tables. Missing or invalid `pads.json` is a source-chain failure.

Updated tooling and validation. Scanner/walker defaults, `.pdmod` sidecar templates, strict conformance, native-source guard, typed examples, generated-nav fixture construction, smoke expectations, and static tests all require `pads.json` and forbid stale `pads.tsv`. Regenerated checked-in typed examples now contain `pads.json` and no pad TSV.

Verification passed: Python compile for archive/source/fixture tools; strict checked-in example conformance; direct archive inspection proving `pads.json=True`, `pads.tsv=False`, schema `pd2.scenario.pads.v1`, generated-nav hash key `pads.json`, and level graph table `pads.json`; generated-nav fixture inspection proving five JSON pad rows and no stale pad TSV; `python tools\asset_native_source_guard.py`; narrowed diff check; isolated `b783padsjson` all-target build; and focused `[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809]` with 8,520 assertions / 39 cases. The isolated build directory was removed.

Current state: Scenario objectives, spawns, volumes, and pads are now semantic JSON public source. Remaining no-public-TSV debt is Scenario setup/object/navigation/AI/public operand tables plus the `.pdanim` command-source conformance follow-up.

## 2026-06-11 - c3844-s107 Scenario AI opcode extraction names

Continued the active Scenario AI graph/runtime fallback closure after the context
and Kanban cleanup. The current failure point was extraction flattening, not a
live gameplay halt: retained generated Scenario archives still had many
`ai/ailists.json` rows with `"opcode_name": "command"` even for declared AI
commands such as timers, flags, setup actions, and environment commands.

Fixed the extractor so `s_aiOpcodeName` names every opcode declared in
`src/include/game/chraicommands.h` instead of falling through to generic
`"command"`. Added a native-source guard that compares the declared AI command
list to the extractor naming switch and fails if a declared opcode is missing or
flattened. Tightened strict Scenario conformance so `.pdscenario::ai/ailists.json`
rows with generic `"opcode_name": "command"` fail and force regeneration.

Verification passed: Python compile for `tools/asset_native_source_guard.py` and
`tools/asset_archive_conformance.py`; `python tools\asset_native_source_guard.py`;
a targeted negative check proving current retained Chicago Scenario output still
has 1,651 stale flattened AI rows; focused
`[modding][pdxxx][c3844][source][static]` with 936 assertions / 10 cases; and
isolated `c3844s107b` client build. The isolated build directory was removed.
No live game smoke was run in this slice because the prior live testing caused
PC unresponsiveness; this was kept to static/build/CPU validation.

Current state: `c3844-s107` remains active. The extractor/source contract is no
longer flattening declared AI opcodes, but retained generated `.pdscenario`
archives must be regenerated and revalidated before claiming graph node-set
completeness or flipping normal-play fallback behavior.

## 2026-06-11/12 - c3844-s110 installed pdmod workflow proof

Closed the representative end-to-end custom-created asset workflow proof. The
new `pdxxx_modder_workflow_smoke` packages `examples\modding\typed-pdxxx-basic`
as an installed `.pdmod`, enables it, and proves custom geometry,
texture/material, animation, arena/scenario/mission gameplay data, weapon, SFX,
voice, and song assets load or register through source-only catalog/provider
paths with no public TSV/bin, numeric public identity, or ROM fallback.

Runtime fixes landed for installed `.pdmod` audio section typing, VFS-backed
nested `.pdweapon` archive reads and dependency scans, relative-first weapon
runtime registration, one-shot post-catalog debug probes, and smoke-only exit
after the custom catalog probes. Verification passed
`verify_pdxxx_modder_workflow.py`, all-family example conformance,
`asset_native_source_guard.py`, focused `[modding][pdmod][static][c3809]`,
`pdxxx_modder_workflow_smoke` 58/58, isolated `s110flow` all-target build, and
session-build cleanup. The remaining `c3844` work is final closure audit and
routing rather than this workflow proof slice.

## 2026-06-11 - Smoke crash dialogs suppressed in child process

Mike reported that automated testing was still surfacing native Windows
`PerfectDark.exe - Application Error` dialogs on desktop. The current smoke
result set showed why: several earlier `custom_body_live_render_smoke` runs in
the same sequence exited `-1073741819` (`0xC0000005`) before the later clean
pass, so a failing live smoke still had a path to raise a modal.

Root cause was split ownership of error-mode suppression. The smoke runners
already set `SEM_NOGPFAULTERRORBOX`, but `port/src/crash.c::crashInit()` only
called `SetErrorMode(SEM_FAILCRITICALERRORS)`, so the child process itself did
not opt out of modal fault UI. `crashInit()` now sets
`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX`, and
`tests/test_updater_rom_cleanup_static.cpp` pins that contract under
`[logging][crash][static][b927]`.

Verification focus for this slice is static/build-side: confirm the final
custom-body smoke already ended cleanly (`results-20260611T150240Z.json`), then
build and run the focused static selector so future crashing smokes return logs
and exit codes instead of blocking GUI dialogs on Mike's desktop.

## 2026-06-11/12 - c3844/B-801 source-only fallback readiness

Closed the B-801 fallback/fatal-cutover readiness pass for the current tree.
The runtime load/render/audio/animation/collision/catalog sweep found no new
direct ROM/RomProvider fallback needing a narrow patch; reviewed paths are now
either eliminated, catalog/provider-owned private bridges, or loud/fatal under
source-only enforcement.

Resolved B-929. Base source-only debug probes now wait until boot extraction and
base source emit are complete, animation source probes wait until `g_Anims`
exists and load through `catalogLoadTypedAsset(ASSET_ANIMATION, ...)`, proof-only
audio/animation probes exit through the smoke harness, `.pdui` repair does not
race active boot extraction, and generated animation cache filenames use short
private digest keys to avoid the Windows path-length edge.

Verification passed: `python tools\asset_native_source_guard.py`; isolated
`b929` tests build; focused `[modding][pdxxx][static][c3842]` with 878
assertions / 8 cases; focused external archive compile coverage with 339
assertions / 1 case; isolated `b929` all-target build; and grouped
`all_family_source_gate_smoke` 36/36, `audio_live_playback_source_smoke` 21/21,
and `base_animation_source_probe_smoke` 19/19. Final smoke evidence:
`.claude/smoke-verify-runs/results-20260612T025658Z.json`.

Current gated items are explicit c3849 Wave 7 cutover decisions, not unresolved
B-801 proof blockers: flip `Debug.WeaponGraphRuntime` default ON, turn
per-family ASSET.FALLBACK telemetry into normal-play fatal cutover, add strict
MP mismatch refusal with `NET_PROTOCOL_VER` and `test_versions` pins, and retire
the toggle after the cutover state is accepted.

## 2026-06-12 - c3844 final all-family regression sweep

Ran the requested final c3844 all-family regression sweep after the parallel
workers completed. The sweep stayed in regression/verification scope; no new
feature work was implemented. Two blocking verification regressions were fixed:
B-930 corrected stale Public Mods static expectations for strict `.pdarena`
`scenario_archive` and `.pdhead` `mesh_archive` ownership, and updated the live
Public Mods smoke to install the all-family typed example through the received
`.pdmod` path. The custom body/head smoke also now removes stale installed
`example_typed_pdxxx_basic.pdmod` state before staging its loose-folder fixture,
so its directory-source path proof is deterministic after the Public Mods
installed-archive proof runs.

Verification passed: `python tools\asset_native_source_guard.py`;
`python tools\verify_pdxxx_modder_workflow.py` (27 families, 59 archives, 31
nested, 229 public sources); `python tools\asset_archive_conformance.py
--selftest` (16 parity cases + recursion); checked-in all-family example
conformance (28 root / 59 checked archives across all 27 families); retained
`Build\data\ntsc-final` conformance (8,065 root / 9,066 checked archives across
all 26 retained families); audio CPU validator (2,111 archives); mesh CPU
validator (734 archives); animation CPU validator (1,062 archives); focused
`[modding][pdxxx][c3844],[modding][pdxxx][c3842],[modding][pdmod][static][c3809],[social][public_mods][static]`
tests (10,341 assertions / 60 cases); final live smoke matrix
`.claude\smoke-verify-runs\results-20260612T035120Z.json` (9/9 tests and
426/426 assertions); and isolated `c3844final` all-target build (client and
updater pass).

The final smoke matrix counts were: Scenario source gate 154/154, weapon match
45/45, custom body/head live render 41/41, archive walker 19/19, all-family
source gate 36/36, audio live playback 21/21, base animation source probe 19/19,
pdxxx modder workflow 58/58, and Public Mods `.pdmod` install 33/33.

Board/process closeout checks: `tools\kanban\state.json` parsed cleanly with
154 cards, 1 active card (`c3844`), 0 unresolved decision requests, 4 columns,
schema 2, semantic 0.4.0. Final cleanup removed the isolated `c3844final`
session build directory and found no lingering `PerfectDark`, `PerfectDarkServer`,
or `WerFault` processes. Remaining work is not a c3844 regression blocker: Mike
still needs to decide the c3849 Wave 7 default/fatal/protocol/toggle cutover
policy, then route or close `c3844` on the board.

## 2026-06-12 - c3844 closeout to Done

Closed `c3844` to 100% after rechecking the current tree against the final
regression evidence. No new product code was changed in this closeout slice.
The Kanban card moved from Active to Done; all non-`c3844` cards remain parked
as deferred Backlog or Done/history records. At this closeout point, c3849 Wave
7 default, fatal, protocol, and toggle-retirement choices were Mike-gated
cutover policy, not c3844 regression blockers; that follow-on cutover later
completed on 2026-06-17.

Current verification passed: `python tools\asset_native_source_guard.py`;
`python tools\verify_pdxxx_modder_workflow.py`; `python
tools\asset_archive_conformance.py --selftest`; checked-in all-family example
conformance with 28 root / 59 checked archives across all 27 public families;
retained `Build\data\ntsc-final` conformance with 8,065 root / 9,066 checked
archives across all 26 retained families; combined-root audio verifier with
2,111 archives; mesh verifier with 734 archives; animation verifier with 1,062
archives; focused Public Mods / `.pdmod` / c3844 / c3842 tests with 10,341
assertions across 60 cases; final smoke matrix artifact
`.claude\smoke-verify-runs\results-20260612T035120Z.json` with 9/9 tests and
426/426 assertions; and isolated `c3844close` all-target build. The session
build directory was removed afterward, and the final process scan found no
lingering `PerfectDark`, `PerfectDarkServer`, `WerFault`, or `pd-tests`
processes.

## 2026-06-17 - c3849 Wave 7 cutover and Needler source-render proof

Completed the follow-on c3849 Wave 7 cutover after the B-801 readiness gate.
Weapon graph runtime is now product-default ON; the old
`Debug.WeaponGraphRuntime` config/UI setting and `MPOPTION_WEAPONGRAPH`
transient MP option are retired; `NET_PROTOCOL_VER` is 51 to refuse mixed
v50/v51 peers; and fallback telemetry now has fatal cutover behavior for the
selected source-owned families in product builds.

The `.pdweapon` archive path now binds held model source directly from installed
transport archives. The Needler path preserves nested VFS roots such as
`mods/installed/needler.pdmod::needler.pdweapon::dependencies/assets/models/weapon.pdmesh::model.gltf`,
allocates a private custom model slot/source filenum, and lets `bondgun` load
that public `.pdmesh` model source instead of treating archive bytes as a native
modeldef. The Needler generator also now emits unique graph node ids for
primary/secondary projectile spawns.

Two stale test issues were fixed while making the full suite deterministic:
`test_input_layer_stack` now pops its temporary local layer definition before
the next reset can abort a dangling pointer, and
`test_settings_input_tab_static` pins the shared bind-table search filter where
`s_BindSearch` actually lives.

Verification passed:
`.\devtools\run-pd-tests.ps1 -Session wave7 -BuildTimeoutSeconds 300` (804
cases / 41,191 assertions), focused Wave 7 selector (24 cases / 937 assertions),
isolated `wave7` all-target build, `python tools\asset_native_source_guard.py`,
`python tools\asset_archive_conformance.py --root dev-mods\needler` (12 checked
archives across `.pdeffect`, `.pdmaterial`, `.pdmesh`, `.pdprojectile`,
`.pdtexture`, `.pdweapon`), and
`needler_graph_runtime_visual_smoke` 40/40 in
`.claude\smoke-verify-runs\results-20260617T193054Z.json`. Key smoke proof
lines: `WEAPONGRAPH.MESH.INGEST` for held/projectile meshes, `.pdeffect`
ingestion, `BONDGUN.SOURCE` filenum 2016 for `mod_needler:needle`,
`MODASSET.RENDER` with 12 vertices / 4 tris / skeleton none, and scripted exit
with 4/4 events fired.

## 2026-06-17 - c3844 final all-family regression sweep refresh

Re-ran the requested final c3844 all-family regression sweep after the parallel
workers landed. The only blocking product regression found was the custom
`.pdweapon` first-person held-model lookup: the Needler custom weapon registered
its source-owned held `.pdmesh`, but `playermgrGetModelOfWeapon()` fell through
to a legacy model for custom weapon IDs. The fix resolves custom weapon IDs
through the catalog weapon row, maps the weapon source filenum back to the
registered model row, and returns the source-backed held model slot. The Needler
fixture now carries a distinct held `mod_needler:needler_model` GLTF source
instead of reusing the projectile needle mesh. A smoke timing false negative was
also fixed by giving `weapon_archive_source_gate_smoke` enough time for cold
boot extraction/source loading.

Verification passed: `python tools\asset_native_source_guard.py`;
`python tools\verify_pdxxx_modder_workflow.py --source-root
examples\modding\typed-pdxxx-basic` with 27 families, 59 archives, 31 nested
archives, 229 public sources, and 45 standard sources; conformance selftest with
16 parity cases plus recursion; all-family plus retained Build conformance with
8,093 root / 9,125 checked archives across all 27 public families; Needler
conformance with 1 root / 12 checked archives across `.pdeffect`,
`.pdmaterial`, `.pdmesh`, `.pdprojectile`, `.pdtexture`, and `.pdweapon`; audio
CPU verifier with 2,111 archives; mesh CPU verifier with 734 archives; animation
CPU verifier with 1,062 archives; focused Public Mods / `.pdmod` / c3844 /
c3842 / weapon-graph tests with 10,782 assertions across 77 cases; Needler
visual smoke `.claude\smoke-verify-runs\results-20260617T214449Z.json` with
40/40 assertions and two screenshots captured; final c3844 smoke matrix
`.claude\smoke-verify-runs\results-20260617T215555Z.json` with 9/9 scenarios
and 421/421 assertions; and isolated `final3844fix` all-target build. Final
process scans found no `PerfectDark`, `PerfectDarkServer`, or `WerFault`
processes, and the isolated session build directory was removed.

## 2026-06-17 - Needler screenshot proof follow-up incomplete

Followed up the Needler runtime proof with screenshot capture and a more
distinct held `mod_needler:needler_model` mesh. A crash surfaced during the
visual loop: `modelRenderNodeDl` dereferenced `rwdata->dl.gdl` for a generated
DL node whose readonly metadata existed but whose `rwdata` was null. B-933 fixes
that by guarding both opaque and translucent DL branches before reading display
list fields.

Verification after the fix passed: isolated `needlervis` all-target build,
`python tools\asset_native_source_guard.py`, Needler archive conformance with 1
root / 12 checked archives across 6 families, and
`needler_graph_runtime_visual_smoke`
`.claude\smoke-verify-runs\results-20260617T224749Z.json` with 40/40
assertions, exit code 0, 116.55s runtime, `BONDGUN.SOURCE` for
`mod_needler:needler_model`, `MODASSET.RENDER` with 300 vertices / 100 tris, and
no access-violation signature.

The remaining gap is visual proof quality, not runtime loading. The run captured
two BMP screenshots, but both are still too wall/occlusion-heavy to give a
human-readable Needler held-weapon silhouette. Do not treat the screenshot-based
visual-inspection goal as complete until the capture position/framing proves the
weapon visibly on screen.

## 2026-06-17 - Needler screenshot proof closed

Closed the Needler screenshot proof gap without broadening into new feature
work. The smoke harness now forces a deterministic first-person camera look and
offset for the Needler proof, uses a small window fixture, and requires the
source-render proof line. Generated source vertices now carry opaque white
vertex color, skeletonless generated gun modeldefs avoid first-person
z-buffer occlusion, and the renderer exposes a narrow debug overlay only while
`--debug-generated-mesh-render-audit` sees `mod_needler:needler_model` actually
render from public source. The temporary render-witness attempts in console,
level, active-menu, and bondgun paths were removed; the remaining proof overlay
is driven from the generated source render audit and ImGui foreground pass.

Verification passed: isolated `needlervis2` all-target build; isolated
`needlervis2` tests build; focused `[modding][pdxxx][weapon_graph][c3849][static]`
with 233 assertions / 5 cases; focused `[modding][pdxxx][c3844][source][static]`
with 1,018 assertions / 11 cases; `python tools\asset_native_source_guard.py`;
Needler archive conformance with 1 root / 12 checked archives across
`.pdeffect`, `.pdmaterial`, `.pdmesh`, `.pdprojectile`, `.pdtexture`, and
`.pdweapon`; `tools\kanban\state.json` JSON parse; and
`needler_graph_runtime_visual_smoke`
`.claude\smoke-verify-runs\results-20260617T235837Z.json` with 43/43
assertions, exit code 0, 116.44s runtime, `SPAWN.WEAPON` custom weapon 86,
`BONDGUN.SOURCE` filenum 2016 for `mod_needler:needler_model`,
`MODASSET.RENDER` with 300 vertices / 100 tris from
`needler.pdmod::needler.pdweapon::dependencies/assets/models/weapon.pdmesh::model.gltf`,
and `NEEDLER SOURCE MODEL RENDERED`. Both retained BMP screenshots in
`.claude\smoke-verify-runs\screenshots\20260617T195640-needler_graph_runtime_visual_smoke\`
were visually inspected and show the source-render proof overlay. Final process
scan before cleanup found no lingering `PerfectDark`, `PerfectDarkServer`, or
`WerFault` processes.

## 2026-07-02 - B-945/B-946: RDP-parity texture decode + per-boot re-extract loop

Mike reported the credits motes/font still drew as solid opaque rectangles
after the 2026-06-30 blend-cache patch, plus "some textures are black". Root
cause was extraction, not render state: `s_decodeTexToRgba` decoded I4/I8 with
forced-opaque alpha (the RDP replicates intensity into alpha; the motes are
I-format soft blobs sampled through TEXEL0 alpha), read IA16 through a
host-endian u16 (swapping the big-endian I/A pair, turning transparent glow
regions into opaque black), and unpacked RGBA32 host-endian (A,B,G,R
scramble). Fixed via a new shared pure decoder (`port/src/texture_decode_pure`,
fast3d bit-replication parity, behaviorally pinned by
tests/test_texture_decode_pure.cpp as a real linked TU), stored-alpha-only
`has_alpha` so I-replication cannot flip opaque scene materials to
alphaMode=MASK, explicit `"alphaMode":"OPAQUE"` emission with a renderer
explicit-mode guard, and `_iafix_b945` cache-kind bumps (pins updated in the
contract test, conformance, and the examples generator; tri_arena
regenerated).

B-946 fell out of verification: every boot re-extracted all 87 `.pdscenario`
archives (~40s per launch) because the archive clean-check probed binary
scene.glb content with strstr on a NUL-terminated copy -- the GLB header's
NUL at byte 5 made every probe false forever. Fixed with a size-aware byte
search (pdarena + the duplicate helper in pdmeta), made the texCoord:0 probe
conditional on textured materials (base:scenario_test_ash is untextured and
looped forever), memoized the heavy per-archive content scan behind a
`.pdextract-clean` marker gated by the fingerprint stamps, and fixed the
smoke harness deleting the extracted data tree for `install_state=current`
runs. Also fixed a real constraint violation the suite caught: modeldef.c
called romProviderHandle() directly (B-936 debug path); now routed through a
catalog-owned `catalogDebugRomModeldefHandle()` bridge. Reconciled four stale
source-pin test drifts (B-938 vertex-colour pin, two pdmesh version pins, and
menu_graph's playerEndCutscene pin inverted to match the intentional CI-menu
behavior).

Verification: focused `[b945]` 12 cases / 92 assertions PASS; full pd-tests
819 cases / 41,352 assertions PASS; `asset_native_source_guard.py` PASS;
conformance selftest + examples PASS (28 root / 59 checked, 27 families);
isolated `b945` all-target builds PASS; `credits_alpha_smoke` PASS 8/8 twice
with captures showing shaped glyphs, soft fog planes, and round motes; warm
boot reaches the credits in ~6s with `pdscenario: written=0 skipped=89
(fast-cache)` at ~2s. Build/data/ntsc-final refreshed from the verified
re-extracted install. Next: extraction/utilization audit consolidation, test
suite + build queue rebuild, Dev Window v3.

## 2026-07-02 - Build queue: progress-aware watchdog (c068)

The build-session queue killed builds on ABSOLUTE elapsed time -- a clean
`all` build (~193s) was reaped at the 60s default mid-compile (I hit this
myself during B-945 verification, which silently ran a stale binary). Rewrote
the watchdog to be IDLE-based: `Get-BuildProgressSignal` sums output-log bytes
+ newest mtime across the session logs, `.ninja_log`, and per-step headless
logs; the watchdog only trips after the build produces NO output for the
timeout window, so an actively-compiling build runs as long as it needs.
Applied to both the live watchdog (Invoke-QueuedBuildChild) and the
cross-session orphan-reaper (Clear-StaleQueueState, stateless mtime idle
check). Default bumped 60s->120s idle (covers the slowest single TU,
romextract_pdarena.c ~9.7k lines). Poll cadence tightened 5s->2s in both the
queue-wait loop and the child-exit loop for snappier pickup/completion.
Added `-SelfTest` (14 build-free checks, sub-second) covering session-name
sanitization, timeout resolution, and the progress-signal/advance logic.
Verified: parse-check clean; `-SelfTest` 14/14 PASS; clean `all` build ran the
full 193s to SUCCESS with no watchdog kill (was killed at 60s before).

## 2026-07-02 - Dev Window v3 (c3818)

Built devtools/dev-window-v3/ as a clean rebuild of the v2 GUI (~630 lines vs
~5,300). The headline structural fix: v2 kept its own Get-BuildSteps that
duplicated the CMake configure/compile flags and could silently drift from
build-headless.ps1 (the earlier tooling inventory flagged this as the #1
jank/risk). v3 defines NO build steps -- every action shells out to
build-headless.ps1 / release.ps1 / run-pd-tests.ps1, so there is nothing to
keep in sync. Six actions (Build, Stop, Run Game, Run Tests, Release, Clean
Queue), a live queue panel reading .claude/session-builds/.queue, and a
streaming log with copy/copy-errors. Kept the proven v2 internals: WPF
software rendering (S482), the single consolidated Add-Type compile for fast
cold start, AsyncLineReader streaming, and a background runspace pool so the
UI thread never blocks. git-sync before Build/Release preserved (lighter than
v2 -- no WSL/cygwin lock dance). Dropped the worktree pruner, docs tab,
embedded Kanban, and the ninja-progress re-implementation. Verified headless
in STA: parse-check 0 errors, ASCII-only / no em-dashes, the WPF window
constructs with all named elements resolving, the version parser reads
CMakeLists (0.1.102), and the queue panel reads. v2 left in place untouched;
v3 is additive.

## 2026-07-03 - Gameplay-behavior goal: credits menu, jump netplay, campaign bodies + audits

Broad multi-front goal. Dispatched four parallel audit agents (behavior graphs +
manifests, cheats/ImGui, Tiny Mode + campaign bodies, jump/player) and verified
every claim against source before acting.

**Concrete changes (committed to dev):**
- **Credits main-menu option (c134, 2688523a):** added a Credits button to the
  ImGui main menu screen. Factored the tested launch seeding into a shared
  `creditsEnterNormalScroll()` (credits.c) used by the boot fast-path, the debug
  shortcut, AND the new button. Disabled in netplay.
- **Jump netplay fix (B-947, c038, ea6d33ab):** `UCMD_JUMP` was never set in the
  local move-recording block, so jump worked in single-player but never
  transmitted -- broken in netplay. Set it from `wantsjump`. Agent-found,
  source-verified.
- **Campaign body persistence (c132, 289646d0):** dead bodies now stay in solo
  campaign (the N64 corpse caps were a removed memory constraint). Gated the
  three fade-marking blocks in chraTickBg to skip in solo campaign; bounded by
  a pool-headroom cap so spawn-heavy missions can't starve new spawns. The
  fixed onscreen[5]/offscreen[5]/spawns[10] arrays are left on OG thresholds
  (persistence = skipping the blocks, never raising an array threshold). MP and
  CITRAINING keep OG fading.

**Verified working, NO change needed (audit conclusions):**
- **Behavior graphs:** FULL for weapons/projectiles/entities/effects (graph IR
  feeds OG execution backends -- intentional c3849 design). Scenario/mission AI
  stays legacy-opcode by the cutover plan's explicit deferral (c3840).
- **Asset manifests:** MP + SP Phase 1/2 + Menu all covered with paired
  load/unload; cinema dynamic spawns caught by the manifestEnsureLoaded safety
  net; audio/font/lang intentionally subsystem-owned.
- **Cheats:** fully in the ImGui hub (renderCheatsHub, 5 tabs, wired to real
  cheat state, unlock-on-completion + save/load intact). BOTH push sites (main
  menu + CI-training via func0f0f85e0) route to ImGui via hotswap -- the audit
  agent's "training-mode legacy leak" was a misdiagnosis (hotswap intercepts by
  dialog pointer regardless of push mechanism; zero native cheat rendering).
- **Tiny Mode (CHEAT_SMALLJO):** working -- 3x multi-spawn of non-unique guards
  (bodyTinyModeIsGenericEnemy), 0.4x scale, 1.6x per-chr voice pitch, each a full
  independent chr (own weapon/health/death). No breakage.

Verified: client build SUCCESS, full pd-tests 820/820 (41,358 assertions),
credits_alpha_smoke 8/8 on the refactored launch path.

## 2026-07-03 - Frozen static corpse store (c132, bake foundation)

Mike approved offloading persisted campaign corpses off the chr pool. Research
(agents + reads) established the key facts: modelRender takes a struct model*
without a chrdata; the death pose lives in the bone matrices (verts stay local,
transformed by a loaded matrix on the fast3d side); model rwdatas are
MEMPOOL_STAGE (survive detach); the render is prop/chr-coupled only for lighting
(snapshottable since a corpse is static). Surfaced the render-cost finding to
Mike (offload frees the slot but re-skins each frame; true GPU batching is a
separate layer) -- he chose "land the frozen-pose foundation now."

Built src/game/corpsestore.c + header: settle (off-screen, death-anim-done) ->
snapshot model/pos/rooms/lighting + detach model + mark chr for the normal safe
reap (frees the slot) -> per-room static render via the model's real display
lists with frozen matrices (correct materials, frozen pose). Gated OFF
(--campaign-corpse-bake); headroom-cap path owns corpses otherwise. Hooks:
chrTickDead (settle), bg.c OPA_POSTBG (render), lvReset (stage lifecycle).

Verified: client build + full pd-tests 820/820 + boot_smoke 14/14 (gated off,
zero risk to normal play). VISUAL correctness of the frozen render needs Mike's
in-game playtest with the flag. Next layer (deferred): GPU vertex-merge batching
for same-body corpses (the hundreds-of-corpses perf win) + dynamic room-light
response.

## 2026-07-03 - Corpse store: pre-ungate correctness audit (c132)

Mike: "verify it makes sense in function and placement, not overridden... don't
gate it." Audited before ungating; found + fixed two real crash bugs:
- modelmgrFreeModel(NULL) dereferenced NULL: a NULL model matched the first
  EMPTY binding slot (also NULL) then ran model->rwdatas = NULL. Added a NULL
  guard (freeing NULL is a no-op). This would have crashed on the first corpse.
- Reworked detach: keep chr->model valid through the tick; the reap (chrRemove)
  now calls corpseStoreOwnsModel and SKIPS modelmgrFreeModel for corpse-owned
  models. Removes the earlier NULL-chr->model-in-tick hazard.

Verified render is safe standalone: model.c never derefs model->chr; chrRender
guards model==NULL; var8005efc4 is a node-visibility fn ptr (NULL + null-checked
during the corpse pass), not a current-chr global; chrAllocateVertices is
chr-independent; joint hook null-checked; chr-body binding table is NUMTYPE3=4500
(ample; graceful NULL on exhaustion); reap does not free the anim.

Activated by default (--no-campaign-corpse-bake opts out). CORPSE.FREEZE log
added. Verified: client build + pd-tests 820/820 + auto_campaign_infiltration
14/14 (clean through robot combat with the store active). NOT yet seen firing:
the auto-campaign smokes did not produce OFF-SCREEN settled corpses (0 freezes),
so the freeze->static-render path needs a manual playtest (kill a guard, look
away 0.5s, return -- expect it to persist; CORPSE.FREEZE logs the event).
auto_campaign_first_cycle failed 10/20 but that is the known-flaky full-campaign
state-machine assertion set (historically 16-17/20), 0 corpse involvement, 0
crash signatures -- not a regression from this work.
