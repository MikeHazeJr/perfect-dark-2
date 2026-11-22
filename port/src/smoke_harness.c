/*
 * Smoke verify harness (Phase 1, 2026-05-11).
 *
 * See port/include/smoke_harness.h for the API contract and
 * context/designs/engine/smoke-verify-gate.md for the architecture.
 *
 * The harness operates on module-static state. It is single-instance
 * by construction: --smoke <path> sets one test definition, the runner
 * launches one client process per test.
 *
 * Implementation summary:
 *
 *   smokeHarnessInit()
 *     - scan argv for --smoke <path>
 *     - load the JSON test file from disk
 *     - tokenise + decode into s_State (events, assertions are inert at
 *       runtime; the runner re-checks them after exit)
 *     - apply log channel mask + verbose flag
 *     - record start_ticks_ms = SDL_GetTicks()
 *
 *   smokeHarnessTick()
 *     - elapsed_ms = SDL_GetTicks() - start_ticks_ms
 *     - while next event's at_ms <= elapsed_ms: dispatch event
 *     - if elapsed_ms >= timeout_ms: smokeHarnessExit(1, "timeout")
 *
 *   smokeHarnessExit(code, reason)
 *     - log "SMOKE: result=<reason> elapsed_ms=<N> events_fired=<M>"
 *     - exit(code)
 *
 * Event schema (input_sequence[] in the JSON test file). Entries run in at_ms
 * order (ties in written order), and at_ms is script time, which stands still
 * during a wait_until; write the entries in that order, since the harness
 * refuses a sequence that goes back in time.
 *
 *   { "at_ms": N, "type": "wait" }
 *     -- inert marker; useful for sequencing comments / dwell.
 *
 *   { "at_ms": N, "type": "wait_until",
 *     "condition": "network_stage_live", "timeout_ms": 120000 }
 *     -- pause this process's script timeline until a typed production-state
 *        condition is true. Supported conditions are title_sequence_ready,
 *        network_listen_ready, network_stage_live, network_reconnect_available,
 *        cutscene_skip_ready, gameplay_ready, offline_gameplay_ready, and
 *        vehicle_driver_ready, endscreen_visible, agent_select_ready,
 *        agent_create_ready, forge_editor_ready, forge_level_file_open and
 *        scenario_token_held (the scenario authority, offline or the listen
 *        host, sees a living simulant carry the uplink, a briefcase or a
 *        case). The real-time harness
 *        watchdog continues while the script is paused, and every wait
 *        requires a bounded timeout.
 *
 *   { "at_ms": N, "type": "wait_until", "condition": "gameplay_ready",
 *     "timeout_ms": 60000, "stable_ms": 3000,
 *     "assist_action": "ACTION_SKIP_CUTSCENE",
 *     "assist_condition": "cutscene_skip_ready", "assist_hold_ms": 900 }
 *     -- require continuous target readiness while optionally issuing exactly
 *        one bounded production action through a separate typed aperture. The
 *        assist releases on hold expiry and every terminal harness path.
 *
 *   { "at_ms": N, "type": "exit" }
 *     -- scripted clean exit; produces SMOKE: result=scripted_exit.
 *
 *   { "at_ms": N, "type": "unclean_exit" }
 *     -- validation-only process exit after flushing the log, deliberately
 *        skipping atexit so startup crash recovery sees the dirty session.
 *
 *   { "at_ms": N, "type": "network_retire_held_weapon",
 *     "client_id": 1, "hand": "right" }
 *     -- resolve a live player through its stable network identity, validate
 *        the complete held-weapon attachment, then invoke the production
 *        weaponDeleteFromChr lifecycle. It does not alter reconnect packets,
 *        exact-set counters, or receiver state.
 *
 *   { "at_ms": N, "type": "kill_token_holder" }
 *     -- on the scenario authority, the simulant that carries the scenario's
 *        token (wait for scenario_token_held first) dies through the
 *        production damage path (chrDamageByMisc, as poison kills), which the
 *        host replicates. Ends the smoke as a failure when no simulant holds
 *        one, and on a networked client.
 *
 *   { "at_ms": N, "type": "network_timeout_client", "client_id": 1 }
 *   { "at_ms": N, "type": "network_reconnect" }
 *     -- deterministic smoke-only triggers around the production timeout and
 *        reconnect APIs. They do not replace ENet teardown/auth/stage flow.
 *
 *   { "at_ms": N, "type": "key", "key": "Return", "action": "tap" }
 *     -- inject SDL_KEYDOWN / KEYUP for a named key or numeric scancode.
 *        `action` may be "tap" (auto-release one frame later), "press"
 *        (no auto-release) or "release". `tap` is the default.
 *
 *   { "at_ms": N, "type": "action",
 *     "name": "ACTION_MENU_ACCEPT", "action": "tap" }
 *     -- inject an action-map press/release edge directly via
 *        actionmapInjectStateForSmoke(). Bypasses SDL events and the
 *        ImGui focus gate. Deterministic, focus-independent. Use this
 *        when the SDL window cannot reliably hold focus (firewall prompt,
 *        OS modal, alt-tab to a different process). `name` accepts the
 *        full ACTION_* enum identifier or its CamelCase short form
 *        (e.g. "Use", "MenuUp"). `action` may be "tap" / "press" /
 *        "release"; `tap` is the default.
 *
 *   { "at_ms": N, "type": "mouse",
 *     "x": 640, "y": 400, "button": "left", "action": "click" }
 *     -- synthesise an SDL_MOUSEBUTTONDOWN / UP pair at {x, y} for
 *        "left" / "right" / "middle". Needed for genuinely-mouse-only
 *        UIs (Settings -> Debug -> Test Scenarios radios, modding hub
 *        file pickers, skin editor canvas). `action` may be "tap" /
 *        "click" / "press" / "release"; `tap` and `click` are aliases
 *        and produce an auto-release on the next tick. `tap` is the
 *        default.
 *
 *   { "at_ms": N, "type": "mouse_move", "x": 640, "y": 400 }
 *     -- inject SDL_MOUSEMOTION at an absolute client coordinate. Use this
 *        before a mouse click or wheel event so ImGui's hover/focus target is
 *        established through the same SDL backend used in ordinary play.
 *
 *   { "at_ms": N, "type": "mouse_wheel", "wheel_x": 0, "wheel_y": -1 }
 *     -- inject SDL_MOUSEWHEEL through the ordinary ImGui/action-map event
 *        path. The current hover target comes from the preceding mouse_move.
 *
 *   { "at_ms": N, "type": "receive_pdca_list", "path": "social/test/list.txt" }
 *     -- deliver raw PDCA fixtures through the production network
 *        BEGIN/CHUNK/END receive handlers at a deterministic live point.
 *        The event exists only while the smoke harness is active.
 *
 *   { "at_ms": N, "type": "agent_activate", "name": "Agent" }
 *   { "at_ms": N, "type": "agent_delete", "name": "Agent" }
 *     -- invoke the production Agent Session boundary from an ordinary client
 *        so failure rollback and active-delete rules can be observed in one
 *        live process.
 *
 *   { "at_ms": N, "type": "give_weapon", "path": "base:grenade" }
 *     -- player 0 gets the weapon with full ammunition for both functions
 *        and draws it (projectile-events.md slice 1), so one smoke can fire
 *        every projectile weapon in turn. Logs SMOKE.GIVE_WEAPON.
 *
 *   { "at_ms": N, "type": "catalog_weapon_acquire", "path": "mod:id" }
 *   { "at_ms": N, "type": "catalog_weapon_release", "path": "mod:id" }
 *     -- acquire or release one explicit owner through the production typed
 *        weapon lifecycle. These smoke-only events make overlapping-owner
 *        balance observable without replacing the lifecycle implementation.
 *
 *   Both `action` and `mouse` events use the same tap-release sweep
 *   path that the existing key-tap events use, so they share the
 *   one-frame auto-release timing (SMOKE_TAP_RELEASE_MS).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <SDL.h>

#include <PR/ultratypes.h>

#include "bss.h"
#include "data.h"
#include "smoke_fixture_schema.h"
#include "smoke_harness.h"
#include "smoke_readiness.h"
#include "smoke_transition.h"
#include "smoke_virtual_mapping.h"
#include "system.h"
#include "native_write_policy.h"
#include "actionmap.h"   /* actionmapResolveByName, actionmapInjectStateForSmoke */
#include "assetcatalog.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "assetprovider.h"
#include "asset_runtime.h"
#include "net/netdistrib.h"
#include "net/net.h"
#include "net/net_reconnect.h"
#include "net/matchsetup.h"
#include "grid_match_runtime.h"
#include "fs.h"
#include "room.h"
#include "game/bot.h"
#include "game/chraction.h"
#include "game/player.h"
#include "game/playermgr.h"
#include "game/propobj.h"
#include "game/prop.h" /* propExecuteTickOperation (touch_pickup) */
#include "game/forgemode.h"
#include "forge_level_io.h"
#include "game/setuputils.h"
#include "lib/meshcollision.h"
#include "lib/collision.h" /* cdFindGroundAtCyl (B-1310 table approach) */
#include "lib/mtx.h"       /* mtx4TransformVec (B-1310 table bounds) */
#include "scenario_source_runtime.h"
#include "projectile_events.h"
#include "mode_events.h"
#include "constants.h"
#include "game/inv.h"
#include "game/bondgun.h"
#include "input.h"
#include "input_device_identity.h"
#include "inputctx.h"
#include "inputlayer.h"
#include "menupool.h"
#include "pdgui.h"
#include "pdgui_menu_readiness.h"
#include "pdgui_glyphs.h"
#include "pdgui_text_keyboard.h"
#include "scene.h"
#include "agent_session.h"
#include "prefs_agent.h"
#include "file_transfer.h"
#include "presence.h"
#include "social.h"

/* Actual-core smoke probe, never ordinary input admission. */
extern int modmgrSmokeProbePreparedRetry(void);
extern s32 netLocalClientPreparingGridTransfer(void);
extern s32 pdguiCountdownIsActive(void);
extern s32 pdguiCountdownGetSecs(void);

/* c115 (2026-05-14): need gfxGetSdlWindow to stamp the correct windowID
 * on synthesised SDL events. ImGui's SDL2 backend filters events whose
 * windowID does not match the window captured at ImGui_ImplSDL2_Init
 * time -- with windowID=0 the synthesised keys reach the SDL queue but
 * ImGui drops them on the floor, so menu nav never advances. */
#include "../fast3d/gfx_sdl.h"

/* Use the port-level config registration so harness-set values can be
 * queried by other subsystems if needed.  No new pd.ini keys are
 * registered -- the harness reads its config from the test JSON. */
#include "config.h"

/* ---------------------------------------------------------------- */
/* Constants                                                        */
/* ---------------------------------------------------------------- */

#define SMOKE_MAX_EVENTS         512
#define SMOKE_PAD_COUNT          2
#define SMOKE_MAX_PATH           1024
#define SMOKE_MAX_NAME           96
#define SMOKE_DEFAULT_TIMEOUT_MS 90000
#define SMOKE_MAX_WAIT_TIMEOUT_MS 3600000
#define SMOKE_MAX_EVENT_AT_MS    3600000
#define SMOKE_TAP_RELEASE_MS     16   /* one 60Hz frame */
#define SMOKE_PAD_TRANSITION_MS 1000u

typedef enum {
    SMOKE_EVENT_NONE = 0,
    SMOKE_EVENT_WAIT,
    SMOKE_EVENT_WAIT_UNTIL,
    SMOKE_EVENT_KEY_PRESS,
    SMOKE_EVENT_KEY_RELEASE,
    SMOKE_EVENT_KEY_TAP,
    SMOKE_EVENT_ACTION_PRESS,
    SMOKE_EVENT_ACTION_RELEASE,
    SMOKE_EVENT_ACTION_TAP,
    SMOKE_EVENT_MOUSE_PRESS,
    SMOKE_EVENT_MOUSE_RELEASE,
    SMOKE_EVENT_MOUSE_TAP,
    SMOKE_EVENT_MOUSE_MOVE,
    SMOKE_EVENT_MOUSE_WHEEL,
    SMOKE_EVENT_CONTROLLER_ATTACH,
    SMOKE_EVENT_CONTROLLER_BUTTON,
    SMOKE_EVENT_CONTROLLER_AXIS,
    SMOKE_EVENT_CONTROLLER_DETACH,
    SMOKE_EVENT_RECEIVE_PDCA_LIST,
    SMOKE_EVENT_AGENT_ACTIVATE,
    SMOKE_EVENT_AGENT_DELETE,
    SMOKE_EVENT_MODMGR_APPLY_RETRY_PROBE,
    SMOKE_EVENT_FILE_TRANSFER_SOCKET_PROBE,
    SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE,
    SMOKE_EVENT_CATALOG_WEAPON_RELEASE,
    SMOKE_EVENT_CATALOG_RECOVERY_PROBE,
    SMOKE_EVENT_NETWORK_RETIRE_HELD_WEAPON,
    SMOKE_EVENT_NETWORK_ASSERT_RETIRED_PROP_ABSENT,
    SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT,
    SMOKE_EVENT_NETWORK_RECONNECT,
    SMOKE_EVENT_KILL_PLAYER,        /* T-MODDING-008: local player 0 dies (playerDie) */
    SMOKE_EVENT_KILL_TOKEN_HOLDER,  /* B-1209: the simulant carrying the scenario token dies */
    SMOKE_EVENT_KILL_SIMULANT,      /* T-MODDING-011: the first live simulant dies (chrDamageByMisc) */
    SMOKE_EVENT_MOVEMENT_LEDGE,
    SMOKE_EVENT_USE_DOOR,           /* T-MODDING-011: player 0 uses the nearest door (propdoorInteract) */
    SMOKE_EVENT_USE_LIFT_DOOR,      /* T-MODDING-011: player 0 uses the nearest lift door */
    SMOKE_EVENT_DAMAGE_GLASS,       /* T-MODDING-011: player 0's shot breaks the nearest glass (objDamage) */
    SMOKE_EVENT_GIVE_WEAPON,        /* T-MODDING-011: player 0 gets and draws the weapon at path */
    SMOKE_EVENT_TOUCH_PICKUP,       /* T-MODDING-011: player 0 touches the nearest pickup (objPickupTouchForSmoke) */
    SMOKE_EVENT_SCREENSHOT,         /* in-game glReadPixels grab -> path */
    SMOKE_EVENT_EXIT,
    SMOKE_EVENT_UNCLEAN_EXIT
} SmokeEventType;

#define SMOKE_MAX_SHOTPATH 256

typedef enum {
    SMOKE_LEDGE_PLACE_PLAYER,
    SMOKE_LEDGE_PLACE_BOT,
    SMOKE_LEDGE_WALK_OFF,
    SMOKE_LEDGE_VERIFY
} SmokeLedgeCommand;

typedef enum {
    SMOKE_SUPPORT_COMPLEX_LEDGE,
    SMOKE_SUPPORT_VILLA_TABLETOP
} SmokeSupportFixtureId;

typedef struct {
    const char *marker;
    s32 stage;
    RoomNum room;
    f32 x;
    f32 lane_half_width;
    f32 start_z;
    f32 lower_y;
    f32 upper_y;
    f32 edge_z;
    f32 upper_min_z;
    f32 upper_max_z;
    f32 upper_goal_z;
    f32 lower_goal_z;
    f32 lower_return_max_z;
    s32 table_command;
    s32 table_pad;
    /* B-1310: move the authored approach onto the live table's collision
     * bounds (the authored tabletop numbers were never checked in a game). */
    s32 measure_table;
} SmokeSupportFixture;

typedef struct {
    const SmokeSupportFixture *fixture;
    SmokeSupportFixture measured; /* B-1310: the fixture moved onto the live table */
    s32 trace_frames;             /* B-1310: samples logged after takeoff */
    f32 peak_feet;                /* B-1310: highest feet after takeoff */
    struct prop *actor_prop;
    struct defaultobj *table;
    struct prop *table_prop;
    struct coord table_pos;
    f32 table_rotation[3][3];
    s32 actor_chrnum;
    s32 bot_slot;
    u32 epoch;
    s32 last_frame;
    s32 land_frame;
    s32 support_samples;
    s32 took_off;
    s32 descended;
    s32 landed;
    s32 stayed;
    s32 walk_off;
    s32 dropped;
    s32 lower_landed;
    /* B-1310, candidate 4: the actor's z on the last observed frame (its
     * speed along the approach), samples before takeoff, and the player's
     * move-axis releases with the stop they predicted. */
    f32 last_z;
    s32 approach_frames;
    s32 approach_samples;
    s32 approach_released;
    s32 walk_released;
    f32 approach_predicted_z;
    f32 walk_predicted_z;
} SmokeLedge;

typedef struct {
    s32            at_ms;
    SmokeEventType type;
    s32            scancode;          /* for key events */
    s32            action_id;         /* for action events: resolved InputAction */
    s32            mouse_x;           /* for mouse events */
    s32            mouse_y;           /* for mouse events */
    s32            mouse_button;      /* for mouse events: SDL_BUTTON_LEFT/RIGHT/MIDDLE */
    s32            mouse_wheel_x;     /* for mouse-wheel events */
    s32            mouse_wheel_y;     /* for mouse-wheel events */
    s32            controller_control; /* SDL logical button/axis */
    s32            controller_value;   /* button 0/1 or raw virtual Sint16 axis */
    s32            controller_slot;    /* expected player slot for the virtual device */
    s32            client_id;         /* stable target for network events */
    s32            hand;              /* held-weapon event: HAND_RIGHT/LEFT */
    SmokeLedgeCommand ledge_command;
    SmokeSupportFixtureId ledge_fixture;
    s32            release_at_ms;     /* for tap: scheduled release time */
    s32            released;          /* tap: 0 = press pending, 1 = release pending, 2 = done */
    smoke_readiness_condition_t readiness_condition;
    u32            wait_timeout_ms;
    u32            stable_ms;
    s32            assist_action_id;
    smoke_readiness_condition_t assist_condition;
    u32            assist_hold_ms;
    char           path[SMOKE_MAX_SHOTPATH]; /* for screenshot events: output file */
} SmokeEvent;

typedef struct {
    const char *arena;
    const struct prop *player_prop;
    u32 stage_generation;
    s32 block;
    s32 saw_input;
    s32 saw_approach;
    s32 saw_aperture;
    s32 wall_frame;
    f32 wall_x;
    s32 complete;
} SmokeGridPortal;

typedef struct {
    s32          active;
    char         test_path[SMOKE_MAX_PATH];
    char         screenshot_dir[SMOKE_MAX_PATH]; /* --smoke-screenshot-dir: glReadPixels output dir */
    char         scenario_name[SMOKE_MAX_NAME];
    s32          timeout_ms;
    u32          channel_mask;       /* applied via sysLogSetChannelMask */
    s32          verbose;
    s32          jump_logging;       /* test opt-in: enable JUMP:/CAPSULE: markers */
    SmokeEvent   events[SMOKE_MAX_EVENTS];
    s32          event_count;
    s32          next_event_idx;
    u32          start_ticks_ms;
    u32          timeline_pause_ms;
    u32          wait_started_ticks_ms;
    s32          waiting_event_idx;
    smoke_transition_state_t wait_transition;
    s32          wait_assist_injected;
	s32          wait_assist_press_count;
	s32          wait_assist_release_count;
    s32          observed_stage_ready_count;
    s32          observed_stage_teardown_count;
    s32          ready_stage_num;
    u32          ready_stage_generation;
    SmokeGridPortal grid_portal;
    u32          retired_network_prop_syncid;
    u32          retired_network_parent_syncid;
    s32          retired_network_client_id;
    SmokeLedge   ledge;
    s32          events_fired;
    s32          exited;             /* prevent re-entrant exit */
} SmokeState;

static SmokeState s_State;

static s32 smokeGridPortalFail(const char *reason)
{
    const struct player *player = g_Vars.players[0];
    const struct prop *prop = player ? player->prop : NULL;
    sysLogPrintf(LOG_ERROR,
        "GRID.PORTAL.FAIL: arena=%s reason=%s pos=(%.1f,%.1f,%.1f) room=%d yaw=%.1f ground=%.1f speed=%.2f axis=(%.3f,%.3f) focus_lost=%d",
        s_State.grid_portal.arena ? s_State.grid_portal.arena : "", reason,
        prop ? prop->pos.x : 0.0f, prop ? prop->pos.y : 0.0f, prop ? prop->pos.z : 0.0f,
        prop ? (s32)prop->rooms[0] : -1, player ? player->vv_theta : 0.0f,
        player ? player->vv_ground : 0.0f, player ? player->speedforwards : 0.0f,
        actionValue(0, ACTION_AXIS_MOVE_X), actionValue(0, ACTION_AXIS_MOVE_Y), inputCtxIsFocusLost());
    smokeHarnessExit(1, "grid_portal_witness_failed");
    return 0;
}

static s32 smokeGridPortalAnchor(void)
{
    const char *mode = sysArgGetString("--debug-grid-portal-witness");
    SmokeGridPortal *probe = &s_State.grid_portal;
    if (!mode || probe->stage_generation) return 1;

    probe->arena = sysArgGetString("--debug-grid-portal-arena");
    probe->block = strcmp(mode, "block") == 0;
    if (!probe->arena || (!probe->block && strcmp(mode, "cross") != 0)) {
        return smokeGridPortalFail("invalid_arguments");
    }
    const asset_entry_t *arena = assetCatalogResolve(probe->arena);
    struct player *player = g_Vars.players[0];
    if (g_NetMode != NETMODE_NONE || !arena || !arena->enabled || arena->type != ASSET_ARENA
            || arena->ext.arena.load_mode != ARENA_LOADMODE_PLAYABLE
            || arena->ext.arena.stagenum != g_StageNum || g_Vars.stagenum != g_StageNum
            || g_Vars.roomcount != 3 || !s_State.ready_stage_generation
            || !player || !player->prop || !player->prop->chr) {
        return smokeGridPortalFail("unexpected_stage_or_player");
    }

    /* Anchor once on the loaded stage. All subsequent movement, including
     * crossing the shared plane, comes from the fixture's ordinary input. */
    struct coord position = {-350.0f, 160.0f, probe->block ? 450.0f : 0.0f};
    RoomNum rooms[] = {1, -1};
    if (!chrMoveToPos(player->prop->chr, &position, rooms, M_BADTAU / 4.0f, true)
            || player->prop->rooms[0] != 1 || player->prop->rooms[1] != -1
            || fabsf(player->prop->pos.x - position.x) > 1.0f
            || fabsf(player->prop->pos.z - position.z) > 1.0f) {
        return smokeGridPortalFail("anchor_refused");
    }
    probe->player_prop = player->prop;
    probe->stage_generation = s_State.ready_stage_generation;
    sysLogPrintf(LOG_NOTE, "GRID.PORTAL.ANCHOR: arena=%s mode=%s room=1 start_x=%.0f start_z=%.0f",
        probe->arena, mode, player->prop->pos.x, player->prop->pos.z);
    return 1;
}

static s32 smokeGridPortalObserve(void)
{
    SmokeGridPortal *probe = &s_State.grid_portal;
    if (!probe->stage_generation || probe->complete) return 1;
    const struct player *player = g_Vars.players[0];
    if (s_State.ready_stage_generation != probe->stage_generation
            || !player || player->prop != probe->player_prop || player->isdead) {
        return smokeGridPortalFail("player_or_stage_changed");
    }
    /* Own the exact axis read by movement. A digital forward action can be
     * readable to smoke while ordinary focus rules suppress WASD synthesis. */
    const s32 held = actionHeld(0, ACTION_AXIS_MOVE_Y);
    if (!held && !probe->saw_input) return 1;
    const struct prop *prop = player->prop;
    const f32 x = prop->pos.x;
    const f32 z = prop->pos.z;
    const s32 room = prop->rooms[0];
    const s32 exclusive = prop->rooms[1] == -1;
    const s32 grounded = player->bondmovemode == MOVEMODE_WALK && !player->isfalling
        && fabsf(player->vv_ground) <= 5.0f && fabsf(player->vv_manground - player->vv_ground) <= 5.0f;
    if (held) {
        const f32 move_x = actionValue(0, ACTION_AXIS_MOVE_X);
        const f32 move_y = actionValue(0, ACTION_AXIS_MOVE_Y);
        if (!(fabsf(move_x) <= 0.05f && move_y >= 0.95f && move_y <= 1.0f)) {
            return smokeGridPortalFail("forward_axis_not_admitted");
        }
        if (!probe->saw_input) {
            probe->saw_input = 1;
            sysLogPrintf(LOG_NOTE,
                "GRID.PORTAL.INPUT: arena=%s mode=%s axis=(%.3f,%.3f) yaw=%.1f focus_lost=%d",
                probe->arena, probe->block ? "block" : "cross", move_x, move_y,
                player->vv_theta, inputCtxIsFocusLost());
        }
    }
    /* The 500-unit fixture opening spans Z=-250 to Z=250. Allow natural
     * walking drift while keeping the crossing inside it and the wall
     * approach at least 50 units beyond its edge. */
    if (!__builtin_isfinite(x) || !__builtin_isfinite(z)
            || fabsf(z - (probe->block ? 450.0f : 0.0f)) > 150.0f) {
        return smokeGridPortalFail("left_test_lane");
    }
    if (!probe->saw_approach && held && grounded && exclusive && room == 1 && x >= -250.0f && x <= -100.0f) {
        probe->saw_approach = 1;
        sysLogPrintf(LOG_NOTE, "GRID.PORTAL.APPROACH: arena=%s mode=%s room=1 grounded=1 x=%.1f z=%.1f",
            probe->arena, probe->block ? "block" : "cross", x, z);
    }
    if (probe->block) {
        if (x >= 0.0f || room == 2) return smokeGridPortalFail("crossed_solid_partition");
        const s32 at_wall = grounded && exclusive && room == 1 && x >= -100.0f && x <= -5.0f;
        const s32 stopped = at_wall && fabsf(x - probe->wall_x) <= 1.0f;
        const s32 stopped_frames = g_Vars.lvframe60 - probe->wall_frame;
        if (held && probe->saw_approach && stopped && probe->wall_frame && stopped_frames >= 30) {
            probe->complete = 1;
            sysLogPrintf(LOG_NOTE, "GRID.PORTAL.BLOCKED: arena=%s room=1 grounded=1 x=%.1f z=%.1f frames=%d",
                probe->arena, x, z, stopped_frames);
        } else if (!held) {
            return smokeGridPortalFail("input_released_before_wall_proof");
        } else if (!stopped) {
            probe->wall_frame = 0;
            probe->wall_x = x;
        } else if (!probe->wall_frame) {
            probe->wall_frame = g_Vars.lvframe60;
        }
    } else {
        if (!held) return smokeGridPortalFail("input_released_before_crossing");
        if (grounded && x >= -100.0f && x <= 100.0f) probe->saw_aperture = 1;
        if (grounded && exclusive && room == 2 && x >= 100.0f) {
            if (!probe->saw_approach || !probe->saw_aperture) return smokeGridPortalFail("missing_crossing_samples");
            probe->complete = 1;
            sysLogPrintf(LOG_NOTE, "GRID.PORTAL.CROSSED: arena=%s from=1 to=2 grounded=1 x=%.1f z=%.1f",
                probe->arena, x, z);
        }
    }
    return 1;
}

