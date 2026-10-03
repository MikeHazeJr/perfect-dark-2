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
