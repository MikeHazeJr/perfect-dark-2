# Game Loop Sweep — 2026-04-16 (S303, reverent-chatelet)

> Campaign / Co-Op / Counter-Op full game loop audit — fixes + structured
> logging. See session-log.md S303 and tasks-current.md S303 follow-up.

---

## Scope delivered

End-to-end trace of the three gameplay modes (Campaign solo, networked
Co-Op, networked Counter-Op), the manifest pipeline between missions,
starting-weapon flow, and menu bookends. Fixes for 7 issues found during
the trace, plus a unified `GAMELOOP.*` log tag family so the next playtest
can audit each loop without additional instrumentation.

Build verified — `PerfectDark.exe` 51,564,390 / `PerfectDarkServer.exe`
22,837,840 bytes. Only pre-existing warnings (`/*` in comment in
`pdgui_theme.h`, `pdgui_bridge.c`; cast warning in `mainmenu.c` 2P anti
menu handler; `chraction.c` maybe-uninitialized fam).

---

## Findings by mode

### 1. Campaign (Solo)

**Entry path**: `pdgui_menu_solomission.cpp:629 renderMissionSelect()` →
`pdgui_menu_solomission.cpp:1231-1254` Start Mission CTA →
`mainmenu.c:728 menuhandlerAcceptMission(MENUOP_SET)` →
`mainmenu.c:800 mainChangeToStage(resolved_stagenum)`.

**Stage-load pipeline**:
1. `mainChangeToStage` at `pdmain.c:744`
2. SP branch (`g_ClientManifest.num_entries == 0`) →
   `manifestSPTransition(stagenum)` at `pdmain.c:784`
3. Stage load proceeds, then `lvInit` fires `manifestSPRescanSetup` at
   `lv.c:533` for Phase 2 rescan (captures CHR/prop/cinematic spawns
   from `g_StageSetup.props`+`intro`+`ailists`)

**Endscreen**:
- Trigger: `mainEndStage` at `pdmain.c:698`. Solo branch (no
  coop/anti/normmp) → `endscreenPrepare()` at `:731`
- Render via `renderSoloEndscreen` at `pdgui_menu_endscreen.cpp:398`
- Buttons → `pdgui_bridge.c`:
  - `pdguiEndscreenStartMission` (retry): `menuhandlerAcceptMission`
  - `pdguiEndscreenNextMission`: `endscreenAdvance()` +
    `menuhandlerAcceptMission`
  - `pdguiEndscreenExitToMainMenu`: `manifestClear` + `menupoolReleaseAll`
    + ctx pop + `func0f0f8120()` (transitions to CITRAINING)

**Starting weapons**: Come entirely from `INTROCMD_WEAPON` in the setup
file, processed in `playerreset.c:200-227`. No per-mission catalog config;
each stage's intro block defines the full loadout. Non-anti players only
(anti gets weapons from possession).

**Gaps found**:
- **Legacy `MENUROOT_ENDSCREEN`** in `menutick.c:687-689` transitions to
  `STAGE_TITLE` rather than `STAGE_CITRAINING` on non-restart exit. Left
  in place (likely dead code per S262 audit).
- **SP manifest stale-leak risk**: `pdmain.c:764-768` branches on
  `num_entries > 0` — if a prior MP teardown missed `manifestClear`, an
  SP transition would misroute to `manifestMPTransition`. **Fixed with
  warning log** in Fix 3 so the leak is immediately visible in `pd.log`.

---

### 2. Co-Op

**Start sequence**: Lobby leader → `CLC_LOBBY_START` with gamemode=COOP
(`netmenu.c:215` → `netmsg.c:4007`). Server read at `netmsg.c:4374`,
co-op branch `:4843-4906`. Server-side `manifestBuild(&g_ServerManifest,
NULL, NULL)` (`:4859`) → broadcast `SVC_MATCH_MANIFEST` → ready gate →
on all-ready `netServerCoopStageStart(stagenum, difficulty)` at
`net.c:787`. Clients receive `SVC_STAGE_START`, process co-op branch at
`netmsg.c:1192-1250`, send `CLC_STAGE_READY`.

