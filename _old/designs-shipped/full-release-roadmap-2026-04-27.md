# Full Release Roadmap (2026-04-27)

> Single-doc plan covering every architectural pillar and infrastructure system on the path to PD2 v1.0.0. Synthesizes the strategic design docs in `context/designs/`, the active audits in `context/audits/`, and the live state captured in `context/roadmap.md`, `context/infrastructure.md`, `context/session-log.md`, `context/tasks-current.md`, and `context/bugs.md` as of S480 (post-release v0.0.171, 2026-04-27).
>
> Authored under Mike's directive: "Make a plan for how we can get to a full release so we can follow that. It will include every architecture pillar and infrastructure system we've planned and discussed."
>
> **All 11 architectural decisions in Section E resolved 2026-04-27** (Mike approved 5 directly, 6 by delegated authority). See E.0 for the resolution table.
>
> No em-dashes anywhere (PowerShell hygiene). No implementation in this session, plan only. Status markers: SHIPPED / IN-FLIGHT / QUEUED / PLANNED / DEFERRED / OPEN-DECISION / RESOLVED.

---

## A. Vision

### A.1 What "full release" means

Mike's stated direction (per project memory + session log): **PD2 is a Perfect Dark 2 fan port today, an engine platform tomorrow, and an open UGC platform after that.** The full release is the convergence point where:

1. Every base-game mode (Solo Campaign, Combat Simulator, Co-Op, Counter-Op) is stable, content-complete, and competitively playable.
2. Every architectural pillar that powers the platform is in place: catalog system, asset provider, manifest/match pipeline, mod ecosystem, online connectivity, dev tooling.
3. The creative tools (Skin Editor, Audio Mod, Theme Tool, Map Importer, Forge / The Grid level editor, future Studio platform) let players make and share content without engine knowledge.
4. The connectivity model (P2P friend-mesh + presence + voice + theater) supports a small social platform without a dedicated central server, while leaving the door open to federation later.
5. Quality gates are systematic, not ad hoc: pd-tests cohorts, manifest discipline, crash-breadcrumb instrumentation, super-audit cadence.

### A.2 Baseline release vs later milestones

The pre-existing release ladder in `context/roadmap.md` ramps from v0.1.0 "Foundation" through v1.0.0 "Forge" with seven named milestones. This roadmap **keeps that ladder** but reframes it around the architectural pillars that gate each step, rather than around the original feature buckets which have been overtaken by the catalog universality work, the .pdmod migration, the connectivity pivot, and the input-framework consolidation.

The semantic shift in this roadmap:

- Several "v0.x" items in the original ladder are SHIPPED as of S480 (D5 Phase 5, D6 Phase 3, D7, D8, D-MEM, D-STAGE, B-12, MSP, R-1 through R-4, D3R 1-11, A-1-A-7, S-1-S-9, L3, L5).
- The remaining work clusters into five gates: stability/content, architectural foundation, content systems and tools, online + UGC, and polish.
- One pillar from the original ladder has structurally changed: the dedicated server (`PerfectDarkServer.exe`) was retired from the build/release as of S475 in favour of in-client listen-host plus the connectivity pivot. The "game-agnostic dedicated server" pillar (audit DS-1) is therefore **deferred or replaced** depending on Mike's call (see Section E).

### A.3 The four release identities

| Tag | Identity | Scope |
|---|---|---|
| **v0.1.0 Foundation** | "PD2 boots clean, plays clean, mods clean" | Stability + content correctness + the architectural foundation that keeps shipping more content cheap. |
| **v0.3.0 Connected** | "PD2 plays with friends" | Connectivity Phase 1 + 2, public mods page, drop-in/drop-out co-op, NAT matrix verified. |
| **v0.5.0 Studio** | "PD2 lets you build" | Forge content-complete, Studio Platform foundations, mod-pack and theme-bundle authoring polished. |
| **v1.0.0 Forge** | "PD2 is a platform" | Visual scripting, voice chat, theater, federation hooks, all release-gate audits cleared. |

(The v0.2 / v0.4 / v0.6 mid-points retain their original meanings as transitional but are no longer named gates.)

---

## B. Pillars and infrastructure systems

This section enumerates every pillar visible across the design docs, audits, infrastructure tracker, and active task list. Status is normalized: SHIPPED / IN-FLIGHT / QUEUED / PLANNED / DEFERRED / OPEN-DECISION. Origins are linked.

### B.1 Engine Core

| # | Pillar | Status | Scope |
|---|---|---|---|
| E1 | **Asset Catalog** (ADR-003) | SHIPPED | String-keyed FNV-1a hash table + open addressing; catalog ID strings (`namespace:readable_name`) at all interface boundaries; net_hash deprecated. Single source of truth for asset identity. |
| E2 | **Asset Catalog full-pipeline data migration** | QUEUED | Mike's directive: extend ext fields to ALL Layer A data (damage, fire_rate, AI lists, animations), not just selectors. Estimated 100+ sites. Retire static Layer A arrays. Companion to E1 universality sweep. |
| E3 | **Catalog universality sweep Phase 2** | IN-FLIGHT | Selector pool migration: Weapons, Scenarios, Music tracks, Bot profiles, CI bios. 22 sites in 6 commits + pd-tests. No wire/save bumps. (audit `catalog-universality-sweep-2026-04-27.md`) |
| E4 | **Asset Provider (Direct File Access)** | SHIPPED-PHASES-1-3 / QUEUED-PHASE-4 | `asset_provider_t` vtable with `RomProvider` / `FileProvider` / `ArchiveProvider`. Phases 1+2+3 shipped (S326, S346, S377). Phase 4 (filenum retirement, ~23 game-code sites) blocked on three prerequisite API migrations: `assetGetSize` handle-aware, `modeldefLoad` handle-aware, `MENUMODELPARAMS_SET_HANDLE`. |
| E5 | **Session Catalog (u16 session refs)** | SHIPPED-LOAD-PATH / DEFERRED-WIRE-MIGRATION | Phase 6 SP load manifest done (S94). Phase 5a-5f load path migration done. Phase 3 wire migration to fully session-ref-only weapon/model/scenario fields landed at v30/v31/v32. Future: complete catalog ID strings on all wire surfaces. |
| E6 | **Manifest Architecture (3 paths)** | SHIPPED | MP / SP-pre-load / SP-post-setup-scan paths. Manifest clear discipline + late-add logging + diff/apply. SP two-phase pre/post setup scan. (`manifest-architecture.md`, FIX-B.1 finalized S323 Batch H.) |
| E7 | **Match Startup Pipeline (MSP A-F + L5)** | SHIPPED | 7-phase handshake: GATHER -> MANIFEST -> CHECK -> TRANSFER -> READY GATE -> LOAD -> SYNC. All phases done plus L5 match lifecycle (co-op manifest, protocol v35, match_seed). |
| E8 | **Memory Modernization (D-MEM M0-M6)** | SHIPPED | M0 verbose log cleanup, M1 named constants, MEM-1/2/3 ref-counted catalog load, M2 stack->heap, M3 IS4MB collapse, M4 ALIGN16 (later partially reverted in S384 for pointer alignment correctness), M5 separate pool regions, M6 thread safety. |
| E9 | **Stage Decoupling (D-STAGE 1-3)** | SHIPPED | Heap-allocated `g_Stages`, dynamic table, domain separation (stage-table vs solo-stage vs stagenum). |
| E10 | **Participant System (B-12 Phases 1-3)** | SHIPPED | `g_MpParticipants` is sole slot store. Protocol v37 removed `chrslots` u64 bitmask. Wire helpers: `mpParticipantsEncodeActiveMask` / `mpParticipantsDecodeActiveMask`. |
| E11 | **Capsule Collision (D2b)** | SHIPPED-CORE / DEFERRED-POLISH | `capsule.c` sweep; replaces N64 `cdTestVolume` hacks. Stationary jump + stair-step working. Slope-AABB adaptation and ceiling-jump-through deferred (per directive). |
| E12 | **Bot Jump AI (D2c)** | PLANNED | Depends on D2b stability. Not started. |
| E13 | **Spawn System (L2 + B-134)** | SHIPPED | `spawnpool.c` L1-L4 cascade with `spawn_select_tier_t` enum. Capsule-radius threshold (30.0f) for raycast budget. Same-tick reservation bitset. Wall-probe + neighbour-room ground check. |
| E14 | **Random / Fiesta semantics** | IN-FLIGHT | Per Mike's directive notes; partial. Random meta selectors migrated to catalog (S473 maps step, S471 weapons, S472 bodies, S470 heads). Fiesta semantics not fully verified. |
| E15 | **Save Format Migration** | SHIPPED-V1->V2 / QUEUED-CASES | `MPSETUP_VERSION 1->2` at S468 alongside Goldfinger weapon cull (clamp rule documented). Future bumps: catalog universality data migration may require it; SAVE-1 audit calls for save integrity protection (HMAC). |
| E16 | **Wire Format Versioning** | SHIPPED-V44 / QUEUED-CASES | `NET_PROTOCOL_VER` at v44 (S468 hygiene bump). Future bumps coordinated per audit findings: SEC-3 cookie if not yet, P4-B if approved, SEC-7 already at v38, room password v38. |
| E17 | **AllInOne Lineage Cull** | IN-FLIGHT | Phase 1 audit done; Phase 2 in flight. Removes 8 Goldfinger weapons + 28 mod-derived arenas (75->47). Section H decisions resolved 2026-04-26. (`audits/allinone-cull-audit-2026-04-26.md`) |

### B.2 Game Modes / Content

