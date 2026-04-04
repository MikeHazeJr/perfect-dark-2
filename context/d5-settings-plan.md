# Phase D5: Settings, Graphics & QoL — Implementation Plan

## Status: DONE
Last updated: 2026-04-01

> **D5a, D5b, D5c all implemented** — Audio tab (4 volume sliders), Video tab (fullscreen/resolution/VSync/MSAA/UI Scale/etc.), Controls tab (full key rebinding), Game tab (FOV/crosshair/crouch/jump). Build clean. UI Scale multiplier (`Video.UIScaleMult`, 50–200%) added 2026-04-01 to close the v0.1.0 "UI Scaling" blocker. See `context/tasks-current.md`.

Last updated: 2026-03-19 (implementation); 2026-04-01 (status updated)

## Overview

Three independent sub-phases, each self-contained. All work centers on the existing ImGui settings menu in `pdgui_menu_mainmenu.cpp` (4-tab layout: Video, Audio, Controls, Game) and the config persistence system in `config.c`.

---

## D5a: Audio Volume Layers

### Goal
Replace the single Music slider with four independent volume controls: Master, Music, Gameplay (SFX), and UI. Each persisted to pd.ini.

### Current State
- Music volume: exposed via `optionsGetMusicVolume()` / `optionsSetMusicVolume()` → `g_MusicVolume` (u16, range 0–0x7FFF). Scales at `seqSetVolume()` via per-track coefficients.
- SFX volume: `g_SfxVolume` (u16, range 0–0x7FFF). Applied via `sndSetSfxVolume()` which posts events to all 9 key-indexed sound channels. Not exposed in ImGui UI.
- UI sounds: `pdguiPlaySound()` → `menuPlaySound()` → goes through same SFX pipeline as gameplay sounds. No separate volume.
- Audio config: only `Audio.BufferSize` and `Audio.QueueLimit` registered. No volume config vars.

### Architecture

**New globals** (in `port/src/audio.c` or a new `port/src/audiovol.c`):
```c
static f32 g_AudioMasterVolume = 1.0f;   /* 0.0–1.0 */
static f32 g_AudioMusicVolume  = 1.0f;   /* 0.0–1.0 */
static f32 g_AudioGameVolume   = 1.0f;   /* 0.0–1.0 — gameplay SFX */
static f32 g_AudioUiVolume     = 1.0f;   /* 0.0–1.0 — menu/UI sounds */
```

Registered in config:
```c
configRegisterFloat("Audio.MasterVolume",   &g_AudioMasterVolume, 0.f, 1.f);
configRegisterFloat("Audio.MusicVolume",    &g_AudioMusicVolume,  0.f, 1.f);
configRegisterFloat("Audio.GameplayVolume", &g_AudioGameVolume,   0.f, 1.f);
configRegisterFloat("Audio.UIVolume",       &g_AudioUiVolume,     0.f, 1.f);
```

**Volume application strategy:**

The N64 audio pipeline doesn't have bus routing. Rather than rewriting the mixer, we scale at the API boundary:

1. **Music**: Hook into `musicSetVolume()` / `seqSetVolume()`. Before the value reaches the sequencer, multiply by `g_AudioMasterVolume * g_AudioMusicVolume`. Store the "user-intended" volume separately so the slider reflects the music-layer value, not the composite.

2. **Gameplay SFX**: Hook into `sndSetSfxVolume()` and the per-sound `func00033820()` volume path. Scale by `g_AudioMasterVolume * g_AudioGameVolume`. Since `sndSetSfxVolume()` sets all 9 key-indexed channels, this is a single choke point.

3. **UI sounds**: Modify `pdguiPlaySound()` (in `pdgui_audio.cpp`) to scale the sound's volume by `g_AudioMasterVolume * g_AudioUiVolume` before calling `menuPlaySound()`. If `menuPlaySound()` doesn't accept a volume parameter, we temporarily set `g_SfxVolume` before the call and restore it, or introduce a `menuPlaySoundWithVolume()` wrapper.

4. **Master**: Implicitly applied via the above three paths. Changing Master re-applies all three.

**Getter/setter API** (exposed via bridge for ImGui):
```c
f32 audioGetMasterVolume(void);
void audioSetMasterVolume(f32 vol);
f32 audioGetMusicVolume(void);
void audioSetMusicVolume(f32 vol);
f32 audioGetGameVolume(void);
void audioSetGameVolume(f32 vol);
f32 audioGetUiVolume(void);
void audioSetUiVolume(f32 vol);
void audioApplyVolumes(void);  /* recompute and push all composite volumes */
```

