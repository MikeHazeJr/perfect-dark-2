# Logging System Upgrade — Design Document

**Date**: 2026-04-02 (S130)
**Status**: APPROVED — ready for implementation
**Scope**: Enhance existing logging infrastructure; add structured ring buffer, Dev Window log viewer, headless CLI tool, export

---

## Current State (What We Already Have)

The existing system in `port/src/system.c` + `port/include/system.h` is solid:

- **9 log channels**: NETWORK, GAME, COMBAT, AUDIO, MENU, SAVE, MOD, SYSTEM, MATCH
- **5 severity levels**: VERBOSE, NOTE, WARNING, ERROR, CHAT
- **Auto-classification**: `sysLogClassifyMessage()` maps prefix strings ("NET:", "CATALOG:", etc.) to channel bitmasks
- **Channel filtering**: Bitmask-based, persisted in pd.ini. Warnings/Errors always pass filters.
- **File output**: `pd-client.log` / `pd-server.log` with `[MM:SS.ms]` timestamps
- **Ring buffer**: 256 lines × 256 chars, flat text, used by dedicated server overlay
- **Debug menu**: Channel checkboxes, All/None presets, Verbose toggle, mask display
- **~900 callsites** across 80 files already follow prefix convention

### What's Missing

1. **Structured ring buffer** — current ring stores flat text; viewer must re-parse prefixes to filter
2. **Dev Window log viewer tab** — current filters are in debug menu, no scrollable log view with search
3. **Export to file** — no way to save filtered log to disk
4. **Headless CLI tool** — no way for AI to filter logs without running the game
5. **Some channels overloaded** — GAME carries CATALOG, STAGE, PLAYER, etc. (too broad)
6. **No DISTRIB/MANIFEST channel** — mod distribution and manifest logs have no dedicated channel

---

## Design

### 1. Structured Ring Buffer

Replace the flat `char s_LogRing[256][256]` with a structured entry:

```c
typedef struct {
    u32  channel;        /* LOG_CH_* bitmask */
    s32  level;          /* LOG_VERBOSE..LOG_CHAT */
    f32  timestamp;      /* sysGetSeconds() at log time */
    u32  sequence;       /* monotonic counter for ordering */
    char text[256];      /* formatted message (without level prefix) */
} LogEntry;

#define SYSLOG_RING_ENTRIES 2048  /* 8x current; ~500KB */
static LogEntry s_LogEntries[SYSLOG_RING_ENTRIES];
static u32 s_LogEntryHead = 0;
static u32 s_LogEntryCount = 0;
```

**Backward compat**: Keep `sysLogRingGetLine()` working for the server overlay by reading `s_LogEntries[idx].text`.

**New API**:
```c
s32            sysLogEntryGetCount(void);
const LogEntry *sysLogEntryGet(s32 idx);  /* 0 = oldest visible */
u32            sysLogEntryGetSequence(void);  /* for change detection */
```

### 2. Channel Additions

Split the overloaded GAME channel and add missing coverage:

```c
#define LOG_CH_NETWORK   0x0001  /* NET, UPNP, SERVER, LOBBY           */
#define LOG_CH_GAME      0x0002  /* STAGE, INTRO, PLAYER, SIMULANT, SETUP */
#define LOG_CH_COMBAT    0x0004  /* DAMAGE, WEAPON, AMMO, HEALTH, PICKUP */
#define LOG_CH_AUDIO     0x0008  /* SND, AUDIO, MUSIC, SFX             */
#define LOG_CH_MENU      0x0010  /* MENU, HOTSWAP, DIALOG, FONT        */
#define LOG_CH_SAVE      0x0020  /* SAVE, SAVEMIGRATE, CONFIG           */
#define LOG_CH_MOD       0x0040  /* MOD, MODMGR, MODLOAD               */
#define LOG_CH_SYSTEM    0x0080  /* SYS, MEM, MEMPC, CRASH, FS, UPDATER */
#define LOG_CH_MATCH     0x0100  /* MATCH, CHRSLOTS, BOT_ALLOC, MATCHSETUP */
#define LOG_CH_CATALOG   0x0200  /* CATALOG, MANIFEST, ASSET            */
#define LOG_CH_DISTRIB   0x0400  /* DISTRIB (mod distribution pipeline) */
#define LOG_CH_RENDER    0x0800  /* RENDER, GFX, TEXTURE, FAST3D       */
#define LOG_CH_ALL       0xFFFF
#define LOG_CH_NONE      0x0000
#define LOG_CH_COUNT     12
```

