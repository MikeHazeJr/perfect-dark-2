# Deep Project Audit Report - Perfect Dark 2

Date: 2026-05-13
Scope: full project, with explicit emphasis on followup work + infrastructure migration completeness
Auditor: super-audit skill (Claude, main-checkout-sprc036 session)

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose

Perfect Dark 2 is a community fan re-engineering of Rare's *Perfect Dark* (N64, 2000), built on the n64decomp -> fgsfdsfgs port -> jonaeru allinone lineage. PD2 layers six PD2-native pillars on top: the Catalog (single source of truth for all assets), equal-footing mod loading, deterministic modding pipeline, the Grid (social layer), distinct client online mode, and a game-agnostic dedicated server.

### Likely Player & Contributor Types
- PD / GoldenEye veterans (offline + LAN play)
- Speedrunners (rerecord-friendly testing, deterministic replays)
- Modders (per-arena + per-asset overrides, .pdmod authoring)
- Homebrew users (Switch port via fgsfdsfgs lineage)
- Casual online / dedicated-server hosts (P2P up to 32 chrs / dedicated-server scaling up to compute)

### Core Workflows / Gameplay Loops (Inferred)
1. BYOR (Bring Your Own ROM) -> extract at first launch -> populate per-asset .pd* files in `data/<romid>/`
2. Combat Sim listen-host: select arena + mode + bots, start match, P2P with ENet
3. Forge / Grid: in-game level editor + social asset sharing (cross-session repeatability)
4. Dedicated server: PerfectDarkServer.exe hosts a session with a per-game plugin boundary
5. Mod authoring: pack `.pdmod` archive -> validate -> distribute -> load via catalog

### Upstream Lineage Observations

The catalog universality pivot (Steps 0-5, shipped 2026-04-30 through 2026-05-03) is PD2-native and substantially complete. The 13 asset kinds (pdwpn / pdhead / pdbody / pdarena / pdscenario / pdmesh / pdanim / pdsfx / pdvoice / pdsong / pdui / pdfont / pdlang) all have emitters + walkers. The previous `.pdbase` aggregate tier has been retired (Step 5).

The actionmap + input-layer + scene/state systems (Cohorts 1-4 shipped 2026-04-27, Cohorts 5-8 7-of-8 shipped 2026-05-12) are PD2-native and substantially replace the upstream port's raw SDL dispatch. Only the menu graph (s036-08) and a handful of follow-up CS UX cards remain.

The .pdwpn / .pdscenario / etc. emitters and the catalog manager API are clean PD2 inventions. The BYOR completion (2026-05-03) shifted weapon / head / body / arena / animation data to authoring tables in `port/src/*data_authored.c` driven by emitters at boot, with the engine reading exclusively through catalog accessors.

### Pillar Observations

- **Catalog (SOT)**: substantially complete after universality pivot. One remaining slug-convention drift (Finding C-1 below) and a semantic bug in the audio category seed (Finding C-2). Heavyweight `loader_pool` registrations + universal walker scaffold are in place.
- **Mod / native parity**: equal-footing principle holds at the catalog layer (.pdmod and base both register via the same walker). UI mod-manager surfaces mods as catalog entries. No mod-only branching found in this audit's scope.
- **Modding pipeline**: `.pdmod` is the canonical archive format; `modarchive.h` + `file_transfer.h` define authoring + distribution. Round-trip integrity is enforced by SHA-256 sidecars on per-asset .pd* writes (Pass A.4). Network distribution carries mandatory SHA-256 digest per NET_PROTOCOL_VER 46.
- **Grid**: social-share + voice ride on dedicated UDP sockets (port 27109 PDSHR + 27108 PDVOC), signed via Ed25519, NOT part of the ENet wire. Trust-surface implications partially audited; full Grid sweep recommended in a future dedicated pass.
- **Client online mode**: listen-host + dedicated paths exist; threat model documented at `context/designs/connectivity/hosting-modes-listen-vs-dedicated.md`. Dedicated server skips ROM/mod checks at CLC_AUTH (constraint enforced).
- **Dedicated server**: PD2-specific (despite the game-agnostic aspiration). The S486 release-scope decision deferred game-agnostic broker work; current pd-server is PD2 + ENet-protocol-specific. Boundary discipline (PD2 logic behind a plugin boundary) is NOT yet implemented; flagged as Architectural Concern A-2 below.

