# Deep Project Audit Report — Perfect Dark 2
**Scope:** Catalog SOT + Netplay/P2P + Content Loading
**Date:** 2026-04-19
**Auditor:** Claude Opus 4.7 (1M context) — Super Audit skill
**Report path:** `context/audits/2026-04-19-catalog-netplay.md`
**Method:** static source review of `port/src/assetcatalog*.c`, `port/src/assetprovider*.c`, `port/src/assetload.c`, `port/src/fs.c`, `port/src/romdata.c`, `port/src/modmgr.c`, `port/src/net/*`, `src/game/mplayer/participant.c`, the corresponding headers, plus project context in `context/constraints.md`, `context/README.md`, `context/network-system-audit.md` and recent audits (S255–S292 in `context/scratch/audit-s255-s292-2026-04-16.md`).
**Excluded from scope (by user request):** UI/menus, rendering, Forge/Grid editor, Studio, modding hub, input pipeline, dev tooling, release pipeline. These surfaces are only cited when they directly interact with Catalog/Net/Content.

---

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose
Perfect Dark 2 is a PC-only C11/C++ fork of the n64decomp→fgsfdsfgs port-net→jonaeru allinone lineage, re-engineered around four PD2-native pillars: a string-keyed **Asset Catalog** as the single source of truth for every asset, **equal-footing content loading** (mod and native traverse the same code), a deterministic **modding pipeline**, and a **dedicated-server / client-online** P2P model where session scaling is bounded by host hardware rather than N64/port-era 8-player ceilings. Within this audit's scope, the intended outcomes are:

- Any game-code asset lookup (body/head/stage/weapon/prop/audio/arena/model) resolves through the Catalog; no caller dereferences `g_HeadsAndBodies[]`, `g_Stages[]`, `g_MpBodies[]`, `g_MpWeapons[]` directly.
- Mod content loaded from loose files (FileProvider) is functionally indistinguishable from ROM content (RomProvider) — same `asset_entry_t`, same `asset_data_handle_t`, same loader.
- Wire protocol (v37) carries `u16 session_id` or full `catalog_id` strings, never raw `net_hash` (removed 2026-04-02) and never raw array indices.
- Dedicated server can host PD2 sessions without requiring a ROM, without occupying a player slot, and without baking PD2-specific assumptions into the server core.
- Session size scales with `g_NetMaxClients` / host capacity; fixed player counts are only permitted as safe upper bounds on wire fields.

### Likely Player & Contributor Types
- P2P hosts (dedicated-server operators + listen-server hosts), cross-NAT connectors via connect codes (256⁴ sentence codes).
- Mod authors — local (scanner) and wire-distributed (PDCA archive + SHA-256 manifest).
- Community contributors iterating on catalog/scanner/netplay; single core developer (Mike) + AI assistants.

### Core Workflows (In-Scope)
1. **Boot**: `assetCatalogInit → assetCatalogRegisterBaseGame{,Extended} → modmgrScanDirectory → catalogBuildRuntimeCaches`.
2. **Match start**: leader sends `CLC_LOBBY_START` → server builds manifest → `SVC_MATCH_MANIFEST/CATALOG_INFO/DISTRIB_*` → clients download missing mods → `CLC_MANIFEST_STATUS(READY)` → `SVC_MATCH_COUNTDOWN` → `SVC_STAGE_START` (v37 active-slot u64 mask + catalog IDs + session IDs).
3. **In-match replication**: `CLC_MOVE` (60 Hz per player, ~56 bytes), `SVC_CHR_MOVE/STATE/SYNC`, `SVC_NPC_*`, `SVC_PROP_*`, resync via `g_NetPendingResyncFlags`.
4. **Reconnection**: `netServerPreservePlayer` on disconnect → `netServerFindPreserved(name)` on re-auth → `netServerRestorePreserved` → `SVC_STAGE_START` replay + full resync flags.
5. **Asset load**: `catalogResolveBody/Head/Stage/Weapon/...` → `catalogGetXHandle()` → `assetLoadToNew(handle, ...)` → RomProvider fast-path or FileProvider generic path.

### Upstream Lineage Observations
- **Inherited from fgsfdsfgs `port-net`**: ENet lifecycle, `netbuf` model, 60 Hz tick, CLC/SVC message dispatch, preserved-player pattern, wiretypes `u32 tick` / `u32 ucmd` / `struct coord`, libc dirent-based mod-scan, listen-server vs dedicated split. The `netbuf` `u16` length-prefixed strings and the `player == 0` local-swap in `netPlayersAllocate` are port-net idioms.
- **Inherited from jonaeru `allinone`**: per-arena mod-swap mechanism is no longer the live architecture — PD2 replaced it with the catalog+component model; residuals in the scanner's INI recognition remain (file taxonomy: `map.ini`, `character.ini`, `bot.ini`, `weapon.ini`, `audio.ini`, `sfx.ini`, `music.ini`).
- **Authored in PD2**: Asset Catalog, `asset_provider_t` abstraction, `asset_source_t` primary/override, `sessioncatalog` session-id layer, `catalogResolve*` typed resolvers, `MpParticipantPool` (B-12, replaced u64 `chrslots`), manifest architecture, SHA-256 verification, trust-threshold mod-download approval, path-traversal containment via `_fullpath`.
- **Inherited from n64decomp**: `struct mpchrconfig`, `u8/s32/f32` typedefs, `MAX_PLAYERS 8`, `MAX_LOCAL_PLAYERS 4`, `PLAYERCOUNT()` macro with `#if MAX_PLAYERS == 4` unrolled ladder, `MPCHR(index)` branch, `g_PlayerConfigsArray`, `killcounts[MAX_MPCHRS]`.

### Pillar Observations
- **Catalog SOT** — strong in the game-code-facing API (typed resolvers, reverse caches, net-hash path removed from the wire). Weak at the registration boundary: the wire-driven distribution path at `netdistrib.c:1010` registers mod components as `ASSET_NONE` and only re-types on next catalog refresh; manifests can include entries with zero SHA which silently skip integrity verification.
- **Mod-native Parity** — strong for locally-scanned mods (scanner uses the same typed registration functions as `assetcatalog_base.c`). Weak for wire-delivered mods (ASSET_NONE transient; mod download authorised at trust-threshold UI but installed unconditionally enabled).
- **Modding Pipeline** — PDCA archive format is simple and deterministic, but the server-side `buildArchiveDir` iterates in `readdir` order (filesystem dependent) and does not sort entries → **non-deterministic archive layout**. SHA-256 computed over the compressed stream, not the uncompressed content, so determinism depends on zlib+input ordering.
- **Grid Trust** — out of scope but touched indirectly: any Grid payload that maps to a catalog ID flows through `assetCatalogResolve` with type-check, which bounds most risk.
- **Online-mode Boundary** — dedicated-server guards (`!g_NetDedicated`) correctly gate ROM hash and mod-dir checks, null `g_NetLocalClient`, and slot 0 availability. No ROM dependency on server confirmed via `catalogResolveStage`'s `g_Stages == NULL` fallback in `assetcatalog_api.c:93–100`.
- **Dedicated-server Boundary** — server links `participant.c` directly (B-12 Phase 3). Server and client share the same `netmsg.c` dispatch. The participant pool and match wire format are game-agnostic in shape but `struct mpchrconfig` and named match options still leak PD2 semantics through the shared header. Not a finding for the current single-game target but flagged under §5.