static Sint16 smokePadAxisNeutral(s32 axis)
{
    return axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT
        ? SDL_JOYSTICK_AXIS_MIN : 0;
}


/* ---------------------------------------------------------------- */
/* Minimal JSON tokenizer (mirrors modmgr.c style, kept local to    */
/* avoid coupling with mod loader internals).                       */
/* ---------------------------------------------------------------- */

typedef enum {
    JT_NONE = 0, JT_LBRACE, JT_RBRACE, JT_LBRACKET, JT_RBRACKET,
    JT_COLON, JT_COMMA, JT_STRING, JT_NUMBER, JT_TRUE, JT_FALSE, JT_NULL,
    JT_EOF, JT_ERROR
} JTokType;

typedef struct {
    const char *start;
    s32         len;
    JTokType    type;
} JTok;

typedef struct {
    const char *src;
    const char *pos;
    JTok        cur;
} JParse;

static void j_skip_ws(JParse *p)
{
    while (*p->pos) {
        char c = *p->pos;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
            continue;
        }
        /* skip // line comments for authoring convenience */
        if (c == '/' && p->pos[1] == '/') {
            while (*p->pos && *p->pos != '\n') p->pos++;
            continue;
        }
        break;
    }
}

static JTok j_next(JParse *p)
{
    JTok t = { NULL, 0, JT_NONE };
    j_skip_ws(p);
    if (!*p->pos) { t.type = JT_EOF; p->cur = t; return t; }
    t.start = p->pos;
    char c = *p->pos;
    switch (c) {
    case '{': t.type = JT_LBRACE;   t.len = 1; p->pos++; break;
    case '}': t.type = JT_RBRACE;   t.len = 1; p->pos++; break;
    case '[': t.type = JT_LBRACKET; t.len = 1; p->pos++; break;
    case ']': t.type = JT_RBRACKET; t.len = 1; p->pos++; break;
    case ':': t.type = JT_COLON;    t.len = 1; p->pos++; break;
    case ',': t.type = JT_COMMA;    t.len = 1; p->pos++; break;
    case '"': {
        p->pos++;
        t.start = p->pos;
        while (*p->pos && *p->pos != '"') {
            if (*p->pos == '\\' && p->pos[1]) p->pos++;
            p->pos++;
        }
        t.len = (s32)(p->pos - t.start);
        t.type = JT_STRING;
        if (*p->pos == '"') p->pos++;
        break;
    }
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            if (c == '0' && (p->pos[1] == 'x' || p->pos[1] == 'X')) {
                p->pos += 2;
                while (isxdigit((unsigned char)*p->pos)) p->pos++;
            } else {
                if (c == '-') p->pos++;
                while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
                if (*p->pos == '.') {
                    p->pos++;
                    while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
                }
				if (*p->pos == 'e' || *p->pos == 'E') {
					p->pos++;
					if (*p->pos == '+' || *p->pos == '-') p->pos++;
					while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
				}
            }
            t.len = (s32)(p->pos - t.start);
            t.type = JT_NUMBER;
        } else if (!strncmp(p->pos, "true", 4))  { t.type = JT_TRUE;  t.len = 4; p->pos += 4; }
        else if (!strncmp(p->pos, "false", 5)) { t.type = JT_FALSE; t.len = 5; p->pos += 5; }
        else if (!strncmp(p->pos, "null", 4))  { t.type = JT_NULL;  t.len = 4; p->pos += 4; }
        else { t.type = JT_ERROR; p->pos++; }
        break;
    }
    p->cur = t;
    return t;
}

static int j_tok_str_eq(const JTok *t, const char *s)
{
    if (t->type != JT_STRING) return 0;
    s32 sl = (s32)strlen(s);
    if (sl != t->len) return 0;
    return memcmp(t->start, s, sl) == 0;
}

static void j_tok_copy_str(const JTok *t, char *dst, s32 dstsz)
{
    if (t->type != JT_STRING || dstsz <= 0) { if (dstsz > 0) dst[0] = '\0'; return; }
    s32 n = t->len < (dstsz - 1) ? t->len : (dstsz - 1);
    memcpy(dst, t->start, n);
    dst[n] = '\0';
}

static s64 j_tok_int(const JTok *t)
{
    if (t->type != JT_NUMBER || !t->start) return 0;
    return (s64)strtoll(t->start, NULL, 0);
}

static int j_tok_is_integer(const JTok *t)
{
    s32 i = 0;

    if (!t || t->type != JT_NUMBER || !t->start || t->len <= 0) {
        return 0;
    }
    if (t->start[i] == '-') {
        i++;
    }
    if (i >= t->len) {
        return 0;
    }
    if (i + 2 <= t->len && t->start[i] == '0'
            && (t->start[i + 1] == 'x' || t->start[i + 1] == 'X')) {
        i += 2;
        if (i >= t->len) {
            return 0;
        }
        for (; i < t->len; i++) {
            if (!isxdigit((unsigned char)t->start[i])) {
                return 0;
            }
        }
        return 1;
    }
    for (; i < t->len; i++) {
        if (!isdigit((unsigned char)t->start[i])) {
            return 0;
        }
    }
    return 1;
}

/* Skip the value (possibly nested) at the current parse position. */
static void j_skip_value(JParse *p)
{
    JTok t = p->cur;
    if (t.type == JT_LBRACE) {
        s32 depth = 1;
        while (depth > 0) {
            t = j_next(p);
            if (t.type == JT_EOF || t.type == JT_ERROR) return;
            if (t.type == JT_LBRACE) depth++;
            if (t.type == JT_RBRACE) depth--;
        }
    } else if (t.type == JT_LBRACKET) {
        s32 depth = 1;
        while (depth > 0) {
            t = j_next(p);
            if (t.type == JT_EOF || t.type == JT_ERROR) return;
            if (t.type == JT_LBRACKET) depth++;
            if (t.type == JT_RBRACKET) depth--;
        }
    }
    /* scalars consumed already by the caller's j_next */
}

/* ---------------------------------------------------------------- */
/* Named keys -> SDL scancode                                       */
/* ---------------------------------------------------------------- */

typedef struct {
    const char *name;
    s32         scancode;
} SmokeKeyEntry;

static const SmokeKeyEntry s_KeyTable[] = {
    { "Return",      SDL_SCANCODE_RETURN     },
    { "Enter",       SDL_SCANCODE_RETURN     },
    { "Escape",      SDL_SCANCODE_ESCAPE     },
    { "Esc",         SDL_SCANCODE_ESCAPE     },
    { "Tab",         SDL_SCANCODE_TAB        },
    { "Space",       SDL_SCANCODE_SPACE      },
    { "Backspace",   SDL_SCANCODE_BACKSPACE  },
    { "Up",          SDL_SCANCODE_UP         },
    { "Down",        SDL_SCANCODE_DOWN       },
    { "Left",        SDL_SCANCODE_LEFT       },
    { "Right",       SDL_SCANCODE_RIGHT      },
    /* B-1170: menu_settings_keyboard pages with these since 6044d0fa. */
    { "PageUp",      SDL_SCANCODE_PAGEUP     },
    { "PageDown",    SDL_SCANCODE_PAGEDOWN   },
    { "LShift",      SDL_SCANCODE_LSHIFT     },
    { "RShift",      SDL_SCANCODE_RSHIFT     },
    { "LCtrl",       SDL_SCANCODE_LCTRL      },
    { "RCtrl",       SDL_SCANCODE_RCTRL      },
    { "LAlt",        SDL_SCANCODE_LALT       },
    { "RAlt",        SDL_SCANCODE_RALT       },
    { "F1",          SDL_SCANCODE_F1         },
    { "F2",          SDL_SCANCODE_F2         },
    { "F3",          SDL_SCANCODE_F3         },
    { "F4",          SDL_SCANCODE_F4         },
    { "F5",          SDL_SCANCODE_F5         },
    { "F6",          SDL_SCANCODE_F6         },
    { "F7",          SDL_SCANCODE_F7         },
    { "F8",          SDL_SCANCODE_F8         },
    { "F9",          SDL_SCANCODE_F9         },
    { "F10",         SDL_SCANCODE_F10        },
    { "F11",         SDL_SCANCODE_F11        },
    { "F12",         SDL_SCANCODE_F12        },
};

static s32 s_KeyTableLen = (s32)(sizeof(s_KeyTable) / sizeof(s_KeyTable[0]));

static s32 smokeKeyNameToScancode(const char *name)
{
    if (!name) return 0;

    /* single letter: A-Z (case insensitive) */
    if (name[0] && !name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') {
            return SDL_SCANCODE_A + (c - 'A');
        }
        if (c >= '1' && c <= '9') return SDL_SCANCODE_1 + (c - '1');
        if (c == '0') return SDL_SCANCODE_0;
    }

    for (s32 i = 0; i < s_KeyTableLen; i++) {
        if (!strcasecmp(s_KeyTable[i].name, name)) {
            return s_KeyTable[i].scancode;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Channel mask parser                                              */
/* ---------------------------------------------------------------- */

static u32 smokeParseChannelMask(const JTok *t)
{
	if (t->type == JT_STRING) {
		char tmp[128];
		j_tok_copy_str(t, tmp, sizeof(tmp));
		if (!strcasecmp(tmp, "all"))  return 0xFFFFu;
		if (!strcasecmp(tmp, "none")) return 0x0000u;

		char *end = NULL;
		u32 numeric = (u32)strtoul(tmp, &end, 0);
		while (end && *end && isspace((unsigned char)*end)) {
			end++;
		}
		if (end && end != tmp && *end == '\0') {
			return numeric;
		}

		u32 mask = 0;
		for (char *part = strtok(tmp, ",|+; ");
		     part;
		     part = strtok(NULL, ",|+; ")) {
			u32 bit = 0;
			for (s32 i = 0; i < LOG_CH_COUNT; i++) {
				if (!strcasecmp(part, sysLogChannelNames[i])) {
					bit = sysLogChannelBits[i];
					break;
				}
			}
			if (!bit && (!strcasecmp(part, "mod") || !strcasecmp(part, "mods"))) {
				bit = LOG_CH_MOD;
			} else if (!bit && (!strcasecmp(part, "test") || !strcasecmp(part, "testscen"))) {
				bit = LOG_CH_TESTSCEN;
			}
			if (!bit) {
				sysLogPrintf(LOG_WARNING, "SMOKE: unknown log_channel_mask token '%s'", part);
				continue;
			}
			mask |= bit;
		}
		return mask;
	}
	if (t->type == JT_NUMBER) {
		return (u32)j_tok_int(t);
	}
    return 0xFFFFu;
}

/* ---------------------------------------------------------------- */
/* JSON loader                                                      */
/* ---------------------------------------------------------------- */

static char *smokeReadFile(const char *path, s32 *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        sysLogPrintf(LOG_ERROR, "SMOKE: cannot open test file '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) {
        sysLogPrintf(LOG_ERROR, "SMOKE: test file '%s' size %ld out of range", path, sz);
        fclose(f);
        return NULL;
    }
    char *buf = (char *)sysMemAlloc((u32)sz + 1);
    if (!buf) {
        sysLogPrintf(LOG_ERROR, "SMOKE: cannot alloc %ld bytes for test file", sz);
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    if (out_size) *out_size = (s32)n;
    return buf;
}

/* Resolve a mouse button name to an SDL_BUTTON_* code. */
static s32 smokeMouseButtonNameToCode(const char *name)
{
    if (!name || !name[0]) return SDL_BUTTON_LEFT;
    if (!strcasecmp(name, "left")   || !strcasecmp(name, "lmb")) return SDL_BUTTON_LEFT;
    if (!strcasecmp(name, "right")  || !strcasecmp(name, "rmb")) return SDL_BUTTON_RIGHT;
    if (!strcasecmp(name, "middle") || !strcasecmp(name, "mmb")) return SDL_BUTTON_MIDDLE;
    if (!strcasecmp(name, "x1") || !strcasecmp(name, "side1"))   return SDL_BUTTON_X1;
    if (!strcasecmp(name, "x2") || !strcasecmp(name, "side2"))   return SDL_BUTTON_X2;
    return 0;
}

static s32 smokeParseEvent(JParse *p, SmokeEvent *ev)
{
    JTok t = p->cur;
    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);

    ev->at_ms = 0;
    ev->type = SMOKE_EVENT_NONE;
    ev->scancode = 0;
    ev->action_id = -1;
    ev->mouse_x = 0;
    ev->mouse_y = 0;
    ev->mouse_button = 0;
    ev->mouse_wheel_x = 0;
    ev->mouse_wheel_y = 0;
    ev->controller_control = -1;
    ev->controller_value = 0;
    ev->controller_slot = 0;
    ev->client_id = -1;
    ev->hand = -1;
    ev->release_at_ms = -1;
    ev->released = 0;
    ev->readiness_condition = SMOKE_READINESS_INVALID;
    ev->wait_timeout_ms = 0;
    ev->stable_ms = 0;
    ev->assist_action_id = -1;
    ev->assist_condition = SMOKE_READINESS_INVALID;
    ev->assist_hold_ms = 0;

    char type_str[SMOKE_FIXTURE_EVENT_TYPE_CAPACITY] = {0};
    char key_str[32]    = {0};
    char action_str[16] = {0};
    char name_str[64]   = {0};
    char button_str[16] = {0};
    char axis_str[16] = {0};
    char hand_str[16]   = {0};
    char condition_str[32] = {0};
    char assist_action_str[64] = {0};
    char assist_condition_str[32] = {0};
    s32  scancode = 0;
    s64  controller_value = 0;
    s64  at_ms_value = 0;
    s64  wait_timeout_ms = 0;
    s64  stable_ms = 0;
    s64  assist_hold_ms = 0;
    s32  has_action_mode = 0;
    s32  has_wait_timeout = 0;
    s32  has_stable_ms = 0;
    s32  has_assist_action = 0;
    s32  has_assist_condition = 0;
    s32  has_assist_hold_ms = 0;
    s32  has_x = 0;
    s32  has_y = 0;
    s32  has_wheel_x = 0;
    s32  has_wheel_y = 0;
    s32  has_client_id = 0;
    s32  has_hand = 0;
	smoke_fixture_field_mask_t field_mask = 0;
	smoke_fixture_field_mask_t unsupported_fields = 0;

    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
		smoke_fixture_field_mask_t field_bit =
			smokeFixtureEventFieldFromName(field);
		if (!field_bit) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: event has unknown field '%s'", field);
			return 0;
		}
		if (field_mask & field_bit) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: event has duplicate field '%s'", field);
			return 0;
		}
		field_mask |= field_bit;
        t = j_next(p); /* colon */
        if (t.type == JT_COLON) t = j_next(p);

        if (!strcmp(field, "at_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: event at_ms must be an integer");
                return 0;
            }
            at_ms_value = j_tok_int(&t);
		} else if (!strcmp(field, "type")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event type must be a string");
				return 0;
			}
			if (!smokeFixtureEventTypeLengthValid((size_t)t.len)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event type exceeds %u characters",
					(unsigned)SMOKE_FIXTURE_EVENT_TYPE_CAPACITY - 1u);
				return 0;
			}
            j_tok_copy_str(&t, type_str, sizeof(type_str));
        } else if (!strcmp(field, "key")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event key must be a string");
				return 0;
			}
            j_tok_copy_str(&t, key_str, sizeof(key_str));
        } else if (!strcmp(field, "scancode")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event scancode must be an integer");
				return 0;
			}
            scancode = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "action")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event action must be a string");
				return 0;
			}
            if (t.len >= (s32)sizeof(action_str)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: event action is too long");
                return 0;
            }
            j_tok_copy_str(&t, action_str, sizeof(action_str));
            has_action_mode = 1;
        } else if (!strcmp(field, "name")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event name must be a string");
				return 0;
			}
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else if (!strcmp(field, "condition")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until condition must be a string");
				return 0;
            }
            j_tok_copy_str(&t, condition_str, sizeof(condition_str));
        } else if (!strcmp(field, "timeout_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until timeout_ms must be an integer");
                return 0;
            }
            wait_timeout_ms = j_tok_int(&t);
            has_wait_timeout = 1;
        } else if (!strcmp(field, "stable_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until stable_ms must be an integer");
                return 0;
            }
            stable_ms = j_tok_int(&t);
            has_stable_ms = 1;
        } else if (!strcmp(field, "assist_action")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until assist_action must be a string");
				return 0;
			}
            j_tok_copy_str(&t, assist_action_str, sizeof(assist_action_str));
            has_assist_action = 1;
        } else if (!strcmp(field, "assist_condition")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until assist_condition must be a string");
				return 0;
			}
            j_tok_copy_str(&t, assist_condition_str,
                sizeof(assist_condition_str));
            has_assist_condition = 1;
        } else if (!strcmp(field, "assist_hold_ms")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: wait_until assist_hold_ms must be an integer");
                return 0;
            }
            assist_hold_ms = j_tok_int(&t);
            has_assist_hold_ms = 1;
        } else if (!strcmp(field, "path")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event path must be a string");
				return 0;
			}
            j_tok_copy_str(&t, ev->path, sizeof(ev->path));
        } else if (!strcmp(field, "x")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event x must be an integer");
				return 0;
			}
            ev->mouse_x = (s32)j_tok_int(&t);
            has_x = 1;
        } else if (!strcmp(field, "y")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event y must be an integer");
				return 0;
			}
            ev->mouse_y = (s32)j_tok_int(&t);
            has_y = 1;
        } else if (!strcmp(field, "button")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event button must be a string");
				return 0;
			}
            if (t.len >= (s32)sizeof(button_str)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: event button is too long");
                return 0;
            }
            j_tok_copy_str(&t, button_str, sizeof(button_str));
        } else if (!strcmp(field, "axis")) {
            if (t.type != JT_STRING || t.len >= (s32)sizeof(axis_str)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: event axis must be a complete controller axis string");
                return 0;
            }
            j_tok_copy_str(&t, axis_str, sizeof(axis_str));
        } else if (!strcmp(field, "value")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller axis value must be an integer");
                return 0;
            }
            controller_value = j_tok_int(&t);
            if (!smokeFixtureControllerAxisValueValid(controller_value)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller axis value must be in [-32768,32767]");
                return 0;
            }
        } else if (!strcmp(field, "wheel_x")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event wheel_x must be an integer");
				return 0;
			}
            ev->mouse_wheel_x = (s32)j_tok_int(&t);
            has_wheel_x = 1;
        } else if (!strcmp(field, "wheel_y")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event wheel_y must be an integer");
				return 0;
			}
            ev->mouse_wheel_y = (s32)j_tok_int(&t);
            has_wheel_y = 1;
        } else if (!strcmp(field, "slot")) {
            if (!j_tok_is_integer(&t)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller slot must be an integer");
                return 0;
            }
            const s64 slot = j_tok_int(&t);
            if (slot < 0 || slot >= SMOKE_PAD_COUNT) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller slot must be in [0,%d]", SMOKE_PAD_COUNT - 1);
                return 0;
            }
            ev->controller_slot = (s32)slot;
        } else if (!strcmp(field, "client_id")) {
			if (!j_tok_is_integer(&t)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event client_id must be an integer");
				return 0;
			}
			ev->client_id = (s32)j_tok_int(&t);
			has_client_id = 1;
		} else if (!strcmp(field, "hand")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event hand must be 'right' or 'left'");
				return 0;
			}
			j_tok_copy_str(&t, hand_str, sizeof(hand_str));
			if (!strcasecmp(hand_str, "right")) {
				ev->hand = HAND_RIGHT;
			} else if (!strcasecmp(hand_str, "left")) {
				ev->hand = HAND_LEFT;
			} else {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: event hand must be 'right' or 'left' (value='%s')",
					hand_str);
				return 0;
			}
			has_hand = 1;
        } else if (!strcmp(field, "comment")) {
			if (t.type != JT_STRING) {
				sysLogPrintf(LOG_ERROR, "SMOKE: event comment must be a string");
				return 0;
			}
            j_skip_value(p);
        }

        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
    }

	if (t.type != JT_RBRACE) {
		sysLogPrintf(LOG_ERROR, "SMOKE: event object is truncated");
		return 0;
	}
	if (!smokeFixtureEventTypeKnown(type_str)) {
		sysLogPrintf(LOG_ERROR, "SMOKE: unknown event type '%s'", type_str);
		return 0;
	}
	if (!smokeFixtureEventFieldsValid(type_str, field_mask,
			&unsupported_fields)) {
		smoke_fixture_field_mask_t first = unsupported_fields
			& (0u - unsupported_fields);
		sysLogPrintf(LOG_ERROR,
			"SMOKE: field '%s' is not supported for event type '%s'",
			smokeFixtureEventFieldName(first), type_str[0] ? type_str : "wait");
		return 0;
	}

    if (at_ms_value < 0 || at_ms_value > SMOKE_MAX_EVENT_AT_MS) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: event at_ms must be in [0,%d] (value=%lld)",
            SMOKE_MAX_EVENT_AT_MS, (long long)at_ms_value);
        return 0;
    }
    ev->at_ms = (s32)at_ms_value;

    if (!strcmp(type_str, "wait") || !type_str[0]) {
        ev->type = SMOKE_EVENT_WAIT;
        return 1;
    }
    if (!strcmp(type_str, "wait_until")) {
        ev->readiness_condition = smokeReadinessConditionFromName(condition_str);
        if (ev->readiness_condition == SMOKE_READINESS_INVALID) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until event has unknown condition '%s' (at_ms=%d)",
                condition_str, ev->at_ms);
            return 0;
        }
        if (!has_wait_timeout || wait_timeout_ms <= 0
                || wait_timeout_ms > SMOKE_MAX_WAIT_TIMEOUT_MS) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until condition='%s' requires timeout_ms in [1,%d] (at_ms=%d)",
                condition_str, SMOKE_MAX_WAIT_TIMEOUT_MS, ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_WAIT_UNTIL;
        ev->wait_timeout_ms = (u32)wait_timeout_ms;
        if (has_stable_ms
                && (stable_ms < 0 || stable_ms >= wait_timeout_ms)) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: wait_until stable_ms must be in [0,timeout_ms) (stable_ms=%lld timeout_ms=%lld at_ms=%d)",
                (long long)stable_ms, (long long)wait_timeout_ms, ev->at_ms);
            return 0;
        }
        ev->stable_ms = (u32)stable_ms;

        if (has_assist_action || has_assist_condition
                || has_assist_hold_ms) {
            if (!has_assist_action || !has_assist_condition
                    || !has_assist_hold_ms || !has_stable_ms
                    || stable_ms <= 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until requires assist_action, assist_condition, assist_hold_ms, and stable_ms > 0 (at_ms=%d)",
                    ev->at_ms);
                return 0;
            }
            ev->assist_action_id = actionmapResolveByName(assist_action_str);
            if (ev->assist_action_id < 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until has unknown assist_action '%s' (at_ms=%d)",
                    assist_action_str, ev->at_ms);
                return 0;
            }
            ev->assist_condition = smokeReadinessConditionFromName(
                assist_condition_str);
            if (ev->assist_condition == SMOKE_READINESS_INVALID) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until has unknown assist_condition '%s' (at_ms=%d)",
                    assist_condition_str, ev->at_ms);
                return 0;
            }
            if (assist_hold_ms <= 0 || assist_hold_ms >= wait_timeout_ms) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: assisted wait_until assist_hold_ms must be in [1,timeout_ms) (assist_hold_ms=%lld timeout_ms=%lld at_ms=%d)",
                    (long long)assist_hold_ms, (long long)wait_timeout_ms,
                    ev->at_ms);
                return 0;
            }
            ev->assist_hold_ms = (u32)assist_hold_ms;
        }
        return 1;
    }
    if (!strcmp(type_str, "exit")) {
        ev->type = SMOKE_EVENT_EXIT;
        return 1;
    }
    if (!strcmp(type_str, "unclean_exit")) {
        ev->type = SMOKE_EVENT_UNCLEAN_EXIT;
        return 1;
    }
	if (!strcmp(type_str, "network_retire_held_weapon")) {
		if (!has_client_id || ev->client_id < 0
				|| ev->client_id >= NET_MAX_CLIENTS || !has_hand
				|| (ev->hand != HAND_RIGHT && ev->hand != HAND_LEFT)) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: network_retire_held_weapon requires client_id in [0,%d] and hand 'right' or 'left' (at_ms=%d)",
				NET_MAX_CLIENTS - 1, ev->at_ms);
			return 0;
		}
		ev->type = SMOKE_EVENT_NETWORK_RETIRE_HELD_WEAPON;
		return 1;
	}
	if (!strcmp(type_str, "network_assert_retired_prop_absent")) {
		ev->type = SMOKE_EVENT_NETWORK_ASSERT_RETIRED_PROP_ABSENT;
		return 1;
	}
	if (!strcmp(type_str, "kill_player")) {
		ev->type = SMOKE_EVENT_KILL_PLAYER;
		return 1;
	}
	if (!strcmp(type_str, "kill_token_holder")) {
		ev->type = SMOKE_EVENT_KILL_TOKEN_HOLDER;
		return 1;
	}
	if (!strcmp(type_str, "kill_simulant")) {
		ev->type = SMOKE_EVENT_KILL_SIMULANT;
		return 1;
	}
	if (!strcmp(type_str, "movement_ledge") || !strcmp(type_str, "movement_tabletop")) {
		if (!strcmp(name_str, "place_player")) {
			ev->ledge_command = SMOKE_LEDGE_PLACE_PLAYER;
		} else if (!strcmp(name_str, "place_simulant")) {
			ev->ledge_command = SMOKE_LEDGE_PLACE_BOT;
		} else if (!strcmp(name_str, "walk_off")) {
			ev->ledge_command = SMOKE_LEDGE_WALK_OFF;
		} else if (!strcmp(name_str, "verify")) {
			ev->ledge_command = SMOKE_LEDGE_VERIFY;
		} else {
			sysLogPrintf(LOG_ERROR, "SMOKE: unknown %s command '%s'", type_str, name_str);
			return 0;
		}
		ev->type = SMOKE_EVENT_MOVEMENT_LEDGE;
		ev->ledge_fixture = !strcmp(type_str, "movement_tabletop")
			? SMOKE_SUPPORT_VILLA_TABLETOP : SMOKE_SUPPORT_COMPLEX_LEDGE;
		return 1;
	}
	if (!strcmp(type_str, "use_door")) {
		ev->type = SMOKE_EVENT_USE_DOOR;
		return 1;
	}
	if (!strcmp(type_str, "use_lift_door")) {
		ev->type = SMOKE_EVENT_USE_LIFT_DOOR;
		return 1;
	}
	if (!strcmp(type_str, "damage_glass")) {
		ev->type = SMOKE_EVENT_DAMAGE_GLASS;
		return 1;
	}
	if (!strcmp(type_str, "give_weapon")) {
		if (!ev->path[0]) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: give_weapon event missing 'path' (at_ms=%d)", ev->at_ms);
			return 0;
		}
		ev->type = SMOKE_EVENT_GIVE_WEAPON;
		return 1;
	}
	if (!strcmp(type_str, "touch_pickup")) {
		ev->type = SMOKE_EVENT_TOUCH_PICKUP;
		return 1;
	}
    if (!strcmp(type_str, "network_timeout_client")) {
		if (!has_client_id || ev->client_id < 0
				|| ev->client_id >= NET_MAX_CLIENTS) {
			sysLogPrintf(LOG_ERROR,
				"SMOKE: network_timeout_client requires client_id in [0,%d] (at_ms=%d)",
				NET_MAX_CLIENTS - 1, ev->at_ms);
			return 0;
		}
		ev->type = SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT;
		return 1;
	}
    if (!strcmp(type_str, "network_reconnect")) {
		ev->type = SMOKE_EVENT_NETWORK_RECONNECT;
		return 1;
	}
    if (!strcmp(type_str, "screenshot")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR, "SMOKE: screenshot event missing 'path' (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_SCREENSHOT;
        return 1;
    }
    if (!strcmp(type_str, "receive_pdca_list")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: receive_pdca_list event missing 'path' (at_ms=%d)",
                ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_RECEIVE_PDCA_LIST;
        return 1;
    }
    if (!strcmp(type_str, "agent_activate")
            || !strcmp(type_str, "agent_delete")) {
        if (!name_str[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: %s event missing 'name' (at_ms=%d)",
                type_str, ev->at_ms);
            return 0;
        }
        snprintf(ev->path, sizeof(ev->path), "%s", name_str);
        ev->type = !strcmp(type_str, "agent_activate")
            ? SMOKE_EVENT_AGENT_ACTIVATE : SMOKE_EVENT_AGENT_DELETE;
        return 1;
    }
    if (!strcmp(type_str, "modmgr_apply_retry_probe")) {
        ev->type = SMOKE_EVENT_MODMGR_APPLY_RETRY_PROBE;
        return 1;
    }
    if (!strcmp(type_str, "file_transfer_socket_probe")) {
        ev->type = SMOKE_EVENT_FILE_TRANSFER_SOCKET_PROBE;
        return 1;
    }
    if (!strcmp(type_str, "catalog_recovery_probe")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: catalog_recovery_probe event missing 'path' (at_ms=%d)",
                ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_CATALOG_RECOVERY_PROBE;
        return 1;
    }
    if (!strcmp(type_str, "catalog_weapon_acquire")
            || !strcmp(type_str, "catalog_weapon_release")) {
        if (!ev->path[0]) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: %s event missing 'path' (at_ms=%d)",
                type_str, ev->at_ms);
            return 0;
        }
        if (!strcmp(type_str, "catalog_weapon_acquire")) {
            ev->type = SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE;
        } else {
            ev->type = SMOKE_EVENT_CATALOG_WEAPON_RELEASE;
        }
        return 1;
    }
    if (!strncmp(type_str, "controller_", 11)) {
        if (!(field_mask & SMOKE_FIXTURE_FIELD_AT_MS)) {
            sysLogPrintf(LOG_ERROR, "SMOKE: controller event requires explicit at_ms");
            return 0;
        }
        if (!strcmp(type_str, "controller_attach")) ev->type = SMOKE_EVENT_CONTROLLER_ATTACH;
        else if (!strcmp(type_str, "controller_detach")) ev->type = SMOKE_EVENT_CONTROLLER_DETACH;
        else if (!strcmp(type_str, "controller_button")) {
            const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(button_str);
            if (button == SDL_CONTROLLER_BUTTON_INVALID || !has_action_mode
                    || (strcmp(action_str, "press") && strcmp(action_str, "release"))) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller_button requires a valid button and press/release action");
                return 0;
            }
            ev->type = SMOKE_EVENT_CONTROLLER_BUTTON;
            ev->controller_control = button;
            ev->controller_value = !strcmp(action_str, "press");
        } else {
            const SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(axis_str);
            if (axis == SDL_CONTROLLER_AXIS_INVALID || !(field_mask & SMOKE_FIXTURE_FIELD_VALUE)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller_axis requires a valid axis and value");
                return 0;
            }
            ev->type = SMOKE_EVENT_CONTROLLER_AXIS;
            ev->controller_control = axis;
            ev->controller_value = (s32)controller_value;
        }
        return 1;
    }
    if (!strcmp(type_str, "key")) {
        if (key_str[0]) scancode = smokeKeyNameToScancode(key_str);
        if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES) {
            sysLogPrintf(LOG_ERROR, "SMOKE: key event has unresolved key (key='%s', scancode=%d)",
                key_str, scancode);
            return 0;
        }
        ev->scancode = scancode;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_KEY_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_KEY_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")) {
            ev->type = SMOKE_EVENT_KEY_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: key event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "action")) {
        if (!name_str[0]) {
            sysLogPrintf(LOG_ERROR, "SMOKE: action event missing 'name' (at_ms=%d)", ev->at_ms);
            return 0;
        }
        s32 aid = actionmapResolveByName(name_str);
        if (aid < 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: action event has unknown name '%s' (at_ms=%d)",
                name_str, ev->at_ms);
            return 0;
        }
        ev->action_id = aid;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_ACTION_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_ACTION_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")) {
            ev->type = SMOKE_EVENT_ACTION_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: action event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "mouse")) {
        if (!has_x || !has_y) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse event missing x/y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        s32 btn = smokeMouseButtonNameToCode(button_str);
        if (btn <= 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse event has unknown button '%s' (at_ms=%d)",
                button_str, ev->at_ms);
            return 0;
        }
        ev->mouse_button = btn;
        if (!strcmp(action_str, "press")) {
            ev->type = SMOKE_EVENT_MOUSE_PRESS;
        } else if (!strcmp(action_str, "release")) {
            ev->type = SMOKE_EVENT_MOUSE_RELEASE;
        } else if (!has_action_mode || !strcmp(action_str, "tap")
                || !strcmp(action_str, "click")) {
            ev->type = SMOKE_EVENT_MOUSE_TAP;
            ev->release_at_ms = ev->at_ms + SMOKE_TAP_RELEASE_MS;
        } else {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: mouse event has unknown action '%s' (at_ms=%d)",
                action_str, ev->at_ms);
            return 0;
        }
        return 1;
    }
    if (!strcmp(type_str, "mouse_move")) {
        if (!has_x || !has_y) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse_move event missing x/y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_MOUSE_MOVE;
        return 1;
    }
    if (!strcmp(type_str, "mouse_wheel")) {
        if (!has_wheel_x && !has_wheel_y) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE: mouse_wheel event missing wheel_x/wheel_y (at_ms=%d)", ev->at_ms);
            return 0;
        }
        if (ev->mouse_wheel_x == 0 && ev->mouse_wheel_y == 0) {
            sysLogPrintf(LOG_ERROR, "SMOKE: mouse_wheel event has zero delta (at_ms=%d)", ev->at_ms);
            return 0;
        }
        ev->type = SMOKE_EVENT_MOUSE_WHEEL;
        return 1;
    }

    sysLogPrintf(LOG_ERROR, "SMOKE: unknown event type '%s' (at_ms=%d)", type_str, ev->at_ms);
    return 0;
}

