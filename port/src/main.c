#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <PR/ultratypes.h>
#include <PR/ultrasched.h>
#include <PR/os_message.h>

#include "lib/main.h"
#include "lib/anim.h"
#include "lib/mempc.h"
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
#include "config.h"
#include "mod.h"
#include "modmgr.h"
#include "modarchive.h"
#include "modvfs.h"
#include "modelcatalog.h"
#include "pdgui.h"
#include "pdgui_theme_loader.h"
/* menumgr.h removed — P10 D5.7 OG Menu Removal */
#include "playerstats.h"
#include "achievements.h"
#include "system.h"
#include "console.h"
#include "utils.h"
#include "net/net.h"
#include "net/netdistrib.h"
#include "net/p2p.h"
#include "net/group_session.h"
#include "updater.h"
#include "actionmap.h"
#include "inputlayer.h"
#include "scene.h"
#include "savemigrate.h"
#include "savefile.h"
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
#include "weapon_nested_runtime_harness.h"
#include "asset_source_debug.h"
#include "modasset_compiler.h"
#include "loader_pool.h"
#include "loader_walker.h"
#include "catalog_mgr_heads.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_arenas.h"
#include "game/stagetable.h"
#include "game/chr.h"
#include "game/music.h"
#include "game/bondgun.h"
#include "game/player.h"
#include "game/prop.h"
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
 *       Pre-render smoke hook: after a match stage is loaded and a bot prop
 *       exists, moves the first spawned bot into player 0's current room so
 *       render-source smokes can prove the custom bot model reaches the real
 *       prop/chr/model render path instead of depending on random arena spawn
 *       visibility. One-shot per boot.
 *
 *   --debug-mount-bike
 *       Post-setupCreateProps hook: walks g_Vars.activeprops on the
 *       first frame after stage load and mounts player 0 on the
 *       first OBJTYPE_HOVERBIKE prop found.  One-shot per boot;
 *       no effect on stages without a hoverbike.
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
 *   --debug-force-first-person
 *       Pre-render smoke hook: after the gameplay stage is loaded, holds
 *       player 0 in CAMERAMODE_DEFAULT for a short window so first-person
 *       weapon render diagnostics can prove generated source meshes reach
 *       bgunRender. Inert unless passed on argv.
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
 *       state or generated mesh render auditing. Weapon source smokes also
 *       get this stream through --debug-force-first-person /
 *       --debug-generated-mesh-render-audit.
 *
 *   --launch-load-agent <name>
 *       Post-saveInit hook: invokes saveLoadAgent(name) on the first
 *       frame after stage load (g_Vars.lvframenum >= 4) to read the
 *       pre-staged <savedir>/agent_<safeName>.json fixture into
 *       g_GameFile.  Provides smoke-test coverage of the saveLoadAgent
 *       wire-format path -- the production code has zero call-sites
 *       today (Agent Select routes through gamefileLoad, not
 *       saveLoadAgent; see port/PHASE_D5_PLAN.md:89 for the intended
 *       wiring).  Name is limited to 63 chars (buffer size 64) and
 *       sanitized inside buildSavePath().  One-shot per boot.
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
/* Mike directive 2026-05-18: CLI infrastructure for end-to-end smoke
 * tests of CS matches. --debug-auto-start-match arms a one-shot that
 * fires Start Match after the Room dialog opens, skipping the human
 * keyboard nav. --debug-mp-options seeds g_MpSetup.options with a
 * caller-supplied hex bitmask so smokes can preset MPOPTION_BOTJUMP /
 * MPOPTION_AUTORANDOMWEAPON_START / etc. without driving the lobby
 * UI checkboxes. Both consumed on first frame where the relevant
 * surface exists. */