### Assumptions / Unknowns
- `netbufReadStr` behaviour under non-null-terminated payloads confirmed via source; actual exploitability depends on the underlying packet buffer allocation which is `ENetPacket` (heap) — likely past-buffer reads are possible but bounded to the allocation.
- `netmanifest.c` (2044 lines) was sampled, not exhaustively audited — residual concerns are flagged as **Unverified**.
- `modmgr.c` (2479 lines) scanner depth-cap and cjson parser were sampled; S297's cjson float-tokenizer fix is already in.
- The `ALIGN16` regression (S384) is out-of-scope but worth noting that similar pointer-vs-size macro collapses exist elsewhere in the catalog/content area — one candidate is flagged below in §2.5.
- I did not reproduce any runtime behaviour. All findings are static-review; runtime confirmation (packet captures, malformed-peer fuzzing, multi-room stress) is required for HIGH+ items.

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### Critical Issues

#### C-1. `netbufReadStr` returns a pointer with no enforced null terminator; consumers use `strlen`/`strcmp`/`strncpy`/`strcasecmp` unbounded by `len`
- **Severity:** Critical (bounded blast radius: read past the packet-buffer allocation; memory corruption not immediate but information disclosure and parse desync likely).
- **Confidence:** Confirmed (static source evidence across 27 call sites in `netmsg.c`).
- **Area:** Code Quality / Security.
- **Location:** `port/src/net/netbuf.c:120-129` (`netbufReadStr`), with consumers at `port/src/net/netmsg.c:422, 462, 550, 554, 655, 672, 680, 709, 792, 1081, 1125, 1155, 1214, 1399, 4505, 4533, 4558, 4579, 4738, 4739, 4740, 4779, 4815, 5128, 5219, 5260, 5289, 5332, 5373, 5403, 5404, 5421, 5426, 5430` plus `port/src/net/sessioncatalog.c:148`.
- **Lineage:** Inherited-from-port-net (the pattern dates to the port-net u16-length-prefixed string model).
- **Pillar Impact:** Grid Trust (indirect), Dedicated-server Boundary (server-authored strings reach clients), none within Catalog SOT directly.
- **Defect Class:** Trust-Boundary.
- **Current Behaviour:** `netbufReadStr` does `netbufCanRead(buf, len)` then returns `(char *)&buf->data[rp]` and advances `rp += len`. `netbufWriteStr` inserts a trailing null at `buf->data[wp - 1] = 0`, so **well-formed** messages are null-terminated. A malformed peer can send `u16 len = N`, N bytes of arbitrary non-null data, and the next field's bytes will be read by any `strlen(ptr)` / `strncmp(dst, ptr, K)` / `strncpy(dst, ptr, K-1)` / `strcmp(x, ptr)` that exceeds the intended length.
- **Gap / Failure Mode:** Worst-case consumer is `netmsg.c:554` — `if (strlen(msg) > CHAT_MSG_MAX_LEN)` reads until it finds a zero byte, potentially past `wp` and past the ENet packet allocation. Other affected call sites iterate `g_MpNumChrs` times (e.g., 5426 `strcmp(g_MpAllChrConfigPtrs[i]->name, aname)`) — each iteration reads past the intended string boundary into adjacent packet fields or uninitialised buffer memory.
- **Why It Matters:** Any peer or compromised server can cause OOB reads in message handlers. Consequences: crashes (SEGV past ENet packet allocation), information disclosure (packet-buffer residue in logs via `sysLogPrintf("%s", msg)` at line 573), chat/killfeed spoofing by crafting a name that matches another player's unterminated bytes, preserved-player hijacking amplification (see C-2).
- **Risk If Not Fixed:** Crash-on-join DoS (trivially scripted), griefing via malformed chat, undefined behaviour in string comparisons that feed into team lookup, scoreboard, kill feed, and name display.
- **Recommendation:** Make `netbufReadStr` write a null terminator after the read. Implement one of:
  - Refuse to return the pointer unless `buf->data[rp + len - 1] == '\0'` — reject the read and set `buf->error = 1` otherwise.
  - Require callers to pass a destination buffer + size (`netbufReadStrInto(dst, dstsz, buf)`) and drop the pointer-return form.
  - Minimum interim: in `netbufReadStr`, after the canRead check, force-terminate by scanning the returned region for a null within `len`; if absent, truncate by overwriting `buf->data[rp + len - 1] = 0` (mutating the packet, acceptable here because netbuf owns the read buffer during dispatch).
- **Cross-System Changes Required:** Yes — every site that assumes `char *` return value is a C-string (≥27 call sites). A one-line fix in `netbufReadStr` protects all of them at once.
- **Blocks Intended Outcome?:** Partial. Affects Trust & Authority rules.

### High Priority Findings

#### H-1. Preserved-player reconnect matches on `name` only; no identity token or IP binding → session hijacking
- **Severity:** High (security, griefing vector; PD2 context = community servers where griefing is a realistic concern).
- **Confidence:** Confirmed.
- **Area:** Security / Design Fit.
- **Location:** `port/src/net/net.c:1169-1181` (`netServerFindPreserved`), `port/src/net/netmsg.c:471-476` (`netmsgClcAuthRead` reconnect branch).
- **Lineage:** Inherited-from-port-net (fgsfdsfgs preserved-player model).
- **Pillar Impact:** Online-mode Boundary, Dedicated-server Boundary.
- **Defect Class:** Trust-Boundary.
- **What We Found:** On reconnect, `netServerFindPreserved(name)` does case-insensitive strncasecmp over `g_NetPreservedPlayers[32]` looking only at `name`. The returned preserved slot is handed to `netServerRestorePreserved`, which inherits `playernum`, `team`, `killcounts`, `numdeaths`, `numpoints`. No auth token, no IP/port match, no ROM-hash binding, no connect-code challenge. The preserve window is 600 frames (~10 s) per existing doc, which is long enough to race a legitimate client's reconnection.
- **Why It Matters:** Any peer who knows a target's screen name — which is broadcast in the lobby, kill feed, and chat — can: (a) wait for the target to disconnect (or induce the disconnection via network harassment); (b) connect and auth within the preserve window; (c) assume the target's playernum, team, and score. Under client-online mode, this also inherits match state. Combined with C-1, a crafted `CLC_AUTH` name can race preserve slots of multiple players in one malformed packet.
- **Risk If Not Fixed:** Identity takeover, score theft, team-swap griefing, match-state corruption. On dedicated servers where multiple matches pass through the same name pool, cross-match identity confusion.
- **Recommendation:** On first auth, issue a server-generated 128-bit token; store alongside `pp->name` in the preserved slot; require the token on reconnect auth (`CLC_AUTH` adds a `u8 token[16]` tail). Fallback: bind the preserved slot to the original ENet peer address for the 10-second window.
- **Cross-System Changes Required:** Yes — bumps `NET_PROTOCOL_VER` to v38, adds a token field to `CLC_AUTH`, adds persistent-client storage.
- **Blocks Intended Outcome?:** Partially.