/* Parse one entry of the scenario's top-level `screenshots` array ({at_ms,name})
 * into a SMOKE_EVENT_SCREENSHOT event whose path mirrors the runner's
 * `<dir>/<ordinal:03>-<sanitized-name>.bmp`. This routes the standard scenario
 * screenshots through the in-game glReadPixels capture (focus/size-independent)
 * instead of the runner's PrintWindow/BitBlt path. Requires --smoke-screenshot-dir. */
static s32 smokeBuildScreenshotEvent(JParse *p, SmokeEvent *ev, s32 ordinal)
{
    JTok t = p->cur;
    char name_str[SMOKE_MAX_NAME] = {0};
    s64 at_ms = 0;

    if (t.type != JT_LBRACE) return 0;
    t = j_next(p);
    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(p); continue; }
        char field[32]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(p);
        if (t.type == JT_COLON) t = j_next(p);
        if (!strcmp(field, "at_ms")) {
            at_ms = j_tok_int(&t);
        } else if (!strcmp(field, "name")) {
            j_tok_copy_str(&t, name_str, sizeof(name_str));
        } else {
            j_skip_value(p);
        }
        t = j_next(p);
        if (t.type == JT_COMMA) t = j_next(p);
    }

    if (at_ms < 0 || at_ms > SMOKE_MAX_EVENT_AT_MS) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: screenshot at_ms must be in [0,%d] (value=%lld)",
            SMOKE_MAX_EVENT_AT_MS, (long long)at_ms);
        return -1;
    }
    if (!s_State.screenshot_dir[0]) return 0; /* no output dir -> nothing to do */
    if (!name_str[0]) {
        snprintf(name_str, sizeof(name_str), "shot-%d", (int)ordinal);
    }
    char safe[SMOKE_MAX_NAME];
    s32 j = 0;
    for (s32 i = 0; name_str[i] && j < (s32)sizeof(safe) - 1; i++) {
        char c = name_str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-') {
            safe[j++] = c;
        } else {
            safe[j++] = '_';
        }
    }
    safe[j] = '\0';

    ev->at_ms = (s32)at_ms;
    ev->type = SMOKE_EVENT_SCREENSHOT;
    snprintf(ev->path, sizeof(ev->path), "%s/%03d-%s.bmp",
        s_State.screenshot_dir, (int)ordinal, safe);
    return 1;
}

static void smokeTrackExplicitHold(u8 *held, s32 down, s32 *held_count)
{
	if (down && !*held) {
		*held = 1;
		(*held_count)++;
	} else if (!down && *held) {
		*held = 0;
		(*held_count)--;
	}
}

/* A wait_until pauses virtual script time while real time continues. Explicit
 * press/release pairs are authored in virtual time, so crossing a wait with a
 * press held would silently stretch it to the full real-time wait. Reject the
 * fixture before launch; bounded wait assists are the only legal ownership
 * mechanism inside a paused interval. The stable event sort makes same-time
 * ordering deterministic and preserves JSON order. */
static s32 smokeValidateWaitHoldSchedule(void)
{
	u8 key_held[SDL_NUM_SCANCODES];
	u8 action_held[ACTION_COUNT];
	u8 mouse_held[SDL_BUTTON_X2 + 1];
	s32 held_count = 0;

	memset(key_held, 0, sizeof(key_held));
	memset(action_held, 0, sizeof(action_held));
	memset(mouse_held, 0, sizeof(mouse_held));

	for (s32 i = 0; i < s_State.event_count; i++) {
		const SmokeEvent *ev = &s_State.events[i];
		switch (ev->type) {
		case SMOKE_EVENT_KEY_PRESS:
			smokeTrackExplicitHold(&key_held[ev->scancode], 1, &held_count);
			break;
		case SMOKE_EVENT_KEY_RELEASE:
			smokeTrackExplicitHold(&key_held[ev->scancode], 0, &held_count);
			break;
		case SMOKE_EVENT_ACTION_PRESS:
			smokeTrackExplicitHold(&action_held[ev->action_id], 1, &held_count);
			break;
		case SMOKE_EVENT_ACTION_RELEASE:
			smokeTrackExplicitHold(&action_held[ev->action_id], 0, &held_count);
			break;
		case SMOKE_EVENT_MOUSE_PRESS:
			smokeTrackExplicitHold(&mouse_held[ev->mouse_button], 1, &held_count);
			break;
		case SMOKE_EVENT_MOUSE_RELEASE:
			smokeTrackExplicitHold(&mouse_held[ev->mouse_button], 0, &held_count);
			break;
        case SMOKE_EVENT_CONTROLLER_ATTACH:
        case SMOKE_EVENT_CONTROLLER_DETACH:
            if (held_count > 0) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller transition crosses explicit input hold(s)");
                return 0;
            }
            break;
		case SMOKE_EVENT_WAIT_UNTIL:
			if (held_count > 0) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: wait_until at_ms=%d crosses %d explicit input hold(s); use a bounded wait assist",
					ev->at_ms, held_count);
				return 0;
			}
			break;
		default:
			break;
		}
	}
	return 1;
}

/* Controller groups must survive one normal frame. Never allow a wait, exit,
 * screenshot, or lifecycle transition to split or observe a partial group. */
static s32 smokePadIsWrite(SmokeEventType type)
{
    return type == SMOKE_EVENT_CONTROLLER_BUTTON || type == SMOKE_EVENT_CONTROLLER_AXIS;
}

static s32 smokePadGroupInput(SmokeEventType type)
{
    return smokePadIsWrite(type) || type == SMOKE_EVENT_KEY_PRESS
        || type == SMOKE_EVENT_KEY_RELEASE || type == SMOKE_EVENT_KEY_TAP
        || type == SMOKE_EVENT_MOUSE_PRESS || type == SMOKE_EVENT_MOUSE_RELEASE
        || type == SMOKE_EVENT_MOUSE_TAP || type == SMOKE_EVENT_MOUSE_MOVE
        || type == SMOKE_EVENT_MOUSE_WHEEL;
}

static s32 smokeValidateControllerSchedule(void)
{
    u8 buttons[SMOKE_PAD_COUNT][SDL_CONTROLLER_BUTTON_MAX] = {{0}};
    u8 axes[SMOKE_PAD_COUNT][SDL_CONTROLLER_AXIS_MAX] = {{0}};
    s32 slot_held[SMOKE_PAD_COUNT] = {0};
    s32 attached = 0;
    s32 held_count = 0;
    for (s32 i = 0; i < s_State.event_count; ++i) {
        const SmokeEvent *ev = &s_State.events[i];
        const s32 slot = ev->controller_slot;
        const s32 slot_bit = 1 << slot;
        if (ev->type == SMOKE_EVENT_CONTROLLER_ATTACH
                || ev->type == SMOKE_EVENT_CONTROLLER_DETACH) {
            if ((i > 0 && s_State.events[i - 1].at_ms == ev->at_ms)
                    || (i + 1 < s_State.event_count && s_State.events[i + 1].at_ms == ev->at_ms)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller transition requires its own timestamp");
                return 0;
            }
            const s32 attaching = ev->type == SMOKE_EVENT_CONTROLLER_ATTACH;
            if (attaching == ((attached & slot_bit) != 0)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller attach/detach ownership mismatch");
                return 0;
            }
            if (attaching) {
                /* The production SDL watch assigns the lowest free player slot. */
                const s32 lower_slots = slot_bit - 1;
                if ((attached & lower_slots) != lower_slots) {
                    sysLogPrintf(LOG_ERROR, "SMOKE: attach lower controller slots first");
                    return 0;
                }
                attached |= slot_bit;
            } else {
                attached &= ~slot_bit;
                memset(buttons[slot], 0, sizeof(buttons[slot]));
                memset(axes[slot], 0, sizeof(axes[slot]));
                held_count -= slot_held[slot];
                slot_held[slot] = 0; /* detach neutralizes only this device */
            }
        } else if (smokePadIsWrite(ev->type)) {
            if (!(attached & slot_bit)) {
                sysLogPrintf(LOG_ERROR, "SMOKE: controller state requires an attached device");
                return 0;
            }
            for (s32 j = 0; j < s_State.event_count; ++j) {
                const SmokeEvent *other = &s_State.events[j];
                if (other->at_ms != ev->at_ms || j == i) continue;
                if (!smokePadGroupInput(other->type)
                        || (other->type == ev->type && other->controller_slot == slot
                            && other->controller_control == ev->controller_control)) {
                    sysLogPrintf(LOG_ERROR, "SMOKE: controller timestamp has conflicting state or non-input events");
                    return 0;
                }
            }
            const s32 previous_held = slot_held[slot];
            if (ev->type == SMOKE_EVENT_CONTROLLER_BUTTON) {
                smokeTrackExplicitHold(&buttons[slot][ev->controller_control],
                    ev->controller_value, &slot_held[slot]);
            } else {
                smokeTrackExplicitHold(&axes[slot][ev->controller_control],
                    ev->controller_value != smokePadAxisNeutral(ev->controller_control), &slot_held[slot]);
            }
            held_count += slot_held[slot] - previous_held;
        } else if (ev->type == SMOKE_EVENT_WAIT_UNTIL && held_count) {
            sysLogPrintf(LOG_ERROR, "SMOKE: wait_until crosses controller input hold(s)");
            return 0;
        } else if ((ev->type == SMOKE_EVENT_EXIT || ev->type == SMOKE_EVENT_UNCLEAN_EXIT) && attached) {
            sysLogPrintf(LOG_ERROR, "SMOKE: exit requires controller_detach first");
            return 0;
        }
    }
    if (attached) {
        sysLogPrintf(LOG_ERROR, "SMOKE: controller fixture must finish detached");
        return 0;
    }
    return 1;
}


static s32 smokeParseJson(const char *src)
{
    JParse p;
    p.src = src;
    p.pos = src;
    p.cur.type = JT_NONE;

	if (!smokeFixtureJsonValid(src)) {
		sysLogPrintf(LOG_ERROR,
			"SMOKE: test JSON is malformed, truncated, or has trailing data");
		return 0;
	}

    JTok t = j_next(&p);
    if (t.type != JT_LBRACE) {
        sysLogPrintf(LOG_ERROR, "SMOKE: test JSON must be an object");
        return 0;
    }
    t = j_next(&p);

    /* Sensible defaults so the harness still works for a near-empty test */
    s_State.channel_mask = 0xFFFFu;
    s_State.verbose      = 0;
    s_State.jump_logging = 0;
    s_State.timeout_ms   = SMOKE_DEFAULT_TIMEOUT_MS;
    s_State.event_count  = 0;
    strcpy(s_State.scenario_name, "(unnamed)");

    while (t.type != JT_RBRACE && t.type != JT_EOF) {
        if (t.type != JT_STRING) { t = j_next(&p); continue; }
        char field[64]; j_tok_copy_str(&t, field, sizeof(field));
        t = j_next(&p);
        if (t.type == JT_COLON) t = j_next(&p);

        if (!strcmp(field, "scenario_name")) {
            j_tok_copy_str(&t, s_State.scenario_name, sizeof(s_State.scenario_name));
        } else if (!strcmp(field, "log_channel_mask")) {
            s_State.channel_mask = smokeParseChannelMask(&t);
        } else if (!strcmp(field, "verbose")) {
            s_State.verbose = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "jump_logging")) {
            s_State.jump_logging = (s32)j_tok_int(&t);
        } else if (!strcmp(field, "timeout_seconds")) {
            s64 secs = j_tok_int(&t);
            if (secs <= 0) {
                sysLogPrintf(LOG_ERROR,
                    "SMOKE: timeout_seconds must be positive (value=%lld)",
                    (long long)secs);
                return 0;
            }
            if (secs > 3600) secs = 3600;
            s_State.timeout_ms = (s32)(secs * 1000);
        } else if (!strcmp(field, "input_sequence")) {
            if (t.type == JT_LBRACKET) {
                /* The events run in at_ms order (stable sort below), so an
                 * entry authored after a later-timed one would silently run
                 * before it: a wait written after an action would run first.
                 * Refuse a sequence that goes back in time. */
                s32 seq_index = 0;
                s32 prev_at_ms = 0;
                t = j_next(&p);
                while (t.type != JT_RBRACKET && t.type != JT_EOF) {
                    if (t.type == JT_LBRACE) {
                        if (s_State.event_count >= SMOKE_MAX_EVENTS) {
                            sysLogPrintf(LOG_ERROR, "SMOKE: event cap reached (%d); refusing a partial timeline",
                                SMOKE_MAX_EVENTS);
                            return 0;
                        } else {
                            SmokeEvent *ev = &s_State.events[s_State.event_count];
                            if (!smokeParseEvent(&p, ev)) {
                                return 0;
                            }
                            if (seq_index > 0 && ev->at_ms < prev_at_ms) {
                                sysLogPrintf(LOG_ERROR,
                                    "SMOKE: input_sequence entry %d has at_ms=%d, before the entry above it (at_ms=%d); events run in at_ms order, so author them in that order",
                                    seq_index, ev->at_ms, prev_at_ms);
                                return 0;
                            }
                            prev_at_ms = ev->at_ms;
                            seq_index++;
                            s_State.event_count++;
                        }
					} else {
						sysLogPrintf(LOG_ERROR,
							"SMOKE: input_sequence entries must be objects");
						return 0;
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
			} else {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: input_sequence must be an array");
				return 0;
            }
        } else if (!strcmp(field, "screenshots")) {
            /* Standard scenario screenshots -> in-game glReadPixels events. */
            if (t.type == JT_LBRACKET) {
                s32 ordinal = 0;
                t = j_next(&p);
                while (t.type != JT_RBRACKET && t.type != JT_EOF) {
                    if (t.type == JT_LBRACE) {
                        ordinal++;
                        if (s_State.event_count < SMOKE_MAX_EVENTS) {
                            SmokeEvent *ev = &s_State.events[s_State.event_count];
                            s32 screenshot_result = smokeBuildScreenshotEvent(
                                &p, ev, ordinal);
                            if (screenshot_result < 0) {
                                return 0;
                            }
                            if (screenshot_result > 0) {
                                s_State.event_count++;
                            }
                        } else {
                            if (s_State.screenshot_dir[0]) {
                                sysLogPrintf(LOG_ERROR,
                                    "SMOKE: event cap reached (%d); refusing a partial screenshot timeline",
                                    SMOKE_MAX_EVENTS);
                                return 0;
                            }
                            j_skip_value(&p);
                        }
					} else {
						sysLogPrintf(LOG_ERROR,
							"SMOKE: screenshot entries must be objects");
						return 0;
                    }
                    t = j_next(&p);
                    if (t.type == JT_COMMA) t = j_next(&p);
                }
			} else {
				sysLogPrintf(LOG_ERROR,
					"SMOKE: screenshots must be an array");
				return 0;
            }
        } else {
            /* assertions / description / tags / boot_args / paths_of_interest
             * are runner-side concerns; ignore them in the harness. */
            j_skip_value(&p);
        }

        t = j_next(&p);
        if (t.type == JT_COMMA) t = j_next(&p);
    }

    /* Stable-sort events by at_ms: screenshots may be appended after the
     * input_sequence yet need to interleave, and the dispatch loop stops at the
     * first future event. Insertion sort keeps equal-time ordering stable. */
    for (s32 i = 1; i < s_State.event_count; i++) {
        SmokeEvent key = s_State.events[i];
        s32 k = i - 1;
        while (k >= 0 && s_State.events[k].at_ms > key.at_ms) {
            s_State.events[k + 1] = s_State.events[k];
            k--;
        }
        s_State.events[k + 1] = key;
    }
	if (!smokeValidateWaitHoldSchedule() || !smokeValidateControllerSchedule()) {
		return 0;
	}

    /* wait_until pauses script time while the real watchdog continues. The
     * sum of every bounded pause plus the latest virtual event is therefore
     * the fixture's worst-case real-time schedule. Require strict headroom so
     * a scripted success can never share the watchdog's failure boundary. */
    u64 wait_budget_ms = 0;
    u64 latest_event_ms = 0;
    for (s32 i = 0; i < s_State.event_count; i++) {
        const SmokeEvent *ev = &s_State.events[i];
        if ((u64)ev->at_ms > latest_event_ms) {
            latest_event_ms = (u64)ev->at_ms;
        }
        if (ev->type == SMOKE_EVENT_WAIT_UNTIL) {
            wait_budget_ms += (u64)ev->wait_timeout_ms;
        } else if (ev->type == SMOKE_EVENT_CONTROLLER_ATTACH || ev->type == SMOKE_EVENT_CONTROLLER_DETACH) {
            wait_budget_ms += SMOKE_PAD_TRANSITION_MS;
        }
    }
    if (wait_budget_ms > 0
            && latest_event_ms + wait_budget_ms >= (u64)s_State.timeout_ms) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE: timeout_ms=%d must exceed schedule bound=%llu (latest_event_ms=%llu wait_budget_ms=%llu)",
            s_State.timeout_ms,
            (unsigned long long)(latest_event_ms + wait_budget_ms),
            (unsigned long long)latest_event_ms,
            (unsigned long long)wait_budget_ms);
        return 0;
    }

    return 1;
}

