# Lobby Unification — Solo/Online Room Convergence

> **Goal**: Close remaining feature gaps between Solo Combat Simulator and Online Play lobbies, retire legacy matchsetup screen, harden online bot spawning.
> The two lobbies already share `pdgui_menu_room.cpp` via `s_IsSoloMode`. Architecture is ~90% unified.
> Back to [index](README.md) | Parent tracker: [tasks-current.md](tasks-current.md)

---

## Key Files

| File | Lines | Role |
|------|-------|------|
| `port/fast3d/pdgui_menu_room.cpp` | 2,319 | Central room screen — Solo + Online via `s_IsSoloMode` |
| `port/fast3d/pdgui_menu_matchsetup.cpp` | 1,582 | Legacy matchsetup — retirement candidate |
| `port/fast3d/pdgui_menu_mpsettings.cpp` | 278 | MP settings (handicaps live here) |
| `port/fast3d/pdgui_menu_teamsetup.cpp` | 454 | Team setup (5 presets live here) |
| `port/src/scenario_save.c` | 586 | JSON scenario save/load |
| `port/src/net/matchsetup.c` | 726 | Solo launch path — `matchStart()` |
| `port/src/net/netlobby.c` | 207 | Online launch path — `netLobbyRequestStartWithSims()` |
| `port/src/net/netmsg.c` | — | Network message handlers |

---

## Phase 1: Close Feature Gaps in Room Screen

These features exist in Solo (via legacy paths or separate screens) but aren't exposed in the Online room screen.

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-1 | **Custom Weapon Slot Editing** | Solo has 6 individually configurable weapon slots. Online only has the weapon set preset dropdown. Add the 6-slot custom editor to the room screen, synced via network for online mode. | M | DONE (UI done, network sync deferred to U-8) |
| U-2 | **Handicap Sliders** | Per-player damage modifiers exist in `pdgui_menu_mpsettings.cpp` but aren't wired into the room screen. Add handicap controls to the room UI, networked for online. | M | DONE ("Player Handicaps..." button pushes g_MpHandicapsMenuDialog; leader-only in online) |
| U-3 | **Team Auto-Presets** | `pdgui_menu_teamsetup.cpp` has 5 presets (Two/Three/Four Teams, Humans vs Sims, Human-Sim Pairs). Wire these into the room screen's team setup section. | S | DONE ("Team Setup..." button pushes g_MpTeamsMenuDialog; leader-only in online) |
| U-4 | **Save/Load Scenario** | JSON scenario save/load exists (`port/src/scenario_save.c`) but isn't surfaced in the room UI. Add save/load buttons. For online, only the room leader can load a scenario. | M | DONE (Save/Load/Delete buttons in Combat Sim tab; leader-gated; double-click or Load button; syncs arena+weapon UI after load) |
| U-5 | **Slow Motion Toggle** | Available in Solo's options but missing from the Online options list in the room screen. Add it. | XS | DONE |
| U-6 | **SP Characters in MP** | 76 heads and 63 bodies registered in catalog with feature-lock gates. Verify all SP characters are selectable in both Solo and Online character pickers when unlocked. Memory note: "ALL heads registered in catalog, SP-only available via unlock system." | S | DONE (confirmed: no filter; all chars shown in both paths) |

---

## Phase 2: Retire Legacy Code

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-7 | **Audit pdgui_menu_matchsetup.cpp** | Confirm all 1,582 lines of functionality are covered by `pdgui_menu_room.cpp`. If so, remove the file and update all references (CMakeLists, includes, menu manager routing). | M | PARTIAL — see audit below |

### U-7 Audit Results (2026-04-05)

**Verdict: NOT safe to remove yet.** Two feature gaps + one shared function dependency.

**Feature coverage (matchsetup.cpp → room.cpp):**

