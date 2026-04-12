# Audio Mod Menu — Design Document

> **Created**: 2026-04-11
> **Purpose**: Design for an in-game Audio Mod Menu that lets users import, create, audition, and manage audio mods (SFX, UX, voice, music); build soundtrack packs from mod tracks; and play custom soundtracks during matches/missions on a dedicated audio bus.
> **Status**: DESIGN — no implementation code yet. Phase 2 sessions implement from this doc.
> **Companion docs**: [menu-replacement-plan.md](menu-replacement-plan.md), [component-mod-architecture.md](../component-mod-architecture.md)

---

## 1. Reference Survey

### 1.1 Celeste Everest Mod Framework — Audio Pipeline

Everest uses FMOD Studio banks as its audio unit. Mods place `.bank` files in an `Audio/` directory; a companion `Audio/*.guids.txt` maps FMOD GUIDs to event paths. At init, `IngestNewBanks()` iterates all registered mod bank assets and loads them via `system.loadBankFile()`. Event lookup dictionaries (`cachedModEvents`, `cachedBankPaths`) merge mod and base events transparently.

**Key pattern**: Bank-level hot-loading into a running FMOD system. Mods can both replace and add events. No manifest beyond the FMOD guids.txt is required.

**Relevance to PD2**: PD2 does NOT use FMOD — it uses SDL2 raw audio (`SDL_QueueAudio` at 22050Hz S16 stereo) with a legacy N64 ADPCM RSP pipeline for base sounds. We cannot adopt FMOD's bank model, but we CAN adopt the "convention-over-configuration directory scan" pattern.

