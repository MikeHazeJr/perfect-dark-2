# HUD Score Panel — Dock-Below-Minimap Design

> **Created**: 2026-04-13
> **Status**: IMPLEMENTED
> **Files**: pdgui_hud.cpp, pdgui_bridge.c, pdgui_hud.h, mpstats.c, pdgui_menu_mainmenu.cpp

---

## Layout

```
  +------------------+
  |   GBI RADAR      |   ← Rendered by radar.c in GBI pass (before ImGui)
  |   (minimap)      |
  +------------------+
         4px gap
  +------------------+
  | PlayerA       12 |   ← Name (colored) + score (white), 60% opacity
  | ████████░░░░░░░░ |   ← Progress bar: score/limit ratio, player/team color
  |                  |
  | PlayerB        8 |
  | █████░░░░░░░░░░░ |
  |                  |
  |      2:45        |   ← Timer (centered), color-coded by urgency
  +------------------+
```

- **No background fill** — text + bars only, 60% opacity
- **Position**: docked below radar's bottom edge via `pdguiHudGetRadarRect()`
- **Fallback**: top-right corner when radar is hidden
- **Panel width**: 150 scaled px, centered under radar

---

## Content Modes

### FFA / No Teams
- Top 2 **players** sorted by score (via `mpGetPlayerRankings()`)
- Progress bar: gold (1st) / silver (2nd)
- Score limit: `g_MpSetup.scorelimit` (0-based, +1 for display; >=99 = unlimited)

### Team Mode
- Top 2 **teams** sorted by score (via `mpGetTeamRankings()`)
- Progress bar: team color (Red/Blue/Green/Yellow/Orange/Purple/Grey/White)
- Score limit: `g_MpSetup.teamscorelimit` (0 = unlimited)

### No Score Limit
- Bars show relative to the highest score in the match

---

## Event Flow: Score Sync

```
Kill event (any machine):
  chrDie() [chraction.c]
    → mpstatsRecordDeath() [mpstats.c]
        → updates killcounts[], numdeaths
        → pdguiKillfeedPush() — local ImGui killfeed
        → g_NetPendingResyncFlags |= NET_RESYNC_FLAG_SCORES  ← NEW

Server end-of-frame (net.c:1602):
  if (g_NetPendingResyncFlags & NET_RESYNC_FLAG_SCORES)
    → netmsgSvcPlayerScoresWrite() to ALL clients
    → flag cleared
```

**Before this change**: `NET_RESYNC_FLAG_SCORES` was never set on kill events — scores only synced on reconnect/demand. Now every kill triggers an immediate score broadcast.

---

## Killfeed: Bot-vs-Bot Analysis

The killfeed already includes all 4 kill combinations:
- `chrDie()` calls `mpstatsRecordDeath()` for ALL chr deaths (player and bot)
- `mpstatsRecordDeath()` calls `pdguiKillfeedPush()` when `ampchr && vmpchr` — no player-only gate
- On remote clients: `SVC_CHR_DAMAGE` replicates damage → client-side `chrDie` → same path

The tasks-current.md note "Killfeed only shows player kills" appears to refer to the unused lobby `netDistribSendKillFeed()` system, not the in-game ImGui killfeed.

---

## Net Sync Protocol

| Event | Message | Direction | Timing |
|-------|---------|-----------|--------|
| Score change | SVC_PLAYER_SCORES (0x23) | S→C reliable | Per-kill (event-driven) |
| Kill event | SVC_CHR_DAMAGE (0x42) | S→C reliable | Per-hit (already event-driven) |
| Chr death | chrDie runs locally | N/A | Fires on all machines when HP reaches 0 |

---

## B-60 Fix: Stray Characters in Settings

**Root cause**: `drawPdWindowFrame()` drew title text via `dl->AddText()` into the window draw list without clipping. Font descenders from "Settings" (the 'g' and 's' characters) extended below the title bar area. The `##main_menu` window uses `NoBackground`, so these descenders were visible behind the tab content.

**Fix**: `dl->PushClipRect()` constraining title text + glow to the title bar rectangle. Descenders are now clipped at the title bar boundary.

---

## Bridge Functions Added

| Function | Returns | Purpose |
|----------|---------|---------|
| `pdguiHudGetRadarRect(float*, float*, float*, float*)` | 1 if visible | Radar rect in normalized 0-1 coords |
| `pdguiHudGetScoreLimit()` | s32 | FFA score limit (0-based) |
| `pdguiHudGetTeamScoreLimit()` | s32 | Team score limit |
| `pdguiHudIsTeamsEnabled()` | s32 | Teams on/off |
| `pdguiHudGetTeamColor(s32)` | u32 RGBA | Team color |
| `pdguiHudGetTeamName(s32)` | const char* | Team display name |