#### H-2. Wire-delivered mods hot-register as `ASSET_NONE` — breaks mod/native parity until next catalog refresh
- **Severity:** High.
- **Confidence:** Confirmed.
- **Area:** Design Fit / Code Quality.
- **Location:** `port/src/net/netdistrib.c:1010` (`assetCatalogRegister(slot->id, ASSET_NONE)`).
- **Lineage:** Authored-in-PD2.
- **Pillar Impact:** **Mod-native Parity**, **Catalog SOT**.
- **Defect Class:** Trust-Boundary (transient asymmetric registration).
- **What We Found:** When the client receives a PDCA archive via `SVC_DISTRIB_BEGIN/CHUNK/END` and calls `extractArchive` successfully, `netDistribClientHandleEnd` iterates `ini_names[]` to find the first matching INI. For every non-audio type, it calls `assetCatalogRegister(slot->id, ASSET_NONE)` and manually populates `id`/`category`/`dirpath`/`enabled`/`temporary`/`bundled`/`model_scale`. The entry type is `ASSET_NONE`, so `catalogResolveBody`, `catalogResolveStage`, `catalogResolveWeapon`, etc. all type-reject it and return 0 with `[CATALOG-ERROR]` logs.
- **Gap / Failure Mode:** Until the next `catalogRefreshMods()` (which clears non-bundled entries and re-scans `mods/` via the proper typed scanner), a hot-installed mod that a player just accepted is non-resolvable through the typed API. `assetCatalogIterateByType(ASSET_MAP, ...)` skips it. The scanner's proper-type registration would kick in on the next refresh, but there is no guaranteed refresh between distribution and match start — the distribution path assumes the extracted files will be re-scanned implicitly.
- **Why It Matters:** Violates the "if you stripped the labels, you couldn't tell" pillar property. A session-distributed mod with `temporary=1` never gets a non-`ASSET_NONE` type at all, because there's no refresh before the match starts that would re-type it; the downstream match code then cannot find it by type, and the `manifestEnsureLoaded` SP-13 late-add path at `netmanifest.c` logs torn-modeldef warnings (see `context/session-log.md` S312).
- **Risk If Not Fixed:** Silent mod-loading failures that only manifest when the mod is a body/head/arena type. Intermittent "works on second connect" behaviour. Non-reproducible QA reports.
- **Recommendation:** In `netDistribClientHandleEnd`, switch on the INI basename and call the typed registrar (`assetCatalogRegisterMap`, `assetCatalogRegisterCharacter`, `assetCatalogRegisterBody`, `assetCatalogRegisterHead`, `assetCatalogRegisterWeapon`, `assetCatalogRegisterProp`) — or factor the scanner's INI-to-typed-registration loop into a shared helper and call it from both paths. Also consider calling `catalogBuildRuntimeCaches()` once after the final hot-register of a distribution batch.
- **Cross-System Changes Required:** Yes — touches `assetcatalog_scanner` (extract helper) and `netdistrib.c` (rewrite the registration loop).
- **Blocks Intended Outcome?:** Yes.

#### H-3. `SVC_STAGE_START` active-slot u64 mask caps total match slots at 64 — hard scaling ceiling
- **Severity:** High.
- **Confidence:** Confirmed.
- **Area:** Design Fit / Scaling.
- **Location:** `port/src/net/netmsg.c:1100-1104` (read), `port/src/net/netmsg.c` (write, via `mpParticipantsEncodeActiveMask`), `src/game/mplayer/participant.c:364-399` (encode/decode).
- **Lineage:** Authored-in-PD2 (B-12 Phase 3, 2026-04-17).
- **Pillar Impact:** **Scaling Ceiling**.
- **Defect Class:** Scaling.
- **What We Found:** The active-slot bitmask is a `u64`, hard-capped at 64 positions. `mpParticipantsEncodeActiveMask` explicitly loops `i < g_MpParticipants.capacity && i < 64`. With `MAX_PLAYERS=8` + `MAX_BOTS=32` = 40 slots this is fine; scaling past 40 local match slots either breaks wire compatibility or silently drops high-index participants. The PD2 goal statement (per `SKILL.md`, `audit-prompt.md`, and `context/constraints.md`) is explicit: **"Scaling philosophy: P2P session size scales with host compute and bandwidth; hardcoded `MAX_PLAYERS = 8` (or any fixed-count literal) anywhere is a finding."**
- **Current Behaviour / Failure Mode:** Works at current counts. Blocks the design target of 32+ player P2P + dedicated-server session growth without wire protocol rework.
- **Why It Matters:** Pillar-level scaling ceiling. Any effort to unlock >40 slot matches requires a protocol bump and either a length-prefixed slot-list or a variable-length bitmap.
- **Risk If Not Fixed:** Hard ceiling on session size; cannot deliver on "host-bound scaling" pillar.
- **Recommendation:** Replace the u64 mask with a length-prefixed bitmap: `u16 slot_count; u8 mask[ceil(slot_count/8)]`. Bump `NET_PROTOCOL_VER`. Update `mpParticipantsEncodeActiveMask`/`Decode` and the corresponding write site in `netmsgSvcStageStartWrite`.
- **Cross-System Changes Required:** Yes (wire protocol, both participant encode/decode, server and client).
- **Blocks Intended Outcome?:** Yes (pillar-critical at the intended long-term scale).

