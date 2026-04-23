# Resume Report - 2026-04-23

> Three-section situational snapshot produced on 2026-04-23 at Mike's request after a gap in activity. The audit portion is also saved as [`2026-04-23-full.md`](2026-04-23-full.md) following the Super Audit skill structure; read that file for the canonical findings.

---

## Section 1 - Super Audit (summary + pointer)

**Canonical file:** [`context/audits/2026-04-23-full.md`](2026-04-23-full.md).

This 4-23 audit is a **delta pass** on top of [`2026-04-21-full.md`](2026-04-21-full.md). The 4-21 report framed the remaining programme as seven Tiers (0 through 6) and a pillar C-1. Between 4-21 and today, the tree ran through Sessions S412 to S443 plus four Cursor-authored commits. Status:

- **Tier 0 (tripwire + doc comments) DONE** (S415).
- **Tier 1 (P1-A..D server security hardening) DONE** (S416). Protocol bumped to **v39**.
- **Tier 2 (hosting modes ADR) DONE** (S420).
- **Tier 3 (P3-A/B listen host "go online") DONE** (S421).
- **Tier 4 P4-A (broker ADR) DONE** (S422/S423). **P4-B implementation spike still pending approval.**
- **Tier 5 (P5-A interest management) design DONE** (S424); implementation open.
- **Tier 6 (SP-1 sites, hub/room ADR) DONE** (S425).

So almost every High and Critical from 4-21 is closed or has a clear next step. What remains is (a) broker implementation, (b) interest management implementation, and (c) small carry-overs (SAVE-1, LAYOUT-2, M-1).

**New findings this audit (2026-04-23):**

- **AUDIT-23-M1 (Medium, Code Quality):** `CMakeLists.txt` has a UTF-8 BOM in the working tree, uncommitted. First introduced on-commit in Cursor's `bd9a07d7`, removed in a later auto-commit, and now back as an uncommitted local edit. Editor-level origin (Cursor's default encoding on this host). Fix: strip the BOM, retune Cursor to "UTF-8 without BOM", add a pre-commit check for BOM on `*.txt`, `*.cmake`, `*.c`, `*.h`, `*.cpp`.
- **AUDIT-23-M2 (Medium, Repo Hygiene / Supply Chain):** `.gitignore` has no `~$*` pattern. Cursor's `bd9a07d7` committed `~$2_Implementation_Plan_Apr20.docx` (162-byte Word lock file) as part of a docs-update diff. It was removed by `00637b13`, but only because the file was deleted locally; the gitignore does not protect against recurrence. Fix: add `~$*`, `*.tmp`, `*~`, `.DS_Store` to `.gitignore`; tighten dev-window auto-commit to avoid `git add -A`.
- **AUDIT-23-L1 (Low, Repo Hygiene):** `context/PD2_FixPlan_420Bugs.docx`, `PD2_FixPlan_420Bugs_Audited.docx` at repo root, and `context/_docx_extract/*` (23 extracted XML files) are all tracked. Two parallel sources of truth for the 4-20 fix plan; they can drift. Fix: collapse into the `.md` tree or move the docx under `context/designs/` with the extracted XMLs gitignored.

**No new Criticals or Highs.** The pillar carry-over (C-1 / MASTER-C5, game-agnostic server) is unchanged: P4-A doc exists, P4-B code does not.

Scorecard delta: Security/Trust 7 -> 8 (Wave 3A landed as code, not tracker entries); Architectural Discipline 6 -> 6.5 (hosting + broker ADRs). Other dimensions unchanged.

---

## Section 2 - Git and Session-Log Diff Since Sunday (2026-04-19)

### Commit volume

`git log --since="2026-04-19" --pretty=oneline | wc -l` returned **91 commits**, all authored by `Mike Hays <mikehaysjr@aol.com>`. Author filter alone does not separate hands, because both Cursor and Claude work lands on Mike's name. The differentiator is the trailer line:

- `Made-with: Cursor` - 4 commits.
- `Co-Authored-By: Claude ...` - many commits (Claude worktree merges).
- Plain `chore: auto-commit before release (dev window)` - dev-window release-pipeline auto-commits, no human in the loop.
- Plain `chore: pre-release commit vX.Y.Z` - dev-window release-pipeline version bumps.