| # | Pillar | Status | Scope |
|---|---|---|---|
| C1 | **Solo Campaign (M1)** | SHIPPED | M1.1 mission select redesign, M1.2 solo flow + pause, M1.3 options sub-menus. Full mission flow verified per audit. |
| C2 | **Combat Simulator (M2)** | SHIPPED | M2.1 UI catalog audit, M2.2 match flow + endscreen + auto-save. |
| C3 | **SP-stage MP-readiness (Option E)** | OPEN-DECISION | B-228 reopened. P5 stage relax is NOOP without overlay of SP setup transport-prop blob (LIFT/ESCASTEP) into MP setup. Highest-impact stages: Airbase, Attackship, AirForceOne, Infiltration, CITraining, Defection, Defense, Investigation, Deepsea, SkedarRuins. (`audits/sp-stage-mp-readiness-2026-04-24.md`) |
| C4 | **Counter-Op (D14a)** | PLANNED-V0.6.0 | NPC possession mechanic. Room-type support already in network-architecture.md. Per-session role-assign UI (L-5 setup screen) planned but not built. |
| C5 | **Co-op polish (D12)** | PLANNED-V0.6.0 | Drop-in/drop-out (L-6), telefrag prevention, server-side coop manifest rebuild. Some pieces shipped opportunistically (S264 endscreen team). |
| C6 | **Spectator Mode (D10)** | DESIGN-DRAFT-V0.6.0 | Connectivity doc Section 5 unifies Spectator (live) + Theater (saved replay) under one subsystem. Wire scaffolding present at v42 (`CLC_SPECTATE_REQUEST 0x16`, `SVC_SPECTATE_ACK 0x6a`, `SVC_STATE_FRAME 0x6b`). Implementation pending. |
| C7 | **Theater (saved-match playback)** | PLANNED-V1.0.0 | Connectivity Phase 6. Replay capture format + playback UI. |
| C8 | **The Grid (Forge level editor)** | F0+POLISH-SHIPPED / F1-F8-PLANNED | Free-fly Dr Carroll mode + HUD shell + main-menu Forge entry + map-variant editing + bot tab shipped (S307 + S313). F1-F8 design ready (`forge-level-editor-2026-04-16.md`). |
| C9 | **Listening Rooms** | PLANNED-V0.4.0 | Connectivity Phase 4 - shared-music sessions over Phase 2 social channel. Wire decided (PDSHR port 27109). |

### B.3 Online Connectivity

| # | Pillar | Status | Scope |
|---|---|---|---|
| N1 | **ENet Protocol** | SHIPPED-V44 | UDP, server-authoritative with client prediction. 60Hz tick. Statically linked. |
| N2 | **NAT Traversal D8** | SHIPPED-PHASE-1 / EXTENDED | STUN client (RFC 5389 minimal), 2-probe NAT type detection, symmetric hole-punch, relay fallback, NAT diagnostics overlay. Connectivity doc supersedes scope to a 5-tier escalation (Direct/STUN/UPnP/ICE/TURN) shipped in Phase 1. |
| N3 | **Connect Codes (4-word phonetic)** | SHIPPED | Sentence-based, no raw IP in any UI. `connectCodeEncode`/`Decode` host-byte-order. |
| N4 | **Friend Presence + Identity (Ed25519)** | IN-FLIGHT-PHASE-1 / QUEUED-P1.J/P1.K | Network-agnostic identity via `SHA256(pubkey \|\| domain)[:4]`. TOFU pubkey caching. 5-min endpoint TTL. P1.J in-match invite + P1.K NAT diagnostics harness deferred. (`audits/connectivity-phase1-decisions.md`) |
| N5 | **Public Mods Page + Player Profile** | PLANNED-PHASE-3 | Full mod browse + profile statistics + uploaded mod gallery. Connectivity Section 7. |
| N6 | **Voice Chat (libopus)** | DESIGN-DECIDED-PHASE-5 | Push-to-talk over UDP port 27108 (PDVOC). 16kHz mono PCM, 20ms slices, ~24 kbps/stream. Codec + wire format decided; `voiceTick` + `voice_wire.c` + UI indicator pending. (`audits/connectivity-libopus-decision-2026-04-25.md`) |
| N7 | **Match Startup Handshake** | SHIPPED | (covered by E7) |
| N8 | **Interest Management (PVS culling, SEC-8/9)** | DESIGN-DRAFT | `interest-management-replication.md` 4-phase plan: room/stage relevance -> radius/grid -> PVS -> cadence throttling. Implementation open. Required for >8 client scaling. |
| N9 | **Lobby / Room System (R-1 to R-4 + R-5)** | SHIPPED-R1-R4 / PLANNED-R5 | R-1 foundation, R-2 demand-driven rooms, R-3 sync protocol, R-4 match start + S253 settings/playlist additives. R-5 server GUI redesign with Players + Rooms panels and operator actions planned. |
| N10 | **Master Server / Federation (D16)** | PLANNED-V0.4.0 | 4-phase plan in network-architecture.md §7. 750 LOC total. Mesh peer discovery + cross-server matchmaking + signed transfer tokens + trust levels. |
| N11 | **Listen Mode vs Dedicated** | LISTEN-SHIPPED / DEDICATED-DEFERRED | Listen-host inside game client (`g_NetDedicated == 0`) shipped. Dedicated server retired from build/release as of S475 per project memory. Game-agnostic plugin ABI (P4-A) is doc-complete; P4-B/C deferred until Mike's call on whether to revive dedicated track. |
| N12 | **Drop-in/Drop-out Co-op (L-6)** | PLANNED-V0.3.0 | Client-side drop-in prompt + server-side coop manifest rebuild. Building blocks landed (gap-A and gap-B in S303). |
| N13 | **In-client Host (H-1 P3-A/P3-B)** | SHIPPED | `pdgui_menu_network` listen host route + `pdgui_lobby` for `NETMODE_SERVER && !g_NetDedicated`. Net.Server.Port pd.ini + CLI override. (S421) |

### B.4 Server / Trust / Security

| # | Pillar | Status | Scope |
|---|---|---|---|
| S1 | **SPF-1 Hub / Room / Identity / Phonetic** | SHIPPED | Thin platform layer (S47d). Identity profile is authoritative name source (B-26). |
| S2 | **Server Admin / RCON (MASTER-C2a, SEC-2)** | SHIPPED | Hashed token (SHA-256 domain-salted), CLI/INI loadout, plaintext zero-scrub. `CLC_ADMIN 0x15` / `SVC_ADMIN 0x68`. (S393) |
| S3 | **Persistent Bans (MASTER-C2c/d)** | SHIPPED | `$S/bans.ini`, atomic Windows save via `MoveFileExA + _commit`. IPv4-mapped IPv6 compare. (S393, S416) |
| S4 | **Preserved-Player Cookie (MASTER-C3, SEC-3)** | SHIPPED | 16-byte server-issued cookie + name match for reconnect. Constant-time compare. (S393) |
| S5 | **Room Passwords (SEC-14)** | SHIPPED-V38 | Hashed at create time (domain-salted SHA-256). `roomCheckPassword` constant-time. |
| S6 | **Server Info Query Token Challenge (SEC-7)** | SHIPPED-V38 | 2-stage challenge handshake to neutralize amplification. |
| S7 | **netbufReadStr NUL-terminate (SEC-1)** | SHIPPED | All 40+ callsites covered by single fix. (S391) |
| S8 | **netbufWriteGset field-wise (LAYOUT-1)** | SHIPPED | Per-field serialize replaces raw memcpy. (S392) |
| S9 | **Mod SHA-256 Mandatory (SEC-5)** | OPEN | Audit found verification opt-in. Required mandatory. |
| S10 | **Updater Ed25519 Signing (SEC-6)** | OPEN | Single trust root via GitHub channel today. Ed25519 signed releases needed for trust separation. |
| S11 | **MP Stat Integrity (SAVE-1)** | OPEN | MP stats trust client. Server-side validation needed. |
| S12 | **pd.ini Address Validation (LAYOUT-2)** | OPEN | `LastJoinAddr` reused without validation. `connectCodeDecode` or IP-parser gate at load. |
| S13 | **Game-Agnostic Server / Manifest Broker (P4-A/B/C, DS-1)** | DOC-DONE-P4-A / DEFERRED-P4-B+ | Plugin ABI ADR done. P4-B (manifest broker spike) and P4-C (`server_stubs.c` shrink) pending Mike's call to revive dedicated server track or retire pillar. |
| S14 | **Hardcoded Player Cap Tripwire (AUDIT-M4)** | OPEN-QUICK | `_Static_assert(MAX_PLAYERS + MAX_BOTS <= 64)`. 5 min. |

### B.5 Mod Ecosystem