**ImGui UI** (`renderSettingsAudio`):
```
Volume Controls
─────────────────
Master Volume     [====████████====]  80%
Music             [====██████======]  60%
Gameplay          [====████████████]  100%
UI                [====██████======]  60%
─────────────────
☐ Disable MP Death Music
```

Each slider calls its setter, which internally calls `audioApplyVolumes()` to push the composite values to the engine.

### Files to Modify
- `port/src/audio.c` — add volume globals, config registration, getter/setter API
- `port/fast3d/pdgui_audio.cpp` — scale UI sounds by UI volume layer
- `port/fast3d/pdgui_bridge.c` — expose audioGet/Set functions for C++ ImGui code
- `port/fast3d/pdgui_menu_mainmenu.cpp` — rewrite `renderSettingsAudio()` with 4 sliders
- `src/game/music.c` or `src/lib/snd.c` — hook volume scaling at application points

### Estimated: ~200 LOC

---

## D5b: Video/Graphics Settings UI Polish + Live-Apply

### Goal
Complete the Video settings tab with all options properly exposed, categorized, and with live-apply where possible.

### Current State
The Video tab (`renderSettingsVideo`) already has:
- Fullscreen toggle + mode (Borderless/Exclusive)
- Resolution picker
- Center window, MSAA, VSync, Framerate limit
- Uncap tickrate, Display FPS, Texture filtering, Detail textures, HUD centering

### What's Missing / Needs Polish
1. **Resolution change live-apply**: Currently just sets config vars. Need to call `videoSetWindowSize()` / `videoSetFullscreen()` on change. Some changes require an "Apply" button + confirmation dialog ("Keep these settings? Reverting in 10s...").
2. **Display mode enumeration**: List available monitor resolutions via `SDL_GetNumDisplayModes()` / `SDL_GetDisplayMode()` instead of hardcoded values (if currently hardcoded).
3. **Restart-required indicator**: Mark settings that need restart (MSAA, some GL settings) with "(requires restart)" text.
4. **Brightness/Gamma slider**: SDL_SetWindowBrightness or shader-based gamma correction. Range 0.5–2.0.
5. **Section headers**: Group settings into "Display", "Performance", "Rendering" sub-sections for clarity.

### Files to Modify
- `port/fast3d/pdgui_menu_mainmenu.cpp` — `renderSettingsVideo()` rewrite
- `port/src/video.c` — add live-apply functions if missing, gamma support
- `port/fast3d/pdgui_bridge.c` — bridge any new video getter/setter functions

### Estimated: ~150 LOC changes

---

## D5c: Controls Rebinding UI

### Goal
Full interactive key rebinding in the ImGui Controls tab. Player selects an action, presses a key/button, binding is saved.

### Current State
- Bind system exists: `binds[controller][CK_*][INPUT_MAX_BINDS]` — each action can have up to 3 binds.
- Serialization exists: `inputSaveBinds()` / `inputLoadBinds()` convert between internal array and string format for pd.ini.
- Default binds exist: `inputSetDefaultKeyBinds()` with PC M+KB, PC Gamepad, and N64 presets.
- Controls tab currently shows: mouse enable, aim lock, lock mode, mouse speed, crosshair speed. No keybind UI.

### Design

**Layout:**
```
Controls
─────────────────
Mouse Settings
  ☐ Mouse Enabled
  Mouse Speed X    [====████====]
  Mouse Speed Y    [====████====]
  ...
─────────────────
Key Bindings                    [Reset to Defaults]
  Action          Primary    Secondary
  ─────────────────────────────────────
  Fire            LMB        [+]
  Aim             RMB        [+]
  Use/Accept      E          [+]
  Reload          R          [+]
  Melee           F          [+]
  Jump            Space      [+]
  Crouch          LCtrl      [+]
  Forward         W          [+]
  Back            S          [+]
  ...
─────────────────
```

**Bind capture flow:**
1. Player clicks a bind cell → cell shows "Press a key..." (highlighted, pulsing)
2. `s_CapturingBind = { controller, ck, slot }` state set
3. Next frame: poll `inputKeyPressed()` for any key/button
4. On detection: write to `binds[controller][ck][slot]`, clear capture state
5. ESC cancels capture, DELETE/Backspace clears the bind
6. Duplicate detection: if new bind conflicts with another action, show warning and swap/clear

**Advanced input modes:**
Each bind entry supports an optional modifier that changes how the input is interpreted:

- **Single press** (default): fires on key-down. Standard behavior.
- **Double press**: fires when the same key is pressed twice within a configurable window (~300ms). Stored as `BINDFLAG_DOUBLE` in the bind entry. Example: double-tap forward to sprint.
- **Long press**: fires when key is held for a configurable duration (~500ms). Stored as `BINDFLAG_LONG`. Example: hold reload to cycle weapons.
- **Combo / cheat code buffer**: A separate input sequence buffer (`inputSequenceBuffer[]`) records the last N button presses with timestamps. Game systems can register sequence patterns (e.g., Up-Up-Down-Down-Left-Right-Left-Right-B-A) and get a callback when matched. This is independent of the bind system — it listens to raw inputs and pattern-matches against registered sequences.

**Bind struct extension:**
```c
struct inputbind {
    u32 vk;          /* virtual key (scancode / mouse / joy button) */
    u8  flags;        /* BINDFLAG_NONE, BINDFLAG_DOUBLE, BINDFLAG_LONG */
    u16 threshold;    /* timing threshold in ms (double-press window / long-press hold time) */
};
```

**Sequence buffer** (for cheat codes / combos):
```c
#define INPUT_SEQ_BUFFER_SIZE 32
struct inputseqentry {
    u32 vk;
    u32 timestamp;   /* ms since session start */
};
static struct inputseqentry g_InputSeqBuffer[INPUT_SEQ_BUFFER_SIZE];
static s32 g_InputSeqHead = 0;

/* Registration API */
void inputRegisterSequence(const u32 *pattern, s32 len, void (*callback)(void));
void inputSequenceTick(void);  /* called each frame, checks for matches */
```

The UI exposes bind mode as a small dropdown or cycle button next to each bind cell: [Single ▾] / [Double ▾] / [Hold ▾].

**Action display names** — map `CK_*` to human-readable names:
```c
static const char *ckNames[CK_TOTAL_COUNT] = {
    [CK_C_R]        = "Look Right",
    [CK_C_L]        = "Look Left",
    [CK_C_D]        = "Look Down",
    [CK_C_U]        = "Look Up",
    [CK_RTRIG]      = "Fire",
    [CK_LTRIG]      = "Aim",
    [CK_A]          = "Use / Accept",
    [CK_B]          = "Cancel / Melee",
    [CK_ZTRIG]      = "Reload",
    [CK_START]      = "Pause / Menu",
    [CK_DPAD_U/D/L/R] = "D-Pad ...",
    [CK_STICK_*]    = "Move ...",
    [CK_ACCEPT]     = "UI Accept",
    [CK_CANCEL]     = "UI Cancel",
    ...
};
```

**Virtual key → display name**: `inputGetKeyName(u32 vk)` — already exists or easily built from SDL scancode names.

### Files to Modify
- `port/fast3d/pdgui_menu_mainmenu.cpp` — rewrite `renderSettingsControls()` with bind table + capture mode
- `port/src/input.c` — expose `inputGetBind()`, `inputSetBind()`, `inputGetKeyName()` if not already public
- `port/include/input.h` — public API for bind access
- `port/fast3d/pdgui_bridge.c` — bridge for bind get/set from C++

### Estimated: ~500 LOC (largest sub-phase — bind capture, input modes, sequence buffer)

---

## D5d: Verification

- All 4 audio sliders persist to pd.ini and restore on restart
- Audio layers are independent: muting Music doesn't affect Gameplay or UI sounds
- Video settings apply live where possible, restart-required settings are labeled
- Resolution changes offer revert-on-timeout
- All CK actions are rebindable with capture UI
- Default binds restore correctly
- Binds persist across sessions
- No regressions in existing settings (FOV, crosshair, crouch mode, etc.)

---

## Implementation Order

1. **D5a (Audio)** — Most architecturally impactful. Adds the 3-layer volume system.
2. **D5b (Video)** — Polish pass on existing UI. Lower risk.
3. **D5c (Controls)** — Most UI code. Independent of audio/video work.
4. **D5d (Verification)** — Cross-cutting review after all three are done.

## Key Files Reference

| File | Role |
|------|------|
| `port/src/config.c` | INI persistence engine |
| `port/src/audio.c` | SDL audio device, buffer management |
| `port/src/video.c` | SDL window, video modes, config vars |
| `port/src/input.c` | Key bindings, mouse, gamepad, serialization |
| `port/fast3d/pdgui_menu_mainmenu.cpp` | ImGui settings menu (all 4 tabs) |
| `port/fast3d/pdgui_audio.cpp` | UI sound bridge |
| `port/fast3d/pdgui_bridge.c` | C bridge for ImGui ↔ game data |
| `src/lib/snd.c` | Core sound system, SFX volume |
| `src/game/music.c` | Music sequencer, music volume |
| `src/game/options.c` | Volume getter/setter wrappers |
