# Comprehensive Bug & Systemic Issue Audit
**Date**: 2026-04-02
**Scope**: `src/game/`, `src/lib/`, `port/src/`, `port/fast3d/`, `port/include/`, `port/src/net/`
**Type**: Read-only audit — no source files modified
**Auditor**: Claude Sonnet 4.6 (AI-assisted code review)

---

## Executive Summary

| Severity | Count |
|----------|-------|
| CRITICAL | 2 |
| HIGH | 3 |
| MEDIUM | 8 |
| LOW | 6 |
| **Total** | **19** |

**Top priorities**: The ChrResync null-prop buffer desync (CRITICAL-1) can crash clients or corrupt all bot state every time the network sends a prop the client doesn't recognize. The distrib archive_bytes unbounded allocation (CRITICAL-2) allows a malicious server to trigger a multi-gigabyte malloc. Both need fixing before any multiplayer beta.

---

## CRITICAL Findings

### CRITICAL-1 — ChrResync null-prop skips without consuming fields (buffer desync / crash)

**File**: `port/src/net/netmsg.c`, lines 2756–2803
**Category**: Network protocol, crash vector

**Issue**: In `netmsgSvcChrResyncRead`, when `netbufReadPropPtr()` returns NULL for a bot entry (prop not found locally), the code does `continue` without consuming the remaining ~20 fields that the server serialized for that bot. This desynchronizes the netbuf read cursor, corrupting the deserialization of every subsequent bot entry in the packet.

```c
for (u8 i = 0; i < botcount; ++i) {
    struct prop *prop = netbufReadPropPtr(src);
    if (!prop) {
        continue;  // BUG: 20+ reads are NOT consumed for this slot
    }
    struct coord pos;
    netbufReadCoord(src, &pos);    // skipped
    f32 angle = netbufReadF32(src);// skipped
    // ... 15+ more reads skipped
```

**Impact**: Any mismatch between server and client prop tables (e.g., late-joining client, prop not yet spawned) causes all subsequent bot sync data to be parsed from the wrong offset. This produces garbage position/health values for all bots after the null slot, and on a short packet can cause `netbuf` to read past the end of the receive buffer (OOB read → crash or undefined behavior). In practice this will trigger every time a client receives a resync packet that contains a bot the client hasn't spawned yet.

**Fix approach**: When `prop == NULL`, still consume all the fields for that slot (read them and discard), then `continue`. Alternatively, if the server only sends bots that exist on both sides, assert that and log an error without `continue`-ing.

**Effort**: S (10–15 minutes — add a skip-path that calls all the same read functions)

---

### CRITICAL-2 — archive_bytes from network used directly for malloc without upper bound

**File**: `port/src/net/netdistrib.c`, lines 762–768
**Category**: Network protocol, memory safety

**Issue**: The `archive_bytes` field comes from the server's `SVC_DISTRIB_BEGIN` message. It is stored as a `u32` (or similar) in the receive slot and used directly to compute a `malloc` size with only +1024 headroom and a 65536 floor:

```c
uLongf raw_len = (uLongf)(slot->archive_bytes + 1024);
if (raw_len < 65536) raw_len = 65536;
u8 *raw = (u8 *)malloc(raw_len);
```

A malicious or buggy server can send `archive_bytes = 0xFFFFFFFF` (or any very large value), causing `raw_len` to overflow or requesting a multi-gigabyte allocation. On 64-bit with `uLongf = unsigned long` (8 bytes on Linux, 4 bytes on Windows), integer overflow behavior differs by platform. On Windows (where `uLong` is 32-bit), `0xFFFFFFFF + 1024` wraps to 1023, causing a tiny allocation and later buffer overwrite during decompression.

**Impact**: Crash via OOM (benign case), or wrapping to a tiny buffer followed by heap corruption when zlib writes decompressed data into it (exploitable).

**Fix approach**: Add a sanity cap before the malloc: `if (slot->archive_bytes > DISTRIB_MAX_UNCOMPRESSED_BYTES) { log error; goto done; }` where `DISTRIB_MAX_UNCOMPRESSED_BYTES` is a reasonable ceiling (e.g., 256 MB). Also check for `raw_len < slot->archive_bytes` as an overflow indicator.

