# ADR: Game-agnostic `pd-server` — manifest broker, catalog IDs, optional policy module (C-1)

**Status:** Proposed (design-first — **P4-A**; implementation tracked as **P4-B** / **P4-C**)  
**Date:** 2026-04-21  
**Updated:** 2026-04-21 — **primary model** set to **host manifest + catalog IDs**; server **does not ship game content**. Loadable code (plugin) is **secondary** and only where rules cannot be expressed as data.  
**Depends on:** Explicit agreement before large refactors (Tier 4). **Orthogonal** to in-client listen hosting (Tier 3).

---

## Context

Today **`PerfectDarkServer.exe`** (`CMake` target `pd-server`) is a **single executable** that links:

- **Entry + lifecycle:** `port/src/server_main.c` — `main()`, SDL/GL optional GUI, `netInit()`, `hubInit()`, admin/bans, asset catalog base registration, update checks.
- **Stub layer:** `port/src/server_stubs.c` — defines hundreds of **game globals** (`g_Vars`, `g_MpSetup`, `g_MatchConfig`, …) and **stub functions** so shared TUs (`port/src/net/netmsg.c`, game-adjacent helpers) **link without** the full `src/game/` tree. Notable PD2-specific data includes tables such as **`g_MpArenas[]`** (historical stagenum alignment with `setup.c` / catalog expectations).
- **Bridge:** `port/src/server_bridge.c` — C API used by `port/fast3d/server_gui.cpp` (lobby player info, kicks/bans, `g_MpSetup` stage/scenario accessors). Mirrors the role of `pdgui_bridge.c` on the client, but compiled only for the server.
- **Shared net stack:** ENet host, `netmsg.c` dispatch, hub/room, catalog distribution — compiled for both `pd` and `pd-server` with `PD_SERVER` preprocessor gates.

**Problem:** The **“game-agnostic dedicated server programme”** pillar (AUDIT-C1 / MASTER-C5 class) is **not met** while `netmsg.c` and peers implicitly depend on PD2 globals and stubs, and while the server **behaves as if** it owns PD2-shaped **static** data.

**End state (product):**

1. **Each player has an independent, dynamically generated local catalog** — private to the client; paths, discovery, and caching are implementation details.
2. **Loading, data handling, and memory management for a match are driven by a host-supplied manifest** — clients **resolve** manifest entries **against** their private catalog as needed (`context/constraints.md`: **catalog ID strings** at boundaries, not `net_hash` / integer asset identity on the wire).
3. **The dedicated server should not require actual game content in its binary** — it is a **broker**: receives the host’s manifest, stores and distributes it, tracks **revision / hashes** for network agreement, and gates match start on **readiness** (including mod-accept flows). **No ROMs, levels, textures, or other art** ship inside `PerfectDarkServer.exe` if the protocol and manifest carry sufficient **metadata** (IDs + expected hashes + optional small rule payloads).
4. **Trust modes** (e.g. Trust Everyone, Trust Friends, Confirm First) are **per-player** UX + readiness — Confirm First shows a **modal** listing required mod/content (catalog-resolved labels) **without** breaking the lobby for other players or the host; only the confirming player’s readiness waits.

This ADR defines **that broker model first**, then **optional** loadable **policy** code for anything that **cannot** be expressed as manifest + catalog IDs + hashes. **P4-B** is a minimal vertical slice toward the broker path; **P4-C** shrinks `server_stubs.c` and tightens CMake visibility as code moves off baked-in game tables.

---

## Decision (high level)

### A. Primary: Manifest broker + catalog IDs (no game content in the server)

- **Authority:** The **host’s manifest** (for a room/match) is the **dictionary** for what the session means: which assets/mods/stages/skins/etc. must agree, with **catalog ID strings** as stable identity at protocol boundaries.
- **Server role:** Accept manifest from host path, **version** it (revision counter), **fan out** to clients, enforce **central consistency** you choose (e.g. every ready client acknowledges manifest rev **N** and hash set **H**). The server compares **IDs and hashes** and readiness bits — it does **not** load meshes or audio to validate a stage.
- **Clients:** Resolve manifest entries via **their** dynamic catalog; fetch/enable mods until **hash** matches manifest (or host-distributed baseline). **Actual bytes** live on clients (and distribution path), not baked into the server exe.
- **Pointers vs IDs:** **Wire and manifest** use **catalog IDs** only. **C function pointers** may still appear **inside** an optional native **policy module** for link-time / code organization — they are **not** the identity layer for assets (see below).

### B. Secondary: Optional loadable policy module (PD2 or future game)

Where rules cannot be expressed as **data** in the manifest (rare), a **versioned** `pd_server_plugin_reg`-style ABI (DLL / so or static archive) may supply **hooks** — not **arena tables** as the long-term source of truth. That module still keys public behavior off **catalog IDs** when touching assets.

**Non-decision (until spike):** Dynamic `LoadLibrary` vs static link for that module — broker-first work should not depend on the loader.

---

## Core vs responsibilities (manifest-first)

