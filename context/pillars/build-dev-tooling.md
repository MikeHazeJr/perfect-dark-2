# Build / Dev Tooling

> CMake + MSYS2/MinGW + Ninja. Active targets: `pd` (game), `pd-tests` (Catch2), and `pd-updater` (Updater.exe). Standalone `pd-server` / `PerfectDarkServer.exe` is removed/deprecated; listen-host inside the client is the server path. Static linking; only `opengl32.dll` is dynamic. Headless build wrapper + per-session isolated builds + queued watchdog. Self-updating release pipeline with Ed25519 signing.

---

## What it is

The dev tooling is the bridge between AI sessions writing code and Mike running builds. AI does NOT compile in production; AI writes and uses the headless / session-isolated build wrappers when verification is needed; Mike builds the player-facing build via Dev Window v2 and tests in-game.

Code:

- Build entry: [devtools/build-headless.ps1](../../devtools/build-headless.ps1) (715 lines).
- Per-session isolated build: [devtools/build-session.ps1](../../devtools/build-session.ps1) (847 lines).
- Bash equivalent prelude: [devtools/build-env.sh](../../devtools/build-env.sh).
- PowerShell prelude: [devtools/_build-env-prelude.ps1](../../devtools/_build-env-prelude.ps1).
- Dev Window v2 (WPF GUI): [devtools/dev-window-v2/](../../devtools/dev-window-v2/) plus [devtools/dev-window-v2.ps1](../../devtools/dev-window-v2.ps1).
- Release pipeline: [devtools/release.ps1](../../devtools/release.ps1) (1021 lines).
- Test runner: [devtools/run-pd-tests.ps1](../../devtools/run-pd-tests.ps1) (sets canonical build env, suppresses Windows loader popups, runs isolated `pd-tests.exe`).
- Update system: [port/src/updater.c](../../port/src/updater.c), [port/src/updater_standalone/](../../port/src/updater_standalone/).
- Configuration audit: see Configuration section below.

---

## Build environment invariants

Per [procedures.md](../procedures.md) and [CLAUDE.md](../../CLAUDE.md):

- `TEMP=C:\Users\mikeh\AppData\Local\Temp` and `/c/msys64/mingw64/bin` prepended to PATH.
- `CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime`, `CCACHE_BASEDIR` set.
- Both [_build-env-prelude.ps1](../../devtools/_build-env-prelude.ps1) and [build-env.sh](../../devtools/build-env.sh) are idempotent and dot-sourced everywhere. Do not rediscover env, do not invent alternatives.

From bash:

```bash
source devtools/build-env.sh && ninja -C Build pd pd-tests
```

From PowerShell:

```powershell
.\devtools\build-headless.ps1   # self-configures env
```

---

## Build wrappers

### `build-headless.ps1`

Canonical headless build entry. Smart-clean detection (compiler change, generator change, missing CMakeCache, `-Clean` flag). ccache probe before launcher inject. State persists in `Build/.last_build_state.json`. Writes per-step stdout/stderr/exit-code logs at `_build-headless-<timestamp>-<step>.*`. Heartbeat with PID list + ninja_log size. Captures CMake error log on configure failure. Self-test mode (`-SelfTest`) verifies the wrapper itself.

**SP-9 truncation guard** (S190 implementation, [build-headless.ps1:763-804](../../devtools/build-headless.ps1:763)). Pre-commit `git diff HEAD --numstat` check. Fires when net delta < -20 lines AND additions < 1/3 of deletions. Aborts auto-commit, names suspect files, prints restore command. Build continues from working copy. Tested: fires on -95 net (0+/95-); silent on -49 net with 41 additions (intentional rewrite, correct no-fire).

### `build-session.ps1`

Per-session isolated build. Each session routes to `.claude/session-builds/<session-id>/` via the canonical headless build script. Per-session locking, watchdog, PID tracking, queue. `-Tail`, `-List`, `-Remove` operations.

Queued by default (S579). Sessions keep isolated build trees but only one heavy build runs at a time; waiting sessions get queue position, active elapsed time, estimated wait, and waited-time status.