**Sources**: [Everest.Content.cs](https://github.com/EverestAPI/Everest/blob/dev/Celeste.Mod.mm/Mod/Everest/Everest.Content.cs), [Audio.cs](https://github.com/EverestAPI/Everest/blob/dev/Celeste.Mod.mm/Patches/Audio.cs). License: MIT.

### 1.2 FMOD Studio Custom Bank Loading

FMOD's `System::loadBankFile()` loads `.bank` files from disk at runtime. Banks are self-contained archives. The typical mod workflow: (1) discover `.bank` files, (2) `loadBankFile` each, (3) enumerate new events and merge into the event lookup table. Banks are additive — loading a new bank adds its events without replacing unless events share the same GUID/path.

**Relevance to PD2**: PD2 doesn't use FMOD. However, the "additive bank" concept maps to our design: mod audio directories are scanned at startup, their entries are registered in the catalog, and they're additive to the base game's 1545 SFX entries. Proprietary license (free < $200K revenue).

### 1.3 Beat Saber Custom Songs

Custom songs use OGG Vorbis format. Each custom song lives in its own folder under `CustomLevels/`, with an `info.dat` JSON manifest describing BPM, offset, and difficulty sets. The SongCore mod scans `CustomLevels/` at startup, reads each `info.dat`, and injects entries into the song selection UI alongside base game content.

**Key pattern**: Convention-over-configuration directory scanning. One folder per song with a standardized JSON manifest. Audio is a single OGG file. Custom entries are injected into the existing UI rather than building a separate menu.

**Relevance to PD2**: This maps directly to our component-mod architecture. A music mod would be: `mods/<slug>/audio.ini` + `mods/<slug>/track.ogg`. The catalog scanner already handles `audio/` directories with `ASSET_AUDIO` type. We adopt the "inject into existing soundtrack UI" approach.

**Sources**: [bsmg.wiki/mapping/basic-audio.html](https://bsmg.wiki/mapping/basic-audio.html), [bsmg.wiki/mapping/map-format/audio.html](https://bsmg.wiki/mapping/map-format/audio.html). License: CC BY-NC-SA 4.0 (wiki content).

### 1.4 Source Engine Sound Replacement (VPK)

Source uses text-based soundscript files (`scripts/game_sounds_*.txt`). Each entry has a name, channel, volume, soundlevel, pitch, and a `wave` field pointing to `.wav`/`.mp3` under `sound/`. The filesystem search path determines override priority — mod directories searched before base game. Mods override by name, not by file path.

**Key pattern**: Text manifest as the discovery mechanism, filesystem search path for override priority (mod > base), `sound/` directory convention.

**Relevance to PD2**: Our `audio.ini` component manifest plays the same role as Source's soundscript files. The catalog's last-write-wins semantics for `assetCatalogRegister()` provides the override priority.

**Sources**: [source-sdk-2013 SoundEmitterSystem.cpp](https://github.com/ValveSoftware/source-sdk-2013). License: Valve Source SDK license.

### 1.5 SDL2 Audio Mixing

SDL_mixer provides channels (numbered tracks for SFX) + a single music stream. `Mix_PlayMusic()` plays one music track at a time. For custom PCM mixing, `Mix_HookMusic()` injects raw audio data into the output pipeline. SDL_mixer supports FLAC, MP3, OGG, WAV.

**Relevance to PD2**: PD2 currently uses raw `SDL_QueueAudio` — NOT SDL_mixer. To add a mod soundtrack bus alongside the existing N64 ADPCM pipeline, we have two options: (A) migrate to SDL_mixer for multi-channel mixing, or (B) add a second SDL audio stream that mixes into the existing queue. **Option B is lower-risk** — it preserves the existing pipeline and only adds a parallel WAV/OGG playback path.

**Sources**: [wiki.libsdl.org/SDL2_mixer](https://wiki.libsdl.org/SDL2_mixer/FrontPage). License: zlib.

---

## 2. How PD2 Works Today — Grounded Analysis

### 2.1 Audio Backend

- **Backend**: SDL2 raw audio via `SDL_QueueAudio` at 22050Hz, AUDIO_S16SYS, stereo. No SDL_mixer.
- **Init**: `audioInit()` in `port/src/audio.c:57` opens the SDL audio device.
- **Volume layers**: Four layers persisted to pd.ini — Master, Music, Gameplay (SFX), UI (`port/src/audio.c:33-36`).
- **Engine hooks**: `musicSetVolume()` (music.c), `sndSetSfxVolume()` (snd.c), `pdguiPlaySound()` applies UI volume.
- **Mod SFX override**: `audioPlayFileSound(path, volume, pan)` at `port/src/audio.c:218` loads a WAV from disk, converts to device format via `SDL_AudioCVT`, applies volume/pan, and queues via `SDL_QueueAudio`. This bypasses the N64 ADPCM pipeline. Called from `snd.c:2174` when `r.is_mod_override` is true.

### 2.2 Music Track System

- **Base tracks**: `g_MpTracks[]` array in `src/game/mplayer/mplayer.c`. Each entry has `musicnum` (N64 sequencer index) and `name` (langID for display). Tracks are selected via `mpGetNumUnlockedTracks()` (`mplayer.c:3220`).
- **Persistence**: Single-tune `g_BossFile.tracknum` + multi-tune `g_BossFile.multipletracknums[16]` bitmask + `g_BossFile.usingmultipletunes` flag.
- **Network**: Base game music is LOCAL-ONLY (each client picks its own music independently at match start via `mpMusicStart()`). **Mod audio will be network-synced** via the mod propagation pipeline (see §6.6).
- **NOT in catalog**: Base game music tracks are NOT registered in the asset catalog. They exist only as the legacy `g_MpTracks[]` array.

### 2.3 Catalog Audio Types (exist but mostly unused)

- **`ASSET_AUDIO`** (enum index 20): Full ext union member — `ext.audio.sound_id`, `ext.audio.name[64]`, `ext.audio.category` (AUDIO_CAT_SFX=0 / AUDIO_CAT_MUSIC=1 / AUDIO_CAT_VOICE=2), `ext.audio.duration_ms`, `ext.audio.file_path[128]`. Defined in `port/include/assetcatalog.h:244-250`.
- **Registration**: 1545 base SFX entries registered as `base:sfx_%04x` at `port/src/assetcatalog_base_extended.c:452-466`. Category = AUDIO_CAT_SFX. **Zero AUDIO_CAT_MUSIC entries registered today.**
- **`ASSET_SFX`** (enum index 7): Pack type, no ext fields. `assetCatalogRegisterSfx()` exists but is never called.
- **`ASSET_MUSIC`** (enum index 8): Pack type, no ext fields. Never registered.
- **Scanner support**: `port/src/assetcatalog_scanner.c` already handles `"audio"`, `"sfx"`, `"music"` directory types and parses `audio.ini` with sound_id, name, category, duration_ms, file_path fields (scanner lines 222/238-239/397-403).

### 2.4 Batch 12 Soundtrack UI

- **`renderSoundtrack`** at `port/fast3d/pdgui_menu_mpsettings.cpp:680`: Shows current track name, "Select Tune"/"Select Tunes" button (pushes `g_MpSelectTunesMenuDialog`), "Multiple Tunes" checkbox.
- **`renderSelectTunes`** at `port/fast3d/pdgui_menu_mpsettings.cpp:531`: Lists `mpGetNumUnlockedTracks()` base game tracks. Single-tune mode: click selects one. Multi-tune mode: checkboxes per track + Select All / Select None / Randomize buttons.
- **Pattern**: Both delegate to legacy handlers in `setup.c` via the shadow menuitem/handlerdata ABI (same as all D5 P3 batches).

### 2.5 Modding Hub Integration Point

- **`pdgui_menu_moddinghub.cpp`**: Standalone window with 3 tabs — Mod Manager (tab 0), INI Editor (tab 1), Model Scale (tab 2).
- **Entry**: `pdguiModdingHubShow()` from main menu "Modding..." button.
- **Extension point**: Audio Mod Menu could be tab 3 or a sub-screen launched from the Mod Manager tab.

### 2.6 Existing Mod Save Pattern

- Theme editor's `saveThemeAsMod()` at `pdgui_menu_theme_editor.cpp:119` demonstrates: (1) sanitize name to dirname, (2) `fsCreateDir("mods/<slug>")`, (3) write JSON manifest, (4) call `pdguiThemeRegisterModDir()` for immediate availability without restart.

---

## 3. Concrete Design Proposal

### 3.1 Catalog Extension — Music Track Registration

**Problem**: Base game music tracks live in `g_MpTracks[]`, not the catalog. Mod music tracks need to be discoverable alongside base tracks. The soundtrack UI reads `g_MpTracks[]` directly.

**Solution**: Register base game music tracks as `ASSET_AUDIO` entries with `AUDIO_CAT_MUSIC` at startup.

```
New registration in assetcatalog_base_extended.c:
  for (s32 i = 0; i < ARRAYCOUNT(g_MpTracks); i++) {
    snprintf(idbuf, sizeof(idbuf), "base:music_%04x", g_MpTracks[i].musicnum);
    e = assetCatalogRegisterAudio(idbuf, g_MpTracks[i].musicnum,
                                   langGet(g_MpTracks[i].name),
                                   AUDIO_CAT_MUSIC, 0, "");
    e->bundled = 1; e->enabled = 1;
    e->load_state = ASSET_STATE_LOADED; e->ref_count = ASSET_REF_BUNDLED;
  }
```

**New catalog resolution**: Add `catalogResolveAudio()` to `assetcatalog_resolve.c`:
```
typedef struct {
    const asset_entry_t *entry;
    s32 sound_id;
    s32 category;  // AUDIO_CAT_SFX/MUSIC/VOICE
    const char *file_path;  // disk path for mod audio, "" for ROM
} catalog_audio_result_t;

s32 catalogResolveAudio(const char *id, catalog_audio_result_t *out);
```

**[DECIDED]**: All ~30 base music tracks from `g_MpTracks[]` are registered as `ASSET_AUDIO` / `AUDIO_CAT_MUSIC` catalog entries with `base:` prefix. The game sees mod content natively alongside base content — both are catalog entries, distinguished only by the mod tag (`base:` vs `modslug:`). Uniform catalog queries for all tracks, no special-casing. Registering ~30 base tracks is cheap (same pattern as 1545 SFX entries). Code reading `g_MpTracks[]` directly must be updated to also query the catalog for mod tracks.

### 3.2 Mod Audio Component — On-Disk Format

A music mod component follows the existing component-mod architecture:

```
mods/my-soundtrack/
  audio.ini          # INI manifest
  track.ogg          # Audio file (WAV or OGG)
```

**audio.ini**:
```ini
[audio]
type = audio
name = My Custom Track
category = 1          # AUDIO_CAT_MUSIC
duration_ms = 180000  # 3 minutes
file_path = track.ogg
```

An SFX override mod replaces a base game sound by matching `sound_id`:
```ini
[audio]
type = audio
name = Custom Explosion
category = 0          # AUDIO_CAT_SFX
sound_id = 0x0042     # overrides base:sfx_0042
file_path = explosion.wav
```

**Supported import formats**: MP3, WAV, and OGG are all accepted at import time. On import, the file is converted to the native playback format (WAV at device sample rate) and saved as the mod's audio file. This means the mod system stores a single format internally while accepting common formats from users.

**Import conversion pipeline**: `skinEditorImportAudio(path)` detects format by extension, decodes via the appropriate library (stb_vorbis for OGG, dr_mp3 or minimp3 for MP3, SDL_LoadWAV for WAV), resamples to 22050Hz S16 stereo if needed, and writes the result as the mod's `track.wav`. The original file is not kept.

**Dependencies**: OGG requires stb_vorbis (single-header, public domain). MP3 requires minimp3 (single-header, CC0). Both are zero-cost vendored headers.

### 3.3 Mod Soundtrack Bus — Playback Architecture

**Problem**: The existing music pipeline (`musicSetVolume`, N64 sequencer via `sndp`) plays base game MIDI-like sequences. Mod music is PCM audio (WAV/OGG). They need separate playback paths with independent volume control.

**Proposed architecture**: Add a **mod music stream** that runs parallel to the existing N64 sequencer. The stream is a simple state machine:

```
New file: port/src/modmusic.c

State:
  static s16 *s_ModMusicPCM = NULL;    // Decoded PCM buffer
  static u32  s_ModMusicLen = 0;       // Buffer length in bytes
  static u32  s_ModMusicPos = 0;       // Current playback position
  static s32  s_ModMusicPlaying = 0;   // Playing flag
  static f32  s_ModMusicVolume = 1.0f; // 0.0-1.0 mod music volume

API:
  void modMusicPlay(const char *file_path);   // Load + start
  void modMusicStop(void);                     // Stop + free
  void modMusicSetVolume(f32 vol);
  s32  modMusicIsPlaying(void);
  void modMusicMixInto(s16 *outBuf, u32 numSamples);  // Mix into frame
```

**Integration point**: `audioEndFrame()` in `port/src/audio.c:108` currently queues the N64 RSP output. We add `modMusicMixInto()` BEFORE `SDL_QueueAudio` to mix mod music PCM into the existing output buffer.

```
void audioEndFrame(void) {
    if (nextBuf && nextSize) {
        if (audioGetSamplesBuffered() < queueLimit) {
            // NEW: mix mod music into the N64 output buffer
            if (s_ModMusicPlaying) {
                modMusicMixInto((s16 *)nextBuf, nextSize / 4);
            }
            SDL_QueueAudio(dev, nextBuf, nextSize);
        }
        ...
    }
}
```

**Volume**: Mod music volume is `g_AudioMasterVolume * g_AudioMusicVolume * s_ModMusicVolume`. This uses the existing Music volume layer so the player's "Music Volume" slider controls both N64 sequences and mod music.

**Muting base music**: When a mod track is active, the base N64 sequencer is silenced via `musicSetVolume(0)` at the `audio.c` level — our own volume control system, NOT the legacy N64 sequencer mute. When the mod track ends or the user switches back to a base track, `musicSetVolume()` is restored to the player's Music volume setting. **[DECIDED: use our volume control, not sequencer mute]**

**Self-critique**: Mixing into the N64 output buffer means mod music and N64 music play simultaneously if both are active. The above `musicSetVolume(0)` call prevents this. If `nextBuf` is const (the N64 RSP output is read-only), we'd need a separate mix buffer. Checking `audio.c:10`: `static const s16 *nextBuf` — it IS const. Solution: allocate a separate mix buffer in `audioEndFrame`.

### 3.4 Soundtrack Pack System

A "soundtrack pack" is a collection of mod music tracks that the player can enable as their match/mission music. Implemented as a new mod component type.

```
mods/my-pack/
  mod.json
  tracks/
    track1.ogg
    track2.ogg
    track3.ogg
```

**mod.json** for a pack:
```json
{
  "name": "my-pack",
  "display_name": "Retro Beats Pack",
  "version": "1.0.0",
  "category": "music",
  "components": [
    {
      "type": "audio",
      "catalog_id": "my-pack:track1",
      "name": "Track One",
      "category": 1,
      "file_path": "tracks/track1.ogg"
    },
    {
      "type": "audio",
      "catalog_id": "my-pack:track2",
      "name": "Track Two",
      "category": 1,
      "file_path": "tracks/track2.ogg"
    }
  ]
}
```

The scanner registers each component as an `ASSET_AUDIO` entry with `AUDIO_CAT_MUSIC`. The pack itself is the mod directory — no separate "pack" catalog type needed. The soundtrack UI queries `assetCatalogIterateByType(ASSET_AUDIO, ...)` filtered to `category == AUDIO_CAT_MUSIC` to discover all available tracks.

### 3.5 Audio Mod Menu UI

**Location**: New tab (index 3) in the Modding Hub (`pdgui_menu_moddinghub.cpp`), labeled "Audio Mods".

**Layout** (3 panels):

```
+--------------------------------------------------+
| Audio Mods                                        |
|                                                   |
| [SFX] [Music] [Voice] [Packs]   <- category tabs |
|                                                   |
| +--List Panel (left)--+ +--Details (right)------+ |
| | base:sfx_0001       | | Name: Explosion       | |
| | base:sfx_0002       | | Type: SFX             | |
| | mymod:custom_bang   | | Duration: 250ms       | |
| | ...                  | | File: explosion.wav   | |
| |                      | |                       | |
| |                      | | [> Play]  [|| Stop]   | |
| |                      | | [Import New...]       | |
| +----------------------+ +-----------------------+ |
|                                                   |
| [Create Pack...]  [Import Audio...]               |
+--------------------------------------------------+
```

**List panel**: Iterates catalog entries with `assetCatalogIterateByType(ASSET_AUDIO, ...)`, filtered by the selected category tab. **Mod tracks appear inline alongside base content** — the game sees them the same way, distinguished only by mod tag. Base entries shown dimmed (not editable), mod entries shown normally. No separate "mod audio" dialog — mod content is first-class. **[DECIDED: inline display, not segregated]**

**Details panel**: Shows metadata for the selected entry. "Play" button calls `audioPlayFileSound()` for SFX/Voice or `modMusicPlay()` for Music.

**Import**: Opens a file dialog (or text input for path) to import a WAV/OGG file. Creates a new mod directory under `mods/`, writes `audio.ini`, registers in catalog via the same pattern as `saveThemeAsMod()` in the theme editor.

**Create Pack**: Opens a sub-dialog where the user selects multiple music tracks (checkboxes), names the pack, and saves it as a new mod directory.

### 3.6 Soundtrack Menu Extension

**Current**: `renderSoundtrack` in `pdgui_menu_mpsettings.cpp:680` shows base game tracks only.

**Extension**: Add a collapsible "Mod Tracks" section below the base game tracks in `renderSelectTunes`. This section lists all `ASSET_AUDIO` entries with `category == AUDIO_CAT_MUSIC` that are NOT base game (i.e., `!entry->bundled`). Mod tracks feel first-class — same interaction, same selection behavior, same visual weight as base tracks. The collapsible section is an organizational aid, not a segregation boundary. **[DECIDED: collapsible section OK, mod content first-class]**

```
renderSelectTunes (extended):
  [Base Game Tracks]
    > Track 1 - dataDyne Central
    > Track 2 - Carrington Villa
    ...

  [v Mod Tracks]           <- collapsible, starts collapsed
    > my-pack:track1 - Track One
    > my-pack:track2 - Track Two
    ...
```

When a mod track is selected:
1. Set `g_BossFile.tracknum` to a sentinel value (e.g., -2 = "mod track")
2. Store the selected mod track's catalog ID in a new field: `g_BossFile.mod_track_id[64]`
3. At match start, `mpMusicStart()` checks: if `tracknum == -2`, call `modMusicPlay(entry->ext.audio.file_path)` instead of the N64 sequencer path

**Network**: Mod audio is **network-synced**. When a mod track is shared, selected for download, used in a match, or included in a manifest, it is distributed to connected players via the existing mod propagation pipeline. This means all players in a match can hear the host's custom soundtrack if they accept the mod download. **[DECIDED: network-synced via mod propagation pipeline, NOT local-only]**

**Self-critique**: Adding `mod_track_id[64]` to `g_BossFile` changes the save format. This requires a SAVE_VERSION bump. Alternative: store the mod track selection in pd.ini via `configRegisterString()` instead of g_BossFile. **Recommend pd.ini** — avoids save format change and is consistent with how volume layers are stored.

---

## 4. Controller Input Model

### 4.1 Input Context

The Audio Mod Menu uses `g_CtxImGuiMenu` / `g_ImcMenu` — the standard menu input context. No new IMC or input context required. This is critical: the input system was just fixed for cross-contamination (S208 Issue 2, `actionmapLoadBinds` break fix). Adding a new IMC for audio browsing would risk re-introducing cross-contamination.

### 4.2 Button Assignments

| Action | Keyboard | Controller | Notes |
|--------|----------|------------|-------|
| Navigate list | Arrow Up/Down | D-pad Up/Down, Left Stick | Standard menu nav (ACTION_MENU_UP/DOWN) |
| Select entry | Enter | A button | ACTION_USE |
| Back / Close | Escape | B button | ACTION_CANCEL_USE |
| Category tab cycle | — | LB / RB | ACTION_MENU_TAB_PREV/NEXT → ImGuiKey_PageUp/PageDown (S208 fix) |
| Play preview | Space | X button | New: mapped within renderAudioMods via ImGuiKey check |
| Stop preview | Space (toggle) | X button (toggle) | Same key, toggle behavior |
| Import | — | Y button | New: mapped via ImGuiKey check |

### 4.3 Mode Transitions

```
Main Menu → Modding... → [Audio Mods tab]
  g_CtxImGuiMenu already on stack (pushed by modding hub)
  No additional push/pop needed

Soundtrack menu (in-match settings):
  Main Menu → Combat Simulator → Room → Settings → Soundtrack
  g_CtxImGuiMenu on stack via room/settings push
  Extended renderSelectTunes shows mod tracks inline
  No additional context push
```

**Self-critique**: The "Play" button binding (X / Space) is not an ACTION_* binding — it's a direct ImGuiKey check in the renderer. This is consistent with how other menus handle per-screen bindings (e.g., Batch 12 "Select All" / "Randomize" buttons are direct ImGui button clicks, not action bindings).

---

## 5. Phased Implementation Plan

### Batch A-1: Catalog Audio Extension (foundation)
**Scope**: Register base game music tracks in catalog. Add `catalogResolveAudio()`. Add `catalog_audio_result_t`.
**Files touched**: `port/src/assetcatalog_base_extended.c` (+30), `port/src/assetcatalog_resolve.c` (+40), `port/include/assetcatalog.h` (+15)
**Dependencies**: None
**Network**: No wire changes
**Build impact**: Client +1KB, server unchanged
**Acceptance**: `assetCatalogGetCountByType(ASSET_AUDIO)` returns base SFX count + base music track count. `catalogResolveAudio("base:music_0000", &r)` succeeds.

### Batch A-2: Mod Music Stream (modmusic.c)
**Scope**: New `port/src/modmusic.c` + `port/include/modmusic.h`. WAV loading, PCM playback, volume control, mixing into `audioEndFrame`.
**Files touched**: NEW `port/src/modmusic.c` (~200 lines), NEW `port/include/modmusic.h` (~20 lines), `port/src/audio.c` (+15 — mix buffer + modMusicMixInto call in audioEndFrame)
**Dependencies**: Batch A-1
**Network**: No wire changes. Mod music is client-local.
**Build impact**: Client +5KB, server unchanged (modmusic.c outside SRC_SERVER whitelist)
**Acceptance**: `modMusicPlay("mods/test/track.wav")` plays audio. Volume responds to Music volume slider. Stops cleanly on `modMusicStop()`.

### Batch A-3: Audio Mod Menu UI
**Scope**: New tab in Modding Hub for browsing/auditioning/importing audio mods. Import writes `audio.ini` + registers in catalog.
**Files touched**: `port/fast3d/pdgui_menu_moddinghub.cpp` (+300 — new tab, list panel, details panel, import flow), NEW `port/fast3d/pdgui_menu_audiomod.cpp` (~600 lines — if standalone file preferred)
**Dependencies**: Batch A-1, A-2
**Network**: No wire changes
**Build impact**: Client +15KB, server unchanged
**Acceptance**: Tab visible in Modding Hub. Lists all ASSET_AUDIO entries. Play button works for SFX (via audioPlayFileSound) and Music (via modMusicPlay). Import creates a valid mod directory that persists across restart.

### Batch A-4: Soundtrack Menu Extension
**Scope**: Extend `renderSelectTunes` with collapsible "Mod Tracks" section. Store mod track selection in pd.ini. Wire `mpMusicStart` to read mod track selection.
**Files touched**: `port/fast3d/pdgui_menu_mpsettings.cpp` (+80 — mod tracks section in renderSelectTunes), `port/src/audio.c` (+5 — configRegisterString for mod track selection), `src/game/mplayer/mplayer.c` (+20 — mpMusicStart check for mod track)
**Dependencies**: Batch A-2, A-3
**Network**: Mod track selection stored locally in pd.ini. Network distribution handled by Batch A-7 via mod propagation pipeline.
**Build impact**: Client +3KB, server unchanged
**Acceptance**: Mod tracks appear in Soundtrack menu below base tracks. Selecting a mod track plays it during match. Base game tracks still work. pd.ini persists selection.

### Batch A-5: Soundtrack Pack Creation
**Scope**: "Create Pack" dialog in Audio Mod Menu. Multi-select music tracks, name the pack, save as mod directory.
**Files touched**: `port/fast3d/pdgui_menu_moddinghub.cpp` or `pdgui_menu_audiomod.cpp` (+200 — pack creation dialog)
**Dependencies**: Batch A-3
**Network**: None
**Build impact**: Client +5KB
**Acceptance**: Creating a pack writes a valid mod directory with mod.json listing multiple audio components. Pack tracks appear in Soundtrack menu after creation.

### Batch A-6: Multi-Format Import (MP3 + WAV + OGG) — v1 REQUIRED
**Scope**: Import accepts MP3, WAV, and OGG files. On import, decode to native PCM and save as mod. Add stb_vorbis (public domain) for OGG and minimp3 (CC0) for MP3. Extend `audioPlayFileSound` and `modMusicPlay` to handle all three input formats. **[DECIDED: all three formats in v1, not deferred]**
**Files touched**: NEW `port/external/stb_vorbis.c` (vendored), NEW `port/external/minimp3.h` (vendored), `port/src/audio.c` (+50 — OGG + MP3 loading paths), `port/src/modmusic.c` (+30 — format detection + decode)
**Dependencies**: Batch A-2
**Network**: None
**Build impact**: Client +60KB (stb_vorbis ~5000 lines + minimp3 ~2000 lines)
**Acceptance**: `.mp3`, `.wav`, and `.ogg` files all import correctly and play via both audioPlayFileSound and modMusicPlay. Format conversion on save produces valid output.

### Batch A-7: Network Sync for Mod Audio
**Scope**: Integrate mod audio with the existing mod propagation pipeline. When a mod track is shared, selected for download, used in a match, or included in a manifest, distribute it to connected players. **[DECIDED: network-synced, uses existing mod propagation]**
**Files touched**: `port/src/net/modpropagation.c` (+40 — audio mod type registration), `port/src/modmusic.c` (+20 — propagation hooks)
**Dependencies**: Batch A-4, existing mod propagation pipeline
**Network**: Uses existing mod propagation wire protocol — no new message types needed, just a new mod content type
**Build impact**: Client +2KB, server +1KB
**Acceptance**: Host with a mod soundtrack propagates it to connected clients. Clients can hear the host's custom music during match.

---

## 6. Resolved Design Questions

> All questions resolved 2026-04-11 by game director (Mike).

### 6.1 Mod Music Bus Architecture — RESOLVED
**Question**: Use N64 sequencer mute or our own volume control to silence base music during mod playback?
**Decision**: Use OUR volume control system — `musicSetVolume(0)` or equivalent at the `audio.c` level. NOT the legacy N64 sequencer mute. This keeps control in our audio pipeline where we can manage it cleanly.

### 6.2 Audio Catalog Registration Scope — RESOLVED
**Question**: Register ~30 base music tracks as catalog entries alongside the 1545 SFX entries?
**Decision**: YES — register all ~30 base music tracks from `g_MpTracks[]` as `ASSET_AUDIO` / `AUDIO_CAT_MUSIC` catalog entries. The game sees mod content natively, same as base game content. Distinguished only by mod tag (`base:` vs `modslug:`). Uniform catalog queries for all tracks.

### 6.3 Mod.json vs audio.ini for Audio Components — RESOLVED
**Question**: Standardize on one format or allow both?
**Decision**: No preference on format duality — the game client / mod system interfaces handle creation, users won't interact with mod files directly. Keep `audio.ini` for singles and `mod.json` for packs. Both paths already work in the scanner.

### 6.4 Import Format Support — RESOLVED
**Question**: WAV-only for v1, or add OGG?
**Decision**: Import accepts MP3, WAV, AND OGG — all three formats in v1. Convert to native format when saving as mod. No deferred format support. Dependencies: stb_vorbis (public domain) for OGG, minimp3 (CC0) for MP3.

### 6.5 Soundtrack UI Location — RESOLVED
**Question**: Mod tracks inline in existing Soundtrack menu, or separate dialog?
**Decision**: Mod tracks appear natively alongside base content — the game sees them the same way, only distinguished by mod tag. Design should reflect this: mod audio shows inline with base audio, not segregated into a separate dialog. The collapsible section approach is fine as long as mod content feels first-class.

### 6.6 Network Sync — RESOLVED (added by director)
**Question**: Should mod audio be local-only or network-synced?
**Decision**: Mod audio is **network-synced**. Distributed to connected players when shared, selected for download, used in a match, or included in a manifest. Uses the existing mod propagation pipeline — no new wire protocol needed, just a new mod content type registration.

---

## 7. Data Structures Summary

### New Files
| File | Purpose | Lines (est.) |
|------|---------|-------------|
| `port/src/modmusic.c` | Mod music stream playback | ~200 |
| `port/include/modmusic.h` | Public API for modmusic | ~20 |
| `port/fast3d/pdgui_menu_audiomod.cpp` | Audio Mod Menu UI (optional standalone) | ~600 |

### Modified Files
| File | Change | Lines (est.) |
|------|--------|-------------|
| `port/src/assetcatalog_base_extended.c` | Register base music tracks | +30 |
| `port/src/assetcatalog_resolve.c` | `catalogResolveAudio()` | +40 |
| `port/include/assetcatalog.h` | `catalog_audio_result_t` + decl | +15 |
| `port/src/audio.c` | Mix buffer, modMusicMixInto call, config string | +25 |
| `port/fast3d/pdgui_menu_mpsettings.cpp` | Mod tracks in renderSelectTunes | +80 |
| `port/fast3d/pdgui_menu_moddinghub.cpp` | Audio Mods tab + integration | +300 |
| `src/game/mplayer/mplayer.c` | mpMusicStart mod track check | +20 |

### No Changes To
- `pdgui_menu_solomission.cpp` (standing rule)
- `port/fast3d/pdgui_backend.cpp` (no new render hooks)
- Build files (CMake GLOB_RECURSE auto-discovers new .c/.cpp)

**Note**: `port/src/net/modpropagation.c` IS touched (Batch A-7) for network sync of mod audio.