### Assumptions / Unknowns
- Runtime validation of fixes (B-307, B-311, c106) requires Mike's playtest; static review only.
- Grid Phase 2+ network code lives on separate UDP sockets not exercised in this audit.
- Switch homebrew (port-net target) was not validated for the new boot order.
- Dedicated-server plugin boundary work is deferred (S486); not audited in depth.

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### System & Workflow Validation Findings

#### Critical Issues

(none in this audit pass; the system meets its design intent in the audited surfaces. Critical historical issues either shipped fixes recently or are correctly deferred to dedicated multi-session lanes.)

#### High Priority Findings

**HF-1. GPU swarm pipeline is a structural stub (B-308/B-309/B-310 long-open).**
- Severity: High
- Confidence: Confirmed
- Location: `port/fast3d/swarm_gpu.cpp` (384 lines total); compare to `port/src/swarm_test.c` (1674 lines)
- Lineage: Authored in PD2 (S483 design; never completed)
- Pillar Impact: Scaling Ceiling + design-intent break (CPU/GPU benchmark parity)
- Intended Outcome: GPU swarm provides head-to-head CPU vs GPU compute path comparison at scales up to TESTSCEN_SWARM_MAX_COUNT (4096).
- Current Behavior: GPU pipeline only implements seek-player position update. NO bot AI (B-308), NO collision constraints (B-309: half-radius, perim-disable, scale-matched cylinder), NO spawn upgrades (B-310: volume-spawn, retry-on-fail, streaming-fill, overlap-fallback). Hard-capped at SWARM_GPU_MAX=256 (`port/fast3d/swarm_gpu.cpp:101`) even though the CPU side allows 4096.
- Gap / Failure Mode: GPU vs CPU "benchmark" is not apples-to-apples. CPU bots have full AI + tuned collision + diverse spawn; GPU bots are placeholders that just chase the player. Numbers are not comparable.
- Why It Matters: Defeats the entire purpose of having a GPU benchmark in the test scenarios. Reviewer can't reason about CPU vs GPU compute performance because the workloads differ.
- Risk If Not Fixed: GPU benchmark is misleading at best; a load-test result will not predict real-game GPU bot performance.
- Recommendation: Tackle in dedicated multi-session lane. Per `context/designs/in-flight/gpu-swarm-and-test-scenarios.md` Phase 2 (gated on Mike's approval of four design calls: GL compute path choice, chr-pool ceiling, hit-detection model, empty-map source). Estimate 3-5 sessions. Until then, do NOT use GPU swarm output as a perf indicator; document this in the swarm UI.
- Cross-System Changes Required: Yes (GPU compute kernel design, chr-pool interface, spawn API, collision shim).
- Blocks Intended Outcome?: Yes (benchmark parity not achieved).

**HF-2. SWARM_GPU_MAX is 256 but documented to match TESTSCEN_SWARM_MAX_COUNT (which is 4096).**
- Severity: High (drift between code and comment, behavior-affecting)
- Confidence: Confirmed
- Location: `port/fast3d/swarm_gpu.cpp:101` (`#define SWARM_GPU_MAX 256 /* must match TESTSCEN_SWARM_MAX_COUNT */`); `port/include/testscenarios.h:45` (`#define TESTSCEN_SWARM_MAX_COUNT 4096`)
- Lineage: Authored in PD2 (S483 design; stale comment after Slice expansion to 4096)
- Pillar Impact: None (test-scenario internal)
- Intended Outcome: GPU cap should track the CPU cap to allow apples-to-apples.
- Current Behavior: GPU is hard-capped at 256; comment claims they match. Ladder cycle to 512+ silently truncates to 256 on GPU side. User has no warning.
- Gap / Failure Mode: Misleading documentation + silent truncation.
- Why It Matters: Reviewer of the code is misled. Tester is misled. Already documented bugs (B-311 GPU FATAL at 128) are consistent with this surface being undersized + bugged.
- Risk If Not Fixed: Maintenance hazard. The next person sizing the GPU SSBO will trust the comment.
- Recommendation: At minimum, fix the comment to read `/* hard cap; CPU side TESTSCEN_SWARM_MAX_COUNT is 4096 but GPU pipeline scaling is gated on B-308/B-309/B-310 + B-311 fix */`. Long-term, parameterize via a runtime cap that can be raised as the GPU pipeline matures. (Code fix shipped in this audit's same-day follow-up commit; see Action Item 1 below.)
- Cross-System Changes Required: No (single-line comment, plus optional defensive cap warning).
- Blocks Intended Outcome?: Partially.

**HF-3. CPU swarm crash at 256-bot ladder step (B-307, OPEN since 2026-05-02).**
- Severity: High
- Confidence: Confirmed (Mike's playtest log + bugs.md entry; cause hypothesis-driven, not isolated)
- Location: `port/src/swarm_test.c` (cycler entry); `src/game/chr.c::chrTickAll`; `src/game/bot.c` (bot tick); `src/lib/collision.c` (broadphase) per the bugs.md candidate list
- Lineage: Inherited-from-port shape + Authored-in-PD2 swarm test extension
- Pillar Impact: Scaling Ceiling
- Intended Outcome: CPU swarm scales to TESTSCEN_SWARM_MAX_COUNT (4096) for benchmark fidelity.
- Current Behavior: Silent crash on first frame after spawning 256 bots. No FATAL emitted. Log ends mid-stream after the BENCHMARK summary.
- Gap / Failure Mode: One of four hypothesis classes per bugs.md B-307. Without a backtrace this can't be isolated by static analysis alone.
- Why It Matters: CPU side is supposed to be the well-trodden baseline.
- Risk If Not Fixed: Caps the benchmark utility at 128 alive (where Mike's most recent run stopped without crashing).
- Recommendation: Build a debug binary with SEH stack capture (`-g3 -fno-omit-frame-pointer`) + run the 256-cycle repro to capture the PC and frame. Decode via `addr2line -e PerfectDark.exe <PC>`. The crash class is likely (a) broadphase O(N^2) tick budget exhaustion, (b) static bot-AI scratch buffer sized to 128. Defensive workaround: cap the CPU ladder at 128 in `port/src/swarm_test.c::cycler_tick` until rooted.
- Cross-System Changes Required: Yes (debug build + likely chr/bot/collision touch).
- Blocks Intended Outcome?: Yes (caps benchmark).

#### Medium Priority Findings

**MF-1. Catalog seed-table slug convention drifts from .pdwpn walker convention (c053).**
- Severity: Medium
- Confidence: Confirmed
- Location: `port/src/assetcatalog_base_extended.c:75` (seed table, 41 entries, `base:falcon2_silencer` etc.); `port/src/weapondata_authored.c:5847` (`g_WeaponDataCatalogIds[]` 86 entries, `base:falcon2silencer` etc.); `port/src/loader_walker_weapon.c:34` (walker dedup via `assetCatalogResolve(id)`)
- Lineage: Authored in PD2 (universality pivot artifact)
- Pillar Impact: Catalog SOT
- Intended Outcome: One catalog row per weapon (86 total). Slug is the canonical identity.
- Current Behavior: Seed loop registers 41 MP-weapon entries with underscored slugs. Walker registers 86 entries with smooshed slugs. Both succeed (the dedup check sees different IDs). Catalog ends up with 127 rows (41 + 86) for 86 weapons, with the seed-registered duplicates carrying the legacy slug.
- Gap / Failure Mode: Duplicate rows. Lookups may hit either depending on which slug a caller uses. Saves persist whichever slug was written at save time.
- Why It Matters: Catalog SOT erosion. The "single source of truth" doesn't have a single name for the same thing.
- Risk If Not Fixed: Confusion in mod authoring, debugging, and any feature that iterates ASSET_WEAPON rows (saved-game weapon refs, scoreboard "kills with weapon X", catalog browser UI). Future save-format migration will need to handle both slug forms.
- Recommendation: Pick the canonical (recommend `g_WeaponDataCatalogIds[]` form since it's the walker's source of truth) + provide a save-format migration that rewrites old slugs at load time. Then retire the seed loop's slug column. Two-step migration; needs Mike's design call on which side wins.
- Cross-System Changes Required: Yes (save format migration + seed table change + audit any persisted-slug lookups).
- Blocks Intended Outcome?: Partially (Catalog SOT principle compromised).

**MF-2. Audio category seed treats russ-table as parallel-indexed to leaf-SFX (semantically wrong).**
- Severity: Medium
- Confidence: Confirmed
- Location: `port/src/assetcatalog_base_extended.c:641-643`
- Lineage: Authored in PD2 (Phase 3 Slice 10, 2026-05-02)
- Pillar Impact: Catalog SOT (audio category metadata)
- Intended Outcome: Catalog ASSET_AUDIO rows correctly classified as VOICE or SFX.
- Current Behavior: Code treats `g_AudioRussMappings[i]` as if `i` is a leaf SFX index. But the russ table is indexed by `confignum` (extracted from each sound's packed soundnum). The two index spaces are different per `propsnd.c::psGetTheoreticalVolPan` (line 1409-1412). For leaf SFX 0..443 the code happens to read a russ entry at the same position, but the semantic mapping is wrong; for leaf SFX 444..1544 the code always tags AUDIO_CAT_SFX (no russ lookup possible).
- Gap / Failure Mode: Catalog row audio category is incorrect for the FIRST 444 leaf SFX (some tagged voice when their underlying sound isn't voice, and vice versa). For 444-1544 it's all SFX, missing actual voice content. This mirrors the c106 bug in pdvoice extraction (also fixed via russ-table bitfield unpack).
- Why It Matters: Catalog ASSET_AUDIO category drives mod-manager UI filtering, catalog iteration for music/sfx/voice. Wrong metadata in the catalog SOT.
- Risk If Not Fixed: Mod manager shows wrong categories. Future "all voice files" iteration returns wrong set.
- Recommendation: Apply the same fix pattern as c106: walk the russ table, decode each entry's soundnum via `union soundnumhack` to extract the 11-bit `id`, and tag the corresponding leaf SFX index as voice in a pre-built bitset. Then use the bitset during the seed loop. Sister fix to the c106 pdvoice extraction fix already shipped.
- Cross-System Changes Required: No (single-file fix).
- Blocks Intended Outcome?: Partially (Catalog audio category SOT broken).

**MF-3. Skedar surface-normal locomotion Slices 4 + 5 still backlog (c3738).**
- Severity: Medium
- Confidence: Confirmed
- Location: `context/designs/in-flight/skedar-surface-normal-locomotion.md` Slices 4-5; `src/game/chraction.c` (surface-normal plumbing); `port/src/net/netmsg.c:3251` (v47 wire surface_up vec3)
- Lineage: Authored in PD2
- Pillar Impact: None (single-system, gameplay-only)
- Intended Outcome: Skedar bots locomote along surface normals with gravity-flip on edge transitions + aim projected through world space.
- Current Behavior: Slices 1-3 shipped (chr-struct plumbing, visual tilt, MP sync via v47 surface_up). Slices 4 (gravity-flip on edge transitions) and 5 (aim projection through world space) deferred pending Mike's playtest gate. Slice 6 (GPU parity in compute kernel) is gated on the broader GPU pipeline migration (HF-1 above).
- Gap / Failure Mode: Skedar visual + sync work but don't fully behave on edges; aim doesn't compensate for the surface tilt.
- Why It Matters: Skedar feel-of-play is incomplete. Net protocol bump to v47 is already paid; deferring 4-5 leaves the design half-shipped.
- Risk If Not Fixed: Skedar bots glitch at corners; Mike's playtest gate stalls.
- Recommendation: Schedule Mike's playtest of Slices 1-3 to validate the foundation. Then ship Slices 4 + 5 together (they share the surface-normal math). Estimate 1-2 sessions.
- Cross-System Changes Required: Yes (chr movement, aim system).
- Blocks Intended Outcome?: Partially (Skedar half-shipped).

#### Low Priority Findings

**LF-1. Menu graph migration (s036-08) has 35 raw call sites + tests do not pin the rate of decline.**
- Severity: Low
- Confidence: Confirmed
- Location: `port/fast3d/pdgui_menu_botsetup.cpp`, `pdgui_menu_cheats.cpp`, `pdgui_menu_controldiagram.cpp`, `pdgui_menu_mpadvanced.cpp`, `pdgui_menu_mpsettings.cpp`, `pdgui_menu_mpsetup.cpp`, `pdgui_menu_solomission.cpp`, `pdgui_menu_training.cpp` (largest at 11 raw calls)
- Lineage: Authored in PD2
- Pillar Impact: None (single-system)
- Intended Outcome: All `menuPushDialog(&...)` calls migrated to `menuGraphFirePushDialog(...)` for typed edges + dispatch logging.
- Current Behavior: ~35 raw call sites remain across 12 files. Migration pattern (L.16-L.39 in input-universality-and-transitions.md) is per-edge with test pin.
- Gap / Failure Mode: No regression -- code works -- but graph completeness is below 100%.
- Why It Matters: The menu graph is the layer that makes menu transitions auditable. Each unmigrated edge is invisible to graph-completeness tools.
- Risk If Not Fixed: Long-term maintenance friction. Future "what menu pushed what" debugging is harder for the unmigrated paths.
- Recommendation: Multi-session lane. 2-4 edges per session, prioritized by usage frequency (training.cpp has 11 calls -- low priority since training is a fixed flow; solomission.cpp 3 calls is higher-value since it's a hot path).
- Cross-System Changes Required: No (per-screen-local change).
- Blocks Intended Outcome?: No.

### Missing / Incomplete Features Blocking Success
- GPU bot AI + collision + spawn migration (B-308/B-309/B-310) is the big remaining structural gap for the benchmark.
- s036-08 menu graph completion is the only c036 subtask still open; not release-blocking.
- c081 (Queue Match) and c086 (per-element CS bindings) are NEW features tracked separately from the Cohort 5-8 completion.

### Positive Observations
- The universality pivot Steps 0-5 are clean: 13 asset kinds all have per-asset emitters + walkers, .pdbase aggregate retired without regression, BYOR completion ships authoring tables that the engine reads exclusively through catalog accessors.
- Wire protocol versioning is well-pinned: NET_PROTOCOL_VER 47 in source matches `g_TestExpectedNetProtocolVer = 47` in `tests/test_versions.cpp:46`. The silent-bump guard works.
- MPSETUP_VERSION 2 likewise pinned.
- The .pdmod archive format is clean (zip + mod.json + SHA-256 sidecars) and the file-transfer protocol carries mandatory digest at NET_PROTOCOL_VER 46.
- The actionmap chord-support extension shipped 2026-05-12 (sprc036) closes the F-key migration cleanly with VK_CHORD_SHIFT_F1 / VK_CHORD_SHIFT_F2 / VK_CHORD_ALT_RETURN.
- The c106 fix (russ-table bitfield unpack in pdvoice extraction) is a clean root-cause fix that adds a propagation note for future caller audits.

---

## 3) Code Quality Review

### Critical Issues

(none)

### High Priority Findings

**HQ-1. SWARM_GPU_MAX = 256 hardcoded; documented to match a 4096 cap.**
(Already detailed under HF-2; categorized here as code-quality issue too. Stale comment + magic number drift.)

### Medium Priority Findings

**MQ-1. MAX_PLAYERS = 8 fixed-count literal across multiple subsystems.**
- Severity: Medium
- Confidence: Confirmed
- Area: Code Quality + Scaling
- Location: `src/include/constants.h:44`, `port/include/pdgui_constants.h:20`; consumed by 60+ subsystems in port and src trees.
- Lineage: Inherited-from-port (port-net branch bumped from N64 era 4 to 8); not authored in PD2 originally but PD2 has not lifted it.
- Pillar Impact: Scaling Ceiling
- What We Found: `MAX_PLAYERS = 8` is a compile-time constant baked into chr arrays, scoreboard layouts, packet field widths, mp config arrays. PD2's design target is "P2P session size scales with host compute and bandwidth," but the cap is hard.
- Why It Matters: A 16-player PD2 session is gated by recompiling and re-auditing every callsite, not by host hardware.
- Risk If Not Fixed: Architecturally caps PD2's session-size ceiling below the per-pillar design intent.
- Recommendation: Long-term: replace with a runtime cap parameterized by host config (defaulting to 8 for compatibility). Audit each `[MAX_PLAYERS]` array for safe expansion. Multi-session work.
- Cross-System Changes Required: Yes (every subsystem with player-indexed arrays).

**MQ-2. ACTIONMAP_MAX_PLAYERS = 4 (splitscreen) hardcoded, separate from MAX_PLAYERS = 8.**
- Severity: Medium
- Confidence: Confirmed
- Area: Code Quality
- Location: `port/include/actionmap.h:45`
- Lineage: Inherited-from-port (4 splitscreen players is the upstream design); PD2 single-local-player constraint (per constraints.md) means players 1-3 are unused.
- Pillar Impact: None
- What We Found: Two parallel caps. `MAX_PLAYERS = 8` for network slots; `ACTIONMAP_MAX_PLAYERS = 4` for local input slots. The constraints.md says PD2 supports exactly one local human player (no split-screen). So the 4-cap is overprovisioned for PD2's actual target.
- Why It Matters: Wasted state (3x unused per-player arrays in actionmap). Not a bug, just code clarity.
- Risk If Not Fixed: None functionally; new contributors will think split-screen is supported (it isn't).
- Recommendation: Long-term: drop to 1 to match constraints.md "no local multiplayer". Or leave as 4 with a comment explicitly stating PD2 only uses player 0.

### Low Priority Findings

**LQ-1. `tools/kanban/state.json` schema v0.3.0 includes both `open_questions` and `pending_completion` -- additive but worth a migration audit if shape changes.**
- Severity: Low
- Confidence: Confirmed
- Area: Code Quality (tooling)
- Location: `tools/kanban/state.json:2-4`
- Lineage: Authored in PD2 (c121 + c123)
- Pillar Impact: None
- What We Found: kanban state.json schema_version=2 + semantic_version 0.3.0. The `x_extensibility_rule` covers additive evolution. Tooling that parses state.json should be audited to ensure it tolerates the new fields.
- Risk If Not Fixed: None immediately; potential future field-add break if a parser is strict.
- Recommendation: Spot-check `tools/parked_evaluator.py`, `tools/kanban_evaluator.py`, `tools/daily_flow/orchestrator.py` for new-field tolerance.

**LQ-2. TODO/FIXME density audit.**
- Severity: Low
- Confidence: Confirmed
- Area: Code Quality
- Location: 18+ TODOs across `port/src/`, `port/fast3d/`, `src/game/`; concentrated in `gfx_pc.cpp` (6), `mpsetups.c` (2), `mixer.c` (1), `pdsched.c` (1), `preprocess/misc.c` (3).
- Recommendation: Sweep each, classify as still-relevant or stale. Probably 5-10 minutes total. Not blocking.

### Positive Observations
- The `weapondata_authored.h` allowed-includers list is well-documented and enforced via grep-guard test.
- Wire protocol additive changes (v32, v33, v34, ..., v47) are documented in constraints.md with the bump rationale and consumer changes; the test_versions.cpp pin guards against silent bumps.
- The catalog manager API (`catalog_mgr_*`) is the only allowed accessor surface; "engine code MUST NOT include weapondata_authored.h" is enforced architecturally.
- The c106 fix includes a thorough comment block explaining the bitfield unpack rationale + the c106 ID + the link to `union soundnumhack`. Future readers can audit similar consumers using the same pattern.

---

## 4) Security, Trust & Privacy Review

### Critical Issues

(none surfaced in this audit's scope. Prior security work shipped: SEC-7 server-info 2-stage challenge, MASTER-C2 admin RCON hashing, MASTER-C3 cookie-gated reconnect, SEC-14 room password hashing, all per NET_PROTOCOL_VER 38.)

### High Priority Findings

(none surfaced in this audit's scope.)

### Medium Priority Findings

**MS-1. Stale `.git/HEAD.lock` from daily-flow orchestrator at 06:08 was still present at 09:40 (3+ hours).**
- Severity: Medium
- Confidence: Confirmed
- Area: Security + reliability
- Location: `.git/HEAD.lock`; daily-flow orchestrator at `tools/daily_flow/orchestrator.py`
- Lineage: Authored in PD2 (c118)
- Pillar Impact: Tooling
- What We Found: The daily-flow orchestrator (c118) runs at 06:00 daily and was observed to leave `.git/HEAD.lock` behind when its commits failed or it crashed. This blocks subsequent sessions from committing until the lock is removed manually.
- Why It Matters: Operational reliability. Each stale lock requires manual intervention.
- Risk If Not Fixed: Sessions can't commit when orchestrator hits a transient error; manual recovery required.
- Recommendation: Audit `tools/daily_flow/orchestrator.py` git error-handling. Wrap git operations in try/finally that removes stale locks on its own crash. Use `git --git-dir` flag if appropriate to avoid touching the live index.

### Low Priority Findings

(none in this scope)

### Positive Observations
- Connect codes hide raw IPs in UI per constraints.md (NO_RAW_IP_IN_UI).
- Persistent bans in `$S/bans.ini` are atomically written. `serverBansIsBanned(ip)` runs before any slot allocation in `netServerEvConnect`.
- Admin RCON token stored as SHA-256 with domain salt; constant-time compare.
- Room passwords hashed at create time (SEC-14, S393).
- Mandatory SHA-256 digest on SVC_DISTRIB_BEGIN since v46 (S507) prevents mod-distribution MITM.

---

## 5) Cross-Cutting Risks & Architecture Concerns

**A-1. Catalog seed-walker dedup is by slug, not by underlying weapon_id.** Two registrations with different slugs for the same WEAPON_* enum value coexist (see MF-1). The catalog dedup pattern in `loader_walker_weapon.c::s_register` checks `assetCatalogResolve(id) == NULL` -- if the same WEAPON_* arrives with a different slug, dedup is silent. Recommendation: extend the dedup to also check by weapon_id (or runtime_index) after slug lookup misses. Or align slugs (MF-1 recommendation).

**A-2. Dedicated-server game-agnosticism aspiration vs. PD2-specific reality.** Per S486 (2026-04-27), the dedicated-server-broker work was deferred so listen-host online could ship. The current `pd-server` binary IS PD2-specific (ENet protocol matched to PD2, no per-game plugin boundary). This is consistent with the deferred-scope decision but means the "game-agnostic dedicated server" pillar is aspirational, not implemented. Recommendation: when deferral lifts, the per-game plugin boundary needs explicit design + a session list / lobby / room layer that doesn't import PD2 types. Out of scope for current ship.

**A-3. GPU/CPU swarm parity gap (B-308/B-309/B-310/B-311) is multi-session lane.** Detailed in HF-1. The benchmark is unfit-for-purpose until the GPU side gets bot AI + collision constraints + spawn upgrades + 128+ scaling fix. Recommendation: dedicated 3-5 session lane; the design doc at `context/designs/in-flight/gpu-swarm-and-test-scenarios.md` Phase 2 outlines four design calls Mike needs to make before implementation.

**A-4. MAX_PLAYERS = 8 fixed-count literal (MQ-1) propagates across 60+ subsystems.** Long-term scaling concern. Recommendation: replace with runtime cap. Multi-session lane.

**A-5. Catalog audio category seed bug (MF-2) is a sister-pattern to the c106 pdvoice extraction bug.** Same russ-table bitfield mishandling. Recommendation: apply the c106 fix pattern to `assetcatalog_base_extended.c:641-643`. Small change.

---

## 6) Verification Limits (Static Analysis vs Runtime)

### Confirmed by Code Evidence
- HF-1 (GPU swarm stub state): verified by file-size + missing AI/collision/spawn code paths
- HF-2 (SWARM_GPU_MAX drift): verified by grep
- HF-3 (B-307 CPU 256 crash): confirmed-from-log via bugs.md entry
- MF-1 (slug convention drift): verified by walking seed table + walker file
- MF-2 (audio category semantic): verified by reading the registration loop + russ-table struct
- MQ-1, MQ-2 (player count caps): verified by grep on the #define
- MS-1 (stale git lock): observed live during this session

### Probable Issues Inferred from Patterns
- Skedar Slices 4-5 deferral (MF-3): inferred from design doc + kanban state
- Menu graph migration progress (LF-1): inferred from grep count + L.* history

### Unverified Risks Requiring Runtime
- B-307 root cause: needs debug build with SEH stack capture
- B-311 root cause: same; needs GPU compute kernel state inspection at the 128 cycle
- c106 fix runtime effect: needs Mike's playtest to confirm `EXTRACT.PDVOICE: written>0` after the fix lands
- MF-2 audio category seed bug: produces wrong catalog metadata; effect surfaces in mod-manager UI but no functional regression in gameplay

### What Would Be Needed to Fully Validate
- A debug build with `-g3 -fno-omit-frame-pointer` + SEH stack capture for the swarm crashes (B-307, B-311)
- A GPU compute kernel instrumentation pass to dump per-bot state at cycle boundaries
- A Grid sandbox endpoint for full Grid-trust validation
- A multi-tenant dedicated-server test fixture for cross-game isolation audit (when that pillar lands)
- A mod-pack fuzzer for the .pdmod loader's untrusted-input surface
- A 16+ player live test session to exercise MAX_PLAYERS hard-cap behaviors (would currently hit a compile-time wall, not a runtime wall)

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: 8/10 (catalog universality + input cohorts substantial; GPU benchmark is misleading; Skedar half-shipped)

**Code Quality Score**: 8/10 (well-pinned wire protocol, well-organized catalog manager, but lingering scaling-ceiling constants + slug drift)

**Security & Trust Score**: 9/10 (SEC-7/MASTER-C2/MASTER-C3/SEC-14 + mandatory distrib SHA-256 shipped; one operational reliability concern around git locks)

**Architectural Discipline Score**: 8/10 (Catalog SOT mostly holds; mod parity holds; dedicated-server game-agnosticism deferred; one slug-drift breakage)

### Total Findings by Severity
- Critical: 0
- High: 3
- Medium: 6
- Low: 3

### Total Findings by Confidence
- Confirmed: 11
- Probable: 1
- Unverified: 0

### Total Findings by Lineage
- Authored in PD2: 9
- Inherited from port/port-net: 2
- Inherited from n64decomp: 0
- Inherited from allinone: 0
- Unknown: 1

### Total Findings by Pillar Impact
- Catalog SOT: 2 (MF-1, MF-2)
- Mod-native Parity: 0
- Modding Pipeline: 0
- Grid Trust: 0
- Online-mode Boundary: 0
- Dedicated-server Boundary: 1 (A-2)
- Scaling Ceiling: 3 (HF-1, HF-2, MQ-1)

### Top Scaling Ceilings Identified
- `SWARM_GPU_MAX = 256` (port/fast3d/swarm_gpu.cpp:101): GPU swarm hard-capped at 256 while CPU side allows 4096. Bottleneck: GPU compute kernel state management (SSBO sized for 256; no streaming fill).
- `MAX_PLAYERS = 8` (src/include/constants.h:44 + port/include/pdgui_constants.h:20): network slot cap. Bottleneck: 60+ subsystems with `[MAX_PLAYERS]` arrays. Lifting requires compile-time audit across the tree.
- `MAX_BOTS = 32` (parameterized via PARTICIPANT_DEFAULT_CAPACITY): bot cap. Bottleneck: chr-pool + AI tick + collision broadphase (CPU O(N^2) sweep).

### Top Data-Layout Integrity Breaches (Phase 2.5)
- **None Critical** surfaced in this audit's scope. The Vec3 / matrix sweep is a separate audit pass. The c106 fix DID just resolve a russ-table bitfield unpack issue (semantic, not stride mismatch). The audit-flagged `assetcatalog_base_extended.c:641-643` has a similar semantic bug (MF-2) but not a stride-bytes issue.

### Top Catalog / Mod-parity Breakages
- MF-1: weapon slug convention drift (seed `base:falcon2_silencer` vs walker `base:falcon2silencer`) -> two catalog rows per weapon for 41 of 86 weapons.
- MF-2: audio category seed treats russ-table as parallel-indexed -> wrong AUDIO_CAT_VOICE / AUDIO_CAT_SFX tags on catalog rows.

### Top 5 Action Items (Highest Impact First)
1. **Fix SWARM_GPU_MAX drift comment** + log a warning when GPU swarm count > 64 (defensive; tracks B-311 workaround). Single-file change. ~30 min. (Can ship same-day with this audit.)
2. **Apply c106-pattern fix to assetcatalog_base_extended.c:641-643** (audio category seed semantic). Single-file change. ~30 min. (Sister fix to the already-shipped c106 pdvoice extraction fix.)
3. **Schedule B-307 + B-311 debug-build investigation session** with SEH stack capture. Estimate 1-2 sessions. Without this the benchmark caps at 64 GPU / 128 CPU.
4. **Schedule c053 slug-reconciliation slice** with Mike's design call (retire seed table OR align slug conventions). Estimate 1 session.
5. **Schedule GPU pipeline migration multi-session lane (B-308/B-309/B-310)** per the Phase 2 design doc. Estimate 3-5 sessions. Without this, GPU swarm benchmark is misleading.

### Must-Fix Before Public Release
- None of the findings here is release-blocking. The catalog SOT issues (MF-1, MF-2) are correctness concerns that need fixing before community modders rely on the catalog as the canonical reference. The scaling ceilings (MQ-1, HF-2) cap PD2 below its design intent but don't break the 8-player default ship.
- The B-307 + B-311 crashes (HF-3 + the GPU side of HF-1) are benchmark-only crashes; they don't affect the default game session. Document the safe-count workarounds in user-facing docs.

### Estimated Effort to Remediate Critical / High Issues
- HF-1 (GPU pipeline migration): 3-5 sessions, large surface area, design calls needed.
- HF-2 (SWARM_GPU_MAX comment + warning): 30 min, single-file, low risk.
- HF-3 (B-307 root cause): 1-2 sessions, debug build + likely small fix.
- Sum: 4-7 dedicated sessions to fully clear the High-priority queue.
