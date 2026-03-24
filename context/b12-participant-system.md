# B-12: Dynamic Participant System — Architecture Design

> Replaces: fixed u32 chrslots bitmask + MAX_PLAYERS/MAX_BOTS split
> Status: DESIGN (not yet implemented)
> Session: S26

---

## Director's Intent

All match participants (human players and bots) share a single, dynamic slot pool.
Default capacity: 32. Cheat-expandable to arbitrary count. No structural distinction
between a human and a bot at the slot level — they're all participants.

The system supports up to 32+ unique human players on independent machines, plus any
combination of splitscreen on local machines (each contributing 1–4 players). A match
of 8 machines × 4 splitscreen each = 32 human players, or 32 solo machines = 32 players,
or any mix — all with optional bots filling remaining slots.

---

## Core Concepts

### Three Separate Concepts (Do Not Confuse)

| Concept | Purpose | Size | Changes? |
|---------|---------|------|----------|
| **Local player slots** | Splitscreen viewports, input, HUD, PFS on THIS machine | Fixed: MAX_LOCAL_PLAYERS = 4 | NO — hardware-bound |
| **Network clients** | Connected machines (each may have 1–4 local players) | Dynamic | YES |
| **Participant slots** | Everyone in the match: all humans (local + remote) + bots | Dynamic: default 32, expandable | YES — this redesign |

**Key insight:** A single network client can contribute 1–4 local players to the match.
Machine A (2 splitscreen players) + Machine B (3 splitscreen players) + Machine C (solo)
= 6 human participants across 3 network clients, each with their own splitscreen setup.

Local player slot 0–3 on a remote machine maps to participant slots in the server's pool.
The server must track which participants belong to which client for input routing and
disconnect cleanup.

### Multi-Player-Per-Client Networking

Each network client advertises its local player count during handshake:

```c
// Client → Server during lobby join
typedef struct {
    u8 local_player_count;  // 1-4
    struct mpchrconfig configs[4]; // One per local player
} ClientJoinMsg;
```

The server allocates N participant slots for that client and tracks the mapping:

```c
typedef struct mpparticipant {
    ParticipantType type;
    u8 team;
    u8 localslot;           // 0-3 relative to THEIR machine (LOCAL or REMOTE)
    u8 _pad;
    s32 client_id;          // Which network client owns this participant (-1 for bots, 0 for server)
    struct mpchrconfig *config;
    struct chrdata *chr;
} MpParticipant;
```

On disconnect, server removes ALL participants belonging to that client.
Input messages are tagged with (client_id, local_slot) to route to the correct participant.

### Participant Descriptor

```c
// New type — replaces the chrslots bitmask concept
typedef enum {
    PARTICIPANT_NONE = 0,   // Empty slot
    PARTICIPANT_LOCAL,      // Local human (has a local player slot 0-3)
    PARTICIPANT_REMOTE,     // Remote human (has a netclient)
    PARTICIPANT_BOT,        // AI simulant
} ParticipantType;

typedef struct mpparticipant {
    ParticipantType type;
    u8 team;
    u8 localslot;           // 0-3 if LOCAL, 0xFF otherwise
    u8 _pad;
    struct netclient *client; // Non-NULL if REMOTE
    struct mpchrconfig *config; // Points into g_MpChrConfigs pool
    struct chrdata *chr;    // Runtime chr pointer (NULL until spawned)
} MpParticipant;
```

### Dynamic Pool

```c
// Replaces g_MpSetup.chrslots (u32), MAX_MPCHRS, MAX_BOTS, MAX_PLAYERS
typedef struct mpparticipantpool {
    MpParticipant *slots;   // Heap-allocated array
    s32 count;              // Number of active (non-NONE) participants
    s32 capacity;           // Current array size (default 32, expandable)
} MpParticipantPool;

// Global instance
extern MpParticipantPool g_MpParticipants;
```

---

## Migration Strategy

### Phase 1: Introduce Participant Pool (Parallel)

Add the new system alongside chrslots. Both exist. New code writes to both.
Old code still reads chrslots. This lets us migrate incrementally without
breaking everything at once.

