# Save / Wire Format

> Version-pinned, migration-aware, mod-friendly. Save files are PC-native JSON. Wire protocol carries catalog ID strings, not integer indices. Three version constants; mixed-version play is rejected at handshake.

---

## What it is

Two persisted formats and one transient format share the same versioning discipline:

1. **Save format** - PC-native JSON files at known paths (`agent_<name>.json`, `player_<name>.json`, `mpsetup_<name>.json`, `system.json`). Replaces N64 EEPROM. `SAVE_VERSION = 2`.
2. **MP setup format** - binary WAD format for MP setup blocks. `MPSETUP_VERSION = 3`.
3. **Wire format** - ENet UDP frames. `NET_PROTOCOL_VER = 58`.

All three are version-pinned in headers and verified in tests; mixed-version mismatches are rejected at handshake.

Code:

- Save format: [port/include/savefile.h](../../port/include/savefile.h), [port/src/savefile.c](../../port/src/savefile.c).
- Save migration framework: [port/src/savemigrate.c](../../port/src/savemigrate.c).
- MP setup: [port/include/mpsetups.h](../../port/include/mpsetups.h), [port/src/mpsetups.c](../../port/src/mpsetups.c).
- Bit-pack primitives: [src/game/savebuffer.c](../../src/game/savebuffer.c).
- Wire protocol: see [pillars/connectivity.md](connectivity.md).

---

## Save format

`SAVE_VERSION = 2` continues to version system, player, and match-setup JSON
documents. D-005 option A gives Agent Profile JSON its own current schema
version so Agent evolution does not invalidate those independent stores.

Four save types:

- **`saveagent`** ([savefile.h:55-110](../../port/include/savefile.h:55)) - per-agent profile. Version, stable name identity, total time, the exact 21-stage by 3-difficulty completion domain, co-op completion masks, firing-range scores, discovered weapons, all ten gamefile flag bytes plus `unk1e`, audio/control state, and the exact 30-challenge by 4-player-count completion matrix.
- **`savemplayer`** ([savefile.h:116-157](../../port/include/savefile.h:116)) - per-MP-player profile. `head_id[CATALOG_ID_LEN]` and `body_id[CATALOG_ID_LEN]` are the SA-4 string IDs, not raw indices. Stats are u32 (no truncation). Medals u32. Playtime u32 seconds.
- **`savempsetup`** ([savefile.h:172-194](../../port/include/savefile.h:172)) - MP match setup. `stage_id[CATALOG_ID_LEN]`, `weapons[8]` (expanded from 6), `bots[SAVE_MAX_BOTS]` (32), `playerTeams[SAVE_MAX_PLAYERSLOTS]` (8). Every saved bot includes its authoritative `profile_id`; body/head IDs and the cached type/difficulty remain presentation/runtime derivations.
- **`savesystem`** - system-wide settings (controls, audio, video, etc.).

JSON parsing: minimal hand-written tokenizer at [savefile.c:40-100+](../../port/src/savefile.c:40), same approach as [modmgr.c](../../port/src/modmgr.c) and [updater.c](../../port/src/updater.c).

### Agent Profile Store and activation transaction

Agent names are the sole stable profile identity. Names are limited to the
engine's ten-character domain and accept alphanumeric characters, spaces,
underscores, and hyphens, with no leading or trailing space. The store publishes
at most 30 validated profiles in deterministic case-insensitive order. It
rejects malformed documents, unsupported versions, wrong field types or array
shapes, duplicate fields, path/embedded-name mismatch, and case-insensitive
identity collisions before any live state changes.

`saveLoadAgent` parses into a candidate first. Commit then replaces
the gamefile, resets cheats, applies audio/control/options, restores challenge
completion, and recomputes challenge availability. `gamefileCaptureOptions`
is shared by the JSON writer and legacy pak serializer so retained option bits
cannot drift. Create, copy, delete, list, and revision APIs all use this store;
legacy pak file IDs are not part of the public menu boundary.

[port/src/agent_session.c](../../port/src/agent_session.c) is the one
production activation transaction. It calls the JSON load and then applies the
per-agent preference, social, hub, and presence identity. Agent Select and the
CLI fast path both call this boundary exactly once; failed activation leaves
the previous agent active. Agent Create, Copy, and Delete mutate the same JSON
store without implicitly changing the active identity.

D-005 option A is implemented and validated. One Agent Profile v3 JSON owns
both game state and every per-agent preference. Current documents must contain
the complete required schema. Only the exact legacy v2 writer shape may enter
migration; an optional legacy INI sidecar is parsed into the same candidate
before the current JSON is atomically committed, then retired idempotently.
Current documents never read or write sidecars. Activation prepares the
complete candidate before live game, preference, social, hub, or presence
identity changes, and active deletion is rejected. User-driven settings, mod,
update, pause-option, and Combat Simulator playlist mutations save through the
same active v3 document.

The final executable matrix passed exact migration and restart 24/24, corrupt
activation rollback and active-delete rejection 21/21, ordinary Agent Select
20/20, invalid campaign CLI rejection 12/12, and the 17-mission plus restart
release run 23/23. The last run reloaded all 17 best times from v3 with the
preferences block intact.

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

`MPSETUP_VERSION = 3` at [port/include/mpsetups.h](../../port/include/mpsetups.h), bumped on 2026-07-30 so each bot's permanent public `.pdbotprofile` catalog ID survives a binary setup round trip. PC blocks grow from 80 to 4096 bytes.

