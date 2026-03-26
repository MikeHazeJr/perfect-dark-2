# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Awaiting Build Test

| Item | Status |
|------|--------|
| **SPF-1**: Hub lifecycle, room system, identity, phonetic encoding (S47d) | Run `.\devtools\build-headless.ps1 -Target server` |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **MEM-1**: Asset load state fields in asset_entry_t (S47a) | Needs full cmake pass |
| **B-13**: Prop scale fix -- modelGetEffectiveScale (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open

| Bug | Severity | Status |
|-----|----------|--------|
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing across all mod maps. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Possible missing texture or clear color issue. Needs investigation. |

---

## Active Work Tracks

### Memory Modernization (D-MEM)

| Task | Status |
|------|--------|
| MEM-1: Load state fields | CODED (S47a) -- needs build test |
| MEM-2: assetCatalogLoad/Unload | PENDING |
| MEM-3: ref_count + eviction | PENDING |

### B-12: Participant System

| Phase | Status |
|-------|--------|
| Phase 1: Parallel pool | CODED -- needs build test |
| Phase 2: Callsite migration | DONE (S47b) -- build pass |
| Phase 3: Remove chrslots + protocol v22 | READY -- depends on Phase 2 QC |

### Asset Catalog Expansion

| Task | Status |
|------|--------|
| S46b: Full enumeration (anims, SFX, textures) | PENDING |

---

## Prioritized Next Up

| # | Task | Details |
|---|------|---------|
| 1 | **Collision Rewrite (D2b+)** | Capsule for movement (walk/jump/land), original collision for damage/weapons. Mesh-polygon collision for all objects. |
| 2 | **B-18: Pink sky** | Investigate Skedar Ruins sky rendering. |
| 3 | **B-12 Phase 3** | Remove chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v22. |
| 4 | **B-13 Part 2** | g_ModNum interim fix for GEX scale during catalog-based stage loading. |
| 5 | **Pause Menu Fixes** | End Game overlay, Settings back-out, suppress OG Paused text. |
| 6 | **Starting Weapon Option** | Toggle + weapon picker / random pool in match setup. |
| 7 | **Spawn Scatter** | Distribute across map pads, face away from nearest wall. |
| 8 | **BotController Architecture** | Wrapper around chr/aibot. Extension points for physics, combat, lifecycle. |
| 9 | **Custom Post-Game Menu** | ImGui-based endscreen. Also fully resolves B-10. |
| 10 | **D5: Settings/Graphics/QoL** | FOV slider, resolution, audio volumes. See [d5-settings-plan.md](d5-settings-plan.md). |

---

## Pause Menu UX (S26 feedback)

| Issue | What Mike Wants |
|-------|----------------|
| End Game confirm/cancel too small | Separate overlay dialog. B cancels to pause menu. |
| Settings B-button exits to main menu | Should back out one level only |
| OG Paused text behind ImGui (B-15) | Suppress legacy pause rendering. Low priority. |
| Scroll-hidden buttons | Prefer docked/always-visible, minimize scrolling |

---

## Backlog

- Systemic bug audit: SP-1 remaining files (activemenu.c, player.c, endscreen.c, menu.c)
- S46b: Full asset catalog enumeration
- Update tab UX: version selection + version policy design
- TODO-1: SDL2/zlib still DLL (low priority)
