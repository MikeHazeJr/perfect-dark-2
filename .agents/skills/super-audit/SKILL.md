---
name: super-audit
description: **Perfect Dark 2 Super Audit** — Mike's deep static-analysis skill for the Perfect Dark 2 fork. Invoke by saying "Super Audit", "run our Super Audit", "run the Super Audit skill", or "kick off a Super Audit" — this is the canonical invocation name. Also triggers on "audit PD2", "audit the code", "review the project", "review the netplay", "review the menus", "review the catalog", "review the Grid", "review the dedicated server", "check the codebase", "find issues", "find inherited bugs", "lineage check", "catalog audit", "mod parity review", "scaling review", "security review", "layout sweep", "type-conversion sweep", "struct drift check", "data-layout audit", "deep review", or Cowork Dispatch child handoffs carrying an audit subtask — even when the word "audit" isn't used. Perfect Dark 2 is a fan re-engineering of Rare's Perfect Dark built on the n64decomp / fgsfdsfgs (port-net) / jonaeru (allinone) lineage with decentralized P2P multiplayer, a Catalog-as-single-source-of-truth asset system, a modding pipeline, The Grid social layer, client online mode, and a game-agnostic dedicated server. The skill produces a findings-only report in a fixed structure and never writes implementation code, refactors, or modifies the project.
---

# Perfect Dark 2 Super Audit

A structured, opinionated auditor for the Perfect Dark 2 fork. Produces a detailed findings report — never implementation code, never rewrites.

## What this skill does

- Reads project context (via `context/`, if present)
- Loads the full audit prompt from `references/audit-prompt.md`
- Performs a three-dimensional audit (Design Fit / Code Quality / Security)
- Rates findings by severity, confidence, and upstream lineage
- Flags scaling ceilings, inherited defects, Catalog SOT violations, and mod/native asymmetry as first-class findings
- Writes the report to `context/audits/YYYY-MM-DD-<scope>.md`

## When to trigger

Trigger confidently on any of:

- "audit PD2" / "audit the project" / "audit the code"
- "review the netplay" / "review the menus" / "review the catalog" / "review the Grid" / "review the dedicated server"
- "find issues" / "find inherited bugs" / "lineage check" / "upstream sweep"
- "layout sweep" / "type-conversion sweep" / "struct drift" / "data-layout audit" / "sizeof mismatch hunt"
- "security review" / "deep review" / "scaling review" / "mod parity review"
- "do a pass on <subsystem>" where the request is evaluative rather than generative
- Cowork Dispatch handoff briefings that include an audit subtask

Do NOT trigger for: generative requests (write code, implement feature), debugging a single failing test, or questions about design intent that don't ask for evaluation.

## Workflow

### 1. Read context first

If a `context/` directory exists in the project, read it before auditing. Prefer these files (they may or may not all exist):

- `context/project-overview.md` — high-level state
- `context/current-focus.md` — what's in-flight right now
- `context/architecture.md` — system map
- `context/known-issues.md` — already-tracked problems (do not duplicate as findings)
- `context/upstream-quirks.md` — known inherited defects (confirm vs. re-find)
- `context/catalog-schema.md` — Catalog structure, if documented
- `context/grid-protocol.md` — Grid payload contracts, if documented
- `context/dedicated-server-plugin-api.md` — per-game plugin boundary, if documented
- `context/audits/` — prior audit reports; read the most recent to avoid duplicate findings

If the `context-manager` or `game-port-director` skills are available, coordinate with them for context access rather than re-reading directly.

### 2. Determine scope

Parse the user request into one of:

- **Full audit** — all provided surfaces, full report
- **Subsystem audit** — e.g., "audit the netplay", "audit the Catalog". Same report structure, scope narrowed to the named subsystem and its direct interfaces.
- **Dimension audit** — e.g., "security review", "scaling review". Same structure but the other two dimensions are summarized briefly while the named one is treated in full depth.
- **Lineage sweep** — e.g., "find inherited bugs". Focus on upstream-shape pattern scanning (N64-era constants, libultra naming, fixed player loops, manual byte-swap paths, etc.).
- **Catalog/mod-parity sweep** — e.g., "check mod equality". Focus on SOT enforcement and mod vs. native code-path symmetry.

If scope is unclear, ask — but default to full audit.

### 3. Load and execute the audit prompt

Load `references/audit-prompt.md` and follow it exactly. That file is the source of truth for report format, evidence standards, and review depth. Do not paraphrase or shortcut its structure.

### 4. Produce the report

Write the report to `context/audits/YYYY-MM-DD-<scope>.md` where:

- `YYYY-MM-DD` is today's date
- `<scope>` is `full`, or a short slug of the subsystem (`netplay`, `catalog`, `grid`, `dedicated-server`, `menus`, `input`, `modding`, `lineage`, `scaling`, `security`, `layout`)

If `context/audits/` does not exist, create it.

### 5. Summarize for the user

After writing the file, give a terse in-chat summary:

- Report path
- Count of Critical / High findings
- Top 3 action items from the report

Do NOT paste the full report in chat; the file is the artifact.

## Project context (cached essentials)

This cache exists so the skill triggers effectively even when `context/` is unavailable. For the full framing, always prefer `context/` first.

**Project:** Perfect Dark 2 — fan re-engineering of Rare's *Perfect Dark* (N64, 2000).

**Upstream lineage (dependency order):**

1. `n64decomp/perfect_dark` — Ryan Dwyer's matching N64 C decompilation (semantic root; MIT).
2. `fgsfdsfgs/perfect_dark` — PC / modern-platform port. `port` branch (single + split-screen) and `port-net` branch (NetPlay parent, ~8-player).
3. `jonaeru/perfect_dark` at `allinone-latest` — AllInOne mod parent (Perfect Dark Plus + All Solos in Multi + GoldenEye X Multi + per-arena mod swap mechanism).

**PD2-native architectural pillars (not in any parent):**

- **The Catalog** — single source of truth for all assets (levels, weapons, characters, props, audio, textures, mod packs, configs). Must be complete, current, fully-described. Any asset lookup outside the Catalog is a finding.
- **Equal-footing content loading** — mod content and native content traverse the same code paths. Any asymmetry is a candidate finding.
- **Modding pipeline** — deterministic, repeatable authoring → package → validate → distribute → load chain.
- **The Grid** — social layer + repeatability; untrusted input surface from a shared context.
- **Client online mode** — distinct mode from offline / LAN / split-screen; audit the mode boundary.
- **Game-agnostic dedicated server** — independent binary designed to host PD2 *and other games*; PD2-specific logic must live behind a per-game plugin boundary.

**Stance on inherited bugs:** liabilities, not features. Matching-decomp preservation does not apply to PD2. Upstream-shape code is a hunting signal, not a protected class. The audit runs a dedicated Phase 2.5 structural scan for upstream-shape patterns and for data-layout / type-migration hazards — a defect class PD2 has already seen in practice (a Vec3-style type change leaving a 4-byte junk tail that corrupted rendering).

**Scaling philosophy:** P2P session size scales with host compute and bandwidth; dedicated-server session size scales with server capacity. Hardcoded `MAX_PLAYERS = 8` (or any fixed-count literal inherited from N64 / port-net era) anywhere is a finding.

**Dependencies in scope:** SDL2 (input, windowing, audio), OpenGL / GLES 3.0+, Dear ImGui (debug / dev UI where present), plus networking transports on the `port-net` side.

## Cowork Dispatch integration

When invoked via Codex Desktop's Cowork Dispatch:

- **As a child session:** expect a handoff briefing that names the scope and any specific code ranges. Honor that scope; do not expand. Return only the scoped report.
- **As the master Dispatch agent:** spawn child sessions for per-subsystem audits in parallel when the full codebase won't fit a single context. Good child scopes:
  - Catalog audit
  - Netplay / P2P audit
  - Menu + input authority audit
  - Grid audit
  - Dedicated-server audit (boundary discipline + per-game plugin interface)
  - Modding pipeline audit (determinism + supply chain)
  - Scaling sweep (fixed-count literals, O(n²) loops, packet-width caps)
  - Lineage sweep (upstream-shape pattern scan)
  - **Data-layout sweep (Phase 2.5 type-migration and struct-stride hazard scan — especially Vec3/Vec4/matrix types, bitfields, packed structs, and any persisted format)**

  Each child gets: a scoped briefing, the path to `references/audit-prompt.md`, the path to the relevant code subtree, and an output path (`context/audits/YYYY-MM-DD-<scope>.md`).

- **After children return:** merge the per-scope reports into a master `context/audits/YYYY-MM-DD-full.md`. De-duplicate findings that appeared in multiple scopes and surface cross-cutting concerns that no single child could see.

## Report output

The full report structure is defined in `references/audit-prompt.md`. Do not deviate from that structure.

## What this skill does NOT do

- Write or modify implementation code
- Refactor the project
- Run the game, run tests, or fetch live data
- Paste the full report into chat (the file is the artifact)
- Trigger on generative requests (write / implement / build / create / add)
- Treat inherited bugs as preserved features
- Treat fixed player-count ceilings as acceptable
- Treat mods as second-class content
