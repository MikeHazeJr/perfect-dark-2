# MATCHSETUP Config Struct — State Audit (2026-04-23)

## Mike's question: are mpbody/mphead stored as state or computed at emit time?

**Short answer: the MATCHSETUP log values are computed at emit time (inside `matchStart`), not persistent state.**

---

## Struct layers

The match-start pipeline touches two separate structs:

### 1. `struct matchslot` (`port/include/net/matchsetup.h`)
This is the *pre-match* config, stored in `g_MatchConfig.slots[]`.

| Field | Role |
|-------|------|
| `body_id[64]` | PRIMARY — catalog ID string (e.g. `"base:elvis1"`) |
| `head_id[64]` | PRIMARY — catalog ID string (e.g. `"base:head_elvis"`) |
| `bodynum` | DEPRECATED — cached `mp_index` integer, NOT always in sync |
| `headnum` | DEPRECATED — cached `mp_index` integer, NOT always in sync |

### 2. `struct mpbotconfig.base` (`src/game/mplayer/mplayer.h`)
Populated inside `matchStart()` from `g_MatchConfig.slots[]`.

| Field | Role |
|-------|------|
| `mpbodynum` | DERIVED — resolved from `body_id` via `assetCatalogResolve` at matchStart |
| `mpheadnum` | DERIVED — resolved from `head_id` via `assetCatalogResolve` at matchStart |

The MATCHSETUP log line (matchsetup.c:779) emits `bot->base.mpbodynum` / `bot->base.mpheadnum`, which are freshly computed from the catalog IDs moments before the log. They are NOT persisted state on the matchslot.

---

## State duplication risk in `g_MatchConfig.slots[]`

`matchslot.bodynum` / `headnum` (the deprecated integers) ARE state fields. They are updated when `matchConfigAddBot` is called (lines 497-512 in matchsetup.c), but NOT when the room UI writes `body_id` / `head_id` directly to a slot (room.cpp:2247-2253, 3382-3389). This is a real inconsistency: the direct-write UI paths update the PRIMARY strings but leave the cached integers stale.

However, this inconsistency is benign for the current code path because `matchStart()` re-derives everything from the PRIMARY strings at match time. The deprecated integers are only read by legacy code that hasn't been migrated to catalog IDs.

**This is a known M0.1 carry-over, not a new defect.**

---

## The actual head bug (B-235)

The wrong head (President) for Maian (elvis1) was NOT caused by state duplication. It was caused by a domain-confusion bug in the room UI (introduced in commit `3a055323`):

```cpp
// WRONG (both pdgui_menu_room.cpp:2243 and :3385)
const char *hid = catalogMpHeadId(b);  // b is a BODY mp_index, not a HEAD mp_index!
```

`MPBODY_ELVIS1 = 12` and `MPHEAD_PRESIDENT = 12` happen to share the same integer. Passing the body's mp_index (12) to `catalogMpHeadId` returns the head at mp_index 12 = President.

### Fix applied (2026-04-23)

**pdgui_menu_room.cpp:2242-2243** (Set Character context menu — multi-select):
```cpp
// Before: const char *hid = catalogMpHeadId(b);
s32 defHead = catalogGetBodyDefaultMpHeadIdx((s32)b);
const char *hid = (defHead >= 0) ? catalogMpHeadId(defHead) : NULL;
```

**pdgui_menu_room.cpp:3385** (individual bot edit modal):
```cpp
// Before: const char *hid = catalogMpHeadId(b);
s32 defHead2 = catalogGetBodyDefaultMpHeadIdx((s32)b);
const char *hid = (defHead2 >= 0) ? catalogMpHeadId(defHead2) : NULL;
```

**matchsetup.c:488-491** (`matchConfigAddBot` fallback head when body given but head NULL):
```c
// Before: hardcoded "base:head_dark_combat"
const char *h = (head_id && head_id[0]) ? head_id : catalogGetBodyDefaultHead(body_id);
if (!h || !h[0]) h = "base:head_dark_combat";
```

---

## Writer path audit — who sets head_id on bot slots?

All paths that write `g_MatchConfig.slots[i].head_id`:

| Path | Location | Safe? |
|------|----------|-------|
| `matchConfigAddBot(body_id, head_id, ...)` | matchsetup.c:488 | Fixed — now uses `catalogGetBodyDefaultHead(body_id)` as fallback |
| Set Character context menu (multi-select) | room.cpp:2251 | Fixed — now uses `catalogGetBodyDefaultMpHeadIdx(b)` |
| Bot edit modal character picker | room.cpp:3387 | Fixed — same fix |
| Network receive CLC_LOBBY_START | netmsg.c:5079 | Reads string from wire — safe (server-authoritative) |
| `matchConfigRerollBot` | matchsetup.c:548-549 | Calls `pickRandomBodyHead` — correct, uses `catalogGetBodyDefaultHead` |

---

## Propagation check — other `catalogMpHeadId(b)` with body b patterns?

Searched for `catalogMpHeadId` usages across the codebase. No other sites pass a body mp_index where a head mp_index is expected. The bridge path (`pdgui_bridge.c:1379`) correctly calls `catalogGetBodyDefaultMpHeadIdx(mpbodynum)` before calling `catalogMpHeadId`.

---

## Playtest verification targets

1. Add a Maian bot via Room screen → character picker → verify head shows Maian (Elvis), not President.
2. Add a Maian bot via "Set Character" context menu (multi-select) → same check.
3. Verify President body (`base:president`) still gets President head correctly.
4. Verify Joanna (body 0, head 0) is unaffected.