static bool        g_BootDebugAutoStartMatch = false;
static s32         g_BootLaunchMpMatchPending = 0;
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
static s32         g_BootHostAutostartFired = 0;
static s32         g_BootHostAutostartSettleFrames = 0;
/* c3845: auto-start sequencing state machine.
 *   0 = waiting for remote-in-lobby + settle (handled by SettleFrames above)
 *   1 = room created + remote clients added; spacing frames before start
 *   2 = start fired (terminal; mirrored by g_BootHostAutostartFired) */
static s32         g_BootHostAutostartPhase = 0;
static s32         g_BootHostAutostartRoomId = 0xFF;
static s32         g_BootHostAutostartPostJoinFrames = 0;
static s32         g_BootMatchTimeLimitSec = 0; /* 0 = unset; >0 = forced seconds */
static u32         g_BootDebugMpOptions   = 0;
static bool        g_BootDebugMpOptionsSet = false;
static const char *g_BootDebugInstallReceivedMod = NULL;
static const char *g_BootDebugReceivePdcaList = NULL;
static const char *g_BootDebugRejectCatalogIngressList = NULL;
static const char *g_BootDebugSpawnWeapon = NULL;
static const char *g_BootDebugBotBody = NULL;
static const char *g_BootDebugBotHead = NULL;
static bool        g_BootDebugPlaceBotNearPlayer = false;
static s32         g_BootDebugPlaceBotNearPlayerPending = 0;
static s32         g_BootDebugPlaceBotNearPlayerFrames = 0;
static s32         g_BootDebugPlaceBotNearPlayerHoldFrames = 0;
static bool        g_BootDebugPlaceBotNearPlayerLogged = false;
static struct prop *g_BootDebugPlaceBotNearPlayerProp = NULL;
static u32         g_BootDebugPlaceBotNearPlayerTraceMask = 0;
static s32         g_BootDebugPlaceBotNearPlayerTraceBudget = 160;
static const s32   k_BootDebugPlaceBotNearPlayerMaxFrames = 900;
static const s32   k_BootDebugPlaceBotNearPlayerHoldMaxFrames = 180;
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

/* c118 (2026-05-15): --launch-load-agent one-shot. CLI fast-path that
 * invokes saveLoadAgent(name) directly at boot, mirroring the
 * --debug-mount-bike / --debug-spawn-at deferred-tick pattern. Provides
 * smoke-test coverage of the saveLoadAgent wire-format path without
 * scripted Agent Select menu nav (which routes through gamefileLoad,
 * not saveLoadAgent -- see port/PHASE_D5_PLAN.md:89 for the intended
 * wiring that was planned but never executed). The fixture is the v2
 * agent JSON pre-staged via run.ps1::Copy-SmokeFixtures. saveInit()
 * fires synchronously in main() at line ~957, well before any deferred
 * tick, so the save dir is wired when this fires. One-shot per boot. */
