# Systemic Bug Patterns — Architectural Issue Catalog

> Recurring bug classes rooted in architectural mismatches between N64 assumptions and the PC port. These aren't individual bugs — they're *categories* that produce bugs wherever the pattern exists. Use this as an audit checklist.
>
> For one-off bugs, see [bugs.md](bugs.md).

---

## SP-1: MAX_PLAYERS Array Indexed by Bot mpindex

**Severity**: CRITICAL — ACCESS_VIOLATION crash
**Root cause**: Arrays sized `MAX_PLAYERS` (8) indexed with bot mpindex values (8–31). N64 had max ~12 entities; PC has up to 36 (MAX_MPCHRS).

**Key arrays affected**:
- `g_Menus[MAX_PLAYERS]`, `g_AmMenus[MAX_PLAYERS]` (bss.h)
- `g_MpSelectedPlayersForStats[MAX_PLAYERS]` (bss.h)
- `g_BgunAudioHandles[MAX_PLAYERS]`, `g_LaserSights[MAX_PLAYERS]` (bss.h)
- `g_PlayerExtCfg[MAX_PLAYERS]` (data.h)
- `g_FileLists[MAX_PLAYERS]` (data.h)

**Fix strategy**: Bounds-check and SKIP for bots — never alias via modulo.

**Files fixed (S15)**: ingame.c, mplayer.c, bondview.c, menutick.c
**Files fixed (P6-A / Tier 6)**: menu.c (`currentPlayerIsMenuOpenInSoloOrMp`, `func0f0f8120`), activemenu.c (`amOpen`, `amOpenPickTarget`, `amRender`)
**Files still needing audit**: player.c:5094 (and any remaining `g_Menus[` / `g_AmMenus[` index sites per grep)

**Search command**: `grep -rn 'g_MpPlayerNum\|% MAX_PLAYERS\|AVOID_UB' src/`

---

## SP-2: Modulo-Hack Bounds "Fix" (AVOID_UB)

**Severity**: HIGH — silent data corruption
**Root cause**: `% MAX_PLAYERS` used as "bounds clamp" silently aliases bot data onto wrong player. Bot index 11 → index 3 (`11 % 8 = 3`), corrupting player 3's state.

**Correct approach**: Bounds-check and skip, not modulo-alias.

**Files known affected**: bondview.c (fixed S15), mplayer.c:704/3754

**Search command**: `grep -rn 'AVOID_UB\|% MAX_PLAYERS\|% MAX_LOCAL' src/`

---

## SP-3: g_PlayerExtCfg Beyond MAX_LOCAL_PLAYERS

**Severity**: MEDIUM — reads garbage for remote/bot players
**Root cause**: `g_PlayerExtCfg[MAX_PLAYERS]` meaningful only for local players (indices 0–3). Code indexes with values 4–7.

**Correct approach**: Use `MAX_LOCAL_PLAYERS` (4) as bound, or `PLAYER_EXTCFG()` macro (masks with `& 3`).

**Files known**: mplayer.c:704/3754, bondwalk.c:908

---

## SP-8: prop->chr Accessed Without NULL Check

**Severity**: HIGH–CRITICAL — null pointer dereference crash
**Root cause**: Code checks `prop->type == PROPTYPE_CHR || PROPTYPE_PLAYER` before accessing `prop->chr`, but does NOT check that `chr` itself is non-NULL. For PROPTYPE_CHR, chr is almost always set at creation — but for PROPTYPE_PLAYER, chr can be NULL during stage load, match cleanup, or dedicated-server transitional states.

**When it happens**: Stage transitions (player prop exists before chr is bound), Co-op/Multiplayer late-join, dedicated server with no local player occupying slot 0.

**Pattern to audit**:
```c
if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
    prop->chr->anything   // DANGER: chr may be NULL for PROPTYPE_PLAYER
```

**Correct pattern**:
```c
if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) && prop->chr) {
    prop->chr->anything
```
or locally:
```c
struct chrdata *chr = prop->chr;
if (chr) { ... }
```

