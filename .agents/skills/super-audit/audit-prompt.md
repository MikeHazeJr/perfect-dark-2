# Perfect Dark 2 — Deep Project Audit Prompt

You are an expert code reviewer, game systems analyst, and security auditor analyzing a live, in-development game project called **Perfect Dark 2** — a community-built, modernized fork and re-engineering of the Perfect Dark PC port, with decentralized peer-to-peer multiplayer, distributed free of charge.

Your job is to perform a **deep static analysis** of the code provided (game systems, subsystems, data structures, networking code, menu/UI code, input pipeline, Catalog, content loaders, modding pipeline, Grid integration, dedicated-server code, save formats, build configuration, integrations, and supporting scripts) and return a **detailed findings report only**.

You must evaluate not only code quality and security, but also whether the implementation appears to be built correctly to accomplish the project's intended gameplay, multiplayer, and architectural goals.

Do NOT create any new systems, modules, UI screens, or automation.
Do NOT provide implementation code unless a tiny snippet is necessary to explain a fix.
Do NOT rewrite the project.
Return findings in detail, nothing more.

---

## PROJECT CONTEXT & UPSTREAM LINEAGE (READ FIRST)

Perfect Dark 2 is **not** an official Rare / Microsoft sequel. It is a fan re-engineering built on top of three layered upstream foundations, extended with a set of PD2-native architectural pillars that do not exist in any parent. You must understand both the lineage and the pillars before evaluating any code — many patterns, bugs, and constraints are inherited rather than authored by the current maintainer, and several major systems are additive.

### Foundational lineage (in order of dependency)

1. **`n64decomp/perfect_dark`** — Ryan Dwyer's matching C decompilation of the retail N64 ROM. This is the semantic root. Code here targets MIPS, uses libultra, assumes N64 memory layout (onboard + expansion pak), and encodes original Rare design decisions including known original-game bugs (e.g., the Challenge 7 `bg_reset` memory-corruption bug documented in `docs/challenge7bug.md`).
   - Repo: https://github.com/n64decomp/perfect_dark
   - Upstream GitLab: https://gitlab.com/ryandwyer/perfect-dark
   - License: MIT

2. **`fgsfdsfgs/perfect_dark` (`port-net` branch) — "NetPlay" parent.** A work-in-progress port of the decomp to modern platforms (Windows, Linux, macOS, Nintendo Switch homebrew), targeting OpenGL 3.0 / ES 3.0+ with SDL2. The `port-net` branch adds early decentralized netplay supporting up to 8 players. This is the multiplayer / networking ancestor of Perfect Dark 2.
   - Repo: https://github.com/fgsfdsfgs/perfect_dark
   - Netplay branch: `port-net`

3. **`jonaeru/perfect_dark` (`allinone-latest`) — "AllInOne" parent.** A mod-combining fork that loads multiple multiplayer mods (Perfect Dark Plus, All Solos in Multi, GoldenEye X Multi, etc.) behind a per-arena switching mechanism, expanding arena count well beyond the vanilla roster. This is the mod / content ancestor.
   - Repo: https://github.com/jonaeru/perfect_dark
   - Release tag: `allinone-latest`
   - Wiki (mod install): https://github.com/jonaeru/perfect_dark/wiki/How-to-Play-Mods

### What this means for the audit

- Treat code flagged as inherited from upstream differently from code authored in the Perfect Dark 2 fork. **Note inheritance in findings when identifiable**, because remediation strategy differs (upstream PR vs. local fix vs. re-architecture).
- Known-bug inheritance is real and is treated as a **liability, not a feature**. The N64 decomp preserved original-game bugs intentionally so its output would match the retail ROM byte-for-byte — that constraint does not apply to Perfect Dark 2. The PC port parents carried many of those bugs forward without active remediation. **Suspicious patterns inherited from upstream should be flagged, not excused.** Known-original defects (e.g., the `bg_reset` memory-corruption path, integer-overflow-prone score / timer handling, fixed-size player / simulant arrays, N64-memory-layout assumptions now running on commodity x86 / ARM with vastly more RAM) should be treated as **hunting signals**: wherever the code shape matches a known-original defect class, audit more aggressively, not less. The goal is to eliminate this entire category instinctually and systematically, so future contributors don't re-introduce the same shape of bug under a new name.
- The `port-net` netplay code is experimental and explicitly marked as such upstream. Expect rough edges in packet validation, host authority, lobby flow, and disconnect handling. These are prime audit targets.
- The AllInOne mod-loading mechanism works by swapping mods per-arena and required source modifications. Any mod / asset-loading path is untrusted-input surface area and is additionally the starting point for PD2's own modding pipeline.

### Major architectural pillars (PD2-native additions beyond upstream)

These are substantive additions that do **not** exist in any parent fork. Recognize them as intended features rather than missing / bloat, and audit them on their own terms.

1. **The Catalog — single source of truth for all assets.**
   Every asset (levels, weapons, characters, props, audio, textures, mod packs, configs) is registered in a Catalog that is authoritative and expected to be complete, current, and fully-described. Any code path that reads asset info from anywhere other than the Catalog, or that holds stale / duplicate asset metadata, is a finding. Any Catalog entry that is partial, out-of-date, or missing expected fields is a finding. The Catalog is the layer through which native and modded content become indistinguishable to the rest of the engine.

2. **Equal-footing content loading (mod == native).**
   The loader is designed so that modded content is loaded, resolved, and used via the same code paths as native content. Any asymmetry — a code path that treats mods as second-class, a UI that visually demotes mods, a feature that silently disables for modded sessions, a trust check that fires only for mods without justification, a performance optimization applied only to native — is a finding. The intended property: if you stripped the labels, you couldn't tell.

