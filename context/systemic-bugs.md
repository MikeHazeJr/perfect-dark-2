# Systemic Bug Patterns — Architectural Issue Catalog

> Recurring bug classes rooted in architectural mismatches between N64 assumptions and the PC port. These aren't individual bugs — they're *categories* that produce bugs wherever the pattern exists. Use this as an audit checklist.
>
> For one-off bugs, see [bugs.md](bugs.md).

---

## SP-30: Public examples can pass archive structure while using runtime-ignored fields

**Severity**: HIGH — creator edits appear valid but cannot affect production

**Pattern:** A typed archive can be structurally valid and catalog-visible yet
teach creators keys that the production parser never reads. B-971's
`.pdtheme` `accent`/`chrome` example was the concrete instance.

**2026-08-08 propagation:** B-974 through B-978 confirmed the same class in
weapon, character, voice, and especially effect archives. Presence in a
descriptor, catalog row, generic runtime binding, compiled-but-flattened graph,
or conformance allow-list is not utilization. Conformance itself can also
contradict runtime, as with timeline-only `.pdeffect` archives that validate but
cannot activate.

**Audit:** Cross-check every generated/example source key against its production
parser and downstream consumer, not only descriptor/manifest conformance.

```powershell
python tools/build_typed_pdxxx_examples.py
python tools/asset_archive_conformance.py --root examples/modding/typed-pdxxx-basic --require-all-families
rg -n "parse_.*json|strcmp\\(key" port src
```

**Rule:** Example generation tests must pin parser-recognized keys for any
structured file. A family remains `partial` until every advertised field has a
production consumer or an explicit validation error.

---

## SP-29: User-facing control surfaces drift from the action map

**Severity**: HIGH — controls remain in code but become undiscoverable,
mislabelled, or impossible to rebind

**Root cause**: the action enum, default IMC bindings, Settings binding rows,
runtime consumers, and glyph hints are maintained as independent lists. Adding
an action to one list does not require its appearance in the others. Fixed
keyboard/controller strings also go stale after rebinding or device changes.

**Fixed instances (B-967/B-968, 2026-07-30)**:

- Settings now surfaces every one of the 117 user-bindable actions. The four
  derived continuous axis channels are explicitly represented by the global
  stick-layout, sensitivity, inversion, and deadzone controls.
- Forge E/Q bindings live in the Forge IMC, not the always-active Gameplay IMC.
- Direct vehicle use has a default mapping and production activation consumer.
- Player-facing menu hints resolve current action glyphs instead of fixed
  Xbox/default-key labels.

**Correct approach**:

- Every new `InputAction` must be classified as user-bindable or a derived
  channel in focused coverage.
- A user-bindable action requires an owning IMC row, persistence, default or
  explicit unbound state, and a production consumer.
- Hints use `pdguiGlyphGetActionLabel` or `pdguiDrawActionPrompt`; prose must
  not claim a fixed binding.
- Duplicate physical bindings must be checked under the action map's
  first-winner rule and the relevant active IMC stack.

**Propagation search**:

```text
rg -n "ACTION_[A-Z0-9_]+|addBind\\(|s_BindableActions|Enter/Space|B/Esc|D-Pad" port src tests
```

---

## SP-28: Generic menu fallback omits interactive item types

**Severity**: HIGH — a reachable menu can render chrome while silently losing
its actual controls and content

**Root cause**: the generic typed-dialog renderer handled only a subset of the
legacy item enum and displayed a placeholder/default OK for unhandled items.
Dedicated later registrations hid the defect on common routes, leaving any
unregistered or mod-adjacent dialog incomplete.

**Fixed instance (B-966, 2026-07-30)**: LIST, CAROUSEL, PLAYERSTATS, and RANKING
now have functional generic renderers over the live handler/data ABI. Dedicated
renderers remain last-registration-wins for richer screens.

**Correct approach**:

- Exhaustively switch every interactive `MENUITEMTYPE_*` supported by active
  dialog definitions.
- Unknown interactive types fail visibly in diagnostics but must not be
  represented as successful/complete UX.
- Preserve handler ABI widths; never reinterpret 32-bit fields as pointers on
  this 64-bit port.
- Keep a focused test that compares supported enum types with generic or
  dedicated production renderers.

**Propagation search**:

```text
rg -n "MENUITEMTYPE_|DEFERRED|placeholder|default OK|unk04u32" src/game port/fast3d tests
```

---

## SP-27: Canonical Catalog IDs Are Shadowed by Writable Legacy Fields

**Severity**: HIGH — creator-selected identity can change or disappear after a
save, network, or configuration round trip

