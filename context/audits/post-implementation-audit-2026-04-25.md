# Post-implementation Super Audit -- dev `7ec5a516`

**Date:** 2026-04-25 22:27 local
**Method:** Third-party static read + build verification on the actual code at HEAD. Auditor was spawned fresh after the day's implementation work landed; not the author of any of the audited commits.
**Stance:** Read-only. No code changes were made. Findings only.

---

## 1. Audit baseline

| | Value |
|---|---|
| Repo HEAD (main checkout) | `7ec5a516ffacb0dcd9b02ed6bd2f38385a7d3d6c` (`merge: Connectivity follow-ups (S468) from adoring-borg-2b6076`) |
| Branch | `dev` |
| Working tree | clean (`git status --short` empty) |
| Build target | x86_64-windows (MinGW + Ninja, MSYS2) |
| CMake configure | clean (0.9s configure; SDL2/OPUS/ZLIB/CURL static-linked) |
| Build command | `source devtools/build-env.sh && ninja -C Build pd pd-server` |
| Build result | exit 0 |
| `Build/PerfectDark.exe` | 55 930 968 bytes (mtime 2026-04-25 13:14) |
| `Build/PerfectDarkServer.exe` | 23 272 584 bytes (mtime 2026-04-25 13:14) |
| `NET_PROTOCOL_VER` (port/include/net/net.h) | **42** |

Note on environment: the worktree this session was spawned in (`.claude/worktrees/dreamy-curran-9c2a95`, branch `claude/dreamy-curran-9c2a95`) is at an older HEAD `6ad460e5` and is not the audit target. All code reads in this audit use absolute paths into the main checkout, which is on dev `7ec5a516`. The first build attempt via `devtools/build-headless.ps1` crashed in PowerShell with a runspace error and produced no binaries despite reporting exit 0; the bash path (`build-env.sh + ninja`) succeeded. Logged so Mike knows the headless powershell path is currently broken (not gated by this audit; flag for follow-up).

---

## 2. Per-item verification table

Each item maps to a claim made in the day's session reports / audit docs. Status:

- **CONFIRMED**: code at HEAD matches the claim.
- **DRIFT-FROM-CLAIM**: code at HEAD diverges from the claim in a way that affects correctness or coverage.
- **NEW-BUG-FOUND**: a real defect surfaced during verification.
- **DEFERRED-CORRECTLY**: claim explicitly defers; no code expected.