static s32         g_BootLoadAgentArmed = 0;
static char        g_BootLoadAgentName[64] = {0};

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
		const char *modsdir = modmgrGetModsDir();
		if (modsdir) {
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
			assetCatalogRegisterWeaponModelFiles();
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

	discordShutdown();
	inputLayerShutdown();
	sceneShutdown();
	inputCtxShutdown();
	updaterShutdown();
	pdguiShutdown();
	netDisconnect();
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

/* c115 (2026-05-14): chrMoveToPos lives in src/game/chraction.c; declare
 * an extern shim rather than #include "game/chraction.h" to keep this
 * PC-port file outside the chr*.h dependency surface (which transitively
 * drags in PR/gbi.h and N64 micro-code defines). Signature must match
 * src/include/game/chraction.h:208 exactly. */
extern bool chrMoveToPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms, f32 angle, bool ignorebg);

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
	if (g_BootDebugSpawnWeapon && g_BootDebugSpawnWeapon[0]) {
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
		s32 botidx = matchConfigAddBot(BOTTYPE_GENERAL, BOTDIFF_NORMAL,
			debug_body_id, debug_head_id, NULL);
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

/* Mike directive 2026-05-18: --debug-mp-options seed. Applied after
 * matchConfigInit so the bitmask wins over any default the Room would
 * otherwise set on entry. Idempotent (re-applies on each call). */
static void bootApplyDebugMpOptions(void)
{
	if (!g_BootDebugMpOptionsSet) return;
	extern struct mpsetup g_MpSetup;
	g_MpSetup.options = g_BootDebugMpOptions;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-mp-options applied -- g_MpSetup.options=0x%08x",
		g_BootDebugMpOptions);
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
		char *id = path ? strchr(path + 1, '|') : NULL;
		if (!path || !id) continue;
		*path++ = '\0';
		*id++ = '\0';
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
			ASSET_GAMEMODE, id) ? 0 : 1;
		sysLogPrintf(registered == 0 && catalog_absent && runtime_absent
				&& dependency_count == 0
				? LOG_NOTE : LOG_WARNING,
			"CATALOG.INGRESS.REJECT: kind=%s id='%s' registered=%d catalog_absent=%d runtime_absent=%d deps=%d",
			kind, id, registered, catalog_absent, runtime_absent,
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

/* Arm the --debug-mount-bike one-shot.  The actual mount runs inside
 * mainTick once activeprops is populated; see
 * bootDebugMountBikeTick() below. */
static void bootApplyDebugMountBike(void)
{
	if (!g_BootMountBike) {
		return;
	}
	g_BootMountBikePending = 1;
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
	g_BootDebugPlaceBotNearPlayerHoldFrames = 0;
	g_BootDebugPlaceBotNearPlayerLogged = false;
	g_BootDebugPlaceBotNearPlayerProp = NULL;
	g_BootDebugPlaceBotNearPlayerTraceMask = 0;
	g_BootDebugPlaceBotNearPlayerTraceBudget = 160;
	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-place-bot-near-player armed (will hold first spawned bot in player 0 room before prop sorting)");
}

s32 bootDebugPlaceBotNearPlayerIsAuditProp(const struct prop *prop)
{
	return g_BootDebugPlaceBotNearPlayerPending
		&& g_BootDebugPlaceBotNearPlayerProp
		&& prop == g_BootDebugPlaceBotNearPlayerProp;
}

void bootDebugPlaceBotNearPlayerTrace(const char *stage, const struct prop *prop,
		s32 once_bit, s32 a, s32 b, s32 c, s32 d)
{
	if (!modAssetCompilerGeneratedModeldefRenderAuditEnabled()) {
		return;
	}
	if (!g_BootDebugPlaceBotNearPlayerPending) {
		return;
	}
	if (!prop) {
		prop = g_BootDebugPlaceBotNearPlayerProp;
	}
	if (!prop || prop != g_BootDebugPlaceBotNearPlayerProp) {
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
 * the agent name into a static buffer; the actual saveLoadAgent call
 * runs inside mainTick once g_Vars.lvframenum >= 4 (same gate as the
 * other deferred ticks). Empty / missing arg leaves the latch off.
 * Name longer than 63 chars is rejected with a WARNING (the file-side
 * limit is SAVE_NAME_MAX but the CLI capture buffer is 64 to keep this
 * file independent of savefile.h's constants). */
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
			catalogDepForEachTyped(asset_id,
				bootCountCatalogIngressDependency, &dependency_count);
			sysLogPrintf(LOG_NOTE,
				"CATALOG.INGRESS.REJECT.STATE: id='%s' catalog_absent=1 provider_absent=1 runtime_absent=%d deps=%d",
				asset_id,
				assetRuntimeFindByTypeAndId(type, asset_id) ? 0 : 1,
				dependency_count);
		}
		return;
	}

	sysLogPrintf(LOG_NOTE,
		"BOOT: --debug-load-catalog-assets result type=%s id='%s' result=%s state=%d payload=%d ref=%d bytes=%u",
		type_name, asset_id, loaded ? "OK" : "FAIL",
		(s32)entry->load_state, (s32)entry->payload_kind,
		(s32)entry->ref_count, entry->data_size_bytes);

	if (!loaded) {
		return;
	}

	{
		const char *catalog_path = entry->source.primary.provider == fileProvider()
			? fileProviderPath(entry->source.primary) : NULL;
		const asset_runtime_binding_t *binding =
			assetRuntimeFindByTypeAndId(type, asset_id);
		const char *runtime_path = binding ? binding->primary_path : NULL;
		s32 exact = catalog_path && runtime_path
			&& strcmp(catalog_path, runtime_path) == 0;
		sysLogPrintf(exact ? LOG_NOTE : LOG_WARNING,
			"CATALOG.INGRESS.IDENTITY: type=%s id='%s' provider=%s length=%u exact=%d catalog='%s' runtime='%s'",
			type_name, asset_id,
			entry->source.primary.provider == fileProvider() ? "FileProvider" : "other",
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
	if (state) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK sound=%d handle=%p state=%p",
			kind, asset_id, audio->sound_id, (void *)handle, (void *)state);
		return 1;
	}
	if (audio->entry && audio->entry->load_state >= ASSET_STATE_LOADED) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK sound=%d handle=%p state=registered",
			kind, asset_id, audio->sound_id, (void *)handle);
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
	if (audio->sound_id >= 0 || (audio->entry && audio->entry->load_state >= ASSET_STATE_LOADED)) {
		if (audio->sound_id >= 0) {
			u32 compiled_size = 0;
			void *compiled = modSequenceLoad((u16)audio->sound_id, &compiled_size);
			if (!compiled) {
				sysLogPrintf(LOG_WARNING,
					"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL track=%d state=compile_failed",
					kind, asset_id, audio->sound_id);
				return 0;
			}
			sysMemFree(compiled);
		}
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=OK track=%d state=registered",
			kind, asset_id, audio->sound_id);
		return 1;
	}
	sysLogPrintf(LOG_WARNING,
		"BOOT: --debug-play-catalog-audio result kind=%s id='%s' result=FAIL track=%d state=unassigned",
		kind, asset_id, audio->sound_id);
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
	if (sysArgCheck("--debug-exit-after-catalog-probes")
			&& smokeHarnessIsActive()) {
		smokeHarnessExit(0, "scripted_exit");
	}
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

	for (s32 part = 0; part < 128; part++) {
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
static void bootApplyCliFastPaths(void)
{
	if (sysArgCheck("--debug-generated-mesh-render-audit")) {
		modAssetCompilerSetGeneratedModeldefRenderAudit(1);
	}
	bootApplyDebugInstallReceivedMod();
	bootApplyDebugRejectCatalogIngressList();
	bootApplyDebugReceivePdcaList();
	bootApplyMainMenu();
	bootApplyLaunchModdingHub();
	bootApplyLaunchCredits();
	bootApplyLaunchScenario();
	bootApplyLaunchMission();
	bootApplyLaunchMpRoom();
	bootApplyDebugMpOptions();
	bootApplyDebugMountBike();
	bootApplyDebugSpawnAt(sysArgGetString("--debug-spawn-at"));
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
	}
	return 1;
}

