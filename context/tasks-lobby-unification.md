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

## Phase 3: Online-Specific Polish

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-8 | **Network sync for new features** | Weapon slots, team assignments, slow motion already synced via CLC_LOBBY_START/SVC_STAGE_START. Added per-player handicap sync (u8 × MAX_PLAYERS) to both message paths. | L | DONE |
| U-9 | **Post-match flow verification** | Solo: created `pdguiSoloRoomReturn()` that preserves config (unlike `pdguiSoloRoomOpen()` which resets). Wired into endscreen "Play Again" and Enter/Start shortcut. Online: POSTGAME→LOBBY already preserves config. | S | DONE |

---

## Phase 4: Fix Online Bot Spawn Race Condition

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-10 | **Bot spawn stage-readiness gate** | Added U-13 hardening to `botTick` failsafe: defers `botSpawnAll()` until `g_NumSpawnPoints > 0` or `g_PadsFile != NULL` (stage data loaded). 60-frame timeout (~1s) prevents infinite deferral. Logs diagnostic on every deferral. Architecture was already sound (`lvupdate240` gate), this adds explicit spawn-point readiness verification. | L | DONE |

---

## Execution Notes

- `s_IsSoloMode` flag in `pdgui_menu_room.cpp` controls Solo vs Online behavior differences
- `matchStart()` in `port/src/net/matchsetup.c` is the Solo launch path
- `netLobbyRequestStartWithSims()` is the Online launch path
- Post-match: Solo → `pdguiSoloRoomReturn()` (preserves config), Online → room state POSTGAME → LOBBY
- Phase 1 tasks are independent — can be done in any order
- Phase 3 U-8 depends on Phase 1 completion (can't sync features that don't exist yet)
- Phase 4 is independent and can be investigated in parallel with Phases 1–3

**Suggested order**: U-5 (trivial) → U-6 (verify) → U-1 → U-2 → U-3 → U-4 → U-8 (net sync) → U-7 (retire legacy) → U-9 (post-match verify) → U-10 (spawn race root cause)

U-1 through U-6 completed 2026-04-05. Phase 1 complete — all feature gaps closed.
U-8, U-9, U-10 completed 2026-04-05. Phases 3 & 4 complete — net sync, post-match flow, bot spawn hardening done.
Remaining: U-7 (retire legacy matchsetup.cpp — blocked on 2 feature gaps + arenaGetName relocation).

U-1 through U-6 completed 2026-04-05. Phase 1 complete — all feature gaps closed.
