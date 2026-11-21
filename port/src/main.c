#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <n_libaudio.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "lib/anim.h"
#include "lib/mempc.h"
#include "lib/meshcollision.h"
#include "bss.h"
#include "data.h"

#include "video.h"
#include "audio.h"
#include "input.h"
#include "inputctx.h"
#include "fs.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "stage_archive_repair.h"
#include "scenario_global_ailists.h"
#include "config.h"
#include "mod.h"
#include "modmgr.h"
#include "modmgr_save_status.h"
#include "modarchive.h"
#include "modvfs.h"
#include "modelcatalog.h"
#include "pdgui.h"
#include "pdgui_font_mod.h"
#include "pdgui_theme_loader.h"
/* menumgr.h removed — P10 D5.7 OG Menu Removal */
#include "playerstats.h"
#include "achievements.h"
#include "system.h"
#include "memsizes.h"
#include "native_write_policy.h"
#include "console.h"
#include "utils.h"
#include "net/net.h"
#include "net/netenet.h"
#include "net/netmsg.h"
#include "net/grid_edit_room_net.h"
#include "net/grid_mod_proposal.h"
#include "room.h"
#include "net/room_ui.h"
#include "menupool.h"
#include "net/netdistrib.h"
#include "net/p2p.h"
#include "net/group_session.h"
#include "updater.h"
#include "actionmap.h"
#include "inputlayer.h"
#include "scene.h"
#include "savemigrate.h"
#include "savefile.h"
#include "agent_session.h"
#include "v006_save_harness.h"
#include "v006_scenario_harness.h"
#include "asset_source_harness.h"
#include "asset_catalog_relocation_harness.h"
#include "body_head_source_harness.h"
#include "testscenarios.h"
#include "net/matchsetup.h"
#include "prefs_agent.h"
#include "discord.h"
#include "identity.h"
#include "social.h"
#include "social_hub.h"
#include "presence.h"
#include "chat.h"
#include "file_transfer.h"
#include "pdgui_toast.h"
#include "spectator.h"
#include "theater.h"
#include "listening_room.h"
#include "social_share.h"
#include "voice.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "assetcatalog_cache.h"
#include "assetprovider.h"
#include "asset_runtime.h"
#include "effect_instance_runtime.h"
#include "player_stage_init_fault.h"
#include "combat_sim_verify.h"
#include "asset_source_debug.h"
#include "modasset_compiler.h"
#include "mod_author_runtime.h"
#include "forge_level_io.h"
#include "forge_revision_store.h"
#include "grid_archive_policy.h"
#include "grid_match_package_io.h"
#include "grid_match_runtime.h"
#include "scenario_scene_renderer.h"
#include "scenario_source_runtime.h"
#include "scenario_stage_rules.h"
#include "forge/forge_core.h"
#include "game/forgemode.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_arenas.h"
#include "game/stagetable.h"
#include "game/chr.h"
#include "game/inv.h"
#include "game/music.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/pad.h"
#include "game/prop.h"
#include "game/propobj.h"
#include "game/setuputils.h"
#include "lib/music.h"
#include "lib/snd.h"

/* Engine Phase 2: thread pool + progress channel + boot overlay UI. */
#include "boot_pool.h"
#include "boot_progress.h"
#include "pdgui_bootoverlay.h"

/* Smoke verify harness (2026-05-11): client-only headless gate driven by
 * tools/smoke-verify/tests/*.json + tools/smoke-verify/run.ps1.  Inert
 * unless `--smoke <path>` is on the command line. */
#include "smoke_harness.h"
#include "vehicle_debug.h"

u32 g_OsMemSize = 0;
s32 g_OsMemSizeMb = 64;
s8 g_Resetting = false;
OSSched g_Sched;

OSMesgQueue g_MainMesgQueue;
OSMesg g_MainMesgBuf[32];

u8 *g_MempHeap = NULL;
u32 g_MempHeapSize = 0;

u32 g_VmNumTlbMisses = 0;
u32 g_VmNumPageMisses = 0;
u32 g_VmNumPageReplaces = 0;
u8 g_VmShowStats = 0;

s32 g_TickRateDiv = 1;
s32 g_TickExtraSleep = true;

s32 g_SkipIntro = false;

s32 g_JumpLoggingEnabled = 0;

/* Phase 2 fix #6 (input-menu pillar, 2026-05-01): when set, the HUD draws
 * an overlay showing the current interaction-cast half-angle (degrees)
 * + range so Mike can dial fix #5's tightening empirically. Settings ->
 * Debug Flags exposes the toggle + a slider. Default off. */
s32 g_InteractCastDebugDraw = 0;

s32 g_FileAutoSelect = -1;

extern s32 g_StageNum;
extern void pdguiThemeReloadPduiSourceTextures(void);

static void bootArmDebugLoadCatalogAssets(void);
static s32 bootDebugCatalogProbesCanRunBeforeBaseEmit(void);
s32 bootApplyDeferredDebugLoadCatalogAssets(void);

/* ---------------------------------------------------------------- *
 * Smoke-verify CLI fast-paths (c115, 2026-05-13).
 *
 * These flags exist so the smoke-verify pipeline (and Mike's manual
 * playtest dashboard) can drive directly to specific test points
 * without needing scripted menu navigation.  Each flag is parsed at
 * boot, captured into a static global below, and consumed at the
 * relevant lifecycle hook (pre-netInit, post-catalog-init, post-
 * setupCreateProps).  All flags are inert when absent from argv.
 *
 *   --no-net
 *       Gates netInit() entirely.  The network stack never binds,
 *       no UDP listener is created, ENet is never initialised.
 *       Closes the Windows Defender Firewall focus-steal class
 *       (smoke-verify-dispatch-findings-2026-05-13.md, S-3).
 *
 *   --extract-assets-only
 *       Runs the normal catalog/extractor/walker/cache boot path, then exits
 *       before scheduler, stage, gameplay, and render-loop startup. Also skips
 *       netInit and audioInit. Used for stale archive regeneration/validation
 *       without loading a live scene.
 *
 *   --main-menu
 *       Skips boot animation and lands at the title screen with
 *       the main menu auto-opened.  Behaviour: boots through
 *       STAGE_CITRAINING (same path --skip-intro takes) but arms
 *       g_PostExitMainMenuView so the main menu pops after the CI
 *       camera intro finishes.  --skip-intro is left untouched.
 *
 *   --launch-modding-hub
 *       Opens the production Modding Hub directly on its Mod Manager tab
 *       after catalog initialization. This only shortcuts navigation to the
 *       existing UI; toggles, validation, Apply Changes, catalog rebuilds,
 *       and rollback continue through the ordinary Mod Manager path.
 *
 *   --mod-author <recipe.json>
 *       Saves and enables the mod a recipe file describes
 *       (mod_author_recipe.h) through the same save, rescan, enable and
 *       apply path the in-game creators use. Runs once the catalog is ready
 *       and before every other launch or debug flag, so one launch can
 *       create a mod and then load or spawn what it made. Logged as
 *       MODAUTHOR.* lines.
 *
 *   --debug-weapon-tool-create <weapon catalog id>
 *   --debug-weapon-tool-name <display name>
 *       Runs the Modding Hub's Weapons creator without the mouse: starts
 *       from the named weapon as a template and saves it under the display
 *       name, through the same two functions the Create and Save buttons
 *       call. Runs right after --mod-author. Logged as WEAPONMOD.* lines.
 *
 *   --debug-character-tool-create <character, body or head catalog id>
 *   --debug-character-tool-name <display name>
 *   --debug-character-tool-gender <male|female>
 *   --debug-character-tool-build <human|guard|maian|template>
 *   --debug-character-tool-height <number>
 *       Runs the Modding Hub's Characters creator without the mouse: opens
 *       the named entry as a template, sets the form (gender, build and
 *       height apply to a body or a head; each is optional and keeps the
 *       template's own when left out) and saves it under the display name,
 *       through the functions the Use as Template and Create + Enable
 *       buttons call. Runs right after the weapon tool's flags. Logged as
 *       CHARMOD.* lines.
 *
 *   --debug-level-tool-new <arena catalog id>
 *   --debug-level-tool-name <display name>
 *       Runs the Modding Hub's Levels tool without the mouse: makes a new
 *       level from the named level under the display name, through the
 *       function the Create button calls. Runs right after the character
 *       tool's flags and before --debug-forge-level, so that flag and
 *       --launch-forge-level can name the level it made. Logged as
 *       LEVELTOOL.* lines.
 *
 *   --debug-forge-level <arena catalog id>
 *   --debug-forge-level-name <display name>
 *   --debug-forge-level-weapon <weapon catalog id>
 *       Edits a level without the Forge editor: opens the named arena's
 *       placements (forge_level_io.h), adds a player start a short step in
 *       front of its first one and another spot for its first weapon pickup,
 *       leaves its first ammo crate out, and saves the result as a level mod
 *       under the display name, through the open and save calls the Forge
 *       editor makes. With a weapon id, the copied pickup gives that weapon
 *       of the catalog instead of being one of the match's weapon places.
 *       With --debug-forge-level-vehicle it also makes a hoverbike 600 units
 *       in front of the level's first start, facing the way it faces: the
 *       vehicle smokes' own place to drive (B-1241). With
 *       --debug-forge-level-prop <model catalog id> it also makes a plain prop
 *       drawn with that model, 420 units in front of the first start.
 *       Runs right after the level tool's flags, so one launch can make
 *       the level and then start a match on it. Logged as FORGE.LEVEL.*
 *       lines.
 *
 *   --launch-forge-level <arena catalog id>
 *       Starts a Forge session on the named level once the boot stage has
 *       placed the player: a clean match setup on that level and the stage
 *       change, as choosing it in The Grid does (forgeLevelStartForge). The
 *       editor's Level File section then knows which level it is on.
 *
 *   --debug-forge-session-save <display name>
 *       With --launch-forge-level: two seconds into that level, with the
 *       editor camera flying, opens the level's placements, adds a player
 *       start where the camera is and saves the result as a level mod under
 *       the display name, through the calls the Level File section's buttons
 *       make. It then turns the camera to a yaw away from every axis and a
 *       pitch that is not level, and logs where the editor's world markers
 *       put five known points (FORGE.MARKERS.PROBE), which checks by numbers
 *       the projection and that the editor's angles are the view's. It takes
 *       no screenshot and ends nothing: a smoke
 *       waits on forge_level_file_open and times those itself. Logged as
 *       FORGE.LEVEL.* lines. With --debug-forge-session-undo the added start
 *       is undone and redone before the save, through the session's undo,
 *       as the editor's Undo and Redo buttons do. With
 *       --debug-forge-session-live it then copies and moves the level's
 *       first crate or prop (or makes an ammo crate in a level with none)
 *       ahead of the probe view, for the world to show while the camera
 *       flies (FORGE.LEVEL.LIVE lines).
 *
 *   --launch-credits
 *       After catalog init, enters the NORMAL scrolling Perfect Dark
 *       credits (NOT the alt-title path): mainChangeToStage(
 *       STAGE_CREDITS=0x5c), setNumPlayers(1), bond/coop/anti playernum
 *       seeding, and lvSetDifficulty(DIFF_A).  Deliberately does NOT call
 *       creditsRequestAltTitle() -- that latches the alt-title branch in
 *       creditsReset() which starts solid-black with slidesenabled=false
 *       and only fades the "PERFECT DARK" wordmark in after ~60 frames,
 *       never showing the credit slides.  The normal path keeps
 *       slidesenabled=true so the scrolling credit slides + Handel Gothic
 *       fonts + up-to-500 transparent particles render from frame 0,
 *       letting a smoke capture them (exercises the c3844 universal
 *       alpha-mode fix).  One-shot per boot.  See bootApplyLaunchCredits().
 *
 *   --launch-scenario <empty_map|swarm_cpu|swarm_gpu>
 *       After catalog init, calls testScenarioLaunch(...) for the
 *       named scenario.  empty_map -> TESTSCEN_EMPTY_MAP,
 *       swarm_cpu -> TESTSCEN_SWARM_CPU, swarm_gpu -> TESTSCEN_SWARM_GPU.
 *
 *   --launch-mission <stage_id> [--difficulty <agent|special|perfect>]
 *       stage_id can be hex (0x21) or a symbolic catalog ID
 *       (base:defection).  Seeds g_MissionConfig (difficulty,
 *       stage_id, stagenum) and routes the boot through the resolved
 *       stagenum.
 *
 *   --launch-mp-room <arena_id> <scenario_id> <bot_count>
 *       Seeds g_MatchConfig (stage_id, scenario_id, plus
 *       matchConfigAddBot * bot_count) so the Room screen renders
 *       with everything ready to launch.  Routes the boot through
 *       STAGE_CITRAINING with the Combat Sim room overlay auto-opened.
 *       With --debug-auto-start-match, arms a deferred direct match start
 *       for deterministic match-start smoke coverage after CI setup exists.
 *
 *   --host-autostart [--host-autostart-mode <mp|coop|counterop>]
 *       Once one remote peer reaches the listen-host lobby, replays the same
 *       high-level room start command used by the ordinary Room UI. Combat
 *       Simulator is the default. Co-op and Counter-Op interpret the stage ID
 *       supplied to --launch-mp-room as a typed mission map and use
 *       --difficulty; Counter-Op assigns the one remote room member as Anti.
 *       --host-autostart-peers N waits for N remote peers (default 1); 0 is
 *       for co-op only: the host starts alone, with AI buddies.
 *   --coop-ai-buddies <1..4>
 *       The Room's Partner choice: simulant buddies for a co-op mission the
 *       host (or the offline Room) starts alone, clamped to the original's
 *       limit (getMaxAiBuddies).
 *   --debug-room-tab <combat|campaign|counterop>
 *       The Room opens on that tab; --debug-auto-start-match waits for it.
 *   --launch-challenge <slot>
 *       Starts the challenge in that slot (0-based among the challenges this
 *       profile can play) through matchStartFromChallenge, the ImGui
 *       challenge launch, once the CI boot stage has a player.
 *
 *       --host-autostart-matches <n> (1..8) starts n matches in the same
 *       room: after each match the start re-arms once the host has left its
 *       end screen into the room and the room is back in LOBBY, then waits
 *       about 10 s for the clients before the next start.
 *
 *   --debug-fail-stage-player-init <reset|spawn>:<player>
 *       Smoke-only one-shot fault at the lvReset multi-player transaction
 *       boundary. It is rejected unless --smoke is active and exists solely
 *       to prove reverse rollback and clean stage-abort behavior.
 *
 *   --debug-ready-gate-settings-change
 *       Smoke-only one-shot ordinary-client settings update. It waits for the
 *       exact CLSTATE_PREPARING boundary, changes a valid local handicap, and
 *       sends it through netClientSettingsChanged/CLC_SETTINGS. This proves a
 *       prepared launch rolls back before the validated candidate publishes.
 *
 *   --debug-ready-gate-settings-repeat
 *       Smoke-only one-shot. The same send at the same boundary with nothing
 *       changed, as a Room opening or a settings menu closing sends it
 *       (B-1208): the prepared launch must keep its roster and start.
 *
 *   --debug-spawn-weapon <catalog_id>
 *       After --launch-mp-room seeds g_MatchConfig, forces the spawn weapon
 *       mode to SPECIFIC using the supplied weapon catalog ID.  Used by
 *       archive-source smokes to prove a named .pdweapon is the runtime
 *       weapon rather than relying on Random mode.
 *
 *   --debug-bot-body <catalog_id> / --debug-bot-head <catalog_id>
 *       With --launch-mp-room, assigns added bots the requested body/head
 *       catalog IDs after validating that the catalog rows have private MP
 *       selector indices.  Used by source-render smokes to prove custom
 *       character geometry reaches live rendering.
 *
 *   --debug-place-bot-near-player
 *   --debug-place-bot-near-player-hold-sec <1..60>
 *       Pre-render smoke hook: after a match stage has remained in normal
 *       first-person gameplay long enough to reject intro-camera transients,
 *       moves the first spawned bot into player 0's current room. This lets
 *       render and combat smokes prove the real prop/chr/model/shot path
 *       without depending on random arena spawn visibility. The optional
 *       bounded duration defaults to three seconds. One-shot per boot.
 *
 *   --debug-bots-hold-fire
 *       Smoke-only: simulants never fire a shot (chrTickShoot); they still
 *       move, aim and set off proximity mines. Target-dummy smokes use it
 *       now that bots can fight with mod weapons (T-MODDING-008).
 *
 *   --debug-mount-bike
 *       Post-setupCreateProps hook: walks g_Vars.activeprops on the
 *       first frame after stage load and mounts player 0 on the
 *       first OBJTYPE_HOVERBIKE prop found.  One-shot per boot;
 *       no effect on stages without a hoverbike.
 *
 *   --debug-swap-vehicle <vehicle catalog id>
 *       Stage setup hook: every hoverbike a stage creates takes the
 *       model of the named vehicle, so a mod vehicle can be driven
 *       (with --debug-mount-bike) before levels can place vehicles.
 *       See port/include/vehicle_debug.h.
 *
 *   --debug-mount-seat <driver|gunner>
 *       Every mount of a vehicle whose behavior graph has a gunner seat
 *       asks for that seat, instead of the seat the player's place and
 *       view choose. A one-seat vehicle is not affected.
 *
 *   --debug-spawn-at x,y,z,room
 *       Post-setupCreateProps hook: teleports player 0 to the given
 *       world coordinate + room id on the first frame after stage
 *       load (g_Vars.lvframenum >= 4).  Used by smoke tests that need
 *       deterministic positioning (wall-jump regression, physics
 *       coverage).  Four comma-separated tokens: x, y, z, room (all
 *       parsed as integers via strtol with float coercion for x/y/z;
 *       room is s16 / RoomNum).  One-shot per boot.
 *
 *   --debug-spawn-at-after-intro
 *       Companion for --debug-spawn-at in a mission that has an
 *       intro: the teleport waits until player 0 has had control
 *       (normal tick mode, walking) for one second of game time. An
 *       intro animates the player's own body with absolute positions
 *       and the character tick moves the prop along with it, so a
 *       teleport made while the intro plays is undone on the same
 *       frame (B-1291).
 *
 *   --debug-spawn-at-look-at x,y,z
 *       Companion for --debug-spawn-at: once the teleport has run,
 *       every frame turns player 0's view, yaw and pitch, toward the
 *       given world point, and logs how well the camera looks at it
 *       (look_dot, 1.000 when it looks straight at the point).
 *
 *   --debug-force-first-person
 *       Pre-render smoke hook: after the gameplay stage is loaded, holds
 *       player 0 in CAMERAMODE_DEFAULT for a short window so first-person
 *       weapon render paths can be exercised deterministically. Inert unless
 *       passed on argv; it does not enable weapon diagnostic logging.
 *
 *   --debug-force-first-person-look x,y,z
 *       Optional companion for --debug-force-first-person. Applies a
 *       normalized first-person camera look vector during the same pre-render
 *       window so screenshot smokes can frame source-rendered weapons
 *       deterministically.
 *
 *   --debug-force-first-person-cam-offset x,y,z
 *       Optional companion for --debug-force-first-person. Adds a camera
 *       position offset during the same pre-render window, keeping wall-adjacent
 *       deterministic spawns from occluding screenshot proof.
 *
 *   --debug-weapon-diag
 *       Enables the LOG.WPN.DIAG diagnostic stream without forcing camera
 *       state or generated mesh render auditing. Generated-mesh audit smokes
 *       also get this finite-budget stream through
 *       --debug-generated-mesh-render-audit.
 *
 *   --launch-load-agent <name>
 *       Post-saveInit hook: activates one pre-staged JSON profile through
 *       the same agentSessionActivate transaction used by Agent Select.
 *       Dispatch waits until g_Vars.lvframenum >= 4 so smoke ordering is
 *       deterministic. The profile name is validated by the Agent Profile
 *       Store and the hook fires at most once per boot.
 *
 *   --debug-v006-save-failclosed
 *       Runs the production-linked V-006 setup persistence harness after the
 *       catalog and save directory are ready.  It proves malformed/semantic
 *       JSON and truncated/future binary loads preserve live state, then
 *       injects a pre-replace failure through both save APIs and verifies the
 *       prior destination bytes remain exact.  Smoke-only; exits immediately.
 *
 *   --listen-bind <port>
 *       Post-netInit hook: invokes netStartServer(port, NET_MAX_CLIENTS)
 *       on the first mainTick frame so the client boots straight into a
 *       listen-server bound on the requested UDP port -- bypassing the
 *       menu nav (Multiplayer -> Host Game) that production code requires.
 *       Provides smoke-test coverage of the listen-host bind path. Pair
 *       with the natural --host log routing so a sister --connect-host
 *       client process writes to a distinct log. Port is parsed via
 *       strtol; out-of-range values (<=0 or >0xFFFF) leave the latch off
 *       with a WARNING. One-shot per boot.
 *
 *   --connect-host <addr>:<port>
 *       Post-netInit hook: invokes netStartClient(addr) on the first
 *       mainTick frame so the client boots straight into a connect-to-
 *       host attempt -- bypassing the menu nav (Multiplayer -> Join Game
 *       -> Enter Connect Code) that production code requires. addr is
 *       passed through unchanged to netParseAddr, so both "ip:port" and
 *       bare hostname forms work. Provides smoke-test coverage of the
 *       client connect path. One-shot per boot.
 *
 *   --dump-swarm-state <path>
 *       Track 2c (c3807, 2026-05-16): post-dispatch hook for the GPU
 *       swarm benchmark scenarios. Once per 60 frames (~1 Hz) after
 *       the player prop is positioned, calls
 *       swarmGpuReadbackTextureRows(0, 5, ...) to extract all 5 rows
 *       of the READ-side state texture (pos / vel / surface_up /
 *       AI ints / range) for SWARM_GPU_MAX bots, and appends the
 *       result to the configured file with a header of magic
 *       "PDSWARMv1", frame index, bot count, and row count. Stops
 *       after SWARM_DUMP_MAX_FRAMES (10) dumps so the file size is
 *       bounded at ~3.2 MB. Path is taken as-is and passed to fopen
 *       in binary mode; relative paths resolve to the process CWD.
 *       One-shot CLI capture; no live debug-key. Cheap no-op when a
 *       GPU swarm scenario is not active or the GL state texture is
 *       not armed. See context/designs/in-flight/gpu-swarm-and-test-
 *       scenarios.md Track 2c for the file format spec.
 * ---------------------------------------------------------------- */

static bool        g_BootNoNet            = false;
static bool        g_BootExtractAssetsOnly = false;
static bool        g_BootMainMenu         = false;
static s32         g_BootLaunchModdingHubPending = 0;
static SDL_atomic_t g_BootAssetChainFailures;
/* c3844 (2026-06-23): --launch-credits one-shot. Latched at parse, consumed
 * once after catalog init (same lifecycle point the other --launch-* fast-
 * paths fire) by entering the NORMAL scrolling credits -- mainChangeToStage(
 * STAGE_CREDITS) + numplayers/bond-coop-anti seeding + DIFF_A, WITHOUT
 * creditsRequestAltTitle (the alt-title branch starts solid-black with slides
 * disabled; see bootApplyLaunchCredits). Drives a deterministic credits-screen
 * boot so a smoke can capture the credits' transparent particles + Handel
 * Gothic fonts from frame 0 -- exercising the c3844 universal alpha-mode fix.
 * Inert when absent from argv. */
static bool        g_BootLaunchCredits    = false;
/* T-CATALOG-002: opt-in ordinary-client proof. Once a live stage and the
 * configured theme lifecycle are ready, queue a real mainChangeToStage and
 * log the active parent/dependency ownership after the new stage initializes.
 * The diagnostic is observational: it never acquires or releases assets. */
static s32         g_BootDebugThemeStageTransitionProof = 0;
static s32         g_BootDebugThemeStageTransitionFrom = -1;
static const char *g_BootLaunchScenario   = NULL;
static const char *g_BootLaunchMission    = NULL;
static const char *g_BootLaunchDifficulty = NULL;
static const char *g_BootLaunchMpArena    = NULL;
static const char *g_BootLaunchMpScenario = NULL;
static s32         g_BootLaunchMpBotCount = 0;
/* Week PD multiplayer smokes: --match-scorelimit <1..100> and
 * --match-teamscorelimit <1..400> seed the ordinary match limits of a
 * --launch-mp-room match so a scripted match can end on a score. Unlike the
 * seconds time limit these are real setup values: they show in the Room and
 * travel in the normal start message. 0 means the flag was not passed. */
static s32         g_BootMatchScoreLimit = 0;
static s32         g_BootMatchTeamScoreLimit = 0;
/* Mike directive 2026-05-18: CLI infrastructure for end-to-end smoke
 * tests of CS matches. --debug-auto-start-match arms a one-shot that
 * fires Start Match after the Room dialog opens, skipping the human
 * keyboard nav. --debug-mp-options seeds g_MpSetup.options with a
 * caller-supplied hex bitmask so smokes can preset MPOPTION_BOTJUMP /
 * MPOPTION_AUTORANDOMWEAPON_START / etc. without driving the lobby
 * UI checkboxes. Both consumed on first frame where the relevant
 * surface exists. */
static bool        g_BootDebugAutoStartMatch = false;
static s32         g_BootDebugRoomTab = -1;      /* --debug-room-tab: 0..2, -1 unset */
static s32         g_BootLaunchMpMatchPending = 0;
/* T-NETWORKING-011 (finding 10): --launch-challenge <slot> starts the
 * challenge in that slot (0-based, counting only the challenges this profile
 * can play, as challengeSetCurrentBySlot does) through the ImGui challenge
 * launch, matchStartFromChallenge, once the CI boot stage has a player. The
 * Room has no challenge entry yet; this lets a smoke run the fixed table. */
static s32         g_BootLaunchChallengeSlot = -1;
static s32         g_BootLaunchChallengePending = 0;
/* c3845 (2026-06-23): two-process listen-host match smoke infra.
 * --host-autostart fires the SAME high-level lobby-leader start path the
 * Room "Start Match" button uses (netLobbyRequestStartWithSims), but on
 * the HOST only and once a REMOTE client has reached CLSTATE_LOBBY plus a
 * short settle delay. This drives the full networked match lifecycle
 * (room assign -> manifest -> ready gate -> SVC_STAGE_START) without any
 * Room-UI navigation, which crashes in deeper sub-screens (mp_room_flow).
 * --match-timelimit-sec <n> forces a SECONDS-granularity time limit so a
 * regression match ends deterministically in ~35 s; the wire timelimit is
 * minutes-only (6-bit), so this overrides g_MpTimeLimit60 directly at the
 * lv.c time-limit gate (test-only, gated on the latch). */
static bool        g_BootHostAutostartArmed = false;
static s32         g_BootHostAutostartPeers = 1;
static u8          g_BootHostAutostartGameMode = NETGAMEMODE_MP;
static s32         g_BootHostAutostartFired = 0;
static s32         g_BootHostAutostartSettleFrames = 0;
static s32         g_BootReadyGateSettingsChangeArmed = 0; /* 1 change, 2 repeat (B-1208) */
static s32         g_BootReadyGateSettingsChangeFired = 0;
/* c3845: auto-start sequencing state machine.
 *   0 = waiting for remote-in-lobby + settle (handled by SettleFrames above)
 *   1 = room created + remote clients added; spacing frames before start
 *   2 = start fired (terminal; mirrored by g_BootHostAutostartFired) */
static s32         g_BootHostAutostartPhase = 0;
static s32         g_BootHostAutostartRoomId = 0xFF;
static s32         g_BootHostAutostartPostJoinFrames = 0;
/* T-NETWORKING-011 (finding 4): --host-autostart-matches <n> (1..8, default
 * 1) starts n matches in the same room. After a match the start re-arms once
 * the host is back in the room (end screen Continue, as a player would press
 * it), the match room is released and the room is in LOBBY again, plus a
 * settle window that lets the clients return too; the re-fire reuses the
 * room, skipping room creation. */
static s32         g_BootHostAutostartMatches = 1;
static s32         g_BootHostAutostartStarted = 0;
static s32         g_BootHostAutostartRematchFrames = 0;
static s32         g_BootMatchTimeLimitSec = 0; /* 0 = unset; >0 = forced seconds */
static u32         g_BootDebugMpOptions   = 0;
static bool        g_BootDebugMpOptionsSet = false;
static const char *g_BootDebugInstallReceivedMod = NULL;
static const char *g_BootDebugReceivePdcaList = NULL;
static const char *g_BootDebugRejectCatalogIngressList = NULL;
static const char *g_BootDebugSpawnWeapon = NULL;
/* T-NETWORKING-011: --debug-bot-spec "type:difficulty,..." gives the
 * --launch-mp-room simulants these types and difficulties in order (BOTTYPE_*
 * 0..12, BOTDIFF_* 0..5); simulants past the list stay general/normal. */
#define BOOT_DEBUG_BOT_SPEC_MAX 32
static s32         g_BootDebugBotSpecCount = 0;
static u8          g_BootDebugBotSpecType[BOOT_DEBUG_BOT_SPEC_MAX];
static u8          g_BootDebugBotSpecDiff[BOOT_DEBUG_BOT_SPEC_MAX];
static const char *g_BootDebugBotBody = NULL;
static const char *g_BootDebugBotHead = NULL;
static bool        g_BootDebugPlaceBotNearPlayer = false;
/* T-NETWORKING-011: --debug-place-bot-near-player-num <n> (default 0) puts
 * the bot in front of player n instead of player 0. On a listen host, player 1
 * is the client's player, so a client screenshot can show a simulant. */
static s32         g_BootDebugPlaceBotNearPlayerNum = 0;
/* T-NETWORKING-011 (finding 3): --debug-place-bot-near-player-visible searches
 * around the target player instead of only straight ahead and takes a spot
 * only with a clear line of sight from that player (the forward-only search
 * can put the bot behind the wall the player faces). --debug-face-nearest-bot
 * turns this peer's local player toward the nearest simulant each frame and
 * points the BOT.RENDER.AUDIT traces at it, so a client can show whether it
 * draws a simulant the host owns. */
static s32         g_BootDebugPlaceBotNearPlayerVisible = 0;
static s32         g_BootDebugFaceNearestBot = 0;
/* B-1193: --debug-end-mission "<seconds>:complete|fail" ends a mission on the
 * host (or offline) after that much play, through the level script's own end
 * (func0000e990). Nothing else ends a networked co-op or counter-op mission
 * in a smoke. */
static s32         g_BootDebugEndMissionSec = -1;
static s32         g_BootDebugEndMissionComplete = 0;
static s32         g_BootDebugEndMissionFired = 0;
/* B-1196: --debug-clone-guard <seconds> clones a guard on the host (or
 * offline) after that much mission play, as an unalerted guard's
 * try_spawn_clone2 does, so a smoke can prove a co-op client gets the clone
 * (SVC_NPC_SPAWN) instead of waiting for the AI to make one. */
static s32         g_BootDebugCloneGuardSec = -1;
static s32         g_BootDebugCloneGuardFired = 0;
static struct prop *g_BootDebugFaceBotProp = NULL;
static u32         g_BootDebugFaceBotLastLogMs = 0;
static s32         g_BootDebugFaceBotLogs = 0;
static s32         g_BootDebugPlaceBotNearPlayerPending = 0;
static s32         g_BootDebugPlaceBotNearPlayerFrames = 0;
static s32         g_BootDebugPlaceBotNearPlayerReadyFrames = 0;
static s32         g_BootDebugPlaceBotNearPlayerHoldFrames = 0;
static s32         g_BootDebugPlaceBotNearPlayerHoldFailures = 0;
static u32         g_BootDebugPlaceBotNearPlayerHoldStartMs = 0;
static u32         g_BootDebugPlaceBotNearPlayerHoldDurationMs = 3000;
static bool        g_BootDebugPlaceBotNearPlayerLogged = false;
static struct prop *g_BootDebugPlaceBotNearPlayerProp = NULL;
static u32         g_BootDebugPlaceBotNearPlayerTraceMask = 0;
static s32         g_BootDebugPlaceBotNearPlayerTraceBudget = 160;
static const s32   k_BootDebugPlaceBotNearPlayerMaxWaitFrames = 900;
static const s32   k_BootDebugPlaceBotNearPlayerStableFrames = 30;
/* Mike directive 2026-05-18 follow-up: override the swarm bench's
 * default arena. Default is base:mp_felicity (a cramped alley/rooftop
 * map where wallrun mechanics aren't visually obvious). Smokes /
 * playtest can pass --debug-swarm-map base:mp_skedar (open courtyards
 * + tall pillars) for better wallrun visibility. */
static const char *g_BootDebugSwarmMap = NULL;
static bool        g_BootMountBike        = false;
/* Latched one-shot for the bike-mount hook; tickled by pdmain.c's
 * mainTick once activeprops is populated and player 0 is alive. */
static s32         g_BootMountBikePending = 0;
static s32         g_BootMountBikeStage = -1;

/* c115 (2026-05-14): --debug-spawn-at one-shot latch. Parsed at boot,
 * fired on the first frame where the player prop exists. Mirrors the
 * --debug-mount-bike pattern. RoomNum is s16 in src/include/types.h
 * but we store as s32 to avoid pulling types.h into this file; the
 * tick path narrows at the chrMoveToPos call. */
static s32         g_BootSpawnAtPending = 0;
static f32         g_BootSpawnAtX = 0.0f;
static f32         g_BootSpawnAtY = 0.0f;
static f32         g_BootSpawnAtZ = 0.0f;
static s32         g_BootSpawnAtRoom = 0;
/* B-1291: companions of --debug-spawn-at (after-intro wait, look-at hold). */
static s32         g_BootSpawnAtAfterIntro = 0;
static s32         g_BootSpawnAtControl60 = 0;
static s32         g_BootSpawnAtWaitLogged = 0;
static s32         g_BootSpawnAtDone = 0;
static s32         g_BootSpawnAtLookAt = 0;
static f32         g_BootSpawnAtLookX = 0.0f;
static f32         g_BootSpawnAtLookY = 0.0f;
static f32         g_BootSpawnAtLookZ = 0.0f;
static s32         g_BootSpawnAtLookLogs = 0;
static u32         g_BootSpawnAtLookLastLogMs = 0;
static bool        g_BootDebugForceFirstPerson = false;
static s32         g_BootDebugForceFirstPersonPending = 0;
static s32         g_BootDebugForceFirstPersonFrames = 0;
static const s32   k_BootDebugForceFirstPersonFrames = 900;
static bool        g_BootDebugForceFirstPersonLook = false;
static bool        g_BootDebugForceFirstPersonLookLogged = false;
static struct coord g_BootDebugForceFirstPersonLookVec = {0.0f, 0.0f, 1.0f};
static bool        g_BootDebugForceFirstPersonCamOffset = false;
static struct coord g_BootDebugForceFirstPersonCamOffsetVec = {0.0f, 0.0f, 0.0f};

/* c115 (2026-05-14): deferred launch-scenario state. Worker delta's
 * iter-2 re-run showed that calling testScenarioLaunch synchronously
 * from bootApplyCliFastPaths fires before the stage setup has placed
 * the player prop. The first match's respawn fails (player at
 * y=-2^31), the match ends instantly into endscreen_solo, imgui_menu
 * IMC pushes on top, and gameplay-gated actions (the swarm-cycler
 * ACTION_TESTSCEN_CYCLE_COUNT, ACTION_USE) get suppressed by
 * gameplayInputSuppressed(). Fix: latch the parsed scenario id at
 * boot, return without calling testScenarioLaunch, and dispatch it on
 * the first mainTick frame where g_Vars.lvframenum >= 4 -- the same
 * gate the --debug-mount-bike hook uses for the activeprops walk. */
static s32         g_BootLaunchScenarioPending = 0;
static s32         g_BootLaunchScenarioId      = 0;  /* TESTSCEN_NONE */

/* c118 (2026-05-15): --launch-load-agent one-shot. The CLI path mirrors the
 * --debug-mount-bike / --debug-spawn-at deferred-tick pattern and activates
 * the staged JSON profile through the shared Agent Session boundary. The
 * ordinary Agent Select menu uses that same transaction. saveInit() runs
 * before the deferred tick, so the profile store is ready when this fires. */
static s32         g_BootLoadAgentArmed = 0;
static char        g_BootLoadAgentName[AGENT_PROFILE_NAME_MAX] = {0};

/* c118 (2026-05-15): --listen-bind <port> one-shot. CLI fast-path that
 * invokes netStartServer(port, NET_MAX_CLIENTS) on the first mainTick
 * frame so the client boots straight into a listen-server bound on the
 * requested UDP port -- bypassing the menu nav (Multiplayer -> Host
 * Game) that production code requires. Sister test fast-path for the
 * connectivity pillar's two-process peer-link smoke. Deferred to a tick
 * rather than fired from bootApplyCliFastPaths so netInit's threaded
 * boot-pool path has fully drained (g_NetInit is set inside the worker)
 * and so the smoke harness's SMOKE: scenario= sentinel lands before the
 * NET: created server log line -- which keeps assertion ordering
 * deterministic for the two-process smoke test reader.
 * One-shot per boot. */
static s32         g_BootListenBindArmed = 0;
static s32         g_BootListenBindPort  = 0;

/* c118 (2026-05-15): --connect-host <addr>:<port> one-shot. CLI
 * fast-path that invokes netStartClient(addr) on the first mainTick
 * frame so the client boots straight into a connect-to-host attempt --
 * bypassing the menu nav (Multiplayer -> Join Game -> Enter Connect
 * Code) that production code requires. Sister to --listen-bind for the
 * two-process peer-link smoke. addr is captured into a static buffer
 * (NET_MAX_ADDR = 64 in port/include/net/net.h, but we keep this file
 * independent of net.h's constants and use the same 64-byte limit
 * directly). Deferred to mainTick for the same reason --listen-bind is:
 * netInit must be complete before netStartClient is invoked, and
 * deferring lets the sentinel land before the connect log line.
 * One-shot per boot. */
static s32         g_BootConnectHostArmed   = 0;
static char        g_BootConnectHostAddr[64] = {0};

/* Track 2c (c3807, 2026-05-16): --dump-swarm-state <path> one-shot.
 * CLI fast-path that periodically extracts the GPU swarm state texture
 * and appends it to a file for offline inspection / replay. Path is
 * captured into a static buffer; the actual readback runs inside
 * mainTick once g_Vars.lvframenum >= 4 (player positioned), throttled
 * to 1 Hz via a frame counter. Latches off after SWARM_DUMP_MAX_FRAMES
 * dumps so the file is bounded. Cheap no-op when the flag wasn't on
 * the command line OR when no GPU swarm scenario is active OR when
 * the state texture isn't armed (driver missing glBindImageTexture).
 *
 * File format (little-endian, frame N is the N-th appended block):
 *   bytes [0..9):    magic = "PDSWARMv1" (9 ASCII chars, NO terminator)
 *   bytes [9..10):   pad byte (zero) for 4-byte alignment of u32 fields
 *   bytes [10..14):  reserved (zero)
 *   bytes [14..16):  pad
 *   bytes [16..20):  frame_idx (u32, dispatch counter monotonic 0..)
 *   bytes [20..24):  count     (u32, SWARM_GPU_MAX = 4096; mirrors the
 *                                texture width / bots-per-row capacity)
 *   bytes [24..28):  row_count (u32, 5; pos / vel / surface_up / AI /
 *                                range)
 *   bytes [28..32):  pad (zero)
 *   bytes [32..end): row_count * count * 4 floats, in row-major order
 *                    (row 0 then row 1 ...; each row is `count` 4-float
 *                    texels). Total per block = 5 * 4096 * 16 = 320 KB +
 *                    32 B header = 320032 bytes. After 10 dumps the
 *                    file is ~3.2 MB.
 *
 * Consumers should fread the 32 B header, validate the magic prefix,
 * then fread 5 * 4096 * 16 B of float data. A future replay / sync
 * tool maps the 4 ints out of row 3 via memcpy into a u32 + 3 spare. */
#define SWARM_DUMP_MAX_FRAMES   10
#define SWARM_DUMP_INTERVAL_FRAMES  60   /* ~1 Hz at 60 fps */
static s32         g_BootDumpSwarmArmed   = 0;
static char        g_BootDumpSwarmPath[260] = {0};
static s32         g_BootDumpSwarmDumpsLeft = 0;
static s32         g_BootDumpSwarmTickCounter = 0;

s32 bootGetMemSize(void)
{
	return (s32)g_OsMemSize;
}

void *bootAllocateStack(s32 threadid, s32 size)
{
	static u8 bruh[0x1000];
	return bruh;
}

void bootCreateSched(void)
{
	osCreateMesgQueue(&g_MainMesgQueue, g_MainMesgBuf, ARRAYCOUNT(g_MainMesgBuf));
	if (osTvType == OS_TV_MPAL) {
		osCreateScheduler(&g_Sched, NULL, OS_VI_MPAL_LAN1, 1);
	} else {
		osCreateScheduler(&g_Sched, NULL, OS_VI_NTSC_LAN1, 1);
	}
}

static void gameInit(void)
{
	osMemSize = g_OsMemSizeMb * 1024 * 1024;

	for (s32 i = 0; i < MAX_LOCAL_PLAYERS; ++i) {
		struct extplayerconfig *cfg = g_PlayerExtCfg + i;
		cfg->fovzoommult = cfg->fovzoom ? cfg->fovy / 60.0f : 1.0f;
	}

	if (g_HudCenter == HUDCENTER_NORMAL) {
		g_HudAlignModeL = G_ASPECT_CENTER_EXT;
		g_HudAlignModeR = G_ASPECT_CENTER_EXT;
	} else if (g_HudCenter == HUDCENTER_WIDE) {
		g_HudAlignModeL = G_ASPECT_LEFT_EXT | G_ASPECT_WIDE_EXT;
		g_HudAlignModeR = G_ASPECT_RIGHT_EXT | G_ASPECT_WIDE_EXT;
	}
}

/* Engine Phase 2 boot orchestrator (2026-05-03).
 *
 * Runs the full catalog / extract / verify / walker / emitter sequence
 * on a boot-pool worker thread while the main thread renders the boot
 * overlay.  Phase boundaries push begin / end events into the progress
 * channel; per-file updates inside romExtract* drive the bar fill.
 *
 * Single-worker enqueue per the Phase 2 plan (see
 * context/designs/engine/startup-acceleration.md): one job runs the
 * entire sequence serially.  Phase 3 will rewrite romExtractVerifyAll
 * to fan out per-file SHA-256 across the pool's worker threads. */
static void bootRecordAssetPhase(const char *phase, s32 result)
{
	if (result < 0) {
		SDL_AtomicAdd(&g_BootAssetChainFailures, 1);
		sysLoudFailf("ASSETCHAIN",
			"phase '%s' reported an incomplete extraction/load result (%d)",
			phase ? phase : "unknown", result);
	}
}

static s32 bootAllowsBackgroundStageRepair(void)
{
	if (g_BootExtractAssetsOnly || g_NetDedicated || nativeWritePolicyActive() ||
			sysArgCheck("--boot-stage") || sysArgCheck("--profile") ||
			sysArgCheck("--launch-mission") || sysArgCheck("--launch-scenario") ||
			sysArgCheck("--launch-challenge") || sysArgCheck("--launch-mp-room") ||
			sysArgCheck("--connect-host") || sysArgCheck("--host-autostart") ||
			sysArgCheck("--no-background-stage-repair")) return 0;
	for (s32 i = 0; i < modmgrGetCount(); ++i) {
		if (modmgrGetModEnabled(i)) return 0;
	}
	return 1;
}

static void bootRunCatalogWork(void *arg)
{
	(void)arg;

	bootProgressBeginPhase(BOOT_PHASE_EXTRACT_FILES);
	romdataInit();
	catalogCacheVerifyRom(g_RomName, NULL);
	bootRecordAssetPhase("rom-files", romExtractAllFiles());
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_VERIFY_FILES);
	bootRecordAssetPhase("rom-files-verify", romExtractVerifyAll());
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EXTRACT_SEGS);
	bootRecordAssetPhase("rom-segments", romExtractAllSegments());
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_VERIFY_SEGS);
	bootRecordAssetPhase("rom-segments-verify", romExtractVerifyAllSegments());
	romExtractEmitBootIntegrityReport();
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_RELEASE_ROM);
	romdataReleaseRom();
	bootProgressEndPhase();

	/* c115 (2026-05-13): --no-net gates the entire network stack.  When
	 * set, g_NetInit stays false, every net.c entry point early-returns
	 * cleanly, and Windows never raises the "Allow on networks?" prompt.
	 * Smoke-verify-dispatch-findings-2026-05-13.md S-3 explains why this
	 * matters: the firewall dialog steals SDL focus and breaks every
	 * scripted test that copies the binary into a fresh per-test dir. */
	if (!g_BootNoNet && !g_BootExtractAssetsOnly) {
		netInit();
	} else if (g_BootExtractAssetsOnly) {
		sysLogPrintf(LOG_NOTE, "BOOT: --extract-assets-only set; netInit() skipped");
	} else {
		sysLogPrintf(LOG_NOTE, "BOOT: --no-net set; netInit() skipped");
	}
	g_ValidGbcRomFound = romdataCheckGbcRom();
	gameInit();

	bootProgressBeginPhase(BOOT_PHASE_CATALOG_INIT);
	catalogInit();
	stageTableInit();
	assetCatalogInit();
	assetCatalogRegisterBaseGame();
	assetCatalogRegisterStageSceneFiles();
	catalogManagerHeadInit();
	catalogManagerBodyInit();
	catalogManagerArenaInit();
	/* External-format archive mods register component descriptors during
	 * modmgrInit(). Keep mod loading behind assetCatalogInit() so mounted
	 * .pdmod INIs and loose folder layouts share a valid catalog target. */
	modmgrInit();
	{
		/* Every mods folder the scan read, the user folder last: the
		 * catalog keeps the last registration of an id. */
		for (s32 root = modmgrGetModsRootCount() - 1; root >= 0; --root) {
			const char *modsdir = modmgrGetModsRoot(root);
			assetCatalogScanComponents(modsdir);
			assetCatalogScanBotVariants(modsdir);
		}
	}
	sysLogPrintf(LOG_NOTE, "Asset Catalog: %d entries registered", assetCatalogGetCount());
	bootArmDebugLoadCatalogAssets();
	/* Source-only catalog probes only need the mounted mod catalog rows.
	 * Running them here keeps workflow smokes from waiting on unrelated base
	 * emitters while still proving provider/catalog activation. */
	if (bootDebugCatalogProbesCanRunBeforeBaseEmit()) {
		(void)bootApplyDeferredDebugLoadCatalogAssets();
	}
	bootProgressEndPhase();

	/* B-325 (2026-05-03): emitters run BEFORE the walker, not after. The
	 * walker reads `data/<romid>/<kind>/*.pd<ext>` and populates loader_pool
	 * typed payload (struct weapon, struct head, etc.). On a fresh install
	 * the per-asset dirs are empty until the emitters write them. Running
	 * the walker first leaves loader_pool inactive (s_LoaderActive=0), so
	 * catalogManagerGetWeaponByIndex returns NULL for every index -- which
	 * is what caused B-324 (bgunCalculateBlend AV: NULL weapon->sway).
	 *
	 * Order is now: emit -> walk -> register weapon model files (depends on
	 * the weapon pool being populated) -> build caches. */

	bootProgressBeginPhase(BOOT_PHASE_EMIT_MESH);
	bootRecordAssetPhase("pdmesh", romExtractAllPdmesh(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ANIM);
	bootRecordAssetPhase("pdanim-weapon", romExtractAllPdanim(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_SFX);
	bootRecordAssetPhase("pdsfx", romExtractAllPdsfx(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_VOICE);
	bootRecordAssetPhase("pdvoice", romExtractAllPdvoice(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_SONG);
	bootRecordAssetPhase("pdsong", romExtractAllPdsong(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_WPN);
	bootRecordAssetPhase("pdweapon-projectile-entity",
		romExtractAllPdweapon(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_HEAD);
	bootRecordAssetPhase("pdhead", romExtractAllPdhead(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_BODY);
	bootRecordAssetPhase("pdbody", romExtractAllPdbody(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_BODY);
	bootRecordAssetPhase("pdcharacter", romExtractAllPdcharacter(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ARENA);
	stageArchiveRepairSetEnabled(bootAllowsBackgroundStageRepair());
	bootRecordAssetPhase("pdarena-pdscenario", romExtractAllPdarena(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_ANIMCHR);
	bootRecordAssetPhase("pdanim-character", romExtractAllPdanimChr(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_FONT);
	bootRecordAssetPhase("pdfont", romExtractAllPdfont(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_LANG);
	bootRecordAssetPhase("pdlang", romExtractAllPdlang(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_TEXTURE);
	bootRecordAssetPhase("pdtexture", romExtractAllPdtexture(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_UI);
	bootRecordAssetPhase("pdui", romExtractAllPdui(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_EMIT_META);
	bootRecordAssetPhase(
		"pdgamemode-pdbotprofile-pdhud-pdmission-pdmaterial-pdskin-"
		"pdeffect-pdprop-pdvehicle",
		romExtractAllPdmeta(0));
	bootRecordAssetPhase("pdtheme", romExtractAllPdtheme(0));
	/* Global AI lists slice 2: the binary's lists as base:ai_global. */
	bootRecordAssetPhase("pdai", romExtractAllPdai(0));
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_WALKER);
	{
		loader_walker_result_t walker_result;
		loaderWalkerLoadAll(&walker_result);
		s32 walker_failures = walker_result.total_envelope_failures
			+ walker_result.total_register_failures;
		if (walker_failures > 0) {
			SDL_AtomicAdd(&g_BootAssetChainFailures, walker_failures);
			sysLoudFailf("ASSETCHAIN",
				"universal walker rejected %d archive envelope(s) and "
				"%d catalog registration(s)",
				walker_result.total_envelope_failures,
				walker_result.total_register_failures);
		}
		if (loaderPoolIsActive()) {
			bootRecordAssetPhase("weapon-model-source",
				assetCatalogRegisterWeaponModelFiles());
		}
	}
	bootProgressEndPhase();

	bootProgressBeginPhase(BOOT_PHASE_BUILD_CACHES);
	if (SDL_AtomicGet(&g_BootAssetChainFailures) == 0) {
		catalogBuildRuntimeCaches();
		catalogLoadInit();
		modmgrLoadComponentState();
		modmgrCatalogChanged();
	} else {
		sysLoudFailf("ASSETCHAIN",
			"runtime cache build skipped because %d extraction/load "
			"failure(s) left the public asset chain incomplete",
			SDL_AtomicGet(&g_BootAssetChainFailures));
	}
	bootProgressEndPhase();

	bootProgressMarkComplete();
}

static void cleanup(void)
{
	sysLogPrintf(LOG_NOTE, "shutdown");
	catalogLoadLogStats();

	// Signal all subsystems that we are exiting. Must be set before
	// netDisconnect so that blocking teardown paths (UPnP HTTP delete,
	// stage transitions) are skipped and the process exits quickly.
	g_AppQuitting = 1;
	mainStopGridWorldForShutdown();

	discordShutdown();
	inputLayerShutdown();
	sceneShutdown();
	inputCtxShutdown();
	updaterShutdown();
	/* B-1053: UI consumers must release session-owned catalog references before
	 * clean recovery retirement removes the received package rows beneath them. */
	pdguiShutdown();
	/* B-1044: retire and remove live session-only distribution content while
	 * catalog/provider/runtime owners are still available. A pending recovery
	 * decision intentionally preserves its dirty marker for the next launch. */
	netCrashRecoveryMarkClean();
	netDisconnect();
	mainRetireGridForShutdown();
	modmgrShutdown();
	actionmapSaveBinds();
	configSave(CONFIG_PATH);
	statsShutdown();
	videoShutdown();
	crashShutdown();

	/* H-7: Free persistent allocations AFTER all subsystems are shut down,
	 * so no subsystem dereferences freed memory during its teardown. */
	mempPCValidate("shutdown");
	mempPCFreeAll();

	/* SDL_Quit tears down all SDL subsystems (video, audio, timer, etc.)
	 * that were opened via SDL_Init / SDL_InitSubSystem during boot. */
	SDL_Quit();
}

/* ---------------------------------------------------------------- *
 * c115 smoke-verify CLI fast-paths.                                *
 * ---------------------------------------------------------------- */

/* Extern shims for game-side symbols not exposed via port/include
 * headers.  Keeping these inline (rather than including
 * src/include/game/*) avoids pulling the game's local CLAUDE.md
 * legacy-types contract into this PC-port file. */
extern void setCurrentPlayerNum(s32 playernum);
extern bool currentPlayerTryMountHoverbike(struct prop *prop);
extern s32 g_PostExitMainMenuView;

/* c115 (2026-05-14): these live in src/game/chraction.c; declare extern
 * shims rather than #include "game/chraction.h" to keep this PC-port file
 * outside the chr*.h dependency surface (which transitively drags in
 * PR/gbi.h and N64 micro-code defines). Signatures must match the public
 * declarations in src/include/game/chraction.h exactly. */
extern bool chrMoveToPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms, f32 angle, bool ignorebg);
extern bool chrRelocateToFloorWithCachedGround(struct chrdata *chr,
		const struct coord *floorpos, f32 rootheight, RoomNum *rooms, f32 theta,
		u16 floorcol, u8 floortype, RoomNum floorroom);
extern bool g_DebugBotsHoldFire;
extern f32 cdFindGroundInfoAtCyl(struct coord *pos, f32 radius, RoomNum *rooms,
		u16 *floorcol, u8 *floortype, u16 *floorflags, RoomNum *floorroom,
		s32 *inlift, struct prop **lift);
extern f32 meshFindFloor(struct coord *pos, f32 radius, f32 *out_normalY);
extern f32 cdFindFloorYColourTypeAtPos(struct coord *pos, RoomNum *rooms, u16 *floorcol, u8 *floortype);
extern s32 lvGetStageTime60(void);
extern void bgFindRoomsByPos(struct coord *pos, RoomNum *inrooms,
		RoomNum *aboverooms, s32 max, RoomNum *bestroom);
/* T-NETWORKING-011 (finding 3): src/lib/collision.c and src/game/atan2f.c,
 * as declared in lib/collision.h and game/atan2f.h. */
extern bool cdTestLos06(struct coord *arg0, RoomNum *rooms1, struct coord *arg2,
		RoomNum *rooms2, u32 types);
extern f32 atan2f(f32 x, f32 z);

/* c3844 (2026-06-23): --launch-credits credits-entry shims. These live in
 * src/game/{credits,title,lv}.c; declare extern shims rather than #include
 * "game/credits.h" / "game/title.h" / "game/lv.h" to keep this PC-port file
 * outside the game-header dependency surface (matching the chrMoveToPos /
 * setCurrentPlayerNum shims above). mainChangeToStage is already reachable via
 * the included "lib/main.h"; g_Vars + DIFF_A come in through bss.h/constants.h.
 * Signatures must match src/include/game/title.h:68, game/lv.h:38 exactly.
 * (creditsRequestAltTitle is intentionally NOT shimmed -- the --launch-credits
 * fast-path uses the normal scrolling-credits path; see bootApplyLaunchCredits.) */
extern void setNumPlayers(s32 numplayers);
extern void lvSetDifficulty(s32 difficulty);
/* Shared normal-scrolling-credits launcher (src/game/credits.c). Single source
 * of truth for the seeding sequence used by BOTH this boot fast-path and the
 * ImGui main-menu Credits button. Signature must match game/credits.h. */
extern void creditsEnterNormalScroll(void);
/* Production Modding Hub entry point. The boot shortcut only opens tool 0;
 * all state mutation remains owned by the ordinary Mod Manager UI. */
extern void pdguiModdingHubShowTool(s32 tool);
extern s32 pdguiModdingHubDebugWeaponCreate(const char *template_id, const char *display_name);
extern s32 pdguiCharacterToolDebugCreate(const char *template_id, const char *display_name,
	const char *gender, const char *build, s32 height);
extern s32 pdguiLevelToolDebugNew(const char *template_id, const char *display_name);
extern s32 pdguiHotswapWasActive(void);

/* Map a --difficulty string to the DIFF_* constants used by g_MissionConfig
 * and lvSetDifficulty.  Returns DIFF_A on unknown / NULL input. */
static s32 bootResolveDifficulty(const char *name)
{
	if (!name || !name[0]) {
		return DIFF_A;
	}
	if (strcasecmp(name, "agent") == 0 || strcasecmp(name, "a") == 0) {
		return DIFF_A;
	}
	if (strcasecmp(name, "special") == 0
			|| strcasecmp(name, "sa") == 0
			|| strcasecmp(name, "special-agent") == 0) {
		return DIFF_SA;
	}
	if (strcasecmp(name, "perfect") == 0
			|| strcasecmp(name, "pa") == 0
			|| strcasecmp(name, "perfect-agent") == 0) {
		return DIFF_PA;
	}
	sysLogPrintf(LOG_WARNING,
		"BOOT: --difficulty '%s' unrecognised; defaulting to Agent", name);
	return DIFF_A;
}

static bool bootResolveHostAutostartMode(const char *name, u8 *out_mode)
{
	if (out_mode == NULL) {
		return false;
	}
	if (name == NULL || name[0] == '\0' || strcasecmp(name, "mp") == 0) {
		*out_mode = NETGAMEMODE_MP;
		return true;
	}
	if (strcasecmp(name, "coop") == 0
			|| strcasecmp(name, "co-op") == 0) {
		*out_mode = NETGAMEMODE_COOP;
		return true;
	}
	if (strcasecmp(name, "counterop") == 0
			|| strcasecmp(name, "counter-op") == 0
			|| strcasecmp(name, "anti") == 0) {
		*out_mode = NETGAMEMODE_ANTI;
		return true;
	}
	return false;
}

/* Try to resolve a stage identifier (catalog ID like "base:defection" OR
 * "base:scenario_chicago", decimal like "38", or hex like "0x26") into a
 * stagenum. Returns -1 on failure. */
static s32 bootResolveStageIdToNum(const char *id)
{
	if (!id || !id[0]) {
		return -1;
	}

	/* Hex / decimal numeric form. */
	if (isdigit((unsigned char)id[0])) {
		char *endp = NULL;
		int base = (id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) ? 16 : 10;
		long v = strtol(id, &endp, base);
		if (endp && endp != id && *endp == '\0' && v > 0 && v < 0x100) {
			return (s32)v;
		}
	}

	/* Catalog ID. Scenario archives are source-owned stage content, so their
	 * catalog ID must be accepted directly instead of requiring callers to
	 * bridge through a legacy numeric stage argument. */
	const asset_entry_t *entry = assetCatalogResolve(id);
	if (entry) {
		if (entry->type == ASSET_MAP && entry->ext.map.stagenum > 0) {
			return entry->ext.map.stagenum;
		}
		if (entry->type == ASSET_SCENARIO && entry->ext.scenario.stagenum > 0) {
			return entry->ext.scenario.stagenum;
		}
		if (entry->type == ASSET_ARENA && entry->ext.arena.stagenum > 0) {
			return entry->ext.arena.stagenum;
		}
	}

	return -1;
}

/* Apply --main-menu's post-CI main menu auto-open arm.  Must run after
 * catalog init (so g_PostExitMainMenuView is reachable in BSS).  No-op
 * unless --main-menu was on the command line. */
static void bootApplyMainMenu(void)
{
	if (!g_BootMainMenu) {
		return;
	}
	g_PostExitMainMenuView = 0;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --main-menu armed g_PostExitMainMenuView=0 (top-level)");
}

/* --mod-author <recipe.json>: save and enable the mod a recipe describes,
 * through the path the in-game creators use. */
static void bootApplyModAuthorRecipe(const char *recipe_path)
{
	if (!recipe_path || !recipe_path[0]) {
		return;
	}
	sysLogPrintf(LOG_NOTE, "BOOT: --mod-author recipe='%s' result=%s", recipe_path,
		modAuthorRuntimeRunRecipe(recipe_path) ? "OK" : "FAIL");
}

/* Authorize the exact native archive created by the smoke's recipe. */
static void bootApplyDebugGridShareWeapon(void)
{
	if (!smokeHarnessIsActive()) return;
	const char *weapon_id = sysArgGetString("--debug-grid-share-weapon");
	if (!weapon_id || !weapon_id[0]) return;
	char err[256] = {0};
	if (!forgeLevelAllowWeaponMatchSharing(weapon_id, err, sizeof(err))) {
		sysLogPrintf(LOG_WARNING, "SMOKE.GRID.WEAPON: cannot share weapon=%s reason='%s'", weapon_id, err);
	}
}

/* --debug-weapon-tool-create <weapon id> [--debug-weapon-tool-name <name>]:
 * the Weapons creator's own template start and save, without the mouse. */
static void bootApplyDebugWeaponToolCreate(void)
{
	const char *template_id = sysArgGetString("--debug-weapon-tool-create");
	const char *name = sysArgGetString("--debug-weapon-tool-name");

	if (!template_id || !template_id[0]) {
		return;
	}
	if (!name || !name[0]) {
		name = "Debug Weapon";
	}
	sysLogPrintf(LOG_NOTE, "BOOT: --debug-weapon-tool-create template=%s name='%s' result=%s",
		template_id, name,
		pdguiModdingHubDebugWeaponCreate(template_id, name) ? "OK" : "FAIL");
}

/* --debug-character-tool-create <id> with its optional -name, -gender, -build
 * and -height flags: the Characters creator's own template start and save,
 * without the mouse. A height that is not a whole number from 1 to 65535
 * fails the run instead of being read as "keep". */
static void bootApplyDebugCharacterToolCreate(void)
{
	const char *template_id = sysArgGetString("--debug-character-tool-create");
	const char *name = sysArgGetString("--debug-character-tool-name");
	const char *gender = sysArgGetString("--debug-character-tool-gender");
	const char *build = sysArgGetString("--debug-character-tool-build");
	const char *height_text = sysArgGetString("--debug-character-tool-height");
	s32 height = 0;

	if (!template_id || !template_id[0]) {
		return;
	}
	if (!name || !name[0]) {
		name = "Debug Character";
	}
	if (height_text && height_text[0]) {
		char *end = NULL;
		const long value = strtol(height_text, &end, 10);
		if (!end || *end != '\0' || value < 1 || value > 65535) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-character-tool-create template=%s name='%s' result=FAIL (--debug-character-tool-height '%s' is not a whole number from 1 to 65535)",
				template_id, name, height_text);
			return;
		}
		height = (s32)value;
	}
	sysLogPrintf(LOG_NOTE, "BOOT: --debug-character-tool-create template=%s name='%s' result=%s",
		template_id, name,
		pdguiCharacterToolDebugCreate(template_id, name,
			gender && gender[0] ? gender : NULL, build && build[0] ? build : NULL,
			height) ? "OK" : "FAIL");
}

/* --debug-level-tool-new <arena id> [--debug-level-tool-name <name>]: the
 * Levels tool's own New Level, without the mouse. */
static void bootApplyDebugLevelToolNew(void)
{
	const char *template_id = sysArgGetString("--debug-level-tool-new");
	const char *name = sysArgGetString("--debug-level-tool-name");

	if (!template_id || !template_id[0]) {
		return;
	}
	if (!name || !name[0]) {
		name = "Debug Level";
	}
	sysLogPrintf(LOG_NOTE, "BOOT: --debug-level-tool-new template=%s name='%s' result=%s",
		template_id, name, pdguiLevelToolDebugNew(template_id, name) ? "OK" : "FAIL");
}

/* --debug-forge-level <arena id> [--debug-forge-level-name <name>]: Forge's
 * own open, edit and save of a level's placements, without the editor. */
static void bootApplyDebugForgeLevel(void)
{
	const char *arena_id = sysArgGetString("--debug-forge-level");
	const char *name = sysArgGetString("--debug-forge-level-name");
	const char *weapon_id = sysArgGetString("--debug-forge-level-weapon");

	if (!arena_id || !arena_id[0]) {
		return;
	}
	if (!name || !name[0]) {
		name = "Debug Level";
	}
	sysLogPrintf(LOG_NOTE, "BOOT: --debug-forge-level arena=%s name='%s' weapon=%s result=%s",
		arena_id, name, (weapon_id && weapon_id[0]) ? weapon_id : "(none)",
		forgeLevelDebugEdit(arena_id, name, weapon_id,
			sysArgCheck("--debug-forge-level-vehicle") ? 1 : 0,
			sysArgGetString("--debug-forge-level-prop")) ? "OK" : "FAIL");
}

/* Arm the real Mod Manager after the catalog and GUI registries are ready.
 * Agent Select is created later by the CI stage and takes focus, so opening the
 * hub synchronously here would put it behind that menu. The main-tick hook
 * consumes this latch only after a normal hotswap menu is active. */
static void bootApplyLaunchModdingHub(void)
{
	if (!sysArgCheck("--launch-modding-hub")) {
		return;
	}
	g_BootLaunchModdingHubPending = 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-modding-hub armed for production menu-ready open");
}

s32 bootLaunchModdingHubTick(void)
{
	if (!g_BootLaunchModdingHubPending || !pdguiHotswapWasActive()) {
		return 0;
	}
	pdguiModdingHubShowTool(0);
	g_BootLaunchModdingHubPending = 0;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-modding-hub consumed -> production Mod Manager tool 0");
	return 1;
}

/* c3844 (2026-06-23): Apply --launch-credits by entering the NORMAL scrolling
 * credits, NOT the alt-title path. Fired once from bootApplyCliFastPaths after
 * catalog init -- mainChangeToStage queues the STAGE_CREDITS (0x5c) transition
 * the same way titleTickPdLogo does, so no deferred tick is needed (the credits
 * screen has no player-prop dependency unlike the scenario / mp-room fast-paths).
 *
 * Root cause of the original black screen (fixed here): the first cut of this
 * fast-path mirrored title.c:851-864's ALT-TITLE exit and called
 * creditsRequestAltTitle(). That latches g_CreditsAltTitleRequested, so when
 * creditsReset() runs at stage load (lv.c:764) it takes the alt-title branch
 * (credits.c:1917-1922) which sets slidesenabled=false and blacktimer60=1140.
 * The creditsDraw content gate (credits.c:1773-1775) only renders when
 * `slidesenabled || blacktimer60 < TICKS(60) || blacktimer60 > TICKS(1200)`,
 * so at blacktimer60=1140 with slides off NOTHING draws -- the framebuffer stays
 * the solid-black fill from credits.c:1762. blacktimer60 then ticks up ~1/frame
 * (credits.c:1737) and only after ~60 frames (>1200) does the slow "PERFECT
 * DARK" wordmark fade in. The actual scrolling credit slides + Handel Gothic
 * foreground text (creditsDrawSlide -> creditsDrawForegroundText) are gated on
 * slidesenabled and NEVER appear on the alt-title path.
 *
 * The normal path (NO creditsRequestAltTitle) leaves slidesenabled=true
 * (credits.c:1898), so creditsTickSlide/creditsDrawSlide run from frame 0:
 * the first credit slide's Handel Gothic text + the up-to-500 XLU particles
 * (creditsDrawParticles) render immediately and deterministically, with no
 * blacktimer60 wait. This is what the c3844 universal alpha-mode capture needs.
 *
 * No save-flag gate applies: creditsReset() runs unconditionally for
 * STAGE_CREDITS regardless of g_AltTitleEnabled (that flag only governs the
 * title.c attract-loop auto-entry, which the direct mainChangeToStage bypasses).
 * No-op unless --launch-credits was on the command line. */
static void bootApplyLaunchCredits(void)
{
	if (!g_BootLaunchCredits) {
		return;
	}
	/* Intentionally does NOT take the alt-title branch -- see header comment.
	 * creditsEnterNormalScroll keeps slidesenabled=true so the scrolling credit
	 * slides + Handel Gothic fonts render from frame 0. Shared with the ImGui
	 * main-menu Credits button so both entry points behave identically. */
	creditsEnterNormalScroll();
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-credits consumed -> mainChangeToStage(STAGE_CREDITS=0x%02x) numplayers=1 difficulty=%d (normal scrolling-credits path, slidesenabled=true)",
		(u32)STAGE_CREDITS, DIFF_A);
}

/* Arm the --launch-scenario one-shot. Resolves the scenario id string
 * to a test_scenario_t enum and latches it for bootLaunchScenarioTick
 * to consume on the first frame where the player prop is positioned
 * (lvframenum >= 4). Synchronous testScenarioLaunch here would fire
 * before stage setup placed the player, causing matchStart to bounce
 * straight into endscreen_solo. See g_BootLaunchScenarioPending
 * comment block above for the full failure mode. */
static void bootApplyLaunchScenario(void)
{
	if (!g_BootLaunchScenario || !g_BootLaunchScenario[0]) {
		return;
	}
	test_scenario_t scen = TESTSCEN_NONE;
	if (strcasecmp(g_BootLaunchScenario, "empty_map") == 0
			|| strcasecmp(g_BootLaunchScenario, "empty-map") == 0
			|| strcasecmp(g_BootLaunchScenario, "empty") == 0) {
		scen = TESTSCEN_EMPTY_MAP;
	} else if (strcasecmp(g_BootLaunchScenario, "swarm_cpu") == 0
			|| strcasecmp(g_BootLaunchScenario, "swarm-cpu") == 0) {
		scen = TESTSCEN_SWARM_CPU;
	} else if (strcasecmp(g_BootLaunchScenario, "swarm_gpu") == 0
			|| strcasecmp(g_BootLaunchScenario, "swarm-gpu") == 0) {
		scen = TESTSCEN_SWARM_GPU;
	} else {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-scenario '%s' unknown (expected empty_map|swarm_cpu|swarm_gpu)",
			g_BootLaunchScenario);
		return;
	}
	g_BootLaunchScenarioId      = (s32)scen;
	g_BootLaunchScenarioPending = 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-scenario '%s' armed (will dispatch testScenarioLaunch(%d) once lvframenum >= 4)",
		g_BootLaunchScenario, (s32)scen);
}

/* Apply --launch-mission by seeding g_MissionConfig and pointing
 * g_StageNum at the resolved stagenum.  We do NOT route through
 * menuhandlerAcceptMission here because the menupool / IMC stack
 * isn't safe to push to from pre-mainProc state.  Instead we mirror
 * the subset of state menuhandlerAcceptMission writes that the
 * stage-load path actually consumes. */
static void bootApplyLaunchMission(void)
{
	if (!g_BootLaunchMission || !g_BootLaunchMission[0]) {
		return;
	}
	s32 stagenum = bootResolveStageIdToNum(g_BootLaunchMission);
	if (stagenum < 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-mission '%s' unresolved (try 0xNN hex or base:* catalog ID)",
			g_BootLaunchMission);
		return;
	}
	s32 diff = bootResolveDifficulty(g_BootLaunchDifficulty);

	memset(&g_MissionConfig, 0, sizeof(g_MissionConfig));
	g_MissionConfig.difficulty = (u8)diff;
	g_MissionConfig.pdmode = 0;
	g_MissionConfig.iscoop = 0;
	g_MissionConfig.isanti = 0;
	g_MissionConfig.pdmodereaction = 0;
	g_MissionConfig.pdmodehealth = 128;
	g_MissionConfig.pdmodedamage = 128;
	g_MissionConfig.pdmodeaccuracy = 128;
	/* Stash the catalog ID when one was supplied so endscreen restart
	 * paths can re-resolve through the catalog.  Falls back to legacy
	 * stagenum when the input was numeric. */
	if (g_BootLaunchMission[0] != '0' || (g_BootLaunchMission[1] != 'x' && g_BootLaunchMission[1] != 'X')) {
		strncpy(g_MissionConfig.stage_id, g_BootLaunchMission,
			sizeof(g_MissionConfig.stage_id) - 1);
		g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
	}
	g_MissionConfig.stagenum = (u8)stagenum;
	g_MissionConfig.stageindex = 0;

	g_StageNum = stagenum;
	if (g_FileAutoSelect < 0) {
		g_FileAutoSelect = 0;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-mission '%s' -> stagenum=0x%02x difficulty=%d",
		g_BootLaunchMission, (u32)stagenum, diff);
}

static const char *bootValidateDebugBotAsset(const char *flag,
	const char *id, asset_type_e expected_type, s32 *mp_index)
{
	if (mp_index) {
		*mp_index = -1;
	}
	if (!id || !id[0]) {
		return NULL;
	}

	const asset_entry_t *entry = assetCatalogResolve(id);
	if (!entry) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: %s failed id='%s' reason=missing_catalog_entry",
			flag, id);
		return NULL;
	}
	if (entry->type != expected_type) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: %s failed id='%s' reason=wrong_type actual=%d expected=%d",
			flag, id, (s32)entry->type, (s32)expected_type);
		return NULL;
	}
	if (entry->mp_index < 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: %s failed id='%s' reason=missing_mp_index",
			flag, id);
		return NULL;
	}

	if (mp_index) {
		*mp_index = entry->mp_index;
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: %s '%s' mp%s=%d",
		flag, id,
		expected_type == ASSET_BODY ? "body" : "head",
		(s32)entry->mp_index);
	return id;
}

/* Apply --launch-mp-room by seeding g_MatchConfig (the lobby/Room
 * screen's input state) and arming the solo-room overlay so the
 * Room screen opens on first CI frame.  The actual match start
 * still requires the user to press Start Match in the Room screen
 * -- this fast-path only gets the smoke test to the screen, where
 * the existing keyboard-driven Start Match button is reachable. */
static void bootApplyLaunchMpRoom(void)
{
	if (!g_BootLaunchMpArena || !g_BootLaunchMpArena[0]) {
		return;
	}
	matchConfigInit();
	strncpy(g_MatchConfig.stage_id, g_BootLaunchMpArena,
		sizeof(g_MatchConfig.stage_id) - 1);
	g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
	if (g_BootLaunchMpScenario && g_BootLaunchMpScenario[0]) {
		strncpy(g_MatchConfig.scenario_id, g_BootLaunchMpScenario,
			sizeof(g_MatchConfig.scenario_id) - 1);
		g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
	}
	/* Derive the runtime mode from the ID and apply the mode's rules before
	 * the bots below are seated, so a team-only mode gets two balanced teams
	 * and --host-autostart sends a mode that matches the ID. */
	if (matchConfigApplyScenarioRules("launch-mp-room") < 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-mp-room scenario '%s' did not resolve to a game mode",
			g_MatchConfig.scenario_id);
	}
	/* Stored limits are "limit minus one" (mpApplyLimits adds it back). */
	if (g_BootMatchScoreLimit > 0) {
		g_MatchConfig.scorelimit = (u8)(g_BootMatchScoreLimit - 1);
		sysLogPrintf(LOG_NOTE, "BOOT: --match-scorelimit applied: score=%d",
			g_BootMatchScoreLimit);
	}
	if (g_BootMatchTeamScoreLimit > 0) {
		g_MatchConfig.teamscorelimit = (u16)(g_BootMatchTeamScoreLimit - 1);
		sysLogPrintf(LOG_NOTE, "BOOT: --match-teamscorelimit applied: score=%d",
			g_BootMatchTeamScoreLimit);
	}
	if (smokeHarnessIsActive() && sysArgCheck("--debug-grid-fiesta")) {
		g_MatchConfig.options |= MPOPTION_SPAWNWITHWEAPON;
		g_MatchConfig.spawn_weapon_id[0] = '\0';
		g_MatchConfig.spawnWeaponMode = SPAWNWEAPON_MODE_FIESTA;
		g_MatchConfig.spawnWeaponNum = SPAWNWEAPON_FIESTA_SENTINEL;
		sysLogPrintf(LOG_NOTE, "BOOT: configured Grid Fiesta spawn weapons");
	} else if (g_BootDebugSpawnWeapon && g_BootDebugSpawnWeapon[0]) {
		strncpy(g_MatchConfig.spawn_weapon_id, g_BootDebugSpawnWeapon,
			sizeof(g_MatchConfig.spawn_weapon_id) - 1);
		g_MatchConfig.spawn_weapon_id[sizeof(g_MatchConfig.spawn_weapon_id) - 1] = '\0';
		g_MatchConfig.spawnWeaponMode = SPAWNWEAPON_MODE_SPECIFIC;
		g_MatchConfig.spawnWeaponNum = 0xFF;
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-spawn-weapon '%s' (SPECIFIC)",
			g_MatchConfig.spawn_weapon_id);
	}
	/* Resolve stage_id -> stagenum so the room screen displays the
	 * right arena thumbnail even before matchStart() runs. */
	{
		const asset_entry_t *ae = assetCatalogResolve(g_MatchConfig.stage_id);
		if (ae && ae->type == ASSET_ARENA) {
			g_MatchConfig.stagenum = (u8)ae->ext.arena.stagenum;
		}
	}
	/* Add bots up to the requested count.  matchConfigAddBot enforces
	 * MATCH_MAX_SLOTS and the per-humans-cap clamp, so passing 31 on
	 * a malformed config is safe (excess slots silently dropped). */
	s32 debug_body_mp = -1;
	s32 debug_head_mp = -1;
	const char *debug_body_id = bootValidateDebugBotAsset("--debug-bot-body",
		g_BootDebugBotBody, ASSET_BODY, &debug_body_mp);
	const char *debug_head_id = bootValidateDebugBotAsset("--debug-bot-head",
		g_BootDebugBotHead, ASSET_HEAD, &debug_head_mp);
	if ((debug_body_id || debug_head_id) && g_BootLaunchMpBotCount == 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-bot-appearance requested but --launch-mp-room bot_count=0");
	}
	for (s32 i = 0; i < g_BootLaunchMpBotCount; ++i) {
		const u8 bottype = i < g_BootDebugBotSpecCount
			? g_BootDebugBotSpecType[i] : BOTTYPE_GENERAL;
		const u8 botdiff = i < g_BootDebugBotSpecCount
			? g_BootDebugBotSpecDiff[i] : BOTDIFF_NORMAL;
		/* The default stays the literal general/normal call that the custom
		 * body render fixtures rely on (test_mod_external_archive_static). */
		s32 botidx = i < g_BootDebugBotSpecCount
			? matchConfigAddBot(bottype, botdiff, debug_body_id, debug_head_id, NULL)
			: matchConfigAddBot(BOTTYPE_GENERAL, BOTDIFF_NORMAL,
				debug_body_id, debug_head_id, NULL);
		if (i < g_BootDebugBotSpecCount) {
			if (botidx >= 0) {
				sysLogPrintf(LOG_NOTE,
					"BOOT: --debug-bot-spec added slot=%d type=%d difficulty=%d",
					botidx, (s32)bottype, (s32)botdiff);
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-bot-spec could not add type=%d difficulty=%d (no unlocked profile or no slot)",
					(s32)bottype, (s32)botdiff);
			}
		}
		if (botidx >= 0 && (debug_body_id || debug_head_id)) {
			struct matchslot *slot = &g_MatchConfig.slots[botidx];
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-bot-appearance applied slot=%d body='%s' head='%s' mpbody=%d mphead=%d",
				botidx,
				slot->body_id[0] ? slot->body_id : "(empty)",
				slot->head_id[0] ? slot->head_id : "(empty)",
				(s32)slot->bodynum,
				(s32)slot->headnum);
		}
	}
	/* Arm CI -> main menu auto-pop with view=0 (top-level) so the
	 * smoke test's scripted keys can navigate from main menu ->
	 * Combat Simulator to reach the room.  A future enhancement
	 * could open the Room directly via pdguiSoloRoomOpen, but that
	 * pushes the s_SoloRoomActive flag which is only consumed inside
	 * pdguiLobbyRender's NETMODE_NONE branch -- safer to walk the
	 * normal main-menu path which the smoke harness already knows
	 * how to drive.
	 *
	 * c3845: EXCEPT a listen-host that auto-starts (--host-autostart, the
	 * two-process match smoke). It must stay in its native listen-host
	 * lobby to accept the joining client -- auto-opening the main menu
	 * navigates out of the session and tears down the listen server
	 * (CHAT: NET: disconnected ~0.3s after bind), so the client times out
	 * before it can connect. The --host-autostart hook starts the seeded
	 * match directly once the peer has joined. */
	if (!g_BootHostAutostartArmed) {
		g_PostExitMainMenuView = 0;
	}
	/* Drop into CI like the other Room entry points. */
	if (g_StageNum == STAGE_TITLE) {
		g_StageNum = STAGE_CITRAINING;
	}
	/* The listen-host Room renders while the remote peer authenticates. Its
	 * first-frame initializer would otherwise call matchConfigInit() again and
	 * discard the stage, weapon, and roster seeded above before autostart. */
	if (g_BootHostAutostartArmed) {
		extern void pdguiRoomScreenAdoptMatchConfig(void);
		pdguiRoomScreenAdoptMatchConfig();
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-mp-room arena='%s' scenario='%s' bots=%d (seeded g_MatchConfig)",
		g_BootLaunchMpArena,
		g_BootLaunchMpScenario ? g_BootLaunchMpScenario : "(default)",
		g_BootLaunchMpBotCount);

	if (g_BootDebugAutoStartMatch) {
		g_BootDebugAutoStartMatch = false;
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-auto-start-match consumed");
		sysLogPrintf(LOG_NOTE,
			"BOOT: --launch-mp-room direct match start armed");
		g_BootLaunchMpMatchPending = 1;
		g_PostExitMainMenuView = -1;
	}
}

/* --launch-challenge: drop into CI like the Room entry points and arm the
 * one-shot start (bootLaunchChallengeTick). */
static void bootApplyLaunchChallenge(void)
{
	if (g_BootLaunchChallengeSlot < 0) {
		return;
	}

	g_PostExitMainMenuView = -1;

	if (g_StageNum == STAGE_TITLE) {
		g_StageNum = STAGE_CITRAINING;
	}

	g_BootLaunchChallengePending = 1;
	sysLogPrintf(LOG_NOTE, "BOOT: --launch-challenge armed: slot=%d", g_BootLaunchChallengeSlot);
}

/* Mike directive 2026-05-18: --debug-mp-options seed. Applied after
 * matchConfigInit so the bitmask wins over any default the Room would
 * otherwise set on entry. Idempotent (re-applies on each call). */
static void bootApplyDebugMpOptions(void)
{
	if (!g_BootDebugMpOptionsSet) return;
	extern struct mpsetup g_MpSetup;
	matchConfigReplaceUserOptions(g_BootDebugMpOptions,
		"--debug-mp-options");
	g_MpSetup.options = matchConfigGetUserOptions();
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-mp-options applied -- user=0x%08x forced=0x%08x",
		matchConfigGetUserOptions(), g_MatchConfig.options_engine_forced);
}

static void bootApplyDebugInstallReceivedMod(void)
{
	if (!g_BootDebugInstallReceivedMod || !g_BootDebugInstallReceivedMod[0]) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-install-received-mod installing '%s'",
		g_BootDebugInstallReceivedMod);
	if (fileTransferDebugInstallReceivedModForSmoke(
			g_BootDebugInstallReceivedMod, 1) != 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-install-received-mod failed for '%s'",
			g_BootDebugInstallReceivedMod);
	} else {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-install-received-mod applied '%s'",
			g_BootDebugInstallReceivedMod);
	}
}

static void bootApplyDebugReceivePdcaList(void)
{
	if (!g_BootDebugReceivePdcaList || !g_BootDebugReceivePdcaList[0]) return;
	if (!smokeHarnessIsActive()) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-receive-pdca-list is smoke-only; request ignored");
		return;
	}
	s32 delivered = netDistribDebugReceivePdcaListForSmoke(
		g_BootDebugReceivePdcaList);
	sysLogPrintf(delivered > 0 ? LOG_NOTE : LOG_WARNING,
		"BOOT: --debug-receive-pdca-list delivered=%d list='%s'",
		delivered, g_BootDebugReceivePdcaList);
}

static void bootCountCatalogIngressDependency(const char *dep_id,
	asset_type_e expected_type, void *userdata)
{
	(void)dep_id;
	(void)expected_type;
	(*(s32 *)userdata)++;
}

static asset_type_e bootDebugParseAssetType(const char *s);

static void bootApplyDebugRejectCatalogIngressList(void)
{
	if (!g_BootDebugRejectCatalogIngressList
			|| !g_BootDebugRejectCatalogIngressList[0]) return;
	if (!smokeHarnessIsActive()) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-reject-catalog-ingress-list is smoke-only; request ignored");
		return;
	}
	FILE *fp = fopen(g_BootDebugRejectCatalogIngressList, "rb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING,
			"CATALOG.INGRESS.REJECT: could not open list '%s'",
			g_BootDebugRejectCatalogIngressList);
		return;
	}
	char line[FS_MAXPATH + 256];
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		char *kind = line;
		char *path = strchr(kind, '|');
		char *type_name = path ? strchr(path + 1, '|') : NULL;
		char *id = type_name ? strchr(type_name + 1, '|') : NULL;
		if (!path || !type_name) continue;
		*path++ = '\0';
		*type_name++ = '\0';
		asset_type_e type = ASSET_GAMEMODE;
		if (id) {
			*id++ = '\0';
			type = bootDebugParseAssetType(type_name);
		} else {
			/* Backward compatibility with the original kind|path|id fixture. */
			id = type_name;
			type_name = "gamemode";
		}
		if (type == ASSET_NONE || !id[0]) continue;
		s32 registered = 0;
		if (strcmp(kind, "folder") == 0) {
			registered = assetCatalogScanExternalLayoutFolder(
				"catalog_ingress_reject", path);
		} else if (strcmp(kind, "archive") == 0) {
			mod_archive_t *archive = modArchiveOpen(path);
			if (archive) {
				const char *mount_id = "catalog_ingress_reject_archive";
				if (modVfsMount(mount_id, archive)) {
					registered = assetCatalogScanComponentsFromArchive(
						mount_id, archive);
					modVfsUnmount(mount_id);
				}
				modArchiveClose(archive);
			}
		}
		s32 dependency_count = 0;
		catalogDepForEachTyped(id, bootCountCatalogIngressDependency,
			&dependency_count);
		s32 catalog_absent = assetCatalogResolve(id) ? 0 : 1;
		s32 runtime_absent = assetRuntimeFindByTypeAndId(
			type, id) ? 0 : 1;
		sysLogPrintf(registered <= 0 && catalog_absent && runtime_absent
				&& dependency_count == 0
				? LOG_NOTE : LOG_WARNING,
			"CATALOG.INGRESS.REJECT: kind=%s type=%s id='%s' registered=%d catalog_absent=%d runtime_absent=%d deps=%d",
			kind, type_name, id, registered, catalog_absent, runtime_absent,
			dependency_count);
	}
	fclose(fp);
}

/* Accessor for the Room dialog's first-frame auto-start hook (consumed
 * in port/fast3d/pdgui_menu_room.cpp). Returns 1 once when set so a
 * single Start Match press fires; subsequent calls return 0. */
s32 bootConsumeDebugAutoStartMatch(void)
{
	if (!g_BootDebugAutoStartMatch) return 0;
	g_BootDebugAutoStartMatch = false;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-auto-start-match consumed");
	return 1;
}

/* The Room's first-frame tab request (--debug-room-tab): returns the tab
 * once, -1 after that or when unset. */
s32 bootConsumeDebugRoomTab(void)
{
	const s32 tab = g_BootDebugRoomTab;

	if (tab < 0) return -1;
	g_BootDebugRoomTab = -1;
	sysLogPrintf(LOG_NOTE, "BOOT: --debug-room-tab consumed: tab=%d", tab);
	return tab;
}

/* Arm the --debug-mount-bike one-shot.  The actual mount runs inside
 * mainTick once activeprops is populated; see
 * bootDebugMountBikeTick() below. */
static void bootApplyDebugMountBike(void)
{
	if (!g_BootMountBike) {
		return;
	}
	g_BootMountBikePending = 1;
	g_BootMountBikeStage = -1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-mount-bike armed (will mount on first hoverbike prop after stage load)");
}

/* c115 (2026-05-14): Arm the --debug-spawn-at one-shot. Parses the
 * comma-separated tokens captured at CLI-parse time into x/y/z/room
 * floats + s32 and sets g_BootSpawnAtPending. The actual teleport
 * runs inside mainTick once the player prop exists; see
 * bootDebugSpawnAtTick() below. Malformed input (wrong token count,
 * unparseable numbers) leaves the latch off and emits a WARNING. */
static void bootApplyDebugSpawnAt(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	/* Local copy so strtok_r can mutate it. */
	char buf[128];
	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* strtok is fine here -- we run on the main thread before any
	 * subsystem has spun up worker threads, and we don't nest calls. */
	const char *tokX = strtok(buf, ",");
	const char *tokY = strtok(NULL, ",");
	const char *tokZ = strtok(NULL, ",");
	const char *tokR = strtok(NULL, ",");
	if (!tokX || !tokY || !tokZ || !tokR) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-spawn-at expects 4 comma-separated tokens (x,y,z,room); got: '%s'",
			arg);
		return;
	}

	g_BootSpawnAtX    = (f32)strtod(tokX, NULL);
	g_BootSpawnAtY    = (f32)strtod(tokY, NULL);
	g_BootSpawnAtZ    = (f32)strtod(tokZ, NULL);
	g_BootSpawnAtRoom = (s32)strtol(tokR, NULL, 0);
	g_BootSpawnAtPending = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-spawn-at armed: pos=(%f,%f,%f) room=%d",
		g_BootSpawnAtX, g_BootSpawnAtY, g_BootSpawnAtZ, g_BootSpawnAtRoom);
}

/* B-1291: the companions of --debug-spawn-at. Without --debug-spawn-at they
 * do nothing, and say so. */
static void bootApplyDebugSpawnAtCompanions(s32 after_intro, const char *look_at)
{
	if (after_intro) {
		if (!g_BootSpawnAtPending) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-spawn-at-after-intro ignored: --debug-spawn-at is not armed");
		} else {
			g_BootSpawnAtAfterIntro = 1;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-spawn-at-after-intro armed (the teleport waits for one second of control)");
		}
	}

	if (look_at && look_at[0]) {
		char buf[128];
		const char *tokX;
		const char *tokY;
		const char *tokZ;

		strncpy(buf, look_at, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = 0;
		tokX = strtok(buf, ",");
		tokY = strtok(NULL, ",");
		tokZ = strtok(NULL, ",");
		if (!tokX || !tokY || !tokZ) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-spawn-at-look-at expects 3 comma-separated tokens (x,y,z); got: '%s'",
				look_at);
		} else if (!g_BootSpawnAtPending) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-spawn-at-look-at ignored: --debug-spawn-at is not armed");
		} else {
			g_BootSpawnAtLookX = (f32)strtod(tokX, NULL);
			g_BootSpawnAtLookY = (f32)strtod(tokY, NULL);
			g_BootSpawnAtLookZ = (f32)strtod(tokZ, NULL);
			g_BootSpawnAtLookAt = 1;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-spawn-at-look-at armed: point=(%f,%f,%f)",
				g_BootSpawnAtLookX, g_BootSpawnAtLookY, g_BootSpawnAtLookZ);
		}
	}
}

/* B-769 (2026-06-06): Arm a smoke-only camera override. The actual
 * camera write runs after lvTickPlayer and before lvRender so the
 * first-person weapon render path cannot be overwritten by MP swirl or
 * other per-frame tick logic before playerRenderHud runs. */
static void bootApplyDebugForceFirstPerson(void)
{
	if (!g_BootDebugForceFirstPerson) {
		return;
	}
	g_BootDebugForceFirstPersonPending = 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-force-first-person armed (will hold player 0 in default camera before render)");
}

static void bootApplyDebugForceFirstPersonLook(const char *arg)
{
	char buf[128];
	char *tokX;
	char *tokY;
	char *tokZ;
	f32 x;
	f32 y;
	f32 z;
	f32 len_sq;

	if (!arg || !arg[0]) {
		return;
	}

	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	tokX = strtok(buf, ",");
	tokY = strtok(NULL, ",");
	tokZ = strtok(NULL, ",");
	if (!tokX || !tokY || !tokZ) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-force-first-person-look expects 3 comma-separated tokens (x,y,z); got: '%s'",
			arg);
		return;
	}

	x = (f32)strtod(tokX, NULL);
	y = (f32)strtod(tokY, NULL);
	z = (f32)strtod(tokZ, NULL);
	len_sq = x * x + y * y + z * z;
	if (len_sq < 0.000001f) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-force-first-person-look ignored zero-length vector: '%s'",
			arg);
		return;
	}

	{
		f32 inv_len = 1.0f / sqrtf(len_sq);
		g_BootDebugForceFirstPersonLookVec.x = x * inv_len;
		g_BootDebugForceFirstPersonLookVec.y = y * inv_len;
		g_BootDebugForceFirstPersonLookVec.z = z * inv_len;
	}
	g_BootDebugForceFirstPersonLook = true;
	g_BootDebugForceFirstPersonLookLogged = false;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-force-first-person-look armed: look=(%f,%f,%f)",
		g_BootDebugForceFirstPersonLookVec.x,
		g_BootDebugForceFirstPersonLookVec.y,
		g_BootDebugForceFirstPersonLookVec.z);
}

static void bootApplyDebugForceFirstPersonCamOffset(const char *arg)
{
	char buf[128];
	char *tokX;
	char *tokY;
	char *tokZ;

	if (!arg || !arg[0]) {
		return;
	}

	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	tokX = strtok(buf, ",");
	tokY = strtok(NULL, ",");
	tokZ = strtok(NULL, ",");
	if (!tokX || !tokY || !tokZ) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-force-first-person-cam-offset expects 3 comma-separated tokens (x,y,z); got: '%s'",
			arg);
		return;
	}

	g_BootDebugForceFirstPersonCamOffsetVec.x = (f32)strtod(tokX, NULL);
	g_BootDebugForceFirstPersonCamOffsetVec.y = (f32)strtod(tokY, NULL);
	g_BootDebugForceFirstPersonCamOffsetVec.z = (f32)strtod(tokZ, NULL);
	g_BootDebugForceFirstPersonCamOffset = true;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-force-first-person-cam-offset armed: offset=(%f,%f,%f)",
		g_BootDebugForceFirstPersonCamOffsetVec.x,
		g_BootDebugForceFirstPersonCamOffsetVec.y,
		g_BootDebugForceFirstPersonCamOffsetVec.z);
}

static void bootApplyDebugPlaceBotNearPlayer(void)
{
	if (!g_BootDebugPlaceBotNearPlayer) {
		return;
	}
	g_BootDebugPlaceBotNearPlayerPending = 1;
	g_BootDebugPlaceBotNearPlayerFrames = 0;
	g_BootDebugPlaceBotNearPlayerReadyFrames = 0;
	g_BootDebugPlaceBotNearPlayerHoldFrames = 0;
	g_BootDebugPlaceBotNearPlayerHoldFailures = 0;
	g_BootDebugPlaceBotNearPlayerHoldStartMs = 0;
	g_BootDebugPlaceBotNearPlayerLogged = false;
	g_BootDebugPlaceBotNearPlayerProp = NULL;
	g_BootDebugPlaceBotNearPlayerTraceMask = 0;
	g_BootDebugPlaceBotNearPlayerTraceBudget = 160;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-place-bot-near-player armed (stable_frames=%d hold_ms=%u)",
		k_BootDebugPlaceBotNearPlayerStableFrames,
		g_BootDebugPlaceBotNearPlayerHoldDurationMs);
}

s32 bootDebugPlaceBotNearPlayerIsAuditProp(const struct prop *prop)
{
	return (g_BootDebugPlaceBotNearPlayerPending
			&& g_BootDebugPlaceBotNearPlayerProp
			&& prop == g_BootDebugPlaceBotNearPlayerProp)
		|| (g_BootDebugFaceBotProp && prop == g_BootDebugFaceBotProp);
}

void bootDebugPlaceBotNearPlayerTrace(const char *stage, const struct prop *prop,
		s32 once_bit, s32 a, s32 b, s32 c, s32 d)
{
	if (!modAssetCompilerGeneratedModeldefRenderAuditEnabled()) {
		return;
	}
	/* The placed bot on the peer that placed it, or the simulant this peer's
	 * --debug-face-nearest-bot is facing (finding 3). */
	const struct prop *placed = g_BootDebugPlaceBotNearPlayerPending
		? g_BootDebugPlaceBotNearPlayerProp : NULL;
	if (!placed && !g_BootDebugFaceBotProp) {
		return;
	}
	if (!prop) {
		prop = placed ? placed : g_BootDebugFaceBotProp;
	}
	if (!prop || (prop != placed && prop != g_BootDebugFaceBotProp)) {
		return;
	}
	if (once_bit >= 0 && once_bit < 32) {
		const u32 bit = 1u << once_bit;
		if (g_BootDebugPlaceBotNearPlayerTraceMask & bit) {
			return;
		}
		g_BootDebugPlaceBotNearPlayerTraceMask |= bit;
	} else {
		if (g_BootDebugPlaceBotNearPlayerTraceBudget <= 0) {
			return;
		}
		g_BootDebugPlaceBotNearPlayerTraceBudget--;
	}

	const struct chrdata *chr = prop->type == PROPTYPE_CHR ? prop->chr : NULL;
	const struct model *model = chr ? chr->model : NULL;
	const struct modeldef *modeldef = model ? model->definition : NULL;

	sysLogPrintf(LOG_NOTE,
		"BOT.RENDER.AUDIT: stage=%s prop=%p type=%d flags=0x%x active=%d rooms=(%d,%d,%d) pos=(%f,%f,%f) chr=%p chrnum=%d body=%d head=%d hidden=0x%x chrflags=0x%x model=%p modeldef=%p a=%d b=%d c=%d d=%d",
		stage ? stage : "(null)",
		(void *)prop,
		(s32)prop->type,
		(u32)prop->flags,
		(s32)prop->active,
		(s32)prop->rooms[0],
		(s32)prop->rooms[1],
		(s32)prop->rooms[2],
		prop->pos.x,
		prop->pos.y,
		prop->pos.z,
		(void *)chr,
		chr ? (s32)chr->chrnum : -1,
		chr ? (s32)chr->bodynum : -1,
		chr ? (s32)chr->headnum : -1,
		chr ? (u32)chr->hidden : 0,
		chr ? (u32)chr->chrflags : 0,
		(void *)model,
		(void *)modeldef,
		a, b, c, d);
}

/* c118 (2026-05-15): Arm the --launch-load-agent one-shot. Captures
 * the agent name into a static buffer; agentSessionActivate runs inside
 * mainTick once g_Vars.lvframenum >= 4 (same gate as the
 * other deferred ticks). Empty / missing arg leaves the latch off.
 * Names longer than the shared Agent Profile Store identity limit are
 * rejected before the one-shot is armed. */
static void bootApplyLaunchLoadAgent(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootLoadAgentName) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-load-agent name too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootLoadAgentName, arg, maxLen);
	g_BootLoadAgentName[maxLen] = '\0';
	g_BootLoadAgentArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-load-agent armed: name='%s'",
		g_BootLoadAgentName);
}

/* c118 (2026-05-15): Arm the --listen-bind <port> one-shot. Parses the
 * port string into an s32 and validates it's in the legal UDP range.
 * The actual netStartServer call runs inside mainTick once g_NetInit
 * is set; see bootListenBindTick() below. Out-of-range values leave
 * the latch off and emit a WARNING. */
static void bootApplyListenBind(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	char *end = NULL;
	long portval = strtol(arg, &end, 0);
	if (!end || *end != '\0' || portval <= 0 || portval > 0xFFFF) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --listen-bind expects a UDP port in 1..65535; got: '%s'",
			arg);
		return;
	}

	g_BootListenBindPort = (s32)portval;
	g_BootListenBindArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --listen-bind armed: port=%d",
		g_BootListenBindPort);
}

/* c118 (2026-05-15): Arm the --connect-host <addr>:<port> one-shot.
 * Captures the address+port string into a static buffer; the actual
 * netStartClient call runs inside mainTick once g_NetInit is set; see
 * bootConnectHostTick() below. Empty / missing arg leaves the latch
 * off. Buffer is 64 bytes (matches NET_MAX_ADDR in port/include/net/
 * net.h but we keep this file independent of that header's constants). */
static void bootApplyConnectHost(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootConnectHostAddr) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --connect-host address too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootConnectHostAddr, arg, maxLen);
	g_BootConnectHostAddr[maxLen] = '\0';
	g_BootConnectHostArmed = 1;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --connect-host armed: addr='%s'",
		g_BootConnectHostAddr);
}

/* Track 2c (c3807, 2026-05-16): Arm the --dump-swarm-state <path>
 * one-shot. Captures the path into the static buffer and primes the
 * dump countdown to SWARM_DUMP_MAX_FRAMES. The actual readback +
 * append runs inside bootDumpSwarmStateTick() once the player prop
 * is positioned and the GPU dispatch loop has had a chance to populate
 * the texture. Path strings longer than the buffer or empty input
 * leave the latch off with a WARNING. */
static void bootApplyDumpSwarmState(const char *arg)
{
	if (!arg || !arg[0]) {
		return;
	}

	const size_t maxLen = sizeof(g_BootDumpSwarmPath) - 1;
	if (strlen(arg) > maxLen) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --dump-swarm-state path too long (max %zu chars); got: '%s'",
			maxLen, arg);
		return;
	}

	strncpy(g_BootDumpSwarmPath, arg, maxLen);
	g_BootDumpSwarmPath[maxLen] = '\0';
	g_BootDumpSwarmArmed       = 1;
	g_BootDumpSwarmDumpsLeft   = SWARM_DUMP_MAX_FRAMES;
	g_BootDumpSwarmTickCounter = 0;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --dump-swarm-state armed: path='%s' max_dumps=%d interval=%d_frames",
		g_BootDumpSwarmPath, SWARM_DUMP_MAX_FRAMES, SWARM_DUMP_INTERVAL_FRAMES);
}

static char *bootTrimArgToken(char *s)
{
	char *end;

	if (!s) {
		return s;
	}
	while (*s && isspace((u8)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}

	end = s + strlen(s) - 1;
	while (end > s && isspace((u8)*end)) {
		*end-- = '\0';
	}
	return s;
}

static const char *bootDebugAssetTypeName(asset_type_e type)
{
	switch (type) {
	case ASSET_MODEL:     return "model";
	case ASSET_BODY:      return "body";
	case ASSET_HEAD:      return "head";
	case ASSET_WEAPON:    return "weapon";
	case ASSET_PROJECTILE: return "projectile";
	case ASSET_ENTITY:    return "entity";
	case ASSET_MATERIAL:  return "material";
	case ASSET_MAP:       return "map";
	case ASSET_ARENA:     return "arena";
	case ASSET_CHARACTER: return "character";
	case ASSET_SKIN:      return "skin";
	case ASSET_PROP:      return "prop";
	case ASSET_VEHICLE:   return "vehicle";
	case ASSET_MISSION:   return "mission";
	case ASSET_ANIMATION: return "animation";
	case ASSET_TEXTURE:   return "texture";
	case ASSET_EFFECT:    return "effect";
	case ASSET_AUDIO:     return "audio";
	case ASSET_LANG:      return "lang";
	case ASSET_UI:        return "ui";
	case ASSET_FONT:      return "font";
	case ASSET_HUD:       return "hud";
	case ASSET_GAMEMODE:  return "gamemode";
	case ASSET_BOT_PROFILE: return "botprofile";
	case ASSET_SCENARIO:  return "scenario";
	case ASSET_THEME:     return "theme";
	case ASSET_AI_LISTS:  return "ai_lists";
	default:              return "unknown";
	}
}

static asset_type_e bootDebugParseAssetType(const char *s)
{
	if (!s || !s[0]) {
		return ASSET_NONE;
	}
	if (strcmp(s, "model") == 0) return ASSET_MODEL;
	if (strcmp(s, "body") == 0) return ASSET_BODY;
	if (strcmp(s, "head") == 0) return ASSET_HEAD;
	if (strcmp(s, "weapon") == 0) return ASSET_WEAPON;
	if (strcmp(s, "projectile") == 0) return ASSET_PROJECTILE;
	if (strcmp(s, "entity") == 0) return ASSET_ENTITY;
	if (strcmp(s, "material") == 0) return ASSET_MATERIAL;
	if (strcmp(s, "map") == 0) return ASSET_MAP;
	if (strcmp(s, "arena") == 0) return ASSET_ARENA;
	if (strcmp(s, "character") == 0) return ASSET_CHARACTER;
	if (strcmp(s, "skin") == 0) return ASSET_SKIN;
	if (strcmp(s, "prop") == 0) return ASSET_PROP;
	if (strcmp(s, "vehicle") == 0) return ASSET_VEHICLE;
	if (strcmp(s, "mission") == 0) return ASSET_MISSION;
	if (strcmp(s, "animation") == 0 || strcmp(s, "anim") == 0) return ASSET_ANIMATION;
	if (strcmp(s, "texture") == 0) return ASSET_TEXTURE;
	if (strcmp(s, "effect") == 0) return ASSET_EFFECT;
	if (strcmp(s, "audio") == 0 || strcmp(s, "sfx") == 0
			|| strcmp(s, "voice") == 0 || strcmp(s, "song") == 0
			|| strcmp(s, "music") == 0) return ASSET_AUDIO;
	if (strcmp(s, "lang") == 0 || strcmp(s, "language") == 0) return ASSET_LANG;
	if (strcmp(s, "ui") == 0) return ASSET_UI;
	if (strcmp(s, "font") == 0) return ASSET_FONT;
	if (strcmp(s, "hud") == 0) return ASSET_HUD;
	if (strcmp(s, "gamemode") == 0) return ASSET_GAMEMODE;
	if (strcmp(s, "botprofile") == 0 || strcmp(s, "bot_profile") == 0) return ASSET_BOT_PROFILE;
	if (strcmp(s, "scenario") == 0) return ASSET_SCENARIO;
	if (strcmp(s, "theme") == 0) return ASSET_THEME;
	if (strcmp(s, "ai_lists") == 0 || strcmp(s, "ai") == 0) return ASSET_AI_LISTS;
	return ASSET_NONE;
}

static void bootDebugLogTypedPayload(asset_type_e type, const char *asset_id, s32 loaded)
{
	const asset_entry_t *entry = assetCatalogResolve(asset_id);
	const char *type_name = bootDebugAssetTypeName(type);

	if (!entry) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-load-catalog-assets result type=%s id='%s' result=MISSING",
			type_name, asset_id ? asset_id : "(null)");
		if (asset_id && strncmp(asset_id, "ingress:", 8) == 0) {
			s32 dependency_count = 0;
			s32 loader_absent = -1;
			catalogDepForEachTyped(asset_id,
				bootCountCatalogIngressDependency, &dependency_count);
			if (type == ASSET_ANIMATION) {
				const char *wanted = strchr(asset_id, ':');
				wanted = wanted ? wanted + 1 : asset_id;
				loader_absent = 1;
				for (s32 i = 0; i < loaderPoolGetAnimationCount(); i++) {
					const char *name = loaderPoolGetAnimationName(i);
					if (name && strcmp(name, wanted) == 0) {
						loader_absent = 0;
						break;
					}
				}
			}
			sysLogPrintf(LOG_NOTE,
				"CATALOG.INGRESS.REJECT.STATE: id='%s' catalog_absent=1 provider_absent=1 runtime_absent=%d deps=%d loader_absent=%d",
				asset_id,
				assetRuntimeFindByTypeAndId(type, asset_id) ? 0 : 1,
				dependency_count, loader_absent);
		}
		return;
	}

	if (loaded && type == ASSET_TEXTURE) {
		mod_texture_rgba32_source_t source;
		extern s32 g_NotLoadMod;
		s32 prior_not_load_mod = g_NotLoadMod;
		s32 source_result;

		/* The CLI probe runs during menu boot, where g_NotLoadMod normally
		 * suppresses gameplay texture overrides.  Temporarily lift only that
		 * presentation policy so the requested public source reaches the same
		 * decoder used at gameplay consumption time. */
		g_NotLoadMod = 0;
		source_result = entry->source_texnum >= 0
			? modTextureLoadRgba32Source((u16)entry->source_texnum, &source)
			: -1;
		g_NotLoadMod = prior_not_load_mod;
		if (source_result > 0) {
			modTextureFreeRgba32Source(&source);
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-load-catalog-assets consumer type=texture id='%s' result=OK",
				asset_id);
		} else {
			loaded = 0;
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-load-catalog-assets consumer type=texture id='%s' result=FAIL",
				asset_id);
		}
	}

	if (loaded && type == ASSET_FONT) {
		const char *path = entry->ext.font.font_file;
		size_t len = path ? strlen(path) : 0;
		s32 is_vector = len >= 4 &&
			(strcmp(path + len - 4, ".ttf") == 0 ||
			 strcmp(path + len - 4, ".otf") == 0);
		if (is_vector) {
			char error[192];
			if (!pdguiFontModValidateCatalogId(asset_id, error, sizeof(error))) {
				loaded = 0;
				catalogReleaseTypedAsset(ASSET_FONT, asset_id);
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-load-catalog-assets consumer type=font id='%s' result=FAIL reason=%s",
					asset_id, error[0] ? error : "invalid public vector face");
			} else {
				sysLogPrintf(LOG_NOTE,
					"BOOT: --debug-load-catalog-assets consumer type=font id='%s' result=OK",
					asset_id);
			}
		}
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-load-catalog-assets result type=%s id='%s' result=%s state=%d payload=%d ref=%d bytes=%u source_tex=%d source_anim=%d source_sound=%d",
		type_name, asset_id, loaded ? "OK" : "FAIL",
		(s32)entry->load_state, (s32)entry->payload_kind,
		(s32)entry->ref_count, entry->data_size_bytes,
		(s32)entry->source_texnum, (s32)entry->source_animnum,
		(s32)entry->source_soundnum);

	if (!loaded) {
		return;
	}

	{
		const char *catalog_path = entry->source.primary.provider == fileProvider()
			? fileProviderPath(entry->source.primary) : NULL;
		const asset_runtime_binding_t *binding =
			assetRuntimeFindByTypeAndId(type, asset_id);
		/* Model, texture, animation, and audio use specialized payload adapters
		 * rather than asset_runtime_binding_t. A successful typed load consumes
		 * entryGetFilePath(entry), which is this exact FileProvider path; report
		 * that selected activation path without inventing a generic binding. */
		const char *runtime_path = binding ? binding->primary_path
			: (loaded ? catalog_path : NULL);
		const char *adapter = binding ? "binding" : "specialized";
		s32 exact = catalog_path && runtime_path
			&& strcmp(catalog_path, runtime_path) == 0;
		sysLogPrintf(exact ? LOG_NOTE : LOG_WARNING,
			"CATALOG.INGRESS.IDENTITY: type=%s id='%s' provider=%s adapter=%s length=%u exact=%d catalog='%s' runtime='%s'",
			type_name, asset_id,
			entry->source.primary.provider == fileProvider() ? "FileProvider" : "other",
			adapter,
			catalog_path ? (u32)strlen(catalog_path) : 0, exact,
			catalog_path ? catalog_path : "", runtime_path ? runtime_path : "");
	}

	if (type == ASSET_MODEL || type == ASSET_WEAPON || type == ASSET_BODY
			|| type == ASSET_HEAD || type == ASSET_PROP) {
		struct modeldef *modeldef = catalogGetLoadedModeldef(asset_id);
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-load-catalog-assets modeldef type=%s id='%s' ptr=%p",
			type_name, asset_id, (void *)modeldef);
	}

	if (type == ASSET_ARENA || type == ASSET_MAP || type == ASSET_GAMEMODE
			|| type == ASSET_SCENARIO) {
		struct colmesh *mesh = catalogGetLoadedColmesh(asset_id);
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-load-catalog-assets colmesh type=%s id='%s' ptr=%p",
			type_name, asset_id, (void *)mesh);
	}

	if (type == ASSET_ANIMATION) {
		const struct animtableentry *anim = NULL;
		u32 clip_size = 0;
		const void *clip = catalogGetLoadedAnimationClip(asset_id, &anim, &clip_size);
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-load-catalog-assets animation id='%s' clip=%p entry=%p bytes=%u",
			asset_id, clip, (const void *)anim, clip_size);
	}
}

s32 bootEnsureUiArchivesReadyAfterTextureInit(void)
{
	static s32 s_ready = 0;
	char data_root[FS_MAXPATH + 1];
	loader_walker_kind_result_t kr;
	s32 texture_result;
	s32 written;

	if (s_ready) {
		return 1;
	}

	/* The first boot extraction pass runs before texReset(), so UI textures
	 * cannot be decoded into .pdui archives there. Run the texture emitter
	 * first so source-only UI decode can read the public .pdtexture archives,
	 * then emit/register .pdui before rendering repairs a missing UI set. */
	texture_result = romExtractAllPdtexture(0);
	written = romExtractAllPdui(0);
	if (texture_result < 0 || written < 0) {
		bootRecordAssetPhase("pdtexture-late-ui-repair", texture_result);
		bootRecordAssetPhase("pdui-late-repair", written);
		return -1;
	}
	fsDataDir(data_root, sizeof(data_root));
	if (!data_root[0]) {
		bootRecordAssetPhase("pdui-late-data-root", -1);
		return 0;
	}
	loaderWalkerScanUi(data_root, &kr);
	if (kr.envelope_failures > 0 || kr.register_failures > 0) {
		SDL_AtomicAdd(&g_BootAssetChainFailures,
			kr.envelope_failures + kr.register_failures);
		sysLoudFailf("ASSETCHAIN",
			"late UI walker rejected %d archive envelope(s) and "
			"%d catalog registration(s)",
			kr.envelope_failures, kr.register_failures);
		return -1;
	}
	if (kr.entries_scanned > 0 || kr.entries_registered > 0 || written > 0) {
		s_ready = 1;
		modmgrCatalogChanged();
		sysLogPrintf(LOG_NOTE,
			"BOOT: UI source archives ready after texReset written=%d scanned=%d registered=%d",
			written, kr.entries_scanned, kr.entries_registered);
		pdguiThemeReloadPduiSourceTextures();
	}
	return s_ready;
}

static void bootEnsureUiArchivesReadyForCliSourceLoads(void)
{
	(void)bootEnsureUiArchivesReadyAfterTextureInit();
}

static const char *g_BootDebugLoadCatalogAssetsArg = NULL;
static const char *g_BootDebugLoadCatalogAssetsFileArg = NULL;
static s32 g_BootDebugLoadCatalogAssetsSourceOnly = 0;
static s32 g_BootDebugLoadCatalogAssetsPending = 0;
static s32 g_BootDebugLoadCatalogAssetsApplied = 0;
static const char *g_BootDebugProbeAnimationSourceArg = NULL;
static s32 g_BootDebugProbeAnimationSourceOnly = 0;
static const char *g_BootDebugPlayCatalogAudioArg = NULL;
static s32 g_BootDebugPlayCatalogAudioSourceOnly = 0;
static s32 g_BootDebugAudioPlaybackStarted = 0;
static u32 g_BootDebugAudioPlaybackStartTicks = 0;

static const char *bootDebugAudioCategoryName(s32 category)
{
	switch (category) {
	case AUDIO_CAT_SFX:   return "sfx";
	case AUDIO_CAT_VOICE: return "voice";
	case AUDIO_CAT_MUSIC: return "song";
	default:              return "audio";
	}
}

static s32 bootDebugParseAudioCategory(const char *s)
{
	if (!s || !s[0]) {
		return -1;
	}
	if (strcmp(s, "sfx") == 0 || strcmp(s, "audio") == 0) {
		return AUDIO_CAT_SFX;
	}
	if (strcmp(s, "voice") == 0) {
		return AUDIO_CAT_VOICE;
	}
	if (strcmp(s, "song") == 0 || strcmp(s, "music") == 0) {
		return AUDIO_CAT_MUSIC;
	}
	return -1;
}

static s32 bootDebugPlayCatalogSound(const char *kind, const char *asset_id,
	const catalog_audio_result_t *audio)
{
	struct sndstate *handle = NULL;
	struct sndstate *state;

	state = sndStart(0, (s16)audio->sound_id, &handle, -1, -1, -1.0f, -1, -1);
	if (state && audioFileSoundOwnsHandle(state)
			&& audioFileSoundGetState(state) == AL_PLAYING) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK sound=%d playback=file_voice handle=%p state=%p",
			kind, asset_id, audio->sound_id, (void *)handle, (void *)state);
		return 1;
	}
	if (sndIsPlayingPublicMp3Sound((s16)audio->sound_id)) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK sound=%d playback=public_mp3 state=native_started",
			kind, asset_id, audio->sound_id);
		return 1;
	}
	sysLogPrintf(LOG_WARNING,
		"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL sound=%d handle=%p state=%p",
		kind, asset_id, audio->sound_id, (void *)handle, (void *)state);
	return 0;
}

static s32 bootDebugPlayCatalogSong(const char *kind, const char *asset_id,
	const catalog_audio_result_t *audio)
{
	s32 track = audio->sound_id;

	if (track < 0) {
		track = modSequenceVirtualTrackForCatalogId(asset_id);
	}
	if (track >= 0) {
		if (g_SndDisabled) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL track=%d state=audio_disabled",
				kind, asset_id, track);
			return 0;
		}
		for (s32 i = 0; i < ARRAYCOUNT(g_SeqInstances); i++) {
			struct seqinstance *seq = &g_SeqInstances[i];
			if (seq->seqp == NULL || n_alCSPGetState(seq->seqp) != AL_STOPPED) {
				continue;
			}
			if (seqPlay(seq, track)) {
				seqSetVolume(seq, musicGetVolume());
				sysLogPrintf(LOG_NOTE,
					"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK track=%d playback=native_sequence player=%d state=native_started",
					kind, asset_id, track, i);
				return 1;
			}
			/* A rejected source must not be retried in a different player. */
			break;
		}
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL track=%d state=playback_rejected",
			kind, asset_id, track);
		return 0;
	}
	sysLogPrintf(LOG_WARNING,
		"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL track=%d state=unassigned",
		kind, asset_id, track);
	return 0;
}

static void bootApplyDebugPlayCatalogAudioToken(char *token,
	s32 force_source_only)
{
	char *eq;
	char *kind;
	char *asset_id;
	s32 expected_category;
	catalog_audio_result_t audio;
	asset_type_e prior_source_only;
	s32 loaded;

	token = bootTrimArgToken(token);
	if (*token == '\0' || *token == '#') {
		return;
	}

	eq = strchr(token, '=');
	if (!eq) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio token '%s' missing kind=id form",
			token);
		return;
	}

	*eq = '\0';
	kind = bootTrimArgToken(token);
	asset_id = bootTrimArgToken(eq + 1);
	expected_category = bootDebugParseAudioCategory(kind);

	if (expected_category < 0 || !asset_id[0]) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio invalid token kind='%s' id='%s'",
			kind, asset_id);
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-play-catalog-audio request kind=%s id='%s' source_only=%d",
		kind, asset_id, force_source_only ? 1 : 0);

	if (!catalogResolveAudio(asset_id, &audio)) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=MISSING",
			kind, asset_id);
		return;
	}
	if (audio.category != expected_category) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=CATEGORY_MISMATCH actual=%s",
			kind, asset_id, bootDebugAudioCategoryName(audio.category));
		return;
	}

	prior_source_only = assetSourceDebugOnlyType();
	if (force_source_only) {
		assetSourceDebugSetOnlyType(ASSET_AUDIO);
	}
	loaded = catalogLoadTypedAsset(ASSET_AUDIO, asset_id);
	if (loaded && audio.category == AUDIO_CAT_MUSIC) {
		loaded = bootDebugPlayCatalogSong(kind, asset_id, &audio);
	} else if (loaded) {
		loaded = bootDebugPlayCatalogSound(kind, asset_id, &audio);
	}
	if (loaded) {
		g_BootDebugAudioPlaybackStarted = 1;
		g_BootDebugAudioPlaybackStartTicks = SDL_GetTicks();
	}
	if (force_source_only) {
		assetSourceDebugSetOnlyType(prior_source_only);
	}
	if (!loaded) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio playback kind=%s id='%s' result=FAIL",
			kind, asset_id);
	}
}

static void bootExitAfterCatalogProbesIfRequested(void)
{
	if (g_BootDebugLoadCatalogAssetsApplied
			&& sysArgCheck("--debug-exit-after-catalog-probes")
			&& smokeHarnessIsActive()) {
		if (g_BootDebugAudioPlaybackStarted) {
			u32 elapsed = SDL_GetTicks() - g_BootDebugAudioPlaybackStartTicks;
			/* Let the normal audio scheduler run before ending a playback probe. */
			if (elapsed < 1000u) {
				return;
			}
			sysLogPrintf(LOG_NOTE,
				"BOOT: catalog audio native playback scheduler window elapsed_ms=%u", elapsed);
		}
		smokeHarnessExit(0, "scripted_exit");
	}
}

void bootCatalogProbeExitTick(void)
{
	bootExitAfterCatalogProbesIfRequested();
}

static void bootApplyDebugPlayCatalogAudio(const char *arg,
	s32 force_source_only)
{
	char buf[2048];
	char *cursor;

	if (!arg || !arg[0]) {
		return;
	}

	if (strlen(arg) >= sizeof(buf)) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-play-catalog-audio argument too long; max=%zu",
			sizeof(buf) - 1);
		return;
	}

	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	cursor = buf;
	while (cursor && *cursor) {
		char *next = strchr(cursor, ',');

		if (next) {
			*next++ = '\0';
		}

		bootApplyDebugPlayCatalogAudioToken(cursor, force_source_only);
		cursor = next;
	}
	bootExitAfterCatalogProbesIfRequested();
}

static void bootApplyDebugLoadCatalogAssetToken(char *token,
	s32 force_source_only)
{
	char *eq;
	char *type_name;
	char *asset_id;
	asset_type_e type;
	asset_type_e prior_source_only;
	s32 loaded;

	token = bootTrimArgToken(token);
	if (*token == '\0' || *token == '#') {
		return;
	}

	eq = strchr(token, '=');
	if (!eq) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-load-catalog-assets token '%s' missing type=id form",
			token);
		return;
	}

	*eq = '\0';
	type_name = bootTrimArgToken(token);
	asset_id = bootTrimArgToken(eq + 1);
	type = bootDebugParseAssetType(type_name);

	if (type == ASSET_NONE || !asset_id[0]) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-load-catalog-assets invalid token type='%s' id='%s'",
			type_name, asset_id);
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-load-catalog-assets request type=%s id='%s' source_only=%d",
		bootDebugAssetTypeName(type), asset_id, force_source_only ? 1 : 0);
	prior_source_only = assetSourceDebugOnlyType();
	if (force_source_only) {
		assetSourceDebugSetOnlyType(type);
	}
	loaded = catalogLoadTypedAsset(type, asset_id);
	if (force_source_only) {
		assetSourceDebugSetOnlyType(prior_source_only);
	}
	bootDebugLogTypedPayload(type, asset_id, loaded);
}

static void bootApplyDebugLoadCatalogAssets(const char *arg,
	s32 force_source_only)
{
	char buf[2048];
	char *cursor;

	if (!arg || !arg[0]) {
		return;
	}

	if (strlen(arg) >= sizeof(buf)) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-load-catalog-assets argument too long; max=%zu",
			sizeof(buf) - 1);
		return;
	}

	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	cursor = buf;
	while (cursor && *cursor) {
		char *next = strchr(cursor, ',');

		if (next) {
			*next++ = '\0';
		}

		bootApplyDebugLoadCatalogAssetToken(cursor, force_source_only);
		cursor = next;
	}
}

static void bootApplyDebugLoadCatalogAssetsFile(const char *path,
	s32 force_source_only)
{
	FILE *f;
	char line[1024];
	char full_path[FS_MAXPATH + 1];
	s32 count = 0;
	s32 first_line = 1;

	if (!path || !path[0]) {
		return;
	}

	f = fopen(path, "rb");
	if (!f) {
		f = fsFileOpenRead(path);
	}
	if (!f) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-load-catalog-assets-file could not open '%s'",
			path);
		return;
	}

	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		if (first_line && len >= 3 &&
				(u8)line[0] == 0xef && (u8)line[1] == 0xbb &&
				(u8)line[2] == 0xbf) {
			memmove(line, line + 3, len - 2);
			len -= 3;
		}
		first_line = 0;
		while (len > 0 && (line[len - 1] == '\n' ||
				line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		bootApplyDebugLoadCatalogAssetToken(line, force_source_only);
		count++;
	}
	fclose(f);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-load-catalog-assets-file loaded %d line(s) from '%s'",
		count, fsFullPath(path, full_path, sizeof(full_path)));
}

static void bootArmDebugLoadCatalogAssets(void)
{
	const char *arg = sysArgGetString("--debug-load-catalog-assets");
	const char *file_arg =
		sysArgGetString("--debug-load-catalog-assets-file");
	const char *anim_probe =
		sysArgGetString("--debug-probe-animation-source");
	const char *audio_play =
		sysArgGetString("--debug-play-catalog-audio");

	if (g_BootDebugLoadCatalogAssetsApplied) {
		return;
	}

	if ((!arg || !arg[0]) && (!file_arg || !file_arg[0])
			&& (!anim_probe || !anim_probe[0])
			&& (!audio_play || !audio_play[0])) {
		return;
	}

	g_BootDebugLoadCatalogAssetsArg = arg;
	g_BootDebugLoadCatalogAssetsFileArg = file_arg;
	g_BootDebugLoadCatalogAssetsSourceOnly =
		sysArgCheck("--debug-load-catalog-assets-source-only");
	g_BootDebugProbeAnimationSourceArg = anim_probe;
	g_BootDebugProbeAnimationSourceOnly =
		sysArgCheck("--debug-probe-animation-source-only")
		|| g_BootDebugLoadCatalogAssetsSourceOnly;
	g_BootDebugPlayCatalogAudioArg = audio_play;
	g_BootDebugPlayCatalogAudioSourceOnly =
		sysArgCheck("--debug-play-catalog-audio-source-only")
		|| g_BootDebugLoadCatalogAssetsSourceOnly;
	g_BootDebugLoadCatalogAssetsPending = 1;
}

static s32 bootDebugArgMentionsBaseCatalogId(const char *arg)
{
	return arg && strstr(arg, "base:") != NULL;
}

static s32 bootDebugCatalogProbesCanRunBeforeBaseEmit(void)
{
	if (!g_BootDebugLoadCatalogAssetsPending) {
		return 0;
	}
	if (!g_BootDebugLoadCatalogAssetsSourceOnly) {
		return 0;
	}
	if (g_BootDebugLoadCatalogAssetsFileArg
			&& g_BootDebugLoadCatalogAssetsFileArg[0]) {
		return 0;
	}
	if (bootDebugArgMentionsBaseCatalogId(g_BootDebugLoadCatalogAssetsArg)
			|| bootDebugArgMentionsBaseCatalogId(g_BootDebugProbeAnimationSourceArg)
			|| bootDebugArgMentionsBaseCatalogId(g_BootDebugPlayCatalogAudioArg)) {
		return 0;
	}
	return 1;
}

static s32 bootDebugCatalogProbesNeedCompletedBaseEmit(void)
{
	if (!g_BootDebugLoadCatalogAssetsPending) {
		return 0;
	}
	if (!g_BootDebugLoadCatalogAssetsSourceOnly
			&& !g_BootDebugProbeAnimationSourceOnly
			&& !g_BootDebugPlayCatalogAudioSourceOnly) {
		return 0;
	}
	return !bootDebugCatalogProbesCanRunBeforeBaseEmit();
}

static s32 bootDebugCatalogProbesNeedAnimationTable(void)
{
	if (!g_BootDebugLoadCatalogAssetsPending || g_Anims) {
		return 0;
	}
	if (g_BootDebugProbeAnimationSourceArg
			&& g_BootDebugProbeAnimationSourceArg[0]) {
		return 1;
	}
	if (g_BootDebugLoadCatalogAssetsArg
			&& strstr(g_BootDebugLoadCatalogAssetsArg, "animation=")) {
		return 1;
	}
	return 0;
}

static f32 bootAbsF(f32 value)
{
	return value < 0.0f ? -value : value;
}

static s32 bootCoordIsDefault(const struct coord *rot,
	const struct coord *translate, const struct coord *scale)
{
	const f32 eps = 0.0001f;
	return bootAbsF(rot->x) < eps && bootAbsF(rot->y) < eps
		&& bootAbsF(rot->z) < eps
		&& bootAbsF(translate->x) < eps
		&& bootAbsF(translate->y) < eps
		&& bootAbsF(translate->z) < eps
		&& bootAbsF(scale->x - 1.0f) < eps
		&& bootAbsF(scale->y - 1.0f) < eps
		&& bootAbsF(scale->z - 1.0f) < eps;
}

static s32 bootCoordChanged(const struct coord *rot0,
	const struct coord *translate0, const struct coord *scale0,
	const struct coord *rot1, const struct coord *translate1,
	const struct coord *scale1)
{
	const f32 eps = 0.0001f;
	return bootAbsF(rot0->x - rot1->x) > eps
		|| bootAbsF(rot0->y - rot1->y) > eps
		|| bootAbsF(rot0->z - rot1->z) > eps
		|| bootAbsF(translate0->x - translate1->x) > eps
		|| bootAbsF(translate0->y - translate1->y) > eps
		|| bootAbsF(translate0->z - translate1->z) > eps
		|| bootAbsF(scale0->x - scale1->x) > eps
		|| bootAbsF(scale0->y - scale1->y) > eps
		|| bootAbsF(scale0->z - scale1->z) > eps;
}

static void bootApplyDebugProbeAnimationSource(void)
{
	const char *asset_id = g_BootDebugProbeAnimationSourceArg;
	asset_entry_t *entry;
	const char *source_path;
	s32 animnum;
	s32 old_source_only;
	s32 loaded;
	u8 frame0;
	u8 frame1;
	s32 second_frame;
	s32 non_default = 0;
	s32 changed = 0;
	s32 samples = 0;
	u32 part_count = 0;
	enum anim_frame_layout_result layout_result;

	if (!asset_id || !asset_id[0]) {
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-probe-animation-source request id='%s' source_only=%d",
		asset_id, g_BootDebugProbeAnimationSourceOnly ? 1 : 0);

	entry = assetCatalogGetMutable(asset_id);
	if (!entry || entry->type != ASSET_ANIMATION) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-probe-animation-source result id='%s' result=MISSING",
			asset_id);
		return;
	}

	old_source_only = (s32)assetSourceDebugOnlyType();
	if (g_BootDebugProbeAnimationSourceOnly) {
		assetSourceDebugSetOnlyType(ASSET_ANIMATION);
	}

	loaded = catalogLoadTypedAsset(ASSET_ANIMATION, asset_id);
	animnum = entry->ext.anim.anim_id;
	if (animnum < 0 || animnum >= animGetTotalCount() || !g_Anims) { /* c3849: customs too */
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-probe-animation-source result id='%s' result=INVALID_ANIM anim=%d",
			asset_id, animnum);
		if (g_BootDebugProbeAnimationSourceOnly) {
			assetSourceDebugSetOnlyType((asset_type_e)old_source_only);
		}
		return;
	}

	source_path = catalogGetAnimOverride(animnum);

	if (!loaded) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-probe-animation-source result id='%s' anim=%d result=LOAD_FAIL source=%s",
			asset_id, animnum, source_path ? source_path : "(none)");
		if (g_BootDebugProbeAnimationSourceOnly) {
			assetSourceDebugSetOnlyType((asset_type_e)old_source_only);
		}
		return;
	}

	animLoadHeader((s16)animnum);
	frame0 = animLoadFrame((s16)animnum, 0);
	second_frame = g_Anims[animnum].numframes > 1 ? 1 : 0;
	frame1 = animLoadFrame((s16)animnum, second_frame);

	layout_result = animGetLoadedPartCount((s16)animnum, &part_count);
	if (layout_result != ANIM_FRAME_LAYOUT_OK
			|| frame0 >= ANIM_FRAME_CACHE_SIZE || frame1 >= ANIM_FRAME_CACHE_SIZE) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-probe-animation-source result id='%s' result=FAIL layout=%s frame0_slot=%u frame1_slot=%u",
			asset_id, animFrameLayoutResultString(layout_result), (unsigned)frame0, (unsigned)frame1);
		if (g_BootDebugProbeAnimationSourceOnly) {
			assetSourceDebugSetOnlyType((asset_type_e)old_source_only);
		}
		return;
	}

	for (s32 part = 0; part < (s32)part_count; part++) {
		struct coord rot0;
		struct coord translate0;
		struct coord scale0;
		struct coord rot1;
		struct coord translate1;
		struct coord scale1;

		animGetRotTranslateScale(part, false, NULL, (s16)animnum,
			frame0, &rot0, &translate0, &scale0);
		animGetRotTranslateScale(part, false, NULL, (s16)animnum,
			frame1, &rot1, &translate1, &scale1);
		samples++;
		if (!bootCoordIsDefault(&rot0, &translate0, &scale0)
				|| !bootCoordIsDefault(&rot1, &translate1, &scale1)) {
			non_default++;
		}
		if (bootCoordChanged(&rot0, &translate0, &scale0,
				&rot1, &translate1, &scale1)) {
			changed++;
		}
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-probe-animation-source result id='%s' anim=%d result=OK source=%s frames=%d bytes_per_frame=%d header_len=%d data=0x%x frame0_slot=%u frame1_slot=%u sampled_parts=%d non_default_parts=%d changed_parts=%d",
		asset_id, animnum, source_path ? source_path : "(none)",
		(s32)g_Anims[animnum].numframes,
		(s32)g_Anims[animnum].bytesperframe,
		(s32)g_Anims[animnum].headerlen,
		(unsigned)g_Anims[animnum].data,
		(unsigned)frame0, (unsigned)frame1,
		samples, non_default, changed);

	if (g_BootDebugProbeAnimationSourceOnly) {
		assetSourceDebugSetOnlyType((asset_type_e)old_source_only);
	}
	bootExitAfterCatalogProbesIfRequested();
}

s32 bootApplyDeferredDebugLoadCatalogAssets(void)
{
	if (g_BootDebugLoadCatalogAssetsApplied) {
		bootExitAfterCatalogProbesIfRequested();
		return 0;
	}
	if (!g_BootDebugLoadCatalogAssetsPending) {
		return 0;
	}

	if (bootDebugCatalogProbesNeedCompletedBaseEmit()
			&& !bootProgressIsComplete()) {
		return 0;
	}
	if (bootDebugCatalogProbesNeedAnimationTable()) {
		return 0;
	}
	if (g_BootDebugPlayCatalogAudioArg && !g_SndDisabled
			&& g_SeqInstances[0].seqp == NULL) {
		return 0;
	}

	g_BootDebugLoadCatalogAssetsPending = 0;
	bootEnsureUiArchivesReadyForCliSourceLoads();
	bootApplyDebugLoadCatalogAssets(g_BootDebugLoadCatalogAssetsArg,
		g_BootDebugLoadCatalogAssetsSourceOnly);
	bootApplyDebugLoadCatalogAssetsFile(g_BootDebugLoadCatalogAssetsFileArg,
		g_BootDebugLoadCatalogAssetsSourceOnly);
	bootApplyDebugProbeAnimationSource();
	bootApplyDebugPlayCatalogAudio(g_BootDebugPlayCatalogAudioArg,
		g_BootDebugPlayCatalogAudioSourceOnly);
	g_BootDebugLoadCatalogAssetsApplied = 1;
	bootExitAfterCatalogProbesIfRequested();
	return 1;
}

/* Dispatcher called once from main() after the catalog is fully
 * initialised. */
static void bootGridPrepareCreatorReviewSource(void);

static void bootApplyCliFastPaths(void)
{
	const char *player_init_fault =
		sysArgGetString("--debug-fail-stage-player-init");

	/* First, so every fast path below (spawn weapon, room launch, catalog
	 * probes) already sees the mod a recipe creates. */
	bootApplyModAuthorRecipe(sysArgGetString("--mod-author"));
	if (smokeHarnessIsActive() && sysArgCheck("--debug-grid-variant-placement-fallbacks")
			&& sysArgCheck("--debug-grid-variant-fallback-save")) {
		const char *recipe = sysArgGetString("--debug-grid-variant-fallback-vehicle-recipe");
		if (!recipe || !modAuthorRuntimeRunRecipe(recipe)) {
			smokeHarnessExit(1, "grid_variant_fallback_vehicle_recipe_failed");
			return;
		}
	}
	bootApplyDebugGridShareWeapon();
	bootApplyDebugWeaponToolCreate();
	bootApplyDebugCharacterToolCreate();
	bootApplyDebugLevelToolNew();
	bootApplyDebugForgeLevel();
	bootGridPrepareCreatorReviewSource();

	if (player_init_fault != NULL) {
		const player_stage_init_fault_status_t status =
			playerStageInitFaultConfigure(player_init_fault,
				smokeHarnessIsActive() != 0, MAX_PLAYERS);
		sysLogPrintf(status == PLAYER_STAGE_INIT_FAULT_OK
				? LOG_NOTE : LOG_WARNING,
			"BOOT: --debug-fail-stage-player-init spec='%s' status=%s smoke=%d",
			player_init_fault, playerStageInitFaultStatusString(status),
			smokeHarnessIsActive() != 0);
	}
	if (sysArgCheck("--debug-ready-gate-settings-change")) {
		if (!smokeHarnessIsActive()) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-ready-gate-settings-change is smoke-only; request ignored");
		} else {
			g_BootReadyGateSettingsChangeArmed = 1;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-ready-gate-settings-change armed smoke=1 one_shot=1");
		}
	}
	if (sysArgCheck("--debug-ready-gate-settings-repeat")) {
		if (!smokeHarnessIsActive()) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-ready-gate-settings-repeat is smoke-only; request ignored");
		} else {
			g_BootReadyGateSettingsChangeArmed = 2;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-ready-gate-settings-repeat armed smoke=1 one_shot=1");
		}
	}
	if (sysArgCheck("--debug-generated-mesh-render-audit")) {
		modAssetCompilerSetGeneratedModeldefRenderAudit(1);
	}
	bootApplyDebugInstallReceivedMod();
	bootApplyDebugRejectCatalogIngressList();
	bootApplyDebugReceivePdcaList();
	if (sysArgCheck("--debug-asset-source-scene") && smokeHarnessIsActive()) {
		s32 result = assetSourceSceneHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_source_scene_failure");
	}
	if (sysArgCheck("--debug-asset-source-audio") && smokeHarnessIsActive()) {
		s32 result = assetSourceAudioHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_source_audio_failure");
	}
	if (sysArgCheck("--debug-asset-source-character") && smokeHarnessIsActive()) {
		s32 result = assetSourceCharacterHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_source_character_failure");
	}
	if (sysArgCheck("--debug-asset-source-numeric") && smokeHarnessIsActive()) {
		s32 result = assetSourceNumericHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_source_numeric_failure");
	}
	if (sysArgCheck("--debug-asset-catalog-relocation") && smokeHarnessIsActive()) {
		s32 result = assetCatalogRelocationHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_catalog_relocation_failure");
	}
	if (sysArgCheck("--debug-asset-source-body-head") && smokeHarnessIsActive()) {
		s32 result = bodyHeadSourceHarnessRun();
		smokeHarnessExit(result == 0 ? 0 : 2,
			result == 0 ? "scripted_exit" : "asset_source_body_head_failure");
	}
	if (sysArgCheck("--debug-v006-save-failclosed")) {
		s32 result = v006SaveFailclosedRun();
		if (smokeHarnessIsActive()) {
			smokeHarnessExit(result == 0 ? 0 : 2,
				result == 0 ? "scripted_exit" : "v006_save_failure");
		}
	}
	bootApplyMainMenu();
	bootApplyLaunchModdingHub();
	bootApplyLaunchCredits();
	bootApplyLaunchScenario();
	bootApplyLaunchMission();
	bootApplyLaunchMpRoom();
	bootApplyLaunchChallenge();
	bootApplyDebugMpOptions();
	if (sysArgCheck("--debug-v006-scenario-transaction")) {
		s32 result = v006ScenarioTransactionRun();
		if (result != 0) {
			smokeHarnessExit(2, "v006_scenario_failure");
		}
	}
	bootApplyDebugMountBike();
	if (sysArgGetString("--debug-swap-vehicle")) {
		vehicleDebugSetSwap(sysArgGetString("--debug-swap-vehicle"));
		sysLogPrintf(LOG_NOTE, "BOOT: --debug-swap-vehicle %s armed (replaces each hoverbike a stage creates)",
			vehicleDebugSwapId());
	}
	if (sysArgGetString("--debug-mount-seat")) {
		const char *seat = sysArgGetString("--debug-mount-seat");
		if (strcmp(seat, "driver") == 0) {
			hoverbikeDebugSetMountSeat(VEHICLE_DEBUG_MOUNT_SEAT_DRIVER);
		} else if (strcmp(seat, "gunner") == 0) {
			hoverbikeDebugSetMountSeat(VEHICLE_DEBUG_MOUNT_SEAT_GUNNER);
		} else {
			sysLogPrintf(LOG_ERROR,
				"BOOT: --debug-mount-seat '%s' is not driver or gunner; the player's place and view choose the seat",
				seat);
			seat = NULL;
		}
		if (seat) {
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-mount-seat %s armed (every mount of a two-seat vehicle asks for that seat)",
				seat);
		}
	}
	bootApplyDebugSpawnAt(sysArgGetString("--debug-spawn-at"));
	bootApplyDebugSpawnAtCompanions(sysArgCheck("--debug-spawn-at-after-intro") ? 1 : 0,
		sysArgGetString("--debug-spawn-at-look-at"));
	bootApplyDebugForceFirstPerson();
	bootApplyDebugPlaceBotNearPlayer();
	bootApplyLaunchLoadAgent(sysArgGetString("--launch-load-agent"));
	bootApplyListenBind(sysArgGetString("--listen-bind"));
	bootApplyConnectHost(sysArgGetString("--connect-host"));
	bootApplyDumpSwarmState(sysArgGetString("--dump-swarm-state"));
	bootArmDebugLoadCatalogAssets();
	(void)bootApplyDeferredDebugLoadCatalogAssets();
}

/* Called once per frame from pdmain.c's mainTick when the
 * --launch-scenario one-shot is armed. Dispatches testScenarioLaunch
 * once the stage setup has placed the player prop, so the scenario's
 * matchStart sees a positioned player and the first respawn succeeds
 * (instead of failing into endscreen_solo). Returns 1 when the
 * scenario was attempted, else 0.
 *
 * Gates:
 *   - g_BootLaunchScenarioPending must be set
 *   - g_Vars.lvframenum >= 4 (same gate --debug-mount-bike uses; load
 *     black frames are out of the way and the player prop has been
 *     spawned + positioned by setupCreateProps).
 *
 * One-shot: clears the latch on either success or failure so a
 * subsequent stage change does not re-fire the scenario. */
s32 bootLaunchScenarioTick(void)
{
	if (!g_BootLaunchScenarioPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	test_scenario_t scen = (test_scenario_t)g_BootLaunchScenarioId;
	const char *map_override = (g_BootDebugSwarmMap && g_BootDebugSwarmMap[0])
		? g_BootDebugSwarmMap : NULL;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-scenario consuming latch: testScenarioLaunch(%d) at lvframenum=%d map_override=%s",
		(s32)scen, (s32)g_Vars.lvframenum,
		map_override ? map_override : "(default)");
	if (!testScenarioLaunch(scen, map_override)) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-scenario deferred dispatch failed; falling back to default boot stage");
	}
	g_BootLaunchScenarioPending = 0;
	return 1;
}

/* The yaw and pitch the session debug flag turns the editor camera to before
 * it measures. The yaw is far from every axis on purpose: at 0, 90, 180 and
 * 270 degrees sin or cos is zero and a wrong sign in the facing math cannot
 * be seen. The pitch is not level, so that a pitch that does not reach the
 * view shows too. */
#define BOOT_FORGE_PROBE_YAW_DEG 217.0f
#define BOOT_FORGE_PROBE_PITCH_DEG -20.0f

/* Where the editor's world markers put five known points, logged so a run
 * can check the projection, and that the editor camera's angles are the
 * view's, without anyone looking at a picture.
 *
 * The camera's yaw is a turn angle t, and a turn angle t faces (-sin t, 0,
 * cos t), the game's reading of a player's turn angle (bondmove.c,
 * bmoveUpdateVerta). Pitch p tips that toward +y: look = (-sin t cos p,
 * sin p, cos t cos p), up = (sin t sin p, cos p, -cos t sin p), and the
 * view's right is (-cos t, 0, -sin t). These are the vectors the editor's
 * view is drawn with (forgemode.c, forgeCameraOverride). So a point along the
 * look must be at the middle of the picture (0.5, 0.5), one beside it along
 * the right must be right of the middle, one along the up must be above it,
 * and one behind must be out of view.
 *
 * The first run of this probe (2026-11-17, yaw 217, level) put the point
 * ahead at x = -0.146: the view was looking along +X whatever the editor's
 * yaw said (B-1240). The fifth point is ahead by the mirrored reading of the
 * angle, (+sin t, ., cos t), which the editor's fly and placement code used
 * until then (B-1238); it is logged, not judged.
 *
 * Fractions of the renderer's native size, the units forgeProjectWorldPos
 * answers in and the Forge HUD scales to the window. The player's own turn
 * angle and pitch are logged beside the editor's: they are what the game's
 * player code holds that frame, for diagnosis. */
static void bootForgeMarkerProbe(void)
{
	struct coord cam;
	const f32 native_w = (f32)videoGetNativeWidth();
	const f32 native_h = (f32)videoGetNativeHeight();
	const f32 reach = 1000.0f;
	const f32 aside = 300.0f;
	const f32 yaw = forgeGetCameraYawDeg() * 0.017453292f;
	const f32 pitch = forgeGetCameraPitchDeg() * 0.017453292f;
	const f32 look[3] = { -sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
	const f32 up[3] = { sinf(yaw) * sinf(pitch), cosf(pitch), -cosf(yaw) * sinf(pitch) };
	const f32 right[3] = { -cosf(yaw), 0.0f, -sinf(yaw) };
	const f32 mirrored[3] = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
	f32 point[3];
	char text[5][48];

	cam.x = 0.0f;
	cam.y = 0.0f;
	cam.z = 0.0f;
	forgeGetCameraPos(&cam);

	for (s32 i = 0; i < 5; i++) {
		const f32 *along = (i == 4) ? mirrored : look;
		const f32 distance = (i == 3) ? -reach : reach;
		const f32 *beside = (i == 1) ? right : ((i == 2) ? up : NULL);
		f32 x = 0.0f;
		f32 y = 0.0f;

		point[0] = cam.x + along[0] * distance + (beside ? beside[0] * aside : 0.0f);
		point[1] = cam.y + along[1] * distance + (beside ? beside[1] * aside : 0.0f);
		point[2] = cam.z + along[2] * distance + (beside ? beside[2] * aside : 0.0f);
		if (native_w > 0.0f && native_h > 0.0f && forgeProjectWorldPos(point, &x, &y, NULL)) {
			snprintf(text[i], sizeof(text[i]), "(%.3f, %.3f)", x / native_w, y / native_h);
		} else {
			snprintf(text[i], sizeof(text[i]), "hidden");
		}
	}
	sysLogPrintf(LOG_NOTE,
		"FORGE.MARKERS.PROBE: ahead=%s right=%s up=%s behind=%s other_reading=%s native=%dx%d camera=(%.0f, %.0f, %.0f) yaw=%.1f pitch=%.1f player_theta=%.1f player_verta=%.1f",
		text[0], text[1], text[2], text[3], text[4], (s32)native_w, (s32)native_h, cam.x, cam.y, cam.z,
		forgeGetCameraYawDeg(), forgeGetCameraPitchDeg(),
		g_Vars.currentplayer ? g_Vars.currentplayer->vv_theta : 0.0f,
		g_Vars.currentplayer ? g_Vars.currentplayer->vv_verta : 0.0f);
}

/* Inspect the resident padfile after the normal loader has resolved pad rooms
 * and fixed up the group's pointers. The new pad has exactly one first hop. */
static s32 bootForgeConnectPadsNative(const char *phase, const f32 *position)
{
	const s32 expected = position ? 9 : 8;
	const struct waypoint *points = g_StageSetup.waypoints;
	const struct waygroup *groups = g_StageSetup.waygroups;
	if (!g_PadsFile || !g_StageSetup.padfiledata || g_PadsFile->numpads != expected
			|| !points || !groups || !groups[0].neighbours || !groups[0].waypoints
			|| groups[1].neighbours) return 0;

	u32 member_mask = 0, pad_mask = 0;
	for (s32 i = 0; i < expected; ++i) {
		const s32 member = groups[0].waypoints[i];
		const s32 pad = points[i].padnum;
		if (member < 0 || member >= expected || (member_mask & (1u << member))
				|| pad < 0 || pad >= expected || (pad_mask & (1u << pad))
				|| points[i].groupnum != 0 || !points[i].neighbours) return 0;
		member_mask |= 1u << member;
		pad_mask |= 1u << pad;
	}
	if (points[expected].padnum >= 0 || groups[0].waypoints[expected] != -1) return 0;

	s32 refs = 0, first_hop = -1, reciprocal = 0;
	for (s32 i = 0; i < expected; ++i) {
		const s32 pad = points[i].padnum;
		u32 neighbours = 0;
		s32 degree = 0;
		for (; degree < expected; ++degree) {
			const s32 next = points[i].neighbours[degree];
			if (next < 0) break;
			if (next >= expected || next == i || (neighbours & (1u << next))) return 0;
			neighbours |= 1u << next;
			if (pad == 8) first_hop = points[next].padnum;
			if (pad == 0 && points[next].padnum == 8) reciprocal = 1;
		}
		if (pad == 8 && degree != 1) return 0;
		refs += degree;
	}
	if (refs != expected * 2) return 0;
	if (position) {
		struct pad anchor = {0}, connected = {0};
		padUnpack(0, PADFIELD_ROOM, &anchor);
		padUnpack(8, PADFIELD_POS | PADFIELD_ROOM, &connected);
		if (!reciprocal || first_hop != 0 || anchor.room != 1 || connected.room != anchor.room) return 0;
		for (s32 axis = 0; axis < 3; ++axis) {
			if (!(fabsf(connected.pos.f[axis] - position[axis]) <= 0.01f)) return 0;
		}
	}
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.ROUTE: phase=%s pads=%d waypoints=%d waygroups=1 edges=%d first_hop=%d",
		phase, expected, expected, refs / 2, first_hop);
	return 1;
}

static s32 bootForgeConnectPadsTick(s32 *step)
{
	static f32 position[3];
	char err[256] = "";
	if (*step == 2 && forgeIsFreefly() && g_Vars.lvframenum >= 120) {
		if (!forgeLevelSessionDoc() && !forgeLevelSessionOpen(forgeLevelSessionArena(), err, sizeof(err))) goto fail;
		forge_level_doc_t *doc = forgeLevelSessionDoc();
		forge_level_pad_t anchor;
		if (!forgeLevelSessionCanConnectPads() || !bootForgeConnectPadsNative("original", NULL)
				|| forgeLevelDocPadCount(doc) != 8 || forgeLevelDocSpawnCount(doc) != 8
				|| !forgeLevelDocFindPad(doc, "pad_0000", &anchor)) {
			snprintf(err, sizeof(err), "the original arena's native seed route is unavailable");
			goto fail;
		}
		/* Stay inside room 1, offset from its doorway-side routed pad. */
		position[0] = anchor.position[0] - 120.0f;
		position[1] = anchor.position[1];
		position[2] = anchor.position[2] + 60.0f;
		char spawn_id[32], path_id[32];
		forge_level_spawn_t spawn;
		forgeLevelSessionEditBegin();
		const s32 added = forgeLevelDocAddSpawn(doc, position, anchor.look, NULL,
			spawn_id, sizeof(spawn_id), err, sizeof(err));
		forgeLevelSessionEditEnd(added);
		if (!added || !forgeLevelDocGetSpawn(doc, 8, &spawn) || strcmp(spawn.spawn_id, spawn_id) != 0
				|| strcmp(spawn.pad_ref, "pad_0008") != 0
				|| !forgeLevelSessionConnectPads(spawn.pad_ref, anchor.pad_ref, path_id, sizeof(path_id), err, sizeof(err))
				|| strcmp(path_id, "path_0001") != 0) goto fail;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.ROUTE: connected spawn=%s pad=%s target=%s path=%s position=(%.2f,%.2f,%.2f)",
			spawn_id, spawn.pad_ref, anchor.pad_ref, path_id, position[0], position[1], position[2]);
		if (!forgeLevelSessionPlayDraft(err, sizeof(err))) goto fail;
		*step = 6;
		return 1;
	}
	if (*step == 6 && forgeLevelDraftIsPlaying() && g_Vars.lvframenum >= 120) {
		if (!bootForgeConnectPadsNative("draft", position) || !forgeLevelDraftReturn()) {
			snprintf(err, sizeof(err), "the native draft route did not load or could not return");
			goto fail;
		}
		*step = 7;
		return 1;
	}
	if (*step == 7 && forgeIsFreefly() && g_Vars.lvframenum >= 60) {
		if (!bootForgeConnectPadsNative("returned", NULL) || !forgeLevelSessionHasUnsavedChanges()
				|| forgeLevelSessionUndoCount() != 2 || !forgeLevelSessionUndo()
				|| forgeLevelDocMemberChanged(forgeLevelSessionDoc(), FORGE_LEVEL_MEMBER_PATHS)
				|| forgeLevelDocPadCount(forgeLevelSessionDoc()) != 9 || !forgeLevelSessionRedo()
				|| !forgeLevelDocMemberChanged(forgeLevelSessionDoc(), FORGE_LEVEL_MEMBER_PATHS)) {
			snprintf(err, sizeof(err), "the original route or draft history did not survive return");
			goto fail;
		}
		*step = 9;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.ROUTE: returned with linked draft history=retained");
		smokeHarnessExit(0, "grid_connect_pads_complete");
		return 1;
	}
	return 0;

fail:
	*step = 9;
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.ROUTE.FAIL: %s", err[0] ? err : "the route edit had unexpected output");
	smokeHarnessExit(1, "grid_connect_pads_failed");
	return 1;
}

typedef struct boot_forge_pickup_probe {
	s32 stage;
	s32 command;
	s32 second_command;
	s32 chained;
	struct coord trigger_center;
	struct prop *player;
	f32 pickup_x;
	s32 entered_frame;
	u32 step_started;
	u32 match_options;
	u32 mp_options;
	s32 loadout_overridden;
	char archive_sha256[SHA256_HEX_SIZE];
	char volume[32];
} boot_forge_pickup_probe_t;

static const f32 kBootForgePickupHalfSize[] = {120.0f, 220.0f, 120.0f};

static s32 bootForgePickupInside(const boot_forge_pickup_probe_t *probe, const struct prop *prop)
{
	for (s32 axis = 0; axis < 3; ++axis) {
		if (!(fabsf(prop->pos.f[axis] - probe->trigger_center.f[axis]) <= kBootForgePickupHalfSize[axis])) return 0;
	}
	return 1;
}

static s32 bootForgePickupInRoom(const struct coord *position, RoomNum room)
{
	struct coord point = *position;
	RoomNum inside[24], above[22];
	bgFindRoomsByPos(&point, inside, above, 20, NULL);
	for (s32 i = 0; i < 20 && inside[i] != -1; ++i) {
		if (inside[i] == room) return 1;
	}
	return 0;
}

static void bootForgePickupRestoreLoadout(boot_forge_pickup_probe_t *probe)
{
	if (!probe->loadout_overridden) return;
	g_MatchConfig.options = probe->match_options;
	g_MpSetup.options = probe->mp_options;
	probe->loadout_overridden = 0;
}

static s32 bootForgePickupAuthor(boot_forge_pickup_probe_t *probe, const char *arena_id,
	char *err, size_t err_cap)
{
	if (!forgeLevelSessionDoc() && !forgeLevelSessionOpen(arena_id, err, err_cap)) return 0;
	const forge_level_source_t *source = forgeLevelSessionSource();
	const asset_entry_t *arena = assetCatalogResolve(arena_id);
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	forge_level_spawn_t spawn;
	forge_level_pad_t trigger_pad, pickup_pad;
	if (g_NetMode != NETMODE_NONE || !source || strcmp(source->arena_id, arena_id) != 0
			|| strlen(source->archive_sha256) != SHA256_HEX_SIZE - 1
			|| strcmp(forgeLevelCloneProvenance(arena_id, source->archive_path), GRID_ARENA_PROVENANCE_ORIGINAL) != 0
			|| !arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum
			|| !forgeLevelSessionCanPlayDraft() || forgeLevelDocObjectCount(doc) != 0
			|| forgeLevelDocSpawnCount(doc) != 8 || forgeLevelDocPadCount(doc) != 8
			|| !forgeLevelDocGetSpawn(doc, 0, &spawn) || strcmp(spawn.pad_ref, "pad_0000") != 0
			|| !forgeLevelDocFindPad(doc, "pad_0001", &trigger_pad) || strcmp(trigger_pad.room_ref, "room_1") != 0
			|| !forgeLevelDocFindPad(doc, "pad_0006", &pickup_pad) || strcmp(pickup_pad.room_ref, "room_2") != 0) {
		snprintf(err, err_cap, "the exact original two-room source is unavailable");
		return 0;
	}
	probe->stage = g_StageNum;
	snprintf(probe->archive_sha256, sizeof(probe->archive_sha256), "%s", source->archive_sha256);
	memcpy(probe->trigger_center.f, trigger_pad.position, sizeof(probe->trigger_center.f));
	char record[32];
	forge_level_object_t pickup;
	forge_level_trigger_t trigger;
	forgeLevelSessionEditBegin();
	s32 ok = 1;
	for (s32 i = 7; i > 0 && ok; --i) {
		ok = forgeLevelDocGetSpawn(doc, i, &spawn)
			&& forgeLevelDocRemoveSpawn(doc, spawn.spawn_id, err, err_cap);
	}
	ok = ok && forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, pickup_pad.position, pickup_pad.look,
			record, sizeof(record), err, err_cap)
		&& forgeLevelSessionGivePickup(record, "base:dragon", err, err_cap)
		&& forgeLevelDocAddTrigger(doc, FORGE_LEVEL_TRIGGER_GIVE_PICKUP, trigger_pad.pad_ref, record,
			kBootForgePickupHalfSize, err, err_cap)
		&& forgeLevelDocGetObject(doc, 0, &pickup) && strcmp(pickup.record_id, record) == 0
		&& pickup.order == 0 && strcmp(pickup.pad_ref, "pad_0008") == 0
		&& forgeLevelDocGetTrigger(doc, 0, &trigger) && strcmp(trigger.target_id, record) == 0;
	forgeLevelSessionEditEnd(ok);
	if (!ok) return 0;
	probe->command = pickup.order;
	snprintf(probe->volume, sizeof(probe->volume), "%s", trigger.volume_id);
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.PICKUP: authored arena=%s provenance=original archive_sha256=%s spawn=pad_0000 trigger=%s pad=%s fixed=%s weapon=base:dragon pickup_pad=%s",
		arena_id, probe->archive_sha256, probe->volume, trigger.pad_ref, record, pickup.pad_ref);
	return 1;
}

static s32 bootForgeAuthorChainedLogic(boot_forge_pickup_probe_t *probe, char *err, size_t err_cap)
{
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	forge_level_pad_t pad;
	forge_level_object_t other;
	char record[32];
	forgeLevelSessionEditBegin();
	s32 ok = forgeLevelDocFindPad(doc, "pad_0006", &pad)
		&& forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, pad.position, pad.look,
			record, sizeof(record), err, err_cap)
		&& strcmp(record, "setup_0001") == 0
		&& forgeLevelSessionGivePickup(record, "base:cyclone", err, err_cap)
		&& forgeLevelDocGetObject(doc, 1, &other) && strcmp(other.record_id, record) == 0;
	if (ok) {
		size_t length = 0;
		const char *graph = forgeLevelDocMemberText(doc, FORGE_LEVEL_MEMBER_LEVEL_GRAPH, &length);
		const char *nodes = graph ? strstr(graph, "\"nodes\"") : NULL;
		const char *edges = graph ? strstr(graph, "\"edges\"") : NULL;
		nodes = nodes ? strchr(nodes, '[') : NULL;
		edges = edges ? strchr(edges, '[') : NULL;
		const char new_nodes[] =
			"{\"id\":\"grid.logic.qualified\",\"kind\":\"gate.player.has_item\",\"item\":\"base:dragon\"},"
			"{\"id\":\"grid.logic.second\",\"kind\":\"og.pickup.give\",\"object\":\"setup_0001\"},";
		const char new_edges[] =
			"{\"from\":\"grid.trigger.volume_pad_9999.give\",\"to\":\"grid.logic.qualified\",\"input\":\"exec\"},"
			"{\"from\":\"grid.logic.qualified\",\"to\":\"grid.logic.second\",\"input\":\"exec\",\"output\":\"pass\"},";
		ok = nodes && edges && nodes < edges && length <= INT_MAX;
		if (ok) {
			++nodes;
			++edges;
			const size_t capacity = length + sizeof(new_nodes) + sizeof(new_edges);
			char *candidate = malloc(capacity);
			if (candidate) {
				const s32 written = snprintf(candidate, capacity, "%.*s%s%.*s%s%s",
					(int)(nodes - graph), graph, new_nodes, (int)(edges - nodes), nodes, new_edges, edges);
				ok = written > 0 && (size_t)written < capacity
					&& forgeLevelDocReplaceLogic(doc, candidate, (size_t)written, err, err_cap);
				free(candidate);
			} else {
				ok = 0;
			}
		}
	}
	forgeLevelSessionEditEnd(ok);
	if (!ok) {
		if (!err[0]) snprintf(err, err_cap, "the native logic chain could not be authored");
		return 0;
	}
	probe->second_command = other.order;
	probe->chained = 1;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LOGIC: authored gate=base:dragon second=%s weapon=base:cyclone undo=%d",
		record, forgeLevelSessionUndoCount());
	return 1;
}

static s32 bootForgePickupTriggerTick(const char *arena_id, s32 *step)
{
	static boot_forge_pickup_probe_t probe;
	char err[256] = "";
	const u32 now = SDL_GetTicks();
	if (*step == 2 && forgeIsFreefly() && g_Vars.lvframenum >= 120) {
		if (!bootForgePickupAuthor(&probe, arena_id, err, sizeof(err))) goto fail;
		if (sysArgCheck("--debug-forge-chained-level-logic")
				&& !bootForgeAuthorChainedLogic(&probe, err, sizeof(err))) goto fail;
		probe.match_options = g_MatchConfig.options;
		probe.mp_options = g_MpSetup.options;
		probe.loadout_overridden = 1;
		g_MatchConfig.options &= ~MPOPTION_SPAWNWITHWEAPON;
		g_MpSetup.options &= ~MPOPTION_SPAWNWITHWEAPON;
		if (!forgeLevelSessionPlayDraft(err, sizeof(err))) goto fail;
		probe.step_started = now;
		*step = 6;
		return 1;
	}
	if (*step == 6 && forgeLevelDraftIsPlaying() && g_Vars.lvframenum >= 120) {
		const struct player *player = g_Vars.players[0];
		struct prop *actor = player ? player->prop : NULL;
		const struct defaultobj *object = setupGetObjByCmdIndex(probe.command);
		if (g_NetMode != NETMODE_NONE || g_StageNum != probe.stage || PLAYERCOUNT() != 1
				|| !actor || !actor->chr || player != g_Vars.currentplayer || player->isdead
				|| actor->rooms[0] != 1 || bootForgePickupInside(&probe, actor)
				|| !bootForgePickupInRoom(&actor->pos, 1) || (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)
				|| invHasSingleWeaponExcAllGuns(WEAPON_DRAGON)
				|| !object || object->type != OBJTYPE_WEAPON || ((const struct weaponobj *)object)->weaponnum != WEAPON_DRAGON
				|| !object->prop || object->prop->rooms[0] != 2 || object->pad != 8
				|| !bootForgePickupInRoom(&object->prop->pos, 2)
				|| !(fabsf(actor->pos.x - object->prop->pos.x) > 1000.0f)
				|| !(fabsf(probe.trigger_center.x - object->prop->pos.x) > 1000.0f)) {
			snprintf(err, sizeof(err), "the pickup, outside player or empty pre-entry inventory did not load");
			goto fail;
		}
		if (probe.chained) {
			const struct defaultobj *other = setupGetObjByCmdIndex(probe.second_command);
			if (invHasSingleWeaponExcAllGuns(WEAPON_CYCLONE) || !other || other->type != OBJTYPE_WEAPON
					|| ((const struct weaponobj *)other)->weaponnum != WEAPON_CYCLONE
					|| !other->prop || !bootForgePickupInRoom(&other->prop->pos, 2)) {
				snprintf(err, sizeof(err), "the second physical pickup or empty inventory did not load");
				goto fail;
			}
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LOGIC: second pickup room=2 has_cyclone=0");
		}
		probe.player = actor;
		probe.pickup_x = object->prop->pos.x;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PICKUP: before player=0 player_room=1 pickup_room=2 has_dragon=0 separation_x=%.1f",
			fabsf(actor->pos.x - probe.pickup_x));
		struct coord enter = probe.trigger_center;
		enter.y = actor->pos.y;
		RoomNum rooms[] = {1, -1};
		if (!chrMoveToPos(actor->chr, &enter, rooms, 0.0f, true) || actor->rooms[0] != 1
				|| !bootForgePickupInside(&probe, actor) || !bootForgePickupInRoom(&actor->pos, 1)) {
			snprintf(err, sizeof(err), "the player could not be positioned inside the actual room-1 trigger");
			goto fail;
		}
		probe.entered_frame = g_Vars.lvframenum;
		probe.step_started = now;
		*step = 8;
		return 1;
	}
	if (*step == 8) {
		const struct player *player = g_Vars.players[0];
		if (g_StageNum != probe.stage || !forgeLevelDraftIsPlaying() || !player || player->prop != probe.player
				|| player != g_Vars.currentplayer || player->isdead || probe.player->rooms[0] != 1
				|| !bootForgePickupInside(&probe, probe.player) || !bootForgePickupInRoom(&probe.player->pos, 1)) {
			snprintf(err, sizeof(err), "the exact entering player or stage changed before volume dispatch");
			goto fail;
		}
		if (g_Vars.lvframenum > probe.entered_frame && invHasSingleWeaponExcAllGuns(WEAPON_DRAGON)
				&& (!probe.chained || invHasSingleWeaponExcAllGuns(WEAPON_CYCLONE))) {
			if (probe.chained) sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LOGIC: chained grants player=0 dragon=1 cyclone=1");
			sysLogPrintf(LOG_NOTE,
				"SMOKE.GRID.PICKUP: after player=0 player_room=1 pickup_room=2 has_dragon=1 separation_x=%.1f volume=%s",
				fabsf(probe.player->pos.x - probe.pickup_x), probe.volume);
			bootForgePickupRestoreLoadout(&probe);
			if (!forgeLevelDraftReturn()) {
				snprintf(err, sizeof(err), "the successful playtest did not request normal return");
				goto fail;
			}
			probe.step_started = now;
			*step = 7;
			return 1;
		}
		if ((u32)(now - probe.step_started) >= 3000) {
			snprintf(err, sizeof(err), "ordinary volume entry did not grant the native weapon within three seconds");
			goto fail;
		}
	}
	if (*step == 7 && forgeIsFreefly() && g_Vars.lvframenum >= 60) {
		const forge_level_source_t *source = forgeLevelSessionSource();
		forge_level_source_t restored = {0};
		forge_level_doc_t *original = forgeLevelOpen(arena_id, &restored, err, sizeof(err));
		const s32 restored_original = original && forgeLevelDocSpawnCount(original) == 8
			&& forgeLevelDocObjectCount(original) == 0 && forgeLevelDocTriggerCount(original) == 0
			&& strcmp(probe.archive_sha256, restored.archive_sha256) == 0;
		forgeLevelDocFree(original);
		forge_level_doc_t *retained = forgeLevelSessionDoc();
		const char *logic = forgeLevelDocMemberText(retained, FORGE_LEVEL_MEMBER_LEVEL_GRAPH, NULL);
		const s32 retained_edit = probe.chained ? logic && strstr(logic, "\"grid.logic.second\"") != NULL
			: forgeLevelDocTriggerCount(retained) == 1;
		if (!source || strcmp(source->arena_id, arena_id) != 0 || !restored_original || g_StageNum != probe.stage
				|| forgeLevelDraftIsPlaying() || setupGetObjByCmdIndex(probe.command)
				|| !forgeLevelSessionHasUnsavedChanges() || forgeLevelSessionUndoCount() != 1 + probe.chained
				|| !retained_edit) {
			if (!err[0]) snprintf(err, sizeof(err), "return did not restore the original native source and retain the trigger draft");
			goto fail;
		}
		*step = 9;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PICKUP: returned arena=%s archive_sha256=%s %s=retained",
			arena_id, probe.archive_sha256, probe.chained ? "draft_logic" : "draft_trigger");
		smokeHarnessExit(0, probe.chained ? "grid_level_logic_complete" : "grid_fixed_pickup_trigger_complete");
		return 1;
	}
	if ((*step == 6 || *step == 7) && (u32)(now - probe.step_started) >= 90000) {
		snprintf(err, sizeof(err), "the native draft or return failed to load within ninety seconds");
		goto fail;
	}
	return 0;

fail:
	*step = 9;
	bootForgePickupRestoreLoadout(&probe);
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.PICKUP.FAIL: %s", err[0] ? err : "unexpected typed authoring output");
	smokeHarnessExit(1, "grid_fixed_pickup_trigger_failed");
	return 1;
}

static forge_level_doc_t *bootForgeDraftWithUnknownAction(forge_level_doc_t *doc, char *err, size_t err_cap)
{
	forge_level_overrides_t *members = calloc(1, sizeof(*members));
	if (!members) return NULL;
	forge_level_texts_t texts = {0};
	forge_level_doc_t *invalid = NULL;
	char *graph = NULL;
	if (!forgeLevelDocOverridesAll(doc, NULL, members, err, err_cap)) goto done;
	for (s32 i = 0; i < FORGE_LEVEL_MEMBER_COUNT; ++i) {
		for (u32 j = 0; j < members->count; ++j) {
			const asset_archive_clone_override_t *entry = &members->entries[j];
			if (strcmp(entry->entry, forgeLevelDocMemberName(i)) != 0) continue;
			texts.text[i] = entry->data;
			texts.size[i] = entry->len;
			break;
		}
	}
	const size_t size = texts.size[FORGE_LEVEL_MEMBER_LEVEL_GRAPH];
	if (!size || size > FORGE_LEVEL_CHECKPOINT_MAX_BYTES) goto done;
	graph = malloc(size + 1);
	if (!graph) goto done;
	memcpy(graph, texts.text[FORGE_LEVEL_MEMBER_LEVEL_GRAPH], size);
	graph[size] = '\0';
	char *action = strstr(graph, "\"og.pickup.give\"");
	if (!action) goto done;
	memcpy(action, "\"og.pickup.fake\"", sizeof("\"og.pickup.fake\"") - 1);
	texts.text[FORGE_LEVEL_MEMBER_LEVEL_GRAPH] = graph;
	invalid = forgeLevelDocRead(&texts, err, err_cap);

done:
	free(graph);
	free(members);
	return invalid;
}

static s32 bootForgeValidateDraftTick(const char *arena_id, s32 *step)
{
	if (*step != 2 || !forgeIsFreefly() || g_Vars.lvframenum < 120) return 0;
	boot_forge_pickup_probe_t source_proof = {0};
	char err[256] = "", sha[SHA256_HEX_SIZE];
	forge_level_doc_t *invalid = NULL;
	forge_level_doc_t *original = NULL;
	mod_author_result_t *attempt = NULL;
	const char *name = sysArgGetString("--debug-level-tool-name");
	if (!forgeLevelSessionDoc() && !forgeLevelSessionOpen(arena_id, err, sizeof(err))) goto fail;
	const forge_level_source_t *source = forgeLevelSessionSource();
	const asset_entry_t *enabled = assetCatalogResolve(arena_id);
	if (!name || !source || !enabled || enabled->type != ASSET_ARENA || !enabled->enabled) {
		snprintf(err, sizeof(err), "the saved original arena and its name are required");
		goto fail;
	}
	forge_revision_t before;
	if (!forgeLevelSessionPinSavedRevision(&before, err, sizeof(err))) goto fail;

	attempt = calloc(1, sizeof(*attempt));
	if (!attempt) {
		snprintf(err, sizeof(err), "could not allocate the ordinary Save As result");
		goto fail;
	}
	const mod_author_request_t request = {
		.template_path = source->archive_path, .mods_root = modmgrGetModsDir(), .display_name = name,
	};
	if (modAuthorPlan(&request, attempt, err, sizeof(err)) != MOD_AUTHOR_OK || !attempt->updated
			|| strcmp(attempt->catalog_id, arena_id) != 0 || strcmp(before.arena_id, arena_id) != 0
			|| strcmp(before.digest, source->archive_sha256) != 0
			|| sha256VerifyFile(attempt->archive_path, before.digest) != 1) {
		if (!err[0]) snprintf(err, sizeof(err), "the ordinary Save As target is not the exact pinned original");
		goto fail;
	}
	char target_path[MOD_AUTHOR_PATH_MAX];
	snprintf(target_path, sizeof(target_path), "%s", attempt->archive_path);
	if (!bootForgePickupAuthor(&source_proof, arena_id, err, sizeof(err))) goto fail;
	forge_level_doc_t *draft = forgeLevelSessionDoc();
	const u32 generation = forgeLevelSessionGeneration();
	const u32 catalog_generation = assetCatalogGetGeneration();
	const s32 stage_index = g_StageIndex;
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeLevelValidateOriginalDraft(draft, source, sha, err, sizeof(err))
			|| strlen(sha) != SHA256_HEX_SIZE - 1 || strcmp(sha, source_proof.archive_sha256) == 0) goto fail;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VALIDATE: valid unsaved=%d archive_sha256=%s", forgeLevelSessionHasUnsavedChanges(), sha);

	invalid = bootForgeDraftWithUnknownAction(draft, err, sizeof(err));
	if (!invalid) goto fail;
	if (forgeLevelValidateOriginalDraft(invalid, source, sha, err, sizeof(err)) || sha[0]
			|| !strstr(err, "native module 'og.pickup.fake' does not exist")) goto fail;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VALIDATE: rejected %s", err);

	/* Save As and the session command share this production author function.
	 * The malformed clone stays outside the user's valid editor document. */
	if (forgeLevelSaveAs(invalid, source, name, attempt, err, sizeof(err)) != MOD_AUTHOR_ERR_ARCHIVE
			|| attempt->catalog_id[0] || !strstr(err, "native module 'og.pickup.fake' does not exist")) {
		if (!err[0]) snprintf(err, sizeof(err), "ordinary Save As did not refuse the invalid draft before publication");
		goto fail;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VALIDATE: ordinary save refused arena=%s reason='%s'", arena_id, err);

	forge_level_source_t reopened;
	original = forgeLevelOpen(arena_id, &reopened, err, sizeof(err));
	forge_revision_t after;
	const s32 still_pinned = forgeRevisionReadCurrent(directory, arena_id, &after, NULL, 0);
	const asset_entry_t *resolved = assetCatalogResolve(arena_id);
	if (!original || strcmp(reopened.archive_sha256, source_proof.archive_sha256) != 0
			|| forgeLevelDocObjectCount(original) || forgeLevelDocTriggerCount(original)
			|| sha256VerifyFile(target_path, before.digest) != 1 || !still_pinned
			|| strcmp(before.digest, after.digest) != 0 || strcmp(before.path, after.path) != 0
			|| !resolved || resolved != enabled || !resolved->enabled || resolved->ext.arena.stagenum != source_proof.stage
			|| assetCatalogGetGeneration() != catalog_generation || g_StageNum != source_proof.stage
			|| g_StageIndex != stage_index || mainStageChangePending() || forgeLevelDraftOwnsStageTransition()
			|| forgeLevelSessionGeneration() != generation || forgeLevelSessionDoc() != draft
			|| !forgeLevelSessionHasUnsavedChanges() || forgeLevelSessionUndoCount() != 1 || forgeLevelSessionRedoCount()
			|| forgeLevelDocTriggerCount(draft) != 1) {
		if (!err[0]) snprintf(err, sizeof(err), "draft validation or Save As refusal changed the stage, catalog, saved source, pin, or draft");
		goto fail;
	}
	forgeLevelDocFree(original);
	forgeLevelDocFree(invalid);
	free(attempt);
	*step = 9;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VALIDATE: unchanged arena=%s stage=0x%02x undo=1 source_sha256=%s pin_sha256=%s enabled=1 catalog=%u",
		arena_id, g_StageNum, reopened.archive_sha256, after.digest, catalog_generation);
	smokeHarnessExit(0, "grid_validate_draft_complete");
	return 1;

fail:
	forgeLevelDocFree(original);
	forgeLevelDocFree(invalid);
	free(attempt);
	*step = 9;
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.VALIDATE.FAIL: %s", err[0] ? err : "unexpected draft validation output");
	smokeHarnessExit(1, "grid_validate_draft_failed");
	return 1;
}

/* --launch-forge-level <arena id> and --debug-forge-session-save <name>.
 * Called every gameplay frame from the Forge editor's tick
 * (pdguiForgeEditorTick). One-shot steps:
 *
 *   1. Once the boot stage has placed the player (lvframenum >= 4, the gate
 *      --launch-scenario uses), start a Forge session on the arena.
 *   2. With --debug-forge-session-save: two seconds into that level, with the
 *      editor camera flying, add a player start where the camera is, facing
 *      the way it faces, and save the level under the name
 *      (forgeLevelDebugSessionEdit). The level file stays open, so the HUD
 *      marks its starts and objects in the world.
 *   3. Twenty frames later: turn the camera to a yaw far from every axis
 *      and a pitch that is not level (BOOT_FORGE_PROBE_YAW_DEG,
 *      BOOT_FORGE_PROBE_PITCH_DEG).
 *   4. Forty frames after that, once frames have been drawn at that angle:
 *      log where the markers' projection puts five known points
 *      (bootForgeMarkerProbe).
 *   5. From then on, if something holds the forward action: once the
 *      camera has left that place and stood still again, log how far it
 *      flew and how much of that was along its look (FORGE.FLY.PROBE).
 *
 * The flag takes no screenshot and ends nothing. A smoke waits for the level
 * file to be open (forge_level_file_open) and times its screenshot and its
 * exit from there, on the harness's own clock.
 * --debug-forge-connect-pads is a smoke-only alternative that probes an
 * original two-room arena, plays the linked draft and returns to its editor.
 * --debug-forge-fixed-pickup-trigger probes inventory before and after
 * entering an authored volume in an original two-room draft.
 *
 * Returns 1 on a frame where a step ran. */
static s32 bootGridSaveRestartVariant(const char *original_id, const char *name);
static s32 bootGridPlayRestartVariant(const char *original_id);
static s32 bootGridSaveFallbackVariant(const char *original_id, const char *name);
static s32 bootGridPlayFallbackVariant(const char *original_id);
static s32 bootGridSavePlayedVariantTick(const char *name, s32 host);
static s32 bootGridPlaySavedVariantTick(void);

s32 bootForgeLevelTick(void)
{
	static s32 s_step = 0;
	static const char *s_arena = NULL;
	static const char *s_name = NULL;
	static const char *s_fork_name = NULL;
	static const char *s_restart_variant = NULL;
	static const char *s_fallback_variant = NULL;
	static s32 s_draft = 0;
	static s32 s_connect_pads = 0;
	static s32 s_fixed_pickup_trigger = 0;
	static s32 s_validate_draft = 0;
	static s32 s_draft_returns = 0;
	static s32 s_saved_frame = 0;
	static struct coord s_fly_from;
	static struct coord s_fly_last;
	static s32 s_fly_moving = 0;
	static s32 s_fly_still = 0;

	if (smokeHarnessIsActive()) {
		const char *played_name = sysArgGetString("--debug-grid-save-played-variant");
		if (played_name) return bootGridSavePlayedVariantTick(played_name, 0);
		played_name = sysArgGetString("--debug-grid-save-host-played-variant");
		if (played_name) return bootGridSavePlayedVariantTick(played_name, 1);
		if (sysArgCheck("--debug-grid-play-saved-variant")) return bootGridPlaySavedVariantTick();
		const char *original = sysArgGetString("--debug-grid-variant-restart-play");
		if (original) return bootGridPlayRestartVariant(original);
		original = sysArgGetString("--debug-grid-variant-fallback-play");
		if (original) return bootGridPlayFallbackVariant(original);
	}
	if (s_step == 9) {
		return 0;
	}
	if (s_step == 0) {
		s_arena = sysArgGetString("--launch-forge-level");
		s_name = sysArgGetString("--debug-forge-session-save");
		s_fork_name = sysArgGetString("--debug-forge-fork");
		s_restart_variant = smokeHarnessIsActive() ? sysArgGetString("--debug-grid-variant-restart-save") : NULL;
		s_fallback_variant = smokeHarnessIsActive() ? sysArgGetString("--debug-grid-variant-fallback-save") : NULL;
		s_draft = sysArgCheck("--debug-forge-draft-playtest");
		s_connect_pads = smokeHarnessIsActive() && sysArgCheck("--debug-forge-connect-pads");
		s_fixed_pickup_trigger = smokeHarnessIsActive() && sysArgCheck("--debug-forge-fixed-pickup-trigger");
		s_validate_draft = smokeHarnessIsActive() && sysArgCheck("--debug-forge-validate-draft");
		if (!s_arena || !s_arena[0]) {
			if (s_fallback_variant) smokeHarnessExit(1, "grid_variant_fallback_missing_arena");
			if (s_validate_draft) smokeHarnessExit(1, "grid_validate_draft_missing_arena");
			if (s_fixed_pickup_trigger) smokeHarnessExit(1, "grid_fixed_pickup_trigger_missing_arena");
			if (s_connect_pads) smokeHarnessExit(1, "grid_connect_pads_missing_arena");
			if (s_name && s_name[0]) {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-forge-session-save name='%s' result=FAIL (it needs --launch-forge-level <arena id>)",
					s_name);
			}
			s_step = 9;
			return 0;
		}
		sysLogPrintf(LOG_NOTE, "BOOT: --launch-forge-level arena=%s armed", s_arena);
		s_step = 1;
	}
	if (s_step == 1) {
		s32 started;

		if (g_Vars.lvframenum < 4) {
			return 0;
		}
		started = forgeLevelStartForge(s_arena);
		sysLogPrintf(started ? LOG_NOTE : LOG_WARNING, "BOOT: --launch-forge-level arena=%s result=%s",
			s_arena, started ? "OK" : "FAIL");
		if (!started && s_connect_pads) smokeHarnessExit(1, "grid_connect_pads_start_failed");
		if (!started && s_fixed_pickup_trigger) smokeHarnessExit(1, "grid_fixed_pickup_trigger_start_failed");
		if (!started && s_validate_draft) smokeHarnessExit(1, "grid_validate_draft_start_failed");
		if (!started && s_restart_variant) smokeHarnessExit(1, "grid_variant_restart_start_failed");
		if (!started && s_fallback_variant) smokeHarnessExit(1, "grid_variant_fallback_start_failed");
		s_step = (started && (s_validate_draft || s_fixed_pickup_trigger || s_connect_pads || s_draft || (s_fork_name && s_fork_name[0])
			|| (s_name && s_name[0]) || s_restart_variant || s_fallback_variant)) ? 2 : 9;
		return 1;
	}
	if (s_validate_draft) return bootForgeValidateDraftTick(s_arena, &s_step);
	if (s_fixed_pickup_trigger) return bootForgePickupTriggerTick(s_arena, &s_step);
	if (s_connect_pads) return bootForgeConnectPadsTick(&s_step);
	if (s_fallback_variant) {
		if (!forgeIsFreefly() || g_Vars.lvframenum < 120) return 0;
		const s32 saved = bootGridSaveFallbackVariant(s_arena, s_fallback_variant);
		s_step = 9;
		smokeHarnessExit(saved ? 0 : 1, saved ? "grid_variant_fallback_saved" : "grid_variant_fallback_save_failed");
		return 1;
	}
	if (s_restart_variant) {
		if (!forgeIsFreefly() || g_Vars.lvframenum < 120) return 0;
		const s32 saved = bootGridSaveRestartVariant(s_arena, s_restart_variant);
		s_step = 9;
		smokeHarnessExit(saved ? 0 : 1, saved ? "grid_variant_restart_saved" : "grid_variant_restart_save_failed");
		return 1;
	}
	if (s_fork_name && s_fork_name[0]) {
		if (!forgeIsFreefly() || g_Vars.lvframenum < 120) return 0;
		char err[256] = "";
		s32 forked = forgeLevelDebugForkPinned(s_fork_name, err, sizeof(err));
		sysLogPrintf(forked ? LOG_NOTE : LOG_WARNING, "FORGE.REVISION.DEBUG: result=%s reason='%s'",
			forked ? "OK" : "FAIL", err);
		s_step = 9;
		return 1;
	}
	if (s_draft) {
		char err[256] = "";
		if (s_step == 2 && forgeIsFreefly() && g_Vars.lvframenum >= 120) {
			if ((!s_draft_returns && !forgeLevelDebugEditDraft(err, sizeof(err)))
					|| !forgeLevelSessionPlayDraft(err, sizeof(err))) {
				sysLogPrintf(LOG_WARNING, "FORGE.DRAFT.DEBUG.FAIL: %s", err);
				s_step = 9;
				return 1;
			}
			forgeBotAddRequest(1);
			s_step = 6;
			return 1;
		}
		if (s_step == 6 && forgeLevelDraftIsPlaying() && g_Vars.lvframenum >= 120) {
			if (forgeLevelDraftReturn()) s_step = 7;
			return 1;
		}
		if (s_step == 7 && forgeIsFreefly() && g_Vars.lvframenum >= 60) {
			s32 retained = forgeLevelSessionHasUnsavedChanges()
				&& forgeLevelSessionUndoCount() == 1 && forgeLevelSessionUndo()
				&& !forgeLevelSessionHasUnsavedChanges() && forgeLevelSessionRedo()
				&& forgeLevelSessionHasUnsavedChanges()
				&& forgeBotSettings()->active_count == 0
				&& forgeBotSettings()->frozen_count == 0;
			if (!retained) {
				sysLogPrintf(LOG_WARNING, "FORGE.DRAFT.DEBUG.FAIL: draft history or bot counts did not survive the round trip");
				s_step = 9;
				return 1;
			}
			sysLogPrintf(LOG_NOTE, "FORGE.DRAFT.DEBUG.OK: return=%d history=retained",
				++s_draft_returns);
			s_step = s_draft_returns == 2 ? 9 : 2;
			return 1;
		}
		return 0;
	}
	if (s_step == 2) {
		struct coord cam;
		f32 position[3];
		f32 look[3];
		f32 live_look[3];
		f32 yaw;
		s32 saved;

		if (!forgeIsFreefly() || g_Vars.lvframenum < 120) {
			return 0;
		}
		/* The camera's facing, flat. Its yaw is the player's turn angle t, and
		 * a player with turn angle t faces (-sin t, 0, cos t); a player started
		 * on a pad with this look gets the same angle back (player.c), so the
		 * start faces what the camera faces. */
		cam.x = 0.0f;
		cam.y = 0.0f;
		cam.z = 0.0f;
		forgeGetCameraPos(&cam);
		yaw = forgeGetCameraYawDeg() * 0.017453292f;
		position[0] = cam.x;
		position[1] = cam.y;
		position[2] = cam.z;
		look[0] = -sinf(yaw);
		look[1] = 0.0f;
		look[2] = cosf(yaw);
		/* --debug-forge-session-live: edits for the world to show, along the
		 * flat look of the probe view the screenshots are taken in. */
		live_look[0] = -sinf(BOOT_FORGE_PROBE_YAW_DEG * 0.017453292f);
		live_look[1] = 0.0f;
		live_look[2] = cosf(BOOT_FORGE_PROBE_YAW_DEG * 0.017453292f);
		saved = forgeLevelDebugSessionEdit(s_name, position, look,
			sysArgCheck("--debug-forge-session-undo") ? 1 : 0,
			sysArgCheck("--debug-forge-session-live") ? live_look : NULL);
		sysLogPrintf(saved ? LOG_NOTE : LOG_WARNING, "BOOT: --debug-forge-session-save name='%s' result=%s",
			s_name, saved ? "OK" : "FAIL");
		s_saved_frame = g_Vars.lvframenum;
		s_step = saved ? 3 : 9;
		return 1;
	}
	/* A stage that was loaded again counts its frames from zero; then there is
	 * nothing left to wait for. */
	if (s_step == 3) {
		if (g_Vars.lvframenum >= s_saved_frame && g_Vars.lvframenum < s_saved_frame + 20) {
			return 0;
		}
		forgeDebugSetCameraAngles(BOOT_FORGE_PROBE_YAW_DEG, BOOT_FORGE_PROBE_PITCH_DEG);
		s_step = 4;
		return 1;
	}
	if (s_step == 4) {
		if (g_Vars.lvframenum >= s_saved_frame && g_Vars.lvframenum < s_saved_frame + 60) {
			return 0;
		}
		bootForgeMarkerProbe();
		s_fly_from.x = 0.0f;
		s_fly_from.y = 0.0f;
		s_fly_from.z = 0.0f;
		forgeGetCameraPos(&s_fly_from);
		s_fly_last = s_fly_from;
		s_fly_moving = 0;
		s_fly_still = 0;
		s_step = 5;
		return 1;
	}

	/* 5. The fly check: does the camera fly where it looks? It needs a hand,
	 * or a smoke, holding the forward action after the probe. Once the camera
	 * has left the place the probe measured from and then stood still for ten
	 * frames, log how far it went and how much of that was along its look and
	 * along its right (1.000 along the look is "it flew where it looked").
	 * Counted in frames of stillness, not in time, so a slow frame rate does
	 * not cut the flight short. Without input nothing is logged. */
	{
		struct coord now;
		f32 dx;
		f32 dy;
		f32 dz;
		f32 step;

		now = s_fly_last;
		forgeGetCameraPos(&now);
		dx = now.x - s_fly_last.x;
		dy = now.y - s_fly_last.y;
		dz = now.z - s_fly_last.z;
		step = sqrtf(dx * dx + dy * dy + dz * dz);
		s_fly_last = now;
		dx = now.x - s_fly_from.x;
		dy = now.y - s_fly_from.y;
		dz = now.z - s_fly_from.z;
		if (!s_fly_moving) {
			if (sqrtf(dx * dx + dy * dy + dz * dz) > 50.0f) {
				s_fly_moving = 1;
			}
			return 0;
		}
		s_fly_still = (step < 0.01f) ? s_fly_still + 1 : 0;
		if (s_fly_still < 10) {
			return 0;
		}
		{
			const f32 yaw = forgeGetCameraYawDeg() * 0.017453292f;
			const f32 pitch = forgeGetCameraPitchDeg() * 0.017453292f;
			const f32 distance = sqrtf(dx * dx + dy * dy + dz * dz);
			const f32 along_look = (dx * (-sinf(yaw) * cosf(pitch)) + dy * sinf(pitch)
				+ dz * (cosf(yaw) * cosf(pitch))) / distance;
			const f32 along_right = (dx * -cosf(yaw) + dz * -sinf(yaw)) / distance;

			sysLogPrintf(LOG_NOTE,
				"FORGE.FLY.PROBE: moved=(%.0f, %.0f, %.0f) distance=%.0f along_look=%.3f along_right=%.3f yaw=%.1f pitch=%.1f",
				dx, dy, dz, distance, along_look, along_right,
				forgeGetCameraYawDeg(), forgeGetCameraPitchDeg());
		}
		s_step = 9;
		return 1;
	}
}

s32 bootDebugThemeStageTransitionProofTick(void)
{
    if (!g_BootDebugThemeStageTransitionProof) {
        return 0;
    }

    if (g_BootDebugThemeStageTransitionProof == 1) {
        if (g_Vars.lvframenum < 4) {
            return 0;
        }

        s32 closure_count = pdguiThemeLoaderLogActiveOwnership(
            "pre_stage_transition");
        if (closure_count <= 0) {
            sysLogPrintf(LOG_WARNING,
                "BOOT: --debug-theme-stage-transition-proof has no active lifecycle closure");
            g_BootDebugThemeStageTransitionProof = 0;
            return 1;
        }

        g_BootDebugThemeStageTransitionFrom = g_Vars.stagenum;
        g_BootDebugThemeStageTransitionProof = 2;
        sysLogPrintf(LOG_NOTE,
            "BOOT: --debug-theme-stage-transition-proof queueing ordinary transition from=0x%02x to=0x%02x closure=%d",
            g_BootDebugThemeStageTransitionFrom, STAGE_CREDITS, closure_count);
        mainChangeToStage(STAGE_CREDITS);
        return 1;
    }

    if (g_Vars.stagenum != STAGE_CREDITS || g_Vars.lvframenum < 4) {
        return 0;
    }

    s32 closure_count = pdguiThemeLoaderLogActiveOwnership(
        "post_stage_transition");
    sysLogPrintf(LOG_NOTE,
        "BOOT: --debug-theme-stage-transition-proof consumed: from=0x%02x to=0x%02x closure=%d",
        g_BootDebugThemeStageTransitionFrom, g_Vars.stagenum, closure_count);
    g_BootDebugThemeStageTransitionProof = 0;
    return 1;
}

/* Called once per frame from pdmain.c's mainTick when --launch-mp-room
 * is paired with --debug-auto-start-match. Defers matchStart until the
 * CI boot stage has created the player prop, then starts the match before
 * the first CI render can tick unrelated source-body setup. */
s32 bootLaunchMpMatchTick(void)
{
	if (!g_BootLaunchMpMatchPending) {
		return 0;
	}
	if (g_Vars.stagenum != STAGE_CITRAINING) {
		return 0;
	}
	if (!g_Vars.players[0]
			|| !g_Vars.players[0]->prop
			|| !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	g_BootLaunchMpMatchPending = 0;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-mp-room direct match start");
	if (matchStart() != 0) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --launch-mp-room direct match start failed");
	} else {
		combatSimVerifyOnMatchStart("direct");
	}
	return 1;
}

/* Called once per frame from pdmain.c's mainTick while --launch-challenge is
 * armed: once the CI boot stage has created the player, start the challenge
 * through the same call the ImGui challenge screen makes. */
s32 bootLaunchChallengeTick(void)
{
	if (!g_BootLaunchChallengePending) {
		return 0;
	}
	if (g_Vars.stagenum != STAGE_CITRAINING) {
		return 0;
	}
	if (!g_Vars.players[0]
			|| !g_Vars.players[0]->prop
			|| !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	g_BootLaunchChallengePending = 0;
	sysLogPrintf(LOG_NOTE, "BOOT: --launch-challenge firing: slot=%d", g_BootLaunchChallengeSlot);
	matchStartFromChallenge(g_BootLaunchChallengeSlot);
	return 1;
}

/* T-NETWORKING-011 (finding 3): --debug-face-nearest-bot. Every frame of an
 * ordinary match this turns the local player, level, toward the nearest
 * simulant and points the BOT.RENDER.AUDIT traces at it, so a screenshot and
 * the audit show how this peer draws a simulant it may not own. The yaw uses
 * the engine's convention for players (chrGetAngleToPos): a player faces
 * (360 - vv_theta) degrees, measured like atan2f(dx, dz). */
/* B-1193: end the running mission once, on the host or offline. A client
 * never ends a networked mission itself; the host's end reaches it. */
s32 bootDebugEndMissionTick(void)
{
	extern s32 objectivesDebugCompleteCurrentMission(void); /* game/objectives.h */
	s32 forced = 0;

	if (g_BootDebugEndMissionSec < 0 || g_BootDebugEndMissionFired) {
		return 0;
	}
	if (g_NetMode == NETMODE_CLIENT || g_MainIsEndscreen
			|| g_Vars.tickmode != TICKMODE_NORMAL
			|| g_Vars.stagenum == STAGE_TITLE || g_Vars.stagenum == STAGE_CITRAINING) {
		return 0;
	}
	/* Missions only: solo, co-op or counter-op, not a Combat Simulator match. */
	if (g_Vars.coopplayernum < 0 && g_Vars.antiplayernum < 0
			&& (g_Vars.normmplayerisrunning || g_Vars.mplayerisrunning)) {
		return 0;
	}
	if (g_Vars.lvframe60 < g_BootDebugEndMissionSec * 60) {
		return 0;
	}

	g_BootDebugEndMissionFired = 1;
	if (g_BootDebugEndMissionComplete) {
		forced = objectivesDebugCompleteCurrentMission();
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-end-mission ending stage=0x%02x result=%s objectives_completed=%d lvframe60=%d netmode=%d",
		(u32)g_Vars.stagenum, g_BootDebugEndMissionComplete ? "complete" : "fail",
		forced, (s32)g_Vars.lvframe60, (s32)g_NetMode);
	func0000e990();
	return 1;
}

/* B-1196: clone one guard once, on the host or offline, in a solo, co-op or
 * counter-op mission. A cloneable guard (CHRCFLAG_CLONEABLE) is preferred,
 * then any living guard; the clone gets the guard's held weapons, team and
 * flags as aiDuplicateChr gives them, and may appear on screen. It stands
 * idle (see below). */
s32 bootDebugCloneGuardTick(void)
{
	extern struct prop *chrSpawnAtChr(struct chrdata *basechr, s32 body, s32 head, u32 chrnum, u8 *ailist, u32 spawnflags);
	extern u8 *ailistFindById(s32 ailistid);
	struct chrdata *base = NULL;
	struct prop *cloneprop;
	struct chrdata *clone;
	struct prop *wprops[2] = { NULL, NULL };

	if (g_BootDebugCloneGuardSec < 0 || g_BootDebugCloneGuardFired) {
		return 0;
	}
	if (g_NetMode == NETMODE_CLIENT || g_MainIsEndscreen
			|| g_Vars.tickmode != TICKMODE_NORMAL
			|| g_Vars.stagenum == STAGE_TITLE || g_Vars.stagenum == STAGE_CITRAINING) {
		return 0;
	}
	if (g_Vars.coopplayernum < 0 && g_Vars.antiplayernum < 0
			&& (g_Vars.normmplayerisrunning || g_Vars.mplayerisrunning)) {
		return 0;
	}
	if (g_Vars.lvframe60 < g_BootDebugCloneGuardSec * 60) {
		return 0;
	}

	g_BootDebugCloneGuardFired = 1;

	/* Each candidate in turn (cloneable guards first) until one has room
	 * beside it for the clone. */
	cloneprop = NULL;

	for (s32 pass = 0; pass < 2 && cloneprop == NULL; pass++) {
		for (s32 i = 0; i < g_NumChrSlots && cloneprop == NULL; i++) {
			struct chrdata *chr = &g_ChrSlots[i];

			/* Not chrIsDead through a local extern: bool's size depends on
			 * include order (SP-75), so read the action directly. */
			if (!chr->prop || chr->prop->type != PROPTYPE_CHR || chr->prop->chr != chr
					|| chr->aibot || !chr->model
					|| chr->actiontype == ACT_DIE || chr->actiontype == ACT_DEAD
					|| (chr->chrflags & CHRCFLAG_HIDDEN)) {
				continue;
			}
			if ((pass == 0) != ((chr->chrflags & CHRCFLAG_CLONEABLE) != 0)) {
				continue;
			}

			/* The idle list, not the AI's search list: a clone hunting the
			 * players could end a smoke's mission before its scripted end.
			 * What reaches a client (SVC_NPC_SPAWN) is the same either way. */
			cloneprop = chrSpawnAtChr(chr, chr->bodynum, -1, chr->chrnum,
				ailistFindById(GAILIST_IDLE), SPAWNFLAG_ALLOWONSCREEN);

			if (cloneprop && cloneprop->chr) {
				base = chr;
			} else {
				cloneprop = NULL;
			}
		}
	}

	if (base == NULL || cloneprop == NULL) {
		sysLogPrintf(LOG_WARNING, "BOOT: --debug-clone-guard found no living guard with room for a clone");
		return 0;
	}

	clone = cloneprop->chr;
	chrSetChrnum(clone, chrsGetNextUnusedChrnum());
	base->chrdup = clone->chrnum;
	clone->team = base->team;

	for (s32 h = 0; h < 2; h++) {
		if (base->weapons_held[h] && base->weapons_held[h]->weapon) {
			struct weaponobj *w = base->weapons_held[h]->weapon;

			wprops[h] = chrGiveWeapon(clone, w->base.modelnum, w->weaponnum,
				h == 1 ? OBJFLAG_WEAPON_LEFTHANDED : 0);
		}
	}

	clone->flags = base->flags;
	clone->flags2 = base->flags2;

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-clone-guard cloned chr %d as chr %d syncid=%u cloneable=%d weapons=%u,%u netmode=%d",
		(s32)base->chrnum, (s32)clone->chrnum, (unsigned)cloneprop->syncid,
		(base->chrflags & CHRCFLAG_CLONEABLE) ? 1 : 0,
		wprops[0] ? (unsigned)wprops[0]->syncid : 0u, wprops[1] ? (unsigned)wprops[1]->syncid : 0u,
		(s32)g_NetMode);
	return 1;
}

s32 bootDebugFaceNearestBotTick(void)
{
	struct player *pl;
	struct chrdata *best = NULL;
	f32 bestdist2 = 0.0f;

	if (!g_BootDebugFaceNearestBot) {
		return 0;
	}
	if (!g_Vars.normmplayerisrunning || g_Vars.tickmode != TICKMODE_NORMAL) {
		return 0;
	}
	pl = (g_NetMode == NETMODE_CLIENT && g_NetLocalClient && g_NetLocalClient->player)
		? g_NetLocalClient->player : g_Vars.players[0];
	if (!pl || !pl->prop || pl->isdead) {
		return 0;
	}

	for (s32 i = 0; i < g_BotCount && i < MAX_BOTS; i++) {
		struct chrdata *chr = g_MpBotChrPtrs[i];
		f32 dx;
		f32 dz;
		f32 dist2;

		if (!chr || !chr->prop) {
			continue;
		}
		dx = chr->prop->pos.x - pl->prop->pos.x;
		dz = chr->prop->pos.z - pl->prop->pos.z;
		dist2 = dx * dx + dz * dz;
		if (!best || dist2 < bestdist2) {
			best = chr;
			bestdist2 = dist2;
		}
	}
	if (!best || bestdist2 < 1.0f) {
		return 0;
	}

	{
		const f32 dx = best->prop->pos.x - pl->prop->pos.x;
		const f32 dz = best->prop->pos.z - pl->prop->pos.z;
		f32 theta = 360.0f - atan2f(dx, dz) * (180.0f / 3.14159265f);

		while (theta >= 360.0f) {
			theta -= 360.0f;
		}
		while (theta < 0.0f) {
			theta += 360.0f;
		}
		pl->vv_theta = theta;
		pl->vv_verta = 0.0f;
	}
	g_BootDebugFaceBotProp = best->prop;

	{
		const u32 now = SDL_GetTicks();

		if (g_BootDebugFaceBotLogs < 30
				&& (g_BootDebugFaceBotLogs == 0 || now - g_BootDebugFaceBotLastLogMs >= 1000)) {
			const struct model *model = best->model;
			const struct anim *anim = model ? model->anim : NULL;
			const f32 dist = sqrtf(bestdist2);
			const f32 dx = best->prop->pos.x - pl->prop->pos.x;
			const f32 dz = best->prop->pos.z - pl->prop->pos.z;
			const f32 lx = pl->cam_look.x;
			const f32 lz = pl->cam_look.z;
			const f32 ll = sqrtf(lx * lx + lz * lz);
			/* 1.0 when the camera from the last frame looks straight at it */
			const f32 lookdot = ll > 0.001f ? (lx * dx + lz * dz) / (ll * dist) : 0.0f;

			g_BootDebugFaceBotLastLogMs = now;
			g_BootDebugFaceBotLogs++;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-face-nearest-bot facing chrnum=%d dist=%.0f theta=%.1f look_dot=%.3f prop_flags=0x%x hidden=0x%x actiontype=%d model=%p anim=%d frame=%.2f pos=(%.0f,%.0f,%.0f)",
				(s32)best->chrnum, dist, pl->vv_theta, lookdot,
				(u32)best->prop->flags, (u32)best->hidden, (s32)best->actiontype,
				(const void *)model,
				anim ? (s32)anim->animnum : -1, anim ? anim->frame : -1.0f,
				best->prop->pos.x, best->prop->pos.y, best->prop->pos.z);
		}
	}
	return 1;
}

/* Called once per frame from pdmain.c's mainTick when the
 * --debug-mount-bike one-shot is armed.  Walks g_Vars.activeprops
 * looking for the first OBJTYPE_HOVERBIKE prop, and on match calls
 * currentPlayerTryMountHoverbike(prop) once, then clears the latch. This
 * deliberately exercises the production candidate/commit path but does not
 * prove normal propFindForInteract LOS/collision selection.
 *
 * Gates:
 *   - g_BootMountBikePending must be set (caller checks before calling)
 *   - the real offline gameplay layer, normal tick, update, and Player 0
 *     control/walking readiness must already be true
 *   - g_Vars.players[0] must be alive and have a valid prop
 *   - prop must be PROPTYPE_OBJ + obj->type == OBJTYPE_HOVERBIKE
 *
 * Returns 1 when the mount was attempted (whether or not it succeeded),
 * else 0.  Caller clears g_BootMountBikePending on either outcome. */
s32 bootDebugMountBikeTick(void)
{
	if (!g_BootMountBikePending) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop
			|| !g_Vars.players[0]->prop->chr) {
		return 0;
	}
	/* B-1158: a solo player outside a cutscene has no body model (the normal
	 * tick removes it every frame), so this must not wait for one: that wait
	 * never ended in campaign stages. chrMoveToPos now skips the model when
	 * there is none. Keep the lvframenum >= 4 settle --debug-spawn-at uses. */
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (sceneCurrentLayer() != LAYER_GAMEPLAY
			|| g_Vars.tickmode != TICKMODE_NORMAL
			|| g_Vars.lvupdate240 <= 0
			|| g_PlayersWithControl[0] == 0
			|| g_Vars.players[0]->isdead
			|| g_Vars.players[0]->bondmovemode != MOVEMODE_WALK) {
		return 0;
	}
	if (g_BootMountBikeStage < 0) {
		g_BootMountBikeStage = g_Vars.stagenum;
	} else if (g_BootMountBikeStage != g_Vars.stagenum) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-mount-bike stage changed before a hoverbike became available; one-shot consumed");
		g_BootMountBikePending = 0;
		return 1;
	}

	/* The active list runs on into the paused list (propActivate links the
	 * new head to g_Vars.pausedprops), so this walks every listed prop, paused
	 * ones too. */
	static s32 s_noBikeLogged = 0;
	s32 walked = 0;
	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		walked++;
		if (prop->type == PROPTYPE_OBJ
				&& prop->obj
				&& prop->obj->type == OBJTYPE_HOVERBIKE
				&& (prop->obj->hidden & OBJHFLAG_MOUNTED) == 0) {
			/* Place Player 0 in a generic side-entry position so this hook
			 * exercises the production proximity/facing/commit transaction.
			 * The direct candidate call does not prove the normal
			 * propFindForInteract LOS/collision selection. The hook remains
			 * stage-agnostic: it uses the bike's own room and orientation rather
			 * than map coordinates. */
			const s32 previousPlayerNum = g_Vars.currentplayernum;
			setCurrentPlayerNum(0);
			struct coord target = prop->pos;
			f32 turn = hoverpropGetTurnAngle(prop->obj);
			struct chrdata *chr = g_Vars.players[0]->prop->chr;
			const u32 oldhidden = chr->hidden;
			bool placed;
			target.x += sinf(turn + M_BADPI * 0.5f) * 150.0f;
			target.z += cosf(turn + M_BADPI * 0.5f) * 150.0f;
			/* The placement may be where the camera sees it, as the game's own
			 * chrSetPos allows: without this, chrAdjustPosForSpawn refuses
			 * every place in view, which on a level of one open room is every
			 * place beside the vehicle (the room vehicle smokes, B-1241). */
			chr->hidden |= CHRHFLAG_WARPONSCREEN;
			placed = chrMoveToPos(g_Vars.players[0]->prop->chr, &target, prop->rooms, turn, false);
			if ((oldhidden & CHRHFLAG_WARPONSCREEN) == 0) {
				chr->hidden &= ~CHRHFLAG_WARPONSCREEN;
			}
			if (!placed) {
				setCurrentPlayerNum(previousPlayerNum);
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-mount-bike placement failed for Player 0");
				g_BootMountBikePending = 0;
				return 1;
			}
			/* The bootstrap is an explicit direct PC-use intent. It exercises the
			 * production candidate/commit path without pretending to be a normal
			 * propFindForInteract LOS/collision selection. */
			g_Vars.players[0]->pcinteractusekind = 2;
			bool ok = currentPlayerTryMountHoverbike(prop);
			setCurrentPlayerNum(previousPlayerNum);
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-mount-bike consumed: path=debug_candidate_no_propfind_los prop=%p stagenum=0x%02x result=%s",
				(void *)prop, (u32)g_Vars.stagenum, ok ? "MOUNTED" : "REJECTED");
			g_BootMountBikePending = 0;
			return 1;
		}
		prop = prop->next;
	}
	if (!s_noBikeLogged) {
		/* Once, so a smoke that never mounts says why. */
		s_noBikeLogged = 1;
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-mount-bike ready but no free hoverbike among %d listed props on stage 0x%02x; still waiting",
			walked, (u32)g_Vars.stagenum);
	}
	/* Keep the latch pending when the current stage has not exposed a bike yet.
	 * Stage/player readiness, rather than a process- or frame-start timeout,
	 * owns when this one-shot may consume itself; a later prop activation can
	 * still exercise the same production candidate path. */
	return 0;
}

/* B-1291: --debug-spawn-at-look-at. Once the teleport has run, every frame
 * turns player 0's view toward the point. Yaw by the engine's convention for
 * players (a player faces (360 - vv_theta) degrees, measured like
 * atan2f(dx, dz)); pitch is vv_verta, up positive (bmoveUpdateHead builds the
 * look vector as (0, sin(verta), cos(verta)) before the yaw). The prop of a
 * player sits at eye height, so it is the origin of the view. */
static s32 bootDebugSpawnAtLookAtTick(void)
{
	struct player *pl;
	f32 dx;
	f32 dy;
	f32 dz;
	f32 flat;
	f32 theta;

	if (!g_BootSpawnAtLookAt || !g_BootSpawnAtDone) {
		return 0;
	}
	pl = g_Vars.players[0];
	if (!pl || !pl->prop || g_Vars.tickmode != TICKMODE_NORMAL) {
		return 0;
	}

	dx = g_BootSpawnAtLookX - pl->prop->pos.x;
	dy = g_BootSpawnAtLookY - pl->prop->pos.y;
	dz = g_BootSpawnAtLookZ - pl->prop->pos.z;
	flat = sqrtf(dx * dx + dz * dz);
	if (flat < 1.0f) {
		return 0;
	}

	theta = 360.0f - atan2f(dx, dz) * (180.0f / 3.14159265f);
	while (theta >= 360.0f) {
		theta -= 360.0f;
	}
	while (theta < 0.0f) {
		theta += 360.0f;
	}
	pl->vv_theta = theta;
	pl->vv_verta = atan2f(dy, flat) * (180.0f / 3.14159265f);

	{
		const u32 now = SDL_GetTicks();

		if (g_BootSpawnAtLookLogs < 12
				&& (g_BootSpawnAtLookLogs == 0 || now - g_BootSpawnAtLookLastLogMs >= 1000)) {
			const f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
			/* 1.000 when the camera of the last frame looks straight at the point */
			const f32 lookdot = (pl->cam_look.x * dx + pl->cam_look.y * dy + pl->cam_look.z * dz) / dist;

			g_BootSpawnAtLookLastLogMs = now;
			g_BootSpawnAtLookLogs++;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-spawn-at-look-at facing: theta=%.1f verta=%.1f look_dot=%.3f dist=%.0f eye=(%.0f,%.0f,%.0f) room=%d",
				pl->vv_theta, pl->vv_verta, lookdot, dist,
				pl->prop->pos.x, pl->prop->pos.y, pl->prop->pos.z,
				(s32)pl->prop->rooms[0]);
		}
	}
	return 1;
}

/* c115 (2026-05-14): Called once per frame from pdmain.c's mainTick
 * when the --debug-spawn-at one-shot is armed. Teleports the current
 * player (player 0) to the JSON-parsed (x,y,z,room) using chrMoveToPos
 * once the stage setup has placed the player prop.
 *
 * Gates (mirror the bike-mount hook):
 *   - g_BootSpawnAtPending must be set
 *   - g_Vars.lvframenum >= 4 (load black frame out of the way, player
 *     prop spawned by setupCreateProps)
 *   - g_Vars.players[0] must be alive and have a valid prop->chr
 *
 * One-shot: clears the latch regardless of chrMoveToPos's success/fail
 * so a subsequent stage change does not re-teleport. */
s32 bootDebugSpawnAtTick(void)
{
	if (!g_BootSpawnAtPending) {
		return bootDebugSpawnAtLookAtTick();
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop || !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	if (g_BootSpawnAtAfterIntro) {
		/* B-1291: a mission's intro animates the player's own body with
		 * absolute positions, and while the tick mode is the cutscene's the
		 * character tick moves the prop along with the animation
		 * (chr0f0220ec), so a teleport made now is undone on this frame. An
		 * intro of several shots re-enters the cutscene a frame after it
		 * was left, so one second of uninterrupted control is asked for. */
		if (g_Vars.tickmode != TICKMODE_NORMAL
				|| g_Vars.players[0]->bondmovemode != MOVEMODE_WALK) {
			if (!g_BootSpawnAtWaitLogged) {
				g_BootSpawnAtWaitLogged = 1;
				sysLogPrintf(LOG_NOTE,
					"BOOT: --debug-spawn-at-after-intro waiting: stagenum=0x%02x frame=%d tickmode=%d movemode=%d",
					(u32)g_Vars.stagenum, (s32)g_Vars.lvframenum,
					(s32)g_Vars.tickmode, (s32)g_Vars.players[0]->bondmovemode);
			}
			g_BootSpawnAtControl60 = 0;
			return 0;
		}
		g_BootSpawnAtControl60 += g_Vars.lvupdate60;
		if (g_BootSpawnAtControl60 < 60) {
			return 0;
		}
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-spawn-at-after-intro control held: stagenum=0x%02x frame=%d waited_for_intro=%d",
			(u32)g_Vars.stagenum, (s32)g_Vars.lvframenum, g_BootSpawnAtWaitLogged);
	}

	struct prop *prop = g_Vars.players[0]->prop;
	struct chrdata *chr = prop->chr;

	struct coord target;
	target.x = g_BootSpawnAtX;
	target.y = g_BootSpawnAtY;
	target.z = g_BootSpawnAtZ;

	/* chrMoveToPos wants a RoomNum[] terminated by -1 (-2 in some
	 * call sites means "any room"); the AI-script call site at
	 * chraicommands.c:5215 uses [room, -1] which is the canonical
	 * single-room form. */
	RoomNum rooms[2];
	rooms[0] = (RoomNum)g_BootSpawnAtRoom;
	rooms[1] = -1;

	/* Pass angle=0.0f -- the smoke tests that use this flag set up
	 * their own look state via subsequent scripted input. The force
	 * flag (final arg) is true so chrAdjustPosForSpawn doesn't refuse
	 * the teleport because of bg-collision near the target. */
	setCurrentPlayerNum(0);
	bool ok = chrMoveToPos(chr, &target, rooms, 0.0f, true);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-spawn-at consumed: prop=%p stagenum=0x%02x result=%s pos=(%f,%f,%f) room=%d",
		(void *)prop, (u32)g_Vars.stagenum, ok ? "OK" : "FAILED",
		g_BootSpawnAtX, g_BootSpawnAtY, g_BootSpawnAtZ, g_BootSpawnAtRoom);

	g_BootSpawnAtPending = 0;
	g_BootSpawnAtDone = ok ? 1 : 0;
	return 1;
}

/* The player this hook switched away from when it targets a player other than
 * 0; bootDebugPlaceBotNearPlayerRestorePlayer, called right after the hook,
 * switches back so the rest of the frame keeps its own current player. */
static s32 s_BootDebugPlaceBotNearPlayerPrevPlayer = -1;

void bootDebugPlaceBotNearPlayerRestorePlayer(void)
{
	if (s_BootDebugPlaceBotNearPlayerPrevPlayer >= 0) {
		setCurrentPlayerNum(s_BootDebugPlaceBotNearPlayerPrevPlayer);
		s_BootDebugPlaceBotNearPlayerPrevPlayer = -1;
	}
}

s32 bootDebugPlaceBotNearPlayerPreRenderTick(void)
{
	const s32 targetplayer = g_BootDebugPlaceBotNearPlayerNum;
	struct chrdata *bot = NULL;
	struct prop *playerprop = NULL;
	struct coord target;
	RoomNum rooms[2];
	RoomNum room = (RoomNum)-1;
	RoomNum floorroom = (RoomNum)-1;
	u16 floorcol = 0;
	u8 floortype = 0;
	f32 targetdistance = 0.0f;
	f32 targetlateral = 0.0f;
	f32 playerground = -4294967296.0f;
	const char *targetgroundsource = "none";
	bool foundtarget = false;

	if (!g_BootDebugPlaceBotNearPlayerPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[targetplayer] || !g_Vars.players[targetplayer]->prop
			|| !g_Vars.players[targetplayer]->prop->chr) {
		return 0;
	}

	if (targetplayer != 0 && g_Vars.currentplayernum != targetplayer) {
		s_BootDebugPlaceBotNearPlayerPrevPlayer = g_Vars.currentplayernum;
	}
	setCurrentPlayerNum(targetplayer);
	if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) {
		return 0;
	}

	/* Intro cameras can briefly report a usable room immediately before the
	 * cutscene IMC and camera animation take ownership. Requiring a bounded
	 * run of ordinary first-person ticks keeps this hook on the same gameplay
	 * boundary that a real shot uses instead of latching a transient camera
	 * position. Once the first placement commits, keep following the player
	 * until the requested wall-clock hold duration expires. */
	if (!g_BootDebugPlaceBotNearPlayerLogged) {
		if (g_Vars.tickmode != TICKMODE_NORMAL
				|| g_Vars.currentplayer->cameramode != CAMERAMODE_DEFAULT) {
			g_BootDebugPlaceBotNearPlayerReadyFrames = 0;
			return 0;
		}

		g_BootDebugPlaceBotNearPlayerReadyFrames++;
		if (g_BootDebugPlaceBotNearPlayerReadyFrames
				< k_BootDebugPlaceBotNearPlayerStableFrames) {
			return 0;
		}
	}

	playerprop = g_Vars.currentplayer->prop;
	playerground = g_Vars.currentplayer->vv_ground;
	if (playerground <= -30000.0f) {
		playerground = g_Vars.currentplayer->vv_manground;
	}
	if (playerground <= -30000.0f) {
		goto wait_or_timeout;
	}
	room = g_Vars.currentplayer->cam_room >= 0
		? (RoomNum)g_Vars.currentplayer->cam_room
		: playerprop->rooms[0];
	if (room == (RoomNum)-1 && playerprop->rooms[0] >= 0) {
		room = playerprop->rooms[0];
	}
	if (room == (RoomNum)-1 && g_Vars.currentplayer->cam_room >= 0) {
		room = (RoomNum)g_Vars.currentplayer->cam_room;
	}
	if (room == (RoomNum)-1) {
		goto wait_or_timeout;
	}

	for (s32 i = 0; i < MAX_BOTS; ++i) {
		struct chrdata *candidate = g_MpBotChrPtrs[i];
		if (candidate && candidate->prop && candidate->model) {
			bot = candidate;
			break;
		}
	}
	if (!bot) {
		goto wait_or_timeout;
	}

	f32 forward_x = g_Vars.currentplayer->cam_look.x;
	f32 forward_z = g_Vars.currentplayer->cam_look.z;
	if (forward_x > -0.001f && forward_x < 0.001f &&
			forward_z > -0.001f && forward_z < 0.001f) {
		forward_x = g_Vars.currentplayer->vv_sintheta;
		forward_z = g_Vars.currentplayer->vv_costheta;
	}
	{
		const f32 forwardlen = sqrtf(forward_x * forward_x + forward_z * forward_z);
		if (forwardlen < 0.001f) {
			goto wait_or_timeout;
		}
		forward_x /= forwardlen;
		forward_z /= forwardlen;
	}
	/* Finding 3: in visible mode the hold keeps the direction of the first
	 * placement. A peer facing the bot (--debug-face-nearest-bot) turns the
	 * player, and following that turn would move the bot again. */
	if (g_BootDebugPlaceBotNearPlayerVisible) {
		static f32 s_VisibleForwardX = 0.0f;
		static f32 s_VisibleForwardZ = 1.0f;

		if (g_BootDebugPlaceBotNearPlayerLogged) {
			forward_x = s_VisibleForwardX;
			forward_z = s_VisibleForwardZ;
		} else {
			s_VisibleForwardX = forward_x;
			s_VisibleForwardZ = forward_z;
		}
	}
	rooms[0] = room;
	rooms[1] = -1;

	/* Multi-level arenas can expose more than one floor in a single room. A
	 * fixed point on the camera ray is therefore not enough: Chicago's first
	 * 240-unit point resolves to an upper GEOFLAG_STEP floor roughly 600 units
	 * above the player, and the next ordinary chr tick correctly moves the bot
	 * out of view. Search a small, deterministic cone from far to near and
	 * accept only source-derived ground samples on the player's floor level.
	 * A player's prop Y is their standing camera/body position (159 units above
	 * the Chicago floor in the ordinary spawn), so level comparison must use the
	 * movement controller's authoritative vv_ground rather than prop->pos.y.
	 * Keep the
	 * legacy query first because it owns dynamic-floor metadata; if its
	 * intentional GEOFLAG_STEP semantics return a different level, consult the
	 * collision mesh cache at the same point rather than changing global stair
	 * climbing behavior for the sake of a smoke-only placement hook. */
	{
		static const f32 distances[] = {240.0f, 200.0f, 160.0f, 120.0f, 100.0f, 80.0f};
		static const f32 lateralfracs[] = {0.0f, -0.20f, 0.20f};
		/* Finding 3: with --debug-place-bot-near-player-visible, turn the search
		 * around the player; without it, straight ahead only, as before. */
		static const f32 visibleturns[] = {0.0f, 45.0f, -45.0f, 90.0f, -90.0f,
			135.0f, -135.0f, 180.0f};
		const s32 numturns = g_BootDebugPlaceBotNearPlayerVisible
			? (s32)(sizeof(visibleturns) / sizeof(visibleturns[0])) : 1;
		f32 chosenturn = 0.0f;
		const bool logcandidates = !g_BootDebugPlaceBotNearPlayerLogged;

		if (logcandidates) {
			sysLogPrintf(LOG_NOTE,
				"BOOT.PLACE.CANDIDATES: player=(%f,%f,%f) player_ground=%f player_rooms=(%d,%d) camera=(%f,%f,%f) camera_room=%d query_room=%d bot_radius=%f forward=(%f,%f)",
				playerprop->pos.x, playerprop->pos.y, playerprop->pos.z,
				playerground,
				(s32)playerprop->rooms[0], (s32)playerprop->rooms[1],
				g_Vars.currentplayer->cam_pos.x,
				g_Vars.currentplayer->cam_pos.y,
				g_Vars.currentplayer->cam_pos.z,
				(s32)g_Vars.currentplayer->cam_room, (s32)room,
				bot->radius, forward_x, forward_z);
		}

		for (s32 turnindex = 0; turnindex < numturns && !foundtarget; turnindex++) {
		const f32 turn = visibleturns[turnindex] * (3.14159265f / 180.0f);
		const f32 dir_x = forward_x * cosf(turn) + forward_z * sinf(turn);
		const f32 dir_z = forward_z * cosf(turn) - forward_x * sinf(turn);
		const f32 right_x = -dir_z;
		const f32 right_z = dir_x;
		for (s32 lateralindex = 0;
				lateralindex < (s32)(sizeof(lateralfracs) / sizeof(lateralfracs[0])) && !foundtarget;
				lateralindex++) {
			for (s32 distanceindex = 0;
					distanceindex < (s32)(sizeof(distances) / sizeof(distances[0]));
					distanceindex++) {
				const f32 distance = distances[distanceindex];
				const f32 lateral = distance * lateralfracs[lateralindex];
				struct coord candidate = g_Vars.currentplayer->cam_pos;
				u16 candidatefloorcol = 0;
				u8 candidatefloortype = 0;
				RoomNum candidatefloorroom = (RoomNum)-1;
				RoomNum candidaterooms[21];
				RoomNum candidateaboverooms[21];
				RoomNum candidatebestroom = (RoomNum)-1;
				RoomNum resolvedrooms[2];
				f32 resolvedground = -30000.0f;

				candidate.x += dir_x * distance + right_x * lateral;
				candidate.y = playerprop->pos.y + 100.0f;
				candidate.z += dir_z * distance + right_z * lateral;
				candidaterooms[0] = (RoomNum)-1;
				candidateaboverooms[0] = (RoomNum)-1;
				bgFindRoomsByPos(&candidate, candidaterooms,
					candidateaboverooms, 20, &candidatebestroom);
				resolvedrooms[0] = candidaterooms[0] >= 0
					? candidaterooms[0] : candidatebestroom;
				resolvedrooms[1] = (RoomNum)-1;

				const f32 legacyground = cdFindGroundInfoAtCyl(&candidate, bot->radius, rooms,
					&candidatefloorcol, &candidatefloortype, NULL,
					&candidatefloorroom, NULL, NULL);
				f32 selectedground = legacyground;
				f32 meshground = -30000.0f;
				const char *selectedsource = "legacy";

				if (!(legacyground > -30000.0f
						&& fabsf(legacyground - playerground) <= 80.0f)) {
					f32 meshnormaly = 1.0f;
					meshground = meshFindFloor(&candidate, bot->radius, &meshnormaly);

					if (meshground > -30000.0f
							&& fabsf(meshground - playerground) <= 80.0f) {
						selectedground = meshground;
						selectedsource = "mesh";
						candidatefloorcol = 0;
						candidatefloortype = 0;
						candidatefloorroom = (RoomNum)-1;
					}
				}

				if (resolvedrooms[0] >= 0) {
					resolvedground = cdFindGroundInfoAtCyl(&candidate,
						bot->radius, resolvedrooms, NULL, NULL, NULL,
						NULL, NULL, NULL);
				}

				if (logcandidates) {
					sysLogPrintf(LOG_NOTE,
						"BOOT.PLACE.CANDIDATE: distance=%f lateral=%f pos=(%f,%f,%f) fixed_room=%d fixed_ground=%f fixed_floor_room=%d mesh_ground=%f in_room=%d above_room=%d best_room=%d resolved_ground=%f player_y=%f player_ground=%f",
						distance, lateral, candidate.x, candidate.y, candidate.z,
						(s32)room, legacyground, (s32)candidatefloorroom,
						meshground, (s32)candidaterooms[0],
						(s32)candidateaboverooms[0], (s32)candidatebestroom,
						resolvedground, playerprop->pos.y, playerground);
				}

				/* Finding 3: in visible mode the player must see the spot. The
				 * player's prop position is at eye height; aim at the bot's chest. */
				s32 losclear = 1;
				if (g_BootDebugPlaceBotNearPlayerVisible) {
					struct coord losfrom = playerprop->pos;
					struct coord losto = candidate;
					losto.y = selectedground + 120.0f;
					losclear = resolvedrooms[0] >= 0
						&& selectedground > -30000.0f
						&& cdTestLos06(&losfrom, playerprop->rooms, &losto,
							resolvedrooms, CDTYPE_BG | CDTYPE_DOORS);
					if (logcandidates && lateralindex == 0) {
						sysLogPrintf(LOG_NOTE,
							"BOOT.PLACE.VISIBLE: turn=%.0f distance=%.0f lateral=%.0f ground_ok=%d los=%d room=%d",
							visibleturns[turnindex], distance, lateral,
							(selectedground > -30000.0f
								&& fabsf(selectedground - playerground) <= 80.0f) ? 1 : 0,
							losclear, (s32)resolvedrooms[0]);
					}
				}

				if (selectedground > -30000.0f
						&& fabsf(selectedground - playerground) <= 80.0f
						&& losclear) {
					target = candidate;
					/* The candidate is a floor-domain coordinate. The relocation
					 * boundary converts it to the explicitly requested floor-relative
					 * model-root domain. */
					target.y = selectedground;
					targetdistance = distance;
					targetlateral = lateral;
					targetgroundsource = selectedsource;
					floorcol = candidatefloorcol;
					floortype = candidatefloortype;
					floorroom = candidatefloorroom;
					chosenturn = visibleturns[turnindex];
					foundtarget = true;
					break;
				}
			}
		}
		}
		if (foundtarget && g_BootDebugPlaceBotNearPlayerVisible && logcandidates) {
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-place-bot-near-player-visible chose turn=%.0f distance=%.0f lateral=%.0f",
				chosenturn, targetdistance, targetlateral);
		}
	}

	if (!foundtarget) {
		goto wait_or_timeout;
	}

	/* This hook owns a floor coordinate, not an actor root. Keep that domain
	 * distinction explicit; the model policy preserves the requested root
	 * height while ordinary animation remains authoritative. */
	if (!chrRelocateToFloorWithCachedGround(bot, &target,
			playerprop->pos.y - playerground, rooms, 0.0f,
			floorcol, floortype, floorroom)) {
		goto wait_or_timeout;
	}

	if (bot->prop) {
		/* Do not synthesize ONTHISSCREEN or reactivate an already-live bot.
		 * chrTick owns the visibility flag and the matching per-frame model
		 * matrices. Advertising visibility here, after propsTickPlayer, lets
		 * propFindAimingAt consume stale off-screen model state in this frame.
		 * The next ordinary chr tick sees the committed position, builds the
		 * matrices, and publishes visibility before propsSort. */
		g_BootDebugPlaceBotNearPlayerProp = bot->prop;
		bootDebugPlaceBotNearPlayerTrace("placed", bot->prop, 0,
			(s32)bot->prop->rooms[0],
			(s32)bot->prop->flags,
			(s32)bot->prop->active,
			(s32)g_Vars.lvframenum);
	}

	if (!g_BootDebugPlaceBotNearPlayerLogged) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-place-bot-near-player consumed: bot=%p chrnum=%d result=OK floor=(%f,%f,%f) root_y=%f room=%d floor_room=%d player_room=%d camera_forward=(%f,%f) distance=%f lateral=%f ground_source=%s hold_ms=%u",
			(void *)bot,
			(s32)bot->chrnum,
			target.x, target.y, target.z,
			bot->prop ? bot->prop->pos.y : 0.0f,
			(s32)(bot->prop ? bot->prop->rooms[0] : (RoomNum)-1),
			(s32)floorroom,
			(s32)room,
			forward_x, forward_z,
			targetdistance, targetlateral,
			targetgroundsource,
			g_BootDebugPlaceBotNearPlayerHoldDurationMs);
		g_BootDebugPlaceBotNearPlayerLogged = true;
		g_BootDebugPlaceBotNearPlayerHoldStartMs = SDL_GetTicks();
	}

	g_BootDebugPlaceBotNearPlayerHoldFrames++;
	if ((u32)(SDL_GetTicks() - g_BootDebugPlaceBotNearPlayerHoldStartMs)
			>= g_BootDebugPlaceBotNearPlayerHoldDurationMs) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-place-bot-near-player hold complete: bot=%p chrnum=%d frames=%d failures=%d elapsed_ms=%u room=%d flags=0x%x",
			(void *)bot,
			(s32)bot->chrnum,
			g_BootDebugPlaceBotNearPlayerHoldFrames,
			g_BootDebugPlaceBotNearPlayerHoldFailures,
			(u32)(SDL_GetTicks() - g_BootDebugPlaceBotNearPlayerHoldStartMs),
			(s32)(bot->prop ? bot->prop->rooms[0] : (RoomNum)-1),
			(u32)(bot->prop ? bot->prop->flags : 0));
		g_BootDebugPlaceBotNearPlayerPending = 0;
	}
	return 1;

wait_or_timeout:
	if (g_BootDebugPlaceBotNearPlayerLogged) {
		g_BootDebugPlaceBotNearPlayerHoldFailures++;
		if ((u32)(SDL_GetTicks() - g_BootDebugPlaceBotNearPlayerHoldStartMs)
				>= g_BootDebugPlaceBotNearPlayerHoldDurationMs) {
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-place-bot-near-player hold complete: frames=%d failures=%d elapsed_ms=%u result=DEGRADED",
				g_BootDebugPlaceBotNearPlayerHoldFrames,
				g_BootDebugPlaceBotNearPlayerHoldFailures,
				(u32)(SDL_GetTicks() - g_BootDebugPlaceBotNearPlayerHoldStartMs));
			g_BootDebugPlaceBotNearPlayerPending = 0;
		}
		return 0;
	}

	g_BootDebugPlaceBotNearPlayerFrames++;
	if (g_BootDebugPlaceBotNearPlayerFrames > k_BootDebugPlaceBotNearPlayerMaxWaitFrames) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --debug-place-bot-near-player timed out: frame=%d stage=0x%02x player_room=%d",
			(s32)g_Vars.lvframenum,
			(u32)g_Vars.stagenum,
			playerprop ? (s32)playerprop->rooms[0] : -1);
		g_BootDebugPlaceBotNearPlayerPending = 0;
		return 1;
	}
	return 0;
}

s32 bootDebugForceFirstPersonPreRenderTick(void)
{
	if (!g_BootDebugForceFirstPersonPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (g_BootLaunchMpArena && g_Vars.stagenum == STAGE_CITRAINING) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop || !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	setCurrentPlayerNum(0);
	if (!g_Vars.currentplayer) {
		return 0;
	}

	s32 before = (s32)g_Vars.currentplayer->cameramode;
	s32 before_tickmode = (s32)g_Vars.tickmode;
	s32 before_movemode = (s32)g_Vars.currentplayer->bondmovemode;
	s32 queued_weapon = (s32)g_Vars.currentplayer->gunctrl.switchtoweaponnum;

	if (g_BootDebugForceFirstPersonFrames <= 0
			&& (g_Vars.tickmode != TICKMODE_NORMAL
				|| g_Vars.currentplayer->bondmovemode != MOVEMODE_WALK)) {
		player0f0b9a20();
		if (queued_weapon >= 0) {
			bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
			bgunEquipWeapon2(HAND_RIGHT, queued_weapon);
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-force-first-person restored queued weapon=%d after normal-mode release",
				queued_weapon);
		}
	}
	playerSetCameraMode(CAMERAMODE_DEFAULT);
	if (g_BootDebugForceFirstPersonLook || g_BootDebugForceFirstPersonCamOffset) {
		struct coord pos = g_Vars.currentplayer->cam_pos;
		struct coord up = {0.0f, 1.0f, 0.0f};
		if (g_Vars.currentplayer->prop) {
			/* B-1186: the player's prop already sits at eye height (spawns
			 * place it at ground + vv_eyeheight, player.c), so it is the
			 * first-person camera position. Adding vv_eyeheight again put
			 * the forced camera 159 units up, inside or above the ceiling,
			 * and shots fired from it flew out of the world. */
			pos = g_Vars.currentplayer->prop->pos;
		}
		if (g_BootDebugForceFirstPersonCamOffset) {
			pos.x += g_BootDebugForceFirstPersonCamOffsetVec.x;
			pos.y += g_BootDebugForceFirstPersonCamOffsetVec.y;
			pos.z += g_BootDebugForceFirstPersonCamOffsetVec.z;
		}
		playerSetCamProperties(&pos,
			&up,
			g_BootDebugForceFirstPersonLook
				? &g_BootDebugForceFirstPersonLookVec
				: &g_Vars.currentplayer->cam_look,
			g_Vars.currentplayer->cam_room);
		if (!g_BootDebugForceFirstPersonLookLogged) {
			g_BootDebugForceFirstPersonLookLogged = true;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-force-first-person-look active: stagenum=0x%02x frame=%d look=(%f,%f,%f) cam_offset=(%f,%f,%f) room=%d",
				(u32)g_Vars.stagenum,
				(s32)g_Vars.lvframenum,
				g_BootDebugForceFirstPersonLookVec.x,
				g_BootDebugForceFirstPersonLookVec.y,
				g_BootDebugForceFirstPersonLookVec.z,
				g_BootDebugForceFirstPersonCamOffsetVec.x,
				g_BootDebugForceFirstPersonCamOffsetVec.y,
				g_BootDebugForceFirstPersonCamOffsetVec.z,
				(s32)g_Vars.currentplayer->cam_room);
		}
	}

	if (g_BootDebugForceFirstPersonFrames <= 0) {
		g_BootDebugForceFirstPersonFrames = k_BootDebugForceFirstPersonFrames;
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-force-first-person active: stagenum=0x%02x frame=%d before=%d after=%d frames=%d tickmode=%d->%d movemode=%d->%d",
			(u32)g_Vars.stagenum,
			(s32)g_Vars.lvframenum,
			before,
			(s32)g_Vars.currentplayer->cameramode,
			g_BootDebugForceFirstPersonFrames,
			before_tickmode,
			(s32)g_Vars.tickmode,
			before_movemode,
			(s32)g_Vars.currentplayer->bondmovemode);
	} else {
		g_BootDebugForceFirstPersonFrames--;
		if (g_BootDebugForceFirstPersonFrames == 0) {
			g_BootDebugForceFirstPersonPending = 0;
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-force-first-person consumed: stagenum=0x%02x frame=%d",
				(u32)g_Vars.stagenum,
				(s32)g_Vars.lvframenum);
		}
	}

	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --launch-load-agent one-shot is armed. Invokes the same
 * agentSessionActivate(name) transaction used by ordinary Agent Select.
 *
 * Gates (mirror the other deferred-tick hooks):
 *   - g_BootLoadAgentArmed must be set
 *   - g_Vars.lvframenum >= 4 (deferred just like spawn-at / mount-bike;
 *     even though profile activation does not need stage/player props,
 *     deferring keeps the harness assertion ordering deterministic so
 *     the smoke test sees the SAVE: log lines after the SAVE: initialized
 *     line from saveInit())
 *
 * agentSessionActivate returns 0 on success and -1 on failure. The shared
 * save and session layers publish the validation and activation receipts
 * used by both CLI and ordinary-menu smoke tests.
 *
 * One-shot: clears the latch regardless of result so a subsequent
 * stage change does not re-fire. */
s32 bootLaunchLoadAgentTick(void)
{
	if (!g_BootLoadAgentArmed) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}

	s32 result = agentSessionActivate(g_BootLoadAgentName);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-load-agent consumed: name='%s' result=%s",
		g_BootLoadAgentName, result == 0 ? "OK" : "FAILED");

	g_BootLoadAgentArmed = 0;
	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --listen-bind one-shot is armed. Invokes netStartServer
 * with the parsed port and NET_MAX_CLIENTS so the client boots
 * straight into a listen-server bound on the requested UDP port --
 * sister to bootConnectHostTick() for the two-process peer-link
 * smoke. The actual ENet bind log line ("NET: created server on port
 * %u") comes from netStartServer itself; the BOOT: consumed line is
 * the smoke-runner's wait-for marker.
 *
 * Gates:
 *   - g_BootListenBindArmed must be set
 *   - g_NetInit must be true (netInit() drained inside the boot pool's
 *     bootRunCatalogWork worker; by the time mainTick fires the boot
 *     overlay shutdown is done and the worker has joined). If the
 *     binary was launched with --no-net the latch will sit armed
 *     forever -- intentional: --no-net + --listen-bind is nonsense
 *     and the operator should be told via a single WARNING. We log
 *     that warning once on the first tick where netInit is missing.
 *
 * One-shot: clears the latch regardless of netStartServer's result so
 * a subsequent stage change does not re-bind. */
s32 bootListenBindTick(void)
{
	if (!g_BootListenBindArmed) {
		return 0;
	}

	extern s32 g_NetInit;
	if (!g_NetInit) {
		static s32 warned = 0;
		if (!warned) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --listen-bind cannot fire: g_NetInit is false (likely --no-net was also passed)");
			warned = 1;
		}
		/* Hold the latch; if netInit eventually completes we'll fire.
		 * If --no-net was passed the latch sits forever, which is fine
		 * for a smoke test -- the assertion will catch the missing
		 * NET: created server log. */
		return 0;
	}

	extern s32 netStartServer(u16 port, s32 maxclients);
	extern s32 g_NetMaxClients;
	s32 rc = netStartServer((u16)g_BootListenBindPort, g_NetMaxClients);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --listen-bind consumed: port=%d maxclients=%d result=%s rc=%d",
		g_BootListenBindPort, g_NetMaxClients,
		rc == 0 ? "OK" : "FAILED", (int)rc);

	g_BootListenBindArmed = 0;
	return 1;
}

/* c118 (2026-05-15): Called once per frame from pdmain.c's mainTick
 * when the --connect-host one-shot is armed. Invokes netStartClient
 * with the captured "addr:port" string so the client boots straight
 * into a connect-to-host attempt -- sister to bootListenBindTick() for
 * the two-process peer-link smoke. The actual ENet connect log line
 * ("NET: connecting to %s...") comes from netStartClient itself; the
 * BOOT: consumed line is the smoke-runner's wait-for marker.
 *
 * Gates:
 *   - g_BootConnectHostArmed must be set
 *   - g_NetInit must be true (same constraint as bootListenBindTick).
 *
 * One-shot: clears the latch regardless of netStartClient's result so
 * a subsequent stage change does not re-connect. */
s32 bootConnectHostTick(void)
{
	if (!g_BootConnectHostArmed) {
		return 0;
	}

	extern s32 g_NetInit;
	if (!g_NetInit) {
		static s32 warned = 0;
		if (!warned) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --connect-host cannot fire: g_NetInit is false (likely --no-net was also passed)");
			warned = 1;
		}
		return 0;
	}

	extern s32 netStartClient(const char *addr);
	s32 rc = netStartClient(g_BootConnectHostAddr);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --connect-host consumed: addr='%s' result=%s rc=%d",
		g_BootConnectHostAddr, rc == 0 ? "OK" : "FAILED", (int)rc);

	g_BootConnectHostArmed = 0;
	return 1;
}

/* c3845 (2026-06-23): Called once per frame from pdmain.c's mainTick when
 * --host-autostart is armed. On the listen HOST only, waits until a REMOTE
 * client (any slot whose state is >= CLSTATE_LOBBY, excluding the host's own
 * local-client slot) has finished the join handshake, holds a ~2 s settle
 * delay so the manifest/catalog handshake state is stable, then fires the
 * SAME high-level start path the Room "Start Match" button uses --
 * netLobbyRequestStartWithSims(...) seeded from the typed stage ID that
 * --launch-mp-room populated. The explicit autostart mode selects Combat
 * Simulator, co-op, or Counter-Op metadata. The bridge replays CLC_LOBBY_START
 * through the server handler locally, performing the full room-assign /
 * manifest-broadcast / participant-playernum / ready-gate setup that
 * netServerStageStart() depends on -- so we deliberately do NOT call
 * netServerStageStart() raw.
 *
 * Fires EXACTLY ONCE via the g_BootHostAutostartFired guard.
 *
 * Gates:
 *   - g_BootHostAutostartArmed set (caller pre-checks the latch is meaningful)
 *   - g_NetInit true and g_NetMode == NETMODE_SERVER and listen (not dedicated)
 *   - g_NetLocalClient present and in CLSTATE_LOBBY (host self in lobby)
 *   - at least one OTHER client slot has reached CLSTATE_LOBBY
 *   - settle delay (~120 frames @ 60 fps ≈ 2 s) elapsed since that condition
 */
/* While --host-autostart is armed and has not fired, say what it is waiting
 * on: once when the reason changes and then about every three seconds. A
 * two-process smoke that never starts its match otherwise leaves no trace of
 * which condition was never met. Log only. */
static void bootHostAutostartLogWaiting(const char *reason, s32 remote_in_lobby)
{
	static const char *last_reason = NULL;
	static u32 calls_since_log = 0;

	if (reason != last_reason || ++calls_since_log >= 300) {
		extern s32 g_NetInit;
		sysLogPrintf(LOG_NOTE,
			"BOOT: --host-autostart waiting: reason=%s netinit=%d netmode=%d dedicated=%d "
			"local_state=%d remote_in_lobby=%d peers_needed=%d settle=%d phase=%d",
			reason, (s32)(g_NetInit != 0), (s32)g_NetMode, (s32)g_NetDedicated,
			g_NetLocalClient ? (s32)g_NetLocalClient->state : -1,
			remote_in_lobby, g_BootHostAutostartPeers,
			g_BootHostAutostartSettleFrames, g_BootHostAutostartPhase);
		last_reason = reason;
		calls_since_log = 0;
	}
}

/*
 * T-NETWORKING-011 (finding 4): --host-autostart-matches. After a match,
 * re-arm the start for the next one in the same room once the host has left
 * its end screen into the room (Continue, as a player presses it), the match
 * room is released and the room is in LOBBY again, then a settle window of
 * about 10 s for the clients to press Continue too. The re-fire reuses the
 * room: phase 1 spacing, then the start.
 */
static void bootHostAutostartRearmTick(void)
{
	extern s32 pdguiIsInRoom(void);
	hub_room_t *room;

	if (g_NetMode != NETMODE_SERVER || g_NetMatchRoomId != 0xFF
			|| g_BootHostAutostartRoomId == 0xFF
			|| menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)
			|| menupoolIsActive(MENU_TYPE_ENDSCREEN_SOLO) || !pdguiIsInRoom()) {
		g_BootHostAutostartRematchFrames = 0;
		return;
	}

	room = roomGetById((u8)g_BootHostAutostartRoomId);

	if (room == NULL || room->state != ROOM_STATE_LOBBY) {
		g_BootHostAutostartRematchFrames = 0;
		return;
	}

	if (++g_BootHostAutostartRematchFrames < 600) {
		return;
	}

	g_BootHostAutostartRematchFrames = 0;
	g_BootHostAutostartFired = 0;
	g_BootHostAutostartPhase = 1;
	g_BootHostAutostartPostJoinFrames = 0;
	g_BootHostAutostartSettleFrames = 120;
	sysLogPrintf(LOG_NOTE, "BOOT: --host-autostart re-armed: match %d of %d in room %d",
		g_BootHostAutostartStarted + 1, g_BootHostAutostartMatches, g_BootHostAutostartRoomId);
}

enum boot_grid_private_draft_role {
	BOOT_GRID_DRAFT_NONE,
	BOOT_GRID_DRAFT_HOST,
	BOOT_GRID_DRAFT_CREATOR,
	BOOT_GRID_DRAFT_ORDINARY
};

enum boot_grid_private_draft_step {
	BOOT_GRID_DRAFT_WAIT_ROOM,
	BOOT_GRID_DRAFT_WAIT_GRANT,
	BOOT_GRID_DRAFT_WAIT_COMMIT,
	BOOT_GRID_DRAFT_WAIT_RESUME_INVITE,
	BOOT_GRID_DRAFT_WAIT_RESUME_GRANT,
	BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_START,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_SPAWN,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_EDIT,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_VISIBILITY,
	BOOT_GRID_DRAFT_WAIT_DECORATION_START,
	BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION,
	BOOT_GRID_DRAFT_WAIT_DECORATION_VALID,
	BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE,
	BOOT_GRID_DRAFT_WAIT_LAYOUT_AMBIENT_OFF,
	BOOT_GRID_DRAFT_WAIT_LIVE_DISCONNECT,
	BOOT_GRID_DRAFT_WAIT_LIVE_DETACH,
	BOOT_GRID_DRAFT_WAIT_LIVE_AUTH,
	BOOT_GRID_DRAFT_WAIT_LIVE_ROOM_INVITE,
	BOOT_GRID_DRAFT_WAIT_LIVE_ROOM,
	BOOT_GRID_DRAFT_WAIT_LIVE_EDIT,
	BOOT_GRID_DRAFT_WAIT_LIVE_RETIRED,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN,
	BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETIRED,
	BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW,
	BOOT_GRID_DRAFT_WAIT_EXTENSION_ROOM,
	BOOT_GRID_DRAFT_WAIT_EXTENSION_ACTIVE,
	BOOT_GRID_DRAFT_WAIT_CLOSE,
	BOOT_GRID_DRAFT_FLUSH_CLOSE,
	BOOT_GRID_DRAFT_DONE
};

static enum boot_grid_private_draft_role s_GridDraftRole;
static enum boot_grid_private_draft_step s_GridDraftStep;
static u32 s_GridDraftStepTick;
static u8 s_GridDraftRoom = 0xff;
static s32 s_GridDraftResume;
static s32 s_GridDraftPlaytest;
static s32 s_GridDraftVariantPlaytest;
static s32 s_GridDraftVisibility;
static s32 s_GridDraftDecoration;
static s32 s_GridDraftDecorationOnPlan;
static s32 s_GridDraftLiveReconnect;
static s32 s_GridDraftRoomExtension;
static s32 s_GridDraftPreparedReview;
static s32 s_GridDraftTransferReview;
static s32 s_GridDraftApplyLayout;
static s32 s_GridDraftLayoutDecoration;
static s32 s_GridDraftLayoutAmbientOff;
static s32 s_GridDraftRelocateLayout;
static s32 s_GridDraftLayoutReconnect;
static const char kGridLayoutRelocationArena[] = "mod:arena_grid_extension_relocate";
static struct {
	struct chrdata *actors[MAX_MPCHRS];
	struct prop *props[MAX_MPCHRS];
	u64 prop_generations[MAX_MPCHRS];
	struct player *player;
	const struct room *rooms;
	u64 first_serial;
	s32 stage_index;
	s32 frame;
	s32 requested;
	s32 second;
	s32 retry_reported_transfer;
	s32 retry_verified_timeout;
	s32 captured_relocation;
	s32 verified_relocation;
	s32 match_time;
	f32 relocation_origin;
	u8 actor_state[SHA256_DIGEST_SIZE];
} s_GridLayoutSmoke;
static struct {
	grid_edit_room_control_t before_disconnect;
	u32 catalog;
} s_GridLayoutReconnect;
static char s_GridPreparedCreatorArenaSha[SHA256_HEX_SIZE];
static grid_edit_staged_variant_info_t s_GridRoomExtension;
static grid_match_runtime_identity_t s_GridRoomExtensionWorld;
static grid_edit_variant_review_t s_GridRoomExtensionOffer;
static u32 s_GridRoomExtensionCatalog;
static u32 s_GridRoomExtensionEpoch;
static grid_edit_room_invite_t s_GridDraftInitialInvite;
static u32 s_GridDraftConnection;
static u64 s_GridDraftInitialActor;
static u64 s_GridDraftResumeGeneration;
static u64 s_GridDraftResumeActor;
static u32 s_GridPlaytestEpoch;
static s32 s_GridPlaytestStage;
static f32 s_GridPlaytestBaseline;
static forge_revision_t s_GridPlaytestVariant;
static grid_edit_move_spawn_t s_GridPlaytestMovedSpawn;
static s32 s_GridPlaytestMovedPad;

static const struct { unsigned players, creators; } s_GridVisibilityCases[] = {
	{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0},
};
static unsigned s_GridVisibilityCase;
static u32 s_GridVisibilityStarted;
static u32 s_GridVisibilityStable;
static u32 s_GridVisibilityLastView;
static s32 s_GridVisibilityPeerVerified;
static grid_edit_move_spawn_t s_GridVisibilityCursor;
static grid_edit_move_decoration_t s_GridDecorationOriginal;
static struct {
	const struct prop *prop;
	u32 command;
	u64 lifecycle;
	RoomNum room;
	struct coord position;
	struct coord moved_position;
} s_GridDecorationNative;

#define GRID_DRAFT_SMOKE_BRIGHTNESS 137
#define GRID_DRAFT_SMOKE_BASE_BRIGHTNESS 104
#define GRID_DRAFT_SMOKE_RESUME_TAIL_BRIGHTNESS 149
#define GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS 155
#define GRID_VISIBILITY_SMOKE_HOLD_MS 1300u
#define GRID_VISIBILITY_SMOKE_TIMEOUT_MS 12000u
#define GRID_DECORATION_SMOKE_TIMEOUT_MS 12000u
#define GRID_DECORATION_SMOKE_X_DELTA 24.0f
#define GRID_PLAYTEST_RETURN_SMOKE_TIMEOUT_MS 15000u
#define GRID_LIVE_RECONNECT_SMOKE_TIMEOUT_MS 45000u
#define GRID_EXTENSION_REVIEW_SMOKE_TIMEOUT_MS 20000u
#define GRID_EXTENSION_TRANSFER_SMOKE_TIMEOUT_MS 90000u

static void bootGridDraftInit(void)
{
	if (!smokeHarnessIsActive()) return;
	const char *role = sysArgGetString("--debug-grid-private-draft");
	if (!role) return;
	if (strcmp(role, "host") == 0) s_GridDraftRole = BOOT_GRID_DRAFT_HOST;
	else if (strcmp(role, "creator") == 0) s_GridDraftRole = BOOT_GRID_DRAFT_CREATOR;
	else if (strcmp(role, "ordinary") == 0) s_GridDraftRole = BOOT_GRID_DRAFT_ORDINARY;
	else smokeHarnessExit(1, "grid_draft_invalid_role");
	s_GridDraftResume = sysArgCheck("--debug-grid-private-resume");
	s_GridDraftVisibility = sysArgCheck("--debug-grid-private-visibility");
	s_GridDraftDecoration = sysArgCheck("--debug-grid-private-decoration");
	s_GridDraftLiveReconnect = sysArgCheck("--debug-grid-private-live-reconnect");
	s_GridDraftVariantPlaytest = sysArgCheck("--debug-grid-private-saved-variant");
	s_GridDraftRoomExtension = sysArgCheck("--debug-grid-private-room-extension");
	s_GridDraftPreparedReview = sysArgCheck("--debug-grid-private-prepared-review") != 0;
	s_GridDraftTransferReview = sysArgCheck("--debug-grid-private-transfer-review") != 0;
	s_GridDraftApplyLayout = s_GridDraftTransferReview && sysArgCheck("--debug-grid-private-layout-apply");
	s_GridDraftLayoutDecoration = sysArgCheck("--debug-grid-private-layout-decoration");
	s_GridDraftLayoutAmbientOff = sysArgCheck("--debug-grid-private-layout-ambient-off");
	s_GridDraftDecorationOnPlan = s_GridDraftLayoutDecoration
		|| (s_GridDraftDecoration && sysArgCheck("--debug-grid-decoration-on-plan"));
	s_GridDraftRelocateLayout = s_GridDraftApplyLayout && sysArgCheck("--debug-grid-private-layout-relocate");
	s_GridDraftLayoutReconnect = sysArgCheck("--debug-grid-private-layout-reconnect") != 0;
	if (s_GridDraftLayoutDecoration && (!s_GridDraftApplyLayout || s_GridDraftRelocateLayout
			|| s_GridDraftLayoutReconnect || sysArgCheck("--debug-grid-private-layout-retry"))) {
		smokeHarnessExit(1, "grid_layout_decoration_requires_single_apply");
	}
	if (s_GridDraftLayoutAmbientOff && !s_GridDraftLayoutDecoration) {
		smokeHarnessExit(1, "grid_layout_ambient_off_requires_decoration");
	}
	if (s_GridDraftLayoutReconnect && !s_GridDraftApplyLayout) {
		smokeHarnessExit(1, "grid_layout_reconnect_without_apply");
	}
	if (s_GridDraftPreparedReview && !s_GridDraftRoomExtension) {
		smokeHarnessExit(1, "grid_prepared_review_without_extension");
	}
	if (s_GridDraftTransferReview && !s_GridDraftPreparedReview) {
		smokeHarnessExit(1, "grid_transfer_review_without_preparation");
	}
	s_GridDraftPlaytest = s_GridDraftVariantPlaytest || s_GridDraftVisibility || s_GridDraftDecoration
		|| s_GridDraftLiveReconnect || s_GridDraftRoomExtension || sysArgCheck("--debug-grid-private-playtest");
	if ((s_GridDraftResume && s_GridDraftPlaytest)
			|| s_GridDraftVariantPlaytest + s_GridDraftVisibility + s_GridDraftDecoration
				+ s_GridDraftLiveReconnect + s_GridDraftRoomExtension > 1) {
		smokeHarnessExit(1, "grid_draft_conflicting_modes");
	}
}

/* Author the creator's independent copy before registering with the host. */
static void bootGridPrepareCreatorReviewSource(void)
{
	if (!s_GridDraftPreparedReview || s_GridDraftTransferReview || s_GridDraftRole != BOOT_GRID_DRAFT_CREATOR) return;
	if (g_NetMode == NETMODE_CLIENT || g_NetMode == NETMODE_SERVER || g_NetStageEpoch) {
		smokeHarnessExit(1, "grid_prepared_review_creator_already_networked");
		return;
	}
	const char *parent = "mod:arena_grid_playtest_draft";
	const char *expected = "mod:arena_grid_extension_staged";
	mod_author_result_t saved = {0};
	forge_level_source_t source = {0};
	scenario_stage_atmosphere_t look;
	char reason[256] = {0};
	forge_level_doc_t *doc = forgeLevelOpen(parent, &source, reason, sizeof(reason));
	s32 ok = doc && forgeLevelDocGetAtmosphere(doc, &look)
		&& look.has_ambient && look.ambient_light == GRID_DRAFT_SMOKE_BASE_BRIGHTNESS;
	if (ok) {
		look.ambient_light = GRID_DRAFT_SMOKE_BRIGHTNESS;
		ok = forgeLevelDocSetAtmosphere(doc, &look, reason, sizeof(reason))
			&& forgeLevelForkExtendedRevision(parent, source.archive_sha256, doc,
				"Grid Extension Staged", 2, 300, &saved, reason, sizeof(reason))
			&& strcmp(saved.catalog_id, expected) == 0
			&& modAuthorRuntimeEnable(saved.mod_id, NULL, reason, sizeof(reason));
	}
	forgeLevelDocFree(doc);
	forge_level_source_t candidate = {0};
	doc = ok ? forgeLevelOpen(saved.catalog_id, &candidate, reason, sizeof(reason)) : NULL;
	if (!doc || strcmp(candidate.archive_sha256, source.archive_sha256) == 0) {
		forgeLevelDocFree(doc);
		sysLogPrintf(LOG_ERROR, "SMOKE.GRID.EXTENSION: independent creator authoring failed reason='%s'", reason);
		smokeHarnessExit(1, "grid_prepared_review_creator_source");
		return;
	}
	forgeLevelDocFree(doc);
	snprintf(s_GridPreparedCreatorArenaSha, sizeof(s_GridPreparedCreatorArenaSha), "%s", candidate.archive_sha256);
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.EXTENSION: creator authored locally arena=%s sha256=%s parent=%s before_connect=1",
		saved.catalog_id, s_GridPreparedCreatorArenaSha, source.archive_sha256);
}

static s32 bootGridDraftIsHostPeer(u8 client_id)
{
	const struct netclient *client = &g_NetClients[client_id];
	return client->id == client_id && client->peer && client->state == CLSTATE_LOBBY
		&& client->room_id == s_GridDraftRoom;
}

static void bootGridDraftFail(const char *phase)
{
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.DRAFT: failed phase=%s reason=%s", phase, netGridEditError());
	smokeHarnessExit(1, phase);
}

static s32 bootGridDraftRecordInitialInvite(const grid_edit_room_invite_t *invite,
		const struct netclient *peer)
{
	if (!peer || !peer->peer || invite->identity.room_id != s_GridDraftRoom
			|| !invite->identity.generation || invite->baseline_revision != 1) return 0;
	s_GridDraftInitialInvite = *invite;
	s_GridDraftConnection = peer->peer->connectID;
	return 1;
}

static s32 bootGridDraftSameArena(const grid_edit_room_invite_t *invite)
{
	const grid_edit_room_invite_t *initial = &s_GridDraftInitialInvite;
	return invite->identity.room_id == s_GridDraftRoom && invite->baseline_revision == initial->baseline_revision
		&& invite->host_actor == initial->host_actor && strcmp(invite->arena_id, initial->arena_id) == 0
		&& strcmp(invite->arena_sha256, initial->arena_sha256) == 0;
}

static s32 bootGridDraftSameSource(const grid_edit_room_invite_t *invite, const struct netclient *peer)
{
	return peer && peer->peer && peer->peer->connectID == s_GridDraftConnection
		&& bootGridDraftSameArena(invite);
}

static s32 bootGridDraftVariantParentMatches(const char *arena_id, const forge_level_source_t *source,
		char *reason, size_t reason_cap)
{
	const grid_edit_room_invite_t *initial = &s_GridDraftInitialInvite;
	char directory[FS_MAXPATH + 1];
	forge_revision_t saved, parent;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	snprintf(saved.arena_id, sizeof(saved.arena_id), "%s", arena_id);
	snprintf(saved.digest, sizeof(saved.digest), "%s", source->archive_sha256);
	snprintf(saved.path, sizeof(saved.path), "%s", source->archive_path);
	return forgeRevisionReadParent(directory, &saved, &parent, reason, reason_cap) == FORGE_REVISION_PARENT_AVAILABLE
		&& strcmp(parent.arena_id, initial->arena_id) == 0 && strcmp(parent.digest, initial->arena_sha256) == 0;
}

static s32 bootGridDraftVerifySavedVariant(const char *arena_id)
{
	const grid_edit_room_invite_t *initial = &s_GridDraftInitialInvite;
	forge_level_source_t original_source, variant_source;
	char reason[256] = {0};
	forge_level_doc_t *original = forgeLevelOpen(initial->arena_id, &original_source, reason, sizeof(reason));
	forge_level_doc_t *variant = original ? forgeLevelOpen(arena_id, &variant_source, reason, sizeof(reason)) : NULL;
	scenario_stage_atmosphere_t before = {0}, after = {0};
	s32 same = original && variant && strcmp(original_source.archive_sha256, initial->arena_sha256) == 0
		&& forgeLevelDocGetAtmosphere(original, &before) && forgeLevelDocGetAtmosphere(variant, &after)
		&& before.has_ambient && before.ambient_light == GRID_DRAFT_SMOKE_BASE_BRIGHTNESS
		&& after.has_ambient && after.ambient_light == GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS
		&& before.has_sky && after.has_sky && memcmp(before.sky_rgb, after.sky_rgb, sizeof(before.sky_rgb)) == 0
		&& before.has_fog && after.has_fog && before.fog_start == after.fog_start && before.fog_end == after.fog_end
		&& before.primary_music[0] && strcmp(before.primary_music, after.primary_music) == 0;
	static const s32 members[] = {FORGE_LEVEL_MEMBER_PADS, FORGE_LEVEL_MEMBER_SPAWNS, FORGE_LEVEL_MEMBER_OBJECTS,
		FORGE_LEVEL_MEMBER_VOLUMES, FORGE_LEVEL_MEMBER_SETUP_FIELDS, FORGE_LEVEL_MEMBER_INTRO};
	for (size_t i = 0; same && i < ARRAYCOUNT(members); i++) {
		size_t before_size = 0, after_size = 0;
		const char *source = forgeLevelDocMemberText(original, members[i], &before_size);
		const char *saved = forgeLevelDocMemberText(variant, members[i], &after_size);
		same = source && saved && before_size && before_size == after_size && memcmp(source, saved, before_size) == 0;
	}
	if (same) same = bootGridDraftVariantParentMatches(arena_id, &variant_source, reason, sizeof(reason));
	if (same) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host reopened variant=%s ambient=%d preserved_author_data=1 parent_sha256=%s",
			arena_id, after.ambient_light, initial->arena_sha256);
	} else {
		sysLogPrintf(LOG_WARNING, "SMOKE.GRID.DRAFT: saved native document did not verify: %s", reason);
	}
	forgeLevelDocFree(variant);
	forgeLevelDocFree(original);
	return same;
}

static s32 bootGridPlaytestOffsetSpawn(grid_edit_move_spawn_t *pose)
{
	if (strcmp(pose->spawn_id, "spawn_0000") != 0) return 0;
	pose->position[0] += 24.0f;
	return 1;
}

static s32 bootGridPlaytestPoseMatches(const grid_edit_move_spawn_t *pose, const f32 position[3], const f32 look[3])
{
	for (s32 axis = 0; axis < 3; axis++) {
		if (!(fabsf(pose->position[axis] - position[axis]) <= 0.01f)
				|| !(fabsf(pose->look[axis] - look[axis]) <= 0.01f)) return 0;
	}
	return 1;
}

static s32 bootGridPlaytestSavedStartVariant(void)
{
	const grid_edit_room_invite_t *initial = &s_GridDraftInitialInvite;
	char arena_id[CATALOG_ID_LEN];
	char reason[256] = {0};
	forge_level_source_t original_source, variant_source;
	forge_level_spawn_t original_spawn, variant_spawn;
	forge_level_pad_t original_pad, variant_pad;
	grid_edit_move_spawn_t live;
	forge_level_doc_t *original = NULL, *variant = NULL;
	s32 verified = 0;
	if (!netGridEditGetSpawnPose(0, &live)
			|| !netGridEditSaveVariant("Grid_Start_Playtest_Variant", arena_id, sizeof(arena_id))
			|| strcmp(arena_id, "mod:arena_grid_start_playtest_variant") != 0
			|| !netGridEditHasSavedVariant(arena_id)) goto done;
	/* Reopen the original after saving so its digest also witnesses immutability. */
	original = forgeLevelOpen(initial->arena_id, &original_source, reason, sizeof(reason));
	variant = forgeLevelOpen(arena_id, &variant_source, reason, sizeof(reason));
	if (!original || !variant || strcmp(original_source.archive_sha256, initial->arena_sha256) != 0
			|| !forgeLevelDocGetSpawn(original, 0, &original_spawn)
			|| !forgeLevelDocGetSpawn(variant, 0, &variant_spawn)
			|| strcmp(original_spawn.spawn_id, variant_spawn.spawn_id) != 0
			|| strcmp(original_spawn.spawn_id, live.spawn_id) != 0
			|| !forgeLevelDocFindPad(original, original_spawn.pad_ref, &original_pad)
			|| !forgeLevelDocFindPad(variant, variant_spawn.pad_ref, &variant_pad)) goto done;
	snprintf(s_GridPlaytestMovedSpawn.spawn_id, sizeof(s_GridPlaytestMovedSpawn.spawn_id), "%s", original_spawn.spawn_id);
	memcpy(s_GridPlaytestMovedSpawn.position, original_pad.position, sizeof(original_pad.position));
	memcpy(s_GridPlaytestMovedSpawn.look, original_pad.look, sizeof(original_pad.look));
	if (!bootGridPlaytestOffsetSpawn(&s_GridPlaytestMovedSpawn)
			|| !bootGridPlaytestPoseMatches(&s_GridPlaytestMovedSpawn, live.position, live.look)
			|| !bootGridPlaytestPoseMatches(&s_GridPlaytestMovedSpawn, variant_pad.position, variant_pad.look)
			|| !bootGridDraftVariantParentMatches(arena_id, &variant_source, reason, sizeof(reason))) goto done;
	char extra;
	if (sscanf(variant_spawn.pad_ref, "pad_%d%c", &s_GridPlaytestMovedPad, &extra) != 1
			|| s_GridPlaytestMovedPad < 0) goto done;
	char directory[FS_MAXPATH + 1];
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	verified = forgeRevisionReadCurrent(directory, arena_id, &s_GridPlaytestVariant, reason, sizeof(reason))
		&& strcmp(s_GridPlaytestVariant.digest, variant_source.archive_sha256) == 0;
done:
	forgeLevelDocFree(variant);
	forgeLevelDocFree(original);
	if (verified) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: saved native start=%s pad=%d x=%.2f parent_sha256=%s variant_sha256=%s",
			s_GridPlaytestMovedSpawn.spawn_id, s_GridPlaytestMovedPad, s_GridPlaytestMovedSpawn.position[0],
			initial->arena_sha256, s_GridPlaytestVariant.digest);
	} else if (reason[0]) {
		sysLogPrintf(LOG_WARNING, "SMOKE.GRID.PLAYTEST: saved Variant start validation failed: %s", reason);
	}
	return verified;
}

static s32 bootGridPlaytestCheckActiveVariant(void)
{
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	char sha[SHA256_HEX_SIZE];
	if (!active || active->stage != g_StageNum || strcmp(active->arena_id, s_GridPlaytestVariant.arena_id) != 0
			|| !g_PadsFile || !g_StageSetup.padfiledata || s_GridPlaytestMovedPad >= g_PadsFile->numpads) return 0;
	sha256ToHex(active->arena_sha256, sha);
	if (strcmp(sha, s_GridPlaytestVariant.digest) != 0) return 0;
	struct pad native = {0};
	padUnpack(s_GridPlaytestMovedPad, PADFIELD_POS | PADFIELD_LOOK | PADFIELD_ROOM, &native);
	if (native.room != 1 || !bootGridPlaytestPoseMatches(&s_GridPlaytestMovedSpawn, native.pos.f, native.look.f)) return 0;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: loaded native start=%s pad=%d room=%d x=%.2f epoch=%u variant_sha256=%s",
		s_GridPlaytestMovedSpawn.spawn_id, s_GridPlaytestMovedPad, native.room, native.pos.x, g_NetStageEpoch, sha);
	return 1;
}

typedef struct boot_grid_witness_reader {
	long offset;
	u32 last_poll;
} boot_grid_witness_reader_t;

/* Smoke-only barriers from the current shared install's process logs. */
static s32 bootGridPlaytestReadWitness(boot_grid_witness_reader_t *reader, const char *path,
		const char *prefix, char *out, size_t out_cap)
{
	const u32 now = SDL_GetTicks();
	if ((u32)(now - reader->last_poll) < 100u) return 0;
	reader->last_poll = now;
	FILE *stream = fopen(path, "rb");
	if (!stream) return 0;
	s32 found = 0;
	if (fseek(stream, reader->offset, SEEK_SET) == 0) {
		char line[4096];
		for (size_t i = 0; i < 512 && fgets(line, sizeof(line), stream); i++) {
			if (!strchr(line, '\n')) break;
			long next = ftell(stream);
			if (next < 0) break;
			reader->offset = next;
			const char *start = strstr(line, prefix);
			if (start) {
				snprintf(out, out_cap, "%s", start + strlen(prefix));
				found = 1;
				break;
			}
		}
	}
	fclose(stream);
	return found;
}

static s32 bootGridPlaytestReadHostWitness(const char *prefix, char *out, size_t out_cap)
{
	static boot_grid_witness_reader_t reader;
	return bootGridPlaytestReadWitness(&reader, "./logs/game client/pd-host.log", prefix, out, out_cap);
}

static s32 bootGridPlaytestReadLight(s32 stage, s32 brightness, f32 *native, f32 *rendered)
{
	char prefix[96];
	char record[128];
	snprintf(prefix, sizeof(prefix), "GRID.PLAYTEST.LIGHT: stage=0x%02x ambient=%d ", stage, brightness);
	if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return 0;
	s32 rooms, room;
	if (sscanf(record, "rooms=%d sample_room=%d native=%f rendered=%f", &rooms, &room, native, rendered) != 4
			|| rooms < 1 || room < 1) {
		bootGridDraftFail("grid_playtest_invalid_render_witness");
		return 0;
	}
	return 1;
}

static s32 bootGridVisibilityReadCursorSpawn(void)
{
	return netGridEditGetSpawnPose(0, &s_GridVisibilityCursor)
		&& strcmp(s_GridVisibilityCursor.spawn_id, "spawn_0000") == 0
		&& __builtin_isfinite(s_GridVisibilityCursor.position[0]) && __builtin_isfinite(s_GridVisibilityCursor.position[2]);
}

static s32 bootGridVisibilityValidView(const grid_playtest_view_t *view)
{
	if (view->epoch != s_GridPlaytestEpoch || !view->serial || view->room_count != 2 || view->actor_count != 4) return 0;
	for (u8 i = 0; i < view->room_count; i++) {
		if (view->rooms[i].min_x >= view->rooms[i].max_x || view->rooms[i].min_z >= view->rooms[i].max_z) return 0;
	}
	u8 bots = 0;
	for (u8 i = 0; i < view->actor_count; i++) bots += view->actors[i].bot != 0;
	return bots == 3;
}

static s32 bootGridVisibilityCursorMatches(u16 x, u16 z)
{
	/* Use the live room bounds and the same unmodified start in both profiles. */
	if (!g_Rooms || g_Vars.roomcount != 3) return 0;
	const f32 min_x = g_Rooms[1].bbmin[0] < g_Rooms[2].bbmin[0] ? g_Rooms[1].bbmin[0] : g_Rooms[2].bbmin[0];
	const f32 max_x = g_Rooms[1].bbmax[0] > g_Rooms[2].bbmax[0] ? g_Rooms[1].bbmax[0] : g_Rooms[2].bbmax[0];
	const f32 min_z = g_Rooms[1].bbmin[2] < g_Rooms[2].bbmin[2] ? g_Rooms[1].bbmin[2] : g_Rooms[2].bbmin[2];
	const f32 max_z = g_Rooms[1].bbmax[2] > g_Rooms[2].bbmax[2] ? g_Rooms[1].bbmax[2] : g_Rooms[2].bbmax[2];
	const f32 source_x = s_GridVisibilityCursor.position[0];
	const f32 source_z = s_GridVisibilityCursor.position[2];
	if (!(source_x > min_x && source_x < max_x && source_z > min_z && source_z < max_z)) return 0;
	const s32 expected_x = (s32)((source_x - min_x) / (max_x - min_x) * UINT16_MAX);
	const s32 expected_z = (s32)((source_z - min_z) / (max_z - min_z) * UINT16_MAX);
	return abs(expected_x - x) <= 1 && abs(expected_z - z) <= 1;
}

static s32 bootGridVisibilityStable(u32 now, s32 expected)
{
	if (!expected) {
		s_GridVisibilityStable = 0;
		return 0;
	}
	if (!s_GridVisibilityStable) s_GridVisibilityStable = now;
	return (u32)(now - s_GridVisibilityStable) >= GRID_VISIBILITY_SMOKE_HOLD_MS;
}

static void bootGridVisibilityHostTick(u32 now)
{
	static boot_grid_witness_reader_t creator_log;
	const unsigned phase = s_GridVisibilityCase;
	const unsigned players = s_GridVisibilityCases[phase].players;
	const unsigned creators = s_GridVisibilityCases[phase].creators;
	const unsigned long long generation = s_GridDraftInitialInvite.identity.generation;
	if (!s_GridVisibilityStarted) {
		s32 old_players, old_creators;
		if (!netGridEditGetPlaytestVisibility(&old_players, &old_creators)
				|| (!phase && (old_players || old_creators))
				|| !netGridEditSetPlaytestVisibility(players, creators)) {
			bootGridDraftFail("grid_view_policy");
			return;
		}
		s_GridVisibilityStarted = now;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VIEW: policy room=%u generation=%llu epoch=%u phase=%u players=%u creators=%u",
			(unsigned)s_GridDraftRoom, generation, s_GridPlaytestEpoch, phase, players, creators);
	}
	if ((u32)(now - s_GridVisibilityStarted) >= GRID_VISIBILITY_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_view_host_timeout");
		return;
	}

	grid_playtest_view_t view;
	u16 x = 0, z = 0;
	const s32 visible = netGridEditGetCreatorCursor(&view, &x, &z);
	if ((visible && !players) || (visible && (!bootGridVisibilityValidView(&view) || !bootGridVisibilityCursorMatches(x, z)))) {
		bootGridDraftFail("grid_view_host_cursor");
		return;
	}
	if (!s_GridVisibilityPeerVerified) {
		char prefix[150], record[64];
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.VIEW: creator room=%u generation=%llu epoch=%u phase=%u ",
			(unsigned)s_GridDraftRoom, generation, s_GridPlaytestEpoch, phase);
		if (bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log", prefix, record, sizeof(record))) {
			unsigned confirmed;
			if (sscanf(record, "view=%u", &confirmed) != 1 || confirmed != creators) {
				bootGridDraftFail("grid_view_creator_witness");
				return;
			}
			s_GridVisibilityPeerVerified = 1;
		}
	}
	if (!bootGridVisibilityStable(now, visible == (s32)players) || !s_GridVisibilityPeerVerified) return;

	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VIEW: host verified epoch=%u phase=%u cursor=%u view=%u x=%u z=%u",
		s_GridPlaytestEpoch, phase, players, creators, (unsigned)x, (unsigned)z);
	s_GridVisibilityStarted = s_GridVisibilityStable = 0;
	s_GridVisibilityPeerVerified = 0;
	if (++s_GridVisibilityCase == ARRAYCOUNT(s_GridVisibilityCases)) {
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
		mainEndStage();
	}
}

static void bootGridVisibilityCreatorTick(const grid_edit_room_state_t *draft)
{
	if (!draft->ready || draft->revision != 2 || !draft->enabled || draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
			|| !bootGridDraftSameSource(&draft->invite, g_NetLocalClient)
			|| draft->invite.identity.generation != s_GridDraftInitialInvite.identity.generation) {
		bootGridDraftFail("grid_view_creator_draft");
		return;
	}
	netGridEditSetCreatorCursor(s_GridVisibilityCursor.position);
	const u32 now = SDL_GetTicks();
	const unsigned phase = s_GridVisibilityCase;
	const unsigned creators = s_GridVisibilityCases[phase].creators;
	const unsigned long long generation = s_GridDraftInitialInvite.identity.generation;
	if (!s_GridVisibilityStarted) {
		char prefix[150], record[64];
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.VIEW: policy room=%u generation=%llu epoch=%u phase=%u ",
			(unsigned)s_GridDraftRoom, generation, s_GridPlaytestEpoch, phase);
		if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
		unsigned expected_players, expected_creators;
		if (sscanf(record, "players=%u creators=%u", &expected_players, &expected_creators) != 2
				|| expected_players != s_GridVisibilityCases[phase].players || expected_creators != creators) {
			bootGridDraftFail("grid_view_invalid_policy_witness");
			return;
		}
		s_GridVisibilityStarted = now;
	}
	if ((u32)(now - s_GridVisibilityStarted) >= GRID_VISIBILITY_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_view_creator_timeout");
		return;
	}

	grid_playtest_view_t view = {0};
	const s32 visible = netGridEditGetPlaytestView(&view);
	/* The last case can first see the previous frame while revocation travels. */
	if (visible && ((!creators && phase < 2) || !bootGridVisibilityValidView(&view))) {
		bootGridDraftFail("grid_view_invalid_frame");
		return;
	}
	if (!bootGridVisibilityStable(now, creators ? visible && view.serial > s_GridVisibilityLastView : !visible)) return;

	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VIEW: creator room=%u generation=%llu epoch=%u phase=%u view=%u rooms=%u actors=%u serial=%u lobby=1",
		(unsigned)s_GridDraftRoom, generation, s_GridPlaytestEpoch, phase, creators,
		(unsigned)view.room_count, (unsigned)view.actor_count, view.serial);
	if (visible) s_GridVisibilityLastView = view.serial;
	s_GridVisibilityStarted = s_GridVisibilityStable = 0;
	if (++s_GridVisibilityCase == ARRAYCOUNT(s_GridVisibilityCases)) {
		netGridEditSetCreatorCursor(NULL);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
	}
}

static s32 bootGridDecorationClose(f32 a, f32 b)
{
	const f32 difference = a - b;
	return difference >= -0.05f && difference <= 0.05f;
}

static s32 bootGridDecorationDraftMatches(f32 x, f32 z)
{
	grid_edit_move_decoration_t pose;
	return netGridEditDecorationCount() == 1 && netGridEditGetDecorationPose(0, &pose)
		&& strcmp(pose.record_id, s_GridDecorationOriginal.record_id) == 0
		&& bootGridDecorationClose(pose.x, x) && bootGridDecorationClose(pose.z, z);
}

static struct prop *bootGridDecorationLoadedProp(u32 *command)
{
	const grid_match_runtime_identity_t *arena = gridMatchRuntimeActive();
	if (!arena || arena->stage != g_StageNum || arena->offer.room_id != s_GridDraftRoom
			|| strcmp(arena->arena_id, s_GridDraftInitialInvite.arena_id) != 0
			|| !scenarioSourceGridDecorationCommand(arena->scenario_id, g_StageSetup.props,
				s_GridDecorationOriginal.record_id, command)) return NULL;
	struct defaultobj *obj = setupGetObjByCmdIndex(*command);
	struct prop *prop = obj && obj->type == OBJTYPE_BASIC ? obj->prop : NULL;
	return prop && prop->obj == obj && prop->type == PROPTYPE_OBJ && (prop->flags & PROPFLAG_ENABLED)
		&& !prop->colmesh && prop->rooms[0] > 0 && prop->rooms[1] == -1 ? prop : NULL;
}

static void bootGridRoomExtensionStage(const grid_edit_room_state_t *draft);

static s32 bootGridLayoutAmbientIsOff(void)
{
	s32 enabled, brightness;
	return netGridEditPrivatePlaytestAmbientState(&enabled, &brightness) && !enabled && !brightness;
}

static s32 bootGridLayoutDecorationWitness(const grid_edit_room_state_t *draft, u64 layout,
		const char *phase)
{
	u32 command;
	const struct prop *prop = bootGridDecorationLoadedProp(&command);
	const struct coord *moved = &s_GridDecorationNative.moved_position;
	if (!prop || prop != s_GridDecorationNative.prop || command != s_GridDecorationNative.command
			|| propGetLifecycleGeneration(prop) != s_GridDecorationNative.lifecycle
			|| prop->rooms[0] != s_GridDecorationNative.room
			|| prop->pos.x != moved->x || prop->pos.y != moved->y || prop->pos.z != moved->z
			|| g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage
			|| gridMatchRuntimePrivateLayoutGeneration() != layout
			|| draft->revision != (s_GridDraftLayoutAmbientOff ? 4u : 3u)
			|| (s_GridDraftLayoutAmbientOff && !bootGridLayoutAmbientIsOff())
			|| !bootGridDecorationDraftMatches(moved->x, moved->z)) return 0;
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.LAYOUT.DECORATION: %s room=%u epoch=%u layout=%llu revision=%llu record=%s command=%u prop_room=%d lifecycle=%llu x=%.1f y=%.1f z=%.1f",
		phase, (unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)layout,
		(unsigned long long)draft->revision, s_GridDecorationOriginal.record_id, command, (s32)prop->rooms[0],
		(unsigned long long)s_GridDecorationNative.lifecycle, prop->pos.x, prop->pos.y, prop->pos.z);
	return 1;
}

static void bootGridDecorationRelease(const char *phase)
{
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: release_%s room=%u generation=%llu epoch=%u record=%s revision=2",
		phase, (unsigned)s_GridDraftRoom, (unsigned long long)s_GridDraftInitialInvite.identity.generation,
		s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
}

static s32 bootGridDecorationStartHost(u32 now)
{
	if (netGridEditDecorationCount() != 1 || !netGridEditGetDecorationPose(0, &s_GridDecorationOriginal)) return 0;
	struct prop *prop = bootGridDecorationLoadedProp(&s_GridDecorationNative.command);
	if (!prop || !bootGridDecorationClose(prop->pos.x, s_GridDecorationOriginal.x)
			|| !bootGridDecorationClose(prop->pos.z, s_GridDecorationOriginal.z)) return 0;
	s_GridDecorationNative.lifecycle = propGetLifecycleGeneration(prop);
	if (!s_GridDecorationNative.lifecycle) return 0;
	s_GridDecorationNative.prop = prop;
	s_GridDecorationNative.room = prop->rooms[0];
	s_GridDecorationNative.position = prop->pos;
	if (s_GridDraftDecorationOnPlan && (prop->rooms[0] != 1 || !netGridEditSetPlaytestVisibility(0, 1))) return 0;
	bootGridDecorationRelease("invalid");
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION;
	s_GridDraftStepTick = now;
	return 1;
}

static void bootGridDecorationHostTick(const grid_edit_room_state_t *draft, u32 now)
{
	static boot_grid_witness_reader_t creator_log;
	u32 command;
	const struct prop *prop = bootGridDecorationLoadedProp(&command);
	if (g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage || !draft->ready
			|| !draft->enabled || draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
			|| !prop || command != s_GridDecorationNative.command
			|| propGetLifecycleGeneration(prop) != s_GridDecorationNative.lifecycle
			|| prop->rooms[0] != s_GridDecorationNative.room || prop->pos.y != s_GridDecorationNative.position.y
			|| (u32)(now - s_GridDraftStepTick) >= GRID_DECORATION_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_decoration_host_native_identity");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION) {
		if (draft->revision != 2 || !bootGridDecorationDraftMatches(s_GridDecorationOriginal.x, s_GridDecorationOriginal.z)
				|| prop->pos.x != s_GridDecorationNative.position.x || prop->pos.z != s_GridDecorationNative.position.z) {
			bootGridDraftFail("grid_decoration_rejection_changed_state");
			return;
		}
		char prefix[180], record[64];
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.DECORATION: creator refused room=%u generation=%llu epoch=%u record=%s ",
			(unsigned)s_GridDraftRoom, (unsigned long long)s_GridDraftInitialInvite.identity.generation,
			s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
		if (!bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log", prefix, record, sizeof(record))) return;
		if (strncmp(record, "revision=2 next_sequence=2 intact=1 lobby=1",
				sizeof("revision=2 next_sequence=2 intact=1 lobby=1") - 1) != 0) {
			bootGridDraftFail("grid_decoration_refusal_witness");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: verified refusal epoch=%u record=%s revision=2 intact=1",
			s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
		bootGridDecorationRelease("valid");
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE;
		s_GridDraftStepTick = now;
		return;
	}
	if (draft->revision == 2) return;
	const f32 x = s_GridDecorationOriginal.x + GRID_DECORATION_SMOKE_X_DELTA;
	const f32 z = s_GridDecorationOriginal.z;
	if (draft->revision != 3 || draft->can_undo_decoration || !bootGridDecorationDraftMatches(x, z)) {
		bootGridDraftFail("grid_decoration_host_move_commit");
		return;
	}
	if (!bootGridDecorationClose(prop->pos.x, x) || !bootGridDecorationClose(prop->pos.z, z)) return;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: native moved epoch=%u record=%s command=%u room=%d lifecycle=%llu x=%.1f z=%.1f",
		s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id, command, prop->rooms[0],
		(unsigned long long)s_GridDecorationNative.lifecycle, prop->pos.x, prop->pos.z);
	if (s_GridDraftLayoutDecoration) {
		s_GridDecorationNative.moved_position = prop->pos;
		if (s_GridDraftLayoutAmbientOff) {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LAYOUT_AMBIENT_OFF;
			s_GridDraftStepTick = now;
		} else {
			bootGridRoomExtensionStage(draft);
		}
		return;
	}
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
	mainEndStage();
}

static void bootGridLayoutAmbientOffTick(const grid_edit_room_state_t *draft, u32 now)
{
	if (!draft->ready || draft->revision < 3 || draft->revision > 4
			|| g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage
			|| (u32)(now - s_GridDraftStepTick) >= GRID_DECORATION_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_layout_ambient_off_scope");
		return;
	}
	if (draft->revision == 3) {
		static boot_grid_witness_reader_t creator_log;
		char prefix[180], line[32];
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.DECORATION: creator accepted epoch=%u record=%s revision=3 undo=1",
			s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
		if (!bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log", prefix, line, sizeof(line))) return;
		if (!netGridEditSetAmbient(0, 0)) bootGridDraftFail("grid_layout_disable_ambient");
		return;
	}
	if (draft->enabled || draft->brightness) {
		bootGridDraftFail("grid_layout_ambient_off_draft");
		return;
	}
	if (!bootGridLayoutAmbientIsOff()) return;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LAYOUT.AMBIENT: disabled room=%u epoch=%u revision=4",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
	bootGridRoomExtensionStage(draft);
}

static void bootGridDecorationCreatorTick(const grid_edit_room_state_t *draft)
{
	const u32 now = SDL_GetTicks();
	if (!draft->ready || !draft->enabled || draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
			|| !bootGridDraftSameSource(&draft->invite, g_NetLocalClient)
			|| draft->invite.identity.generation != s_GridDraftInitialInvite.identity.generation
			|| (u32)(now - s_GridDraftStepTick) >= GRID_DECORATION_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_decoration_creator_state");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION) {
		if (draft->pending_command) return;
		if (draft->revision != 2 || draft->next_sequence != 2 || draft->can_undo_decoration
				|| strcmp(netGridEditError(), "The host refused this Shared Grid edit.") != 0
				|| !bootGridDecorationDraftMatches(s_GridDecorationOriginal.x, s_GridDecorationOriginal.z)) {
			bootGridDraftFail("grid_decoration_creator_refusal");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: creator refused room=%u generation=%llu epoch=%u record=%s revision=2 next_sequence=2 intact=1 lobby=1",
			(unsigned)s_GridDraftRoom, (unsigned long long)s_GridDraftInitialInvite.identity.generation,
			s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_DECORATION_VALID;
		s_GridDraftStepTick = now;
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE) {
		if (draft->pending_command) return;
		const grid_edit_move_decoration_t *undo = &draft->previous_decoration;
		if (draft->revision != 3 || draft->next_sequence != 3 || !draft->can_undo_decoration
				|| strcmp(undo->record_id, s_GridDecorationOriginal.record_id) != 0
				|| undo->x != s_GridDecorationOriginal.x || undo->z != s_GridDecorationOriginal.z
				|| !bootGridDecorationDraftMatches(s_GridDecorationOriginal.x + GRID_DECORATION_SMOKE_X_DELTA,
					s_GridDecorationOriginal.z)) {
			bootGridDraftFail("grid_decoration_creator_commit");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: creator accepted epoch=%u record=%s revision=3 undo=1",
			s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
		if (s_GridDraftLayoutDecoration) {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW;
			s_GridDraftStepTick = now;
		} else {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
		}
		return;
	}
	const s32 invalid = s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_START;
	grid_playtest_view_t view;
	grid_playtest_target_t target;
	if (s_GridDraftDecorationOnPlan) {
		if (!netGridEditGetPlaytestView(&view) || !view.has_world_bounds) return;
		if (!bootGridVisibilityValidView(&view) || view.layout_generation != 0) {
			bootGridDraftFail("grid_decoration_plan_identity");
			return;
		}
		target = (grid_playtest_target_t){ view.epoch, view.layout_generation };
	}
	char prefix[180], record[16];
	snprintf(prefix, sizeof(prefix), "SMOKE.GRID.DECORATION: release_%s room=%u generation=%llu epoch=%u record=%s revision=2",
		invalid ? "invalid" : "valid", (unsigned)s_GridDraftRoom,
		(unsigned long long)s_GridDraftInitialInvite.identity.generation, s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id);
	if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
	grid_edit_move_decoration_t move = s_GridDecorationOriginal;
	if (draft->revision != 2 || draft->next_sequence != 2 || draft->pending_command) {
		bootGridDraftFail("grid_decoration_creator_send_state");
		return;
	}
	if (invalid && s_GridDraftDecorationOnPlan) {
		grid_playtest_target_t stale = target;
		stale.layout_generation++;
		grid_edit_room_state_t after;
		if (netGridEditMoveDecorationOnPlan(&move, &stale)
				|| strcmp(netGridEditError(), "The live plan changed. Choose the position again on the current plan.") != 0
				|| !netGridEditGetState(&after) || after.pending_command || after.next_sequence != draft->next_sequence
				|| after.revision != draft->revision || !bootGridDecorationDraftMatches(move.x, move.z)) {
			bootGridDraftFail("grid_decoration_stale_target_changed_draft");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: rejected stale plan epoch=%u shown_layout=0 stale_layout=1 revision=2 next_sequence=2",
			target.epoch);
		const grid_playtest_view_room_t *other = &view.rooms[1];
		const f32 x = ((f32)other->min_x + other->max_x) * (0.5f / UINT16_MAX);
		const f32 z = ((f32)other->min_z + other->max_z) * (0.5f / UINT16_MAX);
		move.x = view.min_x + (view.max_x - view.min_x) * x;
		move.z = view.min_z + (view.max_z - view.min_z) * z;
	} else {
		move.x += invalid ? 100000.0f : GRID_DECORATION_SMOKE_X_DELTA;
	}
	const s32 sent = s_GridDraftDecorationOnPlan
		? netGridEditMoveDecorationOnPlan(&move, &target) : netGridEditMoveDecoration(&move);
	if (!sent) {
		bootGridDraftFail("grid_decoration_creator_send");
		return;
	}
	const char *destination = invalid ? (s_GridDraftDecorationOnPlan ? "other_room" : "out_of_bounds") : "same_room";
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: creator sent %s epoch=%u record=%s revision=2%s",
		destination, s_GridPlaytestEpoch, move.record_id, s_GridDraftDecorationOnPlan ? " plan=1 layout=0" : "");
	s_GridDraftStep = invalid ? BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION : BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE;
	s_GridDraftStepTick = now;
}

static void bootGridDraftHostCommitResumeTail(const grid_edit_room_state_t *draft,
		const struct netclient *creator)
{
	if (!draft->is_host || !draft->pending_resume || !draft->snapshot_ready || draft->ready
			|| draft->peer_actor != s_GridDraftResumeActor
			|| draft->invite.identity.generation != s_GridDraftResumeGeneration
			|| !bootGridDraftSameSource(&draft->invite, creator)) {
		bootGridDraftFail("grid_draft_host_missed_resume_tail");
		return;
	}
	if (!draft->can_edit) return;
	/* The snapshot pump precedes receive dispatch. This tick can commit
	 * after confirmation and before the guest can acknowledge the snapshot. */
	grid_edit_room_state_t committed;
	if (!netGridEditSetAmbient(1, GRID_DRAFT_SMOKE_RESUME_TAIL_BRIGHTNESS)
			|| !netGridEditGetState(&committed) || !committed.can_edit || !committed.pending_resume || committed.ready
			|| committed.revision != 3 || !committed.enabled || committed.brightness != GRID_DRAFT_SMOKE_RESUME_TAIL_BRIGHTNESS
			|| committed.peer_actor != s_GridDraftResumeActor
			|| committed.invite.identity.generation != s_GridDraftResumeGeneration) {
		bootGridDraftFail("grid_draft_host_resume_tail_commit");
		return;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host resume_tail_commit revision=%llu ambient=%d generation=%llu can_edit=%d pending_resume=%d ready=%d",
			(unsigned long long)committed.revision, committed.brightness, (unsigned long long)committed.invite.identity.generation,
			committed.can_edit, committed.pending_resume, committed.ready);
}

static s32 bootGridRoomExtensionBounds(f32 second_max);

static u16 bootGridLayoutProjectionSeam(u32 room2_max)
{
	/* The managed arena spans -2100 to room2_max on X, with the portal at zero. */
	return (u16)((2100u * UINT16_MAX) / (2100u + room2_max));
}

static s32 bootGridLayoutProjectionMatches(const grid_playtest_view_t *view, u32 room2_max)
{
	const u16 seam = bootGridLayoutProjectionSeam(room2_max);
	if (view->epoch != s_GridPlaytestEpoch || !view->serial || view->room_count != 2 || view->actor_count != 4
			|| view->rooms[0].min_x != 0 || view->rooms[0].max_x != seam
			|| view->rooms[1].min_x != seam || view->rooms[1].max_x != UINT16_MAX) return 0;
	for (u8 i = 0; i < view->room_count; ++i) {
		if (view->rooms[i].min_z != 0 || view->rooms[i].max_z != UINT16_MAX) return 0;
	}
	u8 bots = 0;
	for (u8 i = 0; i < view->actor_count; ++i) bots += view->actors[i].bot;
	return bots == 3;
}

static s32 bootGridLayoutProjectionHidden(void)
{
	grid_playtest_view_t view;
	grid_edit_room_control_t receipt;
	return !netGridEditGetPlaytestView(&view) && !netGridEditGetObservedLayout(&receipt);
}

static s32 bootGridLayoutReconnectObserve(const grid_edit_room_state_t *draft, s32 resumed)
{
	grid_edit_room_control_t receipt;
	grid_playtest_view_t view;
	if (!netGridEditGetObservedLayout(&receipt) || !netGridEditGetPlaytestView(&view)) return 0;
	const u64 layout = s_GridLayoutSmoke.second ? 2 : 1;
	const u32 room2_max = s_GridLayoutSmoke.second ? 2800 : 3000;
	const u64 generation = resumed ? s_GridDraftResumeGeneration : s_GridDraftInitialInvite.identity.generation;
	grid_edit_variant_review_info_t review;
	if (receipt.kind != GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE || receipt.apply_result
			|| receipt.invite.identity.room_id != s_GridDraftRoom || receipt.invite.identity.generation != generation
			|| receipt.apply_previous_layout_generation != layout - 1 || receipt.apply_layout_generation != layout
			|| !gridEditVariantReviewMatches(&receipt.review, &s_GridRoomExtensionOffer)
			|| !bootGridLayoutProjectionMatches(&view, room2_max) || netGridEditGetStagedReview(&review)
			|| assetCatalogGetGeneration() != s_GridLayoutReconnect.catalog
			|| assetCatalogResolveAny(receipt.review.arena_id) || !gridMatchRuntimeIsIdle()
			|| g_NetStageEpoch || g_NetLocalClient->stage_ready) {
		bootGridDraftFail("grid_layout_reconnect_projection_or_scope");
		return 0;
	}
	if (resumed) {
		grid_edit_room_control_t applied = s_GridLayoutReconnect.before_disconnect;
		if (applied.invite.identity.generation >= generation) {
			bootGridDraftFail("grid_layout_reconnect_old_draft_identity");
			return 0;
		}
		applied.invite.identity = draft->invite.identity;
		if (!gridEditLayoutApplyMatches(&applied, &receipt)) {
			bootGridDraftFail("grid_layout_reconnect_baseline_changed");
			return 0;
		}
	} else {
		s_GridLayoutReconnect.before_disconnect = receipt;
	}
	char arena_sha[SHA256_HEX_SIZE], manifest_sha[SHA256_HEX_SIZE];
	sha256ToHex(receipt.review.arena_sha256, arena_sha);
	sha256ToHex(receipt.apply_manifest_sha256, manifest_sha);
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.LAYOUT.RECONNECT: %s room=%u epoch=%u layout=%llu draft=%llu actor=%llu serial=%llu arena_sha256=%s manifest_sha256=%s rooms=%u actors=%u seam_x=%u",
		resumed ? "restored" : "applied", (unsigned)s_GridDraftRoom, view.epoch, (unsigned long long)layout,
		(unsigned long long)generation, (unsigned long long)draft->local_actor,
		(unsigned long long)receipt.review.offer_serial, arena_sha, manifest_sha,
		(unsigned)view.room_count, (unsigned)view.actor_count, (unsigned)view.rooms[0].max_x);
	return 1;
}

static void bootGridLiveReconnectHostTick(const grid_edit_room_state_t *draft, u32 now)
{
	if (!draft->is_host || !bootGridDraftSameArena(&draft->invite)
			|| (u32)(now - s_GridDraftStepTick) >= GRID_LIVE_RECONNECT_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_live_reconnect_host_draft");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN
			|| s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_LIVE_RETIRED) {
		if (!draft->ready || draft->revision != 4 || !draft->enabled
				|| draft->brightness != GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS
				|| draft->invite.identity.generation != s_GridDraftResumeGeneration) {
			bootGridDraftFail("grid_live_reconnect_return_draft");
			return;
		}
		if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN) {
			if (g_NetStageEpoch || !g_MainIsEndscreen || g_NetLocalClient->state != CLSTATE_LOBBY) return;
			extern void pdguiEndscreenExitToRoom(void);
			extern void pdguiSetInRoom(s32 inRoom);
			pdguiEndscreenExitToRoom();
			pdguiSetInRoom(1);
			netSendLobbyResync();
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_RETIRED;
			return;
		}
		if (mainGridRoomReturnPending() || !mainGridRoomBackdropActive() || !gridMatchRuntimeIsIdle()
				|| netGridEditPrivatePlaytestRoom(s_GridDraftRoom)) return;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: returned room=%u epoch=%u revision=4 ambient=%d",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, draft->brightness);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
		s_GridDraftStepTick = now;
		return;
	}

	const grid_match_runtime_identity_t *arena = gridMatchRuntimeActive();
	if (!arena || arena->offer.room_id != s_GridDraftRoom || arena->stage != s_GridPlaytestStage
			|| strcmp(arena->arena_id, s_GridDraftInitialInvite.arena_id) != 0
			|| g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage
			|| !netGridEditPrivatePlaytestRoom(s_GridDraftRoom)) {
		bootGridDraftFail("grid_live_reconnect_world_changed");
		return;
	}
	char digest[SHA256_HEX_SIZE];
	sha256ToHex(arena->arena_sha256, digest);
	if (strcmp(digest, s_GridDraftInitialInvite.arena_sha256) != 0) {
		bootGridDraftFail("grid_live_reconnect_world_revision");
		return;
	}
	if (s_GridDraftLayoutReconnect && (gridMatchRuntimePrivateLayoutGeneration() != (s_GridLayoutSmoke.second ? 2u : 1u)
			|| !bootGridRoomExtensionBounds(s_GridLayoutSmoke.second ? 2800.0f : 3000.0f)
			|| g_StageIndex != s_GridLayoutSmoke.stage_index || g_Vars.players[0] != s_GridLayoutSmoke.player
			|| memcmp(g_MpAllChrPtrs, s_GridLayoutSmoke.actors, sizeof(s_GridLayoutSmoke.actors))
			|| assetCatalogGetGeneration() != s_GridRoomExtensionCatalog
			|| assetCatalogResolveAny(s_GridRoomExtension.arena_id))) {
		bootGridDraftFail("grid_layout_reconnect_host_layout_changed");
		return;
	}
	struct netclient *creator = &g_NetClients[1];
	switch (s_GridDraftStep) {
	case BOOT_GRID_DRAFT_WAIT_LIVE_DETACH:
		if (creator->state != CLSTATE_DISCONNECTED || creator->peer || !draft->creator_detached) return;
		if (draft->ready || draft->peer_actor || draft->revision != 2
				|| draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
				|| draft->invite.identity.generation != s_GridDraftInitialInvite.identity.generation
				|| !netGridEditPrivateResumeRoomOpen(s_GridDraftRoom)) {
			bootGridDraftFail("grid_live_reconnect_detach_state");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: detached room=%u epoch=%u revision=2 ambient=%d",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, draft->brightness);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_AUTH;
		return;
	case BOOT_GRID_DRAFT_WAIT_LIVE_AUTH:
		if (creator->state != CLSTATE_LOBBY || !creator->peer || creator->room_id != 0xff) return;
		if (creator->peer->connectID == s_GridDraftConnection || netGridEditCanInvite(1)
				|| !netGridEditInviteToResumeRoom(1)) {
			bootGridDraftFail("grid_live_reconnect_room_invite");
			return;
		}
		s_GridDraftConnection = creator->peer->connectID;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: room_invite room=%u epoch=%u client=1 fresh_connection=1",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_ROOM;
		return;
	case BOOT_GRID_DRAFT_WAIT_LIVE_ROOM: {
		if (!bootGridDraftIsHostPeer(1)) return;
		grid_edit_room_state_t offered;
		if (creator->stage_ready || !draft->creator_detached || draft->ready
				|| !bootGridDraftSameSource(&draft->invite, creator)
				|| !netGridEditInvite(1, s_GridDraftInitialInvite.arena_id, s_GridDraftInitialInvite.arena_sha256)
				|| !netGridEditGetState(&offered) || !offered.pending_resume
				|| offered.invite.identity.generation <= s_GridDraftInitialInvite.identity.generation
				|| offered.peer_actor <= s_GridDraftInitialActor) {
			bootGridDraftFail("grid_live_reconnect_draft_invite");
			return;
		}
		s_GridDraftResumeGeneration = offered.invite.identity.generation;
		s_GridDraftResumeActor = offered.peer_actor;
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.LIVE_RECONNECT: draft_invite room=%u epoch=%u generation=%llu actor=%llu source_sha256=%s",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)s_GridDraftResumeGeneration,
			(unsigned long long)s_GridDraftResumeActor, offered.invite.arena_sha256);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_EDIT;
		return;
	}
	case BOOT_GRID_DRAFT_WAIT_LIVE_EDIT: {
		if (draft->revision == 2) {
			bootGridDraftHostCommitResumeTail(draft, creator);
			return;
		}
		if (!draft->ready) return;
		if (!bootGridDraftSameSource(&draft->invite, creator) || creator->stage_ready
				|| draft->creator_detached || draft->pending_resume || netGridEditPrivateResumeRoomOpen(s_GridDraftRoom)
				|| draft->peer_actor != s_GridDraftResumeActor
				|| draft->invite.identity.generation != s_GridDraftResumeGeneration) {
			bootGridDraftFail("grid_live_reconnect_grant_state");
			return;
		}
		s32 ambient;
		f32 native, rendered;
		if (draft->revision != 4 || !netGridEditPrivatePlaytestAmbient(&ambient)
				|| ambient != GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS
				|| !bootGridPlaytestReadLight(g_StageNum, ambient, &native, &rendered)) return;
		if (!draft->enabled || draft->brightness != ambient || rendered <= s_GridPlaytestBaseline) {
			bootGridDraftFail("grid_live_reconnect_render");
			return;
		}
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.LIVE_RECONNECT: rendered resumed edit room=%u epoch=%u revision=4 ambient=%d before=%.1f after=%.1f",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, ambient, s_GridPlaytestBaseline, rendered);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
		mainEndStage();
		return;
	}
	default:
		bootGridDraftFail("grid_live_reconnect_host_step");
	}
}

static s32 bootGridRoomExtensionBounds(f32 second_max)
{
	if (!g_Rooms || g_Vars.roomcount != 3) return 0;
	for (s32 room = 1; room <= 2; room++) {
		const struct room *bounds = &g_Rooms[room];
		if (bounds->bbmin[1] != 0.0f || bounds->bbmax[1] != 600.0f
				|| bounds->bbmin[2] != -900.0f || bounds->bbmax[2] != 900.0f) return 0;
	}
	return g_Rooms[1].bbmin[0] == -2100.0f && g_Rooms[1].bbmax[0] == 0.0f
		&& g_Rooms[1].centre.x == -1050.0f
		&& g_Rooms[2].bbmin[0] == 0.0f && g_Rooms[2].bbmax[0] == second_max
		&& g_Rooms[2].centre.x == second_max / 2.0f;
}

static s32 bootGridRoomExtensionCandidateMatches(void)
{
	grid_edit_staged_variant_info_t current;
	return netGridEditGetStagedVariant(&current)
		&& current.draft_revision == s_GridRoomExtension.draft_revision
		&& current.current_revision == s_GridRoomExtension.current_revision
		&& !strcmp(current.arena_id, s_GridRoomExtension.arena_id)
		&& !strcmp(current.arena_sha256, s_GridRoomExtension.arena_sha256);
}

static s32 bootGridRoomExtensionOriginalWorld(const grid_match_runtime_identity_t *initial, u32 catalog)
{
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	return active && !gridMatchRuntimePending() && g_NetStageEpoch == s_GridPlaytestEpoch
		&& g_StageNum == s_GridPlaytestStage && g_NetLocalClient->state == CLSTATE_GAME
		&& active->offer.room_id == s_GridDraftRoom && active->offer.generation == initial->offer.generation
		&& !strcmp(active->offer.manifest_digest, initial->offer.manifest_digest)
		&& !strcmp(active->arena_id, initial->arena_id)
		&& !memcmp(active->arena_sha256, initial->arena_sha256, sizeof(active->arena_sha256))
		&& assetCatalogGetGeneration() == catalog
		&& bootGridRoomExtensionBounds(s_GridLayoutSmoke.second ? 3000.0f : 2700.0f);
}

static s32 bootGridRoomExtensionReviewMatches(const grid_edit_variant_review_t *review,
		const grid_edit_room_state_t *draft)
{
	char parent[SHA256_HEX_SIZE];
	sha256ToHex(review->parent_sha256, parent);
	return review->offer_serial && review->draft_revision == draft->revision
		&& review->stage_epoch == s_GridPlaytestEpoch && review->edited_room == 2
		&& review->extension == (s_GridLayoutSmoke.second ? 100 : 300)
		&& (!s_GridLayoutSmoke.second || review->offer_serial > s_GridLayoutSmoke.first_serial)
		&& !strcmp(review->arena_id, s_GridLayoutSmoke.second ? kGridLayoutRelocationArena : "mod:arena_grid_extension_staged")
		&& !strcmp(parent, draft->invite.arena_sha256);
}

static void bootGridRoomExtensionStage(const grid_edit_room_state_t *draft)
{
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	if (!active || !bootGridRoomExtensionBounds(2700.0f)) {
		bootGridDraftFail("grid_extension_original_bounds");
		return;
	}
	const grid_match_runtime_identity_t initial = *active;
	const u32 catalog = assetCatalogGetGeneration();
	char parent_sha[SHA256_HEX_SIZE];
	sha256ToHex(initial.arena_sha256, parent_sha);
	char arena[CATALOG_ID_LEN];
	if (strcmp(initial.arena_id, draft->invite.arena_id) || strcmp(parent_sha, draft->invite.arena_sha256)
			|| !netGridEditStageRoomExtension("Grid Extension Staged", 2, 300, arena, sizeof(arena))
			|| strcmp(arena, "mod:arena_grid_extension_staged")
			|| !netGridEditGetStagedVariant(&s_GridRoomExtension)
			|| strcmp(arena, s_GridRoomExtension.arena_id)
			|| !strcmp(parent_sha, s_GridRoomExtension.arena_sha256)
			|| s_GridRoomExtension.draft_revision != draft->revision
			|| s_GridRoomExtension.current_revision != draft->revision
			|| assetCatalogResolveAny(arena) || !bootGridRoomExtensionOriginalWorld(&initial, catalog)) {
		bootGridDraftFail("grid_extension_stage_changed_world");
		return;
	}
	char rejected[CATALOG_ID_LEN];
	if (netGridEditStageRoomExtension("Grid Extension Too Long", 2, 1400, rejected, sizeof(rejected))
			|| !strstr(netGridEditError(), "The extended room would exceed 40 m.")
			|| !bootGridRoomExtensionCandidateMatches() || !bootGridRoomExtensionOriginalWorld(&initial, catalog)) {
		bootGridDraftFail("grid_extension_refusal_changed_world");
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.EXTENSION: resident unchanged epoch=%u catalog=%u revision=%llu room1_max=%.0f room2_max=%.0f refused_length=1400 sha256=%s",
		s_GridPlaytestEpoch, catalog, (unsigned long long)draft->revision, g_Rooms[1].bbmax[0],
		g_Rooms[2].bbmax[0], s_GridRoomExtension.arena_sha256);

	/* The directed review is part of the creator's authorized playtest view. */
	if (!netGridEditSetPlaytestVisibility(0, 1)) {
		bootGridDraftFail("grid_extension_review_visibility");
		return;
	}
	s_GridRoomExtensionWorld = initial;
	s_GridRoomExtensionCatalog = catalog;
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW;
	s_GridDraftStepTick = SDL_GetTicks();
}

static s32 bootGridRoomTransferCreatorObserved(const grid_edit_room_state_t *draft,
	const grid_edit_variant_review_t *review)
{
	static boot_grid_witness_reader_t creator_log;
	char prefix[320], record[384];
	snprintf(prefix, sizeof(prefix),
		"SMOKE.GRID.EXTENSION: creator transferred room=%u generation=%llu epoch=%u revision=%llu serial=%llu sha256=%s installed=0",
		(unsigned)s_GridDraftRoom, (unsigned long long)draft->invite.identity.generation, review->stage_epoch,
		(unsigned long long)review->draft_revision, (unsigned long long)review->offer_serial,
		s_GridRoomExtension.arena_sha256);
	return bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log", prefix,
		record, sizeof(record));
}

/* The normal mesh ground query intentionally tolerates beyond-edge hits.
 * Take an exact ray and a separate native tile query through the removed floor. */
static s32 bootGridRelocationFloor(const struct coord *point, RoomNum room, s32 present)
{
	struct coord from = *point, to = *point;
	from.y = 160.0f;
	to.y = -32.0f;
	s32 hit = -1;
	const f32 fraction = meshRayCastWorldMasked(&from, &to, GEOFLAG_FLOOR1 | GEOFLAG_FLOOR2, NULL, &hit);
	RoomNum rooms[] = {room, -1};
	const f32 tile = cdFindFloorYColourTypeAtPos(&from, rooms, NULL, NULL);
	return present ? hit >= 0 && fabsf(from.y + (to.y - from.y) * fraction) < 0.125f && tile == 0.0f
		: hit < 0 && fraction == 1.0f && tile < -1000.0f;
}

static s32 bootGridLayoutActorIdentity(void)
{
	if (g_Vars.players[0] != s_GridLayoutSmoke.player
			|| memcmp(g_MpAllChrPtrs, s_GridLayoutSmoke.actors, sizeof(g_MpAllChrPtrs))) return 0;
	for (s32 i = 0; i < MAX_MPCHRS; ++i) {
		struct prop *prop = g_MpAllChrPtrs[i] ? g_MpAllChrPtrs[i]->prop : NULL;
		if (prop != s_GridLayoutSmoke.props[i]
				|| propGetLifecycleGeneration(prop) != s_GridLayoutSmoke.prop_generations[i]) return 0;
	}
	return 1;
}

static s32 bootGridLayoutActorState(u8 out[SHA256_DIGEST_SIZE])
{
	struct player *host = g_Vars.players[0];
	if (!host || host != g_Vars.currentplayer || !bootGridLayoutActorIdentity()) return 0;
	u8 score_bytes[2 + MAX_MPCHRS * (6 + 2 * MAX_MPCHRS)];
	struct netbuf scores = {.data = score_bytes, .size = sizeof(score_bytes)};
	if (netmsgSvcPlayerScoresWrite(&scores) || scores.wp < 2 || score_bytes[1] != 4) return 0;
	sha256_ctx state;
	sha256Init(&state);
	sha256Update(&state, score_bytes, scores.wp);
	const f32 host_status[] = {host->bondhealth, host->vv_theta, host->vv_verta};
	const s32 equipment[] = {host->isdead, bgunGetWeaponNum(HAND_RIGHT), bgunGetWeaponNum(HAND_LEFT),
		host->hands[HAND_RIGHT].loadedammo[0], host->hands[HAND_RIGHT].loadedammo[1],
		host->hands[HAND_LEFT].loadedammo[0], host->hands[HAND_LEFT].loadedammo[1]};
	sha256Update(&state, host_status, sizeof(host_status));
	sha256Update(&state, equipment, sizeof(equipment));
	sha256Update(&state, host->ammoheldarr, sizeof(host->ammoheldarr));
	const s32 inventory_count = host->equipmaxitems;
	if (!host->equipment || inventory_count <= 0 || inventory_count > 256) return 0;
	sha256Update(&state, &inventory_count, sizeof(inventory_count));
	sha256Update(&state, &host->equipcuritem, sizeof(host->equipcuritem));
	for (s32 i = 0; i < inventory_count; ++i) {
		const struct invitem *item = &host->equipment[i];
		sha256Update(&state, &item->type, sizeof(item->type));
		switch (item->type) {
		case -1: break;
		case INVITEMTYPE_WEAP: sha256Update(&state, &item->type_weap, sizeof(item->type_weap)); break;
		case INVITEMTYPE_DUAL: sha256Update(&state, &item->type_dual, sizeof(item->type_dual)); break;
		case INVITEMTYPE_PROP: sha256Update(&state, &item->type_prop, sizeof(item->type_prop)); break;
		default: return 0;
		}
	}
	for (s32 i = 0; i < 4; ++i) {
		const struct chrdata *chr = g_MpAllChrPtrs[i];
		if (!chr || (i && (!chr->aibot || !chr->aibot->ammoheld))) return 0;
		const f32 health[] = {chr->damage, chr->maxdamage, chr->cshield};
		sha256Update(&state, health, sizeof(health));
		if (chr->aibot) {
			const struct aibot *bot = chr->aibot;
			sha256Update(&state, &bot->weaponnum, sizeof(bot->weaponnum));
			sha256Update(&state, bot->loadedammo, sizeof(bot->loadedammo));
			sha256Update(&state, bot->ammoheld, AMMO_TYPE_COUNT * sizeof(*bot->ammoheld));
		}
	}
	sha256Final(&state, out);
	return 1;
}

/* Called only for an actual slot-0 relocation, around its native commit
 * with neither game time nor another actor tick between the two reads. */
void bootGridLayoutSmokeActorState(s32 moved)
{
	if (!s_GridDraftRelocateLayout || !s_GridLayoutSmoke.second || !s_GridLayoutSmoke.requested) return;
	struct prop *prop = g_Vars.players[0] ? g_Vars.players[0]->prop : NULL;
	if (!prop || prop->rooms[0] != 2) return;
	if (!moved) {
		if (!(prop->pos.x > 2800.0f && prop->pos.x < 3000.0f)
				|| !bootGridRelocationFloor(&prop->pos, 2, 0)) return;
		s_GridLayoutSmoke.relocation_origin = prop->pos.x;
		s_GridLayoutSmoke.match_time = lvGetStageTime60();
		s_GridLayoutSmoke.captured_relocation = bootGridLayoutActorState(s_GridLayoutSmoke.actor_state);
		return;
	}
	u8 state[SHA256_DIGEST_SIZE];
	if (!s_GridLayoutSmoke.captured_relocation || s_GridLayoutSmoke.match_time != lvGetStageTime60()
			|| !(prop->pos.x > 0.0f && prop->pos.x < 2800.0f)
			|| !bootGridRelocationFloor(&prop->pos, 2, 1) || !bootGridLayoutActorState(state)
			|| memcmp(state, s_GridLayoutSmoke.actor_state, sizeof(state))) return;
	s_GridLayoutSmoke.verified_relocation = 1;
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.RELOCATE: actor slot=0 from_x=%.1f to_x=%.1f room=2 time=%d scores_health_inventory_ammo=unchanged mesh_tiles=verified",
		s_GridLayoutSmoke.relocation_origin, prop->pos.x, s_GridLayoutSmoke.match_time);
}

static s32 bootGridLayoutSceneExtent(f32 expected)
{
	f32 rightmost = 0.0f;
	const s32 count = scenarioSceneRendererVertexCount();
	if (count <= 0) return 0;
	for (s32 i = 0; i < count; ++i) {
		f32 pos[3];
		if (!scenarioSceneRendererVertexPosition(i, pos) || !__builtin_isfinite(pos[0])) return 0;
		if (pos[0] > rightmost) rightmost = pos[0];
	}
	return fabsf(rightmost - expected) < 0.125f;
}

/* Exercise the same Packaged Level File and Mod Manager APIs that the
 * two manual steps use. Nothing from this process is passed in memory. */
static s32 bootGridSaveRestartVariant(const char *original_id, const char *name)
{
	char directory[FS_MAXPATH + 1];
	char err[256] = {0};
	forge_revision_t original, variant, parent;
	mod_author_result_t *fork = calloc(1, sizeof(*fork));
	forge_level_doc_t *reopened = NULL;
	s32 saved = 0;

	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	const forge_level_source_t *opened = forgeLevelSessionSource();
	if (!fork || !bootGridRoomExtensionBounds(2700.0f)
			|| ((!opened || strcmp(opened->arena_id, original_id) != 0)
				&& !forgeLevelSessionOpen(original_id, err, sizeof(err)))
			|| !forgeLevelSessionPinSavedRevision(&original, err, sizeof(err))
			|| strcmp(original.arena_id, original_id) != 0
			|| !forgeLevelSessionSaveExtendedVariant(name, 2, 300, fork, err, sizeof(err))
			|| !forgeRevisionPublishLocal(directory, fork->catalog_id, fork->archive_path,
				NULL, &variant, err, sizeof(err))
			|| forgeRevisionReadParent(directory, &variant, &parent, err, sizeof(err))
				!= FORGE_REVISION_PARENT_AVAILABLE
			|| strcmp(parent.arena_id, original.arena_id) != 0
			|| strcmp(parent.digest, original.digest) != 0) goto done;

	forge_level_source_t source;
	reopened = forgeLevelOpen(original.arena_id, &source, err, sizeof(err));
	if (!reopened || strcmp(source.archive_sha256, original.digest) != 0
			|| strcmp(variant.arena_id, original.arena_id) == 0
			|| strcmp(variant.digest, original.digest) == 0) goto done;

	/* Saving an extended variant publishes its files without enabling it.
	 * Persist the usual user selection for the next boot. */
	modmgrRescanDirectory();
	for (s32 i = 0; i < modmgrGetCount(); i++) {
		modinfo_t *mod = modmgrGetMod(i);
		if (!mod || strcmp(mod->id, fork->mod_id) != 0) continue;
		modmgrSetEnabled(i, 1);
		modmgr_save_result_t result;
		if (!mod->enabled || !modmgrSaveConfigChecked(NULL, &result)) goto done;
		saved = 1;
		break;
	}
done:
	if (saved) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VARIANT.RESTART: saved original=%s parent=%s variant=%s sha256=%s",
			original.arena_id, original.digest, variant.arena_id, variant.digest);
	} else {
		sysLogPrintf(LOG_WARNING, "SMOKE.GRID.VARIANT.RESTART: could not save exact variant %s",
			err[0] ? err : name);
	}
	forgeLevelDocFree(reopened);
	free(fork);
	return saved;
}

static s32 bootGridVerifyRestartVariant(const char *original_id, const char *variant_id,
		forge_revision_t *variant)
{
	char directory[FS_MAXPATH + 1];
	char err[256] = {0};
	forge_revision_t original, parent;
	forge_level_source_t original_source, variant_source;
	forge_level_doc_t *original_doc = NULL;
	forge_level_doc_t *variant_doc = NULL;
	s32 verified = 0;

	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionReadCurrent(directory, original_id, &original, err, sizeof(err))
			|| !forgeRevisionReadCurrent(directory, variant_id, variant, err, sizeof(err))
			|| forgeRevisionReadParent(directory, variant, &parent, err, sizeof(err))
				!= FORGE_REVISION_PARENT_AVAILABLE
			|| strcmp(parent.arena_id, original_id) != 0
			|| strcmp(parent.digest, original.digest) != 0
			|| strcmp(variant->digest, original.digest) == 0) goto done;
	original_doc = forgeLevelOpen(original_id, &original_source, err, sizeof(err));
	variant_doc = forgeLevelOpen(variant_id, &variant_source, err, sizeof(err));
	verified = original_doc && variant_doc
		&& strcmp(original_source.archive_sha256, original.digest) == 0
		&& strcmp(variant_source.archive_sha256, variant->digest) == 0;
done:
	if (verified) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VARIANT.RESTART: reopened original=%s parent=%s variant=%s sha256=%s",
			original.arena_id, original.digest, variant->arena_id, variant->digest);
	} else {
		sysLogPrintf(LOG_WARNING, "SMOKE.GRID.VARIANT.RESTART: persisted source or lineage did not verify %s", err);
	}
	forgeLevelDocFree(variant_doc);
	forgeLevelDocFree(original_doc);
	return verified;
}

static s32 bootGridPlayRestartVariant(const char *original_id)
{
	static s32 checked;
	static s32 first_tick;
	static struct coord start;
	static forge_revision_t variant;
	if (checked < 0 || !g_BootLaunchMpArena || g_NetMode != NETMODE_NONE
			|| !g_Vars.normmplayerisrunning || g_Vars.tickmode != TICKMODE_NORMAL
			|| g_Vars.lvframenum < 60) return 0;
	const asset_entry_t *arena = assetCatalogResolve(g_BootLaunchMpArena);
	struct player *player = g_Vars.players[0];
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum
			|| !player || !player->prop) return 0;

	if (!checked) {
		const struct coord extension = { .f = { 2850.0f, 160.0f, 0.0f } };
		if (!bootGridVerifyRestartVariant(original_id, arena->id, &variant)
				|| !bootGridRoomExtensionBounds(3000.0f) || !bootGridLayoutSceneExtent(3000.0f)
				|| !bootGridRelocationFloor(&extension, 2, 1)) {
			checked = -1;
			smokeHarnessExit(1, "grid_variant_restart_native_load_failed");
			return 1;
		}
		checked = 1;
		first_tick = g_Vars.lvframenum;
		start = player->prop->pos;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VARIANT.RESTART: native arena=%s sha256=%s room2_max=3000 scene_max=3000 mesh_floor=0 tile_floor=0",
			variant.arena_id, variant.digest);
		return 1;
	}
	const struct coord *pos = &player->prop->pos;
	const f32 dx = pos->x - start.x;
	const f32 dz = pos->z - start.z;
	const f32 distance = sqrtf(dx * dx + dz * dz);
	const s32 moving = fabsf(actionValue(0, ACTION_AXIS_MOVE_Y)) > 0.5f
		|| fabsf(actionValue(0, ACTION_AXIS_MOVE_X)) > 0.5f;
	if (g_Vars.lvframenum > first_tick && moving && distance >= 48.0f && (player->prop->rooms[0] == 1 || player->prop->rooms[0] == 2)) {
		checked = -1;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.VARIANT.RESTART: played arena=%s room=%d distance=%.1f frames=%d",
			variant.arena_id, (s32)player->prop->rooms[0], distance, (s32)g_Vars.lvframenum - first_tick);
		smokeHarnessExit(0, "grid_variant_restart_played");
		return 1;
	}
	return 0;
}

static const char kBootGridFallbackWeapon[] = "mod:weapon_grid_triangle";
static const char kBootGridFallbackProp[] = "grid_required_mesh:tri_required";
static const char kBootGridFallbackVehicle[] = "mod:vehicle_grid_hover";
static const char kBootGridFallbackBaseProp[] = "base:model_crate2_cq";
static const char kBootGridFallbackBaseBike[] = "base:model_hovbike";
static const char *const kBootGridFallbackSourceMods[] = {
	"mod.weapon_grid_triangle", "grid_required_mesh", "mod.vehicle_grid_hover",
};
static const char *const kBootGridFallbackIds[] = {"base:falcon2", "base:slayer"};
static const s32 kBootGridFallbackWeapons[] = {WEAPON_FALCON2, WEAPON_SLAYER};

static s32 bootGridSaveFallbackVariant(const char *original_id, const char *name)
{
	const s32 placements = sysArgCheck("--debug-grid-variant-placement-fallbacks");
	char err[256] = "";
	char directory[FS_MAXPATH + 1];
	char records[4][32];
	char models[2][CATALOG_ID_LEN];
	char bike_model_id[CATALOG_ID_LEN] = "";
	forge_level_placement_fallback_t fallbacks[4];
	forge_level_pad_t first, second, prop_pad;
	forge_level_spawn_t spawn;
	forge_level_object_t still_open;
	forge_revision_t original, variant;
	forge_level_source_t reopened_source;
	forge_level_doc_t *reopened = NULL;
	grid_match_sources_t reviewed = {0};
	mod_author_result_t *saved = calloc(1, sizeof(*saved));
	s32 disabled = 0;
	s32 ok = 0;
	const forge_level_source_t *opened = forgeLevelSessionSource();
	if (!saved || ((!opened || strcmp(opened->arena_id, original_id) != 0)
			&& !forgeLevelSessionOpen(original_id, err, sizeof(err)))) goto done;
	forge_level_doc_t *doc = forgeLevelSessionDoc();
	if (!doc || !forgeLevelSessionCanReviewVariantSources() || forgeLevelDocObjectCount(doc) != 0
			|| forgeLevelDocSpawnCount(doc) != 8 || !forgeLevelSessionPinSavedRevision(&original, err, sizeof(err))
			|| strcmp(original.arena_id, original_id) != 0
			|| !forgeLevelDocFindPad(doc, "pad_0006", &first) || strcmp(first.room_ref, "room_2") != 0
			|| !forgeLevelDocFindPad(doc, "pad_0007", &second) || strcmp(second.room_ref, "room_2") != 0
			|| !forgeLevelDocFindPad(doc, "pad_0005", &prop_pad) || strcmp(prop_pad.room_ref, "room_2") != 0) goto done;

	if (placements) {
		const asset_entry_t *bike_model = assetRuntimeVehiclePlacementModel(kBootGridFallbackVehicle);
		if (!bike_model) {
			snprintf(err, sizeof(err), "the real Grid Hover model is unavailable");
			goto done;
		}
		snprintf(bike_model_id, sizeof(bike_model_id), "%s", bike_model->id);
	}
	/* Put both pickups deep inside room 2, away from the Combat spawn
	 * pool's near-doorway choices, and keep their touch ranges separate. */
	second.position[0] = first.position[0];
	forgeLevelSessionEditBegin();
	s32 edited = 1;
	for (s32 i = 7; i > 0 && edited; --i) {
		edited = forgeLevelDocGetSpawn(doc, i, &spawn) && forgeLevelDocRemoveSpawn(doc, spawn.spawn_id, err, sizeof(err));
	}
	edited = edited && forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, first.position, first.look,
			records[0], sizeof(records[0]), err, sizeof(err))
		&& forgeLevelSessionGivePickup(records[0], kBootGridFallbackWeapon, err, sizeof(err))
		&& forgeLevelDocAddObject(doc, FORGE_LEVEL_NEW_WEAPON_PLACE, 0, second.position, second.look,
			records[1], sizeof(records[1]), err, sizeof(err))
		&& forgeLevelSessionGivePickup(records[1], kBootGridFallbackWeapon, err, sizeof(err))
		&& forgeLevelDocAddProp(doc, kBootGridFallbackProp, prop_pad.position, prop_pad.look,
			records[2], sizeof(records[2]), err, sizeof(err));
	if (edited && placements) {
		const f32 bike_position[3] = {first.position[0] + 500.0f, first.position[1], 0.0f};
		edited = forgeLevelDocAddHoverbike(doc, bike_model_id, bike_position, first.look,
			records[3], sizeof(records[3]), err, sizeof(err));
	}
	forgeLevelSessionEditEnd(edited);
	if (!edited || !forgeLevelSessionReviewVariantSources(&reviewed, err, sizeof(err))) goto done;

	u16 omitted[3];
	size_t omitted_count = 0;
	s32 mesh_present = 0;
	for (size_t i = 0; i < reviewed.package_count; i++) {
		const grid_match_source_package_t *package = &reviewed.packages[i];
		const s32 weapon = package->owner.type == ASSET_WEAPON && strcmp(package->owner.id, kBootGridFallbackWeapon) == 0;
		const s32 mesh = package->owner.type == ASSET_MODEL && strcmp(package->owner.id, kBootGridFallbackProp) == 0;
		const s32 bike = package->owner.type == ASSET_VEHICLE && strcmp(package->owner.id, kBootGridFallbackVehicle) == 0;
		mesh_present |= mesh;
		if (weapon || (placements && (mesh || bike))) {
			if (omitted_count >= ARRAYCOUNT(omitted)) goto done;
			omitted[omitted_count++] = (u16)i;
		}
	}
	if (!mesh_present || omitted_count != (placements ? 3u : 1u)) {
		snprintf(err, sizeof(err), "the exact reviewed placement owners are incomplete");
		goto done;
	}
	if (placements) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: reviewed digest=%s omitted_weapon=%s omitted_prop=%s omitted_vehicle=%s bike_model=%s pickups=2",
			reviewed.digest, kBootGridFallbackWeapon, kBootGridFallbackProp, kBootGridFallbackVehicle, bike_model_id);
	} else {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: reviewed digest=%s omitted=%s kept=%s pickups=2",
			reviewed.digest, kBootGridFallbackWeapon, kBootGridFallbackProp);
	}
	if (forgeLevelSessionSaveSelectedVariant(name, &reviewed, omitted, omitted_count, NULL, 0, NULL, err, sizeof(err))
			|| !strstr(err, "still requires this omitted package")) goto done;
	if (placements) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: refused omission while native placements still required omitted packages=%u", (unsigned)omitted_count);
	} else {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: refused omission while the two pickups still required %s", kBootGridFallbackWeapon);
	}
	for (s32 i = 0; i < 2; i++) {
		s32 scale = 0;
		if (!forgeLevelPickupLook(kBootGridFallbackIds[i], models[i], sizeof(models[i]), &scale, err, sizeof(err))) goto done;
		fallbacks[i] = (forge_level_placement_fallback_t){
			.kind = FORGE_LEVEL_FALLBACK_PICKUP, .record_id = records[i], .expected_catalog_id = kBootGridFallbackWeapon,
			.weapon_catalog_id = kBootGridFallbackIds[i], .model_catalog_id = models[i], .extra_scale = scale,
		};
	}
	if (placements) {
		fallbacks[2] = (forge_level_placement_fallback_t){
			.kind = FORGE_LEVEL_FALLBACK_PROP, .record_id = records[2], .expected_catalog_id = kBootGridFallbackProp,
		};
		fallbacks[3] = (forge_level_placement_fallback_t){
			.kind = FORGE_LEVEL_FALLBACK_HOVERBIKE, .record_id = records[3], .expected_catalog_id = bike_model_id,
		};
	}
	if (!forgeLevelSessionSaveSelectedVariant(name, &reviewed, omitted, omitted_count, fallbacks, placements ? 4 : 2,
			saved, err, sizeof(err)) || strcmp(saved->catalog_id, original.arena_id) == 0
			|| forgeLevelSessionDoc() != doc || !forgeLevelDocGetObject(doc, 0, &still_open)
			|| strcmp(still_open.weapon_catalog_id, kBootGridFallbackWeapon) != 0) goto done;
	if (placements && (!forgeLevelDocGetObject(doc, 2, &still_open)
			|| strcmp(still_open.model_catalog_id, kBootGridFallbackProp) != 0
			|| !forgeLevelDocGetObject(doc, 3, &still_open) || strcmp(still_open.model_catalog_id, bike_model_id) != 0)) goto done;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionPublishLocal(directory, saved->catalog_id, saved->archive_path, NULL, &variant, err, sizeof(err))) goto done;
	reopened = forgeLevelOpen(original.arena_id, &reopened_source, err, sizeof(err));
	if (!reopened || strcmp(reopened_source.archive_sha256, original.digest) != 0
			|| forgeLevelDocObjectCount(reopened) != 0 || forgeLevelDocSpawnCount(reopened) != 8) goto done;

	/* Persist normal Mod Manager selection so cold play cannot use omitted assets. */
	for (s32 i = 0; i < modmgrGetCount(); i++) {
		const char *id = modmgrGetModId(i);
		if (!id || !modmgrGetModEnabled(i)) continue;
		for (size_t j = 0; j < omitted_count; j++) {
			if (strcmp(id, kBootGridFallbackSourceMods[j]) != 0) continue;
			modmgrSetEnabled(i, 0);
			disabled += !modmgrGetModEnabled(i);
			break;
		}
	}
	modmgr_save_result_t result;
	ok = disabled == (s32)omitted_count && modmgrSaveConfigChecked(NULL, &result);
	if (!ok) snprintf(err, sizeof(err), "persisting disabled source mods failed (%d of %u)", disabled, (unsigned)omitted_count);

done:
	if (ok) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: saved original=%s sha256=%s variant=%s sha256=%s triangle_enabled=0 draft_unchanged=1",
			original.arena_id, original.digest, variant.arena_id, variant.digest);
		if (placements) sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: disabled placed source mods=%d", disabled);
	} else {
		sysLogPrintf(LOG_ERROR, "SMOKE.GRID.FALLBACK.FAIL: save %s", err[0] ? err : "unexpected native authoring, source review or saved selection");
	}
	forgeLevelDocFree(reopened);
	gridMatchSourcesFree(&reviewed);
	free(saved);
	return ok;
}

static s32 bootGridReadFallbackVariant(const char *original_id, const char *variant_id,
		char *err, size_t err_cap)
{
	const s32 placements = sysArgCheck("--debug-grid-variant-placement-fallbacks");
	char directory[FS_MAXPATH + 1];
	forge_revision_t original, variant;
	forge_level_source_t original_source, variant_source;
	forge_level_doc_t *original_doc = NULL;
	forge_level_doc_t *variant_doc = NULL;
	s32 ok = 0;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionReadCurrent(directory, original_id, &original, err, err_cap)
			|| !forgeRevisionReadCurrent(directory, variant_id, &variant, err, err_cap)
			|| strcmp(original.digest, variant.digest) == 0) goto done;
	original_doc = forgeLevelOpen(original_id, &original_source, err, err_cap);
	variant_doc = forgeLevelOpen(variant_id, &variant_source, err, err_cap);
	if (!original_doc || !variant_doc || assetCatalogResolve(kBootGridFallbackWeapon)
			|| strcmp(original_source.archive_sha256, original.digest) != 0
			|| strcmp(variant_source.archive_sha256, variant.digest) != 0
			|| forgeLevelDocObjectCount(original_doc) != 0 || forgeLevelDocSpawnCount(original_doc) != 8
			|| forgeLevelDocObjectCount(variant_doc) != (placements ? 4 : 3) || forgeLevelDocSpawnCount(variant_doc) != 1) goto done;
	const size_t omitted_count = placements ? ARRAYCOUNT(kBootGridFallbackSourceMods) : 1;
	for (size_t i = 0; i < omitted_count; i++) {
		const modinfo_t *mod = modmgrFindMod(kBootGridFallbackSourceMods[i]);
		if (!mod || mod->enabled) goto done;
	}
	if (placements && (assetCatalogResolve(kBootGridFallbackProp) || assetCatalogResolve(kBootGridFallbackVehicle))) goto done;
	forge_level_object_t object;
	for (s32 i = 0; i < 2; i++) {
		if (!forgeLevelDocGetObject(variant_doc, i, &object) || object.order != i
				|| strcmp(object.weapon_catalog_id, kBootGridFallbackIds[i]) != 0) goto done;
	}
	if (!forgeLevelDocGetObject(variant_doc, 2, &object) || object.order != 2
			|| strcmp(object.model_catalog_id, placements ? kBootGridFallbackBaseProp : kBootGridFallbackProp) != 0) goto done;
	if (placements && (!forgeLevelDocGetObject(variant_doc, 3, &object) || object.order != 3
			|| strcmp(object.kind, "hoverbike") != 0 || strcmp(object.model_catalog_id, kBootGridFallbackBaseBike) != 0)) goto done;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: reopened original=%s sha256=%s variant=%s sha256=%s omitted_weapon_available=0",
		original.arena_id, original.digest, variant.arena_id, variant.digest);
	if (placements) sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: reopened with omitted prop and vehicle unavailable sources=%u", (unsigned)omitted_count);
	ok = 1;
done:
	forgeLevelDocFree(original_doc);
	forgeLevelDocFree(variant_doc);
	return ok;
}

static s32 bootGridCollectFallback(struct player *player, s32 index, s32 weapon, RoomNum room)
{
	struct defaultobj *object = setupGetObjByCmdIndex(index);
	if (!object || object->type != OBJTYPE_WEAPON || !object->prop || object->prop->rooms[0] != room
			|| ((struct weaponobj *)object)->weaponnum != weapon
			|| invHasSingleWeaponExcAllGuns(weapon)) return 0;
	struct prop *pickup = object->prop;
	struct coord position = pickup->pos;
	position.x -= 48.0f;
	position.y = player->prop->pos.y;
	RoomNum rooms[] = {room, -1};
	if (!bootForgePickupInRoom(&position, room) || !chrMoveToPos(player->prop->chr, &position, rooms, 0.0f, true)
			|| player->prop->rooms[0] != room || !bootForgePickupInRoom(&player->prop->pos, room)) return 0;
	/* Use the game's touch test and pickup transaction for the loaded world prop. */
	const s32 operation = objTestForPickup(pickup);
	if (operation == TICKOP_NONE) return 0;
	propExecuteTickOperation(pickup, operation);
	return invHasSingleWeaponExcAllGuns(weapon);
}

static s32 bootGridFallbackNativePlacement(s32 command, s32 type, const char *model_id, char *err, size_t err_cap)
{
	const struct defaultobj *object = setupGetObjByCmdIndex(command);
	const struct prop *prop = object ? object->prop : NULL;
	catalog_model_result_t binding;
	if (!object || object->type != type || !prop || prop->obj != object
			|| !(prop->flags & PROPFLAG_ENABLED) || prop->rooms[0] != 2
			|| !object->model || !object->model->definition
			|| !catalogResolveModel(model_id, &binding) || !binding.handle.provider
			|| binding.modelnum < 0 || object->modelnum != binding.modelnum) {
		snprintf(err, err_cap, "stock placement cmd=%d type=%d modelnum=%d prop=%d room=%d expected_model=%s",
			command, object ? object->type : -1, object ? object->modelnum : -1, prop != NULL,
			prop ? prop->rooms[0] : -1, model_id);
		return 0;
	}
	return 1;
}

static s32 bootGridPlayFallbackVariant(const char *original_id)
{
	static s32 phase;
	static s32 first_pickup_frame;
	const s32 placements = sysArgCheck("--debug-grid-variant-placement-fallbacks");
	char err[256] = "";
	if (phase < 0 || !g_BootLaunchMpArena || g_NetMode != NETMODE_NONE
			|| !g_Vars.normmplayerisrunning || g_Vars.tickmode != TICKMODE_NORMAL
			|| g_Vars.lvframenum < 60 || !g_Vars.lvupdate240) return 0;
	const asset_entry_t *arena = assetCatalogResolve(g_BootLaunchMpArena);
	struct player *player = g_Vars.players[0];
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum
			|| PLAYERCOUNT() != 1 || !player || player != g_Vars.currentplayer
			|| !player->prop || !player->prop->chr || player->isdead) return 0;
	if (!phase) {
		if (!bootGridReadFallbackVariant(original_id, arena->id, err, sizeof(err))) {
			if (!err[0]) snprintf(err, sizeof(err), "reopened revisions, placement IDs, or omitted sources did not match");
			goto fail;
		}
		for (s32 i = 0; i < 2; i++) {
			const struct defaultobj *placed = setupGetObjByCmdIndex(i);
			const s32 weapon = placed && placed->type == OBJTYPE_WEAPON
				? ((const struct weaponobj *)placed)->weaponnum : -1;
			const s32 room = placed && placed->prop ? placed->prop->rooms[0] : -1;
			const s32 owned = invHasSingleWeaponExcAllGuns(kBootGridFallbackWeapons[i]);
			if (!placed || placed->type != OBJTYPE_WEAPON || !placed->prop || !placed->model
					|| room != 2 || weapon != kBootGridFallbackWeapons[i] || owned) {
				snprintf(err, sizeof(err),
					"pickup cmd=%d type=%d prop=%d model=%d room=%d weapon=%d expected=%d owned=%d start_room=%d start=(%.0f,%.0f,%.0f)",
					i, placed ? placed->type : -1, placed && placed->prop, placed && placed->model,
					room, weapon, kBootGridFallbackWeapons[i], owned, (s32)player->prop->rooms[0],
					player->prop->pos.x, player->prop->pos.y, player->prop->pos.z);
				goto fail;
			}
		}
		if (placements) {
			if (!bootGridFallbackNativePlacement(2, OBJTYPE_BASIC, kBootGridFallbackBaseProp, err, sizeof(err))
					|| !bootGridFallbackNativePlacement(3, OBJTYPE_HOVERBIKE, kBootGridFallbackBaseBike, err, sizeof(err))) goto fail;
			sysLogPrintf(LOG_NOTE,
				"SMOKE.GRID.FALLBACK: native arena=%s room=2 pickups=base:falcon2,base:slayer prop=%s hoverbike=%s inventory_before=0,0",
				arena->id, kBootGridFallbackBaseProp, kBootGridFallbackBaseBike);
		} else {
			const struct defaultobj *prop = setupGetObjByCmdIndex(2);
			const char *model = prop && prop->modelnum >= 0 ? catalogModelIdByModelnum(prop->modelnum) : NULL;
			if (!prop || prop->type != OBJTYPE_BASIC || !prop->prop || !prop->model || prop->prop->rooms[0] != 2
					|| !model || strcmp(model, kBootGridFallbackProp) != 0) {
				snprintf(err, sizeof(err), "retained prop cmd=2 type=%d prop=%d model=%d modelnum=%d model_id=%s room=%d",
					prop ? prop->type : -1, prop && prop->prop, prop && prop->model, prop ? prop->modelnum : -1,
					model ? model : "(none)", prop && prop->prop ? (s32)prop->prop->rooms[0] : -1);
				goto fail;
			}
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: native arena=%s room=2 pickups=base:falcon2,base:slayer prop=%s inventory_before=0,0",
				arena->id, model);
		}
		if (!bootGridCollectFallback(player, 0, kBootGridFallbackWeapons[0], 2)) {
			snprintf(err, sizeof(err), "Falcon 2 touch did not grant inventory in room 2");
			goto fail;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: picked up %s in room=2 inventory=1", kBootGridFallbackIds[0]);
		first_pickup_frame = g_Vars.lvframenum;
		phase = 1;
		return 1;
	}
	if (g_Vars.lvframenum <= first_pickup_frame) return 0;
	const s32 slayer = bootGridCollectFallback(player, 1, kBootGridFallbackWeapons[1], 2);
	const s32 falcon = invHasSingleWeaponExcAllGuns(kBootGridFallbackWeapons[0]);
	if (!slayer || !falcon) {
		snprintf(err, sizeof(err), "Slayer touch collected=%d Falcon 2 retained=%d", slayer, falcon);
		goto fail;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.FALLBACK: picked up %s in room=2 inventory=1", kBootGridFallbackIds[1]);
	phase = -1;
	smokeHarnessExit(0, "grid_variant_fallback_played");
	return 1;

fail:
	phase = -1;
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.FALLBACK.FAIL: restart %s", err);
	smokeHarnessExit(1, "grid_variant_fallback_play_failed");
	return 1;
}

static const char kBootPlayedOriginal[] = "mod:arena_grid_required_mods";
static const char kBootPlayedVariant[] = "mod:arena_grid_played_variant";
static const char kBootHostPlayedVariant[] = "mod:arena_grid_host_played_variant";
static const char kBootPlayedMeshSha[] = "90682b258c9272480c2419d92c8d7d15da946993ea2cdc6878eb8b3a9b316d2f";

static s32 bootGridPlayedSourcesUninstalled(s32 mesh_mounted)
{
	return !modmgrFindMod("mod.arena_grid_required_mods") && !modmgrFindMod("mod.weapon_grid_triangle")
		&& !modmgrFindMod("grid_required_mesh") && !assetCatalogResolveAny(kBootPlayedOriginal)
		&& !assetCatalogResolveAny(kBootGridFallbackWeapon)
		&& (mesh_mounted || !assetCatalogResolveAny(kBootGridFallbackProp));
}

static const grid_match_source_package_t *bootGridPlayedPackage(const grid_match_sources_t *sources,
	const char *id, asset_type_e type)
{
	for (size_t i = 0; sources && i < sources->package_count; i++) {
		const grid_match_source_package_t *package = &sources->packages[i];
		if (package->owner.type == type && strcmp(package->owner.id, id) == 0) return package;
	}
	return NULL;
}

static s32 bootGridPlayedMeshBytes(const grid_match_source_package_t *expected, const void *bytes, u32 size)
{
	if (!expected || expected->bundled || strcmp(expected->suffix, ".pdmesh") != 0
			|| !bytes || size != expected->size) return 0;
	u8 digest[SHA256_DIGEST_SIZE];
	char hex[SHA256_HEX_SIZE];
	sha256Hash(bytes, size, digest);
	sha256ToHex(digest, hex);
	return memcmp(digest, expected->digest, sizeof(digest)) == 0 && strcmp(hex, kBootPlayedMeshSha) == 0;
}

static s32 bootGridPlayedInstalledMatches(const grid_match_source_package_t *package)
{
	if (!package || package->bundled) return 0;
	const asset_entry_t *entry = assetCatalogResolve(package->owner.id);
	char descriptor[FS_MAXPATH + 1], locator[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	if (!entry || entry->type != package->owner.type || entry->temporary
			|| !gridMatchCatalogArchiveDescriptor(entry, descriptor, sizeof(descriptor))
			|| !gridMatchPackageLocation(descriptor, locator, sizeof(locator), chain, sizeof(chain), NULL)
			|| chain[0]) return 0;
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(locator, &size);
	u8 digest[SHA256_DIGEST_SIZE] = {0};
	if (bytes && size == package->size) sha256Hash(bytes, size, digest);
	const s32 exact = bytes && size == package->size && memcmp(digest, package->digest, sizeof(digest)) == 0;
	free(bytes);
	return exact;
}

static s32 bootGridPlayedHostSourcesMatch(const grid_match_sources_t *match)
{
	return bootGridPlayedInstalledMatches(bootGridPlayedPackage(match, kBootPlayedOriginal, ASSET_ARENA))
		&& bootGridPlayedInstalledMatches(bootGridPlayedPackage(match, kBootGridFallbackProp, ASSET_MODEL))
		&& bootGridPlayedInstalledMatches(bootGridPlayedPackage(match, kBootGridFallbackWeapon, ASSET_WEAPON));
}

static s32 bootGridPlayedHostForkMatches(const forge_revision_t *variant, const forge_level_object_t *original,
		const char *pickup_model, char *err, size_t err_cap)
{
	forge_level_source_t source;
	forge_level_doc_t *doc = forgeLevelOpen(variant->arena_id, &source, err, err_cap);
	if (!doc) return 0;
	s32 mesh = 0, fallback = 0, omitted = 1;
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc); i++) {
		forge_level_object_t object;
		if (!forgeLevelDocGetObject(doc, i, &object) || object.removed) continue;
		if (strcmp(object.kind, "prop") == 0 && strcmp(object.model_catalog_id, kBootGridFallbackProp) == 0) mesh = 1;
		if (object.weapon_settable && strcmp(object.weapon_catalog_id, kBootGridFallbackWeapon) == 0) omitted = 0;
		if (object.weapon_settable && strcmp(object.record_id, original->record_id) == 0
				&& strcmp(object.pad_ref, original->pad_ref) == 0
				&& strcmp(object.weapon_catalog_id, kBootGridFallbackIds[0]) == 0
				&& strcmp(object.model_catalog_id, pickup_model) == 0) fallback = 1;
	}
	forgeLevelDocFree(doc);
	return strcmp(source.archive_sha256, variant->digest) == 0 && mesh && fallback && omitted;
}

static s32 bootGridSavePlayedVariantTick(const char *name, s32 host)
{
	static u32 saved_tick;
	static s32 finished;
	if (finished) return 0;
	if (saved_tick) {
		/* Leave time for the ordinary Room resync to reach the host. */
		if (SDL_GetTicks() - saved_tick < 1000u) return 0;
		finished = 1;
		smokeHarnessExit(0, "grid_played_variant_saved");
		return 1;
	}
	if (host ? (g_NetMode != NETMODE_SERVER || g_NetDedicated) : g_NetMode != NETMODE_CLIENT) return 0;
	const grid_match_result_t *current = netGridLastMatchResult();
	if (!mainGridRoomBackdropActive() || mainGridRoomReturnPending()
			|| !forgeLevelCanReviewPlayedVariantSources(current)
			|| g_NetLocalClient->room_id != current->offer.room_id) return 0;
	const grid_match_result_t result = *current;
	const grid_match_sources_t *match = gridMatchRuntimeReturnedSources(&result);
	const grid_match_source_package_t *received_mesh = bootGridPlayedPackage(match, kBootGridFallbackProp, ASSET_MODEL);
	char err[256] = "", directory[FS_MAXPATH + 1], parent_hex[SHA256_HEX_SIZE];
	char pickup_model[CATALOG_ID_LEN];
	forge_level_doc_t *doc = NULL;
	grid_match_sources_t review = {0};
	forge_level_object_t pickup = {0};
	forge_level_placement_fallback_t fallback = { .kind = FORGE_LEVEL_FALLBACK_PICKUP };
	mod_author_result_t *saved = calloc(1, sizeof(*saved));
	void *stored_mesh = NULL, *stored_weapon = NULL;
	forge_revision_t variant, parent;
	s32 ok = 0;
	u32 arena_size = 0, mesh_size = 0;
	const void *arena = gridMatchRuntimeReadReturnedArena(&result, &arena_size);
	const void *mesh = !host && received_mesh
		? gridMatchRuntimeReadReturnedSource(&result, received_mesh - match->packages, &mesh_size) : NULL;
	u8 actual_parent[SHA256_DIGEST_SIZE];
	if (!saved || !arena || !arena_size || strcmp(result.arena_id, kBootPlayedOriginal) != 0
			|| (host ? !bootGridPlayedHostSourcesMatch(match)
				: (!bootGridPlayedSourcesUninstalled(0) || !bootGridPlayedMeshBytes(received_mesh, mesh, mesh_size)))) goto done;
	sha256Hash(arena, arena_size, actual_parent);
	if (memcmp(actual_parent, result.arena_sha256, sizeof(actual_parent)) != 0
			|| !forgeLevelReviewPlayedVariantSources(&result, &doc, &review, err, sizeof(err))) goto done;
	sha256ToHex(actual_parent, parent_hex);
	const grid_match_source_package_t *weapon = bootGridPlayedPackage(&review, kBootGridFallbackWeapon, ASSET_WEAPON);
	const grid_match_source_package_t *kept = bootGridPlayedPackage(&review, kBootGridFallbackProp, ASSET_MODEL);
	if (!weapon || (host ? !bootGridPlayedInstalledMatches(kept) : !bootGridPlayedMeshBytes(kept, mesh, mesh_size))) goto done;
	s32 pickups = 0;
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc); i++) {
		forge_level_object_t object;
		if (forgeLevelDocGetObject(doc, i, &object) && !object.removed && object.weapon_settable
				&& strcmp(object.weapon_catalog_id, kBootGridFallbackWeapon) == 0) {
			pickup = object;
			pickups++;
		}
	}
	if (pickups != 1 || !forgeLevelPickupLook(kBootGridFallbackIds[0], pickup_model, sizeof(pickup_model),
			&fallback.extra_scale, err, sizeof(err))) goto done;
	fallback.record_id = pickup.record_id;
	fallback.expected_catalog_id = pickup.weapon_catalog_id;
	fallback.weapon_catalog_id = kBootGridFallbackIds[0];
	fallback.model_catalog_id = pickup_model;
	const u16 omitted = (u16)(weapon - review.packages);
	if (!forgeLevelSavePlayedVariant(&result, name, &review, &omitted, 1, &fallback, 1, saved, err, sizeof(err))
			|| err[0] || strcmp(saved->catalog_id, host ? kBootHostPlayedVariant : kBootPlayedVariant) != 0
			|| (host ? !bootGridPlayedHostSourcesMatch(gridMatchRuntimeReturnedSources(&result))
				: !bootGridPlayedSourcesUninstalled(0))) goto done;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	if (!forgeRevisionReadCurrent(directory, saved->catalog_id, &variant, err, sizeof(err))
			|| forgeRevisionReadParent(directory, &variant, &parent, err, sizeof(err)) != FORGE_REVISION_PARENT_AVAILABLE
			|| strcmp(parent.arena_id, result.arena_id) != 0 || strcmp(parent.digest, parent_hex) != 0
			|| strcmp(variant.digest, parent.digest) == 0) goto done;
	if (host) {
		forge_revision_t current_pin;
		if (!forgeRevisionReadCurrent(directory, result.arena_id, &current_pin, err, sizeof(err))
				|| strcmp(current_pin.digest, parent_hex) != 0
				|| !bootGridPlayedHostForkMatches(&variant, &pickup, pickup_model, err, sizeof(err))) goto done;
	}
	stored_mesh = forgeRevisionLoadDependency(directory, kept, &mesh_size, err, sizeof(err));
	stored_weapon = forgeRevisionLoadDependency(directory, weapon, NULL, NULL, 0);
	ok = !stored_weapon && bootGridPlayedMeshBytes(kept, stored_mesh, mesh_size);
	if (ok && host) sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYED: host saved room=%u original=%s parent=%s variant=%s sha256=%s kept=%s mesh_sha256=%s omitted=%s fallback=%s installed_exact=1",
		(unsigned)result.offer.room_id, result.arena_id, parent.digest, variant.arena_id, variant.digest,
		kBootGridFallbackProp, kBootPlayedMeshSha, kBootGridFallbackWeapon, kBootGridFallbackIds[0]);
	else if (ok) sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYED: saved room=%u original=%s parent=%s variant=%s sha256=%s kept=%s mesh_sha256=%s omitted=%s fallback=%s mods_installed=0",
		(unsigned)result.offer.room_id, result.arena_id, parent.digest, variant.arena_id, variant.digest,
		kBootGridFallbackProp, kBootPlayedMeshSha, kBootGridFallbackWeapon, kBootGridFallbackIds[0]);

done:
	free(stored_weapon);
	free(stored_mesh);
	free(saved);
	gridMatchSourcesFree(&review);
	forgeLevelDocFree(doc);
	if (host) {
		if (!ok) sysLogPrintf(LOG_ERROR, "SMOKE.GRID.PLAYED: host save proof failed %s",
			err[0] ? err : "unexpected installed sources, native placements or saved fork");
		finished = 1;
	} else if (ok) saved_tick = SDL_GetTicks();
	else {
		sysLogPrintf(LOG_ERROR, "SMOKE.GRID.PLAYED: save proof failed %s", err[0] ? err : "unexpected receipt, native placements or stored sources");
		finished = 1;
		smokeHarnessExit(1, "grid_played_variant_save_failed");
	}
	return 1;
}

static s32 bootGridPlaySavedVariantTick(void)
{
	static s32 finished;
	if (finished || g_NetMode != NETMODE_NONE || !g_BootLaunchMpArena || !g_Vars.normmplayerisrunning
			|| g_Vars.tickmode != TICKMODE_NORMAL || g_Vars.lvframenum < 60 || !g_Vars.lvupdate240) return 0;
	const asset_entry_t *arena = assetCatalogResolve(g_BootLaunchMpArena);
	struct player *player = g_Vars.players[0];
	if (!arena || arena->type != ASSET_ARENA || arena->ext.arena.stagenum != g_StageNum || PLAYERCOUNT() != 1
			|| !player || player != g_Vars.currentplayer || !player->prop || !player->prop->chr || player->isdead) return 0;
	finished = 1;
	char err[256] = "", directory[FS_MAXPATH + 1];
	char descriptor[FS_MAXPATH + 1], locator[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	char folder[FS_MAXPATH + 1], expected[FS_MAXPATH + 1];
	forge_revision_t parent, variant;
	forge_level_source_t reopened;
	forge_level_doc_t *doc = NULL;
	void *bytes = NULL;
	s32 ok = 0;
	fsFullPath("$S/forge-revisions", directory, sizeof(directory));
	const grid_match_sources_t *plan = forgeLevelVariantDependencyPlanForStage(g_StageNum);
	const grid_match_source_package_t *kept = bootGridPlayedPackage(plan, kBootGridFallbackProp, ASSET_MODEL);
	const asset_entry_t *model = assetCatalogResolve(kBootGridFallbackProp);
	if (strcmp(arena->id, kBootPlayedVariant) != 0 || !bootGridPlayedSourcesUninstalled(1) || !kept
			|| bootGridPlayedPackage(plan, kBootGridFallbackWeapon, ASSET_WEAPON) || !model || !model->temporary
			|| !gridMatchCatalogArchiveDescriptor(model, descriptor, sizeof(descriptor))
			|| !gridMatchPackageLocation(descriptor, locator, sizeof(locator), chain, sizeof(chain), NULL) || chain[0]
			|| !gridMatchPackagePrivatePath(kept->digest, kept->suffix, folder, expected) || strcmp(locator, expected) != 0
			|| !forgeRevisionReadCurrent(directory, arena->id, &variant, err, sizeof(err))
			|| forgeRevisionReadParent(directory, &variant, &parent, err, sizeof(err)) != FORGE_REVISION_PARENT_AVAILABLE
			|| strcmp(parent.arena_id, kBootPlayedOriginal) != 0 || strcmp(parent.digest, variant.digest) == 0) goto done;
	u32 size = 0;
	bytes = gridMatchPackageLoad(locator, &size);
	doc = forgeLevelOpen(arena->id, &reopened, err, sizeof(err));
	if (!doc || strcmp(reopened.archive_sha256, variant.digest) != 0 || !bootGridPlayedMeshBytes(kept, bytes, size)) goto done;
	s32 pickup_index = -1, prop_index = -1;
	for (s32 i = 0; i < forgeLevelDocObjectCount(doc); i++) {
		forge_level_object_t object;
		if (!forgeLevelDocGetObject(doc, i, &object) || object.removed) continue;
		if (object.weapon_settable && strcmp(object.weapon_catalog_id, kBootGridFallbackIds[0]) == 0) pickup_index = object.order;
		if (strcmp(object.kind, "prop") == 0 && strcmp(object.model_catalog_id, kBootGridFallbackProp) == 0) prop_index = object.order;
	}
	const struct defaultobj *prop = prop_index >= 0 ? setupGetObjByCmdIndex(prop_index) : NULL;
	const struct defaultobj *pickup = pickup_index >= 0 ? setupGetObjByCmdIndex(pickup_index) : NULL;
	const char *model_id = prop ? catalogModelIdByModelnum(prop->modelnum) : NULL;
	if (!prop || prop->type != OBJTYPE_BASIC || !prop->prop || !prop->model || !model_id
			|| strcmp(model_id, kBootGridFallbackProp) != 0 || !pickup || pickup->type != OBJTYPE_WEAPON
			|| !pickup->prop || !pickup->model || pickup->prop->rooms[0] <= 0
			|| ((const struct weaponobj *)pickup)->weaponnum != WEAPON_FALCON2) goto done;
	const RoomNum room = pickup->prop->rooms[0];
	if (!bootGridCollectFallback(player, pickup_index, WEAPON_FALCON2, room)) goto done;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYED: restarted variant=%s sha256=%s parent=%s mesh=%s mesh_sha256=%s pickup=%s room=%d inventory=1 mods_installed=0",
		variant.arena_id, variant.digest, parent.digest, model_id, kBootPlayedMeshSha, kBootGridFallbackIds[0], (s32)room);
	ok = 1;

done:
	free(bytes);
	forgeLevelDocFree(doc);
	if (!ok) sysLogPrintf(LOG_ERROR, "SMOKE.GRID.PLAYED: restart proof failed %s", err[0] ? err : "unexpected mounted mesh, parent or native pickup");
	smokeHarnessExit(ok ? 0 : 1, ok ? "grid_played_variant_restarted" : "grid_played_variant_restart_failed");
	return 1;
}

static s32 bootGridLayoutStageRelocation(const grid_edit_room_state_t *draft)
{
	const u64 serial = s_GridRoomExtensionOffer.offer_serial;
	char arena[CATALOG_ID_LEN];
	if (!netGridEditStageRoomExtension("Grid Extension Relocate", 2, 100, arena, sizeof(arena))
			|| strcmp(arena, kGridLayoutRelocationArena)
			|| !netGridEditGetStagedVariant(&s_GridRoomExtension)
			|| strcmp(arena, s_GridRoomExtension.arena_id) || s_GridRoomExtension.draft_revision != draft->revision
			|| s_GridRoomExtension.current_revision != draft->revision || assetCatalogResolveAny(arena)) return 0;
	s_GridLayoutSmoke.second = 1;
	s_GridLayoutSmoke.requested = 0;
	s_GridLayoutSmoke.first_serial = serial;
	memset(&s_GridRoomExtensionOffer, 0, sizeof(s_GridRoomExtensionOffer));
	s_GridDraftStepTick = SDL_GetTicks();
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.RELOCATE: staged epoch=%u catalog=%u revision=%llu room2_max=3000 target=2800 sha256=%s",
		s_GridPlaytestEpoch, s_GridRoomExtensionCatalog, (unsigned long long)draft->revision,
		s_GridRoomExtension.arena_sha256);
	return 1;
}

static s32 bootGridLayoutRetrySmoke(void)
{
	return s_GridDraftApplyLayout && smokeHarnessIsActive()
		&& sysArgCheck("--debug-grid-private-layout-retry");
}

static void bootGridLayoutRetryHost(const grid_edit_room_state_t *draft)
{
	grid_edit_variant_review_info_t review;
	if (!draft->ready || draft->revision != s_GridRoomExtension.draft_revision
			|| !bootGridRoomExtensionOriginalWorld(&s_GridRoomExtensionWorld, s_GridRoomExtensionCatalog)
			|| !bootGridRoomExtensionCandidateMatches() || gridMatchRuntimePrivateLayoutGeneration()
			|| g_StageIndex != s_GridLayoutSmoke.stage_index || g_Rooms != s_GridLayoutSmoke.rooms
			|| !bootGridLayoutActorIdentity() || !bootGridDraftIsHostPeer(1) || !bootGridDraftIsHostPeer(2)
			|| assetCatalogResolveAny(s_GridRoomExtension.arena_id)
			|| !netGridEditGetStagedReview(&review)
			|| !gridEditVariantReviewMatches(&s_GridRoomExtensionOffer, &review.variant)
			|| !review.match_sources_prepared) {
		bootGridDraftFail("grid_layout_retry_changed_original_round");
		return;
	}
	if (review.apply_pending) return;

	static boot_grid_witness_reader_t creator_log;
	char prefix[224], record[64];
	snprintf(prefix, sizeof(prefix), "SMOKE.GRID.LAYOUT.RETRY: creator intact room=%u epoch=%u serial=%llu revision=%llu layout=0 ",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)review.variant.offer_serial,
		(unsigned long long)draft->revision);
	if (!bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log",
			prefix, record, sizeof(record))) return;

	const struct coord extension = { .f = {2850.0f, 160.0f, 0.0f} };
	if (!bootGridRelocationFloor(&extension, 2, 0) || !bootGridLayoutSceneExtent(2700.0f)
			|| g_Vars.lvframe60 <= s_GridLayoutSmoke.frame || lvGetStageTime60() <= s_GridLayoutSmoke.match_time
			|| !netGridEditCanApplyStagedLayout()) {
		bootGridDraftFail("grid_layout_retry_did_not_keep_running");
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.LAYOUT.RETRY: original intact room=%u epoch=%u serial=%llu revision=%llu layout=0 room2_max=2700 scene_max=2700 no_extension_floor=1 actors=4 frame_before=%d frame_after=%d time_before=%d time_after=%d",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)review.variant.offer_serial,
		(unsigned long long)draft->revision, s_GridLayoutSmoke.frame, g_Vars.lvframe60,
		s_GridLayoutSmoke.match_time, lvGetStageTime60());
	s_GridLayoutSmoke.retry_verified_timeout = 1;
	if (!netGridEditApplyStagedLayout()) bootGridDraftFail("grid_layout_retry_could_not_prepare");
}

static void bootGridLayoutSmokeCheck(const grid_edit_room_state_t *draft)
{
	const s32 retry = bootGridLayoutRetrySmoke();
	if (retry && !s_GridLayoutSmoke.retry_verified_timeout) {
		bootGridLayoutRetryHost(draft);
		return;
	}
	const s32 second = s_GridLayoutSmoke.second;
	const u64 generation = second || retry ? 2 : 1;
	const u64 previous_generation = second ? 1 : 0;
	if (gridMatchRuntimePrivateLayoutGeneration() < generation) {
		const char *error = netGridEditError();
		if (error && error[0]) bootGridDraftFail("grid_layout_refused");
		return;
	}
	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	struct coord probe = { .f = {second ? 2900.0f : 2850.0f, 160.0f, 0.0f} };
	const f32 floor = meshFindFloor(&probe, 20.0f, NULL);
	const s32 floor_ok = second ? bootGridRelocationFloor(&probe, 2, 0) : floor == 0.0f;
	if (!draft->ready || draft->revision != s_GridRoomExtension.draft_revision
			|| gridMatchRuntimePrivateLayoutGeneration() != generation
			|| !active || gridMatchRuntimePending() || g_NetStageEpoch != s_GridPlaytestEpoch
			|| g_StageNum != s_GridPlaytestStage || g_StageIndex != s_GridLayoutSmoke.stage_index
			|| active->offer.room_id != s_GridDraftRoom
			|| active->offer.generation != s_GridRoomExtensionWorld.offer.generation
			|| strcmp(active->offer.manifest_digest, s_GridRoomExtensionWorld.offer.manifest_digest)
			|| g_Vars.lvframe60 <= s_GridLayoutSmoke.frame || g_Vars.players[0] != s_GridLayoutSmoke.player
			|| memcmp(g_MpAllChrPtrs, s_GridLayoutSmoke.actors, sizeof(g_MpAllChrPtrs))
			|| ((s_GridDraftRelocateLayout || retry) && !bootGridLayoutActorIdentity())
			|| assetCatalogGetGeneration() != s_GridRoomExtensionCatalog
			|| g_Rooms == s_GridLayoutSmoke.rooms || !bootGridRoomExtensionBounds(second ? 2800.0f : 3000.0f) || !floor_ok
			|| !bootGridDraftIsHostPeer(1) || !bootGridDraftIsHostPeer(2)
			|| assetCatalogResolveAny(s_GridRoomExtension.arena_id)) {
		bootGridDraftFail("grid_layout_changed_round_or_missed_floor");
		return;
	}
	if (retry && (!bootGridRelocationFloor(&probe, 2, 1) || !bootGridLayoutSceneExtent(3000.0f))) {
		bootGridDraftFail("grid_layout_retry_missing_floor_or_scene");
		return;
	}
	if (second && (!s_GridLayoutSmoke.verified_relocation || lvGetStageTime60() <= s_GridLayoutSmoke.match_time
			|| !bootGridLayoutSceneExtent(2800.0f))) {
		bootGridDraftFail("grid_layout_missing_host_relocation_state_or_render");
		return;
	}
	const s32 reconnect = s_GridDraftLayoutReconnect && (!s_GridDraftRelocateLayout || second);
	static boot_grid_witness_reader_t creator_log;
	if (reconnect) {
		char prefix[320], record[160];
		snprintf(prefix, sizeof(prefix),
			"SMOKE.GRID.LAYOUT.RECONNECT: applied room=%u epoch=%u layout=%llu draft=%llu actor=%llu serial=%llu arena_sha256=%s ",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)generation,
			(unsigned long long)draft->invite.identity.generation, (unsigned long long)draft->peer_actor,
			(unsigned long long)s_GridRoomExtensionOffer.offer_serial, s_GridRoomExtension.arena_sha256);
		if (!bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log",
				prefix, record, sizeof(record))) return;
	} else {
		const char prefix[] = "NET.GRID.LAYOUT: event=outcome side=creator ";
		char record[512];
		if (!bootGridPlaytestReadWitness(&creator_log, "./logs/game client/pd-grid-creator.log",
				prefix, record, sizeof(record))) return;
		unsigned room, epoch, result;
		unsigned long long previous, next, serial, actor;
		if (sscanf(record, "room=%u epoch=%u previous=%llu next=%llu serial=%llu actor=%llu result=%u",
				&room, &epoch, &previous, &next, &serial, &actor, &result) != 7
				|| room != s_GridDraftRoom || epoch != s_GridPlaytestEpoch
				|| serial != s_GridRoomExtensionOffer.offer_serial || !actor) {
			bootGridDraftFail("grid_layout_creator_outcome");
			return;
		}
		if (retry && previous == 0 && next == 1 && result == GRID_EDIT_LAYOUT_APPLY_ABORTED) return;
		if (next != generation || previous != previous_generation || result != GRID_EDIT_LAYOUT_APPLY_COMMITTED) {
			bootGridDraftFail("grid_layout_creator_outcome");
			return;
		}
	}
	if (s_GridDraftLayoutDecoration && !bootGridLayoutDecorationWitness(draft, generation, "after")) {
		bootGridDraftFail("grid_layout_decoration_changed_resident_prop");
		return;
	}
	if (second) {
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.RELOCATE: same round room=%u epoch=%u generation=2 room2_max=2800 scene_max=2800 floor_removed=1 frame_before=%d frame_after=%d actors=4",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, s_GridLayoutSmoke.frame, g_Vars.lvframe60);
	} else {
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.LAYOUT: same round room=%u epoch=%u generation=%llu room2_max=%.0f floor=%.0f frame_before=%d frame_after=%d actors=4",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)generation, g_Rooms[2].bbmax[0], floor,
			s_GridLayoutSmoke.frame, g_Vars.lvframe60);
		if (s_GridDraftRelocateLayout) {
			if (!bootGridLayoutStageRelocation(draft)) bootGridDraftFail("grid_layout_stage_relocation");
			return;
		}
	}
	if (reconnect) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: release_disconnect room=%u epoch=%u",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_DETACH;
		s_GridDraftStepTick = SDL_GetTicks();
		return;
	}
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
	mainEndStage();
}

static s32 bootGridLayoutAnchorHost(void)
{
	struct player *host = g_Vars.players[0];
	struct prop *prop = host ? host->prop : NULL;
	if (!prop || !prop->chr || host->isdead || host->bondmovemode != MOVEMODE_WALK) return 0;
	struct coord point = {.f = {2900.0f, host->vv_eyeheight, 0.0f}};
	if (!bootGridRelocationFloor(&point, 2, 1)) return 0;
	const s32 slot = g_Vars.currentplayernum;
	setCurrentPlayerNum(0);
	const u32 old_hidden = prop->chr->hidden;
	const f32 yaw = host->vv_theta, pitch = host->vv_verta;
	prop->chr->hidden |= CHRHFLAG_WARPONSCREEN;
	RoomNum rooms[] = {2, -1};
	const s32 placed = chrMoveToPos(prop->chr, &point, rooms, BADDEG2RAD(360.0f - yaw), false);
	if (!(old_hidden & CHRHFLAG_WARPONSCREEN)) prop->chr->hidden &= ~CHRHFLAG_WARPONSCREEN;
	host->vv_theta = yaw;
	host->vv_verta = pitch;
	setCurrentPlayerNum(slot);
	if (!placed || prop->rooms[0] != 2 || fabsf(prop->pos.x - 2900.0f) >= 0.125f
			|| fabsf(prop->pos.z) >= 0.125f || !bootGridRelocationFloor(&prop->pos, 2, 1)) return 0;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.RELOCATE: anchored slot=0 x=%.1f room=2 old_floor=verified epoch=%u",
		prop->pos.x, s_GridPlaytestEpoch);
	return 1;
}

static s32 bootGridLayoutSmokeRequest(const grid_edit_room_state_t *draft)
{
	struct coord probe = { .f = {2850.0f, 160.0f, 0.0f} };
	s32 count = 0;
	for (s32 i = 0; i < MAX_MPCHRS; ++i) {
		if (g_MpAllChrPtrs[i]) ++count;
	}
	if (count != 4 || g_Vars.lvframe60 <= 0 || !g_Vars.players[0]) {
		bootGridDraftFail("grid_layout_original_floor_or_roster");
		return 0;
	}
	if (s_GridLayoutSmoke.second) {
		if (!bootGridLayoutActorIdentity() || !bootGridLayoutAnchorHost()) {
			bootGridDraftFail("grid_layout_could_not_anchor_host");
			return 0;
		}
	} else {
		if (meshFindFloor(&probe, 20.0f, NULL) >= -1000.0f) {
			bootGridDraftFail("grid_layout_original_floor_or_roster");
			return 0;
		}
		memcpy(s_GridLayoutSmoke.actors, g_MpAllChrPtrs, sizeof(g_MpAllChrPtrs));
		for (s32 i = 0; i < MAX_MPCHRS; ++i) {
			struct prop *prop = g_MpAllChrPtrs[i] ? g_MpAllChrPtrs[i]->prop : NULL;
			s_GridLayoutSmoke.props[i] = prop;
			s_GridLayoutSmoke.prop_generations[i] = propGetLifecycleGeneration(prop);
		}
		s_GridLayoutSmoke.player = g_Vars.players[0];
		s_GridLayoutSmoke.stage_index = g_StageIndex;
	}
	s_GridLayoutSmoke.rooms = g_Rooms;
	s_GridLayoutSmoke.frame = g_Vars.lvframe60;
	if (bootGridLayoutRetrySmoke()) s_GridLayoutSmoke.match_time = lvGetStageTime60();
	if (s_GridDraftLayoutDecoration && !bootGridLayoutDecorationWitness(draft, 0, "before")) {
		bootGridDraftFail("grid_layout_decoration_apply_baseline");
		return 0;
	}
	if (!netGridEditApplyStagedLayout()) {
		bootGridDraftFail("grid_layout_prepare");
		return 0;
	}
	s_GridLayoutSmoke.requested = 1;
	return 1;
}

static void bootGridRoomExtensionReviewHost(const grid_edit_room_state_t *draft, u32 now)
{
	const u32 timeout = s_GridDraftTransferReview ? GRID_EXTENSION_TRANSFER_SMOKE_TIMEOUT_MS
		: GRID_EXTENSION_REVIEW_SMOKE_TIMEOUT_MS;
	if ((u32)(now - s_GridDraftStepTick) >= timeout) {
		bootGridDraftFail("grid_extension_review_host_timeout");
		return;
	}
	if (s_GridDraftApplyLayout && s_GridLayoutSmoke.requested) {
		bootGridLayoutSmokeCheck(draft);
		return;
	}
	if (!bootGridRoomExtensionOriginalWorld(&s_GridRoomExtensionWorld, s_GridRoomExtensionCatalog)) {
		bootGridDraftFail("grid_extension_review_changed_world");
		return;
	}

	grid_edit_variant_review_info_t review;
	if (!s_GridRoomExtensionOffer.offer_serial) {
		const s32 ready = s_GridDraftTransferReview ? netGridEditCanOfferStagedReviewTransfer()
			: netGridEditCanOfferStagedReview();
		if (!ready) return;
		const s32 sent = s_GridDraftTransferReview ? netGridEditOfferStagedReviewTransfer()
			: netGridEditOfferStagedReview();
		if (!sent || !netGridEditGetStagedReview(&review) || review.reply
				|| !bootGridRoomExtensionReviewMatches(&review.variant, draft)) {
			bootGridDraftFail("grid_extension_review_offer");
			return;
		}
		char sha[SHA256_HEX_SIZE];
		sha256ToHex(review.variant.arena_sha256, sha);
		if (strcmp(sha, s_GridRoomExtension.arena_sha256)) {
			bootGridDraftFail("grid_extension_review_wrong_revision");
			return;
		}
		s_GridRoomExtensionOffer = review.variant;
		return;
	}

	if (!netGridEditGetStagedReview(&review) || !gridEditVariantReviewMatches(&s_GridRoomExtensionOffer, &review.variant)) {
		bootGridDraftFail("grid_extension_review_offer_changed");
		return;
	}
	if (!review.reply) return;
	const grid_edit_variant_review_reply_e expected = s_GridDraftPreparedReview
		? GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED : GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
	if (review.reply != expected || (s_GridDraftPreparedReview && !review.sources_prepared)) {
		bootGridDraftFail("grid_extension_review_wrong_receipt");
		return;
	}
	/* Keep the transfer alive until the guest smoke has read its own retained result. */
	if (s_GridDraftTransferReview && !bootGridRoomTransferCreatorObserved(draft, &review.variant)) return;
	if (s_GridDraftApplyLayout && !netGridEditCanApplyStagedLayout()) return;
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.EXTENSION: review %s room=%u generation=%llu epoch=%u revision=%llu serial=%llu sha256=%s",
		s_GridDraftTransferReview ? "transferred" : s_GridDraftPreparedReview ? "prepared" : "acknowledged",
		(unsigned)s_GridDraftRoom,
		(unsigned long long)draft->invite.identity.generation, review.variant.stage_epoch,
		(unsigned long long)review.variant.draft_revision, (unsigned long long)review.variant.offer_serial,
		s_GridRoomExtension.arena_sha256);
	if (s_GridDraftApplyLayout) {
		bootGridLayoutSmokeRequest(draft);
		return;
	}
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
	mainEndStage();
}

static s32 bootGridRoomTransferHostShaMatches(const grid_edit_variant_review_t *offer, const char *sha)
{
	static boot_grid_witness_reader_t host_log;
	const char *prefix = s_GridLayoutSmoke.second ? "SMOKE.GRID.RELOCATE: staged "
		: "SMOKE.GRID.EXTENSION: resident unchanged ";
	char record[384];
	if (!bootGridPlaytestReadWitness(&host_log, "./logs/game client/pd-host.log", prefix,
			record, sizeof(record))) return 0;
	unsigned epoch, catalog;
	unsigned long long revision;
	char expected[SHA256_HEX_SIZE] = {0};
	const char *layout = s_GridLayoutSmoke.second
		? "epoch=%u catalog=%u revision=%llu room2_max=3000 target=2800 sha256=%64[0-9a-f]"
		: "epoch=%u catalog=%u revision=%llu room1_max=0 room2_max=2700 refused_length=1400 sha256=%64[0-9a-f]";
	if (sscanf(record, layout,
			&epoch, &catalog, &revision, expected) != 4 || !catalog
			|| epoch != offer->stage_epoch || revision != offer->draft_revision || strcmp(expected, sha) != 0) return -1;
	return 1;
}

static s32 bootGridLayoutRetryCreator(const grid_edit_room_state_t *draft,
		const grid_edit_variant_review_info_t *review)
{
	grid_playtest_view_t view;
	if (review->apply_pending || !netGridEditGetPlaytestView(&view)) return 0;
	static boot_grid_witness_reader_t own_log;
	char prefix[200], record[280];
	snprintf(prefix, sizeof(prefix),
		"NET.GRID.LAYOUT: event=outcome side=creator room=%u epoch=%u previous=0 next=1 serial=%llu actor=%llu result=4 ",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)review->variant.offer_serial,
		(unsigned long long)draft->local_actor);
	if (!bootGridPlaytestReadWitness(&own_log, "./logs/game client/pd-grid-creator.log",
			prefix, record, sizeof(record))) return 0;
	grid_edit_room_control_t receipt;
	if (!view.has_world_bounds || view.layout_generation || view.max_x != 2700.0f
			|| !bootGridLayoutProjectionMatches(&view, 2700) || netGridEditGetObservedLayout(&receipt)
			|| !review->sources_prepared || draft->revision != review->variant.draft_revision) {
		bootGridDraftFail("grid_layout_retry_creator_projection");
		return 0;
	}
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.LAYOUT.RETRY: creator intact room=%u epoch=%u serial=%llu revision=%llu layout=0 room2_max=2700 actors=4 retained=1 lobby=1",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)review->variant.offer_serial,
		(unsigned long long)draft->revision);
	return 1;
}

static void bootGridRoomExtensionTransferCreator(const grid_edit_room_state_t *draft)
{
	static struct {
		s32 captured;
		u32 catalog;
		s32 stage;
		s32 stage_index;
		s32 room_count;
		const struct room *rooms;
		grid_edit_variant_review_t accepted;
		u32 arena_bytes;
	} before;
	const char *arena = s_GridLayoutSmoke.second ? kGridLayoutRelocationArena : "mod:arena_grid_extension_staged";
	const asset_entry_t *mesh = assetCatalogResolve("example:tri_mesh");
	if (!mesh || !mesh->enabled || mesh->type != ASSET_MODEL || assetCatalogResolveAny(arena)
			|| g_NetStageEpoch || !gridMatchRuntimeIsIdle() || !g_NetLocalClient
			|| g_NetLocalClient->state != CLSTATE_LOBBY || g_NetLocalClient->stage_ready
			|| g_NetLocalClient->room_id != s_GridDraftRoom || draft->local_actor != s_GridDraftInitialActor) {
		bootGridDraftFail("grid_transfer_review_creator_source_or_scope");
		return;
	}
	if (!before.captured) {
		before.catalog = assetCatalogGetGeneration();
		before.stage = g_StageNum;
		before.stage_index = g_StageIndex;
		before.room_count = g_Vars.roomcount;
		before.rooms = g_Rooms;
		before.captured = 1;
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.TRANSFER: creator baseline room=%u catalog=%u stage=0x%02x variant_absent=1 model=example:tri_mesh",
			(unsigned)s_GridDraftRoom, before.catalog, before.stage);
	}
	if (assetCatalogGetGeneration() != before.catalog || g_StageNum != before.stage
			|| g_StageIndex != before.stage_index || g_Vars.roomcount != before.room_count || g_Rooms != before.rooms) {
		bootGridDraftFail("grid_transfer_review_creator_changed_world");
		return;
	}
	grid_edit_variant_review_info_t review;
	if (!netGridEditGetStagedReview(&review)) {
		if (before.accepted.offer_serial) bootGridDraftFail("grid_transfer_review_creator_offer_lost");
		return;
	}
	/* The first prepared review can remain visible until its actual Apply
	 * outcome. The new offer must carry a fresh serial in the same round. */
	if (s_GridLayoutSmoke.second && review.variant.offer_serial == s_GridLayoutSmoke.first_serial) return;
	if (!bootGridRoomExtensionReviewMatches(&review.variant, draft) || !review.includes_dependencies) {
		bootGridDraftFail("grid_transfer_review_creator_offer_scope");
		return;
	}
	char sha[SHA256_HEX_SIZE];
	sha256ToHex(review.variant.arena_sha256, sha);
	if (!before.accepted.offer_serial) {
		if (!review.transfer_available) return;
		if (s_GridDraftLayoutReconnect && !s_GridLayoutSmoke.second && !s_GridLayoutReconnect.catalog) {
			grid_playtest_view_t view;
			grid_edit_room_control_t receipt;
			if (!netGridEditGetPlaytestView(&view)) return;
			if (!bootGridLayoutProjectionMatches(&view, 2700) || netGridEditGetObservedLayout(&receipt)) {
				bootGridDraftFail("grid_layout_reconnect_original_projection");
				return;
			}
			s_GridLayoutReconnect.catalog = before.catalog;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LAYOUT.RECONNECT: original room=%u epoch=%u rooms=2 actors=4 seam_x=%u",
				(unsigned)s_GridDraftRoom, view.epoch, (unsigned)view.rooms[0].max_x);
		}
		const s32 match = bootGridRoomTransferHostShaMatches(&review.variant, sha);
		if (!match) return;
		if (match < 0 || !review.transfer_offered || !review.arena_bytes || review.reply
				|| review.sources_prepared || !netGridEditAcceptStagedReviewTransfer()) {
			bootGridDraftFail("grid_transfer_review_creator_consent");
			return;
		}
		before.accepted = review.variant;
		before.arena_bytes = review.arena_bytes;
		return;
	}
	if (!gridEditVariantReviewMatches(&before.accepted, &review.variant) || review.arena_bytes != before.arena_bytes) {
		bootGridDraftFail("grid_transfer_review_creator_revision_changed");
		return;
	}
	if (!review.reply) return;
	if (review.reply != GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED || !review.sources_prepared) {
		bootGridDraftFail("grid_transfer_review_creator_not_prepared");
		return;
	}
	const s32 retry = bootGridLayoutRetrySmoke();
	if (!retry || !s_GridLayoutSmoke.retry_reported_transfer) {
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.EXTENSION: creator transferred room=%u generation=%llu epoch=%u revision=%llu serial=%llu sha256=%s installed=0 catalog=%u stage=0x%02x",
			(unsigned)s_GridDraftRoom, (unsigned long long)draft->invite.identity.generation, review.variant.stage_epoch,
			(unsigned long long)review.variant.draft_revision, (unsigned long long)review.variant.offer_serial,
			sha, before.catalog, before.stage);
		s_GridLayoutSmoke.retry_reported_transfer = retry;
	}
	if (retry && !bootGridLayoutRetryCreator(draft, &review)) return;
	if (s_GridDraftRelocateLayout && !s_GridLayoutSmoke.second) {
		s_GridLayoutSmoke.second = 1;
		s_GridLayoutSmoke.first_serial = review.variant.offer_serial;
		memset(&before.accepted, 0, sizeof(before.accepted));
		before.arena_bytes = 0;
		s_GridDraftStepTick = SDL_GetTicks();
		return;
	}
	if (s_GridDraftLayoutReconnect) {
		s_GridRoomExtensionOffer = review.variant;
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_DISCONNECT;
		s_GridDraftStepTick = SDL_GetTicks();
		return;
	}
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
}

static void bootGridRoomExtensionReviewCreator(const grid_edit_room_state_t *draft, s32 has_draft)
{
	const u32 timeout = s_GridDraftTransferReview ? GRID_EXTENSION_TRANSFER_SMOKE_TIMEOUT_MS
		: GRID_EXTENSION_REVIEW_SMOKE_TIMEOUT_MS;
	if ((u32)(SDL_GetTicks() - s_GridDraftStepTick) >= timeout) {
		bootGridDraftFail("grid_extension_review_creator_timeout");
		return;
	}
	if (!has_draft || !draft->ready || draft->is_host
			|| draft->invite.identity.generation != s_GridDraftInitialInvite.identity.generation
			|| !bootGridDraftSameSource(&draft->invite, g_NetLocalClient)) {
		bootGridDraftFail("grid_extension_review_creator_authority");
		return;
	}
	if (s_GridDraftTransferReview) {
		bootGridRoomExtensionTransferCreator(draft);
		return;
	}
	grid_edit_variant_review_info_t offered;
	if (!netGridEditGetStagedReview(&offered)) return;
	const u32 catalog = assetCatalogGetGeneration();
	const s32 installed = assetCatalogResolveAny(offered.variant.arena_id) != NULL;
	char sha[SHA256_HEX_SIZE];
	sha256ToHex(offered.variant.arena_sha256, sha);
	const grid_edit_variant_review_reply_e expected = s_GridDraftPreparedReview
		? GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED : GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
	grid_edit_variant_review_info_t replied;
	if (offered.reply || !bootGridRoomExtensionReviewMatches(&offered.variant, draft)
			|| installed != s_GridDraftPreparedReview
			|| (s_GridDraftPreparedReview && strcmp(sha, s_GridPreparedCreatorArenaSha) != 0)
			|| !netGridEditReplyToStagedReview(1) || !netGridEditGetStagedReview(&replied)
			|| replied.reply != expected || (s_GridDraftPreparedReview && !replied.sources_prepared)
			|| !gridEditVariantReviewMatches(&offered.variant, &replied.variant)
			|| assetCatalogGetGeneration() != catalog) {
		bootGridDraftFail("grid_extension_review_creator_receipt");
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.EXTENSION: creator %s room=%u generation=%llu epoch=%u revision=%llu serial=%llu sha256=%s installed=%d",
		s_GridDraftPreparedReview ? "prepared" : "acknowledged", (unsigned)s_GridDraftRoom,
		(unsigned long long)draft->invite.identity.generation,
		replied.variant.stage_epoch, (unsigned long long)replied.variant.draft_revision,
		(unsigned long long)replied.variant.offer_serial, sha, installed);
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
}

static void bootGridRoomExtensionTick(const grid_edit_room_state_t *draft, u32 now)
{
	if (!draft->ready || draft->revision != s_GridRoomExtension.draft_revision
			|| !bootGridRoomExtensionCandidateMatches()) {
		bootGridDraftFail("grid_extension_candidate_lost");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW) {
		bootGridRoomExtensionReviewHost(draft, now);
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_ROOM) {
		if (mainGridRoomReturnPending() || !mainGridRoomBackdropActive() || !gridMatchRuntimeIsIdle()
				|| netGridEditPrivatePlaytestRoom(s_GridDraftRoom) || g_NetStageEpoch
				|| g_NetLocalClient->state != CLSTATE_LOBBY) return;
		if (s_GridDraftPreparedReview) {
			grid_edit_variant_review_info_t review;
			if (netGridEditGetStagedReview(&review) || !bootGridDraftIsHostPeer(1)
					|| !bootGridDraftIsHostPeer(2) || assetCatalogResolveAny(s_GridRoomExtension.arena_id)) {
				bootGridDraftFail("grid_prepared_review_did_not_retire");
				return;
			}
			if (s_GridDraftLayoutDecoration && !bootGridDecorationDraftMatches(
					s_GridDecorationNative.moved_position.x, s_GridDecorationNative.moved_position.z)) {
				bootGridDraftFail("grid_layout_decoration_return_pose");
				return;
			}
			sysLogPrintf(LOG_NOTE,
				"SMOKE.GRID.EXTENSION: prepared review retired room=%u epoch=%u revision=%llu creator_lobby=1 ordinary_lobby=1",
				(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)draft->revision);
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
			s_GridDraftStepTick = now;
			return;
		}
		if (s_GridRoomExtensionEpoch) {
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.EXTENSION: returned room=%u epoch=%u revision=%llu ordinary_lobby=1",
				(unsigned)s_GridDraftRoom, s_GridRoomExtensionEpoch, (unsigned long long)draft->revision);
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
			s_GridDraftStepTick = now;
			return;
		}
		char ordinary[CATALOG_ID_LEN];
		if (!netGridEditSaveVariant("Grid Extension Ordinary", ordinary, sizeof(ordinary))
				|| strcmp(ordinary, "mod:arena_grid_extension_ordinary")
				|| strcmp(ordinary, netGridEditSavedVariantId())
				|| !netGridEditHasSavedVariant(ordinary)
				|| !netGridEditHasSavedVariant(s_GridRoomExtension.arena_id)
				|| !bootGridRoomExtensionCandidateMatches()
				|| assetCatalogResolveAny(s_GridRoomExtension.arena_id)) {
			bootGridDraftFail("grid_extension_ordinary_save_replaced_candidate");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.EXTENSION: room saved=%s staged=%s revision=%llu sha256=%s",
			ordinary, s_GridRoomExtension.arena_id, (unsigned long long)draft->revision,
			s_GridRoomExtension.arena_sha256);
		if (!netGridEditStartPrivateVariantPlaytest(s_GridRoomExtension.arena_id, 3, BOTDIFF_NORMAL)) {
			bootGridDraftFail("grid_extension_next_round_start");
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_EXTENSION_ACTIVE;
		return;
	}

	const grid_match_runtime_identity_t *active = gridMatchRuntimeActive();
	s32 ambient;
	f32 native, rendered;
	if (!active || !g_NetStageEpoch || !netGridEditPrivatePlaytestAmbient(&ambient)
			|| ambient != GRID_DRAFT_SMOKE_BRIGHTNESS
			|| !bootGridPlaytestReadLight(g_StageNum, ambient, &native, &rendered)) return;
	char sha[SHA256_HEX_SIZE];
	sha256ToHex(active->arena_sha256, sha);
	if (g_NetStageEpoch <= s_GridPlaytestEpoch || active->stage != g_StageNum
			|| active->offer.room_id != s_GridDraftRoom || strcmp(active->arena_id, s_GridRoomExtension.arena_id)
			|| strcmp(sha, s_GridRoomExtension.arena_sha256) || !bootGridRoomExtensionBounds(3000.0f)) {
		bootGridDraftFail("grid_extension_next_round_bounds");
		return;
	}
	s_GridRoomExtensionEpoch = g_NetStageEpoch;
	sysLogPrintf(LOG_NOTE,
		"SMOKE.GRID.EXTENSION: loaded room=%u previous_epoch=%u epoch=%u arena=%s sha256=%s room1_min=%.0f room2_max=%.0f room2_center=%.0f",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, s_GridRoomExtensionEpoch, active->arena_id, sha,
		g_Rooms[1].bbmin[0], g_Rooms[2].bbmax[0], g_Rooms[2].centre.x);
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
	mainEndStage();
}

static void bootGridPlaytestHostTick(const grid_edit_room_state_t *draft, u32 now)
{
	if ((s_GridDraftLiveReconnect || s_GridDraftLayoutReconnect) && (s_GridDraftStep >= BOOT_GRID_DRAFT_WAIT_LIVE_DETACH
			&& s_GridDraftStep <= BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN)) {
		bootGridLiveReconnectHostTick(draft, now);
		return;
	}
	const u64 baseline_revision = s_GridDraftVariantPlaytest ? 2 : 1;
	const u64 edited_revision = baseline_revision + 1;
	const u64 return_revision = edited_revision + (s_GridDraftDecoration || s_GridDraftLayoutDecoration)
		+ (s_GridDraftLayoutAmbientOff != 0);
	if (!draft->is_host || !bootGridDraftSameSource(&draft->invite, &g_NetClients[1])
			|| draft->invite.identity.generation != s_GridDraftInitialInvite.identity.generation) {
		bootGridDraftFail("grid_playtest_draft_authority");
		return;
	}
	if (s_GridDraftRoomExtension && (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW
			|| s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_ROOM
			|| s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_ACTIVE)) {
		bootGridRoomExtensionTick(draft, now);
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_START) {
		if (!draft->ready || draft->revision < baseline_revision) return;
		if (draft->revision != baseline_revision || !draft->enabled
				|| draft->brightness != GRID_DRAFT_SMOKE_BASE_BRIGHTNESS) {
			bootGridDraftFail("grid_playtest_baseline_draft");
			return;
		}
		if (s_GridDraftVariantPlaytest && !bootGridPlaytestSavedStartVariant()) {
			bootGridDraftFail("grid_playtest_saved_variant");
			return;
		}
		const s32 started = s_GridDraftVariantPlaytest
			? netGridEditStartPrivateVariantPlaytest(s_GridPlaytestVariant.arena_id, 3, BOTDIFF_NORMAL)
			: netGridEditStartPrivatePlaytest(3, BOTDIFF_NORMAL);
		if (!started) {
			bootGridDraftFail("grid_playtest_launch");
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE;
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE) {
		s32 current;
		f32 native;
		if (!g_NetStageEpoch || !netGridEditPrivatePlaytestAmbient(&current)
				|| current != GRID_DRAFT_SMOKE_BASE_BRIGHTNESS
				|| !bootGridPlaytestReadLight(g_StageNum, current, &native, &s_GridPlaytestBaseline)) return;
		if (s_GridDraftVariantPlaytest && !bootGridPlaytestCheckActiveVariant()) {
			bootGridDraftFail("grid_playtest_loaded_variant");
			return;
		}
		s_GridPlaytestEpoch = g_NetStageEpoch;
		s_GridPlaytestStage = g_StageNum;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: release_creator room=%u generation=%llu epoch=%u",
			(unsigned)s_GridDraftRoom, (unsigned long long)draft->invite.identity.generation,
			s_GridPlaytestEpoch);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_EDIT;
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_EDIT) {
		if (g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage) {
			bootGridDraftFail("grid_playtest_epoch_changed");
			return;
		}
		s32 current;
		f32 native, rendered;
		if (draft->revision != edited_revision || !netGridEditPrivatePlaytestAmbient(&current)
				|| current != GRID_DRAFT_SMOKE_BRIGHTNESS
				|| !bootGridPlaytestReadLight(g_StageNum, current, &native, &rendered)) return;
		if (!draft->ready || !draft->enabled || draft->brightness != current
				|| rendered <= native || rendered <= s_GridPlaytestBaseline) {
			bootGridDraftFail("grid_playtest_light_did_not_change");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: rendered edit epoch=%u revision=%llu before=%.1f native=%.1f after=%.1f",
			s_GridPlaytestEpoch, (unsigned long long)draft->revision, s_GridPlaytestBaseline, native, rendered);
		if (s_GridDraftLiveReconnect) {
			s_GridPlaytestBaseline = rendered;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: release_disconnect room=%u epoch=%u",
				(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_DETACH;
			s_GridDraftStepTick = now;
			return;
		}
		if (s_GridDraftVisibility) {
			if (!bootGridVisibilityReadCursorSpawn()) {
				bootGridDraftFail("grid_view_host_spawn");
				return;
			}
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_VISIBILITY;
			return;
		}
		if (s_GridDraftDecoration || s_GridDraftLayoutDecoration) {
			if (!bootGridDecorationStartHost(now)) bootGridDraftFail("grid_decoration_host_baseline");
			return;
		}
		if (s_GridDraftRoomExtension) {
			if (s_GridDraftLayoutReconnect) s_GridPlaytestBaseline = rendered;
			bootGridRoomExtensionStage(draft);
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN;
		mainEndStage();
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_VISIBILITY) {
		if (g_NetStageEpoch != s_GridPlaytestEpoch || g_StageNum != s_GridPlaytestStage
				|| !draft->ready || draft->revision != edited_revision || draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS) {
			bootGridDraftFail("grid_view_stage_changed");
			return;
		}
		bootGridVisibilityHostTick(now);
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_REJECTION
			|| s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE) {
		bootGridDecorationHostTick(draft, now);
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_LAYOUT_AMBIENT_OFF) {
		bootGridLayoutAmbientOffTick(draft, now);
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETURN) {
		if (g_NetStageEpoch || !g_MainIsEndscreen || g_NetLocalClient->state != CLSTATE_LOBBY) return;
		s32 enabled, brightness;
		if (!draft->ready || draft->revision != return_revision
				|| draft->enabled != !s_GridDraftLayoutAmbientOff
				|| draft->brightness != (s_GridDraftLayoutAmbientOff ? 0 : GRID_DRAFT_SMOKE_BRIGHTNESS)
				|| netGridEditPrivatePlaytestAmbientState(&enabled, &brightness)) {
			bootGridDraftFail("grid_playtest_return_state");
			return;
		}
		/* Same exit, Room open and resync actions as Combat Simulator Continue. */
		extern void pdguiEndscreenExitToRoom(void);
		extern void pdguiSetInRoom(s32 inRoom);
		pdguiEndscreenExitToRoom();
		pdguiSetInRoom(1);
		netSendLobbyResync();
		if (s_GridDraftRoomExtension) {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_EXTENSION_ROOM;
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETIRED;
		s_GridDraftStepTick = now;
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_RETIRED) {
		if ((u32)(now - s_GridDraftStepTick) >= GRID_PLAYTEST_RETURN_SMOKE_TIMEOUT_MS) {
			bootGridDraftFail("grid_playtest_continue_timeout");
			return;
		}
		/* Continue queues the stage stop. Keep the draft and both peers until
		 * the next Room backdrop has released the old match world. */
		if (mainGridRoomReturnPending() || !mainGridRoomBackdropActive() || !gridMatchRuntimeIsIdle()) return;
		if (netGridEditPrivatePlaytestRoom(s_GridDraftRoom) || g_NetStageEpoch
				|| g_NetLocalClient->state != CLSTATE_LOBBY || !draft->ready || !draft->enabled
				|| draft->revision != return_revision || draft->brightness != GRID_DRAFT_SMOKE_BRIGHTNESS) {
			bootGridDraftFail("grid_playtest_continue_state");
			return;
		}
		if (s_GridDraftVariantPlaytest) {
			grid_edit_move_spawn_t spawn;
			if (!netGridEditGetSpawnPose(0, &spawn) || strcmp(spawn.spawn_id, s_GridPlaytestMovedSpawn.spawn_id)
					|| !bootGridPlaytestPoseMatches(&s_GridPlaytestMovedSpawn, spawn.position, spawn.look)) {
				bootGridDraftFail("grid_playtest_continue_spawn");
				return;
			}
		}
		if (s_GridDraftDecoration) {
			const f32 x = s_GridDecorationOriginal.x + GRID_DECORATION_SMOKE_X_DELTA;
			const f32 z = s_GridDecorationOriginal.z;
			if (!bootGridDecorationDraftMatches(x, z)) {
				bootGridDraftFail("grid_decoration_continue_pose");
				return;
			}
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DECORATION: retained room=%u epoch=%u record=%s revision=3 x=%.1f z=%.1f",
				(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, s_GridDecorationOriginal.record_id, x, z);
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: returned room=%u epoch=%u revision=%llu ambient=%d",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch, (unsigned long long)draft->revision, draft->brightness);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
		s_GridDraftStepTick = now;
	}
}

static void bootGridDraftHostTick(void)
{
	if (s_GridDraftStep == BOOT_GRID_DRAFT_DONE || g_NetMode != NETMODE_SERVER
			|| g_NetDedicated || !g_NetLocalClient) return;
	if (s_GridDraftRoom == 0xff || g_NetLocalClient->room_id != s_GridDraftRoom) {
		bootGridDraftFail("grid_draft_host_room");
		return;
	}
	const u32 now = SDL_GetTicks();
	if (s_GridDraftStep == BOOT_GRID_DRAFT_FLUSH_CLOSE) {
		/* The creator exits on the targeted close. Keep servicing the
		 * reliable transport with the ordinary peer still in this room. */
		if (!bootGridDraftIsHostPeer(2)) {
			bootGridDraftFail("grid_draft_ordinary_left_before_shutdown");
			return;
		}
		if ((u32)(now - s_GridDraftStepTick) >= 3000u) {
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host close_flushed room=%u ordinary_lobby=1",
				(unsigned)s_GridDraftRoom);
			s_GridDraftStep = BOOT_GRID_DRAFT_DONE;
			smokeHarnessExit(0, "grid_draft_complete");
		}
		return;
	}
	const s32 creator_returning = (s_GridDraftLiveReconnect || s_GridDraftLayoutReconnect)
		&& s_GridDraftStep >= BOOT_GRID_DRAFT_WAIT_LIVE_DETACH
		&& s_GridDraftStep <= BOOT_GRID_DRAFT_WAIT_LIVE_ROOM;
	if ((!creator_returning && !bootGridDraftIsHostPeer(1)) || !bootGridDraftIsHostPeer(2)) {
		bootGridDraftFail("grid_draft_host_members");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_ROOM) {
		if (!netGridEditCanInvite(1)) return;
		if (!netGridEditInvite(1, g_BootLaunchMpArena, NULL)) {
			bootGridDraftFail("grid_draft_invite");
			return;
		}
		if (s_GridDraftResume || s_GridDraftPlaytest) {
			grid_edit_room_state_t offered;
			if (!netGridEditGetState(&offered) || !offered.pending_invite || !offered.is_host
					|| !offered.peer_actor || !bootGridDraftRecordInitialInvite(&offered.invite, &g_NetClients[1])) {
				bootGridDraftFail("grid_draft_host_initial_offer");
				return;
			}
			s_GridDraftInitialActor = offered.peer_actor;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host room=%u creator=1 ordinary=2",
			(unsigned)s_GridDraftRoom);
		s_GridDraftStep = s_GridDraftPlaytest ? BOOT_GRID_DRAFT_WAIT_PLAYTEST_START : BOOT_GRID_DRAFT_WAIT_COMMIT;
		return;
	}

	grid_edit_room_state_t state;
	if (!netGridEditGetState(&state)) {
		bootGridDraftFail("grid_draft_authority_lost");
		return;
	}
	if (s_GridDraftPlaytest && s_GridDraftStep != BOOT_GRID_DRAFT_WAIT_CLOSE) {
		bootGridPlaytestHostTick(&state, now);
		return;
	}
	if (s_GridDraftResume && s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_COMMIT) {
		if (!state.creator_detached) return;
		if (!state.is_host || state.ready || state.pending_invite || !state.enabled || state.revision != 2
				|| state.brightness != GRID_DRAFT_SMOKE_BRIGHTNESS || state.peer_actor
				|| state.invite.identity.generation != s_GridDraftInitialInvite.identity.generation
				|| !bootGridDraftSameSource(&state.invite, &g_NetClients[1])) {
			bootGridDraftFail("grid_draft_host_retained_value");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host retained revision=2 ambient=%d generation=%llu source_sha256=%s",
			state.brightness, (unsigned long long)state.invite.identity.generation, state.invite.arena_sha256);
		if (!netGridEditCanInvite(1) || !netGridEditInvite(1, state.invite.arena_id, state.invite.arena_sha256)
				|| !netGridEditGetState(&state) || !state.pending_invite || !state.pending_resume || !state.snapshot_ready
				|| state.ready || state.revision != 2 || state.peer_actor <= s_GridDraftInitialActor
				|| state.invite.identity.generation <= s_GridDraftInitialInvite.identity.generation
				|| !bootGridDraftSameSource(&state.invite, &g_NetClients[1])) {
			bootGridDraftFail("grid_draft_host_resume_invite");
			return;
		}
		s_GridDraftResumeGeneration = state.invite.identity.generation;
		s_GridDraftResumeActor = state.peer_actor;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host resume_invite generation=%llu actor=%llu source_sha256=%s",
			(unsigned long long)s_GridDraftResumeGeneration, (unsigned long long)s_GridDraftResumeActor,
			state.invite.arena_sha256);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT;
		return;
	}
	if (s_GridDraftResume && s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT && state.revision == 2) {
		bootGridDraftHostCommitResumeTail(&state, &g_NetClients[1]);
		return;
	}
	const u64 revision = s_GridDraftResume ? 4 : 2;
	const s32 brightness = s_GridDraftResume ? GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS : GRID_DRAFT_SMOKE_BRIGHTNESS;
	const enum boot_grid_private_draft_step commit_step = s_GridDraftResume
		? BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT : BOOT_GRID_DRAFT_WAIT_COMMIT;
	if (s_GridDraftStep == commit_step && state.ready && state.revision == revision) {
		const char *name = s_GridDraftResume ? "Grid_Resume_Draft_Variant" : "Grid_Shared_Draft_Variant";
		const char *expected = s_GridDraftResume ? "mod:arena_grid_resume_draft_variant" : "mod:arena_grid_shared_draft_variant";
		char arena[CATALOG_ID_LEN];
		if (!state.is_host || !state.enabled || state.brightness != brightness
				|| (s_GridDraftResume && (state.pending_resume || state.peer_actor != s_GridDraftResumeActor
					|| state.invite.identity.generation != s_GridDraftResumeGeneration
					|| !bootGridDraftSameSource(&state.invite, &g_NetClients[1])))
				|| !netGridEditSaveVariant(name, arena, sizeof(arena)) || strcmp(arena, expected) != 0
				|| (s_GridDraftResume && !bootGridDraftVerifySavedVariant(arena))) {
			bootGridDraftFail("grid_draft_save_variant");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host revision=%llu ambient=%d",
			(unsigned long long)revision, state.brightness);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
		s_GridDraftStepTick = SDL_GetTicks();
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_CLOSE
			&& (u32)(now - s_GridDraftStepTick) >= 1500u) {
		netGridEditClose();
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=host closed room=%u creator_lobby=1 ordinary_lobby=1",
			(unsigned)s_GridDraftRoom);
		s_GridDraftStep = BOOT_GRID_DRAFT_FLUSH_CLOSE;
		s_GridDraftStepTick = SDL_GetTicks();
	}
}

static void bootGridLiveReconnectClientTick(u32 now)
{
	if (g_NetStageEpoch || (u32)(now - s_GridDraftStepTick) >= GRID_LIVE_RECONNECT_SMOKE_TIMEOUT_MS) {
		bootGridDraftFail("grid_live_reconnect_creator_stage_or_timeout");
		return;
	}
	char prefix[160];
	char record[80];
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_LIVE_DISCONNECT) {
		if (s_GridDraftLayoutReconnect && !s_GridLayoutReconnect.before_disconnect.apply_layout_generation) {
			grid_edit_room_state_t draft;
			if (!g_NetLocalClient || g_NetLocalClient->state != CLSTATE_LOBBY
					|| !netGridEditGetState(&draft) || !draft.ready || draft.local_actor != s_GridDraftInitialActor
					|| draft.revision != 2 || !bootGridDraftSameSource(&draft.invite, g_NetLocalClient)) {
				bootGridDraftFail("grid_layout_reconnect_creator_before_disconnect");
				return;
			}
			if (!bootGridLayoutReconnectObserve(&draft, 0)) return;
		}
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.LIVE_RECONNECT: release_disconnect room=%u epoch=%u",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
		grid_edit_room_state_t draft;
		if (!netGridEditGetState(&draft) || !draft.ready || draft.revision != 2
				|| draft.brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
				|| !bootGridDraftSameSource(&draft.invite, g_NetLocalClient)
				|| netDisconnect() != 0 || g_NetMode != NETMODE_NONE || netGridEditGetState(&draft)
				|| (s_GridDraftLayoutReconnect && !bootGridLayoutProjectionHidden())) {
			bootGridDraftFail("grid_live_reconnect_creator_disconnect");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: creator disconnected room=%u epoch=%u revision=2",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_AUTH;
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_LIVE_AUTH) {
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.LIVE_RECONNECT: detached room=%u epoch=%u revision=2 ",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
		const char *address = sysArgGetString("--connect-host");
		if (g_NetMode != NETMODE_NONE || !address || strncmp(address, "127.0.0.1:", 10) != 0
				|| netStartClient(address) != 0) {
			bootGridDraftFail("grid_live_reconnect_creator_reauth");
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_ROOM_INVITE;
		return;
	}
	if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient || g_NetLocalClient->state < CLSTATE_LOBBY) return;
	struct netclient *local = g_NetLocalClient;
	grid_edit_room_state_t draft;
	if (!local->peer || local->id != 1 || local->state != CLSTATE_LOBBY || local->stage_ready
			|| (local->flags & CLFLAG_SPECTATOR) || netGridEditGetState(&draft)
			|| (s_GridDraftLayoutReconnect && !bootGridLayoutProjectionHidden())) {
		bootGridDraftFail("grid_live_reconnect_creator_room_authority");
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_LIVE_ROOM_INVITE) {
		grid_edit_room_invite_t invite;
		if (local->room_id != 0xff || local->peer->connectID == s_GridDraftConnection
				|| netGridEditPendingInvite(&invite)) {
			bootGridDraftFail("grid_live_reconnect_creator_lounge");
			return;
		}
		s32 advertised = 0;
		for (s32 i = 0; i < g_RoomCacheCount; ++i) {
			const room_cache_entry_t *room = &g_RoomCache[i];
			if (room->id != s_GridDraftRoom) continue;
			advertised = room->state == ROOM_STATE_LOBBY && room->access == ROOM_ACCESS_INVITE
				&& room->client_count == 2 && room->client_count < room->max_players;
			break;
		}
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.LIVE_RECONNECT: room_invite room=%u epoch=%u client=1 ",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		if (!advertised || !bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
		s_GridDraftConnection = local->peer->connectID;
		if (netRequestRoomJoin(s_GridDraftRoom, "") != 0) {
			bootGridDraftFail("grid_live_reconnect_creator_join");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: creator requested Room join room=%u epoch=%u fresh_connection=1",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_ROOM;
		return;
	}
	if (local->room_id == 0xff) {
		if (!netRoomRequestPending()) {
			sysLogPrintf(LOG_ERROR, "SMOKE.GRID.LIVE_RECONNECT: Room join failed: %s", netRoomRequestMessage());
			bootGridDraftFail("grid_live_reconnect_creator_join_result");
		}
		return;
	}
	if (local->room_id != s_GridDraftRoom || local->peer->connectID != s_GridDraftConnection) {
		bootGridDraftFail("grid_live_reconnect_creator_wrong_room");
		return;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LIVE_RECONNECT: creator rejoined room=%u epoch=%u lobby=1 role=none",
		(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
	if (s_GridDraftLayoutReconnect) {
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.LAYOUT.RECONNECT: ungranted room=%u epoch=%u receipt=0 projection=0",
			(unsigned)s_GridDraftRoom, s_GridPlaytestEpoch);
	}
	s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_RESUME_INVITE;
}

static void bootGridDraftClientTick(void)
{
	if (s_GridDraftStep == BOOT_GRID_DRAFT_DONE) return;
	if ((s_GridDraftLiveReconnect || s_GridDraftLayoutReconnect) && s_GridDraftRole == BOOT_GRID_DRAFT_CREATOR
			&& s_GridDraftStep >= BOOT_GRID_DRAFT_WAIT_LIVE_DISCONNECT
			&& s_GridDraftStep <= BOOT_GRID_DRAFT_WAIT_LIVE_ROOM) {
		bootGridLiveReconnectClientTick(SDL_GetTicks());
		return;
	}
	const s32 track_link = s_GridDraftResume || s_GridDraftPlaytest;
	if (track_link && s_GridDraftRole == BOOT_GRID_DRAFT_CREATOR && s_GridDraftRoom != 0xff
			&& (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient || !g_NetLocalClient->peer
				|| g_NetLocalClient->state != CLSTATE_LOBBY || g_NetLocalClient->room_id != s_GridDraftRoom
				|| g_NetLocalClient->peer->connectID != s_GridDraftConnection)) {
		bootGridDraftFail("grid_draft_creator_link_changed");
		return;
	}
	if (s_GridDraftPlaytest && g_NetStageEpoch) {
		bootGridDraftFail("grid_playtest_remote_stage_epoch");
		return;
	}
	if (s_GridDraftRole == BOOT_GRID_DRAFT_ORDINARY && s_GridDraftRoom != 0xff) {
		/* The smoke requires the shutdown reason from the normal transport
		 * log before this marker. Match the last live room until teardown. */
		if (g_NetMode == NETMODE_NONE) {
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=ordinary disconnected room=%u",
				(unsigned)s_GridDraftRoom);
			s_GridDraftStep = BOOT_GRID_DRAFT_DONE;
			smokeHarnessExit(0, "grid_draft_complete");
			return;
		}
		if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient
				|| g_NetLocalClient->state != CLSTATE_LOBBY
				|| g_NetLocalClient->room_id != s_GridDraftRoom) {
			bootGridDraftFail("grid_draft_ordinary_left_room");
			return;
		}
	}
	if (g_NetMode != NETMODE_CLIENT
			|| !g_NetLocalClient || g_NetLocalClient->state != CLSTATE_LOBBY
			|| g_NetLocalClient->room_id == 0xff) return;
	grid_edit_room_invite_t invite;
	grid_edit_room_state_t state;
	const s32 invited = netGridEditPendingInvite(&invite);
	const s32 has_draft = netGridEditGetState(&state);
	if (s_GridDraftRole == BOOT_GRID_DRAFT_ORDINARY) {
		grid_playtest_view_t view;
		if (g_NetLocalClient->id != 2 || invited || has_draft) {
			bootGridDraftFail("grid_draft_ordinary_membership");
			return;
		}
		if (s_GridDraftVisibility && netGridEditGetPlaytestView(&view)) {
			bootGridDraftFail("grid_view_ordinary_disclosure");
			return;
		}
		if (s_GridDraftLayoutReconnect && !bootGridLayoutProjectionHidden()) {
			bootGridDraftFail("grid_layout_reconnect_ordinary_disclosure");
			return;
		}
		if (s_GridDraftRoom == 0xff) {
			s_GridDraftRoom = g_NetLocalClient->room_id;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=ordinary client=2 room=%u lobby=1",
				(unsigned)s_GridDraftRoom);
		}
		return;
	}
	if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_ROOM && invited) {
		s_GridDraftRoom = g_NetLocalClient->room_id;
		if (g_NetLocalClient->id != 1
				|| (track_link && !bootGridDraftRecordInitialInvite(&invite, g_NetLocalClient))
				|| !netGridEditReply(1)) {
			bootGridDraftFail("grid_draft_accept");
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_GRANT;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_GRANT && has_draft && state.ready) {
		if (state.is_host || state.revision != 1
				|| (track_link && (!state.local_actor || !state.enabled
					|| state.brightness != GRID_DRAFT_SMOKE_BASE_BRIGHTNESS
					|| state.invite.identity.generation != s_GridDraftInitialInvite.identity.generation
					|| !bootGridDraftSameSource(&state.invite, g_NetLocalClient)))
				|| (!s_GridDraftPlaytest && !netGridEditSetAmbient(1, GRID_DRAFT_SMOKE_BRIGHTNESS))) {
			bootGridDraftFail("grid_draft_ambient");
			return;
		}
		if (s_GridDraftPlaytest) {
			s_GridDraftInitialActor = state.local_actor;
			if (s_GridDraftVariantPlaytest) {
				if (state.next_sequence != 1 || !netGridEditGetSpawnPose(0, &s_GridPlaytestMovedSpawn)
						|| !bootGridPlaytestOffsetSpawn(&s_GridPlaytestMovedSpawn)
						|| !netGridEditMoveSpawn(&s_GridPlaytestMovedSpawn)) {
					bootGridDraftFail("grid_playtest_creator_move_start");
					return;
				}
				sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: creator moved start=%s x=%.2f room=%u lobby=1",
					s_GridPlaytestMovedSpawn.spawn_id, s_GridPlaytestMovedSpawn.position[0], (unsigned)s_GridDraftRoom);
				s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_SPAWN;
			} else {
				s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE;
			}
			return;
		}
		if (s_GridDraftResume) {
			s_GridDraftInitialActor = state.local_actor;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator initial_grant generation=%llu actor=%llu source_sha256=%s",
				(unsigned long long)state.invite.identity.generation, (unsigned long long)state.local_actor,
				state.invite.arena_sha256);
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_COMMIT;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_SPAWN && has_draft && state.revision == 2) {
		grid_edit_move_spawn_t spawn;
		if (!state.ready || state.pending_command || state.local_actor != s_GridDraftInitialActor || state.next_sequence != 2
				|| !netGridEditGetSpawnPose(0, &spawn) || strcmp(spawn.spawn_id, s_GridPlaytestMovedSpawn.spawn_id)
				|| !bootGridPlaytestPoseMatches(&s_GridPlaytestMovedSpawn, spawn.position, spawn.look)) {
			bootGridDraftFail("grid_playtest_creator_observe_start");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: creator observed start=%s revision=2 x=%.2f",
			spawn.spawn_id, spawn.position[0]);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_BASELINE) {
		char prefix[128];
		char record[48];
		snprintf(prefix, sizeof(prefix), "SMOKE.GRID.PLAYTEST: release_creator room=%u generation=%llu epoch=",
			(unsigned)s_GridDraftRoom, (unsigned long long)s_GridDraftInitialInvite.identity.generation);
		if (!bootGridPlaytestReadHostWitness(prefix, record, sizeof(record))) return;
		unsigned epoch;
		if (sscanf(record, "%u", &epoch) != 1 || !epoch || !has_draft || !state.ready
				|| state.revision != (s_GridDraftVariantPlaytest ? 2u : 1u)
				|| !netGridEditSetAmbient(1, GRID_DRAFT_SMOKE_BRIGHTNESS)) {
			bootGridDraftFail("grid_playtest_creator_edit");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PLAYTEST: creator sent ambient=%d host_epoch=%u room=%u lobby=1",
			GRID_DRAFT_SMOKE_BRIGHTNESS, epoch, (unsigned)s_GridDraftRoom);
		if (s_GridDraftVisibility || s_GridDraftDecoration || s_GridDraftLiveReconnect || s_GridDraftRoomExtension) {
			s_GridPlaytestEpoch = epoch;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_COMMIT;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_COMMIT && has_draft
			&& state.revision == (s_GridDraftVariantPlaytest ? 3u : 2u)) {
		if (state.pending_command || !state.enabled || state.brightness != GRID_DRAFT_SMOKE_BRIGHTNESS
				|| (s_GridDraftVariantPlaytest && (state.local_actor != s_GridDraftInitialActor || state.next_sequence != 3))) {
			bootGridDraftFail("grid_draft_guest_value");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator revision=%llu ambient=%d",
			(unsigned long long)state.revision, state.brightness);
		if (s_GridDraftRoomExtension && !s_GridDraftLayoutDecoration) {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW;
			s_GridDraftStepTick = SDL_GetTicks();
			return;
		}
		if (s_GridDraftLiveReconnect) {
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_LIVE_DISCONNECT;
			s_GridDraftStepTick = SDL_GetTicks();
			return;
		}
		if (s_GridDraftVisibility) {
			if (!bootGridVisibilityReadCursorSpawn()) {
				bootGridDraftFail("grid_view_creator_spawn");
				return;
			}
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_PLAYTEST_VISIBILITY;
			return;
		}
		if (s_GridDraftDecoration || s_GridDraftLayoutDecoration) {
			if (netGridEditDecorationCount() != 1 || !netGridEditGetDecorationPose(0, &s_GridDecorationOriginal)) {
				bootGridDraftFail("grid_decoration_creator_baseline");
				return;
			}
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_DECORATION_START;
			s_GridDraftStepTick = SDL_GetTicks();
			return;
		}
		if (s_GridDraftResume) {
			netGridEditClose();
			if (netGridEditGetState(&state) || g_NetLocalClient->room_id != s_GridDraftRoom
					|| g_NetLocalClient->state != CLSTATE_LOBBY) {
				bootGridDraftFail("grid_draft_creator_leave");
				return;
			}
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator voluntarily_left room=%u revision=2 lobby=1",
				(unsigned)s_GridDraftRoom);
			s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_RESUME_INVITE;
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
	} else if (s_GridDraftRoomExtension && s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_EXTENSION_REVIEW) {
		bootGridRoomExtensionReviewCreator(&state, has_draft);
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_PLAYTEST_VISIBILITY) {
		if (!has_draft) {
			bootGridDraftFail("grid_view_creator_authority_lost");
			return;
		}
		bootGridVisibilityCreatorTick(&state);
	} else if (s_GridDraftStep >= BOOT_GRID_DRAFT_WAIT_DECORATION_START
			&& s_GridDraftStep <= BOOT_GRID_DRAFT_WAIT_DECORATION_MOVE) {
		if (!has_draft) {
			bootGridDraftFail("grid_decoration_creator_authority_lost");
			return;
		}
		bootGridDecorationCreatorTick(&state);
	} else if (s_GridDraftVisibility && s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_CLOSE && has_draft) {
		grid_playtest_view_t view;
		if (netGridEditGetPlaytestView(&view)) bootGridDraftFail("grid_view_after_revoke");
	} else if (s_GridDraftLayoutReconnect && (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_INVITE
			|| (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_GRANT && (!has_draft || !state.ready)))
			&& !bootGridLayoutProjectionHidden()) {
		bootGridDraftFail("grid_layout_reconnect_premature_projection");
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_INVITE && invited) {
		if (has_draft || invite.identity.generation <= s_GridDraftInitialInvite.identity.generation
				|| !bootGridDraftSameSource(&invite, g_NetLocalClient) || !netGridEditReply(1)) {
			bootGridDraftFail("grid_draft_creator_resume_accept");
			return;
		}
		s_GridDraftResumeGeneration = invite.identity.generation;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator accepted_resume generation=%llu source_sha256=%s",
			(unsigned long long)s_GridDraftResumeGeneration, invite.arena_sha256);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_RESUME_GRANT;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_GRANT && has_draft && state.ready) {
		if (state.is_host || !state.can_edit || state.pending_resume || state.revision != 3 || !state.enabled
				|| state.brightness != GRID_DRAFT_SMOKE_RESUME_TAIL_BRIGHTNESS || state.local_actor <= s_GridDraftInitialActor
				|| state.next_sequence != 1 || state.invite.identity.generation != s_GridDraftResumeGeneration
				|| !bootGridDraftSameSource(&state.invite, g_NetLocalClient)) {
			bootGridDraftFail("grid_draft_creator_resume_grant");
			return;
		}
		if (s_GridDraftLayoutReconnect && !bootGridLayoutReconnectObserve(&state, 1)) return;
		s_GridDraftResumeActor = state.local_actor;
		sysLogPrintf(LOG_NOTE,
			"SMOKE.GRID.DRAFT: role=creator resumed revision=%llu ambient=%d generation=%llu actor=%llu source_sha256=%s",
			(unsigned long long)state.revision, state.brightness, (unsigned long long)s_GridDraftResumeGeneration,
			(unsigned long long)s_GridDraftResumeActor, state.invite.arena_sha256);
		if (!netGridEditSetAmbient(1, GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS)) {
			bootGridDraftFail("grid_draft_creator_post_resume_command");
			return;
		}
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_RESUME_COMMIT && has_draft && state.revision == 4) {
		if (!state.ready || state.pending_command || !state.enabled
				|| state.brightness != GRID_DRAFT_SMOKE_RESUME_BRIGHTNESS || state.next_sequence != 2
				|| state.local_actor != s_GridDraftResumeActor || state.invite.identity.generation != s_GridDraftResumeGeneration) {
			bootGridDraftFail("grid_draft_creator_resumed_value");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator revision=%llu ambient=%d actor=%llu",
			(unsigned long long)state.revision, state.brightness, (unsigned long long)s_GridDraftResumeActor);
		s_GridDraftStep = BOOT_GRID_DRAFT_WAIT_CLOSE;
	} else if (s_GridDraftStep == BOOT_GRID_DRAFT_WAIT_CLOSE && !has_draft) {
		if (g_NetLocalClient->room_id != s_GridDraftRoom
				|| strcmp(netGridEditError(), "The host ended Shared Grid.") != 0) {
			bootGridDraftFail("grid_draft_guest_close");
			return;
		}
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.DRAFT: role=creator closed room=%u lobby=1",
			(unsigned)s_GridDraftRoom);
		s_GridDraftStep = BOOT_GRID_DRAFT_DONE;
		smokeHarnessExit(0, "grid_draft_complete");
	}
}

enum boot_grid_proposal_role {
	BOOT_GRID_PROPOSAL_NONE,
	BOOT_GRID_PROPOSAL_HOST,
	BOOT_GRID_PROPOSAL_DONOR,
	BOOT_GRID_PROPOSAL_ORDINARY,
};

enum boot_grid_proposal_step {
	BOOT_GRID_PROPOSAL_WAIT_ROOM,
	BOOT_GRID_PROPOSAL_WAIT_DECLINE,
	BOOT_GRID_PROPOSAL_WAIT_REOFFER,
	BOOT_GRID_PROPOSAL_WAIT_APPROVAL,
	BOOT_GRID_PROPOSAL_WAIT_ACTIVE,
	BOOT_GRID_PROPOSAL_WAIT_RESULT,
	BOOT_GRID_PROPOSAL_WAIT_RETURN,
	BOOT_GRID_PROPOSAL_WAIT_RETIRE,
	BOOT_GRID_PROPOSAL_DONE,
};

static const char kBootGridProposalWeapon[] = "mod:weapon_grid_triangle";
static const char kBootGridProposalMod[] = "mod.weapon_grid_triangle";
static const char kBootGridProposalArena[] = "mod:arena_grid_proposal";
static enum boot_grid_proposal_role s_BootGridProposalRole;
static enum boot_grid_proposal_step s_BootGridProposalStep;
static u32 s_BootGridProposalTick;
static u8 s_BootGridProposalRoom = 0xff;
static u64 s_BootGridProposalFirstSerial;
static s32 s_BootGridProposalRound = 1;
static s32 s_BootGridProposalConsent;
static u32 s_BootGridProposalEpoch;
static grid_match_runtime_identity_t s_BootGridProposalWorld;
static u8 s_BootGridProposalWeaponSha[SHA256_DIGEST_SIZE];
static u8 s_BootGridProposalArenaSha[SHA256_DIGEST_SIZE];
static char s_BootGridProposalScratch[FS_MAXPATH + 1];

static void bootGridProposalInit(void)
{
	if (!smokeHarnessIsActive()) return;
	const char *role = sysArgGetString("--debug-grid-mod-proposal");
	if (!role) return;
	if (strcmp(role, "host") == 0) s_BootGridProposalRole = BOOT_GRID_PROPOSAL_HOST;
	else if (strcmp(role, "donor") == 0) s_BootGridProposalRole = BOOT_GRID_PROPOSAL_DONOR;
	else if (strcmp(role, "ordinary") == 0) s_BootGridProposalRole = BOOT_GRID_PROPOSAL_ORDINARY;
	else smokeHarnessExit(1, "grid_proposal_invalid_role");
}

static void bootGridProposalStep(enum boot_grid_proposal_step step)
{
	s_BootGridProposalStep = step;
	s_BootGridProposalTick = SDL_GetTicks();
}

static void bootGridProposalFail(const char *reason)
{
	const char *notice = netmsgGridModProposalNotice();
	sysLogPrintf(LOG_ERROR, "SMOKE.GRID.PROPOSAL: failed phase=%s room=%u round=%d detail=%s",
		reason, (unsigned)s_BootGridProposalRoom, s_BootGridProposalRound, notice ? notice : "");
	bootGridProposalStep(BOOT_GRID_PROPOSAL_DONE);
	smokeHarnessExit(1, reason);
}

static s32 bootGridProposalWeaponSource(s32 temporary, u8 sha[SHA256_DIGEST_SIZE])
{
	const asset_entry_t *entry = assetCatalogResolve(kBootGridProposalWeapon);
	char descriptor[FS_MAXPATH + 1], locator[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
	if (!entry || entry->type != ASSET_WEAPON || entry->temporary != temporary
			|| strcmp(entry->id, kBootGridProposalWeapon) != 0
			|| !gridMatchCatalogArchiveDescriptor(entry, descriptor, sizeof(descriptor))
			|| !gridMatchPackageLocation(descriptor, locator, sizeof(locator), chain, sizeof(chain), NULL) || chain[0]) return 0;
	u32 size = 0;
	void *bytes = gridMatchPackageLoad(locator, &size);
	if (!bytes || !size) { free(bytes); return 0; }
	sha256Hash(bytes, size, sha);
	free(bytes);
	if (temporary) {
		char folder[FS_MAXPATH + 1];
		return !modmgrFindMod(kBootGridProposalMod)
			&& gridMatchPackagePrivatePath(sha, ".pdweapon", folder, s_BootGridProposalScratch)
			&& strcmp(locator, s_BootGridProposalScratch) == 0;
	}
	return modmgrFindMod(kBootGridProposalMod) != NULL;
}

static s32 bootGridProposalPeersReturned(void)
{
	for (u8 id = 1; id <= 2; id++) {
		const struct netclient *peer = &g_NetClients[id];
		if (!peer->peer || peer->room_id != s_BootGridProposalRoom || peer->state != CLSTATE_LOBBY
				|| peer->reviewing_results || (peer->flags & (CLFLAG_SPECTATOR | CLFLAG_ABSENT))) return 0;
	}
	return 1;
}

static s32 bootGridProposalStart(void)
{
	extern s32 netLobbyRequestStartWithSims(u8 gamemode, const char *stage_id, u8 difficulty,
		u8 antiClientId, u8 numSims, u8 simType, u8 timelimit, u32 options, u8 scenario,
		u8 scorelimit, u16 teamscorelimit, u8 weaponSetIndex);
	/* The normal Specific selection carries only a typed ID until Start Match resolves it. */
	const char *weapon = s_BootGridProposalRound == 1 ? "base:falcon2" : kBootGridProposalWeapon;
	snprintf(g_MatchConfig.spawn_weapon_id, sizeof(g_MatchConfig.spawn_weapon_id), "%s", weapon);
	g_MatchConfig.spawnWeaponMode = SPAWNWEAPON_MODE_SPECIFIC;
	matchConfigSetUserOption(MPOPTION_SPAWNWITHWEAPON, 1);
	return netLobbyRequestStartWithSims(NETGAMEMODE_MP, kBootGridProposalArena, 0, NET_NULL_CLIENT,
		0, 0, g_MatchConfig.timelimit, matchConfigGetUserOptions(), g_MatchConfig.scenario, g_MatchConfig.scorelimit,
		g_MatchConfig.teamscorelimit, g_MatchConfig.weaponSetIndex >= 0 ? (u8)g_MatchConfig.weaponSetIndex : 0xff) == 0;
}

static s32 bootGridProposalHostReview(s32 accept, const grid_mod_proposal_control_t *offer)
{
	if (offer->offer.room_id != s_BootGridProposalRoom || offer->packages.count != 1) return 0;
	const grid_match_source_package_t *source = &offer->sources.packages[offer->packages.indexes[0]];
	if (source->owner.type != ASSET_WEAPON || strcmp(source->owner.id, kBootGridProposalWeapon) != 0
			|| assetCatalogResolveAny(kBootGridProposalWeapon) || modmgrFindMod(kBootGridProposalMod)) return 0;
	if (accept) {
		return offer->proposal_serial != s_BootGridProposalFirstSerial
			&& memcmp(source->digest, s_BootGridProposalWeaponSha, sizeof(s_BootGridProposalWeaponSha)) == 0
			&& netmsgGridResolveModProposal(1);
	}
	char folder[FS_MAXPATH + 1];
	if (!gridMatchPackagePrivatePath(source->digest, source->suffix, folder, s_BootGridProposalScratch)) return 0;
	s_BootGridProposalFirstSerial = offer->proposal_serial;
	memcpy(s_BootGridProposalWeaponSha, source->digest, sizeof(s_BootGridProposalWeaponSha));
	if (!netmsgGridResolveModProposal(0) || fsFileSize(s_BootGridProposalScratch) >= 0
			|| netmsgGridModIsProposalOnly(kBootGridProposalWeapon)) return 0;
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: host declined room=%u upload=0 scratch=0",
		(unsigned)s_BootGridProposalRoom);
	return 1;
}

static s32 bootGridProposalCheckActive(const grid_match_runtime_identity_t *world, const char *role)
{
	const s32 used = s_BootGridProposalRound == 2;
	const char *weapon = used ? kBootGridProposalWeapon : "base:falcon2";
	if (world->offer.room_id != s_BootGridProposalRoom || strcmp(world->arena_id, kBootGridProposalArena) != 0
			|| (used && (g_NetStageEpoch <= s_BootGridProposalEpoch
				|| world->offer.generation <= s_BootGridProposalWorld.offer.generation
				|| memcmp(world->arena_sha256, s_BootGridProposalArenaSha, sizeof(s_BootGridProposalArenaSha)) != 0))
			|| g_MatchConfig.spawnWeaponMode != SPAWNWEAPON_MODE_SPECIFIC
			|| strcmp(g_MatchConfig.spawn_weapon_id, weapon) != 0
			|| !gridMatchRuntimeValidateActiveSpawnWeapon(weapon, g_MatchConfig.spawnWeaponNum, NULL, 0)) return 0;
	grid_match_sources_t sources = {0};
	if (!gridMatchRuntimeCopyActiveSources(&sources, NULL, 0)) return 0;
	s32 count = 0;
	s32 valid = 1;
	char weapon_sha[SHA256_HEX_SIZE] = "-";
	for (size_t i = 0; i < sources.package_count; i++) {
		const grid_match_source_package_t *package = &sources.packages[i];
		if (strcmp(package->owner.id, kBootGridProposalWeapon) != 0) continue;
		count++;
		u8 digest[SHA256_DIGEST_SIZE];
		valid = package->owner.type == ASSET_WEAPON
			&& bootGridProposalWeaponSource(s_BootGridProposalRole != BOOT_GRID_PROPOSAL_DONOR, digest)
			&& memcmp(package->digest, digest, sizeof(digest)) == 0;
		if (valid && s_BootGridProposalRole != BOOT_GRID_PROPOSAL_ORDINARY)
			valid = memcmp(package->digest, s_BootGridProposalWeaponSha, sizeof(s_BootGridProposalWeaponSha)) == 0;
		sha256ToHex(package->digest, weapon_sha);
	}
	gridMatchSourcesFree(&sources);
	if (!valid || count != used || (s_BootGridProposalRole == BOOT_GRID_PROPOSAL_ORDINARY
			&& (s_BootGridProposalConsent != used || (!used && assetCatalogResolveAny(kBootGridProposalWeapon))))) return 0;
	s_BootGridProposalWorld = *world;
	s_BootGridProposalEpoch = g_NetStageEpoch;
	memcpy(s_BootGridProposalArenaSha, world->arena_sha256, sizeof(s_BootGridProposalArenaSha));
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: active role=%s round=%d room=%u epoch=%u included=%d weapon=%s sha256=%s",
		role, s_BootGridProposalRound, (unsigned)s_BootGridProposalRoom, s_BootGridProposalEpoch, count, weapon, weapon_sha);
	return 1;
}

static void bootGridProposalPlayTick(s32 host, const char *role)
{
	struct netclient *cl = g_NetLocalClient;
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_ACTIVE) {
		if (!host && netDistribClientConsentPending()) {
			const char *id = netDistribClientGridConsentId(0);
			if (!id || strcmp(id, kBootGridProposalWeapon) != 0 || netDistribClientGridConsentId(1)
					|| s_BootGridProposalRound != 2 || s_BootGridProposalRole != BOOT_GRID_PROPOSAL_ORDINARY
					|| cl->state != CLSTATE_PREPARING) {
				bootGridProposalFail("grid_proposal_unexpected_consent");
				return;
			}
			if (!s_BootGridProposalConsent) {
				s_BootGridProposalConsent = 1;
				sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: ordinary consent prompt room=%u package=%s count=1",
					(unsigned)s_BootGridProposalRoom, id);
			}
		}
		const grid_match_runtime_identity_t *world = gridMatchRuntimeActive();
		if (!world || !g_NetStageEpoch || cl->state != CLSTATE_GAME || !cl->stage_ready) return;
		if (s_BootGridProposalRound == 2 && (!g_Vars.players[0] || g_Vars.currentplayer != g_Vars.players[0]
				|| !invHasSingleWeaponExcAllGuns(g_MatchConfig.spawnWeaponNum))) return;
		if (!bootGridProposalCheckActive(world, role)) bootGridProposalFail("grid_proposal_active_sources");
		else bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_RESULT);
		return;
	}
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_RESULT) {
		if (g_NetStageEpoch || !g_MainIsEndscreen || cl->state != CLSTATE_LOBBY) return;
		const grid_match_result_t *result = netGridLastMatchResult();
		if (!gridMatchResultMatchesStage(result, &s_BootGridProposalWorld.offer, s_BootGridProposalEpoch,
				s_BootGridProposalWorld.arena_id, s_BootGridProposalArenaSha)) {
			bootGridProposalFail("grid_proposal_result_identity");
			return;
		}
		if (s_BootGridProposalRound == 1 || s_BootGridProposalRole != BOOT_GRID_PROPOSAL_ORDINARY) {
			extern void pdguiEndscreenExitToRoom(void);
			extern void pdguiSetInRoom(s32 inRoom);
			pdguiEndscreenExitToRoom();
			pdguiSetInRoom(1);
			netSendLobbyResync();
		}
		bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_RETURN);
		return;
	}
	if (mainGridRoomReturnPending() || !mainGridRoomBackdropActive() || !gridMatchRuntimeIsIdle()
			|| cl->reviewing_results || (host && !bootGridProposalPeersReturned())) return;
	if ((host && (!netmsgGridModIsProposalOnly(kBootGridProposalWeapon) || fsFileSize(s_BootGridProposalScratch) <= 0))
			|| (s_BootGridProposalRole == BOOT_GRID_PROPOSAL_ORDINARY && assetCatalogResolveAny(kBootGridProposalWeapon))) {
		bootGridProposalFail("grid_proposal_room_lease_lifetime");
		return;
	}
	sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: returned role=%s round=%d room=%u epoch=%u",
		role, s_BootGridProposalRound, (unsigned)s_BootGridProposalRoom, s_BootGridProposalEpoch);
	if (s_BootGridProposalRound == 1) {
		s_BootGridProposalRound = 2;
		if (host && !bootGridProposalStart()) bootGridProposalFail("grid_proposal_start_specific");
		else bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_ACTIVE);
	} else {
		if (host) netListenHostRoomLeave();
		bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_RETIRE);
	}
}

static void bootGridProposalTick(void)
{
	static const char *const roles[] = {"", "host", "donor", "ordinary"};
	const char *role = roles[s_BootGridProposalRole];
	const s32 host = s_BootGridProposalRole == BOOT_GRID_PROPOSAL_HOST;
	const s32 donor = s_BootGridProposalRole == BOOT_GRID_PROPOSAL_DONOR;
	const char *notice = netmsgGridModProposalNotice();
	struct netclient *cl = g_NetLocalClient;
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_DONE) return;
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_ROOM) {
		if (!cl || cl->state != CLSTATE_LOBBY || cl->room_id == 0xff || !cl->room_id
				|| (host ? g_NetMode != NETMODE_SERVER : g_NetMode != NETMODE_CLIENT)) return;
		s_BootGridProposalRoom = cl->room_id;
		if (host && !bootGridProposalPeersReturned()) return;
		if (!host && !netmsgGridCanProposeMod()) return;
		if (donor && (!bootGridProposalWeaponSource(0, s_BootGridProposalWeaponSha)
				|| !netmsgGridProposeMod(kBootGridProposalWeapon))) {
			bootGridProposalFail("grid_proposal_donor_send");
			return;
		}
		bootGridProposalStep(host || donor ? BOOT_GRID_PROPOSAL_WAIT_DECLINE : BOOT_GRID_PROPOSAL_WAIT_ACTIVE);
		return;
	}
	if (SDL_GetTicks() - s_BootGridProposalTick >= 90000u) {
		bootGridProposalFail("grid_proposal_timeout");
		return;
	}
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_RETIRE) {
		if (host) {
			if (assetCatalogResolveAny(kBootGridProposalWeapon) || netmsgGridModIsProposalOnly(kBootGridProposalWeapon)
					|| fsFileSize(s_BootGridProposalScratch) >= 0) return;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: host retired room=%u catalog=0 scratch=0 mods=%d",
				(unsigned)s_BootGridProposalRoom, modmgrFindMod(kBootGridProposalMod) != NULL);
			if (modmgrFindMod(kBootGridProposalMod)) { bootGridProposalFail("grid_proposal_persistent_mod"); return; }
		} else if (g_NetMode != NETMODE_NONE) return;
		bootGridProposalStep(BOOT_GRID_PROPOSAL_DONE);
		smokeHarnessExit(0, "grid_mod_proposal_complete");
		return;
	}
	if (!cl || !g_NetMode || cl->room_id != s_BootGridProposalRoom) {
		bootGridProposalFail("grid_proposal_room_changed");
		return;
	}
	if (s_BootGridProposalStep >= BOOT_GRID_PROPOSAL_WAIT_ACTIVE) {
		bootGridProposalPlayTick(host, role);
		return;
	}
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_DECLINE) {
		if (host) {
			const grid_mod_proposal_control_t *offer = netmsgGridReviewModProposal();
			if (!offer) return;
			if (!bootGridProposalHostReview(0, offer)) { bootGridProposalFail("grid_proposal_host_decline"); return; }
		} else {
			if (notice && strcmp(notice, "Sending the approved mod packages to the host.") == 0) {
				bootGridProposalFail("grid_proposal_upload_before_approval");
				return;
			}
			if (!notice || strcmp(notice, "The host did not add the proposed mod.") != 0) return;
			sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: donor observed decline room=%u upload=0", (unsigned)s_BootGridProposalRoom);
			if (!netmsgGridProposeMod(kBootGridProposalWeapon)) { bootGridProposalFail("grid_proposal_donor_reoffer"); return; }
		}
		bootGridProposalStep(host ? BOOT_GRID_PROPOSAL_WAIT_REOFFER : BOOT_GRID_PROPOSAL_WAIT_APPROVAL);
		return;
	}
	if (s_BootGridProposalStep == BOOT_GRID_PROPOSAL_WAIT_REOFFER) {
		const grid_mod_proposal_control_t *offer = netmsgGridReviewModProposal();
		if (!offer) return;
		if (!bootGridProposalHostReview(1, offer)) { bootGridProposalFail("grid_proposal_host_approve"); return; }
		bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_APPROVAL);
		return;
	}
	if (host) {
		if (!netmsgGridModIsProposalOnly(kBootGridProposalWeapon)) return;
		u8 digest[SHA256_DIGEST_SIZE];
		if (!bootGridProposalWeaponSource(1, digest) || memcmp(digest, s_BootGridProposalWeaponSha, sizeof(digest)) != 0
				|| !bootGridProposalPeersReturned()) { bootGridProposalFail("grid_proposal_admission_identity"); return; }
		char hex[SHA256_HEX_SIZE];
		sha256ToHex(digest, hex);
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: host admitted room=%u id=%s sha256=%s temporary=1",
			(unsigned)s_BootGridProposalRoom, kBootGridProposalWeapon, hex);
		if (!bootGridProposalStart()) { bootGridProposalFail("grid_proposal_start_unused"); return; }
	} else {
		if (!notice || strcmp(notice, "The host can use your mod in this Room.") != 0) return;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.PROPOSAL: donor observed approved upload room=%u", (unsigned)s_BootGridProposalRoom);
	}
	bootGridProposalStep(BOOT_GRID_PROPOSAL_WAIT_ACTIVE);
}

static void bootGridSongWitnessTick(void)
{
	static s32 observed_track = -1;
	static s32 done;
	static u8 observed_room;
	static char scratch[FS_MAXPATH + 1];
	if (done || !smokeHarnessIsActive() || !g_NetLocalClient) return;
	const char *id = sysArgGetString("--debug-grid-song-witness");
	if (!id || (g_NetMode != NETMODE_SERVER && g_NetMode != NETMODE_CLIENT)) return;

	const s32 guest = g_NetMode == NETMODE_CLIENT;
	if (observed_track >= 0) {
		if (g_NetStageEpoch || g_MainIsEndscreen || g_NetLocalClient->state != CLSTATE_LOBBY
				|| g_NetLocalClient->room_id != observed_room || !gridMatchRuntimeIsIdle()) return;
		if (modSequenceVirtualTrackId(observed_track) || assetCatalogResolve(id) || fsFileSize(scratch) >= 0) return;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.SONG: retired role=guest room=%u id=%s track=%d slot_released=1 catalog=0 scratch=0",
			(unsigned)observed_room, id, observed_track);
		done = 1;
		return;
	}

	const grid_match_runtime_identity_t *world = gridMatchRuntimeActive();
	if (g_SndDisabled || !world || world->stage != g_StageNum || !g_NetStageEpoch
			|| g_NetLocalClient->state != CLSTATE_GAME || !g_NetLocalClient->stage_ready
			|| g_NetLocalClient->room_id != world->offer.room_id) return;
	const s32 track = scenarioSourceStagePrimaryMusic(world->stage);
	const char *track_id = modSequenceVirtualTrackId(track);
	if (!track_id || strcmp(track_id, id) != 0) return;

	for (s32 i = 0; i < ARRAYCOUNT(g_SeqInstances); i++) {
		const struct seqinstance *seq = &g_SeqInstances[i];
		if (!seq->seqp || seq->tracknum != track || !seq->volume || !g_SeqChannels[i].inuse
				|| g_SeqChannels[i].tracktype != TRACKTYPE_PRIMARY || n_alCSPGetState(seq->seqp) != AL_PLAYING) continue;
		const asset_entry_t *entry = assetCatalogResolve(id);
		if (!entry || entry->type != ASSET_AUDIO || entry->ext.audio.category != AUDIO_CAT_MUSIC
				|| !!entry->temporary != guest) return;
		if (guest) {
			char descriptor[FS_MAXPATH + 1], chain[FS_MAXPATH + 1];
			if (!gridMatchCatalogArchiveDescriptor(entry, descriptor, sizeof(descriptor))
					|| !gridMatchPackageLocation(descriptor, scratch, sizeof(scratch), chain, sizeof(chain), NULL)
					|| chain[0] || fsFileSize(scratch) <= 0) return;
		}
		observed_track = track;
		observed_room = world->offer.room_id;
		sysLogPrintf(LOG_NOTE, "SMOKE.GRID.SONG: playing role=%s room=%u epoch=%u id=%s track=%d player=%d volume=%u temporary=%d",
			guest ? "guest" : "host", (unsigned)observed_room, g_NetStageEpoch, id, track, i,
			(unsigned)seq->volume, guest);
		done = !guest;
		return;
	}
}

s32 bootHostAutostartTick(void)
{
	bootGridSongWitnessTick();
	if (s_BootGridProposalRole && (s_BootGridProposalRole != BOOT_GRID_PROPOSAL_HOST || g_BootHostAutostartFired)) {
		bootGridProposalTick();
		return 0;
	}
	if (s_GridDraftRole == BOOT_GRID_DRAFT_CREATOR || s_GridDraftRole == BOOT_GRID_DRAFT_ORDINARY) {
		bootGridDraftClientTick();
		return 0;
	}
	if (s_GridDraftRole == BOOT_GRID_DRAFT_HOST && g_BootHostAutostartFired) {
		bootGridDraftHostTick();
		return 0;
	}
	if (g_BootHostAutostartArmed && g_BootHostAutostartFired && g_BootHostAutostartMatches > 1
			&& g_BootHostAutostartStarted < g_BootHostAutostartMatches) {
		bootHostAutostartRearmTick();
	}

	if (!g_BootHostAutostartArmed || g_BootHostAutostartFired) {
		return 0;
	}

	/* net.h (included above) provides g_NetMode, g_NetDedicated,
	 * g_NetLocalClient, g_NetClients[], NET_MAX_CLIENTS, NETMODE_SERVER,
	 * CLSTATE_LOBBY. g_NetInit is not in a header (externed locally like the
	 * other boot ticks). netLobbyRequestStartWithSims lives in
	 * pdgui_bridge.c with C linkage and has no public prototype. */
	extern s32 g_NetInit;
	extern s32 netLobbyRequestStartWithSims(u8 gamemode, const char *stage_id,
		u8 difficulty, u8 antiClientId, u8 numSims, u8 simType, u8 timelimit,
		u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit,
		u8 weaponSetIndex);
	/* c3845: listen-host room create + membership (pdgui_bridge.c, C linkage). */
	extern u8  netLobbyRequestCreateRoom(void);
	extern s32 netLobbyHostAddRemoteClientsToRoom(u8 room_id);

	if (!g_NetInit || g_NetMode != NETMODE_SERVER || g_NetDedicated) {
		bootHostAutostartLogWaiting("not-a-listen-host-yet", -1);
		return 0;
	}
	if (!g_NetLocalClient || g_NetLocalClient->state < CLSTATE_LOBBY) {
		bootHostAutostartLogWaiting("host-client-not-in-lobby", -1);
		return 0;
	}

	/* Look for a remote client that has reached the lobby. The host's own
	 * local client is g_NetLocalClient; skip it so we only fire once a real
	 * peer has joined (slot 1 on a listen-host). */
	s32 remoteInLobby = 0;
	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *cl = &g_NetClients[ci];
		if (cl == g_NetLocalClient) {
			continue;
		}
		if (cl->state >= CLSTATE_LOBBY) {
			remoteInLobby++;
		}
	}
	if (remoteInLobby < g_BootHostAutostartPeers) {
		g_BootHostAutostartSettleFrames = 0; /* reset settle if peer drops pre-fire */
		bootHostAutostartLogWaiting("waiting-for-remote-in-lobby", remoteInLobby);
		return 0;
	}

	/* Peer is in lobby; hold a short settle window so the manifest/catalog
	 * exchange that follows CLC_AUTH has time to quiesce before we fire. */
	if (g_BootHostAutostartSettleFrames < 120) {
		g_BootHostAutostartSettleFrames++;
		bootHostAutostartLogWaiting("settling-after-join", remoteInLobby);
		return 0;
	}

	/* c3845 PHASE 1: settle complete — create the host's room and pull every
	 * connected remote client into it BEFORE starting the match.
	 *
	 * Why this is needed: the listen-host boots into the global lounge
	 * (room_id == 0xFF). CLC_LOBBY_START's leader check (netmsg.c:5268-5301)
	 * accepts only the creator of the sender's room or the global lobby leader;
	 * an un-roomed host satisfied neither, so the prior run logged
	 * "CLC_LOBBY_START rejected ... not room creator". Creating a room makes the
	 * host the creator (branch a). The match dispatch is then room-scoped
	 * (netServerStageStart -> netSendToRoom, net.c:1008/2345; ready gate
	 * netmsg.c:5766; CLSTATE_GAME transition net.c:971), so the joined client
	 * must also be a room member or it never receives SVC_STAGE_START and
	 * "MATCH: player spawned slot=1" never fires.
	 *
	 * Rate limit: CLC_ROOM_CREATE is 1/sec/client (netmsg.c:7672); the bridge
	 * resets the host's bucket before replaying, and CLC_LOBBY_START is NOT a
	 * room-mutation op (its handler never calls netmsgRoomRateAllow), so the
	 * create + later start do not collide. The remote-client adds are pure
	 * server-side (roomJoin + room_id + SVC_ROOM_ASSIGN), bypassing the limiter
	 * entirely. We still space the start a few frames after the join so the
	 * SVC_ROOM_ASSIGN round-trip and room state settle cleanly. */
	if (g_BootHostAutostartPhase == 0) {
		u8 roomId;
		if (s_GridDraftLiveReconnect || s_GridDraftLayoutReconnect) {
			if (netRequestRoomCreate("Grid Creator Reconnect", ROOM_ACCESS_INVITE, "", 4) != 0
					|| g_NetLocalClient->room_id == 0xff) {
				bootGridDraftFail("grid_live_reconnect_create_room");
				return 0;
			}
			roomId = g_NetLocalClient->room_id;
		} else {
			roomId = netLobbyRequestCreateRoom();
		}
		if (roomId == 0xFF) {
			/* Could not create/own a room — without it the start would be
			 * rejected as "not room creator". Surface and bail (one-shot
			 * latch is intentionally NOT set so a later frame can retry once
			 * the host settles, but log so the failure is visible). */
			sysLogPrintf(LOG_WARNING,
				"BOOT: --host-autostart: room create failed — deferring start (host not room owner)");
			return 0;
		}
		g_BootHostAutostartRoomId = roomId;

		s32 addedClients = netLobbyHostAddRemoteClientsToRoom(roomId);
		sysLogPrintf(LOG_NOTE,
			"BOOT: --host-autostart: room %u ready, added %d remote client(s) to it",
			(unsigned)roomId, (int)addedClients);

		g_BootHostAutostartPhase = 1;
		g_BootHostAutostartPostJoinFrames = 0;
		return 0;
	}

	/* c3845 PHASE 1->2 spacing: let the room-assign / membership settle for a
	 * few frames (and respect the 1 s room-mutation window margin) before the
	 * start replay. */
	if (g_BootHostAutostartPhase == 1) {
		if (g_BootHostAutostartPostJoinFrames < 15) {
			g_BootHostAutostartPostJoinFrames++;
			return 0;
		}
		g_BootHostAutostartPhase = 2;
		/* fall through to fire the start this frame */
	}

	g_BootHostAutostartFired = 1;
	if (s_BootGridProposalRole == BOOT_GRID_PROPOSAL_HOST) {
		bootGridProposalTick();
		return 0;
	}
	if (s_GridDraftRole == BOOT_GRID_DRAFT_HOST) {
		s_GridDraftRoom = g_BootHostAutostartRoomId;
		bootGridDraftHostTick();
		return 0;
	}
	/* Every attempt counts, so a start that fails below is not retried
	 * past --host-autostart-matches. */
	g_BootHostAutostartStarted++;

	const u8 mode = g_BootHostAutostartGameMode;
	/* Room creation publishes Combat Simulator defaults. Campaign autostart
	 * keeps its explicitly requested mission instead of consuming that arena. */
	const char *stageId = mode == NETGAMEMODE_MP
		? g_MatchConfig.stage_id : g_BootLaunchMpArena;
	const u8 difficulty = mode == NETGAMEMODE_MP
		? 0 : (u8)bootResolveDifficulty(g_BootLaunchDifficulty);
	u8 antiClientId = NET_NULL_CLIENT;
	s32 remoteParticipants = 0;
	u8 weaponSet = (u8)(g_MatchConfig.weaponSetIndex >= 0
		? g_MatchConfig.weaponSetIndex : 0xFF);

	for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
		struct netclient *client = &g_NetClients[ci];

		if (client == g_NetLocalClient
				|| client->state < CLSTATE_LOBBY
				|| (client->flags & CLFLAG_SPECTATOR)
				|| client->room_id != g_BootHostAutostartRoomId) {
			continue;
		}
		remoteParticipants++;
		antiClientId = client->id;
	}
	if (mode == NETGAMEMODE_ANTI && remoteParticipants != 1) {
		sysLogPrintf(LOG_ERROR,
			"BOOT: --host-autostart Counter-Op requires exactly one remote room participant; found=%d",
			remoteParticipants);
		return 1;
	}
	if (mode != NETGAMEMODE_ANTI) {
		antiClientId = NET_NULL_CLIENT;
	}

	/* Derive the bot payload metadata from the prepared slots, not the CLI
	 * request. matchConfigAddBot may reject excess requests, and zero bots have
	 * no lead difficulty, whose canonical wire sentinel is zero. */
	if (g_MatchConfig.numSlots == 0
			|| g_MatchConfig.numSlots > MATCH_MAX_SLOTS) {
		sysLogPrintf(LOG_ERROR,
			"BOOT: --host-autostart rejected invalid prepared slot count=%u",
			(unsigned)g_MatchConfig.numSlots);
		return 1;
	}
	u8 numSims = 0;
	u8 simType = 0;
	for (s32 si = 0; si < g_MatchConfig.numSlots; si++) {
		const struct matchslot *slot = &g_MatchConfig.slots[si];
		if (slot->type != SLOT_BOT) {
			continue;
		}
		if (numSims == 0) {
			simType = slot->botDifficulty;
		}
		numSims++;
	}
	if (mode != NETGAMEMODE_MP) {
		numSims = 0;
		simType = 0;
		weaponSet = 0xFF;
	} else {
		/* The start writer rejects a mode that disagrees with the scenario
		 * ID, and a team-only mode needs Teams in the sent options. */
		(void)matchConfigApplyScenarioRules("host-autostart");
	}
	sysLogPrintf(LOG_NOTE,
		"BOOT: --host-autostart firing: mode=%u stage='%s' difficulty=%u anti_client=%u scenario=%u timelimit=%u sims=%u",
		(unsigned)mode, stageId, (unsigned)difficulty,
		(unsigned)antiClientId,
		(unsigned)(mode == NETGAMEMODE_MP ? g_MatchConfig.scenario : 0),
		(unsigned)(mode == NETGAMEMODE_MP ? g_MatchConfig.timelimit : 0),
		(unsigned)numSims);

	s32 rc = netLobbyRequestStartWithSims(
		mode,
		stageId,
		difficulty,
		antiClientId,
		numSims,
		simType,
		mode == NETGAMEMODE_MP ? g_MatchConfig.timelimit : 0,
		mode == NETGAMEMODE_MP ? matchConfigGetUserOptions() : 0,
		mode == NETGAMEMODE_MP ? g_MatchConfig.scenario : 0,
		mode == NETGAMEMODE_MP ? g_MatchConfig.scorelimit : 0,
		mode == NETGAMEMODE_MP ? g_MatchConfig.teamscorelimit : 0,
		weaponSet);

	sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_WARNING,
		"BOOT: --host-autostart consumed: result=%s rc=%d",
		rc == 0 ? "OK" : "FAILED", (int)rc);
	if (g_BootHostAutostartMatches > 1) {
		sysLogPrintf(LOG_NOTE, "BOOT: --host-autostart started match %d of %d rc=%d",
			g_BootHostAutostartStarted, g_BootHostAutostartMatches, (int)rc);
	}
	return 1;
}

/* B-1103/SP-67: production-path negative proof. schedStartFrame receives the
 * manifest and sets the ordinary remote client to PREPARING before mainTick
 * reaches this hook. Change one valid profile setting and use the same
 * netClientSettingsChanged -> CLC_SETTINGS path as the lobby UI. The authority
 * must roll back its immutable prepared roster before publishing this update.
 * The hook is unavailable without an active smoke and fires at most once. */
s32 bootReadyGateSettingsChangeTick(void)
{
	u8 prior_handicap;
	u8 candidate_handicap;
	s32 rc;

	if (!g_BootReadyGateSettingsChangeArmed
			|| g_BootReadyGateSettingsChangeFired) {
		return 0;
	}
	if (!smokeHarnessIsActive()) {
		g_BootReadyGateSettingsChangeArmed = 0;
		sysLogPrintf(LOG_WARNING,
			"SMOKE.NET.SETTINGS_CHANGE rejected smoke=0 one_shot=0");
		return 1;
	}
	if (g_NetMode != NETMODE_CLIENT || g_NetLocalClient == NULL
			|| g_NetLocalClient->state != CLSTATE_PREPARING) {
		return 0;
	}

	if (g_BootReadyGateSettingsChangeArmed == 2) {
		/* B-1208: the same ordinary send with nothing changed. The authority
		 * must keep its prepared roster and start the match. */
		g_BootReadyGateSettingsChangeFired = 1;
		g_BootReadyGateSettingsChangeArmed = 0;
		rc = netClientSettingsChanged();
		sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
			"SMOKE.NET.SETTINGS_REPEAT sent state=%u handicap=%u rc=%d ordinary_clc_settings=1 one_shot=1",
			(unsigned)g_NetLocalClient->state,
			(unsigned)g_NetLocalClient->settings.handicap, rc);
		return 1;
	}

	prior_handicap = g_PlayerConfigsArray[0].handicap;
	candidate_handicap = prior_handicap == 0
		? 0x80 : (prior_handicap == 0xff ? 0xfe : prior_handicap + 1);
	g_PlayerConfigsArray[0].handicap = candidate_handicap;
	g_BootReadyGateSettingsChangeFired = 1;
	g_BootReadyGateSettingsChangeArmed = 0;
	rc = netClientSettingsChanged();
	if (rc != 0) {
		g_PlayerConfigsArray[0].handicap = prior_handicap;
		g_NetLocalClient->settings.handicap = prior_handicap;
	}
	sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
		"SMOKE.NET.SETTINGS_CHANGE sent state=%u old_handicap=%u new_handicap=%u rc=%d ordinary_clc_settings=1 one_shot=1",
		(unsigned)g_NetLocalClient->state, (unsigned)prior_handicap,
		(unsigned)candidate_handicap, rc);
	return 1;
}

/* c3845 (2026-06-23): accessor for the lv.c time-limit gate. Returns the
 * forced match time limit in SECONDS when --match-timelimit-sec was passed,
 * or 0 when unset (engine uses the normal minutes-granularity limit). */
s32 bootGetMatchTimeLimitSec(void)
{
	return g_BootMatchTimeLimitSec;
}

/* Track 2c (c3807, 2026-05-16): --dump-swarm-state deferred tick.
 *
 * Called once per frame from pdmain.c's mainTick when the latch is
 * armed. Throttles itself to SWARM_DUMP_INTERVAL_FRAMES so the file
 * grows at ~1 Hz. On fire:
 *   1. Calls swarmGpuReadbackTextureRows(0, 5, ...) to pull the full
 *      READ-side state texture.
 *   2. Appends a 32 B header + 320 KB float payload to the configured
 *      path (binary append; file is created on the first write).
 *   3. Decrements the dumps-left counter; clears the latch once zero.
 *
 * Gates:
 *   - g_BootDumpSwarmArmed must be set
 *   - g_Vars.lvframenum >= 4 (player positioned, stage settled)
 *   - swarmGpuReadbackTextureRows must return a non-zero texel count
 *     (otherwise the GPU swarm isn't armed; skip silently this frame)
 *
 * The throttle counter increments every gated frame (not every armed
 * frame) so the spacing between dumps reflects gameplay frames, not
 * wall-clock. At 60 fps that's 1 second between dumps. Returns 1 on
 * any decision (dump fired OR throttled), 0 if the latch was off. */
s32 bootDumpSwarmStateTick(void)
{
	extern int swarmGpuReadbackTextureRows(int row_start, int row_count,
		float *out_buf, int out_capacity);

	if (!g_BootDumpSwarmArmed) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}

	g_BootDumpSwarmTickCounter++;
	if (g_BootDumpSwarmTickCounter < SWARM_DUMP_INTERVAL_FRAMES) {
		return 1;
	}
	g_BootDumpSwarmTickCounter = 0;

	/* SWARM_GPU_MAX = TESTSCEN_SWARM_MAX_COUNT = 4096. 5 rows * 4096
	 * texels * 4 floats = 81920 floats = 320 KB. Stack would be tight;
	 * use a static so we don't blow the 8 MB main stack. The buffer
	 * is rewritten on every dump call so no aliasing between dumps. */
	static float s_DumpBuf[5 * 4096 * 4];
	const int got = swarmGpuReadbackTextureRows(0, 5,
		s_DumpBuf, (int)(sizeof(s_DumpBuf) / sizeof(float)));
	if (got <= 0) {
		/* GPU swarm not armed this frame (e.g. scenario not launched
		 * yet, or compute unavailable). Silently skip; we'll try again
		 * next interval. */
		return 1;
	}

	FILE *f = fopen(g_BootDumpSwarmPath, "ab");
	if (!f) {
		sysLogPrintf(LOG_WARNING,
			"BOOT: --dump-swarm-state fopen failed for '%s'; disarming",
			g_BootDumpSwarmPath);
		g_BootDumpSwarmArmed = 0;
		return 1;
	}

	/* 32 B header. Magic is 9 ASCII chars + 1 pad + 22 B reserved/data
	 * to land the payload at offset 32 (16-byte-aligned for RGBA32F).
	 * Layout MUST match the docblock above g_BootDumpSwarmPath. */
	unsigned char header[32];
	memset(header, 0, sizeof(header));
	memcpy(header + 0, "PDSWARMv1", 9);
	const u32 frame_idx = (u32)g_Vars.lvframenum;
	const u32 count_w   = 4096;   /* SWARM_GPU_MAX; mirrors texture width */
	const u32 row_count = 5;
	memcpy(header + 16, &frame_idx, sizeof(u32));
	memcpy(header + 20, &count_w,   sizeof(u32));
	memcpy(header + 24, &row_count, sizeof(u32));

	size_t hwrite = fwrite(header, 1, sizeof(header), f);
	size_t pwrite = fwrite(s_DumpBuf, sizeof(float), (size_t)got * 4, f);
	fclose(f);

	const s32 dump_no = SWARM_DUMP_MAX_FRAMES - g_BootDumpSwarmDumpsLeft + 1;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --dump-swarm-state dump %d/%d wrote frame=%u texels=%d "
		"header_bytes=%zu payload_floats=%zu path='%s'",
		(int)dump_no, (int)SWARM_DUMP_MAX_FRAMES,
		(unsigned)frame_idx, got, hwrite, pwrite,
		g_BootDumpSwarmPath);

	g_BootDumpSwarmDumpsLeft--;
	if (g_BootDumpSwarmDumpsLeft <= 0) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --dump-swarm-state consumed: max_dumps reached; disarming");
		g_BootDumpSwarmArmed = 0;
	}
	return 1;
}

/* Reuse is deliberately limited to the smoke adapter. Asset admission below
 * still runs normally: an immutable base needing repair must fail boot. */
static s32 bootReuseAbsolutePath(const char *path)
{
	return path && strlen(path) >= 3 && strlen(path) < FS_MAXPATH
		&& isalpha((unsigned char)path[0])
		&& path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

static s32 bootInitNativeReuse(int argc, const char **argv)
{
	const char *root = sysArgGetString("--reuse-write-root");
	if (!sysArgCheck("--reuse-write-root")) return 1;
	const char *base = sysArgGetString("--basedir");
	const char *outputs[] = { "--savedir", "--debug-home-path", "--smoke-screenshot-dir" };
	if (!bootReuseAbsolutePath(root) || !bootReuseAbsolutePath(base) || !sysArgGetString("--smoke")
			|| !sysArgCheck("--no-net") || !sysArgCheck("--no-update-check")) {
		/* Refusal must stay silent until inherited output handles are checked. */
		return 0;
	}
	for (int i = 1; i < argc; ++i) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--portable") || !strcmp(arg, "--moddir")
				|| !strcmp(arg, "--dedicated")
				|| !strncmp(arg, "--extract-", 10) || !strncmp(arg, "--import-", 9)
				|| !strncmp(arg, "--migrate-", 10) || !strncmp(arg, "--bench-", 8)
				|| !strncmp(arg, "--dump-", 7) || !strncmp(arg, "--listen-", 9)
				|| !strncmp(arg, "--connect-", 10) || !strncmp(arg, "--host-", 7)
				|| (!strncmp(arg, "--debug-", 8) && strcmp(arg, "--debug-home-path"))) {
			return 0;
		}
		/* Avoid first/last-argument disagreements between adapter and client. */
		if (!strcmp(arg, "--reuse-write-root") || !strcmp(arg, "--basedir")
				|| !strcmp(arg, "--smoke") || !strcmp(arg, "--savedir")
				|| !strcmp(arg, "--debug-home-path") || !strcmp(arg, "--smoke-screenshot-dir")) {
			if (i + 1 >= argc || !argv[i + 1][0] || !strncmp(argv[i + 1], "--", 2)) return 0;
			for (int j = i + 2; j < argc; ++j) {
				if (!strcmp(arg, argv[j])) return 0;
			}
		}
	}
	if (!nativeWritePolicyInit(base, root)) return 0;
	for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
		const char *path = sysArgGetString(outputs[i]);
		/* Debug home routes logs into root/logs; it never writes the root
		 * directory itself. The mutation policy still requires descendants. */
		const bool debugRoot = path && !strcmp(outputs[i], "--debug-home-path") && !strcmp(path, root);
		if (!bootReuseAbsolutePath(path) || (!debugRoot && !nativeWritePolicyAllowsPath(path))) {
			fprintf(stderr, "NATIVE.REUSE: profile-scoped %s required\n", outputs[i]);
			return 0;
		}
	}
	return 1;
}

/* A repair child has its own decoder scratch and writes only its staged output.
 * Its parent has already extracted and verified the shared raw input files. */
static s32 bootRunStageRepairWorker(void)
{
	const char *output = sysArgGetString("--stage-repair-output");
	const char *save = sysArgGetString("--savedir");
	const char *rom = sysArgGetString("--rom-file");
	const s32 stage = sysArgGetInt("--stage-repair", -1);
	if (!sysArgCheck("--extract-assets-only") || nativeWritePolicyActive() ||
			!output || !save || strcmp(output, save) || !fsPathIsAbsolute(output) ||
			!rom || !fsPathIsAbsolute(rom) || stage < 0) return 2;
	sysInit();
	fsInit();
	if (fsFileSize(rom) <= 0 || romdataInit() < 0 || romdataReleaseRom() < 0) return 2;
	catalogInit();
	stageTableInit();
	assetCatalogInit();
	assetCatalogRegisterBaseGame();
	assetCatalogRegisterStageSceneFiles();
	return romExtractStandaloneScenarioCandidate(stage, output) == 0 ? 0 : 2;
}

int main(int argc, const char **argv)
{
	/* Pin the decimal point before anything formats or parses a number:
	 * profiles, settings, mod caches and extracted JSON are written with
	 * printf("%g") and read back with strtod, which follow LC_NUMERIC. A
	 * comma-decimal locale would write "0,5" that the next launch cannot
	 * read (audit 2026-10-28 PD-01A, CQ-01). Text display does not use it. */
	setlocale(LC_NUMERIC, "C");
	sysInitArgs(argc, argv);
	if (sysArgCheck("--reuse-write-policy-info")) {
		if (argc != 2) return 2;
		const bool supported = nativeWritePolicyLinkContract();
		printf("{\"schema\":\"pd2.native-write-policy.v1\",\"supported\":%s}\n",
			supported ? "true" : "false");
		return supported ? 0 : 2;
	}
	if (!bootInitNativeReuse(argc, argv)) return 2;
	if (sysArgCheck("--stage-repair")) return bootRunStageRepairWorker();

	/* D13: Apply pending update VERY EARLY — before any subsystem init.
	 * If an update was downloaded previously, this renames the .update file
	 * into place and re-execs. If no pending update, this is a no-op.
	 * NOTE: sysLogPrintf is safe to call before sysInit (uses static buffers).
	 * detectExePath uses only stdlib — no SDL or game init required. */
	if (!nativeWritePolicyActive()) updaterApplyPending();

	if (!sysArgCheck("--no-crash-handler")) {
		crashInit();
	}

	/* FIX-A.1/A.4: Capture stack base for chraTick depth monitoring.
	 * Called from main()'s frame so the base address is near the top
	 * of the 8 MB stack.  Must be before the game loop starts. */
	chrTickStackInit();

	/* Parse --dedicated early so the window title is correct in videoInit */
	if (sysArgCheck("--dedicated")) {
		extern s32 g_NetDedicated;
		g_NetDedicated = 1;
	}

	/* c115 (2026-05-13): smoke-verify CLI fast-paths.  Parse here so
	 * the netInit gate (--no-net) and the post-catalog hooks
	 * (--launch-scenario, --launch-mission, --launch-mp-room) all see
	 * the same captured values.  Each flag is opt-in; absent flags
	 * leave the corresponding global at its zero-init default. */
	g_BootNoNet            = sysArgCheck("--no-net") ? true : false;
	g_BootExtractAssetsOnly = sysArgCheck("--extract-assets-only") ? true : false;
	g_BootMainMenu         = sysArgCheck("--main-menu") ? true : false;
	/* c3844 (2026-06-23): --launch-credits boots straight into the credits
	 * scroll for the transparent-particle + font alpha capture smoke. */
	g_BootLaunchCredits    = sysArgCheck("--launch-credits") ? true : false;
	if (g_BootLaunchCredits) {
		sysLogPrintf(LOG_NOTE, "BOOT: --launch-credits armed");
	}
	g_BootDebugThemeStageTransitionProof =
		sysArgCheck("--debug-theme-stage-transition-proof") ? 1 : 0;
	if (g_BootDebugThemeStageTransitionProof) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-theme-stage-transition-proof armed");
	}
	g_BootLaunchScenario   = sysArgGetString("--launch-scenario");
	g_BootLaunchMission    = sysArgGetString("--launch-mission");
	g_BootLaunchDifficulty = sysArgGetString("--difficulty");
	g_BootLaunchMpArena    = sysArgGetString("--launch-mp-room");
	g_BootMountBike        = sysArgCheck("--debug-mount-bike") ? true : false;
	g_BootDebugForceFirstPerson = sysArgCheck("--debug-force-first-person") ? true : false;
	effectInstanceRuntimeSetAuditEnabled(
		sysArgCheck("--debug-effect-runtime-audit") ? 1 : 0);
	if (effectInstanceRuntimeAuditEnabled()) {
		sysLogPrintf(LOG_NOTE, "BOOT: --debug-effect-runtime-audit armed");
	}
	bootApplyDebugForceFirstPersonLook(sysArgGetString("--debug-force-first-person-look"));
	bootApplyDebugForceFirstPersonCamOffset(sysArgGetString("--debug-force-first-person-cam-offset"));

	/* Mike directive 2026-05-18: end-to-end CS smoke infra. */
	g_BootDebugAutoStartMatch = sysArgCheck("--debug-auto-start-match") ? true : false;
	if (sysArgCheck("--launch-challenge")) {
		const s32 slot = sysArgGetInt("--launch-challenge", -1);

		if (slot >= 0 && slot < 30) {
			g_BootLaunchChallengeSlot = slot;
		} else {
			sysLogPrintf(LOG_WARNING, "BOOT: --launch-challenge expects a slot 0..29; ignored");
		}
	}
	/* Co-op AI buddies (Mike, 2026-11-16): the Room's Partner choice, for a
	 * smoke; the stage start clamps it to the original's limit. */
	if (sysArgCheck("--coop-ai-buddies")) {
		const s32 buddies = sysArgGetInt("--coop-ai-buddies", 0);

		if (buddies >= 1 && buddies <= 4) {
			g_NetCoopAiBuddies = (u8)buddies;
			sysLogPrintf(LOG_NOTE, "BOOT: --coop-ai-buddies armed: buddies=%d", buddies);
		} else {
			sysLogPrintf(LOG_WARNING, "BOOT: --coop-ai-buddies expects 1..4; ignored");
		}
	}
	/* --debug-room-tab <combat|campaign|counterop>: the Room opens on that
	 * tab (with --debug-auto-start-match, its Start Match waits for it). */
	{
		const char *tab = sysArgGetString("--debug-room-tab");

		if (tab && tab[0]) {
			if (strcasecmp(tab, "combat") == 0) {
				g_BootDebugRoomTab = 0;
			} else if (strcasecmp(tab, "campaign") == 0 || strcasecmp(tab, "coop") == 0) {
				g_BootDebugRoomTab = 1;
			} else if (strcasecmp(tab, "counterop") == 0) {
				g_BootDebugRoomTab = 2;
			}
			if (g_BootDebugRoomTab >= 0) {
				sysLogPrintf(LOG_NOTE, "BOOT: --debug-room-tab armed: tab=%d", g_BootDebugRoomTab);
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-room-tab '%s' unknown (combat|campaign|counterop)", tab);
			}
		}
	}
	/* c3845 (2026-06-23): listen-host two-process match smoke infra. A co-op
	 * host may start alone (--host-autostart-peers 0: AI buddies). */
	g_BootHostAutostartArmed = sysArgCheck("--host-autostart") ? true : false;
	g_BootHostAutostartPeers = sysArgGetInt("--host-autostart-peers", 1);
	if (g_BootHostAutostartPeers < 0 || g_BootHostAutostartPeers >= MAX_PLAYERS) {
		sysLogPrintf(LOG_WARNING, "BOOT: invalid --host-autostart-peers; autostart disabled");
		g_BootHostAutostartArmed = false;
	}
	g_BootHostAutostartMatches = sysArgGetInt("--host-autostart-matches", 1);
	if (g_BootHostAutostartMatches < 1 || g_BootHostAutostartMatches > 8) {
		sysLogPrintf(LOG_WARNING, "BOOT: --host-autostart-matches expects 1..8; using 1");
		g_BootHostAutostartMatches = 1;
	} else if (g_BootHostAutostartArmed && g_BootHostAutostartMatches > 1) {
		sysLogPrintf(LOG_NOTE, "BOOT: --host-autostart-matches armed: matches=%d",
			g_BootHostAutostartMatches);
	}
	if (g_BootHostAutostartArmed) {
		const char *mode = sysArgGetString("--host-autostart-mode");
		if (!bootResolveHostAutostartMode(mode,
				&g_BootHostAutostartGameMode)) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --host-autostart-mode '%s' invalid; autostart disabled",
				mode ? mode : "");
			g_BootHostAutostartArmed = false;
		} else if (g_BootHostAutostartPeers == 0
				&& g_BootHostAutostartGameMode != NETGAMEMODE_COOP) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --host-autostart-peers 0 is for co-op only; autostart disabled");
			g_BootHostAutostartArmed = false;
		} else {
			sysLogPrintf(LOG_NOTE,
				"BOOT: --host-autostart armed mode=%u difficulty=%s",
				(unsigned)g_BootHostAutostartGameMode,
				g_BootHostAutostartGameMode == NETGAMEMODE_MP
					? "unused"
					: (g_BootLaunchDifficulty
						? g_BootLaunchDifficulty : "agent"));
		}
	}
	{
		const char *tlsec = sysArgGetString("--match-timelimit-sec");
		if (tlsec && tlsec[0]) {
			long v = strtol(tlsec, NULL, 0);
			if (v > 0 && v <= 3600) {
				g_BootMatchTimeLimitSec = (s32)v;
				sysLogPrintf(LOG_NOTE,
					"BOOT: --match-timelimit-sec armed: seconds=%d",
					g_BootMatchTimeLimitSec);
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --match-timelimit-sec expects 1..3600; got: '%s'",
					tlsec);
			}
		}
	}
	{
		const char *slimit = sysArgGetString("--match-scorelimit");
		const char *tlimit = sysArgGetString("--match-teamscorelimit");
		if (slimit && slimit[0]) {
			long v = strtol(slimit, NULL, 0);
			if (v >= 1 && v <= 100) {
				g_BootMatchScoreLimit = (s32)v;
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --match-scorelimit expects 1..100; got: '%s'", slimit);
			}
		}
		if (tlimit && tlimit[0]) {
			long v = strtol(tlimit, NULL, 0);
			if (v >= 1 && v <= 400) {
				g_BootMatchTeamScoreLimit = (s32)v;
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --match-teamscorelimit expects 1..400; got: '%s'", tlimit);
			}
		}
	}
	g_BootDebugSwarmMap       = sysArgGetString("--debug-swarm-map");
	g_BootDebugInstallReceivedMod =
		sysArgGetString("--debug-install-received-mod");
	g_BootDebugReceivePdcaList =
		sysArgGetString("--debug-receive-pdca-list");
	g_BootDebugRejectCatalogIngressList =
		sysArgGetString("--debug-reject-catalog-ingress-list");
	g_BootDebugSpawnWeapon    = sysArgGetString("--debug-spawn-weapon");
	g_BootDebugBotBody        = sysArgGetString("--debug-bot-body");
	g_BootDebugBotHead        = sysArgGetString("--debug-bot-head");
	{
		/* "type:difficulty,..." with BOTTYPE_* 0..12 and BOTDIFF_* 0..5. A bad
		 * entry rejects the whole flag, so a smoke never runs a different mix. */
		const char *spec = sysArgGetString("--debug-bot-spec");

		if (spec && spec[0]) {
			const char *p = spec;
			s32 count = 0;
			s32 ok = 1;

			while (*p && ok) {
				char *end = NULL;
				const long type = strtol(p, &end, 10);
				long diff;

				if (end == p || *end != ':' || type < BOTTYPE_GENERAL || type > BOTTYPE_VENGE) {
					ok = 0;
					break;
				}
				p = end + 1;
				diff = strtol(p, &end, 10);
				if (end == p || diff < BOTDIFF_MEAT || diff > BOTDIFF_DARK
						|| count >= BOOT_DEBUG_BOT_SPEC_MAX) {
					ok = 0;
					break;
				}
				g_BootDebugBotSpecType[count] = (u8)type;
				g_BootDebugBotSpecDiff[count] = (u8)diff;
				count++;
				p = end;
				if (*p == ',') {
					p++;
				} else if (*p) {
					ok = 0;
				}
			}
			if (ok && count > 0) {
				g_BootDebugBotSpecCount = count;
				sysLogPrintf(LOG_NOTE, "BOOT: --debug-bot-spec armed: %d simulant(s) '%s'", count, spec);
			} else {
				sysLogPrintf(LOG_WARNING, "BOOT: --debug-bot-spec rejected '%s' (expects type:difficulty,...)", spec);
			}
		}
	}
	g_BootDebugPlaceBotNearPlayer = sysArgCheck("--debug-place-bot-near-player") ? true : false;
	if (sysArgCheck("--debug-bots-hold-fire")) {
		g_DebugBotsHoldFire = true;
		sysLogPrintf(LOG_NOTE, "BOOT: --debug-bots-hold-fire armed");
	}
	{
		const char *holdsec = sysArgGetString(
			"--debug-place-bot-near-player-hold-sec");
		if (holdsec && holdsec[0]) {
			char *end = NULL;
			long seconds = strtol(holdsec, &end, 10);
			if (!g_BootDebugPlaceBotNearPlayer) {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-place-bot-near-player-hold-sec requires --debug-place-bot-near-player; ignored");
			} else if (!end || *end != '\0' || seconds < 1 || seconds > 60) {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-place-bot-near-player-hold-sec expects 1..60; got: '%s'",
					holdsec);
			} else {
				g_BootDebugPlaceBotNearPlayerHoldDurationMs =
					(u32)seconds * 1000u;
			}
		}
	}
	if (sysArgCheck("--debug-place-bot-near-player-num")) {
		const s32 num = sysArgGetInt("--debug-place-bot-near-player-num", -1);

		if (!g_BootDebugPlaceBotNearPlayer) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-place-bot-near-player-num requires --debug-place-bot-near-player; ignored");
		} else if (num < 0 || num >= MAX_PLAYERS) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-place-bot-near-player-num expects 0..%d; ignored", MAX_PLAYERS - 1);
		} else {
			g_BootDebugPlaceBotNearPlayerNum = num;
			sysLogPrintf(LOG_NOTE, "BOOT: --debug-place-bot-near-player-num armed: player=%d", num);
		}
	}
	if (sysArgCheck("--debug-place-bot-near-player-visible")) {
		if (!g_BootDebugPlaceBotNearPlayer) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --debug-place-bot-near-player-visible requires --debug-place-bot-near-player; ignored");
		} else {
			g_BootDebugPlaceBotNearPlayerVisible = 1;
			sysLogPrintf(LOG_NOTE, "BOOT: --debug-place-bot-near-player-visible armed");
		}
	}
	{
		const char *endmission = sysArgGetString("--debug-end-mission");
		if (endmission && endmission[0]) {
			s32 seconds = -1;
			char result[16] = "";
			if (sscanf(endmission, "%d:%15s", &seconds, result) == 2 && seconds >= 0
					&& (strcmp(result, "complete") == 0 || strcmp(result, "fail") == 0)) {
				g_BootDebugEndMissionSec = seconds;
				g_BootDebugEndMissionComplete = strcmp(result, "complete") == 0;
				sysLogPrintf(LOG_NOTE, "BOOT: --debug-end-mission armed: seconds=%d result=%s",
					seconds, result);
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-end-mission rejected '%s' (want <seconds>:complete or <seconds>:fail)",
					endmission);
			}
		}
	}
	{
		const char *cloneguard = sysArgGetString("--debug-clone-guard");
		if (cloneguard && cloneguard[0]) {
			s32 seconds = -1;
			if (sscanf(cloneguard, "%d", &seconds) == 1 && seconds >= 0) {
				g_BootDebugCloneGuardSec = seconds;
				sysLogPrintf(LOG_NOTE, "BOOT: --debug-clone-guard armed: seconds=%d", seconds);
			} else {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-clone-guard rejected '%s' (want <seconds>)", cloneguard);
			}
		}
	}
	if (sysArgCheck("--debug-face-nearest-bot")) {
		g_BootDebugFaceNearestBot = 1;
		g_BootDebugPlaceBotNearPlayerTraceMask = 0;
		g_BootDebugPlaceBotNearPlayerTraceBudget = 160;
		sysLogPrintf(LOG_NOTE, "BOOT: --debug-face-nearest-bot armed");
	}
	{
		const char *mpopts = sysArgGetString("--debug-mp-options");
		if (mpopts && mpopts[0]) {
			g_BootDebugMpOptions = (u32)strtoul(mpopts, NULL, 0);
			g_BootDebugMpOptionsSet = true;
		}
	}
	/* --launch-mp-room takes three positional args: <arena> <scenario>
	 * <bot_count>.  sysArgGetString returns the slot immediately after
	 * the flag; we scan argv linearly for the next two.  Unset on
	 * malformed input so the post-init hook degrades to no-op. */
	if (g_BootLaunchMpArena) {
		s32 found = -1;
		for (s32 i = 1; i < argc - 1; ++i) {
			if (argv[i] && strcmp(argv[i], "--launch-mp-room") == 0) {
				found = i;
				break;
			}
		}
		if (found >= 0 && (found + 3) < argc) {
			g_BootLaunchMpScenario = argv[found + 2];
			g_BootLaunchMpBotCount = (s32)strtol(argv[found + 3], NULL, 0);
			if (g_BootLaunchMpBotCount < 0) {
				g_BootLaunchMpBotCount = 0;
			} else if (g_BootLaunchMpBotCount > 31) {
				g_BootLaunchMpBotCount = 31;
			}
		} else {
			g_BootLaunchMpArena = NULL;
		}
	}
	combatSimVerifyInit();

	conInit();
	sysInit();
	fsInit();
	/* Priority M / B-238 / M-1.7: optional perf harness for the .pdmod
	 * archive layer. Runs when --bench-pdmod is passed; exits cleanly when
	 * done. No effect otherwise. */
	{
		extern void modArchiveRunBenchmark(void);
		modArchiveRunBenchmark();
	}
	/* M0.2 Phase B: register pd.ini keys BEFORE configLoad (called inside configInit) */
	actionmapInit();
	configInit();
	netConfigSanitizeLoadedAddresses();
	/* M0.2 Phase B: parse bind strings that configLoad just populated */
	actionmapLoadBinds();
	inputLayerInit();
	sceneInit();

	/* D13: Initialize update system + save migration after filesystem is ready */
	updaterInit();
	saveMigrateInit();
	saveInit(); /* B-129: wire save dir into savefile.c — must follow fsInit() */

	/* Phase 1 connectivity: identity + social storage. Identity gives us a
	 * stable device UUID; the social store derives a 4-word connect code
	 * from that UUID and loads friends/blocks/visibility from disk. Must run
	 * before netInit() so any presence-aware net code can read the local
	 * identity. The dedicated server initialises identity through hubInit()
	 * instead, and does not load social state. */
	identityInit();
	socialInit();
	/* Mike directive 2026-05-17: cold-network-until-agent contract.
	 *
	 * All online subsystems that own a socket or publish identity
	 * (p2p / presence / groupSession / chat / voice / file-transfer /
	 * spectator / theater / listening-room / share / pdgui-toast) are
	 * deferred to socialHubBringOnline(), which the unified Agent Profile
	 * activation transaction fires after a successful complete load. The
	 * connect code that drives these subsystems is per-agent, so
	 * binding a UDP socket and announcing identity before an agent
	 * is loaded would publish a placeholder identity to friends.
	 *
	 * socialInit (local-only -- loads friends.json / blocks.json) is
	 * fine to run at boot. Everything else waits. */

	/* D13: Start background update check (non-blocking) */
	if (!g_BootExtractAssetsOnly && !sysArgCheck("--no-update-check")) {
		updaterCheckAsync();
	}
	if (!g_BootExtractAssetsOnly) {
		videoInit();
		pdguiInit(videoGetWindowHandle());
		/* menuMgrInit() removed — P10 D5.7 OG Menu Removal */
		statsInit();
		achievementsInit();
		/* D-005: capture the pd.ini machine baseline before the first unified
		 * Agent Profile is activated. */
		prefsAgentInit();

		/* D7: Discord Rich Presence — connects to Discord IPC pipe if running.
		 * Fails silently if Discord is not open. */
		discordInit();
		inputInit();

		/* Input context stack: must init after inputInit() (SDL event watch)
		 * and push g_CtxGameplay as the base context before any menu/GUI code
		 * that might reference the context stack. */
		inputCtxInit();
		inputCtxPush(&g_CtxGameplay);

		audioInit();
	} else {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --extract-assets-only set; window/UI/input startup skipped");
		sysLogPrintf(LOG_NOTE, "BOOT: --extract-assets-only set; audioInit() skipped");
	}

	/* Dedicated server: mute ALL audio — music and sound effects.
	 * The server has no player, no need for any audio output. */
	if (g_NetDedicated && !g_BootExtractAssetsOnly) {
		extern void optionsSetMusicVolume(s32 vol);
		optionsSetMusicVolume(0);
		sndSetSfxVolume(0);
	}

	/* Smoke verify harness init (2026-05-11): must come after SDL is up
	 * (videoInit + pdguiInit) so SDL_GetTicks() is valid for time
	 * scheduling, and after configInit/sysLogSet* registration so the
	 * test-declared channel mask + verbose flag override pd.ini cleanly.
	 * No-op when --smoke is not present. */
	if (!g_BootExtractAssetsOnly) {
		if (nativeWritePolicyRefused()) {
			sysLogPrintf(LOG_ERROR, "NATIVE.REUSE: device admission refused; controller visibility unverified");
			return 2;
		}
		(void)smokeHarnessInit();
		bootGridDraftInit();
		bootGridProposalInit();
	}

	/* Engine Phase 2 boot orchestrator (2026-05-03): the catalog work
	 * block (romdataInit through modmgrCatalogChanged) runs on a worker
	 * thread inside bootRunCatalogWork while the main thread drives the
	 * boot overlay's render loop.  Window stays responsive throughout.
	 *
	 * Single-worker enqueue per Phase 2 design: one job runs the full
	 * sequence serially.  Phase 3 will fan out the verify pass per file.
	 *
	 * Server build path is the same; bootPumpOverlay no-ops on
	 * g_NetDedicated and the worker still runs through the orchestrator. */
	bootProgressInit();
	bootPoolInit();
	if (!g_BootExtractAssetsOnly) {
		pdguiBootOverlayInit();
	}

	SDL_AtomicSet(&g_BootAssetChainFailures, 0);
	bootPoolEnqueue(bootRunCatalogWork, NULL);

	while (!bootProgressIsComplete()) {
		if (!g_BootExtractAssetsOnly) {
			pdguiBootOverlayPump();
		} else {
			SDL_Delay(1);
		}
		/* Smoke harness timeout coverage during boot: if the catalog work
		 * thread hangs we still want to fire the timeout exit rather than
		 * wait for some upstream watchdog. Cheap no-op when not in smoke
		 * mode. Does not push input events here -- the boot overlay frame
		 * does not consume SDL key events. */
		if (!g_BootExtractAssetsOnly) {
			if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
			smokeHarnessTick();
		}
		/* Cooperative pause when not in dedicated mode; the dedicated
		 * branch already inserts SDL_Delay inside the no-op pump path. */
	}

	bootPoolWaitIdle();

	if (!g_BootExtractAssetsOnly) {
		pdguiBootOverlayShutdown();
	}
	bootPoolShutdown();
	bootProgressShutdown();

	s32 assetChainFailures = SDL_AtomicGet(&g_BootAssetChainFailures);
	if (assetChainFailures > 0) {
		sysLoudFailf("ASSETCHAIN",
			"boot stopped: %d extraction/load failure(s); see earlier "
			"per-phase diagnostics", assetChainFailures);
		if (!g_BootExtractAssetsOnly) {
			/* The ordinary boot path initialized UI/input/audio/mod systems
			 * before the worker started. Register the normal teardown before
			 * returning the nonzero process result. */
			atexit(cleanup);
		}
		return 2;
	}

	/* Global AI lists, design step 5: the public rows must compile back to
	 * the exact bytes of g_GlobalAilists (scenario_global_ailists.c). */
	if (sysArgCheck("--check-global-ailists") &&
			scenarioGlobalAiListsRoundTrip() != 0) {
		sysLoudFailf("AI.GLOBAL",
			"global AI lists do not round-trip through their public rows; see the AI.GLOBAL lines");
	}

	if (g_BootExtractAssetsOnly) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init");
		sysLogPrintf(LOG_NOTE,
			"BOOT: --extract-assets-only shutdown; gameplay/window teardown skipped");
		return 0;
	}

	/* B-1044: normal mod/catalog population deliberately ignores mods/.temp.
	 * Inspect it only after the worker has completed so clean stale content can
	 * be retired immediately and a dirty tree stays quarantined behind the
	 * first-frame recovery modal. */
	netCrashRecoveryStartup();

	/* B-994: UI initializes before the background catalog/mod walk. Resolve
	 * and activate the persisted public .pdtheme exactly once here, on the
	 * main thread, after the worker has joined and before ordinary UI use. */
	pdguiThemeLoaderOnCatalogReady();

	atexit(cleanup);

	bootCreateSched();

	g_OsMemSize = osGetMemSize();

	g_MempHeapSize = g_OsMemSize;
	g_MempHeap = sysMemZeroAlloc(g_MempHeapSize);
	if (!g_MempHeap) {
		sysFatalError("Could not alloc %u bytes for memp heap.", g_MempHeapSize);
	}

	sysLogPrintf(LOG_NOTE, "memp heap at %p - %p", g_MempHeap, g_MempHeap + g_MempHeapSize);
	if (g_RomFile) {
		sysLogPrintf(LOG_NOTE, "rom  file at %p - %p", g_RomFile, g_RomFile + g_RomFileSize);
	} else {
		sysLogPrintf(LOG_NOTE, "rom  file released (Phase 3 Pass C): runtime reads disk-only");
	}

	/* NOTE: catalogValidateAll() was previously here, but it calls
	 * modeldefLoadToNew() -> mempAlloc() which requires the pool system.
	 * mempSetHeap() isn't called until mainInit() (pdmain.c), so loading
	 * models here crashed with access violations on every entry.
	 * Moved to pdmain.c after mempSetHeap(). */

	g_SndDisabled = sysArgCheck("--no-sound");

	const char *bootStageArg = sysArgGetString("--boot-stage");
	if (bootStageArg && bootStageArg[0]) {
		g_StageNum = bootResolveStageIdToNum(bootStageArg);
		if (g_StageNum < 0) {
			sysLogPrintf(LOG_WARNING,
				"BOOT: --boot-stage '%s' unresolved (try 0xNN, decimal, or base:* catalog ID)",
				bootStageArg);
			g_StageNum = STAGE_TITLE;
		}
	} else {
		g_StageNum = STAGE_TITLE;
	}

	g_FileAutoSelect = sysArgGetInt("--profile", -1);

	if (g_StageNum == STAGE_TITLE && (sysArgCheck("--skip-intro") || g_SkipIntro)) {
		// shorthand for --boot-stage 0x26
		g_StageNum = STAGE_CITRAINING;
	} else if (g_StageNum < 0x01 || g_StageNum > STAGE_EXTRA26) {
		// stage num out of range
		g_StageNum = STAGE_TITLE;
	}

	/* c115 --main-menu: corrected --skip-intro semantics.  Boot through
	 * STAGE_CITRAINING (same path --skip-intro takes), then arm the
	 * post-exit main menu auto-pop so the player lands at the main menu
	 * after the CI camera intro finishes.  Distinct from --skip-intro
	 * (which drops the player into CI free-roam with no menu).  See
	 * smoke-verify-dispatch-findings-2026-05-13.md S-5. */
	if (g_BootMainMenu && g_StageNum == STAGE_TITLE) {
		g_StageNum = STAGE_CITRAINING;
	}

	if (g_NetJoinLatch || g_NetHostLatch) {
		if (g_FileAutoSelect < 0) {
			// default to profile 0 if going into a net game
			g_FileAutoSelect = 0;
		}
		if (g_NetDedicated) {
			/* Dedicated server: load game defaults immediately so we don't
			 * need an agent file. The server doesn't play — it just hosts. */
			extern struct gamefile g_GameFile;
			extern void gamefileLoadDefaults(struct gamefile *file);
			gamefileLoadDefaults(&g_GameFile);
		}
		// skip the intro if going into a net game
		g_StageNum = STAGE_CITRAINING;
	}

	if (g_StageNum != STAGE_TITLE) {
		sysLogPrintf(LOG_NOTE, "boot stage set to 0x%02x", g_StageNum);
	}

	if (g_FileAutoSelect >= 0) {
		sysLogPrintf(LOG_NOTE, "player profile set to %d", g_FileAutoSelect);
	}


	/* Set FORWARDPITCH on by default for all players if not already set. */
	for (s32 i = 0; i < MAX_PLAYERS; ++i) {
		if (!(g_PlayerConfigsArray[i].options & ~(OPTION_FORWARDPITCH))) {
			/* options is still at zero (first-run default) — set our defaults */
			g_PlayerConfigsArray[i].options |= OPTION_FORWARDPITCH;
		}
	}

	/* Phase 3 Pass D (2026-05-02): drain the boot-deferred toast queue.
	 * romExtractVerifyAll / romExtractVerifyAllSegments populated the
	 * queue if they detected hash-mismatch + recovery / unrecoverable
	 * outcomes earlier in boot.  romExtractEmitBootIntegrityReport may
	 * have queued an aggregate toast as well.  Replay them now with
	 * fresh enqueued_ms timestamps so the toast renderer (kicks in once
	 * mainProc enters its loop) shows them as fresh notifications.
	 * Server build: no-op. */
	romExtractToastDrain();

	/* c115 (2026-05-13): apply smoke-verify CLI fast-paths now that the
	 * catalog is fully initialised and the game state is ready for
	 * scenario / match seeding.  Each fast-path is self-contained; on
	 * failure they log a warning and the boot continues to whatever
	 * stage g_StageNum points at. */
	bootApplyCliFastPaths();

	/* c126 (2026-05-18): arm the campaign auto-runner if --auto-campaign
	 * is on the command line. The first frame of mainTick picks up the
	 * armed state and pushes the requested solo mission via
	 * mainChangeToStage. Cheap no-op otherwise. */
	{
		extern void autocampaignInitFromCli(void);
		autocampaignInitFromCli();
	}

	mainProc();

	return 0;
}

PD_CONSTRUCTOR static void gameConfigInit(void)
{
	configRegisterInt("Game.MemorySize", &g_OsMemSizeMb, 64, 2048);
	configRegisterInt("Game.CenterHUD", &g_HudCenter, 0, 2);
	configRegisterInt("Game.MenuMouseControl", &g_MenuMouseControl, 0, 1);
	configRegisterFloat("Game.ScreenShakeIntensity", &g_ViShakeIntensityMult, 0.f, 10.f);
	configRegisterInt("Game.TickRateDivisor", &g_TickRateDiv, 0, 10);
	configRegisterInt("Game.ExtraSleep", &g_TickExtraSleep, 0, 1);
	configRegisterInt("Game.SkipIntro", &g_SkipIntro, 0, 1);
	configRegisterInt("Game.DisableMpDeathMusic", &g_MusicDisableMpDeath, 0, 1);
	configRegisterInt("Game.GEMuzzleFlashes", &g_BgunGeMuzzleFlashes, 0, 1);
	configRegisterInt("Debug.JumpLogging", &g_JumpLoggingEnabled, 0, 1);
	configRegisterInt("Debug.InteractCastDebugDraw", &g_InteractCastDebugDraw, 0, 1);
	for (s32 j = 0; j < MAX_LOCAL_PLAYERS; ++j) {
		const s32 i = j + 1;
		configRegisterFloat(strFmt("Game.Player%d.FovY", i), &g_PlayerExtCfg[j].fovy, 5.f, 175.f);
		configRegisterInt(strFmt("Game.Player%d.FovAffectsZoom", i), &g_PlayerExtCfg[j].fovzoom, 0, 1);
		configRegisterInt(strFmt("Game.Player%d.MouseAimMode", i), &g_PlayerExtCfg[j].mouseaimmode, 0, 1);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedX", i), &g_PlayerExtCfg[j].mouseaimspeedx, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.MouseAimSpeedY", i), &g_PlayerExtCfg[j].mouseaimspeedy, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.RadialMenuSpeed", i), &g_PlayerExtCfg[j].radialmenuspeed, 0.f, 10.f);
		configRegisterFloat(strFmt("Game.Player%d.CrosshairSway", i), &g_PlayerExtCfg[j].crosshairsway, 0.f, 10.f);
		configRegisterInt(strFmt("Game.Player%d.CrouchMode", i), &g_PlayerExtCfg[j].crouchmode, 0, CROUCHMODE_TOGGLE_ANALOG);
		configRegisterInt(strFmt("Game.Player%d.ExtendedControls", i), &g_PlayerExtCfg[j].extcontrols, 0, 1);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairColour", i), &g_PlayerExtCfg[j].crosshaircolour, 0, 0xFFFFFFFF);
		configRegisterUInt(strFmt("Game.Player%d.CrosshairSize", i), &g_PlayerExtCfg[j].crosshairsize, 0, 4);
		configRegisterInt(strFmt("Game.Player%d.CrosshairHealth", i), &g_PlayerExtCfg[j].crosshairhealth, 0, CROSSHAIR_HEALTH_ON_WHITE);
		configRegisterInt(strFmt("Game.Player%d.UseKeyReloads", i), &g_PlayerExtCfg[j].usereloads, 0, 0);
		configRegisterFloat(strFmt("Game.Player%d.JumpHeight", i), &g_PlayerExtCfg[j].jumpheight, 0.f, 20.f);
	}
}
