# Phase D3: Dynamic Mod Loader — Implementation Plan

## Overview

Replace the hardcoded mod system (5 static mod directories, `g_ModNum` global, CLI args) with a dynamic mod manager that:

1. Scans a `mods/` directory for mod packs at startup
2. Loads `mod.json` manifests (with `modconfig.txt` legacy fallback)
3. Registers assets into dynamically-allocated tables (bodies, heads, arenas, stages)
4. Provides an in-game Mod Manager menu for enable/disable with hot-toggle
5. Synchronizes mod lists over the network, with auto-download for missing mods
6. Converts the 5 existing mods to standard `mod.json` format

## Design Principles

- **Server-authoritative mods**: In netplay, the server's mod list is canonical. Clients must match.
- **Hot-toggle via soft reload**: Enabling/disabling a mod rebuilds asset tables and returns to the title screen. No process restart needed.
- **Backwards-compatible**: Legacy `modconfig.txt` mods still work (auto-wrapped in a generated manifest).
- **Stable network IDs**: Assets identified by `"modid:assetid"` string hashes (CRC32), not array indices. Indices can shift when mods are toggled; hashes are stable.
- **Extend PD's menu system**: New menu item types where needed, same renderer.
- **Unified lobby with party leader**: The multiplayer lobby (MP and co-op) will be a single unified screen. Match controls (map, mode, settings, mod requirements) are controlled by the **party leader**, who is not necessarily the host/server. The host runs the server process, but the party leader role can be assigned/transferred. This decouples server authority (game simulation) from session management (match configuration). The mod manager interacts with this: the party leader's enabled mod list becomes the session's required mod list.

---

## Sub-Phase D3.1: Mod Manager Core (`modmgr.c/h`)

### New Files
- `port/include/modmgr.h` — Public API + structs
- `port/src/modmgr.c` — Implementation

### Data Structures

```c
// --- modmgr.h ---

#define MODMGR_MAX_MODS        32
#define MODMGR_MAX_BODIES       128
#define MODMGR_MAX_HEADS        128
#define MODMGR_MAX_ARENAS       128
#define MODMGR_MAX_STAGES       128
#define MODMGR_MOD_ID_LEN       64
#define MODMGR_MOD_NAME_LEN     128
#define MODMGR_MOD_DIR          "mods"

// Mod state
typedef struct modinfo {
    char id[MODMGR_MOD_ID_LEN];           // e.g. "goldfinger64"
    char name[MODMGR_MOD_NAME_LEN];       // e.g. "Goldfinger 64"
    char version[32];                       // e.g. "1.0.0"
    char author[64];
    char description[256];
    char dirpath[FS_MAXPATH];              // absolute path to mod directory
    u32 hash;                              // SHA-256 truncated to u32 for quick compare
    bool enabled;                          // user toggle
    bool loaded;                           // assets currently in tables
    bool bundled;                          // shipped with the game (former hardcoded mods)
    bool has_modconfig;                    // has legacy modconfig.txt
    bool has_modjson;                      // has mod.json
    // Content counts (from manifest)
    s32 num_stages;
    s32 num_bodies;
    s32 num_heads;
    s32 num_arenas;
} modinfo_t;

// Registered asset with provenance tracking
typedef struct modasset {
    char mod_id[MODMGR_MOD_ID_LEN];       // which mod owns this
    char asset_id[64];                      // e.g. "gf64_bond"
    u32 nethash;                           // CRC32 of "modid:assetid" for network
} modasset_t;

// Global state
extern modinfo_t   g_ModRegistry[MODMGR_MAX_MODS];
extern s32         g_ModRegistryCount;
extern bool        g_ModManagerInitialized;
```

### API

```c
// Lifecycle
void modmgrInit(void);                     // Scan mods/, load manifests, build registry
void modmgrShutdown(void);                 // Free dynamic tables
void modmgrReload(void);                   // Rebuild asset tables from enabled mods (hot-toggle)

// Registry
s32  modmgrGetCount(void);                 // Number of discovered mods
modinfo_t *modmgrGetMod(s32 index);        // Get mod by index
modinfo_t *modmgrFindMod(const char *id);  // Find mod by ID string

// Enable/Disable
void modmgrSetEnabled(s32 index, bool enabled); // Toggle mod, mark dirty
bool modmgrIsDirty(void);                  // True if enable state changed since last reload
void modmgrApplyChanges(void);             // Rebuild tables + return to title (soft reload)

// Config persistence
void modmgrSaveConfig(void);               // Write enabled/disabled states to config
void modmgrLoadConfig(void);               // Read enabled/disabled states from config

// Network
u32  modmgrGetManifestHash(void);          // Combined hash of all enabled mods (for quick compare)
s32  modmgrWriteManifest(u8 *buf, s32 maxlen); // Serialize enabled mod list for network
s32  modmgrReadManifest(const u8 *buf, s32 len, char *missing, s32 misslen); // Compare against local
```