/* ---------------------------------------------------------------- */
/* SDL event injection                                              */
/* ---------------------------------------------------------------- */

static SDL_Keycode smokeScancodeToKeycode(s32 scancode)
{
    /* SDL has a public mapping function; fall through to it for
     * correctness across keyboard layouts.  The KEYDOWN event needs
     * both scancode and sym filled (some consumers use one, some the
     * other). */
    return SDL_GetKeyFromScancode((SDL_Scancode)scancode);
}

/* Resolve the active SDL window so we can stamp windowID on synthesised
 * events. ImGui's SDL2 backend (imgui_impl_sdl2.cpp:400) compares the
 * incoming event's windowID against the one captured at init; a zero
 * windowID causes ImGui to drop the event silently, which broke menu
 * nav in worker delta's iter-2 re-run. We try the public accessor
 * first, then fall back to SDL_GetKeyboardFocus / SDL_GetMouseFocus
 * for the (unlikely) case where the harness fires before gfx_sdl_init
 * has populated the window pointer. Returns 0 if no window resolves;
 * the caller still pushes the event so SDL-side debug consumers still
 * receive it. */
static SDL_Window *smokeResolveWindow(void)
{
    SDL_Window *w = gfxGetSdlWindow();
    if (!w) {
        w = SDL_GetKeyboardFocus();
    }
    if (!w) {
        w = SDL_GetMouseFocus();
    }
    if (!w) {
        return NULL;
    }
    return w;
}

static Uint32 smokeResolveWindowId(void)
{
    SDL_Window *w = smokeResolveWindow();
    return w ? SDL_GetWindowID(w) : 0;
}

/* ImGui's SDL backend may poll the OS cursor during NewFrame after consuming
 * queued SDL_MOUSEMOTION. Keep the real window cursor and the synthetic event
 * at the same client coordinate so hover/click hit-testing cannot snap back to
 * the user's prior cursor position between frames. */
static void smokeWarpMouseTo(s32 x, s32 y)
{
    SDL_Window *w = smokeResolveWindow();
    if (w) {
        SDL_WarpMouseInWindow(w, x, y);
    }
}

static void smokePushKey(s32 scancode, s32 down)
{
    if (scancode <= 0) return;
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.timestamp = SDL_GetTicks();
    ev.key.windowID  = smokeResolveWindowId();
    ev.key.state     = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat    = 0;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    ev.key.keysym.sym      = smokeScancodeToKeycode(scancode);
    ev.key.keysym.mod      = 0;
    SDL_PushEvent(&ev);
}

/* Synthesise an SDL_MOUSEBUTTONDOWN / UP event at {x, y} for the named
 * button. Uses SDL_PushEvent so the event flows through the same path
 * as a real user click -- this matters because ImGui's IsItemHovered /
 * IsItemActive only fire when the press / release sequence is correct.
 *
 * c115 (2026-05-14): also synthesise an SDL_MOUSEMOTION event before the
 * button event so ImGui's hover state catches up to the click position.
 * Without the motion event ImGui treats the click as happening at the
 * mouse cursor's last real position, which is usually still the title
 * bar on the freshly-launched harness window. */
static void smokePushMouse(s32 x, s32 y, s32 button, s32 down)
{
    if (button <= 0) return;
    const Uint32 ts  = SDL_GetTicks();
    const Uint32 wid = smokeResolveWindowId();
    if (down) {
        smokeWarpMouseTo(x, y);
        /* Move-then-click so ImGui's hover hit-test lands on the right
         * widget. Only needed on the press edge; the release edge fires
         * at the same coords so no extra motion is required. */
        SDL_Event mev;
        SDL_zero(mev);
        mev.type             = SDL_MOUSEMOTION;
        mev.motion.timestamp = ts;
        mev.motion.windowID  = wid;
        mev.motion.which     = 0;
        mev.motion.state     = 0;
        mev.motion.x         = (Sint32)x;
        mev.motion.y         = (Sint32)y;
        mev.motion.xrel      = 0;
        mev.motion.yrel      = 0;
        SDL_PushEvent(&mev);
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type            = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.timestamp = ts;
    ev.button.windowID = wid;
    ev.button.which    = 0;
    ev.button.button   = (Uint8)button;
    ev.button.state    = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.clicks   = 1;
    ev.button.x        = (Sint32)x;
    ev.button.y        = (Sint32)y;
    SDL_PushEvent(&ev);
}

static void smokePushMouseMove(s32 x, s32 y)
{
    smokeWarpMouseTo(x, y);
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_MOUSEMOTION;
    ev.motion.timestamp = SDL_GetTicks();
    ev.motion.windowID = smokeResolveWindowId();
    ev.motion.which = 0;
    ev.motion.state = 0;
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = 0;
    ev.motion.yrel = 0;
    SDL_PushEvent(&ev);
}

static void smokePushMouseWheel(s32 wheel_x, s32 wheel_y)
{
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_MOUSEWHEEL;
    ev.wheel.timestamp = SDL_GetTicks();
    ev.wheel.windowID = smokeResolveWindowId();
    ev.wheel.which = 0;
    ev.wheel.x = wheel_x;
    ev.wheel.y = wheel_y;
    ev.wheel.preciseX = (float)wheel_x;
    ev.wheel.preciseY = (float)wheel_y;
    ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    SDL_PushEvent(&ev);
}

/* ---------------------------------------------------------------- */
/* Public API                                                       */
/* ---------------------------------------------------------------- */

static s32 smokeFileTransferSocketProbe(void)
{
    if (!smokeHarnessIsActive() || !sysArgCheck("--portable") || g_MainIsBooting ||
            !presenceFileTransportReady() || strcmp(socialMyAgentName(), "initiator") ||
            socialFriendCount() != 1) return 0;
    const social_friend_t *peer = socialFriendAt(0);
    if (!peer || !peer->handle || strcmp(peer->agent_name, "invitee")) return 0;

    char home[400], path[480];
    sysGetHomePath(home, sizeof(home));
    int n = snprintf(path, sizeof(path), "%s/ft-socket-probe.bin", home);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    s32 write_ok = 1;
    for (u32 i = 0; i < 4097; i++) {
        if (fputc((int)((i * 37u + 11u) & 255u), f) == EOF) {
            write_ok = 0;
            break;
        }
    }
    if (fclose(f) != 0) write_ok = 0;
    if (!write_ok) return 0;
    const s32 sent = fileTransferSendFile(peer->handle, path);
    sysLogPrintf(sent == 0 ? LOG_NOTE : LOG_WARNING,
        "SMOKE.FT.SOCKET: send local=0x%08x peer=0x%08x bytes=4097 result=%d path=%s",
        (unsigned)socialMyHandle(), (unsigned)peer->handle, sent, path);
    return sent == 0;
}

int smokeHarnessIsActive(void)
{
    return s_State.active ? 1 : 0;
}

/* Debug.JumpLogging gate (defined in port/src/main.c, default 0). The JUMP: and
 * CAPSULE: physics diagnostic markers honor this flag so normal play does not
 * flood the log. Each smoke fixture owns the complete on/off assignment below
 * so persisted user settings cannot leak diagnostics into another test. */
extern s32 g_JumpLoggingEnabled;

int smokeHarnessInit(void)
{
    if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
    const SceneFireCounts *scene_counts;

    memset(&s_State, 0, sizeof(s_State));

    const char *path = sysArgGetString("--smoke");
    if (!path || !path[0]) {
        return 0;
    }

    /* Force log to be unfiltered while we are setting up so the test
     * loader's own messages are visible. */
    sysLogPrintf(LOG_NOTE, "SMOKE: harness init, test=%s", path);

    strncpy(s_State.test_path, path, sizeof(s_State.test_path) - 1);

    /* Optional: route the scenario's `screenshots` array through the in-game
     * glReadPixels capture, written into this dir. Must be read before
     * smokeParseJson (the parser builds the screenshot event paths from it). */
    const char *shotdir = sysArgGetString("--smoke-screenshot-dir");
    if (shotdir && shotdir[0]) {
        strncpy(s_State.screenshot_dir, shotdir, sizeof(s_State.screenshot_dir) - 1);
        sysLogPrintf(LOG_NOTE, "SMOKE: screenshot dir=%s (in-game glReadPixels)",
            s_State.screenshot_dir);
    }

    s32 sz = 0;
    char *src = smokeReadFile(path, &sz);
    if (!src) {
        sysLogPrintf(LOG_ERROR, "SMOKE: failed to read test file");
        exit(2);
    }

    if (!smokeParseJson(src)) {
        sysLogPrintf(LOG_ERROR, "SMOKE: failed to parse test file");
        sysMemFree(src);
        exit(2);
    }
    sysMemFree(src);

    /* Apply test-declared env */
    sysLogSetChannelMask(s_State.channel_mask);
    sysLogSetVerbose(s_State.verbose);

    sysLogPrintf(LOG_NOTE, "SMOKE: scenario=%s timeout_ms=%d events=%d channel_mask=0x%04X verbose=%d",
        s_State.scenario_name,
        s_State.timeout_ms,
        s_State.event_count,
        s_State.channel_mask,
        s_State.verbose);

    s_State.active = 1;
    s_State.next_event_idx = 0;
    s_State.timeline_pause_ms = 0;
    s_State.wait_started_ticks_ms = 0;
    s_State.waiting_event_idx = -1;
    memset(&s_State.wait_transition, 0, sizeof(s_State.wait_transition));
    s_State.wait_assist_injected = 0;
    scene_counts = sceneInstrumentGet();
    s_State.observed_stage_ready_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_READY] : 0;
    s_State.observed_stage_teardown_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_TEARDOWN] : 0;
    s_State.ready_stage_num = -1;
    s_State.ready_stage_generation = 0;
    s_State.events_fired = 0;
    s_State.start_ticks_ms = SDL_GetTicks();

    /* Per-test ownership: assign both states so a persisted Debug.JumpLogging
     * value cannot leak into a fixture that did not opt in. */
    g_JumpLoggingEnabled = s_State.jump_logging ? 1 : 0;

    if (g_JumpLoggingEnabled) {
        sysLogPrintf(LOG_NOTE, "SMOKE: jump_logging enabled (JUMP:/CAPSULE: markers on)");
    }

    return 1;
}

static void smokeCountCatalogDependency(const char *dep_id,
        asset_type_e expected_type, void *userdata)
{
    (void)dep_id;
    (void)expected_type;
    (*(s32 *)userdata)++;
}

static void smokeCatalogRecoveryProbe(const char *asset_id, s32 at_ms)
{
    const asset_entry_t *entry = assetCatalogResolve(asset_id);
    asset_type_e type = entry ? entry->type : ASSET_NONE;
    s32 load_result = entry && type != ASSET_NONE
        ? catalogLoadTypedAsset(type, asset_id) : 0;
    entry = assetCatalogResolve(asset_id);
    const asset_runtime_binding_t *binding = entry
        ? assetRuntimeFindByTypeAndId(entry->type, asset_id) : NULL;
    s32 dep_count = 0;
    if (entry) catalogDepForEachTyped(asset_id,
        smokeCountCatalogDependency, &dep_count);
    const s32 file_provider = entry
        && entry->source.primary.provider == fileProvider();
    const char *provider_path = file_provider
        ? fileProviderPath(entry->source.primary) : NULL;
    sysLogPrintf(entry && file_provider && binding ? LOG_NOTE : LOG_WARNING,
        "SMOKE: catalog_recovery_probe id='%s' catalog=%d temporary=%d load=%d provider=%d runtime=%d deps=%d provider_path='%s' runtime_path='%s' at_ms=%d",
        asset_id, entry ? 1 : 0, entry ? entry->temporary : 0, load_result,
        file_provider, binding ? 1 : 0, dep_count,
        provider_path ? provider_path : "",
        binding ? binding->primary_path : "", at_ms);
    if (load_result) catalogReleaseTypedAsset(type, asset_id);
}

static void smokeObserveStageReadyEpoch(void)
{
    const SceneFireCounts *scene_counts = sceneInstrumentGet();
    s32 ready_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_READY] : 0;
    s32 teardown_count = scene_counts
        ? scene_counts->fire_count[SCENE_EVENT_STAGE_TEARDOWN] : 0;

    if (teardown_count != s_State.observed_stage_teardown_count) {
        sysLogPrintf(teardown_count < s_State.observed_stage_teardown_count
                ? LOG_ERROR : LOG_NOTE,
            "SMOKE.READINESS: stage teardown observed old=%d new=%d; clearing epoch",
            s_State.observed_stage_teardown_count, teardown_count);
        s_State.observed_stage_teardown_count = teardown_count;
        s_State.ready_stage_num = -1;
    }

    if (ready_count < s_State.observed_stage_ready_count) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.READINESS: stage-ready counter regressed old=%d new=%d; clearing epoch",
            s_State.observed_stage_ready_count, ready_count);
        s_State.observed_stage_ready_count = ready_count;
        s_State.ready_stage_num = -1;
        return;
    }
    if (ready_count == s_State.observed_stage_ready_count) {
        return;
    }

    s_State.observed_stage_ready_count = ready_count;
    s_State.ready_stage_num = g_StageNum;
    s_State.ready_stage_generation++;
    sysLogPrintf(LOG_NOTE,
        "SMOKE.READINESS: stage-ready epoch generation=%u stage=0x%02x event_count=%d",
        s_State.ready_stage_generation, s_State.ready_stage_num, ready_count);
}

/* B-1209: the match slot of the living simulant that carries the scenario's
 * token (Hacker Central's uplink, Hold the Briefcase's briefcase, Capture the
 * Case's case), or -1. Only the scenario authority runs the scenario, so a
 * networked client answers -1. */
static s32 smokeScenarioTokenHolder(const char **token)
{
    if (token) {
        *token = "none";
    }
    if (!g_Vars.normmplayerisrunning || g_NetMode == NETMODE_CLIENT) {
        return -1;
    }
    for (s32 i = PLAYERCOUNT(); i < g_MpNumChrs && i < MAX_MPCHRS; i++) {
        struct chrdata *chr = g_MpAllChrPtrs[i];
        const char *held = NULL;

        if (chr == NULL || chr->aibot == NULL || chr->prop == NULL || chrIsDead(chr)) {
            continue;
        }
        if (chr->aibot->hasuplink) {
            held = "uplink";
        } else if (chr->aibot->hasbriefcase) {
            held = "briefcase";
        } else if (chr->aibot->hascase) {
            held = "case";
        }
        if (held) {
            if (token) {
                *token = held;
            }
            return i;
        }
    }
    return -1;
}

/* The host observes the exact prepare/rollback boundary. The guest observes
 * the failed exact offer and the production return to the same lobby. */
static struct {
    hub_room_t room_before;
    u8 room_id;
    s32 observed;
    s32 reported;
} s_GridDeclineWitness;

void smokeHarnessGridDeclineObserve(const struct netclient *client)
{
    if ((!sysArgCheck("--debug-grid-decline-witness") && !sysArgCheck("--debug-grid-corrupt-witness"))
            || s_GridDeclineWitness.observed
            || !client || client != g_NetLocalClient || client->room_id == 0xff
            || client->state != CLSTATE_PREPARING) return;
    if (g_NetMode == NETMODE_SERVER) {
        const hub_room_t *room = roomGetById(client->room_id);
        if (!room || room->state != ROOM_STATE_PREPARING || room->client_count != 2) return;
        s_GridDeclineWitness.room_before = *room;
    } else if (g_NetMode != NETMODE_CLIENT) {
        return;
    }
    s_GridDeclineWitness.room_id = client->room_id;
    s_GridDeclineWitness.observed = 1;
}

static s32 smokeGridOfferFailedLobby(const char *corrupt_arena)
{
    const struct netclient *local = g_NetLocalClient;
    /* In the corruption case the guest can record its room assignment
     * before the ordinary host autostart issues the offer. */
    if (corrupt_arena && g_NetMode == NETMODE_CLIENT && !s_GridDeclineWitness.observed
            && local && local->state == CLSTATE_LOBBY && local->room_id != 0xff) {
        s_GridDeclineWitness.room_id = local->room_id;
        s_GridDeclineWitness.observed = 1;
    }
    const u8 room_id = s_GridDeclineWitness.room_id;
    if (!s_GridDeclineWitness.observed || !local || room_id != local->room_id
            || local->state != CLSTATE_LOBBY || local->reviewing_results || g_NetStageEpoch
            || gridMatchRuntimePending() || gridMatchRuntimeActive()) return 0;

    if (g_NetMode == NETMODE_SERVER) {
        const hub_room_t *room = roomGetById(room_id);
        const hub_room_t *before = &s_GridDeclineWitness.room_before;
        if (!room || room->state != ROOM_STATE_LOBBY
                || room->client_count != before->client_count
                || room->creator_client_id != before->creator_client_id
                || room->settings_revision != before->settings_revision
                || room->playlist_revision != before->playlist_revision
                || memcmp(room->clients, before->clients, room->client_count) != 0) return 0;
        for (u8 i = 0; i < room->client_count; i++) {
            const u8 id = room->clients[i];
            if (id >= NET_MAX_CLIENTS || g_NetClients[id].room_id != room_id
                    || g_NetClients[id].state != CLSTATE_LOBBY || g_NetClients[id].reviewing_results) {
                return 0;
            }
        }
        if (!s_GridDeclineWitness.reported) {
            sysLogPrintf(LOG_NOTE,
                "SMOKE.GRID.%s: role=host room=%u members=%u settings_revision=%u playlist_revision=%u retained=1 epoch=0 review=0",
                corrupt_arena ? "CORRUPT" : "DECLINED", (unsigned)room_id,
                (unsigned)room->client_count, (unsigned)room->settings_revision,
                (unsigned)room->playlist_revision);
        }
    } else {
        if (g_NetMode != NETMODE_CLIENT
                || netDistribClientIsGridArena() || netDistribClientConsentPending()) return 0;
        distrib_client_status_t status;
        netDistribClientGetStatus(&status);
        if (!status.current_bytes_total || status.received_count) return 0;
        if (corrupt_arena) {
            if (netDistribClientManifestDeclined() || strcmp(status.current_id, corrupt_arena) != 0
                    || status.current_bytes_received != status.current_bytes_total
                    || assetCatalogResolveAny(corrupt_arena)) return 0;
            char private_dir[FS_MAXPATH + 1];
            struct stat info;
            fsFullPath("$S/grid-session", private_dir, sizeof(private_dir));
            if (stat(private_dir, &info) == 0 || errno != ENOENT) return 0;
            if (!s_GridDeclineWitness.reported) {
                sysLogPrintf(LOG_NOTE,
                    "SMOKE.GRID.CORRUPT: role=guest room=%u retained=1 rejected_bytes=%u catalog=0 private_files=0 epoch=0 review=0 declined=0",
                    (unsigned)room_id, (unsigned)status.current_bytes_received);
            }
        } else if (!netDistribClientManifestDeclined() || status.current_bytes_received) {
            return 0;
        } else if (!s_GridDeclineWitness.reported) {
            sysLogPrintf(LOG_NOTE,
                "SMOKE.GRID.DECLINED: role=guest room=%u retained=1 received_bytes=0 epoch=0 review=0",
                (unsigned)room_id);
        }
    }
    s_GridDeclineWitness.reported = 1;
    return 1;
}

/* A scheduled marker can retry once through the ordinary listen-host request
 * after both peers have had time to see the declined lobby. */
static s32 smokeGridRetryArena(void)
{
    extern s32 netLobbyRequestStartWithSims(u8 mode, const char *stage_id,
        u8 difficulty, u8 anti_client, u8 bots, u8 sim_type, u8 timelimit,
        u32 options, u8 scenario, u8 scorelimit, u16 teamscorelimit, u8 weapon_set);
    static s32 attempted;
    const char *arena_id = sysArgGetString("--debug-grid-retry-arena");
    if (!arena_id || attempted) return 1;
    if (g_NetMode != NETMODE_SERVER || g_NetDedicated || !s_GridDeclineWitness.reported
            || !smokeGridOfferFailedLobby(NULL) || strcmp(arena_id, g_MatchConfig.stage_id) != 0
            || strcmp(g_MatchConfig.scenario_id, "base:combat") != 0
            || !g_MatchConfig.numSlots || g_MatchConfig.numSlots > MATCH_MAX_SLOTS) return 0;

    u8 bots = 0;
    u8 sim_type = 0;
    for (u8 i = 0; i < g_MatchConfig.numSlots; i++) {
        const struct matchslot *slot = &g_MatchConfig.slots[i];
        if (slot->type != SLOT_BOT) continue;
        if (!bots) sim_type = slot->botDifficulty;
        bots++;
    }
    const u8 room_id = s_GridDeclineWitness.room_id;
    const u8 weapon_set = (u8)(g_MatchConfig.weaponSetIndex >= 0
        ? g_MatchConfig.weaponSetIndex : 0xff);
    attempted = 1;
    sysLogPrintf(LOG_NOTE, "SMOKE.GRID.RETRY: starting room=%u arena=%s bots=%u",
        (unsigned)room_id, arena_id, (unsigned)bots);
    if (netLobbyRequestStartWithSims(NETGAMEMODE_MP, arena_id, 0, NET_NULL_CLIENT,
            bots, sim_type, g_MatchConfig.timelimit, matchConfigGetUserOptions(),
            g_MatchConfig.scenario, g_MatchConfig.scorelimit, g_MatchConfig.teamscorelimit,
            weapon_set) != 0) return 0;
    const grid_match_runtime_identity_t *retry = gridMatchRuntimePending();
    return retry && retry->offer.room_id == room_id && retry->offer.generation == 2
        && strcmp(retry->arena_id, arena_id) == 0;
}

