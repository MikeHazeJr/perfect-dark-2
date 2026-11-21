# Skedar Swarm native build checkpoint

T-BENCHMARKING-008, chat 6aff5927-5abc-8191-8f0b-5cc654a1002f on Shadowbane. The first action was to recheck canonical dev, exact source claims, the index, and existing queues under Mike's build-only reserve change. Canonical checkout is C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike. Model and effort were unavailable from the worker session. Parent-reported routing was Sol 6.2 Ultra.

## Actual build attempt

- N-1055 patch SHA-256 3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43. The six exact paths were applied unstaged under q_20261119223859063_22cd5c37. Forward, reverse and whitespace checks passed and the Git turn closed. No later Swarm patch was applied.
- The standard queued .\devtools\build-session.ps1 -Session swarmbase -Target client ran under q_20261119224124167_646b476c. The updated storage preflight passed and Ninja started. The wrapper exited 1 and closed the build turn. No PerfectDark.exe or Swarm object was produced. No game turn was acquired.
- First failure is src/game/bondgun.c:4000 and :4043. The current HEAD caller expects wgV2RuntimeNoiseAdmit and wgV2RuntimeNoiseConsumed. Both declarations and definitions exist in build-era HEAD. The pre-existing unstaged changes in port/include/weapon_graph_v2_runtime.h and port/src/weapon_graph_v2_runtime.cpp remove those functions and introduce wgV2RuntimeNoiseBegin and wgV2RuntimeNoiseComplete. The caller is clean and still uses the older interface. The actual dirty writer has not been verified. Preserve both files and resolve through their owner or a verified handoff.
- Build-era dev HEAD 71eb950f14b4ffef1846b263315b00a7fd6df37c and local origin/dev a17511135e729eae6f0f7d4e787e96c607dcd2fe. Scope digests cover 3,611 visible files with no byte changes between captures, plus three unchanged indexed submodule OIDs. The policy owner's four staged entries drifted during the build, producing different manifest digests. Later dev advanced to f3aa5c3e1015cb7f47eeabbb0c7319cee824c76c for the isolated build-floor commit. The exact frozen patch still passed reverse check at that refresh.
- Compiler output SHA-256 3549e7824219d3dd53ce9778f6c2036430dc2ae2a27c0ef8afb27b5f293dce5c. Full output remains in the existing isolated build directory at .claude/session-builds/swarmbase/_build-headless-20261119-174243-compile-client.out.log. Reduced diagnostic and queue closeout accompany this handoff.

## Resume conditions

1. Verify the real owner has either restored source/API agreement or handed off the exact overlapping files. Preserve peer changes. Recheck canonical dev, the frozen reverse application, and admission. Take a new build FIFO turn and rerun .\devtools\build-session.ps1 -Session swarmbase -Target client. Reuse the owned isolated build directory and bind the successful executable SHA-256 to a new source receipt.
2. Managed gameplay is a separate gate. The unchanged canonical smoke requirement is 204 GiB and the five full-install slots were occupied, including two pinned installs. Frontend held game-runtime q_20261119201136178_43e102ec, with CORE q_20261119221911141_18e20974 and frontend q_20261119224149816_790db5bb queued. Recheck current headroom, slot admission for two retained private copies and the FIFO. D-040 is frontend-specific and D-038 is Grid-specific. There was no identified Swarm authorization to move the managed store.
3. Base 10 is the published source template for current dev. Its base:mp_skedar arena is SHA-256 651dc5413fa6acbaddb9426c088c8261e140a1a2e91cf65cb47aee8021faf638. The loose and arena-bundled scenario agree at 4ec31051d29d87c54cc6d4546cff0813ccbd4063b81d6c574bfbd4e1450251f1. These are read-only source observations. Use the template only through admitted private smoke copies.
4. With exact built $client and runtime admission, run the following sequentially. Read $cpuSeed from the successful CPU recipe. Preserve independently bound logs and receipts, inspect two captures per backend and validate actual provider activation and GPU readback/application with the frozen pair verifier.

    .\tools\smoke-verify\run.ps1 -Test skedar_swarm_baseline_cpu -Install 'D:\PD2-wt\shared-base-10' -SourceBinary $client -PerTestInstall -RetainHousekeeping
    .\tools\smoke-verify\run.ps1 -Test skedar_swarm_baseline_gpu -SourceSeed $cpuSeed -PerTestInstall -RetainHousekeeping

No Swarm commit, publication, game launch, graphics capture, or protected install change occurred. All later encounter and snapshot patches remain held behind the matched native baseline.
