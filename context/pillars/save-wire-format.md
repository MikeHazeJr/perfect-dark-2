# Save / Wire Format

> Version-pinned, migration-aware, mod-friendly. Save files are PC-native JSON. Wire protocol carries catalog ID strings, not integer indices. Three version constants; mixed-version play is rejected at handshake.

---

## What it is

Two persisted formats and one transient format share the same versioning discipline:

1. **Save format** - PC-native JSON files at known paths (`agent_<name>.json`, `player_<name>.json`, `mpsetup_<name>.json`, `system.json`). Replaces N64 EEPROM. `SAVE_VERSION = 2`.
2. **MP setup format** - binary WAD format for MP setup blocks. `MPSETUP_VERSION = 2`.
3. **Wire format** - ENet UDP frames. `NET_PROTOCOL_VER = 46`.

All three are version-pinned in headers and verified in tests; mixed-version mismatches are rejected at handshake.

Code:

- Save format: [port/include/savefile.h](../../port/include/savefile.h), [port/src/savefile.c](../../port/src/savefile.c).
- Save migration framework: [port/src/savemigrate.c](../../port/src/savemigrate.c).
- MP setup: [port/include/mpsetups.h](../../port/include/mpsetups.h), [port/src/mpsetups.c](../../port/src/mpsetups.c).
- Bit-pack primitives: [src/game/savebuffer.c](../../src/game/savebuffer.c).
- Wire protocol: see [pillars/connectivity.md](connectivity.md).

---

## Save format

`SAVE_VERSION = 2` at [port/include/savefile.h:41](../../port/include/savefile.h:41) (SA-4: string IDs replace raw integers).

Four save types:

- **`saveagent`** ([savefile.h:55-110](../../port/include/savefile.h:55)) - per-agent profile. Version, name, totaltime, `besttimes[60][3]` (60 stages x 3 difficulties), `coopcompletions[3]` bitmask, `firingrangescores[9]`, `weaponsfound[6]`, control mode arrays, audio volumes, `challengecompleted[128]`. Generous allocations explicitly to escape N64 bit-pack limits.
- **`savemplayer`** ([savefile.h:116-157](../../port/include/savefile.h:116)) - per-MP-player profile. `head_id[CATALOG_ID_LEN]` and `body_id[CATALOG_ID_LEN]` are the SA-4 string IDs, not raw indices. Stats are u32 (no truncation). Medals u32. Playtime u32 seconds.
- **`savempsetup`** ([savefile.h:172-194](../../port/include/savefile.h:172)) - MP match setup. `stage_id[CATALOG_ID_LEN]`, `weapons[8]` (expanded from 6), `bots[SAVE_MAX_BOTS]` (32), `playerTeams[SAVE_MAX_PLAYERSLOTS]` (8).
- **`savesystem`** - system-wide settings (controls, audio, video, etc.).

JSON parsing: minimal hand-written tokenizer at [savefile.c:40-100+](../../port/src/savefile.c:40), same approach as [modmgr.c](../../port/src/modmgr.c) and [updater.c](../../port/src/updater.c).

---

## Save migration framework

[port/src/savemigrate.c](../../port/src/savemigrate.c). Chain-based.

API:

- `saveMigrateRegister(type, fromVersion, toVersion, fn)` - 32-slot registry.
- `saveMigrateBackup(filepath, version)` - copies to `<path>.v<N>.bak` (refuses to overwrite an existing backup).
- `saveMigrateCheck(file)` - returns 0 (target match), 1 (migration available), -1 (file is newer, downgrade case).

The framework is wired and ready for chains (1->2->3->...); only one version step (1 to 2) is in flight today.

### `mpsetupfileLoadWad` migration rule

Per [tests/test_save_migration.cpp:208-224](../../tests/test_save_migration.cpp:208), the live `if (version < 2)` block in [src/game/mplayer/mplayer.c:4474-4493](../../src/game/mplayer/mplayer.c:4474) clamps weapons[i] >= 0x27 to `MPWEAPON_DISABLED` and zeros the random-filter mask. This handles the 2026-04-26 weapon cull that retired the Goldfinger 64 weapons.

A static guard in the test pins the live loader's gate ordering: random-filter unpack, then `if (version < 2)` migration, then next saved field. Future loaders that drop the version gate fail the test.

---

## MP setup format

`MPSETUP_VERSION = 2` at [port/include/mpsetups.h:19](../../port/include/mpsetups.h:19), bumped from 1 on 2026-04-26 alongside `NET_PROTOCOL_VER 43 -> 44` for the Goldfinger 64 weapon cull.

The constant lives in the **public header** (not file-local in [port/src/mpsetups.c](../../port/src/mpsetups.c)) so the test pin (`tests/test_versions_pin.c`) can read the live value through the public header. (Promoted 2026-04-26.)

v < 2 saves migrate via the clamp rule in `mpsetupfileLoadWad`: weapon values >= 0x27 (the old PP9I slot) are clamped to `MPWEAPON_DISABLED` (now 0x28, formerly 0x30); the random-filter mask is cleared.

