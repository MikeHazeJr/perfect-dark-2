# Approved PerfectDark workspace disposition — 2026-10-03

Mike approved both current PerfectDark app-access verification and removal of
the specific old failed test copy in Slack `#perfect-dark1791057011.004399`,
with Recycle Bin preferred. This approval supersedes the earlier D-012 choice
wait. His direct instruction to commit and push remains publication authority.
Model: gpt-6; coordination session `01a0f5c1-d031-77b0-a372-3391765df0d5`.

## Actual recoverable disposition

Only `.claude/smoke-storage/shared/client` was moved, belonging to run
`20261002T063150413966Z-menu_virtual_controller_agent_cancel-d8f83de6`.
It was completed, failed and unpinned; its owner marker matched. The immutable
seed, complete recipe, 12,389 referenced blobs and live payload hashes/file
metadata all passed. All 13,450 files passed exclusive nonuse probes. The
payload was 361,670,914 bytes; its separately verified owner marker added 112
bytes. An initial overly strict total-byte check stopped before mutation;
the registry intentionally excludes that marker, and the corrected check
verified both byte counts without changing production storage behavior.

At **20:18:43.862 UTC**, Windows Shell IFileOperation moved that one directory
to Mike's Recycle Bin, creating payload `$RXMU22N` and metadata `$IXMU22N`.
The pre-delete callback required recycle semantics and the exact target scope;
the post-delete callback supplied the new Recycle Bin item. The original path
is absent. The new metadata names exactly that original directory, and the
entire recycled payload was rechecked against the archive and original file
metadata, including the owner marker. Verification completed **20:18:51 UTC**.
There was no permanent-delete fallback and the Recycle Bin was not emptied.
This uses Microsoft's [RecycleOnDelete operation flag](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-setoperationflags)
and [post-delete Recycle Bin item](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperationprogresssink-postdeleteitem).

The registry changed only that run's `status`, `full_bytes` and new `recycled`
receipt. Managed full copies decreased from three to two. Both pinned failed
copies, current client/test binaries, retained diagnostic client, seed, recipe,
blobs and logs remain. The archive is 842,725,830 bytes; storage policy and caps
are unchanged. The Recycle Bin retains the full copy, so this frees a managed
workspace slot and **does not reclaim disk space**.

Private evidence is under
`.claude/pd-initial-integration/20261003-night/workspace-consent/`:

| Receipt | SHA-256 |
|---|---|
| `verified-plan.json` | `8edecd672b9d9f40b393bb6e70ef7015db4d26690cab6871d561945e0c1132cd` |
| `recycle-verified.json` | `890aa99228cf33d9834141b9d58e24b66080df6a957791953d13dd3114d7ac8f` |
| `registry-disposition.json` | `3c7f5cd36aa2e6e7a43b3d33703aeafa82096ed2af0c45b2285ac32c363946b0` |
| `app-interface.json` | `663486409aa12883c96c21276f3006f083f3958cc2ea0f5c622a60dd8d45058c` |

`registry-delta.json` separately verifies the saved pre-operation registry
against the actual new registry: only the approved row changed. Native output,
the complete before-state backup, scoped helpers and initial refusal are kept.
Owned removal executors were absent at **20:19:44.473 UTC**. Storage and heavy
queues were immediately finished, well before the 20:23:50 window deadline.

## App-access and ordinary runtime gate

D-010 is decided as user consent. **Actual app access remains unverified.**
This executor's 336-tool inventory exposes no native Computer Use, node_repl,
Sky or tool-search interface; no live app prompt/request ID is available.
No denied GUI call was retried, no grants/settings were rewritten, and no
game or capture was launched. This is a capability blocker, not another
request for Mike to repeat approval.

Route the unchanged prepared ordinary gate to an executor with the supported
native Computer Use interface. After actual app access is verified, it needs
one fresh coordinated **15-minute** runtime window including owned cleanup,
ending before today's **20:45 UTC / 16:45 Eastern** cutoff. D-012 copy handling
is complete; D-010 actual access and initial asset/menu fidelity remain open.
Existing build/unit/hosted CI acceptance is unchanged and is documented in
[the passing source-contract audit](pd-source-contract-ci-repair-2026-10-03.md).
T-TOOLING-008 remains partial; no full initial-scope acceptance is claimed.

## 20:55 UTC continuation: supported local capability proven

Mike explicitly cancelled today's October 3 Plex pause for Astral and PD in
edited `#general1791060142.951179` and confirmation `1791060214.886569`.
The earlier 20:45 cutoff above is historical for this day; eligible work may
continue through the usual 07:00 Eastern night end. Future defaults remain.
All project deletions require Recycle Bin recovery. D-012 records and payload
remain intact; no additional removal occurred.

