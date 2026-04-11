# Updater Parse Diagnosis — 2026-04-11

**Session**: S199 (agitated-wozniak worktree)
**Symptoms**: v0.0.75 not in client/server update list; "Couldn't parse update list" on manual Check for Updates.

---

## Hypothesis Ranking (post-investigation)

| # | Hypothesis | Verdict | Evidence |
|---|-----------|---------|----------|
| H1 | Two parsers, only one works | **FALSE** | Single code path: `updaterCheckAsync()` → `checkThread()` → `parseReleasesJson()`. UI "Check Now" and startup both call the same function. |
| H2 | Tag/name format drift | **FALSE** | v0.0.75 tag = `v0.0.75` (confirmed via GitHub API). `UPDATER_TAG_CLIENT = "v"` matches. `versionParseTag("v0.0.75",...)` → `versionParse("v0.0.75",...)` → `sscanf("0.0.75",...)` → 0.0.75 ✓ |
| H3 | Asset-pattern mismatch | **FALSE** | v0.0.75 asset = `PerfectDark-v0.0.75-win64.zip` → ends in `.zip` = `UPDATER_ASSET_ZIP_SUFFIX` ✓. Asset URL found and populated. |
| H4 | Off-by-one / fixed-size buffer | **FALSE** | `UPDATER_MAX_RELEASES = 64`. `per_page=30` means ≤30 results. body = 1099 bytes < `UPDATER_MAX_BODY_LEN` (2048). |
| H5 | Prerelease flag filtering | **ELIMINATED** | v0.0.75 IS `prerelease=true`. **Confirmed by Mike: user was on Dev channel.** Dev channel does not filter prereleases. All three symptoms are a real bug, not expected behavior. |
| **H6** | **GitHub returning non-array response** | **MOST LIKELY ROOT CAUSE** | `parseReleasesJson` returns -1 ONLY when top-level token ≠ `JTOK_LBRACKET`. A GitHub rate-limit response (`{"message":"API rate limit exceeded",...}`) is a JSON object, not array → causes this path. The error "could not parse response" is displayed as "Couldn't parse update list" in the UI. |

---

## Code Path Map

### Startup check (fires at game launch)
```
updaterInit()         port/src/updater.c:~900
  └── SDL_CreateThread(checkThread)
        └── checkThread()            updater.c:644
              ├── curlGet(GitHub API, per_page=30)  updater.c:649-654
              └── parseReleasesJson(buf.data)        updater.c:591-637
                    └── returns count (≥0) or -1 (non-array response)
```

Log: `UPDATER: parsed N valid release(s) from API response`

### UI "Check for Updates" button
```
pdgui_menu_update.cpp — "Check Now" button onClick
  └── updaterCheckAsync()          updater.c:1044
        └── SDL_CreateThread(checkThread)
              └── (same as startup path above)
```

UI display: `UPDATER_CHECK_FAILED` → `ImGui::Text("Check failed: %s", updaterGetError())`  
Error string: `"Update check failed: could not parse response"` (displayed as "Couldn't parse update list")

### Server update check
```
server_gui.cpp — "Check Now##srv" button onClick
  └── updaterCheckAsync()          updater.c:1044
        └── (same as startup path above)
```

**Confirmed**: server uses IDENTICAL parser. `UPDATER_TAG_SERVER = "v"` (same as client). Single shared updater instance. No separate fix needed.

---

## v0.0.75 vs v0.0.74 Structural Comparison (GitHub API)

| Field | v0.0.74 | v0.0.75 | Parser impact |
|-------|---------|---------|---------------|
| `tag_name` | `v0.0.74` | `v0.0.75` | Both parse fine |
| `prerelease` | `true` | `true` | Same — dev channel includes both |
| `draft` | `false` | `false` | Both included |
| Asset `.zip` | `PerfectDark-v0.0.74-win64.zip` | `PerfectDark-v0.0.75-win64.zip` | Both end in `.zip` ✓ |
| `body` length | 1099 chars | 1099 chars (identical content) | Both within 2048-byte limit |
| Total releases | 52 | 53 | per_page=30 returns 30 newest, includes both |

**No structural difference between v0.0.74 and v0.0.75 that would affect parsing.**

---

## Why "doesn't appear in list" follows from "parse error"

All three symptoms trace to the same root: `parseReleasesJson` returns -1.

- `count < 0` → `s_Updater.status = UPDATER_CHECK_FAILED`
- `s_Updater.releaseCount` stays 0 (never updated)
- `updaterGetReleaseCount()` returns 0 → no rows in version table → v0.0.75 "not in list"
- UI shows `"Check failed: Update check failed: could not parse response"` → Mike sees "Couldn't parse update list"

---

## Confirmed Bugs Fixed in This Session

### Bug 1: `per_page=30` hardcoded (minor, future-proof fix)
**File**: `port/src/updater.c:650`
**Issue**: 53 total releases and growing. `per_page=30` always returns the 30 newest, so v0.0.75 is still included — but eventually old releases people want to pin to could fall off the list. More importantly, GitHub's max per_page is 100.
**Fix**: `per_page=30` → `per_page=100`

### Bug 2: No diagnostic logging on parse failure (instrumentation)
**File**: `port/src/updater.c:668-672` (checkThread, count < 0 branch)
**Issue**: When parse fails, the log just says "could not parse response" with no indication of what GitHub actually returned. Impossible to distinguish rate limit vs. bad JSON vs. HTML error page.
**Fix**: Log raw response preview (first 200 chars) + response size on every failure path.

### Bug 3: HTTP status code not captured
**File**: `port/src/updater.c` (curlGet)
**Issue**: `curlGet` uses `CURLOPT_FOLLOWLOCATION` and returns `CURLcode`. An HTTP 403 (rate limit) returns `CURLE_OK` because curl completed successfully — but the body is a JSON error object. No way to know the HTTP status without explicitly reading it.
**Fix**: After `curl_easy_perform`, retrieve `CURLINFO_RESPONSE_CODE` and log if non-200.

---

## What "doesn't appear in list" could mean on stable channel (NOT a bug)

If the client is on the **stable** channel (`UPDATE_CHANNEL_STABLE`), the channel filter in `parseReleasesJson:615` skips all prerelease releases:
```c
if (s_Updater.channel == UPDATE_CHANNEL_STABLE && rel.isPrerelease) {
    continue;
}
```
Since v0.0.75 is `prerelease=true`, it would be filtered. This is EXPECTED behavior — dev builds are intentionally hidden on the stable channel. If this is the reported scenario, the fix is: the user should switch to the Dev channel in Settings > Updates.

---

## Fix: Code Changes (`port/src/updater.c`)

Three targeted changes:
1. `per_page=30` → `per_page=100` (line 650)
2. HTTP status code logged via `CURLINFO_RESPONSE_CODE` in `curlGet`
3. Raw response preview logged when `parseReleasesJson` returns -1

See commit in this session for exact diff.

---

## Next Steps

1. Reproduce the error to confirm — deploy and check log output
2. If "HTTP 403" appears in log: add backoff/retry logic for rate limits
3. If some other non-array response: investigate the specific GitHub error
4. If no error in log (parse succeeds after fix): may have been a transient network issue

---

*Generated: 2026-04-11 | Session: S199*