#### H-4. Initial mod distribution is integrity-unverified until the match manifest is present
- **Severity:** High.
- **Confidence:** Confirmed.
- **Area:** Security / Supply-Chain.
- **Location:** `port/src/net/netdistrib.c:940-963` (SHA-256 verification in `netDistribClientHandleEnd`) vs. `netdistrib.c:700-745` (initial catalog diff + first-join distribution).
- **Lineage:** Authored-in-PD2.
- **Pillar Impact:** **Modding Pipeline** (supply chain), Grid Trust (indirect).
- **Defect Class:** Supply-Chain.
- **What We Found:** SHA-256 verification at line 944 iterates `g_ClientManifest.num_entries`. If `num_entries == 0` (no manifest yet — the case on first auth-time catalog diff, before any `SVC_MATCH_MANIFEST` has been sent), the loop runs zero iterations and falls through to `extractArchive` unchecked. Separately, line 947 guards on `memcmp(me->sha256, s_zero32, 32) != 0`; any manifest entry with all-zero SHA silently skips integrity for that component.
- **Gap:** The trust-threshold UI prompt at line 801 protects bandwidth/disk against enormous archives, but does not provide integrity. A malicious server can hand a client a poisoned mod before the manifest phase, or a manifest that zeros the SHA field for a chosen component.
- **Why It Matters:** Erosion of the modding-pipeline determinism/integrity pillar. Combined with the fact that hot-registered mods have `enabled=1` by default (line 1015) and can land in `mods/{category}/{id}/` permanently (non-temporary branch), a malicious server can write files to a player's `mods/` tree with no integrity check.
- **Risk If Not Fixed:** Supply-chain compromise — a compromised community server feeds arbitrary file contents into players' `mods/` trees; subsequent launches load the tampered mod against the Catalog. Catalog SOT is inviolate at the API but the bytes it points to are untrusted.
- **Recommendation:**
  - **Always require a SHA-256** in `SVC_CATALOG_INFO` for every advertised mod before the diff is issued; refuse components whose SHA is zero.
  - **Move the verification** to unconditional: after decompress, always compute the hash and compare against the advertised hash from `SVC_CATALOG_INFO` (which should be copied into the recv slot).
  - Prefer storing wire-delivered non-temporary mods under `mods/.server-delivered/{server-fingerprint}/` with an explicit user consent step, separate from locally-authored `mods/`.
- **Cross-System Changes Required:** Yes — bumps protocol, adds SHA to `SVC_CATALOG_INFO`, rewrites verify path.
- **Blocks Intended Outcome?:** Yes.

### Medium Priority Findings

#### M-1. `netbufWriteU16/U32/U64` perform unaligned pointer writes; reader uses `memcpy` (H-6 fix is writer-asymmetric)
- **Severity:** Medium (portability/UB).
- **Confidence:** Confirmed.
- **Area:** Code Quality.
- **Location:** `port/src/net/netbuf.c:208`, `:218`, `:228` (write side) vs. `:62-67`, `:73-78`, `:86-91` (read side already memcpy).
- **Lineage:** Inherited-from-port-net, partially fixed in H-6 (read side).
- **Defect Class:** Layout.
- **What We Found:** Reader uses `memcpy(&ret, &buf->data[buf->rp], sizeof(ret));` for U16/U32/U64 to avoid unaligned access UB. Writer does `*(u16 *)&buf->data[buf->wp] = PD_LE16(v);` — an unaligned deref on Windows x86_64 works due to permissive hardware but remains C-UB and will trap on strict-alignment targets (if a Switch or ARM build is ever restored).
- **Why It Matters:** Consistency with the H-6 fix; future portability; UB is UB.
- **Recommendation:** Mirror the reader pattern — use `memcpy(&buf->data[wp], &swapped, sizeof)`. One-line fix per function.

#### M-2. `assetCatalogRegister` "last-write-wins" clobbers an existing entry with `memset(0)`, losing prior `source.override` and enabled state
- **Severity:** Medium.
- **Confidence:** Confirmed.
- **Area:** Code Quality / Catalog SOT.
- **Location:** `port/src/assetcatalog.c:422-425`.
- **Lineage:** Authored-in-PD2.
- **Pillar Impact:** Catalog SOT.
- **Defect Class:** None (logic bug).
- **What We Found:** When an ID already exists, `assetCatalogRegister` reuses the slot but does `memset(entry, 0, sizeof(asset_entry_t))` first. Any `source.override`, `source.primary`, `load_state`, or `enabled` flags previously set are wiped. The registration order (base first, mods second) usually protects against this, but: (a) the hot-reload path (`catalogRefreshMods`) clears mods, re-scans, and if a mod registration runs before a base re-registration for an overlapping ID, base would clobber mod; (b) the scanner and `netdistrib` can register mod content that overrides the same base ID — whoever runs later wins. A mod that legitimately overrides `base:dark_combat` and later gets re-scanned could reset its override.
- **Why It Matters:** Fragile contract — assumes registration ordering discipline without enforcement.
- **Recommendation:** Either (a) preserve `source.override` and `enabled` across re-registrations with the same ID; or (b) document the ordering rule and add a debug assert when a base entry clobbers a mod's `override`.

#### M-3. `FileProvider` path-intern pool silently fails when exhausted (32 KB, 1024 paths)
- **Severity:** Medium.
- **Confidence:** Confirmed.
- **Area:** Code Quality.
- **Location:** `port/src/assetprovider_file.c:30-71`.
- **Lineage:** Authored-in-PD2.
- **Pillar Impact:** Catalog SOT, Modding Pipeline.
- **Defect Class:** Scaling.
- **What We Found:** `fileProviderInternPath` prints a single `LOG_WARNING` on first overflow (`s_Warned`) and returns `0`, which the caller turns into a null handle. Downstream, any `assetLoad` on that handle silently returns `NULL`. The user-facing symptom is "that one mod's asset doesn't load" with no diagnostic beyond the one-time log line.
- **Why It Matters:** 1024 paths is reasonable now; with the explicit architectural direction toward a "first-class modding pipeline" and Grid-shared workshops, mod installs at that scale are plausible. Silent failure degrades the "equal footing" pillar.
- **Recommendation:** Grow the pool on demand (2x realloc) or, minimally, log every exhausted call (not just the first). Expose a counter in the Mods UI so users see how many paths are live.

#### M-4. `netmsg.c:5226-5231` `ids[]`/`cats[]` stack arrays sized to `CATALOG_COLLECT_MAX` (256) — wire can't cover a server with >256 mods
- **Severity:** Medium.
- **Confidence:** Confirmed.
- **Area:** Design Fit / Scaling.
- **Location:** `port/src/net/netmsg.c:5215-5229` (`netmsgSvcCatalogInfoRead`), `port/src/net/netdistrib.c:709` (same cap client-side).
- **Lineage:** Authored-in-PD2.
- **Pillar Impact:** Scaling Ceiling, Modding Pipeline.
- **Defect Class:** Scaling.
- **What We Found:** The client-side catalog collect buffer is a fixed `char ids[256][64]`. `CLC_CATALOG_DIFF` mirrors this with `char missing_ids[256][CATALOG_ID_LEN]`. A server with 256+ advertised mods overflows the cap; the read validates `count > CATALOG_COLLECT_MAX` and rejects the message — so it's safe but truncates.
- **Why It Matters:** Community servers accumulating large mod libraries (thousands of user-made skins, music tracks, arenas) cannot advertise their full set.
- **Recommendation:** Chunk the catalog announcement (e.g., `SVC_CATALOG_INFO_PAGE index count`) or switch to a streamed enumeration. Less intrusive alternative: allocate `ids`/`cats` from the stage pool (`mempAlloc MEMPOOL_STAGE`) with count driven by the server.