| Feature | matchsetup.cpp | room.cpp | Status |
|---------|---------------|----------|--------|
| Player/bot list, add/remove | Yes | Yes | Covered |
| Bot edit: name, difficulty, character | Popup | Modal + ctx menu | Covered |
| Bot edit: bot type | Popup combo | Context menu | Covered (different location) |
| Bot edit: team assignment | Popup combo | Not in modal | Covered (context elsewhere) |
| **Advanced bot customizer (trait sliders, presets)** | **Yes (D3R-8)** | **No** | **GAP** |
| **3D character preview in bot edit** | **Yes** | **No** | **GAP** |
| Scenario, arena, weapons, limits, options | Yes | Yes | Covered |
| Multi-select, duplicate, re-roll | Removed | Yes | room.cpp is superset |
| Save/Load scenario, handicaps, team presets | No | Yes | room.cpp is superset |

**Shared function dependency:**
- `arenaGetName()` (+ 45-entry override table) is **defined** in matchsetup.cpp (line 331) and **used externally** by room.cpp (lines 191, 222). Must be relocated before removal.

**Active code paths pushing `g_MatchSetupMenuDialog`:**
- `menutick.c:253` — first player joins MP (initial setup flow)
- `menutick.c:537` — return-from-match `prevmenudialog`
- `mainmenu.c:4845` — old C-side Combat Sim button handler
- `setup.c:5736` — tick handler dialog identity check

These may be dead paths (ImGui overlay intercepts at every entry point via `pdguiSoloRoomOpen()` and endscreen return), but proving it requires careful trace of all edge cases.

**Prerequisites for full removal (U-7b):**
1. Relocate `arenaGetName()` + override table to room.cpp (or a shared utility)
2. Port advanced bot customizer (trait sliders, bot variant presets) to room.cpp bot modal
3. Port 3D character preview to room.cpp bot modal (optional — nice-to-have)
4. Redirect or remove the 4 `g_MatchSetupMenuDialog` references in menutick.c/mainmenu.c/setup.c
5. Remove `pdguiMenuMatchSetupRegister()` from pdgui_menus.h
6. Remove the `screenManifestRegister` call for matchsetup dialog

---


### U-7b Migration Plan (2026-04-05)

Deep audit of the two feature gaps blocking matchsetup.cpp retirement.

---

#### 1. Advanced Bot Customizer (D3R-8)

**Location**: `matchsetup.cpp` lines ~544–914, inside `renderPlayersPanel()` → `##bot_edit_popup` BeginPopup.

**State to migrate** (6 items, all pure UI / parallel-to-config):
```cpp
struct BotTraits { float accuracy; float reactionTime; float aggression; char baseType[32]; };
static BotTraits s_BotTraits[MATCH_MAX_SLOTS]; // parallel to g_MatchConfig.slots[]
static bool      s_BotTraitsInitialized = false;
static bool      s_BotPopupShowAdvanced = false;  // Advanced section toggle per-popup
// Preset cache (ASSET_BOT_VARIANT entries from catalog):
static const asset_entry_t *s_BotPresets[MAX_BOT_PRESETS]; // MAX_BOT_PRESETS = 64
static s32   s_BotPresetCount      = 0;
static s32   s_BotPresetCacheDirty = 1;
static s32   s_BotPresetSelected   = -1;
static char  s_SavePresetName[MAX_PLAYER_NAME] = {0};
```

**Configurable traits**:
- **Accuracy** — `SliderFloat`, 0.0–1.0
- **Reaction Time** — `SliderFloat`, 0.0–1.0 (lower = faster)
- **Aggression** — `SliderFloat`, 0.0–1.0
- **Base Type** — `Combo` from 18 named sim types: `NormalSim, MeatSim, EasySim, HardSim, PerfectSim, DarkSim, PeaceSim, ShieldSim, RocketSim, KazeSim, FistSim, PreySim, CowardSim, JudgeSim, FeudSim, SpeedSim, TurtleSim, VengeSim`

**Presets**:
- **Load Preset**: `BeginCombo` over `s_BotPresets[]` (ASSET_BOT_VARIANT catalog entries). Selecting a preset copies `preset->ext.bot_variant.{accuracy, reaction_time, aggression, base_type}` → `BotTraits`.
- **Save as Preset**: nested `BeginPopup("##save_preset")`, `InputText` for name → calls `botVariantSave(name, baseType, accuracy, reactionTime, aggression, "custom", "", "")`. On success: sets `s_BotPresetCacheDirty = 1`. (botVariantSave writes ini to disk + registers in catalog.)
- Preset cache rebuild: `assetCatalogIterateByType(ASSET_BOT_VARIANT, ...)` — triggered lazily when Advanced section opens and cache is dirty.