**Effort**: S (add one validation block before line 762)

---

## HIGH Findings

### HIGH-1 — SVC_PLAYER_MOVE: client ID from network not bounds-checked before array index

**File**: `port/src/net/netmsg.c`, lines 1192–1203
**Category**: Network protocol, memory safety

**Issue**: `id = netbufReadU8(src)` reads a u8 from the packet, which can be 0–255. The code then does `struct netclient *movecl = &g_NetClients[id]` with no check that `id < NET_MAX_CLIENTS + 1`. The array `g_NetClients` has `NET_MAX_CLIENTS + 1` slots (the +1 is the temp/local slot). A malformed server packet with `id > NET_MAX_CLIENTS` causes an out-of-bounds array access.

```c
id = netbufReadU8(src);
outmoveack = netbufReadU32(src);
netbufReadPlayerMove(src, &newmove);
// ...
if (src->error || srccl->state < CLSTATE_GAME) {
    return src->error;  // only error-checked AFTER the read
}
struct netclient *movecl = &g_NetClients[id];  // no bounds check
```

**Impact**: OOB read/write into adjacent memory. On the client side this reads into whatever follows `g_NetClients[]`, potentially corrupt state or crash.

**Fix approach**: Add `if (id >= NET_MAX_CLIENTS + 1) { return 1; }` immediately after the `src->error` check block. Follow the same pattern already used correctly in `netmsgSvcPlayerStatsRead` (lines 1267–1276).

**Effort**: S (two-line guard)

---

### HIGH-2 — sprintf into fixed 50-byte buffer with unbounded langGet() strings

**File**: `port/src/net/netmsg.c`, lines 3470–3481
**Category**: Memory safety, crash vector

**Issue**: In `netmsgSvcObjStatusRead`:

```c
char buffer[50] = "";
sprintf(buffer, "%s %d: ", langGet(L_MISC_044), availableindex + 1);
// then:
strcat(buffer, langGet(L_MISC_045)); // "Completed"
```

`langGet()` returns a pointer to a localization string. If `L_MISC_044` ("Objective") is longer than ~41 chars in a given locale, `sprintf` overflows `buffer[50]`. The subsequent `strcat` of `L_MISC_045`/046/047 appends onto an already potentially full buffer.

**Impact**: Stack buffer overflow → crash. Exploitable if language file is attacker-controlled (e.g., mod-injected lang file). In practice, the English strings are short enough, but any locale with long translations will crash.

**Fix approach**: Replace with `snprintf(buffer, sizeof(buffer), ...)` and replace `strcat` with `strncat(buffer, str, sizeof(buffer) - strlen(buffer) - 1)`, or size the buffer dynamically. Best fix: use a 256-byte buffer and snprintf+strncat.

**Effort**: S

---

### HIGH-3 — fread return value unchecked in savefile load

**File**: `port/src/savefile.c`, line 177
**Category**: Save/load robustness, data corruption

**Issue**:
```c
fread(buf, 1, len, fp);
buf[len] = '\0';
```

The return value of `fread` is not checked. A partial read (disk I/O error, file truncated mid-write from a previous crash, or network filesystem drop) silently succeeds with fewer bytes than `len`. The rest of `buf` contains whatever was in the malloc'd buffer (uninitialized or previous data). This uninitialized garbage is then null-terminated and passed to the JSON parser.

**Impact**: Save data is silently corrupted. The JSON tokenizer will parse garbage, potentially producing wrong values for player stats, unlocks, settings, or keybinds. No error is reported to the user.

**Fix approach**: Check `fread` return: `size_t nread = fread(buf, 1, len, fp); if (nread != (size_t)len) { log error; free(buf); fclose(fp); return; }`. Also check `ferror(fp)`.

**Effort**: S

---

## MEDIUM Findings

### MEDIUM-1 — netmsgClcChatRead: 1024-byte `tmp` is dead code

**File**: `port/src/net/netmsg.c`, line 434
**Category**: Code quality, minor

**Issue**: `char tmp[1024]` is declared but never used. `msg` points directly into the receive buffer. The variable wastes 1KB of stack and suggests the original intent was to copy the string for safety (preventing use-after-buffer-recycle), which was not completed.