```c
// Compatibility shim — keeps old code working during migration
u32 mpParticipantsToLegacyChrslots(void);  // Generates a u32 from the pool
bool mpIsParticipantActive(s32 index);      // Replaces (chrslots & (1u << i))
s32  mpGetActiveParticipantCount(void);     // Replaces popcount(chrslots)
s32  mpGetActiveBotCount(void);             // Replaces popcount(chrslots & BOT_MASK)
s32  mpGetActivePlayerCount(void);          // Replaces popcount(chrslots & PLAYER_MASK)
```

### Phase 2: Migrate Callsites

Replace all `chrslots & (1u << index)` patterns with `mpIsParticipantActive(index)`.
Replace all MAX_BOTS/MAX_PLAYERS loop bounds with `g_MpParticipants.capacity`.
Replace chrslots set/clear with `mpAddParticipant()` / `mpRemoveParticipant()`.

### Phase 3: Remove chrslots

Once all callsites migrated, delete the u32 chrslots field and legacy shims.

---

## Array Resizing

### Static → Dynamic Conversions

These arrays are currently compile-time fixed. They become heap-allocated:

| Current | Size | New |
|---------|------|-----|
| `g_MpAllChrPtrs[MAX_MPCHRS]` | 32 | `g_MpParticipants.slots[i].chr` (embedded in participant) |
| `g_MpAllChrConfigPtrs[MAX_MPCHRS]` | 32 | `g_MpParticipants.slots[i].config` (embedded in participant) |
| `g_BotConfigsArray[MAX_BOTS]` | 24 | Merged into `g_MpChrConfigs` pool (unified) |
| `g_PlayerConfigsArray[16]` | 16 | Keep for local players (indices 0-3 + co-op). Remote player configs go in unified pool. |
| `g_MpBotChrPtrs[MAX_BOTS]` | 24 | Eliminated — use participant pool |
| `chrnumsbydistanceasc[MAX_MPCHRS]` | 32 | Heap-allocated parallel array, sized to capacity |
| `chrdistances[MAX_MPCHRS]` | 32 | Same |
| `chrsinsight[MAX_MPCHRS]` | 32 | Same |
| `numpoints[MAX_MPCHRS]` | 32 | Same |

### Stays Fixed (Local-Only)

| Array | Size | Why |
|-------|------|-----|
| `g_Menus[MAX_LOCAL_PLAYERS]` | 4 | Splitscreen viewports |
| `g_Pfses[MAX_LOCAL_PLAYERS]` | 4 | Local save data |
| `g_BgunAudioHandles[MAX_LOCAL_PLAYERS]` | 4 | Local audio |
| `g_LaserSights[MAX_LOCAL_PLAYERS]` | 4 | Local rendering |

---

## Network Protocol (v20)

### SVC_STAGE_START Changes

Old (v19):
```
u32 chrslots          // 4 bytes — bitmask
```

New (v20):
```
u16 participant_count // 2 bytes
for each participant:
    u8  type          // PARTICIPANT_LOCAL/REMOTE/BOT
    u8  team
    s8  client_id     // Which network client owns this (-1 = bot, 0 = server host)
    u8  local_slot    // 0-3 relative to owning client
    u8  bot_type      // BOTTYPE_xxx if BOT, 0 otherwise
    u8  bot_diff      // BOTDIFF_xxx if BOT, 0 otherwise
    u16 body          // character body ID
    u16 head          // character head ID
```

This is more bytes but also more information — no need for secondary config sync messages.

### Client Join / Input Routing

```
// CL_JOIN (client → server)
u8  local_player_count   // 1-4 splitscreen players on this machine
for each local player:
    mpchrconfig config   // Character setup

// SVC_INPUT (server → all clients, per-tick)
// Tagged with participant_index so each client applies input
// to the correct local player

// CL_INPUT (client → server)
u8  local_player_count
for each local player:
    u8  local_slot       // 0-3
    input_data payload   // Stick, buttons, etc.
```

On client disconnect, server iterates participants and removes all with matching client_id.

### Protocol Version Bump

```c
#define NET_PROTOCOL_VERSION 20  // Was 19
```

Clean break. Pre-v20 clients rejected at handshake.

