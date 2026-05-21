# Unreleased Changes

> Running release-note source for the next GitHub release.
> Keep bullets short, player-facing where possible, and specific to what changed.

## Highlights

- Dev Window v2 now treats the Kanban board, Codex memory mirror, and release-note source as live project state that should commit with the code.
- Git Pull in Dev Window v2 now fetches remote commits first and lets the operator choose the exact commit to fast-forward to.
- Release notes now come from this running list instead of the old dedicated-server placeholder text.
- Settings now has a simplified Input tab for profiles, connected devices, bindings, and tuning.

## Added

- Added a tracked Codex memory mirror at `tools/kanban/memories.md` so GitHub can carry the project's active memory state alongside code and Kanban state.
- Added shared project-state sync tooling for Dev Window v2 and the release script.
- Added Codex daily automations for the morning maintenance flow and architecture review.
- Added Bug Tracker delete support in the Kanban browser.
- Added a reusable PD2 large-change sweep skill for auditing broad changes against runtime code, tests, context, Kanban, and release notes.
- Added c3813 online lifecycle guards for listen-host/client smoke coverage and reconnect/drop-in/drop-out state restoration.
- Added named input profile slots and per-controller profile assignment, including custom/raw controller devices.
- Added Blender-ready map visual exports inside scenario/arena archives, including OBJ/MTL scenes, decoded TGA wall/floor textures, and material TSV ledgers.
- Added self-contained weapon archives that embed model, animation, audio, projectile, and entity payloads for editing and sharing.

## Changed

- Dev Window build, release, and push sync commits now describe the staged files and include live state paths in the commit body.
- The release script mirrors Codex memory before release commits and warns if the notes file looks stale.
- Memory Review now parses live memory task groups and stores review markup separately from the source memory file.
- Weapon behavior graphs now feed more runtime weapon actions, including recoil/recovery, throw/special handling, projectile spawn values, auto-aim, and sight behavior behind the debug graph-runtime toggle.
- Replaced the old Settings Controls surface with a single actionmap-backed Input binding table.
- Cutscene skipping now uses a held button with a contextual radial progress prompt instead of an accidental tap.
- Startup asset extraction now reuses validated per-family cache stamps and shows a centered progress modal with smoother time-weighted progress.

## Fixed

- Removed the outdated v0.0.7 dedicated-server release notes that were being reused for new releases.
- Cleared stale Kanban Bug Tracker rows B-318 through B-326.
- Fixed Combat Simulator post-match endscreen X and Quit/Disconnect confirmation clicks so the visible results screen can be exited normally.
- Fixed online post-match return-to-room resync so clients replay room assignment, match settings, and playlist state instead of returning with partial room state.
- Fixed failed online match asset transfers so active match prep declines the manifest and returns to lobby instead of repeatedly requesting the same failed content.
- Fixed listen-host logging during early startup so hosted smoke and diagnostics write to the host log before normal system init.
- Fixed Main Menu Escape/title-X closing so the menu actually exits instead of playing cancel and reopening.
- Fixed Main Menu entry timing so the Carrington Institute camera intro finishes before the menu opens and accepts input.
- Fixed gameplay interaction prompts so labels like Open door do not appear during cutscenes.
- Fixed Falcon 2 mission-start viewmodel stretch by hiding the laser sight beam during weapon equip and bounding its muzzle matrix lookup.
- Fixed mouse/keyboard Campaign weapon switching: Q tap/hold, scroll wheel next/previous, number-key direct select, and stale weapon-function HUD labels now use the current weapon state.