**UI flow**:
1. User clicks a bot in the player list → `##bot_edit_popup` opens (`BeginPopup`, not modal)
2. Basic section: Name, Character scroll-list, Type combo, Difficulty combo, Team combo (if teams enabled)
3. Toggle button `"+ Advanced"` / `"- Simple -"` — collapses/expands `s_BotPopupShowAdvanced`
4. Advanced section: Load Preset combo → Base Type combo → Accuracy slider → Reaction slider → Aggression slider → "Save as Preset..." button
5. "Done" button closes popup and clears `s_BotPopupShowAdvanced`

**Trait data is UI-only** — it is NOT currently written back to `g_MatchConfig.slots[]` or serialized to the match config. The BotTraits struct is parallel state that would need to be wired into the bot variant activation path (future work, not required for migration).

**Required headers to add to room.cpp**:
- `#include "botvariant.h"` — for `botVariantSave()`
- `ASSET_BOT_VARIANT` is already available via the `assetcatalog.h` already included in room.cpp

---

#### 2. 3D Character Preview

**Location**: `matchsetup.cpp` lines ~922–964, inside `##bot_edit_popup` popup, right column after `ImGui::SameLine(0, pdguiScale(12.0f))`.

**Infrastructure** (from `port/include/pdgui_charpreview.h`):
```cpp
void pdguiCharPreviewSetRotY(f32 rotY);       // set rotation before request
void pdguiCharPreviewRequest(u8 headnum, u8 bodynum); // triggers FBO render
s32  pdguiCharPreviewIsReady(void);           // non-zero if texture valid
u32  pdguiCharPreviewGetTextureId(void);      // GL texture for ImGui::Image
void pdguiCharPreviewGetSize(s32 *w, s32 *h); // query FBO dimensions
```

**How it renders**:
```cpp
s_BotPreviewRotY += 0.022f;  // ~1.26 rad/s @ 60fps
if (s_BotPreviewRotY > 6.2832f) s_BotPreviewRotY -= 6.2832f;
pdguiCharPreviewSetRotY(s_BotPreviewRotY);
pdguiCharPreviewRequest(bot->headnum, bot->bodynum);  // DERIVED indices, not catalog IDs
float sz = pdguiScale(160.0f);
if (pdguiCharPreviewIsReady()) {
    ImTextureID texId = (ImTextureID)(uintptr_t)pdguiCharPreviewGetTextureId();
    ImGui::Image(texId, ImVec2(sz, sz));
} else {
    // Dark placeholder rect + ImGui::Dummy(sz, sz)
}
// Below: body name label, centered under preview
```

**Known limitation**: Uses `bot->headnum` / `bot->bodynum` (DERIVED legacy indices), NOT `bot->body_id` (PRIMARY catalog ID). In both matchsetup.cpp and room.cpp, selecting a new character only updates `body_id`/`head_id`. The preview reflects whatever bodynum was set at match config init — **does not update live during character selection**. This is an existing limitation in matchsetup.cpp too, not a regression.

**Required change for room.cpp**: Add `#include "pdgui_charpreview.h"` (inside extern "C" block), add the rotation state variable, and widen the bot modal from a fixed 300px window to accommodate the two-column layout.

**Layout consideration**: matchsetup.cpp uses `BeginGroup`/`SameLine`/`BeginGroup` (two-column inline layout). Room.cpp's modal uses `BeginPopupModal` with `ImGuiWindowFlags_AlwaysAutoResize` and a fixed 300px width. To add the preview column, either: (a) widen the modal to `pdguiScale(480.0f)` to fit ~260px controls + ~160px preview + padding, or (b) put the preview above/below the controls in a single column.

---

#### 3. arenaGetName() Dependency

**Exact location in matchsetup.cpp**:
- Override table `s_ArenaNameOverrides[]`: lines 279–325 (45 entries, 0x5126–0x5152, Paradox 0x5150 omitted)
- `s_NumArenaNameOverrides`: line 327
- `arenaGetName()` function body: lines 331–344 (non-static; scoped non-globally — C++ linkage, `const char*` return)