The supported local route is the installed app-managed
`C:/Users/mikeh/AppData/Local/OpenAI/Codex/bin/be3fd7e5c1969ff6/codex.exe`,
version **0.159.0-alpha.12.1**. Its current Computer Use feature and configured
native `node_repl` server are enabled. The stale Store CLI 0.126 lacks the
automatic-review argument used by this route. No configuration, feature,
profile, grants or security settings were changed. The current installed
Computer Use skill **26.930.21537**, guidance, API and confirmations were read.
No helper executable was launched directly and no custom client was built.

One bounded capability-only CLI worker ran **20:53:46–20:55:03 UTC**, normal
`--approve-for-me` review and preserved hook trust. Thread
`01a1038b-8aa3-7e43-8851-87d503b22d69`, real coordination identity
`codex-03b22d69-c4a45a7e`, exited zero. Actual `mcp__node_repl__js` import of
`@oai/sky` passed. Native `node:os` reported **win32 / Shadowbane**.
`sky.list_apps()` passed, returning 40 apps and **zero PerfectDark candidates**.
An initial environment probe found `process` unavailable in the REPL; using
the supported `node:os` module produced the observed local identity. This
did not retry an app denial or repeat the app listing.

Evidence is `.claude/pd-initial-integration/20261003-night/local-capability/`:
`inspection.json`, `cli-probe-1/final.json`, `native-tool-results.json`,
owner/exit receipts and bounded 128-KiB-per-stream logs. No owned worker or
child process remained; no heavy resource was taken. There was no game launch,
per-app state/activation/input/capture call, or grant mutation.

This resolves **local runner capability**. **Actual PerfectDark permission is
still unverified**, and no live prompt/request ID was emitted. The bundled
API has no standalone app-permission request method; its target-window actions
require a returned real window. The official [Computer Use approval flow](https://learn.chatgpt.com/docs/computer-use)
prompts for access during a task, separately from shell approval. User consent
is already recorded. Next establish the actual PerfectDark app prompt/access
through that supported flow, then run the unchanged prepared gate in a fresh
coordinated 15-minute window with the unchanged 200-GiB floor/reserves.
No ordinary-client acceptance is inferred from capability discovery.

## 21:48 UTC continuation: admission blocked before launch

The granted runtime window ended at **21:27 UTC**. The worker's first clock
left **466.979 seconds**, below the unchanged 240-second native run,
138-second finalization and 90-second cleanup reserve, plus setup. It refused
before launching a wrapper, game or managed install. Actual app access was
not requested; there were **zero native assertions and captures**. Native
Sky initialization/listing succeeded, but no PerfectDark target existed.
This attempt is unqualified and does not prove runtime or visual acceptance.

Two execution restrictions are preserved separately. The root combined
coordination/Workbench and canonical storage-preflight batch was rejected
at CreateProcess before any component ran: **`rejected: blocked by policy`**.
The returned record supplies no detailed reason, rule or request ID, so it
does not identify a storage-backend failure. The worker's scoped
`Get-CimInstance Win32_Process` query for `PerfectDark.exe`, filtered to the
managed shared/client executable, returned **`Access denied`**. This is a
process-query restriction; no actual app-permission denial occurred here.
Read-only readiness preparation did not retry either denied action through
another route or change configuration, grants, hooks or security settings.

CLI PID 7196 started **21:18:56 UTC**, exited zero **21:21:37 UTC**, and its
owned-process absence was verified **21:29:14 UTC**. Owned queues were empty.
These receipts establish cleanup of that executor and that it launched no
game; they do not prove a fresh successful managed-game process query.
The unchanged canonical ownership helper records actual PID, executable,
start time, parent and command token, then requires fresh verification for
cleanup. Its earlier **22 passing checks** do not prove permissions for this
restricted worker. No ownership record was invented for an unlaunched game.

Readiness is **BLOCKED**. The 20:08:49 storage receipt remains historical;
fresh canonical admission has not passed. Read-only registry inspection
shows two pinned managed copies and an absent shared/client directory,
providing slot capacity but no fresh disk/process admission. The minimum
operator step is resolution of the exact preflight and owned-process-query
restrictions through supported execution approval, or actual canonical
checks and receipts from a coordinated normal operator session. No request
ID is available to quote and no security-setting change is requested.

Private evidence is `preview-current-1/{result,worker-owner,worker-exit,
root-closeout}.json` and `readiness-closeout/{readiness.json,
two-phase-protocol.txt}` under this night's evidence directory. The prepared
protocol initializes a persistent supported executor and completes legitimate
admission **before** requesting a new 15-minute runtime window; its deadline
comes from that new grant. It has not been executed. D-012, both pins,
immutable archives and current binaries remain preserved. V-014 remains
blocked and the overall initial asset/menu scope remains partial.
