# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work lives in `session-log.md`,
> `bugs.md`, `daily-logs/`, or `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Open -- 2026-04-28 (Debug Test Scenarios / Swarm Launch)

**Status: BLACK-SCENE ROOT CAUSE PATCHED / TESTS SKIPPED BY MIKE.** Scope stayed on Settings -> Debug -> Swarm CPU/GPU launch and the catalog load failures visible in `Build/pd-client.log`.

**Done this slice:**
- Diagnosed the black-scene symptom from the log: `base:mp_skedar` was launched through the Grid/Forge direct stage path, so setup load saw `normmplay=0`, selected the SP setup/manifest for an MP arena, nulled invalid intro data, and produced a visible HUD over a black/empty scene.
- Changed Swarm CPU/GPU launch to enter through `matchStart()` with no-limit match settings, preserving MP setup/manifest ownership. Empty Map still uses the Grid/Forge path.
- Registered distinct first-person hand model files from `g_HeadsAndBodies[].handfilenum` as provider-backed `ASSET_MODEL` entries. This covers the repeated `FILE_GCOMBATHANDSLOD` / filenum 1253 bgun catalog miss in the same log.
- Added static `pd-tests` guards for both invariants: swarm scenarios must not call `pdguiForgeStartSessionOn(stagenum)`, and hand model files must populate RomProvider handles.
- Logged B-275.

**Verification:**
- `git diff --check` passed for the touched runtime/test/context files.
- The existing shared `Build/pd-tests.exe` did not contain the new test cases and is stale relative to the source.
- Isolated build session `swarm275` configured, but the wrapper timed out during client compilation. A direct `pd-tests` attempt against the isolated tree also timed out without surfacing compiler output.
- Mike then directed: skip tests this time. No further build or test verification was run. Partial isolated session `swarm275` was cleaned up with `-Remove -Session swarm275 -Force`; `-List` confirmed it was gone.

**Next debug/playtest step:**
1. Manual smoke Settings -> Debug -> Swarm CPU Bots and Swarm GPU Boids. Expected log: `TESTSCEN.LAUNCH ... via matchStart`, `MATCHSETUP: starting match`, and setup load with `normmplay=1`.
2. Confirm the world renders visibly and `pd-client.log` does not spam `CATALOG_CRITICAL: bgun model filenum=1253 failed to load`.
3. Re-run isolated build/tests later when the current build contention clears.

## Open -- 2026-04-28 (Server / Trust / Security)

**Status: CLIENT-HOSTED TRUST + LOW-RISK PROTOCOL HARDENING PATCHED / SOURCE-CHECKED / ISOLATED BUILD TIMED OUT IN CLIENT COMPILE.** Dedicated-server product work remains deferred unless Mike explicitly revives it. Scope is listen-host/client-hosted online shipping only.

**Done this slice:**
- Made mod distribution hashes mandatory on the actual `SVC_DISTRIB_BEGIN` transfer: v46 appends a 32-byte SHA-256 digest of the compressed PDCA archive bytes; clients reject zero/missing digests at BEGIN and verify the accumulated compressed bytes before decompress/extract at END.
- Hardened malformed net string parsing: zero-length wire strings now return a safe empty string instead of a pointer into following payload bytes, and unterminated wire strings are rejected without mutating inbound packet payload.
- Tightened connect-code address validation: current join UIs no longer prefill or advertise raw IP input, decode 4-word/6-word connect codes through `connectCodeDecodeWithPort`, reject trailing garbage, validate stored raw address octets/ports before displaying as codes, and preserve custom listen ports in host lobby codes.
- Started and completed the first stat-integrity slice: remote `CLC_MOVE` weapon-select requests are now server-side inventory-gated before the listen host applies `bgunEquipWeapon`, so a client cannot equip and score with a weapon the host has not observed them owning.
- Added a second stat-integrity gate: `CLC_SETTINGS` team changes now reject team ids outside `MAX_TEAMS` and ignore in-game team switches when the match is not team-enabled, so the raw wire byte is never written directly into `srccl->config->base.team`.
- Hardened `CLC_LOBBY_START` count parsing: when `numSims` is clamped, over-cap bot config records are drained before the embedded manifest is parsed, preventing manifest-boundary skew from stale or hostile bot counts.
- Hardened room settings sync: `CLC_ROOM_SETTINGS_UPDATE` and `CLC_ROOM_PLAYLIST_UPDATE` now rebuild their rebroadcast packet per recipient because `netSend()` resets the source buffer after queueing; settings also use the normal reliable buffer instead of the old 256-byte stack packet.
- Closed the loaded-address validation gap: after `pd.ini` load, invalid internal `Net.Client.LastJoinAddr` values are cleared and invalid `Net.RecentServer.*` entries are compacted out; the modern server list no longer falls back to displaying raw stored addresses when connect-code conversion fails.
- Confirmed updater signing is already implemented: release zips require `.sha256` and `.sig`; the updater verifies Ed25519 over `sha256(zip)||tag` with the embedded public key and runs a self-test at init. No dedicated-server product work needed for this lane.
- Added focused static/source tests for mandatory distribution digest, zero/unterminated strings, strict connect-code parsing, connect-code-only UI invariants, loaded-address scrubbing, inventory-gated weapon-select packets, lobby over-cap bot draining, team sanitization, and per-recipient room rebroadcasts.

**Verification:**
- `git diff --check` passed for the trust/security touched files, including the address-validation follow-up.
- Earlier isolated build wrapper attempt `sec507` did not reach compilation; it was cleaned up and recorded in `context/build.md`.
- This slice used the required isolated build wrapper: `.\devtools\build-session.ps1 -Session sec575 -Target all`. Configure completed in 1s, ccache was disabled after the compiler-launch probe timed out, and client compilation ran until the Codex command timed out at 45 minutes without surfacing a compiler diagnostic.
- Cleanup was targeted to this session: stale lock PID 20428 was gone, `.\devtools\build-session.ps1 -Remove -Session sec575 -Force` removed the build directory and lock, and `-List` confirmed `sec575` was gone while `t573`, `ml53`, and `cat566` remained untouched.

**Next recursive trust/security item:**
1. Re-run isolated verification when current build contention clears, preferably through the targeted test wrapper once that lane verifies cleanly.
2. Continue the low-risk client-hosted audit with any remaining malformed packet length/count fields reachable from current listen-host shipping paths.

## Open -- 2026-04-28 (Modern main menu / social shell)

**Status: MAIN-MENU ENTRY + SOCIAL SHELL OWNERSHIP PATCHED / STATIC VERIFIED / ISOLATED BUILD TIMED OUT IN CLIENT COMPILE.** Continue the controller-first Social shell without violating ImGui-only/menu-pool/input-context ownership.

**Done this slice:**
- Added `MENU_TYPE_SOCIAL_SHELL` for the friends sidebar, full Social menu, chat panel, profile modal, convert-to-mod modal, add-friend modal, and NAT diagnostics.
- `pdgui_friends.cpp` now acquires/releases `g_CtxImGuiMenu` through `menupoolAcquire(MENU_TYPE_SOCIAL_SHELL, ...)` whenever any interactive social surface is open.
- Controller Back (`ACTION_CANCEL_USE`) now closes the top social surface: chat first, then Social menu, then sidebar; profile / add-friend / convert / NAT diagnostics close their own modal/window.
- `pdgui_nat_diagnostics.cpp` now closes from `ACTION_CANCEL_USE`, not just the Close button.
- Friend rows now render as bordered cards with large focused actions instead of inline micro-buttons.
- Chat attachment actions, incoming invites, profile/session public-mod downloads, local public-mod removal, block-list unblock, replay actions, listening-room track actions, settings copy, and add-friend paste are now regular controller-sized ImGui buttons.
- Status-pill clicks now resync `MENU_TYPE_SOCIAL_SHELL` immediately instead of relying on a later render path.
- The first-screen main menu exposes controller-sized `Social` and `Public Mods` entry points through `menugraph` push ops to `MENU_TYPE_SOCIAL_SHELL`; Public Mods opens the Social shell directly on the Public Mods tab.
- Main-menu Back/Escape now defers while any Social shell surface is open, so Back closes chat/Social/sidebar before unwinding the main menu.
- Social/sidebar/chat/NAT windows request focus when appearing, and Social shell local booleans are cleared if `menupoolReleaseAll()` or another force-close path releases the pool slot externally.
- `pdguiNewFrame()` now treats active Social shell surfaces as a reason to start an ImGui frame, so standalone sidebar/chat/menu surfaces are not skipped by the backend gate.
- Static verification: `git diff --check` passed for the touched social/menu-pool/backend/menugraph/context files after the entry/force-close slice.

**Build note:** Mike provided the isolated-build rule. Earlier `s501ui` attempts hit the PowerShell runspace/configure timeout path. This slice used `.\devtools\build-session.ps1 -Session ui568 -Target all`; configure completed, ccache was disabled after the compiler-launch probe timed out, and client compilation ran until the Codex command timed out at 15 minutes without surfacing a compile diagnostic. The stale `ui568` lock recorded PID 6120; that PID was gone, so cleanup used `.\devtools\build-session.ps1 -Remove -Session ui568 -Force`, and `-List` confirmed `ui568` was gone while other sessions remained untouched. Details recorded in `context/build.md`.

**Next UI slice:**
1. Re-run verification in an isolated build after the current parallel builds clear, preferably with a longer window or the known direct isolated-tree Ninja fallback if the wrapper stalls again.
2. Do an in-game controller pass over first-screen Social/Public Mods entry, sidebar, Social tabs, chat, invites, public mods, profile modal, add-friend modal, and NAT diagnostics; tune only row heights/focus order that clip or traverse incorrectly.
3. If build/gamepad verification is clean, continue with the next safe UI code slice: refine public-mod add/import form layout and default focus inside the Social shell without adding raw input shortcuts or legacy menu-stack state.

## Open -- 2026-04-28 (Quality / Testing / Audits)

**Status: TARGETED TEST PIPELINE PATCHED / VERIFY PENDING.** Mike requested scoped `pd-tests` runs so sessions can build and execute only the tests tied to their current invariant, without colliding in shared `Build/`.

**Build infrastructure side quest complete (S504):** concurrent test builds now have `devtools/build-session.ps1`, which routes each session to `.claude/session-builds/<session-id>/` via the canonical headless build script and adds per-session locking plus cleanup commands. Use this instead of shared `Build/` when multiple sessions may build simultaneously.

**Targeted test pipeline slice in progress (S573):** `devtools/build-headless.ps1` and `devtools/build-session.ps1` now accept `-Target tests` for the `pd-tests` CMake target, and `devtools/run-pd-tests.ps1` builds/runs `pd-tests.exe` from `.claude/session-builds/<session-id>/` with a Catch2 selector such as `[catalog][provider][static]`, `[input]`, `[manifest]`, `[save][migration]`, or `[netbuf]`. Documentation updated in `tests/README.md` and `context/designs/testing-framework-2026-04-26.md`. Verification and cleanup are still in progress for session id `t573`.

**Dev Window v2 side quest complete (S570):** the Push button now stages, commits, pushes, and refreshes UI state; warm BUILD/RUN TESTS paths skip configure when the cache/version/Python tool are current, run CMake builds in parallel, and mirror addin data with `robocopy` when available. Verified with the PowerShell parser and `git diff --check` on the dev-window files; full build was not rerun because the Codex desktop build caveat still applies.

**Completed slice 1 -- catalog/provider identity:** production code outside the catalog API should not use generic `catalogIdByRuntime(ASSET_*)` for domains that have typed helpers (`catalogStageIdBy*`, `catalogModelIdByModelnum`, `catalogBodyIdByBodynum`, `catalogHeadIdByHeadnum`, `catalogWeaponIdBy*`, `catalogGameModeIdByScenarioIndex`). Added a source-wide static guard in `tests/test_catalog_provider_static.cpp`. Verification passed: focused `[catalog][identity][static]` test, then `pd`, `pd-server`, `pd-tests`, and full `pd-tests.exe` (269 cases / 12329 assertions).

**Completed slice 2 -- network packet parsing:** guard the v45 spawn-weapon wire fields so `SVC_STAGE_START` and `CLC_LOBBY_START` parse `spawn_weapon_id` first, then `spawnWeaponMode`, then `spawnWeaponNum` where applicable, without shifting the following field. Added focused `tests/test_spawn_weapon_mode.cpp` coverage for SVC tail alignment, CLC handicap alignment, truncated tail failure, and static production field order.

**Completed slice 3 -- manifest malformed-input behavior:** `manifestDeserialize()` now rolls back entries appended by the failed call on parse error, so truncated or malformed manifest packets cannot leave a partial client/server manifest behind. Mirrored the behavior in `tests/manifest_pure.c` and added a malformed COMPONENT-tail rollback test in `tests/test_manifest.cpp`.

**Completed slice 4 -- save migration/version gating:** added a static production guard in `tests/test_save_migration.cpp` that pins the destructive v1->v2 weapon-cull migration behind `if (version < 2)`, after random-filter unpack and before the next saved field.

**Completed slice 5 -- mode lifecycle / rejected start authority:** read-only audit found `netmsgClcLobbyStartRead()` parsed `CLC_LOBBY_START` into global match setup state before checking whether the sender was the room creator / lobby leader. Patch moves server-mode, non-null-client, lobby refresh, room/global leader detection, and non-leader rejection before the first payload read so rejected starts cannot dirty match setup or ready-gate inputs. Added `tests/test_net_lifecycle_static.cpp` and wired it into `pd-tests` to pin the authority-before-payload invariant. Bug logged as B-272. Verification: isolated `qlc566` wrapper configured but stalled in Ninja; direct isolated `pd-tests` target command list linked `pd-tests.exe`, full suite passed 341 cases / 19366 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 6 -- mode lifecycle / malformed match manifest cleanup:** follow-up audit found `netmsgSvcMatchManifestRead()` staged the incoming manifest hash before deserialize and returned on parse failure without clearing that staged hash. Patch clears `g_ClientManifest` again on malformed parse so rejected manifests leave entries and hash empty and cannot move the client into PREPARING. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-273. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 342 cases / 19382 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 7 -- mode lifecycle / rejected Counter-Op anti-client validation:** follow-up audit found `netmsgClcLobbyStartRead()` committed `g_NetGameMode` and `g_NetCounterOpClientId` before validating the requested Counter-Op anti client. Patch keeps the anti-client id local until invalid-id, not-lobby-ready, and wrong-room rejection paths have all passed, then commits the globals together. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-274. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 343 cases / 19407 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 8 -- mode lifecycle / SVC_STAGE_START mode validation:** follow-up audit found `netmsgSvcStageStartRead()` accepted any server-provided mode byte and committed it to `g_NetGameMode` before branching into mission/combat setup writes. Patch rejects malformed/truncated mode reads and out-of-range mode bytes before committing global mode state. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-276. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 344 cases / 19423 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 9 -- mode lifecycle / SVC_LOBBY_STATE validation:** follow-up audit found `netmsgSvcLobbyStateRead()` accepted any server-provided lobby mode/status bytes, committed `g_NetGameMode`, and treated any status `>= 2` as in-game. Patch rejects out-of-range mode and status bytes before lobby/global state writes. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-277. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 345 cases / 19443 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 10 -- mode lifecycle / SVC_STAGE_START early tick-RNG commit:** follow-up audit found `netmsgSvcStageStartRead()` committed `g_NetTick`, RNG seeds/latch, and `g_NetMatchSeed` before validating the stage session and mode byte. Patch stages those fields in locals and commits them only after stage identity and mode validation pass. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-278. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 346 cases / 19482 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Completed slice 11 -- network packet parsing / SVC_STAGE_START source-client guard:** follow-up audit found `netmsgSvcStageStartRead()` logged `srccl` with null-safe formatting but then dereferenced `srccl->state` without rejecting a missing source client first. Patch adds an explicit null-source rejection before any `srccl->state` access or payload read. Added static coverage in `tests/test_net_lifecycle_static.cpp`. Bug logged as B-279. Verification: isolated `qlc566` affected `pd-tests` object relink passed the full suite, 347 cases / 19492 assertions, and the changed `netmsg.c` compiled for both client and server object targets.

**Next follow-up:** after the targeted runner verifies, use it for the next broader quality recursion candidate: audit handler dispatch contracts for other `srccl` assumptions (`CLC_*` server handlers and `SVC_*` client handlers) and decide whether to add shared dispatch-side null/source guards rather than patching dozens of handlers one by one. Keep it as a separate audit slice because it crosses many network message families.

## Open -- 2026-04-28 (Input infrastructure: cutscene transition flush)

**Status: TRANSITIONAL SHIM RETIREMENT IN PROGRESS / MENU-LAYER BRIDGE PATCHED-PENDING-VERIFY.** Narrow slices only. No broad scene-manager expansion.

**Done this session:**
- Added public `actionmapFlushActionSet(const InputAction *actions, s32 action_count)` so transition/layer code can flush declared shared actions without broadening `actionmapFlushGameplayState()`.
- Declared the cutscene layer action set and wired `onCutscenePush` to flush both gameplay-only state and the cutscene action set. Held ACTION_USE / ACTION_MENU_ACCEPT, CANCEL, PAUSE, ACTION_SKIP_CUTSCENE, and legacy cutscene skip actions are cleared at cutscene entry.
- Tightened pd-tests around the invariant: held menu/gameplay accept clears on cutscene transition flush, and flushing one declared action set preserves unrelated actions.
- Logged B-266 for the held shared accept state that survived the earlier gameplay-only flush.
- Mike playtest confirmed B-266: held A through the objective 1 -> objective 2 transition did not skip the objective 2 intro after the 30-frame gate, while a later fresh press still skipped deliberately.
- Logged B-267 for the separate cutscene layer lifecycle leak visible in the same log: the cutscene IMC stayed active under the endscreen and next mission until the next deliberate skip.
- Fixed B-267's narrow lifecycle gap: production startup now initializes the input layer stack and scene manager, `mainEndStage()` fires `SCENE_EVENT_CUTSCENE_END`, and stage load/unload paths fire `SCENE_EVENT_STAGE_READY` / `SCENE_EVENT_STAGE_TEARDOWN`.
- Added `inputLayerHandleDistanceFromTop()` so scene cleanup can unwind through a tracked layer handle without reaching into opaque input-layer internals.
- Hardened tracked scene close: if a cached layer is no longer top, scene aborts from top through that layer and clears any cached handles that were inside the aborted range.
- Added B-267 pd-tests covering skip-to-endstage cleanup before the next mission intro and nested tracked close with a menu layer above cutscene.
- Propagation audit found the initial B-267 fix covered the central solo/local endstage path but not every active lifecycle entry point. Follow-up patch covers network `SVC_CUTSCENE` start/end, disconnect teardown, and central tickmode transitions out of `TICKMODE_CUTSCENE`.
- Added a static pd-test guard that asserts the active lifecycle roots fire scene events, and that stale `src/lib/main.c` is not part of the client build path.
- Migrated cutscene runtime state behind per-player accessors: active/in-progress flags, skip-requested state, cutscene anim id, current anim frame, and total cutscene frame time now live in `struct player.cutscene`.
- Updated migrated gameplay/render/audio/cutscene script call sites to use `playerCurrent*` or `playerAny*` cutscene accessors instead of reading `g_Vars.in_cutscene`, `g_InCutscene`, or the cutscene frame globals directly.
- Left the old globals as compatibility shims only in the sync point, declarations, server stubs, initialization, and the `USINGDEVICE` macro until the tracked shim-retirement step.
- Added static pd-tests that guard migrated gameplay paths against reintroducing direct cutscene global reads.
- Added `chr->cutscene_protect`, toggled from the per-player cutscene state refresh path. Current pre-v46 behavior protects all allocated player chrs while any player is in cutscene.
- Added canonical protection gates: `chrDamage()` ignores protected targets and logs `CUTSCENE.DAMAGE.IGNORED`; `chrCompareTeams(..., COMPARE_ENEMIES)` does not classify protected targets as enemies; `chrHasLosToChr()` and `botIsTargetInvisible()` treat protected targets as invisible.
- Added a static pd-test guard for the protection field, refresh path, damage gate, hostility gate, LOS gate, and bot invisibility gate.
- Completed cutscene network semantics on the existing v46 protocol: `SVC_CUTSCENE` now carries `active` plus `player_mask`, and `CLC_CUTSCENE_SKIP` lets a client request skip without locally ending the cutscene.
- Server-side skip authority now binds the skip request to `srccl->playernum` and ignores untrusted player numbers from the payload.
- Cutscene protection now narrows to the active player mask instead of protecting every player chr while any player is in cutscene.
- Clients send the skip request after the existing 30-frame gate and leave actual cutscene end to the server/host path. AI script skip checks now use any server-validated player skip request.
- Added focused static pd-tests for v46 cutscene network semantics, dispatch coverage, active-mask handling, per-player protection, and client skip request wiring.
- Declared vehicle driver and observer layer action sets with push/pop/abort callbacks that flush their declared action sets at transition boundaries.
- Moved hoverbike mount/dismount ownership through `sceneFire(SCENE_EVENT_VEHICLE_BOARD/_DISMOUNT)` so the vehicle layer owns `g_ImcVehicle` activation while preserving existing bike behavior.
- Wired Forge session/freefly entry and inactive exit through observer scene events while preserving the existing Forge IMC activation paths.
- Wired spectator live/theater entry and stop/shutdown through observer scene events, with source tracking so one observer owner cannot pop another owner's layer.
- Added focused static pd-tests for vehicle and observer layer action sets, callbacks, scene event wiring, Forge observer helpers, spectator observer helpers, and source-guard behavior.
- Added dedicated pure-ImGui menu-pool identities for main-menu Solo, Settings, Modding, Online, and Stats subviews, reusing the existing Grid submenu identity.
- Routed all `s_MenuView` changes in `pdgui_menu_mainmenu.cpp` through `pdguiMainMenuSetView()`, which acquires/releases the subview's menu-pool slot and emits `MENU_GRAPH` diagnostics.
- Added a render-sync guard so an inline main-menu subview reacquires its slot if a bulk menu-pool teardown happened while the renderer retained its local view state.
- Removed the Grid-only transition special case in favor of the common subview helper.
- Added focused static pd-tests for main-menu subview identities, subview mapping, transition helper ownership, and raw `s_MenuView` assignment prevention outside the helper.
- Added `menugraph.h` / `menugraph.c` with priority menu node descriptors, edge lookup, destination-kind names, and a validated dialog-push firing helper.
- Declared graph nodes for main menu, its inline subviews, solo mission, room, solo/MP endscreen, pause variants, social lobby, network, agent select, and warning modal.
- Migrated the main-menu Change Agent and Cheats direct dialog pushes through `menuGraphFirePushDialog()`, with destination type validation against the menu-pool registry before the legacy push executes.
- Extended static pd-tests to guard graph substrate presence, priority-node descriptor coverage, graph push validation, and the first migrated main-menu push sites.
- Added graph helpers for network operations and menu pops, with diagnostics around operation result codes.
- Added `MENU_TYPE_NETWORK_JOINING` and registered `g_NetJoiningDialog` so joining progress is no longer an untyped child dialog.
- Migrated the Network menu's Stop Hosting, pre-host disconnect, Host, host-success pop, Join, Joining dialog push, and Back paths through graph helpers.
- Migrated the main-menu Online subview's direct connect and recent-server connect paths through graph network edges.
- Extended static pd-tests to guard Network menu and main-menu Online graph usage and block direct network/push/pop calls inside those renderers.
- Migrated MP endscreen Disconnect confirmation through the `MENU_TYPE_ENDSCREEN_MP` graph network edge before returning to the existing endscreen exit path.
- Extended static pd-tests to guard MP endscreen disconnect against reintroducing direct `netDisconnect()` in the renderer.
- Registered `g_FilemgrEnterNameMenuDialog` as `MENU_TYPE_AGENT_CREATE`.
- Migrated Agent Select's New Agent push and Back pop through graph helpers.
- Extended static pd-tests to guard Agent Select create/back against reintroducing direct `menuPushDialog(&g_FilemgrEnterNameMenuDialog)` or `menuPopDialog()` in the renderer.
- Migrated MP pause Resume/Back pop through the `MENU_TYPE_MP_PAUSE` graph pop edge.
- Added an MP pause `end_game` graph edge and routed the End Game warning-modal push through `menuGraphFirePushDialog()`.
- Extended static pd-tests to guard MP pause against reintroducing direct `menuPopDialog()` or `menuPushDialog(target)` in those helpers.
- Split solo in-mission pause onto its own graph edge set instead of reusing the generic pause edges.
- Migrated solo pause Resume/Back pop through the `MENU_TYPE_SOLO_MISSION_PAUSE` graph pop edge.
- Migrated solo pause Abort through the `MENU_TYPE_SOLO_MISSION_PAUSE` graph warning-modal edge.
- Added static pd-tests that guard solo pause Resume/Abort against direct `menuPopDialog()` / `menuPushDialog(&g_MissionAbortMenuDialog)` reintroduction.
- Added `menuSwitchToDialog()` so graph firing can switch to an already-open legacy sibling without treating it as a new child push.
- Added `MENU_GRAPH_DEST_SWITCH_SIBLING` and `menuGraphFireSwitchSibling()`.
- Added `MENU_TYPE_SOLO_INVENTORY`, registered solo Inventory and solo Options in the menu pool, and declared graph nodes for solo Inventory and solo Options back edges.
- Migrated solo pause Inventory/Settings and their Back paths through sibling graph edges.
- Extended static pd-tests to guard the sibling helper, registrations, Inventory/Settings graph calls, and back paths.
- Migrated Social Lobby Create Room and Disconnect through `MENU_TYPE_SOCIAL_LOBBY` graph network edges while preserving the existing packet send and disconnect callbacks.
- Extended static pd-tests to guard the Social Lobby renderer against reintroducing direct create-room packet writes or direct `netDisconnect()`.
- Migrated warning-modal confirm/cancel close paths through `MENU_TYPE_WARNING_MODAL` graph pop edges in the generic typed dialog, MP End Game popup, and PC filemgr placeholder.
- Extended static pd-tests to guard the warning renderer against reintroducing direct `menuPopDialog()` calls.
- Added Room graph push edges for Team Setup and Select Music and migrated the Room renderer to use them while leaving Start Match and Leave Room untouched.
- Extended static pd-tests to guard the Room setup subdialogs against direct `menuPushDialog()` reintroduction.
- Added a main-menu inline subview graph fire helper that validates declared graph edges before delegating to the existing subview pool setter.
- Migrated top-level main-menu Solo Play, Online Play, Settings, Mods, Stats, and The Grid buttons through the inline graph helper.
- Added a main-menu inline back-edge helper that validates each subview's declared `back` edge before returning to the top-level view.
- Migrated subview close, Grid Back, Modding Back, and Stats auto-close through the back-edge helper.
- Added `menuGraphFireSceneOp()` so scene/stage-like graph edges can validate the declared edge before running a behavior-preserving callback.
- Migrated combat-sim pause End Match through `MENU_TYPE_PAUSE_MENU` `end_mission`, with `pdguiPauseSetPlayerAborted()` and `mainEndStage()` preserved inside the callback.
- Migrated The Grid Enter through the existing `MENU_TYPE_GRID_SUBMENU` `enter` scene graph edge, preserving `gridCommitEnter()` inside a callback.
- Migrated Room Start Match through the existing `MENU_TYPE_ROOM` `start_match` scene graph edge, preserving the Combat Sim, Campaign, and Counter-Op start logic inside a callback.
- Migrated Room Leave through the existing `MENU_TYPE_ROOM` `leave_room` graph operation, preserving solo-room close, client leave packet, listen-host local leave, and return-to-social-lobby behavior inside a callback.
- Migrated solo endscreen Continue, Retry, and Main Menu through `MENU_TYPE_ENDSCREEN_SOLO` scene graph edges, with the previous bridge calls preserved inside callbacks.
- Migrated MP endscreen Return to Room, Play Again, and Quit through `MENU_TYPE_ENDSCREEN_MP` scene graph edges, and moved the post-disconnect endscreen exit into the graph-dispatched disconnect callback.
- Migrated Solo Mission start/back and in-mission Restart through graph edges, preserving `menuhandlerAcceptMission()` and catalog-backed restart stage resolution inside graph callbacks.
- Added `menuGraphFirePushOp()` for graph push edges whose behavior must stay behind an existing state-setting handler instead of a raw `menuPushDialog()`, then migrated main-menu Solo Missions and Combat Simulator through `MENU_TYPE_MAIN_SOLO_VIEW` push edges.
- Migrated main-menu Modding hub open paths through the existing `MENU_TYPE_MAIN_MODDING_VIEW` `open_hub` push edge, covering both the top-level Mods shortcut and the closed-hub re-entry button.
- Added and migrated a main-menu Stats `open_panel` push edge so the Stats panel open path also goes through graph dispatch.
- Added `menuGraphFireProcessOp()` and migrated the main-menu Quit confirm path through the existing `MENU_TYPE_MAIN_MENU` `quit` process edge.
- Added `menuGraphFirePopOp()` and migrated the main-menu top-level Close path through the existing `MENU_TYPE_MAIN_MENU` `close` pop edge while preserving pause/control restoration.
- Added local-op graph support and corrected Agent Select `load` to a local operation, then routed the Enter/select load paths through `MENU_TYPE_AGENT_SELECT` `load` without changing auto-load or copy flows.
- Added shared `pdgui_nav` menu-action helpers so ImGui renderers can query menu accept/cancel/nav through the action map instead of raw `ImGui::IsKeyPressed`.
- Added Space and keypad Enter as menu/pause/debug accept bindings, then migrated the shared action-bar and confirm-modal accept/cancel shortcuts to `pdguiMenuAcceptPressed()` / `pdguiMenuCancelPressed()`.
- Migrated the graph-owned endscreen, Network menu Back, Social Lobby disconnect, MP pause Back helper, and Bot Setup Back helper off raw Escape polling.
- Migrated the warning-modal typed, MP End Game, and file-manager placeholder shortcuts off raw Enter/Space/Escape polling.
- Migrated combat-sim pause End Match, Debug Shortcuts close, and parent pause close off raw Enter/Space/Escape polling.
- Migrated Room Leave arm, scenario delete confirm, and Leave Room confirm shortcuts off raw Enter/Space/Escape polling.
- Added static pd-tests that guard these priority sites against raw menu shortcut polling returning.
- Added PageUp/PageDown as action-map defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` so keyboard tab cycling is preserved behind action-map authority.
- Migrated Agent Select accept/cancel/list up/down, main-menu Settings tab cycle, main-menu Cinema close/select/up/down, Room tab cycle, and Stats tab/close shortcuts to `pdgui_nav` helpers.
- Added static pd-tests guarding those priority navigation/tab sites against raw menu action polling returning.
- Migrated simple legacy menu replacement surfaces off raw Back/list navigation polling: countdown cancel, shared file browser parent navigation, Agent Create cancel, Challenges list/back, Control Diagram back/up/down, MP Advanced back, MP Settings back/Done, MP Setup back, Player Config back, and Team Setup Done.
- Added static pd-tests guarding those simple legacy menu files against raw menu action polling returning.
- Migrated Cheats hub close, tab cycling, warning close, and Unlock Everything confirm/cancel to `pdgui_nav` helpers.
- Migrated Mod Manager tab cycling/close and Modding Hub tool cycling/close to `pdgui_nav` helpers.
- Added static pd-tests guarding those cheats/modding files against raw menu action polling returning.
- Migrated Training menu Back/Continue/list navigation helpers in `pdgui_menu_training.cpp` to `pdgui_nav`.
- Added static pd-tests guarding Training against raw menu action polling returning.
- Migrated Solo Mission menu-owned Back/Accept/navigation/tab reads to `pdgui_nav`, including mission select, difficulty, co-op/anti options, briefing, inventory, accept mission, solo pause, abort modal, and solo options tabs.
- Added Q/E as additional menu/pause action-map defaults for `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` to preserve Solo Options tab shortcuts behind action-map authority.
- Added static pd-tests guarding Solo Mission against raw menu action polling returning.
- Added `ACTION_MENU_SECONDARY`, `ACTION_MENU_TERTIARY`, and `ACTION_MENU_DELETE`, with menu/pause defaults and shared `pdgui_nav` helpers for secondary commands.
- Migrated Agent Select copy/delete/open-directory, Room bot-row secondary/tertiary commands, and MP Settings preview commands behind action-map authority.
- Added observer-specific action-map actions and `g_ImcObserver`, with default spectator bindings for subset/member navigation, camera toggle, freefly, stop, ascend, and descend.
- Migrated spectator live/theater controls off raw ImGui key polling and into the observer action set. Freefly movement now uses the gameplay move axis plus observer ascend/descend actions.
- Kept Forge observer behavior separate: `LAYER_OBSERVER` only activates `g_ImcObserver` for `SCENE_OBSERVER_SOURCE_SPECTATOR`, while Forge continues using its existing Forge IMCs.
- Added static pd-tests guarding the observer action set, source-specific observer activation, stable observer scene payload storage, spectator raw-key migration, and observer binding visibility in glyph/control UI.
- Added `ACTION_VOICE_PTT`, defaulted to V across gameplay, cutscene, vehicle, observer, Forge session, menu, pause-menu, and debug overlay IMCs.
- Migrated social voice push-to-talk from raw `ImGui::IsKeyPressed/Released(ImGuiKey_V)` polling to `actionPressed/Released(0, ACTION_VOICE_PTT)`, while preserving the existing ImGui keyboard-capture guard so text fields do not start transmission.
- Added static pd-tests guarding voice PTT action-map binding, shared-action classification, raw V polling removal, and Controls UI visibility.
- Added synthetic action-map chord VKs for Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y, and Ctrl+S, with keydown-to-keyup release tracking so chord releases stay paired even if modifiers release first.
- Added Forge placement/bot command actions and Skin Editor brush/tool/grid/UV/undo/redo/save actions.
- Migrated Forge HUD bot commands, Forge placement cancel, Forge Ctrl+Tab sidebar cycling, and Skin Editor shortcuts behind action-map authority.
- Added Controls UI rows for the new Forge and Skin Editor actions.
- Added static pd-tests guarding the editor/tool action ids, synthetic chord bindings, raw polling removal in Forge HUD/Forge Editor/Skin Editor, and Controls UI visibility.
- Current first-party raw-key audit now shows only the documented main-menu PageUp/PageDown queue drain plus comments; third-party ImGui internals are ignored.
- Retired the old cutscene compatibility globals that were no longer read by production gameplay paths: `g_InCutscene`, `g_CutsceneSkipRequested`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, and `g_CutsceneCurTotalFrame60f`.
- `USINGDEVICE(device)` now queries `playerCurrentInCutscene()` instead of the removed `g_InCutscene` global.
- `SVC_CUTSCENE` handling now updates cutscene active state through `playerSetCutsceneActiveMask(...)` on both client and pd-server builds.
- pd-server stubs now keep a local cutscene active mask instead of a fake `g_InCutscene` global.
- Added static pd-tests guarding the retired cutscene globals and sync wrapper against returning.
- Moved Social menu tab cycling behind `pdguiMenuTabPrevPressed()` / `pdguiMenuTabNextPressed()`.
- Removed backend `ACTION_MENU_TAB_PREV/NEXT` to `ImGuiKey_PageUp/PageDown` injection.
- Removed the main-menu PageUp/PageDown queue drain that only existed to compensate for that injection.
- Added static pd-tests guarding Social tab action ownership and preventing the PageUp/PageDown injection or queue drain from returning.
- Added a narrow `inputctx` to `LAYER_MENU` bridge: when the effective input context top is non-gameplay, `inputctx` publishes one menu layer; when gameplay becomes effective again, it pops that layer.
- Wired the bridge through central input context lifecycle points instead of per-menu callsites: init, shutdown, push, resurrect, deferred pop, immediate pop, and end-frame compaction.
- Added pure pd-tests for the bridge semantics and a source guard that the production bridge calls the real input-layer push/pop/abort APIs.

