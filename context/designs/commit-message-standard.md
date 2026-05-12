# Commit Message Standard

> Pillar: Tooling. Status: SPEC v1.0 SHIPPED (2026-05-11). Design owner: Mike. Author: Claude (sad-rosalind).

The PD2 git history is the project's narrative. Commit messages anchor each change to a kanban card and to any bug or parked thread the change resolves, so a reader six months from now can reconstruct what shipped and why without scraping branch names or chat logs.

Mike called out on 2026-05-11 that recent commits had drifted to generic boilerplate (`chore: pre-release commit v0.0.X`, `WIP: tooling close-out`, `tools: ...`, `docs: ...`). The fix is a structural format plus two enforcement layers: a commit-msg hook that prevents bad messages at write time, and the daily-flow orchestrator's commit-hygiene audit step that catches anything that slips past.

---

## Format

```
<Pillar> - <CardID>: <one-line summary, imperative present tense, <= 72 chars>

<paragraph of 2-4 sentences explaining what changed and why>

<optional: bullet list for multi-scope commits>

Refs: cNNN [, B-NNN] [, pt-NNN]
```

**Subject line.**
- `<Pillar>` is a pillar `name` or `id` from `tools/kanban/state.json`, case-insensitive (`Tooling`, `tooling`, and `Mod Infrastructure` are all valid).
- `<CardID>` is `cNNN`, anchoring to a card that exists in `tools/kanban/state.json`. The card's `pillar` field must match the subject's pillar.
- Summary is imperative present tense (`Add`, `Fix`, `Refactor`, not `Added` or `Adds`) and at most 72 characters for the entire subject line.

**Body.**
- One or more paragraphs explaining what changed and why. The audience is the version of you who comes back to this commit six months later with no other context.
- For multi-scope commits, use a bullet list under the prose paragraph.

**Refs trailer.**
- A line of the form `Refs: cNNN[, B-NNN][, pt-NNN]`. Must include the subject's CardID. List any bugs (B-NNN) or parked threads (pt-NNN) the commit closes or partially resolves.
- Multiple `Refs:` lines are tolerated; the validator reads the last one. Prefer one combined line.

---

## Examples

### Feature commit

```
Tooling - c120: Add commit-msg hook enforcing pillar/card prefix

Recent commits drifted to generic prefixes (chore:, tools:, docs:) that lost
the connection to the kanban card driving the work. This adds a Python
commit-msg hook that validates subject format, pillar registry membership,
card existence in tools/kanban/state.json, and a Refs trailer. The hook is
installed by tools/install-githooks.ps1 which also self-tests rejection and
acceptance paths.

Refs: c120
```

### Bug fix

```
Engine - c073: Fix sliding-collision overshoot on slope clip

bgun.c was reusing the pre-collision velocity vector after the resolve step,
causing the player to skate past corners when the wall normal was close to
the floor plane. The fix uses the post-resolve velocity for the trailing
movement pass and clamps the residual to the collision plane.

Repro: solo, dataDyne corridor near the second guard, walk into the inside
corner at slight angle.

Refs: c073, B-318
```

### Multi-scope commit

```
Tooling - c050: Refactor extract pipeline + retire base/

base/ has been deprecated since Phase 3 of catalog universality but the
extract pipeline still wrote into it as a fallback. This pulls the base/
write path out and routes everything through data/<romid>/files/, plus
splits the extract entry point so callers can target a specific catalog
slice without re-running the full pipeline.

- tools/extract/extract.c: drop base/ fallback writes
- tools/extract/cataloger.c: add per-slice entry point
- context/pillars/catalog.md: mark base/ retired with date

Refs: c050, c082
```

### Merge commit (exception, bypasses validation)

```
Merge claude/sad-rosalind-84b87e: Commit message standard + hook
```

### Parked-thread resumption

```
Modding - c068: Resume palette previewer thread (pt-014)

Picking up the palette previewer work that was parked 2026-04-22 pending the
mod manager UI shell. With the manager shell now landed in c057, the
previewer slots in cleanly as a sub-panel. This commit ports the original
prototype to the new shell API.

Refs: c068, pt-014
```

