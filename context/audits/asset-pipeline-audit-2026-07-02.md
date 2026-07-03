# Asset Pipeline Audit -- Extraction, Utilization, Bloat (2026-07-02)

Point-in-time assessment produced during the `.pdxxx` decode-correctness pass
(B-945/B-946). Covers extraction completeness, archive bloat, and catalog/
manifest utilization across all 27 typed families. Verified against live source
where a claim was load-bearing; unverifiable claims are marked as such.

Retention: supersede or move to `_old/audits/` once the punch list below is
worked or explicitly deferred.

---

## Headline

The two runtime-visible defects Mike reported (credits motes/font as opaque
squares, textures rendering black) were a single root cause in the extractor's
native-texel decode, now fixed and render-verified (B-945). A second defect
made every boot silently re-extract all 87 scenarios (B-946), also fixed. What
remains is a **fidelity/round-trip backlog** (does not affect the shipped game's
render or play) plus **documentation/guard hardening** in the utilization layer.
There is **no actionable archive bloat**.

---

## 1. Extraction completeness

Emitters: one per family under `port/src/romextract_*.c`. All 27 families emit;
whole-tree conformance passes 8,065 root / 9,066 checked archives.

### Fixed this pass (runtime-visible)

- **B-945 texture decode** (`s_decodeTexToRgba`, now `texture_decode_pure.c`):
  I4/I8 forced-opaque alpha, IA16 byte-order swap, RGBA32 channel scramble.
  These shipped wrong pixels into every extracted texture.png and scene.glb
  image. Fixed with RDP-parity decode + `_iafix_b945` cache bump.

### Fixed earlier, still current (B-943, verified in ledger)

- pdarena collision flags (`collision.flags.json`), BG room lights
  (`room_lights.json`), pdanim root-motion + camera channels (B-772),
  pdweapon recoil/slide fields, pdcharacter name langid, pdsfx loop_state.

### Remaining fidelity/BYOR backlog (NOT runtime-visible -- deferred)

| Item | Family | Impact | Notes |
|------|--------|--------|-------|
| N64 source format id not emitted | pdtexture | round-trip only | runtime consumes flat RGBA32 by design |
| CI palette / TLUT not preserved | pdtexture | modding round-trip | 82% of textures are CI; decode is correct, re-encode loses exact palette |
| Mipmap/LOD chain dropped (LOD0 only) | pdtexture | fidelity | 2,788 textures carry 5 LODs |
| Mesh face->collision binding not in faces.json | pdmesh | mesh edit round-trip | lower priority than pdarena (fixed) |
| pdweapon custom held/fire meshes not emitted to public .pdmesh | pdweapon | modding completeness | private-slot bridge, acceptable per constraints |

Priority: all fidelity/BYOR. None is a silent-corruption recurrence. Work them
as an additive/versioned schema pass when modding round-trip becomes the focus.

---

## 2. Archive bloat

**No actionable bloat found.** Container is standard ZIP with zlib deflate
(`port/src/modarchive.c`); the writer falls back to STORED only when deflate
would not shrink already-compressed data (PNG/MP3/OGG). Per-family volume:
animations 35 MB, scenarios 33 MB, sfx 29 MB, textures 15 MB.

Two patterns that look like bloat but are intentional and constraint-mandated:

- **Per-texture / per-mesh atomic archives** (3,503 `.pdtexture`, one ZIP each):
  modder atomicity -- each asset is independently packable/shareable.
- **Nested `.pdmesh` duplication inside bodies/heads/weapons**: the
  self-contained-archive constraint (each `.pdbody` embeds its own mesh). Disk
  duplication is allowed; the catalog dedupes by SHA-256 at ingest so RAM/runtime
  is not duplicated.

`_meta/manifest.json` + hashes overhead is ~2-5 KB/archive, justified for
provenance + source validation. Recommendation: none. If disk ever matters,
canonicalize duplicate nested meshes on disk behind a content-address store --
but that trades away the self-contained-archive constraint and is not worth it now.

---

## 3. Utilization (catalog SSoT + manifests)