**Root cause**: a format writes both the canonical catalog ID and a deprecated
numeric mirror, then a reader applies both in file order. The later legacy
field silently wins despite the nominal migration policy. Keeping both writable
also creates two hand-maintained representations that can drift.

**Fixed instance (B-964, 2026-07-30)**:
- MP setup JSON now writes only `weapon_ids`, preferring the canonical
  `g_MatchConfig` strings so catalog-only weapons survive.
- The deprecated numeric `weapons` array is accepted only as read-only
  migration input when `weapon_ids` is absent, regardless of field order.
- Unresolved canonical IDs, invalid legacy numbers, and failed bot-profile
  reconstruction reject the whole load instead of reporting partial success.

**Correct approach**:
- Writers emit only canonical catalog IDs.
- Readers accept legacy numeric fields for migration only when the canonical
  field is absent.
- A nonempty unresolved canonical ID or failed required record makes the whole
  load fail; it must never be skipped or replaced silently.

**Propagation search**:
```text
rg -n "\".*_ids?\"|legacy|fallback|catalog.*id|weapons\\[" port/src src/game
```
For each persisted or wire object, identify one authoritative identity field,
verify deprecated fields are read-only migration inputs, and prove field-order
independence.

---

## SP-26: Catalog-Extensible Selectors Collapse Back to Native Indices

**Severity**: CRITICAL — advertised mod content appears in a selector but cannot be selected, persisted, or reconstructed

**Root cause**: a selector correctly iterates catalog entries, then converts the chosen entry back to a legacy `runtime_index`, `mp_index`, table offset, enum, or type/difficulty tuple as its authoritative result. Base rows appear to work because their catalog rows mirror native tables. Creator-added rows have no native slot, normally carry `-1`, or collide with an existing tuple, so their catalog identity is lost immediately after selection.

**Fixed instance (B-961, 2026-07-30)**:
- The legacy selector now returns and stores the full profile catalog ID; the modern Room UI exposes the same profile domain.
- `mpbotconfig`, `matchslot`, JSON and binary setup formats, manifests, and both match-start wire directions retain that ID.
- Type, difficulty, and default body are derived only from the active readable public binding. v0-v2 binary setup blocks migrate to v3 by deriving base IDs from their legacy traits.
- Focused static/contract coverage and the all-target build are green. A custom-profile MKB/controller/save/network/gameplay receipt remains the validation gate.

**Correct approach**:
- Keep the full catalog ID as authoritative selector result and runtime state.
- Persist and transmit the catalog ID through save/wire/manifest boundaries.
- Resolve any legacy numeric value only at the final private gameplay boundary.
- A custom catalog row must have an end-to-end test that selects it, survives save/reload and host/client reconstruction, and changes production behavior.

**Propagation search**:
```text
rg -n "IterateUnlockedByType|runtime_index|mp_index|result_.*num|GETOPTIONTEXT|MENUOP_SET" src port
```
For each catalog-backed selector, compare the identity used for display with the identity stored by `MENUOP_SET`; they must be the same catalog ID domain.

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
**Files still needing audit**: NONE as of 2026-07-04 (backlog wave 3). `player.c:5094` was refactored to a per-player action map (`actionHeld(slayerplayeridx, ...)`), not a MAX_PLAYERS array index; the `g_Menus[` / `g_AmMenus[` index sites are bounds-checked (see the `menu.c:3820` SP-1/SP-2 comment). **Audit CLEARED.**

**Search command**: `grep -rn 'g_MpPlayerNum\|% MAX_PLAYERS\|AVOID_UB' src/`

---

## SP-2: Modulo-Hack Bounds "Fix" (AVOID_UB)

**Severity**: HIGH — silent data corruption
**Root cause**: `% MAX_PLAYERS` used as "bounds clamp" silently aliases bot data onto wrong player. Bot index 11 → index 3 (`11 % 8 = 3`), corrupting player 3's state.

**Correct approach**: Bounds-check and skip, not modulo-alias.

**Files known affected**: bondview.c (fixed S15), mplayer.c:704/3754. 2026-07-04 (wave 3): a repo-wide grep for `% MAX_PLAYERS` / `% MAX_LOCAL` found NO live modulo-alias bounds hacks (only benign `#ifdef AVOID_UB` decomp array-size blocks). **Audit CLEARED.**

**Search command**: `grep -rn 'AVOID_UB\|% MAX_PLAYERS\|% MAX_LOCAL' src/`

---

## SP-3: g_PlayerExtCfg Beyond MAX_LOCAL_PLAYERS

**Severity**: MEDIUM — reads garbage for remote/bot players
**Root cause**: `g_PlayerExtCfg[MAX_PLAYERS]` meaningful only for local players (indices 0–3). Code indexes with values 4–7.