| # | Pillar | Status | Scope |
|---|---|---|---|
| M1 | **Component Mod Architecture (D3R 1-11)** | SHIPPED | Per-asset folders + INI manifests + name-based resolution + soft dependencies + server-authoritative distribution. ADR-002 + component-mod-architecture.md. |
| M2 | **.pdmod Unified Format (M-1 to M-4)** | SHIPPED-PENDING-OPERATOR-VERIFY | Single zip-archive format. Loader via VFS mount (no on-disk extraction). Property Handler DLL projects metadata to Windows Explorer. Auto-migration from folder mods on first boot. (`pdmod-unified-mod-format.md`, `audits/pdmod-verification-matrix-2026-04-25.md`) |
| M3 | **Property Handler DLL** | SHIPPED | `tools/pdmod_prophandler/`, projects Title/Authors/Comment/Version to Windows shell. |
| M4 | **Mod Network Distribution (D3R-9)** | SHIPPED | PDCA archives, zlib chunks, crash recovery, download-prompt UI, ordering guard, 256MB cap. |
| M5 | **Mod Pack Export/Import (D3R-10)** | SHIPPED | PDPK format, zlib, Modding Hub tab. |
| M6 | **Mod Apply UX** | SHIPPED | In-place modal, no forced title restart, centered progress popup, green-tint success. |
| M7 | **Trust Gate (mods/shared/)** | SHIPPED-CORE | Reserved top-level names skipped. Inbox archives never auto-mounted. Cross-platform shell metadata pending. |
| M8 | **Audio Mod System (A-1 to A-7)** | SHIPPED | Catalog ext, mod music stream, UI, soundtrack extension, pack creation, multi-format import, network sync. |
| M9 | **Skin Editor (S-1 to S-9)** | SHIPPED | Canvas, live 3D preview, save-as-mod, image import, quantize/dither, blend modes, UV wireframe, network sync. |
| M10 | **Map Import Pipeline (L3 + M-5/M-6/M-7)** | SHIPPED | PD-native importer, Modding Hub UI, retroactive validation, smoke sweep. |
| M11 | **Theme Tool / Theme Editor** | SHIPPED | Palette + nine-slice + menu-style + font bundle. Save-as-mod. Mod-pack ready via `theme.json` keys. |
| M12 | **Theme Bundle as .pdmod (M-2.x)** | DESIGN-READY | Plumbing for `menuStyle` / `font` keys done. .pdmod-saveable bundle pending. |
| M13 | **Bot Customizer (D3R-8)** | SHIPPED | Trait editor, `botvariant.c/h`, save-as-preset, hot-register. |
| M14 | **Nine-Slice Chrome Tool** | SHIPPED | Modding Hub tool. Image import, ruler sliders, save-as-mod, transform pipeline. |
| M15 | **Font Mod (Font Import as Mod)** | SHIPPED | `mods/Fonts/<slug>/*.ttf`, `Video.FontId` pd.ini, Settings dropdown. |
| M16 | **Forge Level Editor (F0-F8)** | F0+POLISH-SHIPPED / F1-F8-PLANNED | (covered by C8) |
| M17 | **Studio Platform (S1-S14)** | DESIGN-READY | Asset import + JSON weapons + map editor + visual scripting integration. ~13,400 LOC over ~35 sessions. (`studio-platform-design.md`) |
| M18 | **Visual Scripting Node Taxonomy** | DESIGN-COMPLETE | Pin types + ten node categories + proof graphs for 5 base-game weapons + full mission flow. Feeds Studio S5 + S7. (`visual-scripting-node-taxonomy.md`) |

### B.6 UI / UX

| # | Pillar | Status | Scope |
|---|---|---|---|
| U1 | **ImGui Menu System (D5 P1-P10 + 254 dialogs)** | SHIPPED | ImGui is sole menu system per P10 D5.7 (S184). Legacy `menugfx` retained only for non-menu GBI utility callers. |
| U2 | **D5 Phase 4 Theme System** | SHIPPED | Theme loader + base-game template mod + theme editor + UI texture overrides (S351). |
| U3 | **D5 Phase 5 Lobby Scene** | SHIPPED | Per-player portrait thumbnails + state badges + join fade-in + hover preview + drop shadow + team-color border. (S352, S356) |
| U4 | **Input Action Map (M0.2)** | SHIPPED | 51-action coverage, IMC stack, action map -> ImGui nav bridge. (`pdguiDriveImGuiNav`) |
| U5 | **Contextual Input Schemes (J-1/2/3)** | SHIPPED | Mission XOR CombatSim split, Vehicle IMC mount/dismount, hold-vs-tap discrimination, ForgeSession/Forge IMCs. (S458, `contextual-input-schemes.md`) |
| U6 | **Input Authority Discipline (Priority K)** | IN-FLIGHT | K-b1 / K-c / K-d landed. K-b2 (`menuhandlerAcceptMission` audit) / K-b3 (defensive pop deletes) / K-e (mark Issues 2/3 STRUCTURALLY-RESOLVED) / K-f (methodology one-pager) remaining. (`audits/input-authority-discipline-2026-04-25.md`) |
| U7 | **Menu Stack Architecture (M-1 to M-23)** | SHIPPED-MOST / DEFERRED-M-24 | Tier 1-5 punch list nearly complete. M-24 strict-tree assertion deferred. (`menu-stack-architecture.md`) |
| U8 | **Flat Menu Navigation (7 rules)** | SHIPPED-FULLY-CONFORMING | All 27 menus conform. Rule 7 RStick scroll lives in `pdguiDriveImGuiNav`. L-fix-4/5/6 polish queued. (`audits/flat-menu-navigation-audit-2026-04-25.md`) |
| U9 | **Per-Action Hold Override** | SHIPPED | HoldMsOverrides UI (S409), terminal extra hold tunable. |
| U10 | **Controller Bindings + Visual Mapper** | SHIPPED | Settings -> Controls -> Controller map split-zone bind table, gamepad silhouette, per-stick tuning, drag-drop. (S400, S407, S409, S410) |
| U11 | **Hold Ring (use prompt)** | SHIPPED | Per-target use-hold tuning + shared hold-progress ring API. (`pdgui-hold-ring.md`) |
| U12 | **Active Menu Radial (weapon/gadget wheel)** | DESIGN-NOTED | Rendered via `amRender`, not ImGui hold ring. (`activemenu-radial-architecture.md`) |
| U13 | **Per-Agent Settings (prefs_agent.ini)** | SHIPPED-CORE | `[Audio]` block migrated. Future: more per-agent surfaces. (`theme-bundle-and-per-agent-settings-2026-04-16.md`) |
| U14 | **UI Chrome Style (procedural title-bars)** | SHIPPED | 5 styles (Classic / Solid / Vertical Bars / Scanlines / Diagonal). |
| U15 | **HUD Layer Order** | SHIPPED | Render ordering + context-aware gating. Interact prompt menu-gated. (`hud-layer-order.md`) |
| U16 | **HUD Killfeed** | SHIPPED | Bot-bot kills + per-mode + per-weapon. Network broadcast (S353b). |
| U17 | **Achievement Unlock Toasts** | SHIPPED | Foreground drawlist slide-in. Polled on solo + MP endscreen. (S378) |
| U18 | **Stats Viewer (D6 Phase 3)** | SHIPPED | Combat / Accuracy / Solo / World Interaction sections. Hit% column. |
| U19 | **Pause Menu + Modal Confirms** | SHIPPED | End Game / Abort Mission / Cheats Confirm / Delete Agent / Leave Room / Scenario Delete all `BeginPopupModal` with 5-frame focus + 3-frame debounce. |
| U20 | **Cinematic / Cutscene Transitions** | OPEN-POLISH | Transition glitches surfaced in playtests. Cutscene IMC layer needed before transition sweep is meaningful (per directive). |
| U21 | **Mission Select / Agent Select / Room Screen** | SHIPPED-WITH-RECENT-POLISH | Progressive focus (Tier 4 M-18-M-21), three-bug room-screen batch (S474), portrait pipeline. |
| U22 | **PD Authentic Styling** | SHIPPED | Theme palette + tint helpers (`pdguiImU32/Vec4TitleGlow/TintSuccess/TintDanger/TintInfo`) + shimmer + content-inset enforcement. |
| U23 | **Modern Main Menu (sidebar/social/chat)** | DESIGN-READY-PHASE-1 | Connectivity Section 8: full controller-only UX, sidebar with social cards, friend list, chat. (`connectivity-and-modern-main-menu.md`) |

### B.7 Quality / Testing

| # | Pillar | Status | Scope |
|---|---|---|---|
| T1 | **pd-tests Catch2 framework** | SHIPPED-COHORTS-1+2 / DEFERRED-COHORT-3 | 155 cases / 1881 assertions. Cherry-picked sources, link-time stubs, no SDL/GL/ENet/ROM. Cohort 1: netbuf, savebuffer, connectcode, manifest, random-pool, version pins, save-migration. Cohort 2: IMC stack, menu pool, flat-menu reachability, right-stick scroll. Cohort 3 (mission/mode/input mapping, master-loader/hand state machines) deferred. (`testing-framework-2026-04-26.md`) |
| T2 | **Bug discipline (bugs.md / systemic-bugs.md)** | SHIPPED-PROCESS | One-off vs systemic separation. ~5 truly OPEN entries (B-182 CRITICAL, B-183 HIGH, B-249 HIGH, B-246-OQ1 OPEN-QUESTION, plus minor). 74 FIXED-PENDING-PLAYTEST entries. |
| T3 | **Manual QC Backlog (qc-tests.md)** | LARGE-BACKLOG | Major categories: SPF-3 Lobby Hub Rooms (8 tests), SPF-1 (18), B-12 Phase 2 (8), 31-bot fix (5), D3R-7-11 (53), UI Scaling (~16+). Significant burn-down required at release. |
| T4 | **Crash Breadcrumb Ring** | SHIPPED | 256-slot ring dumped by VEH/UEF/SIGABRT. Push sites in mainTick, lvTick, chraTickBg/Tick, bwalkTick, botSpawn, botmgrAllocateBot, mainChangeToStage, SVC_STAGE_START send/receive. (S301) |
| T5 | **Diagnostic Instrumentation (5 DIAG tags)** | SHIPPED | `ENDSCREEN.DIAG`, `CHR.DIAG`, `MATCHSTART.DIAG`, `AUDIO.DIAG`, `CRASH.DIAG`. |
| T6 | **Debug Shortcuts Catalog** | SHIPPED-CATALOG / QUEUED-REGISTRY | 11 raw SDL hotkeys + 11 ActionMap Forge bindings catalogued. F2 test-fire, F8 PD_DEV_BUILD asymmetry, F10 duplicated handler open. Central registry recommended. (`audits/debug-shortcuts-audit-2026-04-26.md`) |
| T7 | **Super Audit cadence** | SHIPPED-PROCESS | Wave 1-5 cross-audits; daily delta audits. Weekly skill-driven audit recommended. |
| T8 | **B-179/B-182/B-183 torn-modeldef class** | OPEN | Critical/high crash + geometry issues. Modeldef defensive guards landed (S312); root-cause fix not yet identified. |

### B.8 Dev Tooling / Release

