# Authored sample install and startup checkpoint

Workbench `T-RELEASE-007`, claim `N-1080`. Chat `6aff6b4a-3d44-8191-be76-7adc8ab92013`, actor `codex-8ab92013-0aadcbb3`, canonical `dev` base `92200bbdca4127a3d6ab864f197738180ed71819`. These are workspace copies. Product integration awaits the exact `T-MODDING-008` ownership handoff and native resource admission.

## Inventory and eligibility

| Package | Version | Native identity | Evidence and disposition |
| --- | --- | --- | --- |
| Blank Room | 0.1.0 | `mod_blank_room:arena_blank_room` | First-party source builder and playable arena under Templates. Approved source bytes pinned in the draft inventory. |
| Model Room | 0.1.0 | `mod_model_room:arena_model_room` | First-party GLB generator and playable arena under Templates. Approved source bytes pinned in the draft inventory. Exact ordinary play remains open. |
| Maian Crystal Pistol | 0.2.0 | `mod_crystal_pistol:crystal_pistol` | Typed weapon and texture matched `N-1056` and `N-1058`. The model vault traces the body to an image-generated mesh and Blender retopology, with Phoenix measurements used as scale references. Confirm concept rights and exact BYOR eligibility with the owner before admitting these bytes. The typed weapon's visible name is Crystal Pistol. |

The maps' authoring folders include scripts, documentation, and a standalone GLB. The package builder selects only the reviewed manifest and typed archives and runs the existing release verifier against those staged bytes. The current allowlist contains the two maps. The Needler, hover recipe, legacy pistol recipe, Grid Test Rooms template, and test-only packages are outside that inventory.

## Production wiring after ownership transfer

1. Use one reviewed authored inventory, one toolchain and one shared staging path at the Windows `pd` build output and clean release distribution assembly. `build-headless.ps1`, the supported GUI and the separately located Dev Window paths ultimately build `pd`. Verify the actual launcher and make the packaging dependency react to added, missing and changed inventory members even when the executable needs no relink. Route explicit `-DevMods` selections to extra development fixtures without duplicating a reviewed package ID. Remove default fixture admission from the ordinary dev path.
2. In `devtools/release.ps1`, exclude `mods/` from generic copying out of prior build or user data. Stage the reviewed packages and shortcut under the new install tree before the existing BYOR and archive gates. Packaged dev and stable use this same assembly path. Confirm identical package membership and byte digests from all three actual entry points. Isolate assembly and use unsigned test artifacts while signing and publication remain out of scope.
3. Evolve the current exact-file, hash-verified pistol admission in `port/src/modmgr.c` to cover the shared inventory. Generate approved IDs, paths and package digests from reviewed inputs with the same build identity. Verify the actual installed origin and digest before admitting any unsaved default. Preserve a missing selector as distinct from intentionally empty selection, retain ordered IDs, and keep the higher-priority profile copy for an explicitly selected same ID. Preserve settings across archive rescans and the transition to an existing Agent's mod choices. A same-ID profile override must never inherit an unsaved bundled default.
4. Write `Open Mods Folder.lnk` beside the client in each install using the shared shortcut generator. Scratch checks cover binary parsing and simulated account resolution. Inspect it through the Windows shell from a disposable account/environment to verify a real click and Explorer target.

## Upgrade preservation

The two updater paths shipped at `8556dc8de` use `CopyFileA(..., FALSE)` during recursive apply. Both evaluate the protected folders for stale removal afterward. A new updater rule cannot prevent an old binary from overwriting a path present in its incoming ZIP.

Pair a no-overwrite apply path for updated clients with a legacy-safe transition. One candidate uses a separate compatibility payload whose paths avoid the install `mods` tree and a current updater that installs the verified sample bytes through an atomic no-overwrite step. Check how both old clients select assets, including jumping directly to a later release. Verify the exact existing protected cleanup behavior and both paths against collisions before shipping. The plain release ZIP applied by an unchanged prior updater has a known overwrite defect. A clean staged ZIP or the N-1066 policy exercise cannot close that gate.

Run the existing-config proof with explicit `[]`, ordered user choices, a valid higher-priority same-ID package, and modified installed bytes. Verify enabled order and actual use through an existing Agent, plus byte preservation after both upgrade paths. Keep install-root sample assets separate from the profile opened by the shortcut.

## Native evidence

For each clean install, inspect the exact title/Agent route, list discovered mods and registered typed components, open the ordinary weapon picker and check the pistol catalog ID after it is eligible, and verify the two authored maps in their playable Templates list. Select and enter each map and equip/fire the selected weapon using the real client. Record the executable, install manifest, launch environment, visible labels, logs and exit. Keep these acceptance runs distinct from the controlled v0.1.105 comparison in `STARTUP-AB.md`.

`N-1063` provides the bounded Windows event/no-process check. `N-1069` records the two startup timings and the 35.72-second difference before discovery. The actual launch host and cause remain unresolved. Resume the exact A/B only with independently evidenced copies of the five package inputs, the exact executable and matched private installs. Generic campaign reports remain routed through `N-1070` through `N-1075`.

## Verified scratch outputs

- `q_20261119214233612_0ee9d8cd` passed the canonical asset-native-source guard and the initial five staging checks. The shortcut test caught a truncated binary header. The generator now declares the HotKey and all three reserved fields explicitly.
- `q_20261119214459169_6d1af557` passed all six staging tests and 31 shortcut checks on Windows. A fresh install tree is published atomically after all packages validate. A local collision is preserved and the receipt leaves its installed digest unset. The shortcut command ran against two synthetic accounts and correctly handled missing folders, literal paths and a test reparse point. The test did not open Explorer.
- `q_20261119214654616_ae09c1cd` froze the two packages and link in `C:/Users/mikeh/AppData/Local/Temp/pd2-authored-stage-8a613465006f4445a813660303953b27`. Windows shell readback resolved the system PowerShell target and preserved the launch arguments. The A/B generator passed Python syntax parsing. No A/B fixtures or client were launched.