**Correct approach**: Use `MAX_LOCAL_PLAYERS` (4) as bound, or `PLAYER_EXTCFG()` macro (masks with `& 3`).

**Files known**: mplayer.c:704/3754, bondwalk.c:908. 2026-07-04 (wave 3): all live `g_PlayerExtCfg` indexers verified -- `player.c` fov getters (7714/7730) and `mplayer.c` extcontrols (766/4153) already bound by `MAX_LOCAL_PLAYERS`; the `bondwalk.c` jump-height read was hardened from `MAX_PLAYERS` to `MAX_LOCAL_PLAYERS`. **Audit CLEARED.**

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

**Remaining audit**: mplayer/*.c (Audit 4) -- spot-checked clean 2026-07-04. **2026-07-04 (wave 3): bot.c + botinv.c CLEARED** -- 7 unguarded `chrGetTargetProp(chr)->chr` / `target->chr` dereferences fixed (botinv.c chrsinsight x2 @540/557, chrdistances @890, crossbow blur @589, tranq blur @603; bot.c `botGetTargetsWeaponNum` @1276). `chrGetTargetProp` returns a valid non-NULL prop when `target != -1`, so only `->chr` (a player prop whose chr is unbound during load/late-join/cleanup) needed guarding; `botGetWeaponNum(NULL)` genuinely crashed at `chr->aibot`. Guards are behaviour-neutral when chr is non-NULL (the common case).

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
g_ChrSlots[] and g_MpAllChrPtrs[] (Audit 3). **2026-07-04 (wave 3): mplayer/*.c CLEARED**
-- participant loops over `g_Vars.players[i]` / `g_MpAllChrPtrs[i]` are all null-guarded
(mpspawn_orchestrate.c:106/396, scenarios.c) or bounded by `g_MpNumChrs` (live-slot
contract, entries non-NULL); no unguarded sparse-loop deref found.
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

---

## SP-11: Movement Collision Reading Render-Owned Buffers

**Severity**: CRITICAL — ACCESS_VIOLATION crash, nondeterministic movement collision, mod/Grid incompatibility
**Root cause**: Gameplay movement/capsule code reads transient render-owned buffers (`model->matrices`, display-list hit helpers, frame-local graphics state) as if they were authoritative collision data. Render matrices are only valid after render/update setup for the current frame and can be missing, stale, or indexed differently than collision needs. The B-339 Defection Perfect crash was the visible failure: Stage 2 movement collision called `propobj.c::func0f0849dc()`, which dereferenced `model->matrices[mtxindex]` during early NPC ground acquisition.

**Correct approach**:
- Terrain/rendered room geometry belongs in `meshcollision`'s static world mesh at stage load.
- Movement-solid props own local-space `prop->colmesh` data.
- Dynamic prop queries build transforms from stable object state (`prop->pos`, `defaultobj.realrot`, and explicit door/lift state helpers as needed), not render frame matrices.
- Pickups, zones, water/fog/holograms, Forge pass-through, and Forge projectile-only objects must not contribute movement collision.
- Weapon/projectile/object-hit paths may keep using legacy model hit helpers, with guards, because they are not movement ownership.

**Guardrail landed 2026-05-18 (B-339)**:
- `src/lib/capsule.c` static guard requires no `func0f0849dc`, no `capsuleRenderedPropRayCast`, and no direct `model->matrices` use.
- `meshcollision.c` owns `meshWorldAddRenderedRoom`, `meshRayCastWorld`, `meshRayCastDynamicProps`, `meshAttachModelToProp`, and `meshBuildPropTransform`.

**Search command**: `rg -n "func0f0849dc|model->matrices|gfxAllocate|g_Gfx|modelFindNodeMtx" src/lib src/game port/src port/fast3d`

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

## SP-16: Schema / emitter / parser field-name drift in per-asset envelopes

**Severity**: HIGH — silent zero-init on every record, downstream "loads but doesn't work" symptoms
**Root cause**: The per-asset envelope pipeline has three independently-edited surfaces -- the schema doc
([context/designs/catalog/universality-pivot-schemas.md](designs/catalog/universality-pivot-schemas.md)),
the per-kind emitter (`port/src/romextract_pd<kind>.c`), and the per-kind loader parser
(`port/src/loader_pool.c::parse<Kind>`). When a field is renamed in the schema (e.g. `filenum` -> `mesh`,
`handfilenum` -> `hand`) and one surface is updated but not the others, the runtime symptoms are silent:

- Emitter writes the new name -> `.pd<kind>` files have the new key.
- Parser still reads the old name -> the field's destination in `<kind>_data_t` stays `0` from memset.
- Catalog manager accessor returns the record with the zero field -> downstream code reads 0 and either
  silently falls back to a placeholder path or stalls indefinitely (e.g. master loader waiting for a
  filenum=0 file load that can never complete).

No error is logged unless an explicit "required field missing" guard exists (which `parseHead`/`parseBody`
do not have; missing keys are skip-and-continue).

**When it happens**:
- Schema lock-down rename without an accompanying parser update (B-328, 2026-05-15: `mesh`/`hand` renamed
  in the schema doc + emitter on 2026-05-03 BYOR completion; parser kept reading `filenum`/`handfilenum`
  for 12 days before the missing-field default-zero surfaced as the "weapon won't render" symptom).
- Adding a new envelope field without wiring it through both emitter and parser.
- Renaming an internal struct field and only updating one of the two parse-time codecs.

**Symptom signature** (use this to recognise the class):
- An asset record loads (the record's outer envelope parses; e.g. `LOADER.UNIVERSAL.OK: kind=body
  scanned=68 registered=68` is fine; `LOADER.POOL.BODY.OK: active=1 bodies=68` is fine).
- BUT a specific scalar field in the record's struct is 0 / NULL when read at runtime.
- AND there are no `RESOLVE_FAIL` warnings for that field on the load path.
- Downstream consumer (typically a state-machine waiting for a non-zero filenum or pointer) stalls
  forever without an error log.

**Canonical fix pattern**:
1. Make the parser accept BOTH names: `if (jstream_str_eq(&key, "mesh") || jstream_str_eq(&key, "filenum"))`.
2. Leave the emitter writing the canonical (schema) name.
3. Update the smoke test for that asset kind to assert the `LOADER.POOL.<KIND>.OK: active=1 <kinds>=<N>`
   and `LOADER.UNIVERSAL.OK: kind=<kind> scanned=<N> registered=<N>` count lines -- this catches the
   parser regression (returns 0 records when the only-key gates fail) immediately.

**Audit command** (run when renaming an envelope field or adding a new one):
```
grep -nE 'fprintf.*"(<field>|<old_field>)":' port/src/romextract_pd*.c
grep -nE 'jstream_str_eq\(&key, "(<field>|<old_field>)"' port/src/loader_pool.c
```
Then cross-check against [universality-pivot-schemas.md](designs/catalog/universality-pivot-schemas.md)
Section 2.x for the canonical key name.

**Known instances**:
- B-328 (2026-05-15): `mesh` / `hand` in `.pdhead` and `.pdbody` -- parser reading `filenum` /
  `handfilenum`. Fixed at `port/src/loader_pool.c::parseHead, parseBody`.

---

## SP-17: Per-FACE matrix capture collapses N64 weighted-vertex (multi-matrix) skinning

**Severity**: HIGH — silent geometry corruption on every animated generated chr body (NOT a crash; renders wrong)
**Root cause**: N64 chr bodies skin limbs by **weighted vertices** — the DL interleaves matrix loads and vertex loads so a single triangle's three verts are transformed by *different* bone matrices (e.g. helper matrix 17 + bone matrix 10 across a knee seam). The `.pdmesh` data model records **one matrix per FACE** (`romextract_pdmesh.c::s_objEmitTri`, `model.faces.json` `{face_index, matrix_index}`) and writes all 3 verts RAW under that single (active/last-loaded) matrix. The consume (`modasset_compiler.c` ~6463-6494) emits `gSPVertex(3)` per tri under one matrix. So every weighted *seam* triangle mis-binds the verts that belong to the other bone.

**Why it usually hides**: when the joint is near-straight (bind pose, idle/standing anims, small rotations) a bone and its half-angle helper matrix are nearly equal, so a mis-bound vert barely moves. It only becomes visible when a joint bends hard (large rotation), which makes the helper diverge from the bone and flings the mis-bound verts off-body. Pixel symptom: a faceted limb jumble under poses with sharp joint angles (B-942: dark_combat legs under cutscene anim 1157 bend ~90deg).

**Scale (measured on cdark_combat / Joanna, filenum 0x42, 601 tris)**: **225/601 (37%) are seam triangles** (verts span >1 matrix); 137 (23%) have v0 loaded under a different matrix than the tri's active one; 106 are leg seams spanning every leg joint (pelvis-hip-kneeHelper-thigh-foot, both sides). Affects all generated chr bodies port-wide.

**Instrumentation trap (why this was an impasse for ~4 sessions)**: the seam detectors `B942STITCH`/`B942DESYNC` were added ONLY to the `G_TRI1` handler. cdark_combat emits its triangles via **`G_TRI4`** (the 4-tri packed command, `romextract_pdmesh.c:1536`), which had NO seam check — so the probes reported 0 and the extraction looked "byte-faithful." **Audit rule: any per-vertex/per-tri invariant probe in the DL walk MUST cover both G_TRI1 and G_TRI4 (and G_TRI2 if present).**

**Fix strategy**: carry a **per-vertex matrix index** through the pipeline. Extractor: the per-slot `slots_mtx[]` already tracked in `s_exportGdlToObj` is the source — emit it as a parallel per-vertex array (e.g. a 5th `model.obj` v-token or `model.vtxmtx.json`). Consume: group each DL's verts by their per-vertex matrix and reproduce the N64 interleave — load matrix, `gSPVertex` that batch, then tris referencing the multi-batch vertex cache (`render.json` already preserves the matrix-load ORDER; what's missing is which verts bind to which load). Bump EXPORT_VERSION + FAST_CACHE_KIND.

**Fix LANDED (2026-06-25, B-942, structure-verified — human does final CI-menu render verify before commit)**: chose to extend `model.faces.json` (keyed by face_index, exactly how the consumer already iterates) rather than a new file. Each face record now carries an optional `"vtx_matrix":[m0,m1,m2]` triplet alongside the back-compat per-face `matrix_index`.
- Extractor (`romextract_pdmesh.c`): `s_objEmitTri` gained 3 per-corner matrix args (from `slots_mtx[]` at the G_TRI1/G_TRI4 call sites); writes the triplet to faces.json; added a `seam_face_count` stat (logged as `seamtris=` per model). Added the missing **G_TRI4** STITCH/DESYNC probes (were G_TRI1-only). Bumped `OBJ_EXPORT_VERSION_LABEL` + `FAST_CACHE_KIND` with `_vtxmtx` to force re-extraction.
- Consume (`modasset_compiler.c`): `obj_triangle_t` gained `s32 vtx_matrix[3]` (default -1); `parseFacesJson` reads the optional triplet; new `generatedTriComputeBatch()` groups a tri's 3 corners into contiguous equal-matrix runs (slot permutation + per-run matrix), `fillGeneratedTriVertices()` lays the verts in slot order, and both emit loops (render-stream + flat) now emit `gSPMatrix(LOAD)+gSPVertex` per run then one `gSPTri` over remapped cache slots. **Single-matrix / legacy `-1` tris collapse to run_count==1 = byte-identical pre-B942 emit** (props/hands/heads unchanged — verified: `head_dark_combat` matrices=1 emitted ZERO multibatch tris). gdl source buffer grown `tri_count*3 -> tri_count*7` (worst case 3 mtx + 3 vtx + 1 tri per seam tri). NOTE: the flat `buildGeneratedModeldefFromMesh` path (single matrix 0, no hierarchy) was intentionally left untouched — chr bodies route through the hierarchy/render-stream path.
- Verification: re-extract logged `seamtris=225` on cdark_combat (was hidden as 0); faces.json carries 601 vtx_matrix triplets, 225 seam (corner matrices differ), 0 malformed; G_TRI4 STITCH/DESYNC probes now fire. Consume: dark_combat + model_cdark_combat leg/limb DLs emit multi-batch loads (maxruns 2-3, never >3); 61 modeldefs compiled with no overflow/crash, clean shutdown. **Still pending: human verifies the actual CI-menu Joanna leg render (the money shot) under bent anim 1157 before commit.**

**Search command**: `rg -n "s_objEmitTri|slots_mtx|matrix_index|vtx_matrix" port/src/romextract_pdmesh.c`; consume side `rg -n "generatedTriComputeBatch|vtx_matrix|run_count" port/src/modasset_compiler.c`.

**Known instances**:
- B-942 (2026-06-25): dark_combat (Joanna) legs scramble under cutscene anim 1157. Root-caused to this pattern. **Fix landed + structure-verified (per-vertex matrix carried through extract+consume); awaiting human render verification.** The jointflags/type_hi work was a prior partial (computed the helper matrices but the seam verts were still mis-bound to one matrix).

## SP-18: AI ailist command returns "continue" without advancing `g_Vars.aioffset`

**Severity: HIGH (frame hang).** The ailist dispatcher (`src/game/chrai.c`, the
`while (g_Vars.ailist)` loop) executes `g_CommandPointers[type]()`; a return of 0 means
"continue processing this frame" and the command is REQUIRED to have advanced
`g_Vars.aioffset` itself. Any command whose early "handled" return path skips its advance
makes the loop re-dispatch the same command forever -> the whole frame hangs (watchdog
kill; no crash, MEMPC intact). Found as B-949 (2026-07-04): `aiSayCiStaffQuip` ->
`scenarioSourceAiGraphExecuteSayCiStaffQuip` returned 1 (audio unresolved / char-ptr fail
/ required<0) WITHOUT the `g_Vars.aioffset += 4` that its success path does; intermittent
because it only fires when the quip audio fails to resolve.

**Fix applied (the class, not the instance):** a no-progress guard in the dispatcher --
capture `prevoffset` before the call; if the command returns 0 and left `aioffset`
unchanged, force-advance by `chraiGetCommandLength`. Commands that intentionally move
aioffset (jump/goto/label) already differ and are untouched.

**Audit checklist:** any `scenarioSourceAiGraphExecute*` (or legacy `ai*`) handler that has
BOTH a `g_Vars.aioffset += N` success path AND early `return` paths -- confirm the early
returns either advance aioffset or return the "break" value (non-zero from the command).
Search: `grep -n "return 1;" port/src/scenario_source_runtime.c` near `aioffset +=` sites.
The dispatcher guard now backstops all of them, but the root handlers should still be
correct so the intent (skip vs re-run-next-frame) is explicit.

## SP-19: Recursive graph flood with a depth cap but no visited-set

**Severity: HIGH (load/frame hang).** A recursion that walks a graph (rooms via portals,
nodes via edges) and guards only with a DEPTH cap -- not a visited-set -- revisits the
same nodes via every distinct path. On a highly-connected graph the call count is
branching^depth, which for real data (e.g. `base:villa`, 304 portals, depth cap 20)
explodes into billions of calls = an effective hang, even though it is technically
"bounded". Found as B-948 villa (`func0f001c0c -> func0f00215c -> func0f002844` in
`src/game/dlights.c`): the light-flood recursed with `arg2 < 20` but no visited-set.

**Fix applied:** a total-call cap on the existing per-call counter (`DLIGHTS_FLOOD_CALL_CAP
= 3,000,000` on `var80061440`); normal levels finish far below it (291-683,952), the
pathological level caps + continues (approximate result + WARNING) instead of hanging.
The cleaner-but-bigger alternative is a real visited-set; the cap is the minimal,
behaviour-preserving guard for legacy N64 flood algorithms.

**Audit checklist:** any self-recursive function whose only recursion guard is a depth/
count compare on a parameter, walking geometry/graph data whose connectivity is
data-driven (portals, waypoints, AI links). Especially N64-era algorithms tuned for
small original levels but now fed larger/modded graphs. Search: `grep -rn "func.*(.*arg2
+ 1" src/` and look for recursive calls guarded only by `arg < MAX`.

---

## SP-20: Recycled pool slot inherits stale fields (partial re-init)

**Severity: HIGH (0xc0000005 use-after-stale-value).** An object is allocated from a
recycled pool slot (bump-allocated `MEMPOOL_STAGE` memory) and its init function sets
fields ONE BY ONE rather than zeroing the whole slot first. Every field the init MISSES
keeps the bytes of the PREVIOUS occupant of that slot. When a later read treats that
stale value as an index / tagnum / pointer, it dereferences freed or unrelated memory and
AVs. The bug is invisible at low reuse (slots happen to land on fresh-zeroed memory) and
only appears when reuse crosses into memory previously used by a different data structure
-- so it presents as count-/order-dependent and layout-sensitive, easy to misattribute.

**Known instances (same class, `chrInit` in `src/game/chr.c`):**
- **B-331:** `chr->myspecial` un-inited -> tagnum-shaped garbage -> `objFindByTagId` stale
  `tag->obj` deref -> AV at `chrCalculatePushPos` (fixed 2026-05-16 by adding
  myspecial/yvisang/teamscandist/convtalk/naturalanim defaults -- the INSTANCE).
- The whack-a-mole of adding one field per crash is the anti-pattern.

**Fix applied (the CLASS fix, 2026-07-05):** `memset(chr, 0, sizeof(*chr))` at the top of
`chrInit` (right after the slot is found/NULL-checked, before the explicit inits). Every
field now starts at 0; the existing explicit `-1`/sentinel inits still run and override
where a non-zero default is required. Safe because all callers (chr0f020b14, body.c MP/AI
paths, botmgr.c, playerreset.c) customize AFTER chrInit returns. Modern HW does not need
the N64's skip-the-memset micro-optimization (project rule: correctness > micro-opt).
Validated: combat_sim 15/15, swarm_cpu clean (no crash). NOTE: this did NOT fix B-952
(whose crash is a corrupt MODEL node tree, a different class) -- it prevents the whole
stale-chr-FIELD class going forward.

**Audit checklist:** any `*Init`/`*Reset`/`*Allocate` that (a) takes a slot from a pooled/
recycled array or `mempAlloc`'d region and (b) assigns fields individually instead of
`memset(0)`-first. If a new field is added to the struct but not to the init, it silently
inherits garbage. Prefer zero-then-set-sentinels over enumerate-every-field. Search:
`grep -rn "mempAlloc\|g_.*Slots\[" src/game/*.c` and check the matching init zeroes the
whole record before per-field assignment.

---

## SP-21: Sibling accessor functions with inconsistent NULL handling

**Severity: MEDIUM-HIGH (0xc0000005 on a legitimately-NULL argument).** A family of
related accessor/traversal functions shares a contract where one of them accepts a NULL
node/pointer as a valid "nothing here" input, but one member of the family forgot the
guard. Callers that legitimately produce NULL (from a finder that returns NULL by design)
pass it into whichever family member they need; the guarded members degrade gracefully,
the unguarded one dereferences NULL and AVs. Presents as content-specific (only fires when
a particular asset/scenario yields the NULL) and is easy to misattribute to the caller's
subsystem instead of the primitive.

**Known instance (B-952, `src/lib/model.c`):** `modelNodeFindMtxNode()` returns NULL by
design when a bbox node has no CHRINFO/POSITION/POSITIONHELD ancestor. Its result is fed
directly into the node-position family:
- `modelNodeGetModelRelativePosition` -- guarded (`while (node)`), NULL-safe.
- `modelFindNodeMtxIndex` / `modelFindNodeMtx` -- guarded (`while (node)`), NULL-safe.
- `modelNodeGetPosition` -- **NOT guarded**: bare `switch (node->type & 0xff)` -> AV on
  NULL. base:skedarruins parents a DROPTYPE_5 projectile to a bbox-less root object, so
  `objDrop` fed NULL in and crashed at 5.2s (deterministic). Fixed by adding a NULL guard
  that mirrors the function's own `default:` case (zero position, return), plus a
  caller-side guard in `objDrop` (propobj.c) that logs WHICH object is bbox-less.

**Fix strategy:** when one member of an accessor family treats NULL as valid input, ALL
members reachable from the same finder must. Guard the primitive to match the family's
weakest-precondition member, not just the crashing call site.

**Audit checklist:** for any finder that can return NULL by design (`*Find*` returning a
pointer), grep its call sites and confirm every consumer tolerates NULL. Search:
`grep -rn "modelNodeFindMtxNode\|modelFindBboxNode\|objFindBboxNode" src/` and verify each
result flows only into NULL-tolerant callees.

**Debugging-method note (why this cost a whole session):** the crash breadcrumb pointed at
`chrTick`/chrnum-5052 (the last chr ticked), and 8 hypotheses + 3 custom debug tools
(memp canary, DR0 hardware watchpoint, per-instance modeldef clone) were spent chasing the
CHR subsystem. A 1-step symbolize of the crash RETURN STACK immediately showed
`modelNodeGetPosition <- objDrop <- objDropRecursively <- objTickPlayer` -- an object drop,
not a chr. **Pull and symbolize the crash stack BEFORE trusting a domain breadcrumb**; a
breadcrumb tells you what ran last, not what crashed.

---

## SP-22: Public asset source fields stop at registration or silently fall back

**Severity: HIGH — author edits can be accepted and activated without affecting
production behavior.**

The typed-archive pipeline has several independently successful layers:
extraction, descriptor/manifest parsing, catalog registration, generic runtime
binding, and the final native gameplay/renderer/audio consumer. Tests that stop
at an accessible binding can report a family green even when no production
consumer reads the binding, only a subset of fields is read, or a missing
binding silently falls back to a parallel native/catalog table.

**Known instance (B-953, `.pdgamemode`):** all public fields reach
`asset_runtime_binding_t`, but `scenarioCtxAccepts` consumes only
`gamemode_team_based`. `min_players`, `max_players`, public name/description,
and the authored rules file do not control the scenario picker/rules path. The
test suite explicitly pinned min/max as unwired. A missing binding warns once
and uses `ext.gamemode.team_based`, so the source-chain failure is masked.

**Related sites requiring propagation audit:** bot-profile creation/setup falls
back to `g_BotProfiles`; theme registration falls back to a catalog
FileProvider path; the generic runtime layer supports 21 metadata families, but
direct `assetRuntimeFind*` production queries currently exist only for game
modes, bot profiles, and themes. Specialized catalog/compiler consumers may be
valid and must be credited separately; a binding with no consumer is not proof
of production use.

**Fix strategy:**

1. Inventory every public field for every typed family.
2. Trace each field through extractor, archive, scanner/walker, catalog,
   provider, runtime adapter/cache builder, and a real production consumer.
3. Add behavior-focused tests that change the public value and observe the
   production result; do not accept source-presence/static-string assertions as
   utilization proof.
4. Remove or clearly reject unsupported fields instead of advertising inert
   author controls.
5. Under public-source ownership, treat a missing post-extraction binding/source
   product as an asset-chain failure; do not silently substitute ROM or a
   hand-maintained native mirror.

**Search commands:**

`rg -n "assetRuntimeFind|RUNTIME_MISS|using g_|using ext\\." port src`

`rg -n "entry->ext\\.|binding->" port/src/asset_runtime.c <production-consumer>`

---

## SP-23: Aggregate operations log child failures but return success

**Severity: CRITICAL — partial asset sets can receive success receipts.**

A batch extractor, converter, loader, or registration walk tracks per-item
failures and may even suppress its success cache, but returns only the number
of successful writes/loads. Its caller therefore receives a nonnegative result
for a partial data set and can publish a false completion receipt.

**Known instance (B-958):** 12 typed-family emitter implementations and the
post-texture UI repair path.

**Fix strategy:**

1. Continue the batch far enough to report every child failure.
2. Return a negative aggregate result when any required child failed.
3. Return a success/skip count only when the failure count is zero.
4. Write fast-cache and completion receipts only after complete success.
5. Preserve the same contract in late repair and retry paths.
6. Enumerate every family in focused tests so a new emitter cannot silently
   reintroduce the pattern.

**Search command:**

`rg -n "failed|failures|return written|return .*_written" port/src/romextract_pd*.c port/fast3d/pdgui_theme*.cpp`

---

## SP-24: Placeholder public source presented as extracted content

**Severity: CRITICAL — creators can edit files that do not represent the game
state and that the game does not execute.**

An emitter creates a well-formed typed archive whose payload names an original
backend, uses empty arrays, default colors, generic tuning, or prose such as
`"source": "original_perfect_dark"`. The archive passes envelope and catalog
tests, but it neither contains the complete source state nor drives the native
production path. This is more dangerous than an absent format because the UI
and validation surface imply mod support that does not exist.

**Known instance (B-959):** prop behavior-graph execution and parts of
`.pdmission` remain open, along with residual partial fields in
weapon/character/voice/effect/theme. HUD, material, skin, vehicle, game-mode,
bot-profile, and prop core state now have structured source hydration, strict
schema validation, production consumers, and focused mutation/behavior proof;
live edited-source receipts are still required before those families are
validated.

**Fix strategy:**

1. Inventory the native state and behavior that each advertised family owns.
2. Extract every supported field into an editable, named, source-faithful
   schema; reject unsupported fields rather than filling placeholders.
3. Compile or apply that same public source into the existing production
   consumer, with source-hashed engine products only as private cache.
4. Remove generic-activation-only claims; a binding without a consumer is
   `partial`, not implemented.
5. Add change-the-source/observe-production behavior tests and live evidence.

**Search commands:**

`rg -n "slots.: \\[\\]|source.*original_perfect_dark|parity_backend|default.*material|no consumer yet|ignored" port/src/romextract_pdmeta.c port/src port/fast3d src/game`

`rg -n "assetRuntimeFind" port src -g "!asset_runtime.c" -g "!tests/**"`

---

## SP-25: Public-source failure gated only by optional debug mode

**Severity: CRITICAL — production can silently ignore a broken or edited
public archive.**

A consumer resolves a FileProvider/public typed source, attempts to compile or
play it, and only refuses the native fallback when `assetSourceDebugIsEnabled`
is true. Normal builds therefore accept a source-chain break that source-only
tests reject.

**Known instance (B-960):** font segments, music sequences, animation clips,
file-backed SFX/voice, and MP3 source.

**Fix strategy:**

- Once a typed public source is selected, failure is unconditional.
- Optional debug flags may add diagnostics, never determine correctness.
- Native/ROM/loose-cache paths are allowed only for a route that has not yet
  selected a public source and is explicitly outside the migrated family.
- Induced-failure tests must run without debug flags.

**Search command:**

`rg -n "assetSourceDebugIsEnabledFor|falling back|fallback to ROM|loose extracted|raw ROM bytes" port/src src/game src/lib`

---

## How to Use

- Before starting any work that touches arrays, memory allocation, or stage indexing, scan this file for relevant patterns.
- When fixing a one-off bug, check if it's an instance of a pattern here. If so, do a propagation check (§3.6) on all files listed under that pattern.
- When discovering a new pattern class, add it here with severity, root cause, known sites, and search command.