```c
u32 netmsgClcChatRead(struct netbuf *src, struct netclient *srccl)
{
    char tmp[1024];  // declared but never used
    const char *msg = netbufReadStr(src);  // points into src buffer
```

**Impact**: `msg` is passed to `netmsgSvcChatWrite` which calls `netbufWriteStr`. If the same netbuf is reused before the write completes, this would be a use-after-recycle. Currently safe because `g_NetMsgRel` is a separate buffer, but fragile. The dead variable is a maintenance hazard.

**Fix approach**: Remove `tmp[1024]` entirely, or intentionally copy into it with `snprintf(tmp, sizeof(tmp), "%s", msg)` and pass `tmp` instead of `msg`.

**Effort**: XS

---

### MEDIUM-2 — Chat messages rebroadcast without length validation or rate limiting

**File**: `port/src/net/netmsg.c`, lines 432–443; `port/src/net/net.c`, line 1661
**Category**: Network protocol, denial of service

**Issue**: The server's `netmsgClcChatRead` reads whatever string the client sends (via `netbufReadStr`) and immediately rebroadcasts it to all clients without validating length or rate-limiting. A client can send 1000 chat messages per second, each filling the maximum netbuf payload, forcing the server to relay all of them to every connected client.

**Impact**: Server-side relay amplification: N attacker messages become N × (client_count) outgoing messages. Potential to saturate bandwidth or fill ENet's send queue. Also, no length cap means a chat message can be arbitrarily long up to the netbuf limit.

**Fix approach**: (1) Cap chat string length server-side at e.g. 256 chars before rebroadcast. (2) Add a simple per-client rate limit (e.g., max 2 chat messages per second using a timestamp + token bucket). Both are independent improvements.

**Effort**: S

---

### MEDIUM-3 — netdistrib: chunk_idx silently ignored, out-of-order chunks corrupt data

**File**: `port/src/net/netdistrib.c`, line 723
**Category**: Network protocol, data integrity

**Issue**: `SVC_DISTRIB_CHUNK` includes a `chunk_idx` field that is read but immediately discarded: `(void)chunk_idx;`. Chunks are always appended in receive order. If ENet delivers packets out of order (e.g., due to network reordering, retransmission on a different channel, or deliberate reordering), the decompressed archive will be silently corrupted.

**Impact**: Mod distribution results in a corrupt archive silently treated as valid, causing unpredictable behavior when the asset is loaded. Could cause crashes in asset loading code that doesn't validate archive integrity.

**Fix approach**: Either (a) enforce ordered delivery by using a reliable ordered ENet channel for distribution chunks, or (b) implement the chunk_idx to reassemble chunks in order. Option (a) is simpler given ENet's channel model.

**Effort**: M (requires deciding on reliable vs. reorder-buffer approach)

---

### MEDIUM-4 — netdistrib SVC_DISTRIB_BEGIN: archive_bytes stored without validation at receipt time

**File**: `port/src/net/netdistrib.c`, around lines 700–720 (SVC_DISTRIB_BEGIN handler)
**Category**: Network protocol, input validation

**Issue**: Related to CRITICAL-2. The `archive_bytes` value from the server is stored into the receive slot at BEGIN time without any cap. Only at END time is it used for malloc. Any validation added for CRITICAL-2 should also happen at BEGIN time to reject the slot early.

**Impact**: Slot occupancy with invalid data for the duration of the transfer.

**Fix approach**: Add the same cap at BEGIN time. If `archive_bytes > DISTRIB_MAX_UNCOMPRESSED_BYTES`, log and reject the slot without storing.

**Effort**: XS (companion fix to CRITICAL-2)

---

### MEDIUM-5 — pdsched.c: @TODO re threading/scheduling in main loop

**File**: `port/src/pdsched.c`, line 369
**Category**: Code quality / potential correctness

**Issue**: A `@TODO: Investigate` comment at line 369 marks an unresolved question in the scheduler. Line 213 also has a `// TODO: make this a little less awkward` comment in the scheduling logic. Without reading the full function, these mark known correctness questions in frame-timing code.

