# Sprint Report - c126 Dispatch state-freshness hooks

> External enforcement layer for the Dispatch orchestrator's hard contract
> (every state-claim turn requires a same-turn state read). Companion to
> fixes #1 / #3 / #4 already encoded in auto-memory
> `feedback_dispatch_orchestrator_workflow.md`. This card implements fix #2.
>
> Worktree: `agitated-montalcini-a6f836` (branch `claude/agitated-montalcini-a6f836`)
> Pillar: Tooling
> Author: c126 worktree session, 2026-05-12

## Goal

Implement structural fix #2 of four (per Mike's 2026-05-12 directive): external
enforcement via Claude Code hooks that prevent the Dispatch orchestrator from
ending a turn that contains a state-claim (kanban / session-log / "currently
active" facts) without having read the relevant state in the same turn. The
orchestrator drifted off the in-memory contract four times in one day; this
card adds an OS-process layer the agent cannot bypass via inattention.

Deliverables per the orchestrator's brief:

- A. State-freshness logging script at `~/.claude/scripts/log_state_check.py`
- B. State-freshness check script at `~/.claude/scripts/check_state_freshness.py`
- C. Settings.json wiring at `~/.claude/settings.json` preserving existing
  permissions
- D. Documentation at `context/designs/dispatch-state-freshness-hooks.md`
- E. Verification via synthetic-stdin probes

## Shipped

| File | Change |
|------|--------|
| `C:\Users\mikeh\.claude\scripts\log_state_check.py` | NEW (154 lines). PostToolUse + Stop handler. Classifies Read of state paths, Grep / Glob / MCP session tools, Bash with state patterns -> action="state-check". Stop / StopFailure / PreCompact / SessionEnd -> action="turn-end". Appends one JSONL line to `~/.claude/dispatch-state-log.jsonl`. 5 MB rotation to `.1`. Always exits 0; never crashes. |
| `C:\Users\mikeh\.claude\scripts\check_state_freshness.py` | NEW (155 lines). Stop handler. Scans `last_assistant_message` for 16 state-claim regexes. If matched, walks log backwards from last turn-end (filtered by `session_id`) and confirms at least one state-check after that boundary. If absent, emits `{"decision":"block","reason":"..."}` JSON to stdout to block the Stop event and force a re-check. `stop_hook_active=true` short-circuits to allow. Fresh session short-circuits to allow. |
| `C:\Users\mikeh\.claude\settings.json` | +28 lines `hooks` block. PostToolUse matcher covers Read, Grep, Glob, Bash, and the MCP session-management tools. Stop matcher group has two handlers (logger first to mark turn-end, then checker). Existing `permissions` block preserved verbatim (Bash allow + 5 deny rules). |
| `C:\Users\mikeh\.claude\settings.json.pre-c126.bak` | NEW (backup of pre-merge settings). Recovery path: `Copy-Item ~/.claude/settings.json.pre-c126.bak ~/.claude/settings.json` to roll back if hooks misfire. |
| `context/designs/dispatch-state-freshness-hooks.md` | NEW (270 lines, SENTINEL-terminated). Architecture, hook payload schema, two-script logic, settings.json schema, testing procedure, recovery procedure, caveats and known gaps. Records the SendUserMessage->Stop adaptation and the Windows-path gotcha. |
| `.claude/scratch/probe-state-freshness-hooks.py` | NEW. 15-phase probe with isolated tempdir HOME so it doesn't touch the live log. All PASS. |
| `tools/kanban/state.json` | +c126 card (active column, pillar=tooling, order 160000, priority 2), then pending_completion block populated. |
| `context/session-log.md` | New session entry at top for `agitated-montalcini-a6f836`. |

Commit SHA: (filled in by the auto-merge step at session end - see Verification Notes)

## Decisions

| Decision | Rationale |
|----------|-----------|
| `Stop` hook over `PreToolUse` on `SendUserMessage` | No `SendUserMessage` tool exists in Claude Code. Assistant text output does not flow through any tool. `Stop` fires at end of every assistant turn with `last_assistant_message` available; supports blocking via JSON stdout. Equivalent semantics with cleaner implementation. Documented as deliberate adaptation. |
| Two scripts (logger + checker) not one combined | Separation of concerns: logger is pure data capture and runs on many tools; checker is policy and only runs on Stop. Easier to extend either independently. |
| Conservative state-claim regex set (16 patterns) | False-positive cost is one extra state read (cheap). False-negative cost is orchestrator drift (expensive). Tuned for low false-negative rate. Patterns cover card IDs (`\bc\d{2,4}\b`), kanban/session-log/sprint-report nouns, "currently active/working on", "we're on", `tasks.md`/`state.json` direct references, in-flight, dev branch/head, open cards/bugs, last session/commit/merge, project-status. |
| Forward slashes in JSON command paths | JSON `\\` becomes one backslash, but on Windows bash interpreters strip backslashes in unrecognized escape sequences (`\U`, `\m`, `\.`), corrupting the path. Caught live during impl: hook fired, Python could not find the script, agent saw a blocking PostToolUse error. Fix: `C:/Users/...` cross-platform safe. |
| `Bash` matched as a state-check candidate (with substring filter in script) | Many state checks happen via Bash (`git log`, `git status`, viewing kanban files via `cat`/`less`). Including Bash in the matcher and filtering in the script catches these without requiring matcher-side `if`-rules with permission-rule syntax that varies across Claude Code versions. |
| Hook registration at user-level `~/.claude/settings.json`, not project-local | User scope is correct for this enforcement: the Dispatch orchestrator can run on multiple projects. Caveat: applies to every Claude Code session on this machine, not just Dispatch. Design doc notes the project-local fallback if non-Dispatch sessions find the check noisy. |
| 5 MB log rotation to `.1` (single backup) | At ~200 bytes per entry, 5 MB holds ~25,000 events. Two generations is plenty for weeks of single-session work. Rotation is best-effort; failure is non-fatal (never crash the hook). |

## Blockers

None blocking. Two discoveries that adjusted the plan but did not stop work:

1. **`SendUserMessage` tool does not exist.** The orchestrator's PreToolUse matcher plan was based on a wrong assumption. Adapted to Stop hook with documented equivalence.
2. **Windows backslash-path bug.** JSON `\\Users\\...` paths got corrupted by the bash interpreter Claude Code uses for hook commands; symptom was a live PostToolUse blocking error mid-session. Fixed by using forward slashes (`C:/Users/...`). Documented in the design doc as a known gotcha.

## Follow-ups

- **Pattern tuning.** The 16 state-claim regexes are conservative on purpose. Run a few fresh Dispatch sessions and tune regexes if any false positives are observed (e.g., the `\bc\d{2,4}\b` pattern could match an IPv6 hex group or a chemistry compound name in some niche message; remove the regex or tighten word boundaries if it bites).
- **Telemetry / dashboard.** The log is JSONL; a quick `kanban_evaluator.py`-style CLI summarizer (`turns per day`, `block rate`, `most-blocked patterns`) would help Mike see whether the hook is providing value or noise.
- **Project-local scoping.** If non-Dispatch sessions (CLI, Cowork) find the check noisy, move hook registration from `~/.claude/settings.json` to a Dispatch entry-point project's `.claude/settings.json`.
- **Hook for tool-call diff verification.** Separate concept: a PreToolUse hook on Edit/Write that requires the modified file to exist in tool_response cache before the call. Defers to a separate card.

## Kanban Changes

- **c126** (Tooling): created in `active` column with `pending_completion` populated. `marked_by="claude-code-session-agitated-montalcini-a6f836"`. Three `evidence_refs`: design doc, probe, this sprint-report. Order 160000 (right after c125's 159000).

No other cards moved.

## Files Touched

| Path | Lines added | Lines removed | Purpose |
|------|-------------|---------------|---------|
| `C:\Users\mikeh\.claude\scripts\log_state_check.py` | 154 | 0 | NEW PostToolUse + Stop logger |
| `C:\Users\mikeh\.claude\scripts\check_state_freshness.py` | 155 | 0 | NEW Stop blocker |
| `C:\Users\mikeh\.claude\settings.json` | 28 | 1 | hooks block added; existing closing brace replaced |
| `C:\Users\mikeh\.claude\settings.json.pre-c126.bak` | 6 | 0 | NEW backup of pre-merge settings |
| `context/designs/dispatch-state-freshness-hooks.md` | 270 | 0 | NEW design doc |
| `.claude/scratch/probe-state-freshness-hooks.py` | ~220 | 0 | NEW 15-phase probe |
| `tools/kanban/state.json` | ~25 | 0 | NEW c126 card + pending_completion |
| `context/session-log.md` | ~60 | 0 | NEW session block |
| `.claude/sprint-reports/sprint-c126-hooks.md` | this file | 0 | NEW sprint report |

## Verification Notes (for orchestrator)

**Probe verification:**
- Run `python .claude/scratch/probe-state-freshness-hooks.py` and confirm `ALL PROBES PASS (15 phases)` on stdout. The probe uses an isolated tempdir HOME so the real log is not touched.

**Settings verification:**
- `python -c "import json; s=json.load(open('settings.json')); print('hooks events:', list(s.get('hooks', {}).keys()))"` (from `~/.claude/`) should print `['PostToolUse', 'Stop']` and `permissions preserved: True`.
- Backup at `~/.claude/settings.json.pre-c126.bak` must exist.

**Live hook verification (next Dispatch session, not this one):**
- Start a fresh Dispatch session.
- Have the session make a state claim without reading state: it should be blocked.
- Confirm `~/.claude/dispatch-state-log.jsonl` exists and contains alternating state-check + turn-end entries.
- The hook is live in the CURRENT session as well (user-level settings applies immediately); the orchestrator confirmed this mid-impl when a Bash command containing `kanban/state.json` got logged.

**Kanban verification:**
- `tools/kanban/state.json` -> c126 in `active` with `pending_completion` populated.
- `pending_completion.marked_by` matches the worktree session name.
- Three `evidence_refs` resolvable.

**Build verification:**
- No game-code changes. Build verify is a no-op for this card.

**Git verification:**
- Commits on branch `claude/agitated-montalcini-a6f836` should be tagged `Tooling - c126: <summary>` with body + Refs line per the c125 commit-message standard.
- Auto-merge to `dev` per the standing rule (sequentially, dry-run + line-count verify).

**Memory cross-reference:**
- `feedback_dispatch_orchestrator_workflow.md` (auto-memory) is the in-memory companion. This sprint completes structural fix #2 of the four-fix plan documented there.

## Adaptation note (for the orchestrator's verification)

If the orchestrator reads this report and notices the deliverable description in the brief said "PreToolUse on SendUserMessage" but the implementation uses "Stop hook" - that is intentional and documented. The brief's caveat ("If hooks are not actually supported in Dispatch the way research suggested, document the finding and propose alternative") authorized this adaptation. The `SendUserMessage` tool does not exist in Claude Code; the `Stop` event is the right primitive for end-of-turn enforcement and is fully supported.

<!-- SENTINEL: end of sprint-c126-hooks.md -->