**Verification:**
- Prescribed MSYS2/Ninja flow passed: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- `pd-tests.exe`: 253 test cases / 6641 assertions passed.
- B-267 follow-up verification passed through the same flow: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- Latest `pd-tests.exe`: 262 test cases / 10883 assertions passed.
- B-267 propagation verification passed through the same flow: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- Latest `pd-tests.exe`: 267 test cases / 10926 assertions passed.
- `git diff --check` passed for the input-system files and related context updates.
- Per-player cutscene state migration verification passed through the prescribed flow: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- Latest `pd-tests.exe`: 281 test cases / 15294 assertions passed.
- Cutscene protection verification passed through the prescribed flow: `pd`, `pd-server`, `pd-tests`, then `pd-tests.exe`.
- Latest `pd-tests.exe`: 286 test cases / 16727 assertions passed.
- Cutscene network semantics verification used isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 289 test cases / 16786 assertions passed.
- Vehicle and observer layer verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 291 test cases / 16843 assertions passed.
- Main-menu subview graph verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 293 test cases / 16881 assertions passed.
- Menu graph edge substrate verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 294 test cases / 16925 assertions passed.
- Network graph migration verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 296 test cases / 16953 assertions passed.
- MP endscreen disconnect graph verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 298 test cases / 16970 assertions passed.
- Agent Select graph verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 299 test cases / 16981 assertions passed.
- MP pause graph verification reused isolated build session `ix46`: `.\devtools\build-session.ps1 -Session ix46 -Target all` built `pd` and `pd-server`; isolated Ninja then built and ran `pd-tests`.
- Latest isolated `pd-tests.exe`: 300 test cases / 16993 assertions passed.
- Solo pause graph verification reused isolated build session `ix46`. The session wrapper was invoked but twice stalled in client compile with idle CMake/Ninja children after the command timeout; after confirming no active compiler process, stale `ix46` locks were removed and direct isolated Ninja was used in the same build directory.
- Latest isolated `pd-tests.exe`: 303 test cases / 17031 assertions passed.
- Solo pause sibling graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 303 test cases / 17061 assertions passed.
- Social Lobby graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 304 test cases / 17070 assertions passed.
- Warning modal graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 305 test cases / 17083 assertions passed.
- Room setup subdialog graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 306 test cases / 17092 assertions passed.
- Main-menu inline subview graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 306 test cases / 17103 assertions passed.
- Main-menu inline back-edge graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 306 test cases / 17114 assertions passed.
- Scene-operation graph helper verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 307 test cases / 17128 assertions passed.
- The Grid enter graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 307 test cases / 17133 assertions passed.
- Room Start Match graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 308 test cases / 17144 assertions passed.
- Room Leave graph verification reused isolated build session `ix46`; direct isolated Ninja built the affected targets and ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 309 test cases / 17158 assertions passed.
- Endscreen graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 310 test cases / 17193 assertions passed.
- Solo Mission graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 311 test cases / 17218 assertions passed.
- Main-menu Solo view graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 312 test cases / 17238 assertions passed.
- Main-menu Modding graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 313 test cases / 17246 assertions passed.
- Main-menu Stats graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 314 test cases / 17254 assertions passed.
- Main-menu Quit graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 315 test cases / 17267 assertions passed.
- Main-menu Close graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 316 test cases / 17285 assertions passed.
- Agent Select load graph verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 316 test cases / 17299 assertions passed.
- Raw menu-action helper verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 317 test cases / 17319 assertions passed.
- Priority confirm/exit raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 317 test cases / 17350 assertions passed.
- Priority navigation/tab raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 318 test cases / 17423 assertions passed.
- Simple legacy menu back/nav raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 319 test cases / 17543 assertions passed.
- Cheats/modding raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 320 test cases / 17579 assertions passed.
- Training raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 321 test cases / 17591 assertions passed.
- Solo Mission raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 322 test cases / 17647 assertions passed.
- Secondary menu command raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 323 test cases / 17687 assertions passed.
- Spectator observer raw-input verification reused isolated build session `ix46`. The session wrapper was invoked before building but stalled in client compile with no live compiler after timeout; after clearing the dead `ix46` lock, direct isolated Ninja in the same session build directory built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 324 test cases / 17729 assertions passed.
- Voice PTT raw-input verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 327 test cases / 17748 assertions passed.
- Editor/tool hotkey verification reused isolated build session `ix46`; direct isolated Ninja built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 327 test cases / 17751 assertions passed.
- Cutscene shim-retirement verification reused isolated build session `ix46`. The wrapper was invoked first as directed but timed out in the known client-compile stall. Sandboxed direct Ninja also left stale locks with no live compiler process, so the isolated `ix46` build/test was rerun outside the sandbox.
- Latest isolated `pd-tests.exe`: 328 test cases / 17784 assertions passed.
- `git diff --check` passed outside the sandbox; only the existing LF-to-CRLF warnings for `devtools/_build-env-prelude.ps1` and `devtools/build-headless.ps1` appeared.
- PageUp/PageDown shim verification reused isolated build session `ix46`; isolated Ninja outside the sandbox built `pd`, `pd-server`, and `pd-tests`, then ran `pd-tests.exe`.
- Latest isolated `pd-tests.exe`: 329 test cases / 17795 assertions passed.
- Menu-layer bridge verification is pending isolated build session `ml53`.
- `Build/pd-client.log` playtest evidence:
  - `[03:11.66]` held A advanced from endscreen to stage 0x33.
  - `[03:12.26]` objective 2 intro reached frame 30 and kept playing.
  - `[03:19.05]` A released.
  - `[03:19.65]` fresh A press mapped to `ACTION_SKIP_CUTSCENE`; `[03:19.66]` cutscene IMC exited.

**Next input slice:**
1. Continue transitional-shim audit. Remaining known candidates are `gameplayInputSuppressed()` as the old input-context authority wrapper and ad-hoc `manifestClear` / `mainChangeToStage` / `menupoolReleaseAll` transition triplets.
2. Keep B-267 in Mike's playtest queue: deliberate skip into endscreen, then continue to next mission. Expected log: cutscene IMC deactivates at endstage/teardown before the next intro, and the next intro logs a fresh cutscene activation.

**Task transfer -- sequential completion path:**
1. B-267 root-cause inspection: DONE. Skip-to-endstage missed `SCENE_EVENT_CUTSCENE_END`, and production stage/load lifecycle did not consistently fire scene ready/teardown.
2. B-267 narrow fix: DONE. Skip-to-endstage and stage teardown now unwind cutscene layer/IMC without broad scene-manager expansion.
3. B-267 propagation audit: DONE. Active lifecycle roots now cover skip/endstage, stage ready/teardown, network cutscene sync, disconnect, and tickmode exit from cutscene. Stale `src/lib/main.c` is not compiled and is guarded by test.
4. B-267 regression tests: DONE. Focused `pd-tests` cover cutscene skip -> endscreen -> next mission intro, nested tracked close, and central lifecycle wiring.
5. B-267 verification: BUILD VERIFIED. Mike playtest still required before closing B-267.
6. Per-player cutscene state: DONE / BUILD VERIFIED. `g_InCutscene`, `g_CutsceneSkipRequested`, `g_CutsceneAnimNum`, `g_CutsceneCurAnimFrame60`, `g_CutsceneCurTotalFrame60f`, and `g_Vars.in_cutscene` are behind per-player accessors for migrated gameplay paths. Compatibility shims remain for the later shim-retirement step.
7. Cutscene protection: DONE / BUILD VERIFIED. `chr->cutscene_protect` is toggled with cutscene state and canonical damage/hostility/visibility gates use it.
8. Cutscene network semantics: DONE / BUILD VERIFIED. Existing v46 now includes `SVC_CUTSCENE { active, player_mask }` and `CLC_CUTSCENE_SKIP { playernum }`; server authority binds skip to the authenticated netclient, and protection is narrowed by mask.
9. Vehicle and observer layers: DONE / BUILD VERIFIED. Bike mount/dismount, Forge observer entry/exit, and spectator entry/exit now flow through scene events and tracked layer handles while preserving existing IMC behavior.
10. Menu graph migration: DONE FOR PRIORITY NODES / WATCHING FOR NEW SITES. Main-menu inline subviews now have real `MENU_TYPE_*` pool ownership; graph descriptors, dialog-push, push-op, local-op, sibling-switch, network-op, scene-op, process-op, pop-op, and pop helpers are in place; top-level main-menu inline view opens and subview backs, main-menu Solo Missions and Combat Simulator, main-menu Modding hub open, main-menu Stats panel open, main-menu Quit and Close, Change Agent, Cheats, Network menu, main-menu Online connect paths, Social Lobby create/disconnect, solo/MP endscreen exits, combat-sim pause End Match, The Grid Enter, full Room node, Agent Select load/create/back, MP pause resume/end-game, solo mission start/back/restart, solo pause resume/inventory/settings/abort, and warning-modal confirm/cancel close paths use graph helpers.
11. Raw input migration sweep: DONE FOR CURRENT FIRST-PARTY COMMAND SITES / WATCHING FOR NEW SITES. Shared menu-action helpers are in place; action-bar, confirm modal, endscreen cancel, Network Back, Social Lobby disconnect, MP pause Back, Bot Setup Back, warning-modal shortcuts, combat-sim pause shortcuts, Room Leave/Delete confirm shortcuts, Agent Select navigation, Agent Select secondary/delete/tertiary commands, main-menu Settings/Cinema navigation, Room tabs and bot-row secondary/tertiary commands, Stats tabs/close, countdown cancel, file browser Back, Agent Create cancel, Challenges list/back, Control Diagram back/up/down, MP Advanced back, MP Settings back/Done/preview secondary command, MP Setup back, Player Config back, Team Setup Done, Cheats shortcuts, Mod Manager tab/close, Modding Hub tool/close, Training shortcuts, Solo Mission shortcuts, spectator observer controls, social voice PTT, Forge editor/HUD commands, and Skin Editor shortcuts now use action-map authority. Current audit leaves only the documented main-menu PageUp/PageDown queue drain plus comments/third-party internals.
12. Retire transitional shims: IN PROGRESS. Cutscene compatibility globals, the old sync wrapper, backend PageUp/PageDown action injection, and the main-menu PageUp/PageDown queue drain are retired and build verified. Remaining candidates: `gameplayInputSuppressed()` wrapper and ad-hoc transition triplets once scene ownership is central enough to replace them safely.
13. Determine the next completion task and continue recursively until the input architecture is complete or a hard blocker requires Mike's decision.
14. Final integration verification: run `pd`, `pd-server`, `pd-tests`, grep/static guard checks, and Mike playtests across mission transitions, cutscenes, menus, vehicle, forge/observer, and alt-tab/focus boundaries.

## Open -- 2026-04-27 (Catalog-Owned Asset Pipeline)

**Status: PHASE 0 COMPLETE / PHASE 1 STARTED.** The catalog is the intended single source of truth for all declared assets, not just weapons: identity, metadata, references, source handles, dependencies, load state, loaded payloads, refcounts, and release/unload behavior.

**Done this session:**
- Fixed Phase 0 baseline blockers: `pd-tests` is green and `test_swarm_boid_sim` no longer overshoots when seek distance is shorter than one frame of movement.
- Corrected base weapon catalog registration to the post-GF64-cull MP table: `NUM_MPWEAPONS = 0x29` (41 slots), including `MPWEAPON_NONE`, `MPWEAPON_SHIELD = 0x27`, and `MPWEAPON_DISABLED = 0x28`.
- Split weapon identity API surfaces: runtime `WEAPON_*` handoff vs MP `MPWEAPON_*` selector/setup slot vs catalog ID string boundary identity.
- Added regression tests pinning MP/runtime weapon identity separation and the 41-slot MP table.
- Continued Phase 1 typed identity normalization: added explicit helpers for stage table index vs solo stage index vs stagenum, modelnum, bodynum/headnum, and source filenum/handle identity; migrated ASSET_MAP/MODEL/BODY/HEAD callers off ambiguous generic lookup. Fixed B-265 stagenum-to-stage-table-index confusion in stage_id and manifest backfill paths.
- Started Asset Provider Phase 4: added provider-aware inflated/loaded size query APIs, added handle-aware modeldef load wrappers, migrated `setupLoadModeldef` to load prop/weapon/hat/projectile models from `catalogGetPropHandle()` plus catalog source metadata, and migrated body/head lazy modeldef loaders to `catalogGetBodyHandle()` / `catalogGetHeadHandle()`.
- Continued Asset Provider Phase 4:
  - catalog body/head/weapon/prop resolution results now carry the effective provider handle;
  - Forge runtime door / weapon-pad / prop model loads use `modeldefLoadToNewFromHandle()` instead of direct filenum-only `modeldefLoadToNew()`;
  - menu model state now stores pending/current/body/head provider handles, catalog-resolved menu body/head/weapon previews use provider-aware size/load APIs, and raw filenum menu previews now attempt catalog source-filenum resolution before falling back to the temporary ROM path;
  - `playerTickChrBody` first-person body/head/weapon sizing and modeldef loads now use catalog-resolved provider handles.
  - typed lifecycle wrappers (`catalogLoadTypedAsset`, `catalogReleaseTypedAsset`, `catalogRetainTypedAsset`) now validate catalog entry type before dispatching to the legacy string-only lifecycle calls; SP manifest load/unload and late-add paths use the typed wrappers for known manifest asset types.
  - `bgunTickMasterLoad` / `bgunTickGunLoad` now queue a catalog/provider handle alongside the legacy first-person hand/gun/cart `loadfilenum`; provider handles use provider-aware size/load APIs, and only no-handle uncataloged sources use the temporary ROM fallback.
  - First-person gun async model loads no longer restore `g_FileInfo[loadfilenum]` across texture/DL ticks. The queued gun load stores its own loaded/allocation sizes and calls a size-driven display-list promotion wrapper, leaving `g_FileInfo[]` mutation to legacy ROM callers.
  - Title/logo model loads now resolve `ASSET_MODEL` provider handles through `catalogGetPropHandle()` and use `modeldefLoadFromHandle()` / `assetLoadGetLoadedSize()` for catalog/provider-backed sources. A single warning-backed fallback remains only when no provider handle exists.
  - `modelcatalog` validation now resolves body/head provider handles by source filenum and uses `modeldefLoadToNewFromHandle()` while preserving the existing fault guard. A warning-backed fallback remains only when no provider handle exists.
  - Typed lifecycle loaders now have a type-policy/provider-backed payload path: `catalogLoadTypedAsset()` validates type, loads through the catalog effective provider handle when present, and falls back to the legacy file path only when no provider handle exists.
  - `modeldefLoadFromHandle()` now supports non-ROM provider handles by using a size-driven promotion path instead of `g_FileInfo[]`; the legacy `modeldef0f1a7560()` wrapper still updates `g_FileInfo[]` for old ROM callers.
  - Stage diff (`lv.c`) and screen mini-manifests (`screenmfst.c`) now call typed retain/release/load wrappers instead of raw string-only lifecycle functions.
  - Added focused static tests to keep those migrated lifecycle call sites typed and to prevent the handle modeldef loader from regressing to RomProvider-only.
  - Domain migration audit / low-risk cleanup slice:
    - Added typed `catalogGameModeIdByScenarioIndex()` for MPSCENARIO / `ext.gamemode.mode_id` identity and migrated the remaining production `ASSET_GAMEMODE` `catalogIdByRuntime` fallbacks in `scenario_save.c`, `savefile.c`, `net.c`, `matchsetup.c`, `netmsg.c`, and `pdgui_menu_room.cpp`.
    - Migrated the room screen Campaign and Counter-Op mission starts from generic `catalogIdByRuntime(ASSET_MAP, stagenum)` to `catalogStageIdByStagenum()`.
    - Added a focused static guard in `tests/test_catalog_provider_static.cpp` for the migrated game-mode and room-stage boundaries.
    - Audit found no production raw `catalogLoadAsset()` / `catalogUnloadAsset()` call sites outside `assetcatalog_load.c` and `server_stubs.c`; remaining hits are comments, declarations, implementation, or static tests.
    - Deferred source-filenum bridge sites in `src/game/menu.c`, `src/game/bondgun.c`, and `port/src/modelcatalog.c`; these still probe catalog source handles from legacy file numbers and need the main payload-promotion/provider session, not this cleanup lane.
    - Deferred direct model/file fallback paths in title/menu/player/modelcatalog and any first-person gun no-handle fallback paths; no first-person async loader state edits were made in this cleanup lane.
  - Provider-surface guardrail lane:
    - Audited source for raw `romProviderHandle()`, `assetprovider_internal.h`, direct RomProvider assumptions, and RomProvider-only loader wording/guards outside approved catalog/provider internals. No raw handle or internal-header leaks were found outside the allowlist.
    - Fixed one isolated provider-surface violation: `modelcatalog.c` no longer checks `handle.provider == romProvider()` for its missing-file precheck; it uses the provider-aware inflated-size query for catalog handles and keeps the null-handle ROM fallback for uncataloged legacy sources.
    - Broadened `tests/test_catalog_provider_static.cpp` with source-wide allowlist checks for raw `romProviderHandle()`, `assetprovider_internal.h`, and RomProvider-specific checks; extended typed lifecycle boundary coverage to `netmanifest.c`; and kept modeldef/bondgun guards against RomProvider-only regression wording and gating.
  - Catalog-owned model payload slice:
    - Added `asset_payload_kind_t` and `asset_entry_t::payload_kind` so catalog lifecycle knows how `loaded_data` is owned and released.
    - `catalogLoadTypedAsset()` now activates/caches promoted modeldef payloads for model-like types (`ASSET_MODEL`, `ASSET_BODY`, `ASSET_HEAD`, `ASSET_PROP`) via `modeldefLoadToNewFromHandle()` and marks them `ASSET_STATE_ACTIVE`.
    - Added `catalogGetLoadedModeldef()` as the typed query surface for catalog-owned activated model payloads.
    - Release now frees raw byte payloads with `sysMemFree` but only detaches stage-pool modeldef payloads, avoiding incorrect frees.
    - Added typed language payload activation: `ASSET_LANG` lifecycle loads now call `langManifestEnsureId()`, mark the entry `ASSET_STATE_ACTIVE`, and use `ASSET_PAYLOAD_RUNTIME_ACTIVE` so release detaches the catalog reference without freeing runtime-owned lang memory.
    - Added typed audio runtime activation: `ASSET_AUDIO`, `ASSET_SFX`, and `ASSET_MUSIC` lifecycle loads now validate that a provider/path exists and mark the entry active with `ASSET_PAYLOAD_RUNTIME_ACTIVE` instead of reading whole audio files as generic byte blobs.
    - Added typed individual texture payload activation: `ASSET_TEXTURE` lifecycle loads now use a texture-specific hook, stores loaded bytes with `ASSET_PAYLOAD_SYSMEM_BYTES`, and marks the entry `ASSET_STATE_ACTIVE`. Texture packs/directories remain separate component-level assets.
    - Added typed metadata runtime activation: `ASSET_HUD` and `ASSET_BOT_PROFILE` lifecycle loads now activate from catalog metadata with `ASSET_PAYLOAD_RUNTIME_ACTIVE` instead of falling through to generic byte loading. Release detaches the catalog reference while runtime/catalog metadata ownership stays with the owning subsystem.
    - Extended metadata runtime activation to selector metadata (`ASSET_ARENA` and `ASSET_GAMEMODE`) so arena/scenario catalog entries become active through lifecycle without loading INI bytes as generic payloads.
    - Extended metadata runtime activation to descriptor metadata (`ASSET_SKIN` and `ASSET_BOT_VARIANT`) and kept static coverage requiring all metadata-only lifecycle types to use `ASSET_PAYLOAD_RUNTIME_ACTIVE`.
    - Extended metadata runtime activation to `ASSET_EFFECT`, whose catalog extension is shader/effect metadata rather than an independently owned byte payload.
    - Restored isolated build verification after parallel-lane changes by adding missing social scaling include, aligning cutscene packet implementation with the new player-mask signature, keeping the `CLC_CUTSCENE_SKIP` dispatch single/reachable, and adding dedicated-server stubs for newly referenced inventory/cutscene helpers.
    - Wired weapon/prop `model_file` declarations to catalog primary provider handles in local scanner and network-distributed hot registration, restored distributed character `bodyfile` provider handles, corrected distributed relative-file handles to resolve against the extracted component directory, added `prop.ini`/`texture.ini`/`audio.ini`/`hud.ini` hot-registration coverage, and added `ASSET_WEAPON` to catalog-owned model payload activation.
    - Wired local scanner source handles for `ASSET_TEXTURE` `file_path`, `ASSET_AUDIO` `file_path`, and `ASSET_HUD` `texture_file`, with static coverage matching the distributed hot-registration path.
    - Removed the title/logo model no-handle ROM fallback; title model loads now require a catalog provider handle and fail loud with `CATALOG.MISS` otherwise.
    - Removed the `modelcatalog` validation no-handle ROM fallback; validation now treats missing provider handles as missing catalog source data.
    - Removed the raw menu model preview no-handle ROM fallback; uncataloged raw preview filenums now log `CATALOG.MISS` and skip instead of loading outside the provider layer.
    - Removed the first-person gun queued-load no-handle ROM fallback; queued hand/gun/cart loads now require catalog provider handles and use the existing failure path on miss.
    - Removed the first-person player weapon model no-handle ROM fallback; the temporary ROM model fallback allowlist is now empty and production fallback wording is gone.
    - Centralized file-backed source-handle assignment in the typed catalog registration helpers for character `bodyfile`, weapon/prop `model_file`, texture/audio `file_path`, and HUD `texture_file`. Direct registration callers now populate `entry->source.primary` without waiting for scanner or distribution-specific repair code.
    - Tightened typed texture lifecycle activation so `ASSET_TEXTURE` payloads require a catalog provider handle and no longer fall back to raw `fsFileLoad()` from path fields.
    - Tightened `ASSET_AUDIO` runtime activation so component audio entries require a catalog provider handle while preserving the broader `ASSET_SFX`/`ASSET_MUSIC` compatibility path. Distributed `audio.ini` hot-registration now avoids synthesizing a `destdir/` provider handle when `file_path` is absent.
    - Tightened model-like typed lifecycle activation (`ASSET_MODEL`, weapon, body, head, prop) so model payloads require catalog provider handles and no longer fall through to generic path loading when a handle is missing.
    - Confined generic raw path fallback to legacy untyped `catalogLoadAsset()` compatibility. Typed lifecycle callers that reach the generic branch now require a provider handle and fail loud instead of falling through to `s_catalogLoadEntryFromPath()`.
    - Added `ASSET_MAP` to metadata runtime activation so stage/catalog map entries load as catalog metadata instead of falling into generic provider/path loading. Refreshed stale screen/manifest lifecycle comments to typed load/release wording.
    - Added `ASSET_CHARACTER` to metadata runtime activation so composite character catalog entries no longer load their bodyfile as a generic byte payload.
    - Added `ASSET_TOOL`, `ASSET_VEHICLE`, and `ASSET_MISSION` to metadata runtime activation. These descriptor-only catalog types now avoid generic provider/path loading until a future domain-specific payload policy exists.
    - Moved pack/descriptor entries `ASSET_ANIMATION`, `ASSET_TEXTURES`, `ASSET_SFX`, and `ASSET_MUSIC` to metadata runtime activation. `ASSET_AUDIO` is now the only audio lifecycle type that requires a file provider handle.
    - Added `ASSET_UI` to metadata/runtime activation so renderer-owned UI entries are tracked by catalog lifecycle without being treated as generic byte payloads.
    - Removed the legacy raw path lifecycle fallback. Untyped `catalogLoadAsset()` now dispatches through the entry's actual catalog type, and static coverage prevents `s_catalogLoadEntryFromPath()` / `FALLBACK: catalogLoadAsset` from returning.
    - Retired public untyped lifecycle wrappers entirely. `catalogLoadAsset()`, `catalogUnloadAsset()`, and `catalogRetainAsset()` are no longer declared, implemented, or stubbed; typed lifecycle is the only public catalog retain/load/release surface.
    - Updated stale manifest/hotswap comments and static coverage so untyped lifecycle declarations/calls cannot be reintroduced outside the internal entry-level helpers in `assetcatalog_load.c`.
    - Centralized file-backed primary source-handle assignment behind `catalogSetPrimaryFile(entry, path)`.
    - Migrated local component scanning and network-distributed hot registration off direct `fileProviderHandle()` calls while preserving distributed relative-path resolution against the extracted component directory.
    - Added a source-wide static guard so future direct `fileProviderHandle()` callers stay inside the catalog/provider boundary allowlist.
  - Source-filenum provider handle bridge cleanup:
    - Added `catalogHandleBySourceFilenum(asset_type_e type, s32 source_filenum)` so legacy numeric source-file callers ask the catalog for the effective provider handle instead of open-coding `catalogIdBySourceFilenum()` / `assetCatalogResolve()` / `catalogEffectiveHandle()` loops.
    - Migrated the first-person gun async loader, raw menu model preview path, and `modelcatalog` validation bridge to the shared helper.
    - Preserved warning-backed no-handle ROM fallbacks for uncataloged legacy sources as temporary migration bridges only.
    - Added a static guard to keep these source-filenum bridge callsites catalog-owned.
    - Made the remaining player weapon model no-handle fallback emit a throttled `CATALOG.MISS` warning before using the temporary ROM fallback, and added a source-wide guard so temporary ROM fallback wording stays confined to the known bridge files.
  - Modelnum typed identity/API cleanup:
    - Added explicit modelnum APIs (`catalog_model_result_t`, `catalogResolveModel()`, `catalogResolveModelByModelnum()`, `catalogGetModelHandle()`, `catalogGetModelFilenumByModelnum()`) so `MODEL_*` / `g_ModelStates[]` identity no longer routes through prop-named public helpers.
    - Kept `catalogGetPropHandle()` and `catalogGetPropFilenumByIndex()` as compatibility wrappers while live callsites migrate.
    - Migrated `title.c`, `player.c`, and `setuputils.c` modelnum load sites to typed modelnum result APIs.
    - Added a static guard to prevent these migrated modelnum load sites from regressing to prop-named helpers.
    - Added a source-wide static guard so deprecated prop-named model wrappers remain confined to the catalog API/header during the compatibility period.
  - Typed lifecycle release/retain cleanup:
    - Split `catalogUnloadAsset()` / `catalogRetainAsset()` behavior into entry-level internal helpers.
    - `catalogReleaseTypedAsset()` and `catalogRetainTypedAsset()` now validate the expected type, resolve the mutable entry, and call those internals directly instead of bouncing through untyped public wrappers.
    - Dependency cascade unloads now use the same entry-level internal unload helper.
    - Added a static guard preventing typed lifecycle internals from regressing to untyped public wrapper calls.
    - Added a source-wide static guard preventing production code from calling untyped catalog lifecycle functions outside the catalog implementation/header and server stubs.

**Next execution order:**
1. Current typed lifecycle policy covers every declared asset type; no generic raw path payload fallback remains.
2. Public untyped lifecycle wrappers are retired; typed lifecycle APIs are now the only public retain/load/release surface.
3. File-backed scanner/distribution primary source handles now route through the catalog helper; direct `fileProviderHandle()` use is confined to catalog/provider internals and stubs.
4. Keep source-wide provider-surface guardrails in place and determine the next safe catalog/provider slice recursively until a hard blocker requires Mike's decision.

**Operator-side verification still owed:** in-game validation for Combat Sim setup weapon slots, Random/Fiesta spawn modes, weapon pads, stage transitions, and any mod weapon distribution path.

---

## Open — 2026-04-25 (Priority M / B-238 -- unified `.pdmod` end-to-end)

**Status: RESOLVED-PENDING-PLAYTEST.** Branch `claude/sharp-almeida-aeeb1f`, 13 commits (`77c622a2` -> `c55d96ce`). All M-1 / M-2 / M-3 / M-4 phases landed and build-verified.

- Verification matrix: `context/audits/pdmod-verification-matrix-2026-04-25.md`.
- Migration audit: `context/audits/pdmod-migration-2026-04-25.md`.
- Design doc updated with measured benchmark numbers + cross-platform note + mod-tool migration recipe: `context/designs/pdmod-unified-mod-format.md`.