#### M-5. `extractArchive` assumes the path field is null-terminated within `path_len` bytes
- **Severity:** Medium.
- **Confidence:** Confirmed (defensive depth concern).
- **Area:** Security.
- **Location:** `port/src/net/netdistrib.c:607-653`.
- **Pillar Impact:** Modding Pipeline (supply chain).
- **Defect Class:** Trust-Boundary.
- **What We Found:** The loop reads `path_len` bytes, sets `const char *relpath = (const char *)p;`, advances `p`. The format doc says paths are null-terminated within `path_len`, but a malicious server can send a non-null-terminated payload. Subsequent `snprintf("%s/%s", destdir, safe)`, `strrchr(dirpath, '/')`, and the `_fullpath` containment check all read `safe` as a C string, reading past `path_len` into the next entry's `data_len` and beyond. `snprintf` truncates to `FS_MAXPATH`, so corruption is bounded, but outpath will contain arbitrary archive bytes.
- **Why It Matters:** The `_fullpath` containment check protects the host filesystem. But the intermediate buffer contents are unpredictable. A crafted archive could construct a path string that legitimately passes the containment check while writing to an unintended sub-directory.
- **Recommendation:** Enforce null termination: after `memcpy(&path_len, p, 2)`, verify `p[path_len - 1] == '\0'` or write one; treat violation as a fatal archive reject.

#### M-6. `netmsgSvcLobbyKillFeedRead` iterates `g_MpNumChrs` calling `strcmp` on unterminated names (combines with C-1)
- **Severity:** Medium (stacks with C-1).
- **Confidence:** Confirmed.
- **Location:** `port/src/net/netmsg.c:5423-5434`.
- **Pillar Impact:** Grid Trust (kill-feed content propagates to UI).
- **Defect Class:** Trust-Boundary.
- **What We Found:** For every chr config, two `strcmp` calls against `aname` and `vname` (both from `netbufReadStr`). Per kill, `2 × g_MpNumChrs` unbounded string compares. Amplifies C-1: one server-sent kill-feed message forces `2N` past-buffer reads.
- **Recommendation:** Fix C-1 to remove the class. Separately, bound each strcmp with `strncmp` using `NET_MAX_NAME` (15) for defense in depth.

#### M-7. Server-side `g_NetMsgRel` reset in `netStartFrame` silently drops mid-dispatch writes (historical netbuf hazard)
- **Severity:** Medium (documented upstream — worth revalidating).
- **Confidence:** Probable (not freshly confirmed this audit — see `context/network-system-audit.md` §3.2).
- **Location:** `port/src/net/net.c` (netStartFrame event-dispatch pre-reset pattern).
- **Lineage:** Inherited-from-port-net.
- **Defect Class:** Trust-Boundary / Determinism.
- **Recommendation:** Carry forward the S61 guarantee: any new SVC write path that fires inside an event handler must use `netbufStartWrite(&cl->out)` / `netSend` atomic pattern, not `g_NetMsg`/`g_NetMsgRel`. Document this as a checklist item in any new message handler.

#### M-8. `catalogGetBodyFilenumByIndex` / `catalogGetHeadFilenumByIndex` still present as `[DEPRECATED]` with active callers
- **Severity:** Medium (technical debt; AP Phase 4 owns this).
- **Confidence:** Confirmed.
- **Location:** `port/src/assetcatalog_api.c:518-556`, callers across `body.c` (5), `player.c` (5), `menu.c` (2), `mplayer/setup.c` (2), `setuputils.c` (1), `title.c` (8) per `context/tasks-current.md` S377 note.
- **Pillar Impact:** Catalog SOT (consistency of internal API).
- **Defect Class:** None (migration debt).
- **What We Found:** These bridge functions are documented as deprecated; AP Phase 4 will retire them alongside `fileGetInflatedSize` and `modeldefLoad` handle-aware variants.
- **Recommendation:** Track as-is; not a release blocker. Audit's role is just to surface that the deprecation marker isn't enforced by the build.

### Low Priority Findings

#### L-1. Catalog pool realloc can invalidate pointers, but `s_CatalogGeneration` counter is not enforced at consumers
- **Severity:** Low (probable UAF under specific edge case).
- **Confidence:** Probable.
- **Location:** `port/src/assetcatalog.c:219-237` (`growEntryPool` realloc), `:300-303` (`assetCatalogGetGeneration`).
- **Defect Class:** Layout.
- **What We Found:** `growEntryPool` `realloc`s the entry pool. Any caller holding a pointer returned from `assetCatalogResolve` etc. across a subsequent `assetCatalogRegister` / `assetCatalogClearMods` call has a dangling pointer. `s_CatalogGeneration` is incremented on clear/clearMods but NOT on `growEntryPool` (which is where realloc actually occurs). Consumers don't appear to cache pointers across registration calls in normal flow, but the pattern is not enforced.
- **Recommendation:** Increment `s_CatalogGeneration` inside `growEntryPool` after the realloc. Document that resolved pointers are invalid across any registration call.

#### L-2. `assetCatalogResolveByNetHash` is O(n) but called only on wire arrival; acceptable for v27+ (no net_hash on wire)
- **Severity:** Low (previously HIGH, defanged by v27 protocol change).
- **Confidence:** Confirmed.
- **Location:** `port/src/assetcatalog.c:694-703`.
- **Note:** Now largely dead code post-v27. Consider deleting the function once confirmed no remaining callers.

#### L-3. `netbufReadS8(buf)` returns `0` on underflow, indistinguishable from legitimate zero — callers must check `buf->error`
- **Severity:** Low (documented hazard).
- **Location:** all `netbufRead*` wrappers.
- **Recommendation:** Accept the existing convention; add a `static inline` `netbufHadError(const struct netbuf *)` accessor callers can batch-check after a sequence of reads.

#### L-4. `PLAYERCOUNT()` macro in `src/include/constants.h:94-104` uses unrolled `#if MAX_PLAYERS == N` ladder (upstream-shape)
- **Severity:** Low.
- **Confidence:** Confirmed.
- **Lineage:** Inherited-from-n64decomp.
- **Defect Class:** Lineage-Shape.
- **What We Found:** Classic N64-era hand-unrolled loop: `(g_Vars.players[0] ? 1 : 0) + (g_Vars.players[1] ? 1 : 0) + ...` with `#if MAX_PLAYERS == 4` branches for 2/3/4/>4 tiers. Constraint `MAX_LOCAL_PLAYERS = 4` is active, and S188 set "no local multiplayer (single local player only)".
- **Recommendation:** Post-S188 this macro can collapse to `1`. Low priority — correct and fast as written; just visually N64-shape.