**Used by room.cpp**:
- Line 197: `extern const char *arenaGetName(u16 textId);` declaration
- Line 228: called in `catalogArenaCollect()` (arena list build)

**Used within matchsetup.cpp itself** (will need to stay or move together):
- Line 424 (`arenaCollectCb` → group cache build)
- Line 428 (debug log in `rebuildArenaCache`)
- Line 447 (`matchSetupGetArenaInfo` bridge function)
- Line 1041, 1126, 1171 (arena picker modal rendering)

**Recommended relocation**: Move the override table + `arenaGetName()` into **room.cpp** (remove the `extern` declaration at room.cpp:197, paste the body there instead). matchsetup.cpp's internal uses can then `extern` it from room.cpp. The bridge functions `matchSetupGetArenaCount`/`matchSetupGetArenaInfo` (lines 436–449 of matchsetup.cpp) are C-callable wrappers around `modmgrGetTotalArenas()` / `modmgrGetArena()` — check if any C code calls them before removing (grep `matchSetupGetArena` in src/).

---

#### 4. Migration Plan — Concrete Steps

**Minimum viable migration** (traits only, defer 3D preview):

**Step A — Relocate `arenaGetName()`**
1. Cut `s_ArenaNameOverrides[]`, `s_NumArenaNameOverrides`, and `arenaGetName()` body (lines 279–344) from matchsetup.cpp.
2. Paste into room.cpp before `catalogArenaCollect()` (around line 213).
3. Remove the `extern const char *arenaGetName(u16 textId);` declaration at room.cpp:197.
4. Add `extern const char *arenaGetName(u16 textId);` declaration at the TOP of matchsetup.cpp's extern "C" block (so matchsetup.cpp's own calls still compile).
5. Verify `matchSetupGetArenaInfo` callers: grep `matchSetupGetArena` across src/ — if zero hits, those bridge functions are dead and can be removed later.

**Step B — Add Advanced Traits to room.cpp bot modal**
1. Add state variables to room.cpp (after the `s_EditBotSlotIdx` declaration at ~line 393):
   ```cpp
   struct BotTraits { float accuracy; float reactionTime; float aggression; char baseType[32]; };
   static BotTraits s_BotTraits[MATCH_MAX_SLOTS];
   static bool      s_BotTraitsInitialized = false;
   static bool      s_BotModalShowAdvanced = false;
   static const asset_entry_t *s_BotPresets[64];
   static int       s_BotPresetCount      = 0;
   static bool      s_BotPresetCacheDirty = true;
   static int       s_BotPresetSelected   = -1;
   static char      s_SavePresetName[32]  = "";
   ```
2. Add `#include "botvariant.h"` to room.cpp's extern "C" block.
3. Copy `s_BaseTypeNames[]` (18 strings) from matchsetup.cpp.
4. Add `initBotTraits()` + `rebuildBotPresetCache()` helper functions (verbatim copy works).
5. Call `initBotTraits()` from `pdguiRoomScreenReset()` (where `s_BotModalOpen = false` lives, ~line 2378).
6. Expand the bot modal (lines 2064–2163) to add the Advanced section after the existing Character combo, mirroring matchsetup.cpp lines 793–914. The modal window size may need to grow (currently `pdguiScale(300.0f)` — change to `pdguiScale(360.0f)` to fit sliders comfortably).
7. Reset `s_BotModalShowAdvanced = false` when the modal's Done button is clicked (line 2151).

**Step C — Add 3D Character Preview (optional, nice-to-have)**
1. Add `#include "pdgui_charpreview.h"` to room.cpp's extern "C" block.
2. Add `static float s_BotPreviewRotY = 0.0f;` state.
3. Widen modal: change `pdguiScale(300.0f)` → `pdguiScale(500.0f)` and remove `ImGuiWindowFlags_AlwaysAutoResize` to allow fixed two-column layout.
4. In the modal: wrap existing controls in `ImGui::BeginGroup()`/`ImGui::EndGroup()`, then `ImGui::SameLine(0, pdguiScale(12.0f))`, then the preview group (verbatim from matchsetup.cpp lines 924–962).
5. Note: preview uses `sl->headnum`/`sl->bodynum` (DERIVED). These are stale until `matchStart()` resolves them. Accept the known limitation (same as matchsetup.cpp behavior).

