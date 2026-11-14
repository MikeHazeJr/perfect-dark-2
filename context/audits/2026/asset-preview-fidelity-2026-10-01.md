# Asset preview fidelity - 2026-10-01

09:53 current checkpoint: rebuilt native texture chain and residual material bind
bounds now pass12cases189649assertions, six source cases, animation19/19,
body/head13/13 and character63/63 (26fixed/35random_gender/2integrated). Final
clientD9CFC0B8 and new seed8227ba09 bind every corrected receipt. This resolves
the earlier character allocation/binding blocker; operational witnesses do not
certify strict consumed-member hashes or rendered preview fidelity. Old failed
ordinary preview remains11/29 with no input/screenshots. An authorized interactive
executor is still required for labeled captures; no ordinary retry occurred in
the headless grant. Heavy released09:40:56.814UTC; Astral notified. Initial
per-asset corpus/contact sheet and render acceptance remain open. Evidence:
`.claude/pd-initial-integration/20261001-native-validation/summary.json` and
`character-operational-ledger.json`. Current repair attributiongpt-6; inherited
preview source/test attributiongpt-6.1-sol remains unchanged.

Historical08:23 takeover: propagated native texture source repair is uncompiled; existing
preview tests/binary belong to the prior wave. Own CodexSandboxDesktop has
foreground0/input read denied5. Ordinary preview/labeled captures require an
authorized interactive executor. No renderer/capture proof added. Independent
native source gates and75-minute plan are in20261001-texture-repair; previous
35passed/1failed/27untested character ledger and strict member/fidelity gaps remain.

Workbench: **T-ASSETS-051**, dependent on T-TOOLING-008,
T-EXTRACTION-001 and T-RUNTIME-001. Worker: `asset_fidelity`, model
`gpt-6.1-sol`; trusted coordination identity `codex-0e8900ca-467b81e8`.
Canonical Workbench `/api/meta` was checked before each mutation.

Status: **native build and focused regression pass; rendered proof pending**.
Mike's Oct 1 “approved, begin” authorization supersedes the earlier Plex pause.
Exact approved ownership is recorded on the Workbench item. No Git operations,
game launch, extraction, cleanup, or changes to the existing dirty body tests.

## Observed evidence and source diagnosis

Current root checkpoint (06:38 UTC): the October 1 ordinary preview attempt
failed before input, 10/29 assertions and zero screenshots. A shortened private
30s readiness wait expired during weapon bootstrap, before main-menu arming.
It did not reach Agent Create or test this preview candidate. New private copies
restore canonical 180s readiness and 240s overall limits and pass static contract
checks; native retry needs a new parent window. Exact failed receipt and corrected
plan are linked from the initial integration audit. Rendered acceptance remains
pending; the earlier focused native tests remain passing.

Inspected `.claude/menu0927/sep29-r2-virtual-create-open.png` with the image
tool. Agent Create selects Joanna Dark (Combat)/Dark Combat, but the preview
rectangle is black; stretched geometry is visible behind the dialog. This is
the earlier 873C client receipt, not proof of the current source candidate.
It does not identify a failed material, body bind or animation on its own.

The following defects are established from the production source chain:

- `port/fast3d/gfx_pc.cpp:2173`: offscreen viewport/scissor coordinates
  previously used the main target's scale and native height. Framebuffer
  selection sets `active_fb` without changing those main dimensions. A full
  512×512 offscreen rectangle consequently receives the wrong scale/Y origin
  whenever the main dimensions differ. The repair uses the selected target's
  original/applied dimensions and skips main-window offsets. The normal main
  target branch retains its previous mapping.
- `port/fast3d/pdgui_charpreview.c:899`: menu rendering queues pointers to
  mutable player/menu viewport storage, then restores that storage before
  `gfx_run_dl` dereferences it. The pass now pins its emitted viewport commands
  to a `gfxAllocate` frame-owned snapshot. It restores the shared menu
  viewports, VI perspective matrix/pointer/normalization, camera perspective
  pointers and menu Z-buffer flag alongside the existing VI/scissor restore.
- `src/game/menu.c:2618`: the DEFAULT model projection previously overwrote
  the pane aspect with a screen-corrected dialog aspect. An explicit
  `pdguiCharPreviewIsRendering()` branch preserves the FBO-local dimensions and
  pane aspect while building the preview's projection. Ordinary dialog
  conversion is unchanged.
- `port/fast3d/pdgui_charpreview.c:188`: requests previously wrote the current
  multiplayer menu slot while the singleton renderer always consumed slot 0.
  Both character and handle requests now target slot 0.