static smoke_readiness_facts_t smokeCaptureReadinessFacts(void)
{
    smoke_readiness_facts_t facts;
    const s32 local_player_num = playermgrGetLocalPlayerNum();
    struct player *local_player = local_player_num >= 0
            && local_player_num < MAX_PLAYERS
        ? g_Vars.players[local_player_num] : NULL;
    LayerType scene_layer = sceneCurrentLayer();
    const struct menudialog *menu_dialog = g_Menus[0].curdialog;
    const menu_type_t menu_type = menu_dialog
        ? menupoolTypeForDialogdef(menupoolDialogDef(menu_dialog)) : MENU_TYPE_NONE;
    InputCtxDebugAuthority input_authority;
    SDL_Event menu_key_probe;
    inputCtxDebugSnapshotAuthority(&input_authority);
    memset(&menu_key_probe, 0, sizeof(menu_key_probe));
    menu_key_probe.type = SDL_KEYDOWN;

    memset(&facts, 0, sizeof(facts));
    /* The initial title mode is applied only after the ordinary boot worker
     * has joined and public-source extraction/catalog startup has completed.
     * Project that production boundary instead of teaching fixtures how long
     * a particular clean install happens to take. */
    facts.boot_complete = !g_MainIsBooting;
    facts.title_stage = g_StageNum == STAGE_TITLE;
    facts.title_initial_mode = g_TitleMode == TITLEMODE_LEGAL;
    facts.title_transition_idle = g_TitleNextMode < 0
        && g_TitleDelayedMode < 0;
    facts.title_stage_transition_idle = g_TitleNextStage < 0;
    facts.network_active = g_NetMode == NETMODE_SERVER
        || g_NetMode == NETMODE_CLIENT;
    facts.network_listen_ready = g_NetMode == NETMODE_SERVER
        && netGetHost() != NULL;
    facts.network_reconnect_available = netClientReconnectAvailable();
    facts.local_client_in_game = g_NetLocalClient
        && g_NetLocalClient->state == CLSTATE_GAME;
    facts.manifest_declined = netDistribClientManifestDeclined();
    if (sysArgCheck("--debug-grid-decline-witness")) {
        facts.manifest_declined = smokeGridOfferFailedLobby(NULL);
    }
    facts.local_client_in_lobby = g_NetLocalClient
        && g_NetLocalClient->state == CLSTATE_LOBBY;
    const char *corrupt_arena = sysArgGetString("--debug-grid-corrupt-witness");
    if (corrupt_arena && corrupt_arena[0]) {
        facts.local_client_in_lobby = smokeGridOfferFailedLobby(corrupt_arena);
    }
    facts.local_client_preparing = g_NetLocalClient
        && g_NetLocalClient->state == CLSTATE_PREPARING;
    facts.lobby_countdown_active = facts.local_client_preparing
        && pdguiCountdownIsActive() && pdguiCountdownGetSecs() > 0;
    facts.gameplay_stage = g_StageNum == g_Vars.stagenum
        && g_StageNum != STAGE_TITLE;
    facts.stage_ready_epoch = s_State.ready_stage_generation > 0
        && s_State.ready_stage_num == g_StageNum;
    /* network_stage_live describes every shipping network game mode. Normal
     * Combat Simulator uses normmplayerisrunning; co-op and Counter-Op retain
     * their role globals instead. Requiring only the former made the typed
     * readiness barrier permanently unreachable for network missions. A co-op
     * host alone (AI buddies) has no partner role: its mission's iscoop
     * counts. */
    facts.multiplayer_running = g_Vars.normmplayerisrunning != 0
        || (facts.network_active && (g_Vars.coopplayernum >= 0
            || g_Vars.antiplayernum >= 0))
        || (facts.network_active && g_MissionConfig.iscoop);
    /* Local gameplay readiness is shared by ordinary offline and network
     * fixtures. Network mode still requires the authenticated endpoint to
     * own the resolved local player; offline mode has no netclient object. */
    facts.local_player_present = local_player
		&& (!facts.network_active || (g_NetLocalClient
			&& g_NetLocalClient->player == local_player));
    facts.local_player_spawned = local_player && local_player->prop;
    facts.stage_tick_active = g_Vars.tickmode == TICKMODE_CUTSCENE
        || g_Vars.tickmode == TICKMODE_NORMAL;
    facts.scenario_token_held = smokeScenarioTokenHolder(NULL) >= 0;
    facts.scene_cutscene_layer = scene_layer == LAYER_CUTSCENE;
    facts.scene_gameplay_layer = scene_layer == LAYER_GAMEPLAY;
	facts.cutscene_in_progress = local_player_num >= 0
		&& playerCutsceneInProgress(local_player_num);
	facts.cutscene_frame_ready = local_player_num >= 0
		&& playerCutsceneCurTotalFrame60f(local_player_num) > 30.0f;
	/* A network skip is not production-ready until the listen authority has
	 * published the nonzero generation that the CLC must carry. Offline
	 * campaign has no network generation, so its stage-live facts provide the
	 * equivalent local aperture. */
	facts.cutscene_authority_ready = !facts.network_active
		|| playerCutsceneGeneration() != 0;
    facts.gameplay_tick_normal = g_Vars.tickmode == TICKMODE_NORMAL;
    facts.gameplay_updates_active = g_Vars.lvupdate240 > 0;
	facts.player_in_cutscene = local_player_num >= 0
		&& playerInCutscene(local_player_num);
	facts.player_has_control = local_player_num >= 0
		&& g_PlayersWithControl[local_player_num] != 0;
    facts.player_unpaused = local_player
        && local_player->pausemode == PAUSEMODE_UNPAUSED;
    facts.player_alive = local_player && !local_player->isdead;
    facts.player_walk_mode = local_player
        && local_player->bondmovemode == MOVEMODE_WALK;
    facts.player_bike_mode = local_player
        && local_player->bondmovemode == MOVEMODE_BIKE;
    facts.player_hoverbike = local_player && local_player->hoverbike != NULL;
    /* sceneCurrentLayer() is the top input layer. A buried vehicle layer is
     * not an active driver aperture when an overlay owns the stack. */
    facts.vehicle_driver_layer_active = inputLayerTopType()
        == LAYER_VEHICLE_DRIVER;
    /* The Forge editor camera is flying: a Forge session's own state. And
     * whether the editor has the level's own file open (forge_level_io.h). */
    facts.forge_freefly = forgeIsFreefly() != 0;
    facts.forge_level_file_open = forgeLevelSessionDoc() != NULL;
    facts.endscreen = g_MainIsEndscreen != 0;
	/* B-1193: a mission's results (solo, and networked co-op or counter-op,
	 * one per machine) are the solo end screen type. */
	facts.endscreen_menu_active = menupoolIsActive(MENU_TYPE_ENDSCREEN_MP)
		|| menupoolIsActive(MENU_TYPE_ENDSCREEN_SOLO);
	facts.menu_input_active = inputCtxIsActive(&g_CtxImGuiMenu);
    /* SDL keyboard input belongs to player 0. A buried/sibling dialog, stale
     * pool claim, or focused popup cannot admit ordinary menu key events. */
    facts.agent_select_current = menu_type == MENU_TYPE_AGENT_SELECT;
    facts.agent_create_current = menu_type == MENU_TYPE_AGENT_CREATE;
    facts.agent_select_pool_active = menupoolIsActive(MENU_TYPE_AGENT_SELECT);
    facts.agent_create_pool_active = menupoolIsActive(MENU_TYPE_AGENT_CREATE);
    facts.menu_input_top = inputCtxGetTop() == &g_CtxImGuiMenu;
    facts.menu_focus_lost = input_authority.window_focus_lost;
    facts.menu_focus_settle_remaining_ms = input_authority.focus_settle_remaining_ms;
    facts.menu_key_suppressed = -1;
    facts.menu_binding_capture = -1;
    /* Preserve the existing short-circuit boundary: binding capture can
     * cancel stale state, so it must not be queried just for diagnostics. */
    facts.menu_keyboard_ready = !facts.menu_focus_lost
        && facts.menu_focus_settle_remaining_ms == 0
        && !(facts.menu_key_suppressed = inputCtxShouldSuppressKey(&menu_key_probe))
        && !(facts.menu_binding_capture = pdguiMainMenuBindingCaptureListening());
    facts.grid_consent_preparing = netLocalClientPreparingGridTransfer();
    facts.grid_consent_pending = facts.grid_consent_preparing && netDistribClientConsentPending();
    if (facts.grid_consent_pending) {
        pdgui_menu_view_readiness_t consent_view;
        facts.grid_consent_accept_focused = pdguiMenuViewReadiness("##distrib_prompt",
            !facts.menu_focus_lost, &consent_view) && consent_view.play_focused
            && !pdguiTextKeyboardOwnsMenuInput();
    }
    const s32 lobbyConsentPending = facts.network_active
        && netDistribClientConsentPending() && !pdguiTextKeyboardOwnsMenuInput();
    facts.lobby_content_consent_input = lobbyConsentPending
        && pdguiMenuModalOwnsInput("##distrib_prompt", "Skip", !facts.menu_focus_lost);
    facts.lobby_catalog_room_session_input = lobbyConsentPending
        && menupoolIsActive(MENU_TYPE_ROOM)
        && pdguiMenuModalOwnsInput("##distrib_prompt", "This Session", !facts.menu_focus_lost);
    facts.lobby_countdown_input = facts.lobby_countdown_active
        && !pdguiTextKeyboardOwnsMenuInput()
        && pdguiMenuModalOwnsInput("##countdown_popup", "###countdown_cancel", !facts.menu_focus_lost);
    const pdgui_menu_input_observation_t agent_select_input = pdguiMenuObserveInput(
        "##agent_select", !facts.menu_focus_lost);
    const pdgui_menu_input_observation_t agent_create_input = pdguiMenuObserveInput(
        "##agent_create", !facts.menu_focus_lost);
    facts.agent_select_imgui_rejection_mask = agent_select_input.rejection_mask;
    facts.agent_create_imgui_rejection_mask = agent_create_input.rejection_mask;
    facts.agent_select_imgui_owner = facts.agent_select_imgui_rejection_mask == 0;
    facts.agent_create_imgui_owner = facts.agent_create_imgui_rejection_mask == 0;
    pdgui_menu_view_readiness_t menu_view;
    facts.main_menu_current = menu_type == MENU_TYPE_MAIN_MENU;
    facts.main_menu_pool_active = menupoolIsActive(MENU_TYPE_MAIN_MENU);
    facts.settings_pool_active = menupoolIsActive(MENU_TYPE_MAIN_SETTINGS_VIEW);
    facts.main_menu_imgui_ready = pdguiMenuViewReadiness("##main_menu",
        !input_authority.window_focus_lost, &menu_view) && !pdguiTextKeyboardOwnsMenuInput();
    facts.main_menu_view = menu_view.view;
    facts.main_menu_submitted_tab = menu_view.submitted_tab;
    facts.main_menu_play_focused = menu_view.play_focused;
    facts.main_menu_settings_focused = menu_view.settings_focused;
    const char *active_agent = prefsAgentGetActive();
    facts.active_agent = active_agent && active_agent[0];
    return facts;
}

static void smokeLogReadinessWait(s32 level, const char *status,
        const SmokeEvent *ev, u32 waited_ms, u32 real_elapsed_ms,
		u32 stable_elapsed_ms, const smoke_readiness_facts_t *facts)
{
    if (ev->readiness_condition == SMOKE_READINESS_NETWORK_DECLINED_LOBBY) {
        sysLogPrintf(level, "SMOKE.WAIT.DECLINE: boot=%d net=%d client_lobby=%d declined=%d room=%u",
            facts->boot_complete, facts->network_active, facts->local_client_in_lobby,
            facts->manifest_declined, g_NetLocalClient ? (unsigned)g_NetLocalClient->room_id : 255u);
    }
    if (ev->readiness_condition == SMOKE_READINESS_NETWORK_LOBBY_READY) {
        sysLogPrintf(level, "SMOKE.WAIT.LOBBY: boot=%d net=%d client_lobby=%d",
            facts->boot_complete, facts->network_active, facts->local_client_in_lobby);
    }
    if (ev->readiness_condition == SMOKE_READINESS_GRID_CONSENT_READY) {
        sysLogPrintf(level, "SMOKE.WAIT.GRIDCONSENT: %s preparing=%d pending=%d accept_focused=%d input_top=%d keyboard_ready=%d",
            status, facts->grid_consent_preparing, facts->grid_consent_pending,
            facts->grid_consent_accept_focused, facts->menu_input_top, facts->menu_keyboard_ready);
    }
    if (ev->readiness_condition == SMOKE_READINESS_FORGE_EDITOR_READY
            || ev->readiness_condition == SMOKE_READINESS_FORGE_LEVEL_FILE_OPEN) {
        sysLogPrintf(level, "SMOKE.WAIT.FORGE: freefly=%d level_file_open=%d",
            facts->forge_freefly, facts->forge_level_file_open);
    }
    if (ev->readiness_condition >= SMOKE_READINESS_LOBBY_CATALOG_CONSENT_READY &&
        ev->readiness_condition <= SMOKE_READINESS_LOBBY_CATALOG_ROOM_SESSION_READY) {
        sysLogPrintf(level, "SMOKE.WAIT.LOBBY_UI: preparing=%d consent_input=%d room_session_input=%d countdown=%d countdown_input=%d",
            facts->local_client_preparing, facts->lobby_content_consent_input,
            facts->lobby_catalog_room_session_input, facts->lobby_countdown_active,
            facts->lobby_countdown_input);
    }
    sysLogPrintf(level,
		"SMOKE.WAIT: %s condition=%s at_ms=%d timeout_ms=%u waited_ms=%u real_elapsed_ms=%u stable_ms=%u stable_elapsed_ms=%u assist_action=%d assist_condition=%s assist_hold_ms=%u facts=boot:%d/title:%d/title_initial:%d/title_idle:%d/title_stage_idle:%d/net:%d/listen:%d/reconnect:%d/client_game:%d/stage:%d/ready:%d/mp:%d/player:%d/spawn:%d/tick:%d/layer_cut:%d/layer_game:%d/cut:%d/cut_progress:%d/cut_frame:%d/cut_auth:%d/normal:%d/update:%d/control:%d/unpaused:%d/alive:%d/walk:%d/bike:%d/hoverbike:%d/vehicle_layer_top:%d/end:%d/end_menu:%d/menu_input:%d/agent_select:%d/agent_select_pool:%d/agent_create:%d/agent_create_pool:%d/menu_top:%d/menu_key_ready:%d/agent_select_imgui:%d/agent_create_imgui:%d"
        " focus_lost=%d focus_settle_ms=%u key_suppressed=%d binding_capture=%d agent_select_imgui_reject=0x%x agent_create_imgui_reject=0x%x",
        status, smokeReadinessConditionName(ev->readiness_condition), ev->at_ms,
        ev->wait_timeout_ms, waited_ms, real_elapsed_ms, ev->stable_ms,
		stable_elapsed_ms,
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), ev->assist_hold_ms,
		facts->boot_complete, facts->title_stage,
		facts->title_initial_mode, facts->title_transition_idle,
		facts->title_stage_transition_idle,
		facts->network_active, facts->network_listen_ready,
		facts->network_reconnect_available,
		facts->local_client_in_game,
        facts->gameplay_stage, facts->stage_ready_epoch,
        facts->multiplayer_running,
        facts->local_player_present, facts->local_player_spawned,
        facts->stage_tick_active, facts->scene_cutscene_layer,
        facts->scene_gameplay_layer, facts->player_in_cutscene,
		facts->cutscene_in_progress, facts->cutscene_frame_ready,
		facts->cutscene_authority_ready,
        facts->gameplay_tick_normal, facts->gameplay_updates_active,
        facts->player_has_control, facts->player_unpaused,
		facts->player_alive, facts->player_walk_mode,
		facts->player_bike_mode, facts->player_hoverbike,
		facts->vehicle_driver_layer_active,
		facts->endscreen, facts->endscreen_menu_active,
		facts->menu_input_active,
        facts->agent_select_current, facts->agent_select_pool_active,
        facts->agent_create_current, facts->agent_create_pool_active,
        facts->menu_input_top, facts->menu_keyboard_ready,
        facts->agent_select_imgui_owner, facts->agent_create_imgui_owner,
        facts->menu_focus_lost, facts->menu_focus_settle_remaining_ms,
        facts->menu_key_suppressed, facts->menu_binding_capture,
        facts->agent_select_imgui_rejection_mask, facts->agent_create_imgui_rejection_mask);
    if (ev->readiness_condition >= SMOKE_READINESS_MAIN_MENU_PLAY_READY &&
            ev->readiness_condition <= SMOKE_READINESS_SETTINGS_GAME_READY) {
        char accept[96], back[96], previous[96], next[96];
        pdguiGlyphGetActionLabel(ACTION_MENU_ACCEPT, accept, sizeof(accept));
        pdguiGlyphGetActionLabel(ACTION_MENU_CANCEL, back, sizeof(back));
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_PREV, previous, sizeof(previous));
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_NEXT, next, sizeof(next));
        const char *agent = prefsAgentGetActive();
        sysLogPrintf(level, "SMOKE.MENUOBS: %s condition=%s current=%d pool=%d settings_pool=%d native=%d view=%d tab=%d play_focus=%d settings_focus=%d agent=\"%s\" device=%d accept_vk=%u accept=\"%s\" back=\"%s\" previous=\"%s\" next=\"%s\"",
            status, smokeReadinessConditionName(ev->readiness_condition),
            facts->main_menu_current, facts->main_menu_pool_active, facts->settings_pool_active,
            facts->main_menu_imgui_ready, facts->main_menu_view, facts->main_menu_submitted_tab,
            facts->main_menu_play_focused, facts->main_menu_settings_focused, agent ? agent : "",
            (int)pdguiGlyphGetDevice(), (unsigned int)pdguiGlyphGetPrimaryVk(ACTION_MENU_ACCEPT),
            accept, back, previous, next);
    }
}

/* Smoke owns the virtual joysticks. Production input.c owns their separate
 * controller handles and player assignments through the SDL device watch. */
typedef enum {
    SMOKE_PAD_IDLE, SMOKE_PAD_ATTACHING, SMOKE_PAD_RELEASING,
    SMOKE_PAD_REMOVING
} SmokePadPhase;

typedef struct {
    SDL_Joystick *joystick;
    SDL_JoystickID instance;
    SmokePadPhase phase;
    u32 started_ms;
    s32 pending_state;
    s32 player_slot;
    Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX];
    Sint16 axes[SDL_CONTROLLER_AXIS_MAX];
} SmokePadState;

static SmokePadState s_SmokePads[SMOKE_PAD_COUNT] = {
    { .instance = -1, .player_slot = 0 },
    { .instance = -1, .player_slot = 1 },
};
static s32 s_SmokePadCleanupRegistered;

static s32 smokePadDeviceIndex(SmokePadState *state)
{
    if (state->instance < 0) return -1;
    s32 found = -1;
    SDL_LockJoysticks();
    for (s32 i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_JoystickGetDeviceInstanceID(i) == state->instance) {
            found = i;
            break;
        }
    }
    SDL_UnlockJoysticks();
    return found;
}

static SDL_GameController *smokePadController(SmokePadState *state)
{
    const s32 slot = state->player_slot;
    SDL_GameController *pad = (SDL_GameController *)inputGetPad(slot);
    if (!pad || pad != SDL_GameControllerFromPlayerIndex(slot)
            || SDL_GameControllerGetPlayerIndex(pad) != slot
            || SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))
                != state->instance) return NULL;
    return pad;
}

static s32 smokePadNeutralize(SmokePadState *state)
{
    s32 ok = 1;
    if (!state->joystick || !SDL_JoystickGetAttached(state->joystick)) return 0;
    for (s32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        state->buttons[i] = 0;
        if (SDL_JoystickSetVirtualButton(state->joystick, i, 0) < 0) ok = 0;
    }
    for (s32 i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        state->axes[i] = smokePadAxisNeutral(i);
        if (SDL_JoystickSetVirtualAxis(state->joystick, i,
                state->axes[i]) < 0) ok = 0;
    }
    SDL_JoystickUpdate();
    return ok;
}

/* Re-resolve by instance: SDL device indices can shift after unrelated hotplug.
 * Close only our joystick reference; input.c closes the GameController it owns. */
static s32 smokePadDetachOwned(SmokePadState *state)
{
    s32 ok = 1;
    /* Hold the SDL joystick-list lock across lookup and mutation so another
     * hotplug cannot move the index between the identity check and detach. */
    SDL_LockJoysticks();
    const s32 index = smokePadDeviceIndex(state);
    if (index >= 0 && (!SDL_JoystickIsVirtual(index)
            || SDL_JoystickDetachVirtual(index) < 0)) ok = 0;
    SDL_UnlockJoysticks();
    if (!ok) return 0;
    if (state->joystick) {
        SDL_JoystickClose(state->joystick);
        state->joystick = NULL;
    }
    if (smokePadDeviceIndex(state) >= 0) ok = 0;
    return ok;
}

static s32 smokePadCleanupOne(SmokePadState *state)
{
    s32 ok = 1;
    if (state->instance < 0 && !state->joystick) return 1;
    if (!(SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK)) return 0;
    if (state->joystick && SDL_JoystickGetAttached(state->joystick)) {
        if (!smokePadNeutralize(state)) ok = 0;
    }
    if (!smokePadDetachOwned(state)) ok = 0;
    sysLogPrintf(ok ? LOG_NOTE : LOG_ERROR,
        "SMOKE.PAD: event=terminal_cleanup instance=%d player=%d virtual=1 ok=%d live_release_observed=0",
        (int)state->instance, state->player_slot, ok);
    if (ok) {
        state->instance = -1;
        state->phase = SMOKE_PAD_IDLE;
        state->pending_state = 0;
    }
    return ok;
}

static s32 smokePadCleanup(void)
{
    s32 ok = 1;
    for (s32 slot = 0; slot < SMOKE_PAD_COUNT; ++slot) {
        if (!smokePadCleanupOne(&s_SmokePads[slot])) ok = 0;
    }
    return ok;
}

static void smokePadCleanupAtExit(void)
{
    (void)smokePadCleanup();
}

static s32 smokePadFail(const SmokePadState *state, const char *reason)
{
    sysLogPrintf(LOG_ERROR, "SMOKE.PAD: failure reason=%s instance=%d player=%d sdl=%s",
        reason, (int)state->instance, state->player_slot, SDL_GetError());
    smokeHarnessExit(1, "controller_failed");
    return 0;
}

/* Verify the fixture-owned identity mapping before using logical enum values
 * as raw virtual indices. No physical-device mapping or user binding changes. */
static s32 smokePadMappingValid(SDL_GameController *pad)
{
    for (s32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(
            pad, (SDL_GameControllerButton)i);
        if (bind.bindType != SDL_CONTROLLER_BINDTYPE_BUTTON || bind.value.button != i) return 0;
    }
    for (s32 i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForAxis(
            pad, (SDL_GameControllerAxis)i);
        if (bind.bindType != SDL_CONTROLLER_BINDTYPE_AXIS || bind.value.axis != i) return 0;
    }
    return 1;
}

static s32 smokePadStateMatches(const SmokePadState *state, SDL_GameController *pad)
{
    if (!state->joystick || !SDL_JoystickGetAttached(state->joystick)) return 0;
    for (s32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        if (SDL_JoystickGetButton(state->joystick, i) != state->buttons[i]
                || SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i)
                    != state->buttons[i]) return 0;
    }
    for (s32 i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) {
        const s32 raw = state->axes[i];
        const s32 expected = smokePadAxisNeutral(i) == SDL_JOYSTICK_AXIS_MIN
            ? (raw + 32768) * 32767 / 65535 : raw;
        const s32 mapped = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)i);
        if (SDL_JoystickGetAxis(state->joystick, i) != raw
                || abs(mapped - expected) > 1) return 0;
    }
    return 1;
}

static s32 smokePadObserve(void)
{
    for (s32 slot = 0; slot < SMOKE_PAD_COUNT; ++slot) {
        SmokePadState *state = &s_SmokePads[slot];
        if (state->instance < 0 || state->phase != SMOKE_PAD_IDLE) continue;
        SDL_GameController *pad = smokePadController(state);
        if (!pad || !smokePadStateMatches(state, pad)) return smokePadFail(state, "state_or_player_route");
        if (!state->pending_state) continue;

        InputCtxDebugAuthority authority;
        InputDeviceIdentity frontend;
        inputCtxDebugSnapshotAuthority(&authority);
        const s32 frontend_instance = actionmapGetBindingDeviceIdentity(0, &frontend)
            ? frontend.instance_id : -1;
        sysLogPrintf(LOG_NOTE,
            "SMOKE.PAD: event=state_observed instance=%d player=%d virtual=1 accept=%d cancel=%d last_device=%d focus_lost=%d settle_ms=%u context=%s down=%d primary_present=%d frontend_instance=%d",
            (int)state->instance, slot, actionHeld(0, ACTION_MENU_ACCEPT),
            actionHeld(0, ACTION_MENU_CANCEL), actionmapGetLastDevice(),
            authority.window_focus_lost, authority.focus_settle_remaining_ms,
            authority.effective_top_name ? authority.effective_top_name : "none",
            actionHeld(0, ACTION_MENU_DOWN), inputGetPad(0) != NULL, frontend_instance);
        state->pending_state = 0;
    }
    return 1;
}

