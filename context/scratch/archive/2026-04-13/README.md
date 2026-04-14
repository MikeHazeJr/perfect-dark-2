# 2026-04-13 — Stabilization Drop (scratch archive)

Archived after S253. All work shipped to `dev`; living files updated. This index
points each forensic artifact at the bug/feature it documents and the commit
that landed the fix.

## Shipped fixes (by bug/feature → commit → scratch file)

| Bug / feature | Commit on `dev` | Session | Forensic detail |
|---|---|---|---|
| B-134 spawn validator railing trap (`SPAWNPOOL_CAPSULE_RADIUS=30`) | `68207bae` | S249 | — (diagnosis in session-log S249) |
| B-135/B-136/B-137/B-138/B-139 mp-lobby + mod persistence + countdown linger + songs sort | `8e02a2ef` | S248 | — (post-session dumps superseded by S251-res) |
| B-140 Issue A: playlist auto-advance broadcast (`NETMODE_SERVER_AUDIO 2→1`) | `e13c2d1f` | S249 | `session-state-mp-lobby-mod.md` (hand-off from S249 to S251) |
| B-End-Game-Crash (A) + modal-confirm UX (B): `manifestClear` in `netDisconnect` | `d37e9677` | S252 | `session-state-endgame-crash.md` §4a/§6 |
| B-142 false-kills NULL-guard on `mpPlayerGetIndex` | `4d1e13c1` | S252 | `session-state-endgame-crash.md` §4d (open list) |
| B-141 audio telemetry (counters + CVar + 30 s auto-summary) | `5a42f234` | S251 | — (open, waiting for repro) |
| S250 input-authority Phase 1 (`gameplayInputSuppressed()`) | `5098f903` | S250 | `../../../designs/input-authority-and-menu-pool-2026-04-13.md` (ADR) |
| Dev-window-v2 font/control size polish | `11fd1d5e` | S248 (parallel) | `session-state-devwindow-tiny-text.md` |
| S253 MP lobby residual: Issue 7 (`SVC_ROOM_SETTINGS`), Weapons F-2.1, Issue 2/8 theme rescan, B-140 Issue B two-panel Select Tunes | `82d0c1f3` → `287b0bc4` | S253 | `session-state-mp-lobby-residual.md` |

## Investigation artifacts (no fix landed; open items carried in tasks-current.md)

- `session-state-endgame-crash.md` §4c — **Bug C** (post-game endscreen partial
  render). Six active hypotheses. Scrim + title bar render but content does not;
  needs instrumentation in `renderMpEndscreen`.
- `session-state-endgame-crash.md` §4b — **Airbase 0xc0000005 Start-Match crash**
  status unclear; may share root cause with Bug A (fix shipped in `d37e9677`).
  Needs post-drop repro.
- `manifest-gap-report-2026-04-13.md` — three manifest gaps:
  1. Title/menu stage has no manifest at all (Skin Editor mod chars missing)
  2. SP manifest pre-scan fires before `g_StageSetup.props` populated (B-118
     class — partially mitigated by `manifestSPRescanSetup`)
  3. Cutscene cinema models never entered in manifest at all

## Stale snapshots (retained for completeness, superseded by living files)

- `session-briefing-2026-04-13.md` — Cowork automated briefing dated 2026-04-13T00:00.
  Point-in-time snapshot at `bfa578a4`. Superseded by session-log.md from
  S238 onward. Historical interest only.
- `spawn-smoke-test-results-2026-04-13.md` — S246 M-7.x smoke-test
  infrastructure docs + Mike's run-all instructions. CSV collection is
  ongoing; the document text is stable.

## Open bugs carried forward (see `../../../bugs.md`, `../../../tasks-current.md`)

- Bug B — server countdown lingers after room closes (fix in
  `readyGateTickCountdown()`)
- Bug C — post-game endscreen partial render
- Bug D — invisible networked bots in Chicago (may have cleared with S249/S253 drop)
- Issue 4 — Airbase Start-Match no response
- B-141 — audio skips, waiting for repro against telemetry in `5a42f234`
- Input-authority Phase 2 (menu pool single-instance discipline)
- Playtest-verify the whole 2026-04-13 drop
