# 2026-04-11 — scratch archive

Scratch artifacts from the 2026-04-11 session wave (D5 Phase 3 completion,
Opus 1M playtest-fixes batch, and early updater-parse investigation). All
outcomes captured in living context; these files retained for forensic
continuity.

## D5 Phase 3 — Full menu replacement (COMPLETE)

Phase 3 closed with all 254 dialogs ported to ImGui (see
`../../../infrastructure.md` → D5). Per-batch breakdowns:

| File | Batch | Content |
|---|---|---|
| `D5-P3-batch3-2026-04-11.md` | 3 | Sound Mode dropdown / CI Options redirect verify (S201) |
| `D5-P3-batch4-2026-04-11.md` | 4 | Match setup dialogs |
| `D5-P3-batch5-2026-04-11.md` | 5 | Agent select variants |
| `D5-P3-batch6-2026-04-11.md` | 6 | Scorecard overlays |
| `D5-P3-batch7-2026-04-11.md` | 7 | Typed dialogs (Danger/Success) |
| `D5-P3-batch8-2026-04-11.md` | 8 | Multi-player extras |
| `D5-P3-batch10-2026-04-11.md` | 10 | (no batch 9 — merged into 10) |
| `D5-P3-batch11-2026-04-11.md` | 11 | Debug / sandbox |
| `D5-P3-batch12-2026-04-11.md` | 12 | Final pass / misc overlays |

## Bug fixes shipped

| File | Bug | Living-file reference |
|---|---|---|
| `B-78-2026-04-11.md` | Chat rebroadcast DoS amplification (`CHAT_MSG_MAX_LEN 255`) | `../../../bugs.md` → B-78 Fixed |
| `B-81-2026-04-11.md` | JSON tokenizer unbounded recursion (`S_MAX_DEPTH 64`) | `../../../bugs.md` → B-81 Fixed |
| `B6-head-preview-2026-04-11.md` | Head preview forensic (closed via subsequent charpreview work) | — |
| `playtest-fixes-2026-04-11.md` | Opus 1M playtest batch — B-130/B-131/B-132 + 10-bug sweep (commit `cb6f4763` era) | `../../../bugs.md` → B-130/131/132 Open→Fixed |

## Investigation / pre-work

| File | Topic | Outcome |
|---|---|---|
| `dispatch-briefing-2026-04-11.md` | Cowork dispatch briefing at session start | Snapshot, superseded |
| `updater-parse-diagnosis-2026-04-11.md` | GitHub API parse-failure instrumentation (D13/B-99) | Root cause (rate-limit 403) diagnosed and FIXED in S245 FIX-F (commit `205c74a7`) |