**Both-player manifest coverage**: `manifestBuild` at `netmanifest.c:465-498`
iterates all `g_NetClients[]` in `CLSTATE_LOBBY|GAME` and adds each
client's `body_id`/`head_id` via catalog resolve. Both players captured.

**Spawn**: Co-op uses `scenarioChooseSpawnLocation` which falls through
to `playerChooseGeneralSpawnLocation` using `g_SpawnPoints[]` from
`INTROCMD_SPAWN`. `playerTrySelectPoolSpawn` is gated on
`g_Vars.mplayerisrunning` (false for co-op) so the spawn pool isn't used.

**Completion flow**: `mainEndStage` → `endscreenPushCoop` (`pdmain.c:712`)
pushes `g_2PMissionEndscreen{Completed,Failed}{H,V}MenuDialog`. Deep Sea
case: `menutick.c:588-611` auto-advances stageindex + `manifestClear` +
`mainChangeToStage`. All other stages: endscreen push → user picks
Retry/Exit → handled by `pdgui_bridge.c` bridge paths.

**`manifestClear` coverage** (critical for SP-13/Bug A class):

| Site | File:Line | Status |
|------|-----------|--------|
| Match end (server → client) | `netmsg.c:1471` | L1-1 — pre-existing |
| Deep Sea auto-advance | `menutick.c:610` | S298 — pre-existing |
| Endscreen exit | `pdgui_bridge.c:847` | F-0.4 — pre-existing |
| Disconnect | `net.c` netDisconnect | Bug A — pre-existing |
| **MPENDSCREEN restart-level (coop/anti)** | `menutick.c:~715` | **S303 — NEW** |
| **MPENDSCREEN → CITRAINING exit** | `menutick.c:~740` | **S303 — NEW** |
| **COOPCONTINUE exit** | `menutick.c:~770` | **S303 — NEW** |

**Gaps found**:
- **GAP-B co-op telefrag** — If a coop mission declares only 1
  `INTROCMD_SPAWN`, both P1 and P2 spawn on that pad (`chrCompareTeams`
  skips same-team filtering; pool gated off for coop).
  **Partial fix**: log a WARNING (`GAMELOOP.COOP: only %d spawn pad(s)
  declared...`) in `playerreset.c` when `g_NumSpawnPoints < 2` in coop
  mode so playtest can identify offending stages. A full fix would need
  coop-aware pad scoring or additive pad-file fallback — queued.
- **GAP-A co-op manifest ignores host payload** — `netmsg.c:4859` calls
  `manifestBuild(&g_ServerManifest, NULL, NULL)` unconditionally.
  Combat-Sim path accepts the CLC_LOBBY_START payload via
  `manifestDeserialize` (`:4706`); co-op does not. Logged visibly via new
  `GAMELOOP.COOP: server-side manifest built entries=%d hash=0x%08x
  stage='%s' clients=%u` line so host/server state divergence is
  measurable.

---

### 3. Counter-Op

**Anti-player selection**: Host picks in room Counter-Op tab
(`pdgui_menu_room.cpp:2187-2284`), Start click sets `s_CounterOpClientId`,
sent via CLC_LOBBY_START `antiClientId` byte (`netmsg.c:4391`). Server
validates (`:4533-4553`): in-range, state ≥ LOBBY, same room as leader.

**Server resolves antiplayernum**: `netServerCoopStageStart` at `net.c:787`
scans `g_NetClients` for id matching `g_NetCounterOpClientId` and copies
its `playernum` to `g_Vars.antiplayernum`. **Fallback**: if unresolved,
defaults to slot 1 (or -1 if singleton).

**Wire rebroadcast**: `SVC_STAGE_START` write at `netmsg.c:774` includes
`g_Vars.antiplayernum` (v36, u8 field). Clients consume at `netmsg.c:988
antiPlayerNumWire`, apply at `:1202-1209`. So anti identity IS broadcast;
the previous agent report's "no wire rebroadcast" concern was incorrect.

