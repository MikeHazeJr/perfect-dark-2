# Unreleased Changes

> Player-facing changelog for the next release. Written in plain language.
> When a stable release is pushed, this content becomes the release notes body,
> then this file is cleared for the next cycle.
>
> **Base**: v0.0.7 (released 2026-03-27)

## New Features

*(none yet)*

## Improvements

- Server no longer wastes a player slot on dedicated mode — slot 0 is now available to real players
- Server status bar now shows port only (no raw IP exposed)
- Server log no longer shows client IP addresses — uses client ID/name instead

## Bug Fixes

- B-28: Dedicated server occupied player slot 0, reducing effective capacity to 31 (fixed: slot 0 now assigned to first connecting player)
- B-29: Raw IP visible in server GUI status bar (fixed: port only)
- B-30: Raw IPs in server connection/disconnect log output (fixed: client ID/name instead)

## Technical

- Hub slot pool API: `hubGetMaxSlots()`, `hubSetMaxSlots()`, `hubGetUsedSlots()`, `hubGetFreeSlots()`
- Foundation for future network benchmark → dynamic player cap via `hubSetMaxSlots()`