- `port/fast3d/pdgui_charpreview.c:286`: the complete-body accessor consumes
  the native body domain (`assetcatalog_api.c:1619`), but the preview passed
  the multiplayer carousel index. It now passes `be->runtime_index`; valid
  slot-zero bodies/heads also receive the same compatibility checks.
- `port/fast3d/pdgui_charpreview.c:991`: a cleared FBO was previously published
  ready even when `bodymodeldef` was absent. Publication now requires a loaded
  model with no pending replacement. Loading stays inside the isolated pass
  because `menuRenderModel` can load and draw in the same call. Failed catalog
  selection clears old readiness; failed memory acquisition waits without
  publishing a pass. Selection changes also reset the actual rotation setter.

These source defects are plausible causes of the reported visuals. No claim
is made that all black previews or background distortion are repaired until
the current native client is captured.

## Focused regression candidate

`tests/test_model_preview_fidelity.cpp` has three behavioral cases using the
same production inline seams:

1. Deferred viewport commands retain FBO bounds after mutable player storage
   changes; projection-matrix and next-pass commands retain their pointers.
2. Readiness excludes missing models, unloaded models and pending selection
   replacements, while admitting settled loaded selections.
3. Full 512-square bounds, an inset scissor, OpenGL Y inversion, independently
   scaled 1024×768 offscreen bounds, and explicitly selected 1920×1080 bounds
   use their own target dimensions.

The root registered this test in `CMakeLists.txt`. Two obsolete warning-text
assertions in `tests/test_integrated_head_guard.cpp` were updated in the exact
approved scope; the wider integrated guard is preserved.

## Verification and remaining gates

Final source guard: `python tools/asset_native_source_guard.py` printed
`asset-native-source guard ok`, exit 0, under test-runner FIFO
`q_20261001033329584_cdf39d75`, released immediately afterward. Scoped
`git diff --check` passed. Earlier source-guard FIFO
`q_20261001032714890_053f8b34` also passed and was released.

The root subsequently compiled client/tests successfully and ran the combined
`[model-preview],[profile-state],[charpreview]` gate: 10 cases/82 assertions pass.
The broad input/menu/settings gate passes 246 cases/10,033 assertions. Selectors
overlap. Exact binaries/XML are in
`.claude/pd-initial-integration/20261001T041916343Z-naAive/`. The first managed
runtime failed before seeding on MSYS Python routing; the takeover fixed that
script boundary with tiny integration proof. Actual Agent Create capture and
background propagation await the parent Astral resource window. Readiness remains
a structural loaded-model condition, not pixel occupancy proof.

Material/texture/normal/bind/animation conversion remains a separate fidelity
gate. Current compiler code contains per-corner matrix restoration
(`modasset_compiler.c:8208`), joint-smoothing flags (`:8714`), hierarchy bounds
(`:8761`), material emission (`:9062`) and native animation source rebuilding
(`:9681`). Their presence does not prove that this character renders correctly.
No converter or authored asset representation was changed. Public editable
source remains the runtime input; generated products remain rebuildable cache.
Same-ID source-generation hot reload/revision binding is pending; this repair
does not add a new generation lifecycle.

Owned source freeze (SHA-256; root-owned CMake entry excluded):

| Path | SHA-256 |
|---|---|
| `port/fast3d/pdgui_charpreview.c` | `B4AEF32D7BF81A206D8A2273378A8E7FDA7E4F08CB1A7FF62270B645341E5C45` |
| `port/fast3d/pdgui_model_preview.cpp` | `3DF9D8070A604C8BB2697F18836C622F826F9BB73D3C3AB63588325AE808E022` |
| `port/fast3d/gfx_pc.cpp` | `D5782DB03DC09422B467A670DEF0D2C5FFC8672D9E2AF42FE8E1D272E0A1407B` |
| `port/include/pdgui_model_preview.h` | `880D4A7B23F2A910950A084CBAEB5EBBB0E9174FE1DE17992F3C9144CD3B7E8F` |
| `port/include/pdgui_charpreview.h` | `AC31F7D05843F2CB615BB157E0E4C3E06F5782B5E5841F4F0793AE8B0D1C925A` |
| `port/include/gfx_framebuffer_coords.h` | `CACF38B7EEBD4DA19F1BB22E9EC08BB31288E43FBC2B8CDE955FD3DCFD26BE68` |
| `src/game/menu.c` | `15B5666741DBB54398290DA6075CD6DA96FDD73B60233B57334C12523B5A2E9E` |
| `tests/test_model_preview_fidelity.cpp` | `1EE1E33F9760F02DA3D0EAB35868C99F479AFDED2D397F55DA2F47E2FCDCAE24` |
| `tests/test_integrated_head_guard.cpp` | `8EF7C5CAABD827C6B9314BF786505DEC8CE766670928A5BF5A5C7B32121CCBEA` |