### Scanning Logic (`modmgrInit`)

```
1. Enumerate subdirectories of "mods/"
2. For each subdirectory:
   a. Check for mod.json → parse with cJSON (already in port/src/ as a dependency candidate)
      OR use a minimal JSON parser (strtok-based for our simple format)
   b. Else check for modconfig.txt → create synthetic manifest (legacy mode)
   c. Populate modinfo_t entry
3. Sort by: bundled mods first, then alphabetical by name
4. Load persisted enable/disable state from config
5. For each enabled mod: call modmgrLoadMod() to register assets
```

### Asset Registration (`modmgrLoadMod`)

For each enabled mod, in order:
1. Set `g_ModNum` equivalent context (mod directory for fsFullPath resolution)
2. Parse `modconfig.txt` via existing `modConfigLoad()` (stage configs, allocations, music, weather)
3. Parse `mod.json` content sections for new assets (bodies, heads, arenas)
4. Append to dynamic asset tables (see D3.2)
5. Mark mod as `loaded = true`

---

## Sub-Phase D3.2: Dynamic Asset Tables

### Current State (Static)

| Array | File | Size | Struct |
|-------|------|------|--------|
| `g_MpBodies[]` | mplayer.c:2150 | 63 | `struct mpbody` (7 bytes: bodynum, name, headnum, requirefeature) |
| `g_MpHeads[]` | mplayer.c:1988 | 76 | `struct mphead` (3 bytes: headnum, requirefeature) |
| `g_MpArenas[]` | setup.c:108 | 75 | `struct mparena` (5 bytes: stagenum, requirefeature, name) |
| `g_Stages[]` | stagetable.c:11 | 87 | `struct stagetableentry` (60 bytes) |

### Conversion Strategy

**Option chosen: Shadow dynamic arrays with base + mod segments**

Rather than converting the static arrays to heap-allocated (which would break every pointer/index reference), we use a layered approach:

```c
// In modmgr.c
static struct mpbody  g_ModBodies[MODMGR_MAX_BODIES];   // mod-added bodies
static s32            g_ModBodyCount = 0;

static struct mphead  g_ModHeads[MODMGR_MAX_HEADS];      // mod-added heads
static s32            g_ModHeadCount = 0;

static struct mparena g_ModArenas[MODMGR_MAX_ARENAS];    // mod-added arenas
static s32            g_ModArenaCount = 0;

// Provenance tracking
static modasset_t     g_ModBodyAssets[MODMGR_MAX_BODIES];
static modasset_t     g_ModHeadAssets[MODMGR_MAX_HEADS];
static modasset_t     g_ModArenaAssets[MODMGR_MAX_ARENAS];
```

**Modified accessor functions:**

```c
// mplayer.c — patched
s32 mpGetNumBodies(void) {
    return ARRAYCOUNT(g_MpBodies) + g_ModBodyCount;
}

struct mpbody *mpGetBody(s32 index) {
    if (index < ARRAYCOUNT(g_MpBodies)) return &g_MpBodies[index];
    s32 modidx = index - ARRAYCOUNT(g_MpBodies);
    if (modidx < g_ModBodyCount) return &g_ModBodies[modidx];
    return NULL; // out of range
}

// Same pattern for heads and arenas
```

This approach:
- Keeps base game arrays untouched (zero regression risk)
- Mod assets are appended after base entries
- On hot-toggle, only the mod shadow arrays are cleared and rebuilt
- All existing code using `g_MpBodies[index]` still works for base indices
- New code and network code uses `mpGetBody(index)` accessor

**Network hash lookup:**

```c
// For network messages, convert index → nethash and back
u32 modmgrBodyHash(s32 index) {
    if (index < ARRAYCOUNT(g_MpBodies)) {
        return crc32_str("base:body_%d", index); // stable for base content
    }
    return g_ModBodyAssets[index - ARRAYCOUNT(g_MpBodies)].nethash;
}

s32 modmgrBodyFromHash(u32 hash) {
    // Search base first, then mod tables
    for (s32 i = 0; i < ARRAYCOUNT(g_MpBodies); i++) {
        if (crc32_str("base:body_%d", i) == hash) return i;
    }
    for (s32 i = 0; i < g_ModBodyCount; i++) {
        if (g_ModBodyAssets[i].nethash == hash) return ARRAYCOUNT(g_MpBodies) + i;
    }
    return -1; // not found (mod not installed)
}
```