The constant lives in the **public header** (not file-local in [port/src/mpsetups.c](../../port/src/mpsetups.c)) so the test pin (`tests/test_versions_pin.c`) can read the live value through the public header. (Promoted 2026-04-26.)

v0-v2 files are read using the historical 80-byte block size. The loader preserves the v < 2 weapon clamp, derives a base profile ID from each legacy bot's type/difficulty, expands every block in memory, and writes v3 on the next save.

B-965 hardening (2026-07-30): binary setup files require exact header and
per-block reads/writes. Files with truncated data, versions newer than the
current schema, excessive setup counts, or an invalid one-based default setup
index are rejected. B-1046 (2026-08-12) completes this boundary: the loader
deserializes into a temporary setup table and publishes only after the entire
file succeeds, so a rejected file preserves the prior live table.

B-964 hardening (2026-07-30): JSON MP setup writers emit only canonical
`weapon_ids`, preferring `g_MatchConfig.weapon_ids` so catalog-only creator
weapons survive. The deprecated numeric `weapons` array is read-only migration
input and applies only when the canonical field is absent. An unresolved
nonempty catalog ID, invalid legacy weapon, or unreconstructable required bot
rejects the entire load. B-1046 adds a complete structural/version preflight
and restores the full live setup/match snapshot after any later semantic
failure.

All PC JSON saves and binary MP setup saves now use
`port/src/save_atomic.c`: serialize to a sibling candidate, check write/flush/
close, durably flush the candidate, and atomically replace the destination only
after complete success. Agent, system, MP player, MP setup, saved-scenario, and
binary setup writers share this rule. A failed candidate is removed and the
last good destination remains byte-exact. V-006 production proof injects both
JSON and binary failures after complete candidate writes and passes 17/17.

B-992 capacity hardening (2026-08-08): the private paired custom-weapon domain
now uses every remaining identity in the existing 64-bit MP random-filter
field, indices 41 through 63. The field width and MP setup schema do not
change. Pack/unpack now uses an unsigned `1ULL` mask so bit 63 is defined.
Paired runtime weapon identities remain private catalog adapters and end at
108, within legacy signed-s8 storage. Public saves and network transport still
carry catalog IDs; no numeric custom identity was added to either format.

---

## Wire protocol

`NET_PROTOCOL_VER = 58` at [port/include/net/net.h:12](../../port/include/net/net.h:12). The 2026-08-26 bump gives every stage publication a nonzero epoch and one explicit inactive/waiting/release/active lifecycle. `CLC_STAGE_READY` must echo the exact `SVC_STAGE_START` epoch, and fresh shared gameplay is published only after both the authority and every exact surviving remote participant cross the real post-load boundary. NPC convergence now pairs one full resync with the canonical digest of that exact applied snapshot. v57's endpoint-scoped reconnect transaction, v56's complete cutscene authority stream, v55's authenticated client-settings candidate, v54's transactional typed-state freeze, and earlier bumps remain documented in `net.h` and [pillars/connectivity.md](connectivity.md). Mixed-version peers are rejected at auth.

B-1075's v55 category bytes remain unchanged through v58: STAGE and AUDIO remain compact category
tokens. Their receiver contract is now explicit—resolve the authoritative string
ID to its exact catalog row, then validate that the concrete type maps forward to
the category. Never infer `ASSET_MAP` or `ASSET_AUDIO` from those lossy tokens.
The source-frozen strengthened ordinary two-client receipt
`.claude/smoke-verify-runs/results-20260813T091851Z.json` passes 56/56 with one
exact Felicity load per process and no typed mismatch, load skip, transition
rejection, or rollback. The category contract is therefore a locked
regression gate; stage-transition, reconnect, and friend-play lifecycle proofs
remain before release closure.

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
- **MPSETUP_VERSION = 3** (2026-07-30 bot-profile catalog identity).
- **Catalog ID strings at all interface boundaries** (since 2026-04-02). Wire, save, public APIs use full catalog ID strings; raw N64 array indices (bodynum, headnum, filenum, stagenum, texnum, animnum) must not cross boundaries.
- **`matchslot` / match config: catalog ID strings are primary identity.** `body_id` and `head_id` as strings; integer derivation only at `matchStart` last-mile handoff.
- **30 validated JSON agent profiles** - the product capacity exposed by Agent Select. The profile name remains the stable identity and is limited to ten engine characters.
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
- **System save is not loaded automatically at boot.** Agent activation is now unified and validated, but startup ownership for `saveLoadSystem` remains a separate release-audit question.

---

## Tests

`tests/test_savebuffer`, `test_save_migration`, `test_versions`,
`test_versions_pin.c`, `test_menu_graph`, and
`test_debug_campaign_complete_hotkey`, `test_agent_profile_codec`, and
`test_autocampaign_cli_plan`. The final D-005/B-1063 focused selector passes
1,475 assertions across 29 cases, and the complete suite passes 57,277
assertions across 1,070 cases; see [pillars/tests.md](tests.md) for the
ordinary-client receipt paths.

---

## Where to look

- For wire protocol full changelog (v27 through v58): [pillars/connectivity.md](connectivity.md).
- For catalog ID convention behind every string field: [pillars/catalog.md](catalog.md).
- For mod distribution which carries SHA-256 digests: [pillars/modding.md](modding.md).
- For why bit-pack matters at scale: see B-12 history in `_old/_archive/` or [systemic-bugs.md](../systemic-bugs.md) SP entries.