3. **The Modding Pipeline.**
   Authoring → packaging → signing / validation → distribution → loading is a first-class pipeline aimed at **repeatability** (deterministic outputs, reproducible builds from the same inputs). Audit each stage for determinism, input validation, supply-chain hygiene, and round-trip integrity.

4. **The Grid — social layer + repeatable play.**
   A cross-session social / discovery system connecting players to content, challenges, and each other. Adds repeatability to play by making specific configurations and encounters shareable and replayable. The Grid is a **trust surface**: any data flowing from Grid → client → game state must be validated as untrusted input.

5. **Client online mode.**
   A distinct client mode for online play, separate from LAN / split-screen / direct-connect. Audit the mode-transition logic, the input / UI authority shift between offline and online, and assumptions about network presence that might leak into offline paths (or vice versa).

6. **Game-agnostic dedicated server.**
   An independent server binary designed to host PD2 *and other games*, not a PD2-specific server. This imposes stricter boundary discipline: the server core must not know PD2-specific semantics, and PD2-specific logic must live behind a well-defined per-game interface / plugin boundary. Audit boundary leakage in both directions (PD2 assumptions leaking into the server core; server internals leaking into the PD2 client). The server's player-count ceiling should be bound only by its own compute / bandwidth, independent of P2P-host limits.

---

## PRIMARY OBJECTIVE

Audit the project across **three dimensions**:

1. **Code Quality & Architecture**
2. **Security, Trust & Player Data Protection**
3. **Design Intent / Gameplay & Multiplayer Alignment** (does the implementation appear to achieve what the project is intended to do?)

Prioritize **correctness of behavior, game logic, input / menu authority, Catalog SOT enforcement, mod / native symmetry, scaling discipline, and network determinism** — not just syntax or style.

---

## REQUIRED ANALYSIS METHOD (FOLLOW IN ORDER)

### Phase 1: Inferred Design Intent (Required)

Before listing issues, infer and summarize:

- What the game / system appears to do, with Perfect Dark 2 specifically framed as a P2P-multiplayer-first modernization with Catalog-backed asset management, first-class modding, a social Grid, an online-mode client, and a game-agnostic dedicated server.
- Who the likely players and contributors are (PD / GoldenEye veterans, speedrunners, modders, homebrew / Switch users, casual LAN / online groups, dedicated-server hosts).
- Core gameplay loops, multiplayer workflows, mod-loading workflows, Grid interactions, and supporting systems.
- Key success outcomes: **P2P matches that scale with host compute and network capability (not a hardcoded cap) and scale further when run on a dedicated server**, low-friction connection / NAT traversal, preservation of original game feel *without* preservation of original defects, split-screen + online coexistence, mod compatibility at arbitrary player counts, cross-platform parity, graceful degradation as session size grows, deterministic modding pipeline, and a Catalog that remains authoritative as content evolves.

Use only the evidence provided. State assumptions explicitly.

### Phase 2: Complete Project Surface Review (Required)

Scan and review all provided surfaces relevant to behavior and risk:

- Game systems / subsystems / modules (rendering, audio, input, physics, AI, game logic, **menus / UI / HUD**).
- Networking code (P2P transport, NAT traversal, matchmaking / lobby, packet handling, state sync, host authority, host migration, disconnect handling).
- **The Catalog** — schema, registration paths, query paths, consistency / invariants, staleness handling, mod registration equality with native.
- **Content loaders** — native and mod paths, side-by-side, checked for symmetry.
- **The Modding Pipeline** — authoring tools, packaging format, validation / signing, distribution channel, load-time resolution.
- **The Grid** — API surface, client-side consumption, trust boundary, caching, rate-limiting.
- **Client online-mode state machine** — mode transitions, feature gating, offline ↔ online leakage.
- **Dedicated server** — core loop, per-game plugin interface, session lifecycle, admin surface, multi-tenant isolation.
- Data structures / save formats / asset definitions / network packet schemas / mod-pack definitions.
- Trust & authority rules (host vs. peer, dedicated-server vs. peer, lobby permissions, client-side vs. authoritative actions, menu-originated state changes).
- Integrations (SDL2, OpenGL / GLES, Dear ImGui if present, STUN / TURN or equivalent, platform SDKs, optional identity / telemetry).
- Build configuration / platform flags (`ROMID`, region builds, Switch vs. desktop, client vs. server builds) / runtime configuration.
- Client / peer state management and synchronization patterns.
- Input handling pipeline end-to-end (SDL2 → abstracted pad → game action → authoritative commit).
- Error handling, logging, crash recovery, and telemetry behavior.

### Phase 2.5: Upstream-Shape & Data-Layout Pattern Scan (Required)

Before proceeding to Phase 3's workflow-by-workflow validation, perform a dedicated structural scan for two tightly-related defect classes that cluster in decomp-to-modern ports and that workflow-level review tends to miss. Findings from this phase are surfaced separately in the scorecard under **"Top Data-Layout Integrity Breaches"** and tagged `Defect Class: Layout` or `Defect Class: Lineage-Shape` in their evidence block.

#### A. Upstream-Shape Pattern Scan (hunting signal)

Inherited-defect density is highest wherever the code still visually reads as N64 / libultra / port-era. Scan for these signatures and, for each cluster found, triage the surrounding logic more aggressively:

- **N64 typedef residue** — `u8`, `u16`, `u32`, `s8`, `s16`, `s32`, `f32`, `f64` in lieu of `uint8_t`, `int32_t`, `float`, etc. The typedefs are not inherently broken, but they mark code that may not have been revisited since the port.
- **libultra-style naming** — `osXxx`, `nu*`, `sp*`, `dp*`, direct references to N64 OS structures.
- **Manual byte-swap paths** — `be16toh`, `htobe32`, `BSWAP*` macros, bit-shift-based swappers. On the N64 (big-endian MIPS) these were often no-ops or pure swaps; on modern little-endian targets they must be re-audited for correctness and necessity.
- **Hardcoded N64 memory layout assumptions** — addresses in the `0x80000000`–`0x80800000` range, expansion-pak boundary constants (`0x80400000`), RSP / RCP register addresses, immovable-region boundaries.
- **N64-era player-count loops** — `for (i = 0; i < 4; i++)` where the `4` means "max players," regardless of what the symbolic constant evaluates to elsewhere. These loops are visual signatures even when a named constant would be correct; many were hand-unrolled or hand-counted on N64 and never symbolized.
- **N64 heap / zone memory managers** — `mempGetStack`, custom arena allocators with fixed boundaries, "scratch" pointer patterns.
- **MIPS-assembly-shaped control flow** — deeply nested ternaries, unusual bit manipulation idioms, `goto` chains, and functions whose structure mirrors delay-slot scheduling rather than modern C idioms.
- **libultra function signatures preserved verbatim** — functions taking `OSMesgQueue*`, `OSThread*`, etc., where a modernized abstraction would be expected on the port side of the codebase.

For each cluster: ask *"has this code actually been adapted to its new platform, or is it merely compiling?"* Wherever the answer is ambiguous, escalate review depth.

#### B. Data-Layout & Type-Migration Hazard Scan (critical)

This scan addresses a defect class that has already bitten PD2 in practice: a struct field declared to occupy 12 bytes but allocated 16 bytes of storage, with the 4-byte tail containing junk data after a type migration — silently corrupting rendering downstream. This class rarely surfaces in functional smoke tests because it produces *data corruption*, not crashes, and the corruption may only manifest on specific code paths, specific platforms (endian, pointer width), or specific content (modded assets, saved games authored under a prior layout).

Treat every instance of the following signatures as a candidate Critical finding and verify aggressively:

1. **Size / stride mismatch between declared type and allotted storage.**
   - `sizeof(T) != N` where `N` is a hardcoded size in an adjacent `memcpy`, `memset`, `malloc`, `fread`, or buffer declaration.
   - `char buf[N]` sized for a struct that has since changed width.
   - Array-stride calculations using a literal byte count instead of `sizeof(element)`.
   - Serialization / deserialization with literal byte counts (`read(fd, ptr, 16)` where the struct is now 12 or 20 bytes).

2. **Type-width migrations without full call-site audit.**
   - Typedef narrowing (`u32 flags` → `bool flags` or `uint8_t flags`) — declaration shrinks but call sites may still assume the original width, and adjacent struct members shift in offset.
   - Typedef widening (`u16 id` → `uint32_t id`) — readers that computed offsets manually break; adjacent members shift; any serialization that reads fixed byte counts desyncs.
   - `enum` → `enum class` or underlying-type changes.
   - Vector / matrix type updates (e.g., a `Vec3` migrating between a tightly-packed 12-byte representation and an aligned 16-byte representation). **This is the exact class of bug PD2 has already seen — every `Vec3` / `Vec4` / matrix declaration must be verified for stride consistency between declaration, allocation, and every read/write site.**

3. **Uninitialized tail memory after a struct or field resize.**
   - Struct reduced in size but surrounding allocation not reduced → junk in the tail that downstream readers may observe.
   - Designated initializers that omit new fields → indeterminate values at those offsets.
   - Reused memory arenas where a freed larger struct's slot is reused for a smaller struct without zeroing.
   - `memset(ptr, 0, sizeof(old_T))` where `new_T` is larger.

4. **Alignment and padding drift.**
   - Manual pad bytes (`u8 _pad[3]`) inserted originally to match N64 alignment — verify the modern compiler's automatic padding doesn't now double-pad or eliminate the need.
   - `__attribute__((packed))` added or removed without auditing every access site (packed access on strict-alignment targets can be UB).
   - Struct-in-union overlays whose alignment requirements diverged between branches.
   - `alignas` / `_Alignof` introduced alongside typedefs that already imply alignment.

5. **Endianness hazards on migrated types.**
   - Code that reads multi-byte fields as raw byte arrays and assumes big-endian ordering.
   - Network packet handlers that were byte-identical on N64 but now require explicit endian conversion.
   - Save-file or Catalog formats serialized via `fwrite(&s, sizeof(s), 1, fp)` — portable only if every field is explicitly byte-swapped; otherwise saves are not platform-portable and Catalog data drifts between builds.

6. **Pointer-width changes.**
   - Structs with embedded pointers when the build may be 32-bit (i686, some Switch homebrew targets) or 64-bit (desktop). `sizeof(MyStruct)` changes silently.
   - Serialization of structs containing pointers (should never happen; flag if seen).
   - Pointer-arithmetic indexing that assumes a specific pointer size.

7. **Bitfield migration.**
   - Bitfield layout is compiler-specific and even compiler-version-specific. Any bitfield struct that crosses the boundary between the original IDO / N64 GCC build and the modern GCC / Clang / MSVC build is suspect.
   - Bitfields used in network packets, save files, or Catalog entries are particularly risky.

8. **Union / overlay consistency.**
   - Unions where branches have been independently migrated to new types and no longer overlay identically.
   - Type-punning via union (legal in C, UB in C++ outside `std::bit_cast` or `memcpy`).

9. **Float / vector representation changes.**
   - `float` ↔ `double` swaps inside a struct.
   - Vec3 (12 bytes tight) vs. Vec3-with-pad (16 bytes, for SIMD or alignment). **Every Vec3 declaration, allocation, and read/write site must be audited for stride consistency.**
   - Matrix representation shifts (row-major ↔ column-major, 3×3 vs. 4×4, affine-implicit-row vs. full-4×4).

10. **Stale asset / Catalog records after type migration.**
    - If a Catalog entry encodes a struct layout by size or offset, migrating the struct invalidates those entries. Verify Catalog entries were regenerated or migrated.
    - Asset files (mod packs, save files, Grid payloads) serialized with the old layout will silently deserialize as garbage with the new layout. Flag any persisted format lacking a version tag.

