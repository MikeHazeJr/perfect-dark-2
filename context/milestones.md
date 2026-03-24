# Release Milestones

> Planned stable release builds. Each milestone builds on the previous.
> Back to [index](README.md)

## v0.1.0 "Foundation"
**Goal**: Stable single-player and local multiplayer with mod support

**Features:**
- All D3R phases complete (component mod architecture, asset catalog)
- Combat Simulator with custom bot traits (D3R-8)
- Modding hub accessible from main menu (D3R-7)
- Mod pack export/import (D3R-10)
- Legacy mod paths removed (D3R-11)
- Network distribution protocol (D3R-9)
- UI scaling across resolutions
- Death loop and crash fixes
- Bot count limit raised to match UI

**Release criteria:** All QC tests passing, clean build on dev, no known crash bugs

**Effort:** S — mostly done, needs QC pass and stabilization

---

## v0.2.0 "Connected"
**Goal**: Multiplayer with friends, basic dedicated server

**Features:**
- Dedicated server stable and functional
- Player identity system (pd-identity.dat)
- Phonetic IP encoding for easy connection
- Lobby leader system
- Server password protection and bans
- B-12 participant system fully migrated (Phases 2-3, chrslots removed)
- Stage decoupling Phases 2-3

**Depends on:** v0.1.0
**Effort:** L (~4-6 weeks)

---

## v0.3.0 "Community"
**Goal**: Social hub and content sharing

**Features:**
- Social hub / lounge on dedicated server
- Room system (concurrent independent sessions)
- Public/private mod sharing with versioning
- Direct player-to-player content sharing
- Whitelists (user-ID-based, cross-server)
- Server content library (cached public mods)

**Depends on:** v0.2.0
**Effort:** L (~4-6 weeks)

---

## v0.4.0 "Federation"
**Goal**: Mesh networking and cross-server play

**Features:**
- Mesh peer discovery protocol
- Cross-server matchmaking
- Signed transfer tokens
- Server browser via mesh
- Trust levels for mesh peers

**Depends on:** v0.3.0
**Effort:** XL (~6-8 weeks)

---

## v0.5.0 "Studio"
**Goal**: Full internal mod creation pipeline

**Features:**
- Model Studio (import/export, texture replacement, skins)
- Audio Tools (export/import)
- Level Tools (export/import)

**Depends on:** v0.1.0 (can run parallel with networking track)
**Effort:** L (~4-6 weeks)

---

## v1.0.0 "Forge"
**Goal**: Complete creative platform

**Features:**
- Forge-style level editor (separate main menu entry)
- D5 Settings/Graphics/QoL complete
- Update system (D13) for self-updating
- Full polish pass

**Depends on:** v0.4.0 + v0.5.0 converged
**Effort:** XL (~6-10 weeks)