---

## Sub-Phase D3.3: mod.json Manifest Format

### Schema

```json
{
    "id": "goldfinger64",
    "name": "Goldfinger 64",
    "version": "1.0.0",
    "author": "Community",
    "description": "Goldfinger 64 maps and characters for Perfect Dark",
    "requires": [],
    "content": {
        "stages": [
            {
                "id": "gf64_temple",
                "stagenum": "0x3d",
                "name": "Temple",
                "mp": true,
                "solo": false
            }
        ],
        "bodies": [
            {
                "id": "gf64_bond",
                "name": "James Bond",
                "bodyfile": "files/Cbond_bodyZ",
                "headfile": "files/Hbond_headZ",
                "headnum": 1000
            }
        ],
        "heads": [
            {
                "id": "gf64_natalya",
                "name": "Natalya",
                "headfile": "files/Hnatalya_headZ"
            }
        ],
        "arenas": [
            {
                "id": "gf64_temple_mp",
                "stagenum": "0x3d",
                "name": "Temple"
            }
        ]
    },
    "modconfig": "modconfig.txt"
}
```

### Parsing

Use a minimal JSON parser. Options:
- **cJSON** (single .c/.h, MIT license, widely used) — recommended
- **jsmn** (header-only, even smaller)
- Custom strtok-based (fragile, not recommended)

**Recommendation**: Bundle `cJSON.c`/`cJSON.h` in `port/src/` and `port/include/`. It's ~1700 lines, zero dependencies, handles our needs perfectly.

### Legacy Fallback

If a mod directory has `modconfig.txt` but no `mod.json`:
1. Auto-generate a synthetic `modinfo_t` with:
   - `id` = directory name (e.g., "mod_gex" → "gex")
   - `name` = directory name prettified
   - `version` = "0.0.0"
   - Content counts = 0 (stages come from modconfig.txt parsing, not manifest)
2. Set `has_modconfig = true`, `has_modjson = false`
3. Load via existing `modConfigLoad()` path

---

## Sub-Phase D3.4: Menu System Modernization + Mod Manager Menu

### Architecture Overview

The PD menu system is being replaced with a modern ImGui-based UI. The old system used
static `struct menuitem[]` arrays compiled into the binary with a fixed set of widget types,
bitmap fonts, and per-dialog color schemes. The new system uses Dear ImGui (SDL2 + OpenGL3
backends) with a fluent builder API (`dynmenu`) for procedural menu construction.

**Key design goals:**
- Dynamic/procedural menu construction from runtime data
- Rich widget set: tabs, collapsible sections, carousels, tables, 3D model viewports
- Custom fonts (TTF), per-widget color control, widescreen layout
- PD-themed visual style (custom ImGui theme, not default grey)
- All menus will eventually be migrated; new screens use the new system immediately

### Integration Architecture

```
  PD Game Loop (main.c)
       │
       ├─ videoStartFrame()          ── gfx_start_frame()
       ├─ [game logic / scene build]
       ├─ videoSubmitCommands(cmds)   ── gfx_run() → N64 GBI → OpenGL
       ├─ pdguiNewFrame()            ── ImGui_ImplOpenGL3_NewFrame() + SDL2 NewFrame
       ├─ pdguiRenderMenus()         ── dynmenu builder calls → ImGui draw calls
       ├─ pdguiEndFrame()            ── ImGui::Render() → ImGui_ImplOpenGL3_RenderDrawData()
       └─ videoEndFrame()            ── buffer swap
```

ImGui renders as an overlay AFTER PD's GBI commands but BEFORE the buffer swap. Both
renderers share the same OpenGL context and SDL2 window. ImGui's depth test is disabled
(2D overlay), so it composites cleanly on top of PD's 3D scene.

### SDL2 Event Integration

ImGui's SDL2 backend needs to see input events. The event loop in `gfx_sdl2.cpp`
(`gfx_sdl_handle_events`) will be extended to call `ImGui_ImplSDL2_ProcessEvent(&event)`
for each event before PD's own handling. When ImGui wants keyboard/mouse capture
(`io.WantCaptureKeyboard`, `io.WantCaptureMouse`), PD's input is suppressed.