The catalog (`assetcatalog_load.c`) is the lifecycle owner: 4 load states,
refcounts, 5 payload-ownership kinds, typed `catalogLoadTypedAsset` /
`Retain` / `ReleaseTypedAsset`. 25 typed-lifecycle call sites across game+port.
Manifests drive stage/screen/match load+unload; `manifestClear` +
`manifestMPTransition` teardown appears at 23 sites.

### Verified findings

- **Textures have no typed load/release** -- but this is **not a leak**.
  `texLoad` routes through `catalogResolveTexture` -> `mod_texture_source.c`,
  which is a catalog-owned resolution path. Payloads live in the texpool and are
  freed on stage-pool reset. Actionable only as: add fallback telemetry on
  texture source-resolution failure (currently quieter than the Wave-7 families).
- **Bodies/heads load once at character select and are not released** until
  session end. Intentional (perf: avoids re-loading modeldefs on every menu
  open). Actionable as **documentation** (mark as bundled-equivalent lifetime),
  not a code change, unless a long session's accumulation is measured to matter.
- **SFX/music** lifecycle is owned by the audio subsystem, not the catalog. The
  catalog registers identity; playback owns memory. Consistent with the
  save-wire/audio constraints. Actionable as: confirm custom `.pdsong` sequence
  slots release on scene exit (spot-check, likely fine).
- **ROM fallback residue**: 23 `romdataFileLoad` references, most on the
  legitimate first-boot extraction-bootstrap path (`romExtractIsBootstrapping`
  already suppresses those from fallback telemetry). No new silent runtime ROM
  fallback found; Wave-7 families fail loud/fatal on post-extraction fallback.

### Verified clean (sub-agent flagged, checked against runtime)

- Weapon-graph + effect-graph sub-asset release IS wired: the release free-path
  (`assetcatalog_load.c:1842/1845`) calls `s_catalogClearWeaponGraphRuntime`
  (`weaponGraphRuntimeClearWeapon` by runtime_index; `weaponGraphRuntimeClearAsset`
  for projectile/entity) and `s_catalogClearEffectGraphRuntime`
  (`effectGraphRuntimeClearAsset`). No leak. The sub-agent's concern was unfounded.

---

## 4. Punch list (prioritized)

1. **(fidelity, deferred)** pdtexture schema-v2 additive pass: emit N64 format
   id + surface_type (~cheap), then CI palette + mipmap round-trip (extension).
2. **(hardening)** Add texture source-resolution fallback telemetry parity with
   the Wave-7 families so a missing/invalid `.pdtexture` is loud, not quiet.
3. **(doc, DONE 2026-07-02)** Recorded bodies/heads/fonts/lang as
   bundled-equivalent lifetime in `constraints.md` so their load-without-release
   is not mistaken for a leak.
4. **(deferred)** pdmesh face->collision binding + pdweapon public mesh emit for
   full modding round-trip.

Weapon/effect graph unregister was verified wired (see above) -- no work needed.
None of the remaining items blocks the shipped game. Item 2 is small; items 1/4
are a scoped modding-round-trip project.

---

## 5. Test-suite direction (the "slow and janky" complaint)

The jank is concentrated in **static source-text pins** -- tests that assert an
implementation string is present (e.g. a cache-kind constant, a code line).
They break on unrelated refactors (four such pins had to be reconciled this pass
alone: B-938 vertex-colour, two pdmesh version labels, a menu_graph cutscene
pin). The slowness is the `pd_headers` build dependency and full-catalog-boot
fixtures.

**The rebuild model is `tests/test_texture_decode_pure.cpp`** (added this pass):
a real linked pure translation unit (`texture_decode_pure.c`) tested
behaviorally with concrete input/output vectors -- 12 cases / 92 assertions,
sub-second, no fixture boot, no source-text pin. It cannot drift on refactor
because it checks behavior, not text.

Direction for the rebuild: peel decode/format/serialization logic into pure TUs
(as the texture decoder was) and test those behaviorally, retiring the
corresponding static-text pins. The `build-session -SelfTest` pattern (added for
the queue this pass) is the analogous fast, build-free model for PowerShell
tooling. This is a per-family migration, not a big-bang rewrite.
