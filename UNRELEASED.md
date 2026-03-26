# Unreleased Changes

> Player-facing changelog for the next release. Written in plain language.
> When a stable release is pushed, this content becomes the release notes body,
> then this file is cleared for the next cycle.

## New Features

- Mesh-based collision system: real polygon collision for player movement instead of simplified bounding boxes
- Component-based mod system with per-asset folders and INI manifests
- Modding Hub with Mod Manager, INI Editor, and Model Scale Tool
- Bot Customizer with trait editing and save-as-preset in match setup
- Network mod distribution (PDCA archives, zlib chunks, download prompt)
- Mod Pack export/import (PDPK format with compression)
- Dynamic participant system replacing fixed chrslots bitmask
- Dynamic stage table with heap allocation and bounds-checked accessors
- Server Platform Foundation: hub lifecycle, room system, player identity
- Phonetic connect codes (shorter than word-based codes)
- Asset Catalog with 11 asset types and string-keyed lookups

## Improvements

- Pause menu: START double-fire fix, B button navigation
- 31-bot support (up from 8 on N64)
- Full 63+ character roster available in multiplayer
- Logging always on (no --log flag needed)
- Stage index domain separation prevents out-of-bounds crashes on mod stages
- Game now looks for data folder next to the executable first, not in AppData
- Missing ROM dialog creates the data folder and a readme explaining what's needed
- Missing ROM dialog opens the correct folder in Explorer
- Visual effects system: new ASSET_EFFECT type in Asset Catalog for moddable shader effects
- Collision debug visualization (F9) logs mesh stats
- Unified release system: client + server ship together in one package

## Bug Fixes

- B-17: Mod stages loading wrong maps (catalog smart redirect)
- B-14: START button opening and immediately closing pause menu
- B-16: B button not working for menu navigation
- CI corruption at boot and after MP return
- Fixed data directory search order (exe dir first, then working dir, then AppData)
- Fixed post-build data copy not running (was blocked by server target guard)
- Fixed release script PS5 compatibility (7 syntax fixes)
- Fixed release script hanging on duplicate tags (auto-overwrite with notification)

## Technical

- Legacy g_ModNum system fully removed
- modconfig.txt parsing removed (mods require mod.json)
- Shadow asset arrays removed (catalog-backed caches only)
- fileSlots 2D array flattened to single dimension
- Protocol version 21 (chrslots u64 in SVC_STAGE_START)
- EXE icon embedded via dist/windows/icon.rc
- CMakeLists.txt: fixed missing endif() for WIN32 DLL copy block
