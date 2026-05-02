# Catalog Phase 3 plan: ROM-once-then-disk runtime conversion

> Status: PROPOSAL. Filed in response to Mike's 2026-05-01 architectural
> directive (verbatim):
>
> > "We need our full catalog migrated properly to confirm to our standard
> >  and utilization plan, where the ROM is an initial asset source and
> >  then we use the extracted assets for loading, sans ROM."
>
> Date: 2026-05-02
> Worktree: `claude/catalog-coverage-audit-0501`
> Companion docs:
> - `context/audits/catalog-coverage-audit-2026-05-01.md` (Phase 1 + Phase 2)
> - `context/audits/rom-extraction-audit-2026-04-30.md` (extraction architecture, Pass 5)

## End state

ROM is consumed ONCE on first launch. After extraction, every runtime
asset load reads from the extracted-on-disk file via FileProvider.
The 32 MB ROM is no longer touched by the running game. RomProvider
becomes a one-shot bootstrap input, not a permanent runtime peer of
FileProvider.

The architectural pillar is owned by:

- `context/pillars/catalog.md` (catalog is the load pipeline).
- `context/audits/rom-extraction-audit-2026-04-30.md` (`base/` vs
  `data/` directory split: project-authored / shippable in `base/`,
  BYOR-extracted / never-shipped in `data/`).
- Memories: `catalog-is-the-load-pipeline`,
  `engine-modernization-vision`.

## Precondition: Phase 2 complete

This plan starts AFTER Phase 2 of the coverage audit lands. As of
2026-05-02 dev `68fb0ae3` Phase 2 has shipped Commits 1-4; Commit 5
(Farsight SFX) verified-redundant; Commit 6 (SFX alias range) accepted
as architectural limit. With Phase 2 complete every catalog asset has
either:

- A `source_filenum` binding (pickable for ROM-once extraction); OR
- An explicit non-applicable status documented in the audit's closure
  table.

That's the registration foundation. Phase 3 is the runtime conversion
ON TOP of that foundation.

## Pass-by-pass conversion

### Pass A: Extraction infrastructure prerequisites

These ship before any per-asset-class slice and are tracked in the
rom-extraction-audit (Section 2 + Section 3.16). Items:

1. **`data/<romid>/` directory tier**: catalog reads from `mods/` then
   `data/` then `base/` with explicit override semantics. The catalog
   API should expose `catalogSetPrimaryFile(entry, "data/<romid>/...")`
   alongside the existing `catalogSetPrimaryRomFilenum`. Use the
   FileProvider handle path that already exists.
2. **First-launch extractor**: invoked from `main.c` after `romdataInit`
   succeeds and BEFORE `assetCatalogRegisterBaseGame`. Walks every
   ROM file slot, writes the inflated bytes to `data/<romid>/<slug>.bin`.
   Idempotent (skips if file exists with matching SHA-256).
3. **SHA-256 hash table**: populate the per-ROM-version hash arrays
   at `port/src/romdata.c:227-246` (currently `NULL`-only). Used at
   launch to validate ROM and select the correct extraction offset
   table.
4. **Hash-verify-on-launch self-heal**: each `data/` file carries a
   companion hash sidecar; on boot the loader scans the sidecars,
   re-extracts any file whose hash mismatches, quarantines the
   corrupted file in `data/.quarantine/`. Source: rom-extraction-audit
   recommendation cluster.
5. **`LOUDFAIL` log channel**: any silent procedural fallback today
   (e.g. `pdguiThemeExtractRomTextures` substituting noise textures
   when ROM extraction fails) routes through a loud diagnostic before
   the substitute is shown. The extractor and the catalog loader both
   emit on this channel.

After Pass A the runtime CAN load from `data/` for any catalog entry
whose primary handle has been switched from RomProvider to FileProvider.
Per-asset-class slices below do those switches one class at a time.

### Pass B: Per-asset-class slices

Each slice is one merge. Standing rules continue (queued build, no
em-dashes, sequential auto-merge).

**Slice 1: weapon models / cart models.**
- Source today: `assetCatalogRegisterWeaponModelFiles` registers each
  hi/lo + cart filenum as ASSET_MODEL with
  `catalogSetPrimaryRomFilenum(e, fnum)`.
- Conversion: at extraction time write `data/<romid>/files/G<filenum>.bin`
  per weapon model file. At catalog registration time call
  `catalogSetPrimaryFile(e, "data/<romid>/files/G<filenum>.bin")`
  instead of `catalogSetPrimaryRomFilenum`. RomProvider handle is no
  longer needed for these entries.
- Consumer: bgun's `catalogHandleByModelSourceFilenum -> assetLoadToAddr`
  pipeline already supports FileProvider handles transparently (per
  `assetload.c::assetLoadToNew` generic path).
- Drop the ROM fallback: nothing to drop (was already strict-catalog
  per S484-followup-2).

**Slice 2: weapon SFX banks.**
- Source today: ASSET_AUDIO entries 0x0000..0x0608 with `runtime_index`
  but no per-entry source-file binding (sounds load through the SFX
  bank as a single ROM segment).
