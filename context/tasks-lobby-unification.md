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
| U-4 | **Save/Load Scenario** | JSON scenario save/load exists (`port/src/scenario_save.c`) but isn't surfaced in the room UI. Add save/load buttons. For online, only the room leader can load a scenario. | M | OPEN |
| U-5 | **Slow Motion Toggle** | Available in Solo's options but missing from the Online options list in the room screen. Add it. | XS | DONE |
| U-6 | **SP Characters in MP** | 76 heads and 63 bodies registered in catalog with feature-lock gates. Verify all SP characters are selectable in both Solo and Online character pickers when unlocked. Memory note: "ALL heads registered in catalog, SP-only available via unlock system." | S | DONE (confirmed: no filter; all chars shown in both paths) |

---

## Phase 2: Retire Legacy Code

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-7 | **Audit pdgui_menu_matchsetup.cpp** | Confirm all 1,582 lines of functionality are covered by `pdgui_menu_room.cpp`. If so, remove the file and update all references (CMakeLists, includes, menu manager routing). | M | OPEN |

---

## Phase 3: Online-Specific Polish

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-8 | **Network sync for new features** | Custom weapon slots, handicaps, team presets, and slow motion toggle all need to be synced from leader to clients in online mode. Add the necessary netmsg handlers. | L | OPEN |
| U-9 | **Post-match flow verification** | Confirm Solo returns to room screen via `pdguiSoloRoomOpen()`, Online returns via POSTGAME→LOBBY state transition. Both should preserve the match setup for quick rematch. | S | OPEN |

---

## Phase 4: Fix Online Bot Spawn Race Condition

| # | Task | Description | Effort | Status |
|---|------|-------------|--------|--------|
| U-10 | **Investigate fundamental sequencing difference** | Solo bot spawning is synchronous (pads load → bots allocate → spawn runs). Online has potential race conditions between `SVC_BOT_AUTHORITY`, stage loading, and spawn point population. The defensive fixes (void detection, underground clamp, stuck init, `chraTick` guard) are band-aids. The real fix is ensuring the online path has the same sequencing guarantees as solo. | L | OPEN |

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

U-1, U-2, U-3, U-5, U-6 completed 2026-04-05. Remaining items OPEN.
