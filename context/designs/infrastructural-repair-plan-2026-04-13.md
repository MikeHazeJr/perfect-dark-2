# Infrastructural Repair Plan -- 2026-04-13

> **Created**: 2026-04-13, Opus 4.6 1M analysis session
> **Scope**: Every open bug in bugs.md, grouped by architectural class, with dependency-ordered infrastructure fixes
> **Deliverable**: This document. No code changes in this session.
> **Cross-references**: [bugs.md](../bugs.md), [systemic-bugs.md](../systemic-bugs.md), [infrastructure.md](../infrastructure.md), [constraints.md](../constraints.md)

---

## 1. Open Bug Inventory

### 1.1 Open Bugs by Severity

| ID | Sev | Summary | Status |
|----|-----|---------|--------|
| **B-126** | HIGH | Silent crash ~8min into MP (7 bots + 1 player) | INVESTIGATING |
| **B-112** | HIGH | Chr pointer (rbx) corruption in `chraTick` during 31-bot matches | PARTIAL (entry guard + tracker) |
| **B-118** | MED | Crash during CI intro cutscene loop -- 56 models missed by SP manifest | OPEN |
| **B-19** | MED | Bot spawn stacking on Skedar Ruins -- all bots spawn at same pad | PARTIAL FIX |
| **B-99** | MED | Updater downloads zip but extraction may fail | OPEN |
| **B-18** | MED | Pink sky on Skedar Ruins | OPEN (may overlap B-128 fix) |
| **B-95** | LOW | Update notification banner persists during gameplay | OPEN |
| **B-97** | LOW | Special Assignments / Challenges not separated from mission list | OPEN |
| **B-60** | LOW | Stray 'g'+'s' visible behind Video/Audio tabs in Settings | OPEN |
| **B-72** | LOW | SVC_LOBBY_STATE sends raw stagenum u8 | ALREADY FIXED (see below) |
| **B-21** | MED | Menu double-press / hierarchy issues | LIKELY FIXED (S124) |

Additionally, `tasks-current.md` lists untracked open items:
- Menu opacity stacking (BG gets more opaque after repeated open/close)
- Some maps don't spawn enemies (navmesh/pad coverage gaps)
- Killfeed only shows player kills (bot kills missing)
- JUMP_LANDING log spam every frame during pause

### 1.2 Bugs to Close or Reclassify

**B-72 (SVC_LOBBY_STATE raw stagenum)**: **ALREADY FIXED.** Confirmed by direct code inspection of `netmsg.c:4698-4737`. The v27 refactor changed `netmsgSvcLobbyStateWrite` to accept `const char *stage_id` and send it via `netbufWriteStr`. The read side resolves through `assetCatalogResolve(arena_id)`. No raw stagenum on the wire. **Action: close B-72 in bugs.md.**

**B-21 (Menu double-press)**: **LIKELY FIXED by multiple layers.** S124 Phase E full-stack dedup added `push_tick` keyboard suppression with 100ms grace. S208 added a 150ms `MAIN_MENU_CLOSE_GRACE_MS` guard. The Esc→ImGui event forwarding path now has belt-and-braces protection. **Action: close B-21 if next playtest confirms.**

---

## 2. Bug Classification by Architectural Class

### Class A: Stack/Memory Corruption (CRITICAL)

**Members**: B-126, B-112

**Shared Root Cause Hypothesis**: Both bugs manifest in 31-bot matches during the chr tick loop. B-126 (silent crash after ~8 minutes) and B-112 (chr pointer corruption in chraTick) are likely the *same underlying defect* at different severity thresholds. The crash path is:

1. **Deep call chain stack pressure**: `lvTick()` -> `chrTick()` -> `chraTick()` is a single call chain. `chraTick` is a ~14,000-line monolith that dispatches hundreds of AI commands. Each bot iteration pushes this chain. With 31 bots, the per-frame stack depth multiplies.

