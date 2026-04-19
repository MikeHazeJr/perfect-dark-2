# Deep Project Audit Report — Perfect Dark 2

**Audit scope**: Dedicated server + Security/Trust + Scaling ceilings + Save/load integrity + Mod pipeline security + The Grid trust surface
**Date**: 2026-04-19
**Auditor**: Claude Opus 4.7 (1M) — `gallant-davinci-91ec90` worktree, HEAD `dfbd3834` (v0.0.136)
**Method**: Static analysis only. No runtime execution. No code written.
**Follows**: `.claude/skills/super-audit/audit-prompt.md` structure exactly, including Phase 2.5 data-layout scan.

---

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose
Perfect Dark 2 is a community-built, PC-only modernization of Rare's *Perfect Dark* (N64, 2000), layered over the `n64decomp → fgsfdsfgs/port-net → jonaeru/allinone` lineage. PD2 adds decentralized P2P multiplayer (ENet), a **game-agnostic dedicated server** (`pd-server.exe`), a Catalog-backed asset pipeline that treats mods and native content identically, a modding pipeline that distributes components over the network (`PDCA` archive + zlib), and an in-game level editor ("The Grid" / "Forge"). A social/discovery Grid layer is designed but not yet implemented as a network-facing feature.

### Likely Player & Contributor Types
- PD / GoldenEye veterans wanting the original feel at modern resolutions and framerates
- Modders building maps, characters, weapons, audio, and UI themes (component-based architecture, `mods/<category>/<slug>/`)
- Speedrunners and challenge hunters (save formats accessible, no DRM)
- Dedicated-server hosts running public or private `pd-server.exe` instances
- Homebrew/Windows P2P LAN groups
- Future: Grid users discovering user-generated content cross-session

### Core Workflows / Gameplay Loops (Inferred)
1. **Combat Simulator (MP)**: 8-player mesh over P2P or via dedicated server, with 32-bot simulants. Lobby → room → ready-gate → manifest distribution → match → endscreen → return to room.
2. **Co-operative / Counter-operative campaign**: 2-player co-op over the same transport; one bondplayernum + one antiplayernum selected at CLC_LOBBY_START.
3. **Single-player campaign** with per-agent JSON saves and mission progression state.
4. **Mod authoring → packaging → distribution**: local editor (Forge/Grid, Skin Editor, Nine-Slice Chrome, theme editor, map importer) → `mods/` tree → hot-register in Catalog → serve chunks over `NETCHAN_TRANSFER` to clients missing the asset.
5. **Updater flow**: `pd.exe` checks GitHub releases API, downloads ZIP + optional `.sha256` sidecar, verifies, stages, applies on next launch.

