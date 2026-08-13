# Bug Tracker

> Live one-off defects and release regression gates only. Recurring architectural
> classes belong in [systemic-bugs.md](systemic-bugs.md). Release priority and
> ownership come from the canonical Workbench, not from this file.
>
> The complete pre-consolidation ledger is preserved verbatim at
> [_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md](_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md)
> with SHA-256
> `80A567D268C22901B6117AF310BE206336EC5CC9A9E61B221172F20296093D4E`.

Back to [index](README.md).

---

## Routing rules

- A confirmed current defect stays in **Active 1.0 defects** until its root cause,
  propagation audit, implementation, and required runtime proof pass.
- A historical fix that only needs broad release revalidation belongs in a
  **Release regression cluster**, not as a separate active task.
- Product or infrastructure gaps belong in Workbench tasks or validations. They
  are not duplicated here as bugs.
- Post-1.0 defects remain visible but are never selected by the default 1.0
  prompt unless Mike promotes them.
- The next unused numeric bug ID is **B-1064**. Never reuse an archived ID. If an
  old unnumbered symptom recurs, assign a new B-number with current evidence.

Status terms:

- `confirmed`: current source or a current production repro proves the defect.
- `repro_required`: a credible report exists, but the current build still needs
  a bounded reproduction before code changes.
- `regression_gate`: implementation exists; a broader release receipt must prove
  the behavior remains fixed.
- `post_1.0`: retained, but outside D-004 release scope.

---

## Active 1.0 defects

| ID | Severity | Status | Current truth | Workbench route | Closure gate |
|----|----------|--------|---------------|-----------------|--------------|
| B-919 | Low | confirmed | The collision hit-sound condition in `propobj.c` compares `WEAPON_COMBATKNIFE` twice. The intended second family is not proven, so this must not be fixed by guesswork. | V-010 | Verify intended base behavior from authoritative source, correct the shared mapping, add a focused propagation test, and prove combat-knife plus bolt-family impacts in an ordinary client. |
| B-249 | High | repro_required | A prior playtest reported bot-versus-bot deaths credited to player 0. Static tracing found no hardcoded player-0 write and the existing diagnostic must identify the upstream attacker attribution first. | V-010 | Run a controlled bot-only Combat Simulator match with no player damage, capture attribution diagnostics, fix the authoritative damage/death source, and prove player 0 remains at zero kills. |
| B-242 | Medium | repro_required | Players have previously spawned intersecting walls. Existing pad and ray validation does not prove a complete capsule fits at the committed spawn. | V-010 | Add one shared bounded capsule-placement validator for players and bots, search safe radial candidates or the next pad without infinite retry, then prove representative campaign and Combat Simulator maps. |
| B-174 | High | repro_required | Car Park previously stacked bots at one spawn and then congregated them at one destination. A mitigation and diagnostics exist, but no current ordinary-client closure receipt does. | V-010 | Reproduce with the original map and roster, prove distinct valid spawns and continued AI decisions, fix the shared spawn/path state if it recurs, and retain a timed gameplay artifact. |
| B-183 | High | repro_required | Character bodies or heads previously showed disconnected triangles or holes. Later body/head source fixes narrowed the likely class, but representative current visual proof is absent. | V-010 | Capture representative human, Skedar, integrated-head, mixed body/head, campaign, and Combat Simulator characters from the release candidate with exact catalog identities and no missing geometry. |

These five are the only live one-off 1.0 bugs. Any newly reproduced release defect is
added here immediately and linked to the owning Workbench milestone.

---

## Release regression clusters

The archived ledger contained many `FIXED-PENDING-PLAYTEST`, `MITIGATED`, and
partially diagnosed rows. They remain evidence, but they are consolidated below
so they do not masquerade as 100-plus independent active tasks.