### New Files

| File | Purpose |
|------|---------|
| `port/src/pdgui.c` | ImGui integration layer: init, newframe, render, shutdown |
| `port/include/pdgui.h` | Public API for the GUI system |
| `port/src/dynmenu.c` | Fluent menu builder API |
| `port/include/dynmenu.h` | Builder API declarations |
| `port/src/modmenu.c` | Mods screen (first dynmenu screen) |
| `port/src/pdgui_style.c` | PD-themed ImGui style |
| `port/fast3d/imgui/` | Dear ImGui source (vendored: imgui*.cpp/h + backends) |

### pdgui Integration Layer (`pdgui.c`)

```c
// Initialize ImGui with PD's SDL2 window and OpenGL context
void pdguiInit(void *sdlWindow);

// Call once per frame after GBI rendering, before buffer swap
void pdguiNewFrame(void);

// Render all active menus (calls dynmenu system, then ImGui::Render)
void pdguiRender(void);

// Cleanup
void pdguiShutdown(void);

// Pass SDL events to ImGui (called from gfx_sdl_handle_events)
// Returns true if ImGui consumed the event
bool pdguiProcessEvent(void *sdlEvent);

// Query whether ImGui wants input focus (suppress PD input when true)
bool pdguiWantsInput(void);
```

### Fluent Menu Builder API (`dynmenu.h`)

The builder creates ImGui menus procedurally. Each "screen" is a C function that uses
the builder to declare its contents every frame (immediate mode).

```c
// Screen registration — each screen is a function called every frame when active
typedef void (*pdgui_screen_fn)(void);

void pdguiPushScreen(pdgui_screen_fn fn);   // push screen onto stack
void pdguiPopScreen(void);                  // pop current screen
void pdguiSetScreen(pdgui_screen_fn fn);    // replace stack with single screen

// Builder helpers (thin wrappers around ImGui with PD styling)
bool pdguiButton(const char *label);
bool pdguiCheckbox(const char *label, s32 *value);
void pdguiSliderInt(const char *label, s32 *value, s32 min, s32 max);
void pdguiSliderFloat(const char *label, f32 *value, f32 min, f32 max);
bool pdguiInputText(const char *label, char *buf, s32 bufsize);
void pdguiLabel(const char *text);
void pdguiSeparator(void);
bool pdguiBeginTabBar(const char *id);
bool pdguiBeginTabItem(const char *label);
void pdguiEndTabItem(void);
void pdguiEndTabBar(void);
bool pdguiBeginCollapsible(const char *label);
void pdguiEndCollapsible(void);
void pdguiBeginColumns(s32 count);
void pdguiNextColumn(void);
void pdguiEndColumns(void);

// 3D model viewport (renders PD model to FBO, displays as ImGui image)
void pdguiModelPreview(s32 bodynum, s32 headnum, f32 width, f32 height);

// Table helpers (for scorecards, stats, etc.)
bool pdguiBeginTable(const char *id, s32 columns);
void pdguiTableHeader(const char *label);
void pdguiTableCell(const char *text);
void pdguiEndTable(void);
```

### Mod Manager Screen (`modmenu.c`)

The first screen built with the new system. Accessible from Options.

```c
void modMenuScreen(void)
{
    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mod Manager", NULL, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Mod list with checkboxes
    for (s32 i = 0; i < modmgrGetCount(); i++) {
        modinfo_t *mod = modmgrGetMod(i);
        s32 enabled = mod->enabled;

        if (pdguiCheckbox(mod->name, &enabled)) {
            modmgrSetEnabled(i, enabled);
        }

        // Show details when hovered
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            pdguiLabel(mod->description);
            ImGui::Text("v%s by %s", mod->version, mod->author);
            if (mod->bundled) ImGui::TextColored(ImVec4(0.5,0.8,1,1), "[BUNDLED]");
            ImGui::EndTooltip();
        }
    }

    ImGui::Separator();

    // Apply button (disabled if no changes)
    if (!modmgrIsDirty()) ImGui::BeginDisabled();
    if (pdguiButton("Apply Changes")) {
        modmgrSaveConfig();
        modmgrApplyChanges();
    }
    if (!modmgrIsDirty()) ImGui::EndDisabled();

    ImGui::SameLine();
    if (pdguiButton("Back")) {
        pdguiPopScreen();
    }

    ImGui::End();
}
```

### PD-Themed ImGui Style (`pdgui_style.c`)

