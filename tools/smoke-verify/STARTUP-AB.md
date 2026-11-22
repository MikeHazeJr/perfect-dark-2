# v0.1.105 disabled-mod startup comparison

Workbench V-015. Mike, 2026-11-21: run this A/B "as one finalization run". The Authored Mods
session (claude-pd-authored-mods-20261121) landed the generator
(`tools/smoke-verify/prepare_authored_mod_startup_ab.py`, tests in
`tools/tests/test_prepare_authored_mod_startup_ab.py`) and prepared every input outside the
checkout. Integration (Session A) runs it once, inside its finalization batch. Nothing here has
launched a client.

## Finalization run (one batch)

Inputs, in `C:\Users\mikeh\Perfect-Dark-2\pd2-startup-ab\` (on C:, the checkout's drive: the
runner resolves fixture sources relative to the checkout, so the folder must stay where it is):

| Path | What | Pin |
| --- | --- | --- |
| `published-v0.1.105\PerfectDark-v0.1.105-win64.zip` | the published release, signature verified (see "Verified published input") | SHA-256 `3b941a46fee98038f9e586a5fc194fbbe7a06784cbdc2ff655117bafa0137052` |
| `template-cold\` | that zip extracted as published, plus the project ROM seed as `pd.ntsc-final.z64` (from `D:\PD2-wt\shared-base-10`) | `PerfectDark.exe` `4efa7f4f87d19de65364ae60038763d7d444bf58f6ad4282204f357b89ac5114`, ROM `b4173ee90ba6e3514b17d7f340c6c248e2a4c429fdc3c671dd82bbf5afcbb152`; every file in `template-cold.manifest.json` |
| `inputs\` | the five retained packages and `approximate-inventory.json` naming them | the inventory pins every package hash |
| `pair\` | the two smoke definitions (`tests\`), their payload and `receipt.json` | `receipt.json` SHA-256 `e9b12b45b42871bb33d0bef28664bebff5e85ad5084409fdc2746707d5b68672` |

Steps, inside one admitted game-runtime turn:

1. Check the pins: `PerfectDark.exe` in `template-cold\` against `receipt.json`'s
   `expected_client_sha256`, and every file against `template-cold.manifest.json`.
2. Warm template (the doc below asks for the warm pair first): copy `template-cold\` to a
   private warm template, launch that copy once with no mods (`--portable`, its own save
   folder) until the title is up, exit normally, and pin its data and cache files. Both cohorts
   then start from those same warm bytes. If warming is not possible in the turn, run the cold
   pair and say so in the receipt.
3. The pair, from the warm template (or `template-cold\`):

   ```powershell
   $repo = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
   $pair = 'C:\Users\mikeh\Perfect-Dark-2\pd2-startup-ab\pair'
   $template = 'C:\Users\mikeh\Perfect-Dark-2\pd2-startup-ab\template-cold'   # or the warm copy
   & (Join-Path $repo 'tools\smoke-verify\run.ps1') -Install $template -PerTestInstall `
       -TestsDir (Join-Path $pair 'tests') -Test startup_empty_mods,startup_detected_disabled_mods
   ```

4. Record, per cohort: the staged executable hash, launch flags, runner result and exit code,
   and from each client log the times of `modmgr: initializing`, the first `modmgr: scanning`,
   `modmgr: scan complete` and `modmgr: initialized`, plus the visible title in the retained
   frames. The detected cohort must find exactly the five ids with 0 enabled and 0 components;
   the empty cohort finds none. First audio and first input response are separate witnesses
   (see "Run and capture once admitted").