| Cluster | Historical IDs | Current owner | Required 1.0 evidence |
|---------|----------------|---------------|-----------------------|
| Public-source and graph parity | B-385, B-769, B-772, B-801, B-855, B-935, B-938, B-941 | V-003, V-005, V-010, V-012 | Controlled edits for all 27 public families, deliberate production graph consumers, fail-closed selected source, representative visuals/audio/gameplay, and no ROM/native/loose fallback. |
| Campaign and Combat Simulator lifecycle | B-228b, B-316, B-339 through B-345, B-359, B-360, B-365, B-368 | T-TESTS-002, V-010, V-013 | Full campaign through Credits plus repeated Combat Simulator start, end, return, restart, and stage transitions with no crash, stale manifest, black screen, or missing scene. |
| Bot spawn, attribution, and long-session stability | B-19, B-112, B-126, B-142, B-174, B-204, B-205, B-249 | V-010, V-013 | Multi-map bot spawn and navigation, correct kill credit, 31-bot long-duration stress, bounded stack use, clean crash diagnostics, and no silent process exit. |
| Collision and movement | B-145, B-147, B-206, B-242, B-335, B-339, B-350, B-367 | V-010 | Shared capsule/world collision across floor, wall, ceiling, corner, airborne movement, props, and spawn placement on representative campaign and Combat Simulator stages. |
| Menus, physical input, and vehicles | B-152 through B-154, B-198, B-203, B-209, B-230, B-298, B-317, B-351, B-356, B-361 | V-004, T-VEHICLES-002, V-010 | Real MKB and controller navigation, rebinding, live glyph switching, text focus, pause/end screens, root close/reopen, and complete hoverbike operation without input leakage. |
| Rendering and character presentation | B-18, B-179 through B-184, B-253, B-346, B-362, B-366, B-390, B-395 | V-010, V-013 | Representative stage, character, weapon, sky, text, transparency, material, and first-person captures with exact asset identities and no malformed geometry or stale render state. |
| Save, protocol, distribution, and shutdown | B-267 through B-293, B-327, B-349, B-1041 through B-1053 | V-006, T-RELEASE-005, V-013 | Corrupt/truncated rejection without partial mutation, signed identity and manifest checks, atomic replacement/rollback, exact package lifecycle, clean disconnect, and clean shutdown. |

If a cluster fails, promote the exact reproduced symptom to a new active B-number
instead of reopening every historical row.

---

## Locked recent regression gates

| IDs | Gate | Owner |
|-----|------|-------|
| B-1054, B-1055, B-1056 | Future gameplay captures must show an unobstructed first-person view, a lower-right correctly scaled held weapon, authored GLTF material colour, and readable non-white effect colour. | V-012 and V-013 |
| B-1057, B-1058, B-1059 | Friend play must retain distinct identities, fresh signed authority claims, exactly one listen authority, a separately typed signed match-server route, one idempotent non-authority join, failure rollback, and no probe or relay descriptor promoted to `netStartClient`. | T-RELEASE-005 and V-013 |
| B-1060 | Keep canonical declared room domains authoritative across archive ingress, allocation, portal traversal, and all second-hop consumers. The focused Air Base to Air Force One canary passed 25/25, then the source-frozen 17-mission campaign, live Credits, clean exit, and restart verification passed 23/23 on 2026-08-12. | V-010 and V-013 |
| B-1061, B-1062 | Keep Agent Profile v3 as the sole per-agent campaign and preference authority. Accept only complete v3 or exact v2 plus an optional validated INI migration, prepare before commit, preserve live state on rejection, refuse active deletion, and reject invalid campaign CLI relationships before arming. Final ordinary-client receipts are `results-20260813T030531Z.json`, `results-20260813T030952Z.json`, `results-20260813T031306Z.json`, and `results-20260813T032640Z.json`. | T-TESTS-002, V-010, and V-013 |
| B-1063 | Keep private generated caches under path-checked `$H/mod-cache`, with checked `$S` and `$B` fallbacks, one 128-bit filename key, and the full source SHA-256 in descriptors. The original nested save-root migration smoke passed 24/24 at `results-20260813T030531Z.json` with 594 `$H` cache references and no `$S`, `$B`, or fatal fallback. | T-RUNTIME-001, P-001, V-010, and V-013 |

---

## Post-1.0 bugs

| ID | Status | Route | Reason deferred |
|----|--------|-------|-----------------|
| B-223 | post_1.0 | UI presentation follow-up | Possible compounding of independent full-viewport dim layers has no current release repro. Promote only with a captured failure. |
| B-248-old | post_1.0 | T-MODDING-005 | The Grid free-fly observer gravity issue belongs to deferred Forge/editor work. |
| B-333 | post_1.0 | T-BENCHMARKING-006 | GPU swarm prediction, dedicated-authority alternatives, compression, active-prefix transfer, and reliability are benchmark extensions outside D-004. |

---

## Bug workflow

1. Capture the exact production symptom, build/source fingerprint, log, and
   minimal reproduction.
2. Add the B-number here immediately and link it to one Workbench milestone or
   validation owner.
3. Find the root cause and audit the bug class across sibling call sites. Add a
   systemic pattern to `systemic-bugs.md` when the class can recur.
4. Implement at the shared architectural boundary with explicit ownership,
   lifecycle, failure, and rollback behavior.
5. Run focused tests, the relevant full suite, and an ordinary-client or
   real-peer proof proportional to the defect.
6. Reject evidence if relevant source changed or an exclusive receipt overlapped.
7. On closure, update Workbench evidence/status and remove the row from Active
   1.0 defects during the next dated archive snapshot. Do not grow a second fixed
   history table in this live file.