S582 added the active-build watchdog and observable logs: queued child builds default to a 60-second timeout, stop the child process tree on timeout, clear the active queue slot, and return exit code `124`. New wrapper starts also capture stdout/stderr to `_build-session.out.log` / `_build-session.err.log`, visible through `-List` and `-Tail`.

**Use `build-session.ps1` for AI verification.** Do not use shared `Build/` for AI runs when parallel sessions may build. Reserve `-NoQueue` or `-BuildTimeoutSeconds 0` for intentional manual bypass only.

### Dev Window v2

WPF GUI (`devtools/dev-window-v2/`) for build / run / version / status / git Pull and Push. DPI-aware (S255). Async RunspacePool (1-3 threads) for non-blocking UI updates (S361). Pre-build git sync (S257) via `Invoke-GitSyncBeforeBuild`. Single-exe consistency rules (S270).

S570 made the Push button stage-commit-push-refresh in one click; warm BUILD/RUN TESTS paths skip configure when cache/version/Python tool are current; CMake builds in parallel; addin data mirrored with `robocopy` when available.

B-348 crash/freeze evidence: Dev Window v2 persists the Log tab stream to `devtools/dev-window-v2/dev-window-v2-console.log` before UI filtering. Launch rotates the previous logs to `.1` and `.2` for three total rolling logs. These generated logs are ignored by git and must not be tracked, or Release can dirty the repo during `git pull --rebase`. Use this file first when Release appears stuck or the window crashes; it should contain the last build/release subprocess lines and Dev Window heartbeats.

**Clear Worktrees button** (`flamboyant-ride-cc996f`, 2026-05-11): the prior "Prune Worktrees" button ran only `git worktree prune -v`, which is the registry-side garbage collector and does NOT remove on-disk worktree directories or their branches. Replaced with a "Clear Worktrees..." button that opens a modal WPF dialog (DataGrid with checkbox + Name + Branch + Last Modified + Size + total). Selected entries go through `git worktree remove` -> `--force` -> `Remove-Item -Recurse -Force` -> `git branch -D <branch>` -> optional `git worktree prune -v`. Async runspace enumeration; per-row freed-byte report logged + Done dialog with totals. `PD2V2.WorktreeEntry` (INotifyPropertyChanged class) added to the consolidated `Add-Type` block so the DataGrid two-way binding for Select All / Select None works. Active-worktree detection (`Get-Location` at click time) gates Select All / refuses Clear for the running dev-window's own worktree. Verified end-to-end via `.claude/scratch/test-worktree-clear-e2e.ps1` (untracked) against the real on-disk set. Array-literal subtlety locked in: inside `@(...)`, `"x" + $y` is parsed as TWO elements (string + unary-+ var) because comma binds tighter than `+`; concat must be parenthesised inside an element. Source: `Invoke-GitPruneWorktrees` (function name kept for compatibility with the BtnPruneWorktrees XAML element + the Enable/Disable call sites elsewhere in the script).

---

## Release pipeline

[devtools/release.ps1](../../devtools/release.ps1) (1021 lines). Version sync (max of CMakeLists + git tags + 1 patch). Dual-channel (stable / nightly prerelease). gh CLI integration. Dev-tag prune (newest 10). No-BOM UTF-8 CMakeLists rewrite. `-DryRun`, `-SkipBuild`.

Pre-commit uses `git diff --cached --quiet` check before `git commit` (no longer relies on `git status --porcelain` alone, and does not swallow commit failures, S257).

S2026-05-18 B-336: release and Dev Window auto-sync commits must satisfy the c120 commit-message hook. Automated release/pre-build/pre-rebase commits use `Tooling - c120: ...` subjects, explanatory bodies, and `Refs: c120`; Dev Window build/release/push sync commits use the same full message shape. No release path should emit `chore:` auto-commit subjects.

Push step uses `git pull --rebase`. A non-clean index makes that step fail with "cannot pull with rebase: Your index contains uncommitted changes"; Dev Window v2 calls `Invoke-GitSyncBeforeBuild` first to avoid that.

Release zip contents:
- `PerfectDark.exe` (the game)
- `PerfectDarkServer.exe` is no longer built or bundled.
- `Updater.exe` (the standalone recovery updater)
- `cacert.pem` (Mozilla CA bundle)
- License notices
- ROM is excluded
- Source archive generated via `gh`

