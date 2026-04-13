# Mod Enablement Policy — Default-Enabled

> **Established**: 2026-04-13, S221  
> **Scope**: All mod-declared UI assets (themes today; skins, audio, weapons in future)

## Core Rule

**Mod content is enabled by default.** Users opt *out*, not in.

When a mod asset is first detected — whether discovered at init via directory scan or
hot-registered mid-session via network distribution — its enabled flag defaults to `1`.
The user can toggle it off in the Settings UI; that preference persists in `pd.ini` across
sessions. The asset remains catalogued (not removed) so re-enabling is one click.

## Per-Asset-Type Behavior

### Themes (implemented 2026-04-13)

| Event | Behavior |
|-------|----------|
| **Init scan** (`scan_mods_for_themes`) | Each `mods/<slug>/theme.json` is registered. `enabled=1` by default. |
| **First sight** (slug not in `Theme.SeenMods`) | Theme auto-applies as active. `Theme.SeenMods` updated. |
| **Subsequent sessions** | `Theme.Enable.<slug>` read from `pd.ini`. Respects stored flag. |
| **Hot-registration** (netdistrib or theme editor save) | `pdguiThemeRegisterModDir()` applies the same policy — first-sight auto-applies. |
| **User disables** | Right-click context menu in Settings > Debug > UI Theme. Theme greyed out but stays in grid. If it was active, reverts to `base:theme_blue`. |
| **User re-enables** | Right-click > Enable Theme. One click. Can then left-click to activate. |

### pd.ini Keys

- `Theme.ActiveTheme` — catalog ID of the active theme (e.g., `mod:my_theme`)
- `Theme.SeenMods` — comma-separated slugs of all mod themes seen in prior sessions
- `Theme.Enable.<slug>` — per-theme enabled flag (0 or 1)

### Skins, Audio, Weapons (future)

When these mod types gain their own selector UIs, apply the same pattern:

1. Add `enabled` flag to the mod's registry entry
2. Persist per-mod enabled state in pd.ini (e.g., `Skin.Enable.<slug>`)
3. Track first-sight via a comma-separated seen list (e.g., `Skin.SeenMods`)
4. First-sight enabled mods auto-activate (for single-active types like themes)
   or auto-include (for multi-active types like audio tracks)
5. UI shows enable/disable toggle; disabled assets are greyed but remain catalogued

## Detection Points

Mod assets are detected at two points:

1. **Init-time directory scan** — runs once during subsystem init. Walks `mods/` for
   known manifest files (theme.json, audio.ini, etc.).
2. **Hot-registration via network distribution** — `netdistrib.c` extracts received mod
   archives and calls subsystem-specific registration APIs. For themes, this is
   `pdguiThemeRegisterModDir()`. For audio, this is `assetCatalogRegisterAudio()`.

Both paths feed into the same registration + enablement logic.

## Rationale

Opt-in creates friction for mod content that the server intended to share. If a server
distributes a UI theme, the player should see it without manual steps. The enable/disable
toggle gives users control without making the default experience worse.
