# T-ASSETS-016 `.pdeffect` source/schema evidence

Date: 2026-08-08

## Finding corrected

The base catalog and extractor emitted six synthetic effect presets that did
not represent the production explosion, spark, or smoke tables. The replacement
registers three profile-library assets and serializes every native row:

- 26 explosion profiles from `g_ExplosionTypes`
- 27 spark profiles from `g_SparkTypes`
- 23 smoke profiles from `g_SmokeTypes`

The serializer rejects any omitted row count. Stored values are copied
field-for-field. Explosion numeric sound refs are converted to public catalog
audio IDs. Fixed light, camera-shake, renderer, smoke-spawn, and scorch timing
semantics are recorded from the production consumers. Target, attachment,
priority, enable, owner, and scorch-enable are declared callsite-owned. The base
game has no independent beam or screen profile table, so none is fabricated.

## Verification

- Isolated `assetwave5` client/updater build: PASS.
- Isolated `assetwave5` test build: PASS.
- `[t-assets-016]`: 181 assertions / 3 cases PASS.
- `[effect_graph]`: 235 assertions / 8 cases PASS.
- `python tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

Two failed pre-execution test-build attempts are retained in coordination
history: the new test first used the wrong Catch2 include, then included C game
headers in a C++ translation unit. Both harness-only defects were corrected;
neither failure exercised production code.

## Truthful boundary

This evidence proves base extraction and the public v2 profile schema. It does
not prove v2 activation or edited-source behavior in the game. Shared parsing,
execution, typed dependencies, and fail-closed activation remain assigned to
T-ASSETS-017 through T-ASSETS-020, so T-ASSETS-016 remains `partial` rather than
`implemented` or `validated`.
