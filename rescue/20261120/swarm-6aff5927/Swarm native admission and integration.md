# Swarm native admission and integration handoff

Workbench T-BENCHMARKING-010 continues N-1055 and N-1062 under chat 6aff5927-5abc-8191-8f0b-5cc654a1002f. Preparation base is canonical dev 92200bbdca4127a3d6ab864f197738180ed71819. The exact N-1055 patch SHA-256 is 3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43. The GPU snapshot, encounter scheduler, and acceptance sources from N-1062 remain separate from it.

## Admit the first native window

At the recheck, C: had approximately 187.1 GiB free. The current project policy requires 206 GiB before a build and ordinarily 204 GiB before managed smoke growth. The tests and probe targets use the same build preflight. The managed canonical smoke registry also had five of five full slots occupied. The retained runner does not evict installs. Confirm headroom and capacity for two retained private copies, or use a separately approved policy-compliant storage route after reconciling exact ownership. D-038 is open and scoped to Grid. No release install, peer test install, or protected receipt was modified.

Read the current game-runtime FIFO. BGFX held q_20261119191534312_82e5bc62 on this check with CORE and catalog consent queued. Reserve native build and runtime turns when their prerequisites are met. When ready, reconcile HEAD, local origin/dev, index, exact path claims, and live Core or Grid work. Reserve the existing serialized integration turn for the six N-1055 paths. Check patch SHA and git apply --check --whitespace=error before touching canonical source. Only apply N-1055 to this candidate. Freeze the resulting six paths and preserve unrelated dirty inputs. Bind the actual complete build input identity, including unrelated source that enters the build, and retain the patch and exact client. Identify any later baseline probe that needs to be rebased. A dev advance between the build and smoke recipes can invalidate the source-identity gate.

Take the build FIFO and use the queued project wrapper for the isolated client. Release its turn after actual closeout.

    .\devtools\build-session.ps1 -Session swarmbase -Target client

The candidate executable is .claude/session-builds/swarmbase/PerfectDark.exe. Pin the executable SHA-256. Take the game-runtime FIFO only once the exact binary, compatible typed template, and two private copy admissions are ready. One turn can contain the two short sequential native smokes. Account for template copy and receipt closeout, the 75-second in-game fixtures, and the outer runner watchdog. Use CPU first, then the same managed source seed for GPU_FULL.

    .\tools\smoke-verify\run.ps1 -Test skedar_swarm_baseline_cpu -Install $typedBase -SourceBinary $client -PerTestInstall -RetainHousekeeping
    .\tools\smoke-verify\run.ps1 -Test skedar_swarm_baseline_gpu -SourceSeed $cpuSeed -PerTestInstall -RetainHousekeeping

Read $cpuSeed from the CPU recipe.json.gz seed field. The second command uses no -Install, -SourceBinary, -SourceRom or -Build. The Base 9 candidate has the required named ROM in its source-template root and the two public typed files. The loose and nested Skedar scenarios were observed byte-identical at SHA-256 a4494a3e3196326cc61e8f88d8a0b773466423edcab72ef470532ce4b48cfb56. The outer arena hashed fc82014cd59947de8511e62a183c7414eb55b8bf1ca6e9edf98ea6ee7ad01227. That is a source observation. Pair the template with the verified client/provider route and rule out startup replacement of unqualified bytes before accepting it.

Preserve each distinct recipe, shared seed catalog, exact client and installed typed file bindings, fixture, native log, and two BMPs. The GL screenshots are in-harness events at 35 and 50 script seconds. The top-level runner outcome can have an empty screenshots list even when the managed recipe includes both BMPs. Use the existing supported restore with its separately admitted capacity if a retained private copy is unavailable. Run the unchanged pair verifier against the two manifests. Inspect the actual four native BMPs, surface movement and completed dispatch-to-application chain, and corroborate provider activation from the stage source trace. Commit only the tested in-scope paths under the serialized Git turn. Keep the baseline receipt bound to that exact build. Any discrepancy repair starts afterwards.

## Physical motion and native test linking

The standalone capsule module uses the existing world-space meshtri arrangement. The bounded XZ grid query expands from both spine endpoints across both move endpoints. The sweep finds a true closest pair between spine and triangle, then advances only to the separating contact plane. Native tests cover two-sided wall contact, a grazing edge, real tunnel height, inverted ceiling tangent travel and detach, extended spine grid coverage, initial overlap, sphere and query-budget behavior. An iteration limit returns the last safe fraction without a support witness. A candidate or input failure returns zero travel.

After the baseline qualifies, apply the N-1062 GPU snapshot patch only to its verified C++ preimage. The later lifecycle adapter gates absolute-position work to one pending step, carries stage epoch and prop/character generations with it, drops old generations before state apply, and reserves late fences until completion or stage invalidation. Do not feed dying or empty rows into steering or attacks. Clear identities and private AI reservations before stage memory or props are reclaimed. The material stage seam is lvStop before character/prop teardown. Inspect the exact hunk with the current owner before integrating; live lv.c and CMakeLists.txt already contain other hunks. Preserve the pre-stage launch arm.

