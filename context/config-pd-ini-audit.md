# pd.ini / per-agent / compile-time -- configuration surface audit

> **Last updated**: 2026-04-17 (S313 marathon batch).
> Tracks every `configRegister*` call in the port, why it lives where it
> does, and how future sessions should think about adding new keys.

## Three tiers of configuration

1. **pd.ini** (per-machine, shared across agent profiles) -- for hardware
   and host-level values that don't change between Agent profiles.  Things
   the OS / display / audio backend / network stack need.
2. **saves/prefs_<agent>.ini** (per-agent, via `prefs_agent.c`) -- for
   player preferences (visuals, audio volumes, mod enablement) that
   should follow the active Agent profile.
3. **Compile-time constants** -- tuning knobs that never need end-user
   adjustment and would only cause confusion / misconfiguration if
   exposed in pd.ini.  Recompile to change.

When adding a new setting, default to **per-agent** unless it's
explicitly hardware-level or something pd.ini is already the right home
for.  For truly invariant tuning numbers, use a `#define` or a
`PD_CONSTRUCTOR`-initialized mutable global without `configRegister*`.

---

## Currently in pd.ini (per-machine)

| Key prefix | Why per-machine |
|------------|-----------------|
| `Video.*` (DefaultFullscreen, DefaultMaximize, DefaultWidth/Height, ExclusiveFullscreen, CenterWindow, AllowHiDpi, VSync, FramebufferEffects, FramerateLimit, DisplayFPS, DisplayFPSInterval, MSAA, TextureFilter, TextureFilter2D, DetailTextures, UIScaleMult) | Hardware / display-level.  Window geometry and GPU settings shouldn't change per Agent profile. |
| `Video.UiChromeEnabled` / `UiChromeStyleId` / `UiTitleBarStyle` / `Scanlines` / `ScanlineAlpha` | Also overlaid per-agent via prefs sidecar.  pd.ini provides the global default; agent prefs override on load. |
| `Input.*` (MouseEnabled, MouseLockMode, MouseSpeedX/Y, FakeGamepads, FirstGamepadNum, UseHIDAPI, UseRawInput) | Input device wiring -- varies by PC, not by Agent. |
| `ActionMap.StickSensitivity` / `StickDeadzone` / `StickInvertY` / `SwapSticks` | Controller tuning.  Arguably could move per-agent in the future. |
| `Audio.BufferSize` / `QueueLimit` / `VerboseLog` | SDL audio backend sizing + debug.  Per-machine. |
| `Audio.MasterVolume` / `MusicVolume` / `GameplayVolume` / `UIVolume` | Retained as per-machine defaults; **also overlaid per-agent** (S313 batch -- load applies agent values via audioSet*Volume setters). |
| `Audio.ModPlaylist` / `ModShuffle` / `ModTrackId` | Music selection -- today per-machine, could migrate per-agent in a future pass. |
| `Game.MemorySize` | Engine memory pool sizing -- hardware. |
| `Game.CenterHUD` / `MenuMouseControl` / `ScreenShakeIntensity` / `TickRateDivisor` / `ExtraSleep` / `SkipIntro` / `DisableMpDeathMusic` / `GEMuzzleFlashes` | Gameplay prefs.  Candidates for future per-agent migration, but currently global. |
| `Game.UpdateChannel` | stable vs beta; per-install. |
| `Agent.DefaultFileId` | The Agent profile to auto-load on boot.  Per-machine by design (you can't pick the agent you're loading *with* the agent's own prefs). |
| `Debug.VerboseLogging` / `LogChannelMask` / `ManifestMaxEntries` / `JumpLogging` | Troubleshooting.  Per-machine. |
| `UI.SafeAreaTop` / `SafeAreaBottom` / `SafeAreaLeft` / `SafeAreaRight` | Monitor bezel / overscan compensation.  Per-machine. |
| `Net.Client.LastJoinAddr` | Last server the user joined -- per-machine history. |
| `Net.RecentServer.*` / `Net.RecentServerCount` | Session-to-session server history. |
| `Net.Server.AllowInfoQuery` | Dedicated-server ops knob.  Server operators may want this off for private games. |
| `Net.DistribTrustThresholdMB` | Network mod distribution threshold. |
| `Mods.EnabledMods` / `SizeThresholdMB` | Global default for enabled mod list; **also overlaid per-agent** via prefs sidecar. |

## Currently per-agent (saves/prefs_<agent>.ini, via prefs_agent.c)

| Section | Keys | Notes |
|---------|------|-------|
| `[Theme]` | ActiveId | Theme catalog ID. |
| `[Video]` | UiChromeStyleId, UiChromeEnabled, UiTitleBarStyle, FontId, Scanlines, ScanlineAlpha | Visual preferences.  Font change requires restart. |
| `[Audio]` | MasterVolume, MusicVolume, GameplayVolume, UIVolume | **S313 batch.**  Applied on load via audioSet*Volume setters. |
| `[Mods]` | Enabled = slug1,slug2,... | Agent's enabled-mods set. |

Loading an Agent Select profile (prefsAgentLoad) applies these on top of
whatever pd.ini set at boot.  Saving (prefsAgentSave) is debounced by
content-hash compare so disk writes only happen on actual value changes.

## Compile-time constants (S313 batch cleanup)

These were removed from pd.ini and now live as file-scope globals with
initializers in their respective TUs.  CLI / build-time override paths
remain where applicable.

| Former key | Default | Where it lives now |
|------------|---------|--------------------|
| `Net.LerpTicks` | 3 | `g_NetInterpTicks` initializer in `port/src/net/net.c` |
| `Net.Client.InRate` / `OutRate` | 128 KiB/s | `g_NetClient{In,Out}Rate` initializers in `port/src/net/net.c` |
| `Net.Client.UpdateFrames` | 1 | `g_NetClientUpdateRate` initializer |
| `Net.Server.Port` | `NET_DEFAULT_PORT` (27100) | `g_NetServerPort` initializer.  CLI override via `-port N` still works. |
| `Net.Server.InRate` / `OutRate` | 128 KiB/s | `g_NetServer{In,Out}Rate` initializers |
| `Net.Server.UpdateFrames` | 1 | `g_NetServerUpdateRate` initializer |
| `Update.ProtectedFolders` | `"mods,data,extracted,saves"` | `UPDATER_DEFAULT_PROTECTED` `#define` + `s_ProtectedFoldersCfg` initializer in `port/src/updater.c`.  pd.ini is always protected regardless of this list. |

## Decision guide -- where should a new setting go?

```
                          +---+
                          |new|
                          +-+-+
                            |
                 can user meaningfully override without breaking physics / network compat?
                    +-------+-------+
                   YES              NO -> compile-time constant
                    |
         does it vary per Agent profile
         (player preference, not hardware)?
                    +-------+-------+
                   YES              NO -> pd.ini
                    |
                per-agent prefs
                (prefs_agent.c,
                 call audioSet*
                 or equivalent setter)
```

## Lineage

- **S309** -- first cut of `prefs_agent.c` / `.h`.  Visuals (Theme, Video
  chrome/title bar/scanlines/font) + `[Mods]` enabled list.
- **S313 marathon** -- added `[Audio]` volume layer overlays + fixed
  `prefsLoadForFile` to use `gamefileGetOverview` so sidecar filenames
  match Agent Select display text.  Removed network tuning rates +
  server port + protected folders from pd.ini in favour of compile-time
  defaults.

## Related files

- `port/src/prefs_agent.c` / `port/include/prefs_agent.h` -- per-agent sidecar
- `port/src/config.c` -- pd.ini read/write + `configRegister*` registry
- `port/src/net/net.c` -- network globals + remaining `Net.*` registrations
- `port/src/updater.c` -- updater globals
- Every `configRegister*` call in `port/**/*` is discoverable via
  `grep -r "configRegister" port/`.
