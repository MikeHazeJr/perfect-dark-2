# Lobby Unification Task List

> Both lobbies ALREADY share `pdgui_menu_room.cpp` via `s_IsSoloMode` flag. Architecture is 90% unified.
> This list closes the remaining feature gaps and retires legacy code.
> Back to [index](README.md) | Related: [tasks-current.md](tasks-current.md)

---

## Phase 1: Close Feature Gaps in Room Screen

1. **Custom Weapon Slot Editing** — Add 6-slot custom weapon editor to room screen (Solo has it via legacy path, Online only has preset dropdown). Network sync for online.
2. **Handicap Sliders** — Wire `pdgui_menu_mpsettings.cpp` handicap controls into room UI. Network for online.
3. **Team Auto-Presets** — Expose 5 presets from `pdgui_menu_teamsetup.cpp` in room screen team section.
4. **Save/Load Scenario** — Surface JSON scenario save/load (`port/src/scenario_save.c`) in room UI. Leader-only for online.
5. **Slow Motion Toggle** — Add to Online options (already in Solo).
6. **SP Characters** — Verify all 76 heads / 63 bodies selectable when unlocked in both modes.

---

## Phase 2: Retire Legacy Code

7. **Audit + remove `pdgui_menu_matchsetup.cpp`** (1,582 lines) if fully superseded by `pdgui_menu_room.cpp`.

---

## Phase 3: Online Sync

8. **Network sync handlers** for custom weapon slots, handicaps, team presets, slow motion.
9. **Post-match flow verification** — Solo returns via `pdguiSoloRoomOpen()`, Online via `POSTGAME→LOBBY`.

---

## Phase 4: Fix Online Bot Spawn Sequencing

10. **Make online bot spawn path match solo's synchronous sequencing** (pads load → bots allocate → spawn). Current defensive fixes are band-aids.

---

All items OPEN as of 2026-04-05.