### Cursor-specific commits

Four commits carry `Made-with: Cursor`:

| SHA | Date | Message | Files | +/- |
|-----|------|---------|-----:|----|
| `bd9a07d7` | 2026-04-20 | fix: PC input, solo interact prompt, and April 20 context | 11 | +775/-33 |
| `48ed7f4e` | 2026-04-20 | fix: Apr20 stability batch (lv cap, spawn AABB, UI, mod catalog, Grid) | 12 | +121/-26 |
| `fb5edd6d` | 2026-04-20 | feat(ui): menu input docs, hold ring, hoverbike USE tap/hold | 22 | +1125/-155 |
| `d3247e7e` | 2026-04-21 | fix: B-217 through B-222 stability batch | 14 | +259/-46 |

### What the session log says happened since Sunday

`context/daily-logs/` is empty after 2026-04-17 (only `2026-04-12.md` and `2026-04-17.md` exist). Canonical narrative lives in `session-log.md`. Relevant sessions:

- **S412 (4-21):** Full Super Audit standalone, produced `2026-04-21-full.md`. Findings-only.
- **S415 (4-21):** P0-A participant mask tripwire + P0-B `net_hash` doc comments. Static assert now lives in `participant.c` next to the encode/decode helpers.
- **S416 (4-20):** Tier 1 P1-A..D (admin auth rate limit + disconnect, BCryptGenRandom for cookies, MoveFileExA + in6_addr for bans, admin payload truncation footer). Protocol bumped to v39.
- **S417-S418 (4-20):** PC USE release synthesizes reload when no prompt; `ActionMap.InteractHoldExtraTerminalMs` added as a configurable knob.
- **S419 (4-20):** Dev Window v2 release pipeline aborts on step failure; stale `.git/index.lock` cleanup.
- **S420 (4-20):** Tier 2 P2-A hosting-modes ADR (`designs/hosting-modes-listen-vs-dedicated.md`).
- **S421 (4-20):** Tier 3 P3-A/B listen host UX. `pdgui_menu_network.cpp` now offers "Host game / Go online" and routes through `netStartServer` without `g_NetDedicated`. Lobby/room/distrib paths reused; `Net.Server.Port` added to `pd.ini` via `configRegisterUInt`.
- **S422/S423 (4-21):** Tier 4 P4-A broker ADR. Revised to put catalog IDs and host manifest at the center; policy module secondary. `server_stubs.c` shrink is P4-C follow-up.
- **S424 (4-21):** Tier 5 P5-A interest management design (`designs/interest-management-replication.md`).
- **S425 (4-21):** Tier 6 P6-A SP-1 remaining sites + P6-B hub/room ADR notes.
- **S425 (4-21, duplicate session ID):** Auto-commit window at `mpspawn_orchestrate.c` + `netmsg.c`. Note: session log has two S425 entries with different content - a bookkeeping bug worth noting but not acting on.
- **S426 (4-20):** MP match-start spawn orchestration wired (`mpspawn_orchestrate.c` called before botSpawnAll); player apply-from-pool.
- **S427 (4-20):** Active-menu ImGui gate refactor (pure-state predicate `pdguiActiveMenuIsOpen`, no init fallback).
- **S428 (4-20):** Hungarian assignment for MP spawn orchestrator; anchors, Voronoi, swap refinement.
- **S429 (4-21):** F6 toggle to freeze MP bot AI plus top-center banner.
- **S430 (4-20):** PD_DEV_BUILD, F7 invincibility UI, stable-gating flag.
- **S431-S432 (4-21):** Playtest notes from Chicago CS + input/overlay notes logged (logged only, fixes deferred).
- **S433 (4-21):** Active-menu radial layout and stick inversion fix.
- **S434 (4-21):** 4-20 stability batch (B-217..B-222) implementation. Cursor's `d3247e7e` is the commit manifestation of this session.
- **S435-S436 (4-21):** CI main menu vs interact prompt; `PD2_FixPlan_420Bugs.docx` propagation (menupool per-frame acquire pattern across many pdgui_menu files).
- **S437 (4-21):** B-221.4 controller mapper `SetCursorScreenPos` fix; B-221.5 duplicate-VK ordering fix.
- **S439-S443 (4-21):** CI death respawn fix, subtitle ownership guard, modal scrim coalescing (`pdguiPopupDarkenBeginFrame`/`...Flush`), invalid-stagenum sanitizer.

