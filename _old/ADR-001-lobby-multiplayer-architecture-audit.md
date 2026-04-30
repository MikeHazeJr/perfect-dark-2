# ADR-001: Lobby & Multiplayer Architecture Audit

**Date**: 2026-03-20
**Status**: Complete
**Scope**: Network message protocol (netmsg.c, net.c), lobby system (netlobby.c), update system (updater.c), and supporting port code

---

## Summary

Full audit of the ENet-based multiplayer protocol and lobby architecture. Focus on buffer safety, message encoding/decoding correctness, and string handling across the port's network layer and related subsystems.

## Findings

### Fixed Issues

**1. strncpy null-termination (MEDIUM — 17 locations across 9 files)**

`strncpy()` does not guarantee null-termination when the source string length >= the specified count. Throughout the port code, strncpy was used to copy player names, URLs, file paths, and other strings into fixed-size buffers without explicitly setting the last byte to `'\0'`.

This is a classic C pitfall. If any source string exactly fills or exceeds the buffer, the result is an unterminated string, which can leak adjacent memory contents into log messages, network packets, or file paths.

Files and locations fixed:
- `port/src/net/netmsg.c` — 3 locations (CLC_AUTH name, CLC_SETTINGS name, SVC_STAGE_START name)
- `port/src/net/net.c` — 4 locations (join address, preserved player name, recent server addr/hostname)
- `port/src/updater.c` — 2 locations (asset URL, hash URL from GitHub API)
- `port/src/modmgr.c` — 4 locations (mod name, version, author, dirpath)
- `port/src/fs.c` — 5 locations (pathBuf, baseDir, modDir, saveDir)
- `port/src/libultra.c` — 3 locations (eeprom path variants)
- `port/src/config.c` — 3 locations (config string value, section name in save/load)
- `port/src/input.c` — 1 location (scancode name)
- `port/src/optionsmenu.c` — 2 locations (controller name, key name)

Fix pattern: Add explicit `buf[SIZE - 1] = '\0'` after every strncpy call.

**2. strcpy → snprintf in netmsg.c (LOW)**

One `strcpy(g_MpSetup.name, "server")` replaced with `snprintf()` for consistency and bounds safety. The literal "server" fits in the 18-byte buffer, but snprintf is the right pattern to prevent future regressions if the string ever changes.

### Verified — Not Bugs

**3. netbufReadStr return values**

All callers of `netbufReadStr()` properly check for NULL returns and validate the `src->error` field. The read functions set `src->error` on underflow, and all message handlers check this before acting on parsed data. No unbounded reads possible.

**4. SVC_LOBBY_SYNC player count**

The lobby sync message encodes `numplayers` as a u8 and iterates exactly that many entries. Each entry is read with bounds-checked netbuf operations. The receiving side validates against NET_MAX_CLIENTS. No overflow possible.

**5. CLC_LOBBY_MODE leader validation**

The server validates that the sender is the current lobby leader before accepting mode-change requests. Non-leaders cannot change game settings. The leader election protocol (first CLSTATE_LOBBY client) is deterministic and consistent.

### Known Limitations

**6. Version letter suffix parsing**

`versionParse()` in updateversion.h handles numeric major.minor.patch but does not parse letter suffixes (e.g., "0.0.3a"). The suffix is cosmetic only (VERSION_SEM_LABEL in CMakeLists.txt) and does not affect version comparison logic. Low priority — version comparisons work correctly on the numeric components.

## Architectural Assessment

The network protocol is sound. The server-authoritative model with dedicated-server-only networking (Phase 3) eliminated the complexity of host migration and peer-to-peer state conflicts. The ENet channel separation (unreliable for position, reliable for state/events) is well-structured. The lobby leader election is deterministic and handles disconnection gracefully.

The primary systemic issue was strncpy usage — a class bug that affected 17 call sites across the codebase. All instances have been fixed with a consistent pattern. Future code should prefer `snprintf()` for string formatting and ensure explicit null-termination when `strncpy()` is used.

## Decision

No architectural changes needed. The protocol and lobby system are well-designed for the dedicated-server model. The strncpy fixes are defensive hardening, not corrections to broken behavior (no evidence of actual exploitation), but they close a real class of potential memory-leak vulnerabilities.