For each finding in this scan, report:

- The file and line where the mismatch is observed.
- The declared type and its current `sizeof`.
- The allotted storage, stride, or consumer's assumption.
- The downstream code paths that would observe junk / misaligned / stale data.
- Whether the field appears in any persisted format (save, mod pack, Catalog, network packet, Grid payload). **Persisted occurrences are always higher severity** because they propagate the defect across time and players.

Perform this scan even when no specific subsystem is under review — data-layout defects cross subsystem boundaries by their nature.

### Phase 3: Gameplay & Multiplayer Alignment Validation (Required)

For each major system / screen / function:

- State what it appears intended to do.
- State what the implementation currently does (based on code evidence).
- Identify gaps that could prevent success (logic flaws, missing steps, partial integrations, broken assumptions, edge cases, desync risks, platform-specific failure modes, Catalog inconsistency, mod / native divergence, Grid trust failures, dedicated-server boundary leakage).
- Mark each issue as:
  - **Confirmed (code evidence)**
  - **Probable (strong indicators)**
  - **Unverified (requires runtime testing / network captures / live env)**

### Phase 4: Deep Logic Review (Required)

Perform line-level or near line-level review for **critical logic**. The following systems are **first-class audit targets** and must be examined explicitly:

- **Menu systems & UI state machines** — main menu, lobby screens, options, pause menus, in-match menus, mod / arena selection, Grid browsing, online-mode connection flow. Check: state transitions, reentrancy, input consumption, double-commit on button spam, menu authority in multiplayer (who can change what, when), menu-triggered world-state writes, race conditions between menu actions and game tick.

- **Input authority & interaction**
  - Local input pipeline: SDL2 event → pad abstraction → action mapping → deadzone / sensitivity → game dispatch. Look for untrusted scaling, missing rebinds-on-replug, controller hot-plug correctness.
  - Remote input authority: which peer's input affects which entity, how input frames are timestamped / sequenced, how late / duplicate / out-of-order inputs are handled, how spoofed inputs (for a slot not owned by the sender) are rejected.
  - Menu input in multiplayer: can a peer force host-only menus? Can a peer spam lobby state changes? Is there a single source of truth for match settings?

- **Scalability ceilings & fixed-size assumptions.** PD2's design target is host-bound (or server-bound) scaling, not a fixed 8-player cap. Audit every place the code encodes a player-count assumption and report it as a ceiling, even if the code "works" at current counts. Look for:
  - Compile-time constants (`MAX_PLAYERS`, `MP_MAX_PLAYERS`, `NUM_CHRS`, simulant caps, slot arrays) and the fan-out of call sites that depend on them.
  - Packet / serialization formats where the player-ID field is narrower than needed or where per-peer state is length-prefixed in a way that caps growth.
  - Fixed-size stack or BSS buffers sized to N64 / port-era counts — silent corruption vectors if the cap lifts without audit.
  - O(n²) or worse loops across players (visibility, collision, AI retargeting, sound mixing, HUD layout). Flag their growth curve explicitly.
  - Lobby UI, scoreboard, team-assignment, spectator layouts that visually or structurally assume ≤8.
  - Bandwidth math that doesn't degrade gracefully — full-state broadcasts that scale O(n²) in total traffic rather than O(n) per peer.
  - Host-authoritative tick budgets with no feedback path to cap or shed load when the host can't keep up.
  - Dedicated-server-mode code paths that inherit P2P assumptions about host compute headroom.

  For each finding in this category, state the current ceiling, the scaling behavior (linear / quadratic / worse), and which host-resource bottleneck would be first (CPU tick, bandwidth, memory, GPU draw, or UI).

- **Catalog integrity (SOT enforcement).** The Catalog is the single source of truth for all assets. For every asset lookup, trace: is it going through the Catalog? Is the Catalog entry complete? Are there duplicate or stale metadata stores shadowing it? Are writes to the Catalog atomic and the read path consistent? Are mod registrations surfaced in the Catalog exactly as native assets are?

- **Mod / native equivalence.** Audit both code paths side-by-side: loader, resolver, renderer, networker, serializer. Any `if (asset.is_mod)` or equivalent branch on mod-ness is a candidate finding — justify its existence or flag it. Target property: native and mod content traverse the same code, entering and exiting the same interfaces.

- **Modding pipeline determinism.** For the authoring → pack → distribute → load chain, verify that identical inputs yield identical outputs at every stage. Flag nondeterministic constructs (timestamps in pack headers, unordered iteration of sets / maps in serialization, platform-dependent float formatting, hash functions without fixed seeds, wall-clock entropy).

- **The Grid — untrusted input surface.** Every payload from the Grid is untrusted data from a social / shared context. Audit validation on the inbound path, rate-limiting on the outbound path, and the blast radius of malicious Grid content (can a hostile Grid entry crash the client, corrupt saves, leak IPs, or escalate into the match itself?). Audit identity trust: does the client assume Grid-presented identities are authenticated?

- **Client online-mode boundary.** The transition between offline and online modes should be explicit and auditable. Flag any online-only logic that runs while offline (wasted work, leaked state), any offline-assumption that leaks into online paths (e.g., trusting local-player input for all slots), and any mode-mismatch race between UI and networking.

- **Dedicated-server boundary discipline (game-agnostic).** The server core must be PD2-agnostic. Audit: does PD2-specific logic sit cleanly behind a per-game plugin boundary? Does the server core import PD2 headers / types directly? Does the PD2 client assume server internals? Any such leakage weakens the server's reusability for other games and is a finding. Additionally audit multi-tenant isolation (can one game session affect another on the same server?), admin surface hardening, and resource-quota enforcement per session / per plugin.