**Impact**: Scheduling anomalies under high load or unusual frame timing could produce frame-stutter or incorrect input timing. Low likelihood, medium impact if it manifests.

**Fix approach**: Investigate and resolve the noted TODO — document what was found, even if the behavior is acceptable.

**Effort**: M (investigation required)

---

### MEDIUM-6 — audio.c: sample rate hardcoded to 22020 Hz with known platform risk

**File**: `port/src/audio.c`, line 66
**Category**: Compatibility, correctness

**Issue**:
```c
want.freq = 22020; // TODO: this might cause trouble for some platforms
```
22020 Hz is an unusual sample rate (not 22050 Hz, not 44100 Hz). SDL2 will attempt to open the audio device at this rate; if the hardware/driver doesn't support it, SDL2 may silently resample or fail. The misnamed rate (22020 vs 22050) may also indicate a typo from the original N64 audio frequency.

**Impact**: Audio quality degradation (pitch shift or distortion) on systems that don't natively support 22020 Hz. Silent failure possible.

**Fix approach**: Verify whether the game requires exactly 22020 Hz (N64 audio system ran at 22050 Hz in most variants). If 22050 Hz is correct, fix the typo. If 22020 is intentional, document why and add `SDL_AUDIO_ALLOW_FREQUENCY_CHANGE` to the open flags with explicit resampling.

**Effort**: S

---

### MEDIUM-7 — savefile.c: JSON tokenizer susceptible to deep-recursion on malformed input

**File**: `port/src/savefile.c`, `s_skip_value` and related recursive descent functions
**Category**: Save/load robustness, crash vector

**Issue**: The custom mini JSON tokenizer uses recursive functions to parse nested structures. A malformed save file with pathological nesting (e.g., 10,000 levels of `[[[[...`)) will exhaust the stack via unbounded recursion, causing a stack overflow crash.

**Impact**: A corrupted or attacker-crafted save file crashes the game on load. Severity is lower than CRITICAL since save files are local, but modded/shared saves can trigger this remotely.

**Fix approach**: Add a recursion depth counter passed through the recursive calls; abort parsing with an error if depth exceeds a reasonable limit (e.g., 64).

**Effort**: S–M

---

### MEDIUM-8 — main.c: TODO for subsystem shutdown on quit

**File**: `port/src/main.c`, line 125
**Category**: Resource management, stability

**Issue**: `// TODO: actually shut down all subsystems` — the quit path does not cleanly shut down all systems. This means network connections, ENet state, SDL audio, and open file handles may not be flushed on exit.

**Impact**: On normal exit, the OS reclaims resources, so usually harmless. However, dirty exit without flushing ENet means remote peers may not receive a disconnect notification, leaving them in a connected-but-dead state. Also, any pending save file writes may not be flushed.

**Fix approach**: Implement a shutdown sequence: flush pending saves, call `netDisconnect()`, call `enet_deinitialize()`, call `SDL_Quit()`.

**Effort**: M

---

## LOW Findings

### LOW-1 — netdistrib: buildArchiveDir returns stale pointer on realloc failure

**File**: `port/src/net/netdistrib.c`, lines 178–184
**Category**: Error handling

**Issue**: On realloc failure in `buildArchiveDir`, the function closes `fp` and `d`, then returns the old `buf` pointer, but `*buf_len` is not updated to reflect the truncated data. Callers may use the returned buffer believing it's complete.

**Impact**: Sends a truncated/corrupt archive to clients. Contained but silent — the client receives bad data without an explicit error signal.

**Fix approach**: On realloc failure, set `*buf_len = 0`, free the old buf, and return NULL. Callers should check for NULL return.

**Effort**: S

---

### LOW-2 — distribSendPacketToPeer: enet_peer_send return value unchecked

**File**: `port/src/net/netdistrib.c`, `distribSendPacketToPeer`
**Category**: Error handling, networking

**Issue**: The return value of `enet_peer_send()` is not checked. ENet returns -1 on failure (e.g., peer is disconnecting, packet creation failed). Silently ignoring this means a failed distribution send goes undetected.

**Impact**: Distribution appears to succeed server-side but the client never receives data. The client may hang waiting for `SVC_DISTRIB_END` or timeout silently.

