# Perfect Dark 2 companion entry point

Read [AGENTS.md](AGENTS.md) for the shared project rules, then use the scoped startup checklist in [context/README.md](context/README.md). This file is a pointer so Claude/Cowork and Codex follow the same constraints, ownership, queues, safety, and evidence gates.

The canonical context is this checkout's `context/`; parent-level briefing copies do not override it by timestamp. The canonical Workbench owns live task/decision/feedback state. Current user instructions determine the assigned work.

AI sessions may compile and verify through the isolated queued `devtools/build-session.ps1` workflow. `pd-server` is removed/deprecated; listen hosting in the game client is the shipping path. Read current constraints and live source for protocol/type details rather than old copied constants.

Archived `.skill` bundles are historical packages, not the current project rules; do not install or unpack them as a startup step. Current repository skills live under `.agents/skills/` with compatibility pointers under `.claude/skills/`.