static s32 smokePadTickTransition(const SmokeEvent *ev, u32 now)
{
    SmokePadState *state = &s_SmokePads[ev->controller_slot];
    if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
    if (state->phase != SMOKE_PAD_IDLE
            && now - state->started_ms >= SMOKE_PAD_TRANSITION_MS)
        return smokePadFail(state, "transition_timeout");

    if (ev->type == SMOKE_EVENT_CONTROLLER_ATTACH) {
        if (state->phase == SMOKE_PAD_IDLE) {
            smoke_readiness_facts_t facts = smokeCaptureReadinessFacts();
            if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
            const s32 forge_ready = state->player_slot == 0
                && smokeReadinessConditionMet(SMOKE_READINESS_FORGE_EDITOR_READY, &facts)
                && inputCtxGetTop() == &g_CtxForgeEditor && facts.menu_keyboard_ready;
            const s32 menu_ready = forge_ready
                || smokeReadinessConditionMet(SMOKE_READINESS_AGENT_SELECT_READY, &facts)
                || smokeReadinessConditionMet(SMOKE_READINESS_MAIN_MENU_PLAY_READY, &facts);
            if (!menu_ready || state->instance >= 0 || inputGetPad(state->player_slot)
                    || !(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER))
                return smokePadFail(state, "attach_readiness_or_slot_occupied");
            if (!s_SmokePadCleanupRegistered) {
                /* Registered after normal boot's atexit(cleanup), so this runs
                 * first on an unexpected normal exit, before SDL_Quit. */
                if (atexit(smokePadCleanupAtExit) != 0) return smokePadFail(state, "cleanup_registration");
                s_SmokePadCleanupRegistered = 1;
            }
            SDL_VirtualJoystickDesc desc;
            memset(&desc, 0, sizeof(desc));
            desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            desc.naxes = SDL_CONTROLLER_AXIS_MAX;
            desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            desc.name = state->player_slot == 0
                ? "PD2 Smoke Virtual Controller" : "PD2 Smoke Secondary Virtual Controller";
            for (s32 i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i) desc.axis_mask |= 1u << i;
            for (s32 i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) desc.button_mask |= 1u << i;
            state->phase = SMOKE_PAD_ATTACHING;
            state->started_ms = now;
            /* Resolve the new device before unrelated hotplug can shift its
             * index. Events remain in SDL's normal path after unlocking. */
            SDL_LockJoysticks();
            const s32 index = SDL_JoystickAttachVirtualEx(&desc);
            s32 is_controller = 0;
            if (index >= 0) {
                state->instance = SDL_JoystickGetDeviceInstanceID(index);
                state->joystick = SDL_JoystickOpen(index);
                is_controller = smokeInstallVirtualMapping(index) && SDL_IsGameController(index);
                if (state->instance < 0) {
                    /* No stable identity: roll back only this just-created
                     * index while still holding the unchanged SDL list. */
                    if (state->joystick) SDL_JoystickClose(state->joystick);
                    state->joystick = NULL;
                    if (SDL_JoystickDetachVirtual(index) < 0)
                        sysLogPrintf(LOG_ERROR, "SMOKE.PAD: failure reason=attach_identity_rollback");
                }
            }
            SDL_UnlockJoysticks();
            if (index < 0) return smokePadFail(state, "attach_sdl");
            if (state->instance < 0 || !state->joystick
                    || !is_controller || !smokePadNeutralize(state))
                return smokePadFail(state, "attach_open_or_neutral");
            return 0;
        }
        SDL_GameController *pad = smokePadController(state);
        if (!pad) return smokePadFail(state, "attach_player_route");
        if (!smokePadMappingValid(pad) || !smokePadStateMatches(state, pad))
            return smokePadFail(state, "attach_mapping_or_state");
        SDL_version runtime;
        SDL_GetVersion(&runtime);
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(state->joystick), guid, sizeof(guid));
        char *mapping = SDL_GameControllerMapping(pad);
        sysLogPrintf(LOG_NOTE,
            "SMOKE.PAD: event=attached instance=%d player=%d virtual=1 peer_instance=%d compiled=%d.%d.%d runtime=%d.%d.%d type=%d name='%s' guid=%s mapping='%s'",
            (int)state->instance, state->player_slot,
            (int)s_SmokePads[1 - state->player_slot].instance,
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
            runtime.major, runtime.minor, runtime.patch, (int)SDL_GameControllerGetType(pad),
            SDL_GameControllerName(pad), guid, mapping ? mapping : "");
        SDL_free(mapping);
    } else if (state->phase == SMOKE_PAD_IDLE) {
        if (!smokePadController(state) || !smokePadNeutralize(state)) return smokePadFail(state, "detach_neutralize");
        state->phase = SMOKE_PAD_RELEASING;
        state->started_ms = now;
        return 0;
    } else if (state->phase == SMOKE_PAD_RELEASING) {
        SDL_GameController *pad = smokePadController(state);
        if (!pad || !smokePadStateMatches(state, pad)) return smokePadFail(state, "detach_release_readback");
        if (!smokePadDetachOwned(state)) return smokePadFail(state, "detach_sdl");
        state->phase = SMOKE_PAD_REMOVING;
        return 0;
    } else {
        if (smokePadDeviceIndex(state) >= 0 || inputGetPad(state->player_slot)
                || SDL_GameControllerFromInstanceID(state->instance))
            return smokePadFail(state, "detach_removal_readback");
        sysLogPrintf(LOG_NOTE,
            "SMOKE.PAD: event=detached instance=%d player=%d neutral_observed=1 removed_observed=1 virtual=1 peer_instance=%d",
            (int)state->instance, state->player_slot,
            (int)s_SmokePads[1 - state->player_slot].instance);
        state->instance = -1;
    }
    s_State.timeline_pause_ms += now - state->started_ms;
    state->phase = SMOKE_PAD_IDLE;
    state->pending_state = 0;
    return 1;
}

static s32 smokePadWrite(const SmokeEvent *ev)
{
    SmokePadState *state = &s_SmokePads[ev->controller_slot];
    if (!smokePadController(state) || !state->joystick) return smokePadFail(state, "write_without_player");
    s32 rc;
    if (ev->type == SMOKE_EVENT_CONTROLLER_AXIS) {
        state->axes[ev->controller_control] = (Sint16)ev->controller_value;
        rc = SDL_JoystickSetVirtualAxis(state->joystick, ev->controller_control,
            (Sint16)ev->controller_value);
        sysLogPrintf(LOG_NOTE, "SMOKE.PAD: event=axis instance=%d player=%d axis=%s value=%d virtual=1",
            (int)state->instance, state->player_slot, SDL_GameControllerGetStringForAxis(
                (SDL_GameControllerAxis)ev->controller_control), ev->controller_value);
    } else {
        state->buttons[ev->controller_control] = (Uint8)ev->controller_value;
        rc = SDL_JoystickSetVirtualButton(state->joystick, ev->controller_control,
            (Uint8)ev->controller_value);
        sysLogPrintf(LOG_NOTE, "SMOKE.PAD: event=button instance=%d player=%d button=%s down=%d virtual=1",
            (int)state->instance, state->player_slot, SDL_GameControllerGetStringForButton(
                (SDL_GameControllerButton)ev->controller_control), ev->controller_value);
    }
    if (rc < 0) return smokePadFail(state, "set_virtual_state");
    state->pending_state = 1;
    return 1;
}

static s32 smokeHasPendingTap(void)
{
    for (s32 i = 0; i < s_State.next_event_idx; i++) {
        const SmokeEvent *ev = &s_State.events[i];
        if ((ev->type == SMOKE_EVENT_KEY_TAP
                || ev->type == SMOKE_EVENT_ACTION_TAP
                || ev->type == SMOKE_EVENT_MOUSE_TAP)
                && ev->released == 1) {
            return 1;
        }
    }
    return 0;
}

static smoke_transition_config_t smokeWaitTransitionConfig(
        const SmokeEvent *ev)
{
    smoke_transition_config_t config;
    memset(&config, 0, sizeof(config));
    config.timeout_ms = ev->wait_timeout_ms;
    config.stable_ms = ev->stable_ms;
    config.assist_enabled = ev->assist_action_id >= 0;
    config.assist_hold_ms = ev->assist_hold_ms;
    return config;
}

static s32 smokePressWaitAssist(const SmokeEvent *ev, u32 waited_ms)
{
    if (s_State.wait_assist_injected) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press rejected=already_injected at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
            waited_ms);
        return 0;
    }
    if (!actionmapInjectStateForSmoke(0, ev->assist_action_id, 1)) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press rejected=actionmap at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
            waited_ms);
        return 0;
    }
	s_State.wait_assist_injected = 1;
	s_State.wait_assist_press_count++;
    sysLogPrintf(LOG_NOTE,
        "SMOKE.WAIT.ASSIST: action=%d condition=%s event=press at_ms=%d waited_ms=%u hold_ms=%u",
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), ev->at_ms,
        waited_ms, ev->assist_hold_ms);
    return 1;
}

static s32 smokeReleaseWaitAssist(const SmokeEvent *ev, const char *reason,
        u32 waited_ms)
{
    if (!s_State.wait_assist_injected) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release rejected=not_injected reason=%s at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), reason,
            ev->at_ms, waited_ms);
        return 0;
    }
    if (!actionmapInjectStateForSmoke(0, ev->assist_action_id, 0)) {
        sysLogPrintf(LOG_ERROR,
            "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release rejected=actionmap reason=%s at_ms=%d waited_ms=%u",
            ev->assist_action_id,
            smokeReadinessConditionName(ev->assist_condition), reason,
            ev->at_ms, waited_ms);
        return 0;
    }
    s_State.wait_assist_injected = 0;
	s_State.wait_assist_release_count++;
    smokeTransitionForceRelease(&s_State.wait_transition);
    sysLogPrintf(LOG_NOTE,
        "SMOKE.WAIT.ASSIST: action=%d condition=%s event=release reason=%s at_ms=%d waited_ms=%u",
        ev->assist_action_id,
        smokeReadinessConditionName(ev->assist_condition), reason,
        ev->at_ms, waited_ms);
    return 1;
}

/* Returns 1 when satisfied, 0 while waiting, and -1 after a fail-closed exit. */
static s32 smokeTickReadinessWait(SmokeEvent *ev, u32 now,
        u32 real_elapsed_ms)
{
    smoke_readiness_facts_t facts = smokeCaptureReadinessFacts();
    smoke_transition_config_t config = smokeWaitTransitionConfig(ev);
    smoke_transition_input_t input;
    smoke_transition_plan_t plan;
    s32 first_tick = s_State.waiting_event_idx != s_State.next_event_idx;

    if (first_tick) {
        if (s_State.waiting_event_idx >= 0) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE.WAIT: overlapping wait events active=%d next=%d",
                s_State.waiting_event_idx, s_State.next_event_idx);
            smokeHarnessExit(1, "wait_overlap");
            return -1;
        }
        s_State.waiting_event_idx = s_State.next_event_idx;
        s_State.wait_started_ticks_ms = now;
        smokeTransitionInit(&s_State.wait_transition, &config, now);
        s_State.wait_assist_injected = 0;
		s_State.wait_assist_press_count = 0;
		s_State.wait_assist_release_count = 0;
        smokeLogReadinessWait(LOG_NOTE, "begin", ev, 0, real_elapsed_ms,
			0, &facts);
    }

    u32 waited_ms = now - s_State.wait_started_ticks_ms;
    /* Pausing virtual time while a one-frame tap remains down would turn it
     * into an unbounded hold. Reject this even when the condition is already
     * true so a malformed fixture cannot hide the pending ownership leak. */
    if (first_tick && smokeHasPendingTap()) {
        smokeLogReadinessWait(LOG_ERROR, "pending_tap", ev, waited_ms,
			real_elapsed_ms, 0, &facts);
        smokeHarnessExit(1, "wait_pending_tap");
        return -1;
    }

    memset(&input, 0, sizeof(input));
    input.target_met = smokeReadinessConditionMet(ev->readiness_condition,
        &facts);
    if (ev->assist_action_id >= 0) {
        input.assist_condition_met = smokeReadinessConditionMet(
            ev->assist_condition, &facts);
    }
    plan = smokeTransitionTick(&s_State.wait_transition, &config, &input,
        now);
    if (plan.invalid) {
        smokeLogReadinessWait(LOG_ERROR, "invalid_policy", ev, waited_ms,
			real_elapsed_ms, 0, &facts);
        smokeHarnessExit(1, "wait_policy_invalid");
        return -1;
    }

    if (plan.stability_started) {
        sysLogPrintf(LOG_NOTE,
            "SMOKE.WAIT.STABLE: condition=%s event=begin stable_ms=%u at_ms=%d waited_ms=%u",
            smokeReadinessConditionName(ev->readiness_condition),
            ev->stable_ms, ev->at_ms, waited_ms);
    }
    if (plan.stability_reset) {
        sysLogPrintf(LOG_NOTE,
            "SMOKE.WAIT.STABLE: condition=%s event=reset stable_ms=%u at_ms=%d waited_ms=%u",
            smokeReadinessConditionName(ev->readiness_condition),
            ev->stable_ms, ev->at_ms, waited_ms);
    }

    /* The policy changes ownership state first, then the harness projects that
     * plan through the production action map. Releases always precede terminal
     * timeout/success handling, so no exit can strand a held assist. */
    if (plan.release_assist
            && !smokeReleaseWaitAssist(ev,
                plan.timed_out ? "wait_timeout" : "hold_elapsed",
                waited_ms)) {
        smokeHarnessExit(1, "wait_assist_release_failed");
        return -1;
    }
    if (plan.press_assist && !smokePressWaitAssist(ev, waited_ms)) {
        smokeHarnessExit(1, "wait_assist_press_failed");
        return -1;
    }
    if (plan.timed_out) {
        smokeLogReadinessWait(LOG_ERROR, "timeout", ev, waited_ms,
			real_elapsed_ms, plan.stable_elapsed_ms, &facts);
        smokeHarnessExit(1, "wait_condition_timeout");
        return -1;
    }

    if (plan.satisfied) {
        if (s_State.wait_assist_injected) {
            smokeLogReadinessWait(LOG_ERROR, "held_assist", ev, waited_ms,
				real_elapsed_ms, plan.stable_elapsed_ms, &facts);
            smokeHarnessExit(1, "wait_assist_held_on_success");
            return -1;
        }
        if (ev->assist_action_id >= 0) {
			s32 assist_used = smokeTransitionAssistWasUsed(
				&s_State.wait_transition);
			if ((assist_used && (s_State.wait_assist_press_count != 1
						|| s_State.wait_assist_release_count != 1))
					|| (!assist_used && (s_State.wait_assist_press_count != 0
						|| s_State.wait_assist_release_count != 0))) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.WAIT.ASSIST: action=%d condition=%s rejected=ownership_balance press_count=%d release_count=%d at_ms=%d waited_ms=%u",
					ev->assist_action_id,
					smokeReadinessConditionName(ev->assist_condition),
					s_State.wait_assist_press_count,
					s_State.wait_assist_release_count, ev->at_ms, waited_ms);
				smokeHarnessExit(1, "wait_assist_balance_invalid");
				return -1;
			}
            sysLogPrintf(LOG_NOTE,
				"SMOKE.WAIT.ASSIST: action=%d condition=%s outcome=%s at_ms=%d waited_ms=%u press_count=%d release_count=%d",
                ev->assist_action_id,
                smokeReadinessConditionName(ev->assist_condition),
				assist_used ? "used" : "not_needed", ev->at_ms, waited_ms,
				s_State.wait_assist_press_count,
				s_State.wait_assist_release_count);
        }
        s_State.timeline_pause_ms += waited_ms;
        s_State.waiting_event_idx = -1;
        s_State.wait_started_ticks_ms = 0;
        memset(&s_State.wait_transition, 0, sizeof(s_State.wait_transition));
        smokeLogReadinessWait(LOG_NOTE, "satisfied", ev, waited_ms,
			real_elapsed_ms, plan.stable_elapsed_ms, &facts);
        if (ev->readiness_condition == SMOKE_READINESS_OFFLINE_GAMEPLAY_READY
                && !smokeGridPortalAnchor()) return -1;
        return 1;
    }
    return 0;
}

#define SMOKE_LEDGE_STAY_TICKS 30

/* Both stock approaches face +Z. Complex uses fixed room collision. Villa
 * uses the live coffee table from setup_0492 at pad_0428 in the native level. */
static const SmokeSupportFixture s_SmokeSupportFixtures[] = {
    [SMOKE_SUPPORT_COMPLEX_LEDGE] = {
        .marker = "SMOKE.LEDGE", .stage = STAGE_MP_COMPLEX, .room = 24,
        .x = -1392.0f, .lane_half_width = 30.0f, .start_z = -700.0f,
        .lower_y = 512.0f, .upper_y = 551.0f, .edge_z = -656.0f,
        .upper_min_z = -628.0f, .upper_max_z = -500.0f,
        .upper_goal_z = -560.0f, .lower_goal_z = -745.0f,
        .lower_return_max_z = -685.0f, .table_command = -1,
    },
    [SMOKE_SUPPORT_VILLA_TABLETOP] = {
        .marker = "SMOKE.TABLETOP", .stage = STAGE_VILLA, .room = 17,
        .x = -4065.0f, .lane_half_width = 22.0f, .start_z = 2250.0f,
        .lower_y = -300.0f, .upper_y = -240.0f, .edge_z = 2306.0f,
        .upper_min_z = 2326.0f, .upper_max_z = 2370.0f,
        .upper_goal_z = 2348.0f, .lower_goal_z = 2235.0f,
        .lower_return_max_z = 2270.0f, .table_command = 492, .table_pad = 428,
        .measure_table = 1,
    },
};

/* B-1310: samples of the actor after takeoff, so a missed landing shows its
 * arc (feet, velocity, position) and not only the verdict. */
#define SMOKE_LEDGE_TRACE_FRAMES 150
/* B-1310, candidate 4: before takeoff, one sample every this many frames
 * (up to the cap), so a missing takeoff shows where the actor went and, for a
 * simulant, whether it had a route. */
#define SMOKE_LEDGE_APPROACH_EVERY   10
#define SMOKE_LEDGE_APPROACH_SAMPLES 60
/* B-1310, candidate 4: a simulant under HOLD is at its post within 40 units of
 * the hold on each axis (MA_AIBOTDEFEND in botTickUnpaused: xdist > 40 ||
 * zdist > 40 sends it back), so its lane allows that, with a margin. R16's
 * Complex simulant landed and stood 39.4 off the lane's line. */
#define SMOKE_LEDGE_BOT_POST_HALF_WIDTH 44.0f
/* B-1310, candidate 4: with the move axis released, the player's speed along
 * the approach falls by about 0.93 per frame, in the air and on the ground
 * (R16's traces; the speed comes from the head animation, so there is no one
 * constant in the code), so it coasts about 13 frames of its current speed.
 * The harness releases the axis when that coast would stop the player at the
 * fixture's goal, for the jump and for the walk-off: a timed release ran on to
 * three times the distance and overshot the ledge window and the whole table. */
#define SMOKE_LEDGE_PLAYER_COAST_FRAMES 13.3f
#define SMOKE_LEDGE_PLAYER_RELEASE_SPEED 0.5f

/* B-1310: while the ledge harness holds a simulant, the simulant takes no
 * items: an unarmed simulant otherwise leaves its hold for the nearest weapon
 * first, as the original orders its choices (botTickUnpaused). */
int smokeHarnessBotPickupsHeld(const struct chrdata *chr)
{
    const SmokeLedge *ledge = &s_State.ledge;

    return s_State.active && chr && ledge->bot_slot >= 0 && ledge->actor_prop
        && chr->prop == ledge->actor_prop;
}

static s32 smokeLedgeFail(const char *reason)
{
    const SmokeLedge *ledge = &s_State.ledge;

    sysLogPrintf(LOG_ERROR,
        "%s: result=fail reason=%s chrnum=%d takeoff=%d descended=%d landed=%d stayed=%d walk_off=%d dropped=%d lower_landed=%d peak_feet=%.2f",
        ledge->fixture->marker, reason, ledge->actor_chrnum, ledge->took_off, ledge->descended,
        ledge->landed, ledge->stayed, ledge->walk_off, ledge->dropped,
        ledge->lower_landed, ledge->peak_feet);
    smokeHarnessExit(1, "movement_ledge_failed");
    return 0;
}

static s32 smokeLedgeStageActive(const SmokeSupportFixture *fixture)
{
    return g_NetMode == NETMODE_NONE && g_StageNum == fixture->stage
        && g_Vars.stagenum == fixture->stage && g_Vars.normmplayerisrunning
        && s_State.ready_stage_generation != 0
        && s_State.ready_stage_num == fixture->stage;
}

static struct chrdata *smokeLedgeActor(s32 bot_slot)
{
    struct player *player = g_Vars.players[0];
    struct chrdata *chr;

    if (bot_slot >= 0) {
        if (bot_slot >= g_BotCount || bot_slot >= MAX_BOTS) return NULL;
        chr = g_MpBotChrPtrs[bot_slot];
        if (!chr || !chr->aibot || chrIsDead(chr)) return NULL;
    } else {
        if (!player || !player->prop || player->isdead) return NULL;
        chr = player->prop->chr;
        if (!chr || chr->prop != player->prop) return NULL;
    }
    return chr->prop && chr->prop->chr == chr ? chr : NULL;
}

static s32 smokeLedgeTableStable(const SmokeLedge *ledge)
{
    struct prop *prop = ledge->table_prop;
    const struct coord *pos = &prop->pos;
    const struct coord *initial = &ledge->table_pos;

    return ledge->table->prop == prop && prop->obj == ledge->table
        && meshPropIsMovementSolid(prop) && meshGetFromProp(prop)
        && prop->rooms[0] == ledge->fixture->room
        && fabsf(pos->x - initial->x) <= 0.5f
        && fabsf(pos->y - initial->y) <= 0.5f
        && fabsf(pos->z - initial->z) <= 0.5f
        && memcmp(ledge->table_rotation, ledge->table->realrot,
            sizeof(ledge->table_rotation)) == 0;
}

static s32 smokeLedgeTableContact(const SmokeLedge *ledge, const struct coord *pos,
        f32 feet, struct prop *actor)
{
    const RoomNum rooms[] = { ledge->fixture->room, -1 };
    const struct coord from = { pos->x, feet + 6.0f, pos->z };
    const struct coord to = { pos->x, feet - 6.0f, pos->z };
    struct coord normal;
    f32 fraction = 1.0f;
    struct prop *hit = NULL;

    return meshRayCastDynamicProps(&from, &to, rooms, actor, &fraction,
            &normal, &hit)
        && hit == ledge->table_prop && fabsf(normal.y) > 0.7f
        && fabsf(6.0f - 12.0f * fraction) <= 3.0f;
}

/* B-1310: the live table's collision bounds in world space: its mesh's local
 * box through the prop's transform (conservative for a turned table). */
static s32 smokeLedgeTableBounds(struct prop *prop, struct coord *mn, struct coord *mx)
{
    struct colmesh *mesh = meshGetFromProp(prop);
    Mtxf mtx;

    if (!mesh || mesh->numtris <= 0 || !meshBuildPropTransform(prop, &mtx)) {
        return 0;
    }

    for (s32 i = 0; i < 8; i++) {
        struct coord local = {
            (i & 1) ? mesh->bboxmax.x : mesh->bboxmin.x,
            (i & 2) ? mesh->bboxmax.y : mesh->bboxmin.y,
            (i & 4) ? mesh->bboxmax.z : mesh->bboxmin.z,
        };
        struct coord world;
        mtx4TransformVec(&mtx, &local, &world);
        if (i == 0) {
            *mn = world;
            *mx = world;
            continue;
        }
        mn->x = world.x < mn->x ? world.x : mn->x;
        mn->y = world.y < mn->y ? world.y : mn->y;
        mn->z = world.z < mn->z ? world.z : mn->z;
        mx->x = world.x > mx->x ? world.x : mx->x;
        mx->y = world.y > mx->y ? world.y : mx->y;
        mx->z = world.z > mx->z ? world.z : mx->z;
    }
    return 1;
}

/* B-1310: keep the authored approach (start, edge, goals along +Z) but move it
 * onto the live table: its centre line, its near edge, its top, and the floor
 * the start stands on. Every number is logged, so a blocked approach shows. */
static s32 smokeLedgeMeasureTable(SmokeLedge *ledge, const SmokeSupportFixture *authored)
{
    struct coord mn;
    struct coord mx;

    if (!smokeLedgeTableBounds(ledge->table_prop, &mn, &mx)) {
        return smokeLedgeFail("table_bounds_unavailable");
    }

    SmokeSupportFixture *m = &ledge->measured;
    const f32 dz = mn.z - authored->edge_z;
    *m = *authored;
    m->x = (mn.x + mx.x) * 0.5f;
    m->lane_half_width = (mx.x - mn.x) * 0.5f - 4.0f < authored->lane_half_width
        ? (mx.x - mn.x) * 0.5f - 4.0f : authored->lane_half_width;
    m->upper_y = mx.y;
    m->edge_z = mn.z;
    m->start_z = authored->start_z + dz;
    m->upper_min_z = authored->upper_min_z + dz;
    m->upper_max_z = authored->upper_max_z + dz < mx.z - 4.0f ? authored->upper_max_z + dz : mx.z - 4.0f;
    m->upper_goal_z = (m->upper_min_z + m->upper_max_z) * 0.5f;
    m->lower_goal_z = authored->lower_goal_z + dz;
    m->lower_return_max_z = authored->lower_return_max_z + dz;

    RoomNum rooms[] = { authored->room, -1 };
    struct coord start = { m->x, mx.y + 40.0f, m->start_z };
    m->lower_y = cdFindGroundAtCyl(&start, 20.0f, rooms, NULL, NULL);

    sysLogPrintf(LOG_NOTE,
        "%s: phase=table_measured pos=(%.1f,%.1f,%.1f) x=[%.1f,%.1f] y=[%.1f,%.1f] z=[%.1f,%.1f] lane_x=%.1f half=%.1f floor=%.1f top=%.1f start_z=%.1f edge_z=%.1f upper_z=[%.1f,%.1f] authored_top=%.1f authored_edge_z=%.1f",
        authored->marker, ledge->table_pos.x, ledge->table_pos.y, ledge->table_pos.z,
        mn.x, mx.x, mn.y, mx.y, mn.z, mx.z, m->x, m->lane_half_width, m->lower_y, m->upper_y,
        m->start_z, m->edge_z, m->upper_min_z, m->upper_max_z, authored->upper_y, authored->edge_z);

    if (m->lane_half_width < 4.0f || m->upper_max_z <= m->upper_min_z
            || m->upper_y - m->lower_y < 10.0f || m->upper_y - m->lower_y > 100.0f) {
        return smokeLedgeFail("table_measure_unusable");
    }
    ledge->fixture = m;
    return 1;
}

