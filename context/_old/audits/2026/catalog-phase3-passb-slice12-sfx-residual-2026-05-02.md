# Phase 3 Pass B Slice 12 -- SFX Residual Cleanup

> **Date**: 2026-05-02 PM
> **Session**: catalog-slice12-passc (worktree)
> **Predecessor**: Slice 13 UI chrome correction `e00927a2`.  Slice 12 is the last Pass B item before Pass C (RomProvider drop).
> **Source**: coverage audit Section 3.D ACCEPTED LIMIT (Phase 2 Commit 6 deliberation, 2026-05-02).
> **Plan reference**: `catalog-rom-once-phase3-plan-2026-05-02.md` Slice 12.

---

## Outcome

Doc-only close-out.  No code shape change; `g_AudioRussMappings[]`,
`s_BaseSfx[]`, and the `sndStart` alias decode chain stay exactly as
they are.  This commit pins the architectural rationale at the alias
range comment site so any future contributor reading
`assetcatalog_base_extended.c` finds a single-paragraph summary
pointing to the coverage audit Section 3.D + Phase 3 plan Slice 12 +
the Mike-confirmed user-visible bug closures (Phase 2 Commit 4 + S484
followup-5).

## Why no code change

1. **Catalog already supports leaf-level overrides.**  The 1545
   `ASSET_AUDIO` entries (sound_id 0..0x608) bound to `source_soundnum`
   are mod-overridable via `s_SoundnumOverride` -- Mike validated this
   path during Phase 2 Commit 6 deliberation.
2. **Alias range decodes BEFORE catalog resolve.**  `sndStart` at
   `src/lib/snd.c:2145` decodes the packed `soundnumhack` form.  For a
   high-bit ID (e.g.  Farsight `SFX_813E` = 0x813E), `sp44.hasconfig`
   is set and `sp40.id` is rewritten to
   `g_AudioRussMappings[confignum].soundnum`.  The catalog resolve at
   line 2154 then runs against the post-mapping leaf ID, so any
   leaf-level mod override automatically applies to alias-IDed plays.
3. **Direct alias override has marginal value.**  The dispatch table
   `g_LoadedSoundnums[LOAD_MAX_SOUNDS]` is sized 4096.  Surfacing
   alias IDs directly to the catalog would need 65536-slot indexing
   (256 KB array) plus parallel-index plumbing.  No live use case
   requested.
4. **The user-visible Farsight regression is closed.**  Mike's "Farsight
   fire SFX plays a voiceline" bug landed two structural fixes already:
   - Phase 2 Commit 4 (`68fb0ae3`): regenerated the `L_GUN_*` enum table,
     removed 8 phantom entries, restored `L_GUN_058+` values to
     source-of-truth `gun.h`.
   - S484-followup-5 (`6aaabf44`): SFX enum drift fix; `SFX_813E`
     resolves to 33086 in the current `loader_pdbase_enums.c`.
5. **Phase 3 (data-on-disk) is the better venue if alias-level mod
   control becomes a use case.**  The runtime ROM segment has already
   been disk-backed via Pass A.2 + Slices 2/5/6/8/11.  If a future
   modder needs to override the russ-mapping itself, the cleaner
   refactor is to extract `g_AudioRussMappings[]` as a `data/<romid>/`
   record alongside the SFX bank, not to plumb 65536 catalog slots.

## What this commit lands

- `port/src/assetcatalog_base_extended.c`: extends the existing
  comment block above the SFX table (line 230) with a Slice 12
  close-out paragraph cross-referencing the coverage audit
  Section 3.D and the Phase 3 plan Slice 12.
- `context/audits/catalog-phase3-passb-slice12-sfx-residual-2026-05-02.md`:
  this audit.

## Pass B status after Slice 12

All 13 Pass B slices closed:

| Slice | Status | Lane |
|---|---|---|
| 1 | shipped (`0983b47c`) | weapon models on disk |
| 2 | shipped (`fb7331ce`) | sfxctl + sfxtbl on disk |
| 3 | shipped (`0983b47c`) | lang banks on disk |
| 4 | shipped (`0983b47c`) | character models on disk |
| 5 | shipped (`fb7331ce`, via Slice 2) | character sounds (same SFX bank) |
| 6 | shipped (`fb7331ce`) | animations on disk |
| 7 | shipped (`0983b47c`) | props on disk |
| 8 | shipped (`fb7331ce`, via Slice 2) | prop sounds (same SFX bank) |
| 9 | shipped (`214518b9`) | stage scene files on disk |
| 10 | shipped (`b2122749`) | voice retag (taxonomy) |
| 11 | shipped (`fb7331ce`) | music sequences on disk |
| 12 | THIS COMMIT (doc) | SFX residual / alias range ACCEPTED LIMIT |
| 13 | shipped (`e00927a2`, corrected) | UI chrome to `data/ui/textures/` |

Pass C (drop RomProvider from runtime) is the next coordinated change
in the same session.
