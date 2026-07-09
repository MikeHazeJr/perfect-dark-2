# Full-Parity Extraction + Utilization (mods = base, nothing lost) -- 2026-07-07

**Directive (Mike, 2026-07-07):** "Properly fix the code base to conform to our
mods-equal-to-base-content principle and proper extraction and utilization of the
rom data with accessible formats (our `.pdxxx` archives). Make sure no information
is lost or utilized differently in a way that omits functionality or breaks things."
Chose **Full parity (all utilization)** over lossless-extraction-only.

## Where we are (grounded in the 2026-07-02 asset-pipeline audit + live scope)

The principle is ~90% realized: the catalog is universal, base content is extracted
to `.pdxxx` and consumed identically to mods, and the DECODE is pixel-correct
(B-943 CI palette-offset, B-945 RDP-parity I4/I8/IA16/RGBA32). The shipped game
renders + plays correctly. The non-conformance is that the extraction is **lossy**,
so the `.pdxxx` is not a complete representation of the ROM and base content cannot
fully round-trip. Three families lose data:

| Family | Lost today | Kind |
|--------|-----------|------|
| **pdtexture** | N64 source format id; CI palette/TLUT (82% of textures are CI); mip chain LOD1-4 (LOD0 kept; 2,788 textures carry 5 LODs) | info-lost + omitted-functionality |
| **pdmesh** | per-face -> collision binding not in faces.json | info-lost (mesh-edit round-trip) |
| **pdweapon** | custom held/fire meshes not emitted to public `.pdmesh` | info-lost (weapon modding) |

**Key finding (scope, 2026-07-07):** the fast3d renderer does NO mipmapping --
`GL_TEXTURE_MIN_FILTER` is `GL_LINEAR`/`GL_NEAREST`, and there is no
`glGenerateMipmap` anywhere in `port/fast3d/`. So the dropped N64 mip chain is a
genuinely OMITTED FUNCTIONALITY (distant-texture minification/anti-alias), not just
a round-trip gap -- which is why full parity (not extraction-only) is correct.

## Pipeline touchpoints

- Extraction: `port/src/romextract_pdtexture.c` (`s_emitTexture` @151 ->
  `romExtractDecodeTextureImages`, currently returns RGBA LOD0 + w/h only),
  `romextract_pdmesh.c`, `romextract_pdweapon.c`. Arena texture LOD read already
  exists: `romextract_pdarena.c:5901` reads `numlods` (<=5) -- reuse that path.
- Schemas: `.pdtexture` manifest is `pd_schema_version: 1` (id, texture_file,
  source_state, size). Bump to v2, ADDITIVE (v1 consumers unaffected).
- Consume: `mod_texture_source.c` / `texLoad` -> `catalogResolveTexture` -> texpool;
  renderer `port/fast3d/gfx_opengl.cpp` (texture upload + MIN_FILTER).
- Cache tokens: each emitter has a decode-token string (pdtexture's is
  `..._cipalfix_b943_iafix_b945`); bump to force a one-time re-extract.

## Phased plan (each phase leaves the pipeline WORKING + is independently committable)

### Phase 1 -- Lossless extraction (ADDITIVE, no render change, low risk)
Achieves "no information lost / mods = base" on its own.
- **1a pdtexture schema-v2:** extend `romExtractDecodeTextureImages` (new out-struct:
  n64 format id, surface_type, CI palette/TLUT, all mip LODs). Emit into the archive:
  `format`+`surface_type` in the manifest; `palette.bin`(+json) when CI; `texture.png`
  stays LOD0; add `texture_lod1..N.png`. Bump the cache token; re-extract; verify
  conformance (9,066 archives) + `all_family_source_gate_smoke` + a render smoke
  (unchanged pixels for LOD0).
- **1b pdmesh:** emit per-face -> collision binding into `faces.json` (additive field).
- **1c pdweapon:** emit custom held/fire meshes to public `.pdmesh`.

### Phase 2 -- Full utilization (render/runtime, higher care, per-piece verified)
Removes "utilized differently in a way that omits functionality."
- **2a Renderer mipmapping:** upload the extracted mip chain to GL (or generate if a
  LOD is absent) + set `GL_LINEAR_MIPMAP_LINEAR` minification. N64-faithful distant
  filtering. Verify: render smokes + visual (no regression on magnification).
- **2b CI palette runtime use:** wire any palette-driven behaviour (palette swaps/
  effects) off the preserved TLUT; static textures stay RGBA32. Verify: identify
  whether base uses palette animation at all first (may be a no-op = documented).
- **2c Collision round-trip:** consume the per-face collision binding (mesh-edit
  parity). Verify: collision smokes.
- **2d Weapon meshes:** consume the public weapon meshes. Verify: weapon smokes.

## Verification (every step)
Round-trip conformance (ROM -> `.pdxxx` -> re-encode -> compare to ROM) + the existing
whole-tree conformance suite + family source-gate smoke + render/collision/weapon
smokes. NEVER leave the extraction or render path half-converted between commits.

## Sequencing
Phase 1 fully (all 3 families) before Phase 2 -- the lossless `.pdxxx` is the
foundation everything else consumes, and it is the safe half. Within Phase 1, do
pdtexture first (the bulk + the omitted-functionality driver). This is a focused
multi-session effort, NOT a tail-of-session sprint, precisely because a half-applied
change here breaks all texture rendering.

## Progress

- **Phase 1a step 1 DONE (2026-07-07):** pdtexture is now schema-v2 and emits
  `n64_format` + `num_lods` + `has_alpha`. `romExtractDecodeTextureImages` exposes the
  N64 pure-format enum and mip LOD count via two additive out-params (both already
  computed during decode; the arena caller passes NULL). The **public `.pdtexture`
  source archive** now records what the ROM held -- the game continues to load from
  that editable source (native-source contract intact: the change is in the emitter
  that PRODUCES the public source, not a new ROM/cache read path; the runtime still
  consumes the same `texture.png`). Verified: `asset_archive_conformance`
  3503/3503 `.pdtexture`, `all_family_source_gate_smoke` 36/36, sampled manifests show
  `num_lods=5` preserved on mip-carrying textures across varied `n64_format` ids.
- **Next:** emit the CI palette (TLUT) and the actual LOD image data as new archive
  files (extend the conformance `allowed`-list for `.pdtexture`), then Phase 2 renderer
  mipmapping.
- **NB (tooling):** the B-801 smoke memory guard mis-refused a launch with ~18 GB free
  of 32 GB (`run.ps1:1186`); overridden with `PD_SMOKE_SKIP_MEMORY_GUARD=1` for this
  verification. Worth checking which metric it reads.