static s32 smokeLedgeBindTable(SmokeLedge *ledge, struct prop *actor)
{
    const SmokeSupportFixture *fixture = ledge->fixture;
    struct defaultobj *obj = (struct defaultobj *)setupGetCmdByIndex(fixture->table_command);
    if (!obj || obj->type != OBJTYPE_BASIC || obj->modelnum != MODEL_CV_COFFEE_TABLE
            || obj->pad != fixture->table_pad || !obj->prop || obj->prop->obj != obj
            || obj->prop->rooms[0] != fixture->room) {
        return smokeLedgeFail("authored_table_missing");
    }

    ledge->table = obj;
    ledge->table_prop = obj->prop;
    ledge->table_pos = obj->prop->pos;
    memcpy(ledge->table_rotation, obj->realrot, sizeof(ledge->table_rotation));
    if (!smokeLedgeTableStable(ledge)) return smokeLedgeFail("table_collision_unavailable");

    if (fixture->measure_table) {
        if (!smokeLedgeMeasureTable(ledge, fixture)) return 0;
        fixture = ledge->fixture;
    }

    const struct coord target = { fixture->x, fixture->upper_y, fixture->upper_goal_z };
    if (!smokeLedgeTableContact(ledge, &target, fixture->upper_y, actor)) {
        return smokeLedgeFail("authored_tabletop_contact_missing");
    }
    sysLogPrintf(LOG_NOTE, "%s: phase=table_bound command=%d model=%s pad=%d room=%d collision=dynamic_mesh",
        fixture->marker, fixture->table_command, "base:model_cv_coffee_table",
        fixture->table_pad, fixture->room);
    return 1;
}

/* B-1310, candidate 4: a sample before takeoff. For a simulant it also says
 * whether its hold gave it a route (ACT_GOPOS) and where that route ends:
 * R16's Villa simulant never took off and left nothing to read. */
static void smokeLedgeLogApproach(const SmokeLedge *ledge, const struct chrdata *chr, s32 frame,
                                  f32 feet, f32 ground, f32 velocity, RoomNum floorroom, f32 speed_z)
{
    const struct coord *pos = &chr->prop->pos;

    if (ledge->bot_slot < 0) {
        sysLogPrintf(LOG_NOTE,
            "%s: approach n=%d frame=%d feet=%.2f ground=%.2f velocity=%.2f x=%.1f z=%.1f speed_z=%.2f room=%d",
            ledge->fixture->marker, ledge->approach_samples, frame, feet, ground, velocity,
            pos->x, pos->z, speed_z, floorroom);
        return;
    }
    const s32 routed = chr->actiontype == ACT_GOPOS;
    sysLogPrintf(LOG_NOTE,
        "%s: approach n=%d frame=%d feet=%.2f ground=%.2f velocity=%.2f x=%.1f z=%.1f speed_z=%.2f room=%d action=%d myaction=%d routed=%d forwards=%.2f end=(%.1f,%.1f)",
        ledge->fixture->marker, ledge->approach_samples, frame, feet, ground, velocity,
        pos->x, pos->z, speed_z, floorroom, (s32)chr->actiontype, (s32)chr->myaction, routed,
        chr->aibot ? chr->aibot->speedmultforwards : 0.0f,
        routed ? chr->act_gopos.endpos.x : 0.0f, routed ? chr->act_gopos.endpos.z : 0.0f);
}

/* B-1310, candidate 4: release the player's move axis once its coast would
 * stop it at the fixture's goal: on the jump (moving +Z, to the upper goal)
 * and on the walk-off (moving -Z, to the lower goal). The scripted release
 * stays in each fixture as a backstop. When the player then comes to rest,
 * the predicted and the actual stop are logged together. */
static void smokeLedgeReleaseMoveAtGoal(SmokeLedge *ledge, f32 z, f32 speed_z)
{
    const SmokeSupportFixture *fixture = ledge->fixture;
    const f32 predicted_z = z + speed_z * SMOKE_LEDGE_PLAYER_COAST_FRAMES;
    const char *stage = NULL;
    f32 goal_z = 0.0f;

    if (ledge->approach_released == 1 && !ledge->walk_off && fabsf(speed_z) < 0.05f) {
        ledge->approach_released = 2;
        sysLogPrintf(LOG_NOTE, "%s: phase=coast stage=approach predicted_z=%.1f actual_z=%.1f error=%.1f",
            fixture->marker, ledge->approach_predicted_z, z, z - ledge->approach_predicted_z);
    }
    if (ledge->walk_released == 1 && fabsf(speed_z) < 0.05f) {
        ledge->walk_released = 2;
        sysLogPrintf(LOG_NOTE, "%s: phase=coast stage=walk_off predicted_z=%.1f actual_z=%.1f error=%.1f",
            fixture->marker, ledge->walk_predicted_z, z, z - ledge->walk_predicted_z);
    }

    if (ledge->took_off && !ledge->walk_off && !ledge->approach_released
            && speed_z > SMOKE_LEDGE_PLAYER_RELEASE_SPEED && predicted_z >= fixture->upper_goal_z) {
        ledge->approach_released = 1;
        ledge->approach_predicted_z = predicted_z;
        stage = "approach";
        goal_z = fixture->upper_goal_z;
    } else if (ledge->walk_off && !ledge->walk_released
            && speed_z < -SMOKE_LEDGE_PLAYER_RELEASE_SPEED && predicted_z <= fixture->lower_goal_z) {
        ledge->walk_released = 1;
        ledge->walk_predicted_z = predicted_z;
        stage = "walk_off";
        goal_z = fixture->lower_goal_z;
    }
    if (!stage) return;
    actionmapInjectStateForSmoke(0, ACTION_AXIS_MOVE_Y, 0);
    sysLogPrintf(LOG_NOTE,
        "%s: phase=release stage=%s z=%.1f speed_z=%.2f predicted_z=%.1f goal_z=%.1f",
        fixture->marker, stage, z, speed_z, predicted_z, goal_z);
}

static s32 smokeLedgeObserve(void)
{
    SmokeLedge *ledge = &s_State.ledge;

    if (!ledge->actor_prop) return 1;
    const SmokeSupportFixture *fixture = ledge->fixture;
    if (!smokeLedgeStageActive(fixture) || s_State.ready_stage_generation != ledge->epoch) {
        return smokeLedgeFail("stage_changed");
    }

    const s32 frame = g_Vars.lvframe60;
    if (frame == ledge->last_frame) return 1;
    if (frame < ledge->last_frame) return smokeLedgeFail("simulation_reset");
    const s32 elapsed_frames = frame - ledge->last_frame;
    ledge->last_frame = frame;

    struct chrdata *chr = smokeLedgeActor(ledge->bot_slot);
    if (!chr || chr->prop != ledge->actor_prop || chr->chrnum != ledge->actor_chrnum) {
        return smokeLedgeFail("actor_changed");
    }
    if (ledge->table && !smokeLedgeTableStable(ledge)) return smokeLedgeFail("table_changed");

    const struct player *player = ledge->bot_slot < 0 ? g_Vars.players[0] : NULL;
    const f32 feet = player ? player->vv_manground : chr->manground;
    const f32 ground = player ? player->vv_ground : chr->ground;
    const f32 velocity = player ? player->bdeltapos.y : chr->fallspeed.y;
    const RoomNum floorroom = player ? player->floorroom : chr->floorroom;
    const struct coord *pos = &chr->prop->pos;
    if (!__builtin_isfinite(feet) || !__builtin_isfinite(ground) || !__builtin_isfinite(velocity)
            || !__builtin_isfinite(pos->x) || !__builtin_isfinite(pos->z)) {
        return smokeLedgeFail("nonfinite_movement");
    }
    /* B-1310, candidate 4: the speed along the approach, per frame. */
    const f32 speed_z = (pos->z - ledge->last_z) / (f32)elapsed_frames;
    ledge->last_z = pos->z;
    /* B-1310, candidate 4: a held simulant may stand anywhere at its post. */
    const f32 lane = ledge->bot_slot >= 0 && fixture->lane_half_width < SMOKE_LEDGE_BOT_POST_HALF_WIDTH
        ? SMOKE_LEDGE_BOT_POST_HALF_WIDTH : fixture->lane_half_width;
    const s32 in_lane = fabsf(pos->x - fixture->x) < lane;
    const s32 settled = in_lane && floorroom == fixture->room
        && fabsf(feet - ground) <= 2.0f && fabsf(velocity) <= 0.25f;
    const s32 on_ledge = settled && fabsf(feet - fixture->upper_y) <= 2.0f
        && pos->z >= fixture->upper_min_z && pos->z <= fixture->upper_max_z
        && (!ledge->table || smokeLedgeTableContact(ledge, pos, feet, chr->prop));

    if (!ledge->took_off) {
        ledge->approach_frames++;
        if (ledge->approach_samples < SMOKE_LEDGE_APPROACH_SAMPLES
                && ledge->approach_frames % SMOKE_LEDGE_APPROACH_EVERY == 1) {
            ledge->approach_samples++;
            smokeLedgeLogApproach(ledge, chr, frame, feet, ground, velocity, floorroom, speed_z);
        }
    }
    if (!ledge->took_off && in_lane && pos->z < fixture->edge_z
            && feet > fixture->lower_y + 4.0f && velocity > 0.25f) {
        ledge->took_off = 1;
        /* B-1310, candidate 4: the peak starts here; from 0 it hid every
         * Villa arc, whose heights are all negative. */
        ledge->peak_feet = feet;
        sysLogPrintf(LOG_NOTE,
            "%s: phase=takeoff chrnum=%d frame=%d feet=%.2f velocity=%.2f z=%.2f",
            fixture->marker, chr->chrnum, frame, feet, velocity, pos->z);
    }
    if (ledge->took_off && feet > fixture->upper_y + 2.0f && velocity < -0.25f) {
        ledge->descended = 1;
    }
    if (ledge->took_off) {
        if (feet > ledge->peak_feet) ledge->peak_feet = feet;
        if (ledge->trace_frames < SMOKE_LEDGE_TRACE_FRAMES) {
            ledge->trace_frames++;
            sysLogPrintf(LOG_NOTE,
                "%s: sample n=%d frame=%d feet=%.2f ground=%.2f velocity=%.2f x=%.1f z=%.1f room=%d settled=%d on_ledge=%d descended=%d",
                fixture->marker, ledge->trace_frames, frame, feet, ground, velocity, pos->x, pos->z,
                floorroom, settled, on_ledge, ledge->descended);
        }
    }
    if (ledge->bot_slot < 0) {
        smokeLedgeReleaseMoveAtGoal(ledge, pos->z, speed_z);
    }
    if (!ledge->walk_off) {
        if (!on_ledge && ledge->landed) return smokeLedgeFail("lost_ledge_before_walk_off");
        if (!on_ledge || !ledge->descended) return 1;
        if (!ledge->landed) {
            ledge->landed = 1;
            ledge->land_frame = frame;
            sysLogPrintf(LOG_NOTE,
                "%s: phase=land chrnum=%d frame=%d room=%d feet=%.2f ground=%.2f z=%.2f support=%s",
                fixture->marker, chr->chrnum, frame, floorroom, feet, ground, pos->z,
                ledge->table ? "table" : "room");
        }
        ledge->support_samples++;
        if (!ledge->stayed && ledge->support_samples >= 8
                && frame - ledge->land_frame >= SMOKE_LEDGE_STAY_TICKS) {
            ledge->stayed = 1;
            sysLogPrintf(LOG_NOTE,
                "%s: phase=stay chrnum=%d ticks=%d samples=%d room=%d feet=%.2f support=%s",
                fixture->marker, chr->chrnum, frame - ledge->land_frame,
                ledge->support_samples, floorroom, feet, ledge->table ? "table" : "room");
        }
        return 1;
    }

    if (!ledge->dropped && in_lane && pos->z < fixture->edge_z
            && velocity < -0.25f && feet < fixture->upper_y - 2.0f) {
        ledge->dropped = 1;
        sysLogPrintf(LOG_NOTE,
            "%s: phase=drop chrnum=%d frame=%d feet=%.2f velocity=%.2f z=%.2f",
            fixture->marker, chr->chrnum, frame, feet, velocity, pos->z);
    }
    if (!ledge->lower_landed && ledge->dropped && settled && pos->z <= fixture->lower_return_max_z
            && fabsf(feet - fixture->lower_y) <= 2.0f) {
        ledge->lower_landed = 1;
        sysLogPrintf(LOG_NOTE,
            "%s: phase=lower_land chrnum=%d frame=%d room=%d feet=%.2f ground=%.2f z=%.2f",
            fixture->marker, chr->chrnum, frame, floorroom, feet, ground, pos->z);
    }
    return 1;
}

static s32 smokeLedgeApply(SmokeSupportFixtureId fixture_id, SmokeLedgeCommand command)
{
    SmokeLedge *ledge = &s_State.ledge;
    const SmokeSupportFixture *fixture = &s_SmokeSupportFixtures[fixture_id];
    RoomNum rooms[] = { fixture->room, -1 };

    /* B-1310: once bound, a measured table fixture lives in ledge->measured;
     * the same authored fixture keeps it, by its marker. */
    if (ledge->fixture && ledge->fixture != fixture
            && (ledge->fixture != &ledge->measured || ledge->measured.marker != fixture->marker)) {
        return smokeLedgeFail("fixture_changed");
    }
    if (!ledge->fixture) ledge->fixture = fixture;
    fixture = ledge->fixture;
    if (!smokeLedgeStageActive(fixture)) return smokeLedgeFail("wrong_stage");
    if (command == SMOKE_LEDGE_PLACE_PLAYER || command == SMOKE_LEDGE_PLACE_BOT) {
        const s32 bot_slot = command == SMOKE_LEDGE_PLACE_BOT ? 0 : -1;
        if (ledge->actor_prop) return smokeLedgeFail("duplicate_placement");
        if (bot_slot >= 0 && (g_BotCount != 1 || !(g_MpSetup.options & MPOPTION_BOTJUMP))) {
            return smokeLedgeFail("simulant_setup");
        }
        struct chrdata *chr = smokeLedgeActor(bot_slot);
        if (!chr) return smokeLedgeFail("actor_missing");
        if (fixture->table_command >= 0 && !smokeLedgeBindTable(ledge, chr->prop)) return 0;
        fixture = ledge->fixture;

        struct coord start = { fixture->x, fixture->lower_y + 4.0f, fixture->start_z };
        const s32 previous_player = g_Vars.currentplayernum;
        if (bot_slot < 0) setCurrentPlayerNum(0);
        const s32 placed = chrMoveToPos(chr, &start, rooms, 0.0f, true);
        if (bot_slot < 0) setCurrentPlayerNum(previous_player);
        if (!placed || chr->prop->rooms[0] != fixture->room
                || fabsf(chr->prop->pos.x - start.x) > 1.0f
                || fabsf(chr->prop->pos.z - start.z) > 1.0f
                || fabsf(chr->ground - fixture->lower_y) > 2.0f) {
            sysLogPrintf(LOG_ERROR,
                "%s: start placed=%d room=%d ground=%.2f x=%.2f z=%.2f",
                fixture->marker, placed, chr->prop->rooms[0], chr->ground,
                chr->prop->pos.x, chr->prop->pos.z);
            return smokeLedgeFail("wrong_start_support");
        }
        ledge->actor_prop = chr->prop;
        ledge->actor_chrnum = chr->chrnum;
        ledge->bot_slot = bot_slot;
        ledge->epoch = s_State.ready_stage_generation;
        ledge->last_frame = g_Vars.lvframe60;
        ledge->last_z = chr->prop->pos.z;
        sysLogPrintf(LOG_NOTE,
            "%s: phase=place actor=%s chrnum=%d room=%d feet=%.2f x=%.2f z=%.2f",
            fixture->marker, bot_slot < 0 ? "player" : "simulant", chr->chrnum,
            fixture->room, chr->ground, chr->prop->pos.x, chr->prop->pos.z);
        if (bot_slot >= 0) {
            struct coord upper = { fixture->x, fixture->upper_y, fixture->upper_goal_z };
            botApplyHold(chr, &upper, rooms, 0.0f);
        }
        return 1;
    }

    if (!ledge->actor_prop) return smokeLedgeFail("placement_missing");
    if (command == SMOKE_LEDGE_WALK_OFF) {
        if (ledge->walk_off) return smokeLedgeFail("duplicate_walk_off");
        if (!ledge->took_off) return smokeLedgeFail("takeoff_missing");
        if (!ledge->landed) return smokeLedgeFail("ledge_landing_missing");
        if (!ledge->stayed) return smokeLedgeFail("sustained_support_missing");
        if (ledge->bot_slot >= 0) {
            struct chrdata *chr = smokeLedgeActor(ledge->bot_slot);
            struct coord lower = { fixture->x, fixture->lower_y, fixture->lower_goal_z };
            if (!chr || chr->prop != ledge->actor_prop || chr->chrnum != ledge->actor_chrnum) {
                return smokeLedgeFail("actor_changed");
            }
            botApplyHold(chr, &lower, rooms, M_BADPI);
        } else {
            /* B-1310: a smoke can only press the movement axis forward
             * (actionmapInjectStateForSmoke sets +1.0), so the player walks
             * off facing back down the approach (-Z), as the simulant's hold
             * does (M_BADPI). */
            struct player *player = g_Vars.players[0];
            if (!player || player->prop != ledge->actor_prop) return smokeLedgeFail("actor_changed");
            player->vv_theta = 180.0f;
            sysLogPrintf(LOG_NOTE, "%s: phase=turn actor=player theta=%.1f",
                fixture->marker, player->vv_theta);
        }
        ledge->walk_off = 1;
        return 1;
    }

    if (!ledge->walk_off || !ledge->dropped) return smokeLedgeFail("departure_missing");
    if (!ledge->lower_landed) return smokeLedgeFail("lower_landing_missing");
    sysLogPrintf(LOG_NOTE, "%s: result=pass actor=%s chrnum=%d",
        fixture->marker, ledge->bot_slot < 0 ? "player" : "simulant", ledge->actor_chrnum);
    return 1;
}