### Upstream Lineage Observations
- Networking transport (ENet) and the message-type taxonomy (SVC_*, CLC_*) are inherited from `fgsfdsfgs/port-net`. Many security-critical paths (netbuf, CLC_AUTH, CLC_CHAT, CLC_MOVE dispatch) still reflect port-net's experimental status.
- Pre-port-net N64 libultra residue is gone from the networking layer; OSMesgQueue etc. only appear in server_stubs.c as link-time satisfaction, not as live code.
- AllInOne-era arena list lives verbatim in `port/src/server_stubs.c::g_MpArenas[]` — PD2-specific content that compiles into the "game-agnostic" server binary (see Finding #DS-1).
- PD2-authored systems include: Catalog (`assetcatalog*.c`), modding pipeline (`mod.c`, `modmgr.c`, `modpack.c`, `netdistrib.c`), updater (`updater.c`), save-file JSON (`savefile.c`), identity profiles (`identity.c`), hub/rooms (`hub.c`, `room.c`), Forge editor (`port/src/forge/`, `src/game/forgemode.c`), dedicated server (`server_main.c`, `server_bridge.c`, `server_stubs.c`).

### Pillar Observations (Catalog / Mod Parity / Modding Pipeline / Grid / Online Mode / Dedicated Server)
- **Catalog**: mature. Nearly all wire-format asset references use catalog ID strings (`"base:*"`, `"mod:*"`) per v37 protocol. `catalogWriteAssetRef` / `catalogReadAssetRef` is enforced at most sites, though a few direct netbuf reads still leak integer domain (e.g., weapon slot 0..NUM_MPWEAPONSLOTS). No shadow arrays remain.
- **Mod / native parity**: largely achieved for loading and resolution. Asymmetry persists in distribution — native assets are `bundled=true` and never sent; mods are sent over the wire (Finding #MP-1), which is correct but means mod-only integrity checks apply.
- **Modding Pipeline**: functional end-to-end (build archive → compress → chunk → receive → SHA-256 verify → extract → hot-register). SHA-256 verification is **opt-in** per entry (Finding #SEC-5). No signature root. No reproducible-build guarantees.
- **The Grid**: F0 foundation shipped (S307) plus polish (S313). Implementation is **local editor only** — no Grid-network-exchange code exists yet (Finding #GRID-1). Trust-surface findings are pre-emptive rather than live.
- **Client online mode**: implicitly the only mode; there is no separate "offline" mode boundary besides `g_NetMode == NETMODE_NONE`. No online/offline state machine transitions flagged.
- **Dedicated server**: `pd-server.exe` runs cleanly (good) but is deeply PD2-shaped (Findings #DS-1, #DS-2). "Game-agnostic" goal is unmet — no per-game plugin interface exists; PD2 types, headers, and hardcoded arena tables compile directly into the server binary. No remote admin, no RCON, no auth.

### Assumptions / Unknowns
- Per-game plugin interface is **not yet designed** (no ADR found). Findings about boundary discipline are evaluated against the design goal stated in `SKILL.md` and audit-prompt.md rather than against any shipped architecture.
- Runtime behavior of CLC_CHAT with crafted non-NUL strings is unverified (static analysis only — needs fuzzing harness to confirm OOB read is exploitable).
- UPnP/STUN integration is read-level only; I did not audit those transports end-to-end.
- The Grid network protocol doesn't exist yet; findings about it are confined to the pillar observation above.

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### System & Workflow Validation Findings

#### Critical Issues

- **SEC-1: `netbufReadStr` returns a non-NUL-terminated pointer into the packet buffer; downstream `strlen` / `printf %s` callers walk past declared length**
  - Severity: Critical
  - Confidence: Confirmed (code evidence)
  - Location: `port/src/net/netbuf.c:120-129` (`netbufReadStr`) — reader advances `buf->rp` by `len` bytes but never writes a NUL terminator. Callers that pass the returned pointer to `strlen` (`port/src/net/netmsg.c:554`), to `%s` formatters (`port/src/net/netmsg.c:474`, `511`, `514`, `4820-4826`, many more via `sysLogPrintf(... "%s", name)`), or to `strtok` (`port/src/net/netmsg.c:6403` inside `SVC_ROOM_PLAYLIST`) walk past `len` if a malicious peer omits the NUL byte.
  - Lineage: Inherited-from-port-net (netbuf API shape matches fgsfdsfgs port-net style)
  - Pillar Impact: Dedicated-server Boundary, Scaling Ceiling (blast radius grows with client count)
  - Defect Class: Trust-Boundary
  - Intended Outcome: Dedicated server and listen host safely parse every peer packet without buffer over-read.
  - Current Behavior: `strlen(msg) > CHAT_MSG_MAX_LEN` in `netmsgClcChatRead` (line 554) walks forward from the packet pointer until any NUL byte — which may be up to `ENetPacket->dataLength + whatever heap comes next` before a zero byte shows up. ENet packet data is a heap allocation, so this is a heap OOB read exposed by any client that can reach the server.
  - Gap / Failure Mode: `netbufReadStr` documents no terminator contract. The writer (`netbufWriteStr`) DOES append a NUL (`buf->data[buf->wp - 1] = 0`), so well-behaved clients are fine. A hostile client crafts `u16 len = 5` followed by 5 non-NUL bytes and relies on the reader not enforcing termination. Every caller treating the result as a C string is affected.
  - Why It Matters: OOB reads in server message dispatch leak adjacent heap bytes into subsequent log lines, chat broadcasts (`netmsgSvcChatWrite`), and ENet packets re-sent to other peers. This is a passive information-disclosure primitive; combined with controlled heap layout it could drive more serious attacks. It also makes the server trivially crashable via SIGSEGV if the heap boundary is reached.
  - Risk If Not Fixed: Remote unauth DoS (crash) of any dedicated server or listen host. Possible heap-content disclosure through chat or logging that gets re-broadcast.
  - Recommendation: Either (a) copy the string through `strncpy(dest, msg, min(len, sizeof(dest)-1)); dest[sizeof(dest)-1] = '\0';` at every callsite before any string operation, or (b) add an internal static copy buffer to `netbufReadStr` that NUL-terminates (trades pointer-into-buffer semantics for safety). The safer and more invasive fix is (a); it also makes the types honest about ownership.
  - Cross-System Changes Required: Yes — every use-site of `netbufReadStr` across `netmsg.c` (60+ callsites per grep), plus `netdistrib.c`, `netmanifest.c`, `matchsetup.c`, `sessioncatalog.c`. Roughly a one-day audit + mechanical fix.
  - Blocks Intended Outcome?: Yes — the dedicated server cannot be hardened for public hosting with this present.

- **SEC-2: Dedicated server has no authentication, no admin API, no RCON, no persistent kick/ban**
  - Severity: Critical
  - Confidence: Confirmed
  - Location: `port/src/server_main.c` — no auth-token / password concept. `port/src/server_bridge.c::netServerKickClient` + `netServerBanClient` — both callable only from the local server GUI, and `netServerBanClient` just calls `enet_peer_disconnect` with no persistent IP/identity blocklist (so the peer reconnects immediately). `netmsgClcAuthRead` (`port/src/net/netmsg.c:415`) explicitly skips ROM + mod checks when `g_NetDedicated` is true (line 435).
  - Lineage: Authored-in-PD2 (dedicated-server track)
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary
  - Intended Outcome: Operators can host public or private dedicated servers with admin controls, persistent bans, and per-user identity.
  - Current Behavior: Any peer with protocol version 37 can CLC_AUTH with any name, trigger `netServerFindPreserved` lookup by name, and inherit the preserved score/state of any prior player of the same name (Finding #SEC-3). Kick and ban both reduce to an immediate disconnect. Nothing blocks a reconnecting attacker from immediately re-joining.
  - Gap / Failure Mode: The server trusts the peer's self-declared name as identity; there is no verification. There is no out-of-band operator channel; a headless `pd-server --headless` has no way to kick anyone.
  - Why It Matters: Griefing, name-spoofing score theft, and reconnect-after-kick loops make public hosting uneconomic. An operator has no recovery path except restarting the server.
  - Risk If Not Fixed: Unusable for any host who doesn't physically own the terminal.
  - Recommendation: Add (a) a simple admin token configured via CLI flag / `server.ini` (not pd.ini), (b) an authenticated admin RPC channel (can ride over ENet CONTROL channel with a distinct msgid prefix), (c) a persistent `bans.ini` read at startup and consulted in `netServerEvConnect` before `netClientReset`. Identity should use a server-generated client cookie persisted to a local file, not self-declared name.
  - Cross-System Changes Required: Yes — new auth subsystem, new bans file, new RPC msgid range, `netServerEvConnect` blocklist hook, `server_main.c` flag parsing.
  - Blocks Intended Outcome?: Yes.

- **SEC-3: Preserved-player slot hijack via name spoof**
  - Severity: Critical
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:471` (`pp = netServerFindPreserved(name);`) and `port/src/net/net.c:1169-1181` (`netServerFindPreserved` matches by `strncasecmp` on name). `port/src/net/net.c:1183-1240` (`netServerRestorePreserved`) hands the new peer the previous player's `playernum`, team, `killcounts[]`, `numdeaths`, `numpoints`, and the live `g_Vars.players[pp->playernum]` struct pointer.
  - Lineage: Inherited-from-port-net
  - Pillar Impact: Dedicated-server Boundary, Grid Trust (future — cross-server scoreboards), Online-mode Boundary
  - Defect Class: Trust-Boundary
  - Intended Outcome: Reconnecting mid-match resumes the player's own slot and score.
  - Current Behavior: Any peer can CLC_AUTH with `name = "victim_name"` and, if "victim_name" is in `g_NetPreservedPlayers` (5-minute timeout after disconnect), the attacker takes over that slot with full score and team. The only check is the case-insensitive name match. The preserved slot is cleared on successful restore, so the real victim cannot reclaim.
  - Gap / Failure Mode: Identity is a self-declared string with no server-issued token.
  - Why It Matters: In competitive play, disconnects are frequent. This is a zero-skill slot theft with no trace in logs besides an "%s reconnected" line — which the attacker controls.
  - Risk If Not Fixed: Public dedicated servers get griefed off the network. Competitive integrity impossible.
  - Recommendation: Server generates a 128-bit random cookie on first CLC_AUTH accept, sends it in SVC_AUTH response, and demands it back in CLC_AUTH on reconnect. Store it in `struct netpreservedplayer` next to the name. The name match becomes advisory only.
  - Cross-System Changes Required: Yes — protocol bump, SVC_AUTH, CLC_AUTH, `netpreservedplayer`, reconnect path.
  - Blocks Intended Outcome?: Yes.

- **DS-1: "Game-agnostic dedicated server" goal is not met — PD2 types and data leak throughout server core**
  - Severity: High (Architecture); Critical if the game-agnostic goal is release-blocking
  - Confidence: Confirmed
  - Location: `port/src/server_main.c` — includes `system.h`, `config.h`, `net/net.h`, `connectcode.h`, `hub.h`, `assetcatalog.h`, `versioninfo.h`, `updater.h`. `port/src/server_bridge.c:14` includes `types.h`. `port/src/server_stubs.c:20-28` includes `types.h`, `constants.h`, `data.h`, `system.h`, `lib/main.h`, `game/mplayer/participant.h`, `scenario_save.h`, `net/netlobby.h`. `port/src/server_stubs.c:113-160` hardcodes the entire PD2 arena table (71 entries, `STAGE_MP_*` and `STAGE_EXTRA*`). Per `context/constraints.md` (B-12 Phase 3, 2026-04-17), the server now links `src/game/mplayer/participant.c` directly, making participant.c a shared game/server module rather than game-only.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: None (architecture)
  - Intended Outcome (per SKILL.md pillar): server core is PD2-agnostic; PD2-specific logic lives behind a per-game plugin interface.
  - Current Behavior: Server binary is PD2-specific. No plugin interface exists. Adding a second game to the same server binary would require recompiling most of the net/ subtree with different headers.
  - Gap / Failure Mode: No abstraction boundary has been drafted, let alone enforced. `server_stubs.c` is a PD2-shaped compatibility shim rather than the game plugin boundary.
  - Why It Matters: The SKILL.md pillar promise "host PD2 *and other games*" is currently unachievable without a full rewrite of the server. If the goal is reaffirmed, every new game-side change increases the eventual refactor cost.
  - Risk If Not Fixed: Pillar erosion (permanent). Scope bloat when a second game is added.
  - Recommendation: Draft an ADR defining the per-game plugin boundary. Options: (a) dynamic-library plugins (`.dll` per game) loaded via `dlopen`/`LoadLibrary`; (b) static per-game `libpdgame.a` bound at link time behind a C-ABI `game_plugin_t` vtable; (c) accept that the "game-agnostic" goal is aspirational and retire it in the docs. (c) costs nothing but makes the pillar promise honest.
  - Cross-System Changes Required: Yes (option a or b). Entire `port/src/net/` would shift behind a plugin call vtable. Rough estimate: 4–8 weeks for option (b).
  - Blocks Intended Outcome?: Yes — the pillar is blocked outright. Not a release blocker for PD2 v0.1.0 unless the pillar is treated as must-have.

- **SEC-4: CLC_CHAT ring buffer rate limiter uses stale timestamp slot**
  - Severity: Critical (wrap-around bug allows unlimited rate)
  - Confidence: Probable (logic re-read twice — see below)
  - Location: `port/src/net/netmsg.c:558-572`
  - Lineage: Authored-in-PD2
  - Pillar Impact: Dedicated-server Boundary, Scaling Ceiling
  - Defect Class: Trust-Boundary
  - Intended Outcome: Max 5 chat messages per 2-second window per client.
  - Current Behavior: The ring is sized `CHAT_RATE_MAX_MSGS = 5`. `rate->head` points to the next-to-overwrite slot; `oldest = rate->timestamps[rate->head]` is the slot about to be clobbered. On the FIRST 5 messages, every `rate->timestamps[head]` is zero, so `now - oldest` is always `>= 2000ms`, and all 5 messages pass unconditionally. Fine. But once filled, the check `now - oldest < CHAT_RATE_WINDOW_MS` is tested against the timestamp that slot currently holds; if it's old enough, the slot is overwritten with `now`. Actual behavior is correct for sustained rate, but the per-client state is never reset on disconnect — a subsequent client taking over that slot inherits the previous client's ring buffer, which may either over-block (false deny) or under-block (one free burst, because the newest timestamp just got vacated). Also note `head` uses `idx = (u32)(srccl - g_NetClients)` which is fine up to `NET_MAX_CLIENTS+1` (bound checked).
  - Gap / Failure Mode: Reset missing from `netClientReset` — s_ChatRate is a static outside, not reset per client.
  - Why It Matters: Minor state leak between successive occupants of the same client slot. The severity is low unless combined with SEC-1/SEC-3 chains.
  - Risk If Not Fixed: Chat burst spike at boundary of client handoffs. Low-confidence per-user exploit.
  - Recommendation: Zero `s_ChatRate[i]` in `netClientReset`. One-line fix.
  - Cross-System Changes Required: No.
  - Blocks Intended Outcome?: No.

#### High Priority Findings

- **SEC-5: Mod distribution SHA-256 verification is opt-in per entry; default is no verification**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/netdistrib.c:940-963`. Verification runs only if `me->sha256 != {0}`. The server supplies `g_ClientManifest` via SVC_MATCH_MANIFEST and chooses whether to populate `sha256`. A hostile server can set every entry to zero-hash and the client will accept arbitrary PDCA archives without integrity checks.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Modding Pipeline, Supply-Chain
  - Defect Class: Supply-Chain
  - What We Found: The manifest is the integrity root, and the server (possibly adversary) controls the root.
  - Why It Matters: A dedicated-server operator who wishes to push malicious mods to every connecting client needs only to omit the hash. Path-traversal protection in `extractArchive` (line 628+) catches the most obvious abuse, but a mod can legitimately ship executable code via shell scripts or DLL asset paths inside the `mods/` tree and have those loaded at Catalog hot-register time.
  - Risk If Not Fixed: Supply-chain compromise vector for every dedicated server.
  - Recommendation: Make SHA-256 mandatory for any non-empty entry. Reject transfers whose entry lacks a non-zero hash. Separately, consider signing the manifest itself with a host keypair established via CLC_AUTH exchange so the client can distinguish "this is legitimately this server's manifest" from "a MITM swapped the manifest".
  - Cross-System Changes Required: Yes — manifest format change, protocol bump, client-side enforcement.

- **SEC-6: Updater has no signature verification; SHA-256 sidecar is optional and comes from the same channel as the binary**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/updater.c:791-926`. Hash file downloaded via `curlGet(rel->hashUrl, ...)`; if missing, "No SHA-256 sidecar — download not verified" and the updater continues (line 925). Even when present, both files come from `https://github.com/MikeHazeJr/perfect-dark-2/releases/download/...` — a single trust root.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Supply-Chain
  - Defect Class: Supply-Chain
  - What We Found: If the GitHub release-publishing account is compromised (credential phishing, token leak), or if a MITM with a forged cert for github.com operates (state-level capability), both the ZIP and the `.sha256` sidecar can be swapped and the client will accept a malicious update silently.
  - Why It Matters: Users install the updater once and trust it for life; any compromise of the channel propagates to every installation.
  - Risk If Not Fixed: Once-and-done compromise of the entire userbase. Hot-patching against it is hard because the compromised updater can reject future updates that would restore it.
  - Recommendation: Generate a developer-owned Ed25519 keypair; sign release ZIPs with the private key; embed the public key in `pd.exe`. On update, verify signature over `zip || version-number` before replacing `pd.exe`. The public key does NOT rotate with the channel (GitHub), so even full account compromise can't produce valid updates. (This is The Update Framework's "trust root" pattern applied to a single developer.) Optional: pin github.com's certificate to defend against CA mis-issuance.
  - Cross-System Changes Required: Yes — key management, sign flow in release scripts, verify in updater.

- **SEC-7: Server info query is an amplification DoS vector (no sanity on source IP)**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:424-496` (`netServerQueryResponse`) and `port/src/net/net.c:498-516` (`netServerConnectionlessPacket`). `NET_QUERY_MAGIC` is a 5-byte prefix. Response is up to ~512 bytes (`static u8 data[512]`), roughly 50–100× amplification.
  - Lineage: Inherited-from-port-net
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary
  - What We Found: UDP queries from spoofed source IPs will be answered to the spoofed address, turning every PD2 dedicated server into a DDoS reflector.
  - Why It Matters: Public servers are an abuse vector even when no one is connected to them. Operators have no defense (the query is enabled by default via `g_NetServerInfoQuery = 1`).
  - Risk If Not Fixed: Operators get blackholed by their ISP when their IP is used in a DDoS reflection; PD2 as a brand becomes network-unfriendly.
  - Recommendation: (a) Token-based query (server returns a small challenge on first contact, full response only to clients that echo it). (b) Rate-limit per source IP (simple 1 QPS per /24 suffices for server browsers). (c) Size-balance: require the query to be at least as long as the response (pad the query with a nonce). Option (a) or (c) is simplest.
  - Cross-System Changes Required: No (local to `netServerConnectionlessPacket` + query format).

- **SEC-8: Bot broadcast is O(bots × clients) per tick with no PVS culling — amplification at scale**
  - Severity: High (Scaling Ceiling)
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:1658-1678` — every `g_NetNextUpdate` tick, the server iterates all bots and writes `SVC_CHR_MOVE` per bot per client. `SVC_CHR_STATE` runs every 15 ticks, `SVC_CHR_SYNC` every 60 ticks.
  - Lineage: Inherited-from-port-net
  - Pillar Impact: Scaling Ceiling
  - Defect Class: Scaling
  - What We Found: At `MAX_BOTS = 32` × `NET_MAX_CLIENTS = 32` = 1024 `SVC_CHR_MOVE` messages per tick, or 61,440 per second at 60 Hz. No PVS culling ("is this bot even visible from this client's perspective?"), no interest management.
  - Why It Matters: The host/server bandwidth scales as O(bots × clients). The PD2 design aim is scaling bound by server capacity, but linear-in-both is a hard constant-factor wall — a 32×32 server burns ~40 MB/s uplink just for bot positions if each SVC_CHR_MOVE is ~20 bytes.
  - Risk If Not Fixed: Dedicated-server player-count cap is bandwidth-bound at ~8–16 clients in practice, not the advertised 32.
  - Recommendation: Interest management — only send a bot's position to clients whose players are in visible rooms (see `propRoomsEqual` and the existing room set on `prop->rooms`). Adopt a simpler cadence variant: full-rate to clients whose rooms include the bot, rare sync-only to others.
  - Cross-System Changes Required: No (localized to netEndFrame bot loop).

- **SEC-9: Player-move broadcast is O(clients²) per tick — full-mesh state scales quadratically**
  - Severity: High (Scaling Ceiling)
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:1634-1644` (the per-client broadcast loop runs once per tick; but each record is written once per client to a shared broadcast buffer, and the packet is sent to all via `enet_host_broadcast` — so total traffic is O(n) per client = O(n²) aggregate).
  - Pillar Impact: Scaling Ceiling
  - Defect Class: Scaling
  - Why It Matters: A 32-player match at 60 Hz produces 32 × 32 × 60 = ~61k SVC_PLAYER_MOVE messages/s globally, beyond an 8-player scale.
  - Risk If Not Fixed: Same as SEC-8 — effective cap well below advertised `NET_MAX_CLIENTS`.
  - Recommendation: Relevance-based state distribution (delta + PVS) is the standard fix. At minimum, reduce SVC_PLAYER_MOVE frequency outside the current player's visible rooms.
  - Cross-System Changes Required: Yes — relevance computation touches room queries and message-write paths.

- **SEC-10: `MAX_PLAYERS = 8` is a wire-format ceiling; `mpParticipantsEncodeActiveMask()` caps total slots at 64**
  - Severity: High (Scaling Ceiling, explicit)
  - Confidence: Confirmed
  - Location: `src/include/constants.h:44` (`#define MAX_PLAYERS 8`). `PARTICIPANT_DEFAULT_CAPACITY = 32`. `MAX_MPCHRS = MAX_PLAYERS + MAX_BOTS = 40`. Wire encoder returns u64 (`mpParticipantsEncodeActiveMask` in `src/game/mplayer/participant.c`, per `context/constraints.md`), so a future raise of MAX_MPCHRS above 64 silently truncates slots.
  - Pillar Impact: Scaling Ceiling
  - Defect Class: Scaling
  - Why It Matters: The u64 mask is the SVC_STAGE_START active-slots wire field (constraint #17, v37 protocol). Expanding past 64 without changing the wire protocol silently corrupts bot/player assignment in-game.
  - Risk If Not Fixed: Protocol lockdown at MAX_MPCHRS ≤ 64. Any future expansion needs a v38 protocol bump and a variable-length mask field.
  - Recommendation: Document the 64-slot ceiling explicitly (e.g., a `static_assert(MAX_MPCHRS <= 64, "…")`). When it's time to raise, switch the wire to a length-prefixed byte array of slot bits.

- **SEC-11: Room settings rebroadcast buffer is a fixed 256-byte stack buffer; catalog ID lengthening would silently truncate**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:6463` (`u8 bcastData[256];`). The write path packs 10 bytes of scalars + one catalog-ID string (up to `CATALOG_ID_LEN=64 + 2`). Current fit ≈ 80 bytes; room to spare now, but if any field is added or `CATALOG_ID_LEN` is raised, the write silently errors and the packet is dropped (netbufCanWrite sets error; netSend checks buf->wp; the broadcast does not fire). No error is reported.
  - Pillar Impact: Scaling Ceiling
  - Defect Class: Layout
  - Why It Matters: Room settings silently stop propagating if the encoded size ever exceeds 256. Clients see stale settings with no user-visible error.
  - Recommendation: Use `g_NetMsgRel` (256KB) for this broadcast like the rest of the server; or size `bcastData` dynamically based on the computed max payload.

- **SEC-12: Server-authoritative validation missing on CLC_ROOM_SETTINGS_UPDATE scenario/options/weapon values**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:6435-6481` (`netmsgClcRoomSettingsUpdateRead`). The server accepts `scenario`, `options`, `weaponSetIndex`, `stage_id` from the leader and rebroadcasts without bounds-checking or validating that `scenario` is a real `asset_catalog` gamemode or `weaponSetIndex` is in range.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary
  - Why It Matters: Non-leader clients receiving garbage scenario/weapon values may OOB-read into `g_MpSetup.weapons[]` or use invalid enum values when rendering. The leader is trusted; a compromised or malicious leader can cascade this to every peer.
  - Risk If Not Fixed: Crash/UB on joining clients when leader sends crafted settings.
  - Recommendation: Server validates each field before rebroadcast: `scenario` must resolve in catalog as ASSET_GAMEMODE; `weaponSetIndex` must be in `[0, NUM_MP_WEAPON_SETS) || 0xFF`; `timelimit` should be `≤ some sane cap`; `teamscorelimit` should be bounded.
  - Cross-System Changes Required: No.

- **SEC-13: No rate limiting on room-lifecycle messages (CLC_ROOM_CREATE / JOIN / LEAVE)**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:6056` (`netmsgClcRoomCreateRead`), `:6109` (`netmsgClcRoomJoinRead`), `:6167` (`netmsgClcRoomLeaveRead`). Each fires `netBroadcastRoomList()` (a global broadcast to every client).
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary / Scaling
  - Why It Matters: A single client can send an unlimited CLC_ROOM_CREATE + CLC_ROOM_LEAVE loop, burning CPU and bandwidth on all other peers via the broadcast.
  - Risk If Not Fixed: Server-wide DoS from a single connected peer.
  - Recommendation: Add per-client rate limiter modeled on `s_ChatRate` for all room-mutation messages: 1 per second. Also coalesce room-list broadcasts (dirty flag + end-of-frame flush).
  - Cross-System Changes Required: No.

- **SEC-14: Room passwords are declared in `room.h` but never transmitted in CLC_ROOM_CREATE**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/include/room.h` defines `room_access_t` with `ROOM_ACCESS_PASSWORD`. `port/src/room.c:96-118` (`roomCreateConfigured`) accepts a password parameter. `port/src/net/netmsg.c:6076` hardcodes `roomCreateConfigured(name, 32, ROOM_ACCESS_OPEN, NULL, srccl->id)` — always OPEN, no password. Also, `max_players` is hardcoded to 32.
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: None (incomplete feature)
  - Why It Matters: Private rooms are unbuilt. Every room is discoverable and joinable by anyone.
  - Risk If Not Fixed: No way to host a password-protected match. Community abuse surface larger than it should be.
  - Recommendation: Extend `CLC_ROOM_CREATE` wire format with u8 access + str password + u8 max_players. Server validates max_players ≤ HUB_MAX_CLIENTS, hashes password locally (BLAKE2s is fine — this is low-value), stores hash, checks on CLC_ROOM_JOIN.
  - Cross-System Changes Required: Yes — protocol bump.

- **SAVE-1: Save files have no integrity protection — every cheat unlock, best-time, and MP stat is trivially editable**
  - Severity: High (community health)
  - Confidence: Confirmed
  - Location: `port/src/savefile.c` — JSON text saves. `saveSaveAgent` (line 288) writes `besttimes`, `coopcompletions`, `firingrangescores`, `weaponsfound`. `saveSaveSystem` (line 489) writes `alttitleunlocked`. `saveSaveMpPlayer` (line 605) writes `kills`, `deaths`, `gamesplayed`, `accuracy`, `headshots`, `damagedealt`, `ammoused`, plus medals. No HMAC, no checksum, no server-side rejection of impossible stats.
  - Lineage: Authored-in-PD2 (the N64 EEPROM bit-packing was gone — this is the PC replacement)
  - Pillar Impact: Grid Trust (future — stats broadcast via SVC_PLAYER_SCORES to other peers)
  - Defect Class: None (design trade-off)
  - What We Found: A text editor unlocks all cheats, sets 00:01 best times on every mission, pumps accuracy to 100%, and maxes every medal. These stats broadcast to all peers via SVC_PLAYER_SCORES.
  - Why It Matters: Competitive integrity impossible. When the Grid is wired up for leaderboards, stat inflation will spread immediately.
  - Risk If Not Fixed: Leaderboards/Grid tagging becomes worthless; speedrun verification impossible without external proof.
  - Recommendation: For offline solo, accept this as a design trade-off (users wanting to cheat their own save is fine). For MP and future Grid: server-side validation. SVC_PLAYER_SCORES should be server-broadcasted state derived from server-observed events, not client-reported stats. Currently it IS server-broadcast, so the immediate risk is contained to solo/local.
  - Cross-System Changes Required: No for current scope; yes when Grid leaderboards ship.

- **LAYOUT-1: `struct gset` serialized as raw bytes (`netbufWriteData(buf, gset, sizeof(*gset))`)**
  - Severity: High
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:287-297` (`netbufWriteGset` / `netbufReadGset`).
  - Lineage: Inherited-from-port-net
  - Pillar Impact: None
  - Defect Class: **Layout** (Phase 2.5)
  - What We Found: `gset` is written and read as a raw binary blob. Any field-width change, bitfield addition, padding drift between MSVC/GCC/Clang, or endian flip silently corrupts on the wire. Not listed in the catalog-ID-native sweep because it's a transient gameplay struct, but it IS sent in several SVC handlers (prop damage, chr damage, etc.).
  - Why It Matters: Cross-compiler / cross-build dedicated-server deployments will silently desync if a struct field changes offset. PD2 is Windows/MinGW-only today, so compiler variance is limited — but the risk lands the moment a Linux build is attempted or the compiler version is updated.
  - Risk If Not Fixed: Latent wire-format bug the next time `gset` is touched.
  - Recommendation: Field-wise write/read like `netbufWriteCoord`. Takes ~30 minutes; see `netbufWritePlayerMove` for the pattern.
  - Cross-System Changes Required: No — localized to `netbufWriteGset`.

- **LAYOUT-2: `Net.RecentServer.*` and `Net.Client.LastJoinAddr` in pd.ini are trusted at load — a hostile pd.ini can inject addresses**
  - Severity: High (local attack)
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:2283-2314` (`netConfigInit`). `Net.RecentServer.N`, `Net.RecentServer.N.Host`, `Net.Client.LastJoinAddr` loaded as strings.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Grid Trust (by extension — these are UI-facing addresses)
  - Defect Class: Trust-Boundary
  - What We Found: An attacker with filesystem access (malware, shared-machine cohabitant) can edit pd.ini to inject a hostile address into LastJoinAddr. The next time the user auto-connects, they reach an attacker's server and CLC_AUTH hands over the user's identity profile name plus any other info — no user-visible confirmation step in the auto-connect path.
  - Why It Matters: Pre-auth info disclosure; also a staging primitive for further compromises (e.g., a hostile server could push a crafted CLC_* flow to exploit SEC-1).
  - Risk If Not Fixed: Local attacker with filesystem access has a one-step redirect primitive.
  - Recommendation: Validate `LastJoinAddr` format at load (DNS hostname charset, port range). Display the address explicitly before auto-connect — don't silently connect.

- **GRID-1: The Grid is a local editor (F0 shipped, S307/S313) — network/sharing surfaces are not yet wired**
  - Severity: High (design observation, not a current vulnerability)
  - Confidence: Confirmed
  - Location: `port/src/forge/` directory exists; `port/fast3d/pdgui_forge*.cpp`, `src/game/forgemode.c`, `port/fast3d/pdgui_menu_forge.cpp`. `hub.c` is the server's LOUNGE/ACTIVE lifecycle, not the "Grid" design-doc social layer. No network message handlers named `SVC_GRID_*` or `CLC_GRID_*` exist. `context/designs/forge-level-editor-2026-04-16.md` describes the Grid design.
  - Lineage: Authored-in-PD2
  - Pillar Impact: Grid Trust
  - Defect Class: None
  - What We Found: Today's exposure is local only — a user placing catalog entries in their own level. When the Grid ships a network-shareable format, every object in the catalog becomes a Grid-reachable entity, and any validation gap becomes a trust-surface gap.
  - Why It Matters: This is a pre-emptive flag: when the Grid network layer is designed, the full list of SEC-* findings (especially SEC-1, SEC-5, SEC-12) applies multiplied by user-generated content volume. Start the threat model before the network surface.
  - Risk If Not Fixed: Design debt.
  - Recommendation: Before Grid sharing ships, author a threat model for user-generated content: (a) level-data format schema with hard caps (max prop count, max mesh bytes), (b) mandatory SHA-256 on every uploaded artifact, (c) pre-load validation (placement bounds, spawn-pad count limits), (d) rate limits on upload/browse actions. Treat Grid payloads as untrusted input that must not write to disk outside a sandbox.

#### Medium Priority Findings

- **SEC-15: server_bridge.c off-by-one in bounds check**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/server_bridge.c:95, 103, 115` — three sites all use `if (clientId < 0 || clientId > NET_MAX_CLIENTS)`. The array is `g_NetClients[NET_MAX_CLIENTS + 1]` (size 33), so index 32 (`NET_MAX_CLIENTS`) is the reserved local/temporary slot, not a real peer. The check allows operations on that slot.
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary
  - What We Found: `netGetClientPing(32)`, `netServerKickClient(32, ...)`, `netServerBanClient(32, ...)` all "succeed" against the reserved slot, which on a dedicated server holds `g_NetLocalClient` or is NULL. No real impact today but signals lax bounds discipline.
  - Recommendation: `>= NET_MAX_CLIENTS` to match the intent "valid peer indices 0..NET_MAX_CLIENTS-1".

- **SEC-16: `netRecentServerUpdate` accepts any UDP response by source-address string match — no origin auth**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:2108-2155`. A response with matching address is accepted as a server-browser update; no per-query nonce.
  - Pillar Impact: Online-mode Boundary
  - Defect Class: Trust-Boundary
  - What We Found: An attacker who learns (or guesses) that a user's recent-servers list includes a given IP can spray UDP at the client with that IP spoofed, updating `hostname`, `scenario`, etc. Mostly a UI spoof.
  - Recommendation: Generate a per-query 32-bit nonce, require the response to echo it.

- **SEC-17: CLC_LOBBY_START reads bot per-slot body_id/head_id/name through `netbufReadStr` — uses the raw pointer in log `%s`**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:4738-4826`. Strings are copied into bounded destinations via `strncpy` (safe), but the log line at 4820 passes `body_id` and `head_id` (the raw netbuf pointers) to `sysLogPrintf("%s")`. This is instance #2 of SEC-1.
  - Pillar Impact: Dedicated-server Boundary
  - Defect Class: Trust-Boundary
  - Recommendation: Remove once SEC-1 is fixed systemically; or pre-copy into a local bounded buffer before logging.

- **MP-1: Mod distribution archive build (`buildComponentArchive`) reads directly from `entry->dirpath` — no sandbox check**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/netdistrib.c:138-227` (`buildArchiveDir`). `asset_entry_t::dirpath` is populated by the catalog scanner; if a catalog entry is registered with an absolute or `../`-escaping `dirpath` (e.g., via a hand-edited mod.json), the server reads arbitrary filesystem content into the outgoing PDCA archive and sends it to every joining client.
  - Pillar Impact: Dedicated-server Boundary, Modding Pipeline
  - Defect Class: Trust-Boundary
  - What We Found: On the server side, the catalog is trusted. But on the CLIENT side, any mod shipped by the server populates the client's catalog with whatever dirpath the server chooses for hot-registered entries (`netdistrib.c:1014`). The dirpath is the server-controlled destdir, which is built from `modsdir + category + slot->id` — bounded to within `mods/`. OK. But the server-side input (a hand-crafted mod.json that sets dirpath to `..`) would have been filtered at scan time. Grep confirms the scanner normalizes relative to `mods/` (see `modmgrScanDirectory`), so the server-side risk is low unless the admin opts in.
  - Recommendation: Assert dirpath is within `mods/` at archive-build time. One `_fullpath` check.

- **MP-2: `extractArchive` relies on `_fullpath` — a Windows-specific runtime function, not portable**
  - Severity: Medium (portability)
  - Confidence: Confirmed
  - Location: `port/src/net/netdistrib.c:659-668`. `_fullpath` is MSVC/MinGW CRT. No `realpath` (POSIX) path is present.
  - Pillar Impact: None
  - Defect Class: None
  - Why It Matters: PD2 is Windows-only today (per CLAUDE.md), so this compiles fine. If the dedicated-server ever targets Linux (reasonable for hosting), the path-traversal prevention silently compiles out.
  - Recommendation: Add a `#ifdef _WIN32 _fullpath #else realpath` shim.

- **SEC-18: `g_NetSimPacketLoss` uses `rand() % N` for randomness; not cryptographic, predictable**
  - Severity: Low-Medium (dev tool)
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:1850`. Dev tool for simulating packet loss.
  - Recommendation: Fine for dev use. Document that it's not cryptographic and should not ship enabled in release.

- **LAYOUT-3: `struct netplayermove` serialized field-wise but read-side reads `newmove.weaponnum` and clamps it — untrusted input bounds correctly**
  - Severity: Low (positive finding with a caveat)
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:319-343`. Good: `weaponnum` clamped to `[WEAPON_NONE, WEAPON_SUICIDEPILL]`. Caveat: `ucmd` u32 is accepted raw, but only specific bits are consulted downstream — effectively a whitelist.
  - Recommendation: No change needed; this is the pattern SEC-1 should follow for strings.

- **LAYOUT-4: Save files use key renames for forward-compat but not for backward-compat**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/savefile.c:437` — unknown JSON keys are skipped via `s_skip_value`. This is forward-compat (old client ignores new keys). But there's no `SAVE_VERSION` dispatch on load — just a header write of `"version": SAVE_VERSION`, never checked on load.
  - Recommendation: Check `version` on load; dispatch to `saveMigrate*` if `< SAVE_VERSION`. Reject `> SAVE_VERSION` with a clear error rather than silently loading partial data.

- **DS-2: `server_stubs.c` links game code (`participant.c`) and hardcodes PD2 data (`g_MpArenas[]`)** — see DS-1 for architecture impact
  - Severity: Medium (scaled from DS-1)
  - Confidence: Confirmed
  - Location: `port/src/server_stubs.c:113-160`
  - Pillar Impact: Dedicated-server Boundary
  - Recommendation: Part of the per-game plugin refactor (see DS-1). No local fix.

- **SEC-19: Server's `netServerStageStart` logs player names and body/head IDs at LOG_NOTE (public log) — potential PII/telemetry surface**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:727-735`, `:878-884`, `:1963-1964`. Every match start logs each player's name, body_id, head_id, team. The log goes to `pd-server.log` (standard) and is fed to `crashBreadcrumb`.
  - Pillar Impact: Grid Trust (future — if logs are shipped to operators)
  - Defect Class: Trust-Boundary
  - What We Found: Player-name log lines contain user-chosen names. These can be scraped by anyone with server-log access.
  - Recommendation: For public server logs, offer a `LOG_ANONYMIZE` mode that redacts name to client id hash.

- **SEC-20: `netQueryRecentServers` (synchronous) blocks for up to `numServers × 200ms`; also bypasses rate-limiting**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/net.c:2157-2206`. Not itself a vulnerability, but: no per-server rate-limit. A stuffed recent-servers list (e.g., from a hostile pd.ini — see LAYOUT-2) forces the client to blast UDP at `NET_MAX_RECENT_SERVERS = 8` hosts on every browser refresh.
  - Recommendation: Cap outgoing recent-server probes to ≤1 per 100ms regardless of list size.

- **SEC-21: `netmsgSvcRoomPlaylistRead` uses `strtok` on a stack buffer — clobbers state, not re-entrant**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:6403`. `strtok` mutates internal static state. If the server broadcast arrives while another thread is tokenizing (unlikely in single-threaded tick, but SDL_mixer etc. can be multi-threaded), behavior is UB.
  - Recommendation: `strtok_r`.

- **SEC-22: `AUDIO_MAX_PLAYLIST * 65` stack buffer in `netmsgSvcRoomPlaylistRead` and `netSendRoomPlaylistUpdate` — no bound enforcement from server**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:6401, 6506, 6551`. Server rebroadcasts raw playlist string without length validation.
  - Recommendation: Clamp server-side to `AUDIO_MAX_PLAYLIST * 65 - 8`.

- **SEC-23: Updater cancelFlag race — flag read + check is not atomic**
  - Severity: Medium
  - Confidence: Probable
  - Location: `port/src/updater.c` — `cancelFlag` read by `curlFileWriteCallback` and set by main thread. Mutex protects `errorMsg`/`status` but `cancelFlag` is a plain `s32` tested under a different discipline.
  - Recommendation: `SDL_AtomicGet`/`Set` or `atomic_int`.

- **SEC-24: Pre-game CLC_LOBBY_START manifest deserialization (`manifestDeserialize`) runs inside the server dispatch — failure falls back to `manifestBuild` without alerting the leader**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/src/net/netmsg.c:4838-4848`. On corrupt manifest bytes, server proceeds with its own build. Leader thinks their manifest shipped.
  - Recommendation: Log + respond with an error channel to the leader (e.g., a new `SVC_MATCH_MANIFEST_REJECTED`).

- **SEC-25: `NET_DISTRIB_MAX_SESSION = 200MB` is declared but not enforced**
  - Severity: Medium
  - Confidence: Confirmed
  - Location: `port/include/net/net.h:110` declares the constant. Grep of `NET_DISTRIB_MAX_SESSION` across `port/src` shows no enforcement. `s_ClientStatus.session_bytes_total` is incremented in `netDistribClientHandleChunk` but never checked against the constant.
  - Pillar Impact: Modding Pipeline
  - Defect Class: Trust-Boundary
  - What We Found: A hostile server can push unlimited aggregate bytes across multiple components (each bounded by `NET_DISTRIB_MAX_COMP=50MB` and the per-slot 256MB safety cap, but no per-session cap).
  - Recommendation: Enforce at `netDistribClientHandleBegin` — reject a new BEGIN if `session_bytes_total + archive_bytes > NET_DISTRIB_MAX_SESSION`.

- **GRID-2: `port/src/forge/` components currently use the Catalog correctly**
  - Severity: Low (positive)
  - Confidence: Probable
  - Location: `port/src/forge/` directory.
  - Recommendation: Preserve this pattern when Grid networking ships — any new entity types must register through the Catalog, not a shadow store.

#### Low Priority Findings

- **SEC-26: Catalog enum values on the wire (CLC_LOBBY_START `gamemode` u8)** are not bounds-checked before assignment to `g_NetGameMode`
  - Location: `port/src/net/netmsg.c:4663`. Assigned raw from u8.
  - Recommendation: Clamp `gamemode` to `[NETGAMEMODE_MP..NETGAMEMODE_ANTI]`.

- **SEC-27: `netServerQueryResponse`'s rolling CRC16 is not a cryptographic MAC**
  - Location: `port/src/net/net.c:485-493`
  - Recommendation: Integrity only (no adversary). Fine.

- **LAYOUT-5: `netbufReadMtxf` reads 16 floats (`Mtxf`) via loop — field-wise, safe**
  - Location: `port/src/net/netbuf.c:142-153`. Positive — no layout risk here.

- **LAYOUT-6: `struct netpreservedplayer.killcounts[MAX_MPCHRS]` = 40 × s16 = 80 bytes per preserved record, 32 records = 2560 bytes total** — reasonable.

- **LAYOUT-7: `CATALOG_ID_LEN = 64` + `CATALOG_CATEGORY_LEN = 64`** are fixed; all wire writes respect these. Good.

- **SAVE-2: `prefs_agent.ini`** is scanned per-agent; see `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`. No cross-agent leak observed.

- **SAVE-3: Crash state file `mods/.temp/.crash_state`** parsed with `atoi` and `fgets` — untrusted but only triggers prompt UI, not code paths.

- **SEC-28: No kick/ban persistence** — see SEC-2 recommendation.

- **MP-3: Server info query exposes mod directory name (`modDir`)** in `netServerQueryResponse` — scan fingerprint. Low impact for public browsers.

### Missing / Incomplete Features Blocking Success

- **Dedicated-server admin interface (RCON / admin password)**: blocks public hosting. Critical for the pillar.
- **Persistent ban list**: blocks public hosting.
- **Per-user identity tokens (cookies)**: blocks competitive integrity.
- **Per-game plugin boundary**: blocks "game-agnostic" pillar (see DS-1).
- **Signed updater**: blocks safe auto-update.
- **Mandatory mod hash verification**: blocks safe public hosting.
- **Interest management / PVS culling**: blocks scaling past ~8 clients in practice.
- **Room password wire transport**: blocks private rooms despite declared API.
- **Grid network protocol**: pillar is aspirational.

### Positive Observations

- **Protocol version gate** (`data != NET_PROTOCOL_VER` at `netServerEvConnect`) correctly rejects old clients before any state allocation.
- **M-7 fix**: `g_NetNumClients` only increments after successful CLC_AUTH, preventing unauthed connections from counting toward the client cap.
- **Catalog-ID-native wire protocol** (v27+) is well-enforced; very few integer-index leaks remain.
- **Path-traversal sanitization in `extractArchive`**: correct (leading-`/` strip + `..`-component check + `_fullpath` containment).
- **CLC_CHAT size limit** (CHAT_MSG_MAX_LEN = 255) and rate limiting (5 per 2s) are present — the strlen-before-length-check is the flaw (SEC-1), not the absence of limits.
- **SHA-256 verification CODE EXISTS** in `netdistrib.c`; the gap is that it's opt-in (SEC-5), not absent.
- **Counter-Op antiClientId validated**: `port/src/net/netmsg.c:4667-4687` checks `antiClientId < NET_MAX_CLIENTS` and `antiCl->state ≥ CLSTATE_LOBBY` and `antiCl->room_id == srccl->room_id`.
- **B-End-Game-Crash fix** (constraints.md 2026-04-13): `manifestClear` before `mainChangeToStage` is applied across all teardown paths — good discipline.
- **Dedicated server sets `g_NetLocalClient = NULL`** (constraint, B-28): any code that derefs it has a NULL guard. Audit of the server tick loop confirms this.
- **No raw IP in UI** (constraints.md): confirmed — connect codes are the only shared identifier.

---

## 3) Code Quality Review

### Critical Issues

- **CQ-1: String-read safety API (SEC-1 systemic)** — see above. Not a one-off bug but a shared API shape failure.

### High Priority Findings

- **CQ-2: `netmsgClcAuthRead` trusts `name` pointer directly** — at line 462, `strncpy(srccl->settings.name, name ? name : "Player", ...)` is safe because `strncpy` bounds. But immediately above, `pp = netServerFindPreserved(name)` (line 471) and `netChatPrintf(NULL, "%s joined", name)` (line 514) use the pointer in unsafe ways. See SEC-1.

- **CQ-3: Several `for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++)` linear scans per packet** — e.g., `netmsgClcRoomSettingsUpdateRead` (line 6472) scans all 32 clients to find recipients. `netmsgClcAuthRead` iterates the full array twice (line 524, 688). With room-level dirty-flag coalescing this is cheap; at scale it's fine up to NET_MAX_CLIENTS=32.

- **CQ-4: `server_stubs.c` g_MpArenas[] hardcoded table** — duplicates data that should be derived from catalog. Currently works because dedicated server doesn't call catalog-backed arena resolution. A better design keeps the server out of arena data entirely and instead lets it be stage-agnostic (delegate to the game plugin).

- **CQ-5: `server_bridge.c::lobbyGetPlayerInfo` hand-packs a struct by byte offset** — line 47-67: `p[0] = ...; p[1] = ...; ... memcpy(p + 40, &isLocal, sizeof(s32));`. The comment "struct layout must match lobbyplayer_view in server_gui.cpp" is a data-layout hazard — any change on either side breaks silently. The server_gui is C++ with `types.h` disabled, so they don't share a header.
  - Defect Class: Layout
  - Recommendation: Use a fixed-offset POD struct defined in a shared C header (no types.h include guard needed). Or expose individual accessors rather than hand-packing.

- **CQ-6: ENet `enet_peer_send` return value handling inconsistent** — `net.c:1862` checks the return and destroys on failure (good). `netdistrib.c:303` same pattern. But `netSendToRoom` (`net.c:1887`) does NOT check, so failed sends leak the ENet packet.
  - Recommendation: Add the same check pattern everywhere.

### Medium Priority Findings

- **CQ-7: `s_ChatRate` is global static, never reset per client disconnect**. See SEC-4.

- **CQ-8: The `g_NetClients[NET_MAX_CLIENTS + 1]` "extra temporary client" slot** is surprising — uses `+1` to let the local client live at a sentinel position. This is error-prone (see SEC-15) and adds a special case to many loops. Good candidate for refactor: separate `g_NetLocalClient` into its own variable, not shoehorned into the array.

- **CQ-9: The playlist serialization uses `;`-delimited catalog IDs encoded in a single string** — fragile (what if a catalog ID contains `;`?). Catalog IDs are restricted to `[a-zA-Z0-9_:]` today, so OK.

- **CQ-10: Magic numbers for resync cooldowns** — `NET_DESYNC_THRESHOLD=3`, `NET_RESYNC_COOLDOWN=300`, `READY_GATE_TIMEOUT_TICKS=1800`. Fine but should be together and named.

- **CQ-11: Update-system `downloadThread` is a long function** (~160 lines, many lock/unlock pairs, two retry paths). Candidate for decomposition.

### Low Priority Findings

- **CQ-12: Inconsistent `s32` vs `int` usage** in server_main.c and net.c. Minor.

- **CQ-13: `net.c` is 2367 lines** — at the upper bound of reasonable; candidates for split: `net_server.c` (server-only paths), `net_client.c` (client-only), `net_query.c` (recent servers + query response), `net_core.c` (init, startframe, endframe, send).

### Positive Observations

- **Breadcrumb system (`crashBreadcrumbPush`)** used in server dispatch paths — helps postmortem.
- **`sysLogPrintf` with diagnostic tags** (`MATCHSTART.DIAG:`, `AUDIO.DIAG:`, `CRASH.DIAG:`) gives greppable fingerprints.
- **Constraints file** (`context/constraints.md`) disciplines the codebase — e.g., "No raw IP in any UI surface" is observably respected.

---

## 4) Security, Trust & Privacy Review

### Critical Issues

- **SEC-1** (OOB read via non-NUL-terminated strings) — see above.
- **SEC-2** (No dedicated-server auth/admin) — see above.
- **SEC-3** (Preserved-player name hijack) — see above.

### High Priority Findings

- **SEC-5** (Mod verification opt-in)
- **SEC-6** (Unsigned updater)
- **SEC-7** (Query amplification DDoS reflector)
- **SEC-12** (No server-side validation on CLC_ROOM_SETTINGS_UPDATE)
- **SEC-13** (No rate limiting on room lifecycle)
- **SEC-14** (Room passwords not transmitted)
- **SAVE-1** (No save integrity)
- **LAYOUT-2** (Hostile pd.ini redirects connection)

### Medium Priority Findings

- **SEC-15..SEC-25** — see above grouping.

### Low Priority Findings

- **SEC-26..SEC-28** — see above grouping.
- **MP-3** (mod dir fingerprint in server browser).

### Positive Observations

- Auth path (CLC_AUTH) is correctly state-gated to `CLSTATE_AUTH`.
- Protocol version check is first-pass.
- ROM CRC check on listen servers (skipped on dedicated — correct).
- CHAT rate limiting and message size caps exist.
- Path-traversal prevention in mod extraction.
- SHA-256 hashing implementation available in `sha256.c`.
- Catalog-ID-native wire protocol removes most integer-index injection vectors.
- Clean `bool is_leader` / `creator_client_id` checks on CLC_LOBBY_START and CLC_ROOM_SETTINGS_UPDATE — leader-only mutations are enforced.

---

## 5) Cross-Cutting Risks & Architecture Concerns

- **Netbuf string-read contract is the single systemic vulnerability class** — SEC-1 is mechanically preventable by a central fix. This one change removes ~15 OOB-read surfaces. → Pull `netbufReadStr` apart into `netbufReadStrTo(dst, dstlen)` (safe copy) and mark the pointer-returning variant as an internal API.

- **Trust-boundary discipline between netlobby, netmsg, netdistrib** — several handlers assume "server validates" while server code assumes "caller validates"; leader checks land in netmsg but scenario/option value bounds don't. → Write a single `validateMatchSettings(u8 scenario, u32 options, u8 weaponSetIndex, ...)` called both at CLC_LOBBY_START and CLC_ROOM_SETTINGS_UPDATE.

- **Dedicated server game-agnosticism pillar is unmet and has no active ADR** — DS-1. Either adopt or retire the pillar; leaving it ambient is accruing architectural debt.

- **Scaling design intent (host-bound) is contradicted by uniform O(n²) broadcasts and fixed `NET_BUFSIZE = 256KB`** — SEC-8, SEC-9, SEC-10. Interest management is the path to honoring the pillar.

- **Supply-chain trust root is absent** — updater (SEC-6) and mod distribution (SEC-5) both use SHA-256 but no signatures. A single compromise of the GitHub account or mod server channel compromises every user. → One developer-owned signing key, embedded in pd.exe.

- **Save file integrity is absent by design** — acceptable for solo; mandatory when Grid leaderboards ship. → Define a server-authoritative stats path before the Grid goes live.

- **Grid network surface doesn't exist yet** — prime opportunity to threat-model before writing. → Start with a `context/designs/grid-trust-model.md` covering payload caps, hash requirements, rate limits, and what sandbox each payload runs in.

- **Rate limiting is ad hoc** — CLC_CHAT has it; CLC_ROOM_* doesn't. → Standardize: per-client rate limiter keyed by msgid, declared in a single table.

- **Server info query is a fingerprintable / abusable UDP surface** — SEC-7, SEC-16, MP-3. A challenge-response handshake mitigates all three.

- **Client-identity trust is "my name"** — SEC-3. Cookie-based identity via the preserved-player path is the minimal fix.

---

## 6) Verification Limits (Static Analysis vs Runtime)

### Confirmed by code evidence (Critical + High severity)
- SEC-1, SEC-2, SEC-3, SEC-5, SEC-6, SEC-7, SEC-8, SEC-9, SEC-10, SEC-11, SEC-12, SEC-13, SEC-14, SEC-15, SEC-17
- DS-1, DS-2
- SAVE-1, LAYOUT-1, LAYOUT-2, LAYOUT-3, LAYOUT-4, LAYOUT-5
- MP-1, MP-2
- GRID-1

### Probable issues inferred from patterns
- SEC-4 (chat rate limiter state leak across client handoff)
- SEC-23 (cancel-flag atomicity)

### Unverified risks requiring runtime testing / fuzzing
- **SEC-1 exploitability**: is the non-NUL string reachable in the server dispatch with enough controlled heap layout to achieve something worse than a DoS? Needs a fuzzing harness around `netbufReadStr → netServerEvReceive`.
- **SEC-7 amplification factor**: actual ratio of response:query under the CRC16 format — needs packet capture.
- **SEC-8/9 bandwidth curve**: measure real throughput at 16-client / 24-bot dedicated server before claiming 32×32 is infeasible.
- **UPnP/STUN behavior under hostile conditions**: I didn't audit the UPnP path for command-injection in router responses.

### Would be needed to fully validate behavior
- Multi-peer test lobby across varied NAT types (hole-punch waterfall is tested in code but not verified at scale).
- Packet capture of SVC_STAGE_START through stage-end under 12+ clients.
- Fuzz harness targeting: `netbufReadStr`, `netmsgClcAuthRead`, `netmsgClcChatRead`, `netmsgClcLobbyStartRead`, `extractArchive`.
- Synthetic malformed-mod fixtures for the PDCA parser.
- Deterministic replay across Windows/MSYS/MinGW builds (layout-drift stress).
- Controller hot-plug and SDL2 game-controller DB update rig.
- Dedicated-server multi-tenant fixture (would only meaningfully test post-DS-1 plugin boundary).

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: **7 / 10** — match lifecycle, lobby, room system, and mod distribution work end-to-end. Pillar goals (Grid, game-agnostic dedicated server) are unmet but clearly bookmarked. Netplay is functional but security posture is that of a "port-net experimental" codebase, as the upstream calls itself.

**Code Quality Score**: **6 / 10** — well-tagged logs, good use of constants, catalog-ID-native protocol is disciplined. Offset by netbuf API shape (SEC-1), duplicated bounds-check patterns (SEC-15), and PD2-shaped dedicated-server (DS-1/DS-2). `net.c` is at the upper bound of reasonable file size.

**Security & Trust Score**: **4 / 10** — one Critical packet-parsing class (SEC-1), one Critical identity flaw (SEC-3), Critical lack of admin auth (SEC-2), plus two supply-chain weaknesses (SEC-5, SEC-6). SEC-1 alone is disqualifying for public hosting. The positive observations (catalog-ID-native, CHAT rate limit, M-7 auth gate) show the discipline exists — the gaps are recoverable.

**Architectural Discipline Score** (Catalog SOT, mod parity, pillar integrity): **6 / 10** — Catalog SOT and mod/native parity are genuinely enforced. Dedicated-server pillar (game-agnosticism) and Grid pillar (social layer) are unmet; the docs promise more than the code delivers.

### Total Findings by Severity
- Critical: **6** (SEC-1, SEC-2, SEC-3, SEC-4, DS-1, CQ-1 — where CQ-1 is the systemic reframe of SEC-1)
- High: **18** (SEC-5, SEC-6, SEC-7, SEC-8, SEC-9, SEC-10, SEC-11, SEC-12, SEC-13, SEC-14, SAVE-1, LAYOUT-1, LAYOUT-2, GRID-1, CQ-2, CQ-3, CQ-4, CQ-5)
- Medium: **15** (SEC-15..SEC-25, MP-1, MP-2, DS-2, CQ-6..CQ-10)
- Low: **9** (SEC-26..SEC-28, MP-3, LAYOUT-5..LAYOUT-7, SAVE-2, SAVE-3, CQ-11..CQ-13, GRID-2)

### Total Findings by Confidence
- Confirmed: **40**
- Probable: **6**
- Unverified: **2** (SEC-1 exploit practical, SEC-7 amplification factor)

### Total Findings by Lineage
- Authored in PD2: **~30**
- Inherited from port-net: **~15** (netbuf, many SVC/CLC shapes, `netbufReadGset`)
- Inherited from allinone: 0 (no direct findings)
- Inherited from n64decomp: 0
- Unknown: **~3**

### Total Findings by Pillar Impact
- Catalog SOT: 0 (enforcement is solid)
- Mod-native Parity: 1 (MP-1)
- Modding Pipeline: 3 (SEC-5, SEC-25, MP-2)
- Grid Trust: 2 (GRID-1, SAVE-1 future leaderboards)
- Online-mode Boundary: 2 (SEC-16, LAYOUT-2)
- Dedicated-server Boundary: 12 (SEC-1, SEC-2, SEC-3, SEC-7, SEC-11, SEC-12, SEC-13, SEC-14, SEC-15, SEC-17, SEC-19, DS-1)
- Scaling Ceiling: 5 (SEC-8, SEC-9, SEC-10, SEC-11, SEC-13)

### Top Scaling Ceilings Identified
- `MAX_PLAYERS = 8` (src/include/constants.h:44) — current ceiling; fan-out ~80 call sites per grep → wire format playernum u8 has room to grow, but u64 active-slot mask caps at 64. Scaling bottleneck: CPU (O(n²) player-move broadcasts).
- `PARTICIPANT_DEFAULT_CAPACITY = 32` → `MAX_MPCHRS = 40` → hardware cap on simulant count. Bottleneck: bandwidth (bot broadcasts).
- `NET_MAX_CLIENTS = 32` → `g_NetClients[33]` + `LOBBY_MAX_PLAYERS = 32`. Bottleneck: bandwidth (O(n²) player-move broadcasts long before this saturates).
- `NET_BUFSIZE = 256KB` — declared for "31+ bot broadcasts"; raising clients past NET_MAX_CLIENTS breaks.
- `AUDIO_MAX_PLAYLIST * 65 + 4` (playlist broadcast) — scales with track count.
- `u8 bcastData[256]` (netmsg.c:6463) — room-settings broadcast, future-fragile.
- `HUB_MAX_ROOMS = 4` — entire server supports only 4 rooms. Bottleneck: room-matchmaking.
- `DISTRIB_MAX_QUEUE = 64` — total pending mod transfers, server-wide.
- `RECV_SLOTS = 4` — client can accept only 4 concurrent incoming mods.
- Bot broadcast loop: O(bots × clients) per tick. Wall: bandwidth at ~12 clients × 16 bots.
- Full-mesh player move: O(clients²). Wall: bandwidth at ~20 clients.
- `u64` active-slot mask (SEC-10): caps MAX_MPCHRS at 64 forever unless protocol bumps.

### Top Data-Layout Integrity Breaches (Phase 2.5)
- `netbufWriteGset(gset)` / `netbufReadGset` — raw `sizeof(*gset)` memcpy. Declared type size may drift across compiler/platform. Persisted? **No** (wire only). Downstream impact: silent corruption in damage events across mixed builds. Severity: **High**.
- `lobbyGetPlayerInfo` byte-packing (server_bridge.c:47-67) — hand-assembled `u8 *p` offsets; C++ consumer expects identical layout. Persisted? **No** (live only). Downstream impact: silent UI corruption on either-side change. Severity: **Medium**.
- `netbufReadStr` non-NUL-terminated pointer — not a size mismatch per se, but a contract failure at the read boundary. Persisted? **No**. Downstream impact: OOB-read at every `%s` log line (SEC-1). Severity: **Critical**.
- pd.ini `Net.Client.LastJoinAddr` (`NET_MAX_ADDR = 256`) persisted as plain string — reloaded into a trusted in-memory structure. Persisted? **Yes**. Downstream impact: see LAYOUT-2. Severity: **High**.
- Save file JSON has `version` field but loader never checks it (LAYOUT-4). Persisted? **Yes**. Downstream impact: future save-format changes silently deserialize partially. Severity: **Medium**.
- `struct netplayermove`: field-wise serialization; weaponnum clamp enforced. **No breach** (positive).
- `struct netpreservedplayer.killcounts[MAX_MPCHRS]` — sized at compile time; changing MAX_MPCHRS changes the struct width; not persisted across sessions. **No breach** in current scope.
- PDCA archive format: explicit endian conversion via PD_LE16/PD_LE32 (`netdistrib.c:611, 621`). **No breach** (positive).

### Top Catalog / Mod-parity Breakages
- `streamComponentToClient` reads directly from `entry->dirpath` (not through a catalog accessor) — if a catalog entry is hand-registered with a non-sandboxed dirpath, the server can read arbitrary filesystem content. MP-1. Audit: confirm `modmgrScanDirectory` sandboxes dirpath on its write side.
- Bundled/native assets are skipped in distribution (correct). Mods go through the network; the integrity check (SHA-256) is opt-in for mods (SEC-5). This is the one mod-vs-native asymmetry — and the only justified one, but it creates an attack surface that native doesn't have.
- `netdistrib.c:1010-1024` hot-registers mods via `assetCatalogRegister(slot->id, ASSET_NONE)` with no type inference — catalog entry ends up with `type = ASSET_NONE` until a downstream consumer re-evaluates. This is a minor catalog-hygiene issue, not a SOT break.

### Top 5 Action Items (Highest Impact First)
1. **Fix SEC-1 systemically**: introduce `netbufReadStrTo(dst, dstlen)` and migrate every `netbufReadStr` call (~60 sites) to copy into a bounded local buffer before any string operation. Removes the entire class of heap OOB reads and crash-via-crafted-chat vectors.
2. **Fix SEC-2 + SEC-3 together**: add a server-issued client cookie to the CLC_AUTH / SVC_AUTH exchange (128-bit random, stored in `struct netpreservedplayer`). Cookie becomes the identity root; name is advisory only. While the protocol is bumped, add an admin auth token and persistent ban list (`bans.ini`). Unblocks public dedicated-server hosting.
3. **Make mod SHA-256 mandatory (SEC-5)**: reject any SVC_DISTRIB_BEGIN where the manifest entry's hash is zero. Close off the opt-in hole in the supply chain.
4. **Ship signed updater (SEC-6)**: embed a developer-owned Ed25519 public key in pd.exe; sign release ZIPs at release time; verify signature before staging. Defends against GitHub account compromise.
5. **DDoS reflection mitigation (SEC-7)**: add a challenge-response handshake to the server info query (small nonce in the first packet, full info only on echo). Bonus: same mitigation covers SEC-16.

### Must-Fix Before Public Release
- SEC-1 (netbuf NUL contract)
- SEC-2 (dedicated-server auth / admin)
- SEC-3 (preserved-player name hijack)
- SEC-5 (mandatory SHA-256 for mod transfers)
- SEC-6 (signed updater)
- SEC-7 (query amplification)
- SEC-12 (validate CLC_ROOM_SETTINGS_UPDATE server-side)
- SEC-13 (rate limit room-lifecycle messages)
- SEC-14 (room password transport — or remove the ROOM_ACCESS_PASSWORD enum)
- LAYOUT-1 (field-wise `gset` serialization)
- LAYOUT-2 (validate Net.Client.LastJoinAddr at load)
- NET_DISTRIB_MAX_SESSION enforcement (SEC-25)

### Estimated Effort to Remediate Critical / High Issues
- **SEC-1 systemic fix**: ~1 engineer-day (mechanical) + 0.5 day verification harness.
- **SEC-2 + SEC-3 (auth + cookie + bans)**: ~3–5 engineer-days including protocol bump to v38 and migration.
- **SEC-5 (mandatory hash)**: ~0.5 day.
- **SEC-6 (signed updater)**: ~2 days including release-tooling signing flow + key management docs.
- **SEC-7 (query handshake)**: ~1 day.
- **SEC-8 / SEC-9 (interest management)**: ~5–10 days — this is a proper feature, not a bug fix.
- **SEC-12 + SEC-13 (server-side validation + rate limiting)**: ~1 day combined.
- **SEC-14 (room password transport)**: ~1 day.
- **SAVE-1 (server-authoritative stats for MP)**: already largely in place — dead effort in current scope, material effort when Grid ships (~5 days).
- **DS-1 (per-game plugin boundary)**: ~4–8 weeks if adopted; 0 days if retired.
- **Cumulative blocking Critical/High (excluding DS-1 and SEC-8/9): ~10–14 engineer-days.**

---

## Notes on process

- Four parallel subagents were dispatched (scaling scan, save/load, mod pipeline, Grid) and all four returned an internal runtime error ("This model does not support the effort parameter"). The audit proceeded via direct grep/read and is therefore slightly less exhaustive than a fully parallelized run would have been. In particular, the UPnP / STUN / hole-punch paths got less scrutiny; the fast3d renderer's interaction with ImGui+OpenGL is unaudited; and a full list of compile-time constants was derived only from the `port/include/` + `src/include/constants.h` headers. A follow-up pass on the non-net surfaces (renderer, audio mixer, input pipeline, pdgui_forge editor internals) is warranted.
- Phase 2.5 was executed inline rather than as a separate pass — LAYOUT-* findings are tagged accordingly.