| # | Pillar | Status | Scope |
|---|---|---|---|
| V1 | **Dev Window v2** | SHIPPED-POLISHED | Build + Run + Version + Status + Git Pull/Push + DPI + light-theme PD redesign + per-test output + async RunspacePool + post-release latest refresh. (S256-S480) |
| V2 | **Build System (CMake + Ninja + ccache + PCH)** | SHIPPED | `file(GLOB_RECURSE)` discovery, build-headless.ps1, ccache sloppiness fix, PCH. CACERT regeneration gated on mtime (S475 8x build speedup). |
| V3 | **Standalone Updater (D13)** | SHIPPED | Semantic versioning, GitHub API, SHA-256, self-replace, save migration, ImGui UI, dual-tag releases, two channels. CA bundle fix S360. |
| V4 | **Release Pipeline** | SHIPPED | `release.ps1`, force-commit fallback, Build dir auto-creation, BOM-free CMakeLists writes (S477). |
| V5 | **Headless Build (build-headless.ps1)** | SHIPPED | Self-configures via `_build-env-prelude.ps1`. Idempotent. AI-facing entry point. |
| V6 | **Worktree System** | SHIPPED | `.claude/worktrees/`, async prune in dev-window-v2 (S475). |
| V7 | **PD_DEV_BUILD Gating** | SHIPPED | F6/F7/F12/Settings Debug tab dev-only. `release.ps1` stable adds `PD_STABLE_RELEASE=ON`. (S430) |
| V8 | **Run Tests Button** | SHIPPED | Toggle-Tests / Catch2 parser / per-case output / status row + MessageBox. (S475-S477) |
| V9 | **Auto-Memory System** | SHIPPED | `.auto-memory/MEMORY.md` index + per-fact memory files. Working preferences embedded. |

### B.9 Audio / Visual / Stats

| # | Pillar | Status | Scope |
|---|---|---|---|
| A1 | **Persistent Stats (D6)** | SHIPPED | `playerstats.c` string-keyed counters + JSON persistence. Damage dealt/received + shots hit + Hit% (S378). |
| A2 | **Discord Rich Presence (D7)** | SHIPPED | Thin IPC client, no link deps. Main Menu / Solo / CS / Co-op / Counter-Op / Forge / Lobby / Dedicated Server states. (S348) |
| A3 | **Sky Tearing Fix (B-128)** | SHIPPED | Systemic GBI state reset (FIX-C.1). |
| A4 | **Audio Underrun Mitigation (B-141)** | SHIPPED-INSTRUMENTATION / OPEN-ROOT-CAUSE | Drop/underrun/hitch counters + 30s summary. Root cause not pinned. |
| A5 | **Audio Channel Routing** | SHIPPED-CORE / OPEN-ENFORCEMENT | Per-agent volume layers shipped. S323 channel routing enforcement open. |
| A6 | **Cinematic Transitions** | OPEN-POLISH | (covered by U20) |

### B.10 Implied / Cross-cutting

| # | Pillar | Status | Scope |
|---|---|---|---|
| X1 | **Catalog Settings UI** | DESIGN-NOTED-S14 | User-visible Settings -> Catalog page with manifest highlighting. Buried in `session-catalog-and-modular-api.md` §14. |
| X2 | **Objective HUD & Waypoint System** | DESIGN-NOTED-S16 | Three phases: text overlay -> screen-edge arrows -> 3D billboards. Buried in `session-catalog-and-modular-api.md` §16. |
| X3 | **Cross-platform Shell Metadata (Mac/Linux)** | DEFERRED | macOS Spotlight `mdimporter` and Linux file-manager hooks. Zip-comment mirror is universal fallback. |
| X4 | **Cross-platform Play (Mac/Linux ports)** | DEFERRED-OUT-OF-SCOPE | Per connectivity doc; PC-only release. Future pillar. |
| X5 | **GPU-compute Swarm AI (Skedar setpieces)** | UNKNOWN-FEASIBILITY | Discussed conceptually; not specced. Forward-looking note only. |
| X6 | **Voice Activity Detection / Noise Suppression** | OUT-OF-SCOPE | Push-to-talk only at v1.0. |
| X7 | **VERSION-gate Strip Project-Wide** | QUEUED | Cohort C class closed for player init; project-wide hygiene pass surfaced by weapon system audit (visionmode + handmodeldef/cartmodeldef latents). |

---

## C. Dependency graph

This is a **gating** view: what must be in place before what becomes safe or sensible to do. The arrows are "blocks" not "succeeds chronologically."

### C.1 Engine ground truth

```
E1 Asset Catalog (SHIPPED)
  |
  +-- E2 Full-pipeline data migration (QUEUED)
  |     |
  |     +-- gates (eventually) E16 wire/save format simplifications
  |
  +-- E3 Universality sweep Phase 2 (IN-FLIGHT)
  |     |
  |     +-- gates retiring vestigial Layer A handlers
  |
  +-- E4 Asset Provider Phase 4 (QUEUED)
        |
        +-- gates (a) handle-aware modeldefLoad
        |   gates (b) handle-aware MENUMODELPARAMS
        |   gates (c) assetGetSize handle-aware
        |     |
        |     +-- gates filenum retirement (~23 sites)
        |     |
        |     +-- gates ArchiveProvider for Forge/Grid stage files

E5 Session Catalog (SHIPPED-LOAD / DEFERRED-WIRE)
  |
  +-- gates (eventually) full string-only wire surface

E6 Manifest Architecture (SHIPPED) ---- E7 Match Startup Pipeline (SHIPPED)

E10 Participant System (SHIPPED) ---- gates correct slot semantics across all paths

E11 Capsule Collision (SHIPPED-CORE / DEFERRED-POLISH)
  |
  +-- gates E12 Bot Jump AI (PLANNED)
  |
  +-- gates C8 Forge collision query reliability for placed objects
```

### C.2 Game modes

```
E11 Capsule polish ---- gates C5 Co-op polish (telefrag fixes, drop-in)
E10 Participant ---- gates C5 / C4 Counter-Op slot assignment
C3 SP-stage Option E (OPEN-DECISION) ---- gates SP-stages-in-MP feature parity
M16 Forge F1-F8 ---- gates C8 Grid full functionality
N7 Match handshake (SHIPPED) ---- gates C5 / C6 / C9 multiplayer modes
```

### C.3 Online connectivity

```
N1 ENet / N2 NAT / N3 Connect Codes (SHIPPED)
  |
  +-- N4 Friend Presence Phase 1 (IN-FLIGHT)
        |
        +-- N4 P1.J in-match invite + P1.K NAT diagnostics (QUEUED)
              |
              +-- gates Phase 2 chat / file transfer
                    |
                    +-- gates N5 Public Mods Page (Phase 3)
                          |
                          +-- gates C9 Listening Rooms (Phase 4)
                                |
                                +-- gates C6 Spectator (Phase 5)
                                      |
                                      +-- gates N6 Voice (Phase 5)
                                            |
                                            +-- gates C7 Theater (Phase 6)

N8 Interest Management (DESIGN-DRAFT) ---- gates >8-client scaling
N10 Master Server (PLANNED) ---- gates federation; not gating Phase 1
N11 Listen vs Dedicated ---- if Mike revives dedicated, S13 P4-B/C unblock
```

### C.4 Server / trust

```
S1-S8 (SHIPPED) ---- minimum trust surface for in-client listen-host
S9 Mod SHA-256 mandatory (OPEN) ---- gates safer cross-network mod exchange
S10 Updater Ed25519 (OPEN) ---- gates trust separation from GitHub
S11 MP stat integrity (OPEN) ---- gates competitive integrity
S12 pd.ini address validation (OPEN) ---- gates hardening pass
S13 P4-B/C (DEFERRED-DECISION) ---- gates dedicated server revival
S14 MAX_PLAYERS tripwire (5min) ---- compile-time safety net
```

### C.5 Mod ecosystem

```
M1 Component architecture (SHIPPED) ---- gates everything else mod-related
M2 .pdmod format (SHIPPED-PENDING) ---- once verified, M12 Theme Bundle plumbing trivial
M2 / M11 / M9 / M8 ---- gate M17 Studio Platform (S1 import flows)
M16 Forge F1-F8 ---- gates M17 Studio S8-S10 map editor
M17 Studio S5+S7 ---- consume M18 Visual Scripting node taxonomy
```

### C.6 UI / UX

```
U4 Action Map (SHIPPED) ---- gates U5/U6/U10
U5 J-1/2/3 (SHIPPED) ---- gates U20 Cutscene transitions
U6 Priority K (IN-FLIGHT) ---- gates U7/U8 final compliance
U7 Menu Stack (SHIPPED-MOST) ---- gates U23 Modern main menu
U23 Modern main menu (DESIGN-READY) ---- requires N4 Phase 1 social cards live
```

### C.7 Quality

```
T1 pd-tests Cohort 1+2 (SHIPPED) ---- gates safer landings
T1 Cohort 3 (DEFERRED) ---- gates state-machine regression coverage
T3 QC Backlog ---- gates release sign-off
T4-T6 Instrumentation ---- gates triage cycle
```

### C.8 Most-load-bearing dependencies (the tight ones)

1. **E11 Capsule polish** unblocks both C5 (drop-in/drop-out telefrag) and C8 (Forge collision queries on placed objects). Worth lifting before scoping Forge F4 (zones) and C5.
2. **N4 Phase 1 + P1.J/K** unblocks all subsequent connectivity phases. Each phase narrowly depends on the prior one's social wire layer.
3. **U6 Priority K + U5 ForgeSession IMC** must be discipline-clean before U20 Cutscene-transition sweep is sensible. The whole input framework gates the cutscene work.
4. **M2 .pdmod format operator-verify** is the lock on all subsequent mod-tool migrations. Once Mike confirms migration + Property Handler + hot-toggle, M12 Theme Bundle and the theme/skin/font tools get their .pdmod-saveable polish for free.
5. **S9/S10/S11/S12 security work** gates public-friendly online integrity. None individually expensive; all required before claiming the connectivity pillar is "trustworthy."

---

## D. Sequencing

Five gates. Each is a coherent stretch of work, not a fixed time window. Mike sets pace; the gates are the order.

### D.1 Gate 1: Stability and Content Correctness (-> v0.1.0 "Foundation")