#### L-5. `\ TODO:` / `\ HACK:` anomalies in `net.c:netPlayersAllocate/netSyncIdsAllocate` (documented LOW-5 in network-system-audit)
- **Severity:** Low.
- **Location:** `port/src/net/net.c` (per existing `context/network-system-audit.md` §11 LOW-5).
- **Recommendation:** Replace with `// TODO:` during next net.c touch.

### Missing / Incomplete Features Blocking Success
- **HIGH**: Hot-registered mods not type-resolvable (H-2) blocks the "equal footing" pillar for wire-delivered content until a follow-up refresh fires.
- **HIGH**: No integrity verification of wire-delivered mods before the first match manifest (H-4) blocks deterministic modding-pipeline.
- **HIGH**: Active-slot u64 mask (H-3) caps session size at 64 — blocks the stated "host-bound scaling" design target.

### Positive Observations
- **Catalog IDs at all boundaries** — v27's removal of `net_hash` from the wire, v32's scenario-as-catalog-ID, v37's elimination of `chrslots` u64 field — these are the exact architectural moves the pillar demands.
- **`catalogResolveX` type checks** — every typed resolver verifies `e->type == ASSET_X` before populating the result; wrong-type lookups return 0 and log `[CATALOG-ERROR]` rather than returning stale data.
- **Provider handle abstraction (Phase 4)** — `catalogGetBodyHandle/HeadHandle/PropHandle` + the Phase 4 stage-result `*_handle` fields keep `romProviderHandle()` internal to catalog/provider. Game code never touches provider internals.
- **SHA-256 framework** — the verification machinery exists and is used (line 940 onward). The gap is in when it's required, not whether it's implemented.
- **Distribution defenses layered** — 512 MB `SVC_DISTRIB_BEGIN` cap, 256 MB decompress cap, 128 MB compressed-buffer cap, chunk-index ordering validation, path-traversal filter + `_fullpath` containment, user trust threshold with approval gate. Multiple independent checks.
- **Participant pool as sole slot store** — B-12 Phase 3 concluded cleanly: legacy `chrslots`/`BOT_SLOT_OFFSET`/`CHRSLOTS_*_MASK` fully retired, wire helpers consolidated in `participant.c`, server links `participant.c` directly.

---

## 3) Code Quality Review

### Critical Issues
See C-1 above — the highest-leverage code-quality fix in this audit is in `netbuf.c`.

### High Priority Findings
See H-1 through H-4.

### Medium Priority Findings

- **CQ-M1.** `assetCatalogRegister` uses `malloc`/`realloc` without `free` paths documented. `assetCatalogInit` does `free(s_EntryPool)` and reallocates. Pool never shrinks.
- **CQ-M2.** `struct netclient` contains `struct netbuf out` with a 64 KB `u8 out_data[NET_CLIENT_BUFSIZE]`. Array `g_NetClients[NET_MAX_CLIENTS + 1]` = 33 slots × ~64 KB = ~2 MB BSS. Acceptable; noted for scaling-up bandwidth headroom.
- **CQ-M3.** `rehashTable` does not increment `s_CatalogGeneration` on successful grow; consumers relying on the generation counter for invalidation may miss a pool change. Paired with L-1.
- **CQ-M4.** `buildArchiveDir` iterates `readdir` order → non-deterministic PDCA archive byte layout. Two servers building the same mod tree may emit different archive bytes (different compressed output, different SHA-256). Breaks the "deterministic modding pipeline" pillar if the same mod is hosted by two servers and clients try to cache-share. **Recommendation:** sort entries by path within `buildArchiveDir` before writing.

### Low Priority Findings
- **CQ-L1.** `assetcatalog.c:84` `crc32_table[256]` — 1 KB of pre-computed table, kept for `net_hash` computation even though net_hash is no longer on the wire. Consider removing the CRC32 path and keeping only FNV-1a for identity.
- **CQ-L2.** `netbufReadStr` log path at `netbuf.c:32` prints `rp/wp` but not the requested `num` field's source (field name, caller). Adding `__func__` or a caller tag would speed net-frame forensics.

### Positive Observations
- **Provider vtable is clean** — three functions, documented contract, pure delegation. `assetLoadToNew` fast-paths RomProvider via `fileLoadRomToNew` for byte-identical behaviour vs. the legacy path.
- **`asset_data_handle_t` size** — 24 bytes (ptr + 2×u64) — embeddable in `asset_source_t` and results without per-handle allocation.
- **Participant pool API** — small, coherent surface (`mpIsParticipantActive`, `mpAddParticipantAt`, `mpRemoveParticipant`, `mpParticipantFirst/Next`), good naming.
- **Catalog ID wire format** — session IDs (`u16`) for in-session efficiency, full strings for lobby/connection. Right balance of bandwidth vs. stability.

---

## 4) Security, Trust & Privacy Review

### Critical Issues

See **C-1** (null-termination hazard) above — the primary inbound-message attack surface.

### High Priority Findings

See **H-1** (preserved-player hijacking) and **H-4** (integrity-gap in initial mod distribution).

### Medium Priority Findings

- **S-M1.** `CLC_CHAT` rate limiter uses `SDL_GetTicks()` deltas; `CHAT_RATE_MAX_MSGS` / `CHAT_RATE_WINDOW_MS` bounded but the ring buffer's `rate->head` wraps — if the window isn't saturated, a peer can pulse at window boundaries. Low practical risk.
- **S-M2.** `netmsgClcAuthRead:435-453` ROM hash check is gated on `!g_NetDedicated`. A malicious listen-server host can advertise any ROM CRC (they own the ROM comparison) — but that's fine because listen-server host is trusted. Dedicated-server path skips the check, trusting the server operator has hand-validated their mod set. Correct.
- **S-M3.** `netmsg.c:4505-4533` `CLC_ROOM_SETTINGS_UPDATE` reads arena_id, scenario_str, weapon IDs as catalog strings from clients; server validates via `assetCatalogResolve`. Any resolve failure falls through to legacy defaults. Audit the fall-through consequences in a follow-up: is a client-specified bogus `arena_id` enough to reset the match to a default arena the room didn't agree to?
- **S-M4.** Crafting a `catalog_id` with embedded control characters (`\n`, `\r`, `\t`) passes `assetCatalogResolve` if it matches a registered ID exactly, but Log output via `sysLogPrintf("%s", id)` would inject control chars into log files. Sanitise at log boundaries.

### Low Priority Findings

- **S-L1.** `netbufReadStr` returns a pointer into the mutable receive buffer. No current consumer writes through the pointer, but the type is non-const `char *` — a bug that writes through it would corrupt inbound packet state mid-handler. Make it `const char *`.
- **S-L2.** `connect code` 4-word sentences offer obscurity, not security (per existing audit). Acceptable for the intended "friend invites friend" use case; document if not already.

