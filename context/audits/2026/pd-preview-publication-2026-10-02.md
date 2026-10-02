# Native menu preview repair publication

Workbench: T-ASSETS-051, T-TOOLING-008. Publication owner:
`01a0f5c1-d031-77b0-a372-3391765df0d5`, model gpt-6. Inherited implementation
attribution remains gpt-6.1-sol.

Preview commands now map viewport and scissor coordinates through the selected
framebuffer's dimensions. The preview pass pins its viewport in frame-owned
memory before deferred GBI execution, isolates projection and menu state, and
keeps its requested aspect instead of applying the main-window conversion.
Loading executes inside that isolated pass because it can finish and draw in
the same call. Catalog resolution failures and pending replacement models no
longer publish an old or cleared preview. Request submission and rendering use
the same singleton menu slot; integrated-body checks use the native body index.

The repair is present in the already-built accepted client `4AE22FA3...`.
October 1 retained focused profile/preview tests pass 10 cases/82 assertions;
the overlapping broader cohort passes 246 cases/10,033 assertions. The preview
tests cover deferred viewport lifetime, unrelated command preservation, missing
models/pending replacements, and offscreen framebuffer coordinate scaling.
Their receipts are in
`.claude/pd-initial-integration/20261001T041916343Z-naAive/`.

October 2 review found all seven production/header files unchanged from the
initial reviewed source snapshot. The test source contains the corrected SDK
include used by the successful build. Exact identities are retained in
`.claude/pd-initial-integration/20261002-night/preview-source-integrity.json`.
Publication changes no production behavior beyond that reviewed candidate.

Visual fidelity remains pending. Earlier preview runs reached Agent Select but
issued no scripted input or images in the sandbox desktop. Normal user desktop
readiness later passed; the newly verified Computer Use runner capability is a
separate fact and does not establish Perfect Dark app approval or foreground
input. The next preview run requires the bounded resource handoff and supported
app control. Keep the canonical input sequence, assertions, 180-second readiness
wait and 240-second cap. Keyboard traversal follows only after preview passes;
Settings edit/restart and physical-device acceptance remain separate gates.