---

## Update system

[port/src/updater.c](../../port/src/updater.c). libcurl HTTPS to GitHub Releases API. Mini JSON tokenizer (same pattern as `savefile.c`, `modmgr.c`). SDL_mutex-protected shared state. SDL_atomic cancel flag (SEC-23).

**Mozilla CA bundle embedded** as `cacert_blob.h` to escape `SSL_CTX_load_verify_store` issues on MSYS2 libcurl (S360 fix; root-cause: MSYS2 libcurl.a without that function).

**Ed25519 signature verification** via `ed25519.h` and embedded `updater_pubkey.h`. Release zips require `.sha256` and `.sig`; updater verifies Ed25519 over `sha256(zip)||tag` with the embedded public key and runs a self-test at init.

B-337 guardrail (2026-05-18): `sign-release.ps1` refuses to sign when the selected private key's derived public key differs from `port/include/updater_pubkey.h`. The GitHub release can have valid-looking sidecars and still fail every updater if this key pair diverges, so signing-key/header mismatch is a hard release blocker.

B-338 guardrail (2026-05-18): updater stale-file cleanup must preserve root-level BYOR ROM files (`*.z64`, `*.v64`, `*.n64`) in both the in-game updater and standalone `Updater.exe`. This protection is structural, not configurable: it applies even when `pd.ini` is absent or `Update.ProtectedFolders` is stale. Release zips exclude ROMs, so without this rule a root ROM absent from staging is misclassified as stale and can be deleted. Already-published releases with the old cleanup remain unsafe until superseded by a fixed newer prerelease; confirmed exposed prereleases are v0.0.199 and v0.0.200, and the rolling prerelease prune should retire them as newer dev builds ship.

B-349 guardrail (2026-05-19): all client/updater logs live under root `logs/` and updater cleanup protects that folder as user/runtime state. Client logs route to `logs/game client/`; standalone updater logs to `logs/updater/pd-updater.log`. Updater cleanup paths log protected skips and stale deletions so future ROM-preservation reports have direct evidence.

Two channels: stable and dev (`Updates.ShowDevReleases` config).

`UPDATER_DEFAULT_PROTECTED = "mods,data,extracted,saves,logs"` - directories preserved across self-update.

**Standalone `Updater.exe`** at [port/src/updater_standalone/updater_gui.c](../../port/src/updater_standalone/updater_gui.c). Recovery path if in-process self-update breaks. Packaged into release zips per `release.ps1`.

D13 update system status: shipped per S245 FIX-F (curlGet returns HTTP code, 403 rate-limit path, fsFullPath fallback for empty installDir, 1MB min size check on extracted exe).

---

## Configuration

Three tiers per `config-pd-ini-audit.md` (last refreshed S313):

1. **`pd.ini`** - user-mutable game-wide settings. Registered via `configRegister*` (Int / UInt / String / Float / Bool).
2. **Per-agent `prefs_agent.ini`** - sidecar to each agent save. Audio volumes, control prefs (S313 introduced, S305 + S313 designs).
3. **Compile-time** - constants in headers; for tuning knobs, not user prefs.

Decision flowchart in `config-pd-ini-audit.md` (file is being folded into this doc per the rebuild plan; the audit content itself moves into `_old/audits/`).

`Net.Server.Port` (pd.ini) is the default listen-server UDP port for in-client hosting; CLI `--port` still overrides in `netInit()` after config load.

---

## Static linking

[CMakeLists.txt:301](../../CMakeLists.txt:301) statically links SDL2 from MSYS2; [:393](../../CMakeLists.txt:393) statically links zlib; [:411](../../CMakeLists.txt:411) statically links libcurl with the full TLS chain (libssl, libcrypto, libnghttp2/3, brotli, idn2, psl, zstd). Embedded Mozilla CA bundle at [:163](../../CMakeLists.txt:163). [:830](../../CMakeLists.txt:830) confirms `opengl32.dll` is the only allowed dynamic dep.

---

## Active invariants

Per [procedures.md](../procedures.md) and [constraints.md](../constraints.md):

