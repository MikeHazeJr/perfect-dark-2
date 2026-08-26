# T-ENGINE-004 closure evidence - 2026-08-26

## Result

T-ENGINE-004 is validated on canonical `dev` at commit
`8b254877778d635014c50d1904a06aad25c4248e`. The finite closure contract in
`context/designs/in-flight/player-init-architectural-fixes.md` section 9.10 is
complete. Every one-off bug routed to T-ENGINE-004 is a retained regression
gate; no confirmed or repro-required T-ENGINE-004 bug remains.

This is an evidence-only promotion. No product, verifier, fixture, or test
source changed after the accepted B-1096/B-1097 production unit, so no build,
suite, or gameplay receipt was repeated.

## Closure matrix

| Required gate | Accepted evidence | Verdict |
|---|---|---|
| Focused player-init and lifecycle contracts plus full `pd-tests` | Current frozen product `1C184C57...` (2,734 files), verifier `E0FA8E6A...` (410 files), client `8262681E...`, and tests `A9B1ADAA...`; focused B-1096/B-1097/B-1088 passes 718/20, complete suite passes 66,960/1,209, native-source guard passes, and both manifests have zero differences | Pass |
| Ordinary Campaign plus all 17 missions/restart | In `results-20260824T021919Z.json`, `auto_campaign_release` passes 23/23 with exit 0 and no assertion failure. The rejected Combat Simulator row in that mixed receipt is not counted | Pass |
| Air Base to Air Force One transition canary | In `results-20260824T021919Z.json`, `auto_campaign_airbase_transition_canary` passes 25/25 with exit 0. The independent rejected row is not counted | Pass |
| Ordinary Combat Simulator start, fire, end, return, and repeat start | `results-20260826T024226Z.json` passes 56/56 with both match cycles, real CMP150 fire/hit, stats/awards, Play Again, clean exit, and no leaked process | Pass |
| Listen authority and ordinary client match starts plus failure rollback | Accepted rows in `results-20260826T075433Z.json` pass initiator authority 214/214, co-op 96/96, Counter-Op 98/98, later-player rollback 60/60, and settings rollback 43/43. Rejected invitee/reconnect rows in that mixed receipt are not counted | Pass |
| Both D-003 authority-first friend-play roles | Initiator authority passes 214/214 above. Focus-independent invitee authority passes corrected retained-log evaluation 170/170 in `.claude/source-freeze/v1m1b1104v58/invitee-route-retained-log-revalidation.json`; the immutable raw 169/170 receipt remains rejected only for its stale two-digit epoch regex | Pass |
| Two-process disconnect/reconnect preserving identity, room, score, credential, body/head, chr/prop, world, and inventory | `results-20260826T121959Z.json` passes 116/116 on exact client `8262681E...`: one production weapon retirement plus read-only absence, one endpoint-scoped retry, snapshot `live_side_effects=0`, exclusive prop topology, exact world and both inventories, one commit, 30 real Cyclone shots accepted by the authority, final credential retirement, exit 0, zero failures, zero operational failures, and zero leaked process | Pass |
| V-009 visual regression gate | Workbench V-009 is validated/pass. `context/evidence/2026-08-12-v009-needler-visual-readability.md` retains the inspected unobstructed held-model and distinct pink/cyan effect proof. No new visual capture was made in this closure audit | Pass |

## Rejected evidence discipline

Mixed result files are not treated as aggregate passes. Only their individually
passing named rows are used above. The stale `pd-tests.exe` receipt preceding
the accepted 718/20 and 66,960/1,209 batch remains rejected. Historical
reconnect, parser-boundary, focus, and crash receipts remain immutable diagnosis
evidence and are not substituted for the accepted replacement gates.

## Route boundary

B-1105 is a real base Mauler public-model activation defect, but canonical
Workbench and `context/bugs.md` route it to V-010, not T-ENGINE-004. Its path
through catalog model conversion, gun-load retry, equip, fire, and rendering is
the next Milestone 1 implementation unit. B-183 and B-1086 also remain V-010
visual/source defects. They do not keep the completed player-init/lifecycle
dependency falsely partial.

## Milestone effect

Promoting T-ENGINE-004 changes Milestone 1 from 2 validated, 8 partial, and 5
missing leaves to 3 validated, 7 partial, and 5 missing leaves: 20 percent
production-validated. V-010 and T-RELEASE-002 remain partial until all other
explicit base-game and graph dependencies close.
