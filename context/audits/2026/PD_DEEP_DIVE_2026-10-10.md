# Perfect Dark — Deep-Dive: Stability, Asset Pipeline, Behavior Graphs, Editor/Theater/Modding
**Date:** 2026-10-10 · **Method:** read-only, local files only, four parallel investigation lanes + synthesis. File:line evidence throughout. Nothing modified.

---

## The one thing that gates almost everything

**The code is green; you just can't *run and see* it.** The build compiles/links, the full automated suite passes in CI (CI37176234687 succeeded 2026-10-04; ~1,835 cases / 334,167 assertions), release tags reach **v0.1.102** (the "v0.1.8" figure is stale). But the project **cannot verify an ordinary running client**, and that single infrastructural wall is why most "fixed" work is unproven and every release-visual gate (V-010..V-013) is open.

The blocker is three stacked obstructions (tracked as V-014 / D-010):
1. **Foreground HWND = 0.** On the build desktop (`CodexSandboxDesktop`) `GetForegroundWindow()` returns 0 and input-desktop read fails with error 5, so the smoke harness never gains foreground input ownership and runs time out with no input injected (`context/audits/2026/menu-accessibility-2026-09-06.md:46-47`). A partial mitigation — an opt-in `-MaintainForeground` runner that *did* acquire the game HWND (`:175`) — exists but isn't the standard path.
2. **Computer-Use app grant missing.** The automated preview needs Computer-Use approved for the **PerfectDark** app; `get_window_state` was denied "Computer Use was not approved to use perfectdark" (V-014 detail). Local runner capability is proven; the gap is the per-app grant + a real window (defeated by #1).
3. **B-801 memory-guard launch refusal.** The runtime refuses to launch below a 2048 MB available-commit floor (923 MB observed), blocking journey runs (`bugs.md:223-224`).

**Unblock sequence:** run on a real interactive desktop via `-MaintainForeground`, grant the PerfectDark Computer-Use app ("Always allow"), ensure ≥2 GB free commit, then capture via the proven bootstrap path and view frames critically. **Until this is done, nothing below can be *verified*, only coded.**

---

## 1. Stability + getting Base Game content running properly

**Stable:** build/link/updater, the automated test suite, and the historic crash classes (anim CPU-spin B-1101/B-1102, reconnect AVs B-1096/B-1097, inputlayer SIGSEGV B-312, FIESTA weapon-array overrun B-263, model rodata AV B-261) are all **fixed and guarded**. No open SIGSEGV/heap-corruption bug is tracked; there's no `Saved/Crashes` corpus. The `exit 1` seen in verification runs is the readiness *timeout* from the blocker above, not a crash.

**Broken / unverified for Base Game:**
- **A cohort of real base-game defects is source-complete but NEVER built or run** (`T-ENGINE-005`, status partial — "No C/C++ build, pd-tests, game, smoke, capture… has run"): **B-249** (bot-vs-bot kills mis-credited to player 0), **B-183** (body 86 / head 4 has no `MODELPART_CHR_HEADSPOT` → missing head geometry), **B-174** (Car Park bots stack at one spawn), **B-242** (players/bots spawn intersecting walls, no capsule-fit validation).
- **B-1068 (confirmed):** `scenarioLoad` resets/mutates live match/player/bot state *before* the file is fully validated; an invalid typed arena/scenario/weapon/body/head can fall through to a legacy integer or partial value, and arena load writes a stage-table index into the stagenum domain with wrap-prone casts — directly contradicting V-006's "fail-closed" claim. A real correctness/stability hole in the load path; fix authored, not run.
- **Protocol proof gap:** source is `NET_PROTOCOL_VER 61` (`port/include/net/net.h:13`), but the MP/reconnect/co-op/cutscene/roster regression gates (B-1064…B-1104) were production-verified at **v57/v58**. v59 (hoverbike authority), v60 (room settings/playlists), v61 (authenticated presentation roster) landed since. Only a native roster check + 2-client Join 37/37 touches v61. **Most "verified" networked base-game behavior is unproven on the shipping wire.**
- **Milestone 1 ≈ 19–20%** (3 validated / 9 partial / 4 missing of 16), stalled on *verification*, not code. The gate holding it is V-010 (`missing/not_run`).
- **Stale orientation doc:** `context/README.md` "live state" still says wire v59 / build v0.0.175+; truth is v61 / v0.1.102. Anyone orienting from the README inherits a two-version-stale picture.

**What needs done:** (1) clear the runtime-verification blocker; (2) build+run the T-ENGINE-005 cohort; (3) verify B-1068 (corrupt/mixed-validity + v1→v3 migration + save/load/start smoke); (4) re-run the MP/reconnect matrix on v61; (5) fix the stale README live-state.

---

## 2. Asset pipeline I/O — ROM → `*.pdxxx` → game

**Working (static):** ROM extraction across all 27 families (V-002 byte round-trip validated; 5,562/5,562 archives register), `*.pdxxx` catalog load/provider, public-source guard (V-001), `.pdmesh` static extraction (733/733). This half is genuinely complete and passing.

**Broken / unproven (runtime round-trip):**
- **B-1106 (THE gap, critical):** after a clean 733/733 extraction, `base:sp_body_118` / nested `base:model_cchicrob` fails the runtime render-stream contract and **rolls back 64 Main Menu loads**, so the Carrington door models never activate. Root cause (verified in code): the producer resets geometry knowledge between consecutive opaque/translucent lists while retaining the live RSP vertex cache; flattened schema-v2 had no typed cache provenance. The **schema-v3 fix is real and landed** (`port/include/modasset_render_stream.h:19-23`; producer `romextract_pdmesh.c:2712-2717`; pure contract `modasset_render_stream.c`; runtime consumer + fail-closed relocation `modasset_compiler.c:6834-8486`) — but it is **only automation-accepted, never production-verified** (the prior attempt died to a host power interruption). V-010 is `missing/not_run`.
- **B-1086 (high):** 3440-wide Carrington far-right door solid-white obstruction, gated behind B-1106; needs a true-3440 visual proof.
- **B-183 (high):** head/body assembly gap — extraction "succeeds" while producing content that's wrong when assembled (round-trip-fidelity failure static checks pass).
- **Structural issue:** the pipeline repeatedly accepts "public source feeds the native consumer" as a proxy for "it renders correctly" — only **19% production-validated** (3 validated / 9 partial / 4 missing of 16 leaves). Representative ledger admissions: "This is native/source acceptance, not rendered appearance."
- **Ingest hardening (medium):** ~20 unbounded `strcpy` into fixed catalog struct buffers (`assetcatalog_scanner.c`), same class as the 31→63-char render-stream truncation that already bit a real run; an `FS_MAXPATH` ingest path-length limit that already caused a fatal.

**What needs done:** (1) production-verify B-1106/B-1086 via the one outstanding true-3440 Carrington smoke (Main Menu manifest commits, `model_cchicrob` builds, both doors activate once, V-009 no-obstruction passes, + Campaign/Combat-Sim propagation); (2) close B-183; (3) finish the embedded-typed-source dependency audit for not-yet-covered families; (4) convert "native-accepted" leaves to rendered-output proof; (5) harden ingest buffers.

---

## 3. All game behavior + weapons → modular behavior graphs

**This is the least-far-along pillar — barely started.** There are two systems, often conflated:
- **v1 (`weapon_graph_runtime.c`, closed):** a **value adapter, not an executor** — graph JSON compiles to IR whose authored scalars (damage, RPM, flags…) are fed into the **original N64 decomp routines** in `bondgun.c`/`propobj.c`, which still execute *all* behavior. Authored events/edges/gates do **not** drive control flow. All 86 base weapons flow through this.
- **v2 (`weapon_graph_v2*.cpp`, T-MODDING-002 partial):** a **genuine executable async DAG executor** — but it implements exactly **one** firing profile (`held_single_shot.v1` = hitscan single-shot), **custom-slot-only, offline-only**. It fails closed on network, Theater, bots, base weapons, and every non-single-shot archetype (`weapon_graph_v2_catalog.cpp:76-78`: "network/Theater/bot execution is not implemented").

**Coverage reality:**
- **Only 1 of 10 firing archetypes is graph-executable.** Missing (all still native): automatic, burst (explicitly blocked, `weapon_graph_v2.cpp:191-194`), beam, charge/release (Mauler), thrown, melee, device/special, projectile, entity.
- **General behavior is ~0% graph-driven.** Real executable graph runtimes exist *only* for Forge-placed props (`prop_graph_runtime.c`, narrow) and effects. **AI/characters/bots are 100% native decomp** — no behavior-graph runtime. Doors/lifts/cameras/triggers = native. Objectives are data-driven *state evaluation*, not executed mission logic.
- **No graph weapon has EVER been proven to fire/hit/render in real gameplay** — every passing receipt is an offline harness "installed" gate (blocked by the same runtime-verification wall). T-MODDING-008 (ship a sample weapon) and V-010/V-012 are missing.

The v2 executor architecture is sound and genuinely live-wired (threaded through `bondgun.c` `bgunGraphV2Accept/Tick/Cancel`), so the foundation is real — it just covers one archetype offline.

**What needs done (in order):** (1) prove the one existing slice live (unblock the runtime wall); (2) add v2 archetypes in the planned order (auto/burst/beam → charge/throw/melee/device, with the Mauler/throw emitter-event correction); (3) projectile + entity executable profiles (fix embedded-asset ingestion B-911); (4) **lift the net/Theater/bot gates** — the biggest shipping blocker for a multiplayer-first game; (5) base-weapon cutover so base + custom share one contract; (6) generalize beyond weapons — promote the prop graph runtime to general props/doors/triggers, then build the big missing piece, a **character/AI behavior-graph runtime** (currently 0%).

---

## 4. Level editor / Theater / Modding pipeline

| System | Verdict |
|---|---|
| **Forge (level editor)** | Prototype wired to a **non-existent asset library** |
| **Theater (record/replay)** | **Recorder real; player entirely absent** |
| **Modding pipeline** | **Most complete — genuinely production-grade** |

**Forge / "The Grid":** the editor shell, placement, gizmo, 256-op undo, property editor, prefabs, and a *real* fail-closed runtime spawn path all exist. **But its ~100-entry object catalog references assets (`base:floor_1x1`, `base:prop_crate_large`, `base:prop_barrel`…) that do not exist in the game catalog** (`forge_core.c:61+` vs `assetcatalog_base_extended.c:1126-1130`; no `prop.json`/`.pdprop` in `data/`), so core placement either logs "cannot resolve" and spawns nothing or trips a fatal. The headline capability is non-functional against shipped content. On top of that: logic actions are log-only placeholders (`forge_logic.c:10,124,278`), navmesh generation is a stub returning 0 (`forge_ai.c:10,87` → placed AI can't path), and save writes a legacy folder mod instead of `.pdmod`. Roadmap `T-MODDING-005 = missing`.

**Theater:** a mature snapshot/checkpoint + event-stream **recorder** is production-wired (`theaterTick` in the main loop; atomic `.pdth` write + crash recovery; match/roster/manifest capture). But **playback is entirely unimplemented** — `theaterStartReplay` validates then `return -1` ("world playback unit is not connected"), `theaterIsReplaying` hardcodes `return 0` (`theater.c:1232-1257`). The user-facing purpose — watching a match with play/pause/seek/spectator — **does not exist**. The recorder also self-logs that it omits effects/explosions/smoke and animation/audio/cutscene streams. Note: it's a snapshot recorder, not deterministic re-sim or input replay, so even once playback exists it's interpolated reproduction (a fidelity ceiling by design). `T-RELEASE-003` Milestone 2 = 0/2 gates; V-011 missing.

**Modding pipeline (the strong one):** `.pdmod` format/loader/VFS/per-mod cache, SHA-256-verified network distribution with zero-digest rejection and a trust-threshold gate, install transaction with commit/rollback, nested typed-archive transfer, a distribution queue with capacity caps, strict path-traversal rejection, and BYOR ("bring your own ROM") extraction — all real and tested (89/89 real-peer fixture; the B-1038 content-distribution work). Open: the declined-content stage-audience defect (candidate fix, qualification open), WAN/NAT + large-transfer performance unproven, **no code signing and no sandbox** (once a `.pdmod` reaches `mods/installed/` it runs fully trusted — defensible for a friend-graph trust model but should be an explicit documented decision), and no shipped graph-backed sample weapon (T-MODDING-008 missing).

**What needs done:** Forge — **author the building-block asset library** (or repoint the catalog at existing `base:model_*`) so placement works, then wire logic execution, implement navmesh, and migrate save to `.pdmod` into `mods/installed/` with Combat-Sim selection + network sync. Theater — **build the playback unit** (load `.pdth`, reconstruct stage, drive entities from checkpoints+events, add play/pause/seek/spectator), extend recorder coverage to effects/anim/audio, and decide the graph-weapon-in-Theater policy. Modding — close the declined-content defect, prove WAN/perf, ship the sample weapon, and make an explicit signing/sandbox decision.

---

## Overall: the critical path

1. **Clear the runtime-verification blocker** (foreground HWND via `-MaintainForeground` + grant the PerfectDark Computer-Use app + ≥2 GB free commit for B-801). This is the master key — it unblocks verifying everything else, and most of the "source-complete but unrun" work collapses into "run it and look."
2. **Build + run the base-game defect cohort** (T-ENGINE-005: B-249/B-183/B-174/B-242) and **verify B-1068** — these are real correctness/stability bugs with fixes already written.
3. **Production-verify B-1106** (the render-stream schema-v3 fix) with the 3440-wide Carrington smoke → closes V-010 → proves the ROM→`*.pdxxx`→game round-trip.
4. **Re-run the MP/reconnect regression matrix on v61** so networked base-game behavior is proven on the shipping protocol.
5. **Behavior graphs:** prove the one existing weapon slice live, then expand archetypes and lift the net/Theater/bot gates; the long pole is a character/AI behavior-graph runtime (0% today).
6. **Editor/Theater:** author Forge's asset library + wire its logic/navmesh; build Theater playback. **Modding** is already strong — finish the declined-content fix, WAN proof, and a sample weapon.

**Honest overall read:** Perfect Dark's *foundation* is genuinely strong — clean port boundary, green build, deep test culture, complete asset extraction, memory-safe netcode, production-grade modding distribution. It is stuck behind a single infrastructural wall (can't run-and-see an ordinary client), which makes a large body of written-but-unverified work look like progress that isn't yet real. And the "all behavior as modular graphs" ambition is at the very beginning (one weapon archetype offline; zero AI). Clear the verification wall first and the true state of everything else becomes visible — and verifiable — at once.