- Conversion: extract each leaf SFX as a self-contained file
  (e.g. `.tbl` slice + `.ctl` header). Per-SFX granularity is
  expensive; the practical cut is "extract the whole SFX bank to
  `data/<romid>/sfx/sfxctl.bin` + `sfxtbl.bin` and have the loader
  bind the ASSET_AUDIO category=SFX entries to that pair." Full
  per-SFX file granularity if Mike wants it; otherwise bank-level
  override via the snd.c `audioPlayFileSound` path that already
  exists for mod tracks.
- Consumer: `snd.c::sndStart` already calls `catalogResolveSound`. If
  the resolved entry has a file_path, the existing
  `audioPlayFileSound(r.path, ...)` branch handles loading.
- Drop the ROM fallback: snd.c stays the same; the SFX bank load at
  startup switches from `assetLoadRomToNew(SEG_SFXCTL, ...)` to
  reading `data/<romid>/sfx/sfxctl.bin` directly via FileProvider.

**Slice 3: weapon UI strings (lang banks).**
- Source today: ASSET_LANG entries with `bank_id` AND now (post Phase
  2 Commit 2) `source_filenum` binding via `catalogSetPrimaryRomFilenum`.
- Conversion: extract each lang bank to `data/<romid>/lang/<bank>.bin`.
  Switch the binding to `catalogSetPrimaryFile`. langLoad migrates
  from `assetLoadRomToNew(langGetFileId(bank), ...)` to
  `assetLoad(catalogGetLangHandle(bank_id), ...)` per the new typed
  helper.

**Slice 4: character models (heads + bodies + hand).**
- Source today: ASSET_BODY / ASSET_HEAD entries bind `source_filenum`
  to the model file. Hand models registered separately as ASSET_MODEL
  with `runtime_index = -handfilenum`.
- Conversion: extract per-character model file to
  `data/<romid>/chrs/<slug>.bin`. Catalog registration switches to
  `catalogSetPrimaryFile`. Chr-spawn / bodyAllocateModel paths
  already route via catalog handles.

**Slice 5: character sounds (footsteps / death / hurt).**
- Live in the same SFX bank as Slice 2. Once the bank-level extraction
  lands, character sounds are accessible via the same path.

**Slice 6: animations.**
- Source today: ASSET_ANIMATION entries register by `animnum` (index
  into `g_Anims[]`); the actual animation data comes from the
  `animations` ROM segment loaded as one block.
- Conversion: extract the animations segment to
  `data/<romid>/animations.bin`. The existing per-anim
  `g_AnimReplacements[animnum]` mod-override path already handles
  individual file replacements via `modAnimationLoadData`. No
  per-anim catalog source binding change required for the bulk
  segment; binding stays at the segment level.

**Slice 7: prop models.**
- Source today: ASSET_MODEL entries cover `g_ModelStates[]` MODEL_*
  with `source_filenum = g_ModelStates[i].fileid` and
  `catalogSetPrimaryRomFilenum`.
- Conversion: extract each prop model to `data/<romid>/props/M<id>.bin`.
  Switch binding to `catalogSetPrimaryFile`. Consumer
  (`modeldef.c::modeldefLoad`) already supports catalog-handle loads
  transparently.

**Slice 8: prop sounds.**
- Same SFX bank as Slice 2.

**Slice 9: stage scene files (bg / tile / pads / setup / mpsetup).**
- Source today (post Phase 2 Commit 1): ASSET_MODEL entries with
  `base:stage_<class>_<filenum>` slugs and
  `catalogSetPrimaryRomFilenum`.
- Conversion: extract each per-stage file to
  `data/<romid>/stages/<stagenum>/<class>.bin`. Switch binding to
  `catalogSetPrimaryFile`. Consumers (`bg.c::bgLoadFile`,
  `setup.c::setupReadFile`, `tilesreset.c`) load through the existing
  `romdataFileLoad` path which is already catalog-aware.

**Slice 10: voice lines.**
- Live in the SFX bank with `category = AUDIO_CAT_VOICE` (currently
  unset on base entries; AUDIO_CAT_VOICE registration is a Phase 2
  follow-up not in the current scope). Once Slice 2 + the
  `AUDIO_CAT_VOICE` re-tagging happens, voice lines are accessible.

**Slice 11: music.**
- Source today: ASSET_AUDIO category=MUSIC with `runtime_index = mp_idx`
  and the music sequence-bank ROM segment.
- Conversion: extract the music sequence bank to
  `data/<romid>/sequences/...`. The per-track override path
  (`audioPlayFileSound`) already supports mod music; `data/`-side
  base music gets the same treatment.

**Slice 12: SFX banks (the residual catch-all from Slice 2).**
- After Slice 2 the SFX bank is on disk; this slice handles the
  remaining cleanup of the `g_AudioRussMappings` table and
  consideration of whether to expose the alias range as catalog-
  routable (deferred per Coverage Audit Section 3.D ACCEPTED LIMIT).