/* Called once per frame from pdmain.c's mainTick when the
 * --debug-mount-bike one-shot is armed.  Walks g_Vars.activeprops
 * looking for the first OBJTYPE_HOVERBIKE prop, and on match calls
 * currentPlayerTryMountHoverbike(prop) once, then clears the latch.
 *
 * Gates:
 *   - g_BootMountBikePending must be set (caller checks before calling)
 *   - g_Vars.lvframenum >= 4 (CI fly-in / load black frames out of the way)
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
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop) {
		return 0;
	}

	struct prop *prop = g_Vars.activeprops;
	while (prop) {
		if (prop->type == PROPTYPE_OBJ
				&& prop->obj
				&& prop->obj->type == OBJTYPE_HOVERBIKE
				&& (prop->obj->hidden & OBJHFLAG_MOUNTED) == 0) {
			/* currentPlayerTryMountHoverbike reads g_Vars.currentplayer +
			 * g_Vars.currentplayerstats, so make sure player 0 is current
			 * before we call.  Don't dereference unless setCurrentPlayerNum
			 * succeeded -- but it's a void function, so we trust the
			 * caller's invariant (lvFrame >= 4 implies player 0 alive). */
			setCurrentPlayerNum(0);
			bool ok = currentPlayerTryMountHoverbike(prop);
			sysLogPrintf(LOG_NOTE,
				"BOOT: --debug-mount-bike consumed: prop=%p stagenum=0x%02x result=%s",
				(void *)prop, (u32)g_Vars.stagenum, ok ? "MOUNTED" : "REJECTED");
			g_BootMountBikePending = 0;
			return 1;
		}
		prop = prop->next;
	}
	/* No hoverbike on this stage.  Clear the latch after the load
	 * settles so we don't keep scanning every frame for the entire
	 * session; a follow-up stage change won't re-fire (one-shot). */
	if (g_Vars.lvframenum > 120) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-mount-bike: no hoverbike prop on stagenum=0x%02x; one-shot consumed",
			(u32)g_Vars.stagenum);
		g_BootMountBikePending = 0;
		return 1;
	}
	return 0;
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
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop || !g_Vars.players[0]->prop->chr) {
		return 0;
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
	return 1;
}