### Large-magnitude auto-commits (worth flagging)

Four auto-commits show symmetric insert/delete counts suggestive of line-ending flips rather than real edits:

- `2db578b5` (4-20) - +1055/-1055 across 6 files.
- `4fcc42ec` (4-20) - +2470/-295 across 41 files (mixed real+ending edits).
- `061c3868` (4-19) - +1026/-1026 across 10 files.
- `11c95ea1` (4-19) - +2228/-2228 across 5 files.

This is the "CRLF-inflated-line-counts" pattern. Recommend pinning `.gitattributes` for per-extension EOL so the pipeline stops producing this noise.

### Anomalies found

- **`CMakeLists.txt` BOM flip-flop.** `bd9a07d7` added a UTF-8 BOM; a later auto-commit stripped it; current working tree has it back as uncommitted edit. Consistent signal that Cursor's text-encoding setting does not match the repo.
- **`~$2_Implementation_Plan_Apr20.docx` committed by Cursor in `bd9a07d7`.** Word lock file, removed two commits later. `.gitignore` does not prevent recurrence.
- **Two S425 sessions in `session-log.md`** (one at Tier 6 P6-A/B note, one at 4-21 spawn orch batch). Merge artifact; context housekeeping.
- **No entries in `context/daily-logs/`** for dates 4-18 and after. Per CLAUDE.md standing order 1 the session log is acceptable as the record, but the "daily-logs" convention has lapsed without an explicit decision.

### What was NOT reverted

Zero reverts since Sunday. Everything that was attempted stuck.

---

## Section 3 - Cursor-Impact Assessment

### Bugs Cursor was chasing

Cursor's commits correspond to these bug rows in `context/bugs.md`:

- **B-203** (D-pad fire mode binding) - addressed in `bd9a07d7`.
- **B-209** (joy sample count for reload/interact) - addressed in `bd9a07d7`.
- **S397** (interact prompt gating) - addressed in `bd9a07d7`.
- **B-202 / B-204 / B-205 / B-206 / B-208 / B-210 / B-214 / B-215 / B-216** - Apr20 stability batch in `48ed7f4e` (weapon wheel highlight, lvupdate240 catch-up cap, spawn AABB fallback, Tunes cache, Modding Hub debounce, mod catalog rebuild after delete, Scale tool paths, Grid skip redundant CI transition).
- **B-217..B-222** - 4-20 playtest class, fixed in `d3247e7e` (F6 freeze full, Chicago bot pile-up, FP weapon mismatch, mod.json spam, input/hold-ring basket, overlay tint basket).
- **B-221.x** - input polish in `fb5edd6d` and `d3247e7e` (hoverbike tap mount on PC, hold ring grace/pin, tap-vs-hold deferred activate, visual mapper screen-pos, duplicate VK priority, scorecard Back).
- **B-222 companions** - `fb5edd6d` also touched `pdgui_menu_mainmenu.cpp` and hold-ring docs.

### Current tree state vs before Cursor

- **All 6 B-217..B-222 rows are "FIXED-PENDING-PLAYTEST".** Cursor's code went in; the playtest pass that would close them has not happened yet (S442 explicitly notes "Playtest itself not executed in this environment"). Static read of the diffs shows:
  - Defensive nullchecks are present in the new helpers (`propobjPcHoverbikeTapMountOnUseRelease` guards `currentplayer`, `currentplayerstats`, `g_InteractProp`).
  - `actionmap.cpp` hold-ring state machine adds two explicit new fields (`hold_pin_full_until_ms`, `hold_vis_grace_until_ms`) with clean semantics and a 100 ms grace window on release.
  - `bot.c` B-218 XZ-proximity discard bounds its loop by `g_BotCount`, only runs for matched `aibotnum`.
  - `bot.c` B-217 F6 freeze returns `TICKOP_NONE` without calling `chrTick`, which matches the bug description.
  - `modmgr.c` B-220 `fsFileSize` guard lives where it should.