| Area | **Core (`pd-server`)** | **Clients / host** |
|------|------------------------|---------------------|
| **Process / OS** | `main`, SDL (GUI), headless, logging, admin | Full game + local catalog |
| **Transport** | ENet, hub/room, connect codes | — |
| **Manifest** | Store, revise, distribute, attach to room/match; readiness per peer | Host **authors** manifest; clients **resolve** against private catalog |
| **Identity** | Catalog ID strings + hashes on wire (`constraints.md`) | Local mapping ID → bytes |
| **Trust / Confirm First** | Per-player readiness; do not block whole lobby | Modals; accept/decline policy |
| **Game payloads** | **None** in shipping server binary (target) | All loading / memory for assets |

Optional **policy module** row: only if manifest cannot encode a rule; must not reintroduce **shipped art** into the exe.

---

## Message dispatch strategy

**Today:** `netmsg.c` is a large switch with direct access to `g_MpSetup`, `g_MatchConfig`, stubs, etc.

**Target:**

1. **Keep wire codec in core** — one `NET_PROTOCOL_VER` stream; manifest/manifest-ack paths use **catalog IDs** and **hash/revision** fields, not opaque pointers.
2. **Prefer data over hooks** — “is this stage allowed?” becomes “is this `stage_id` listed in the **current host manifest** with matching hash?” before falling back to PD2-specific code.
3. **Hooks** (`pd_server_game_ops` or equivalent) only for **non-data** checks; default core implementation may **reject** unknown games until a module registers.

**Stub replacement:** Pure link glue (`playerDie`, …) may remain **empty in core** or in a thin module. **Baked tables** (`g_MpArenas`, …) should **migrate out** of the server’s authority model in favor of **manifest + catalog**, then **delete** from `server_stubs.c` as `netmsg` stops consulting them.

---

## Lobby / match config shapes

- **Wire shapes** remain canonical; **manifest** is the **authoritative bundle** for “what this match needs” beyond minimal room state.
- **Per-player dynamic catalogs** are **not** replicated wholesale — only **manifest + acks + hashes** cross the broker boundary in a disciplined way.

---

## CMake targets (proposed)

| Target | Role |
|--------|------|
| **`pd-server-core`** | Transport, hub/room, manifest broker, admin — **no** monolithic PD2 tables as product truth |
| **`pd2_server_policy`** (optional) | Non-data rules + remaining stubs only if required |
| **`pd-server`** / `PerfectDarkServer.exe` | Core + optional policy; **no** game assets linked as content |

**Explicit symbols:** If a DLL is used, export only the **registration entry**; broker surfaces stay in core.

---

## Versioning

| Kind | Rule |
|------|------|
| **Wire protocol** | `NET_PROTOCOL_VER` — bump when message definitions change (manifest fields, trust acks, etc.). |
| **Manifest schema** | Version or revision field **inside** manifest payloads; server tracks **host manifest revision** per room. |
| **Policy module ABI** | `PD_SERVER_PLUGIN_ABI_VERSION` — bump when optional hook struct changes. |

---

## Migration strategy (P4-B / P4-C)

1. **P4-B (spike):** Smallest **broker-aligned** slice — e.g. **host manifest** received and **stored** on the server, **fan-out** to joining clients, **per-player readiness** bits for “accepted manifest rev / hash” (wire + server state only). **Must not** break lobby for others; Confirm First can be **stub UI on client** with server-side readiness first. **Avoid** introducing new **baked** PD2 tables as the fix. Prefer **catalog IDs + hashes** in the manifest path. “Byte-identical” applies to **existing** behaviors **outside** the new manifest fields; new protocol fields imply a deliberate **`NET_PROTOCOL_VER`** bump per `constraints.md`.
2. **P4-C:** Reduce `server_stubs.c` — remove reliance on **static game lists** where manifest replaces them; track **line count** and CMake link surfaces; document metrics in `session-log.md` or a small metrics note.

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| **Manifest schema creep** | Version manifest body; document minimal required fields (IDs + hashes). |
| **`netmsg.c` tangling** | Incremental extraction; manifest checks before PD2-specific branches. |
| **Confirm First stalls match** | Per-player readiness only; host start rules explicit in wire spec. |
| **Security** | Broker trusts **host** for manifest content; clients validate **hashes**; RCON/bans remain core. |

---

## References (current tree)

- `port/src/server_main.c` — dedicated entry, init order, `g_NetDedicated = 1`, catalog + net + hub.
- `port/src/server_bridge.c` — lobby/player bridge for `server_gui.cpp`; may narrow over time as manifest drives setup.
- `port/src/server_stubs.c` — legacy globals + stubs; **target shrink** as broker + client catalog resolution replace static authority.
- `CMakeLists.txt` — `SRC_SERVER` list, `PD_SERVER=1`, `OUTPUT_NAME PerfectDarkServer`.
- `context/server-architecture.md` — link **hosting modes** + this ADR.

---

## Out of scope (v1 ADR)

- Full **relay / master server** product.
- **Multiple games** in one binary (design allows).
- Rewriting all of **`netmsg.c`** in one pass.

---

## Decision record

| Date | Outcome |
|------|---------|
| 2026-04-21 | Initial draft — plugin ABI + stubs. |
| 2026-04-21 | **Superseded emphasis:** manifest broker + catalog IDs primary; server ships **no game content**; optional policy module secondary; P4-B/C realigned. |
