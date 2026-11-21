# N-1055 retained build and runtime handoff

Workbench T-BENCHMARKING-008 and prior N-1109. Shadowbane canonical C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike, branch dev. First action was to verify live HEAD, exact frozen files, shared build artifacts, and queue state. Observed dev 1b29c009e5f9de558cbae2aab844fcd64e622761, local origin/dev cbb7bdf066327b64a2d174d12a3df9d41a9bc567, ahead seven. Session chat 6aff5927-5abc-8191-8f0b-5cc654a1002f. Effective worker model and effort unavailable. Parent-reported routing was Sol 6.2 Ultra.

## Compiled N-1055 candidate

Use the retained .claude/session-builds/jenkins-client-n1124/PerfectDark.exe, SHA-256 e947f55f0fb4ec7cc06ca7ab0321b2d5ea7ee24924c8605aa87010fe337efdea. Original native build q_20261120030911032_c24a1e14 exited 0. The artifact was independently checked and then verified in offline turn q_20261120031539319_c017a966. Log lines 2070 and 2655 compile port/src/swarm_test.c and port/fast3d/swarm_gpu.cpp with -DPD_DEV_BUILD=1 and the frozen swarm_gpu.h include. Observed and final compile receipts agree on 89 dirty source/build input fingerprints, including the three N-1055 paths. All six exact source and fixture files match the original applied hashes and frozen patch 3f25a29dfb89c4710b5d57ad3d492f9af9f31bd6efa8df0f774312b4ef3b9a43. The executable contains the paired flag and native log markers.

This is a RelWithDebInfo development build of 1b29c009e plus recorded working-copy inputs. Use the same byte-for-byte swarm-n1137-native-build-source.json in both run packets. It binds the binary, build inputs, flags, frozen paths and read-only Base 12 assets. This build proof removes the need to repeat swarmbase for these exact inputs. Actual provider selection, driver shader compile/link, GPU dispatch and state application, and gameplay await native verification. If the paired recipe records a different Git HEAD, stop and reconcile executable provenance without rewriting the recipe or moving canonical dev.

## Managed preparation and launch gate

The live canonical policy still requires 204 GiB for this smoke path, plus available managed slots. C: was 166.861 GiB at the read, and the registry had 5/5 full slots with two pinned. T-TOOLING-016/N-1128 is a detached candidate and N-1130 records pending trusted integration. Re-evaluate when canonical policy, capacity or the supported preparation route changes. The storage root remains governed by the canonical policy.

The retained runner's -Install and -SourceSeed paths currently call New-SmokeManagedInstall at tools/smoke-verify/run.ps1:2412 and :2415, then launch around :2632. The current AGENTS.md runtime rule requires prepared storage, fixtures and commands before game-runtime starts. Controller-cancel reuse is a separate exact-contract path. The one-shot retained runner needs a supported prepare-then-launch handoff before either frozen fixture can run. Leave game-runtime unreserved while resolving that handoff. The shared base is a read-only input.

Base 12, D:\PD2-wt\shared-base-12, matches this dev ancestry. The published Base 12 .pdarena for base:mp_skedar hashes to 651dc5413fa6acbaddb9426c088c8261e140a1a2e91cf65cb47aee8021faf638. Its nested and loose .pdscenario are byte-identical at 4ec31051d29d87c54cc6d4546cff0813ccbd4063b81d6c574bfbd4e1450251f1. Use the same managed seed, exact executable and public source for both fixtures. Their frozen profile uses classic OpenGL by default, a portable offline launch, and swarm_cpu or swarm_gpu. Confirm the fixture name and actual renderer, seed and installed public source in the receipts.

## Evidence after runtime is admitted

1. Prepare the CPU private copy and all launch inputs outside game-runtime. Coordinate staging or extraction with the corresponding supported queue when required. Pin its managed seed to the checked executable and Base 12 bytes. Observe one complete startup and actual typed provider activation, four live matching actors, the 35s and 50s back-buffer captures, the 60s scripted exit, and verified process teardown. Release runtime promptly.
2. Prepare GPU_FULL from the same CPU recipe seed, with a separate private install and a fresh runnable FIFO turn when applicable. Capture the same windows. Require actual shader init, dispatch, readback and application of all four identities in each window. Both cases are 75-second fixtures. Prepare and analyze evidence outside game-runtime.
3. Retain the complete recipes, shared seed catalog, native logs and BMPs. Bind each recipe ID and definition SHA to its corresponding four-actor fixture, with the identical source manifest, client and source digests. Use the frozen pair checker and inspect all four actual BMPs. Report the first mismatched actor or output application before changing the GPU code.
4. Keep lifecycle, physics, encounter authoring and supported authoritative network changes behind native baseline comparison. This turn verified the retained client and prepared offline evidence handling.

## Offline evidence preparation

A new copy-only assembler checks the retained recipe, source receipt, seed, file entries, capture schedule, and collected artifact bytes against the frozen verifier. It prints a manifest after acceptance or creates manifest.json exclusively with --write. Use only after the evidence is collected in a quiescent run directory. The assembler takes the managed run ID and coordination runtime ID from the operator. It checks consistency and requires separate observation of provider activation, process origin and visuals.

The eight synthetic controls passed in closed test-runner q_20261120031913208_8c49212f. They include a successful synthetic pair, recipe HEAD and retained policy rejection, file size and path binding, a missing population with refreshed log hash, and no overwrite. These controls produced no native gameplay evidence.

After unpacking, reproduce the controls with:

    python -B .\baseline-manifest-prep\test_assemble_skedar_swarm_run.py
    python -B .\baseline-manifest-prep\assemble_skedar_swarm_run.py --help