---

## Enforcement

Two layers, matching the prevention plus detection pattern from the
daily-flow orchestrator spec.

### Layer 1 - commit-msg hook (prevention)

`.githooks/commit-msg` is a POSIX shell shim that delegates to
`.githooks/commit-msg.py`. The Python validator reads
`tools/kanban/state.json` and rejects commits that fail any of:

- Subject does not match `<Pillar> - <CardID>: <summary>`.
- Pillar is not registered in state.json (case-insensitive id or name).
- CardID does not exist in state.json.
- Subject pillar does not match the card's pillar field.
- Subject exceeds 72 characters.
- Subject is a bare placeholder (`wip`, `fix`, `update`, `test`, `chore`, `stuff`, `misc`, `tmp`, `temp`, `todo`).
- Body is empty.
- Body has no `Refs:` trailer.
- `Refs:` trailer does not include the subject's CardID.

Bypassed automatically (no validation) for:

- Merge commits (`Merge ...`) - git auto-generated.
- Revert commits (`Revert "..."`) - git auto-generated.
- Rebase autosquash markers (`fixup!`, `squash!`, `amend!`).

Install once per checkout:

```powershell
pwsh tools/install-githooks.ps1
```

The install script sets `core.hooksPath = .githooks` (local config, covers all worktrees of this repo) and runs four probes against the validator: reject `wip`, reject `chore: random update`, reject missing Refs, accept a well-formed message. The script makes no real commits.

Emergency bypass when explicitly authorized:

```powershell
git commit --no-verify -m "..."
```

### Layer 2 - daily-flow orchestrator audit (detection)

The Phase 2A commit-hygiene audit step (spec at `context/designs/daily-flow-orchestrator.md`) reads the previous day's `claude/*` and `dev` commits, runs the same format check the hook applies, and flags any that:

- Slipped past the hook via `--no-verify`.
- Pre-date the hook install (grandfathered, reported informationally).
- Have a Refs trailer but reference a card whose `column` was never `active` or `done` in the audit window (potential bookkeeping miss).

Findings land in `tools/daily_flow/state/commit-hygiene-YYYY-MM-DD.json` and surface in the morning briefing.

---

## Anti-patterns

| Bad | Why | Fix |
| --- | --- | --- |
| `chore: pre-release commit v0.0.195` | No pillar, no card. The version bump is a side effect, not the subject. | Tie the commit to its driving card. If it is a pure release-prep commit, anchor to the tooling card that owns the release script. |
| `tools: Parked panel becomes ...` | Uses an informal `tools:` prefix, no card anchor, mixes scope verbs. | `Tooling - cNNN: Make parked panel collapsible right sidebar` |
| `WIP` | Bare placeholder. No reader can reconstruct what is in progress. | `Tooling - cNNN: WIP scaffolding for commit-msg hook` |
| `fix bug` | Tells the reader nothing. | Name the bug, the file, the symptom. `Engine - cNNN: Fix off-by-one in challengeLoadConfig stride (B-323)` |
| `update files` | Worst case. | Same as above. Or do not commit yet - if you cannot describe what changed, you are not done. |
| Subject `Tooling - c120: ...` with body `Refs: c200` (wrong card) | Refs trailer disagreeing with the subject. The hook catches this. | Decide which card actually drove the work; reference that card in both places. |

---

## Migration

Existing commits stay as-is. The standard applies to all NEW commits authored after the hook is installed. No history rewrites.

The daily-flow audit reports pre-standard commits informationally so they
do not look like regressions in the hygiene report.

---

## Related

- Memory entry (canonical brief): `feedback_commit_message_standard.md` in the user's auto-memory.
- Daily-flow orchestrator (Phase 2A audit step): `context/designs/daily-flow-orchestrator.md`.
- Kanban state of record: `tools/kanban/state.json`.
- Bug tracker: `tools/bugs/state.json`.
- Parked threads: `tools/kanban/parked.json`.