**Fix approach**: Check return value; if -1, log a warning and mark the distribution slot as failed for that peer.

**Effort**: XS

---

### LOW-3 — input.c: VK name table populated with strcpy into fixed-size entries

**File**: `port/src/input.c`, lines 612–646
**Category**: Memory safety (low risk)

**Issue**: `vkNames[key]` entries are populated using `strcpy` from SDL key names (retrieved via `SDL_GetScancodeName`). If SDL returns an unexpectedly long key name and the `vkNames` entries are fixed-size, this could overflow. The risk depends on `vkNames` entry size (not verified in this audit).

**Impact**: Unlikely to trigger in practice since SDL key names are short, but non-zero risk with unusual/extended hardware.

**Fix approach**: Use `strncpy` with the known entry size, or use `snprintf`.

**Effort**: XS

---

### LOW-4 — mpsetups.c: strcpy into setup name buffers without explicit size guard

**File**: `port/src/mpsetups.c`, lines 571–577
**Category**: Memory safety (low risk)

**Issue**: Three `strcpy` calls copy between `setup->bytes`, `name`, and `g_MpSetup.name` without explicit size bounds. Safety depends on the invariant that these buffers all share the same `MPSETUP_MAXNAME` size and the data was already validated when loaded.

**Impact**: Low — the data originates from a trusted local save file. If a malformed setup file is imported, the name could be too long.

**Fix approach**: Use `strncpy` with the known `MPSETUP_MAXNAME` size for all three copies.

**Effort**: XS

---

### LOW-5 — fs.c: strcpy into homeDir from exeDir without explicit size check

**File**: `port/src/fs.c`, line 144
**Category**: Memory safety (low risk)

**Issue**: `strcpy(homeDir, exeDir)` — both are `FS_MAXPATH`-sized arrays. The code relies on `sysGetExecutablePath` not filling `exeDir` to exactly `FS_MAXPATH` without a null terminator. If the exe path is exactly `FS_MAXPATH - 1` characters plus a null, the copy is safe. If `sysGetExecutablePath` returns a truncated path without null, the behavior is undefined.

**Impact**: Very low — only triggers on extremely deep installation paths. Crash or silent data corruption.

**Fix approach**: Replace with `strncpy(homeDir, exeDir, FS_MAXPATH - 1); homeDir[FS_MAXPATH - 1] = '\0';`

**Effort**: XS

---

### LOW-6 — fast3d/gfx_pc.cpp: multiple HACK/TODO comments indicating known rendering compromises

**File**: `port/fast3d/gfx_pc.cpp`, lines 603, 1679, 1808, 1811
**Category**: Rendering correctness

**Issue**: Known rendering hacks:
- Line 603: `// TODO: trips in some places with garbage size` — texture size not validated before use
- Line 1679: aspect ratio hardcoded / assumed for framebuffers (HACK comment)
- Lines 1808, 1811: texture format fallback hacks for CI4/CI8 formats

**Impact**: Potential rendering artifacts in specific levels or with specific textures. The framebuffer aspect ratio assumption will produce wrong rendering at non-standard resolutions.

**Fix approach**: Each requires targeted investigation. Line 603 should add a size validation guard. Line 1679 should use the actual configured resolution. Lines 1808/1811 should be documented or resolved when texture format support is expanded.

**Effort**: M each

---

## Systemic Patterns

### Pattern 1 — `sprintf` used instead of `snprintf` (pervasive)

**Instances**: 8 in `port/src/*.c`, 55+ in `port/fast3d/*.cpp`, 350+ across `src/`
**Risk**: Buffer overflow wherever the format string can produce output longer than the destination buffer. Current instances in `port/` use short literals ("%.2f", "%d FPS") and a 16-byte destination — safe in practice. The `netmsg.c` instance (HIGH-2) uses `langGet()` with unbounded output — unsafe.

**Recommended sweep**: Replace all `sprintf(buf, ...)` with `snprintf(buf, sizeof(buf), ...)`. This is a mechanical change; do it in batches by file. The `port/fast3d/` tree has 55+ instances in the renderer — lower priority since they use literal format strings with bounded output, but should still be fixed for correctness.