**Step D — Redirect / remove g_MatchSetupMenuDialog references**
Once traits and arenaGetName are migrated, verify the 4 C-side push points are unreachable via the ImGui overlay:
- `menutick.c:253`, `menutick.c:537`, `mainmenu.c:4845`, `setup.c:5736`
If confirmed dead, remove the pushes and eventually the entire matchsetup.cpp + header registration.

**Verdict on effort**:
- Step A (arenaGetName relocation): ~30 min, purely mechanical cut/paste + grep check
- Step B (trait sliders): ~2 hrs, ~150 lines of state + UI code
- Step C (3D preview): ~1 hr, ~60 lines + modal resize/layout
- Step D (dead path removal): ~30 min

**Recommended order**: A → B → C (optional) → D.
Traits (B) can be done without the 3D preview (C) — no dependency between them.

---


### U-8 Audit Results (2026-04-05)

**Verdict: All three remaining items already on the wire. No new code needed.**

| Item | Data | Where synced | Verdict |
|------|------|--------------|---------|
| **Custom weapon slots** | `g_MpSetup.weapons[0..5]` | `CLC_LOBBY_START` write (netmsg.c:3690–3701) as catalog ID strings; `SVC_STAGE_START` write (netmsg.c:740–754) as session IDs | DONE — already synced |
| **Team preset sync** | `ncl->settings.team` (per client) | `SVC_STAGE_START` write (netmsg.c:763–767): copies `ncl->config->base.team` into `ncl->settings.team` and writes per client | DONE — when leader applies preset it sets each player's team, SVC_STAGE_START carries it |
| **Slow motion toggle** | `MPOPTION_SLOWMOTION_ON` bit in `g_MpSetup.options` | `CLC_LOBBY_START` write (netmsg.c:3682) `netbufWriteU32(options)`; `SVC_STAGE_START` write (netmsg.c:737) `netbufWriteU32(g_MpSetup.options)` | DONE — options u32 carries all bits including slow motion |

---

## Phase 3: Online-Specific Polish

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-8 | **Network sync for new features** | Custom weapon slots, handicaps, team presets, and slow motion toggle all need to be synced from leader to clients in online mode. Add the necessary netmsg handlers. | L | DONE (2026-04-05) — all items verified on-wire. See U-8 audit below. |
| U-9 | **Post-match config preservation** | Solo returns to room screen and should preserve match config for quick rematch. Added `pdguiSoloRoomReturn()` (skips `pdguiRoomScreenReset()` call) — "Play Again" button and keyboard shortcut now use it. Online path unchanged (POSTGAME→LOBBY preserves state inherently). | S | DONE (2026-04-05) |

---

## Phase 4: Fix Online Bot Spawn Race Condition

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-10 | **Stage-readiness gate for online bot spawn** | Added 60-frame deferral gate to `botTick` failsafe: if `g_NumSpawnPoints==0 && g_PadsFile==NULL`, defer `botSpawnAll()` up to 60 frames so pads can finish loading. `s_BotSpawnDeferFrames` resets with `s_BotSpawnFailsafeDone` on stage change. Root-cause investigation complete 2026-04-05 — see audit below. Recommended fix: client-side deferred bot-authority activation. | L | PARTIAL — gate added 2026-04-05; root cause documented; fix not yet implemented |

### U-10 Root Cause Audit (2026-04-05)

#### Solo path — confirmed call chain

1. `matchStart()` — `port/src/net/matchsetup.c` (called from solo room screen)
2. → `mpStartMatch()` — `src/game/mplayer/mplayer.c:194`
3. → `mainChangeToStage(stagenum)` — `mplayer.c:542` — queues `g_MainChangeToStageNum`
4. Next main loop frame: `lvReset(stagenum)` — runs **synchronously** (single frame)
   - → `setupLoadFiles()` → pads file loaded → `setupPreparePads()` → **`g_PadsFile` set**
   - → `setupCreateProps()` → bot props allocated; `rooms[0] == -1` at this point
   - → `playerReset()` → waypoints scanned → **`g_NumSpawnPoints` populated**
