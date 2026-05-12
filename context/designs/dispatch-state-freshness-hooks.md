# Dispatch state-freshness hooks (c126)

> External enforcement layer for the Dispatch orchestrator's hard contract: every turn that claims kanban / session / state-of-the-world status must be backed by a same-turn read of the relevant state file.

Companion to the in-memory enforcement encoded in `feedback_dispatch_orchestrator_workflow.md` (auto-memory). The orchestrator drifted off that contract four times in one day even after writing it down. This document specifies the **external** layer: Claude Code hooks that block a turn from completing if it claims state without reading state.

---

## Why hooks, not memory alone

Memory entries are advisory: the orchestrator reads them at session start, but can drift over the course of a long conversation. The Stop hook fires unconditionally at the end of every assistant turn and can block the turn from completing - bypassing the orchestrator's own attention budget.

The four-fix structural plan (Mike's directive 2026-05-12):
1. Hard contract in auto-memory (encoded - feedback_dispatch_orchestrator_workflow.md)
2. **External enforcement via Claude Code hooks (this document)**
3. No state claims without same-turn verification (sub-rule of 1, in memory)
4. Reduce surface for state-claiming - defer to checking (behavioral norm, in memory)

Fixes 1, 3, 4 rely on the agent following its own rules. Fix 2 makes the rule unbypassable: the hook process is a separate OS process, not part of the agent.

---

## What I discovered while implementing

The orchestrator's plan assumed a tool named **SendUserMessage** with `PreToolUse` matchability. **No such tool exists in Claude Code.** Assistant text output (chat messages) does not go through any tool at all - it flows directly from model output to terminal.

The right Claude Code primitive is the **`Stop` event**, which fires at the end of every assistant turn and exposes:
- `last_assistant_message`: the final response text the agent emitted
- `stop_hook_active`: true if this Stop was already blocked once this turn (loop guard)
- `session_id`, `transcript_path`, `cwd`

`Stop` handlers can block the turn from completing by printing JSON to stdout:
```json
{"decision": "block", "reason": "..."}
```
Claude sees the reason as a system message and continues working. After Stop is blocked once, `stop_hook_active=true` on the next Stop call to prevent infinite loops.

This is a deliberate adaptation from the orchestrator's brief; semantics are equivalent (block the turn-end if claim-without-check) and the implementation is cleaner because there is exactly one "end of turn" event to hook.

---

## Architecture

Two scripts plus settings wiring:

```
~/.claude/
  scripts/
    log_state_check.py        # PostToolUse + Stop handler (logger)
    check_state_freshness.py  # Stop handler (blocker)
  dispatch-state-log.jsonl    # append-only event log, 5 MB rotation -> .1
  settings.json               # hooks key registers both scripts
```

### log_state_check.py (PostToolUse + Stop)

Fires on every PostToolUse for an allowlist of tools (Read, Grep, Glob, Bash, MCP session/directory tools) and on every Stop / StopFailure / PreCompact / SessionEnd.

Classification:
- **Read of state paths** -> action="state-check"
  - Path substrings: `tools/kanban/`, `tools/bugs/`, `.claude/sprint-reports`, `.claude/scratch/probe`, `tools/parked_evaluator.py`, `tools/kanban_evaluator.py`, `context/session-log.md`, `context/tasks.md`, `context/bugs.md`
- **Grep / Glob / MCP session tools** -> action="state-check" (always; broad exploration counts)
- **Bash with state patterns** -> action="state-check"
  - Substrings: `git log`, `git status`, `git show`, `git diff HEAD`, `kanban/state.json`, `sprint-reports`, `tasks.md`, `session-log.md`, `bugs.md`
- **Stop / StopFailure / PreCompact / SessionEnd** -> action="turn-end"
- Anything else -> not logged

Output: one JSONL line per qualifying event:
```json
{"timestamp":"2026-05-12T19:35:49Z","event":"PostToolUse","tool":"Read","action":"state-check","session_id":"...","cwd":"...","path":"tools/kanban/state.json"}
{"timestamp":"2026-05-12T19:36:01Z","event":"Stop","tool":"","action":"turn-end","session_id":"...","cwd":"..."}
```

Always exits 0. Failures (bad JSON in stdin, file-system errors) are swallowed - the hook must never break the agent.

Log rotation: if size exceeds 5 MB, rename to `.1` (replacing any prior `.1`). Best-effort; failure is non-fatal.

### check_state_freshness.py (Stop only)

Fires on every Stop. Logic:

```
payload = json.load(stdin)

if payload.stop_hook_active:
    allow()  # never loop

last_msg = payload.last_assistant_message
if not last_msg:
    allow()

matched_pattern = scan_state_claim(last_msg)
if matched_pattern is None:
    allow()

entries = read_jsonl_log()
walk entries backwards to find most recent turn-end with matching session_id
if no prior turn-end for this session:
    allow()  # fresh session
walk forward from that turn-end:
    if any state-check entry exists:
        allow()
deny(reason="state claim detected, no state-check in this turn")
```

State-claim patterns (case-insensitive regex, conservative on purpose):
- `\bc\d{2,4}\b` - card IDs (c126, c12, c3738)
- `\bkanban\b`
- `\bsession[- ]log\b`
- `\bsprint[- ]report\b`
- `\bactive (card|task|work|state|session|fronts?)\b`
- `\bcurrent (card|task|work|state|session|status)\b`
- `\bcurrently (working on|active|in progress|blocked)\b`
- `\b(we're|we are|you're|you are) (on|working on|in)\b`
- `\bpending[ _-]completion\b`
- `\btasks\.md\b`
- `\bstate\.json\b`
- `\bin[- ]flight\b`
- `\bdev (branch|head)\b`
- `\bopen (card|cards|task|tasks|work|bug|bugs)\b`
- `\blast (session|commit|merge|sprint)\b`
- `\bproject[- ]status\b`

False-positive cost: one extra state read (cheap). False-negative cost: orchestrator drift (expensive). Tuned for low false-negative rate.

Block reason text (printed in deny JSON) explicitly lists the recovery actions: read kanban/state.json, session-log.md, tasks.md, sprint-reports/, or call list_sessions / search_session_transcripts.

---

## Hook payload schema (reference)

Common fields on every event:
- `session_id` (string)
- `transcript_path` (string)
- `cwd` (string)
- `permission_mode` (string)
- `hook_event_name` (string)

PostToolUse adds: `tool_name`, `tool_input`, `tool_response`, `tool_use_id`, `duration_ms`.

Stop adds: `stop_hook_active` (bool), `last_assistant_message` (string).

---

## settings.json wiring

User-level `~/.claude/settings.json`. Existing `permissions` block is preserved verbatim.

```json
{
  "permissions": { ... preserved ... },
  "hooks": {
    "PostToolUse": [
      {
        "matcher": "Read|Grep|Glob|Bash|mcp__ccd_session_mgmt__list_sessions|mcp__ccd_session_mgmt__search_session_transcripts|mcp__ccd_session_mgmt__archive_session|mcp__ccd_directory__request_directory|mcp__ccd_session__mark_chapter",
        "hooks": [
          { "type": "command", "command": "python C:/Users/mikeh/.claude/scripts/log_state_check.py" }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          { "type": "command", "command": "python C:/Users/mikeh/.claude/scripts/log_state_check.py" },
          { "type": "command", "command": "python C:/Users/mikeh/.claude/scripts/check_state_freshness.py" }
        ]
      }
    ]
  }
}
```

**Matcher syntax notes:**
- The matcher is **tool-name-only**. `|`-separated list of literal tool names works because the regex engine only kicks in for non-alphanumeric/underscore characters.
- We do **not** filter by `tool_input.file_path` in the matcher itself - the logger does that internally. Filtering in matcher would require `if`-rules with permission-rule syntax which is less portable.

**Stop event has two handlers** in one matcher group: the logger marks the turn-end, then the checker decides allow/deny. Order matters: log first so the next turn sees the boundary.

**Windows path gotcha (encountered during impl):** JSON `\\` becomes one backslash in the command. On Windows, bash interpreters strip backslashes in unrecognized escape sequences (`\U`, `\m`, `\.`), corrupting the script path. **Use forward slashes** in command paths: `C:/Users/...` not `C:\\Users\\...`. The error mode is silent: hook fires, script "not found", PostToolUse returns a blocking error that surfaces to the agent. Forward slashes work on both Windows and POSIX Python invocations.

---

## Testing procedure

Run the probe script after any change to either handler:

```powershell
python .claude/scratch/probe-state-freshness-hooks.py
```

The probe spins up an isolated `HOME` (and `USERPROFILE` for Windows) so it does not touch `~/.claude/dispatch-state-log.jsonl`. 15 phases cover:

1-2. Empty payload + non-state Read -> no log entry
3-4. State Read + Grep -> logged as state-check
5. Stop event -> logged as turn-end
6. No state claim in message -> checker allows
7. State claim with no prior state-check -> checker denies with proper JSON
8. State check then state claim -> checker allows
9. stop_hook_active=true -> always allow (loop guard)
10. Fresh session (no prior turn-end) -> allow
11-12. Malformed JSON stdin -> exit 0 (no crash)
13. Bash `git log` -> state-check with bash_excerpt
14-15. State claim phrases "kanban" and "c126" -> deny

Live-session verification: after merging this design + scripts, start a fresh Dispatch session and have it make a state claim without reading state. Expected: the Stop event blocks, Claude re-reads state, then completes the turn. Verify by inspecting `~/.claude/dispatch-state-log.jsonl` for the turn-end + state-check pattern.

---

## Recovery procedure

If hooks misfire and block legitimate work:

**Temporary disable (preserves config):**
```powershell
Copy-Item ~/.claude/settings.json ~/.claude/settings.json.bak
# Edit settings.json: rename "hooks" -> "_hooks_disabled" (any non-recognized key)
```
Or use the pre-c126 backup:
```powershell
Copy-Item ~/.claude/settings.json.pre-c126.bak ~/.claude/settings.json
```
(The pre-c126 backup is written before the merge in this card; preserve it.)

**Permanent fix:** if a real false-positive pattern is found, edit `STATE_CLAIM_PATTERNS` in `check_state_freshness.py` to remove or tighten the offending regex, then run the probe.

**Debugging:** every blocked turn writes its deny JSON to the agent's transcript. Read `transcript_path` from the payload, search for `"decision": "block"`. The `reason` field shows which pattern matched.

---

## Caveats and known gaps

- **Heuristic detection.** State-claim patterns are regex - they cannot tell intent. A message that *quotes* a state claim ("the user asked: what's the active card?") will trigger the check. False-positive cost is one extra state read; acceptable.

- **Cross-session contamination.** The log is keyed by session_id. If two Dispatch sessions run concurrently, their turn-end markers interleave in the same file but the walk-back logic filters by session_id. Safe but adds log noise.

- **Log growth.** 5 MB rotation keeps two generations (live + .1). At ~200 bytes/entry that is ~25,000 entries per generation, plenty for weeks of single-session work. Adjust `ROTATE_BYTES` if needed.

- **Hook not session-local.** `~/.claude/settings.json` applies to **every** Claude Code session, not just Dispatch. Other sessions on the same machine will also be subject to the freshness check. If this becomes noisy for non-Dispatch work, move the hook registration to a Dispatch-specific `.claude/settings.json` at the Dispatch entry point's working directory (project-local scope).

- **PreCompact treated as turn-end.** When the conversation is compacted mid-turn, the prior `last_assistant_message` may not reflect the post-compaction agent's state. Treating PreCompact as a boundary means a state claim AFTER compaction needs a fresh state read - which is the correct behavior even though the boundary is artificial.

- **No equivalent for streaming.** Stop fires on turn completion, not on each streamed chunk. A long turn that makes a state claim, runs many tools, then makes another state claim will be evaluated as one unit on Stop.

---

## Files touched (c126)

| Path | Purpose |
|------|---------|
| `~/.claude/scripts/log_state_check.py` | PostToolUse + Stop logger |
| `~/.claude/scripts/check_state_freshness.py` | Stop blocker |
| `~/.claude/settings.json` | Hooks registration (permissions preserved) |
| `~/.claude/settings.json.pre-c126.bak` | Backup of pre-merge settings |
| `~/.claude/dispatch-state-log.jsonl` | Append-only event log (runtime) |
| `context/designs/dispatch-state-freshness-hooks.md` | This document |
| `.claude/scratch/probe-state-freshness-hooks.py` | 15-phase probe |
| `tools/kanban/state.json` | c126 card entry, tooling pillar |

Verified: probe passes all 15 phases; settings.json validates as JSON; permissions block preserved; logger writes to live log on PostToolUse Bash; backslash-path bug fixed (forward slashes only).

---

## Where to look

- For the auto-memory contract this hook backstops: `~/.claude/projects/.../memory/feedback_dispatch_orchestrator_workflow.md`
- For the original Mike directive (four structural fixes): session prompt for c126
- For Claude Code hooks spec: https://code.claude.com/docs/en/hooks
- For the kanban card and its notes: `tools/kanban/state.json#c126`

<!-- SENTINEL: end of dispatch-state-freshness-hooks.md (c126) -->