**Premise:** Before stacking new architecture, validate that what's shipped works front-to-back across every existing mode and content path.

**Scope:**

1. **Crash investigation (B-182, B-183 torn-modeldef cascade)** -- root-cause sweep beyond the defensive guards already in place. Modeldef integrity is foundational to every mode.
2. **Cutscene transition fixes (U20)** -- transitions glitch on certain stage handoffs (per directive). Required because the Cutscene IMC layer (U5) depends on stable cutscene state.
3. **Random / Fiesta semantics (E14)** -- finish the spawning + match-start coverage. Selectors migrated; semantics not fully verified.
4. **Full mission playthrough verification** -- every solo mission across difficulties via QC checklist. Burn down `qc-tests.md` for solo + MP categories.
5. **AllInOne cull Phase 2 (E17)** -- finish the 15-step sequencing per Section H decisions. v44 protocol bump live.
6. **Open security quick wins (S14 tripwire, S12 pd.ini gate, AUDIT-L1/L2/L3/L5)** -- short list of half-day items.
7. **B-249 kill attribution (HIGH OPEN)** -- competitive-integrity fix, follow-on to B-256 partial.
8. **B-246 Phase B activation** -- wire `bgunMatrixCacheIsStale` per round-10 proposal pending Mike's approval.
9. **L7 polish residuals (FIX-G mission counters, M-fix-4/5/6 menu polish queue)**.

**Exit criteria:**

- All known critical/high open bugs closed (B-182, B-183, B-249) or moved to FIXED-PENDING-PLAYTEST.
- AllInOne Phase 2 merged + protocol v44 stable.
- Solo + MP QC categories fully checked.
- pd-tests cohort 1+2 green (already so per S474).
- Build clean, Dev Window v2 self-tests green.

**Rough effort sketch:** Multi-week. Crash investigation is the long pole.

### D.2 Gate 2: Architectural Foundation (-> v0.1.0 final)

**Premise:** Two systems are mid-flight and architectural in shape. Land them clean before content systems lean on them.

**Scope:**

1. **Input framework completion (U5 + U6 + U7 polish)**:
   - U6 K-b2 / K-b3 / K-e / K-f
   - U5 J-4 documentation + assertions
   - U7 M-24 strict-tree assertion (deferred but worth landing)
   - ForgeSession + ForgeFreefly IMCs locked
   - Cutscene IMC layer formalized (gates U20 polish)
   - Menu transition graph formalized (state machine spec, like the master-loader / hand state machine plan in `testing-framework` Cohort 2)
2. **Catalog full-pipeline data migration (E2)**:
   - Mike's directive to extend catalog struct to ALL Layer A data (damage, fire_rate, AI lists, animations, ext fields).
   - Manager pattern + .pdbase format introduced as the eventual single declarative form.
   - Eager build / lazy reads with manifest-driven eviction model.
   - Retire Layer A static arrays as their consumers migrate.
3. **Asset Provider Phase 4 (E4)** -- complete filenum retirement + Forge/Grid stage files as proper catalog registrations with `FileProvider` primaries.
4. **Session Catalog Phase 3 wire migration (E5)** -- if scope permits, finish the no-net_hash / catalog-string-only wire surface.

**Exit criteria:**

- Cutscene transitions stable (gates content polish).
- Catalog Manager + .pdbase prototype shipped on at least one Layer A domain (probably Weapons, since it has the most ext data).
- Forge stages register as catalog entries with file-backed providers.
- pd-tests gain Cohort 3 cases for menu transition graph + Mission/CombatSim mode invariants.

**Rough effort sketch:** Multi-week. Catalog full-pipeline migration is the long pole and may slip into Gate 3 if it grows.

### D.3 Gate 3: Content Systems and Tools (-> v0.5.0 "Studio")

**Premise:** With foundation in place, expand content surface and creator tools.

**Scope:**

1. **Capsule collision polish (E11)** -- slope-AABB adaptation, ceiling-jump-through, edge-case smoothing.
2. **Bot Jump AI (E12)** -- begin once E11 polish lands.
3. **The Grid F1-F8 (C8 / M16)**:
   - F1 Object catalog + placement
   - F2 Gizmo + properties
   - F3 Save/load + base stage
   - F4 Zones + lighting
   - F5 + F5b Logic + custom game types
   - F6 Interactables + AI
   - F7 Mission authoring (objectives + briefing)
   - F8 Polish + stretch
4. **Studio Platform foundations (M17 S1-S7)**:
   - S1 Asset import (mesh)
   - S2 Asset import (audio)
   - S3 Asset catalog registration
   - S4 Weapon defs (JSON schema)
   - S5 Data-driven projectiles (consumes M18 visual scripting nodes)
   - S6 Studio UI shell
   - S7 Weapon editor
5. **SP-stage MP-readiness Option E (C3)** -- if Mike calls for it, ship the SP-setup transport-prop overlay so lifts/escastep work in MP across the ten high-impact SP stages.
6. **Theme Bundle as .pdmod (M12)** -- finish the M-2.x tool-side authoring polish.
7. **Cohort 3 pd-tests (T1)** -- mission/mode/input mapping coverage.
8. **VERSION-gate strip project-wide (X7)** -- hygiene pass.

**Exit criteria:**

- The Grid functionally complete (F0 through at least F5b).
- Studio Platform M17 S1-S7 shipped.
- Bot AI improvements in place.
- Cohort 3 tests landing.
- Visible content authoring story: a player can edit a skin, theme, audio mod, map, weapon, and bot loadout without leaving the client.

**Rough effort sketch:** Largest gate. The Grid alone is 7 phases; Studio S1-S7 is ~7 sessions. Parallelizable across Grid track and Studio track.

### D.4 Gate 4: Online and UGC (-> v0.3.0 "Community" merged with v0.4.0 "Federation")

**Premise:** Connectivity rebuilt around the social P2P model in `connectivity-and-modern-main-menu.md`. Mods become a first-class shared currency.

**Scope:**

1. **Connectivity Phase 1 finish (N4)**:
   - P1.J in-match invite (CLC_INVITE_JOIN, NET_PROTOCOL_VER bump deferred until this lands)
   - P1.K NAT diagnostics harness
   - Per-friend connect-code copy modal
2. **Modern main menu (U23)** -- sidebar/social/chat fully wired to N4 presence.
3. **NAT matrix testing** -- verify all 5 tiers (Direct/STUN/UPnP/ICE/TURN) on representative NAT classes.
4. **Drop-in / drop-out co-op (N12 / C5)** -- finish L-6 prompt + server-side coop manifest rebuild + telefrag prevention.
5. **Connectivity Phase 2** -- chat + file transfer + status/achievement popups (already plumbed via U17 toasts).
6. **Connectivity Phase 3** -- public mods page + player profile.
7. **Mod sharing system polish** -- friend-to-friend mod transfer over N4 channel, rate limits, integrity gates.
8. **Interest management Phase A or B (N8)** -- room/stage relevance OR radius/grid filter to neutralize O(N*P) broadcast cost. Defer Phase C (PVS) and D (cadence) to Gate 5.
9. **Security release-blockers (S9, S10, S11, S12)** -- mandatory mod SHA-256, Ed25519 updater, MP stat integrity, pd.ini address validation.
10. **R-5 server GUI redesign (N9)** -- if dedicated server track revived.
11. **D16 Master Server (N10)** -- if federation route greenlit.

**Exit criteria:**

- Friends can play across all 5 NAT tiers verified.
- Modern main menu live with social cards.
- Public mods page browseable, per-friend mod sharing live.
- Drop-in/drop-out co-op functional with telefrag prevention.
- Security blockers closed.

**Rough effort sketch:** Multi-week. Phase 1 finish is short; Phase 2/3 each ~2-4 weeks; security blockers each 1-3 days; NAT matrix testing is real-world calendar work.

### D.5 Gate 5: Polish and Release Prep (-> v1.0.0 "Forge")

**Premise:** Convergence. All pillars functional; final pass for accessibility, polish, voice, theater, and release-grade audit clearance.

**Scope:**

1. **Voice chat (N6)** -- libopus implementation, PDVOC frames, UI indicator on Settings tab status pill.
2. **Theater (C7)** -- saved-match playback subsystem (Connectivity Phase 6).
3. **Spectator (C6)** -- complete unified subsystem with Theater (Connectivity Phase 5 polish).
4. **Visual scripting completion (M18 / Studio S5/S7)** -- node taxonomy lands as user-facing.
5. **Studio Platform completion (M17 S8-S14)** -- map editor, ADS/auto-aim, mod packager, hot-reload, network integration.
6. **Forge F8 stretch** -- terrain brushes, co-op editing, blueprints, weather, physics objects.
7. **Federation hooks (N10)** -- Phase D16a-d if not started in Gate 4.
8. **Interest management Phase C/D (N8)** -- PVS + cadence throttling for high-bot or high-spectator scenarios.
9. **Cohort 3 pd-tests final** -- full state-machine coverage.
10. **Theme Tool mod-pack bundling polish**.
11. **Audio polish** -- B-141 root cause if not yet pinned, S323 channel routing enforcement, audio-mod transitions.
12. **ID renames** -- catalog ID slug renames deferred from heads/bodies/maps audits (test_arch -> suburb, etc.) if Mike approves save-format compat plan.
13. **Cross-platform shell metadata (X3)** -- macOS Spotlight + Linux file-manager hooks if scope.
14. **Release-gate audit clearance** -- final 2026-04-19 super-audit findings closed, NAT lab test, full-flow tests across campaign + MP, build automation refinements, packaging.

**Exit criteria:**

- All v1.0.0 features per `roadmap.md` shipped.
- Final super-audit clean.
- pd-tests cohorts 1-3 green.
- QC checklist 100% across solo + MP + Forge + Studio + connectivity.
- Cross-NAT verification matrix all green.
- Documentation fresh.

**Rough effort sketch:** Voice + Theater are each ~2-4 weeks. Studio S8-S14 is ~7 sessions. Final audit clearance is calendar work.