5. Stage AI scripts execute: `aiMpInitSimulants()` (`src/game/mplayer/mpaicommands.c:14`) calls `botSpawnAll()` — primary spawn path for standard MP maps
6. For solo maps used as arenas (no `aiMpInitSimulants` in AI script): botTick failsafe fires on frame 1 — `g_NumSpawnPoints > 0` and `g_PadsFile != NULL` → `botSpawnAll()` fires immediately, no deferral needed

#### Online path — confirmed call chain

1. `SVC_STAGE_START` received → `netmsgSvcStageStartRead()` — `port/src/net/netmsg.c:840`
2. For combat sim: `mpParticipantsFromLegacyChrslots()` + `mpStartMatch()` — `netmsg.c:1144–1147`
3. → `mpStartMatch()` → `mainChangeToStage(stagenum)` — **`mplayer.c:542` — same as solo**
4. Stage load proceeds synchronously via lvReset on next frame — identical to solo from this point

**Note:** `mainChangeToStage()` IS called on the online client path via `mpStartMatch()`. The co-op path (line 1037) also calls it explicitly; both paths converge on the same stage load sequence.

#### The gap: SVC_BOT_AUTHORITY sent before client confirms stage load

In `netServerStageStart()` (`port/src/net/net.c:698–730`), the server:
1. Broadcasts `SVC_STAGE_START` to all clients in the room
2. **Immediately** (same function, same tick, no wait): sends `SVC_BOT_AUTHORITY` to the first `CLSTATE_GAME` client (`net.c:712–722`)

The client receives both messages over ENet reliable ordered channel (always in order), but both arrive before the client has run `lvReset`. The sequence from the client's perspective:

| Frame | Event |
|-------|-------|
| N | Receives `SVC_STAGE_START` → calls `mpStartMatch()` → `mainChangeToStage()` queued (deferred) |
| N | Receives `SVC_BOT_AUTHORITY` → `g_NetLocalBotAuthority = true` — **authority active, stage not loaded yet** |
| N+1 | `lvReset()` runs synchronously — pads loaded, bot props created, spawn points populated |
| N+2+ | `botTick()` first fires — deferral gate checks readiness |

The **window of exposure** is the period between `g_NetLocalBotAuthority = true` (frame N) and stage load completing (frame N+1). During this window, the authority client would try to run bot AI for props that don't exist yet — but bot props aren't created until `lvReset` → `setupCreateProps()` runs, so `botTick` can't fire in this window. The deferral gate is protection for what happens at frame N+2+.

#### When the 60-frame gate actually matters

The deferral gate (`bot.c:1091–1116`) checks:
```c
if (g_NumSpawnPoints == 0 && g_PadsFile == NULL && s_BotSpawnDeferFrames < 60) {
    s_BotSpawnDeferFrames++;  // defer
} else {
    botSpawnAll();            // spawn, timeout or conditions met
}
```

On the **solo path**, by the time botTick fires, `lvReset` has completed: `g_PadsFile != NULL` and `g_NumSpawnPoints > 0`. The gate passes immediately (0 deferrals).

On the **online dedicated-server path**, two scenarios create genuine deferral:

1. **Mod asset loading delay**: If spawn point data comes from a mod loaded via async manifest, `g_NumSpawnPoints` may be 0 for several frames while the catalog finalizes. `g_PadsFile` is set by stage geometry (synchronous), but if the check requires BOTH conditions... checking `g_NumSpawnPoints == 0 && g_PadsFile == NULL` (AND not OR) means the gate passes as soon as EITHER is satisfied. So a brief `g_PadsFile` delay is the more realistic trigger.

2. **Map with no pad-based spawn points**: If a map has zero waypoint-derived spawn points, `g_NumSpawnPoints` stays 0 permanently. After 60 frames the gate times out and `botSpawnAll()` fires anyway. Bots attempt to spawn but may fail to find valid positions — the secondary validation in `botTick:1102–1115` logs bots still at `rooms[0]==-1`.

#### Game director's proposed fix — evaluation