### Positive Observations
- **Protocol version gate at ENet connect** — mismatched clients rejected before any auth processing (`net.c:1246-1250`).
- **Leader impersonation bounded** — `CLC_LOBBY_START` validates `g_Lobby.leaderSlot == srccl - g_NetClients` (prior audit §10.3).
- **Path traversal defense is layered** — string filter + `_fullpath` containment (netdistrib:628-668).
- **Trust threshold with user approval** for large mod downloads (netdistrib:795-809).
- **Chunk ordering enforced** (netdistrib:841-848).
- **Compressed buffer growth capped** at 128 MB (netdistrib:851-859).
- **Sentinel zero for SHA-absent manifest entries** is a known intentional fallback with a log marker (even if H-4 flags the design choice).

---

## 5) Cross-Cutting Risks & Architecture Concerns

- **[X-1] String-on-wire discipline is unenforced.** `netbufReadStr` is used 27+ times. The safe pattern (read into a caller-owned bounded buffer) is not available from the API. A single `netbufReadStrInto(char *dst, u32 dstsz, struct netbuf *src)` helper with C-1's null-termination guarantee would retire the entire class of findings. → Add the helper; migrate callers opportunistically; deprecate the raw-pointer form.

- **[X-2] Registration-path asymmetry.** Locally-scanned mods and wire-delivered mods reach the catalog through different paths, with different typing discipline and different trust checks (H-2, H-4). Pillar property "if you stripped the labels, you couldn't tell" is violated at the registration boundary even though it holds at the resolve boundary. → Unify on a single `catalogRegisterFromIniDir(path, mod_category)` helper called from both scanner and netdistrib.

- **[X-3] Scaling ceilings cluster on MAX_PLAYERS=8 and MAX_BOTS=32.** The u64 active-slot mask (H-3), the `CATALOG_COLLECT_MAX=256` cap (M-4), `NET_MAX_CLIENTS=32`, `LOBBY_MAX_PLAYERS=32`, numerous `[MAX_PLAYERS]` BSS arrays (`g_Menus`, `g_BgunAudioHandles`, `g_LaserSights`, `g_MpSelectedPlayersForStats`, `g_Pfses`, `g_PlayerExtCfg`, `g_FileLists`) — none break current gameplay, all block the declared "host-bound scaling" future. → Treat as a scaling roadmap item; list the concrete caps in `context/constraints.md` so the roadmap is auditable.

- **[X-4] Trust model asymmetry client↔server.** Clients trust server-sourced strings (kill feed, stage start, catalog info, distribution payloads) without length/content validation; servers validate client strings better but still pass them to `strncpy`/`strcmp` that are vulnerable via C-1. In the dedicated-server game-agnostic vision, a compromised-server scenario is not hypothetical. → Apply C-1's fix on both sides (client-side is worse because server messages drive UI + filesystem writes).

- **[X-5] `asset_entry_t` is a ~2 KB struct** (FS_MAXPATH × multiple + 64-byte ID + union with inner paths). Array-of-struct allocation means doubling the pool to grow. At ~512→1024→2048 entries this is a transient spike but not a bug — flagged for memory-telemetry when mod library grows.

- **[X-6] Dedicated-server boundary still has game-agnostic residue.** The server links `participant.c`, `matchsetup.c` (game-specific), `netmsg.c` (PD2 SVC/CLC IDs). The "game-agnostic dedicated server" pillar requires a plugin boundary. Currently the server IS PD2-specific with a compile-time switch (`PD_SERVER`). → Out of scope for this audit; flag for the dedicated-server pillar roadmap.

- **[X-7] Determinism breakage in archive builder (CQ-M4).** If the modding pipeline is supposed to yield reproducible outputs, `buildArchiveDir`'s `readdir` iteration order must be stabilised.

---

## 6) Verification Limits (Static Analysis vs Runtime)

### Confirmed by code evidence
- C-1 null-termination hazard (call site enumeration in netmsg.c)
- H-1 preserved-player name-only match
- H-2 hot-registered mods as ASSET_NONE
- H-3 u64 active-mask scaling ceiling
- H-4 SHA-absent manifest fallthrough
- M-1 unaligned writer
- M-2 last-write-wins clobber
- M-5 non-null-terminated archive path hazard
- CQ-M4 readdir-order non-determinism

### Probable issues inferred from patterns
- M-7 mid-dispatch netbuf reset (relied on prior audit; not re-verified this session)
- L-1 pointer invalidation across `growEntryPool`

### Unverified — requires runtime
- Exploitability magnitude of C-1 (past-buffer reads in practice depend on ENet packet allocation size vs. buf->size)
- H-1 preserve-window race in a live 3+ client session
- Mod download integrity under a crafted malicious server
- PDCA archive with a deliberately non-null-terminated path field
- Effects of pool realloc on long-lived pointers held by UI renderers (mod manager tree, theme editor)

### What runtime validation needs
- Malformed-peer fuzzing harness against `netmsgClcAuthRead`, `netmsgClcSettingsRead`, `netmsgClcChatRead`, `netmsgClcLobbyStartRead`, `netmsgSvcLobbyKillFeedRead`, `netmsgSvcStageStartRead`.
- Malicious-server fuzzing against `netmsgSvcCatalogInfoRead`, `netmsgSvcDistribBeginRead`, `netmsgSvcDistribChunkRead`, `netmsgSvcDistribEndRead`, `extractArchive`.
- Preserved-player race test: two clients with the same name contending over a preserve slot inside a 10-second window.
- Catalog hot-reload torture test: rapidly toggle mods while `catalogResolveBody` is being called by renderer.
- Multi-client match startup above 32 peers (dedicated server max) to surface further scaling pinches.

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: **7/10** — catalog pillar is well-advanced and the wire protocol's catalog-first discipline is exemplary (v27→v37 evolution). Main gaps: wire-delivered mod registration asymmetry (H-2) and scaling ceilings inherited through convenient data structures (H-3, M-4).

**Code Quality Score**: **7/10** — clean separation of provider/catalog/net layers, typed resolvers, coherent participant pool API. Weak spot is the netbuf string API and the asymmetric read/write endian-safety pattern.

**Security & Trust Score**: **5/10** — strong defense-in-depth on distribution (SHA + size + trust threshold + path containment), but compromised by the null-termination class (C-1) that touches every string-carrying message, and by preserved-player hijacking (H-1). Integrity gap (H-4) compounds. Protocol gate and leader impersonation defenses work correctly.

**Architectural Discipline Score** (Catalog SOT, mod parity, pillar integrity): **8/10** — Catalog SOT is well-defended at resolve time; the wire never carries raw indices; typed resolvers enforce type at the boundary. Gaps are at the ingestion boundary (H-2) and the determinism pillar (CQ-M4).

