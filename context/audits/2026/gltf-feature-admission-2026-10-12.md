# glTF required feature admission - 2026-10-12

Workbench: T-ASSETS-052, under T-ASSETS-001 and T-ASSETS-039.
Writer: codex-f9571c12-6af53c63 (Pipeline), model GPT-6. Status: source
connected; Python/source checks pass; native compilation blocked before launch.
The full extraction/runtime/modding/graph goal is partial.

The shared strict glTF reader previously checked asset.version but did not
honor asset.minVersion or extensionsRequired. The C converter reads accessor
base bytes without sparse overrides or integer normalization. Accepting those
declarations could therefore discard required semantics instead of reporting
an unsupported source. This is an admission correction, not an implementation
of sparse decoding, normalized attributes or extension renderers.

## Connected source contract

- Runtime buffer/provider admission, scene planning and animation inspection
  share the strict document feature check. Minimum versions other than 2.0
  reject. Required extensions reject until an actual complete implementation
  can be registered. Declarations require unique, nonempty strings, and every
  required extension must also be listed in extensionsUsed.
- Sparse accessor declarations reject, including accessors with a base
  bufferView whose replacement values would otherwise be silently ignored.
  normalized must be boolean; true rejects until numeric conversion supports
  it, while false and absence retain existing behavior.
- Optional extensions preserve the standard core fallback. Similarly named
  application metadata inside extras is not interpreted as a root declaration.
  No public source bytes are rewritten, and no alternate authored runtime file
  or ROM fallback is introduced.
- Python archive conformance applies the same feature policy to text glTF,
  GLB and direct scene/buffer dictionary entry points. Mesh/animation converter
  entry points retain feature-specific diagnostics. Rejected provider walks
  publish no references; scene plans and buffer outputs remain empty.
- Compiler/cache versions advance to general10, animation10 and modeldef15
  so earlier conversions cannot stand in for current admission. The existing
  three source-contract version pins change with them.

The standard requires required-extension support before loading and defines
sparse overlays and normalized values as accessor semantics:
[Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Ownership and verification boundary

This batch owns modasset_gltf_document.h/.cpp, the diagnostic entry calls in
modasset_compiler.c, cache version constants in modasset_compiler.h,
asset_gltf_source.py, the corresponding two focused test files, three existing
version pins in test_asset_native_source_contract.cpp, and this audit. N-0611
records the exact supplement. Graph13, the new native shooting profile,
persistent actor/observer work, editor/inventory and all unrelated dirty files
remain with their existing writers. No changes to launcher capabilities,
security, cleanup policy or native qualification are part of this batch.

The nine-file coherent freeze-v2 and queued checks are retained under
`.claude/asset1012-gltf-features/`: source-validation.json records Python20/20,
conformance selftest16 parity plus recursion/9 structured contracts, native
source guard PASS, and zero source drift. C++ contracts exercise real shared
provider/scene admission APIs, including zero visitor calls on rejection;
these remain uncompiled. A separate canonical all-target build admission with
`-RetainHousekeeping` exited1 in native-build-admission.json at
Build-Retention.ps1:545: the native child capability is unreleased. No compiler
or native child launched; no retry or bypass occurred. Test FIFO
q_20261012215817718_26a08b2b and build FIFO q_20261012220100137_43dd87aa were
finished immediately. Final documentation records results after the code freeze;
the eight code/test files remain byte-identical to the passing source checks.
No product/runtime/visual acceptance or Git publication is claimed.

Full UV/material/normal/skin/animation fidelity, actual support for these
rejected features, field-level regional extraction, 27-family edited-source
gameplay witnesses, graph family cutover and the requested creator pack remain
under the original closure plan. This batch does not close those gates.