void smokeHarnessTick(void)
{
    if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
    if (!s_State.active || s_State.exited) return;

    u32 now = SDL_GetTicks();
    u32 real_elapsed_ms = now - s_State.start_ticks_ms;

    /* The real watchdog has priority over observation, releases, readiness,
     * and scripted events. At the exact deadline, failure is the only legal
     * outcome; in particular, an exit event cannot win this tick. */
    if (real_elapsed_ms >= (u32)s_State.timeout_ms) {
        smokeHarnessExit(1, "timeout");
        return;
    }

    if (!smokePadObserve()) return;
    if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
    smokeObserveStageReadyEpoch();
    if (!smokeGridPortalObserve()) return;
    if (!smokeLedgeObserve()) return;
    u32 script_elapsed_ms = real_elapsed_ms >= s_State.timeline_pause_ms
        ? real_elapsed_ms - s_State.timeline_pause_ms : 0;
    s32 elapsed = (s32)script_elapsed_ms;

    /* Pending-release sweep: scan all fired tap events whose release_at_ms
     * has come due.  Since events are dispatched in JSON order, we keep
     * the scan bounded to entries between [0, next_event_idx). All three
     * tap variants (key, action, mouse) share this sweep. */
    for (s32 i = 0; i < s_State.next_event_idx; i++) {
        SmokeEvent *ev = &s_State.events[i];
        if (ev->type != SMOKE_EVENT_KEY_TAP &&
            ev->type != SMOKE_EVENT_ACTION_TAP &&
            ev->type != SMOKE_EVENT_MOUSE_TAP) continue;
        if (ev->released >= 2) continue;
        if (ev->released == 1 && elapsed >= ev->release_at_ms) {
            if (ev->type == SMOKE_EVENT_KEY_TAP) {
                smokePushKey(ev->scancode, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: tap-release scancode=%d at_ms=%d",
                    ev->scancode, elapsed);
            } else if (ev->type == SMOKE_EVENT_ACTION_TAP) {
                actionmapInjectStateForSmoke(0, ev->action_id, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: action tap-release id=%d at_ms=%d",
                    ev->action_id, elapsed);
            } else { /* SMOKE_EVENT_MOUSE_TAP */
                smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 0);
                sysLogPrintf(LOG_NOTE, "SMOKE: mouse tap-release btn=%d at_ms=%d",
                    ev->mouse_button, elapsed);
            }
            ev->released = 2;
        }
    }

    /* A controller state timestamp is atomic. Yield before its successor even
     * when both became overdue during one slow frame. */
    s32 controller_group_at = -1;
    /* Dispatch any events whose at_ms has come due. */
    while (s_State.next_event_idx < s_State.event_count) {
        if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
        SmokeEvent *ev = &s_State.events[s_State.next_event_idx];
        if (ev->at_ms > elapsed) break;
        if (controller_group_at >= 0 && ev->at_ms != controller_group_at) break;

        if (ev->type == SMOKE_EVENT_CONTROLLER_ATTACH || ev->type == SMOKE_EVENT_CONTROLLER_DETACH) {
            if (smokeHasPendingTap()) {
                smokePadFail(&s_SmokePads[ev->controller_slot], "transition_pending_tap");
                return;
            }
            if (!smokePadTickTransition(ev, now)) return;
            if (nativeWritePolicyRefused()) smokeHarnessExit(2, "native_write_policy_refused");
            s_State.next_event_idx++;
            s_State.events_fired++;
            script_elapsed_ms = real_elapsed_ms >= s_State.timeline_pause_ms
                ? real_elapsed_ms - s_State.timeline_pause_ms : 0;
            elapsed = (s32)script_elapsed_ms;
            continue;
        }

        if (ev->type == SMOKE_EVENT_WAIT_UNTIL) {
            s32 wait_result = smokeTickReadinessWait(ev, now,
                real_elapsed_ms);
            if (wait_result < 0) {
                return;
            }
            if (wait_result == 0) {
                break;
            }
            s_State.next_event_idx++;
            s_State.events_fired++;
            script_elapsed_ms = real_elapsed_ms >= s_State.timeline_pause_ms
                ? real_elapsed_ms - s_State.timeline_pause_ms : 0;
            elapsed = (s32)script_elapsed_ms;
            continue;
        }

        switch (ev->type) {
        case SMOKE_EVENT_WAIT:
            sysLogPrintf(LOG_NOTE, "SMOKE: wait marker at_ms=%d", ev->at_ms);
            if (!smokeGridRetryArena()) {
                smokeHarnessExit(1, "grid_retry_failed");
                return;
            }
            break;
        case SMOKE_EVENT_MOVEMENT_LEDGE:
            if (!smokeLedgeApply(ev->ledge_fixture, ev->ledge_command)) return;
            break;
        case SMOKE_EVENT_KEY_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: press scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 1);
            break;
        case SMOKE_EVENT_KEY_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: release scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 0);
            break;
        case SMOKE_EVENT_KEY_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: tap-press scancode=%d at_ms=%d", ev->scancode, ev->at_ms);
            smokePushKey(ev->scancode, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_ACTION_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: action press id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 1);
            break;
        case SMOKE_EVENT_ACTION_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: action release id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 0);
            break;
        case SMOKE_EVENT_ACTION_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: action tap-press id=%d at_ms=%d", ev->action_id, ev->at_ms);
            actionmapInjectStateForSmoke(0, ev->action_id, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_MOUSE_PRESS:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse press btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 1);
            break;
        case SMOKE_EVENT_MOUSE_RELEASE:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse release btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 0);
            break;
        case SMOKE_EVENT_MOUSE_TAP:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse tap-press btn=%d xy=(%d,%d) at_ms=%d",
                ev->mouse_button, ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouse(ev->mouse_x, ev->mouse_y, ev->mouse_button, 1);
            ev->released = 1;
            break;
        case SMOKE_EVENT_CONTROLLER_BUTTON:
        case SMOKE_EVENT_CONTROLLER_AXIS:
            if (!smokePadWrite(ev)) return;
            controller_group_at = ev->at_ms;
            break;
        case SMOKE_EVENT_MOUSE_MOVE:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse move xy=(%d,%d) at_ms=%d",
                ev->mouse_x, ev->mouse_y, ev->at_ms);
            smokePushMouseMove(ev->mouse_x, ev->mouse_y);
            break;
        case SMOKE_EVENT_MOUSE_WHEEL:
            sysLogPrintf(LOG_NOTE, "SMOKE: mouse wheel delta=(%d,%d) at_ms=%d",
                ev->mouse_wheel_x, ev->mouse_wheel_y, ev->at_ms);
            smokePushMouseWheel(ev->mouse_wheel_x, ev->mouse_wheel_y);
            break;
        case SMOKE_EVENT_RECEIVE_PDCA_LIST:
        {
            s32 delivered = netDistribDebugReceivePdcaListForSmoke(ev->path);
            sysLogPrintf(delivered > 0 ? LOG_NOTE : LOG_WARNING,
                "SMOKE: receive_pdca_list path='%s' delivered=%d at_ms=%d",
                ev->path, delivered, ev->at_ms);
            break;
        }
        case SMOKE_EVENT_AGENT_ACTIVATE:
        case SMOKE_EVENT_AGENT_DELETE:
        {
            char before[AGENT_PROFILE_NAME_MAX];
            const s32 activate = ev->type == SMOKE_EVENT_AGENT_ACTIVATE;
            s32 result;
            snprintf(before, sizeof(before), "%s", prefsAgentGetActive());
            result = activate
                ? agentSessionActivate(ev->path)
                : agentSessionDelete(ev->path);
            sysLogPrintf(result == 0 ? LOG_NOTE : LOG_WARNING,
                "SMOKE: agent_session op=%s name='%s' result=%d active_before='%s' active_after='%s' at_ms=%d",
                activate ? "activate" : "delete", ev->path, result,
                before, prefsAgentGetActive(), ev->at_ms);
            break;
        }
        case SMOKE_EVENT_MODMGR_APPLY_RETRY_PROBE:
            if (g_MainIsBooting || !modmgrSmokeProbePreparedRetry()) {
                smokeHarnessExit(1, "modmgr_apply_retry_probe_failed");
                return;
            }
            break;
        case SMOKE_EVENT_FILE_TRANSFER_SOCKET_PROBE:
            if (!smokeFileTransferSocketProbe()) {
                smokeHarnessExit(1, "file_transfer_socket_probe_failed");
                return;
            }
            break;
        case SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE:
        case SMOKE_EVENT_CATALOG_WEAPON_RELEASE:
        {
            const s32 acquire = ev->type == SMOKE_EVENT_CATALOG_WEAPON_ACQUIRE;
            const asset_entry_t *entry = assetCatalogResolve(ev->path);
            s32 result = entry && entry->type == ASSET_WEAPON;

            if (acquire) {
                result = catalogLoadTypedAsset(ASSET_WEAPON, ev->path);
            } else if (result) {
                catalogReleaseTypedAsset(ASSET_WEAPON, ev->path);
            }
            entry = assetCatalogResolve(ev->path);
            sysLogPrintf(result ? LOG_NOTE : LOG_WARNING,
                "SMOKE: catalog_weapon_owner op=%s id='%s' result=%d state=%d ref=%d payload=%d at_ms=%d",
                acquire ? "acquire" : "release", ev->path, result,
                entry ? (s32)entry->load_state : -1,
                entry ? entry->ref_count : -1,
                entry ? (s32)entry->payload_kind : -1,
                ev->at_ms);
            break;
        }
        case SMOKE_EVENT_CATALOG_RECOVERY_PROBE:
            smokeCatalogRecoveryProbe(ev->path, ev->at_ms);
            break;
		case SMOKE_EVENT_NETWORK_RETIRE_HELD_WEAPON:
		{
			struct netclient *target = ev->client_id >= 0
				&& ev->client_id < NET_MAX_CLIENTS
				? &g_NetClients[ev->client_id] : NULL;
			struct player *player = target ? target->player : NULL;
			struct chrdata *chr = player && player->prop
				? player->prop->chr : NULL;
			struct prop *held = chr && (ev->hand == HAND_RIGHT
					|| ev->hand == HAND_LEFT)
				? chr->weapons_held[ev->hand] : NULL;
			const s32 attached = held && held->obj && held->obj->model
				&& chr && chr->model && held->parent == player->prop
				&& held->obj->model->attachedtomodel == chr->model
				&& held->obj->model->attachedtonode != NULL;
			const s32 deleting = held && held->obj
				&& (held->obj->hidden & OBJHFLAG_DELETING) != 0;
			const s32 can_regen = held && held->obj
				&& (held->obj->hidden2 & OBJH2FLAG_CANREGEN) != 0;

			if (g_NetMode != NETMODE_SERVER || !target || !target->peer
					|| target->state != CLSTATE_GAME || !target->stage_ready
					|| !player || !player->prop || !chr || !held || !held->obj
					|| held->type != PROPTYPE_WEAPON
					|| held->obj->type != OBJTYPE_WEAPON || !held->weapon
					|| held->syncid < g_NetFirstDynamicSyncId || !attached
					|| deleting || can_regen
					|| !netReconnectWorldPropShouldSerialize(0, 0)) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.NETWORK: retire_held_weapon rejected client=%d hand=%d mode=%d state=%d peer=%d stage_ready=%d player=%d chr=%d held=%d prop=%u dynamic_floor=%u attached=%d deleting=%d can_regen=%d at_ms=%d",
					ev->client_id, ev->hand, g_NetMode,
					target ? (s32)target->state : -1, target && target->peer,
					target && target->stage_ready, player != NULL, chr != NULL,
					held != NULL, held ? (unsigned)held->syncid : 0u,
					(unsigned)g_NetFirstDynamicSyncId, attached, deleting,
					can_regen, ev->at_ms);
				smokeHarnessExit(1, "network_retire_held_weapon_failed");
				return;
			}

			const u32 syncid = held->syncid;
			const u32 parent_syncid = held->parent->syncid;
			const s32 weaponnum = held->weapon->weaponnum;
			weaponDeleteFromChr(chr, ev->hand);
			const s32 serialize_now = netReconnectWorldPropShouldSerialize(
				(held->obj->hidden & OBJHFLAG_DELETING) != 0,
				(held->obj->hidden2 & OBJH2FLAG_CANREGEN) != 0);
			const s32 retirement_started = held == chr->weapons_held[ev->hand]
				&& held->syncid == syncid && held->parent == player->prop
				&& (held->obj->hidden & OBJHFLAG_DELETING) != 0
				&& !(held->obj->hidden2 & OBJH2FLAG_CANREGEN)
				&& !serialize_now;

			if (!retirement_started) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.NETWORK: retire_held_weapon transition_failed client=%d hand=%d prop=%u at_ms=%d",
					ev->client_id, ev->hand, (unsigned)syncid, ev->at_ms);
				smokeHarnessExit(1, "network_retire_held_weapon_failed");
				return;
			}
			s_State.retired_network_prop_syncid = syncid;
			s_State.retired_network_parent_syncid = parent_syncid;
			s_State.retired_network_client_id = ev->client_id;
			sysLogPrintf(LOG_NOTE,
				"SMOKE.NETWORK: retire_held_weapon client=%d hand=%d prop=%u parent=%u weapon=%d attached=1 state=deleting_nonregen serialize_now=0 production_path=weaponDeleteFromChr at_ms=%d",
				ev->client_id, ev->hand, (unsigned)syncid,
				(unsigned)parent_syncid, weaponnum, ev->at_ms);
			break;
		}
		case SMOKE_EVENT_KILL_PLAYER:
		{
			/* T-MODDING-008: kill local player 0 through the production death
			 * path, e.g. to prove a committed mod-weapon charge still releases. */
			struct player *victim = g_Vars.players[0];

			if (victim != NULL && victim->prop != NULL && !victim->isdead) {
				s32 prevplayernum = g_Vars.currentplayernum;

				setCurrentPlayerNum(0);
				playerDie(true);
				setCurrentPlayerNum(prevplayernum);
				sysLogPrintf(LOG_NOTE, "SMOKE.KILL_PLAYER: player=0 died=%d at_ms=%d",
					victim->isdead ? 1 : 0, ev->at_ms);
			} else {
				sysLogPrintf(LOG_WARNING, "SMOKE.KILL_PLAYER: player 0 unavailable or already dead at_ms=%d",
					ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_KILL_TOKEN_HOLDER:
		{
			/* B-1209: the token's carrier dies through the production damage
			 * path (as poison kills), which the host replicates; the scenario
			 * then makes a new token and announces it to clients. */
			const char *token = "none";
			const s32 slot = smokeScenarioTokenHolder(&token);

			if (slot < 0) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.KILL_TOKEN_HOLDER: no living simulant carries a scenario token here (netmode=%d running=%d) at_ms=%d",
					(s32)g_NetMode, (s32)g_Vars.normmplayerisrunning, ev->at_ms);
				smokeHarnessExit(1, "kill_token_holder_no_holder");
				return;
			} else {
				struct chrdata *chr = g_MpAllChrPtrs[slot];
				struct coord dir = {0, 0, 0};

				chrDamageByMisc(chr, 1000.0f, &dir, NULL, NULL);
				sysLogPrintf(LOG_NOTE, "SMOKE.KILL_TOKEN_HOLDER: simulant player=%d token=%s died=%d at_ms=%d",
					slot, token, chrIsDead(chr) ? 1 : 0, ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_KILL_SIMULANT:
		{
			/* T-MODDING-011 (ai-list-graphs.md): the first live simulant takes
			 * lethal damage through the ordinary damage path, so it dies,
			 * runs its dead list and respawns as in play (the simulant twin
			 * smoke's if_bot_respawning branches). */
			struct chrdata *victim = NULL;
			s32 slot;

			for (slot = 0; slot < g_BotCount && slot < MAX_BOTS; slot++) {
				struct chrdata *chr = g_MpBotChrPtrs[slot];

				if (chr != NULL && chr->prop != NULL && chr->aibot != NULL && !chrIsDead(chr)) {
					victim = chr;
					break;
				}
			}
			if (victim != NULL) {
				struct coord vector = { 0.0f, 0.0f, 1.0f };

				chrDamageByMisc(victim, 1000.0f, &vector, NULL, NULL);
				sysLogPrintf(LOG_NOTE, "SMOKE.KILL_SIMULANT: slot=%d chr=%d died=%d at_ms=%d",
					slot, victim->chrnum, chrIsDead(victim) ? 1 : 0, ev->at_ms);
			} else {
				sysLogPrintf(LOG_WARNING, "SMOKE.KILL_SIMULANT: no live simulant at_ms=%d",
					ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_USE_DOOR:
		case SMOKE_EVENT_USE_LIFT_DOOR:
		{
			/* T-MODDING-011 (object-events.md slice 4): player 0 uses the
			 * nearest door the setup placed, through propdoorInteract as the
			 * use button does, so the level program's door rule runs in play
			 * (a closed door opens; an open one closes). */
			struct doorobj *door = NULL;
			s32 doorindex = -1;
			f32 best = 0.0f;
			s32 index;

			if (g_Vars.players[0] && g_Vars.players[0]->prop) {
				const struct coord *at = &g_Vars.players[0]->prop->pos;

				for (index = 0; setupGetCmdByIndex(index) != NULL; index++) {
					struct defaultobj *obj = setupGetObjByCmdIndex(index);

					if (obj && obj->type == OBJTYPE_DOOR && obj->prop
							&& obj->prop->type == PROPTYPE_DOOR
							&& (ev->type != SMOKE_EVENT_USE_LIFT_DOOR
								|| ((obj->hidden & OBJHFLAG_LIFTDOOR)
									&& doorIsClosed((struct doorobj *)obj)))) {
						const f32 dx = obj->prop->pos.x - at->x;
						const f32 dy = obj->prop->pos.y - at->y;
						const f32 dz = obj->prop->pos.z - at->z;
						const f32 d2 = dx * dx + dy * dy + dz * dz;

						if (!door || d2 < best) {
							door = (struct doorobj *)obj;
							doorindex = index;
							best = d2;
						}
					}
				}
			}
			if (door) {
				const s32 prevplayernum = g_Vars.currentplayernum;
				const s32 mode = door->mode;
				const f32 frac = door->frac;

				setCurrentPlayerNum(0);
				propdoorInteract(door->base.prop);
				setCurrentPlayerNum(prevplayernum);
				sysLogPrintf(LOG_NOTE,
					"SMOKE.%s: object=setup_%04d distance=%.0f mode=%d->%d frac=%.2f at_ms=%d",
					ev->type == SMOKE_EVENT_USE_LIFT_DOOR ? "USE_LIFT_DOOR" : "USE_DOOR",
					doorindex, sqrtf(best), mode, door->mode, frac, ev->at_ms);
			} else {
				sysLogPrintf(LOG_WARNING, "SMOKE.%s: no door at_ms=%d",
					ev->type == SMOKE_EVENT_USE_LIFT_DOOR ? "USE_LIFT_DOOR" : "USE_DOOR", ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_GIVE_WEAPON:
		{
			/* projectile-events.md slice 1: player 0 gets the weapon with
			 * full ammunition for both functions and draws it. */
			catalog_weapon_result_t given;
			s32 ok = 0;

			memset(&given, 0, sizeof(given));
			if (g_Vars.players[0] && g_Vars.players[0]->prop
					&& catalogResolveWeapon(ev->path, &given) && given.weapon_num > 0) {
				const s32 prevplayernum = g_Vars.currentplayernum;
				s32 func;

				setCurrentPlayerNum(0);
				invGiveSingleWeapon(given.weapon_num);
				for (func = FUNC_PRIMARY; func <= FUNC_SECONDARY; func++) {
					const s32 capacity = bgunGetAmmoCapacityForWeapon(given.weapon_num, func);

					if (capacity > 0) {
						bgunSetAmmoQtyForWeapon(given.weapon_num, func, capacity);
					}
				}
				bgunEquipWeapon(given.weapon_num);
				setCurrentPlayerNum(prevplayernum);
				ok = 1;
			}
			sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
				"SMOKE.GIVE_WEAPON: weapon=%s weaponnum=%d result=%d at_ms=%d",
				ev->path, ok ? given.weapon_num : -1, ok, ev->at_ms);
			break;
		}
		case SMOKE_EVENT_TOUCH_PICKUP:
		{
			/* T-MODDING-011 (object-events.md slice 4): player 0 touches the
			 * nearest weapon pickup the setup placed, with the decision
			 * objTestForPickup makes once the player stands on it and the
			 * tick operation the prop loop then applies, so the level
			 * program's pickup rule runs in play. */
			struct defaultobj *pickup = NULL;
			s32 pickupindex = -1;
			f32 best = 0.0f;
			s32 index;

			if (g_Vars.players[0] && g_Vars.players[0]->prop) {
				const struct coord *at = &g_Vars.players[0]->prop->pos;

				for (index = 0; setupGetCmdByIndex(index) != NULL; index++) {
					struct defaultobj *obj = setupGetObjByCmdIndex(index);

					if (obj && obj->type == OBJTYPE_WEAPON && obj->prop
							&& obj->prop->type == PROPTYPE_WEAPON
							&& obj->prop->parent == NULL
							&& obj->prop->timetoregen <= 0
							&& (obj->hidden & OBJHFLAG_DELETING) == 0) {
						const f32 dx = obj->prop->pos.x - at->x;
						const f32 dy = obj->prop->pos.y - at->y;
						const f32 dz = obj->prop->pos.z - at->z;
						const f32 d2 = dx * dx + dy * dy + dz * dz;

						if (!pickup || d2 < best) {
							pickup = obj;
							pickupindex = index;
							best = d2;
						}
					}
				}
			}
			if (pickup) {
				const s32 prevplayernum = g_Vars.currentplayernum;
				struct prop *prop = pickup->prop;
				s32 op;

				setCurrentPlayerNum(0);
				op = objPickupTouchForSmoke(prop);
				propExecuteTickOperation(prop, op);
				setCurrentPlayerNum(prevplayernum);
				sysLogPrintf(LOG_NOTE,
					"SMOKE.TOUCH_PICKUP: object=setup_%04d distance=%.0f op=%d at_ms=%d",
					pickupindex, sqrtf(best), op, ev->at_ms);
			} else {
				sysLogPrintf(LOG_WARNING, "SMOKE.TOUCH_PICKUP: no pickup at_ms=%d", ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_DAMAGE_GLASS:
		{
			/* T-MODDING-011 (object-events.md slice 5): player 0's shot
			 * breaks the nearest unbroken glass the setup placed, through
			 * objDamage as a shot does, so the level program's glass rule
			 * runs in play. */
			struct defaultobj *glass = NULL;
			s32 glassindex = -1;
			f32 best = 0.0f;
			s32 index;

			if (g_Vars.players[0] && g_Vars.players[0]->prop) {
				const struct coord *at = &g_Vars.players[0]->prop->pos;

				for (index = 0; setupGetCmdByIndex(index) != NULL; index++) {
					struct defaultobj *obj = setupGetObjByCmdIndex(index);

					if (obj && (obj->type == OBJTYPE_GLASS || obj->type == OBJTYPE_TINTEDGLASS)
							&& obj->prop && (obj->flags & OBJFLAG_INVINCIBLE) == 0
							&& (obj->hidden & OBJHFLAG_DELETING) == 0
							&& (obj->hidden2 & OBJH2FLAG_DESTROYED) == 0) {
						const f32 dx = obj->prop->pos.x - at->x;
						const f32 dy = obj->prop->pos.y - at->y;
						const f32 dz = obj->prop->pos.z - at->z;
						const f32 d2 = dx * dx + dy * dy + dz * dz;

						if (!glass || d2 < best) {
							glass = obj;
							glassindex = index;
							best = d2;
						}
					}
				}
			}
			if (glass) {
				const s32 prevplayernum = g_Vars.currentplayernum;
				struct coord pos = glass->prop->pos;

				setCurrentPlayerNum(0);
				objDamage(glass, 1000.0f, &pos, WEAPON_FALCON2, 0);
				setCurrentPlayerNum(prevplayernum);
				sysLogPrintf(LOG_NOTE,
					"SMOKE.DAMAGE_GLASS: object=setup_%04d distance=%.0f broken=%d at_ms=%d",
					glassindex, sqrtf(best), (glass->hidden2 & OBJH2FLAG_DESTROYED) ? 1 : 0, ev->at_ms);
			} else {
				sysLogPrintf(LOG_WARNING, "SMOKE.DAMAGE_GLASS: no glass at_ms=%d", ev->at_ms);
			}
			break;
		}
		case SMOKE_EVENT_NETWORK_ASSERT_RETIRED_PROP_ABSENT:
		{
			struct prop *found = NULL;
			const u32 syncid = s_State.retired_network_prop_syncid;

			if (g_NetMode == NETMODE_SERVER && syncid != 0
					&& g_Vars.props && g_Vars.maxprops > 0) {
				for (s32 i = 0; i < g_Vars.maxprops; ++i) {
					if (g_Vars.props[i].syncid == syncid) {
						found = &g_Vars.props[i];
						break;
					}
				}
			}
			if (g_NetMode != NETMODE_SERVER || syncid == 0
					|| !g_Vars.props || g_Vars.maxprops <= 0 || found) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.NETWORK: retired_prop_absent rejected client=%d prop=%u parent=%u mode=%d pool=%d present=%d type=%d at_ms=%d",
					s_State.retired_network_client_id, (unsigned)syncid,
					(unsigned)s_State.retired_network_parent_syncid, g_NetMode,
					g_Vars.props != NULL && g_Vars.maxprops > 0, found != NULL,
					found ? (s32)found->type : -1, ev->at_ms);
				smokeHarnessExit(1,
					"network_assert_retired_prop_absent_failed");
				return;
			}
			sysLogPrintf(LOG_NOTE,
				"SMOKE.NETWORK: retired_prop_absent client=%d prop=%u parent=%u authoritative_present=0 read_only=1 production_cleanup=1 at_ms=%d",
				s_State.retired_network_client_id, (unsigned)syncid,
				(unsigned)s_State.retired_network_parent_syncid, ev->at_ms);
			break;
		}
        case SMOKE_EVENT_NETWORK_TIMEOUT_CLIENT:
        {
			struct netclient *target = ev->client_id >= 0
				&& ev->client_id < NET_MAX_CLIENTS
				? &g_NetClients[ev->client_id] : NULL;
			if (g_NetMode != NETMODE_SERVER || !target || !target->peer
					|| target->state < CLSTATE_GAME || !target->stage_ready) {
				sysLogPrintf(LOG_ERROR,
					"SMOKE.NETWORK: timeout_client rejected client=%d mode=%d state=%d peer=%d stage_ready=%d at_ms=%d",
					ev->client_id, g_NetMode, target ? (s32)target->state : -1,
					target && target->peer, target && target->stage_ready,
					ev->at_ms);
				smokeHarnessExit(1, "network_timeout_client_failed");
				return;
			}
			sysLogPrintf(LOG_NOTE,
				"SMOKE.NETWORK: timeout_client client=%d state=%u stage_ready=1 at_ms=%d production_path=1",
				ev->client_id, (unsigned)target->state, ev->at_ms);
			netServerKick(target, DISCONNECT_TIMEOUT);
			break;
		}
        case SMOKE_EVENT_NETWORK_RECONNECT:
		{
			const s32 rc = netClientReconnect();
			sysLogPrintf(rc == 0 ? LOG_NOTE : LOG_ERROR,
				"SMOKE.NETWORK: reconnect rc=%d at_ms=%d production_path=1",
				rc, ev->at_ms);
			if (rc != 0) {
				smokeHarnessExit(1, "network_reconnect_failed");
				return;
			}
			break;
		}
        case SMOKE_EVENT_SCREENSHOT:
            sysLogPrintf(LOG_NOTE, "SMOKE: screenshot at_ms=%d -> %s", ev->at_ms, ev->path);
            gfxRequestSmokeScreenshot(ev->path);
            break;
        case SMOKE_EVENT_EXIT:
            if (sysArgCheck("--debug-grid-portal-witness") && !s_State.grid_portal.complete) {
                smokeGridPortalFail("witness_incomplete");
                return;
            }
            sysLogPrintf(LOG_NOTE, "SMOKE: scripted exit at_ms=%d", ev->at_ms);
            /* object-events.md: a stage the smoke leaves before it ends
             * still logs its object event totals. */
            scenarioSourceGraphObjectEventsReport("smoke_exit");
            projectileEventsReport("smoke_exit");
            modeEventsReport("smoke_exit");
            s_State.next_event_idx++;
            s_State.events_fired++;
            smokeHarnessExit(0, "scripted_exit");
            return;
        case SMOKE_EVENT_UNCLEAN_EXIT:
            s_State.next_event_idx++;
            s_State.events_fired++;
            s_State.exited = 1;
            sysLogPrintf(LOG_NOTE,
                "SMOKE: result=unclean_exit scenario=%s elapsed_ms=%d events_fired=%d/%d code=0",
                s_State.scenario_name, elapsed, s_State.events_fired,
                s_State.event_count);
            fflush(NULL);
            _Exit(0);
        default:
            break;
        }
        s_State.next_event_idx++;
        s_State.events_fired++;
    }
    if (controller_group_at >= 0) {
        SDL_JoystickUpdate();
        /* Next ordinary tick verifies readback after normal SDL/ActionMap/UI
         * dispatch. Never dispatch a second timestamp's controller state here. */
    }
}

void smokeHarnessExit(int code, const char *reason)
{
    if (nativeWritePolicyRefused()) {
        code = 2;
        reason = "native_write_policy_refused";
    }
    u32 now;
    s32 elapsed;

    if (!s_State.active) {
        exit(code);
    }
    if (s_State.exited) return;

    now = SDL_GetTicks();
    elapsed = (s32)(now - s_State.start_ticks_ms);
    /* The real watchdog can preempt the wait policy before its next tick.
     * Release actual runtime ownership here as a final idempotent backstop. */
    if (s_State.wait_assist_injected) {
        if (s_State.waiting_event_idx < 0
                || s_State.waiting_event_idx >= s_State.event_count
                || !smokeReleaseWaitAssist(
                    &s_State.events[s_State.waiting_event_idx],
                    reason ? reason : "harness_exit",
                    now - s_State.wait_started_ticks_ms)) {
            sysLogPrintf(LOG_ERROR,
                "SMOKE.WAIT.ASSIST: terminal cleanup failed reason=%s",
                reason ? reason : "unknown");
            code = 1;
            reason = "wait_assist_cleanup_failed";
        }
    }
    if (!smokePadCleanup()) {
        code = 1;
        reason = "controller_cleanup_failed";
    }
    s_State.exited = 1;

    sysLogPrintf(code == 0 ? LOG_NOTE : LOG_WARNING,
        "SMOKE: result=%s scenario=%s elapsed_ms=%d events_fired=%d/%d code=%d",
        reason ? reason : "unknown",
        s_State.scenario_name,
        elapsed,
        s_State.events_fired,
        s_State.event_count,
        code);

    /* Give the log layer one last flush opportunity.  sysLogPrintf is
     * line-buffered to the file; the next call after exit() would lose
     * any in-flight buffer if we exited too eagerly. */
    fflush(NULL);

    exit(code);
}