### Total Findings by Severity
- Critical: **1** (C-1)
- High: **4** (H-1, H-2, H-3, H-4)
- Medium: **12** (M-1..M-8, CQ-M1..CQ-M4, S-M1..S-M4 — S-M2 neutral, counted in framework only where actionable)
- Low: **8** (L-1..L-5, S-L1..S-L2, CQ-L1..CQ-L2)

### Total Findings by Confidence
- Confirmed: 19
- Probable: 3
- Unverified: 3 (depends on runtime / future build validation)

### Total Findings by Lineage
- Authored in PD2: 11 (H-2, H-3, H-4, M-2, M-3, M-4, M-5, M-6, CQ-M3, CQ-M4, L-1)
- Inherited from port / port-net: 6 (C-1, H-1, M-1, M-7, L-5, S-L2)
- Inherited from n64decomp: 2 (L-4, related `MAX_PLAYERS` literals)
- Unknown: 0

### Total Findings by Pillar Impact
- Catalog SOT: 3 (H-2, M-2, L-1)
- Mod-native Parity: 2 (H-2, M-3)
- Modding Pipeline: 3 (H-4, M-4, CQ-M4)
- Grid Trust: 1 (indirect — M-6)
- Online-mode Boundary: 1 (H-1)
- Dedicated-server Boundary: 1 (X-6)
- Scaling Ceiling: 3 (H-3, M-3, M-4)

### Top Scaling Ceilings Identified
- **`SVC_STAGE_START` u64 active-slot mask** (H-3): current ceiling 64 → CPU/bandwidth below the ceiling, wire-protocol change above it.
- **`CATALOG_COLLECT_MAX = 256`** (M-4): caps advertised mods per server → UI/catalog growth.
- **`FILE_PROVIDER_POOL_BYTES = 32 KB` / `MAX_PATHS = 1024`** (M-3): silent null-handle returns → latent mod-loading silent failures.
- **`NET_MAX_CLIENTS = 32`** (ENet peer slots, constants.h): current ceiling 32 → netclient array, per-client BSS.
- **`MAX_PLAYERS = 8`** (constants.h): bakes into `[MAX_PLAYERS]` arrays across bss.h, HUD, menus, audio.
- **`MAX_BOTS = PARTICIPANT_DEFAULT_CAPACITY = 32`** (constants.h): bot headroom; participant pool can `realloc` but the u64 mask caps total.

### Top Data-Layout Integrity Breaches (Phase 2.5)
- **None Critical** — within scope, I found no `sizeof`/stride mismatch or packed-struct drift. The v37 wire format removed the primary layout-mismatch vector (`chrslots` u64 field). The `struct netplayermove` in-memory layout is field-level serialised on the wire, so padding is irrelevant to correctness.
- **Soft findings** (documented but not rising to layout-integrity breach):
  - `asset_entry_t` has been growing (Phase 2 added `asset_source_t source`, Phase 4 added handle fields in the stage result) — no persisted format encodes its size, so in-memory growth is safe. Flag for watchfulness if the struct is ever serialised.
  - `netplayermove.weapon_id[CATALOG_ID_LEN]` (64 bytes) is present in the in-memory struct but not serialised on the wire (the writer ignores it — line 299-317 of netmsg.c only writes `weaponnum`). Dead memory bloat in `netclient::outmove[2]` / `inmove[2]` = 256 bytes per client × 33 clients = ~8 KB waste. Not a layout breach.
  - `crc32_table[256]` = 1 KB BSS for a largely-dead code path post-v27. Trim candidate.

### Top Catalog / Mod-parity Breakages
- **H-2** — wire-delivered mod hot-registers as `ASSET_NONE`, breaks type-based resolution until next refresh.
- **CQ-M4** — archive builder's readdir-order dependence breaks modding-pipeline determinism (same mod tree, different bytes across hosts).
- **H-4** — initial distribution unverified (no SHA at `SVC_CATALOG_INFO`).

### Top 5 Action Items (Highest Impact First)
1. **Fix `netbufReadStr` null-termination (C-1)** — single-function change in `netbuf.c`; eliminates 27+ downstream hazard sites. One-day task. Protocol-compatible.
2. **Add auth token to preserved-player reconnect (H-1)** — protocol bump to v38; adds 16 bytes to `CLC_AUTH` and per-preserved slot. One-day task, tests needed.
3. **Retype wire-delivered mods (H-2)** — rewrite `netDistribClientHandleEnd` registration loop to call typed registrars (factor shared helper with scanner). Two-day task including a scanner-parity test.
4. **Require SHA-256 at `SVC_CATALOG_INFO` + always verify (H-4)** — protocol bump; carries SHA into `SVC_DISTRIB_BEGIN` where it's verified post-decompress. Two-day task.
5. **Length-prefixed active-slot bitmap replacing u64 (H-3)** — protocol bump; dissolves the 64-slot ceiling. Two-day task with participant-pool symmetric encode/decode.

### Must-Fix Before Public Release
- **C-1** (Critical) — every public-facing string read on every CLC/SVC message is currently exploitable.
- **H-1** (High) — griefing vector is realistic and low-effort to exploit.
- **H-4** (High) — supply-chain integrity gap; a single malicious server compromises the installed `mods/` tree of every connected client.
- **H-2** (High) — silent mod-loading failures are release-quality bugs.

### Estimated Effort to Remediate Critical / High Issues
- **C-1**: 4 h (fix + grep-based call-site sweep + run all existing net tests).
- **H-1**: 1 d (add token, protocol bump, preserved-slot binding, tests).
- **H-2**: 2 d (factor scanner registration helper, rewrite netdistrib registration, parity tests).
- **H-3**: 2 d (protocol change, encode/decode symmetry, participant-pool > 64 capacity test harness).
- **H-4**: 2 d (SHA in SVC_CATALOG_INFO, copy to recv slot, unconditional verify).

Total: **~7 working days** for all Critical + High. M-tier items run in parallel as routine cleanup.

---

## 8) Notes and Cross-References
- Prior audit: `context/network-system-audit.md` (2026-03-27) — all CRITs closed; HIGH-1/HIGH-2/ARCH-1/ARCH-2/MED-1..4 still referenced above where relevant.
- Prior audit: `context/scratch/audit-s255-s292-2026-04-16.md` — checked for duplicate findings; no overlap (that audit focused on UI/dev-window/nine-slice chrome).
- Constraints ledger: `context/constraints.md` — "Name-based asset resolution only", "Catalog ID strings at all interface boundaries", and "Phase 4 `romProviderHandle()` internal" are enforced within the surveyed surfaces.
- Pillar definitions: `.claude/skills/super-audit/audit-prompt.md` §Major architectural pillars.
- Design doc for provider abstraction: `context/designs/direct-file-access-design-2026-04-17.md` (referenced by `assetprovider.h:18-20`).
- Current task list: `context/tasks-current.md` (S377 AP Phase 3 closed; Phase 4 queued — H-2 overlaps Phase 4 scope partially).

---

*End of report.*