**Fixed (S65 — Audit 2 of 4)**: 7 critical instances in propobj.c, explosions.c, smoke.c. See `context/null-guard-audit-props.md`.
- `propobj.c:4455` — parent->chr->hidden in weapon drop (CRITICAL)
- `propobj.c:8392` — playerprop->chr->hidden in cctvTick (CRITICAL)
- `propobj.c:9334-9350` — hitchr in laser fence damage block (CRITICAL)
- `propobj.c:9462` — targetprop->chr in enemy autogun (HIGH)
- `explosions.c:1004` — chrDamageByExplosion in blast radius (HIGH)
- `explosions.c:379` — exproom OOB when rooms[0]=-1 (HIGH)
- `smoke.c:210` — rooms[0] OOB in roomGetFinalBrightnessForPlayer (HIGH)

**Remaining audit**: bot.c, botinv.c (Audit 3), mplayer/*.c (Audit 4).

**Search command**: `grep -n "->chr->\|->chr\." src/game/*.c | grep -v "if.*chr\|chr =\|chr=\|NULL"`

---

## SP-6: PLAYERCOUNT() Iteration with Sparse Player Slots

**Severity**: HIGH — null pointer dereference crash
**Root cause**: `PLAYERCOUNT()` counts non-null entries in `g_Vars.players[]` but loops iterate by sequential index. If slot 0 is NULL and slot 1 is non-null, PLAYERCOUNT()=1 and the loop runs for i=0, accessing `g_Vars.players[0]->anything` → crash.

**When it happens**: During stage load (`lvReset`), player objects aren't spawned yet. After a match that ends without clean teardown, some slots may be non-null while others are null from cleanup.

**Pattern to audit**:
```c
for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
    g_Vars.players[i]->anything  // DANGER: players[i] may be NULL
```

**Correct pattern**: Always null-check `g_Vars.players[i]` in any such loop:
```c
for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
    if (g_Vars.players[i] && g_Vars.players[i]->prop && ...) {
```

**Fixed (S63)**: `music.c:musicIsAnyPlayerInAmbientRoom` (B-36)
**Fixed (S64 — Audit 1 of 4)**:
- `lv.c:227` — `lvTick()` slayer rocket visionmode check (HIGH)
- `lv.c:482` — `lvReset()` player init loop during stage load (CRITICAL)
- `setup.c:1572` — `setupCreateProps()` invInit loop during stage load (CRITICAL)
- `camera.c:250,260,286,296` — 4 matrix lookup loops in cam0f0b53a8/cam0f0b53a4 (HIGH)
- `playermgr.c:700` — `playermgrGetPlayerNumByProp()` prop scan (HIGH)

**Remaining audit**: bondwalk.c/bondmove.c currentplayer early-return guards (Audit 2),
g_ChrSlots[] and g_MpAllChrPtrs[] (Audit 3), mplayer/*.c participant interactions (Audit 4).
See `context/null-guard-audit-players.md` for full findings.

**Search command**: `grep -rn "players\[i\]->\|players\[j\]->" src/game/`

---

## SP-4: Hardcoded Stage Index Domains

**Severity**: HIGH — OOB crashes with mod stages
**Root cause**: Three index domains entangled: stage table (87 entries), solo stages (21 entries), best times (21 entries). Mod stages get valid stage table indices (61–86) but are OOB for solo stages and best times.

**Status**: Phase 1 safety net complete (S23) — bounds checks at all known access points. Phase 2 (dynamic stage table) and Phase 3 (index domain separation with `soloStageGetIndex()`) designed, not coded.

**Guard added**: `if (stageindex >= NUM_SOLOSTAGES) return` in cheats.c, endscreen.c, training.c, mainmenu.c

**Constraint note**: See [constraints.md](constraints.md) — Index Domain Warning section.

---

## SP-5: Large Stack-Allocated Buffers

**Severity**: MEDIUM — stack overflow risk on PC threads
**Root cause**: N64 had single known stack. PC threads default to 1MB. Large buffers in deep call chains can overflow.

**Known dangerous buffers**: See [memory-modernization.md](memory-modernization.md) Phase M2.

---

## SP-7: Magic Number Allocation Sizes

**Severity**: LOW→MEDIUM — readability + silent breakage when constants change
**Root cause**: Bare hex/decimal literals for buffer sizes. When limits change (MAX_BOTS 8→24), hardcoded sizes don't update.

**Status**: Phase M1 of memory modernization — `memsizes.h` created with 30+ named constants. 8 high-priority files converted. ~100 ALIGN16 replacements remaining.

**Search command**: `grep -rn 'mempAlloc(0x\|mempAlloc([0-9]' src/`

---

## SP-9: File Truncation by Build/Edit Pipeline

**Severity**: HIGH — silent data loss; can corrupt context files, scripts, and source files
**Root cause**: The build and edit pipeline (PowerShell scripts, AI edit tools) silently truncates files under certain conditions. Content written beyond a threshold (encoding issue, buffer limit, or streaming flush failure) is discarded without error. The file is saved with fewer lines but the write is reported as successful.

**Known incidents**:
- `devtools/_dev-window.ps1`: 2311 → 2232 lines lost (encoding: em-dashes caused Windows-1252 truncation). Restored from commit `68c0b186`. **Mode A.**
- `port/src/actionmap.cpp`: 1624 → 1590 lines lost (AI output token limit mid-generation). Committed in `2ec0849e`, masked by auto-commit. Repaired by subsequent session. **Mode B.**
- ~19 files in an earlier session (pre-S140, large context): unrecovered forensically, likely Mode B.

**Two distinct failure modes** (see Deep Investigation section below):
- **Mode A** (encoding): PS script written through Windows-1252 code page; first non-ASCII byte silently terminates write. Mitigated by no-em-dash rule in `.ps1` files.
- **Mode B** (AI output limit): AI Edit/Write tool call truncated mid-character when session context is saturated; tool writes truncated content without error. **ONGOING RISK** -- safeguard catches this at commit time but not within-session.

**Safeguard** (**IMPLEMENTED S190**): Pre-commit `git diff HEAD --numstat` check in `devtools/build-headless.ps1`. Fires when net delta < -20 lines AND additions < 1/3 of deletions. Aborts auto-commit, names suspect files, prints restore command. Build continues from working copy. Tested: fires on -95 net (0+/95-); silent on -49 net with 41 additions (intentional rewrite -- correct no-fire).

**Audit checklist** (run after any AI-assisted edit session):
1. `git diff --stat` -- inspect line-count deltas before committing. Unexplained drops are truncation candidates.
2. `tail -5 <file>` after every significant edit. Truncated files end mid-word with no trailing newline.
3. In PowerShell scripts: use hyphens only (no em-dashes, no curly quotes). Save as UTF-8 with BOM if script contains non-ASCII.
4. Start fresh sessions before context grows large (SP-9 Mode B risk rises with session length).

**Fix strategy**: Restore from git (`git checkout <commit> -- <file>`), then re-apply the intended edit cleanly. Do NOT re-edit a truncated file -- the lost content may not be reconstructable from diff alone.

**Search command**: `git diff HEAD --numstat | awk '$2 > $1*3 && $2-$1 > 20 {print "SUSPECT:", $3, "(net", $1-$2, ")"}'`

### SP-9 Deep Investigation — 2026-04-10

**Conducted**: dreamy-goldberg worktree, investigation-only pass (no source changes).

#### Commit Catalog — Confirmed Truncation Incidents

| Commit | Timestamp | File | Before | After | Delta | Notes |
|--------|-----------|------|--------|-------|-------|-------|
| `2ec0849e` | 2026-04-10 00:09 | `port/src/actionmap.cpp` | 1624 | 1590 | -34 | **Committed truncated** — auto-commit masked the damage |
| Working copy only | 2026-04-10 ~00:xx | `devtools/_dev-window.ps1` | 2311 | 2232 | -79 | **Caught before commit** — restored from `68c0b186` |
| Pre-2026-04-05 | Unknown | ~19 files | Unknown | Unknown | Unknown | **Not forensically confirmed** — referenced in SP-9 summary and Apr-5 briefing; no detailed record survives |

actionmap.cpp was subsequently repaired by concurrent worktree sessions. At HEAD (`355326c1`) it is 1606 lines with a proper ending. Whether the difference from the original 1624 lines represents intentional rewriting or silent loss is unresolved.

#### Byte-Level Characterization

**actionmap.cpp (2ec0849e):**
- Truncated: 63,331 bytes. Original: 64,652 bytes. Lost: 1,321 bytes.
- `0xF763` — not a clean buffer boundary. Nearest: 32KB (30,563 away), 64KB (2,205 away).
- File ends mid-word: `    /* Populate default bindings (into IMC struct` (should be `structs, not yet active) */`).
- `\ No newline at end of file` confirmed — abrupt character-level termination, not line-level.
- Em-dash count at truncation point: zero. The em-dash line (`sysLogPrintf(LOG_NOTE, "ACTIONMAP: initialized — ...")`) appears 34 lines later in the lost tail. **Encoding is not the proximate cause for this incident.**
- File has CRLF line endings (0x0d0a confirmed at HEAD).

**_dev-window.ps1 (working copy only):**
- 79 lines lost. No UTF-8 BOM. File contained 4 UTF-8 em-dashes (U+2014, bytes 0xE2 0x80 0x94).
- Root cause confirmed in session log S190: "em dashes replaced with hyphens (Windows-1252 encoding issue)."
- When a PowerShell pipeline writes through Windows-1252, 0xE2 (first byte of UTF-8 em-dash) has no valid mapping, and the write terminates at that byte position. All content from that character onward is silently dropped.

#### Two Distinct Failure Modes

**Mode A — Encoding truncation (PowerShell/Windows-1252 pipeline)**
- Trigger: A file containing non-ASCII UTF-8 bytes (em-dashes, curly quotes) is written through a Windows PowerShell step that defaults to Windows-1252. The first non-representable byte silently terminates the write.
- Pattern: Cut at the first em-dash (or similar non-ASCII char). Position is deterministic for a given file.
- Files at risk: `.ps1` scripts; any file written by PowerShell without explicit `-Encoding UTF8`.
- Status: **Addressed for _dev-window.ps1** — em-dashes replaced with hyphens. Rule: no em-dashes or non-ASCII characters in `.ps1` files.

**Mode B — Response truncation (AI output token limit)**
- Trigger: A Claude Code session with a large accumulated context (long transcript, many prior tool calls) generates a Write or Edit tool call with a large `content` or `new_string` parameter. The model hits its output token limit mid-generation. The parameter value is truncated at that character. The tool writes the truncated content to disk without error.
- Pattern: File tail cut off mid-word, mid-statement, no newline at EOF. Truncation point is not aligned to any buffer boundary — it is wherever the model's output window closed.
- Confirmed evidence: The archive documents a `~19MB transcript size` causing 400 errors in the same approximate period. Both `_archive/session-briefing.md` and `_archive/briefing-2026-03-31.md` record "NEVER use Write tool — Write tool truncation has destroyed files before" as a standing rule.
- Even the Edit tool is vulnerable if `new_string` is large and the session context is saturated. The "Edit not Write" rule reduces risk but does not eliminate it.
- Status: **Not fully mitigated.** Rule exists but is not enforced mechanically.

#### Auto-Commit Masking Vector

The build pipeline auto-commit (`git add -A && git commit`) runs BEFORE each build with no pre-commit checks. This creates a window where:
1. A session leaves a file in a truncated state (tool call result was truncated).
2. The auto-commit runs and permanently records the truncated state in git history.
3. The next session sees the auto-committed version as HEAD and treats it as authoritative.
4. The original truncation is invisible via `git diff` — the damaged state IS the committed baseline.

This is exactly what happened with actionmap.cpp: truncation occurred between 68c0b186 (23:46) and 2ec0849e (00:09), and the auto-commit at 00:09 captured and committed the truncated file.

#### Pre-Commit Hooks

No active hooks in `.git/hooks/`. All files are `.sample` (inactive). Zero mechanical protection exists today.

#### Open Questions (require live repro or additional data)

1. **Is actionmap.cpp at HEAD (1606 lines) complete?** The HEAD version differs from the original 1624-line version. A diff of the tail of 68c0b186:port/src/actionmap.cpp vs HEAD would clarify whether the 18-line delta is intentional rewriting or residual data loss.
2. **What exactly were the 19 files?** No detailed forensic record survives. A git log sweep targeting pre-S140 commits (before 2026-04-04) looking for multi-file shrinkage patterns could identify the incident. Requires running the full shortstat sweep against the earlier date range.
3. **Can Mode B be confirmed via tool call logs?** If Claude Code writes tool call parameters to a log, the truncated `new_string` from the session that produced 2ec0849e would be visible there. Anthropic engineering would need to confirm whether such logs exist.

#### Recommended Safeguard (implementation sketch — not applied)

**Option 1 (recommended): pre-auto-commit line-count check in `build-headless.ps1`**

Insert immediately BEFORE the `git add -A && git commit` step:

```powershell
# SP-9 truncation guard
$numstatLines = (& git -C $ProjectDir diff HEAD --numstat 2>$null) -split "`n"
$flagged = @()
foreach ($line in $numstatLines) {
    if ($line -match '^(\d+)\s+(\d+)\s+(.+)$') {
        $added   = [int]$Matches[1]
        $deleted = [int]$Matches[2]
        $file    = $Matches[3].Trim()
        $net     = $added - $deleted
        # Flag: net shrinkage > 20 AND additions < 1/3 of deletions
        # (excludes legitimate large rewrites where both sides are large)
        if ($net -lt -20 -and $added -lt [Math]::Max(1, [Math]::Floor($deleted / 3))) {
            $flagged += "  $file  (net $net: +$added / -$deleted)"
        }
    }
}
if ($flagged.Count -gt 0) {
    Write-Host "[SP-9 GUARD] Auto-commit aborted — unexpected file shrinkage:" -ForegroundColor Red
    $flagged | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    Write-Host "Verify against HEAD. Restore: git checkout HEAD -- <file>" -ForegroundColor Cyan
    exit 1
}
```

Threshold `-20` catches all known incidents (34 and 79 lines lost) while ignoring normal edits. The `added < deleted/3` filter avoids false positives on large intentional rewrites. Adjust to `-10` for higher sensitivity.

**Option 2: post-merge verification step (add to CRITICAL-PROCEDURES.md)**

After any worktree merge touching source files, before committing:
```bash
git diff HEAD --numstat | awk '$2 > $1*3 && $2-$1 > 20 {print "SUSPECT SHRINKAGE:", $3, "(net", $1-$2, ")"}'
```
Visually inspect any flagged files before proceeding.

**What these safeguards do NOT catch**: A truncation that occurs within a single session before any intermediate commit, leaving no HEAD baseline to compare against. Defense for that case is procedural: the Edit-not-Write rule, session-length discipline (start fresh sessions before context gets large), and the existing `tail -5 <file>` sanity check after every significant edit.

---

## SP-12: Silent Process Death from Stack Canary SIGABRT

**Severity**: CRITICAL — crash with zero diagnostic output
**Root cause**: GCC's `-fstack-protector-strong` detects stack buffer overflows via canary values. When a canary is smashed, `__stack_chk_fail()` calls `abort()` which raises SIGABRT. On Windows/MinGW, the SIGABRT handler runs on the same (potentially corrupt) stack. If the handler's stack frame pushes the stack beyond its committed limit, the handler itself faults — and since VEH doesn't cover signals, the process dies with no output.

**Pattern**: Process terminates silently (no log, no crash dialog, no VEH output) after sustained heavy computation (many bots, deep AI chains, complex collision). Heartbeat timer stops firing. No core dump.

**Instances**: B-126 (silent crash ~8min MP), B-113 (was 2MB stack, expanded to 8MB but class not eliminated).

**Fix (S234 FIX-A)**:
1. SIGABRT handler rewritten with static buffers only, direct file writes, `_exit(3)` — no `sysLogPrintf` or `sysFatalError` which use too much stack
2. `SetUnhandledExceptionFilter` (UEF) + `AddVectoredExceptionHandler` (VEH) already installed as fallbacks
3. Stack watermark tracking per-chr in `chraTick` (`g_ChrTickMaxStackUsed`) — identifies which AI codepath consumes the most stack
4. Stack depth cap in `chraTick` — skips chr when remaining stack < 512KB

**Search command**: `grep -rn 'stack_chk_fail\|SIGABRT\|signal.*SIGABRT\|g_ChrTickMaxStackUsed\|g_ChrTickStackBase' port/src/crash.c src/game/chr.c src/game/chraction.c`

---

## SP-13: `manifestClear` Before `mainChangeToStage` During MP Teardown

**Severity**: CRITICAL — `0xc0000005` access violation
**Root cause**: `mainChangeToStage()` treats any `STAGE_IS_GAMEPLAY()` target as a
match-load, and if `g_ClientManifest.num_entries > 0` it takes the
`manifestMPTransition()` branch and attempts to diff the old manifest against
whatever is loading for the target stage. When the old manifest is a torn-down
MP match manifest and the target is lobby/room/CI-training, the diff walks dead
asset references and faults.

**When it happens**: Any code path that changes stage out of an MP match
without clearing the manifest first. Specifically, paths that end an MP match
and return to the hub — pause-menu "End Game", endscreen "Exit", and the
disconnect-and-clean-up flow.

**Correct pattern**:
```c
manifestClear(&g_ClientManifest);
mainChangeToStage(STAGE_CITRAINING);   // or any non-match target
```

**Known sites (ALL FIXED 2026-04-13)**:

| Site | Fix | Commit |
|------|-----|--------|
| `pdgui_bridge.c:799` — `pdguiEndscreenExitToMainMenu()` | F-0.4 | S233 |
| `netmsg.c:1419` — `netmsgSvcStageEndRead()` (SVC_STAGE_END path) | L1-1 | S235 |
| `net.c::netDisconnect` (before `mainChangeToStage(STAGE_CITRAINING)`) | Bug A | `d37e9677` |

**Audit checklist (before adding any new callsite)**:

1. Does this code call `mainChangeToStage()`?
2. Is `g_ClientManifest` potentially non-empty when this runs (i.e. was an MP
   match active in this flow)?
3. If both yes: insert `manifestClear(&g_ClientManifest);` immediately before
   the `mainChangeToStage()` call.
4. Reference F-0.4 / L1-1 / Bug A in the comment for cross-pattern
   traceability.

**Search command to audit new callsites**:
```
grep -n 'mainChangeToStage' port/src/net/*.c port/fast3d/*.cpp src/game/*.c
```
Then verify each non-menu-system hit is either already preceded by
`manifestClear` OR demonstrably cannot run with a populated `g_ClientManifest`.

**Active constraint**: [constraints.md](constraints.md) — "manifestClear before
mainChangeToStage during MP teardown".

---

## SP-14: Room-bound server state must be cleaned up on room teardown

**Severity**: HIGH — silent protocol desync, stuck UI, ghost stage loads
**Root cause**: Server-side state keyed on `room_id` (`s_ReadyGate`,
`g_NetMatchRoomId`, mod playlist state, match config snapshot, …) must
be torn down when the room it belongs to is destroyed. `roomDestroy()`
only clears the room slot itself — it does not notify downstream
subsystems. Any subsystem holding room-keyed state must hook into
`roomLeave()` / `roomDestroy()` (or run a "room still exists?" check on
each tick).

**When it happens**: host leaves room, last player leaves, host kicks
all, server drops client. Anything that empties a room.

**Canonical fix pattern** (Bug B, 2026-04-14):
1. Add a public "on client left" and "on room destroyed" wrapper that
   reads the subsystem's state and invokes its abort / clear path.
2. Call both wrappers from `roomLeave()` in `port/src/room.c` — once
   the leaver has been removed, and again (defensively) inside the
   `client_count == 0` branch before `roomDestroy()`.
3. Add a per-tick defensive check inside the subsystem's tick function
   that calls `roomGetById(room_id) == NULL` → abort.

**Known sites audited**:
- `s_ReadyGate` (Bug B, 2026-04-14) — fixed via `netReadyGateAbortForRoom`
  + `netReadyGateOnClientLeft` in `netmsg.c`.

**Audit command** (run when adding new room-keyed state):
```
grep -nE 'room_id|matchRoomId|g_NetMatchRoomId' port/src/net/*.c port/src/*.c
```
Then verify every static/global holding `room_id` has a hook in
`roomLeave()` or a defensive per-tick cleanup.

**Active constraint**: [constraints.md](constraints.md) — "Ready gate lifetime
is bound to its room's lifetime".

---

## SP-15: GL texture size and cache lifetime — upload-time caps, cache-scoped teardown

**Severity**: MED — GPU memory leak, possible driver-level allocation failure on large skin/theme assets
**Root cause**: Any code path that uploads a user-controlled image to a
`GLuint` texture must both (a) bound the upload dimensions against
`GL_MAX_TEXTURE_SIZE` (or enforce a compile-time cap below the
lowest-common-denominator driver limit), and (b) free the texture via
`glDeleteTextures` on the exact lifetime boundary of the cache that
owns it. A rescan that rebuilds the cache without deleting the old
GL names leaks textures on every reload.

**When it happens**:
- Theme / chrome style rescans (`pdguiThemeRescanChromeStyles`,
  `pdguiThemeRescanMods`) invoked on mod apply, dev hot-reload, and
  startup.
- Skin editor preview uploads (`s_DownrezPreview`) on character or
  quantization-level change.
- Any mod-supplied PNG loaded via `pdguiLoadTextureFromPNG` or similar.

**Canonical fix pattern** (S-6 / S-5, 2026-04-16):
1. Before uploading, clamp `w`/`h` to
   `min(GL_MAX_TEXTURE_SIZE_RUNTIME_CAP, compile_time_cap)`. If the
   asset exceeds the cap, downscale or reject with a log warning — do
   not pass the raw dimensions to `glTexImage2D`.
2. Before clearing a cache (e.g. `s_chromeStylesClear`), iterate the
   cache and `glDeleteTextures(1, &tex)` for each mod-owned entry.
   **Skip base entries owned by a different init path** (e.g.
   `"base:ui_chrome_frame"` owned by `pdguiThemeLateInit`). Erase from
   the cache map after deletion.
3. For reusable scratch buffers (downrez preview, quantization
   staging), track owner dimensions (`W`, `H`) alongside the pointer
   and `realloc` whenever the target dimensions change — do not reuse
   a buffer sized for a previous character.

**Known sites audited**:
- `pdguiThemeRescanChromeStyles()` (S-6, S294) — fixed via
  `s_chromeStylesFreeModTextures()` in `pdgui_theme.cpp`.
- `s_DownrezPreview` in `pdgui_skin_editor.cpp` (S-5, S294) — fixed
  via tracked `s_DownrezPreviewW`/`H` and realloc-on-resize.
- `modmgrApplyChanges()` (S-8, S294) — now also calls
  `pdguiThemeRescanChromeStyles()` so the cache is torn down on mod
  apply (previously only `pdguiThemeRescanMods()` was called).

**Audit command** (run when adding new GL texture uploads or caches):
```
grep -nE 'glTexImage2D|glGenTextures|s_ThemeTexCache|GL_MAX_TEXTURE_SIZE' port/fast3d/*.cpp port/src/*.c
```
Then verify every texture creation site has a matching
`glDeleteTextures` on its cache's teardown path, and every upload has
a dimension cap check.

---

## How to Use

- Before starting any work that touches arrays, memory allocation, or stage indexing, scan this file for relevant patterns.
- When fixing a one-off bug, check if it's an instance of a pattern here. If so, do a propagation check (§3.6) on all files listed under that pattern.
- When discovering a new pattern class, add it here with severity, root cause, known sites, and search command.