**Anti body/head**: Anti player's body/head aren't in the pre-load
manifest — they come from possession at runtime (`player.c:1262-1303`
copies possessed chr's `weapons_held` + body/head into anti). The
`netmanifest.c:1317-1345` post-load path adds `antibodynum`/`antiheadnum`
if catalog-resolvable — skips silently otherwise.

**Anti spawn**: Same `scenarioChooseSpawnLocation` path as coop. No anti-
specific spawn list.

**Anti weapons**: `INTROCMD_WEAPON` is gated on `PLAYER_IS_NOT_ANTI` —
anti gets nothing from intro, all weapons come from possession.

**Endscreen**: Reuses `g_2PMissionEndscreen{Completed,Failed}{H,V}MenuDialog`
via `endscreenPushAnti`. Win/loss attribution: if
`!ANTI_ABORTED && (bond dead || bond aborted || !objectives complete)`
then failed-for-bond / completed-for-anti; else vice versa. Uses
`enscreenAnyAntiAborted()` (`MAX_COOPCHRS > 2`) which has a NULL-guard on
`g_Vars.antiplayernum < 0`.

**Gaps found**:
- **Counter-Op silent anti fallback** — `net.c:823-824` silently
  defaulted `antiplayernum = 1` if `g_NetCounterOpClientId` resolution
  failed. **Fixed**: now logs a `GAMELOOP.COUNTEROP: antiClientId
  unresolved` WARNING so the fallback is never silent. Success path logs
  `GAMELOOP.COUNTEROP: server start anti ... (from wire)`.
- **`endscreenPushAnti` unguarded `g_Vars.bond` access** at
  `endscreen.c:1926-1929` — dereferenced before NULL check. **Fixed**:
  added NULL guard + `GAMELOOP.COUNTEROP: endscreenPushAnti aborted —
  g_Vars.bond NULL` warning so teardown races are visible.

---

### 4. Manifest between missions

Paths audited:

| Transition | Clear site | Notes |
|------------|------------|-------|
| SP menu → SP mission | `manifestMenuTransition` in `pdmain.c:771` (non-gameplay branch) | Auto |
| SP mission → next SP mission (retry) | `manifestSPTransition` diff at `:768` | No explicit clear |
| SP mission → main menu (exit) | `pdgui_bridge.c:847 pdguiEndscreenExitToMainMenu` | F-0.4 |
| MP match end (server broadcast) | `netmsg.c:1471 netmsgSvcStageEndRead` | L1-1 |
| MP match → disconnect | `net.c netDisconnect` | Bug A |
| Coop Deep Sea auto-advance | `menutick.c:610` | S298 |
| Coop restart-level | `menutick.c` S303 NEW | This session |
| Coop exit → CITRAINING | `menutick.c` S303 NEW | This session |
| Coop continue dialog | `menutick.c` S303 NEW | This session |

**Diagnostic logging**: Every `mainChangeToStage` now tagged with
`GAMELOOP.MANIFEST` showing mode + transition kind + `num_entries`. Stale
MP manifest leaking into SP transition is a WARNING.

---

### 5. Starting weapons

| Mode | Path | Source |
|------|------|--------|
| Campaign solo | `playerreset.c:200-227` INTROCMD_WEAPON | Setup file |
| Campaign coop (both players) | Same path, both process INTROCMD_WEAPON | Setup file |
| Counter-op (bond) | Same path | Setup file |
| Counter-op (anti) | Skipped (gated on PLAYER_IS_NOT_ANTI) | Possession at runtime |
| Combat Sim | `playerreset.c` respawnWeaponNum / g_MpSetup.weapons[] | Match config |

**New log**: `GAMELOOP.WEAPON: playernum=%d mission=0x%02x INTRO gave R=%d
L=%d default=%d%s` per weapon grant. `GAMELOOP.WEAPON: ... INTRO skipped
for anti role` for anti (makes "empty-handed anti" observable).

---

### 6. Menu bookends

**Entry (stage load)**:
- `menuhandlerAcceptMission` in `mainmenu.c:731`: `menuStop()` +
  `inputLockMouse(1)`. Does NOT call `menupoolReleaseAll`. S303 adds
  `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: menuhandlerAcceptMission entry
  stage_id='%s' ...` log at every entry (retry labelled when
  `g_Vars.stagenum == g_MissionConfig.stagenum`).

**Exit (mission complete)**:
- Solo: `endscreenPrepare` pushes `g_SoloMissionEndscreen*` dialog
- Coop: `endscreenPushCoop` pushes `g_2PMissionEndscreen*`
- Anti: `endscreenPushAnti` pushes `g_2PMissionEndscreen*`
- All logged with `GAMELOOP.{CAMPAIGN,COOP,COUNTEROP}: endscreenPush*
  entry` at S303.

**Between missions**:
- `menuhandlerAcceptMission` → `mainChangeToStage` → no transient menu
  state between missions (direct stage-to-stage).

**Abort/quit**: Pause menu → End Game → endscreen exit path → CITRAINING.

**Menu pool cleanup** (S299/S300): `menupoolReleaseAll` called at:
- `pdgui_bridge.c:793/810/841` endscreen bridges
- `matchsetup.c:matchStart`
- `netmsg.c:1219` co-op SVC_STAGE_START client receive
- `inputCtxShutdown` nuclear-reset

---

### 7. Edge cases checked

- **First mission** — clean (manifest empty, SP transition path)
- **Last mission (Skedar Ruins)** — falls through `MPENDSCREEN`
  `stageindex <= SOLOSTAGEINDEX_SKEDARRUINS` gate; `endscreenPushSolo`
  doesn't advance past end; legacy credits path not audited this session
- **Retry** — now logged; `GAMELOOP.*: menuhandlerAcceptMission entry
  ... RETRY`
- **Deep Sea coop** — `manifestClear` in place; S303 adds log
- **Cinematic missions** — pre-load manifest Phase 1 misses cinematic
  spawns; Phase 2 rescan at `lv.c:533 manifestSPRescanSetup` recovers
- **Network disconnect mid-coop** — `netDisconnect` path resets
  `g_NetMatchRoomId` (S300), `g_NetCounterOpClientId` (S300),
  `manifestClear` (Bug A)

---

## Fixes landed

| # | Area | File | What |
|---|------|------|------|
| 1 | Manifest | `src/game/menutick.c` | Generalize S298 Deep Sea `manifestClear` pattern to MPENDSCREEN restart-level, MPENDSCREEN → CITRAINING exit, COOPCONTINUE exit. All three new sites log `GAMELOOP.MANIFEST:` + mode tag. |
| 2 | Counter-Op | `port/src/net/net.c` | Log silent antiplayernum=1 fallback as WARNING (`GAMELOOP.COUNTEROP: antiClientId unresolved`). Success path logs `(from wire)`. |
| 3 | Manifest | `port/src/pdmain.c` | WARNING when stale MP manifest leaks into SP transition. NOTE on every gameplay transition with mode + entry count. |
| 4 | Co-op | `src/game/playerreset.c` | WARNING when `g_NumSpawnPoints < 2` in coop/anti (telefrag audit). |
| 5 | Weapons | `src/game/playerreset.c` | Per-grant `GAMELOOP.WEAPON:` log + explicit anti-skip log. |
| 6 | Endscreen | `src/game/endscreen.c` | `GAMELOOP.*: endscreenPush* entry` logs at all three push variants. NULL-guard on `g_Vars.bond` in `endscreenPushAnti` (prevents AV in teardown races). |
| 7 | Manifest + bookend | `port/fast3d/pdgui_bridge.c`, `port/src/net/netmsg.c`, `src/game/mainmenu.c` | Bookend logs at `pdguiEndscreenStartMission`/`NextMission`/`ExitToMainMenu`, `menuhandlerAcceptMission` entry, SVC_STAGE_END receive, and coop manifestBuild. |

---

## New `GAMELOOP.*` log taxonomy

| Tag | What fires it |
|-----|---------------|
| `GAMELOOP.CAMPAIGN:` | Solo entry, endscreen prepare, next mission, retry, exit, SVC_STAGE_END (CombatSim) |
| `GAMELOOP.COOP:` | Coop start (server), push, Deep Sea advance, manifest build, manifestClear, telefrag audit |
| `GAMELOOP.COUNTEROP:` | Anti start (server), push, NULL-guard, wire resolution status |
| `GAMELOOP.MANIFEST:` | Every `mainChangeToStage` (SP/MP/menu), stale-leak WARNING, per-path clear |
| `GAMELOOP.WEAPON:` | Per-grant INTROCMD_WEAPON, anti-skip notice |

Grep recipe:
```bash
grep -E "GAMELOOP\.(CAMPAIGN|COOP|COUNTEROP|MANIFEST|WEAPON)" pd-client.log
grep -E "GAMELOOP\.(COOP|COUNTEROP|MANIFEST)" pd-server.log
```

All `GAMELOOP.*` tags pass through the unchanged log channel classifier —
they appear in every playtest `pd.log` regardless of `Debug.LogChannelMask`
(matches S301 DIAG taxonomy convention).

---

## Known gaps not fixed in this session

- **GAP-A coop manifest ignores host payload** — detectable via new log,
  but fixing requires a design call (adopt host manifest? diff-merge?).
  Queued to tasks-current.
- **GAP-B coop 1-pad telefrag** — now observable via log; a real fix
  needs coop-aware pad scoring in `playerChooseSpawnLocation` or a
  co-op specific SP-14 expansion.
- **Anti body/head pre-load manifest gap** — possession-based loading
  already relies on post-load catalog resolution; pre-load would require
  knowing the mission's "canonical anti chr" which is not currently
  modeled in mission config.
- **Menu pool not released on `menuhandlerAcceptMission` entry** — S303
  logs the entry but doesn't add `menupoolReleaseAll`; could add but
  `menuStop()` already walks the stack and the entry paths are
  established. Queued for future audit.

---

## Playtest recipe

1. **Solo campaign first mission** — load dataDyne (or first
   available), complete, expect:
   - `GAMELOOP.CAMPAIGN: menuhandlerAcceptMission entry stage_id='...' ...`
   - `GAMELOOP.MANIFEST: SP transition to 0x...`
   - `GAMELOOP.WEAPON: playernum=0 mission=0x... INTRO gave ...` (several)
   - `GAMELOOP.CAMPAIGN: endscreenPrepare stage=0x... ...`
   - `GAMELOOP.CAMPAIGN: endscreen NEXT_MISSION ...` or EXIT_TO_MAIN_MENU

2. **Coop Deep Sea auto-advance** — complete Deep Sea coop, expect:
   - `GAMELOOP.COOP: Deep Sea auto-advance → stageindex=... ...`
   - `GAMELOOP.MANIFEST: MP transition to 0x...` for new stage

3. **Counter-Op** — 2-client match with host picking P2 as anti:
   - server log: `GAMELOOP.COUNTEROP: server start anti stage=0x... antiClientId=1 antiplayernum=1 (from wire)`
   - any WARNING from `antiClientId unresolved` = regression
   - both clients log: `GAMELOOP.COUNTEROP: endscreenPushAnti entry role=...`
   - anti's weapon log: `GAMELOOP.WEAPON: ... INTRO skipped for anti role`

4. **Coop transition after non-Deep-Sea mission** (e.g. Villa → exit
   menu, or any completable coop level):
   - `GAMELOOP.COOP: MPENDSCREEN → CITRAINING lobby return`
   - `GAMELOOP.MANIFEST: MPENDSCREEN exit clearing manifest (N entries)
     before CITRAINING`

5. **Coop 1-pad telefrag audit** — load any coop stage and watch for
   `GAMELOOP.COOP: only %d spawn pad(s) declared...` in `pd.log`. Any
   warning identifies a stage that needs investigation.

---

## Files touched

- `src/game/menutick.c` (+generalized manifestClear, +GAMELOOP logs)
- `src/game/mainmenu.c` (+menuhandler entry log, +system.h)
- `src/game/endscreen.c` (+endscreenPush logs, +bond NULL guard, +system.h)
- `src/game/playerreset.c` (+playerReset entry log, +weapon logs, +coop telefrag audit)
- `port/src/pdmain.c` (+mainChangeToStage manifest route logs, +stale-leak warning)
- `port/src/net/net.c` (+antiplayernum fallback log)
- `port/src/net/netmsg.c` (+stage-end + coop manifest build log)
- `port/fast3d/pdgui_bridge.c` (+endscreen bridge logs)