---

## Wire protocol

`NET_PROTOCOL_VER = 46` at [port/include/net/net.h:12](../../port/include/net/net.h:12). Latest bump 2026-04-28 (S507): mandatory mod-transfer SHA-256 digest on `SVC_DISTRIB_BEGIN`. Cutscene network semantics also v46 (S511).

See [pillars/connectivity.md](connectivity.md) for the full changelog and protocol details.

Mixed-version play is rejected at the ENet auth handshake ([net.c:1560](../../port/src/net/net.c:1560)) and at the presence-channel proto check ([group_session.c:212](../../port/src/group_session.c:212)).

**No integer asset identity may appear on the wire** (constraint, since 2026-04-02). Weapons and models use catalog session u16 refs; scenario uses catalog ID string. The deprecated `net_hash u32` compact form was removed at v27.

---

## Bit-pack primitives

[src/game/savebuffer.c:381-471](../../src/game/savebuffer.c:381), mirrored in [tests/savebuffer_pure.c](../../tests/savebuffer_pure.c):

- `savebufferOr` - set bits at offset
- `savebufferReadBits` - read N bits at offset
- `savebufferClear` - zero buffer

Test coverage at [tests/test_savebuffer.cpp](../../tests/test_savebuffer.cpp): 1-bit, 8-bit, 13-bit cross-byte, multi-field roundtrip mirroring `mpsetupfileSaveWad`, 64-bit `wpnRndPacked`, boundary widths, alternation patterns, zero-buffer reads.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **Save file format compatibility** - config values stored in `pd.ini` via `configRegisterInt / UInt`. Save migration framework exists for future format changes.
- **MPSETUP_VERSION = 2** (S468, 2026-04-26).
- **Catalog ID strings at all interface boundaries** (since 2026-04-02). Wire, save, public APIs use full catalog ID strings; raw N64 array indices (bodynum, headnum, filenum, stagenum, texnum, animnum) must not cross boundaries.
- **`matchslot` / match config: catalog ID strings are primary identity.** `body_id` and `head_id` as strings; integer derivation only at `matchStart` last-mile handoff.
- **30 agent save slots** - hardcoded in filelist struct layout. Cannot increase without breaking save format.
- **`bool` is `s32` in game code** - `<stdbool.h>` is forbidden in `src/game/`. Save struct fields use the project bool type.

---

## What is done

Per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 8:

- Save format is human-readable JSON; mod-friendly, debuggable, extensible.
- Each save file is self-versioned; schema can grow without offset math.
- Migration framework exists and is wired even though only one version step is in flight.
- Save migration test ([tests/test_save_migration.cpp](../../tests/test_save_migration.cpp)) uses pure-helper logic plus static-text guard reading `src/game/mplayer/mplayer.c` to pin the live `if (version < 2)` block.
- Wire protocol version pinned by `tests/test_versions_pin.c`.
- Bit-pack primitives have exhaustive unit tests covering boundary widths up to 63 bits and 64-bit roundtrip.
- Catalog ID strings replace raw indices at all save-file string boundaries (`head_id`, `body_id`, `stage_id`).

---

## Known gaps

- **`tests/savebuffer_pure.c` is hand-synced.** Header at lines 12-17 documents a manual `diff` command for drift audit. If `src/game/savebuffer.c:381-471` evolves, the test mirror silently drifts.
- **`test_save_migration.cpp` migration helper duplicates the live rule.** Lines 54-71 reimplement `migrate_v1_to_v2` and `migrate_random_filter_mask_v1_to_v2`. Static text pin at lines 208-224 catches changes but cannot tell which side moved.
- **Save migration registry is under-exercised.** Framework supports chains (1->2->3->...) but only one migration is in the wild. Cross-migration glue not behaviorally tested.
- **Multiple version constants live in different files.** `SAVE_VERSION` in `savefile.h`, `MPSETUP_VERSION` in `mpsetups.h`, `NET_PROTOCOL_VER` in `net.h`. Constraint ledger is the only place that lists all three together with reasoning. A code reader inspecting one file does not see the others. Suggest cross-reference comment headers.
- **`saveListAgents` API surface** declared at `savefile.h:237` but the disk-scan implementation surface and behavior were not fully read in the audit. May need verification.

---

## Tests

`tests/test_savebuffer`, `test_save_migration`, `test_versions`, `test_versions_pin.c`. Pure mirror `savebuffer_pure.c`. See [pillars/tests.md](tests.md) for the full pd-tests rundown.

---

## Where to look

- For wire protocol full changelog (v27 through v46): [pillars/connectivity.md](connectivity.md).
- For catalog ID convention behind every string field: [pillars/catalog.md](catalog.md).
- For mod distribution which carries SHA-256 digests: [pillars/modding.md](modding.md).
- For why bit-pack matters at scale: see B-12 history in `_old/_archive/` or [systemic-bugs.md](../systemic-bugs.md) SP entries.