| Staged output | SHA-256 |
| --- | --- |
| `mods/bundled/blank_room.pdmod` | `0884f63dfef0410b96204bbf335bacfea3094d69709c4ab96dc484c5c30224fb` |
| `mods/bundled/model_room.pdmod` | `6b08a15ca42b2867c63298cce39340c22870323310de34c14461aa63425e7cb2` |
| `Open Mods Folder.lnk` | `a6d19bba1d5e422d99f9f1bde80fdcf2bbeb98557b96517a8c2b28275b2df3b8` |

## Retained incident-package evidence

N-1063 and N-1064 establish the five discovered IDs and Crystal Pistol v0.2.0. They do not bind any retained copy to the files actually scanned in the incident. The following hashes identify project artifacts that can be used to prepare an explicitly approximate fixture. The original install paths, packaging forms, and byte hashes are still missing.

| ID | Retained artifact | SHA-256 of retained artifact or tree |
| --- | --- | --- |
| `blank_room` | Blank Room v0.1.0, exact staged `.pdmod` from N-1084 | `0884f63dfef0410b96204bbf335bacfea3094d69709c4ab96dc484c5c30224fb` |
| `model_room` | Model Room v0.1.0, exact staged `.pdmod` from N-1084 | `6b08a15ca42b2867c63298cce39340c22870323310de34c14461aa63425e7cb2` |
| `grid_two_rooms` | Grid Test Rooms v0.1.0, frozen four-file loose source tree. The separately staged v0.1.106 private template matches the tree's `.pdarena` bytes. | Loose tree `063b3d347eb90e00ae17d3138e3de15e37bfb0beeaa1a71fe171f4a0b18d32ea`. Arena alone `2c420d72d99bffd8daaa63e3b50b713975a2ed227f06ea897fdee586822cfad8`. |
| `needler` | Two retained Needler v0.4.4 variants from project smoke and dev staging. Their manifests and weapon payloads differ. | Smoke `.pdmod` `55f75fb2bebf52765dab780d72f702cdcd46eeaba65ce6db9bcd821605699314`. Loose dev tree `f41590614e51c0a9aae81d5a08d7ee445509ca1ca8388f3287ccbc5678477712`. |
| `crystal_pistol` | Two exact Maian Crystal Pistol v0.2.0 package artifacts with identical weapon and texture bytes, different `mod.json` content. | Retained smoke `.pdmod` `18b2f5dc7b9a9c83887fcc8e024572d5f192d3153581e2f9457ce88e8635db7b`. Release rebuild `16fccd62e9fa7b7a0f8fc7973d68c09d21f81a765d0b925945c49b784d1132c7`. |

The pistol release package was rebuilt in memory with the existing writer and matches N-0873's release staging digest exactly. Its weapon (`994ac0fed308c7b4ee221ffd44488cc993379ecd113bf68a8d6201a55d9eeb2a`) and texture (`67683f99de2539913d226fe014b474019f9e07716fdbd6212c935ef5aa6fcfb9`) also match the retained smoke blob and N-1056. N-1058's typed and BYOR checks cover those bytes. Neither typed archive carries a provenance or license entry. The source vault records the Tripo body generation from the V2 concept sheet and Blender retopology. The concept PNG (SHA-256 `6ddb32a60cb595e5db97be04b2f33f1ebf702754561b130882a3bb315ecde56a`) has no embedded text or Content Credentials. The vault and targeted Workbench receipts establish no concept-origin or redistribution grant. The vault cites measured Phoenix dimensions, and the exact weapon descriptor still uses the documented Phoenix `posx 9.5` and `posz -23`. The project's `pd-game-model` skill bars extracted numbers in shareable models. Release admission remains on hold for an exact-hash provenance and BYOR disposition.

The shortcut action preflight found the active Core game-runtime reservation `q_20261119195806671_3e9c0162` and stopped before taking a test slot or launching. The next shortcut test uses the target and arguments read from the frozen link, sets `LOCALAPPDATA` only in that child process, and checks that the initially absent scratch path is created and appears in Explorer. This would verify the actual shortcut command separately from an Explorer double-click.

N-1086 requests permission for one disjoint insertion immediately before `devtools/release.ps1` final typed-archive validation. The request covers calling the shortcut generator for the fresh distribution root. The existing owner retains the release script. For upgrade proof, exercise each actual released updater against a disposable install with a modified `mods/bundled/blank_room.pdmod` and compare its SHA-256 after applying the candidate payload. N-1066 and N-1084 cover policy and staging respectively.

## Agent and discovery acceptance follow-up

`AGENT-REGRESSIONS.md` defines ordinary dev and stable create/select, reselect after restart, explicit disable, and absent versus saved `[]` cases. It also covers the distinct choices of an existing Agent and arbitrary user mods. B-1083 establishes why startup registration alone is insufficient. `prefsApplyMods` can change the catalog as the Agent's `enabled_mods` is applied, so capture UI and catalog availability after activation commits. Product wiring for the shared approved inventory remains pending. No product edit or native test ran.

Both the released client and audited dev scan before applying saved settings and call the load path for audio-only entries even when disabled. The retained inventory audit found no `audio.ini` in the seven inspected forms. `STARTUP-AB.md` records the prepared five-package approximate fixture, the required identical starting binary/cache and separate discovery and downstream timing. Neither cohort has launched.