| # | Item | Scope | Claim | Verified at HEAD | Status |
|---|---|---|---|---|---|
| a | **Issue 1 weapon fix** (`264777cd`, B-246 round-2) | `src/game/bondgun.c::bgun0f0a5550` | Scope mode=6/7 visibility gate to LOWER/RAISE stateminors only. | `inHideTransition = (state == HANDSTATE_CHANGEGUN) && (stateminor == LOWER || RAISE)` defined at L7833-7836. Used at L7840-7841 to gate the existing `mode==6 || mode==7` clause. LOG.WPN.DIAG instrumentation kept at L7846-7870. | **CONFIRMED** -- with sub-finding (a.1) below. |
| a.1 | Issue 1 weapon fix -- log columns | LOG.WPN.DIAG | (No explicit claim, but Mike has been reading these logs to fingerprint the wedge.) | `gate_mode6` / `gate_mode7` columns are still emitted as raw `(hand->mode == HANDMODE_6) ? 1 : 0` at L7853-7854. They no longer mean "this gate caused visible=false" -- they mean "raw mode value matched." In the new world `inHideTransition` must also be true for those bits to actually fire the gate. | **DRIFT-FROM-CLAIM (LOW)** -- diagnostic semantics drifted. Future Mike reads of mode6=1 / mode7=1 in the log will now over-count the gate's contribution. |
| b | **B-247 spawn-into-wall** (`4252d7a3`) | groundy sentinel guard | Validate groundy in both spawn paths + body-swap. Falls back to pad Y on sentinel. | Three sites verified: `src/game/player.c:404-412` (orchestrator path, SPAWN.ORCH log prefix); `src/game/player.c:1177-1185` (newlife path, SPAWN.NEWLIFE log prefix); `src/game/forgemode.c:214-218` (body-swap explicit `prop->pos` init). Bound `\|groundy\| > 100000` consistent across both player.c sites. | **CONFIRMED** -- all three sites have the guard with consistent bound and fallback semantics. |
| c | **B-256 kill-attribution** (`7fbc5833`) | switch from dead `lastshooter` to live `lastattacker` | "Replace the `lastshooter >= 0 && timeshooter > 0` check at both sites with a NULL check on `lastattacker`." | `src/game/chr.c:967-968` patched (uses `lastattacker`). `src/game/player.c:5908-5909` patched (uses `lastattacker`). **Third site `port/src/net/netmsg.c:2127-2133` (`SvcPlayerStatsRead`) still reads `pl->prop->chr->lastshooter >= 0 && pl->prop->chr->timeshooter > 0` and falls through to `currentplayernum`-suicide credit on the always-true else.** | **DRIFT-FROM-CLAIM (HIGH)** -- the commit message claims "two read sites" but there are three. The missed site is the network-replicated death-attribution path -- exactly the path that drives MP killfeed and scoreboard credit when another peer dies. The whole reason B-256 was filed (kill mis-attribution in MP playtest) is most plausibly dominated by this site, not the local-only chr.c / player.c paths. |
| d | **J IMC architecture** | `port/src/actionmap.cpp` | `g_ImcMission` / `g_ImcCombatSim` mutually exclusive; vehicle IMC mount/dismount lifecycle; ACTION_SCORECARD_HOLD on gamepad Back. | `imcSceneAssertMutex` (L725) + `imcSceneSetMission` (L735) + `imcSceneSetCombatSim` (L747) deactivate the other on activation. `imcSceneClearGameplay` (L759) defensively drops Vehicle on stage transition. `imcVehicleMount` / `imcVehicleDismount` are clean idempotent toggles. `ACTION_SCORECARD_HOLD` declared at L1205 (action enum) and bound to JBTN_BACK in CS IMC at L2204. **Also addresses 4-24 AUDIT-24-H2/H3** by introducing `g_ImcForgeSession` (priority 6) for FORGE_TOGGLE and `g_ImcForge` (priority 7) for FORGE_ASCEND/DESCEND -- both higher priority than the gameplay IMCs that previously ate the trigger inputs. | **CONFIRMED** -- including unrelated benefit of closing prior-pass H2/H3. |
| e | **K input-authority -- single cursor** | `port/src/inputctx.c` + `port/fast3d/pdgui_backend.cpp` | `inputCtxSyncMouseMode` is the sole `SDL_ShowCursor` caller; `ImGuiConfigFlags_NoMouseCursorChange` short-circuits the ImGui SDL backend. | `pdgui_backend.cpp:371` sets `io.ConfigFlags \|= ImGuiConfigFlags_NoMouseCursorChange`. `imgui_impl_sdl2.cpp:631` short-circuits before its `SDL_ShowCursor` calls (L639, L650). `inputctx.c:491-492, 498-499` is the canonical authority. **HOWEVER three other `SDL_ShowCursor` callers exist in port code:** `port/src/input.c:1215` (`inputMouseShowCursor`, used by an MLOCK_AUTO heuristic at L768/L772); `port/fast3d/gfx_sdl2.cpp:261-263` (`gfx_sdl_set_cursor_visibility`, registered as a `gfx_window_manager_api` function pointer at L495); `port/fast3d/pdgui_backend.cpp:822` (B-92 hotswap deferred-flush, conditioned on hotswap state + `lvframe60 > 4`). All three have `pdguiIsActive()` or other guards but they are independent authorities. | **DRIFT-FROM-CLAIM (MEDIUM)** -- the "sole authority" claim is overstated. The K-c flag fix is correct and load-bearing for the imgui-backend race. The other three callers are largely benign in practice but they are real, and any future change to their guards could re-introduce the cursor race the K pass was meant to retire. |
| e.1 | **K-d drift assertion** | `port/src/menupool.c` | Post-acquire assertion that the requested ctx is live on the stack. | Two sites in `menupool.c`: L257 (already-active branch) and L296 (fresh-acquire branch). Both call `inputCtxIsActive(ctx)` and emit `LOG_WARNING "MENUPOOL: drift -- ..."` on mismatch. | **CONFIRMED.** |
| f | **L flat-menu pass -- 27 menus** | `port/fast3d/pdgui_menu_*.cpp` | Seven rules; spot-check claims of conformance. | **Spot-checked 5 menus not detailed in the per-menu evidence the audit doc carries:** `pdgui_menu_room.cpp` (6 layout BeginChild now `\| ImGuiChildFlags_NavFlattened`, scrollable lists left scoped -- correct); `pdgui_menu_training.cpp` (8+ NavFlattened sites; `bio_scroll`/`ht_entries` correctly excluded as scrollable lists); `pdgui_menu_lobby.cpp` (2/2 layout BeginChild flattened L244, L315); `pdgui_menu_teamsetup.cpp` (2/2 flattened L257, L343); `pdgui_menu_mpsettings.cpp` (4 flattened sites including `handicap_content` and `pdms_*_body`; `tunes_lib`/`tunes_sel` correctly excluded). Every spot-check passed. | **CONFIRMED** -- L pass landed in code matching the audit claim. No false-positive conformance entries surfaced in the spot-checks. |
| g | **Priority N -- bot tick spread** | `src/game/bot.c::botShouldTickAI` | 4-bucket scheme; uniform assignment by `aibot->aibotnum`; per-bot AI rate at 15 Hz at 25-32 bots. | `botShouldTickAI` at L103-131. Group count = 1 / 2 / 3 / 4 for `g_BotCount` ranges <=8 / 9-16 / 17-24 / 25-32. Bucket = `aibot->aibotnum % groups == g_Vars.lvframe60 % groups`. At lvframe60 60 Hz -> 4 groups -> per-bot AI tick = 15 Hz. Bucket assignment uniform because aibotnum is dense and stable. | **CONFIRMED.** |
| h | **Priority O -- capsule sweep** | `src/game/spawnpool.c::spawnPoolFindClearPosition` | Character-height-aware capsule (Skedar tall, Carroll short); sweep order documented; falls back when no clean position. | `spawnPoolGetChrCapsuleHeight` (L463) reads `chr->model` bbox via `modelFindBboxRodata` and returns `bbox->ymax - bbox->ymin` (sanity-clamped 0..1000), falling back to `chr->height` then `SPAWN_CLEAR_HEIGHT_FALLBACK = 185`. `spawnPoolFindClearPosition` (L499) tests fast-path first (L539), then sweeps `SPAWN_CLEAR_RING_COUNT=5` rings at radii `{30, 60, 100, 150, 200}` x `SPAWN_CLEAR_DIR_COUNT=8` directions = 40 candidates; returns false to caller on exhaustion (caller's retry pattern documented in the file header at L444). | **CONFIRMED** -- including the height-fallback chain and the sanity-clamp on degenerate bboxes. |
| i | **Priority Q -- render box** | `port/fast3d/pdgui_charpreview.c::pdguiCharPreviewRenderGBI` | FBO viewport save/override/restore; restore is unconditional with no early-bail leaving FBO viewport active for the next non-FBO render. | Save block at L680-691 (savedPlayerVp + savedSx/y + savedVl/Vt/Vw/Vh + savedFovy + savedAspct). Override block at L695-744. Render at L750. Restore block at L758-785, all unconditional within the function from after the FBO setup. The two early-return paths (L583 `!s_PreviewRequested \|\| s_PreviewFb < 0` and L633 `mm->curparams == 0`) both bail BEFORE any FBO state mutation. The B-135/B-136 GBI scissor + viewport restoration commands are appended AFTER the framebuffer-target reset (L756) and run in display-list order. | **CONFIRMED** -- restore path is hermetic. |
| j | **Priority M -- `.pdmod`** | `port/src/modmgr.c`, `port/src/modarchive.c`, `port/fast3d/pdgui_theme_loader.cpp`, `port/fast3d/pdgui_theme.cpp`, `port/fast3d/pdgui_font_mod.cpp` | VFS, Property Handler DLL, theme-tool round-trip; B-257 (legacy_backup skip) and B-258 (theme.json from .pdmod archives) fixes present. | **B-257 skip filter present at all four claimed sites:** `pdgui_theme_loader.cpp:1213-1216` (`has_legacy_backup_suffix` + reserved-name table from `MODMGR_RESERVED_NAMES_LIST`); `pdgui_theme.cpp:1378-1381` (chrome-style scanner); `pdgui_font_mod.cpp:202-206` (font scanner); `modmgr.c:980-987, 1485-1493, 1542-1547` (modmgr top-level + alt scan loops). `MODMGR_RESERVED_NAMES_LIST = {"shared", "inbox", "untrusted"}` at `modmgr.h:46`. **B-258 archive enumeration present:** `pdgui_theme_loader.cpp:39` includes `modarchive.h`; `theme_entry.embed_data` field exists at L173; apply path (`pdguiThemeLoadFromCatalog`) checks `embed_data` before `fsFileLoad`. Property Handler DLL at `tools/pdmod_prophandler/` is a separate build target -- not exercised here. | **CONFIRMED** at code level. Property Handler Explorer UX still requires Mike's admin-PowerShell registration step per the verification matrix (M-V3). |
| k | **Priority P -- input mapping menu** | `port/fast3d/pdgui_menu_mainmenu.cpp` (Settings -> Controls -> Input Mapping) | Six-tab outer layout (Mission / Combat Simulator / Vehicle / The Grid / Menu / System); hold-vs-tap rendered as sibling rows; per-IMC reset semantics. | `s_ImcTabs[]` at L1748-1787 enumerates exactly the six claimed tabs. Each tab has its own `s_ResetImcsX[]` array (L1741-1746). `formatRowLabel` (L1798) renders `ACTION_SCORECARD_HOLD` and `ACTION_USE` with `actionmapGetEffectiveHoldMs` suffix, so the hold rows appear as siblings of their tap counterparts in the row list. `NUM_IMC_TABS = sizeof(s_ImcTabs) / sizeof(s_ImcTabs[0])` is computed at L1788 -- safe array sizing. | **CONFIRMED.** |
| l | **Connectivity Phases 2-5** | `port/src/spectator.c` / `theater.c` / `voice.c` / `social_share.c` / `port/include/net/net.h` | Spectator wire packets on `NET_PROTOCOL_VER == 42`; theater recorder file format; social_share UDP 27109; libopus + voice on UDP 27108. | `NET_PROTOCOL_VER 42` at `port/include/net/net.h:12` (with comment dated 2026-04-25 ascribing v42 to "Phase 3+ wire-layer additive"). `voice.c:61 #define VOICE_PORT 27108`; `social_share.c:46 #define SHARE_PORT 27109`. CMake configure log shows `OPUS: Static linking via C:/msys64/mingw64/lib/libopus.a`. Each module exports its own init/shutdown (`spectatorInit/Shutdown`, `theaterInit/Shutdown` at L372/L379, `voice.c` and `social_share.c` similar). | **CONFIRMED at perimeter.** Single-writer claim per module is plausible from API surfaces (each module has its own state and dedicated init/shutdown) but I did not chase global-write call graphs end-to-end -- treat as code-perimeter audit, not a full data-flow audit. Trust-gate continuity for cross-peer mod transfer was not exercised in this pass and would benefit from a focused review. |
| m | **B-254 canvas mode** | `src/game/setup.c::setupCreateProps` + `src/game/body.c` + `src/game/forgemode.c::forgeIsCanvasMode` | `forgeIsCanvasMode()` gates chr/objective spawns; gate is canvas-mode-conditional, not always-on. | `forgeIsCanvasMode` at `forgemode.c:617`. Five gate sites verified: `setup.c:1750` (OBJTYPE_CHR), `setup.c:1774` (OBJTYPE_KEY), `setup.c:1781` (OBJTYPE_HAT), `setup.c:1794` (OBJTYPE_AUTOGUN), `body.c:465` (`bodyAllocateChr` early-return). Each site has the form `if (... && !forgeIsCanvasMode() && ...)` -- preserves existing behaviour when canvas mode is off. `s_forge.canvas_mode` is reset at `forgeTransitionToInactive` (L521) and `forgeInit` (L559) so the latch can never persist across sessions. | **CONFIRMED.** |
| n | **B-255 free-fly relative-to-look** | `src/game/forgemode.c` (freefly move handler) | Yaw/pitch transform path correct; trace log fires once per session, not per-frame. | Forward / right vector math at L389-394 (yaw-only right vector, full forward from camera basis); position update at L417-419 mixes `(fwd * my + right * mx) * speed`. Trace block at L405-415 uses `static bool s_b255_logged = false; if (!s_b255_logged && (mx \|\| my)) { sysLogPrintf(...); s_b255_logged = true; }`. **Comment at L399-401 claims the latch is "cleared in forgeTransitionToInactive (canvas_mode field reset there also serves as the 'session is starting fresh' latch for this diagnostic)" -- but `s_b255_logged` is a function-local static. C function-local statics live for process lifetime; they cannot be reset from another function.** `forgeTransitionToInactive` at L489-521 resets `s_forge.canvas_mode` and other struct fields but does not (and architecturally cannot) reset the function-local static. | **DRIFT-FROM-CLAIM (LOW)** -- trace fires once per **process**, not once per **session**. Mike's expected behaviour ("re-enter the Grid mid-session and get a fresh trace") will not happen. Diagnostic-only impact. |

---

## 3. New bugs found

### NEW-1 [HIGH] -- B-256 missed third read site at `port/src/net/netmsg.c:2128`

**File:** `port/src/net/netmsg.c`
**Function:** `netmsgSvcPlayerStatsRead`
**Lines:** 2127-2133

**Symptom:** When the network replicates a peer's death via `SVC_PLAYER_STATS` with the `isdead` flag flipped on, the local client computes the kill credit by reading the dead `chr->lastshooter` field. Because nothing writes to `lastshooter` (the field is initialised to -1 at chr.c:1340 and never reassigned), the `>= 0` check always fails and the code credits `currentplayernum` -- i.e. records the death as a suicide for whichever player the local current-pointer happens to be set to during stat dispatch.

**Why this is the dominant attribution path in MP:** the chr.c (L967) and player.c (L5908) sites that B-256 patched are local-simulation paths. They handle deaths the local sim originated. The netmsg.c site handles deaths replicated from elsewhere (server fan-out, peer broadcast). In a 2+ peer match, most deaths a given client observes flow through the netmsg path, not the local sim path. So:

- The B-256 commit message claims "Likely closes part of B-249 (kill mis-attribution)."
- The local-only paths it patched probably close ~10-20% of MP cases (you killed a bot you were also locally simulating).
- The 80%+ case (the killfeed showing other peers' deaths) is still wrong because it still reads `lastshooter`.

**Repro contour Mike can run:** in any 3+ peer CS match, watch the killfeed. Any death that originates outside the local client's authoritative simulation will continue to render as a suicide ("Player X killed self") even if Player X was killed by Player Y. After this fix, that string flips to the actual attacker.

**Recommended fix:** apply the same shape as the chr.c / player.c patch at `netmsg.c:2128`:

```c
s16 shooter;
if (pl->prop->chr->lastattacker) {
    shooter = (s16)mpPlayerGetIndex(pl->prop->chr->lastattacker);
    if (shooter < 0) {
        shooter = g_Vars.currentplayernum;
    }
} else {
    shooter = g_Vars.currentplayernum;
}
playerDieByShooter(shooter, true);
```

**Caveat for that fix:** `chr->lastattacker` is set by local damage routing (`chraction.c:4896, 5058`). On a remote peer's chr, `lastattacker` is set only when the local client damaged that peer's chr -- which is the common case in P2P (every client simulates everyone). In a server-authoritative model where a non-server peer never simulates damage to other peers' chrs, `lastattacker` could be NULL on the death frame and the suicide fallback would still fire. If Mike's playtest after a netmsg.c patch still shows mis-attribution, the proper structural fix is to encode the attacker player_id directly in the `SVC_PLAYER_STATS` wire format -- which would also bump `NET_PROTOCOL_VER` to 43.

**Severity:** HIGH. This is the bug Mike just shipped a fix for, and the dominant case it was supposed to close is still broken. It rhymes exactly with the kill mis-attribution Mike's original report named.

---

### NEW-2 [LOW] -- LOG.WPN.DIAG `gate_mode6` / `gate_mode7` columns drift from gate semantics

**File:** `src/game/bondgun.c`
**Function:** `bgun0f0a5550`
**Lines:** 7853-7854 (column emission) vs L7840-7841 (the gate)

**Symptom:** the LOG.WPN.DIAG line still emits `gate_mode6 = (hand->mode == HANDMODE_6) ? 1 : 0` and `gate_mode7` similarly. After the round-2 fix scoped the actual mode=6/7 gate to `inHideTransition && (mode==6 \|\| mode==7)`, these columns no longer answer the question "did the mode gate cause visible=false?" -- they answer "was hand->mode equal to 6 or 7?", which is now the OR of "the gate fired" and "the gate didn't fire because state wasn't CHANGEGUN/LOWER/RAISE".

**Why it matters:** Mike has been using these logs to fingerprint the wedge. Before round-2, "mode6=1" and "visible=false" were equivalent. After round-2 they are not. A future log with `mode6=1 noFlag40=0 flag80=0 notLoaded=0 notInuse=0 memType0=0` may now legitimately have `visible=true` (because `inHideTransition` is false), and Mike could spend time re-investigating the mode gate when there is no longer a problem there.

**Recommended fix:** change the gate-flag columns to reflect the actual gate predicate. Option A: emit `gate_mode_in_transition = (inHideTransition && (mode==6 \|\| mode==7)) ? 1 : 0` and drop `gate_mode6` / `gate_mode7`. Option B: keep both raw mode6/mode7 (for diagnosis) but add an `in_transition` column. Option B preserves prior diagnostic continuity.

**Severity:** LOW. Diagnostic-only; doesn't change runtime behaviour.

---

### NEW-3 [LOW] -- B-255 trace fires once per process, not per session

**File:** `src/game/forgemode.c`
**Lines:** 396-415, comment at 396-404 vs static at 406

**Symptom:** the comment says the trace "fires once per session activation; cleared in forgeTransitionToInactive." The implementation uses a function-local `static bool s_b255_logged`. Function-local statics in C have process lifetime; `forgeTransitionToInactive` at L489-521 cannot reach this variable.

**Recommended fix:** Either (a) move `s_b255_logged` to module scope (or into `s_forge.fly` or another struct field that `forgeTransitionToInactive` resets), and reset it from `forgeTransitionToInactive`; or (b) update the comment to reflect what the code actually does ("fires once per process").

**Severity:** LOW. Diagnostic-only.

---

### NEW-4 [LOW] -- `devtools/build-headless.ps1` crashes on configure step

**File:** `devtools/build-headless.ps1` (or its prelude)
**Symptom:** PowerShell unhandled exception "There is no Runspace available to run scripts in this thread" during the Configure (CMake + Ninja + ccache) step. The host crashes; no `Build/` artefacts are produced; OS reports exit 0 making the failure invisible to scripts that check exit codes.

**Workaround:** the bash path (`source devtools/build-env.sh && ninja -C Build pd pd-server`) succeeds.

**Recommended fix:** investigate the PowerShell try/catch block that's invoking a script block off-thread; likely a Start-Job / Start-ThreadJob pattern that lost its runspace. Out of scope for this audit; flagged so the next dev-window run doesn't silently produce a stale Build dir. Mike's automation that depends on `build-headless.ps1` should be considered untrusted until this is fixed (the dev-window auto-commit pipeline likely catches build failures via other means, but worth confirming).

**Severity:** LOW. Affects tooling, not gameplay.

---

## 4. Drift findings (claims that don't match code)

The drift findings are itemised in the per-item table column "Status" wherever it reads DRIFT-FROM-CLAIM. Summarised here:

| Drift ID | Claim | Reality at HEAD | Severity |
|---|---|---|---|
| **DRIFT-1** (item c) | B-256 commit: "replace the check at both sites." | Three read sites of the dead `lastshooter` field exist; only two (`chr.c`, `player.c`) were patched. `netmsg.c:2128` -- the network death-attribution path that drives MP killfeed -- still reads the dead field and falls through to suicide credit. Filed as **NEW-1 (HIGH)**. | HIGH |
| **DRIFT-2** (item e) | K design: "`inputCtxSyncMouseMode` becomes sole authority." | Four `SDL_ShowCursor` callers exist in port code: `inputctx.c:491-499` (intended sole authority), `input.c:1215`, `gfx_sdl2.cpp:261-263`, `pdgui_backend.cpp:822` (B-92 hotswap). All three secondary callers have guards that make them functionally inert in normal use, but the "sole authority" framing is overstated and the secondary paths are real. | MEDIUM |
| **DRIFT-3** (item a.1) | (Implicit, from instrumentation history) LOG.WPN.DIAG `gate_mode6` / `gate_mode7` columns indicate when the mode gate fires. | After round-2's `inHideTransition` scoping, those columns now indicate `mode == 6 \|\| 7` regardless of whether the gate actually fired. Filed as **NEW-2 (LOW)**. | LOW |
| **DRIFT-4** (item n) | B-255 comment: "fires once per session activation; cleared in forgeTransitionToInactive." | Function-local static `s_b255_logged` has process lifetime; the documented session-reset is unreachable. Filed as **NEW-3 (LOW)**. | LOW |

---

## 5. Severity assessment

| Severity | Items |
|---|---|
| **HIGH** | NEW-1 (B-256 third-site miss in `netmsg.c:2128`) |
| **MEDIUM** | DRIFT-2 (K cursor sole-authority overstated) |
| **LOW** | NEW-2 (LOG.WPN.DIAG column drift), NEW-3 (B-255 process-vs-session latch), NEW-4 (build-headless.ps1 crash) |

Notable absence of new HIGHs against the heavy-construction items: J (IMC), L (flat menu), N (bot tick), O (capsule), Q (FBO), M (.pdmod), P (input mapping), B-254 (canvas), Connectivity 2-5 perimeter. These all read clean against their stated design.

The single HIGH finding is one missed call site in a fix that itself is structurally correct. Cost to remediate is small (one if-branch, no protocol change required for v1).

---

## 6. Recommendations to Mike before he playtests

### Highest priority (do before playtest if you can spare 10 min)

1. **Patch `port/src/net/netmsg.c:2127-2133` to read `chr->lastattacker` instead of `chr->lastshooter`.** Same shape as the chr.c / player.c patch landed by `7fbc5833`. Without this, the killfeed in any 2+ peer match will continue to show suicide credit for non-self deaths -- exactly the symptom B-256 was filed to close. Build-verify after; no protocol change. If after the patch the killfeed *still* mis-attributes in your playtest, the proper structural fix is wire-side attribution in `SVC_PLAYER_STATS` (which is a v42 -> v43 bump and a separate work item).

### Watch fors during playtest (informational, not blockers)

2. **LOG.WPN.DIAG column meaning has shifted.** If you see `mode6=1` or `mode7=1` in the gate-fail log AND `visible=true` is rendering, that's not a regression -- it just means the new `inHideTransition` scope correctly let the weapon render. Don't chase it.

3. **B-255 trace will not re-fire on Grid re-entry within the same launch.** If you want a fresh trace, kill and relaunch the client. (Or apply the LOW fix from NEW-3.)

4. **K cursor: if you ever see cursor flicker mid-menu, take a screenshot of the active menu name and the prior input action.** The "sole authority" framing is overstated; if some path in `input.c::inputMouseShowCursor` or `gfx_sdl_set_cursor_visibility` slips through its guard, the symptom would resurface. Not expected to repro in the steady-state playtest, but worth keeping a finger on the pulse.

### Tooling follow-up (deferrable)

5. **`devtools/build-headless.ps1` is currently broken.** Use the bash path until investigated: `source devtools/build-env.sh && ninja -C Build pd pd-server`. The PowerShell host crashes during configure but reports exit 0, which is the worst possible failure mode -- silent. Not gating playtest.

### Items NOT worth re-litigating in this window

The day's J / K / L / M / N / O / P / Q / B-254 / Connectivity-2-5 / B-247 work is structurally correct. The only file in that batch that needs another pass is `netmsg.c` for the B-256 miss. Everything else is shippable as-is for tonight's playtest.

---

## 7. Verification limits

- **Static-only.** No live multi-peer playtest performed. Single-writer claims for spectator / theater / voice / social_share were sampled at API perimeter, not chased through full data-flow.
- **`Property Handler DLL` UX (Priority M, M-V3) was not exercised.** The DLL builds and exports the right entry points (per the verification matrix) but the Explorer-right-click flow needs Mike's admin-PowerShell registration step to validate end-to-end.
- **Trust-gate continuity for cross-peer mod transfer (Connectivity Phase 4) was not exercised.** The verification matrix exercised the local trust gate (M-V5 shared-inbox skip); the cross-peer flow needs a second peer to validate.
- **B-256 missed-site fix not validated by this audit (correctly -- that fix is the next session's work, not this one's audit).** Per assignment: "Read-only audit; don't modify code unless you find a critical regression." NEW-1 is HIGH but not critical-by-Mike's-definition (the build is green; the bug is a continuation of an existing UX issue). Flagged for explicit decision.

---

## 8. Summary scorecard

| Dimension | Score (0-10) | Rationale |
|---|---|---|
| Implementation discipline against stated design | 8.5 | One missed read site in a four-line fix; everything else lands clean. |
| Documentation accuracy | 7.5 | Two LOW DRIFTs in comments / instrumentation columns; one MEDIUM in a design doc claim. |
| Security / trust posture | unchanged | No regression introduced; v42 wire additive only; trust gate continuity preserved at the two sampled boundaries. |
| Build / tooling | 6.5 | Bash build clean. PowerShell headless build crashes but reports exit 0 -- needs fix. |

---

## 9. Findings tally

| Severity | Count | IDs |
|---|---|---|
| Critical | 0 | -- |
| High | 1 | NEW-1 |
| Medium | 1 | DRIFT-2 |
| Low | 3 | NEW-2, NEW-3, NEW-4 |

---

*End of report. Read-only audit; no code changes made. Mike approves remediation scope before any of the above are addressed.*
