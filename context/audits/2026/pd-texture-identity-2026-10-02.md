# Wide native texture identity repair

Workbench: T-TOOLING-008. Owner:
`01a0f5c1-d031-77b0-a372-3391765df0d5`, model gpt-6.

The retained public-source texture generations exhausted the old private tail
of 593 slots while the complete character cohort was loading. Recycling live
generations would invalidate queued models. The PC-only private range now has
62,032 slots and carries unsigned 16-bit identities through generated display
list markers, native texture/cache records, pixel prefixes, and collision/hit
consumers. The glass/shield hit sentinel is outside valid texture identities.
Legacy marker payloads retain their original decoding; public assets continue
to use catalog strings. Native markers exist only in rebuildable RAM/cache.

Custom allocation respects snapshot reservations as well as live generations.
Generation allocation uses a reusable search ceiling, with release/reset and
snapshot transitions reopening eligible slots. A read-only capacity diagnostic
reports custom, retained-generation and snapshot occupancy. No live stage pins
were discarded for a passing cohort. Renderer eviction before identity reuse
and existing source-generation ownership remain intact.

Two residual 4,095 limits in catalog and map material bindings initially rejected
the widened IDs. Both now use the native range boundary. The new marker tests
were explicitly registered before accepting the complete native test cohort.

Accepted October 1 evidence:

- Native texture tests: 12 cases / 189,649 assertions pass.
- Source gates: six model/texture/animation/audio cases pass.
- Animation: 19/19; body/head: 13/13; characters: all 63 templates pass.
- Corrected client `D9CFC0B8...`, immutable seed `8227ba09...`, clean native exits.

Receipts, failed first attempts and final source integrity are retained in
`.claude/pd-initial-integration/20261001-native-validation/`, particularly
`summary.json` and `material-bounds-rerun/`. The later accepted client
`4AE22FA3...` also passes the CCTV and four texture-source dependency cohort.
This publication isolates the native texture identity repair; opt-in member
trace instrumentation is a separate unit.

These gates prove the bounded native consumers. Reviewed visual contact sheets,
full animation/audio fidelity, broader nonvisual coverage and ordinary menus
retain their independent acceptance gates. No public payload was changed or
proprietary asset added to Git. No cleanup or new heavy operation ran during
publication preparation.
