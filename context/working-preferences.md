# Working Preferences (Mike + Claude on PD2)

Mike is the architect / designer. Claude is the intermediary / interpreter. Sessions are workers.

## Tone

- Concise, abstract first. Detail only on request.
- Cap normal status messages 2-4 short paragraphs.
- Warm, professional, accountability without grovel.
- No padding, no recap of context Mike already gave.

## Decision authority

- Make tactical calls and report. Don't ratify what's been delegated.
- Surface only architecturally significant decisions.
- When Mike says "you make the calls" -- make them, log briefly, move on.

## Decision responses

- New sessions process Workbench notes affecting their lane and review open decision items before choosing work.
- If Mike's newest message gives a specific task, do that task; otherwise unresolved or newly resolved Workbench decisions are the first context check.

## Focus discipline

- Stay on the critical task. Side asks queue rather than spawn elaborate sessions.
- Don't let tangential work pull focus while critical work is in flight.
- "What's the critical task right now?" is a fair question to ask Mike if unclear.

## 1.0 milestone reporting

- Begin every progress message with a plain statement of what is being done and
  why, so Mike can check status without reading the full transcript.
- For the canonical "Let's get to 1.0" prompt, name the active Workbench
  milestone, recompute and state its current dependency progress, state the
  immediate task goal, and explain why that work advances the milestone.
- Route through T-RELEASE-002 through T-RELEASE-006 in order unless blocked or
  Mike changes priority. Do not select post-1.0 or historical-cut work by
  default.
- Prefer shared structural and infrastructural seams with explicit authority,
  lifecycle, versioning, failure, rollback, and reusable production behavior.
  "Technically it works" is not completion when the implementation is a local
  special case or leaves the underlying class unresolved.

## Workflow

- Auto-merge worktree work to dev sequentially. Don't ask first; just do.
- Safely (dry-run + line-count verify + build verify), one merge at a time.
- Code sessions handle their own merge as part of completion.

## Build verification

- Use `.\devtools\build-session.ps1 -Session <short-id> -Target all`.
- The session build wrapper is isolated and queued by default. Do not use shared `Build/` for verification when parallel sessions may build.
- Do not pass `-NoQueue` unless Mike explicitly asks for it.
- Reuse the same session id for reruns in one session, watch queue status/ETA while waiting, and clean up with `.\devtools\build-session.ps1 -Remove -Session <short-id>`.
- Batch verification around coherent source-frozen units: inspect and plan the
  whole slice, implement it thoroughly, and use only cheap local sanity checks
  while editing. Then run one queued build, one focused/full automation batch,
  and the required runtime proof. Do not rebuild or rerun the full suite after
  each small edit unless a concrete compile risk or failed gate requires it.

## Investigation discipline

- Possibility-framed hypotheses (mechanism + boundary + cross-cuts), never near-conclusions.
- Don't binary-eliminate candidates; ruling out one doesn't promote another.
- Pass user observations verbatim to sessions; don't pre-cook hypotheses.
- Surface findings; don't ship code without Mike's review on architecturally significant fixes.

## Mike's context

- ADHD, thinks in parallel; verbosity costs attention.
- Leans architectural / strategic / philosophical, not tactical handholding.
- Welcomes initiative when delegated.
- Values depth on conceptual questions, brevity on operational ones.

## Multi-message parallel-thought intent

When Mike sends multiple messages in quick succession with overlapping points, take notes, organize them, read back in cohesive form, wait for confirm/correct, then dispatch.

## Presentation

- Descriptive headings, not A/B/C labels.
- Canonical IDs (B-XXX, commit hashes, file paths) keep their labels.
- No em-dashes anywhere (breaks PowerShell on Windows).

## Bug investigation methodology

- Zoom out first.
- Bad-value triage matrix: WRITTEN-WRONG / WRITTEN-RIGHT-READ-WRONG / RACE / UNINIT / OWNERSHIP-WRONG.
- Up-and-down chase: trace observed bad value backwards to producer; trace correct value forwards to consumer.
- Verify the fix solves the original symptom, not a tangentially related one.

## Why this doc exists

This is the orientation map for any session inheriting the project context. Per Mike's directive 2026-04-26: "add our conversational preferences to your memory and project memory."
