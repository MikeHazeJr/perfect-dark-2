# 4-20 Critical Stability Bugs

> Working snapshot of **open / logged** stability and UX issues from **2026-04-20–21** playtests and log analysis. Authoritative IDs live in [`bugs.md`](bugs.md); narrative detail in [`session-log.md`](session-log.md) **S431**, **S432**.
>
> Back to [README.md](README.md)

---

## Summary table

| ID | Severity | Topic |
|----|----------|--------|
| **B-219** | **HIGH** | First-person weapon invisible / unusable; `GAMELOOP.WEAPON` vs `SPAWN` weapon mismatch (Chicago CS). |
| **B-218** | MED | Chicago initial bot pile-up; respawns OK; spawn orchestrator / pool. |
| **B-221** | MED | Input batch: vehicle double-tap, hold ring, tap vs hold, visual mapper, multi-bind priority, scorecard Back. |
| **B-222** | MED | Overlay black tint; killfeed visibility + position vs minimap. |
| **B-220** | LOW | `mod.json` missing-file spam (`fsFileLoad` ERROR) from stale registry. |
| **B-217** | LOW | F6 bot freeze: residual movement (`chrTick` still runs). |

---

## B-219 — HIGH — FP weapon mismatch (Combat Sim)

- **Symptom:** Player appears to have a weapon (HUD/ammo) but **no first-person model** and **cannot use** it reliably.
- **Log fingerprint:** `GAMELOOP.WEAPON: … INTRO gave R=36 L=-1` vs `SPAWN: player 0 spawned with weapon 2 (Falcon 2)` — right-hand weapon enum inconsistent (36 ≈ `WEAPON_PP9I` vs 2 = Falcon).
- **Suspects:** Intro/gameloop path vs `bondgun` / `bgun`; MP spawn-with-weapon fallback (`SETUP: world pickups … forcing spawn-with-weapon fallback id='base:falcon2'`).
- **Files:** `player.c`, `bondgun.c`, `setup.c`, match intro / `GAMELOOP.WEAPON`.

---

## B-218 — MED — Chicago spawn stacking

- **Symptom:** All bots **spawn in one spot** initially; after deaths, **respawns** are dispersed.
- **Log:** `SPAWN.ORCH: RELAX … kind=duplicate`, `SETUP: world pickups 0 below target 16`, `SPAWNPOOL: build complete -- 37 points`.
- **Related:** B-174, **S423** (high bot-count initial spawn path).
- **Files:** `mpspawn_orchestrate.c`, `player.c`, `bot.c`, `spawnpool.c`.

---

## B-221 — MED — Input / interaction (logged only, fixes deferred)

1. **Vehicle mount:** Behaves like **OG double-tap**; user expects **single tap**. Anchor: `currentPlayerTryMountHoverbike` in `propobj.c` (`lvframe60 - activatetimelast < TICKS(30)`), plus PC `pcinteractusekind` in `propobjInteract` / `bondmove.c`.
2. **Hold ring:** Not **live** while holding; on **release** does not reset to **0** (stays filled). Anchors: `pdgui_interact_prompt.cpp`, `actionHoldProgress` in `actionmap.cpp`.
3. **Tap vs hold:** **Tap X** completes interact where **hold** is intended.
4. **Visual input mapper:** Reported broken; re-approach later.
5. **Multi-bind priority:** One physical key mapped to several actions — need **resolution order** UX.
6. **Scorecard:** **Hold Back** does not show scorecard. Anchor: `scorecardTickButtonState` in `pdgui_menu_pausemenu.cpp` (`actionHeld(0, ACTION_SCORECARD)`).

---

## B-222 — MED — Overlay tint + killfeed (logged only, fixes deferred)

- **Tint:** Semi-opaque **black over full game window** when **F6/F7** dev banners, **invincible**, and/or **Hold X** interact UI are active — should not dim unrelated gameplay.
- **Killfeed:** Only appears when that **ImGui overlay path** is active; should show during **normal** gameplay.
- **Layout:** Killfeed **over minimap**; desired **lower-left** or clear of radar.
- **Suspects:** `pdguiAnyStandardOverlayReason` / `pdguiNewFrame` + `pdguiRender` early-return (`pdgui_backend.cpp`); killfeed `baseX` / `baseY` (`pdgui_menu_mpingame.cpp`).

---

## B-220 — LOW — Mod registry `mod.json` spam

- **Symptom:** ~**100×** `ERROR: fsFileLoad: could not find file: …/data/mods/<dir>/mod.json` per session; same set in **two** bursts at boot.
- **Cause:** Registry lists mods not on disk (audio-only folders, renamed paths, etc.).
- **Related:** B-172 (modmgr should not spam for `!has_modjson`).
- **Files:** `modmgr.c`, `fs.c`, user `data/mods/`, `pd.ini`.

---

## B-217 — LOW — F6 bot freeze incomplete

- **Symptom:** With **F6** (`g_BotUpdatesDisabled`), bots **still drift / walk** — not full freeze.
- **Cause:** `botTick` still runs **`chrTick`** only; AI skipped; locomotion can continue (`bot.c` ~1304–1306).
- **Files:** `bot.c`, `chraction.c` / movement.

---

## Ephemeral log digest (pd-client, 2026-04-21)

From `session-log.md` **S431** (full client log not retained):

- **ERROR:** All sampled lines were `fsFileLoad` missing `mod.json` → **B-220**.
- **WARNING (5):** `DIAG fireVk vk=531 NO BINDING` (see **B-203**); `SETUP` world pickups fallback; `SPAWNPOOL` L1 pad validation; `SPAWN.ORCH` RELAX duplicate; bot underground clamp.
- **Audio:** `AUDIO[B-141]` underruns/hitches worsen under 32-bot load — see **B-141**, **B-204**, **B-205**.

---

## Cross-references

| Ref | Note |
|-----|------|
| `session-log.md` **S431** | Chicago playtest, log scrape |
| `session-log.md` **S432** | Input/overlay detail (logged only) |
| `tasks-current.md` | Punch-list pointers |
| B-141 / B-204 / B-205 | Audio scheduling under load |
| B-172 | Mod manifest / `mod.json` noise class |
| B-174 | Car Park / spawn collapse (related class to B-218) |
| B-203 | D-pad down binding |