**Operator-side verifications still owed (Mike's hand on a real install):**
1. First-boot migration log fires + `mods/.pdmod-migration-done` sentinel appears + each former `mods/<name>/` becomes `<name>.pdmod` + `<name>.legacy_backup/`.
2. Build + register the Property Handler (`ninja -C Build PD2ModPropHandler` then run `tools/pdmod_prophandler/install/register.ps1` as Admin); right-click any `.pdmod` and confirm Properties -> Details populates Title / Authors / Comment / Version.
3. In-game mod manager hot-toggle: disable an enabled archive mod, apply, confirm assets unload; re-enable, apply, confirm reload.
4. Trust gate: `mkdir mods/shared/test_friend && cp some.pdmod mods/shared/test_friend/`. Launch. Confirm log says "skipping reserved top-level 'shared'..." and the inbox archive is NEVER mounted.
5. Theme editor "Save as .pdmod" button: edit a theme, click the new button, confirm `mods/<slug>.pdmod` lands and the theme appears in Settings -> Theme selector after rescan.

After Mike validates the above, B-238 closes fully (RESOLVED -> CLOSED) and the M phase is shippable.

---

## Open — 2026-04-20 (Super Audit 2026-04-20 — Wave 3A hardening + carry-overs)

**Logged 2026-04-21 (Chicago CS playtest — follow-ups):** F6 residual bot motion (**B-217**), Chicago initial bot stack vs OK respawns (**B-218**), FP weapon invisible / `GAMELOOP.WEAPON` vs `SPAWN` weapon mismatch (**B-219**), mod-registry `mod.json` missing-file spam (**B-220**). Full **pd-client.log** scrape (WARNING/ERROR/audio) in `session-log.md` **S431** § Ephemeral log digest (source file not retained).

**Playtest queue (agent cannot run client — Mike):** **Solo CI** — death → fade → respawn without full hub reload (**S440**); **door + NPC line** — subtitles + interact (no crash / empty panel; **listen host** if possible: NPC line still reaches host HUD); **main menu** — CI → nested options/settings, modal scrim should match single-modal darkness (**B-222**); **listen host** — lobby + room, no gameplay HUD / killfeed / interact bleed (**B-222**, **B-221** pool). **B-223** only if modal + pause/countdown/endscreen still feels too dark. Log notes → close **B-222** / **B-221** or narrow **B-223**.

**Logged 2026-04-21 (input / overlay — fixes deferred):** vehicle double-tap vs tap, hold-ring live + release reset, tap-vs-hold interact, visual mapper, multi-bind priority, overlay black tint, killfeed visibility + position, scorecard Back hold — **`session-log.md` S432**; **`bugs.md` B-221, B-222**, follow-up dim sweep **B-223**.

**Done 2026-04-20 (PD_DEV_BUILD / F7 invincibility + stable gating — S430):** CMake `PD_STABLE_RELEASE` → `PD_DEV_BUILD` on `pd`; dev-only F6/F7/F12, Settings Debug tab, stacked HUD banners; `release.ps1` stable configure adds `PD_STABLE_RELEASE=ON`. See `session-log.md` S430.

**Done 2026-04-20 (PC ADS + controller sensitivity UI — S425):** Twin-stick LT ADS on PC (RS aim, LS move); `SensMoveUi` / `SensAimUi` / `SensAdsUi` 1–10 (0.5) in `pd.ini` + Settings; ADS move slowdown + zoom mul + crosshair from RS. See `session-log.md` S425.

**Done 2026-04-20 (H-1 P3-A/P3-B — in-client host / go online):** `pdgui_menu_network` listen host + `pdgui_lobby` route for `NETMODE_SERVER && !g_NetDedicated`; `netSendRoom*` + `netListenHostRoomLeave` for leader/leave; `Net.Server.Port` + README PD2 fork pointer. See `session-log.md` S421.

**Shipping scope update 2026-04-27 (S486):** Do not spend current ship-track effort on the dedicated server product. For now the online target is internal client-hosted connectivity: in-client listen host, join flow, room/lobby UX, NAT/connect-code path, manifest/catalog distribution, ready gate, stage start/end, and reconnect behavior inside the game client. `pd-server` may remain buildable for tooling/regression coverage, but it is not the release surface.

**Done 2026-04-20 (controller + hold housekeeping):** USE hold **Settings UX** when per-action override is set (effective ms + disabled global slider); **C-button policy** in `constraints.md` (UI-only hide on Controller tab); **bondmove** / **actionmap.h** comments; **terminal extra hold** tunable via **`ActionMap.InteractHoldExtraTerminalMs`** + Settings slider; **menu-controller-input-constraints.md** §2.1 sanity table; **INDEX.md** link. Follow-up only if needed: stage-specific hold beyond actionmap + `prop.c` categories.

**Done this session (S410):** Settings → Controls → Controller map — **split zones** (Bind 1 left / Bind 2 right), **right-click clear** per column, **LS/RS cardinal** synthetic VK drop targets; `pickSlotForControllerBindColumn` matches table. See `context/session-log.md` S410.

**Done this session (S409):** Settings → Controls — per-action **hold** overrides UI (HoldMsOverrides); Controller map **NavFlattened** + gamepad/table hint. See `context/session-log.md` S409.

**Done this session (S407):** Settings → Controls → Controller map — gamepad silhouette + per-zone **B1/B2** labels and tooltips (shared-button clarity); bind logic unchanged. See `context/session-log.md` S407.

**Done this session (S400):** Settings → Controls → Controller: per-stick tuning, use-hold ms, visual drag-drop pad map, C-buttons hidden on controller list. See `context/session-log.md` S400.

**Done this session (S399):** B-207 manifest late-add head `parts=0` logging (not false torn); B-213 Skin Editor — `unk5d5_01` bypass for charpreview, preview zoom, Modding view 3 hub close UX, hub Escape exits paint session first. See `context/session-log.md` S399.

**Done this session (S398):** Apr20 implementation-plan batch — B-204/205 `lvupdate240` catch-up cap, B-206 spawn AABB valid fallback, B-202 weapon-wheel focused highlight, B-208 Select Tunes cache, B-210 Modding Hub open debounce, B-214 `modmgrSyncCatalogToRegistry` after mod delete, B-215 Scale tool `ASSET_BODY` paths, B-216 Grid skip redundant CI transition. See `context/session-log.md` S398.

Report: [`context/audits/2026-04-20-full.md`](audits/2026-04-20-full.md).
Totals this audit: **1 C / 4 H / 4 M / 5 L** (delta only).  19 of 26 prior
C/H findings were closed by Waves 1–3 + S394; see session log for the full
fix list.

**Full Super Audit (standalone, 2026-04-21):** [`context/audits/2026-04-21-full.md`](audits/2026-04-21-full.md) — complete pass per `audit-prompt.md` (not delta vs 2026-04-20); scorecard **1 C / 6 H / 3 M / 1 L**; covers game-agnostic server programme + in-client listen-host vs product UI.

### Tier 4 C-1 — game-agnostic dedicated server (P4-A / P4-B / P4-C) — DEFERRED

- **P4-A (doc-only) — DONE 2026-04-21 (revised S423):** ADR [`context/designs/pd-server-plugin-abi-adr.md`](designs/pd-server-plugin-abi-adr.md) — **primary:** host **manifest broker**, **catalog ID** identity, per-client **dynamic catalogs**, **hashes** / manifest revision, **no game content** in server exe; **Trust / Confirm First** (per-player readiness); optional **policy module** only where data cannot express rules; versioning + CMake direction. Audit prompts: [`context/audits/2026-04-21-resolution-prompts.md`](audits/2026-04-21-resolution-prompts.md) Tier 4.
- **P4-B — deferred, not current ship-track:** smallest **broker-aligned** slice — host manifest **received/stored**, **fan-out**, per-player **readiness** for manifest accept/hash (**Confirm First** must not block whole lobby); deliberate **`NET_PROTOCOL_VER`** bump if wire changes; **no** new baked PD2 tables as the fix.
- **P4-C — deferred with P4-B:** shrink `server_stubs.c` **baked authority** as manifest path replaces it; track line count + CMake link surfaces; align **`netmsg`** with broker checks (IDs + hashes + readiness) before PD2-only branches.

**Current decision (S486):** dedicated server remains a valid future architecture track, but it is not part of the next release scope. Do not let P4-B/P4-C block client online connectivity work.

### Wave 3A Hardening — batch candidate (½ d – 1 d)

- **AUDIT-H1** — **Done (S416, P1-A)**: `netmsg.c` admin auth rate limit + `ADMIN_RESP_RATE_LIMIT` / `DISCONNECT_ADMIN_AUTH`; optional IP buckets (no auto-ban by default).
- **AUDIT-H2** — **Done (S416, P1-B)**: `net.c` OS RNG for cookies + emergency SHA fallback.
- **AUDIT-H3** — **Done (S416, P1-C)**: `server_bans.c` Windows `MoveFileExA` + `_commit`.
- **AUDIT-H4** — **Done (S416, P1-C)**: `banAddrEq` `in6_addr` compare (IPv4-mapped).

### Medium — mostly quick wins

- **AUDIT-M1** — **Done (S416)**: min token 16 + `< 32` warning (`server_admin.c`).
- **AUDIT-M2** — **Done (S416, P1-D)**: explicit `(truncated)` footer (not a follow-up bit on wire).
- **AUDIT-M3** — **Done (S416, P1-B)**: single-threaded contract on `netOsRandomBytes` / `netServerIssueCookie` (no mutex — OS APIs thread-safe).
- **AUDIT-M4** (5 min): compile-time tripwire —
  `_Static_assert(MAX_PLAYERS + MAX_BOTS <= 64, "participant active-mask
  wire format caps at 64 slots")` in `pdgui_constants_check.c` or
  `participant.c`.

### Low — polish + docs

- **AUDIT-L1** (15 min): log token length + character class bucket on
  failed CLC_ADMIN_AUTH.
- **AUDIT-L2** (30 min – 1 h): hold-vs-tap `ACTION_USE` — synthesize
  reload on release if hold time < threshold (removes 250 ms UX delay
  for the common case). Or expose threshold as an input-settings value.
- **AUDIT-L3** (10 min): zero-scrub `chosen` plaintext in
  `serverAdminInit` early-return paths.
- **AUDIT-L4** (1–2 h, spread): add `_Static_assert(sizeof(struct X) == N)`
  + `offsetof(…) == M` to the top-10 wire-serialized structs. Cross-cutting
  concern X-6 from 2026-04-19.
- **AUDIT-L5** (30 min): replace string-walk IP extraction in
  `CLC_ADMIN BAN` (`netmsg.c:7031-7059`) with direct
  `ENetAddress` → `inet_ntop`.

### Carry-overs — still open from 2026-04-19 master audit

- **MASTER-H3 / H-3** (High, latent): u64 active-mask caps 64 slots; not
  currently exploitable (MAX_PLAYERS + MAX_BOTS = 40). AUDIT-M4 tripwire
  converts this into a compile-time error if MAX_BOTS is ever raised.
- **SEC-8 / SEC-9** (High): PVS / interest management — **design draft:** [`designs/interest-management-replication.md`](designs/interest-management-replication.md) (2026-04-21). Implementation still open; baseline fan-out in `port/src/net/net.c` `netEndFrame` + `enet_host_broadcast` (prior audit line refs may drift).
- **SAVE-1** (High): MP stat integrity still trusts client. 1 d.
- **LAYOUT-2** (Medium): `pd.ini` `LastJoinAddr` reused without
  validation. `connectCodeDecode` or IP-parser gate at load. 30 min.
- **M-1** (Medium): unaligned writer path flagged in prior audit but no
  concrete repro yet. Carry.
- **M-24** (Low): menupool `parent_type` assertion (strict tree flag).
  Deferred.

---

## Open — 2026-04-19 (Menu Stack Compliance — from `context/designs/menu-stack-architecture.md`)

Design doc codifies the strict tree-stack menu architecture: single-instance-per-type,
linear-chain ancestry, cascade close on transitions, input authority at leaf, docked
buttons / docked previews / nine-slice-interior-only / popup-modal confirms, progressive
focus for mission-select-style flows. Full audit of 30 `pdgui_menu_*.cpp` files shows
4 gold-standard references (mainmenu, warning, theme_editor, modmgr) and ~15 violations.

See [menu-stack-architecture.md](designs/menu-stack-architecture.md) §8 for the full
punch list. One fix per session, one file at a time — each fix is a small diff plus a
playtest.

### Tier 1 — destructive-action confirms (swap sibling-dialog push → `BeginPopupModal`)

- ~~**M-1**: `pdgui_menu_mppause.cpp` — `End Game` replaces legacy `g_MpEndGameMenuDialog` sibling push with `BeginPopupModal` + `pdguiPopupDarkenBehind(0.65f)`. Focus on Cancel.~~ **DONE S385** (`claude/infallible-goldberg-71b379`) — popup modal was already in place from S368 but had a controller-focus race + no input debounce; S385 added 5-frame `SetKeyboardFocusHere(0)` on Cancel + 3-frame click debounce so controller D-pad reliably reaches the End Match confirm button and the opening Enter press can't bleed through. Dialog also registered as `MENU_TYPE_WARNING_MODAL` for pool lifecycle. See bugs.md B-198.
- ~~**M-2**: `pdgui_menu_solomission.cpp` — `Abort Mission` replaces `g_MissionAbortMenuDialog` full-screen push with popup modal over pause menu. Red/danger palette inside the modal body.~~ **DONE 2026-04-19** (`claude/bold-nightingale-a10f3a`) — `renderAbortMission` rewritten as `ImGui::OpenPopup` + `BeginPopupModal` with `pdguiPopupDarkenBehind(0.65f)` scrim. Cancel defaults focus, Abort confirm is red-tinted. 5-frame `SetKeyboardFocusHere(0)` latch + 3-frame Enter/A debounce mirror M-1. Dialog registered as `MENU_TYPE_WARNING_MODAL` in `menupool.c`.
- ~~**M-3**: `pdgui_menu_cheats.cpp` — `Confirm Unlock` replaces `g_CheatsConfirmUnlockMenuDialog` sibling push with popup modal.~~ **DONE 2026-04-19** (`claude/bold-nightingale-a10f3a`) — `renderCheatsConfirmUnlock` rewritten with same `BeginPopupModal` pattern. "No" defaults focus, red "Yes" confirm. 5/3-frame focus latch + debounce. Dialog registered as `MENU_TYPE_WARNING_MODAL`.
- ~~**M-4**: `pdgui_menu_agentselect.cpp` — `Delete` / `Copy` replaces inline prompt with `BeginPopupModal`; default focus on Cancel.~~ **DONE 2026-04-19** (`claude/bold-nightingale-a10f3a`) — Inline dimmed-overlay prompt replaced with viewport-level `BeginPopupModal` rendered after the agent-select window closes. Delete focuses Cancel (destructive — red confirm button, red palette); Copy focuses Confirm (non-destructive — default palette). Agent list key input gated while modal open. 5/3-frame latch + debounce. No new `menupool.c` registration needed (popup is purely ImGui state, no legacy dialogdef push).
- ~~**M-5**: `pdgui_menu_room.cpp` — audit `Leave Room` + scenario `Delete` paths; add missing confirm modals.~~ **DONE S390** (`claude/elegant-lamarr-7dbd97`) — Leave Room / Back to Menu now opens a top-level `BeginPopupModal` (context-aware label: "Leave Room?" / "Back to Menu?"). Scenario Delete now opens a nested `BeginPopupModal` inside the Load Scenario popup showing the filename. Both use canonical S385 pattern: `pdguiPopupDarkenBehind(0.65f)`, 5-frame `SetKeyboardFocusHere(0)` force-focus on Cancel, 3-frame input debounce, red-styled confirm button. Escape/B cancels, Enter/Space/A confirms. `pdguiRoomScreenReset()` clears the new modal state.
- ~~**M-6**: `pdgui_menu_pausemenu.cpp` — `Quit` in scorecard overlay — add confirm modal ("Quit to desktop?" / "Leave match?").~~ **DONE S390** (`claude/elegant-lamarr-7dbd97`) — End Game tab button (inline `s_EndGameConfirm` toggle → proper modal). File has no literal "Quit" button; End Game is the sole destructive action in the pause menu and the C4/C5 target. Canonical S385 pattern: "End Match?" title, `pdguiPopupDarkenBehind(0.65f)`, 5-frame focus force on Cancel, 3-frame debounce. Parent's Escape handler now gated on `endgamePopupWasOpen` so pressing Escape to dismiss the popup doesn't also close the pause menu. Co-exists cleanly with M-7's action-bar Resume and M-22's menupool-owned `g_CtxPauseMenu` push.

### Tier 2 — docked-button migration (C1 — scroll-off risk)

- ~~**M-7**: `pdgui_menu_pausemenu.cpp` → `pdguiBeginActionBar` / `pdguiActionBarButton`.~~ **DONE 2026-04-19** (worktree `claude/naughty-shtern-e1dabb`) — Resume button now rendered in a docked action bar; tab content child height reserves `pdguiBodyHeightForActionBar(availBelow)`. End Game / confirm tab-header buttons still live in the tab strip (that's the tab selector, not a primary action — M-6 covers the confirm-modal migration separately).
- ~~**M-8**: `pdgui_menu_lobby.cpp` → action bar for `Disconnect` / `Create Room`.~~ **DONE 2026-04-19** — `+ Create Room` pulled out of the scrollable `##social_rooms` column and both it + `Disconnect` now render in the docked action bar; body column height sized via `pdguiBodyHeightForActionBar - footerChatH`. Dedicated server sees Disconnect only.
- ~~**M-9**: `pdgui_menu_network.cpp` → action bar.~~ **DONE 2026-04-19** — `Back` moved to docked action bar; body content (Server Browser list + Direct Connect form + inline Connect button) wrapped in `##mp_body` `BeginChild` sized via `pdguiBodyHeightForActionBar`. Connect stays adjacent to the address input (form-submit pattern, not a nav action).
- ~~**M-10**: `pdgui_menu_moddinghub.cpp` → action bar; verify preview dock (C2).~~ **DONE 2026-04-19** — Hand-rolled `Close` button replaced with `pdguiActionBarButton` + `pdguiBeginActionBar`; tool description row still renders above the bar; `hubFooterH` now = `descH + pdguiActionBarHeight() + 12*scale`. Each tool content child already rendered inside a tab-specific `BeginChild` above the footer — C2 compliance confirmed (preview panels sit in the tool content area above the action bar, not inside a scroll).
- ~~**M-11**: `pdgui_menu_stats.cpp` → action bar.~~ **DONE 2026-04-19** — Replaced keyboard-only `TextDisabled("B/Esc: Close")` with a `Close` docked action button; stats body child sized via `pdguiBodyHeightForActionBar(bodyAvail)`. Escape still closes for kb users.
- ~~**M-12**: `pdgui_menu_controldiagram.cpp` → action bar; verify diagram dock (C2).~~ **DONE 2026-04-19** — Both renderers (`renderSoloMissionControlStyle`, `renderMpControl`) migrated from hand-rolled Back footer to docked action bar. C2 verified: the `##smc_info` diagram panel lives in a sibling column next to `##smc_list`, not inside any scroll. Local `PdButton` helper removed (now unused).
- ~~**M-13**: `pdgui_menu_endscreen.cpp` → replace custom `PdEndButton` Y-offset layout with action bar.~~ **DONE 2026-04-19** — Both `renderSoloEndscreen` (Next Mission / Retry / Main Menu) and `renderMpEndscreen` (Return to Room / Disconnect, or Play Again / Quit) now render buttons via `pdguiBeginActionBar` / `pdguiActionBarButton`. Content children use `ImGui::GetContentRegionAvail().y - pdguiActionBarHeight() - gap - padB` to leave room. The explicit `ImGui::IsKeyPressed(Enter)` handler in MP was removed (the primary button's `isFocused=1` covers it via action-bar Enter activation). Red danger palette on destructive secondary actions (Quit, Disconnect, Main Menu after failure) preserved via `PushStyleColor` around the action-bar button. `inputSuppressed` debounce still gates activations. `PdEndButton` helper removed (now unused).

### Tier 3 — preview-in-scroll audits (C2)

- ~~**M-14**: `pdgui_menu_training.cpp` — Bio / Hangar sub-screens — extract 3D preview to docked sibling panel.~~ **DONE S389** (`claude/bold-herschel-603edc`) — added `ImGuiWindowFlags_NoScrollbar | NoScrollWithMouse` to `beginTrainingWindow` so the outer training frame is strictly non-scrolling. The Bio Profile / Training Details (DT/HT) / Hangar Holograph previews draw at absolute screen coords computed from the window origin, so the NoScroll invariant guarantees they are permanently docked regardless of content overflow.
- ~~**M-15**: `pdgui_menu_moddinghub.cpp` — audit 22 `BeginChild` regions; dock model preview.~~ **DONE S389** — audit: of the 22 `BeginChild` regions, only `##scale_right` in the Model Scale Tool held a rotating character preview. Added `ImGuiWindowFlags_NoScrollbar | NoScrollWithMouse` to that panel. The nine-slice Chrome Tool's `##chrome_sidebar` was already NoScroll (verified). Import / INI-edit / pack-list tools have no 3D preview content.
- ~~**M-16**: `pdgui_menu_room.cpp` — verify char preview is in sibling column, not inside player-list scroll.~~ **DONE S389** — verified: the row-hover char preview lives in `ImGui::BeginTooltip()` (separate float window, not a child of the scroll region); the bot-edit 3D preview lives in the edit modal's right-hand `BeginGroup` column, sibling to the left-column controls. Both are outside the `##room_players_list` scroll. Added a docstring comment pinning the invariant for future editors.
- ~~**M-17**: `pdgui_menu_controldiagram.cpp` — verify diagram is outside any scroll region.~~ **DONE S389** — added `ImGuiWindowFlags_NoScrollbar | NoScrollWithMouse` to `##smc_info`, the control-mode layout panel (PC-port stand-in for the legacy N64 MENUITEMTYPE_CONTROLLER diagram). Static per-mode text descriptions are short and must never scroll.

### Tier 4 — progressive-focus adoption (§6)

- ~~**M-18**: `pdgui_menu_solomission.cpp` — formalize `s_FocusGroup` enum (`MISSION_LIST → DIFFICULTY → START`); B steps back one group; focus returns to invoker on pop.~~ **DONE S389** — introduced `MissionFocusGroup { FOCUS_MISSION_LIST, FOCUS_DIFFICULTY, FOCUS_START }` + `s_FocusGroup` static. Replaced all 11 assignments to the legacy `s_DetailPanelFocus` bool with explicit enum transitions; bool is retained as a read-only macro `(s_FocusGroup != FOCUS_MISSION_LIST)` so existing compare sites stay legible. Escape is now tier-aware: START → DIFFICULTY (restoring focus to the selected diff row), DIFFICULTY → MISSION_LIST, MISSION_LIST → popDialog. A on mission row → DIFFICULTY; A on unlocked diff → START (focus index also moves to the Start button so nav highlight follows). Up/Down arrow nav in the right panel syncs `s_FocusGroup` to match `s_DetailFocusIdx` (start-of-panel = DIFFICULTY, last-slot = START). Mouse click on any control jumps group directly.
- ~~**M-19**: `pdgui_menu_mpsetup.cpp` — arena → weapons/limits → confirm progressive focus.~~ **DONE S389** — since mpsetup is a hub-of-pickers pushed from the Match Setup hub (each with CLOSEONSELECT), progressive focus is realised via push/pop rather than tier-changes-within-a-menu. Added `mp_ArmFocusOnOpen` / `mp_ConsumePendingFocus` helpers + focus-on-open to Arena, Scenario (both normal + quickteam variants), Weapons (first dropdown), and Limits (first slider). Controller users land on the first interactive widget on each picker open without a preparatory D-pad press.
- ~~**M-20**: `pdgui_menu_room.cpp` — scenario → ready progressive focus.~~ **DONE S389** — added `s_StartMatchFocusPending` flag. Scenario combo change sets the flag; Start Match button consumes with `ImGui::SetKeyboardFocusHere(0)` on the next frame, jumping controller/keyboard focus from "pick scenario" straight to "confirm/launch" (the Room's analogue of the §6.4 Ready button).
- ~~**M-21**: `pdgui_menu_training.cpp` — challenge → details → start progressive focus for FR / DT / HT.~~ **DONE S389** — four focus-on-open sites: (1) `renderFrDifficulty` Bronze button, (2) `renderFrWeaponList` selected weapon row, (3) `renderDtList` selected device row, (4) `renderHtList` selected holo-training row; plus (5) `renderTrainingDetailsImpl` (shared DT/HT details) Ok/Resume button. Each uses `ImGui::SetKeyboardFocusHere(0)` gated on `IsWindowAppearing()` (list-level) or on the selected row (index-level). A-press immediately advances into details/start without a preparatory nav press.

### Tier 5 — remaining standardization

- ~~**M-22**: migrate direct `inputCtxPush(&g_CtxImGuiMenu)` calls to `menupoolAcquire(type, def, &g_CtxImGuiMenu)` in: `pausemenu`, `endscreen`, `lobby`, `network`, `moddinghub`, `stats`, `update`.~~ **DONE S388** (`claude/laughing-shockley-30c454`). `pausemenu.cpp` routes `g_CtxPauseMenu` through `menupoolAcquire(MENU_TYPE_PAUSE_MENU, NULL, &g_CtxPauseMenu)` / `menupoolRelease(MENU_TYPE_PAUSE_MENU)`. `endscreen.cpp` renderers now take `struct menudialog *dialog` and push ctx via `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)` for both solo and MP paths. Solo endscreen dialogs (`g_SoloMissionEndscreenCompletedMenuDialog` / `FailedMenuDialog`) now registered as `MENU_TYPE_ENDSCREEN_SOLO`. `g_NetMenuDialog` registered as `MENU_TYPE_NETWORK`. `lobby`/`moddinghub`/`stats`/`update` have no direct ctx push — they rely on the parent main-menu ctx (standalone overlays driven by visibility flags / network state). No migration needed for those.
- ~~**M-23**: verify every stage-transition / match-start / match-end / disconnect site calls `menupoolReleaseAll()`: `pdgui_bridge.c`, `matchsetup.c`, `netmsg.c`, `net.c`. Add missing sites (cascade-close invariant, §3.3).~~ **DONE S388**. Audited all four files; `pdgui_bridge.c` (3 sites), `matchsetup.c` (2 sites), `netmsg.c` (2 sites — SVC_STAGE_START co-op + combat branches) were already in place. Added the missing site in `netDisconnect()` (`net.c:1102`): `menupoolReleaseAll()` + `inputCtxPopDeferred(&g_CtxImGuiMenu)` before `mainChangeToStage(STAGE_CITRAINING)` when `wasingame`, guarded by `#if !defined(PD_SERVER)`. `netmsgSvcStageEndRead` is intentionally NOT instrumented — it transitively triggers `menuPushRootDialog` via `mainEndStage → endscreenPush*` which already calls `menupoolReleaseAll()`; adding a second explicit call AFTER `mainEndStage` would kill the just-acquired endscreen pool slot.
- **M-24**: evaluate adding `parent_type` assertion parameter to `menupoolAcquire` — hard-enforces I1/I3. Opt-in `MENUPOOL_STRICT_TREE` build flag. Defer until Tier 1-3 land.

### Reference implementations (do not modify — use as templates)

- `pdgui_menu_mainmenu.cpp` — Delete Agent uses `BeginPopupModal` correctly (canonical C4/C5).
- `pdgui_menu_warning.cpp` — generic DANGER/SUCCESS type-based modal (canonical confirm renderer).
- `pdgui_menu_theme_editor.cpp` — standalone window + modal root pattern.
- `pdgui_menu_modmgr.cpp` — modal for Unsaved Changes / Large Mod / Validation.

---

## Done — 2026-04-19 (S392 — Super Audit Wave 2 Batch C: wire format + data integrity, `claude/vigilant-wiles-ed5537` → `dev` @ `8c5c4a71`)

- **LAYOUT-1** (High): `netbufWriteGset`/`netbufReadGset` in `port/src/net/netmsg.c:294-310` now serialize `struct gset` field-wise (4× `netbufWriteU8`/`ReadU8` over weaponnum/unk0639/unk063a/weaponfunc) instead of raw memcpy. Removes implicit layout/padding dependency; matches `netbufWriteCoord`/`netbufWritePlayerMove` pattern. Wire format bytes unchanged — protocol-compatible.
- **H-2** (High): `port/src/net/netdistrib.c` wire-delivered mods now register via typed `assetCatalogRegister(slot->id, type)` using new `iniFilenameToAssetType()` helper (map/character/bot/skin/weapon/textures/sfx/music ini → ASSET_* type). Ext fields populated via new `populateExtFromIni()` mirroring `assetcatalog_scanner.c::registerComponent()`. ASSET_NONE fallback emits LOG_WARNING. Entries are type-resolvable immediately on arrival (no next-refresh wait).
- **F-Hardcoded-Player-Caps** (Low/systemic): New `port/include/pdgui_constants.h` — C++-safe mirror of MAX_PLAYERS=8, MAX_LOCAL_PLAYERS=4, MAX_BOTS=32, MAX_MPCHRS=40, MAX_TEAMS=8 (types.h `#define bool s32` + constants.h `#define false 0/true 1` collide with C++). New `port/src/pdgui_constants_check.c` `_Static_asserts` drift against canonical values. Removed 5 local `#define` duplicates (`MAX_PLAYERS_PM`, `ES_MAX_*`, `MAX_MPCHRS_HUD`, etc) in `pdgui_hud/menu_endscreen/menu_mpingame/menu_pausemenu/menu_room.cpp`; renamed all usages back to canonical names. Dropped dead `MAX_MPCHRS_TICKER`.
- **F-StaleStructComments** (Low): Removed invalid `/*0xXX*/` byte-offset comments from `mpchrconfig`/`mpplayerconfig`/`mpbotconfig` in `src/include/types.h` (invalidated by `head_id[64]`/`body_id[64]` additions). Verified no binary serialization of these structs anywhere. Added header note documenting PC-only in-memory use.
- **Files**: 10 changed (234 ins / 88 del; 2 new). **Build**: clean. `PerfectDark.exe` 53.4 MB, `PerfectDarkServer.exe` 23.1 MB. **Merge**: worktree `9a50e683` → dev `8c5c4a71` via `--no-ff`; auto-merge with Batch D (`167b7fce`) on `netmsg.c` and `netdistrib.c` resolved cleanly. Post-merge line counts verified — no unexpected shrinkage.
- **Remaining Super Audit findings**: H-1 (preserved-player token), H-3 (u64 slot mask), H-4 (SHA-256 integrity gap), M-1 (unaligned writer).

---

## Done — 2026-04-19 (S391 — MASTER-C1: netbufReadStr NUL termination, `claude/cranky-haslett-e312ba` → `dev` @ `f3e10caa`)

- **MASTER-C1** (Super Audit 2026-04-19): `netbufReadStr` now force-NUL-terminates the receive buffer at `rp+len-1` before returning; return type changed to `const char *`. Protects all 40+ callsites across netmsg.c, net.c, netmanifest.c, sessioncatalog.c at once. Protocol-compatible (no wire change). Callsite sweep confirmed all read-only. Build clean.

---

## Done — 2026-04-19 (S385 — B-198: CS pause End Game confirm focus + CS end-of-match input-death, `claude/infallible-goldberg-71b379`)

- **B-198** (CS pause → End Game: controller can select "End Game" but not reach Confirm; CS end-of-match: no input, no way back to main menu). Two bugs, one session, both fixed.
- **End Game popup focus (Bug 1)**: `renderMpEndGameDialog` used `IsWindowAppearing()`-gated `SetItemDefaultFocus()` which raced with ImGui's popup NavInit on the OpenPopup + BeginPopupModal same-frame path. New approach: added `s_EndGameOpenFrame` + 5-frame `SetKeyboardFocusHere(0)` force-focus window on Cancel + 3-frame input debounce so the Enter/A press that activated the hub-row Selectable can't bleed into the popup's buttons.
- **End-of-match input-death (Bug 2)**: root cause was a race between `menuPushRootDialog`'s `menupoolReleaseAll()` (deferred pops of owned_ctx slots to end-of-frame) and `renderMpEndscreen`'s fresh-entry ctx-push gate. On frame 1 the ctx appeared active (deferred pop pending), so push was skipped. End-of-frame the pop fired. On frame 2 freshEntry was false — push never happened, `g_ImcMenu` never activated. `ACTION_USE`/`ACTION_CANCEL_USE` → no ImGui Enter/Escape events → Enter/Esc/A/B all dead. Fix: push unconditionally whenever the ctx isn't active while this renderer runs (force-close sites also clear the dialog, so no resurrect-loop risk).
- **Compounding fix**: registered missing endscreen dialogs + End Game dialog in `port/src/menupool.c` — `g_MpEndscreenIndGameOverMenuDialog` / `TeamGameOverMenuDialog` / `ChallengeCompletedMenuDialog` all as `MENU_TYPE_ENDSCREEN_MP`, plus `g_MpEndGameMenuDialog` as `MENU_TYPE_WARNING_MODAL`. Joins the Cheated/Failed variants that were already registered.
- **Files**: `port/src/menupool.c` (+39/−1), `port/fast3d/pdgui_menu_endscreen.cpp` (+40/−20), `port/fast3d/pdgui_menu_warning.cpp` (+75/−28). Net +154/−49 across 3 files.
- **Build**: clean 775/775. `PerfectDark.exe` 53,363,195 / `PerfectDarkServer.exe` 23,141,856.
- **Playtest ask**: (1) CS pause → "End Game" with controller — popup opens, focus on Cancel, D-pad Right reaches End Match, A confirms, B cancels. (2) CS match to natural end — endscreen renders, Enter/Esc/A/B all responsive, Main Menu reachable. (3) Challenge mode (Completed / Failed / Cheated) — all still work post-registration.

---

## Done — 2026-04-19 (S384 — B-184 + B-193 root cause: ALIGN16 pointer-alignment regression from `fe107e3e`, `claude/elated-hugle-7ec221` → `dev` @ `90b448ce`)

- **B-184** (per-object vertex-colour tints: yellow computer props, cyan elevator top, olive character faces) and **B-193** (intermittent invisible CI geometry on cold boot) — same root cause. Commit `fe107e3e` (2026-04-17, M4 optimisation) collapsed `ALIGN16(val)` to `(val)` on the grounds that `mempAlloc` returns aligned memory. True for **size** args; broken for **pointer** args at ~30 sites.
- **B-184 mechanism**: `gfxmemory.c:154` — `g_GfxMemPos = (u8 *)ALIGN16((uintptr_t)g_GfxMemPos)` after `gfxAllocateVertices`. `Vtx` is 12 bytes; odd count drifts pointer by 12 mod 16. Next `gfxAllocateColours` binds a misaligned `Col*` as the `vcn` for the subsequent `G_COL`. Every lit draw reads the wrong vertex-colour bytes → consistent per-object tints on correct textures.
- **B-193 mechanism**: `bg.c:1516 / 1934` — `header = (u8 *)ALIGN16((uintptr_t)headerbuffer)` for a `u8[0x50]` stack buffer. No-op macro leaves `header` wherever the stack put `headerbuffer`. Often 16-aligned by luck on 64-bit; occasional misalignment feeds garbage into `preprocessBgSection1Header` and the inflate, leaving `g_BgPrimaryData` unparseable (invisible geometry). Explains the intermittent / "won't reproduce on clean rebuild" pattern — stack-frame layouts shift between builds.
- **Fix**: single-line revert in `src/include/constants.h:79` — `#define ALIGN16(val)        ((((val) + 0xf) | 0xf) ^ 0xf)`. 0–15-byte overhead per alloc is trivial on PC (<120 call sites, hundreds of MB of game memory). Considered splitting into pointer / size macros but rejected: pointer-vs-size isn't always obvious at the call site (e.g. `gfxAllocate` accumulator), single-macro-with-correct-semantics is the safer primitive.
- **Files**: `src/include/constants.h` (+1/−1), `context/bugs.md` (B-184 + B-193 rewritten), `context/session-log.md` (S384 entry).
- **Build**: clean 777/777. `PerfectDark.exe` 53,232,611 / `PerfectDarkServer.exe` 23,154,674.
- **Merge**: base `d06be0f3`, worktree tip `3869ba29`, dev tip `90b448ce`. `git diff d06be0f3..90b448ce --stat`: `src/include/constants.h | 2 +-` — exact one-line change, no collateral movement.
- **Playtest**: (1) cold-boot CI 5× → scene renders every time (no sky-only). (2) In-game: character skin + clothing are authored colours (not olive-green); computer props not yellow/cyan; elevator top gray (not cyan); doors authored colour (not yellow). (3) Other stages (Skedar Ruins, Complex, Felicity, Temple, Dam, Carrington Villa) 2–3× each for regression.
- **What this explains**: S382's fourth-pass static diff audit through the AP Phase sprint (goofy-shaw-96c6c1) was thorough and its rule-outs were correct — the regression was in a **separate** 2026-04-17 commit (`fe107e3e`, M4 optimisation, not part of AP). brave-bouman-13bd68's "B-193 not reproducing on 5-launch streak" was legitimately low-probability, not a false negative.

---

## Done — 2026-04-19 (S379 — 3-bug playtest batch: interact prompt over menus, bot count scroll, Grid walkable pickups, `claude/friendly-kirch-688ea4`)

- **B-189** — Interact prompt pill ("[E] Pick up" / "[A] Open" / etc.) was rendering over ImGui menus. `pdguiInteractPromptRender` drew on the foreground drawlist every frame the game had an interact target, with no menu gate. When you walked up to a pickup and then opened Settings / Pause / Forge, the pill stayed floating above the menu. Fix: one-line `if (pdguiIsActive()) return;` gate at the top — same authority predicate (`inputCtxGetTop() != &g_CtxGameplay`) every other gameplay-HUD-only overlay uses. Restores the documented HUD layer policy in `context/designs/hud-layer-order.md` §7.
- **B-190** — "Players in Room (X Player, Y Bot)" header in the CS Room didn't appear to update on Add Bot / Remove. Not a state bug: `countBots()` + the header TextColored both re-ran every frame. The header lived INSIDE the `##room_players_list` scrollable child, so once the row list filled its viewport, the header scrolled off the top with it. Fix (`pdgui_menu_room.cpp::renderPlayerPanel`): moved the header and the Team Sort dropdown OUTSIDE the scrollable child into the outer bordered panel; `listH = GetContentRegionAvail().y - btnH - 2*ItemSpacing.y` is computed after the sticky header is laid out. Also baked the count/cap into the Add Bot button label itself — `Add Bot  (X / Y)` — so the primary interaction point always shows live state next to the click. Header format extended from `%d Bot` → `%d/%d Bot` to show the cap.
- **B-146 regression** — S295 fixed walkable pickups by ORing `OBJFLAG3_WALKTHROUGH` into the `weapon()` / `ammocrate()` / `ammocratemulti()` macro expansions in `src/include/props.h`, plus setting it explicitly in `weaponCreateForChr` (network path). Macros only affect code recompiled from source — base-game map setup files are pre-compiled binary loaded from ROM, where the `flags3` byte predates the macro change. On Grid, 2 ammo crates + 1 ground weapon remained standable. Fix: `src/game/setup.c::setupCreateObject` now forces `OBJFLAG3_WALKTHROUGH` at runtime when `obj->type` is `OBJTYPE_WEAPON` / `OBJTYPE_AMMOCRATE` / `OBJTYPE_MULTIAMMOCRATE`, matching the macro intent regardless of whether setup data came from source or ROM. The `propobj.c` auto-floor synthesizer gate at line 2321 is unchanged — it correctly suppresses the floor tile when WALKTHROUGH is set.
- **Files**: `port/fast3d/pdgui_interact_prompt.cpp` (+10/−1), `port/fast3d/pdgui_menu_room.cpp` (+27/−13), `src/game/setup.c` (+16/−0). Net +52/−14 across 3 files.
- **Build**: clean 774/774. `PerfectDark.exe` 53,237,073 / `PerfectDarkServer.exe` 23,139,808.
- **Playtest**: (1) walk up to any pickup / door so the `[E] Pick up` pill appears → open Escape menu / pause / Forge editor → pill disappears immediately; close menu → pill returns if target still in range. (2) Solo CS Room → add 10+ bots → header stays pinned + Add Bot button shows live `N / max`. (3) Load Grid via Combat Sim → walk into every ground weapon and ammo crate on the map → capsule slides past / through, never climbs on top.

---


## Done — 2026-04-19 (S377 — AP Phase 3 game-code call-site migration, `claude/intelligent-pasteur-269b40` → `dev` @ `ba7ed31e`)

- **AP Phase 3 done.** Every remaining game-code caller of the legacy pre-AP wrappers `fileLoadToNew` / `fileLoadToAddr` / `fileLoadPartToAddr` is now on the dispatcher API (`assetLoadRomToNew` / `assetLoadRomToAddr` from `port/include/assetload.h`). 12 call sites across 5 files: `langreset.c` (6), `lang.c` (3), `setup.c` (1), `modeldef.c` (1), `bondgun.c` (1).
- **New API**: `assetLoadToAddr(handle, method, buf, size)` dispatcher (mirrors `assetLoadToNew` for caller-allocated buffers — RomProvider fast-path uses the legacy `fileLoad` pipeline) + `assetLoadRomToAddr(filenum, ...)` convenience wrapper.
- **Renames**: `fileLoadToAddr` → `fileLoadRomToAddr` in `src/game/file.c` (the legacy worker becomes a dispatcher-internal RomProvider impl, exported only so the AP fast-path can reach it). Game code MUST NOT call this directly any more.
- **Deletes**: dead public `fileLoadPartToAddr` (zero callers since S374 moved bg.c to `romdataFileLoad` direct), dead public `fileLoadToNew` wrapper (zero callers post-Phase-3), public `fileLoadToAddr` declaration. All from `src/game/file.c` + `src/include/game/file.h`. File header docs + worker preambles updated.
- **Build**: clean 774/774 from full configure on the worktree's own `Build/` (worktree path detected by `build-headless.ps1` redirects to main, so I configured `cmake -G Ninja` directly). `PerfectDark.exe` 53,054,294 / `PerfectDarkServer.exe` 23,119,328.
- **Merge**: `--no-ff` ort strategy, no conflicts. Pre/post-merge line counts verified: total +64 LOC matches `git diff --stat` exactly (`+155 -91`). No file shrank unexpectedly.
- **Phase 4 audit (filenum retirement)** — out of scope for this session, scope documented:
  - Handle accessors `catalogGetBodyHandle` / `HeadHandle` / `PropHandle` already exist (in `assetcatalog_api.c`, decl in `assetcatalog.h:917+`).
  - 23+ game callers of the `[DEPRECATED] catalogGetXFilenumByIndex` accessors remain across `body.c` (5), `player.c` (5), `menu.c` (2), `mplayer/setup.c` (2), `setuputils.c` (1), `title.c` (8).
  - Phase 4 needs three downstream API migrations as prerequisites: (1) `assetGetSize(handle, loadtype)` to replace `fileGetInflatedSize`; (2) handle-aware `modeldefLoad` variant; (3) `MENUMODELPARAMS_SET_HANDLE` macro / menu model param storage migration. Then the 23 game-code sites can move filenum→handle in batches and the deprecated accessors can be deleted along with the SA-5a bridges.
- **Playtest ask**: zero observable change is the success criterion (RomProvider fast-path is byte-identical to the pre-AP load pipeline). Smoke path: title → CI Training → solo mission → MP match start → mid-mission lang switch. Any load failure now logs `WARNING: fileLoadRomToAddr: file %d failed to load (size=%u), returning NULL` (renamed from the prior `fileLoadToAddr` log) so triage is identical.

---

## Done — 2026-04-19 (S378 — D6 Phase 3 finishing touches, `claude/friendly-mccarthy-a90db6` → `dev` @ `3824d95f`)

- **Task** — Close out the incomplete parts of D6 persistent stats:
  (1) expand the Stats Viewer UI to surface everything the gameplay
  wire-in collects; (2) add damage tracking at MP match end; (3) emit
  achievement-unlock toast notifications so players actually see when
  they unlock.
- **Audit findings**:
  - D2 char select — redesign already DONE (S15 / S187); char preview
    works in agent-select, room lobby, and bot setup. No gaps worth
    fixing in-session. Phase summary row in `infrastructure.md`
    accurately reflects the partial-on-D2c-bot-jump-AI state.
  - D5 Phase 5 lobby scene — already DONE (S352 portraits + S356
    5C/5D polish: hover portrait preview, drop shadow, team-color
    border). `infrastructure.md` summary row + detailed D5 section
    both updated this session to reflect DONE state (was stuck on
    "Phase 5 (lobby scene) planned").
  - D6 persistent stats — wire-in at gameplay sites was complete
    (S325) but three concrete gaps remained: damage dealt/received
    per-match was tracked in `mpplayerconfig` but never promoted to
    `statIncrement`; the Stats UI Overview tab only surfaced a
    fraction of what was collected; `achievementGetNewlyUnlocked`
    existed but was never called — achievements silently unlocked
    with zero player feedback.
- **Damage + hit tracking (`src/game/mplayer/mplayer.c::mpCalculateAwards`)** —
  the existing local-player block now increments `mp.damage_dealt`,
  `mp.damage_received`, and `mp.shots_hit` alongside the pre-existing
  time/distance accumulators. `mp.shots_hit` is computed as
  `round(accuracyfrac * numshots)`; combined with `shots.total`
  already tracked by `mpstatsIncrementPlayerShotCount` this gives a
  real hit-accuracy %. All guards match the existing block
  (`playernum < PLAYERCOUNT()` + `!g_CheatsActiveBank*`).
- **Stats Viewer UI (`port/fast3d/pdgui_menu_stats.cpp::renderOverviewTab`)** —
  three new sections: **Combat Simulator** (Matches Played / Won /
  Lost / Win Rate / Time Played / Damage Dealt / Damage Taken /
  Distance), **Solo Missions** (Completed / Failed / Time Played),
  **World Interaction** (Items Picked Up with breakdown for Weapons
  / Ammo Crates / Shields / Keys + Doors Opened). Accuracy section
  picks up Shots Hit + Hit % rows. New `formatDuration` helper
  renders seconds as `Hh Mm` / `Mm Ss` / `Ns` so time stats stay
  compact.
- **Achievement toasts — new `port/fast3d/pdgui_achievement_toast.{h,cpp}` +
  wire-in** — slide-in cards on the ImGui foreground drawlist in the
  top-right corner. Queue holds up to 4 concurrent toasts; each
  lives 270 frames (4.5 s) with 20-frame fade-in + 60-frame
  fade-out and a 0.4*width right-edge slide during the fade-in.
  Drawn via `AddRectFilled` + `AddRect` + `AddText` on the
  foreground drawlist so the toast overlays any menu. Plays
  `PDGUI_SND_FOCUS` on push. `pdguiAchievementToastPollUnlocks()`
  reads `achievementGetNewlyUnlocked` and maps each returned id
  back to its display name + description via `achievementGetByIndex`.
  Called on solo endscreen `IsWindowAppearing` and on MP endscreen
  fresh entry; MP endscreen also gains `achievementsRefresh()`
  (was previously solo-only). `pdguiAchievementToastRender`
  invoked from `pdgui_backend.cpp::pdguiRender` right after the
  interact prompt.
- **Files**: `src/game/mplayer/mplayer.c` (+7 LOC), `port/fast3d/pdgui_backend.cpp` (+6 LOC),
  `port/fast3d/pdgui_menu_endscreen.cpp` (+6 LOC), `port/fast3d/pdgui_menu_stats.cpp` (+139/−2 LOC),
  NEW `port/fast3d/pdgui_achievement_toast.cpp` (211 LOC), NEW `port/include/pdgui_achievement_toast.h` (44 LOC).
- **Build**: clean 775/775 (worktree fresh configure + full build). `PerfectDark.exe` 53,337,398
  / `PerfectDarkServer.exe` 23,140,832. Incremental dev rebuild after `cmake --reconfigure`
  (CMake GLOB_RECURSE has to re-scan for the new .cpp) — 12/12 link clean; final `PerfectDark.exe`
  53,111,624 / `PerfectDarkServer.exe` 23,143,410. Merge `3824d95f` (`--no-ff`). Post-merge
  line counts verified: mplayer.c 4566, pdgui_backend.cpp 915, pdgui_menu_endscreen.cpp 1398,
  pdgui_menu_stats.cpp 570 — all match the expected pre-merge + S378 deltas.
- **Playtest ask**: (1) Stats menu → Overview tab shows populated MP/Solo/World sections after
  a match / solo mission finishes. (2) Unlock any achievement (e.g. first kill → First Blood,
  100 kills → Centurion, 100 headshots → Sharpshooter) — toast slides in from the right edge
  of the endscreen. (3) `saves/playerstats.json` now contains `mp.damage_dealt`,
  `mp.damage_received`, and `mp.shots_hit` keys after an MP match.

---

## Done — 2026-04-19 (S375 — Room enhanced bot management UI, `claude/trusting-jang-e7cf0b`)

- **Feature** — Combat Simulator room player list: team sort dropdown when Teams are enabled (Custom / 2 Teams / 3 Teams / 4 Teams / Humans vs Sims / Human-Sim Pairs), multi-select with Ctrl+Click (toggle), Shift+Click (range in visible sort order), controller Y (GamepadFaceUp = toggle), controller A (GamepadFaceDown via ImGui nav = single select), controller X (GamepadFaceLeft = open context menu). Context menu relabeled: **Set Character** / **Set AI Type** / **Set Bot Type** / **Set Team** / **Duplicate All** / **Re-Roll Name** / **Re-Roll Name + Character** / **Remove All**. Manual name entry now applies to every selected bot — buffer seeded on popup open, Enter commits to all selected slots; placeholder reads `(Multiple)` when selected bots have differing names. Manually setting a team via the context menu flips the Team Sort dropdown back to Custom so the preset label stays truthful.
- **Duplicate All respects the player limit** — snapshots `g_MatchConfig.numSlots` up front (so duplicates aren't chain-duplicated in the same pass) and re-checks `countBots() < matchConfigMaxBotsForHumans(humanCount)` before each add, logging `ROM: Duplicate All capped at %d bots (added=%d skipped=%d humans=%d)` when the cap is hit. Menu entry is disabled when no selected bots or when the live bot count already equals the cap.
- **New state** — `s_BotLastClickedSlot` (anchor for Shift range select), `s_TeamSortMode` (TEAMSORT_CUSTOM / TWO_TEAMS / THREE_TEAMS / FOUR_TEAMS / HUMANS_VS_SIMS / HUMAN_SIM_PAIRS), `s_BotCtxNameBuf[MAX_PLAYER_NAME]`. All three are reset in `pdguiRoomScreenReset()` and again in the first-frame init block of `pdguiRoomScreenRender`.
- **Helper additions** — new `applyTeamSort(int mode)` local helper in `pdgui_menu_room.cpp` that duplicates the preset logic from `pdgui_menu_teamsetup.cpp` (those helpers are TU-local so they couldn't be called across files; duplicating keeps each screen self-contained). New `matchConfigRerollBotName(s32 idx)` in `port/src/net/matchsetup.c` + declaration in `port/include/net/matchsetup.h` — rolls only the bot's name via `generateBotName`, leaving `body_id`/`head_id` unchanged.
- **Controller UX** — while a bot row has nav focus, `ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false)` toggles selection and updates `s_BotLastClickedSlot`; `ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false)` ensures the focused row is selected (single-select if not already) and opens `##bot_ctx`. A (GamepadFaceDown) is routed through ImGui's default nav → Selectable activation path so it falls into the same branch as a plain mouse click. The existing right-click path is unchanged.
- **Files**: `port/fast3d/pdgui_menu_room.cpp` (+280 LOC), `port/src/net/matchsetup.c` (+8 LOC), `port/include/net/matchsetup.h` (+2 LOC).
- **Build**: clean 774/774. `PerfectDark.exe` 53,008,312 / `PerfectDarkServer.exe` 23,118,816. Worktree → `dev` merged as `3959e9e9` (`--no-ff`); post-merge line counts `port/fast3d/pdgui_menu_room.cpp` 3787, `port/include/net/matchsetup.h` 122, `port/src/net/matchsetup.c` 932 — all match the expected pre-merge + B-187 deltas (no shrinkage).

---

## Done — 2026-04-19 (S374 — B-185 bg silent-load failure fix, `claude/thirsty-torvalds-9c7e0f`)

- **B-185** — Intermittent "invisible level" (scene with no bg geometry, only props/doors visible). Root cause: `bgLoadFile` used `fileLoadPartToAddr`, which silently returns without copying when `fileGetRomSizeByTableAddress` is 0 or `romdataFileGetData` is NULL. The destination buffer keeps whatever bytes were there (stack garbage in `bgReset`'s `headerbuffer`, prior-frame junk in `bgLoadRoom`'s scratch), `preprocessBgSection1Header` parses nonsense sizes, and the subsequent rzip inflate either outputs junk or zeros. `g_BgPrimaryData` ends up empty → `var800a4920 != 0` branch skipped → `g_BgRooms`/`g_BgPortals`/`g_BgCommands` all NULL → no bg render pass runs. Props/doors continue to render because they load through the Phase 4 catalog pipeline (`assetLoadToNew`), which has proper NULL handling. Any failure was therefore completely silent in the log.
- Fix: rewrote `bgLoadFile` (`src/game/bg.c`, +29/−3 LOC) to call `romdataFileLoad(stage.bgfileid, &romsize)` directly, validate `bgfileid > 0`, NULL src, and `offset+len <= romsize` (overflow-safe: `offset > romsize || len > romsize - offset`); `sysLogPrintf(LOG_ERROR, ...)` on every failure path; `memset(memaddr, 0, len)` on failure so downstream sees deterministic zeros rather than stack garbage. Happy path uses `memcpy` (equivalent to the PC `dmaExec→bcopy` path without the silent-failure wrapper). `fileLoadPartToAddr` now has zero callers; the definition is left in `src/game/file.c` as dead code to keep the commit focused.
- Did NOT migrate to Phase 4 `assetLoadToNew` handles because bg.c wants partial-slice reads at specific byte offsets inside the seg file (header, primary compressed block, section-2 header, section-2 compressed block, section-3 header, section-3 compressed block, per-room compressed block). `assetLoadToNew` inflates a whole file — different shape. The direct `romdataFileLoad + memcpy` path achieves the same mod-override / external-file / ROM-fallback routing for free (that routing lives inside `romdataFileLoad`, not `fileLoadPartToAddr`).
- Build clean 776/776. `PerfectDark.exe` 52,973,749 / `PerfectDarkServer.exe` 23,143,410.
- Playtest ask: cold-boot into CI Training / any solo mission 5× in a row — scene should render every time. If ANY cold boot still fails, `BG.LOAD:` lines in `pd-client.log` now pinpoint the failing partial read (bgfileid / stageidx / stagenum / offset / len / romsize) so the next repro is diagnosable instead of silent.

---

## Done — 2026-04-19 (S373 — B-181 spawn-with-weapon fallback fix, `claude/interesting-nobel-184c6c`)

- **B-181** — Chris saw weapons in match that weren't in his selected weapon set. Car Park has 10 weapon markers, but desiredPickups was 16 (PLAYERCOUNT + g_BotCount + span_bonus, capped), triggering the "too few pickups" fallback added in commit `0b62fc68`.
- Root cause: `setupCreateProps` fallback used `catalogIdByRuntime(ASSET_WEAPON, g_MpSetup.weapons[i])` to resolve an MPWEAPON_* index to a catalog ID. But the ASSET_WEAPON runtime cache is indexed by catalog *array position* (`e->runtime_index = i` in `assetcatalog_base_extended.c:414`), NOT by MPWEAPON value — s_BaseWeapons[0] is MPWEAPON_FALCON2=0x01 with runtime_index=0, so `catalogIdByRuntime(ASSET_WEAPON, 0x01)` returns "base:falcon2_silencer" (MPWEAPON_FALCON2_SILENCER=0x02, stored at array index 1). Off-by-one for every MPWEAPON except NONE.
- Secondary bug: fallback only wrote `spawn_weapon_id` when it was empty, never re-derived `spawnWeaponNum`. So current-match spawn used whatever matchStart computed before the fallback ran, and the wrong ID propagated to the NEXT match when matchStart re-read `spawn_weapon_id`.
- Fix (`src/game/setup.c`): replaced catalogIdByRuntime lookup with a catalog scan matching `ext.weapon.weapon_id == mpw` (same pattern as `savefile.c:816` / `buildSpawnWeaponList`). Skip MPWEAPON_NONE/SHIELD/DISABLED. Always override (not just if empty) so a stale out-of-set spawn weapon from a prior match gets corrected. Re-derive `spawnWeaponNum = catalogGetMpWeaponNum(mpw)` immediately so the current match respects the fallback.
- Left the other known-broken `catalogIdByRuntime(ASSET_WEAPON, ...)` callsites (netmsg.c:882/1684/4183, matchsetup.c:835 `matchGetWeaponSlotCatalogId`, netmanifest.c:453/767/1096) alone — those are a separate systemic issue (same root cause: registration stores `runtime_index = i` rather than `weapon_id`) and fixing them requires careful audit of each callsite's expected semantics (MP weapon slots vs. solo intro WEAPON_* constants). Out of scope for B-181 specifically.
- Build clean 774/774. `PerfectDark.exe` 53,195,903 / `PerfectDarkServer.exe` 23,140,832.

---

## Done — 2026-04-19 (S372 — B-186 team-color alignment + B-184 dead-code cleanup, `claude/xenodochial-tereshkova-c14bad`)

- **B-186** — Two Teams rendered enemies YELLOW instead of BLUE. Root cause: `src/game/radar.c::g_TeamColours[]` still used the PD-native order (0:Red, 1:Yellow, 2:Blue, ...) while every menu palette (`pdgui_menu_room.cpp`, `pdgui_bridge.c`, `pdgui_menu_pausemenu.cpp`, `pdgui_menu_endscreen.cpp`, `pdgui_menu_mpingame.cpp`) uses the modern 0:Red, 1:Blue, 2:Green, ... ordering. `applyTwoTeams` / `applyHumansVsSims` assign team=0 to humans and team=1 to sims — the room menu showed "Blue" but radar + chr tint + scenario coloring all read `g_TeamColours[1]` = Yellow.
- Fix: reordered `g_TeamColours[]` and the mirror `teamcolours[]` in `src/game/activemenu.c` to Red/Blue/Green/Yellow/Orange/Purple/Grey/White. Replaced langbank `L_OPTIONS_008 + i` lookups in `mpSetDefaultNamesIfEmpty` / `mpSetTeamNamesToDefault` / `mpGetTeamsWithDefaultName` (`src/game/mplayer/mplayer.c`) with a new static `kDefaultTeamNames[]` so fresh boss files get the right names. Re-aligned `port/fast3d/pdgui_menu_teamsetup.cpp::s_TeamColors` + `s_TeamNames` (which had their own Red/Blue/Yellow/Green ordering — yet a third variant) to match. All seven palettes + names now agree.
- **B-184** — investigated without finding an active rainbow-normal source. Confirmed the "SAVED EFFECT: Normal Tint" block in `gfx_pc.cpp` (lines 1253-1276) was fully commented out and the `meshDebug` system's `s_DebugMode` is never written (F9 toggle only logs). Removed the dead SAVED EFFECT block as cleanup. Moved bug to `INVESTIGATED-NO-SMOKING-GUN` with audit notes and the fingerprint questions the next playtest log needs to answer.
- Build clean 774/774. `PerfectDark.exe` 53,214,883 / `PerfectDarkServer.exe` 23,142,880.

---

## Done — 2026-04-19 (S371 — B-179/B-180 head modeldef parts=0 fix, `claude/sweet-chandrasekhar-7d64da`)

- **B-179** — 22/32 bots were defaulting to mphead=0 (Joanna Dark) in MP. Six heads load with parts=0 (head_dark_snow, head_ddshock, head_carrington, head_ddsniper, head_president, head_cassandra) but have valid rootnode+skel.
- Root cause: `validateModeldef` in `port/src/modelcatalog.c` was the remaining `numparts <= 0 ⟹ INVALID` guard that B-166 (`modeldefLoad`) and B-167 (`propobj.c` walkers) didn't propagate to. Each `catalogValidateHeadId()` call (SVC_STAGE_START paths at `netmsg.c:1366/1421/4792`) hit `catalogGetSafeHead → CATALOG_FALLBACK_HEAD` and downgraded the head to dark_combat.
- Fix: `validateModeldef` now only rejects `numparts <= 0` when `category == MODELCAT_BODY`. Heads are allowed through with parts=0 — rendering walks rootnode DL nodes (not parts[]), `modelAttachHead` only needs rootnode, `modelGetPart` already returns NULL on parts=0 and all callers tolerate that.
- Bodies remain strict: body0f02ce8c iterates parts[] for skeletal body+head merge, and falling back to dark_combat is the correct UX for torn bodies.
- **B-180** (missing necks) resolves as a cascade — the neck geometry is part of each head's own rootnode mesh. Fixing the head fallback restores the correct head, and therefore its neck, instead of putting Joanna Dark's head+neck on a mismatched body.
- Build clean 774/774. `PerfectDark.exe` 53,202,559 / `PerfectDarkServer.exe` 23,141,856.
- Merged to dev as `d6d3e01b`.

---

## Done — 2026-04-18 (S369 — Solo pause menu input context fix, `claude/nice-jackson-62879e`)

- **B-171** — tester log showed `MENUPOOL: acquired solo_mission_pause ctx=none(shared)` with NO matching `INPUTCTX: imgui_menu on_push`. Solo pause was drawing over live gameplay input: menu bindings inactive, mouse stuck in relative mode ("invisible mouse" unless holding RMB), child Abort-Mission confirm dialogs unresponsive.
- Root cause: `renderPauseMenu` in `pdgui_menu_solomission.cpp` was missed in the S300 pool-owned-ctx migration — never called the second `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` on `IsWindowAppearing` to attach the ctx to the live pool slot. Every other ImGui renderer does this.
- Fix: canonical two-line addition in IsWindowAppearing — `pdguiPlaySound(PDGUI_SND_OPENDIALOG)` + `menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu)`. Pool release on menuPopDialog cleans up automatically.
- Also tester: CI "End Game" reported broken in a prior session. S368 BeginPopupModal rewrite should fix; if it still breaks, need a fresh log.
- Build clean 2/2 incremental.

---

## Done — 2026-04-18 (S368 — Controller navigation + scrollbar sweep, `claude/nice-jackson-62879e`)

- **Cross-panel controller nav fix** — `pdguiBeginActionBar` now uses `ImGuiChildFlags_NavFlattened`; action bar buttons (Back / Save / Cancel) reachable from body via D-pad. Systemic across every dialog using the action bar primitive.
- **NavFlattened on body containers** — ~25 `BeginChild("##xxx_body", ImVec2(0, bodyH), false, NoBackground)` calls across mppause / mpadvanced / mpsetup / botsetup / cheats / mpsettings / mainmenu / solomission / playerconfig / challenges / warning flagged. Nav crosses body → action bar freely.
- **End Game modal popup** — `renderMpEndGameDialog` rewritten on `ImGui::BeginPopupModal`; the popup owns input exclusively so Cancel + End Match are always reachable on controller even while MP Pause Control sits behind. Scrim + PD-red danger frame + keybinding hint row preserved; `CloseCurrentPopup` + `menuPopDialog` fire together on dismiss.
- **Scrollbar visibility (global theme change)** — `pdgui_style.cpp`: ScrollbarSize 12 → 18, GrabMinSize 10 → 14, ScrollbarBg alpha 0x87 → 0xCC, ScrollbarGrab/Hovered switched to accent `dialog_border2` at 0xE0 / 0xF5. All themes inherit.
- Build clean 774/774. `PerfectDark.exe` 53,170,487 / `PerfectDarkServer.exe` 23,138,784.
- Deferred: BeginPopupModal rewrite for other DANGER dialogs (`g_ExitGameMenuDialog`, cheat warning/confirm) — drop the pattern in when next controller-only audit flags them.

---

## Done — 2026-04-18 (S364 — Memory floor check, `claude/hungry-heisenberg-f18ad5` → dev `b998ec40`)

- `mempSetHeap`: `sysFatalError` if `heaplen < 60 MB` — fires before CARVE, shows clear "Delete pd.ini" message.
- `configRegisterInt("Game.MemorySize")` min: 4 → 64 — clamps stale values on next config write.
- Build clean 780/780.

---

## Done — 2026-04-18 (S363 — Standalone updater UX fixes, `claude/crazy-varahamihira-82fe52` → dev `c957fb62`)

Three fixes to `port/src/updater_standalone/updater_gui.c`:
- **Auto-check on launch**: `PostMessage(IDC_BTN_CHECK)` from `runGui()` after `createControls` — release list populates without manual click.
- **Filter without re-fetch**: Full list in `allReleases[]`; `filterReleases()` applies `showDevReleases` in the UI; checkbox toggle re-filters only, no network call.
- **Button layout**: `AdjustWindowRect` from `WINDOW_CLIENT_H=572` replaces hardcoded 580 — Update/Close buttons no longer clipped.

Build clean 13/13. `Updater.exe`, `PerfectDarkServer.exe`, `PerfectDark.exe` all linked.

---

## Done — 2026-04-18 (S362 — Release pipeline local-testability + Updater.exe bundle + Dev-release prune, `claude/nervous-satoshi-763192` → dev `4260a1fd`)

Three fixes to `devtools/release.ps1`:

- **ROM copy after client build.** `Copy-RomAddinIntoBuild` mirrors `dev-window-v2.ps1::Copy-AddinFiles`; runs right after `pd` links (and at the top of the `-SkipBuild` path), so `Build/data/pd.{ROMID}.z64` is in place before server/updater/push/gh steps that might fail. Mike can launch `Build/PerfectDark.exe` locally even on a failed release.
- **Updater.exe in the release bundle.** `pd-updater` added to the rebuild loop (optional — failure warns, doesn't block); `-SkipBuild` path builds incrementally if missing. Staged + zipped + uploaded as individual GitHub release asset with SHA-256 sidecar.
- **Rolling Dev-release prune (Step 6).** After a successful prerelease publish, `gh release list --json` → filter `isPrerelease=true, isDraft!=true` → keep newest 10 → delete rest with `gh release delete --cleanup-tag` (fallback to `gh api -X DELETE`). Stable releases never touched.

Step numbering bumped to `/8`. PS7 parser clean; `pd-updater` builds successfully from the parent project (`Build/Updater.exe`, 12.8 MB, zero-DLL static link).

**Caveat**: `-DryRun` does not suppress the `-SkipBuild` pre-release commit+push step — pre-existing quirk, left in place. Worth auditing later if it bites someone.

---

## Done — 2026-04-18 (S361 — Dev Window v2 async RunspacePool, dev direct `7d6ec8fb`)

Dev-tool only — no game code change. Release pipeline produced v0.0.120.

- Three separate `Add-Type -Language CSharp` calls consolidated into one guarded block (avoids triple cold-compile on startup).
- Persistent `BgPool` RunspacePool (1–3 threads, ReuseThread apartment) added at startup, disposed on window close.
- `Update-StatusBar` (2 s `MainTimer`) reuses `BgPool` instead of creating a fresh runspace every tick — kills the ongoing UI stutter.
- `Populate-DocList` scans `context/` + `docs/` (~170 files) on `BgPool`; `Loaded` event no longer blocks first paint.
- New `Start-AsyncPoolAction` helper wraps `Invoke-GitPull` / `Invoke-GitPush` / `Invoke-PruneWorktrees` and the Check button.
- `Toggle-Server` / `Toggle-Game` pre-update button text + log line; `UseShellExecute=$false` skips Shell32 lookup; errors surface in a MessageBox.

---

## Done — 2026-04-17 (S360 — Updater Mozilla CA bundle fix, `claude/blissful-curie-2387ee` → merge `3da8191c`)

**Build verified.** Release pipeline produced v0.0.119.

Testers logged `SSL peer certificate or SSH remote key was not OK` on every update check. Root cause: MSYS2's statically-linked `libcurl.a` is built against OpenSSL **without** winstore integration — `CURLSSLOPT_NATIVE_CA` compiles but is a runtime no-op, and no external CA file ships with the exe.

- New `port/src/cacert.pem` (223,837 bytes, copied from mingw64 `ca-bundle.crt`).
- `CMakeLists.txt` generates `${BINARY_DIR}/port/include/cacert_blob.h` via `file(READ ... HEX)` + `REGEX REPLACE` at configure time.
- `updater.c`: new `curlSetupTLS()` helper replaces the two inline SSL blocks in `curlGet()` and the file-download path. Passes `CURLOPT_CAINFO_BLOB` with the embedded bundle. `CURLSSLOPT_NATIVE_CA` kept as harmless secondary.

Zero-DLL compliant. Independent of whatever cert store exists on the tester's machine.

**Verify**: Launch the updater on a fresh Windows install with no user CA store — update check succeeds over HTTPS.

---

## Done — 2026-04-17 (S359 — B-163 secondary crash-site guards, `claude/determined-austin-d54582`, commit `5122a663`)

**Build verified.** Clean 774/774. Release pipeline produced v0.0.118.

S317+S318 addressed the door-creation AV at PC+0x161258. Tester logs on build `acdf4062` confirmed a **second** AV at PC+0x15f83a along the prop/chr-creation path during stage 0x26 setup. `setupCreateObject` had four unguarded `obj->model->scale` dereferences after `setupLoadModeldef` could return NULL via the FIX-B.2 reject path.

FIX-B.2 pattern applied to every remaining unguarded site:
- `src/game/setup.c::setupCreateObject` — early return with WARNING if `g_ModelStates[modelnum].modeldef == NULL` after `setupLoadModeldef`.
- `src/lib/model.c::modelAllocateRwData` — defensive NULL/rootnode guard; all callers inherit crash-proofing.
- `port/src/net/netmsg.c:2213` — guard `laptopDeploy` NULL return before dereferencing `obj`.
- `src/game/body.c::bodyAllocateModel` — guard `headmodeldef` NULL in both random-head and specific-headnum paths.

B-163 bugs.md entry already captured this under "S327 addendum (magical-zhukovsky)" label — matches `5122a663` fix description exactly.

---

## Done — 2026-04-17 (S358 — B-161 title/intro model NULL-guards, dev direct, commit `be0935da`)

**Build verified.** Release pipeline produced v0.0.117.

With `modeldefLoad` now rejecting torn modeldefs at load time (S323 root-cause fix), the title/intro screens needed matching NULL-handling so a rejected logo model could no longer AV on subsequent frames.

- `titleInitNintendoLogo` / `titleInitRareLogo` / `titleInitPdLogo` handle `modeldefLoad()` returning NULL — cascade to next intro mode or SKIP.
- Exit/render paths guard against NULL `g_TitleModel*`.

Completes the B-161 defensive sweep — every path from `modeldefLoad` root chokepoint through `setupCreateDoor` / `setupCreateObject` / `modelAllocateRwData` / `body.c::bodyAllocateModel` / `netmsg.c::laptopDeploy` / `title.c::titleInit*Logo` now converts AV into a WARNING log line.

**Verify**: Cold-boot on a system with a deliberately torn intro logo modeldef — no AV; log shows `TITLE: intro model load failed — skipping to next mode`.

---

## Done — 2026-04-17 (S356 — Forge Door Lifecycle + D5 Phase 5C+5D, `dazzling-vaughan-204b7c`)

**Build verified.** Clean 776/776 (full rebuild on dev). Zero errors.

- **Forge doors**: `s_spawn_door()` in forge_runtime.c builds live `doorobj` from catalog modeldef — pool of 16 from MEMPOOL_STAGE, slide-vector computation (yaw-aware), DOORFLAG_0080/AUTOMATIC, propActivate/Enable. `forgeRuntimeFindDoorByUid()` in forge_runtime.h.
- **OPEN_DOOR/CLOSE_DOOR**: forge_logic.c now calls `doorsRequestMode(door, DOORMODE_OPENING/CLOSING)` via `forgeRuntimeFindDoorByUid`; data-only fallback if no live doorobj.
- **Phase 5C**: Hover on human lobby row → tooltip with live charpreview FBO; falls back to baked thumbnail or player name. FBO suppressed in baking pipeline while hover active.
- **Phase 5D**: Drop shadow on portrait thumbnail; team-color tinting on portrait border when teams on.

---

## Done — 2026-04-17 (S355 — Wave 5 Cross-Audit: S352 + S353 + S354, `goofy-pike-572f8a`)

**Build verified.** Clean 774/774 (worktree) + 4/4 incremental on dev post-merge. Zero errors.
`PerfectDark.exe` 52,810,516 / `PerfectDarkServer.exe` 22,925,709.

S352 and S354: CLEAN — no bugs found across all 10 audit items.
S353 bug fixed: `NET_PROP_DIRTY_MAXSYNCID` raised from 512 → `NET_PROP_MAP_SIZE` (2048) in `port/src/net/netmsg.c`. Props at slots 512–2047 were silently skipping dirty marks, preventing the 120-tick heartbeat CRC from firing on large stages after events on those props. Static array grows 512→2048 bytes; no wire format change; no protocol bump.

---

## Done — 2026-04-17 (S354 — Forge Runtime Wire-In: Props, Weapons, Geometry, Doors, Zones, `practical-varahamihira-3b5f5d`)

**Build verified.** Single file change: `port/src/forge/forge_runtime.c` (+358/-27).

Wired the remaining Forge (The Grid) object types into the live engine:
- **WEAPON_PAD**: `s_spawn_weapon_pad()` — catalog resolve → `weaponCreate` → `func0f08ae0c` → `modelSetScale(1.0f)` → `setup0f0923d4`.
- **PROP / GEOMETRY / INTERACTABLE**: `s_spawn_prop()` — catalog resolve → `objInit` from MEMPOOL_STAGE pool → `modelSetScale(1.0f)` → `setup0f0923d4`. Collision auto-generated from model bbox.
- **ZONE**: `s_register_zone()` stores in `s_zone_rt[]`; per-tick edge-triggered enter/exit in `forgeRuntimeTick` fires `forgeChannelSet` + `forgeLogicFireEvent`. Teleporter type directly sets player position.
- **DOOR**: Catalog entries spawn as static props (visual only). Full `doorobj` pool lifecycle deferred to separate session.

New state: `s_prop_pool` (MEMPOOL_STAGE, 64 slots), `s_prop_count`, `s_zone_rt[128]`, `s_zone_count`.
New includes: `game/propobj.h`, `game/modeldef.h`, `game/setuputils.h`, `lib/model.h`, `lib/memp.h`.

---

## Done — 2026-04-17 (S353 — Prop Sync Event-Driven + Killfeed Verification, `pedantic-saha-8d7ff0`)

**Build verified.** Clean 585/585 (dev). `PerfectDark.exe` 52,772,152 / `PerfectDarkServer.exe` 22,924,028.

- **Prop sync dirty flags**: `s_PropDirtyFlags[512]` + `s_PropDirtyCount` in `netmsg.c`. Each SvcProp*Write marks dirty; 120-tick heartbeat skips entirely if nothing dirty (O(1) vs O(N_props)).
- **Killfeed bot kills**: verified working — no code change needed. All `ampchr && vmpchr` combinations fire `pdguiKillfeedPush`; roadmap entries marked DONE.

---

## Done — 2026-04-17 (S352 — D5 Phase 5: Lobby Player Portraits, `confident-brahmagupta-a3f5a3`)

**Build verified.** Clean 774/774 (worktree) + 585/585 (dev post-merge), zero errors.
`PerfectDark.exe` 52,770,947 / `PerfectDarkServer.exe` 22,922,823.

D5 Phase 5 portrait system wired into `pdgui_menu_room.cpp` (+276 LOC / -43 LOC):

- **Per-slot portrait baking pipeline**: `LobbyPortrait` struct + `s_LobbyPortraits[8]` (baked GL textures via shared charpreview FBO, one per frame, bot-modal guarded).
- **Human row overhaul**: row height 50px; portrait thumbnail (44px) left-aligned; baked texture with Y-flip UVs or initials circle fallback; state badge dot (yellow/green/blue/grey); name + role badge on line 1, body name + state text on line 2.
- **Join fade-in**: `s_LobbyPortraitAlpha[]` ramps 0→1 over ~25 frames per slot.
- **Lifecycle**: reset on every room open (`IsWindowAppearing`) and `pdguiRoomScreenReset`; portrait invalidated when player's body/head IDs change.
- **Solo mode**: baking skipped; initials placeholder; instant alpha.

---

## Done — 2026-04-17 (S353b — Killfeed Network Broadcast + Prop Snapshot Supplement, `quizzical-murdock-95eb09`)

**Build verified.** Clean 774/774 objects, zero errors.

Follow-up to S353. **S353b** shipped ahead of S353 primary's dirty-flag rewrite (superseded by S353/S355).

- **Prop sync**: Replaced CRC polling (`netPropSyncChecksum` + `SVC_PROP_SYNC` write) with per-prop `{hidden, damage}` snapshot dirty detection. Server now only triggers `NET_RESYNC_FLAG_PROPS` when a prop actually diverged. `SVC_PROP_SYNC` read handler still consumes bytes for old-server compat.
- **Killfeed bot kills**: Fixed gap where `mpstatsRecordDeath` never broadcast kill events to network clients. `netDistribSendKillFeed` now called from both suicide and normal kill paths (server only). `SVC_LOBBY_KILL_FEED` extended to reach `CLSTATE_GAME` clients. Client-side read handler now calls `pdguiKillfeedPush` with team lookup from `g_MpAllChrConfigPtrs[]`.

---

## Done — 2026-04-17 (S351 — D5 Phase 4: UI Texture Mod Overrides, `dazzling-heisenberg-f84acc`)

**Build verified.** Clean 585/585 objects, zero errors.
`PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823.

New API in `port/include/pdgui_theme.h`:
- `pdguiThemeScanModUiTextures(mod_dir)` — parse mod.json `"type": "ui"` components; register `catalog_id`/`path` overrides via `s_registerModTexture`.
- `pdguiThemeApplyEnabledModUiTextures()` — apply overrides from all enabled mods.

Wired into `pdguiThemeLateInit()` (after base textures load) and `modmgrApplyChanges()`
(after chrome rescan). Two-pass cjson parser handles key-order independence.

---

## Done — 2026-04-17 (S350 — Wave 3 Cross-Audit: S348 Discord, `vigorous-benz-f8cb68`)

**Build verified.** Clean 776/776 objects, zero errors (includes S351 sources). `PerfectDark.exe` 52,759,787 / `PerfectDarkServer.exe` 22,922,823. Note: S351's `pdgui_theme.cpp` had a latent GCC stray-'#' error (single-line `extern "C" { #include }`) — fixed inline during build verify.

1 bug fixed in `port/src/discord.c`:
- **JSON injection in SET_ACTIVITY payload**: `details`/`state` strings inserted raw via `%s` into JSON. A mod stage slug containing `"` or `\` would corrupt the pipe frame. Fix: added `disc_json_str()` escape helper — escapes `\` and `"` before both `_snprintf` branches in `disc_send_activity`.

All other audit items confirmed clean: IPC protocol, PIPE_NOWAIT handling, fail-silent reconnect, thread safety, memory management, MinGW compatibility, dedicated server exclusion.

---

## Done — 2026-04-17 (S349 — Cross-Audit S346+S347, `cool-poitras-b287e7`)

**Build verified.** Clean 774/774 objects, zero errors.
`PerfectDark.exe` 52,861,458 / `PerfectDarkServer.exe` 22,907,957.

**S346 (AP Phase 4) audit — CLEAN:** `assetprovider_internal.h` included by exactly 5 allowed
callers; no game code calls `romProviderHandle()`; stage handle fields populated with `fileid > 0`
guard; `assetHandleIsNull()` used correctly in `assetLoadToNew`; `catalogGetBodyHandle` /
`catalogGetHeadHandle` / `catalogGetPropHandle` all null-guard with `memset + CATALOG-FATAL + g_CatalogFailure`
pattern; deprecated SA-5a bridge functions marked `[DEPRECATED]` + `[MIGRATION BRIDGE]`.

**S347 (blue tint sweep) audit — 2 missed literals fixed:**
- `pdgui_menu_moddinghub.cpp:2109` chrome-tool preview border `IM_COL32(90,120,170,220)` → `pdguiImU32TitleGlow(220)`
- `pdgui_menu_agentcreate.cpp:324` body-name label `IM_COL32(140,160,200,180)` → `pdguiImU32TintInfo(180)`

Special Agent badge `IM_COL32(80,160,255,255)` in `pdgui_menu_solomission.cpp` intentionally left
(semantic difficulty color, not a PD accent).  Forge HUD blue/cyan literals are editor-mode colors,
not candidates for theme theming.

---

## Done — 2026-04-17 (S348 — D7 Discord Rich Presence, `ecstatic-cartwright-11459d`)

**Build verified.** Clean 774/774 objects, zero errors.
`PerfectDark.exe` 52,883,797 / `PerfectDarkServer.exe` 22,906,762.

D7 implemented as a thin Windows IPC client — no external library, zero new DLL
dependencies.  New files: `port/src/discord.c` + `port/include/discord.h`.  Wired
into `port/src/main.c` (init/shutdown) and `port/src/pdmain.c` (tick).

Presence states: Main Menu / Solo Mission (stage + difficulty) / Combat Simulator
(stage + scenario + counts) / Co-op / Counter-Op / The Grid editor / Lobby /
Dedicated Server.

**Setup required before presence appears in Discord:**
1. Register app at https://discord.com/developers/applications
2. Copy Application ID → replace `"0"` in `port/include/discord.h` → `DISCORD_APP_ID`
3. Upload art assets in Rich Presence → Art Assets tab:
   `pd2_logo`, `icon_solo`, `icon_combat`, `icon_coop`, `icon_counterop`, `icon_forge`

**Playtest items:**
- Launch PD2 with Discord open — verify presence shows "In Main Menu".
- Start a solo mission — verify presence shows stage name + difficulty.
- Start a Combat Simulator match — verify presence shows stage + scenario + counts.
- Close Discord mid-session — verify game does not crash or log spam.
- Reopen Discord — verify presence reconnects within 30 seconds.

---

## Done — 2026-04-17 (S347 — Blue Tint Sweep + Gamepad Audit, `gracious-poitras-6eeeb6`)

**Build verified.** Clean 773/773 (worktree) + 775/775 (dev post-merge). Merge commit to dev.

**Task 1 — Blue tint sweep (6 files, 13 sites):** `pdgui_menu_modmgr.cpp`, `pdgui_menu_moddinghub.cpp`, `pdgui_menu_agentselect.cpp`, `pdgui_menu_agentcreate.cpp`, `pdgui_countdown.cpp`, `pdgui_menu_mainmenu.cpp` — all hardcoded `IM_COL32` PD-blue accent literals replaced with `pdguiImU32TintInfo` / `pdguiImU32TitleGlow` / `pdguiPalImU32(PDPAL_TITLEBG,…)` accessors.

**Task 2 — ImGuiKey_Gamepad audit:** No dead checks found. M0.2 (S181–183) already removed the ~130 redundant gamepad key checks. The 3 remaining `AddKeyEvent(ImGuiKey_Gamepad*,false)` calls in `pdgui_menu_mainmenu.cpp` are B-131 input-flush fixes and must stay.

---

## Done — 2026-04-17 (S346 — Asset Provider Phase 4, `crazy-wilbur-ae5c6d`)

**Build verified.** Clean 773/773, zero errors. `PerfectDark.exe` + `PerfectDarkServer.exe` linked.

`romProviderHandle()` retired from all game code — now internal to catalog/provider layer only.

Changes:
- `port/include/assetprovider_internal.h` — new internal header for `romProviderHandle`/`romProviderFilenum`
- `port/include/assetprovider.h` — removed `romProviderHandle`/`romProviderFilenum` from public API
- `port/include/assetload.h` + `port/src/assetload.c` — new `assetLoadRomToNew()` for game code
- `port/include/assetcatalog.h` — 5 stage handle fields in `catalog_stage_result_t`; `catalogGetBodyHandle`/`catalogGetHeadHandle`/`catalogGetPropHandle` declared; SA-5a filenum fns demoted to `[MIGRATION BRIDGE]`
- `port/src/assetcatalog_api.c` — stage handle population; 3 new handle accessor impls
- `port/src/assetcatalog_base.c` + `assetcatalog_base_extended.c` — use `assetprovider_internal.h`
- `src/game/file.c` — `fileLoadToNew` calls `assetLoadRomToNew`
- `src/game/modeldef.c` — calls `assetLoadRomToNew`
- `src/game/lang.c` + `langreset.c` — reverted to `fileLoadToNew` (lang file IDs are runtime-computed)
- `src/game/setup.c` — uses `stage.setup_handle`/`mpsetup_handle`/`pads_handle`
- `src/game/tilesreset.c` — uses `stage.tile_handle`

**Pending (Phase 5 scope)**: Migrate SA-5a deprecated bridge calls (`catalogGetBodyFilenumByIndex` etc.) to handle-based model load APIs — requires `modeldefLoadByHandle` overloads.

---

## Done — 2026-04-17 (S344 — Audit S339+S340, `optimistic-mcclintock-47fc8d`)

**Build verified.** Clean 4/4 objects, zero errors. Merge commit to dev.

4 bugs fixed:
- `server_gui.cpp`: Stage ID InputText width reserves room for Apply button when dirty
- `server_gui.cpp`: Force Start button also requires `roomGetActiveCount() > 0`
- `server_gui.cpp`: Ban button tooltip clarifies no IP block (same as kick currently)
- `pdgui_menu_theme_editor.cpp`: `renderLivePreview` cursor height uses full drawn dialog height (`headerH + 6*scale`)

---

## Done — 2026-04-17 (S340 — Content-Inset Sweep, `elated-lichterman-9c8f69`)

**Build verified.** Clean 585/585, zero errors. Merge commit `56338aaa` on dev.

7 `pdgui_menu_*.cpp` files patched with `pdguiSetCursorBelowTitle`:
- audiomod, logviewer, moddinghub, modmgr, theme_editor, update: added call
- mpingame: decl only (pill windows manage their own padding)
- forge: no-op shim, no render sites — skipped

---

## Done — 2026-04-17 (S339 — R-5 Server GUI Redesign, `thirsty-jemison-84f6ce`)

**Build verified.** Clean 252/252 server, 521/521 game. Zero errors.
`PerfectDark.exe` 52,773,095 / `PerfectDarkServer.exe` 22,905,738.

Redesigned `port/fast3d/server_gui.cpp` and extended `port/src/server_bridge.c`.

New layout:
- **Status bar**: uptime HH:MM:SS, tick Hz, memory MB, player count, room count, connect code.
- **Players tab**: 6-column table (Name/State/Ping/Team/Kick/Ban). New `netServerBanClient` bridge.
- **Rooms tab**: hub state summary + 6-column room table (ID/Name/Players/State/Stage/Scenario).
- **Operator tab**: match control (game mode, stage ID input, scenario, force start/end) + server control (shutdown, restart-on-update).
- **Updates tab**: unchanged.
- **Log panel** (bottom): filter row [All][NET][ERROR][WARN][CHAT][HUB] + auto-scroll toggle.

Bridge additions: `netServerBanClient`, `serverGetMemoryMB`, `serverGetStageId/Set`, `serverGetScenario/Set`.
`CMakeLists.txt`: `target_link_libraries(pd-server psapi)` for Windows memory query.

**Playtest items:**
- Launch PerfectDarkServer.exe — verify status bar shows uptime ticking, tick Hz ~60, memory MB.
- Connect a client — verify Players tab shows name/state/ping/team.
- Click a filter button in log panel — verify only matching lines shown.
- Operator tab: change stage ID + Force Start — verify log shows new stage_id.

---

## Done — 2026-04-17 (S338 — D-MEM M5+M6: separate pool regions + mutex, `ecstatic-bouman-5d30e4`)

**Build verified.** Clean 775/775, zero errors. `PerfectDark.exe` 52,677,661 / `PerfectDarkServer.exe` 22,919,068.

D-MEM now **fully complete** (M0–M6 + MEM-1/2/3). Changes:
- `src/lib/memp.c` — M5: PERMANENT/STAGE/POOL_8 each get dedicated address regions; M6: MEMP_LOCK/UNLOCK guards on all mutation functions
- `src/include/lib/memp.h` — `mempSetLockFns()` added
- `port/src/pdmain.c` — SDL_mutex registered with mempSetLockFns after mempSetHeap

Bug fix: `mempGetStageFree()` was reading expansion pool (never set up, always returned 0). Now reads onboard STAGE pool — fixes spurious modelcatalog ERROR on boot.

**Playtest:** stage transitions + multiplayer — no behavioral change expected, verify no crashes.

---

## Done — 2026-04-17 (S337 — Dev Window v2 improvements)

Three fixes to `devtools/dev-window-v2/dev-window-v2.ps1` (commit `4d117d67`, worktree `peaceful-williams-59ec2f`):

1. **Prune Worktrees button** — runs `git worktree prune -v`, shows result dialog, logs to Log tab. Button disabled during builds/releases. Worktree count shown in status bar (orange when >20).
2. **Progress bar ActualWidth fix** — `UpdateLayout()` called before `ActualWidth` reads in Start-Build and Start-PushRelease so the 12% git-sync fill actually renders.
3. **HUD "0%" consistency** — spinner path now prefixes `"0% - "` on LblProgressText, matching non-spinner path. Removed dead green background assignment on step transitions.

---

## Done — 2026-04-17 (S336 — Audit S329 B-12 + S332 Modeldef/Audio)

Two bugs fixed, merged to dev (`e1081911`):

1. **`pdguiPauseGetChrSlots` u32 truncation** — pause menu dropped bots 24-31 silently. Fixed: return `u64`, use `1ull<<i`.
2. **weapon modeldef NULL crash** — `player.c::playerChrInitialise` called `modelAllocateRwData(NULL)` on torn weapon mod asset. Fixed: NULL guard + WARNING.

Stale `chrslots` comment in `netmanifest.c` also cleaned up.

**Playtest items:** (1) Pause menu with 32 bots — verify count shows all 32. (2) Torn weapon mod — verify WARNING not crash.

---

## Done — 2026-04-17 (S335 — Wave 1 audit: S328 + S331, `quirky-mcnulty-60c44a` worktree)

**Build verified.** Clean 773/773, zero errors. `PerfectDark.exe` 52,768,999 bytes, `PerfectDarkServer.exe` 22,887,046 bytes.

Three fixes:
1. `port/src/net/netmanifest.c` — `s_manifestAddWeapon` now emits `LOG_WARNING` when a weapon has a catalog ID (`wcan != NULL`) but the entry is missing, matching the body/head helper behavior.
2. `port/src/main.c` — Added `statsShutdown()` to `cleanup()`; stats are now flushed to disk on normal game exit regardless of whether a match finished.
3. `src/game/lv.c` — Distance stat keys renamed from `"distance_units"` + `"mp.distance_units_sample"` / `"solo.distance_units_sample"` to `"mp.distance_units"` / `"solo.distance_units"` — consistent with all other namespaced stats.

**Playtest items:** solo walk then X-quit → `playerstats.json` has `solo.distance_units`; no `distance_units` or `_sample` keys.

---

## Done — 2026-04-17 (S334 — Audit S327 Asset Provider + S330 Memory)

Fixed one bug: `pak.c:pak0f11d9c4` malloc null-check (`5773c465`). S327 and S330 otherwise clean.

---

## Done — 2026-04-17 (S333 — Merge S329 + S332 into dev)

**Build verified.** Clean 585/585, zero errors. `PerfectDark.exe` 52,660,473 bytes, `PerfectDarkServer.exe` 22,901,400 bytes.

Two worktree branches merged into `dev`:
- `exciting-meitner-bc8c70` (S329, B-12 chrslots removal + protocol v37) — merge commit `37bdb4b6`
- `magical-mahavira-f3726f` (S332, B-161 modeldef chokepoint + B-141 audio pacing) — merge commit `12170710`

Context conflicts in `session-log.md` and `tasks-current.md` resolved by keeping both sets of content chronologically.

---

## Done — 2026-04-17 (B-12 Phase 3 — remove chrslots, protocol v37, `exciting-meitner-bc8c70` worktree)

**Build verified.** Clean link [474/474], zero errors. Wire format is a breaking protocol change.

Removed the legacy `u64 chrslots` bitmask end-to-end and made the participant
pool (`g_MpParticipants`) the sole source of match slot state. Constants
`BOT_SLOT_OFFSET`, `CHRSLOTS_PLAYER_MASK`, and `CHRSLOTS_BOT_MASK` are gone;
`MpParticipant.legacy_slot` is gone; the `mpParticipantsTo/FromLegacyChrslots`
shims are replaced with wire-only helpers `mpParticipantsEncodeActiveMask` /
`mpParticipantsDecodeActiveMask`. `NET_PROTOCOL_VER` bumped 36 → 37.

44 mpconfigs.c initializers updated (chrslots placeholder dropped). 60+
runtime callsites migrated: challenge.c, mplayer.c, mpscenarios, menutick.c,
menuitem.c, menu.c, mainmenu.c, ingame.c, setup.c, lv.c, pdmain.c, net.c,
netmsg.c, matchsetup.c, server_stubs.c, pdgui_bridge.c. The dedicated server
now links `participant.c` directly (slot state is no longer stubbed).

### Playtest verification

- **Combat Sim 4 humans + 32 bots** — host from the room screen, connect 3 remote
  clients, hit Start. All 4 humans + 32 bots spawn. Log shows `MATCHSETUP:
  activeMask=0x...` and `NET: Combat Sim setup: ... activeMask=0x...`.
- **Client-side bot spawn** — on any connected client, verify bots appear
  (`SIMULANT: spawning started activeBots=N maxsim=32`). Before the fix,
  `mpParticipantsFromLegacyChrslots` ran after the `chrslots` read; now the
  pool is decoded from the wire active mask in-place.
- **Challenges / quick-team sim** — go to Challenges, select a challenge that
  gifts bots. Bot difficulties and slot count match. `challengePerformSanityChecks`
  clears + rebuilds bot participants using the new API.
- **Save/load MP setup** — save a custom MP config with 16+ bots and reload it.
  Bot participants are re-added via `mpAddParticipantAt` purely from bot
  difficulty (chrslots storage is gone).
- **Pre-v37 client rejected** — connecting a pre-patch client to a v37 server
  should fail handshake with "protocol mismatch (got 36, expected 37)".

---


---

## Done — 2026-04-17 (S323 Batch H — FIX-B.1 deep manifest scanner discovery logging, `zealous-saha-02c1f5` worktree)

**Build verified.** Clean 768/768 link, both executables rebuilt. FIX-B.1 — the last open item of the Master Orchestration Plan — is now closed.

**What changed** (`port/src/net/netmanifest.c` only):
- `s_manifestAddBody/Head/Model` and new `s_manifestAddWeapon` helpers gained a `scan_source` parameter. Each helper pre-checks dedup via new `s_manifestHasEntry()`; when an entry is newly added, logs `MANIFEST-SP: <scan>-scan discovered <kind> '<id>' (<kind>num=N)`. Scan sources: `props` / `intro` / `ailist`.
- Body/head WARNINGs when a scan references a bodynum/headnum that doesn't resolve in the catalog (non-sentinel values only). Makes mod-character gaps visible instead of surfacing only as `manifestEnsureLoaded` late-adds with torn modeldefs (the S312 fingerprint).
- `manifestBuildMission` props loop migrated to the unified helpers; prop-object switch now calls `s_manifestAddModel(..., "props")` instead of inline `manifestAddEntry`. Removed unused `char id[64]` local.
- Per-phase counter snapshots + single summary line at end of `manifestBuildMission`:
  `MANIFEST-SP: scan stage=0x%02x joanna=N props+=M intro+=P ailist+=Q (total=T)`

Scanners already mirrored `stageLoadAllAilistModels` exactly (S298), so no functional coverage extension was needed — the gap was observability, not coverage.

Commit: `e029eec3` on `claude/zealous-saha-02c1f5`.

### Playtest verification

- **Solo Crash Site / Deep Sea / Villa (cinematic missions)** — tail `pd-client.log` during mission load. Expect a burst of `MANIFEST-SP: ailist-scan discovered body '...' (bodynum=...)` / `head '...' (headnum=...)` lines, followed by the summary `MANIFEST-SP: scan stage=0x<hex> joanna=2 props+=N intro+=P ailist+=Q (total=T)` with `ailist+=` non-zero.
- **Mod-character mission** — if a mod body/head is referenced by ailists but not in the catalog, expect new WARNING `MANIFEST-SP: ailist-scan body bodynum=N not in catalog`. Any such warning identifies a catalog-registration gap.
- **Late-add correlation** — any lingering `MANIFEST-SP: late-add '...' (missed by pre-scan)` lines should now also have a corresponding missing-discovery audit trail (either absent `<scan>-scan discovered` line for that asset, or a `bodynum not in catalog` WARNING).

---

## Done — 2026-04-17 (S326 — Asset Provider Phase 1 + 2, `jolly-booth-fb5419` worktree)

**Build verified.** Clean link 771/771, zero errors. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean.

**Phase 1 — Provider interface (zero behavior change):**
- New `port/include/assetprovider.h` defines `asset_data_handle_t` (opaque 128-bit payload + vtable pointer) and `asset_provider_t` vtable (`resolve_size` / `load` / `unload` / `describe`)
- New `port/include/assetload.h` declares `assetLoad` / `assetLoadToNew` / `assetUnload` / `assetDescribe` — the provider-aware dispatcher entry points
- New `port/src/assetprovider_rom.c` — `RomProvider` wraps the existing `romdataFileLoad` path; `opaque[0]` = filenum
- New `port/src/assetprovider_file.c` — `FileProvider` serves loose files via `fsFileLoad`; paths interned into a 32 KB pool so handles stay 128-bit regardless of path length
- New `port/src/assetload.c` — dispatcher; RomProvider fast-path delegates to `fileLoadRomToNew` (legacy body), generic path does `mempAlloc(MEMPOOL_STAGE) + provider.load`
- `src/game/file.c::fileLoadToNew` becomes a one-line wrapper: `return assetLoadToNew(romProviderHandle(filenum), method, loadtype);`. Original body moved to `fileLoadRomToNew` (declared in `src/include/game/file.h`) so the dispatcher avoids recursion. Every existing call site works unchanged.
- Server stubs added in `port/src/server_stubs.c` for the 6 provider entry points (return null handles; server never dispatches through the provider layer)

**Phase 2 — Catalog source descriptor + mod-override provider selection:**
- `asset_entry_t` gains an `asset_source_t source` field with `primary` / `override` handles + a `flags` bitmask
- New `catalogSetPrimary` / `catalogSetOverride` / `catalogClearOverride` / `catalogEffectiveHandle` API (`port/include/assetcatalog.h`, implemented in `port/src/assetcatalog.c`)
- Registration populates `source.primary` declaratively:
  - `assetcatalog_base.c` — every base body/head/sp entry binds to `romProviderHandle(source_filenum)`
  - `assetcatalog_base_extended.c` — base prop models bind to `romProviderHandle(g_ModelStates[i].fileid)` when `fileid > 0`
  - `assetcatalog_scanner.c` — mod characters with `bodyfile` bind to `fileProviderHandle(bodyfile)`
- `entryGetFilePath` in `assetcatalog_load.c` consults `source.primary` first: if it holds a FileProvider handle, the interned path wins over the type-specific `ext.*` fields. Legacy fallback retained for entries with no populated source.
- Net effect: the mod-override path that used to live inside `romdataFileLoad` now flows through declarative `asset_source_t` fields. `catalogResolveFile` still routes by reverse-index, but the file path it returns comes from the provider handle instead of a type-specific switch.

### Playtest verification

- **Base game cold boot** — launch PerfectDark.exe, boot to main menu. No new log errors. All character models, arenas, stages load.
- **Mod loading** — install any component mod that overrides a base character (e.g. any ASSET_CHARACTER with a bodyfile matching a ROM file name). Load CI Training. Verify the mod character body still loads from disk (not ROM). Log line `CATALOG: file N → mod override "..."` should still appear in `pd-client.log`.
- **Dedicated server** — launch PerfectDarkServer.exe. Boot should proceed normally; catalog registers with null provider handles (server has no ROM/disk assets). No crashes on catalog population.

### Follow-up (not in this session)

- Phase 3: migrate call sites from `fileLoadToNew(filenum, ...)` to `assetLoadToNew(handle, ...)` so the catalog becomes the sole entry to loads
- Phase 4: retire `filenum` from public catalog API once Phase 3 completes
- `FileProvider.load` inflate/preprocess path: currently the generic `assetLoadToNew` path skips rzipInflate + `romdataFilePreprocess`. Acceptable for Phase 2 because mod-override bytes still flow through the legacy `romdataFileLoad` pipeline that does its own preprocessing. Phase 3 migration will need to either inline the inflate/preprocess steps into `assetLoadToNew` or push preprocess responsibility to callers.

---

## Done — 2026-04-17 (S325 — D6 stats wire-in + ImGui subtitles, `xenodochial-mendel-93fca2` worktree)

**Build verified.** Clean link 769/769, zero errors. Two parallel tasks:

**Task 1 — D6 Persistent Stats gameplay wire-in complete**:
- MP matches: `mp.matches_won` / `mp.matches_lost` (in `mpCalculateAwards`, local players only)
- Time: `mp.time_played_seconds`, `solo.time_played_seconds`
- Solo missions: `solo.missions_completed`, `solo.mission_failures` (via `endscreenPrepare`, gated on !cheats !coop !anti !pdmode)
- Distance: `distance_units` + `mp.distance_units_sample` / `solo.distance_units_sample` (flushed per 10000 world units to avoid per-tick churn)
- Pickups: `items.picked_up` (always) + `items.keys_picked_up` / `ammo_crates` / `weapons_picked_up` / `shields_picked_up` (by object type)
- Doors: `doors.opened` (via `doorsCheckAutomatic` — player-triggered auto-open path)
- `statsSave()` called on match end and solo mission end
- D6 marked **DONE** in infrastructure.md

**Task 2 — Subtitle ImGui migration complete**:
- New `port/include/pdgui_subtitles.h` + `port/fast3d/pdgui_subtitles.cpp`
- New bridge `pdguiSubtitlesSnapshot` (pdgui_bridge.c) exposes active HUDMSGTYPE_INGAMESUBTITLE / HUDMSGTYPE_CUTSCENESUBTITLE entries as POD structs (no types.h exposure)
- Renders bottom-center panel (46 px margin), 560 px wide (scales with `pdguiScale`), semi-transparent rounded backdrop, 1-px drop shadow on text, text centered + word-wrapped
- Drawn on foreground drawlist so cutscene letterbox bars don't occlude
- `hudmsgsRender` now skips both subtitle types (ImGui is sole renderer); tick lifecycle (fade, audio-channel opacity) still runs in `hudmsgsTick`
- Hook point: `pdguiRender` in `pdgui_backend.cpp` after `pdguiHudRender`

### Playtest verification

- **Solo subtitle (in-game)** — start any SP mission that triggers an in-game subtitle (mission briefings, ambient dialogue). Text should appear at bottom-center with a dim rounded backdrop, NOT at the top.
- **Cutscene subtitle** — play through any mission with a cutscene. Subtitles render at bottom-center and are not occluded by the letterbox bars.
- **Font/theme sync** — change font or theme; subtitle panel should update immediately (uses active ImGui font + theme tint).
- **Stats persistence** — play a match, check `saves/playerstats.json` for new keys: `mp.matches_won`, `mp.time_played_seconds`, `items.picked_up`, `doors.opened`, etc.
- **Solo mission stats** — complete a solo mission, verify `solo.missions_completed` incremented; abort/die, verify `solo.mission_failures`.

---

## Done — 2026-04-17 (S324 — D-MEM M2 + M4; infrastructure.md M3 marked done)

**Build verified.** Clean 771/771, zero errors, both PerfectDark.exe and PerfectDarkServer.exe link clean. Changes:
- **M2 stack→heap** (3 files): `pak.c` `sp60[0x4000]` → malloc/free; `texdecompress.c` `texInflateZlib`+`texInflateNonZlib` scratch buffers → static; `menuitem.c` `alltext/headingtext/bodytext/wrapped[8000]` → static
- **M4 ALIGN16 no-op**: `constants.h` ALIGN16 macro changed from 16-byte-round-up to `(val)` — removes N64 DMA padding from all 119 call sites
- **M3 marked DONE** in infrastructure.md: IS4MB ternary collapse completed S317/S320/S322/S323A

---

## Done — 2026-04-17 (S323 Batch G — cross-audit gap fixes)

**Build verified.** Clean link 768/768, zero errors. 6 items fixed:
- **CRITICAL**: `audioNotifyEngineReady()` now called in `port/src/pdmain.c::mainProc()` after `sndInit()` — `g_AudioEngineReady` is now set at runtime; volume sliders are no longer permanent no-ops
- `playerResetLoResIf4Mb` empty stub deleted (body in player.c, declaration in player.h, call in vi.c behind `#if PAL`, call in playerreset.c)
- Dead `is4mb` local variable removed from hudmsg.c (decl, assignment, always-false arm of condition)
- Orphaned `#define MAX_SEQ_SIZE_4MB` removed from snd.c
- Dead `g_BgunGunMemBaseSize4Mb2P` global deleted (definition in bondgun.c, extern in data.h, extern in bondgunreset.c)
- Stale "takes effect on next restart" tooltip removed from pdgui_menu_mainmenu.cpp font panel

No playtest needed — audio volume fix is functional; rest is dead-code removal.

---

## Open — 2026-04-17 (S323 — B-161 + B-141 root-cause fixes, `magical-mahavira-f3726f` worktree)

### Playtest verification

**B-161 (modeldef torn-load reject at the single chokepoint):**
- **Mission 1 → Mission 2 seamless advance** — Defection → Next Mission → Investigation. Walk 1-2 minutes. No AV expected. If a modeldef was torn at load time, log now shows `MODELDEF: file %u loaded torn — parts=%d root=%p scale=%.3f -- rejecting` at ERROR (caller will handle the NULL as missing-asset via existing FIX-B.2 / S308 guards).
- **CI Training boot** — load CI Training normally. No AV; no new MODELDEF reject lines on healthy assets. Scale clamp WARNING (`... loaded with degenerate scale ... clamping to 1.0`) is acceptable for AllInOneMods models if they ship with bad scale.
- **MP arena transitions** — any stage with props: no modeldef-reject lines in log (all MP stage assets should load clean). If any fire, report the file id.

**B-141 (audio three-tier pacing):**
- **60-second arena play** — tail pd-client.log for `AUDIO[B-141]: 30s summary`. Expect `underruns=0` in a non-hitching run. `buffered(samples) min` should hover 2800-3000 (up from 900-1100). `drops` should stay 0 (queue won't hit the 8192-sample drop threshold).
- **Startup** — from a cold boot through title → mission load, audio should be audible essentially immediately. Under the old path, the queue took 36 seconds to fill from 0 to 1100; now it reaches 2500 in ~115ms (7 frames of fast-fill 736).
- **Stall recovery** — briefly hitch the main thread (alt-tab, heavy pause-menu render). Queue drains; after hitch ends, fast-fill kicks in automatically and restores cushion in ~1s.

---

## Open — 2026-04-17 (S323 Batch D+F — font atlas rebuild + legacy sidecar migration)

### Playtest verification

- **Font live swap** — open Settings → Interface tab → Font dropdown. Change to any installed font mod. Font should update immediately in the current session (no restart required). Change back to Handel Gothic — should also update immediately.
- **Legacy sidecar migration** — if any agents were created before S313, their old sidecar (named from raw N64 bytes, typically `prefs_default.ini` or a garbled name) should auto-migrate to `prefs_<display_name>.ini` on first Agent Select load. Log should show `PREFS: migrated legacy sidecar '...' -> '...'` if migration fires.

---

## Done — 2026-04-17 (S323 Batch A — IS4MB/IS8MB/STAGE_4MBMENU final cleanup, `admiring-mccarthy-38734a` worktree)

**Build verified.** `grep -rn "IS4MB|IS8MB|fourmeg2player|STAGE_4MBMENU" src/ port/` returns zero hits. Both `PerfectDark.exe` and `PerfectDarkServer.exe` link clean [770/770]. No playtest needed — pure dead-code removal with no runtime behavior change.

---

## Open — 2026-04-17 (S323 — menuPushRootDialog pool hygiene, `naughty-buck-8ede1d` worktree)

### Playtest verification

- **Cold boot into main menu** — open main menu, navigate to Settings, close.  Re-open main menu.  No "menuPushDialog rejected — pool slot already active" in log.  Menu is interactive on every open.
- **CI redirect at boot** — if CI Options dialog is pushed at boot (Settings via CI path), close it, return to main menu.  Watchdog in `menuPoolConsistencyCheck` should log nothing (previously logged stale-slot warning on the frame after menuPushRootDialog wiped the stack).
- **Pause → Main Menu transition** — pause in-game, return to main menu via "Exit to Main Menu".  Pool should be cleanly released before the root push.  No "pool slot already active" cascade on subsequent main menu opens.

---

## Open — 2026-04-17 (S323 — Audio channel routing enforcement)

### Playtest verification

- **Mod SFX override + GameplayVolume** — requires a mod with a sound override (`catalogResolveSound` returning `is_mod_override=true`).  Set GameplayVolume to 25%.  Trigger the overridden sound.  It should play at roughly 25% of full volume.  Without the fix it would play at 100% regardless.  Check `pd-client.log` for `CATALOG: sound %d → mod override` line confirming the WAV path was taken.
- **Per-agent audio isolation** — load Agent A, set MasterVolume to 50% via Settings → Audio. Quit to Agent Select (do not sign out — just press Escape from main menu to reach Agent Select). Screen should open and audio volumes should revert to pd.ini defaults (`AUDIO.DIAG` in log shows `audioNotifyEngineReady` baseline). Select Agent B (no custom prefs). Audio should stay at baseline, NOT at Agent A's 50%.
- **Agent A volumes reload on sign-in** — re-select Agent A.  MasterVolume should return to 50%.

---

## Open — 2026-04-17 (S322 — N64 legacy audit Tier 1/2)

### Playtest verification

- **Cold boot** — boot log clean (no IS4MB path taken, no audio init errors). Menu renders normally.
- **Audio** — music plays in multiplayer lobby. No audio crackling or voice-limit errors in log.
- **Multiplayer** — 2-player split-screen works. No crash from fourmeg2player removal or screensplit logic.
- **Stage loading** — load any gameplay stage; no crash from MEMP expansion pool change or model/texture count increases.
- **Menu blur** — menu blur effect renders (IS8MB guard was removed; `g_BlurBuffer` allocated unconditionally).

---

## Open — 2026-04-17 (S321 — N64 demo system stripped)

### Playtest verification

- **Cold boot** — boot log shows single `SP transition to 0x26`, no `0x30` reference anywhere. Boot path: logo sequence → CI Training, clean.
- **Mission cutscenes** — play to a cutscene completion. Mission should end normally (`func0000e990` path, not title-redirect). Music should resume after cutscene (previously guarded by `!g_IsTitleDemo`).
- **Defection mission** — playable normally via solo mission select. The STAGE_DEFECTION mission itself is unaffected.

---

## Open — 2026-04-17 (S320 — Agent Select default theme)

### Playtest verification

- **Agent Select shows base theme on open** — after playing as a custom-themed agent, return to Agent Select (Escape from main menu). The screen should render with `base:theme_blue` (default grey/blue PD palette), not the previously-active agent's custom theme.
- **Theme transition visible on sign-in** — select an agent with a custom theme. The moment the agent is signed in, the UI should visually transform to their theme.
- **Auto-load default agent** — if a default agent is configured (D key), Agent Select should immediately load that agent's theme on first open (no flickering required since it happens same frame).

---

## Open — 2026-04-17 (S319 — spurious boot transition fix)

### Playtest verification

- **Cold boot double-transition eliminated** — `titleInitRareLogo` no longer sets `g_IsTitleDemo = true`. Boot should no longer log `MAIN: replacing pending stage change 0x30 -> 0x26`. Verify the boot log shows a single `GAMELOOP.MANIFEST: SP transition to 0x26` on cold start.

---

## Open — 2026-04-17 (S318 — B-161 class bbox/modeldef NULL sweep)

### Playtest verification

- **Hoverbike dismount** — activate near a hoverbike; dismount should succeed normally. No regression.
- **Glass destruction** — shoot AI Villa Bot glass objects; shard spray should fire. If bbox is missing, object removes silently (no crash, no shards — acceptable).
- **Door interaction range** — approach and open doors in any SP/MP level normally; no input-eating regression.
- **Hoverbike/hoverprop collision** — hoverbike/hoverprop geo block built correctly; collides with walls as expected.
- **Chr bbox render** — no visual regression in character sorting / chr hitbox computation.

---

## Open — 2026-04-17 (S317 addendum — CI Training crash fix, dev direct)

### B-163: Playtest verification

**Status**: Crash guard landed. `setupCreateDoor` now returns early if modeldef NULL and uses identity scale if no bbox node. Build verified. Needs in-game test.

- **Launch CI Training** — game must reach gameplay without AV. If a door is missing, check log for `SETUP: door modelnum %d modeldef NULL` or `no bbox node` to identify which door
- **Double-transition 0x30 → 0x26** — still open: investigate what triggers stage 0x30 at boot and whether CI Training redirect is affected

---

## Open — 2026-04-17 (S316 — solo mission select UX, `epic-mirzakhani-884cc2` worktree)

### Playtest verification

- **Difficulty text in mission select** — launch solo mission list, select any mission. All three difficulty rows (Agent / Special Agent / Perfect Agent) must show their names even before the lang bank loads (fallback strings hardcoded).
- **Dark Agent row** — appears only after Skedar Ruins beaten on Perfect Agent. Selecting it and clicking Start Mission should open PD Mode settings dialog, not launch immediately.

---

## Open — 2026-04-17 (S315 — palette sweep completion, `pedantic-austin-4a93bc` worktree)

### Playtest verification

- **Theme-driven accents sweep verification** — install a custom theme with a wild `titleGlow` (e.g. pure magenta). Confirm the following now follow the theme accent instead of staying cyan:
  - **Lobby screen** — "Connected to dedicated server" banner; "In Match" / "in game" client state labels; ROOM_STATE_MATCH server browser entry
  - **Pause scoreboard** — "SCOREBOARD" header and local-player name row (cyan → theme accent)
  - **Room** — "in game" client state label, tab bar selected-tab overline, bot name in character preview panel, TabSelectedOverline
  - **Challenges** — "Completion:" section subheader
  - **Download overlay** — "Downloading: <component>" label in the distrib progress banner
- **Intentional non-theme colors remain** — verify these are still their original fixed colors, not theme-colored:
  - Solo Mission difficulty ring: Agent=green, Special Agent=blue, Perfect Agent=gold
  - Log viewer: `LOAD:` lines still blue (functional debug coloring)
  - Agent Select initials: still light blue `IM_COL32(200, 220, 255, 255)`

---

## Open — 2026-04-17 (S312 batch 2 — font/UI scaling + controller tab-cycle + border audit)

### Playtest verification

Three renderers touched; no user-visible regression expected.  Verify:

- **Input Mapping** (Settings → Controls → Keyboard & Mouse / Controller)
  on 1440p + 4K — search box, group headers, and row cells should look
  proportional; before the fix the box stayed at 260px regardless of
  display resolution.  Row height/padding at high DPI should no longer
  feel cramped (table cell/frame/item padding now scales).
- **Updates tab** (Settings → Updates) on 1440p + 4K — "Channel" combo
  width and horizontal spacing between the current-version text and the
  combo should scale with DPI.  Restart prompt's "Restart Now / Later"
  buttons should separate proportionally.
- **Mod Manager tab cycling** (Modding Hub → Mod Manager) with a
  controller connected — LB/RB should cycle Installed Mods → By Category
  → By Mod and back.  The three-tab bumper cycle should be round-robin.

### Follow-up queued from S312 batch 2

- ~~**Wire pdgui_glyphs into in-world prompts**~~ — **DONE (S323 audit confirmed)**:
  S311 wired `pdguiInteractPromptRender` → `pdguiDrawActionPromptCentered(ACTION_USE)` for all
  pickup/door/terminal/object prompts. S312 wired forge HUD via `pdguiDrawActionPrompt()`.
  Zero hardcoded `[E]`/`[A]` labels remain in runtime rendering code.
- **Font-atlas rebuild on runtime font swap** — atlas built once at
  `pdguiInit`; runtime swap needs a rebuild or early-pick to take
  effect without restart.
- **Theme Editor mini-preview content-inset** — mini dialog in the
  preview swatch doesn't use content-inset; it uses fixed headerH.
  Cosmetic-only.

---

## Open — 2026-04-17 (S313 marathon follow-up batch -- `great-robinson-15f409` worktree, merged to dev)

Three commits on top of the S311/S312/S313 three-way merge:

- `8057f064` -- **FORGE → GRID log prefix rename** across `forgemode.c` + all `port/src/forge/*.c` files + `pdgui_menu_forge.cpp`.  User-facing rename now complete: HUD badge already read "THE GRID", editor title already read "The Grid", log channel now reads "GRID:".  Internal code names (`forge_*`, `FORGE_MAX_*`, catalog namespaces, file paths) retained.
- `1694d3ec` -- **Audio volumes move to per-agent prefs**.  `prefs_agent.c` gained `[Audio]` block.  Agent Select load applies `MasterVolume` / `MusicVolume` / `GameplayVolume` / `UIVolume` via the corresponding `audioSet*Volume` setters.  Global pd.ini keys remain as per-machine defaults.  Also fixed **agent name handoff** -- `prefsLoadForFile` now uses `gamefileGetOverview` so sidecar filenames match Agent Select display text exactly.
- `6aaf299f` -- **pd.ini audit cleanup**.  Dropped `configRegister` for `Net.LerpTicks`, `Net.Client.{InRate,OutRate,UpdateFrames}`, `Net.Server.{Port,InRate,OutRate,UpdateFrames}`, `Update.ProtectedFolders` -- these are tuning knobs, not user prefs.  Globals retain their file-scope initializers so runtime code still works and `-port` CLI override still works.  New doc `context/config-pd-ini-audit.md` catalogs every remaining `configRegister*` call with a three-tier model + decision flowchart.

### Playtest verification

1. **Log prefix**.  Launch client, tail `pd-client.log`, press F7 to toggle The Grid freefly.  Every line that used to read `FORGE:` / `FORGE.LOGIC:` / `FORGE.SERIALIZE:` etc. should now read `GRID:` / `GRID.LOGIC:` / `GRID.SERIALIZE:`.  No "FORGE" strings should appear in the log for Grid operations.
2. **Per-agent audio volumes**.  Create Agent A, go to Settings → Audio, slide Master to 25%.  Create Agent B, set Master to 100%.  Quit and relaunch.  Load Agent A -- volume should be 25%.  Load Agent B -- volume should be 100%.  Open `saves/prefs_AgentA.ini` / `prefs_AgentB.ini` and confirm the `[Audio]` section reflects each setting independently.
3. **Agent-name handoff**.  Create an Agent with a long or special-char name.  Confirm `saves/prefs_<visible_name>.ini` is written with exactly the display text (not the raw encoded byte sequence).  Legacy agents from before this fix may orphan their old sidecar -- one-shot migration queued as a follow-up.
4. **pd.ini cleanup**.  Delete pd.ini.  Launch client.  Launch dedicated server with `--port 27123`.  Client connects.  Game plays normally.  Stop + restart client -- network tuning, server port, and protected folders all pick up their compile-time defaults; no warnings.
5. **Context doc**.  Open `context/config-pd-ini-audit.md` and verify the three-tier model section matches the actual configRegister surface (run `grep -rn configRegister port/ | wc -l` and spot-check).

### Deferred

- **Legacy sidecar migration** -- agents created before the `gamefileGetOverview` fix may have `prefs_<encoded>.ini` files that no longer match their new `prefs_<overview>.ini` path.  Small one-shot migration on first Agent Select load is queued.
- ~~**Gameplay-preference pd.ini → per-agent**~~ — **DONE S341**: `[Game]` block added to `prefs_agent.c` with all 6 keys + pd.ini baseline capture + reset-on-agent-select.
- ~~**Audio.ModPlaylist / ModShuffle / ModTrackId → per-agent**~~ — **DONE S341**: `[Audio]` extended with `ModPlaylist` (semicolon-delimited) + `ModShuffle`.

---

## Open — 2026-04-17 (S341 — `tender-borg-b2fc3b` worktree)

### Playtest checklist

1. **[Game] round-trip**: Sign in as agent, change HUD centering / SkipIntro / screen shake in Settings → exit → relaunch → same agent; values persist via sidecar.
2. **Reset on sign-out**: Open Agent Select screen; game prefs revert to pd.ini defaults (not previous agent's values).
3. **Audio playlist per-agent**: Two agents with different CS playlists; switching applies each agent's playlist.
4. **Stage loading**: SP mission + MP match — no regressions after assetLoadToNew migration in lang/setup/tiles/modeldef.

### Asset Provider Phase 3 follow-up

- **Phase 4**: retire raw filenum from public catalog API now that all call sites use provider handles
- **FileProvider inflate/preprocess**: generic `assetLoadToNew` path skips rzipInflate + `romdataFilePreprocess` — OK for Phase 3 (mod assets still go through legacy pipeline), needs resolution in Phase 4
- **lang/tiles catalog handles**: `langGetFileId()` + `stage.tilefileid` are still ROM integers. Phase 4 should introduce `langGetHandle()` / `catalogGetTileHandle()` so FileProvider lang/tile packs work.

---

## Open — 2026-04-17 (S311 — UI polish marathon, `zen-poitras-f86b73` worktree → merged to dev)

### Playtest verification of the S311 theme palette sweep

Commit `55c37fc2` + merge. Build: `PerfectDark.exe` 52,288,651 / `PerfectDarkServer.exe` 22,838,513 (pre-S312/S313 merge).

- **Theme-driven title glow** — install a custom theme with a deliberately wild `titleGlow` (e.g. pure magenta `#ff00ff`). Open each of these dialogs and confirm previously-cyan accents now follow the custom color:
  - **Warning dialogs** — default/info dialog and the **MP End Game modal** (pause → End Game → Confirm). Title glow should honour the custom tint via `pdguiImU32TitleGlow`.
  - **File Manager** (agentselect → Copy) — the "Copy agent" prompt follows the custom theme; the "Delete agent" prompt stays red (`pdguiImU32TintDanger`).
  - **Pause-menu rankings** — local-player row tint follows the custom color (35 alpha).
  - **Section headers** — Lobby, Network, Challenges, Team Setup, MP Settings → Select Tunes, Room (Bot Settings / Custom Traits / Save/Load Scenario / Level Editor / Properties), Main Menu (Recent Servers / D5.0a catalog). Every cyan section header follows the custom theme.
  - **Moddinghub + Audiomod + Controldiagram + Modmgr** — selected-entry headers (Audio Mod "Selected: ...", Modding Hub ini/skin entry headers, Control Diagram control-mode name, MP Settings "Library" header) follow the custom tint. Mod Manager "Base Game Asset" label follows `pdguiVec4TintInfo()`.
- **Delete-confirm button scaling** — Settings → Interface → right-click a user theme → Delete Theme. Cancel/Delete buttons scale with resolution (`pdguiScale(120.0f)`).
- **MP End Game content inset** — load a Menu Style mod with thick border corners, trigger the End Game modal. Body text clears the chrome border by the 8px breathe regardless of corner thickness.
- **CS music picker still works** — import a `.mp3` via Modding Hub → Audio Mods → Music → Import. Launch Combat Simulator → MP Settings → Soundtrack → Select Tunes. Track appears. Log carries `SELECTTUNES: open — base_tracks=N mod_tracks=M` + `AUDIOMOD: auto-enabled mod '...'`.
- **Controller nav** — with a controller connected, navigate main menu / Settings tabs / Room screen via D-pad + A + B + LB/RB. All work transparently via `pdguiDriveImGuiNav()` action-map → keyboard-nav translation.

### Follow-up queued from S311

- **Remaining non-blue literal sweep** — agentselect/agentcreate/solomission/modmgr/moddinghub still have PD-blue `IM_COL32` panel/border decorations plus red/yellow/green semantic literals that could fold into `tint_danger`/`text_warning`/`text_positive`.
- **Dead `ImGuiKey_Gamepad*` checks** — ~130 redundant checks across menu files. Harmless (Enter/Escape parallels catch the edges via action-map→keyboard translation). Removing is a ~1h churn task.
- ~~**renderCiSettingsRedirect / renderCiDeadPlayer2 / renderCinemaList S300 pool-ctx migration**~~ — **DONE (S311+/S323)**. All three use `menupoolAcquireDialog` + `pdguiConsumeTitleClose`. The defensive `inputCtxPopDeferred` at mainmenu.cpp:3414 is retained as an idempotent belt-and-suspenders guard.
- ~~**pdgui_lobby.cpp / pdgui_lobby_distrib.cpp / server_gui.cpp / pdgui_skin_editor.cpp blue-tint sweep**~~ — **DONE (S311 pt2 + S315)**. `server_gui.cpp` intentionally skipped (pd-server doesn't link pdgui_style). All semantic cyan/blue accents across all non-server UI files now use `pdguiVec4TitleGlow()`. Remaining hardcoded blues are intentional: log-viewer LOAD category (debug), Solo Mission difficulty colors (PD identity), agentselect initials (artistic).

---

## Open — 2026-04-17 (S312 — Modeldef guards + glyph system + net review, `amazing-mccarthy` worktree)

### Playtest verification of the S312 drop

Build: `PerfectDark.exe` 52,400,200 / `PerfectDarkServer.exe` 22,840,561.

- **Modeldef defensive guards (B-161 reinforcement)** — Replay the B-161
  repro: Defection (0x30) → Next Mission → Investigation (0x33), walk 1-2
  minutes.  Tail `pd-client.log` for either:
  - `DOOR.DIAG: doorGetBbox — no bbox for modelnum=...` (S308 guard) or
  - `MANIFEST-SP: late-add '...' post-load modeldef torn: parts=%d root=%p
    scale=%.3f` (new S312 diagnostic — identifies the exact catalog id
    whose late-add produced the torn state), or
  - `modelFindBboxNode: walker exceeded 10000 steps` / `modeldefFindBboxNode:
    walker exceeded 10000 steps` (new S312 guard — fires on a cyclic
    or dangling rootnode tree).
  No crash should occur.  The diagnostic fingerprints are the handoff for
  root-cause investigation.
- **Glyph system smoke** — no UI surface consumes it yet; the module
  compiles and exposes the API:
  - `pdguiGlyphGetDevice()` returns KBM or GAMEPAD based on the
    actionmap's 500 ms debounce.
  - `pdguiGlyphGetPrimaryVk(ACTION_USE)` returns the primary VK for the
    current device (E on KBM default, A on gamepad default).
  - `pdguiGlyphGetActionLabel(ACTION_USE, buf, sizeof buf)` writes "E"
    or "A" depending on device.
  - `pdguiDrawActionPrompt(ACTION_USE, x, y, "Use")` draws a pill.
  Pull the header via `#include "pdgui_glyphs.h"` (port/include on path);
  any C or C++ TU can call it.

### Follow-up queued from S312

- **Wire glyphs into in-world prompts** — obvious callers: pickup
  prompts (`[E] Pick up AR34` / `[A] Pick up AR34`), door prompts,
  terminal interact prompts, forge HUD controls reminder.  Currently
  the HUD uses hard-coded labels.
- **Root-cause fix for modeldef corruption class** — the S312 late-add
  diagnostic should produce enough log evidence in the next repro to
  pinpoint either the catalog id whose load is torn or a post-late-add
  corruptor.  Landing an actual fix (e.g. reloading the modeldef when
  numparts=0 is detected) is the next step.
- **Per-player glyph device** — if splitscreen is ever re-enabled,
  `pdguiGlyphGetDevice` needs a per-player signal.  Not a concern
  today (single local player only per constraints.md).

---

## Open — 2026-04-17 (S310 — The Grid editor F1-F8 bulk drop, `sharp-lovelace` worktree)

### Playtest verification of The Grid editor overlay

Build: PerfectDark.exe 52,176,729. Two commits in the `sharp-lovelace-a08d90`
worktree (`0732c806` and `ac7be593`). Files touched:
`port/include/forge/forge_core.h`, `port/src/forge/forge_{core,undo,logic,gametype,serialize,ai}.c`,
`port/fast3d/pdgui_forge_editor.cpp`, plus small edits to
`port/fast3d/pdgui_backend.cpp`, `port/src/pdmain.c`,
`port/include/pdgui_forge.h`, `src/game/setup.c`.

**Primary repro -- editor overlay in FREEFLY**:

1. Launch client; click "Forge" on main menu. CI Training loads in NORMAL play mode.
2. Press **F7** to toggle into FREEFLY. Expect a large "The Grid -- Editor"
   window to appear on the right side of the screen with 8 tabs:
   Catalog, Properties, Zones, Lighting, Logic, Game Type, Mission, Settings.
3. **Catalog tab**: search box, category filter dropdown, budget bar
   (green/yellow/red by soft/hard cap), collapsible category headers
   (Geometry, Props, Weapons, Spawn Points, Pickups, Lighting, Effects,
   Zones, Interactables, Characters). Clicking any button places an
   object ~400 units forward from the freefly camera.
4. **Properties tab**: select an object (click one in the catalog or via
   the selection UI), see its transform + type-specific props. Numeric
   transform editing and duplicate/delete buttons work.
5. **Zones tab**: quick-create Trigger/Kill/Teleporter; table lists all
   placed zones. Select/X buttons per row.
6. **Lighting tab**: skylight direction/color/intensity, atmosphere
   (fog/ambient/exposure/bloom), weather (rain/snow/sandstorm/storm),
   ToD cycle enable, sky catalog ID input.
7. **Logic tab**: add event/condition/action nodes (Op dropdown filters
   by kind), table of nodes with Fire button (triggers execution for
   events), wire create by src/dst UID, channel management (add,
   toggle, delete).
8. **Game Type tab**: name/description, structure (single/BoN/wave/phase),
   win condition, scoring rules, starting weapon, health mult, role mode
   (None/Infection/VIP/Juggernaut/HordeDefender/GunGameHunter), 8
   modifier flags, HUD toggles, wave spawner table with per-wave
   enemy/scale/hp/spawn-zone, boss state with simulate button.
9. **Mission tab**: Is-Mission checkbox, briefing/debrief text,
   sequential toggle, Add Primary/Secondary/Bonus objective buttons,
   table with desc + link-node UIDs + Complete/Fail/X buttons.
10. **Settings tab**: map metadata, players, default rules, bounds,
    grid/snap prefs, editor toggles, budget summary, Save-as-Mod +
    Load-from-Mod + Reset Map.

**Persistence**: Settings → type a name → click Save As Mod. Expect
`mods/Forge Maps/<slug>/mod.json` and `map.json` written. Reload via
Load From Mod restores the map metadata + settings.

**Primary repro -- MP elevator fix (`setup.c`)**:
Start a Combat Simulator match on any arena with a lift (Area 51,
Complex, or any CI Training variant with an elevator). Walk onto the
lift pad. Expect the lift to move between its stops. Prior behavior:
lift was completely dead in MP because it was never registered in
`g_Lifts[]`. Solo missions are unaffected (AI script `aiActivateLift`
still re-registers idempotently).

### S310 R1-R4 addendum (Mike refinements landed commit `833c3a95`)

- **R1 seamless swap**: F7 toggle confirmed non-stage-reloading.
  Verify via log stream -- should see `FORGE: freefly body-swap`
  / `FORGE: normal body-restore` pairs on each toggle.
- **R2/R4 weapon source**: Settings tab > "Weapon Source" dropdown.
  Pick MAP_DEFAULTS, save, reload, confirm round-trip.  When match
  setup UI lands the hook point is `s->weapon_source` +
  `s->allow_match_override`.
- **R3 dependencies**: Settings tab > live "Mod Dependencies" section
  shows bullet list; save a map that references a modded weapon,
  then grep `mods/Forge Maps/<slug>/mod.json` for `"dependencies":`
  array.  Distribution pipeline recursion lands with the mod-transfer
  integration pass.

### Follow-up queued from S310

- **Engine-level Dr. Carroll visual swap**: chr->bodynum + headnum
  are now assigned correctly on entry/exit (so saves + MP sync carry
  the right value), but the live mesh re-skin requires a
  `bodyAllocateModel` call on the new pair + a model-tree rebuild.
  Comes in a follow-up polish pass.
- **3D gizmo handles** -- the Properties tab edits transform
  numerically; the design doc §4.2 calls for drag-axis gizmos. Needs
  freefly-camera raycast + in-world axis rendering. Data model supports
  this already.
- **Placement reticle** -- clicking in the Catalog places the object
  ~400u forward from the camera. Full ghost reticle with
  valid/invalid tint needs a forge-owned input tick + click capture.
- **Runtime engine wire-in (partial — S314)** -- SPAWN_POINT objects inject
  into the live spawn pool via `spawnPoolAppendForgePoints`; AI objects spawn
  live bots via `botmgrAllocateBot`. Remaining deferred: placed props/weapons/
  geometry/doors/zones still log-only (need `propAllocate` + model wire path).
  `FORGE_OP_TELEPORT_PLAYER` + `SPAWN_AI` + `ENABLE/DISABLE_OBJECT` are now
  live-engine calls. `FORGE_OP_OPEN_DOOR` still only clears the data-model
  locked flag; `doorActivate()` needs the prop handle wired first.
- **Prop/weapon pad visual spawning** -- bots + spawn points are live (S314);
  floor decor / pickups / geometry still deferred. Need `propAllocate` path
  separate from `setupCreateObject` (which requires full intro-command context).
- **Door prop instantiation** -- `FORGE_CAT_INTERACTABLE` objects are logged
  but not spawned as live engine door props. Needs `propAllocate` + door model
  allocation + `doorActivate` wiring before `OPEN_DOOR` logic action is fully live.
- **AI navmesh generation** -- `forge_ai.c::forgeAiGenerateNavmesh` is a
  stub that logs counts. Full navmesh auto-gen from placed geometry is
  a post-F8 polish pass.
- **Boss health bar HUD** -- data model tracks boss state; the HUD
  drawing for the full-width bar belongs in `pdguiForgeHudRender`
  when `forgeBossState()->active`. Currently only shown in the Game
  Type tab's editor UI.
- **Terrain brushes** (F8 stretch) -- catalog entries for floors /
  ramps / platforms exist. True height-paint terrain is out of scope
  here and would require a new mesh-edit pipeline.
- **Co-op Forge** (F8 stretch) -- multiple Dr. Carroll editors syncing
  edits over the wire. Design doc §13.2 sketches the lock-per-object
  protocol; requires new SVC/CLC msgs.

---

## Open — 2026-04-17 (S309 — optimistic-feistel worktree → merged to dev)

### Playtest verification of the S309 deferred UI drop + config cleanup

Full write-up: `session-log.md` S309. Build: `PerfectDark.exe` 51,916,376 /
`PerfectDarkServer.exe` 22,840,561 bytes.

- **Content-inset sweep** — open each of the 20 migrated menus (agentcreate,
  agentselect, botsetup, challenges, cheats (3 dialogs), controldiagram,
  lobby, mpadvanced, mppause, mpsettings (2), mpsetup, network,
  playerconfig, room, solomission (11 dialogs), stats, teamsetup, training,
  warning (3)) with a Menu Style mod that has thick borders. Content
  (buttons, lists, labels) should clear the nineslice artwork by at least
  8px of breathing room on each side.
- **Theme Editor live preview** — open Theme Editor, observe the new
  preview column to the right of the color pickers. Edit Main Accent,
  Title Text, Title Glow (new S309 slot) — preview updates live. Edit
  Success / Danger / Info tints — three colored buttons reflect the new
  values.
- **Title bar blue tint customizable** — with a custom theme active, edit
  Title Glow (S309 slot). Click "auto" to return to derived-from-border2
  default; confirm glow matches border2 color.
- **Font Mod tool** — Modding Hub → Font Mod tab. Browse to a `.ttf`,
  enter a mod name, click Save. Verify `mods/Fonts/<slug>/<slug>.ttf` +
  `font.json` + `mod.json` exist. Settings → Interface → Font dropdown
  should list the new font without restart. Activating requires restart
  (ImGui atlas is session-once).
- **Scanline scaling** — at 720p, 1080p, 1440p, 4K, the scanline pattern
  should look visually consistent (1px/2px at 720p → 3px/6px at 4K).
  Check Settings → Video → Scanlines toggle; alpha slider works across
  all resolutions.
- **No imgui.ini** — launch game, quit. No `imgui.ini` should appear in
  the run directory.
- **Per-agent prefs** — create two agents (A, B). Sign in as A, pick a
  custom theme + menu style + title bar style. Quit, relaunch, sign in
  as B, pick different theme. Switch back to A — the theme should
  immediately revert to A's choice. Check `saves/prefs_<name>.ini` for
  both agents.
- **"The Grid" rename** — main menu should show "The Grid" button
  (where the "Forge" button previously was).
- **CS music picker fix** — import a `.mp3` or `.wav` via Audio Mods
  tab, select "Music" category, click Import. Verify status line
  confirms "Imported 'X' as <slug>:audio" + `AUDIOMOD: auto-enabled
  mod 'X'` in pd-client.log. Restart client, open Combat Simulator →
  MP Settings → Soundtrack → Select Tunes. Track should appear under
  "Mod Tracks (N)" in the left panel. Click → moves to right "Selected"
  panel. `SELECTTUNES: open — base_tracks=N mod_tracks=N` log line
  helps triage any future regressions.

### Follow-up queued from S309

- **Full pd.ini elimination** — only visuals + mod enablement moved to
  per-agent. Audio volumes, resolution, fullscreen, gameplay bindings
  still live in pd.ini. Needs a scope decision on per-agent input
  bindings.
- **Remaining hardcoded blue tints** — warning.cpp title text,
  agentselect state text, lobby state text, countdown overlay still
  use `IM_COL32(100, 200, 255, ...)` directly. Migrate them to
  `pdguiGetTitleGlow()`.
- **Forge HUD rename** — S310's pdgui_forge_hud.cpp still says "FORGE"
  in the mode badge. S310 owns that file.
- **Agent-name handoff** — `prefsAgentLoad` currently builds the key
  from `filelistfile::name[]` bytes, which may be encoded. Good enough
  for the current save format but replace with `gamefileGetOverview`
  so the filename matches what the user sees in Agent Select.

---

## Open — 2026-04-17 (S308 — dev direct)

### Playtest verification of B-161 (door-tick crash) + B-162 (pause menu hardening)

Commits on `dev`. All items build-verified; needs in-game playtest.

- **B-161 defensive crash guard** — Replay the exact repro from the bug report: Start Mission 1 Objective 1 (`base:defection` stage 0x30), complete it, click "Next Mission" on endscreen, start into Mission 1 Objective 2 (`base:investigation` stage 0x33), run around for 1-2 minutes. Expect: no crash. If crash moved elsewhere, look in `pd-client.log` for the new `DOOR.DIAG: doorGetBbox — no bbox for modelnum=... model=... flags=... doortype=...` WARNING — that line names the exact door's modelnum that has a NULL/torn bbox, which is the handoff for root-cause investigation.
- **B-162 pause menu hardening** — In Mission 1 Obj 2 (or any solo mission), open pause menu ≥3 times per mission. Confirm: (a) all 5 buttons always have visible labels (Resume / Restart Mission / Inventory / Settings / Abort Mission), never blank; (b) title reads "Investigation: Status" (or similar with the stage name) — never just ": Status"; (c) open Restart Confirm overlay, Escape to cancel, close pause, reopen pause — no Restart Confirm overlay bleeds in. Complete mission, advance to next mission, open pause — objectives list must be for the CURRENT mission (never shows leftover objective entries from the prior mission).

### Root-cause investigation queue (B-161 follow-on)

If the defensive fix holds but the `DOOR.DIAG:` WARNING fires repeatedly, the root cause is a modeldef being torn mid-gameplay. Playtest log from 2026-04-16 23:50 showed parallel `WARNING: body0f02ce8c: truly invalid bodymodeldef for bodynum 108 (file 0x004b) ... parts=0 -- skipping` at stage 0x33 load time — catalog says `base:sp_body_108` loaded successfully but modeldef has 0 parts. Two possible mechanisms:
1. Late-add manifest path (`MANIFEST-SP: late-add 'base:sp_body_108' type=0 (missed by pre-scan)`) loading the ROM bytes but not binding rodata correctly.
2. Catalog cache has a stub entry that shadows the ROM-load result.

Targeted instrumentation needed in `bodyAllocateModel` / `setupLoadModeldef` / `catalogResolveXXX` for late-add codepath to confirm.

---

## Open — 2026-04-17 (S307 — Forge F0 merged to dev)

### Playtest verification of Forge F0 Foundation

New modules merged from `claude/tender-sutherland-536de7` commit `8d412cc1`.
Full design ref: `context/designs/forge-level-editor-2026-04-16.md` §15 Phase F0.
Files: `src/include/game/forgemode.h`, `src/game/forgemode.c`,
`port/include/pdgui_forge.h`, `port/fast3d/pdgui_menu_forge.cpp`,
`port/fast3d/pdgui_forge_hud.cpp`; modifications to actionmap (5 new
`ACTION_FORGE_*`), `pdmain.c` (forgeTick wiring), backend.cpp (HUD dispatch),
`pdgui_menu_mainmenu.cpp` (top-level "Forge" button).

Build sizes: PerfectDark.exe 51,780,176 / PerfectDarkServer.exe 22,840,512.

**Primary repro -- Forge entry + mode toggle**:
1. Launch client. Open main menu (F12 from CI, or normal title-screen flow).
2. Click new "Forge" button (between Stats and Quit). Expect log
   `FORGE: launching session (base stage = CITRAINING 0x26)` + sound effect.
3. CI Training stage loads. Once player is wired, expect log
   `FORGE: -> NORMAL (session start (request))` + `FORGE: session active stage=0x26`.
4. Top-left HUD badge should show `FORGE -- NORMAL` in green.
5. Press **F7**. Expect `FORGE: -> FREEFLY (toggle press) pos=(...) yaw=...`,
   badge flips to cyan `FORGE -- FREEFLY`, centered crosshair appears,
   bottom-left readout shows pos / yaw+pitch / `spd NORMAL`, right edge shows
   placeholder "Object Catalog (F1: not yet implemented)" panel, bottom-right
   shows controls reminder.
6. **WASD** moves freefly camera relative to look direction. **Q**/**E**
   descend/ascend. **Mouse** looks (right-stick on controller). **LSHIFT**
   `spd BOOST`. **LCTRL** `spd PRECISION`. Pos readout updates live.
7. Camera should pass through walls/floors (no collision).
8. Press **F7** again. Expect `FORGE: -> NORMAL (toggle press)`. Player chr
   resumes at the freefly camera position; first-person controls work again.
9. Press **Esc** to open main menu. Forge session stays NORMAL. Click Quit.
   Stage transitions to TITLE. Expect `FORGE: -> INACTIVE (stage left gameplay)`.

**Health-signal log lines** (tail with `grep '^.*FORGE:' pd-client.log`):
- `FORGE: init` -- one-time at startup
- `FORGE: enter requested ...` -- on Forge button click
- `FORGE: -> NORMAL` -- on session entry or freefly exit
- `FORGE: -> FREEFLY` -- on freefly entry (with pos+yaw)
- `FORGE: -> INACTIVE` -- on stage leaving gameplay

**Absence is the failure signal**:
- No `-> FREEFLY` after F7 = action binding never registered or actionPressed
  not firing. Check `actionmapSaveBinds` output / `pd.ini` for ForgeToggle.
- No `pos` change in FREEFLY readout = `forgeUpdateFreefly` not running, or
  player chr's prop pointer was NULL.
- No badge after Forge button click = `forgeRequestEnterSession` was queued
  but stage transition didn't complete (g_MainChangeToStageNum stuck), or
  forgeTick is being called from wrong place.

### Follow-up queued from S307 (out of F0 scope)

- **F1 Object Catalog & Placement** -- left-side catalog tree, placement
  reticle with green/red validity tint, undo/redo, object budget tracking.
  See design doc §3 + §4 + §15 Phase F1.
- **Drop-to-ground on freefly exit** -- design says exiting freefly should
  fly Dr. Carroll to nearest valid ground. F0 just leaves the player at the
  freefly position (which can be mid-air; player falls naturally). Acceptable
  for F0 but worth a polish pass before F1 lands.
- **Dr. Carroll model swap** -- design says "the player's body disappears and
  is replaced by the Dr. Carroll model". F0 uses the existing player chr
  model with bondmovemode = CUTSCENE. Need a model swap or hide-mesh + spawn
  Carroll prop in F1 or F2.
- **Controller toggle binding** -- F0 ships keyboard-only (F7). Design says
  hold LB+RB. Add a chord binding once the action map supports chords, or
  bind to a single button in the controller IMC.
- **Pause simulation toggle** -- design says editor mode "pauses (or
  optionally continues) the simulation". F0 leaves sim running; need a
  forge-controlled `lvSetPaused()` toggle in F1 or F2 with a HUD checkbox.
- **Multi-stage base** -- F0 hard-wires CITRAINING. F3 brings the stage
  browser (Paradox template, blank canvas, MP map list).
- **Custom-match integration** -- F0 only enters from main menu. The user
  prompt notes Forge "must work in CI free-roam AND in custom/private
  matches". Latter requires hooking into match setup / room flow; deferred
  to F3 when the base-stage browser exists.

---

## Open — 2026-04-17 (S306 — dev direct)

### Playtest verification of the S306 multi-batch drop

Commits on `dev`: `feb5431c` palette extensions, `a504f3e7` Interface tab + delete scaffolding, `ae4f2f50` input mapping redesign, `5840f28a` (Mike parallel, absorbs BATCH 2 mod delete wiring), `db509d87` Theme Editor bundle dropdowns, `affc61fa` X-button close fix + defensive ctx pop.

1. **Interface tab** — open Settings. New "Interface" tab between Video and Audio. Dropdowns for Color Theme / Menu Style / Title Bar / Font all live here. "Open Color Editor..." and "Open Menu Style Tool..." buttons launch the deeper tools. LB/RB bumpers cycle through 8 tabs.
2. **Color Theme delete** — right-click a user theme (mod theme, not Grey/Blue/Red/Green/White/Silver/Black & Gold) → "Delete Theme…" → confirm. Theme disappears immediately. Built-ins silently refuse. GamepadFaceLeft (controller X) on focused row also opens the context menu.
3. **Mod delete** — open Modding Hub → Installed Mods tab. Right-click (or X-button) a mod → "Delete Mod…" → confirm. Mod disappears.
4. **Theme palette extensions** — open Theme Editor (Interface tab → Open Color Editor). Four groups visible: Window Frame / Text Colors / Interactive Elements / Semantic Accents (+ Reserved behind checkbox). "(auto)" button on Semantic slots zeros to derived default. Save as mod → examine theme.json in `mods/<slug>/` and confirm only the extension keys you actually touched are present.
5. **Theme bundle authoring** — Theme Editor → fill Name → pick a Menu Style (optional) + Font (optional) → Save. Confirm `menuStyle` / `font` keys appear in the saved theme.json.
6. **Input Mapping redesign** — Settings → Controls → Keyboard & Mouse. Group headers visible (Movement / Aim / Combat / Weapons / Vehicle / Menu Nav / C-Buttons / D-Pad / System Hotkeys). Search box filters by action name. Deliberately bind W to two actions — both show red borders with "Conflict: 'W' is bound to more than one action" tooltip. Non-obvious actions show explanatory tooltips on hover (Fire Mode, D-Pad Down, C-Up, etc.). "Save Controls" button at the bottom is gone (auto-saves).
7. **X-button close on Settings** — open Settings, click the X on the first attempt. Should close on first click. Log: `MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0) [via X]` (the `[via X]` suffix is diagnostic — if you see it, the direct-signal channel fired).
8. **Movement after menu close** — open main menu, close. WASD should move immediately, no restart needed. If log shows `MENU_IMGUI: defensive inputCtxPopDeferred(g_CtxImGuiMenu) — leak class caught on top-level close`, that means the boot-time CI-redirect ctx leak was caught and unlocked movement.
9. **Controller X-button on themes + mods** — focus a user theme or mod row with the stick, press X (GamepadFaceLeft = Xbox X / PS Square). Context menu should open.

### Follow-up queued from S306

- **Per-agent prefs.ini** — full implementation per `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`. ~300-400 LOC + careful test matrix (mid-match agent switch, guest agent, Settings-open switch). Dedicated future session.
- **`renderCiSettingsRedirect` / `renderCiDeadPlayer2` / `renderCinemaList` migration to S300 pool-owned ctx** — `affc61fa`'s defensive pop hides the symptom but the underlying raw inputCtxPush/Pop pattern remains. S304 deferred list.
- **X-button close polling in other renderers** — `pdguiConsumeTitleClose()` is only wired into `renderMainMenu`'s close handler. Sweep needed for Theme Editor / Modding Hub / Pause Menu / CI redirect / Endscreen — they still rely on the Escape fallback.
- **Tooltip sweep for input mapping** — 51 actions have basic display names; ~12 have explanatory tooltips. Bind tooltips for the remaining actions would raise the polish level.
- **Per-action save — keyboard / controller device filter** — currently search matches on display name only. Could extend to accept "key:W" or "ctrl:A" syntax to find all actions bound to a given device/key.

---

## Open — 2026-04-16 (S305 — dev direct)

### Playtest verification of the S305 batch

Commits on `dev`. All items build-verified; needs in-game playtest.

- **P0 content-inset** — launch game, open main menu on a narrow window + on 1080p. Confirm Solo Play / Online Play / Change Agent / Settings / Mods / Cheats / Stats / Quit Game all clear the chrome border with visible breathing space. Repeat for Settings body (tabs + content), CI Settings redirect, Cinema list. Absence of edge-to-edge bleed = pass.
- **Settings persistence** — with a mod theme active (Save-as-Mod output, e.g. `user.pokemon.theme`), pick a Menu Style, pick a Title Bar style, quit client, relaunch. Expect the same theme + menu style + title bar on restart. Watch for `PDGUI theme loader: saved theme '...' not resolvable on startup` WARNING (indicates the mod theme was saved but the registry lost it between runs).
- **Menu Style rename** — confirm the Modding Hub tab reads "Menu Style" (not "Nine-Slice Chrome"), the Settings → Video dropdown reads "Menu Style", and the tool header blurb starts with "Menu Style --".
- **Theme Editor dock + refresh** — open Theme Editor, enter Name + Author, click Save on the action row. Expect status "Saved to mods/" green text, and the saved theme to appear under "Custom (from mods/)" in Settings → Debug → UI Theme WITHOUT restart.
- **Menu Style tool preview + Advanced header** — import a small PNG (e.g. 64x64). Source Preview should now fill the sidebar width (not show tiny). Expand Advanced header to confirm Scale X/Y, Center Cut, Desaturate, Quick Presets are all still wired.
- **Font import** — drop a `.ttf` into `mods/Fonts/MyFont/MyFont.ttf`, restart, confirm Settings → Video → Font dropdown lists "MyFont". Pick it, save, confirm `Video.FontId=user.MyFont.font` in `pd.ini`. Restart, confirm menus render with the new font. Remove the mod → confirm graceful fallback to Handel Gothic.
- **Theme bundle** — hand-edit a `mods/<theme_slug>/theme.json` to include `"menuStyle": "user.<chrome_slug>.ui-chrome"` + `"font": "user.<font_slug>.font"`. Activate the theme; chrome should swap live, font message should say restart-needed.

### Follow-up queued from S305

- **Theme Editor UI for bundle fields** — add Menu Style + Font dropdowns to Save-as-Mod panel so users don't have to hand-edit `theme.json`.
- **Per-agent prefs.ini** — full implementation per `context/designs/theme-bundle-and-per-agent-settings-2026-04-16.md`. Dedicated future session.
- **Content-inset sweep for remaining menus** — cheats, mpsetup family, mppause family, room, training, mpsettings, playerconfig, botsetup, agentselect, agentcreate, stats, network, lobby, warning, teamsetup, solomission, controldiagram, challenges, matchsetup — any ImGui renderer that calls `pdguiDrawPdDialog` directly. Audit for `SetCursorPos` + `buttonW = -1.0f` antipattern.
- **B-141 audio underruns** — still ~200/30s in the latest log. Not caused by S305 work but remains open.
- **Old renderers still on raw `inputCtxPush/Pop`** (from S304) — `renderCiDeadPlayer2`, `renderCinemaList` still leak-prone. Watchdog covers them; proper S300 migration pending.

---

## Open — 2026-04-16 (S304 — focused-roentgen)

### Playtest verification of B-160 (menu pool slot leak + darkened/dead main menu)

Build: `PerfectDark.exe` 51,563,427 / `PerfectDarkServer.exe` 22,837,840 bytes. Files touched: `src/game/menu.c`, `src/game/menutick.c`, `src/include/game/menu.h`, `port/fast3d/pdgui_menu_mainmenu.cpp`. Full write-up: `session-log.md` S304.

**Primary repro — main menu reopen cycle (the user-reported bug)**:

1. Launch client. In CI free-roam, open the main menu with whatever key / controller binding you normally use.
2. Confirm log line: `MENUPOOL: acquired main_menu gen=1 def=<ptr> ctx=imgui_menu(owned)` (NOT `ctx=none(shared)` — after the migration the main menu now OWNS the ctx).
3. Press Esc (or controller B) to close the menu. Confirm log line: `MENUPOOL: released main_menu gen=1 ctx=imgui_menu` (immediately, NOT at shutdown).
4. Reopen the main menu. Confirm `MENUPOOL: acquired main_menu gen=2 ...` with a fresh generation number.
5. Repeat 3-5× fast-open/fast-close cycles. Each cycle should be a clean `acquired` / `released` pair, generations should increment monotonically (2, 3, 4, 5).
6. ABSENCE of these log lines is the health signal:
    - `MENU: menuPopDialog called at depth 0 (underflow)` — underflow was the leak trigger; should not appear on normal close
    - `MENU: menuPushDialog rejected — pool slot [main_menu] already active` — this was the user-visible symptom (menu stays darkened / can't reopen)
    - `MENU: watchdog — legacy stack empty but N pool slot(s) active` — the per-frame consistency check; any occurrence means a close path we haven't yet wired is leaking slots (auto-recovered, but identifies the path for follow-up)

**Stress test — force-close paths (the underflow fallback)**:

- Open main menu → wait for CI free-roam timer to tick into a scripted transition (if any). Confirm no leak warning.
- Open main menu → trigger a "Quit to Title" or "Quit to Main Menu" path if available. Confirm clean release in log.
- Open main menu → click "Combat Simulator" → click "Start Match" (triggers `matchStart` → `menuStop` → stage transition). Expect a `MENUPOOL: released N slot(s) (bulk)` from the force-close site; NOT the watchdog warning.

**Stress test — rapid reopen**:

- Open / close / open / close the main menu as fast as possible (mash Esc + Menu-open key). No stuck state, no double-render, no darkened backdrop, no rejected pool warnings.

**Switching around — the original user scenario**:

- Open main menu → click Settings tab → back to main → click Modding tab → back to main → click Online Play → back to main → Esc to close → reopen. Cycle through each sub-view at least twice. Same health signal as primary repro.

### Watchdog diagnostic interpretation

If the watchdog DOES fire (`MENU: watchdog — legacy stack empty but N pool slot(s) active`), the immediately-following `MENUPOOL dump:` lines identify the leaked slot(s). Common expected scenarios:

- `MENUPOOL dump: [main_menu] gen=N def=<ptr> ctx=<ctx>` — main menu close path bypassed `menuPopDialog` somehow. Trace the sequence of log lines in the ~10 frames before the watchdog fired.
- `MENUPOOL dump: [ci_options] gen=N ...` — a CI Options sub-dialog close bypassed pool release (these are still on the raw ctx pattern, see "Not fixed in this session" in S304).
- `MENUPOOL dump: [cheats]` or other migrated-renderer slot — S300 pattern regression. Should not happen after S300.

### Follow-up queued from S304

- **Migrate remaining mainmenu.cpp renderers to S300 pattern** — `renderCiSettingsRedirect` (line 3170+), `renderCiDeadPlayer2` (line 3249+), `renderCinemaList` (line 3358+) still use raw `inputCtxPush/Pop` around `menuPopDialog`. Same leak class as the main menu was; the S304 watchdog will catch any resulting leak, but proper migration removes the raw pattern.
- **Audit `menuPushRootDialog` pool hygiene** — `src/game/menu.c:3733-3736` zeroes `g_Menus[].numdialogs = 0; depth = 0;` without consulting the pool. Any active pool slots survive the root push. The S304 watchdog auto-recovers, but a principled fix is `menupoolReleaseAll()` at the top of `menuPushRootDialog` (or, better, treat root push as a stage-transition-class reset and route it through the existing reset helpers).
- **Underflow root-cause tracing** — the S304 fix treats underflow as a leak-recovery point, but doesn't identify WHICH caller triggered the underflow. A short-lived diagnostic patch could add a `sysLogCallerHint` line (file + function name of caller) to help pinpoint the origin in a future playtest.

---

## Open — 2026-04-16 (S303 — reverent-chatelet)

### Playtest verification of S303 game-loop sweep

Commit on `claude/reverent-chatelet-e076df`. Build: `PerfectDark.exe` 51,564,390 / `PerfectDarkServer.exe` 22,837,840 bytes. Full findings: `context/scratch/game-loop-sweep-2026-04-16.md`.

Tail the following across every playtest:

```bash
grep -E "GAMELOOP\.(CAMPAIGN|COOP|COUNTEROP|MANIFEST|WEAPON)" pd-client.log
grep -E "GAMELOOP\.(COOP|COUNTEROP|MANIFEST)" pd-server.log
```

- **Solo campaign first mission** — expect entry log `GAMELOOP.CAMPAIGN: menuhandlerAcceptMission entry stage_id=...`, SP transition log, per-weapon grants, endscreen prepare + NEXT_MISSION or EXIT_TO_MAIN_MENU bridge log.
- **Deep Sea co-op auto-advance** — expect `GAMELOOP.COOP: Deep Sea auto-advance → ...` followed by `GAMELOOP.MANIFEST: MP transition to 0x...` for the next stage.
- **Non-Deep-Sea co-op mission complete** — expect `GAMELOOP.COOP: MPENDSCREEN → CITRAINING lobby return` + `GAMELOOP.MANIFEST: MPENDSCREEN exit clearing manifest (N entries) before CITRAINING`. If the new `manifestClear` didn't fire (line missing), SP-13-class regression.
- **Counter-Op 2-client match** — server log: `GAMELOOP.COUNTEROP: server start anti stage=0x... antiClientId=N antiplayernum=N (from wire)`. Any `WARNING antiClientId unresolved` = bug — v36 wire didn't carry the value or server state is stale. Both clients log `GAMELOOP.COUNTEROP: endscreenPushAnti entry role=bond|anti`; anti client's weapon log is `GAMELOOP.WEAPON: ... INTRO skipped for anti role`.
- **Co-op 1-pad telefrag audit** — any coop mission should log `GAMELOOP.COOP: only N spawn pad(s) declared ...` as a WARNING if `g_NumSpawnPoints < 2`. Each warning identifies a problem stage.
- **Stale MP manifest leak detection** — if `GAMELOOP.MANIFEST: stale MP manifest (N entries) leaked into SP transition to 0x%02x — routing via MPTransition` ever fires, that identifies a previously-silent teardown gap.

### Follow-up queued from S303

- **GAP-A co-op manifest ignores host payload** — server-side `manifestBuild(&g_ServerManifest, NULL, NULL)` at `netmsg.c:4870` ignores the CLC_LOBBY_START manifest payload on the co-op/anti path (Combat-Sim deserializes via `manifestDeserialize` at `:4706`). Now logged via `GAMELOOP.COOP: server-side manifest built ...`. Design call needed: adopt host manifest + diff-merge against server state, or stay server-authoritative and document the behaviour.
- **GAP-B co-op 1-pad telefrag (proper fix)** — coop-aware pad scoring in `playerChooseSpawnLocation` or a co-op-specific SP-14 expansion in `playerreset.c` that triggers on `g_NumSpawnPoints < LOCALPLAYERCOUNT()`. The S303 audit log makes the problem observable but doesn't prevent the telefrag.
- **Anti body/head pre-load manifest gap** — possession-based resolution already relies on post-load catalog lookup. A proper fix would require modelling a canonical anti-chr per mission in mission config, or upgrading `manifestSPRescanSetup` to scan `g_StageSetup.intro` for possession-bait chrs.
- **`menuhandlerAcceptMission` menu pool release** — now logged but does not call `menupoolReleaseAll`. Low priority — `menuStop()` already walks stack — but could add defensively for parity with endscreen bridge paths.

---

## Open — 2026-04-16 (S300 — peaceful-aryabhata)

### Playtest verification of the S300 batch

Single commit on `dev` (fast-forwarded from `claude/peaceful-aryabhata-7abdc6`). Build: `PerfectDark.exe` 51,534,311 / `PerfectDarkServer.exe` 22,827,178 bytes.

- **ADR §6.1b pool-owned ctx migration** — For each migrated renderer (Cheats, MP Setup family, MP Pause family, MP Advanced family, Player Config, Bot Setup, Agent Select, Room, Training FR, MP Settings family) confirm:
  - Opening the menu emits `MENUPOOL: attached ctx to active <type>` (IsWindowAppearing attach) or `MENUPOOL: acquired <type>` (if `menuPushDialog` hadn't pre-acquired; rare but possible for pure-ImGui menus like Room).
  - Closing via Back/Esc emits `MENUPOOL: released <type>` from `menuCloseDialog`.
  - A Begin()=false cull (rapid open/close race) also releases cleanly — no trapped player input.
  - Stacking Soundtrack → SelectTunes opens both without dedup-rejection warnings; backing out of Tunes keeps Soundtrack's ctx alive (shared mode).
  - Room in solo mode keeps the main menu's ctx alive on close (shared mode); Room in network mode pops its own ctx.
- **SP-14 defensive reset** — host a listen server, start a match, disconnect mid-match (hit the X / quit). Re-host. Tail `pd.log` across the disconnect for the `disconnect-time reset` line if a match was active. After re-host, `g_NetMatchRoomId` starts as `0xFF` (any subsequent ready-gate fire should set it fresh).
- **Manifest diagnostics** — on title-screen load, expect a log line like `MANIFEST-MENU: built N entries — bodies=X(mod=Y,disabled=Z) heads=...`. If Skin Editor mod characters are enabled in Modding Hub and this shows `mod=0`, that's the old gap recurring. Play a stage with cinematic intro spawns (Deep Sea, Crash Site) and tail for `MANIFEST-SP: rescan diff — newly-discovered=N`; `N > 0` confirms the post-load scan is finding cinematic spawn assets.

### Follow-up queued from S300

- ~~Training sub-dialog stacking: `g_FrDifficultyMenuDialog`, `g_FrTrainingInfoPreGameMenuDialog` etc. all map to `MENU_TYPE_TRAINING` in the pool registry.~~ **DONE 2026-04-19 (B-200)** -- playtest confirmed FR was non-functional (weapon click silently dropped). Split FR into `MENU_TYPE_FR_WEAPON_LIST`, `MENU_TYPE_FR_DIFFICULTY`, `MENU_TYPE_FR_INFO` (pre-game + in-game share), `MENU_TYPE_FR_RESULT` (completed + failed share). Registered the previously-unregistered `g_FrDifficultyMenuDialog`. See `context/bugs.md` B-200.
- ~~**Follow-up (same class)**: DT (`g_DtListMenuDialog` + `g_DtDetailsMenuDialog`) and HT (`g_HtListMenuDialog` + `g_HtDetailsMenuDialog`) share `MENU_TYPE_TRAINING` and legitimately stack List -> Details.~~ **DONE 2026-04-19 (B-201)** -- split into `MENU_TYPE_DT_LIST` / `MENU_TYPE_DT_DETAILS` / `MENU_TYPE_DT_RESULT` and `MENU_TYPE_HT_LIST` / `MENU_TYPE_HT_DETAILS` / `MENU_TYPE_HT_RESULT`, mirroring the FR split (B-200). Completed + Failed share the RESULT slot because training flow pushes exactly one. Bio / Hangar stay on `MENU_TYPE_TRAINING` (no stacked Details dialog at this time). See `context/bugs.md` B-201.
- Shared-action leak (ADR §6.2) still open — per-scope state arrays in the actionmap.
- Unit / integration tests for the pool (ADR §6.3).

---

## Open — 2026-04-16 (S302 — quirky-mendeleev)

### Playtest verification of spawn pool tiered selection

Reference: `src/game/spawnpool.c`, `src/game/playerreset.c`, `src/game/player.c`. Build: `PerfectDark.exe` 51,533,440 / `PerfectDarkServer.exe` 22,824,057 bytes.

- **Tier distribution (healthy signal)** — Stock 4-player FFA on Felicity / Warehouse / Temple:
  - `pd.log` should show `SPAWN.TIER: T1_OPTIMAL initial MP spawn ...` for the 4 human spawns and for every respawn. No T2/T3/T4 on well-resourced stock maps.
  - Grep: `grep 'SPAWN.TIER:' pd.log | awk '{print $2}' | sort | uniq -c` — expect near-100% T1_OPTIMAL.
- **Burst reservation (32-bot Chicago)** — start a max-bot Combat Sim on Chicago:
  - First tick should produce 1 local human + 32 bot placements in rapid succession. Every placement should get a distinct pool slot (no two `pool[N]` entries with the same N in the first 33 SPAWN.TIER lines).
  - If the pool was built with >33 slots, expect all T1. If fewer, expect T2_CYCLED once the reservation bitset saturates mid-burst, then resume T1 on the cleared slots.
  - Grep: `grep 'SPAWN.TIER:.*T2_CYCLED' pd.log` — count should be <= pool->count - slots (i.e. T2 only fires when burst exceeds pool capacity).
- **Over-subscribed tiny arena** — pick a mod map with a very small pool (ring test smoke log says L4 layer, small count). Start a 16-bot match. Expect periodic `SPAWN.TIER: T3_REUSED — pool oversubscribed` lines during respawn waves. Players may briefly telefrag each other — that's the designed behaviour (no void spawns, no crashes).
- **Solo map in Combat Sim (zero declared pads)** — load G5 Building / Chicago SP stage as a MP arena. Expect pool build log to show `max_layer=2` or higher (`L2 waypoints` / `L3 grid` / `L4 radial`). Spawn decisions should still log `SPAWN.TIER: T1_OPTIMAL` until the pool is oversubscribed; T2/T3 only when placements exceed pool capacity.
- **Last-resort synthesised position** — engineered repro: load a map with zero intro spawns, zero waypoints, zero pads (e.g. a broken mod map). Pool build will fall all the way to L4 radial; if L4 also fails, `spawnPoolLastResort` should log `SPAWN.TIER: T4_LAST_RESORT — synthesised pos=...` and the player should spawn near the stage AABB centre (not at (0,0,0)). No crash.
- **`spawn_needed` in netplay** — join a dedicated server match with 6 other human clients + 10 bots. On each client's `pd.log`, confirm pool build line reports `needed=%d` with %d = 18 (or higher with span bonus), not 11 (which would be PLAYERCOUNT()=1 + 10 bots).
- **No regressions on S298 reservation bitset** — same-tick burst on Chicago should still produce unique pool indices; reservation auto-clear on `g_Vars.lvframenum` change still works (T2 explicitly clears it too).

### Follow-up if tier distribution looks wrong

- **Heavy T3/T4 on stock maps**: pool count came out too small. Check `pd.log` for `SPAWNPOOL: build complete -- %d points` and compare against `needed`. If produced << needed, the validator is too strict for that map — consider relaxing `SPAWNPOOL_BUDGET_THRESHOLD` or the capsule-radius reject for that specific geometry.
- **T2_CYCLED fires every tick**: reservation bitset isn't auto-clearing — check `spawnPoolTickCheck` is seeing `g_Vars.lvframenum` advance.
- **T4 synthesised at (0,0,0)**: `spawnPoolComputeAABB` returned `valid=false`. Check whether `g_Rooms` / `g_Vars.roomcount` are populated before playerReset runs on this stage.

---

## Open — 2026-04-16 (S301 — confident-chaum) — comprehensive instrumentation drop

**What shipped**: `port/include/crashbreadcrumb.h` + `port/src/crashbreadcrumb.c` (256-slot static ring, dumped by VEH / UEF / SIGABRT / Linux `sigaction` handlers); push sites in `src/lib/main.c` (mainTick heartbeat + mainChangeToStage), `src/game/lv.c` (lvTick), `src/game/chr.c` (CHR.TICK around chraTick dispatcher), `src/game/chraction.c` (chraTickBg), `src/game/bondwalk.c` (bwalkTick), `src/game/bot.c` (botSpawn), `src/game/botmgr.c` (botmgrAllocateBot), `port/src/net/matchsetup.c` (matchStart), `port/src/net/netmsg.c` (SVC_STAGE_START send/receive), `port/src/net/net.c` (netServerStageStart); standard log-channel DIAG lines with prefixes `ENDSCREEN.DIAG:` / `CHR.DIAG:` / `MATCHSTART.DIAG:` / `AUDIO.DIAG:` / `CRASH.DIAG:` covering Bug C / Bug D / Chicago / Airbase / B-141 respectively.

### Playtest: tail the log for the new DIAG tags

On the next repro pass, before reporting, run:

```bash
grep -E "ENDSCREEN\.DIAG|CHR\.DIAG|MATCHSTART\.DIAG|AUDIO\.DIAG|CRASH\.DIAG|HEARTBEAT|LVTICK|BWALK\.TICK|CHR\.TICK|BOT\.SPAWN|STAGE\.CHANGE" pd-client.log
```

and for the server side:

```bash
grep -E "MATCHSTART\.DIAG|CHR\.DIAG|CRASH\.DIAG" pd-server.log
```

### What each tag tells you

- **Bug C — MP endscreen body invisible** (`ENDSCREEN.DIAG:`). On fresh entry you get one `ENTRY (fresh)` line with `g_MpPlayerNum / g_NetMode / challengeResult / titleOverride`. Once Begin succeeds you get a geometry dump with `sf / menu / pad / win / contentAvail / scroll`. On the first frame you also get `body child opened contentW=… contentH=… innerCRA=…x…` and `rankings built count=N teamsMode=…`. If rankings count is 0, that explains the invisible body. If `contentH < minH` you see the pre-existing `ENDSCREEN: contentH clamped` warning. Capped at 80 prints per match.
- **Bug D — invisible networked bots** (`CHR.DIAG:`). Bot allocation logs `botAlloc chrnum=… body=… body_id='…'`. If `bodyAllocateModel` returns NULL you see `WARNING bodyAllocateModel returned NULL`. If `chrAllocate` returns NULL you see `WARNING chrAllocate returned NULL`. At the end of `botSpawn`, final state is `botSpawn DONE chrnum=… model=… chrflags=…x… hidden=…x… invisible=0|1`. An `invisible=1` line names the exact reason (`model=NULL` / `HIDDEN` / `VOID(-1)`).
- **Chicago silent crash ~9s** (`CRASH.DIAG:`). On the next silent death, open `pd-client.log` or `pd.crash.log` and find the `FATAL:` line. Directly below it is `CRASH.DIAG: breadcrumb ring dump follows:` followed by up to 128 entries in time order. Last entry = last subsystem alive. Expect sequences like `HEARTBEAT frame=N …` → `LVTICK frame=N …` → `CHRTICKBG …` → `CHR.TICK slot=17 chrnum=25 action=…` → (crash here). If the trail ends on `BWALK.TICK` or `STAGE.CHANGE`, the crash was NOT in chr AI.
- **Airbase 0xc0000005 / Start-Match no-response** (`MATCHSTART.DIAG:`). Server log should show `matchStart entry …` → `pre-mpStartMatch …` → `post-mpStartMatch …` → `netServerStageStart stage=… clients=N` → `SVC_STAGE_START sent …`. Each missing step is a decision point that bailed. Client log should show `SVC_STAGE_START read begin srccl=… state=…` if the message arrived. Absence of that line on client + presence on server = packet drop in transit. Absence on both = server never sent.
- **B-141 audio skips** (`AUDIO.DIAG:` / enhanced `AUDIO[B-141]:` summary). `audioInit` logs full SDL device spec — watch for `SAMPLE RATE MISMATCH` warning. Every 30 s a summary line now reports drops, underruns, hitches, nullProducer count, mixBufOverflow count, scheduler gap (max/mean ms), and queue depth (min/max samples). A skip during that window will light up exactly one bucket — that tells you whether the bug is OS jitter (hitches), RSP starvation (nullProducer), consumer stall (high max buffered), or mod mixer overflow (mixBufOverflow > 0).

### Follow-up once any track clears

- If Bug C resolves from a single ENDSCREEN.DIAG trace → promote the instrumentation's finding into a proper fix and then reduce `ENDSCREEN_DIAG_MAX_PRINTS` from 80 to 10 (keep a small safety log for regressions).
- If Chicago crash identifies a specific subsystem → add more granular breadcrumbs inside that subsystem (e.g., inside collision.c per-probe if the last entry is always `BWALK.TICK`) and ship a second diagnostic drop.
- If `AUDIO.DIAG: mixBufOverflow > 0` on the first repro → `s_MixBuf` needs to grow past 8192 samples, or `modMusicMixInto` needs to chunk. Easy fix.
- If `CHR.DIAG: WARNING … invisible=1 … VOID(-1)` on every spawn → FIX-A.2 is the area, but we now have the fingerprint for an exact reproducer.

---

## Open — 2026-04-16 (S299 — trusting-banach)

### Playtest verification of Input Authority Phase 2 (menu pool)

Reference: `context/designs/input-authority-and-menu-pool-2026-04-13.md` §6, commit on `claude/trusting-banach-a43c0e`.

- **Structural dedup at `menuPushDialog`** — Try to force a duplicate push: from the main menu, open any dialog (e.g. Cheats) and attempt to invoke the same menu again via a second binding (keyboard + controller nearly simultaneous). Confirm `pd.log` shows either `MENU: menuPushDialog rejected duplicate def %p` (F-3.1 pointer scan) OR `MENU: menuPushDialog rejected — pool slot [...] already active` (pool layer). No double-open should be possible.
- **Nextsibling respect** — Open the main menu (CI free-roam). Pool should log `MENUPOOL: acquired main_menu ...` and `MENUPOOL: acquired ci_options ...` (the auto-opened sibling). No B-153-style double-render. Close with Esc.
- **Force-close cleanup** — Start a solo mission → End Game → Exit to Main Menu. Confirm `pd.log` shows `MENUPOOL: released N slot(s) (bulk)` alongside the existing `INPUTCTX: 'imgui_menu' marked for deferred removal`.
- **Stage-transition reset** — Transition from main menu → mission load → gameplay. Pool should be cleanly empty during and after `inputCtxShutdown` / `inputCtxInit`. Tail `pd.log` across the transition for any residual `MENUPOOL:` lines after the stage has loaded.
- **Regression sweep** — Repeat the S296/S297 playtest checklists (stuck WASD, double main menu, textbox leak, chrome mod visibility). The pool is identity-only this session and should not affect those existing fixes.

### Follow-up queued for future sessions

- ~~Migrate the 10 ImGui renderer `s_*PushedCtx` bools to pool-owned input-context (see ADR §6.1b).~~ DONE S300 — all ten files migrated to `menupoolAcquireDialog` / `menupoolReleaseDialog` pattern; pool attaches ctx to already-active slots; three new `MENU_TYPE_*` values added for mpsettings sub-dialogs that can stack.
- Per-scope state arrays for the shared-action leak (ADR §6.2 follow-up). Still open.
- Unit/integration tests for menu pool (ADR §6.3).

---

## Open — 2026-04-16 (S298 — stoic-proskuriakova)

### Playtest verification of the S298 follow-up batch

Commit on `dev` (fast-forwarded from `claude/stoic-proskuriakova-2440de`). Build: `PerfectDark.exe` 51,363,805 / `PerfectDarkServer.exe` 22,838,411 bytes.

- **Content inset in endscreens** — load a chrome style with large nineslice corners (e.g. 24+ px `border_scale: 2.0` on the Nine-Slice template). Play a solo mission to end; play a Combat Sim match to end. Expect: DEBRIEF / OBJECTIVES columns, rankings table, awards, and the action button row all sit inside the inner frame — no text or button bleeds into the chrome border. Pause menu tabs + Resume button same check.
- **Theme Editor docked footer** — open Main Menu → Theme Editor. On 720p and 1080p verify the Save-as-Mod inputs + Reset/Close buttons are always visible even as the color list is scrolled, and scrolling only affects the color pickers.
- **Room Start Match docked footer** — host a room on a narrow window (resize game window to ~900 px wide), fill the bot list, toggle tabs (Combat Simulator / Campaign / Counter-Op / Level Editor). Expect Start Match + Leave Room button row to always be pinned at the bottom of the dialog, never clipped or pushed offscreen.
- **Deep Sea end-path OOB** — replay Deep Sea co-op → mission complete → expect the next-mission transition to land cleanly on the Deep Sea follow-up (no AV / no -1 index crash even when the campaign list has been modified by mods).
- **SP-1 guard — `endscreenSetCoopCompleted`** — complete any co-op mission; sanity-check no crash from the `1 << stageindex` shift on a mod stage whose `stageindex >= 32`. No visible UI change, just no crash.
- **Manifest scanner FIX-B.1** — play through a mission with cinematic spawns (Deep Sea intro, Crash Site intro, any stage with SPAWNCHRATPAD-driven cutscene chrs). Tail `pd.log` for `manifest-diff:` lines; expect the set of `load` entries to include bodies/heads/models referenced by intro/ailist scans, not just the static props. Best signal: no more "CHR 0xXX missing from manifest" runtime warnings that previously showed up during Deep Sea act 2 cinematic.
- **Spawn pool residuals**:
  - **Reservation** — 32-bot Chicago, inspect log for `SPAWN: initial MP spawn via pool[N]` lines. Every N should be unique (no duplicates across the 32+ entries from the same match-start tick).
  - **Wall-probe orientation** — same match, watch the initial facing of bots on a map with lots of pocket spawns (G5 elevators, Skedar Ruins recessed spawns). Bots should face out of pockets, not into the corner.
  - **Neighbour-room ground check** — on any stage with portal seams (Felicity balconies, Temple bridges) verify no bots spawn mid-air or fall through portal boundaries on first-spawn.

### Follow-up if any of the seven recur

- If content inset clips persist, dump `pdguiThemeGetContentInset` values at render time and compare against `pdguiNinesliceGet(s_CfgUiChromeStyleId)->dst_*` corner values.
- If manifest scanner misses an asset, log the ailist index + cmd[0]..cmd[7] hex bytes for the suspect command — the scanner's dispatch may need an extra AICMD case.
- If reservation bitset exhausts the pool during normal play, drop to logging `s_SpawnReserved[]` snapshots around each select call; expected pattern is "reset each tick" — if it survives longer, `g_Vars.lvframenum` is not advancing as expected.

---

## Open — 2026-04-16 (S297 — silly-jepsen)

### Playtest verification of B-154 / B-155 / B-156

Commit on `claude/silly-jepsen-cc481a`. Build: pd 51,424,970 / pd-server 22,816,554 bytes.

- **B-154 (textbox input leak)** — Open each of these and type alphabetic keys, confirming the background player takes no action:
  - Modding Hub → Nine-Slice Chrome → "Mod Name" InputText: type `Weapon` / `Esteemed` / `Tactical` — no ACTION_USE / ACTION_LOOK / etc.
  - Modding Hub → Nine-Slice Chrome → "Image" path InputText.
  - Skin Editor → name field.
  - MP connect-code entry field.
  - MP chat InputText (if active).
  - Verify Esc still closes each menu and Return/Enter still submits (these are the only keys that bypass the new gate).
- **B-155 (chrome mod visibility in Mods list)** — Nine-Slice Chrome → load image → Save as Mod. Open Modding Hub → Mods tab. The new entry (`user.<slug>.ui-chrome`) should appear immediately, enabled (checked). Quit + relaunch; confirm the mod is still in the list and still enabled (config persisted via `modmgrSaveConfig`). Also verify pending (unapplied) enable/disable toggles on OTHER mods are preserved across the save-triggered rescan (pick a mod, flip enabled, save a chrome mod, confirm the flipped mod's state survived).
- **B-156 (chrome mod visibility in Video dropdown)** — Same save flow. Open Settings → Video → UI Chrome Style. The new chrome style name should appear between "Procedural" and any other discovered style. Select it; chrome updates live. Restart the game; confirm the style persists (`Video.UiChromeStyleId`) and is still in the dropdown.

### Follow-up if any of the three recur

- If B-154 recurs: capture `io.WantCaptureKeyboard` + `inputCtxGetTopName()` state around the leak (add temporary `sysLogPrintf` in the new gate block). If WantCaptureKeyboard reads 0 during an active InputText, upgrade the gate to also consult `io.WantTextInput`.
- If B-155 recurs: verify `modmgrRescanDirectory()` actually finds the new dir — add `sysLogPrintf` listing each candidate `modsdir` and the path the scan decided to walk. Path-mismatch between `$E/../mods` vs `./mods` is the likely suspect.
- If B-156 recurs: grep `pd.log` for `UI.CHROME: registered style` — if the expected id isn't logged, re-trace `s_parseChromeManifest` (dump `has_chrome_tag / out_tex_id / out_tex_file / has_nineslice` just before the final `return`). The S297 float-tokenizer fix doesn't cover every possible JSON parser weakness; `\u` escapes are also unhandled, for instance.
---

## Open — 2026-04-16 (S297 — elegant-mahavira)

### Playtest verification of the S297 UI polish drop

Commit on `claude/elegant-mahavira-198cc0`.

- **Docked Chrome tool footer** — Open Modding Hub → Nine-Slice Chrome, load any image ≥ 1024 × 1024, resize the game window smaller (e.g. 720 p).  The left sidebar should show both **Source Preview (with rulers)** and **Frame Preview (assembled nine-slice)** stacked vertically without scrolling; the right column scrolls through all sliders/toggles/presets; **Save as Mod** / **Reset** remain visible at the bottom at all window sizes.  Verify the two previews still live-update when sliders change.
- **Room member list (teams on)** — Start a room with `Teams` option enabled and at least two human players across two teams plus a few bots.  Verify:
  - Members are grouped by team, humans render before bots within each team.
  - Each team band shows a `-- Team N --` header in that team's color (Red/Blue/Green/Yellow/…).
  - Row background behind each name is tinted to the team color (18 % alpha for others, 35 % for the local player).
  - Local player's row also gets a 2 px white left-edge accent bar.
- **Room member list (teams off)** — Same flow without `Teams` enabled.  Verify no team separators are emitted, local-player row still has a subtle cyan background + accent bar, and bots still render after humans.
- **Title-bar styles** — Open Settings → Video, scroll to `UI Chrome Style`, change `Title Bar Style` through all five values.  Expect:
  - Classic (default) — original 3-color PD gradient.
  - Solid — flat `dialog_border1` band.
  - Vertical Bars — lighter `titlebg` base with darker 1-px stripes every 8 px.
  - Scanlines — classic gradient with 1-px horizontal scanlines.
  - Diagonal Stripes — border1 base with 45° `titlebg` bars every 10 px.
  Verify persistence: restart client, style should be restored from `pd.ini`.
- **Content-inset API (smoke)** — Toggle chrome on, then off.  No visible difference in existing menus (the API is additive; no caller wired yet).  Expect `PDGUI theme: D5.0 early init (...)` log line unchanged.

### Follow-up tasks queued from S297 — CLOSED S298

- ~~Migrate Theme Editor / Room `Start Match` action rows to the Chrome tool's child → child → footer pattern for overflow resilience.~~ DONE S298 — Theme Editor uses explicit `footerH` reservation with pinned Save/Reset/Close row; Room `pdguiRoomScreenRender` pins Start Match/Leave Room footer at `dialogH - footerH`.
- ~~Wire `pdguiThemeGetContentInset` into custom-drawn menus (endscreen, scorecard, HUD overlays) so they never clip into nineslice borders.~~ DONE S298 — wired into `renderSoloEndscreen`, `renderMpEndscreen`, and `renderPauseMenu` via `resolveEndscreenPadding`. Scorecard + HUD don't use `pdguiDrawPdDialog` so no change needed.

---

## Open — 2026-04-16 (S296 — vigilant-robinson)

### Playtest verification of B-152 (stuck WASD) and B-153 (double main menu)

Commit on `claude/vigilant-robinson-ba0ee9`. Reproduction source: `019d97ef-pdclient.log`.

- **B-152 (stuck WASD)** — keyboard-only recommended. Open main menu from CI free-roam, close with Esc, press+release W individually. Character should stop on release. Repeat with A, S, D. Do the same with a controller plugged in to verify the controller path is unaffected (axis should continue to reset each frame from the stick poll). Tail `pd.log` for `BMOVE:` lines — `AXIS_MOVE=0.000,0.000` should be visible after each release, not stuck at 1.0.
- **B-153 (double main menu)** — open main menu → press Esc immediately to close → press Esc again within < 1s to reopen. Single menu copy should render with normal backdrop. Repeat 5+ times to confirm no spurious double-render. Test controller B-button path as well (Mike flagged "also with controller I think").

### Follow-up if B-152 or B-153 recur

- If stuck-axis returns: capture `pd.log` with the BMOVE lines straddling the close → stuck window; check whether `.value` is being written from an unexpected path. Consider adding verbose diagnostic to `actionmapPollFrame`'s synthesis block under `sysLogGetVerbose()`.
- If double-menu returns: hotswap queue is the next place to instrument. Dump `s_Queue` contents (name + dialogdef pointer) each frame when it has > 1 entry. Likely candidate: a new renderer or mod attaching to a dialogdef that's in a nextsibling chain.

---

## Open — 2026-04-16 (S295)

### Playtest verification of the S295 collision + spawning drop

- **B-145 slope jump**: on any Skedar ramp / Carrington stairs / outdoor slope, spam jump while walking up — every press should fire `JUMP: APPLIED` in the log, not `JUMP: BLOCKED`.
- **B-146 pickup walkthrough**: drop a rifle / sniper / launcher in an MP arena, walk into the model — capsule should push the prop or pass through, never step up onto it. Multi-ammocrate piles should be identically non-standable.
- **B-147 ceiling clip**: jump against the edge of a slanted ceiling (Skedar temple, G5 / CI corridor bends). Head should stop at the ceiling, not poke through on the diagonal.
- **B-148 Carrington tables**: break-room tables — player should stand on the top face, not fall through the middle.
- **B-149 spawn pool**: multi-bot match in a small arena with scattered pickups. Over 5 rounds, confirm no mid-air spawns, no "stuck in wall" spawns, no spawns standing on a dropped weapon / crate. Cross-check pool dump in `pd.log` for L4 last-resort entries.

### Scheduled architectural follow-ups (deferred, not regressed by S295)

- **Issue 1-B** (mesh ceiling wiring): `classifyTriFlags` emits a real `GEOFLAG_CEILING`, `meshFindCeiling` filters on normal.y; wire into `bondwalk.c` pre-move clamp and into `capsuleSweep` for upward motion.
- **Issue 2-B** (per-prop mesh extraction): extend `meshWorldAddRoomGeo`-style top-face extraction to per-prop colmesh so desks/crates/tables get correct top faces generally.
- ~~**Issue 5 residual**~~: DONE S298 — `s_SpawnReserved[]` same-tick bitset in `spawnPoolSelect` (auto-cleared on `g_Vars.lvframenum` change + pool rebuild), `spawn_point_t.angle_rad` wall-probe stored at build time and used from `playerreset.c`, `spawnPoolValidateCandidate` passes `bgFindRoomsByPos`-collected neighbour rooms into `cdFindGroundInfoAtCyl`.

---

### Playtest verification of the 2026-04-16 menu dead-input desync fixes (B-150)

Reference: `context/scratch/menu-system-investigation-2026-04-16.md` §7, commit on `claude/relaxed-ride`. (Originally tracked as B-145 in the investigation; renumbered to B-150 after B-145..B-149 were claimed by the S295 collision drop.)

- **F1 — g_PdguiActive removed** — toggle F12 overlay on/off several times in CI free-roam; overlay should appear/disappear and player input toggle correctly each time; verify no "F12 does nothing" regression after rapid cycles.
- **F2 — dead `pdguiGameOverRender` body removed** — no behavioral change expected; just confirm no crash on MP end screen (hotswap endscreen path remains the sole renderer).
- **F3 — `inputCtxEndFrame` watchdog** — tail `pd.log` across a full session (title → solo mission → MP match → end screen → main menu → quit). Expect **zero** `INPUTCTX watchdog:` lines. Any occurrence identifies a remaining leak; capture the stack-dump context.
- **F4 — Begin()=false leak guards (9 renderers)** — stress navigating sub-dialogs quickly (Bot Setup → edit sim → back → back; MP Settings → Soundtrack → Select Tunes → back → back). On any transient window cull we should not see the player frozen without a menu visible.
- **F5 — MpEndscreen one-shot push** — MP match → pause → End Game → verify first-frame clickability of endscreen (original Bug C repro). Then Return-to-Lobby → ensure player control resumes in the lobby.
- **F6 — force-close contract** — no runtime change; review by human.
- **F7 — `s_MainMenuPushedCtx` removed** — open/close main menu from CI multiple times; then from CI exit via Quit → ensure the close path still pops the context (watch for any "menu gone but player frozen" state).

---

## Open — 2026-04-16 (S295 match-pipeline track)

### Playtest verification of S295 match-pipeline fixes (festive-saha worktree)

- **B-151 (GAP-1)** — Online co-op advance past Deep Sea. Expect clean stage transition, no `c0000005`. (Originally tracked as B-145 in the match-pipeline investigation; renumbered to B-151.)
- **C-1 (Challenges)** — Open Combat Challenges with a controller only. D-pad should move the selection from first frame; no "extra key to wake up" gap.
- **Bug C (MP endscreen)** — Next end-of-match repro: tail `pd.log` for `ENDSCREEN:` lines. If either appears, that narrows the six hypotheses (`Begin=false` = window-level issue; `contentH clamped` = layout-arithmetic issue).
- **GAP-3** — Online pause → End Game (Confirm?): expect endscreen to render with rankings/awards before Disconnect. Previously client jumped straight to main menu.
- **C-3/C-4/C-5/C-6/C-8 focus sweep** — Open Team Control, Control Diagram, Cheats → warning + unlock confirm, Player Handicaps, Modding Hub each with controller only: D-pad nav should work from first frame.
- **GAP-4** — After an online match ends, verify the client is not stuck with stale `g_NetMatchRoomId`. Leaving the room and rejoining should have clean state.
- **GAP-10** — When SVC_MATCH_CANCELLED fires, countdown overlay must clear (already covered by memset; this is defense-in-depth via `pdguiCountdownReset`).

---

## Open — 2026-04-16 (S293)

### Playtest verification of the 2026-04-16 Nine-Slice + mods folder drop

- **S293 Nine-Slice Chrome redesign** — verify:
  - Save-as-Mod writes to `mods/UI Chrome/<slug>/` and immediately activates in Settings → Video → UI Chrome Style.
  - Border Scale slider (0.25x–4.0x) changes on-screen corner thickness without rebaking pixels.
  - Proportional Insets toggle: on → sliders show `%`, resolved pixel insets print under the sliders and track Scale X/Y; off → sliders revert to absolute pixels.
  - Import a large image (e.g., 8K screenshot) — expect a clear "Image too large" status line, no crash.
  - Scale X/Y to 400% on a 2K image — expect preview to cap at 4096px, status shows OOM-style hint only if the cap is still too large.
  - Trim sliders: dragging Trim Left past `ImgW - TrimRight - 1` is blocked; no 1-pixel degenerate crop.
  - Mod name with a literal double quote (`foo"bar`) → saved `mod.json` parses cleanly on next scan (no registry drop).
- **S293 mods/ category subfolder scanning** — verify:
  - Existing flat mods (`mods/base-ui/`, `mods/pd-modern-ui/`, `mods/bot-names/`) still load normally.
  - A chrome mod saved to `mods/UI Chrome/<slug>/` appears in Mod Manager and in Settings → Video → UI Chrome Style.
  - Creating an empty `mods/Weapons/` (or similar) does not break the scanner.

### Audit follow-ups deferred from S293

Non-nine-slice items from `scratch/audit-s255-s292-2026-04-16.md`:
- **C-6** `matchConfigAddBot` hardcoded `1` human count (`matchsetup.c:438`, `netmsg.c:6125` SVC_ROOM_SETTINGS rebuild).
- **S-2** Middle-click back bridge vs Skin Editor canvas pan (`pdgui_backend.cpp:337-343` vs `pdgui_skin_editor.cpp:454`).
- **S-3** Shared `s_PdmsOwnsMenuCtx` across Handicap/SelectTunes/Soundtrack/TeamNames (input ctx leak).
- **S-4** `IsMouseHoveringRect(..., false)` on custom close button not clipped to focused window.
- **S-5** Skin Editor downrez preview buffer never realloced on character change.
- **S-6** Chrome style rescan leaks GL textures (`s_chromeStylesClear` zeros count only).
- **S-11** (mods-apply) `modmgrApplyChanges` missing `pdguiThemeRescanChromeStyles()` call.

---

## Open — 2026-04-13+

> Carried forward from the 2026-04-13 stabilization drop. Forensic detail in
> `scratch/archive/2026-04-13/`.

### Playtest verification of the 2026-04-13 drop

Mike to confirm each on next build. Bug/feature → commit on `dev`:

- **Issue 7** room-settings sync (`287b0bc4`) — non-leader sees leader's bot/player count / arena / timelimit real-time.
- **B-140 Issue B** two-panel Select Tunes (`287b0bc4`) — add/remove mod tracks, hover preview, leader's playlist syncs to room.
- **Issue 2/8** theme rescan (`287b0bc4`) — newly-enabled mod themes appear in Settings → Video without restart.
- **Mod Apply follow-up (S260/S284, `2c1a52bc`)** — verify Apply now rebuilds enabled mod manifests/audio in-place (no forced title restart), themes + mod songs populate immediately, and MP dialog close no longer steals main-menu input context.
- **Mod Apply updater-style popup parity (S285, `2c1a52bc`)** — verify Apply now uses centered updater-style progress window (`Applying Changes`) during catalog rebuild/diff/apply and completion acknowledge flow (`OK` / `OK & Close`).
- **Mod Apply success-state visual parity (S286, `2c1a52bc`)** — verify Apply completion state now uses updater-like green-tinted success window while preserving neutral in-progress styling.
- **Release/build outage hardening (S287, `d136fa4d`)** — verify release/dev-window commit sync now supports forced commit fallback (`--no-verify`) when hooks fail, and build scripts create missing `Build/` automatically before configure/build.
- **Nine-Slice Chrome in-client creator (S288, `1b530d8d`)** — verify Modding Hub now exposes `Nine-Slice Chrome` tab with image import, ruler sliders (`L/R/T/B`) with symmetry toggles, live ruler overlay preview, Save-as-Mod output (`mods/<slug>/mod.json` + `ui_chrome_frame.tga`), and immediate style activation in `Settings -> Video -> UI Chrome Style`.
- **Nine-Slice frame preview + desaturation option (S289, `1b530d8d`)** — verify Nine-Slice Chrome tab now includes assembled frame preview pane (not just source rulers) and optional desaturation slider for tint-friendly saved chrome textures.
- **Nine-Slice transform pipeline + docked actions (S290, `1b530d8d`)** — verify Nine-Slice Chrome supports trim edges, X/Y scaling, center-strip removal (`axis + %`), saves transformed output image, docks `Save as Mod`/`Reset` actions to tool footer, and Back input matches Modding Hub Close-button behavior.
- **Skin Editor character selector input-steal fix (S266, `ed1e9340`)** — verify Modding Hub -> Skin Editor character list selection is stable (mouse + controller). `PageUp/PageDown` (LB/RB tab-cycle mapping) should no longer yank the tool away while selecting characters; list should still render with short content heights.
- **Skin Editor base-capture source fix (S268, `ed1e9340`)** — verify New Skin capture seeds layer-0 from captured source body texture dimensions (not 256x256 preview-FBO screenshot content). Check UV overlay alignment and exported base skin quality on at least one base body and one mod body.
- **ImGui nav parity closure (S267, `ed1e9340`)** — verify Room and Solo Options tab cycling follow action-driven `PageUp/PageDown` mapping (controller + keyboard parity), and Agent Select list no longer traps focus (full traversal via controller and MKB).
- **Mod Apply menu-lock regression fix (S274/S284, `2c1a52bc`)** — verify Modding Hub -> Apply no longer lands in CI with captured mouse + no accessible menus; apply should remain in the current UI flow with the new in-place modal.
- **Song mods missing in Select Tunes follow-up (S291, `35a8daaf`)** — verify both paths now surface music tracks in Mod Tracks and keep add/remove working: (1) component audio INIs with textual `category` values (`music`/`sfx`/`voice`) and (2) Mod Apply in-place rebuild no longer drops enabled `audio.ini` mods from the catalog due stale `mod->loaded` flags.
- **Universal menu mouse-back bridge (S276, `2c1a52bc`)** — verify middle-click backs out of ImGui menus consistently (Main Menu submenus, Solo Mission stack, Room, Pause, Training, typed warning dialogs) without breaking right-click list interactions.
- **Global title-bar close button (S279, `2c1a52bc`)** — verify PD title-bar `X` appears across ImGui dialogs and closes one menu layer per click without affecting existing right-click item actions.
- **Select Tunes Mod Tracks click-toggle fix (S277, `2c1a52bc`)** — verify clicking a mod track in the left list toggles membership (adds to right playlist when absent, removes when already present), and leader sync still updates room peers.
- **Modding Hub diagnostics + skin capture candidate logging (S278, `2c1a52bc`)** — reproduce INI Editor missing entries and Skin Editor black/partial preview; inspect new `modhub.ini:*`, `skin_editor:*`, `pdgui_charpreview:*`, and `skin_capture.gfx:*` logs to confirm entry counts, selection/load flow, and selected capture source texture list.
- **Modding Hub popup close-state fix (S280, `2c1a52bc`)** — verify closing Skin Editor popup windows (`X`/`Cancel`) no longer closes Modding Hub via outside-click/escape side effects, and reopening Modding Hub no longer shows stale popup overlays.
- **Skin Editor preview black-panel guard (S281, `2c1a52bc`)** — verify Skin Editor preview pane no longer draws a pitch-black texture when charpreview is not ready; should show rendering status until ready texture is available.
- **UI Chrome style immediate persistence (S282, `2c1a52bc`)** — verify Settings -> Video -> UI Chrome Style now persists immediately when changed (Procedural/Classic survives restart without extra save actions).
- **UI Chrome runtime registration hook + picker persistence (S283, `2c1a52bc`)** — verify chrome style runtime APIs now support importer/save flows (`pdguiThemeRegisterChromeModDir`, `pdguiThemeRescanChromeStyles`), Mod Pack import triggers chrome style rescan immediately, and Settings -> Video persists/restores exact style via `Video.UiChromeStyleId`.
- **S263 systemic pipeline fixes** — verify: Deep Sea coop transition clamps stage index; Counter-Op selected anti player is honored online (not forced to slot 1); co-op/anti launch has no double-transition side effects; team-mode endscreen rankings show player rows (no placeholder '?' entries).
- **B-142** false kills (`4d1e13c1`) — fresh 32-bot Chicago match, idle 30 s, pause → kill counter 0/0.
- **S292 room max-bot/team-mode hardening** (`35a8daaf`) — verify Chicago with max bots from Room UI:
  - Room bot cap now respects runtime bot limit (no `MATCH_MAX_SLOTS` spillover paths).
  - Team-enabled + newly added bots no longer default to all one team (balanced default assignment).
  - Scoreboard should no longer show all `T1` by default in team mode; bots should engage and take damage.
- **B-143 End-Game-Crash** + modal confirm (`d37e9677`) — End Game → Confirm → no AV, CI training loads.
- **B-141 telemetry** (`5a42f234`) — on next audio-skip repro, tail `pd.log` for `AUDIO[B-141]`.
- **B-134 spawn validator** (`0b44b2b8`) — Chicago fire-escape area, no railing-interior spawn.
- **S250 input authority Phase 1** (`5098f903`) — Ctrl+V in Online window = no background jump; hold W → menu → close = no residual walk.
- **Dev-window-v2 polish** — S248 font/control baseline (`11fd1d5e`); S255 adds Pull/Push + DPI/text layout (verify legibility on your display scaling).
- **Bug B** countdown-cancel-on-room-close (`731831ec`, 2026-04-14) — fixed, awaiting playtest verification. Leader leaves room during countdown or client disconnects mid-countdown → 3-2-1 overlay clears, "cancelled" banner shows, no stuck UI. See session-log S254 + spec §7.
- **S264 audit remediation batch** — verify end-to-end:
  - Back/Esc during countdown now cancels from both host and client paths and shows canceller name on all clients.
  - Nested ImGui dialog close paths no longer pop parent-owned menu context (no gameplay input bleed-through while submenu is open).
  - MP scenario/radar SP-6/SP-8 guards hold under sparse player slots and NULL `prop->chr` transitions.
  - Manifest hardening: post-setup SP rescan always diff/applies; unresolved non-base IDs are treated as missing in manifest check.
- **S265 post-merge sanity playtest** — focused confirmation:
  - Counter-Op anti-role selection follows chosen player (`antiClientId`) across host/client.
  - Ready-gate cancel transitions restore lobby state on all peers after `SVC_MATCH_CANCELLED`.
  - Team endscreen rankings show player rows with team grouping/sort (no `?` placeholders).

### Still open (post-drop)

| Item | File / notes |
|------|--------------|
| **Bug C — post-game endscreen partial render** | Scrim + title-bar render; body content invisible. Six hypotheses in `scratch/archive/2026-04-13/session-state-endgame-crash.md` §4c. Needs `sysLogPrintf` instrumentation on each `renderMpEndscreen` early-return + fresh playtest log. |
| **Bug D — invisible networked bots on Chicago** | Chr generation token mismatch likely (FIX-A.2 area). May have cleared with S253; needs post-drop repro. |
| **Chicago silent crash ~9 s** | Needs VEH log + symbolify. May have cleared with today's drop. |
| **Airbase 0xc0000005 / Start-Match no-response** | Log shows manifest OK, no SVC_STAGE_START. May share root cause with Bug A (fix shipped). Needs post-drop repro. |
| **Input authority Phase 2** | Menu pool single-instance discipline. ADR: `designs/input-authority-and-menu-pool-2026-04-13.md` §6. Scope: `src/game/menu.c`, `pdgui_backend.cpp`, possibly new `port/src/menupool.c`. Dedicated session. |
| **B-141 audio skips — root cause** | Blocked on repro against telemetry. |
| **FIX-B.1 deep manifest scanner** | `netmanifest.c`, `setup.c`. Cinematics + AI scripts spawn assets not in the setup list. FIX-B.2 dependency DONE. |
| **Manifest gap follow-ups** — AUDIT S300 | (1) `manifestBuildForMenu()` + `manifestMenuTransition()` live at netmanifest.c:605/1615 and wired from pdmain.c:772; S300 added per-category counters (bodies/heads × mod/disabled) so `MANIFEST-MENU: built …` log line makes mod-miss obvious. (2) SP pre/post-load split IS in place (`manifestSPTransition` pre-load in mainChangeToStage, `manifestSPRescanSetup` post-load in `lvInit`); S300 added `rescan diff — newly-discovered=N` log so post-load cinematic/ailist discovery is measurable. (3) Safety-net still via `manifestEnsureLoaded` at spawn time. Remaining gap: none identified; any new regression will show up in the per-category diagnostic log. |

### Audit follow-ups (S262 — closed in S300)

- ~~**Room-scope teardown hygiene (SP-14 follow-up)**~~ — DONE S300 — `netDisconnect` + `netStartServer` reset `g_NetMatchRoomId = 0xFF` and `g_NetCounterOpClientId = NET_NULL_CLIENT` defensively. Stage-end path (`netServerStageEnd` / `netmsgSvcStageEndRead`) already handled the normal teardown; S300 closes the disconnect-mid-match gap.

### Audit follow-ups (S262 — closed in S298)

- ~~**Campaign end-path OOB guard**~~ — DONE S298 (lower-bound check added to menutick.c alongside S264 upper-bound clamp).
- ~~**Endscreen menu index safety (SP-1)**~~ — DONE (`endscreenPushCoop/Anti` guards already in place; S298 extends the same guard pattern into `endscreenSetCoopCompleted`).
- ~~**Team rankings data/UI mismatch**~~ — DONE S295 (`buildRankings` now uses `mpGetPlayerRankings` + team sort; verified in S298).

---

## Build / Release

| Item | Status | Detail |
|------|--------|--------|
| **Git sync before Build/Release (dev-window-v2)** | DONE (S257) | `Invoke-GitSyncBeforeBuild` + `release.ps1` index-safe commit before `pull --rebase`. See `session-log` S257, `CRITICAL-PROCEDURES.md`. |
| **Git index.lock path fidelity (dev-window-v2)** | DONE (S269/S270/S272/S273) | Lock cleanup handles path-format mismatch while treating dev-root `.git\\index.lock` as canonical (S272). S273 adds same-runtime MSYS cleanup: resolve `rm`/`cygpath` from the active `git.exe` root and remove the derived MSYS lock path (`/home/.../.git/index.lock`) plus Windows/WSL paths. |
| **Cursor commit method (PowerShell-safe)** | DONE (S271) | Added always-apply rule `.cursor/rules/powershell-git-commit-message.mdc` so multiline commit messages use PowerShell here-strings (`$msg = @'...'@; git commit -m $msg`) instead of bash heredoc syntax. |
| **Dev-window python command robustness** | DONE (S261) | Dev-window-v2 + headless configure now pass `-DPD_PYTHON_EXECUTABLE=C:/msys64/usr/bin/python3.exe` explicitly so asset generator custom commands never depend on `python3` being on child PATH. |
| **Static link / DLL elimination** | DONE (S224) — **Mike: verify with objdump** | CMakeLists.txt: SDL2 deps completed (dinput8/dxguid/shell32/user32/uuid), DLL copy block removed. Carve-out: `opengl32.dll` only. Verify: `objdump -p Build/PerfectDark.exe \| grep "DLL Name"` should show only system DLLs. Design: `designs/static-link-dll-elimination-2026-04-13.md`. |
| **L0-LINK: pdguiThemeRegisterModDir server link** | DONE (S231) | Stub confirmed at `port/src/server_stubs.c:417`. Both-targets link verify pending Mike's build. |
| **L0-BUILD: ccache warm-build regression** | CODE DONE (S231) — **Mike: run warm-build timing verify** | `CCACHE_SLOPPINESS=pch_defines,time_macros` in all 3 build scripts. Target: warm `pd` <12 s (was 30.7 s after PCH in `955dffa2`). Run `.\devtools\build-headless.ps1 -Target client` twice; second run should be <12 s. |

---

## Input Authority & Menu Pool (ADR 2026-04-13)

ADR: `context/designs/input-authority-and-menu-pool-2026-04-13.md`

Phase 1 — gameplay-input authority predicate — ✅ DONE (S250, merge `5098f903`).

- `gameplayInputSuppressed()` single truth-source (context-stack top, window focus, 50 ms focus-regain settle).
- Dispatch-site gate in `fireVk()` + read-site gates in `actionPressed/Held/Released/Value/Axis` for gameplay-only actions.
- `actionmapFlushGameplayState()` on `inputCtxPush` (fresh + resurrect) + focus-lost/regain — held keys synthesise clean released edge.
- SDL `WINDOWEVENT_FOCUS_LOST/GAINED` wired in `gfx_sdl2.cpp`.

Phase 2 — menu pool single-instance discipline — QUEUED (own session). Pre-allocated slots keyed by type; open=populate+activate, close=deactivate+clear; duplicate-push structurally impossible. Also resolves shared-action leak (background bondmove reading `ACTION_USE` while menu owns A). Scope: `src/game/menu.c`, `port/fast3d/pdgui_backend.cpp`, possibly new `port/src/menupool.c`. ADR §6.

---

## B-141 Audio Telemetry (S251 — investigation)

B-141 = audio skips / pauses intermittent (2026-04-13 playtest, not reproducible on demand). Telemetry shipped `5a42f234`; waiting for repro to narrow mechanism.

- `audioEndFrame()` counts drops / underruns / hitches (>50 ms inter-frame); always on, low overhead.
- `Audio.VerboseLog=1` in pd.ini → per-event `AUDIO[B-141]` lines.
- Auto 30 s summary if any counter moved; zero-activity windows silent.
- Accessor: `audioGetB141Counters()` for diagnostic UI.
- **Root cause + fix blocked on repro**. Expected mechanism differs by dominant counter: hitch-dominated = main-loop stall (profile RSP or render), underrun-dominated = scheduler preemption / buffer too small, drop-dominated = producer runs ahead during slow frames.

---

## ✅ Shipped 2026-04-13 — MP Lobby / Mod Stabilization Drop

S248 → S253 batch all on `dev`. Session-by-session detail in `session-log.md`;
bug-level detail in `bugs.md`; forensic handoffs in
`scratch/archive/2026-04-13/`.

| Session | Commit | Scope |
|---------|--------|-------|
| S248 | `16de65e6` | B-135 / B-136 / B-137 / B-138 / B-139 + Songs F-2.1 |
| Dev-window polish | `11fd1d5e` | font/control size (parallel `agitated-jackson`) |
| S249 | `e13c2d1f` | B-140 Issue A playlist auto-advance |
| S249 | `0b44b2b8` | B-134 spawn validator railing trap |
| S250 | `5098f903` | Input authority Phase 1 |
| S251 | `5a42f234` | B-141 audio telemetry |
| S252 | `d37e9677`, `4d1e13c1` | B-143 End-Game-Crash + modal confirm; B-142 false kills |
| S253 | `82d0c1f3` → `287b0bc4` | Issue 7, Weapons F-2.1, Issue 2/8, B-140 Issue B |

---

## Backlog (Post v0.1.0)

| Item | Target | Detail |
|------|--------|--------|
| **D5 Phase 5 -- Lobby scene** | v0.1.0+ | Player portraits, connected player avatars, character preview |
| **D14a -- Counter-Op mode** | v0.6.0 | NPC possession mechanic |
| **D15 -- Map Editor / Forge** | v0.5.0 | Level editor, character creator, skin system |
| **D16 -- Master Server** | v0.4.0 | Server registry, heartbeat, browser |
| **D6 -- Persistent Stats** | v1.0.0 | JSON/SQLite stats, post-game scorecard, lifetime viewer |
| **D7 -- Discord Rich Presence** | v1.0.0 | Activity API, join button |
| **D10 -- Spectator Mode** | v0.6.0 | Free-cam, follow-cam, HUD overlay |

---

## Phase-2 Feature Lines (SHIPPED)

### Audio Mod Menu — A-1 → A-7 (COMPLETE 2026-04-12)

Feature complete. Full batch line shipped 2026-04-12: catalog extension (A-1) → mod music stream (A-2) → Audio Mod Menu UI (A-3) → Soundtrack Menu extension (A-4) → pack creation (A-5) → multi-format import (A-6, MP3/OGG/WAV) → network sync (A-7). Seamless network sync added later same day. Protocol v32 → v33 → v34. Detail: `daily-logs/2026-04-12.md`.

### Skin Editor — S-1 → S-9 (COMPLETE 2026-04-12)

Feature complete. Full batch line shipped 2026-04-12: canvas + 2D editor (S-1/S-3) → live 3D preview (S-2) → save-as-mod (S-4) → image import (S-5) → PD-style downrez / quantize (S-6) → blend modes (S-7) → UV wireframe (S-8) → network sync via existing ASSET_SKIN infrastructure (S-9). Detail: `daily-logs/2026-04-12.md`.

### Mod Map Import Pipeline (L3) — COMPLETE 2026-04-13

M-5.x (`mapimport.h`/`mapimport.c` — 6-stage pipeline: PARSE / NORMALIZE / GENERATE / EMIT / VALIDATE / REGISTER) + M-6.x (Modding Hub tab + dialog + Smoke Test + `mapImportRunFull()` wrapper) + M-7.x (retroactive validation via `spawnPoolSmokeAll()` + CSV output). Detail: session-log S240 / S244 / S246.

### Spawn System (L2 Architecture) — COMPLETE 2026-04-13

L1-L4 fallback chain (`spawnpool.c/h`): raycast-budget validator + L1 declared + L2 waypoint + L3 grid + L4 radial. Deterministic from `hash(stage_id) ^ match_seed`. `g_SpawnPoints` expanded 24→40. `match_seed` in `SVC_STAGE_START` (v35). Farthest-first greedy selection in `spawnPoolSelect()`. B-134 capsule-radius threshold (SPAWNPOOL_CAPSULE_RADIUS=30 — see `constraints.md`). Detail: session-log S239 / S241 / S242 / S249.

---

## Master Orchestration Plan — Status 2026-04-13

Plan: `context/designs/master-orchestration-plan-2026-04-13.md`

L0–L7 complete. Full layer-by-layer status (L0-BUILD / L0-LINK / F-0.1/2/3/4 /
FIX-A / L1-1 / FIX-B.2 / L1-2/3/4/5 / F-1.1/2/3/4 / F-2.1/2 / F-3.1/2/3 /
FIX-G / FIX-F) — see `session-log.md` S231 – S253.

Open supporting items:

| ID | Title | Status |
|----|-------|--------|
| **FIX-B.1** | Deep manifest scanner (cinematics + AI scripts) | DONE S298 — `netmanifest.c` now scans `g_StageSetup.intro` + `g_StageSetup.ailists`. |

---

## Design Guidelines (Planned)

| System | Status |
|--------|--------|
| Menu/UI | DONE (in `designs/d5-full-menu-overhaul.md`) |
| Input | IMPLEMENTED (M0.2 action maps). Guidelines doc PLANNED. |
| Networking | PLANNED |
| Mod System | PLANNED |
| Audio | PLANNED |
| Visual/Theme | PLANNED |
| Collision/Physics | PLANNED |
| Level Editor (Forge) | PLANNED |