s32 bootDebugPlaceBotNearPlayerPreRenderTick(void)
{
	struct chrdata *bot = NULL;
	struct prop *playerprop = NULL;
	struct coord target;
	RoomNum rooms[2];
	RoomNum room = (RoomNum)-1;

	if (!g_BootDebugPlaceBotNearPlayerPending) {
		return 0;
	}
	if (g_Vars.lvframenum < 4) {
		return 0;
	}
	if (!g_Vars.players[0] || !g_Vars.players[0]->prop || !g_Vars.players[0]->prop->chr) {
		return 0;
	}

	setCurrentPlayerNum(0);
	if (!g_Vars.currentplayer || !g_Vars.currentplayer->prop) {
		return 0;
	}

	playerprop = g_Vars.currentplayer->prop;
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

	target = g_Vars.currentplayer->cam_pos;
	f32 forward_x = g_Vars.currentplayer->cam_look.x;
	f32 forward_z = g_Vars.currentplayer->cam_look.z;
	if (forward_x > -0.001f && forward_x < 0.001f &&
			forward_z > -0.001f && forward_z < 0.001f) {
		forward_x = g_Vars.currentplayer->vv_sintheta;
		forward_z = g_Vars.currentplayer->vv_costheta;
	}
	target.x += forward_x * 240.0f;
	target.y = playerprop->pos.y;
	target.z += forward_z * 240.0f;
	rooms[0] = room;
	rooms[1] = -1;

	bool ok = chrMoveToPos(bot, &target, rooms, 0.0f, true);
	if (!ok) {
		goto wait_or_timeout;
	}

	if (bot->prop) {
		bot->prop->flags |= PROPFLAG_ENABLED
			| PROPFLAG_ONTHISSCREENTHISTICK
			| PROPFLAG_ONANYSCREENTHISTICK;
		propActivateThisFrame(bot->prop);
		g_BootDebugPlaceBotNearPlayerProp = bot->prop;
		bootDebugPlaceBotNearPlayerTrace("placed", bot->prop, 0,
			(s32)bot->prop->rooms[0],
			(s32)bot->prop->flags,
			(s32)bot->prop->active,
			(s32)g_Vars.lvframenum);
	}

	if (!g_BootDebugPlaceBotNearPlayerLogged) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-place-bot-near-player consumed: bot=%p chrnum=%d result=OK pos=(%f,%f,%f) room=%d player_room=%d camera_forward=(%f,%f) hold_frames=%d",
			(void *)bot,
			(s32)bot->chrnum,
			target.x, target.y, target.z,
			(s32)(bot->prop ? bot->prop->rooms[0] : (RoomNum)-1),
			(s32)room,
			forward_x, forward_z,
			k_BootDebugPlaceBotNearPlayerHoldMaxFrames);
		g_BootDebugPlaceBotNearPlayerLogged = true;
	}

	g_BootDebugPlaceBotNearPlayerHoldFrames++;
	if (g_BootDebugPlaceBotNearPlayerHoldFrames >= k_BootDebugPlaceBotNearPlayerHoldMaxFrames) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --debug-place-bot-near-player hold complete: bot=%p chrnum=%d frames=%d room=%d flags=0x%x",
			(void *)bot,
			(s32)bot->chrnum,
			g_BootDebugPlaceBotNearPlayerHoldFrames,
			(s32)(bot->prop ? bot->prop->rooms[0] : (RoomNum)-1),
			(u32)(bot->prop ? bot->prop->flags : 0));
		g_BootDebugPlaceBotNearPlayerPending = 0;
	}
	return 1;