2. **Stack canary smash via `-fstack-protector-strong`**: If a buffer in the chr/AI subsystem overflows, the canary check fires `__stack_chk_fail()` which calls `abort()` -> `SIGABRT`. The current `crashSigabrtHandler` was added in S191 to catch this, but SIGABRT delivery on Windows/MinGW is unreliable -- if the signal handler itself touches corrupt stack, it double-faults and the process dies silently (no VEH, no log).

3. **Heap corruption from stale chr pointers**: The participant system (B-12) runs parallel to chrslots. If a bot dies and respawns while chraTick is mid-iteration on another bot's AI that references the dying bot's chr pointer, the pointer can become stale. The entry guard at chraTick's top catches NULL/freed chr, but a *reused* chr allocation at the same address would pass the guard with corrupted field values.

**Why VEH doesn't fire**: VEH is a Windows-only mechanism. SIGABRT from `__stack_chk_fail` bypasses VEH entirely (it's a signal, not a structured exception). The process terminates via `abort()` before any exception handler runs. The 8MB stack (B-113 fix) extended the budget but didn't eliminate the fundamental issue: `chraTick` is unbounded in stack depth per AI command dispatch.

**Proposed Infrastructure Fix (FIX-A)**: *Chr Tick Isolation and Lifetime Hardening*

1. **Stack depth cap in chraTick**: Add a recursion/depth counter to the AI command dispatch loop inside chraTick. If a single chr's AI evaluation exceeds N call frames (measured via a thread-local counter or `__builtin_frame_address` comparison), break out and mark the chr for deferred processing. This prevents any single bot from exhausting the stack.

2. **Chr lifetime token**: Add a `generation` counter to chr data. Each allocation increments the generation. All external chr pointers carry `(chr_ptr, generation)` pairs. Before dereferencing, compare generations. If mismatched, the pointer is stale -- skip or re-resolve. This eliminates the reused-address class of B-112.

3. **Crash handler hardening**: The SIGABRT handler must use only static buffers and write directly to a pre-opened file descriptor (not through the logging system which uses stack space). On Windows/MinGW, also install a `SetUnhandledExceptionFilter` as a last-resort fallback that writes to a memory-mapped crashlog.

4. **Diagnostic**: Add a per-chr stack watermark (save `__builtin_frame_address(0)` at chraTick entry, compare to thread stack base). Log any chr whose frame usage exceeds 50% of remaining stack. This identifies the specific AI codepath causing pressure.

**Dependency**: None. Can be implemented immediately.

---

### Class B: Manifest/Asset Completeness (HIGH)

**Members**: B-118, (and the untracked "some maps don't spawn enemies")

**Shared Root Cause Hypothesis**: The manifest system (`netmanifest.c`) pre-scans stages to build a list of required assets before loading. The pre-scan is incomplete:

1. **B-118 (CI intro cutscene crash)**: The Carrington Institute hub has scripted intro sequences that dynamically spawn ~56 character models via setup commands and cinematics scripts. The manifest pre-scan walks the stage's setup data for statically-placed props but does NOT walk the cinematics script language or the AI command list (which can reference `chrSpawn` with arbitrary model IDs). When the stage loads and the cutscene fires a `chrSpawn` for a model not in the manifest, the file loader returns NULL -> crash.

2. **"Maps don't spawn enemies"**: Related -- some stages have enemies placed via AI scripts rather than static setup. If the navmesh/pad data for those enemies was previously loaded only as a side-effect of the manifest having the right entries, removing the AIDROP filter (B-110 fix) may have changed which pads are considered valid.

**Proposed Infrastructure Fix (FIX-B)**: *Manifest Completeness Overhaul*

1. **Deep manifest scanner**: Extend `manifestBuild()` to walk:
   - Setup file prop references (already done)
   - Cinematics scripts (`.cam` data): parse for `chrSpawn` / model load commands
   - AI script command lists: parse for `AICMD_SPAWN_CHR` and similar
   - Objective data: parse for chr/model references in objective conditions
   This is the "right" fix -- the manifest becomes exhaustive.