**Proposal**: Clients send a "stage loaded and ready" confirmation (`CLC_STAGE_LOADED`) after `lvReset` completes. Server only sends `SVC_BOT_AUTHORITY` after all clients confirm.

**Pros:**
- Eliminates the race entirely — server has positive confirmation before delegating authority
- Same synchronous guarantee as solo (stage loaded before bots can tick)
- Consistent with the existing Phase E ready-gate pattern (`READY_GATE_TIMEOUT_TICKS`) already in `netmsg.c:4219–4222`
- Server-side visibility into which clients are ready
- Self-documenting protocol — ordering is explicit in the wire format

**Cons:**
- Adds one round-trip (STAGE_START → CLC_STAGE_LOADED → BOT_AUTHORITY): ~20–60ms extra latency at match start
- Server needs to track per-client load confirmation state and handle timeout for slow/stuck clients
- Only applies to dedicated-server mode (`g_NetDedicated`); listen-server runs bot AI directly
- Adds protocol complexity for a race window that is currently 1 frame wide in practice

**Alternative: client-side deferred activation (simpler)**

Instead of a server handshake, handle the race entirely on the client:

In `netmsgSvcBotAuthorityRead()` (`netmsg.c:4925`), instead of setting `g_NetLocalBotAuthority = true` immediately, set a `g_NetPendingBotAuthority = true` flag. Then in `botTick`, activate authority only when the stage is loaded:

```c
// In botTick, near the top of the per-bot update:
if (g_NetPendingBotAuthority && g_PadsFile != NULL && g_NumSpawnPoints > 0) {
    g_NetLocalBotAuthority = true;
    g_NetPendingBotAuthority = false;
}
```

This defers bot authority to the same readiness conditions the deferral gate already tracks. No server changes required. No new messages. Zero RTT cost.

**Recommendation**: The client-side deferred activation is the minimum correct fix. It collapses the 60-frame timeout into a proper condition check and makes the deferral deterministic instead of time-bounded. The game director's handshake approach is architecturally cleaner (server knows clients are ready) and would be the right choice if server-side observability matters, but adds protocol complexity. Suggest implementing the client-side fix first (low risk, no wire changes), and deferring the full handshake to a follow-up if dedicated-server observability becomes a concern.

#### Key file/line references
- `netServerStageStart()` — `port/src/net/net.c:698` — sends STAGE_START then immediately BOT_AUTHORITY
- `netmsgSvcBotAuthorityRead()` — `port/src/net/netmsg.c:4925` — sets `g_NetLocalBotAuthority = true`
- `mpStartMatch()` — `src/game/mplayer/mplayer.c:194` — calls `mainChangeToStage(stagenum)` at line 542
- `botTick()` — `src/game/bot.c:1055` — deferral gate at lines 1091–1116
- `s_BotSpawnFailsafeDone` / `s_BotSpawnDeferFrames` — `bot.c:1089–1090` (static locals, reset at `lvframe60==0`)

---

## Execution Notes

- `s_IsSoloMode` flag in `pdgui_menu_room.cpp` controls Solo vs Online behavior differences
- `matchStart()` in `port/src/net/matchsetup.c` is the Solo launch path
- `netLobbyRequestStartWithSims()` is the Online launch path
- Post-match: Solo → `pdguiSoloRoomOpen()`, Online → room state POSTGAME → LOBBY
- Phase 1 tasks are independent — can be done in any order
- Phase 3 U-8 depends on Phase 1 completion (can't sync features that don't exist yet)
- Phase 4 is independent and can be investigated in parallel with Phases 1–3

**Suggested order**: U-5 (trivial) → U-6 (verify) → U-1 → U-2 → U-3 → U-4 → U-8 (net sync) → U-7 (retire legacy) → U-9 (post-match verify) → U-10 (spawn race root cause)

U-1 through U-6 completed 2026-04-05. Phase 1 complete. U-8 DONE 2026-04-05 (all net sync verified on-wire — no new code needed). U-9 DONE 2026-04-05. U-10 PARTIAL (stage-readiness gate added 2026-04-05; root cause fully documented 2026-04-05; recommended fix: client-side deferred bot-authority activation in `netmsgSvcBotAuthorityRead`). Remaining: U-7 (retire legacy), U-10 fix implementation.