wait_or_timeout:
	g_BootDebugPlaceBotNearPlayerFrames++;
	if (g_BootDebugPlaceBotNearPlayerFrames > k_BootDebugPlaceBotNearPlayerMaxFrames) {
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
			pos = g_Vars.currentplayer->prop->pos;
			pos.y += g_Vars.currentplayer->vv_eyeheight;
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
 * when the --launch-load-agent one-shot is armed. Invokes
 * saveLoadAgent(name) which reads the pre-staged JSON fixture from
 * <savedir>/agent_<safeName>.json and populates g_GameFile.
 *
 * Gates (mirror the other deferred-tick hooks):
 *   - g_BootLoadAgentArmed must be set
 *   - g_Vars.lvframenum >= 4 (deferred just like spawn-at / mount-bike;
 *     even though saveLoadAgent doesn't need stage/player props,
 *     deferring keeps the harness assertion ordering deterministic so
 *     the smoke test sees the SAVE: log lines after the SAVE: initialized
 *     line from saveInit())
 *
 * saveLoadAgent returns 0 on success, -1 on failure. Logs OK or FAILED
 * based on the return; saveLoadAgent itself emits its own SAVE: log
 * lines (loaded / failed to load / refusing to load) which the smoke
 * test can additionally assert on.
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

	s32 result = saveLoadAgent(g_BootLoadAgentName);

	sysLogPrintf(LOG_NOTE,
		"BOOT: --launch-load-agent consumed: name='%s' result=%s",
		g_BootLoadAgentName, result == 0 ? "OK" : "FAILED");

	/* Mike directive 2026-05-17: this CLI fast-path is the smoke-only
	 * counterpart to the live UI agent-select path in
	 * pdgui_menu_agentselect.cpp. Mirror its post-load hooks so the
	 * social-hub gate flips here too -- smoke tests can then exercise
	 * post-agent presence behavior the same way the live UI does. */
	if (result == 0) {
		socialRebindToActiveAgent(g_BootLoadAgentName);
		socialHubBringOnline();
		presenceMarkAgentLoaded();
	}

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
 * netLobbyRequestStartWithSims(GAMEMODE_MP, ...) seeded from g_MatchConfig
 * (which --launch-mp-room populated). The bridge replays CLC_LOBBY_START
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
s32 bootHostAutostartTick(void)
{
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
		return 0;
	}
	if (!g_NetLocalClient || g_NetLocalClient->state < CLSTATE_LOBBY) {
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
			remoteInLobby = 1;
			break;
		}
	}
	if (!remoteInLobby) {
		g_BootHostAutostartSettleFrames = 0; /* reset settle if peer drops pre-fire */
		return 0;
	}

	/* Peer is in lobby; hold a short settle window so the manifest/catalog
	 * exchange that follows CLC_AUTH has time to quiesce before we fire. */
	if (g_BootHostAutostartSettleFrames < 120) {
		g_BootHostAutostartSettleFrames++;
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
		u8 roomId = netLobbyRequestCreateRoom();
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

	u8 weaponSet = (u8)(g_MatchConfig.weaponSetIndex >= 0
		? g_MatchConfig.weaponSetIndex : 0xFF);
	/* Bot count comes from --launch-mp-room's third positional arg (the
	 * Combat Sim path the Room UI uses passes the room's bot count here as
	 * numSims). With 0 bots and two connected humans (host + joined client)
	 * the CLC_LOBBY_START handler builds 2 player participants, which is a
	 * valid Combat Sim (base:combat min players = 2) -- no bots required. */
	u8 numSims = (u8)(g_BootLaunchMpBotCount < 0 ? 0
		: (g_BootLaunchMpBotCount > 31 ? 31 : g_BootLaunchMpBotCount));
	/* simType 2 = Normal (matches getLeadSimType()'s no-bot default). */
	sysLogPrintf(LOG_NOTE,
		"BOOT: --host-autostart firing: stage='%s' scenario=%u timelimit=%u sims=%u",
		g_MatchConfig.stage_id, (unsigned)g_MatchConfig.scenario,
		(unsigned)g_MatchConfig.timelimit, (unsigned)numSims);

	s32 rc = netLobbyRequestStartWithSims(
		0 /* GAMEMODE_MP */,
		g_MatchConfig.stage_id,
		0,                              /* difficulty (unused for MP) */
		0xFF,                           /* antiClientId = NET_NULL_CLIENT */
		numSims,                        /* bot count from --launch-mp-room */
		2,                              /* simType = Normal */
		g_MatchConfig.timelimit,
		g_MatchConfig.options,
		g_MatchConfig.scenario,
		g_MatchConfig.scorelimit,
		g_MatchConfig.teamscorelimit,
		weaponSet);

	sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_WARNING,
		"BOOT: --host-autostart consumed: result=%s rc=%d",
		rc == 0 ? "OK" : "FAILED", (int)rc);
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

