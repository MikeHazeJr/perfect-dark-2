# HUD Layer Order & Context-Aware Render Gating

> **Created**: 2026-04-13 (S221)
> **Status**: IMPLEMENTED
> **Bugs fixed**: Connected Players sidebar in gameplay, B-95 update banner in gameplay, Score/Radar overlap, B-60 (investigated, separate root cause)

---

## Problem

Three related bugs share the same root cause class: HUD elements rendering outside their intended context.

1. **Connected Players sidebar** renders during active gameplay (should only show in lobby/ready-gate)
2. **B-95**: Update notification banner persists during missions/combat
3. **Score panel occludes radar/minimap** — ImGui HUD overlays GBI radar in the same top-right corner

All stem from the absence of a context-aware render gating system. Each renderer independently checks ad-hoc state flags with no centralized concept of "what rendering context are we in?"

---

## Design

### HUD Context Enum

```c
typedef enum {
    HUD_CTX_NONE      = 0,  /* No HUD (loading, transitions) */
    HUD_CTX_MENU      = 1,  /* Main menu, CI free-roam, menus active */
    HUD_CTX_LOBBY     = 2,  /* Network lobby, room screen, ready gate */
    HUD_CTX_COUNTDOWN = 3,  /* Pre-match countdown (3-2-1-GO) */
    HUD_CTX_GAMEPLAY  = 4,  /* Active combat/mission gameplay */
    HUD_CTX_GAMEOVER  = 5,  /* Post-match results */
} HudContext;
```

**Derivation** (computed each frame in `pdguiRender()`, not stored globally — avoids stale state):
- `GAMEPLAY`: `pdguiPauseGetNormMplayerIsRunning()` is true
- `GAMEOVER`: `pdguiPauseGetPaused() == MPPAUSEMODE_GAMEOVER`
- `COUNTDOWN`: `pdguiCountdownIsActive()` is true
- `LOBBY`: `netGetMode() != NETMODE_NONE && netLocalClientInLobby()`
- `MENU`: hotswap menus active
- `NONE`: fallback

### Layer Render Order

The existing call order in `pdguiRender()` defines the implicit z-stack. Document it as policy:

| Layer | Position | Elements | Context Gate |
|-------|----------|----------|-------------|
| 0. Background | -- | (game world, GBI radar/minimap) | N/A (pre-ImGui) |
| 1. Debug | bottom | Console, F12 debug, log viewer | F12/backtick only |
| 2. Menus | middle | Hotswap menus (main menu, mission select, etc.) | MENU |
| 3. Lobby | middle | Lobby screen, room screen, distrib overlays | LOBBY only |
| 4. Countdown | overlay | 3-2-1-GO popup | COUNTDOWN |
| 5. Notifications | overlay | Update banner, version watermark | MENU only (not GAMEPLAY) |
| 6. Pause/Score | overlay | Pause menu, scorecard (Tab hold) | GAMEPLAY only |
| 7. HUD | overlay | Score panel, timer | GAMEPLAY only |
| 8. Killfeed | overlay | Kill/score ticker | GAMEPLAY, not GAMEOVER |
| 9. Connected | overlay | In-game sidebar (compact player list) | LOBBY only (not GAMEPLAY) |
| 10. Effects | foreground | Shimmer, CRT scanlines | Always |

### Specific Fixes

1. **Connected Players sidebar**: Gate `renderInGameSidebar()` on context != GAMEPLAY. The sidebar should render during LOBBY (ready-gate) transitions but NOT during active gameplay. Fix in `pdguiLobbyRender()` by checking `pdguiPauseGetNormMplayerIsRunning()` — if match is running, skip the sidebar.

2. **B-95 Update banner**: Gate `renderNotificationBanner()` on context != GAMEPLAY. In `pdguiUpdateRender()`, check `pdguiPauseGetNormMplayerIsRunning()` and suppress the banner during active gameplay.

3. **Score/Radar overlap**: The GBI radar renders at top-right (~41px from right edge, ~26px from top). The ImGui score panel positions at `(winW - panelW - 10, 10)`. Fix: when the radar is enabled, shift the score panel's Y position down below the radar area (~80 scaled pixels). Query `g_RadarVisible` or compute from display options.

4. **B-60 stray 'g'+'s'**: Investigated. The Settings tab uses `##` prefixed IDs which are hidden. The PD dialog frame (`drawPdWindowFrame` → `pdguiDrawPdDialog`) draws a body background rect, but the `##main_menu` window uses `NoBackground` and the `##main_settings_body` child also uses `NoBackground`. If the body fill in `pdguiDrawPdDialog` doesn't perfectly cover the tab content area, residual glyphs from adjacent draw list text calls could show through. Root cause is likely a sub-pixel gap in the procedural background fill — not a layer/context issue. Documented for separate fix.

---

## Implementation Notes

- Context is derived each frame, not stored in a global. No stale state risk.
- No new enum type actually needed in code — the gating uses existing accessor functions (`pdguiPauseGetNormMplayerIsRunning()`, `netLocalClientInLobby()`) directly.
- The layer order is formalized as documentation — the call order in `pdguiRender()` IS the z-stack.
- `NoBringToFrontOnFocus` is already used on passive overlays (banner, watermark, killfeed, scorecard). No changes needed there.