Custom ImGui colors and style to evoke PD's aesthetic:
- Dark background with blue/cyan accent (PD's signature palette)
- Semi-transparent window backgrounds (like PD's menu panels)
- Custom font: a clean sans-serif TTF at appropriate size
- Rounded corners, subtle borders, glow on focused items
- Color scheme constants that can be adjusted globally

### Future Screens (planned, not in D3d scope)

These screens will use the same dynmenu/pdgui system:
- **Post-game scorecard**: Rich table with player stats, K/D, accuracy, awards
- **Character browser**: Carousel with 3D model preview, name, unlock status
- **Achievement/unlock gallery**: Grid or list with progress indicators
- **Custom simulant builder**: Sliders for aggression/accuracy/etc., type selection
- **Unified multiplayer lobby**: Party leader controls, player list, mod sync status
- **Settings**: Tabbed interface (Video, Audio, Controls, Game) with all options

### Migration Strategy

All existing PD menus will be converted to the new ImGui system. Migration order:
1. **Mods screen** (new — D3d, first proof of concept)
2. **Options/Settings** (natural fit for tabs + sliders)
3. **Network menus** (host/join/lobby — already use LITERAL_TEXT, easy to port)
4. **Main menu** (simple list of buttons)
5. **Combat Simulator setup** (arena select, weapon select, bot config)
6. **Post-game screen** (new layout with rich scorecard)
7. **In-game pause menu** (simple overlay)
8. **Solo/Co-op mission select** (more complex, has 3D model rendering)

During migration, both systems coexist. The old PD menu system remains functional for
any screen not yet migrated. `pdguiWantsInput()` prevents input conflicts.

### Files Changed
- `port/src/pdgui.c` (NEW) — ImGui integration
- `port/include/pdgui.h` (NEW) — Public GUI API
- `port/src/dynmenu.c` (NEW) — Fluent builder
- `port/include/dynmenu.h` (NEW) — Builder declarations
- `port/src/modmenu.c` (NEW) — Mods screen
- `port/src/pdgui_style.c` (NEW) — PD theme
- `port/fast3d/imgui/` (NEW) — Vendored ImGui source
- `port/fast3d/gfx_sdl2.cpp` (MODIFIED) — Event hook for ImGui
- `port/src/video.c` (MODIFIED) — pdguiNewFrame/Render calls in frame loop
- `port/src/main.c` (MODIFIED) — pdguiInit/Shutdown calls
- `CMakeLists.txt` (MODIFIED) — ImGui source files + include paths

---

## Sub-Phase D3.5: Hot-Toggle (Soft Reload)

### Flow

```
User toggles mods in menu → "Apply Changes"
  │
  ├─ modmgrSaveConfig()          // persist to config file
  ├─ modmgrReload()              // core rebuild:
  │    ├─ Clear mod shadow arrays (g_ModBodies, g_ModHeads, g_ModArenas)
  │    ├─ Reset mod-modified stage table entries to base defaults
  │    ├─ For each enabled mod:
  │    │    ├─ Set filesystem context (mod directory)
  │    │    ├─ Parse modconfig.txt (stage overrides)
  │    │    └─ Parse mod.json content (bodies, heads, arenas)
  │    └─ Rebuild internal caches
  │
  ├─ videoResetTextureCache()     // flush all textures (they may reference mod files)
  ├─ titleSetNextStage(STAGE_TITLE) // return to title screen
  └─ mainChangeToStage(STAGE_TITLE)
```

### Texture Cache Invalidation

Critical: mod-provided textures are cached by file ID. When mods change, the texture cache must be fully flushed. `videoResetTextureCache()` already exists (used in menu transitions). Call it during reload.

### Stage Table Reset

The base `g_Stages[]` entries are modified in-place by `modConfigLoad()`. To support hot-toggle, we need to save the original base stage table at startup:

```c
// In modmgr.c
static struct stagetableentry g_BaseStages[ARRAYCOUNT(g_Stages)];

void modmgrInit(void) {
    // Save pristine stage table before any mods touch it
    memcpy(g_BaseStages, g_Stages, sizeof(g_Stages));
    // ... scan and load mods ...
}

void modmgrReload(void) {
    // Restore pristine stage table
    memcpy(g_Stages, g_BaseStages, sizeof(g_Stages));
    // ... re-apply enabled mods ...
}
```

---

## Sub-Phase D3.6: Network Mod Sync

### Protocol Extension

**New message types:**

| ID | Name | Direction | Channel | Purpose |
|----|------|-----------|---------|---------|
| 0x60 | SVC_MOD_MANIFEST | S→C | reliable | Server's enabled mod list |
| 0x61 | SVC_MOD_CHUNK | S→C | reliable | File transfer chunk |
| 0x62 | CLC_MOD_REQUEST | C→S | reliable | Client requests a mod |
| 0x63 | CLC_MOD_ACK | C→S | reliable | Client confirms mod received |

Protocol version bump to 19.

### Connection Flow

```
Client connects → CLC_AUTH (protocol 19, mod manifest hash)
  │
  Server compares manifest hash:
  ├─ Match → proceed normally (SVC_STAGE_START)
  └─ Mismatch → SVC_MOD_MANIFEST (full list of enabled mods with IDs + versions + hashes)
       │
       Client compares:
       ├─ All present + matching → send CLC_MOD_ACK(0) (all good, hash was stale)
       └─ Missing mods → for each missing:
            ├─ CLC_MOD_REQUEST(mod_id)
            │    │
            │    Server → streams SVC_MOD_CHUNK (LZ4-compressed, 16KB per chunk)
            │    Client → reassembles, verifies hash, writes to mods/
            │    Client → CLC_MOD_ACK(mod_id)
            │
            └─ After all mods received:
                 Client → modmgrReload() (enable received mods)
                 Client → reconnect to server
```

### SVC_MOD_MANIFEST Format

```
u16  mod_count
For each mod:
    u8   id_len
    char id[id_len]          // "goldfinger64"
    u8   version_len
    char version[version_len] // "1.0.0"
    u32  content_hash         // hash of mod directory contents
    u32  size_bytes           // total mod size (for progress bar)
```

### SVC_MOD_CHUNK Format

```
u8   mod_id_len
char mod_id[mod_id_len]
u32  chunk_index
u32  total_chunks
u32  chunk_size
u8   data[chunk_size]         // LZ4-compressed chunk
u32  chunk_crc32              // integrity check
```

### Transfer Limits

- Max mod size: 50 MB (configurable via `net_mod_maxsize`)
- Max total transfer per session: 200 MB
- Transfer channel: ENet channel 3 (dedicated, separate from gameplay)
- Client UI: Progress bar in joining dialog ("Downloading Goldfinger 64... 45%")

### Security

- Server computes SHA-256 of mod directory before transfer
- Client verifies after reassembly
- Mods are sandboxed: only asset files (textures, models, configs), no executable code
- Transfer opt-in: client can refuse downloads (config toggle `net_mod_autodownload`)

---

## Sub-Phase D3.7: Legacy Mod Migration

### Conversion Table

| Current | New ID | New Directory |
|---------|--------|---------------|
| `build/mods/mod_allinone/` | `allinone` | `mods/allinone/` |
| `build/mods/mod_gex/` | `gex` | `mods/gex/` |
| `build/mods/mod_kakariko/` | `kakariko` | `mods/kakariko/` |
| `build/mods/mod_dark_noon/` | `darknoon` | `mods/darknoon/` |
| `build/mods/mod_goldfinger_64/` | `goldfinger64` | `mods/goldfinger64/` |

### Changes Required

1. **Create `mod.json`** for each of the 5 mods (manually — these are known content)
2. **Move directories** from `build/mods/mod_*` to `mods/*` (cleaner naming)
3. **Remove from `constants.h`**: `MOD_NORMAL`, `MOD_GEX`, `MOD_KAKARIKO`, `MOD_DARKNOON`, `MOD_GOLDFINGER_64`
4. **Remove from `fs.c`**: `g_ModNum` global, per-mod directory variables, all `--*moddir` CLI args
5. **Replace `fsFullPath` mod resolution**: Instead of checking `g_ModNum`, iterate enabled mods in priority order
6. **Replace `modConfigLoad` calls**: Called by `modmgrLoadMod()` for each enabled mod's `modconfig.txt`
7. **Mark as bundled**: `bundled = true` in their `modinfo_t` entries

### fs.c Refactor

The current `fsFullPath()` has a hardcoded if/else chain for each mod. Replace with:

```c
// In fsFullPath() — new mod resolution
for (s32 i = 0; i < g_ModRegistryCount; i++) {
    if (g_ModRegistry[i].enabled && g_ModRegistry[i].loaded) {
        snprintf(pathBuf, FS_MAXPATH, "%s/%s", g_ModRegistry[i].dirpath, relPath);
        if (fsFileSize(pathBuf) >= 0) {
            return pathBuf;
        }
    }
}
// Fall back to base dir
```

**Critical consideration**: The current system uses `g_ModNum` to select ONE active mod at a time. The new system allows MULTIPLE mods to be active simultaneously. File resolution priority = order in registry (bundled first, then alphabetical, or user-configurable load order).

### Load Order Conflict Resolution

When multiple mods provide the same file (e.g., two mods both have `textures/0x1234`):
- First mod in load order wins
- Later mods' files are shadowed
- Log a warning: `"modmgr: file conflict: textures/0x1234 provided by both 'gex' and 'goldfinger64', using 'gex'"`

---

## Sub-Phase D3.8: Config Persistence

### Config Keys

```ini
[Mods]
; Enabled mod IDs (comma-separated)
EnabledMods=allinone,gex,kakariko,darknoon,goldfinger64

; Load order (comma-separated, first = highest priority)
LoadOrder=allinone,gex,kakariko,darknoon,goldfinger64

; Network settings
ModAutoDownload=1
ModMaxDownloadSize=52428800
```

### Integration

Use the existing config system (`configRegister*` in `main.c`):

```c
configRegisterString("Mods.EnabledMods", g_ModEnabledList, sizeof(g_ModEnabledList), "allinone,gex,kakariko,darknoon,goldfinger64");
configRegisterString("Mods.LoadOrder", g_ModLoadOrder, sizeof(g_ModLoadOrder), "");
configRegisterInt("Mods.AutoDownload", &g_ModAutoDownload, 1);
```

---

## Implementation Order

### Phase D3a: Foundation (modmgr core + scanning) — ~400 LOC
1. Create `modmgr.h` / `modmgr.c`
2. Bundle cJSON (`port/src/cjson.c`, `port/include/cjson.h`)
3. Implement `modmgrInit()`: directory scan, mod.json parsing, legacy fallback
4. Implement `modmgrSaveConfig()` / `modmgrLoadConfig()`
5. Hook into `main.c` startup (call `modmgrInit()` before game init)
6. Write `mod.json` files for all 5 existing mods
7. **Test**: Verify mods load correctly through new scanner (same behavior as before)

### Phase D3b: Dynamic asset tables — ~300 LOC
1. Add shadow arrays to `modmgr.c`
2. Implement `modmgrRegisterBody/Head/Arena()` functions
3. Patch `mpGetNumBodies()`, `mpGetNumHeads()`, `mpGetNumArenas()` to include mod counts
4. Add `mpGetBody()`, `mpGetHead()`, `mpGetArena()` accessor functions
5. Audit all direct array access and convert to accessors where needed
6. **Test**: Verify character select, arena select still work with base + mod content

### Phase D3c: fs.c refactor — ~200 LOC
1. Replace `g_ModNum` / per-mod directory globals with registry iteration
2. Update `fsFullPath()` to use `g_ModRegistry` for file resolution
3. Remove `--gexmoddir`, `--kakarikomoddir`, etc. CLI args
4. Keep `--moddir` as override for base mod directory path
5. Move mod directories from `build/mods/mod_*` to `mods/`
6. Remove `MOD_GEX`, `MOD_KAKARIKO`, etc. from `constants.h`
7. **Test**: Full regression test — all mods load, all stages playable

### Phase D3d: Mod Manager menu — ~250 LOC
1. Create mod manager dialog (`g_ModManagerMenuDialog`)
2. Implement list handler, description panel, apply button
3. Add "Mods" button to main menu
4. **Test**: Navigate menu, toggle mods, verify UI

### Phase D3e: Hot-toggle — ~150 LOC
1. Save pristine base stage table on startup
2. Implement `modmgrReload()`: clear mod arrays, restore base stages, re-register enabled mods
3. Wire "Apply Changes" to reload + return to title
4. **Test**: Enable/disable mods, verify content changes without restart

### Phase D3f: Network mod sync — ~500 LOC
1. Add `SVC_MOD_MANIFEST`, `CLC_MOD_REQUEST`, `SVC_MOD_CHUNK`, `CLC_MOD_ACK` messages
2. Extend `CLC_AUTH` with mod manifest hash
3. Implement server-side manifest broadcast and chunk streaming
4. Implement client-side manifest comparison and download flow
5. Add LZ4 compression (already available? check dependencies, else bundle minilz4)
6. Add progress bar to joining UI
7. Protocol version bump to 19
8. **Test**: Client with missing mod connects, downloads, reconnects successfully

### Phase D3g: Cleanup and polish — ~100 LOC
1. Remove dead code (old mod system remnants)
2. Add load order UI to mod manager (drag or up/down buttons)
3. Add mod conflict warnings to log
4. Update `context.md` with completed work

---

## File Change Summary

### New Files
| File | Purpose | LOC (est) |
|------|---------|-----------|
| `port/include/modmgr.h` | Mod manager API + structs | 120 |
| `port/src/modmgr.c` | Mod manager implementation | 800 |
| `port/src/modmenu.c` | Mod Manager menu UI | 250 |
| `port/src/cjson.c` | JSON parser (third-party) | 1700 |
| `port/include/cjson.h` | JSON parser header | 300 |
| `mods/allinone/mod.json` | AllInOne manifest | 30 |
| `mods/gex/mod.json` | GEX manifest | 80 |
| `mods/kakariko/mod.json` | Kakariko manifest | 20 |
| `mods/darknoon/mod.json` | Dark Noon manifest | 20 |
| `mods/goldfinger64/mod.json` | Goldfinger 64 manifest | 40 |

### Modified Files
| File | Changes |
|------|---------|
| `port/src/main.c` | Add `modmgrInit()` call, config registration |
| `port/src/fs.c` | Replace hardcoded mod resolution with registry iteration |
| `port/include/mod.h` | Minor: remove `g_ModNum` extern if moved |
| `port/src/mod.c` | Refactor: `modConfigLoad` now called by modmgr |
| `src/include/constants.h` | Remove `MOD_GEX`, `MOD_KAKARIKO`, etc. |
| `src/game/mplayer/mplayer.c` | Patch `mpGetNumBodies/Heads`, add accessors |
| `src/game/mplayer/setup.c` | Patch `mpGetNumArenas`, use accessors |
| `src/game/mainmenu.c` | Add "Mods" button |
| `port/include/net/netmsg.h` | New SVC_MOD_* / CLC_MOD_* message IDs |
| `port/src/net/netmsg.c` | Mod manifest / chunk / request handlers |
| `port/src/net/net.c` | Mod sync in connection flow, chunk streaming |
| `port/src/net/netmenu.c` | Download progress bar in join dialog |
| `CMakeLists.txt` | Add new source files |

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Multi-mod file conflicts | Wrong textures/models loaded | Load order system + conflict logging |
| Stage table corruption on hot-toggle | Crash/broken stages | Pristine backup + full restore before reload |
| Network mod transfer stalls | Client stuck in download | Timeout + retry + cancel button |
| cJSON adds binary size | ~30KB increase | Acceptable for the functionality gained |
| Accessor refactor misses direct array access | Mod content invisible | Grep audit of all `g_MpBodies[`, `g_MpHeads[`, `g_MpArenas[` |
| Existing mods break during migration | Regression | Parallel testing: old system vs new system side-by-side |

---

## Testing Checklist

### D3a (Foundation)
- [ ] Game starts, scans mods/ directory, logs discovered mods
- [ ] All 5 bundled mods found and loaded
- [ ] New community mod with mod.json discovered and loaded
- [ ] Legacy mod (modconfig.txt only) discovered with synthetic manifest
- [ ] Enable/disable state persists across game restarts

### D3b (Dynamic Tables)
- [ ] Character select shows base + mod characters
- [ ] Arena select shows base + mod arenas
- [ ] Mod character models load correctly in-game
- [ ] No crashes when scrolling through extended lists

### D3c (fs.c Refactor)
- [ ] All 5 mods load without CLI args
- [ ] Mod textures and models resolve correctly
- [ ] No file-not-found errors in log
- [ ] Multiple mods active simultaneously

### D3d (Menu)
- [ ] Mod Manager accessible from main menu
- [ ] List shows all discovered mods with enable/disable state
- [ ] Toggle works, "Apply Changes" enabled when dirty
- [ ] Description panel updates on focus change

### D3e (Hot-Toggle)
- [ ] Disable a mod → Apply → return to title → mod content gone
- [ ] Re-enable → Apply → mod content back
- [ ] No texture corruption after toggle
- [ ] Stage table entries correct after toggle

### D3f (Network)
- [ ] Client with matching mods: connects normally
- [ ] Client with missing mod: receives download, installs, reconnects
- [ ] Progress bar shows during download
- [ ] Client can cancel download
- [ ] Large mod (>1MB) transfers correctly
- [ ] Corrupted chunk detected and re-requested