int main(int argc, const char **argv)
{
	sysInitArgs(argc, argv);

	/* D13: Apply pending update VERY EARLY — before any subsystem init.
	 * If an update was downloaded previously, this renames the .update file
	 * into place and re-execs. If no pending update, this is a no-op.
	 * NOTE: sysLogPrintf is safe to call before sysInit (uses static buffers).
	 * detectExePath uses only stdlib — no SDL or game init required. */
	updaterApplyPending();

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
	/* c3845 (2026-06-23): listen-host two-process match smoke infra. */
	g_BootHostAutostartArmed = sysArgCheck("--host-autostart") ? true : false;
	if (g_BootHostAutostartArmed) {
		sysLogPrintf(LOG_NOTE, "BOOT: --host-autostart armed");
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
	g_BootDebugPlaceBotNearPlayer = sysArgCheck("--debug-place-bot-near-player") ? true : false;
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
	 * deferred to socialHubBringOnline(), which prefsAgentLoad and
	 * bootLaunchLoadAgentTick fire AFTER a successful agent load. The
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
		/* S309: per-agent preferences sidecar.  prefsAgentLoad runs on agent
		 * switch; this init just marks the subsystem live. */
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
		(void)smokeHarnessInit();
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
	const char *weaponNestedPlan =
		sysArgGetString("--debug-weapon-nested-harness");
	if (weaponNestedPlan && weaponNestedPlan[0]) {
		s32 pass = weaponNestedRuntimeHarnessRun(weaponNestedPlan);
		if (smokeHarnessIsActive()) {
			smokeHarnessExit(pass ? 0 : 1,
				pass ? "weapon_nested_harness_pass" : "weapon_nested_harness_fail");
		}
	}
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

	if (g_BootExtractAssetsOnly) {
		sysLogPrintf(LOG_NOTE,
			"BOOT: --extract-assets-only complete; exiting before scheduler/stage/gameplay init");
		sysLogPrintf(LOG_NOTE,
			"BOOT: --extract-assets-only shutdown; gameplay/window teardown skipped");
		return 0;
	}

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