- **No regressions spotted** in the sampled reads. No silent file shrinkage (line counts match diffs). No CRLF flip inside Cursor's commits (the flips appear only in dev-window auto-commits).
- **One introduced hygiene issue: the UTF-8 BOM on `CMakeLists.txt`.** See AUDIT-23-M1. Small, editor-level; not dangerous but churny.
- **One repo-hygiene slip that the gitignore should have caught: the `~$...docx` lock file.** Already self-corrected by a later auto-commit, but the gitignore gap is real.

### Regressions / truncation check

- `wc -l`-style line counts of the Cursor-touched files (`actionmap.cpp`, `bot.c`, `propobj.c`, `mpspawn_orchestrate.c`, `modmgr.c`, `pdgui_backend.cpp`, `pdgui_menu_mpingame.cpp`, `pdgui_interact_prompt.cpp`) match the commit diff totals. No truncation signatures.
- No files Cursor touched show any delete-only commits subsequently that would suggest a rollback. The follow-up Claude sessions (S434-S443) extended rather than reverted Cursor's work.
- Protocol version (`NET_PROTOCOL_VER`) is consistently 39 across `constraints.md`, `network-architecture.md`, and `infrastructure.md`. No split-brain.

### "Has it gone well" assessment

Static read says: **Cursor's work landed clean, the fixes look right, and no regressions are visible in the diffs.** The feeling that "it has not gone well" likely reflects:

1. **Verification debt.** Six bugs are stuck in FIXED-PENDING-PLAYTEST because MSYS2/ninja/game-client are not available in the agent environment. Without a Mike-on-Windows playtest, the tracker cannot advance; the batch stays open even though the code is there.
2. **Dim-architecture spill.** B-222 was mostly fixed but spawned B-223 when the team realized there are full-viewport alpha paths outside `pdguiPopupDarkenBehind`. That is legitimate scope discipline, but it looks like "another bug appeared" to an outside read.
3. **Hygiene noise.** BOM on `CMakeLists.txt`, `~$...docx` lock file, 1:1 ins/del auto-commits. Each is small; stacked they feel like thrash.
4. **No playtest loop.** A two-day window where six fixes landed but none were run. That is frustrating even when the fixes are correct.

### Build verification note

A headless build check was attempted. The sandbox is Linux and the project targets Windows x86_64 via MSYS2/MinGW. I cannot run `devtools/build-headless.ps1` nor `ninja -C Build pd pd-server` from this environment. Per the memory notes `build-environment-recipe` and `build-verify-before-done`, that verification falls to Mike on Windows. The static code review substitute is recorded above; final build verification must come from a Windows shell.

### Prioritized action list when work resumes

1. **Strip the BOM from `CMakeLists.txt`.** Single sed line; retune Cursor to "UTF-8 without BOM"; add a pre-commit check for BOM.
2. **Extend `.gitignore` with `~$*`, `*.tmp`, `*~`, `.DS_Store`.** Five minutes; prevents recurrence of the Word lock file class.
3. **Pin CRLF behavior via `.gitattributes`.** Closes the 1:1 ins/del auto-commit noise.
4. **Playtest the B-217..B-222 batch on Windows.** Chicago CS with high bot count (B-218 spawn, B-219 weapon), CI death loop (B-222/B-223 dim architecture), PC hoverbike single-tap (B-221.1), hold ring release reset (B-221.2), scorecard Back hold (B-221.6).
5. **Reconcile the two S425 entries in `session-log.md`.** Rename one so the session tracker is monotonic.
6. **Approve or decline P4-B (broker implementation spike).** Pillar C-1 is the longest-pole item.
7. **SAVE-1 and LAYOUT-2 sweep** (both under-1-day carry-overs).

None of these are emergencies. The tree is green. The playtest is the critical path.

---

*End of resume report. For the canonical Super Audit findings see [`2026-04-23-full.md`](2026-04-23-full.md).*