- **All builds are clean builds.** "Clean Build" toggle was removed; every build deletes build dirs before CMake configure. Eliminates CMake CACHE stale-value bugs (B-22).
- **Worktree builds are forbidden.** `build-headless.ps1:85-92` redirects worktree paths to main project; same logic in `build-session.ps1` and `cursor-build.ps1`. Never run cmake directly from a worktree.
- **AI build verification uses `build-session.ps1`** with isolated `.claude/session-builds/<id>/`. Do not use shared `Build/` when parallel sessions may build.
- **Reuse one `-Session` id** only inside the same active session; parallel sessions must use different ids; the wrapper holds a lock and fails fast on accidental reuse.
- **Cleanup**: `.\devtools\build-session.ps1 -Remove -Session <id>` after a session finishes; `-List` to inspect old directories.
- **Zero-DLL release**: all libs statically linked; opengl32.dll is the only allowed dynamic dep. Tested by absence of unexpected `LoadLibrary` calls.
- **Pre-build git sync via Dev Window** (`Invoke-GitSyncBeforeBuild`); avoid leaving staged edits mid-pipeline.
- **No em-dashes** in `.ps1` files (Windows-1252 encoding causes silent truncation, see SP-9 Mode A).
- **No hooks bypass** (`--no-verify`, `--no-gpg-sign`) unless Mike explicitly authorizes.
- **Ed25519 keypair generation is automated** on first build ([build-headless.ps1:852-864](../../devtools/build-headless.ps1:852)).

---

## What is done

Per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 7:

- Build invariants encoded once in `_build-env-prelude.ps1`; dot-sourced everywhere.
- Self-test mode for the wrapper (`build-headless.ps1 -SelfTest`).
- SP-9 truncation guard prevents auto-commit when files shrink unexpectedly.
- Per-step exit-file logging; killed wrapper still leaves diagnosable artifacts.
- ccache probe avoids hangs in sandboxed environments.
- Per-session isolated builds eliminate shared-`Build/` race.
- Release pipeline builds the client and `Updater.exe`, excludes ROMs, and generates source archives. Standalone `pd-server` is not built or shipped.
- All scripts redirect worktree paths back to main working copy.
- Ed25519 keypair generation automated.
- No-BOM UTF-8 CMakeLists writer prevents the dirty-tree-after-release bug.
- D13 update system shipped end to end (Mozilla CA bundle embedded, signature verification, self-test, dev/stable channels, standalone Updater.exe recovery).

---

## Known gaps

- **`pd_headers` generation step is watchdog-prone.** Multiple recent task entries (S587-S590) report `pd_headers` ninja step running 478-589 seconds against a 600s watchdog with empty stderr. Per [build-headless.ps1:902-910](../../devtools/build-headless.ps1:902), `pd_headers` is invoked with `-j1 -v` (intentional, single-job avoids generated-header race conditions). The watchdog may be hitting it because some header-generation tool blocks on Python or Git invocation rather than running. Investigate; surface real progress in heartbeat.
- **Multi-thousand-line PowerShell scripts.** `build-headless.ps1` 715 lines, `release.ps1` 1021 lines. Mostly appropriate for the surface area; logic like the SP-9 guard is in-line in `build-headless.ps1:763-804` rather than in a sourced helper. Future bug in the regex would silently change the guard. Factor into `version-util.ps1` or new `_build-safety.ps1`.
- **No code-level test for the build scripts.** PowerShell parser checks happen; `-SelfTest` covers limited regression. Smart-clean heuristic, ccache probe path, SP-9 guard are not unit-tested.
- **Worktree-redirect logic is duplicated** in 3 scripts. Centralize.

---

## Active design references

None. Build tooling is operational, not under active design. Changes go directly via Dev Window v2 / `build-headless.ps1` PRs.

---

## Where to look

- For tests + Catch2 + scope aliases: [pillars/tests.md](tests.md).
- For procedures (build verify, git safety, worktree rules): [procedures.md](../procedures.md).
- For Mike's working preferences (when to commit, push, merge): [working-preferences.md](../working-preferences.md).
- For SP-9 truncation context: [systemic-bugs.md](../systemic-bugs.md).