2. **Graceful fallback for missing models**: As a safety net, when `fileLoadToNew()` returns NULL for a model referenced by a spawned chr, don't crash. Instead: (a) substitute a placeholder model (the "null body" already exists as a concept in the catalog), (b) log a WARNING with the missing asset ID, (c) mark the chr as `CHRFLAG_RENDER_DISABLED` so it's invisible but doesn't crash the renderer. This converts hard crashes into visual glitches that can be diagnosed.

3. **Manifest audit mode**: Add a `--manifest-audit` CLI flag that loads every SP stage, builds the manifest, then loads the stage and logs any asset request that wasn't in the manifest. This is a development-time tool for catching gaps.

**Dependency**: FIX-B.2 (graceful fallback) is independent and should ship first. FIX-B.1 (deep scanner) requires understanding the cinematics script format. FIX-B.3 is a dev tool.

---

### Class C: Rendering State Leakage (MEDIUM)

**Members**: B-18, (and the untracked "menu opacity stacking")

**Shared Root Cause Hypothesis**: The GBI display list interpreter (`gfx_pc.cpp`) carries render state across frames and across draw calls. State set by one draw pass (alpha blend mode, render mode, fog parameters, color registers) leaks into subsequent passes if not explicitly reset.

1. **B-18 (Pink sky on Skedar Ruins)**: B-128 fixed the general case (sky inheriting previous frame's blend state via missing `gDPSetRenderMode`). B-18 may be the same mechanism on Skedar Ruins specifically -- the stage may set unusual color register values (via `gDPSetEnvColor` or `gDPSetPrimColor` with pink/magenta tints for the Skedar atmosphere effects) that leak into the sky render. OR Skedar Ruins may have a sky-specific color palette entry that isn't being loaded correctly from the ROM data.

2. **Menu opacity stacking**: Each menu open/close cycle adds a haze overlay (`pdgui_haze_overlay`) but doesn't fully clear it. The alpha accumulates across cycles. This is a rendering state issue in the ImGui layer, not the GBI layer, but the pattern is the same: state from one render pass leaking into the next.

**Proposed Infrastructure Fix (FIX-C)**: *Render State Reset Points*

1. **GBI frame-start state reset**: At the top of each frame's display list (in `lvRender` or the equivalent), emit a canonical set of `gDPSetRenderMode`, `gDPSetEnvColor`, `gDPSetPrimColor`, `gDPSetFogColor` commands that establish a known-good baseline. The sky already does this for render mode after B-128; extend it to all state registers. This is cheap (4-5 GBI commands) and eliminates the entire class of cross-frame state leakage.

2. **B-18 specific investigation**: Before applying the generic fix, playtest Skedar Ruins after B-128 is confirmed working. If the pink sky persists, the cause is stage-specific (palette data, sky type, or env color) rather than state leakage. Check the stage's sky type and whether it uses a custom sky color palette vs the default.

3. **Haze overlay lifecycle**: The haze overlay alpha should reset to zero on menu pop, not decrement. Track the overlay state as a boolean (visible/hidden) rather than an accumulated alpha value. On menu push: set alpha to target. On menu pop: set alpha to zero. No accumulation across cycles.

**Dependency**: FIX-C.2 depends on playtest results. FIX-C.1 and FIX-C.3 are independent.

---

### Class D: HUD/UI Context Gating (LOW-MEDIUM)

**Members**: B-95, B-60, (and the untracked "killfeed only shows player kills")

**Shared Root Cause Hypothesis**: UI elements that should be context-sensitive (only shown in certain game states) are rendered unconditionally.

1. **B-95 (Update banner persists during gameplay)**: The update notification overlay (`pdgui_menu_update.cpp`) renders every frame when `g_UpdateAvailable` is set. It has no check for whether the player is in a mission, in a menu, or at the title screen. It should suppress during active gameplay (when `g_CtxGameplay` is the top input context or when `lvGetPaused() == false`).

2. **B-60 (Stray 'g'+'s' behind tabs)**: These are likely remnant `ImGui::Text()` calls or ImGui ID string suffixes ("##g", "##s") that are rendering visible characters instead of being consumed as ID disambiguators. Could also be remnant shortcut key labels from an older UI layout where "G" for Graphics and "S" for Sound were tab accelerators. The fix is to find and remove the orphaned text draw calls.

3. **Killfeed missing bot kills**: The killfeed renderer likely filters on `PROPTYPE_PLAYER` and skips `PROPTYPE_CHR` (bots). This is a content filter bug, not an architecture bug.

**Proposed Infrastructure Fix (FIX-D)**: *UI Context Awareness*

1. **Global UI state query**: Create a `pdguiGetUIContext()` function that returns an enum: `UICTX_TITLE`, `UICTX_MENU`, `UICTX_GAMEPLAY`, `UICTX_CUTSCENE`, `UICTX_LOADING`, `UICTX_ENDSCREEN`. All overlay renderers (update banner, killfeed, chat, HUD messages) check this before rendering. This centralizes the "should I draw?" decision.

2. **B-60 fix**: Direct -- find and fix the orphaned text draws. Likely a 1-line fix.

3. **Killfeed fix**: Direct -- extend killfeed to include chr (bot) kills. This is a data issue, not architectural.

**Dependency**: FIX-D.1 is independent. FIX-D.2 and FIX-D.3 are trivial point fixes.

---

### Class E: Spawn/Pad Selection (MEDIUM)

**Members**: B-19

**Root Cause Hypothesis**: The AIDROP filter was removed in S149 (B-110 fix). This broadened the set of valid spawn pads on all stages. On most stages this works fine. On Skedar Ruins specifically, the pad data may have a clustering issue: most pads may be geographically close (designed for linear SP gameplay, not arena MP), and the anti-repeat logic (S125 F.1) isn't sufficient to spread spawns when the pad pool is spatially non-diverse.

**Proposed Infrastructure Fix (FIX-E)**: *Spatial Spawn Distribution*

1. **Pad scoring by spatial diversity**: Instead of simple anti-repeat (don't reuse the last N pads), score candidate pads by distance from all currently-alive players and recently-used spawn points. Weight by: (a) minimum distance to any alive player (prefer far), (b) minimum distance to any spawn used in last 30 seconds (prefer far), (c) navigability (pad must have valid floor geometry). Select the highest-scoring pad.

2. **Per-stage spawn pad audit**: Some stages may need manually curated spawn pad lists for MP. If a stage has fewer than 8 well-distributed pads, flag it as "sparse spawn" and use a jitter fallback (random offset within the pad's room volume).

3. **Skedar Ruins specific**: Before the generic fix, run the `--manifest-audit` mode (FIX-B.3) on Skedar Ruins to check how many pads it has and where they are. If the pad count is < 8, the fix is curated pad data, not algorithm changes.

**Dependency**: FIX-E.1 is independent. FIX-E.3 benefits from FIX-B.3 tooling.

---

### Class F: Update System Reliability (MEDIUM)

**Members**: B-99

**Root Cause Hypothesis**: The updater (`port/src/updater.c`) uses GitHub Releases API to find new versions. S199 diagnosed that the download/parse can fail due to:

1. **GitHub API rate limiting**: Unauthenticated GitHub API requests are limited to 60/hour. When rate-limited, the API returns a 403 with a JSON error object. The updater's JSON parser expects a releases array, chokes on the error object, and returns a parse failure. The user then sees "extraction failed" but the actual failure was upstream at the API query step.

2. **Zip extraction path resolution**: The downloaded zip may use relative paths that resolve incorrectly depending on CWD vs. the game's data directory (same class of path issue fixed in S222 for audio mod import). `fopen()` with a relative path fails when CWD doesn't match.

3. **Temp file permissions on Windows**: MSYS2/MinGW `tmpfile()` or manual temp paths under `%TEMP%` can fail when multiple processes (including antivirus) hold locks on the temp directory.

**Proposed Infrastructure Fix (FIX-F)**: *Updater Robustness*

1. **Rate limit detection**: Check HTTP response code before parsing JSON. If 403, log "GitHub API rate limit exceeded" and show a user-facing message with a retry timer. Don't attempt to parse the error JSON as a releases array.

2. **Explicit error reporting**: The updater should distinguish and report: (a) network failure (can't reach GitHub), (b) rate limit (403), (c) parse failure (malformed JSON), (d) download failure (incomplete/corrupt zip), (e) extraction failure (path/permission). Each gets a distinct user-facing message.

3. **Path resolution**: Use `fsFullPath("$S/...")` for all temp file and extraction paths, matching the pattern established in S222 for audio import.

4. **Extraction verification**: After extracting, verify the extracted binary exists and has the expected SHA-256 hash before attempting the self-replace. If verification fails, don't delete the old binary.

**Dependency**: None. Can be implemented immediately.

---

### Class G: Mission Flow / Category (LOW)

**Members**: B-97

**Root Cause Hypothesis**: The solo mission select UI (`pdgui_menu_solomission.cpp`) iterates `g_SoloStages[]` linearly and presents all entries in a single flat list. Perfect Dark's original game distinguishes:
- Missions 1-9 (main campaign)
- Special Assignments (Challenge 1-4)  
- Deep Sea / Crash Site (unlockable bonus missions)

The solo stage data has category information (via stage type or index range), but the ImGui mission list renderer doesn't use it to create section headers or visual grouping.

**Proposed Infrastructure Fix (FIX-G)**: *Mission Category Headers*

1. **Stage category enum**: Add `STAGE_CAT_MISSION`, `STAGE_CAT_SPECIAL`, `STAGE_CAT_BONUS` classification. Derive from solo stage index ranges (0-8 = missions, 9-12 = special assignments, etc.) or from stage data flags if available.

2. **Section headers in mission list**: Render the mission list with collapsible section headers: "Missions", "Special Assignments", "Bonus". Each section contains only its category's stages.

3. **Optional: completion indicators per section**: Show "3/9 completed" next to each section header.

**Dependency**: None. Self-contained UI change.

---

## 3. Dependency Order and Execution Plan

### Phase 1: Safety Critical (blocks all testing)

```
FIX-A  Chr Tick Isolation + Lifetime Hardening
  |
  +-- A.1  Stack depth cap in chraTick
  +-- A.2  Chr lifetime generation tokens
  +-- A.3  Crash handler hardening (static buffers, MMIO log)
  +-- A.4  Per-chr stack watermark diagnostic
```

**Rationale**: B-126/B-112 make 31-bot matches unstable. Every other bug requires stable multiplayer to test. Fix this first.

**Estimated scope**: chr.c, chraction.c, crash.c, participant.c/h. ~200 lines of new infrastructure.

### Phase 2: Asset Safety Net (unblocks SP testing)

```
FIX-B  Manifest Completeness
  |
  +-- B.2  Graceful fallback for missing models (FIRST -- immediate safety)
  +-- B.1  Deep manifest scanner (cinema + AI scripts)
  +-- B.3  Manifest audit CLI tool
```

**Rationale**: B-118 blocks CI intro cutscene testing. FIX-B.2 (graceful fallback) is cheap and converts crashes to visual glitches, letting SP testing proceed while FIX-B.1 is developed.

**Estimated scope**: netmanifest.c, setup.c, fileload (model loader). B.2 ~50 lines. B.1 ~200 lines. B.3 ~100 lines.

### Phase 3: Rendering Correctness (parallel with Phase 2)

```
FIX-C  Render State Reset Points
  |
  +-- C.1  GBI frame-start state reset
  +-- C.3  Haze overlay lifecycle fix
  +-- C.2  B-18 Skedar Ruins investigation (after C.1, needs playtest)
```

**Can run in parallel** with Phase 2 -- different subsystems, no file conflicts.

**Estimated scope**: sky.c or lv.c (+5 lines for state reset), pdgui overlay code (~20 lines).

### Phase 4: UI Polish (parallel with Phase 2-3)

```
FIX-D  UI Context Awareness
  |
  +-- D.1  pdguiGetUIContext() centralized query
  +-- D.2  B-60 stray text fix (trivial)
  +-- D.3  Killfeed bot kills (trivial)

FIX-G  Mission Category Headers
  +-- G.1  Stage category enum
  +-- G.2  Section headers in mission list
```

**Can run in parallel** with Phases 2-3 -- UI-only changes.

**Estimated scope**: pdgui_menu_update.cpp, pdgui_menu_mainmenu.cpp, pdgui_menu_solomission.cpp. ~100 lines total.

### Phase 5: Update System (independent)

```
FIX-F  Updater Robustness
  |
  +-- F.1  Rate limit detection + error classification
  +-- F.2  Path resolution via fsFullPath
  +-- F.3  Extraction verification (SHA-256 check)
```

**Independent** -- can run at any time. No dependencies on other fixes.

**Estimated scope**: updater.c. ~100 lines.

### Phase 6: Spawn Quality (after Phase 1)

```
FIX-E  Spatial Spawn Distribution
  |
  +-- E.3  Skedar Ruins pad audit (diagnostic)
  +-- E.1  Pad scoring by spatial diversity
  +-- E.2  Per-stage spawn pad validation
```

**Depends on Phase 1**: Need stable 31-bot matches to test spawn distribution.

**Estimated scope**: navspawn.c or playerreset.c. ~150 lines.

### Parallel Execution Map

```
Week 1:  [FIX-A.1-A.4] + [FIX-C.1, C.3] + [FIX-D.2, D.3]
Week 2:  [FIX-B.2]      + [FIX-D.1, G.1-G.2] + [FIX-F.1-F.3]
Week 3:  [FIX-B.1, B.3] + [FIX-E.3, E.1]
Week 4:  [FIX-C.2 (playtest)] + [FIX-E.2] + close B-72, B-21
```

---

## 4. New Systemic Patterns for systemic-bugs.md

### SP-10: GBI Render State Cross-Frame Leakage

**Severity**: MEDIUM -- visual corruption, can appear as crashes if render mode causes invalid texture access
**Root cause**: The GBI display list interpreter carries all state registers (`other_mode_l`, env color, prim color, fog) across frames. If a frame ends with a non-default state (e.g., `G_RM_AA_XLU_SURF` from a translucent effect), the next frame's first draw call inherits it.

**Pattern**: Visual corruption that is intermittent and depends on what was on screen the previous frame. Effects like sun flares, teleport beams, explosion particles, or translucent props can set blend modes that corrupt the sky, HUD, or subsequent geometry.

**Instances**: B-128 (sky tearing -- FIXED), B-18 (pink sky on Skedar Ruins -- OPEN, likely same class).

**Fix**: Emit a canonical state reset at the start of each frame's display list. Not per-draw-call (too expensive), but once per frame to establish a known-good baseline.

**Search command**: `grep -rn 'gDPSetRenderMode\|gDPSetEnvColor\|gDPSetPrimColor' src/game/sky.c src/game/lv.c`

---

### SP-11: Manifest Pre-Scan Incompleteness

**Severity**: HIGH -- hard crash when loading stages with dynamically-spawned characters
**Root cause**: The manifest pre-scan (`manifestBuild`) walks static setup data for prop references but does not walk cinematics scripts, AI command lists, or objective conditions. Any character model spawned dynamically (by script, by AI command, or by objective trigger) is absent from the manifest. When the stage loads and requests the model, `fileLoadToNew()` returns NULL -> crash.

**Pattern**: Crashes that occur only on specific SP stages with cutscenes (CI, Pelagic II intro, Air Force One intro) or with scripted NPC spawns. The crash is a NULL model pointer dereference in the rendering or collision system.

**Instances**: B-118 (CI intro cutscene -- 56 models), potentially other SP stages not yet tested.

**Fix**: Two-layer: (1) deep manifest scanner that walks all asset reference sources, (2) graceful fallback for missing models (placeholder + warning log).

**Search command**: `grep -rn 'fileLoadToNew\|manifestAdd\|manifestEnsure' port/src/net/netmanifest.c src/game/setup.c`

---

### SP-12: Silent Process Death from Stack Canary SIGABRT

**Severity**: CRITICAL -- crash with zero diagnostic output
**Root cause**: GCC's `-fstack-protector-strong` detects stack buffer overflows via canary values. When a canary is smashed, `__stack_chk_fail()` calls `abort()` which raises SIGABRT. On Windows/MinGW, the SIGABRT handler runs on the same (potentially corrupt) stack. If the handler's stack frame pushes the stack beyond its committed limit, the handler itself faults -- and since VEH doesn't cover signals, the process dies with no output.

**Pattern**: Process terminates silently (no log, no crash dialog, no VEH output) after sustained heavy computation (many bots, deep AI chains, complex collision). Heartbeat timer stops firing. No core dump.

**Instances**: B-126 (silent crash ~8min MP), B-113 (was 2MB stack, expanded to 8MB but class not eliminated).

**Fix**: (1) SIGABRT handler must use static buffers only and write to pre-opened fd, (2) Install `SetUnhandledExceptionFilter` as last-resort, (3) Add stack watermark tracking per thread, (4) Cap stack depth in known-deep call chains (chraTick AI dispatch).

**Search command**: `grep -rn 'stack_chk_fail\|SIGABRT\|signal.*SIGABRT' port/src/crash.c`

---

## 5. Architectural Smells (Untracked)

These are not bugs yet, but are architectural issues discovered during this audit that could produce bugs:

### SMELL-1: chraTick Monolith (14,000+ lines)

**File**: `src/game/chraction.c`
**Issue**: `chraTick` is a single function of ~14,000 lines with a massive switch/case for AI commands. It's un-auditable, un-testable, and the single largest contributor to stack pressure. Any bug in any AI command is hidden in this monolith.
**Risk**: Stack overflow (B-126), undetectable corruption (B-112), future AI bugs.
**Suggested action**: Long-term refactor into command handler table (`aiCmdHandlers[cmd_type]`). Not urgent but would make the entire Class A bug category impossible.

### SMELL-2: Participant System Parallel Run (chrslots + participants)

**File**: `port/src/participant.c`, `src/game/mplayer.c`
**Issue**: The participant system (B-12) runs *parallel* to the legacy chrslots bitmask. Both systems track the same entities. Phase 3 (remove chrslots) has been "NEXT" since S47b. While both systems coexist, any inconsistency between them is a potential corruption source for B-112/B-126.
**Risk**: Desync between chrslots and participant pool -> stale pointers, double-free, wrong player slot.
**Suggested action**: Prioritize B-12 Phase 3 (chrslots removal) in the v0.2.0 cycle. Every session that adds bot/player code increases the divergence risk.

### SMELL-3: SP-8 Audit Incomplete (prop->chr NULL checks)

**File**: Multiple files in `src/game/`
**Issue**: Systemic bug SP-8 documents the `prop->chr` NULL dereference pattern. Audit 2 of 4 was completed in S65. Audits 3 and 4 (bot.c, botinv.c, mplayer/*.c) are still pending. The same pattern exists wherever `prop->type == PROPTYPE_PLAYER` is checked without a `prop->chr != NULL` guard.
**Risk**: Crashes during stage transitions, dedicated server operation, or late-join.
**Suggested action**: Complete Audits 3 and 4 as part of Phase 1 work (they may overlap with B-126/B-112 root cause).

### SMELL-4: State Transition Gaps in ImGui Menus

**File**: `context/designs/state-transition-audit.md` (GAP-1 through GAP-4)
**Issue**: The state-transition audit found that ImGui menus don't always replicate the legacy menu system's state changes. GAP-1 (endscreen missing explicit context pop), GAP-3 (game over screen context push without pop) are saved by stage transition resets but are fragile. Adding any exit path that doesn't trigger a stage transition would leak input contexts.
**Risk**: Stale input contexts, stuck mouse capture, gameplay input blocked after menu close.
**Suggested action**: Add explicit `inputCtxPop(&g_CtxImGuiMenu)` to every ImGui menu close handler, matching the push in the appear handler. Don't rely on stage transition to clean up.

### SMELL-5: Legacy menuPush/menuPop Stack Still Active

**File**: `src/game/menumgr.c`
**Issue**: The legacy dialog stack (`menuPush`/`menuPop`) is retained as "plumbing" for ImGui menus (ImGui renderers push dialog defs, hotswap intercepts). But the legacy stack has its own state management (pause mode, player control flags, bg transition) that fires alongside ImGui's state management. Constraints.md says the legacy stack "will be stripped entirely" but this hasn't happened. The dual stack creates ambiguity about which system owns state transitions.
**Risk**: Double state changes, missed state restores, the entire class of bugs documented in state-transition-audit.md.
**Suggested action**: Plan the legacy stack removal for post-v0.1.0. Strip `menuPush`/`menuPop` entirely. ImGui menus should own their own state via the input context system.

### SMELL-6: D7 Discord Rich Presence Still Listed

**File**: `context/infrastructure.md`
**Issue**: D7 (Discord Rich Presence) is listed as "Planned" in infrastructure.md. This feature has been abandoned as an ethical decision -- Discord's data practices and the requirement to embed a proprietary SDK in an open-source project are unacceptable.
**Action**: Mark D7 as ABANDONED in infrastructure.md. Reason: ethical decision (proprietary SDK requirement, data practices concerns). Not a technical blocker.

---

## 6. Infrastructure.md Updates

When this plan is approved and work begins, update infrastructure.md:

1. **D7**: Change status from "Planned" to "ABANDONED (ethical decision -- proprietary SDK, data practices concerns, 2026-04-13)"
2. **B-126/B-112**: Create a new tracked phase "D-CHR: Chr Lifecycle Hardening" covering FIX-A
3. **Manifest**: Create "D-MANIFEST: Asset Manifest Completeness" covering FIX-B
4. **SP-10, SP-11, SP-12**: Add to systemic-bugs.md

---

## 7. Summary: Total Open Bug Count and Resolution Path

| Bug | Class | Fix | Phase | Effort |
|-----|-------|-----|-------|--------|
| B-126 | A (Stack/Memory) | FIX-A | 1 | HIGH |
| B-112 | A (Stack/Memory) | FIX-A | 1 | HIGH |
| B-118 | B (Manifest) | FIX-B | 2 | MEDIUM |
| B-18 | C (Render State) | FIX-C | 3 | LOW-MED |
| B-19 | E (Spawn) | FIX-E | 6 | MEDIUM |
| B-99 | F (Updater) | FIX-F | 5 | MEDIUM |
| B-95 | D (UI Context) | FIX-D | 4 | LOW |
| B-97 | G (Mission Flow) | FIX-G | 4 | LOW |
| B-60 | D (UI Context) | FIX-D.2 | 4 | TRIVIAL |
| B-72 | -- | ALREADY FIXED | -- | CLOSE |
| B-21 | -- | LIKELY FIXED | -- | VERIFY+CLOSE |
| Menu opacity | C (Render State) | FIX-C.3 | 3 | LOW |
| No enemies | B (Manifest) | FIX-B | 2 | LOW-MED |
| Killfeed bots | D (UI Context) | FIX-D.3 | 4 | TRIVIAL |
| Log spam | -- | Direct fix | Any | TRIVIAL |

**Total open**: 11 tracked bugs + 4 untracked items = 15 issues.
**Already fixed (close)**: 2 (B-72, B-21).
**Infrastructure fixes needed**: 7 (FIX-A through FIX-G).
**Trivial point fixes**: 4 (B-60, killfeed, log spam, B-21 verification).
**New systemic patterns**: 3 (SP-10, SP-11, SP-12).
**Architectural smells**: 6 (SMELL-1 through SMELL-6).

---

*End of document. This plan covers all bugs open as of 2026-04-13. No code was changed.*