Migration: Move "CATALOG:" and "MANIFEST:" prefixes from LOG_CH_GAME to LOG_CH_CATALOG. Move "DISTRIB:" to LOG_CH_DISTRIB. Add "RENDER:" / "GFX:" to LOG_CH_RENDER. LOAD stays in GAME (it's stage loading). Backward compatible — old mask values still work for the first 9 channels.

### 3. Dev Window Log Viewer Tab

New file: `port/fast3d/pdgui_menu_logviewer.cpp`

**Layout**:
```
┌─ Log Viewer ─────────────────────────────────────────────┐
│ [Channel Filters]  □NET □GAME □COMBAT □AUDIO □MENU ...   │
│ [Severity]  □VERBOSE □NOTE □WARNING □ERROR □CHAT          │
│ [Search: _______________] [Clear] [Export Filtered]       │
│ ─────────────────────────────────────────────────────────│
│ [00:12.34] NET    NOTE  Client 3 connected               │
│ [00:12.35] CATALOG NOTE  Resolving base:dark_combat       │
│ [00:12.40] NET    WARN  Packet loss 5% on client 3       │
│ [00:15.01] GAME   ERROR ChrResync: NULL prop for bot 4   │
│ ...                                                       │
│ ─────────────────────────────────────────────────────────│
│ Showing 847 of 1204 entries │ Auto-scroll: ☑             │
└──────────────────────────────────────────────────────────┘
```

**Features**:
- Channel checkboxes (per channel, with All/None presets)
- Severity checkboxes (default: NOTE+WARN+ERROR; VERBOSE off)
- Text search (substring match, case-insensitive)
- Color-coded severity (gray=VERBOSE, white=NOTE, yellow=WARN, red=ERROR, cyan=CHAT)
- Auto-scroll toggle (default on; pauses when user scrolls up)
- "Export Filtered" button — writes currently filtered entries to `pd-log-export-YYYYMMDD-HHMMSS.txt`
- Entry count display (filtered / total)
- Reads from `s_LogEntries[]` ring buffer — zero-copy, no impact on game performance

**Registration**: Added as a tab in the Dev Window alongside existing Debug, Catalog, etc.

### 4. Headless CLI Log Filter Tool

New file: `tools/logview.sh` (shell wrapper) + `tools/logview.c` (compiled tool)

**Usage**:
```bash
# Filter by channel
tools/logview --channel NET,CATALOG pd-client.log

# Filter by severity
tools/logview --level WARN,ERROR pd-server.log

# Text search
tools/logview --search "desync" pd-client.log

# Combined
tools/logview --channel NET --level ERROR --search "bounds" pd-client.log

# Tail mode (follow)
tools/logview --follow --channel NET pd-client.log
```

**File format**: The existing `[MM:SS.ms] PREFIX: message` format is sufficient. The tool parses the prefix to determine channel, and the level prefix (WARNING:/ERROR:) to determine severity. No file format change needed.

**Output**: Clean, one-line-per-entry, suitable for AI consumption. Optional `--json` flag for structured output.

### 5. Export Format

The export writes a filtered view as plain text matching the existing log format:

```
# PD2 Log Export — 2026-04-02 15:30:45
# Filters: channels=NET,CATALOG severity=WARN,ERROR search="desync"
# Showing 23 of 1847 entries
[00:12.40] WARNING: NET: Packet loss 5% on client 3
[00:15.01] ERROR: CATALOG: ChrResync: NULL prop for bot 4
...
```

The header comment block tells the AI (or human) exactly what was filtered.

### 6. Mod Distribution Trust Threshold

Update `netdistrib.c`:

```c
#define DISTRIB_TRUST_THRESHOLD_DEFAULT_MB  256  /* Prompt above this; no hard ceiling */
```

- **No hard ceiling** — any size mod can be distributed. Total conversions, HD texture packs, and custom campaigns can easily exceed 512MB.
- **Trust threshold (default 256MB, configurable via pd.ini `Net.DistribTrustThresholdMB`, range 16–4096)**:
  - Below threshold: transfer silently (existing behavior)
  - Above threshold: queue the transfer, send a UI prompt to the client ("Server wants to send mod 'X' (247 MB). Accept?")
  - Client can accept (proceed) or decline (disconnect from transfer, stay in lobby)
- The only real protection is the user's approval + available disk space

---

## Implementation Order

1. **Structured ring buffer + new channels** (system.c, system.h) — foundation
2. **Dev Window log viewer tab** (pdgui_menu_logviewer.cpp) — immediate value
3. **Export function** — part of viewer
4. **Headless CLI tool** (tools/logview.c) — AI tooling
5. **Mod distribution trust threshold** (netdistrib.c) — independent

---

## Files Changed

| File | Change |
|------|--------|
| `port/include/system.h` | LogEntry struct, new channels, new API |
| `port/src/system.c` | Structured ring buffer, updated classification |
| `port/fast3d/pdgui_menu_logviewer.cpp` | NEW — Dev Window log viewer tab |
| `port/fast3d/pdgui_backend.cpp` | Register log viewer tab |
| `port/fast3d/pdgui_debugmenu.cpp` | Remove log filter section (moved to viewer) |
| `port/src/net/netdistrib.c` | Trust threshold, 512MB hard ceiling |
| `port/src/net/netdistrib.h` | Trust threshold config API |
| `tools/logview.c` | NEW — headless log filter tool |
| `CMakeLists.txt` | Add logview build target |

---

*End of design. Ready for implementation.*