**Slice 13: UI chrome assets.**
- Tracked separately under
  `context/audits/rom-extraction-audit-2026-04-30.md` Section 3.
  The `pdguiThemeExtractRomTextures` path already writes loose files;
  the conversion here is to redirect the catalog's source binding to
  those files instead of the live ROM-in-memory texture cache.

### Pass C: Drop RomProvider from runtime

Once every base ASSET_* entry's primary handle is FileProvider, the
runtime ROM mapping (`g_RomFile` in romdata.c) is no longer touched by
gameplay code. romdataInit becomes a one-shot extraction-only
codepath: open ROM, validate hash, drive the extractor, close ROM,
free the buffer. RomProvider implementation can be retired; the only
remaining consumer is the extractor itself.

This is the architectural endpoint Mike's directive points at. Once
every asset class is on disk and the runtime never touches the ROM
buffer, the BYOR ROM is a one-time bootstrap input and the project
satisfies the canonical pillar.

### Pass D: Hash-verify and self-heal in steady state

Once the runtime never touches the ROM, `data/` integrity becomes the
critical surface. Each launch:

1. Walk `data/<romid>/` SHA-256 sidecars.
2. Any mismatch -> quarantine the file to `data/.quarantine/<timestamp>/`
   and re-extract from the original ROM (which the user keeps for
   exactly this re-extraction case).
3. Log to LOUDFAIL channel; UI surfaces a one-time toast warning.

If the ROM is no longer present at re-extraction time, the launcher
prompts the user to provide it (one-time). Match the UX flow proposed
in the rom-extraction-audit Section 3.16.

## Sequencing

The slices are independent at the catalog layer (each touches its own
ASSET_* type binding); they are not independent at the extractor layer
(the extraction code grows incrementally). Recommended order:

1. Pass A complete (one merge per item, 5 merges).
2. Slice 9 (stage scene files): smallest scope, well-isolated, exercises
   the FileProvider plumbing for the BG / collision / setup load chain.
3. Slice 7 (prop models): well-isolated, large entry count, validates
   per-entry overhead.
4. Slice 3 (lang banks): small entry count, large per-entry size,
   validates the lang.c migration template.
5. Slice 1 (weapon models): proves the bondgun load chain works end to
   end on disk.
6. Slices 2, 4, 5, 6, 8 (sound / character / anim / prop sound / etc.):
   batch as scope and review effort allow.
7. Slice 10 (voice) + Slice 11 (music) + Slice 12 (SFX residual):
   final cleanup.
8. Pass C: retire RomProvider from runtime.
9. Pass D: self-heal hardening.

Total estimated commit count for Phase 3: 15-20 merges across 6-12
weeks of cadence (one merge per session, multiple sessions in flight
in parallel where slices don't share files).

## Standing rules

- Queued build only via `devtools\build-session.ps1`.
- No em-dashes.
- Auto-merge each slice / pass-item independently per
  `feedback_auto_merge_by_default`.
- Hypothesis framing on subjective judgments.
- File:line + URL evidence for every claim.
- B-IDs in `bugs.md` for any new bug discovered.

## Open questions for Mike

These are the architecturally significant calls that surface during
Phase 3 prep. Mike decides; tactical defaults proceed otherwise.

1. **Per-SFX file granularity vs. bank-level extraction.** Slice 2
   above cuts at the bank level (one `data/<romid>/sfx/sfxctl.bin`
   pair) for parsimony. Per-SFX file granularity (1545 individual
   `.wav` or `.tbl` slices) is more mod-friendly but adds 1545
   extracted files. Default: bank-level. Mike confirms or upgrades.

2. **`base/` vs `data/` for project-authored extracted content.**
   The rom-extraction-audit calls for `base/` to hold project-authored
   shippable content and `data/` to hold BYOR per-user extractions.
   Some assets straddle (e.g. the F11+F13 weapons.pdbase is in `base/`
   but the per-weapon model files live in the ROM). Default
   interpretation: `base/` holds explicit project artifacts (.pdbase
   archives, themes); `data/<romid>/` holds extracted ROM bytes.
   Mike confirms.

3. **AUDIO_CAT_VOICE re-tagging in Phase 2 follow-up.** Coverage audit
   Section 3.H notes this as OPEN low-priority. Phase 3 Slice 10
   depends on it. Recommend rolling it into Slice 10 itself rather
   than as a separate Phase 2 commit. Mike confirms.

4. **Phase 3 cadence.** Per the rom-extraction-audit, the migration is
   safe to do over weeks rather than rushing. Recommend one slice per
   session, multiple sessions in flight where slices don't share
   files. Mike confirms cadence preference.

## Cross-references

- Coverage audit (Phase 1 + 2):
  `context/audits/catalog-coverage-audit-2026-05-01.md`.
- Universality Sweep (selectors):
  `context/audits/catalog-universality-sweep-2026-05-01.md`.
- ROM-extraction architecture audit (Pass 5 final):
  `context/audits/rom-extraction-audit-2026-04-30.md`.
- Catalog pillar:
  `context/pillars/catalog.md`.
- Weapon proving-domain design (.pdbase pattern that proves the F-side
  of this conversion):
  `context/designs/catalog/catalog-full-pipeline-weapons.md`.