Use the capsule kernel with g_WorldMesh and stage-owned stamp scratch at the CPU commit of the GPU proposal or the same Swarm CPU movement boundary. Gather compatible dynamic props in the swept room span through the existing movement-solid and transform helpers. Transform their triangles using current authoritative prop transforms and compare contact fractions. Retain room, triangle, prop generation, and support normal. Owning motion must advance room boxes and position together. Probe separately in negative local up during tangent travel, where no blocking hit is expected. Keep the prior support frame while the rotated capsule orientation is unverified. Validate wall-to-ceiling and detach through native solver evidence with site-specific shape clearance. The fixed-axis kernel does not certify a rotational sweep, dynamic-door wiring, character pose handoff, or runtime budgets.

Add the following only after claiming the exact test-registration hunk in the explicit CMake SRC_TESTS list. Keep the disjoint dirty entries.

    tests/test_skedar_swarm_encounter.cpp
    port/src/skedar_swarm_encounter.c
    tests/test_swarm_capsule_sweep.cpp
    src/lib/swarm_capsule_sweep.c

The live all target builds the client and updater and does not build pd-tests. This corrects the older N-1062 runbook command. For well-scoped client plus game-unit verification, take the build FIFO and run both explicit targets. Take the test FIFO only after the exact tests binary exists. Run no implicit build under a test slot.

    .\devtools\build-session.ps1 -Session skedar-encounter-candidate -Target client
    .\devtools\build-session.ps1 -Session skedar-encounter-candidate -Target tests
    .\devtools\run-pd-tests.ps1 -Session skedar-encounter-candidate -NoBuild -Selector '[swarm][encounter]' -CatchArgs @('-w', 'NoTests')
    .\devtools\run-pd-tests.ps1 -Session skedar-encounter-candidate -NoBuild -Selector '[swarm][sweep]' -CatchArgs @('-w', 'NoTests')

## Stage and supported online handoff

Preserve N-1062’s authored encounter order. Resolve typed source from the catalog-backed stage source seam and retain a fresh stage-owned copy. The physical blocker commits before each objective closure. The same stage epoch drives admissions, exact prop instances, attack-slot expiry and Giant outcome. Configure the existing scenario/provider to keep the editable data as the gameplay source. Use the supported original-level builder or editor and Save As/reopen first, then own a specific editor hunk only if that is required. The unrelated renderer, network, ordinary movement and release paths stay in their established scopes. When writing the actual asset source, run the native source guard and prove the reopened source changes the encounter behavior through the normal provider.

After ordinary local qualification, use supported listen-host and guest match fixtures with the native receiver only. Bind host publications and guest observations to the same stage, actor generations, pose/room/scale and monotonic sequence. Prove old or duplicate valid state is ignored, staging does not alter the current world, death/rejoin gets a converged baseline, and same-map restart starts a new epoch without borrowing old actor state. Retain both logs and native metadata through normal managed recipes. The offline checker reads recorded observations only. Use the managed closeout to bound the last committed-state age as well as the gaps between commits. The offline checker bounds successive commits. Actual localhost and approved WAN runs qualify their respective environments.

After those gates, repeat controlled paired four-row and multi-workgroup GPU runs with a fixed input trace and inspected captures. Measure client frame percentiles, compute/readback/pose and collision timing, snapshot age, drops/restarts, and bandwidth under a declared hardware/configuration budget. No historical ladder or synthetic replay earns real gameplay or performance acceptance.

## T-BENCHMARKING-010 results

- Branch dev and local origin/dev agree at 92200bbdca4127a3d6ab864f197738180ed71819. The scoped source paths and Git index were clean at the final check. The native baseline and both new patches passed git apply --check --whitespace=error. There is no Swarm implementation commit or active Swarm native queue item.
- The standalone sweep patch is SHA-256 15dbb79e00c18dad3887df52c73e592730d2a80924c15a8f0e58b7b9cb9b7a26. The source was independently reviewed. The ten Catch2 cases need the native test build and were neither compiled nor executed. Native attachment, rotational support transitions, pose, dynamic props and game linkage remain open.
- The online observation patch is SHA-256 fece816c829bf82c392700ed331a4294e417b586e166151592125cd5b3fef7bf. The final test-runner q_20261119214319952_d3034d26 closed successfully after 37/37 controls and a successful CLI replay. The synthetic receipt has nine matching actor/pose commits, two attachments and stage epochs, and valid duplicate, stale sequence, stale attachment and stale epoch rejection witnesses. It reports native_gameplay_verified=false. Initial fixture error q_20261119213840851_975d6cb6 and the corrected 31-case run q_20261119213946320_f55831d4 remain in the packet. No real session ran.
- The rolling brief records the actual lane chronology, current owned source files, and remaining native acceptance gates.

Replay the exact final offline controls after unpacking.

    python -B .\native-prep-online\test_verify_skedar_swarm_online.py -v
    python -B .\native-prep-online\verify_skedar_swarm_online.py .\native-prep-online\synthetic-online-witness-final.json

The configured worker model and effort were unavailable from this session. Parent-reported routing was Sol 6.2 Ultra.