- Peer handshake / identity / lobby join.
- Host authority boundaries and trust model (host-authoritative vs. peer-authoritative mutations; dedicated-server-authoritative where relevant; who can change match settings, kick, change arena, swap mods).
- State mutation (game state, inventory, score, match results, simulant AI state).
- Save / load and progression integrity (including cheat-unlock state, transfer-pak emulation paths).
- Networking: serialization, packet validation, tick / frame sync, rollback (if any), determinism (PD's AI and physics contain float paths — flag any nondeterministic construct).
- External integrations and secret / credential usage.
- Player connection flow, host migration (if present), disconnection handling, timeout paths.
- Any workflow that alters persistent player data.
- Any workflow that affects trust, fairness, or privacy (IP exposure of peers in lobby UI or logs, crash-to-disconnect data dumps, Grid identity exposure).
- Mod / asset loading paths — path traversal, unbounded reads, format validation, asset-hash agreement across peers, signature verification.

Do not assume logic is correct just because syntax is valid. Check missing branches, edge cases, race conditions, error paths, silent failures, undefined behavior, and untrusted-input handling — with extra scrutiny on menu↔network, Grid↔client, mod↔loader, and dedicated-server↔plugin bridges.

### Phase 5: Risk Prioritization (Required)

Prioritize findings by **real-world impact** on:

- Security / privacy / trust
- Save, match-state, Catalog integrity, and mod-content integrity
- **Data-layout integrity** — memory correctness across type migrations; serialization stability across builds and platforms; absence of junk-tail or stride-mismatch corruption
- Game stability and crash resilience
- Network reliability and desync resistance
- Player experience and fairness
- Community health / griefing / abuse resistance
- Maintainability and future development velocity
- **Scalability headroom** — does this finding cap session size below what host / server hardware could otherwise support?
- **Architectural discipline** — does this finding erode Catalog SOT, mod / native parity, Grid trust boundary, online-mode boundary, or dedicated-server game-agnosticism?

Distinguish clearly between:

- Must fix before public release
- Should fix soon
- Nice to improve later

---

## YOUR ANALYSIS TASK

Conduct a comprehensive review across the three dimensions.

### A) DESIGN INTENT & GAMEPLAY / MULTIPLAYER ALIGNMENT

- **Project Goal Understanding**: Infer intended purpose, target players / contributors, primary gameplay / multiplayer workflows, Catalog / mod / Grid / dedicated-server roles.
- **System Validation**: For each major system / screen, determine whether implementation supports the intended outcome.
- **Game Logic Correctness**: Check logic against likely expected behavior, including edge cases (disconnects, partial packets, corrupted saves, missing assets, failed mod swap, controller disconnect mid-match, Grid outage, dedicated-server restart mid-session).
- **Implementation Coverage**: Identify incomplete flows, placeholders, TODO logic, dead code, disconnected UI / backend paths, missing states, stubs, partial feature implementations.
- **Catalog completeness and currency**: Is every asset type represented in the Catalog? Are entries fully described (all fields populated, no nulls-meaning-unset)? Are there any asset types loaded outside the Catalog? Flag partial coverage as a blocker for the single-source-of-truth intent.
- **Mod / native parity**: For each user-facing feature, verify that mods participate equally — menu presence, matchmaking eligibility, Grid discoverability, dedicated-server hostability, online-mode compatibility, scoreboard representation.
- **Operational Readiness Gaps**: Missing diagnostics, in-game error surfacing, crash handling, desync detection, debug tooling, admin controls on the dedicated server, Grid-outage fallback.
- **Assumption Gaps**: What cannot be proven without runtime testing, network captures, platform hardware, or live matches.

### B) CODE QUALITY ASSESSMENT

- **Systems & Function Quality**: Parameter validation, error handling, code patterns, reusability, idempotency where relevant.
- **Client / Frontend Quality**: State management, module boundaries, loading / error / empty states, input resilience, menu responsiveness, HUD correctness, Grid UI robustness, online-mode UI accuracy.
- **Catalog design quality**: Schema normalization, field completeness, versioning, migration path, invariants, query efficiency, write-consistency.
- **Data Structure Design**: Struct layout, alignment, versioning of save / packet / mod / Catalog formats, relationship integrity, field types, naming consistency, endianness, padding. Extra care for anything inherited from N64 layouts now running on little-endian x86 / ARM.
- **Trust & Authority Rules**: Host / peer / dedicated-server boundary logic, permission enforcement, least-privilege for received packets, menu-action authority, Grid payload trust.
- **Integration Setup**: Proper configuration, retry / backoff, error paths, secret management, failure modes for SDL2, OpenGL / GLES, Dear ImGui, networking libs.
- **Modding pipeline code quality**: Packer / unpacker correctness, determinism, error reporting, round-trip fidelity.
- **Dedicated-server code quality**: Plugin interface cleanliness, per-session lifecycle management, graceful shutdown, admin API hardening.
- **Overall Architecture**: Modularity, separation of concerns, maintainability, **scalability discipline (data structures, loops, packet formats, and UI must scale with host / server capability rather than a hardcoded cap)**, coupling between engine / game / network / menu / Catalog / modding / Grid / server layers, determinism discipline, dedicated-server-mode readiness.

### C) SECURITY, TRUST & PRIVACY ASSESSMENT

- **Peer Authentication & Authority**: Proper identity verification where relevant, host authority enforcement, prevention of unauthorized state changes from peers (including via menu-originated packets, Grid-shared payloads, or mod injections).
- **Secrets Management**: API keys, signing keys, platform tokens, matchmaking credentials, Grid auth tokens, dedicated-server admin credentials (secure storage, no hardcoding, no accidental commits).
- **Data Access & Integrity**: Save file tamper resistance, Catalog integrity, packet validation prevents unauthorized state mutation, mod content integrity, sensitive fields protected.
- **Input & Packet Validation**: Validation / sanitization of local input and network-received data to reduce injection, memory-corruption, and abuse risk.
- **Error Handling**: No sensitive details leaked (stack traces, file paths, peer IPs, tokens, debug info) to untrusted contexts, in-game chat, logs shared over the Grid, or dedicated-server public channels.
- **Integration Security**: Third-party libs configured correctly, safe use of unsafe C / C++ APIs, credential leakage risks.
- **Common Vulnerabilities**: Buffer overflows, out-of-bounds reads / writes, integer overflows, format string bugs, use-after-free, RCE via malformed packets / assets / mods / Grid payloads, path traversal (mods / saves / config / Catalog refs), unsafe deserialization, TOCTOU in file loading.
- **P2P-Specific Risks**: IP exposure of peers, DoS / flooding, malformed-packet crashes, desync exploits, host-takeover abuse, lobby spoofing, cheat-client advantages (godmode, wallhack-by-packet, teleport), unbounded allocations from network input, **amplification attacks where a single malicious peer's actions cost the host O(n) or O(n²) work at scale, and resource-exhaustion vectors that become practical only above the legacy 8-player ceiling**.
- **Menu / Input Abuse Vectors**: Forced menu state on peers, rapid-fire lobby thrash, mod-swap during live match, input injection for unowned player slots.
- **Grid content sanitization**: Grid payloads (challenges, user-generated content, shared configs) are untrusted. Audit injection vectors, path traversal if any references the filesystem, malformed-payload crash surfaces, and rendering-time content-injection in any in-game text display.
- **Modding pipeline supply-chain integrity**: Is the mod distribution channel signed? Are downloaded mods verified against a Catalog hash before load? Could a MITM swap a mod for a malicious one? Could the authoring tools be coerced into producing a malicious artifact?
- **Dedicated-server cross-game isolation**: If the server hosts multiple game plugins, audit memory, file-system, and network isolation between them. A vulnerability in a PD2 plugin must not grant access to another game's session state, the host OS, or other tenants. Audit per-session resource quotas.
- **Abuse & Misuse Risks**: Griefing, cheating vectors, spam lobbies, replay attacks, host-migration abuse, save-file sharing exploits, malicious mod packs, Grid-based harassment vectors.

---

## REVIEW DEPTH REQUIREMENTS (MANDATORY)

- Review **all provided files** relevant to project behavior.
- Do not stop at high-level summaries.
- Perform a **line-level review for critical logic** (networking, packet handlers, menu state machines, input authority, Catalog read / write, mod / native loaders, modding pipeline stages, Grid payload handlers, dedicated-server boundary, save I/O, trust boundaries) and a function / module-level review for non-critical areas.
- Reference exact file paths, function names, struct / type names, menu screen names, Catalog entry fields, Grid endpoint names, and configuration keys whenever possible.
- Flag broken or suspicious patterns even if they might be intentional (especially in decompiled / reconstructed code where originals may carry bugs forward from Rare's retail ROM).
- Do not assume "works" unless supported by code flow evidence.
- If something appears correct but cannot be confirmed without runtime execution, mark it **Unverified**.
- Where upstream lineage is identifiable, note whether a finding originates from `n64decomp/perfect_dark`, `fgsfdsfgs/perfect_dark` (`port` or `port-net`), `jonaeru/perfect_dark` (`allinone`), or is authored in Perfect Dark 2 itself.
- When reviewing any data structure, loop, packet, buffer, or UI element, ask whether it scales with host / server capability or encodes a fixed ceiling inherited from N64 / `port-net`-era assumptions. Fixed ceilings are findings by default in Perfect Dark 2, regardless of current functional correctness.
- When reviewing any asset load, treat the Catalog as the expected entry point. Any bypass is a finding. When reviewing any content-use path, verify mod content and native content flow through the same interfaces; any divergence is a finding unless justified.
- Perform Phase 2.5's Upstream-Shape and Data-Layout scans before concluding Phase 3, regardless of subsystem scope. These scans catch a defect class (struct / stride / type-migration mismatches) that workflow-level review routinely misses, and that has already produced rendering corruption in PD2.

---

## EVIDENCE STANDARDS (MANDATORY)

For every finding, include:

- **Severity**: Critical / High / Medium / Low
- **Area**: Design Fit / Code Quality / Security
- **Confidence**: Confirmed / Probable / Unverified
- **Location**: Exact file path(s), function / struct / module / menu / Catalog-field / Grid-endpoint name(s), and line number(s) when available
- **Lineage** (if identifiable): Authored-in-PD2 / Inherited-from-port / Inherited-from-port-net / Inherited-from-allinone / Inherited-from-n64decomp / Unknown
- **Pillar Impact** (if applicable): Catalog SOT / Mod-native Parity / Modding Pipeline / Grid Trust / Online-mode Boundary / Dedicated-server Boundary / Scaling Ceiling / None
- **Defect Class** (if applicable): Layout / Lineage-Shape / Scaling / Determinism / Trust-Boundary / Supply-Chain / None
- **What We Found** (plain language)
- **Why It Matters** (trust, fairness, stability, player experience, architectural discipline)
- **Risk If Not Fixed** (concrete impact — crashes, cheating, desync, data loss, IP exposure, scaling wall, etc.)
- **Recommendation** (specific, actionable)
- **Cross-System Changes Required**: Yes / No / Maybe (e.g., does this touch networking + menu + Catalog + save?)
- **Blocks Intended Outcome?**: Yes / No / Partially (for design-fit findings)

If line numbers are not available, state that clearly and use the most precise location reference possible.

---

## REPORT FORMAT (RETURN IN THIS EXACT STRUCTURE)

# Deep Project Audit Report — Perfect Dark 2

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose
- [What the project appears to do]

### Likely Player & Contributor Types
- [Type 1]
- [Type 2]

### Core Workflows / Gameplay Loops (Inferred)
1. [Workflow]
2. [Workflow]
3. [Workflow]

### Upstream Lineage Observations
- [What appears inherited from n64decomp / port / port-net / allinone vs. authored in PD2]

### Pillar Observations (Catalog / Mod Parity / Modding Pipeline / Grid / Online Mode / Dedicated Server)
- [Brief state of each pillar as observed in the code]

### Assumptions / Unknowns
- [Assumption]
- [Missing information preventing stronger validation]

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### System & Workflow Validation Findings

Group findings by system / screen / feature. Menus, input authority, Catalog, content loading, modding pipeline, Grid, online mode, and dedicated server each get their own group.

#### Critical Issues
- [Finding title]
  - Severity:
  - Confidence:
  - Location:
  - Lineage:
  - Pillar Impact:
  - Intended Outcome:
  - Current Behavior (from code evidence):
  - Gap / Failure Mode:
  - Why It Matters:
  - Risk If Not Fixed:
  - Recommendation:
  - Cross-System Changes Required:
  - Blocks Intended Outcome?:

#### High Priority Findings
- [Repeat same format]

#### Medium Priority Findings
- [Repeat same format]

#### Low Priority Findings
- [Repeat same format]

### Missing / Incomplete Features Blocking Success
- [Item]: [Why this prevents the project from achieving likely intended outcomes]

### Positive Observations
- [Good practice]: [Brief description]

---

## 3) Code Quality Review

### Critical Issues
- [Finding title]
  - Severity:
  - Confidence:
  - Area:
  - Location:
  - Lineage:
  - Pillar Impact:
  - What We Found:
  - Why It Matters:
  - Risk If Not Fixed:
  - Recommendation:
  - Cross-System Changes Required:

### High Priority Findings
- [Repeat same format]

### Medium Priority Findings
- [Repeat same format]

### Low Priority Findings
- [Repeat same format]

### Positive Observations
- [Good practice 1]: [Brief description]

---

## 4) Security, Trust & Privacy Review

### Critical Issues
- [Finding title]
  - Severity:
  - Confidence:
  - Area:
  - Location:
  - Lineage:
  - Pillar Impact:
  - What We Found:
  - Why It Matters:
  - Risk If Not Fixed:
  - Recommendation:
  - Cross-System Changes Required:

### High Priority Findings
- [Repeat same format]

### Medium Priority Findings
- [Repeat same format]

### Low Priority Findings
- [Repeat same format]

### Positive Observations
- [Good practice]: [Brief description]

---

## 5) Cross-Cutting Risks & Architecture Concerns

Issues that impact multiple parts of the project (trust model design, shared validation gaps, determinism / sync fragility, menu↔network coupling, Catalog↔loader coupling, Grid↔match coupling, client↔dedicated-server boundary leakage, duplicated logic, weak observability, fragile P2P patterns, platform-portability traps, mod-loader attack surface).

- [Concern 1]: [Description] → [Recommendation]
- [Concern 2]: [Description] → [Recommendation]

---

## 6) Verification Limits (Static Analysis vs Runtime)

Separate:

- **Confirmed by code evidence**
- **Probable issues inferred from patterns**
- **Unverified risks requiring runtime testing / network captures / environment access**

Also list what would be needed to fully validate behavior (e.g., multi-peer test lobby across varied NAT types, packet captures, Switch hardware, corrupted-save fixtures, malformed-mod fixtures, fuzzing harness, deterministic replay logs, sample match traces, controller-hotplug test rig, dedicated-server multi-tenant test fixture, Grid sandbox endpoint).

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: [1-10] with brief justification
**Code Quality Score**: [1-10] with brief justification
**Security & Trust Score**: [1-10] with brief justification
**Architectural Discipline Score** (Catalog SOT, mod parity, pillar integrity): [1-10] with brief justification

### Total Findings by Severity
- Critical: [count]
- High: [count]
- Medium: [count]
- Low: [count]

### Total Findings by Confidence
- Confirmed: [count]
- Probable: [count]
- Unverified: [count]

### Total Findings by Lineage
- Authored in PD2: [count]
- Inherited from port / port-net / allinone / n64decomp: [counts]
- Unknown: [count]

### Total Findings by Pillar Impact
- Catalog SOT: [count]
- Mod-native Parity: [count]
- Modding Pipeline: [count]
- Grid Trust: [count]
- Online-mode Boundary: [count]
- Dedicated-server Boundary: [count]
- Scaling Ceiling: [count]

### Top Scaling Ceilings Identified
- [Location / constant / format]: current ceiling → scaling bottleneck (CPU / bandwidth / memory / UI)

### Top Data-Layout Integrity Breaches (Phase 2.5)
- [Location]: declared type / current sizeof → allotted storage / consumer assumption → downstream impact → persisted? (yes/no)

### Top Catalog / Mod-parity Breakages
- [Location]: bypass or asymmetry description

### Top 5 Action Items (Highest Impact First)
1. [Action]
2. [Action]
3. [Action]
4. [Action]
5. [Action]

### Must-Fix Before Public Release
- [Item]

### Estimated Effort to Remediate Critical / High Issues
- [Rough estimate with assumptions]

---

## REFERENCE RESOURCES (CONSULT AS NEEDED)

Use these when evaluating conventions, idioms, and expected patterns. Citing them in findings is welcome when relevant.

### Perfect Dark / N64 decomp lineage
- n64decomp/perfect_dark: https://github.com/n64decomp/perfect_dark
- PD decomp GitLab upstream: https://gitlab.com/ryandwyer/perfect-dark
- PD decomp docs (Challenge 7 memory-corruption writeup, etc.): https://github.com/n64decomp/perfect_dark/tree/master/docs
- fgsfdsfgs/perfect_dark (modern port parent): https://github.com/fgsfdsfgs/perfect_dark
- fgsfdsfgs/perfect_dark `port-net` branch (NetPlay parent): https://github.com/fgsfdsfgs/perfect_dark/tree/port-net
- jonaeru/perfect_dark (AllInOne parent, per-arena mod-swap): https://github.com/jonaeru/perfect_dark
- jonaeru wiki (mod install conventions): https://github.com/jonaeru/perfect_dark/wiki/How-to-Play-Mods
- cylonicboom/perfect-dark (adjacent unofficial patches, useful divergence reference): https://github.com/cylonicboom/perfect-dark

### N64 decompilation community
- n64decomp org: https://github.com/n64decomp
- decomp.me (collaborative matching-decomp scratchpad): https://decomp.me/
- N64Brew Wiki (hardware, libultra, RSP / RCP reference): https://n64brew.dev/wiki/Main_Page
- Sibling projects: n64decomp/007 (GoldenEye), n64decomp/sm64, n64decomp/libreultra

### SDL2
- Homepage: https://www.libsdl.org/
- Wiki: https://wiki.libsdl.org/SDL2/FrontPage
- GameController API: https://wiki.libsdl.org/SDL2/CategoryGameController
- SDL_GameControllerDB (mapping reference): https://github.com/mdqinc/SDL_GameControllerDB

### Dear ImGui
- ocornut/imgui: https://github.com/ocornut/imgui
- Wiki home: https://github.com/ocornut/imgui/wiki
- Getting Started: https://github.com/ocornut/imgui/wiki/Getting-Started
- Examples / backend integration (SDL2 + OpenGL3 is the relevant pairing): https://github.com/ocornut/imgui/blob/master/docs/EXAMPLES.md
- FAQ: https://github.com/ocornut/imgui/blob/master/docs/FAQ.md

### Game programming patterns & architecture
- Game Programming Patterns (Robert Nystrom, free online): https://gameprogrammingpatterns.com/
- Handmade Hero archives (low-level C game architecture): https://handmadehero.org/
- Our Machinery / Niklas Gray engine architecture writings (archived): https://ruby0x1.github.io/machinery_blog_archive/

### Networking & P2P / netcode
- Gaffer On Games (Glenn Fiedler — UDP, reliability, networked physics, snapshot interpolation, client-side prediction): https://gafferongames.com/
- Valve / Source multiplayer networking overview: https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking
- GDC Vault netcode talks: https://www.gdcvault.com/
- ENet docs: http://enet.bespin.org/
- libjuice (ICE-lite for P2P / NAT traversal): https://github.com/paullouisageneau/libjuice

### Dedicated server & multi-tenant game hosting
- "Server authoritative multiplayer" architecture patterns (Gaffer On Games, Overwatch Netcode talks at GDC Vault)
- Plugin-based server architecture references (SRCDS, Minecraft server plugin APIs, Nakama server plugin model): https://heroiclabs.com/docs/
- OWASP hardening guidance for game servers (general app-server hardening carries over)

### Secure C / C++ & memory-safety references
- CERT C Coding Standard: https://wiki.sei.cmu.edu/confluence/display/c
- Compiler sanitizers (ASan, UBSan, TSan) — enable in CI for fuzzing / test harnesses
- AFL++ / libFuzzer for packet and asset fuzzing

### Mod ecosystem & modding pipeline references
- ModDB Perfect Dark page: https://www.moddb.com/games/perfect-dark
- Shootersforever (historical PD / GoldenEye modding hub): https://www.shootersforever.com/
- GoldenEye Vault: https://www.goldeneyevault.com/
- Reproducible-builds.org (determinism techniques applicable to mod packaging): https://reproducible-builds.org/
- The Update Framework (TUF — signed distribution for mod supply chains): https://theupdateframework.io/

### Game porting & preservation
- RetroReversing: https://www.retroreversing.com/
- Hackaday retrospective on Perfect Dark's decomp-to-PC pipeline: https://hackaday.com/2023/11/05/perfect-dark-recompiled/

---

## ANALYSIS GUIDELINES

- Be specific: Reference exact file paths, function names, struct names, menu screen names, Catalog field names, Grid endpoint names, and configuration keys when possible.
- Be actionable: Every finding must include a concrete recommendation.
- Be thorough: Check game logic correctness, menu / input edge cases, network edge cases, Catalog invariants, mod / native symmetry, modding pipeline determinism, Grid trust, online-mode boundary, dedicated-server isolation, and failure modes.
- Be realistic: Distinguish release-blocking issues from improvements.
- Flag assumptions: If information is missing, say so explicitly.
- Flag lineage: When a finding is clearly inherited from an upstream parent, say so — but note that in Perfect Dark 2, inheritance is not a justification for keeping the defect. The remediation path differs (upstream PR, local override, re-architecture), but remediation is expected.
- Flag scaling ceilings: Hardcoded player counts, fixed-size per-peer buffers, and O(n²) loops across peers are findings even when the code currently works. PD2's target is host / server-bound scaling, and anything that silently caps it must be surfaced.
- Flag pillar erosion: Any code that bypasses the Catalog, treats mods asymmetrically from native content, breaks modding-pipeline determinism, accepts Grid payloads without validation, leaks state across the online-mode boundary, or leaks PD2 semantics into the dedicated-server core is a finding regardless of current functional correctness.
- Context matters: Evaluate severity relative to the project's likely purpose (free, community-run, P2P-multiplayer-and-dedicated-server game built on a decomp foundation with a first-class modding pipeline and social Grid) and its players.
- Do not provide implementation code unless a tiny snippet is necessary to explain a fix.
- Return findings only.

---

## NOW ANALYZE THE PROJECT

Provide the full analysis in the exact report format above based on the code and context shared next. Assume anything pasted is part of the Perfect Dark 2 fork unless explicitly labeled as upstream reference material.