---

### Pattern 2 — Network data used as array index without bounds check (systemic)

**Instances confirmed**: `netmsgSvcPlayerMoveRead` (HIGH-1). Likely similar patterns exist in other message handlers.

**Recommended sweep**: Audit every message handler that reads an index or count from the network and uses it to index an array. Search: `netbufReadU8\|netbufReadU16` followed within 5 lines by `\[`. For each, verify the value is bounds-checked before use.

---

### Pattern 3 — `fread` / `fwrite` return values unchecked

**Instances**: At least 1 confirmed (`savefile.c` line 177). Likely others across file I/O code.

**Recommended sweep**: Grep `fread\s*(` in `port/src/` and verify every call checks the return value. Same for `fwrite`.

---

### Pattern 4 — `strcpy` used where `strncpy` is appropriate

**Instances**: 20+ across `port/src/` (input.c, mpsetups.c, fs.c, optionsmenu.c)
**Risk**: Low in current code because data mostly originates from trusted local sources. Risk increases when data paths cross network or untrusted files.

**Recommended sweep**: Replace all `strcpy(dest, src)` with `strncpy(dest, src, sizeof(dest) - 1); dest[sizeof(dest) - 1] = '\0';` where `dest` is a known-size array.

---

### Pattern 5 — Unchecked `malloc`/`realloc` (selective)

**Instances**: `netdistrib.c` checks malloc return (good). `assetcatalog.c` checks realloc (good). Some smaller allocations in game code are unchecked.

**Risk**: OOM in long-running sessions or under heavy load causes NULL dereference crash.

**Recommended sweep**: Grep `= malloc\|= realloc\|= calloc` and verify every call checks for NULL before use.

---

## Recommended Fix Order

This order maximizes stability and security impact per unit of effort:

### Tier 1 — Fix Before Multiplayer Beta (CRITICAL + highest-impact HIGH)

1. **CRITICAL-1**: ChrResync null-prop buffer desync (`netmsg.c:2762`) — S effort, guaranteed crash in normal play
2. **CRITICAL-2**: Unbounded malloc from archive_bytes (`netdistrib.c:762`) — S effort, exploitable by malicious server
3. **HIGH-1**: SVC_PLAYER_MOVE id bounds check (`netmsg.c:1203`) — S effort, OOB array access from network
4. **HIGH-3**: fread unchecked in savefile load (`savefile.c:177`) — S effort, silent save corruption

### Tier 2 — Fix Before Public Release (HIGH + high-impact MEDIUM)

5. **HIGH-2**: sprintf buffer overflow in objective HUD (`netmsg.c:3470`) — S effort, locale-dependent crash
6. **MEDIUM-2**: Chat rebroadcast rate limiting — S effort, DoS amplification
7. **MEDIUM-3**: Chunk order ignored in mod distribution — M effort, silent data corruption
8. **MEDIUM-4**: Validate archive_bytes at BEGIN time — XS effort, companion to CRITICAL-2
9. **MEDIUM-7**: JSON tokenizer stack overflow on deep nesting — S effort, crafted save crash

### Tier 3 — Quality Pass (MEDIUM/LOW)

10. **MEDIUM-1**: Remove dead `tmp[1024]` in chat handler — XS
11. **MEDIUM-6**: Fix audio sample rate (22020 vs 22050 Hz) — S
12. **MEDIUM-8**: Implement clean shutdown sequence — M
13. **LOW-1**: buildArchiveDir: set buf_len=0 on failure — S
14. **LOW-2**: Check enet_peer_send return value — XS
15. **LOW-3/4/5**: strcpy → strncpy in input, mpsetups, fs — XS each

### Systemic Sweeps (schedule as a dedicated cleanup sprint)

- Replace all `sprintf` → `snprintf` in `port/src/` (Pattern 1)
- Audit all network-derived array indices for bounds checks (Pattern 2)
- Check all `fread`/`fwrite` return values (Pattern 3)
- Replace remaining `strcpy` → `strncpy` (Pattern 4)
- Verify NULL checks on all allocations (Pattern 5)

---

*End of audit. 19 findings total. No source files were modified during this audit.*