### D.6 Out-of-band: ongoing infrastructure that runs across all gates

These don't belong to a single gate; they run continuously.

- **Dev Window v2 polish** (V1) -- ongoing as Mike surfaces UX gaps.
- **Build pipeline upkeep** (V2/V3/V4/V5/V8) -- ongoing.
- **pd-tests authoring discipline** (T1) -- every architectural fix lands a test in the same commit.
- **Bug discipline** (T2) -- bugs.md and systemic-bugs.md updated as work proceeds.
- **Manifest discipline** (E6) -- preserved by every new code path that touches stage transitions.
- **Wire/save format versioning** (E15/E16) -- bumped per change with migration path.
- **Audit cadence** (T7) -- weekly skill-driven super-audit.
- **Catalog-first discipline** (E1) -- every new asset reference goes through the catalog.

---

## E. Decision points

Items where scope or direction needed Mike's call before the gate kicks off. **All 11 decisions resolved 2026-04-27** (see E.0 below). Long-form analysis preserved in E.1 through E.11 for historical record; each subsection now carries a RESOLVED stamp at the top.

### E.0 Resolution table (2026-04-27)

Mike approved E.1, E.2, E.3, E.6, E.7 directly. E.4, E.5, E.8, E.9, E.10, E.11 resolved per delegated authority using the recommendations already in this doc (Mike's note: "if you have explicit recommended defaults in the existing doc, you can mark those approved per delegated authority too").

| ID | Question | Resolution | Rationale |
|---|---|---|---|
| **E.1** | Dedicated server: revive or retire? | **Defer to Gate 4.** Listen + P2P first; revisit dedicated track once connectivity Phase 1+2 is real. P4-A/B/C plugin-ABI track and R-5 server GUI redesign stay stranded for now. | Friend-play story is covered by listen + P2P; community-server volume is the real driver and we cannot judge it without Phase 1+2 data. |
| **E.2** | Catalog full-pipeline migration scope | **Incremental per-domain.** Land Weapons in Gate 2 to validate the Manager + .pdbase pattern. Other asset types (bodies, heads, scenarios, music, bot profiles, AI scripts, animations, prop tables) follow in Gate 3 as their consumers get touched. Manager pattern + .pdbase format already specced in `project_catalog_architecture_future.md`. | Validates the architectural pattern on the highest-ext-data domain (Weapons) without committing to a multi-week sprint. Lets pd-tests cohort lock in the pattern before scaling. |
| **E.3** | SP-stage MP-readiness Option E (B-228) | **Top 3 stages: Airbase, CITraining, Skedar Ruins.** Defer Attackship / AirForceOne / Infiltration / Defection / Defense / Investigation / Deepsea. | Airbase + CITraining are highest-replay-value; Skedar Ruins is the showcase setpiece. Other 7 stages can ship later without blocking v0.5.0. |
| **E.4** | Catalog ID slug renames | **Approved per delegated authority: Bundle into a single Gate 5 release-prep ID rename pass.** Test-style slugs (`test_arch` -> `suburb`, `test_dest` -> `training_day`, `test_lam` -> `grand_library`) ride a single SAVE_VERSION bump. | Rename churn is cosmetic; bundling into one bump avoids multiple migrations and keeps the v1.0 polish surface clean. |
| **E.5** | Grid Blank Map stagenum | **Approved per delegated authority: Defer to Forge F3.** Blank Map becomes the natural empty base stage when F3 (Save/load + base stage) lands; register `forge_blank` as a `FileProvider` catalog entry at that point. The Grid submenu continues to omit the Blank Map row until then. | F3 already needs a base-stage registration mechanism; building Blank Map separately would duplicate plumbing. |
| **E.6** | Voice chat scope at v1.0.0 | **PTT-only.** Hard mute + per-friend mute. VAD deferred past v1.0. | PTT covers ~90% of friend-play utility; VAD adds detector complexity and quality tuning that does not justify v1.0 scope. |
| **E.7** | Federation / Master Server (D16) | **Minimal D16a bootstrap.** Bootstrap-rendezvous nodes for non-friend discovery. Full federation (D16b/c/d: cross-server matchmaking, signed transfer tokens, trust levels) deferred past v1.0. | Bootstrap gives discovery without committing to centralized identity or matchmaking; aligns with the connectivity P2P-first stance. |
| **E.8** | Studio Platform scope at v1.0.0 | **Approved per delegated authority: S1-S10 in Gate 5.** Asset import, weapon editor, map editor. Defer S11-S14 (ADS/auto-aim, mod packager, hot-reload, network integration). | Forge F1-F8 already covers level-editor surface; Studio adds asset/weapon authoring. S11-S14 are cross-cutting integrations best landed once content tools are real. |
| **E.9** | PVS / Interest Management depth | **Approved per delegated authority: Phase A at Gate 4, Phase B at Gate 5, defer C/D.** Phase A (room/stage relevance) + Phase B (radius/grid filter) ship in Gate 4 and Gate 5 respectively; PVS portal walking (Phase C) and cadence throttling (Phase D) deferred past v1.0. | Phase A+B is enough for friend-mesh scaling; PVS is high-cost optimization with limited friend-mesh ROI. |
| **E.10** | Cross-platform port (X4) | **Approved per delegated authority: PC-only through v1.0.0.** Mac/Linux ports parked as a post-v1.0 pillar. macOS Spotlight + Linux file-manager metadata hooks (X3) likewise deferred. | Architecture is portable (SDL2 + OpenGL + statically linked deps), but explicit cross-platform engineering is its own pillar. PC-only ships v1.0 sooner. |
| **E.11** | Audit cadence | **Approved per delegated authority: Per-gate cadence.** One full super-audit at each gate boundary (Gate 1 -> Gate 2 -> ... -> Gate 5). Daily delta audits and weekly skill-driven audits remain ad-hoc. | Per-gate cadence aligns with exit-criteria checks and avoids audit fatigue between gates. |

### E.0.1 Sequencing implications of the resolutions

Several resolutions tighten or relax the gate sequencing in Section D:

- **E.1 (defer dedicated server)** removes P4-B/C and R-5 from Gate 4 scope, tightening Gate 4 to listen + P2P + connectivity Phase 1-3 + drop-in/drop-out + security blockers + Phase A interest management.
- **E.2 (incremental catalog migration)** keeps Gate 2 to a single domain (Weapons + Manager + .pdbase pattern); Gate 3 picks up bodies/heads/scenarios/music/AI scripts/animations as content-system work touches them.
- **E.3 (top-3 SP stages)** scopes C3 in Gate 3 to three stages, not ten.
- **E.6 (PTT-only voice)** trims Gate 5 voice scope.
- **E.7 (minimal D16a)** removes federation matchmaking from v1.0 scope.
- **E.8 (Studio S1-S10)** trims Gate 5 Studio scope.
- **E.9 (Phase A in Gate 4, Phase B in Gate 5)** splits interest-management work across both gates.
- **E.4 / E.5 / E.10 / E.11** are scoping/cadence calls that don't change pillar count, just timing.

The resolved sequence is otherwise unchanged from Section D.

### E.0.2 Decisions deferred (explicitly post-v1.0)

These are now explicitly post-v1.0 per the resolutions above:

- Full federation (D16b/c/d): cross-server matchmaking, signed transfer tokens, trust levels.
- Cross-platform play (Mac/Linux ports + cross-platform shell metadata).
- Voice activity detection / noise suppression.
- Interest management Phase C (PVS) and Phase D (cadence throttling).
- Studio Platform S11-S14 (ADS/auto-aim, mod packager, hot-reload, network integration).
- SP-stages-in-MP Option E for the seven non-top-3 stages (Attackship, AirForceOne, Infiltration, Defection, Defense, Investigation, Deepsea).
- Game-agnostic dedicated server (P4-B/C, R-5) -- pending Gate 4 review.

---

### E.1 Dedicated server track: revive or retire?

**RESOLVED 2026-04-27: Defer to Gate 4** (Mike approved direct).



**Question:** `pd-server` was retired from build/release as of S475. The connectivity pivot replaces dedicated-server hosting with in-client P2P + listen-host. The ADR for game-agnostic server (`pd-server-plugin-abi-adr.md`, P4-A) is doc-complete; P4-B/C are deferred. Audit DS-1 calls the "game-agnostic dedicated server" pillar unmet.

**Options:**

1. **Retire formally.** Update `pillars.md` and `infrastructure.md` to mark D9 dedicated server as DEFERRED-PERMANENTLY for v1.0.0. P4-B/C closed. R-5 server GUI redesign closed. All hosting flows through listen + connectivity P2P.
2. **Revive as opt-in.** Keep listen-host as default; reintroduce `pd-server` as an opt-in dedicated build for community-server use cases. Land P4-B (manifest broker spike) as the smallest viable revival.
3. **Defer decision to Gate 4.** Track listen-host through stability/foundation; revisit when connectivity Phase 1+2 is real.

**Why this matters:** Closes or opens substantial scope for Gate 4. The R-5 server GUI redesign, the P4-A/B/C track, the SEC-9 interest-management work for >8-client scaling, and the network-architecture.md "social hub on dedicated server" vision all hinge on this.

**Recommendation if asked:** Option 3 (defer to Gate 4). Listen-host plus P2P covers the friend-play story; dedicated revival is a content-volume call best made once social Phase 1+2 is real.

### E.2 Catalog full-pipeline data migration scope

**RESOLVED 2026-04-27: Incremental per-domain** (Mike approved direct). Weapons in Gate 2; rest in Gate 3. Manager + .pdbase pattern per `project_catalog_architecture_future.md`.

**Question:** Mike's directive (per audit `catalog-universality-sweep-2026-04-27.md` Section I deferred): "extend the catalog struct to be inclusive of ALL data, not just selection layer." That includes weapon damage, fire rate, AI script tables, animations. Estimated 100+ sites.

**Options:**

1. **Full migration in Gate 2.** Complete the work as one architectural sprint. Manager pattern + .pdbase format become foundational.
2. **Incremental per-domain.** Gate 2 lands one domain (Weapons), validates the Manager + .pdbase pattern, then Gate 3 lands subsequent domains as content systems get touched.
3. **Defer Manager / .pdbase to Gate 5.** Keep the universality sweep selector-only for v0.1.0 / v0.3.0; defer data migration to release-prep.

**Why this matters:** Sets the architectural shape for v0.1.0 vs v1.0.0. Option 1 risks a long Gate 2; Option 3 keeps Layer A static arrays in place across most of release.

**Recommendation if asked:** Option 2. Land Weapons in Gate 2 as the validated path; Gate 3 picks up the rest opportunistically.

### E.3 SP-stage MP-readiness Option E

**RESOLVED 2026-04-27: Top 3 stages only** (Mike approved direct). Airbase, CITraining, Skedar Ruins. Other 7 stages deferred.

**Question:** B-228 reopened with corrected diagnosis. P5 stage relax does nothing without overlaying SP setup transport-prop blob (LIFT/ESCASTEP) into MP setup at runtime. Affects Airbase, Attackship, AirForceOne, Infiltration, CITraining, Defection, Defense, Investigation, Deepsea, SkedarRuins.

**Options:**

1. **Ship Option E in Gate 3.** Lifts and elevators work on SP-stages-in-MP. Substantial scope (10+ stages, transport-prop registration on each).
2. **Don't ship Option E.** Mark SP-stages-in-MP as "playable but no transport." Keep Whitelist conservative.
3. **Ship for top 3 stages.** Airbase + CITraining + Skedar Ruins as proof; defer rest.

**Why this matters:** Defines the content-completeness story for MP at v0.5.0+. Without Option E, SP-class arenas in MP feel broken on lifts.

**Recommendation if asked:** Option 3. Ship for the highest-replay-value stages; keep Whitelist ranked.

### E.4 Catalog ID slug renames

**RESOLVED 2026-04-27: Bundle into Gate 5 release-prep ID rename pass** (delegated authority). Single SAVE_VERSION bump.

**Question:** Test-style slugs (`test_arch`, `test_dest`, `test_lam` -> `suburb`, `training_day`, `grand_library`) deferred from heads/bodies/maps audits. Affects save format compatibility.

**Options:**

1. **Rename now with one-time migration.** Save migration framework already exists; load-time legacy alias map.
2. **Rename at v1.0.0 only.** Bundle with any other ID renames into a single SAVE_VERSION bump.
3. **Don't rename.** Live with the test-style slugs forever; document.

**Why this matters:** Cosmetic but visible in mod authoring + save inspection.

**Recommendation if asked:** Option 2. Bundle into a single Gate 5 release-prep ID rename pass.

### E.5 Grid Blank Map stagenum

**RESOLVED 2026-04-27: Defer to Forge F3** (delegated authority). Blank Map row stays hidden until F3 lands `forge_blank` as a `FileProvider` catalog entry.

**Question:** Per `audits/evening-decisions-2026-04-23.md`: "DO NOT ship CI Training as the blank." Submenu omits Blank Map row until Mike names a stagenum.

**Options:**

1. **Pick an existing empty stagenum.** Mike names it.
2. **Create a new minimal stage.** Author a literal blank skybox with collision floor; register as catalog entry.
3. **Defer to Forge F3 (Save/load + base stage).** Blank Map becomes the natural empty base.

**Why this matters:** Blocks Grid empty-canvas authoring story.

**Recommendation if asked:** Option 3. Naturally falls out of F3 with a `forge_blank` base stage registered as a `FileProvider` catalog entry.

### E.6 Voice chat scope at v1.0.0

**RESOLVED 2026-04-27: PTT-only** (Mike approved direct). VAD deferred past v1.0.

**Question:** `audits/connectivity-libopus-decision-2026-04-25.md` decides codec + wire format; implementation pending.

**Options:**

1. **Push-to-talk only at v1.0.0.** Simplest. Hard mute / per-friend mute. No VAD.
2. **PTT + VAD with manual fallback.** Adds detector; more polish.
3. **Defer voice past v1.0.0.** Closes Phase 5 of connectivity until later.

**Why this matters:** Defines the social-platform scope at v1.0.0.

**Recommendation if asked:** Option 1. Voice is the long-tail piece; PTT covers 90% of friend-play utility without VAD complexity.

### E.7 Federation / Master Server (D16) scope at v1.0.0

**RESOLVED 2026-04-27: Minimal D16a bootstrap** (Mike approved direct). Bootstrap-rendezvous only. D16b/c/d (full federation) deferred past v1.0.

**Question:** D16 master server is planned for v0.4.0 but the connectivity pivot may obviate it.

**Options:**

1. **Ship D16 for v1.0.0.** Mesh peer discovery + cross-server matchmaking + signed transfer tokens.
2. **Defer D16 indefinitely.** Connectivity P2P + presence covers the friend-play story; federation is post-v1.0.
3. **Ship a minimal D16a only.** Bootstrap nodes for rendezvous; defer matchmaking and federation.

**Why this matters:** Closes or opens significant Gate 4 / Gate 5 scope.

**Recommendation if asked:** Option 3. Bootstrap-rendezvous gives non-friend discovery without committing to a full federation.

### E.8 Studio Platform scope at v1.0.0

**RESOLVED 2026-04-27: Studio S1-S10 in Gate 5** (delegated authority). Asset import + weapon editor + map editor. S11-S14 (ADS/auto-aim, mod packager, hot-reload, network integration) deferred past v1.0.

**Question:** `studio-platform-design.md` is 14 phases / ~35 sessions / ~13,400 LOC. Full scope vs partial vs deferred to post-v1.0.

**Options:**

1. **Full Studio in Gate 5.** S1-S14 all shipped.
2. **Studio S1-S10 (asset import + weapon editor + map editor).** Defer S11-S14 (ADS/auto-aim / mod packager / hot-reload / network integration).
3. **Studio S1-S7 only (asset + weapon).** Defer map editor in favor of Forge F1-F8.

**Why this matters:** Defines content-creation surface at v1.0.0.

**Recommendation if asked:** Option 2. Forge covers the level-editor story; Studio covers the asset/weapon authoring story; mod packager and network integration can lag.

### E.9 PVS / Interest Management depth

**RESOLVED 2026-04-27: Phase A in Gate 4, Phase B in Gate 5** (delegated authority). Room/stage relevance + radius/grid filter ship for v1.0. Phase C (PVS) and Phase D (cadence throttling) deferred past v1.0.

**Question:** SEC-8/SEC-9 audit calls for interest management to scale past 8 clients. `interest-management-replication.md` has 4 phases.

**Options:**

1. **Phase A (room/stage relevance) at v0.3.0; Phase B (radius) at v1.0.0; defer C/D.** Pragmatic.
2. **Full Phase A-D for v1.0.0.** Maximum scaling.
3. **Defer all to post-v1.0.** Listen-host is small-group anyway.

**Why this matters:** Defines the "max comfortable concurrent humans" cap.

**Recommendation if asked:** Option 1. Phase A + B is a clear win for friend-mesh scaling; PVS is a high-cost optimization with limited friend-mesh ROI.

### E.10 Cross-platform port (X4) at any release?

**RESOLVED 2026-04-27: PC-only through v1.0.0** (delegated authority). Mac/Linux ports + cross-platform shell metadata (X3) deferred post-v1.0 as their own pillar.

**Question:** PD2 is PC-only. macOS/Linux future pillar.

**Options:**

1. **PC-only through v1.0.0; Mac/Linux post-v1.0.**
2. **Mac/Linux ports start in Gate 5 as parallel track.**
3. **Permanently defer.**

**Recommendation if asked:** Option 1. Architecture is portable (SDL2 + OpenGL + statically-linked deps); but explicit cross-platform engineering is its own pillar best deferred.

### E.11 Whether to allocate audit waves on a fixed cadence

**RESOLVED 2026-04-27: Per-gate cadence** (delegated authority). One full super-audit at each gate boundary. Daily delta audits and weekly skill-driven audits remain ad-hoc.

**Question:** Super-audit cadence is currently ad hoc.

**Options:**

1. **Weekly cadence formalized.** Wednesday or Sunday standing super-audit.
2. **Bi-weekly cadence.**
3. **Per-gate cadence** (one full audit at gate boundaries).

**Recommendation if asked:** Option 3. Saves audit fatigue; aligns with gate exit criteria.

---

## F. What's done

A tally of shipped milestones for momentum reference (S480, 2026-04-27):

### F.1 Engine and infrastructure

- D1 N64 strip (672 platform guards, 114+ files)
- D-MEM M0-M6 + MEM-1/2/3 (memory modernization complete)
- D-STAGE Phases 1-3 (stage decoupling complete)
- B-12 Phases 1-3 (participant pool, protocol v37)
- D3R-1 to D3R-11 (component mod architecture, full)
- MSP A-F + SA-1-7 + Manifest Lifecycle 0-6 + L5 (match startup pipeline)
- Asset Provider Phases 1-3 (S326/S346/S377)
- Catalog universality sweep Phase 1 (heads/bodies/maps, S470/S472/S473)
- AllInOne cull Phase 1 audit (75 -> 47 arenas, 8 weapon retirement)
- Capsule collision system (D2b, in active use)
- Spawn pool L1-L4 + B-134 capsule-radius threshold

### F.2 Game modes

- M1 Solo Campaign (mission select + solo flow + pause + options)
- M2 Combat Simulator (UI catalog audit + match flow + endscreen + auto-save)
- L5 Co-op match lifecycle (manifest, protocol v35, match_seed)
- Counter-Op anti-player wire authority + protocol v36 (S263)
- The Grid F0 + polish (free-fly + HUD + bot tab + map variant + placement ghost, S307+S313)

### F.3 Online connectivity

- ENet protocol at v44
- D8 NAT traversal (STUN + symmetric hole-punch)
- Connect codes (4-word phonetic, no raw IP)
- SPF-1 hub/room/identity/phonetic
- R-1 / R-2 / R-3 / R-4 (room lifecycle, sync, match start)
- Friend presence Phase 1 core (Ed25519 identity, TOFU, P2P layer, social store, presence layer, UI)
- In-client listen-host route (H-1 P3-A/P3-B, S421)

### F.4 Server / trust

- Admin RCON + token hashing (MASTER-C2a, S393)
- Persistent bans (MASTER-C2c/d + IPv4-mapped fix S416)
- Preserved-player cookie (MASTER-C3, S393)
- Room password hashing (SEC-14)
- Server info query token challenge (SEC-7, v38)
- netbufReadStr NUL-terminate (SEC-1, S391)
- netbufWriteGset field-wise (LAYOUT-1, S392)

### F.5 Mod ecosystem

- D3R-9 network distribution
- D3R-10 mod-pack export/import
- D3R-11 legacy cleanup
- A-1 to A-7 audio mod system
- S-1 to S-9 skin editor
- L3 / M-5/6/7 map import pipeline
- M-1 to M-4 .pdmod unified format (operator verification pending)
- Property Handler DLL
- Theme Editor + theme bundling plumbing
- Bot Customizer (D3R-8)
- Nine-Slice Chrome Tool
- Font Mod
- Modding Hub with all tools

### F.6 UI / UX

- D5 Phase 1-5 ImGui menu replacement (254 dialogs)
- D5 Phase 4 themes + UI texture overrides (S351)
- D5 Phase 5 lobby portraits + 5C/5D polish
- M0.2 input action map (51 actions, IMC stack)
- J-1/2/3 contextual input schemes (Mission XOR CombatSim split, S458)
- Menu stack architecture M-1 to M-23 (most done)
- Flat menu navigation (all 27 menus conform)
- Per-action hold overrides
- Controller bindings + visual mapper
- Hold ring API
- Per-agent settings (prefs_agent.ini [Audio])
- UI Chrome Style (5 procedural title-bars)
- HUD layer order + interact prompt menu-gate
- HUD killfeed (bot-bot, network broadcast)
- Achievement unlock toasts
- Stats Viewer (D6 Phase 3)
- Pause menu modal confirms

### F.7 Quality

- pd-tests Catch2 framework (155 cases, 1881 assertions, cohort 1+2)
- Crash breadcrumb ring (256 slots)
- Diagnostic instrumentation (5 DIAG tags)
- Debug shortcuts catalog
- Bug discipline (bugs.md, systemic-bugs.md, working-preferences memory)

### F.8 Dev tooling

- Dev Window v2 (heavily polished S256-S480)
- Build system (CMake + Ninja + ccache + PCH, 8x speedup S475)
- Standalone updater D13 (CA bundle fix S360)
- Release pipeline (BOM-free CMakeLists writes, force-commit fallback)
- Headless build entry point
- Worktree async prune
- PD_DEV_BUILD gating
- Run Tests button
- Auto-memory system

### F.9 Audio / visual / stats

- D6 persistent stats (full wire-in, damage tracking, achievement toasts)
- D7 Discord Rich Presence (8 presence states)
- Sky tearing fix (B-128, FIX-C.1)
- ALIGN16 pointer alignment regression fix (B-184/B-193, S384)

### F.10 Cumulative bug-fix tally

- 47 deep-audit bugs (5 critical + 7 high + 10 medium + 9 low) closed across S185-S186
- ~74 FIXED-PENDING-PLAYTEST entries in bugs.md
- ~5 truly OPEN entries remaining
- ~480 sessions logged

---

## G. Unknowns and risks

Items where scope or feasibility isn't clear yet. Surfaced as forward-looking notes, not blocking items.

### G.1 Architectural unknowns

- **GPU-compute swarm AI for Skedar setpieces (X5).** Discussed conceptually; not specced. May or may not be feasible inside the current renderer architecture. If feasible, lands as a v1.0+ "Spectacle" stretch.
- **The full-pipeline catalog data migration size (E2).** Mike estimated "100+ sites" but the precise scope depends on how far the Manager + .pdbase pattern is taken. Could grow substantially if it absorbs AI script tables, animations, prop tables.
- **Visual scripting graph runtime cost (M18).** Node taxonomy is spec-complete; runtime evaluator cost not benchmarked. Forge F5 logic system depends on this.
- **Cohort 3 pd-tests state-machine extraction strategy (T1).** Master-loader and hand state machines may need extraction (parallel pure-C subset like inputctx_pure / menupool_pure) or in-place testing. Choice not yet made.
- **Audio underrun root cause (A4).** Mitigation in place; root cause unknown. Could surface late as a release-blocker if it reproduces under voice chat load.

### G.2 Connectivity unknowns

- **NAT 5-tier verification matrix.** Real-world NAT classes vary; no lab harness yet. P1.K queued.
- **Voice chat bandwidth under realistic mesh load.** Decision says ~24 kbps/stream worst case ~120 kbps for 4-peer mesh; not measured under congestion.
- **Friend-presence DHT / rendezvous mechanism.** Phase 1 accepts asymmetric reachability; Phase 2 layers libp2p Kademlia or bootstrap nodes additively. Bootstrap node set, hosting, and trust model not specified.
- **Spectator/Theater wire format scaling.** SVC_STATE_FRAME at 10Hz x 16 participants ~10KB/s outbound per spectator. Unknown ceiling for >1 spectator.

### G.3 Content / tooling unknowns

- **Forge collision query reliability for placed objects (F6).** Capsule sweep against arbitrary placed geometry not yet validated.
- **Forge F7 mission authoring UX.** Custom objectives + briefing model not specced beyond doc references.
- **Studio S8-S10 map editor handoff to Forge.** Two systems with adjacent scope; integration vs duplication needs a design pass.
- **Theme bundling user discovery.** Once theme bundles ship as `.pdmod`, discoverability via Public Mods Page depends on Phase 3 timing.

### G.4 Security / trust unknowns

- **Mod sandboxing depth.** SHA-256 verification + provider isolation handle integrity; no resource-quota enforcement on mods (memory, CPU, disk). Could surface as a release concern once cross-network mod sharing is real.
- **Updater binary signing trust root.** Single GitHub channel today; Ed25519 signing scheme not designed end-to-end.
- **Server admin token rotation.** Hashed token persists in server.ini; no rotation flow.
- **Public mods page abuse model.** Ranking, moderation, takedown not specced.

### G.5 Schedule / scope unknowns

- **AllInOne cull Phase 2 size.** ~25 source files + ~144 JSON entries + 6 list.c regions; effort hard to estimate until step 1 of 15 lands.
- **Capsule polish scope (E11).** Slope-AABB and ceiling-jump-through have unknown depth; could be 1 session or 5.
- **Cohort 3 testing surface (T1).** Master-loader and hand state machines have unknown branch counts.
- **QC backlog burn-down rate (T3).** Manual playtest matrix is large; burn-down rate depends on how much can be rolled into pd-tests vs requires hand-on.

### G.6 Long-tail risks

- **B-179/B-182/B-183 torn-modeldef class (T8).** Defensive guards in place; root cause unknown. Could surface as an arbitrary crash class on new content.
- **Save format compatibility under universality data migration.** SAVE_VERSION bump strategy not yet planned for ext-field expansion.
- **Network-protocol bump cadence.** v44 today; expected bumps: P4-B if approved, full string-only wire surface (E5 Phase 3), spectator/theater finalization, voice integration, federation handshake. Mixed-version tolerance via the existing reject-at-handshake pattern; cumulative bumps risk scaring tester populations into stale builds.
- **Identity migration to network-agnostic handles.** Phase 1 ships `SHA256(pubkey \|\| domain)[:4]` connect codes. Backward compatibility with the older 4-word IPv4 connect codes needs a clear plan.

---

## H. Roadmap-as-table-of-contents

For navigation:

| Section | Subject |
|---|---|
| A | Vision: what release means, the four release identities |
| B | Pillars catalog (10 categories, 80+ pillars) |
| B.1 | Engine Core (E1-E17) |
| B.2 | Game Modes / Content (C1-C9) |
| B.3 | Online Connectivity (N1-N13) |
| B.4 | Server / Trust / Security (S1-S14) |
| B.5 | Mod Ecosystem (M1-M18) |
| B.6 | UI / UX (U1-U23) |
| B.7 | Quality / Testing (T1-T8) |
| B.8 | Dev Tooling / Release (V1-V9) |
| B.9 | Audio / Visual / Stats (A1-A6) |
| B.10 | Implied / Cross-cutting (X1-X7) |
| C | Dependency graph |
| D | Sequencing across 5 gates |
| D.1 | Gate 1: Stability and Content Correctness (-> v0.1.0) |
| D.2 | Gate 2: Architectural Foundation (-> v0.1.0 final) |
| D.3 | Gate 3: Content Systems and Tools (-> v0.5.0) |
| D.4 | Gate 4: Online and UGC (-> v0.3.0/v0.4.0) |
| D.5 | Gate 5: Polish and Release Prep (-> v1.0.0) |
| E | Decision points (resolved 2026-04-27) |
| E.0 | Resolution table + sequencing implications + post-v1.0 deferred list |
| E.1 - E.11 | Long-form analysis (RESOLVED stamps at top of each) |
| F | What's done |
| G | Unknowns and risks |
| H | Table of contents (this section) |

---

## Sources

This roadmap synthesizes:

- `context/roadmap.md` (release ladder)
- `context/infrastructure.md` (phase tracker)
- `context/constraints.md` (active and removed constraints)
- `context/session-log.md` S358-S480 tail (recent work)
- `context/tasks-current.md` (active punch list)
- `context/working-preferences.md` (collaboration shape)
- All design docs in `context/designs/` listed in Section A.3 of QUICKSTART
- Audits in `context/audits/` from 2026-04-19 onward
- ADR-001 / ADR-002 / ADR-003

Last updated: 2026-04-27.