---

## Save Format

chrslots is NOT saved to disk (confirmed in research). Bot configs are saved individually
and chrslots is reconstructed at load time. The load path changes from:

```c
// Old: iterate MAX_BOTS, set bits
for (i = 0; i < MAX_BOTS; i++) {
    if (difficulty != BOTDIFF_DISABLED) {
        chrslots |= 1u << (i + BOT_SLOT_OFFSET);
    }
}
```

To:

```c
// New: iterate saved bots, add participants
for (i = 0; i < saved_bot_count; i++) {
    if (difficulty != BOTDIFF_DISABLED) {
        mpAddParticipant(PARTICIPANT_BOT, &config);
    }
}
```

Save format itself doesn't change (SAVE_VERSION stays). Only the load-time
reconstruction logic changes.

---

## Entity Pool Scaling

When participant count rises, gameplay entity pools must scale too.
Session 17 already expanded pools for 32-bot play. The participant system
should validate pool sizes at match start:

```c
// At match start, warn if pools seem undersized
s32 participants = g_MpParticipants.count;
if (participants > 32 && g_NumType1 < participants * 2) {
    sysLogPrintf(LOG_WARNING, "Entity pools may be undersized for %d participants", participants);
}
```

For cheat-mode massive matches, pools should auto-scale or at minimum warn.

---

## Key Functions to Create

```c
// Pool lifecycle
void mpParticipantPoolInit(s32 initial_capacity);  // malloc, zero
void mpParticipantPoolFree(void);                   // free
void mpParticipantPoolResize(s32 new_capacity);     // realloc

// Slot management
s32  mpAddParticipant(ParticipantType type, struct mpchrconfig *config);  // Returns slot index, -1 if full
void mpRemoveParticipant(s32 index);
void mpClearAllParticipants(void);

// Queries
bool mpIsParticipantActive(s32 index);
s32  mpGetActiveParticipantCount(void);
s32  mpGetActiveBotCount(void);
s32  mpGetActivePlayerCount(void);
s32  mpGetFirstEmptySlot(void);
MpParticipant *mpGetParticipant(s32 index);

// Iteration (replaces bitmask loops)
// Usage: for (s32 i = mpParticipantFirst(); i >= 0; i = mpParticipantNext(i))
s32  mpParticipantFirst(void);
s32  mpParticipantNext(s32 current);

// Legacy compatibility (Phase 1 only, removed in Phase 3)
u32  mpParticipantsToLegacyChrslots(void);  // For code not yet migrated
```

---

## Implementation Order

1. Create `participant.h` / `participant.c` with the pool and all functions
2. Add parallel writes (new system + chrslots) in mpStartMatch, mpCreateBot, etc.
3. Add compatibility shims
4. Migrate callsites file-by-file (mplayer.c first, then setup.c, then the rest)
5. Update network serialization (protocol v20)
6. Update save/load reconstruction
7. Remove chrslots and legacy shims
8. Add cheat to expand capacity beyond default

---

## Files Touched

**New files:**
- `src/game/mplayer/participant.h`
- `src/game/mplayer/participant.c`

**Modified (core):**
- `src/include/constants.h` — remove MAX_BOTS, MAX_PLAYERS, MAX_MPCHRS; add DEFAULT_PARTICIPANT_CAPACITY
- `src/include/types.h` — remove chrslots from mpsetup; add participant pool
- `src/include/bss.h` — remove fixed arrays, add dynamic pool extern
- `src/game/mplayer/mplayer.c` — mpStartMatch, bot creation, all chrslots ops
- `src/game/botmgr.c` — botmgrAllocateBot, g_BotCount
- `src/game/setup.c` — chr/model allocation counting

**Modified (secondary):**
- `port/src/net/netmsg.c` — SVC_STAGE_START serialization
- `src/game/mplayer/setup.c` — bot menus, setup UI
- `src/game/menu.c`, `menutick.c` — player selection
- `src/game/challenge.c` — challenge availability
- `src/game/filemgr.c` — save reconstruction

**Untouched:**
- `g_Menus[4]`, `g_Pfses[4]`, viewport code — local-only, stays fixed