5. Write the verdict and artifacts to V-015. This pair is approximate (retained project copies,
   not the incident's original bytes, paths or forms): it can show whether five detected but
   disabled packages change v0.1.105 startup, not reproduce the incident exactly.

Storage: the template is 152,596,729 bytes and each private run copy about as much again before
the game writes its data. Which smoke store the runner uses is the canonical checkout's setting
(Integration's).

## Background (Codex handoff, 2026-11-19)

The rest of this document is the Codex handoff that designed the comparison. Its paths under
`%TEMP%` are where the inputs were first prepared; the bundle above is the durable copy of the
same bytes (re-verified on 2026-11-21).

Workbench N-1063 records the already collected Windows event diagnosis. The first launch at 15:51:14 EST displayed five discovered mods, zero enabled mods, and zero registered mod components. Its last log marker at 02:35.44 was the completed second animation scan. The bounded Shadowbane Application/WER/Hang query from 15:51:14 through 15:56:59 found no attributable record, live process, WER report, or dump. The actual launch host and cause remain unresolved.

Workbench N-1069 records the second launch at 16:18:08. With no user mods it reached Carrington Institute and Defection. It began mod discovery at 00:02.55, 35.72 seconds before the first run's 00:38.27. The controlled pair below isolates five detected-but-disabled packages with matched inputs.

## Prepare the pair

Use the approved v0.1.105 client from commit `8556dc8de559ab9a1633af04476257d979f6c68b`. Its `PerfectDark.exe` SHA-256 is `4efa7f4f87d19de65364ae60038763d7d444bf58f6ad4282204f357b89ac5114`. Use a frozen read-only template with that executable, matching dependencies and authorized ROM inputs. A warm template should carry a single pinned set of populated data and cache bytes for both private copies.

Supply a reviewed inventory JSON array with one entry for each of `blank_room`, `crystal_pistol`, `grid_two_rooms`, `model_room`, and `needler`. Use the retained artifact identities in `INTEGRATION.md` for an approximate pair. N-1064 establishes the observed IDs and Crystal Pistol v0.2.0. Any exact incident classification requires original versions, names, paths, forms, and hashes for every package. The two additional category names in the original report fall outside the five-package variable. Leave them absent in both fixtures and record that distinction when comparing this run with the incident.

Each entry has these fields:

| Field | Required value |
| --- | --- |
| `id`, `name`, `version` | Exact root `mod.json` strings for the observed package |
| `source` | Absolute path to the explicitly approved preserved file or folder |
| `installed_path` | Relative path inside the user mods folder, with slash separators and at most one category. Pin the original path for an exact comparison and document the chosen path for an approximate comparison. |
| `form` | `archive` for a .pdmod or .zip file, or `loose` for the folder |
| `sha256` | Independently pinned SHA-256 under the scheme below |
| `evidence` | Evidence reference identifying the reviewed retained artifact, and the original path and identity for an exact comparison |

For archives, pin SHA-256 over the original archive bytes. For loose folders, enumerate all descendants using slash-separated paths relative to the mod folder. Represent each directory as `[path,"directory"]` and each ordinary file as `[path,"file",lowercase_sha256]`. Sort entries lexicographically by path and serialize the resulting array as compact UTF-8 JSON with Unicode preserved and no trailing newline. Pin SHA-256 over those serialized bytes. This includes empty directory names in the digest. Use original hashes where available and keep any evidence establishing that the supplied packages were present at the incident. Same-version repository copies and newly packed equivalents cannot establish that identity.

The generator uses Windows with Python 3.9 or later. Point it at a fresh scratch directory under a caller-owned parent on the same drive as the repo. The runner resolves fixture sources relative to the checkout, so moving the scratch directory after preparation invalidates its definitions.

```powershell
py -3 $generator --repo $repo --inventory $reviewedInventory --inventory-kind approximate --output $scratchPair
```

Use the result only when preparation succeeds and `receipt.json` is present. The generator copies only the described package evidence, verifies the staged bytes, and records the classification, input hashes, package forms and paths, both fixture hashes, and any `audio.ini` member paths. Both the published and audited client discover and classify files before reading the saved list. Loose `audio.ini`-only discoveries have a separate load path regardless of the enabled flag. The disabled-only fixture refuses root `audio.ini` candidates. Investigate any such original input in a separate audio-aware comparison. `--inventory-kind incident-exact` is only for independently evidenced original bytes, forms, and paths for all five packages.

## Run and capture once admitted

Use an admitted game-runtime and applicable test-runner turn. Verify the frozen template executable SHA-256 before both launches. Use the same template and supported smoke runner for both tests. Stage two private copies from the same template, never use the first launch's resulting cache for the second. Pin a starting manifest of the relevant cache and extracted data paths and SHA-256 values. Verify the same manifest in both private installs before execution using the retained install seed/recipe or an explicit prelaunch readback. Private staging gives each challenge the same install-relative profile and its own game files.

```powershell
$runner = Join-Path $repo 'tools/smoke-verify/run.ps1'
$expectedClient = (Get-Content -Raw -LiteralPath (Join-Path $scratchPair 'receipt.json') |
    ConvertFrom-Json).expected_client_sha256
$actualClient = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $approvedTemplate 'PerfectDark.exe')).Hash
if ($actualClient -ne $expectedClient) { throw 'The template executable differs from v0.1.105.' }
& $runner -Install $approvedTemplate -PerTestInstall -TestsDir (Join-Path $scratchPair 'tests') `
    -Test startup_empty_mods,startup_detected_disabled_mods
```

Both definitions remove inherited mods and the fixture profile inside their private staged install. They use the same explicit `[]` selection, local save/home flags and migration sentinel, which freezes the existing post-migration discovery path. The sound pipeline remains enabled. Both use ordinary install roots without `--moddir`. The five preserved packages enter only the detected-disabled definition. The runner receives just the two definitions under `tests/`. Package payloads and the receipt are siblings outside that discovery directory.

Record the staged executable hash, starting cache manifest, launch flags, runner result and exit code. For each log use `modmgr: initializing` to mark entry, the first `modmgr: scanning` line to mark the first reported root scan, and `modmgr: scan complete` to mark the end of discovery. Report elapsed time before mod-manager entry, discovery time, and the separate interval from scan completion through saved-list load and `modmgr: initialized`. Report additional loader timing and actual title observations separately. The empty cohort must discover zero and the other must discover the five pinned IDs. Both must have zero enabled gameplay packages, zero registered mod components, and no audio-only parsing, mount, or mod load. Retain both native client logs and screenshots. The exact release harness supports `title_sequence_ready` and scripted exit. Inspect the retained frames for the visible title.

Record the first audible output by direct observation or synchronized audio capture and the first responsive input by observing a normal input and its visible menu response. Keep those witnesses separate from the automated assertions. The fixture does not inject input. If a separate ordinary input observation is needed, obtain a matching authorized pair and record input after initial title and audio timing, with the same operator action in both. For an early exit, save the process exit status and active log before any rerun. Retain runner output so its exit-code override line, when present, preserves both the OS and harness codes. Check the actual launch host's bounded Windows Application/WER/Hang interval and preserve any attributable result.

Try the warm pair first. If the warm result leaves boot behavior unresolved, use another admitted pair with matching cold templates and isolated state. Keep the migration condition matched unless migration is the next variable being investigated. A pass bounds behavior to the tested client, forms, inputs and environment. First-launch availability of the shipped authored samples is a separate test using the candidate local dev, packaged dev and stable installs with no saved selection.

The fixture generator and witnesses were compared against the runner and released source. Offline fixture cases and the retained artifact audio audit do not execute the Windows runner or client. New Agent/default and explicit-disable regressions are in `AGENT-REGRESSIONS.md`.

## Prepared offline pair

`q_20261119220719699_f1f60b57` passed four new fixture tests. A separate read-only check found no `audio.ini` in any of the seven retained forms and trees for the five IDs, including their nested typed archives. This rules out that manifest in the reconstructed inputs only.

`q_20261119220900449_38dae881` prepared one pinned **approximate** pair under `C:/Users/mikeh/AppData/Local/Temp/pd2-startup-ab-approximate-1daaa7ff150c4848bd71b20498f6eb5b/pair`. Its `receipt.json` SHA-256 is `76defb75a9316ff352b92f837d3dfaa4aca5a59e3e23fb3fa96581aa5ab83f3d`. It uses the managed smoke archive for Needler, the retained smoke archive for Crystal Pistol, loose Grid Test Rooms, and the two staged map packages. The generator found no `audio.ini` member paths in the selected inputs. The matches identify retained artifacts. The incident's original bytes, packaging forms and locations remain unverified. The exact release executable and a matched starting cache still require verification on the staged installs before any launch.

## Verified published input

`q_20261119224220320_b80b38cf` obtained the published archive and signature under `C:/Users/mikeh/AppData/Local/Temp/pd2-published-v0.1.105-68d05736ce374e2aa3eac993edf34a86`. The ZIP matched the published SHA-256 `3b941a46fee98038f9e586a5fc194fbbe7a06784cbdc2ff655117bafa0137052`. Its Ed25519 signature verified against the public key at `8556dc8de` over the binary ZIP digest and `v0.1.105` tag. The inner `PerfectDark.exe` matched the pinned hash above. The archive contains no ROM member. It was inspected without extracting or running an install. [Published release assets](https://github.com/MikeHazeJr/perfect-dark-2/releases/expanded_assets/v0.1.105)

The successful release log supplied by Mike pins the ROM as SHA-256 `b4173ee90ba6e3514b17d7f340c6c248e2a4c429fdc3c671dd82bbf5afcbb152`. The historical `Downloads/Perfect Dark 2.0` file and three checked project fixtures match that SHA-256. Their MD5 is `7f4171b0c8d17815be37913f535e4e93`, which identifies NTSC v1.0 despite the filename. The tagged README's NTSC v1.1 MD5 is `e03b088b6ac9e0080440efed07c1e40f`. The tagged source describes v1.1 as the intended input and notes that its hash list is advisory. For the approximate comparison, pin the exact bytes observed in the successful release log and record the revision. The ROM match leaves the original mod packages and starting cache unverified.

The input recipe uses the verified public ZIP and the ordinary read-only project seed `D:/PD2-wt/shared-base-10/pd.ntsc-final.z64`. Once the managed install preflight and game-runtime turn permit it, prepare one private template from both and verify its executable, ROM, and starting data/cache manifest before duplicating it for the two cohorts. Use a matched empty cache if a suitable warm template is unavailable. The official non-build preflight refused on C: with 181.74 GiB free and 204 GiB required. No ROM was copied, no template was staged, and no client launched.

## Managed reservation reconciliation

Read-only review on November 20 found two full rows in `D:/PD2-wt/pd2-smoke-mods` and two in `C:/Users/mikeh/Perfect-Dark-2/pd2-smoke/mods`. The old default store under `D:/PD2-wt/mods/.claude` has compacted history. The configured Mods stores belong to the paused MODS and Grid work. Both use a two-install limit. The table uses the unique suffix of each reservation ID.

| Store and reservation | Recorded run owner and PID | Receipt and disposition |
| --- | --- | --- |
| D: weather `a8b8dcac` | `codex-b8057579-d2768fbc`, 52620 | Active. Its screenshot is outside seed `918aa916...` and there is no completion recipe. |
| D: reconnect `a7957d92` | `codex-b8057579-d2768fbc`, 50788 | Active. Its recipe lists 409 changed paths and a failed outcome. It also holds a generated aggregate log. |
| C: vehicle boost `faf71792` | `claude-week-pd-mods`, 22464 | Active. The staged mod selection and recipe files are outside seed `da6d009b...`. There is no completion recipe. |
| C: required character `296efc7a` | `codex-b8057579-d2768fbc`, 70056 | Pinned `retention_blocked`. It has a failed run receipt and a recipe listing 416 changed paths. |

The four PIDs were absent and the game-runtime queue was empty. The supported interrupted closeout requires unchanged seed bytes. It leaves a completed copy in the full-install count. Read-only reuse requires completed unpinned evidence and an exact binary and seed. The reuse runner also rejects the profile and mod fixture changes required by this pair. Every listed seed has a different client SHA-256 from v0.1.105. `N-0832` routes recovery of changed interrupted work to `T-TOOLING-014`, which remains blocked. No reservation, install, or queue was changed. The A/B needs capacity for a new private template and its managed runs through this lane's existing storage admission.

## Fresh isolated storage route review

The managed runner has one checkout-wide location mechanism. `tools/smoke-verify/README.md` documents the untracked `.claude/smoke-storage-location.json`. Both `storage.py` and `Storage-Harness.ps1` resolve it from the runner's project root. `-Install` reads a source template and makes a managed private copy. `-SourceSeed` reads a seed in the selected store. `-StoragePolicy` supplies budgets. None selects a store for just one invocation. The canonical location file is absent. Choosing another store through this supported mechanism would change the effective destination for every run using the canonical checkout and requires explicit scope and coordination.

The smallest supported location change is one checkout-scoped configuration file with `{"schema":1,"root":"D:/PD2-wt/pd2-smoke-authored-mods"}` and no `policy` overrides. That target is absent below an ordinary NTFS directory and D: had 1,258.24 GiB free. Writing the file and using the store require approval for a dedicated canonical smoke window. This preserves the existing 204 GiB run admission requirement, five full-copy slots, 8 GiB full-copy and archive budgets, and retention policy. It does not require using a protected Mods reservation.

C: had 168.15 GiB free, 35.85 GiB short of existing run admission, and all five canonical full-copy slots were occupied. With the existing C: store and no deletion, this pair requires the free-space floor and capacity for two additional private copies. The signed ZIP has 35 files and 119,042,297 uncompressed bytes, with no shipped cache, data, or mods members. A cold template with the verified 32 MiB project ROM contains 152,596,729 bytes. The two staged arms add 37 and 453,766 bytes respectively. Template plus both arms totals 458,243,990 bytes (437.02 MiB) before runtime-generated data and receipts. These payload measurements leave the normal runtime reserve in place.

## Default storage policy handoff

Mike selected removal of the fixed default smoke/run floor and full-install count. `T-TOOLING-016` notes `N-1127` through `N-1129` hold the four-file candidate and its conditional handoff. The candidate patch and all four file hashes match. The patch applied in check-only mode to clean scoped paths at dev `1b29c009`. Source review and PowerShell serialization retain the separate 8 GiB managed-byte and archive budgets, 2 GiB pending run allocation, 2 GiB growth reserve, explicit configured limits, and existing protected-row logic. Ten focused checks are recorded from the author's source-only copy. The separate full test receipt records 31 of 35 passing.

Canonical read-only status at review had five full rows totaling 1,909,020,912 bytes, no nonterminal reuse reservations, and 1,465,422,906 archive bytes. Adding both expected 2 GiB reservations projects 6,203,988,208 managed bytes, leaving 2,385,946,384 bytes under the unchanged cap. This is a capacity estimate. The canonical policy still has the previous floor and count.

The current session lacks its actual trusted repository lifecycle receipt. The conditional four-file handoff remains available. An admitted native owner needs to claim it, verify the integrated policy with the normal canonical preflight and focused checks, and commit the four paths locally before staging. No checkout location change, source integration, live admission, or client execution occurred in this review.
