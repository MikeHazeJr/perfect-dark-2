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
- Added named input profile slots and per-controller profile assignment, including custom/raw controller devices.

## Changed

- Dev Window build, release, and push sync commits now describe the staged files and include live state paths in the commit body.
- The release script mirrors Codex memory before release commits and warns if the notes file looks stale.
- Replaced the old Settings Controls surface with a single actionmap-backed Input binding table.

## Fixed

- Removed the outdated v0.0.7 dedicated-server release notes that were being reused for new releases.
