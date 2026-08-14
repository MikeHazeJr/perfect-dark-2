/*
 * menugraph.c -- Action-map menu transition graph.
 */

#include "menugraph.h"

#include <stddef.h>
#include <string.h>

#include "game/menu.h"
#include "scene.h"
#include "system.h"

#define EDGE_PUSH(id_, action_, label_, target_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_PUSH_MENU, { .push_target = (target_) } }

#define EDGE_PUSH_ANY(id_, action_, label_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_PUSH_MENU, { .push_target = MENU_TYPE_NONE } }

#define EDGE_POP(id_, action_, label_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_POP_TO_PARENT, { .push_target = MENU_TYPE_NONE } }

#define EDGE_POP_ROOT(id_, action_, label_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_POP_TO_ROOT, { .push_target = MENU_TYPE_NONE } }

#define EDGE_SWITCH(id_, action_, label_, target_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_SWITCH_SIBLING, { .sibling_target = (target_) } }

#define EDGE_SCENE(id_, action_, label_, event_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_SCENE_EVENT, { .scene_event = (event_) } }

#define EDGE_NETWORK(id_, action_, label_, op_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_NETWORK_OP, { .network_op = (op_) } }

#define EDGE_PROCESS(id_, action_, label_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_PROCESS_EXIT, { .push_target = MENU_TYPE_NONE } }

#define EDGE_LOCAL(id_, action_, label_, op_) \
    { (id_), (action_), (label_), MENU_GRAPH_DEST_LOCAL_OP, { .local_op = (op_) } }

static const MenuGraphEdge s_MainMenuEdges[] = {
    EDGE_PUSH("solo_play", ACTION_MENU_ACCEPT, "Play", MENU_TYPE_MAIN_SOLO_VIEW),
    EDGE_PUSH("social", ACTION_MENU_ACCEPT, "Social", MENU_TYPE_SOCIAL_SHELL),
    EDGE_PUSH("public_mods", ACTION_MENU_ACCEPT, "Public Mods", MENU_TYPE_SOCIAL_SHELL),
    EDGE_PUSH("change_agent", ACTION_MENU_ACCEPT, "Change Agent", MENU_TYPE_AGENT_SELECT),
    EDGE_PUSH("settings", ACTION_MENU_ACCEPT, "Settings", MENU_TYPE_MAIN_SETTINGS_VIEW),
    EDGE_PUSH("modding", ACTION_MENU_ACCEPT, "Mods", MENU_TYPE_MAIN_MODDING_VIEW),
    EDGE_PUSH("cheats", ACTION_MENU_ACCEPT, "Cheats", MENU_TYPE_CHEATS),
    EDGE_PUSH("stats", ACTION_MENU_ACCEPT, "Stats", MENU_TYPE_MAIN_STATS_VIEW),
    EDGE_PUSH("grid", ACTION_MENU_ACCEPT, "The Grid", MENU_TYPE_GRID_SUBMENU),
    EDGE_PROCESS("quit", ACTION_MENU_ACCEPT, "Quit Game"),
    EDGE_POP_ROOT("close", ACTION_MENU_CANCEL, "Close Main Menu"),
};

static const MenuGraphEdge s_MainSoloEdges[] = {
    EDGE_PUSH("solo_missions", ACTION_MENU_ACCEPT, "Solo Missions", MENU_TYPE_SOLO_MISSION),
    EDGE_PUSH("combat_simulator", ACTION_MENU_ACCEPT, "Combat Simulator", MENU_TYPE_ROOM),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MainSettingsEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_CiOptionsEdges[] = {
    EDGE_POP("close", ACTION_MENU_CANCEL, "Close"),
};

static const MenuGraphEdge s_CinemaEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MainModdingEdges[] = {
    EDGE_PUSH("open_hub", ACTION_MENU_ACCEPT, "Open Modding Hub", MENU_TYPE_MODDING_HUB),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MainStatsEdges[] = {
    EDGE_PUSH("open_panel", ACTION_MENU_ACCEPT, "Stats Panel", MENU_TYPE_STATS_PANEL),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_GridEdges[] = {
    EDGE_SCENE("enter", ACTION_MENU_ACCEPT, "Enter The Grid", SCENE_EVENT_GAMEPLAY_START),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_SoloMissionEdges[] = {
    EDGE_SCENE("start", ACTION_MENU_ACCEPT, "Start Mission", SCENE_EVENT_GAMEPLAY_START),
    EDGE_SCENE("restart", ACTION_MENU_ACCEPT, "Restart Mission", SCENE_EVENT_GAMEPLAY_START),
    EDGE_PUSH_ANY("pd_mode_settings", ACTION_MENU_ACCEPT, "PD Mode Settings"),
    EDGE_PUSH_ANY("accept_mission", ACTION_MENU_ACCEPT, "Accept Mission"),
    EDGE_PUSH_ANY("coop_options", ACTION_MENU_ACCEPT, "Co-op Options"),
    EDGE_PUSH_ANY("anti_options", ACTION_MENU_ACCEPT, "Counter-Op Options"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_RoomEdges[] = {
    EDGE_SCENE("start_match", ACTION_MENU_ACCEPT, "Start Match", SCENE_EVENT_GAMEPLAY_START),
    EDGE_PUSH("team_setup", ACTION_MENU_ACCEPT, "Team Setup", MENU_TYPE_MP_TEAM_SETUP),
    EDGE_PUSH("select_music", ACTION_MENU_ACCEPT, "Select Music", MENU_TYPE_MP_TUNES),
    EDGE_NETWORK("leave_room", ACTION_MENU_CANCEL, "Leave Room", "leave_room"),
};

static const MenuGraphEdge s_EndscreenSoloEdges[] = {
    EDGE_SCENE("continue", ACTION_MENU_ACCEPT, "Continue", SCENE_EVENT_GAMEPLAY_START),
    EDGE_SCENE("retry", ACTION_MENU_ACCEPT, "Retry", SCENE_EVENT_GAMEPLAY_START),
    EDGE_SCENE("main_menu", ACTION_MENU_CANCEL, "Main Menu", SCENE_EVENT_STAGE_TEARDOWN),
};

static const MenuGraphEdge s_EndscreenMpEdges[] = {
    EDGE_SCENE("continue", ACTION_MENU_ACCEPT, "Continue", SCENE_EVENT_STAGE_TEARDOWN),
    EDGE_NETWORK("disconnect", ACTION_MENU_CANCEL, "Disconnect", "disconnect"),
    EDGE_SCENE("quit", ACTION_MENU_CANCEL, "Quit", SCENE_EVENT_STAGE_TEARDOWN),
};

static const MenuGraphEdge s_PauseEdges[] = {
    EDGE_POP("resume", ACTION_MENU_CANCEL, "Resume"),
    EDGE_SCENE("end_mission", ACTION_MENU_ACCEPT, "End Mission", SCENE_EVENT_STAGE_TEARDOWN),
};

static const MenuGraphEdge s_SoloMissionPauseEdges[] = {
    EDGE_POP("resume", ACTION_MENU_CANCEL, "Resume"),
    EDGE_SCENE("restart", ACTION_MENU_ACCEPT, "Restart Mission", SCENE_EVENT_GAMEPLAY_START),
    EDGE_SWITCH("inventory", ACTION_MENU_ACCEPT, "Inventory", MENU_TYPE_SOLO_INVENTORY),
    EDGE_SWITCH("settings", ACTION_MENU_ACCEPT, "Settings", MENU_TYPE_SOLO_OPTIONS),
    EDGE_PUSH("abort", ACTION_MENU_ACCEPT, "Abort Mission", MENU_TYPE_WARNING_MODAL),
};

static const MenuGraphEdge s_SoloInventoryEdges[] = {
    EDGE_SWITCH("back", ACTION_MENU_CANCEL, "Back", MENU_TYPE_SOLO_MISSION_PAUSE),
};

static const MenuGraphEdge s_SoloOptionsEdges[] = {
    EDGE_SWITCH("back", ACTION_MENU_CANCEL, "Back", MENU_TYPE_SOLO_MISSION_PAUSE),
    EDGE_POP("close", ACTION_MENU_CANCEL, "Close"),
};

static const MenuGraphEdge s_MpPauseEdges[] = {
    EDGE_POP("resume", ACTION_MENU_CANCEL, "Resume"),
    EDGE_PUSH("end_game", ACTION_MENU_ACCEPT, "End Game", MENU_TYPE_WARNING_MODAL),
    EDGE_PUSH("control_style", ACTION_MENU_ACCEPT, "Control Style", MENU_TYPE_SOLO_OPTIONS),
    EDGE_NETWORK("disconnect", ACTION_MENU_ACCEPT, "Disconnect", "disconnect"),
};

static const MenuGraphEdge s_SocialLobbyEdges[] = {
    EDGE_NETWORK("create_room", ACTION_MENU_ACCEPT, "Create Room", "server"),
    EDGE_NETWORK("disconnect", ACTION_MENU_CANCEL, "Disconnect", "disconnect"),
};

static const MenuGraphEdge s_SocialShellEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_NetworkEdges[] = {
    EDGE_NETWORK("host", ACTION_MENU_ACCEPT, "Host Game", "server"),
    EDGE_NETWORK("join", ACTION_MENU_ACCEPT, "Join Game", "client"),
    EDGE_NETWORK("reconnect", ACTION_MENU_ACCEPT, "Reconnect", "client_reconnect"),
    EDGE_NETWORK("disconnect", ACTION_MENU_ACCEPT, "Disconnect", "disconnect"),
    EDGE_PUSH("joining", ACTION_MENU_ACCEPT, "Joining", MENU_TYPE_NETWORK_JOINING),
    EDGE_POP("host_started", ACTION_MENU_ACCEPT, "Host Started"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_AgentSelectEdges[] = {
    EDGE_LOCAL("load", ACTION_MENU_ACCEPT, "Load Agent", "load_agent"),
    EDGE_PUSH("create", ACTION_MENU_ACCEPT, "Create Agent", MENU_TYPE_AGENT_CREATE),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_AgentCreateEdges[] = {
    EDGE_POP("save", ACTION_MENU_ACCEPT, "Save Agent"),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
};

static const MenuGraphEdge s_CheatsEdges[] = {
    EDGE_PUSH("unlock_all", ACTION_MENU_ACCEPT, "Unlock Everything", MENU_TYPE_WARNING_MODAL),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
    EDGE_POP("close", ACTION_MENU_CANCEL, "Close"),
    EDGE_POP("redirect", ACTION_MENU_CANCEL, "Redirect"),
    EDGE_POP("warning_close", ACTION_MENU_CANCEL, "Close Warning"),
};

static const MenuGraphEdge s_ChallengesEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpTeamSetupEdges[] = {
    EDGE_POP("done", ACTION_MENU_ACCEPT, "Done"),
};

static const MenuGraphEdge s_MpPlayerConfigEdges[] = {
    EDGE_POP("close", ACTION_MENU_CANCEL, "Close"),
};

static const MenuGraphEdge s_MpSetupEdges[] = {
    EDGE_PUSH_ANY("more_options", ACTION_MENU_ACCEPT, "More Options"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpAdvancedEdges[] = {
    EDGE_PUSH("scenario", ACTION_MENU_ACCEPT, "Scenario", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("quick_team_scenario", ACTION_MENU_ACCEPT, "Quick Team Scenario", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("arena", ACTION_MENU_ACCEPT, "Arena", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("weapons", ACTION_MENU_ACCEPT, "Weapons", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("quick_team_weapons", ACTION_MENU_ACCEPT, "Quick Team Weapons", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("limits", ACTION_MENU_ACCEPT, "Limits", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("handicaps", ACTION_MENU_ACCEPT, "Player Handicaps", MENU_TYPE_MP_SETTINGS),
    EDGE_PUSH("simulants", ACTION_MENU_ACCEPT, "Simulants", MENU_TYPE_MP_BOT_SETUP),
    EDGE_PUSH("teams", ACTION_MENU_ACCEPT, "Teams", MENU_TYPE_MP_TEAM_SETUP),
    EDGE_PUSH_ANY("manage_settings", ACTION_MENU_ACCEPT, "Manage Settings"),
    EDGE_PUSH("load_settings", ACTION_MENU_ACCEPT, "Load Settings", MENU_TYPE_MP_PLAYER_CONFIG),
    EDGE_PUSH("start_game", ACTION_MENU_ACCEPT, "Start Game", MENU_TYPE_MP_SETUP),
    EDGE_PUSH("load_player", ACTION_MENU_ACCEPT, "Load Player", MENU_TYPE_MP_PLAYER_CONFIG),
    EDGE_PUSH_ANY("player_settings", ACTION_MENU_ACCEPT, "Player Settings"),
    EDGE_PUSH("drop_out", ACTION_MENU_ACCEPT, "Drop Out", MENU_TYPE_MP_PAUSE),
    EDGE_PUSH("soundtrack", ACTION_MENU_ACCEPT, "Soundtrack", MENU_TYPE_MP_SOUNDTRACK),
    EDGE_PUSH("team_names", ACTION_MENU_ACCEPT, "Team Names", MENU_TYPE_MP_TEAMNAMES),
    EDGE_PUSH_ANY("abort_game", ACTION_MENU_ACCEPT, "Abort Game"),
    EDGE_PUSH("player_name", ACTION_MENU_ACCEPT, "Name", MENU_TYPE_MP_PLAYER_CONFIG),
    EDGE_PUSH_ANY("character", ACTION_MENU_ACCEPT, "Character"),
    EDGE_PUSH("control", ACTION_MENU_ACCEPT, "Control", MENU_TYPE_MP_PAUSE),
    EDGE_PUSH("player_options", ACTION_MENU_ACCEPT, "Player Options", MENU_TYPE_MP_PLAYER_CONFIG),
    EDGE_PUSH("statistics", ACTION_MENU_ACCEPT, "Statistics", MENU_TYPE_MP_PLAYER_CONFIG),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpBotSetupEdges[] = {
    EDGE_PUSH_ANY("character", ACTION_MENU_ACCEPT, "Character"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpSettingsEdges[] = {
    EDGE_POP("done", ACTION_MENU_ACCEPT, "Done"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpSoundtrackEdges[] = {
    EDGE_PUSH("select_music", ACTION_MENU_ACCEPT, "Select Music", MENU_TYPE_MP_TUNES),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpTunesEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpTeamNamesEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_TrainingEdges[] = {
    EDGE_PUSH_ANY("bio_profile", ACTION_MENU_ACCEPT, "Bio Profile"),
    EDGE_PUSH_ANY("bio_text", ACTION_MENU_ACCEPT, "Bio Text"),
    EDGE_PUSH_ANY("hangar_location", ACTION_MENU_ACCEPT, "Hangar Location"),
    EDGE_PUSH_ANY("hangar_vehicle", ACTION_MENU_ACCEPT, "Hangar Vehicle"),
    EDGE_PUSH_ANY("hangar_holograph", ACTION_MENU_ACCEPT, "Hangar Holograph"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
    EDGE_POP("close", ACTION_MENU_CANCEL, "Close"),
};

static const MenuGraphEdge s_FrWeaponListEdges[] = {
    EDGE_PUSH("difficulty", ACTION_MENU_ACCEPT, "Difficulty", MENU_TYPE_FR_DIFFICULTY),
    EDGE_PUSH("info", ACTION_MENU_ACCEPT, "Training Info", MENU_TYPE_FR_INFO),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_FrDifficultyEdges[] = {
    EDGE_PUSH("start", ACTION_MENU_ACCEPT, "Start Firing Range", MENU_TYPE_FR_INFO),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
};

static const MenuGraphEdge s_FrInfoEdges[] = {
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
    EDGE_POP("abort", ACTION_MENU_CANCEL, "Abort"),
};

static const MenuGraphEdge s_FrResultEdges[] = {
    EDGE_POP("continue", ACTION_MENU_ACCEPT, "Continue"),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_DtListEdges[] = {
    EDGE_PUSH("details", ACTION_MENU_ACCEPT, "Details", MENU_TYPE_DT_DETAILS),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_DtDetailsEdges[] = {
    EDGE_POP("begin", ACTION_MENU_ACCEPT, "Begin"),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
};

static const MenuGraphEdge s_DtResultEdges[] = {
    EDGE_POP("continue", ACTION_MENU_ACCEPT, "Continue"),
};

static const MenuGraphEdge s_HtListEdges[] = {
    EDGE_PUSH("details", ACTION_MENU_ACCEPT, "Details", MENU_TYPE_HT_DETAILS),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_HtDetailsEdges[] = {
    EDGE_POP("begin", ACTION_MENU_ACCEPT, "Begin"),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
};

static const MenuGraphEdge s_HtResultEdges[] = {
    EDGE_POP("continue", ACTION_MENU_ACCEPT, "Continue"),
};

static const MenuGraphEdge s_WarningModalEdges[] = {
    EDGE_POP("confirm", ACTION_MENU_ACCEPT, "Confirm"),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
};

#define NODE(type_, name_, edges_) \
    { (type_), (name_), (edges_), (s32)(sizeof(edges_) / sizeof((edges_)[0])) }

static const MenuGraphNode s_Nodes[] = {
    NODE(MENU_TYPE_MAIN_MENU, "main_menu", s_MainMenuEdges),
    NODE(MENU_TYPE_MAIN_SOLO_VIEW, "main_solo_view", s_MainSoloEdges),
    NODE(MENU_TYPE_MAIN_SETTINGS_VIEW, "main_settings_view", s_MainSettingsEdges),
    NODE(MENU_TYPE_CI_OPTIONS, "ci_options", s_CiOptionsEdges),
    NODE(MENU_TYPE_MAIN_MODDING_VIEW, "main_modding_view", s_MainModdingEdges),
    NODE(MENU_TYPE_MAIN_STATS_VIEW, "main_stats_view", s_MainStatsEdges),
    NODE(MENU_TYPE_GRID_SUBMENU, "grid_submenu", s_GridEdges),
    NODE(MENU_TYPE_CINEMA, "cinema", s_CinemaEdges),
    NODE(MENU_TYPE_SOLO_MISSION, "solo_mission", s_SoloMissionEdges),
    NODE(MENU_TYPE_ROOM, "room", s_RoomEdges),
    NODE(MENU_TYPE_ENDSCREEN_SOLO, "endscreen_solo", s_EndscreenSoloEdges),
    NODE(MENU_TYPE_ENDSCREEN_MP, "endscreen_mp", s_EndscreenMpEdges),
    NODE(MENU_TYPE_PAUSE_MENU, "pause_menu", s_PauseEdges),
    NODE(MENU_TYPE_MP_PAUSE, "mp_pause", s_MpPauseEdges),
    NODE(MENU_TYPE_SOLO_MISSION_PAUSE, "solo_mission_pause", s_SoloMissionPauseEdges),
    NODE(MENU_TYPE_SOLO_INVENTORY, "solo_inventory", s_SoloInventoryEdges),
    NODE(MENU_TYPE_SOLO_OPTIONS, "solo_options", s_SoloOptionsEdges),
    NODE(MENU_TYPE_SOCIAL_LOBBY, "social_lobby", s_SocialLobbyEdges),
    NODE(MENU_TYPE_SOCIAL_SHELL, "social_shell", s_SocialShellEdges),
    NODE(MENU_TYPE_NETWORK, "network", s_NetworkEdges),
    NODE(MENU_TYPE_AGENT_SELECT, "agent_select", s_AgentSelectEdges),
    NODE(MENU_TYPE_AGENT_CREATE, "agent_create", s_AgentCreateEdges),
    NODE(MENU_TYPE_CHEATS, "cheats", s_CheatsEdges),
    NODE(MENU_TYPE_CHALLENGES, "challenges", s_ChallengesEdges),
    NODE(MENU_TYPE_MP_TEAM_SETUP, "mp_team_setup", s_MpTeamSetupEdges),
    NODE(MENU_TYPE_MP_PLAYER_CONFIG, "mp_player_config", s_MpPlayerConfigEdges),
    NODE(MENU_TYPE_MP_SETUP, "mp_setup", s_MpSetupEdges),
    NODE(MENU_TYPE_MP_SETTINGS, "mp_settings", s_MpSettingsEdges),
    NODE(MENU_TYPE_MP_SOUNDTRACK, "mp_soundtrack", s_MpSoundtrackEdges),
    NODE(MENU_TYPE_MP_TUNES, "mp_tunes", s_MpTunesEdges),
    NODE(MENU_TYPE_MP_TEAMNAMES, "mp_teamnames", s_MpTeamNamesEdges),
    NODE(MENU_TYPE_MP_ADVANCED, "mp_advanced", s_MpAdvancedEdges),
    NODE(MENU_TYPE_MP_BOT_SETUP, "mp_bot_setup", s_MpBotSetupEdges),
    NODE(MENU_TYPE_TRAINING, "training", s_TrainingEdges),
    NODE(MENU_TYPE_FR_WEAPON_LIST, "fr_weapon_list", s_FrWeaponListEdges),
    NODE(MENU_TYPE_FR_DIFFICULTY, "fr_difficulty", s_FrDifficultyEdges),
    NODE(MENU_TYPE_FR_INFO, "fr_info", s_FrInfoEdges),
    NODE(MENU_TYPE_FR_RESULT, "fr_result", s_FrResultEdges),
    NODE(MENU_TYPE_DT_LIST, "dt_list", s_DtListEdges),
    NODE(MENU_TYPE_DT_DETAILS, "dt_details", s_DtDetailsEdges),
    NODE(MENU_TYPE_DT_RESULT, "dt_result", s_DtResultEdges),
    NODE(MENU_TYPE_HT_LIST, "ht_list", s_HtListEdges),
    NODE(MENU_TYPE_HT_DETAILS, "ht_details", s_HtDetailsEdges),
    NODE(MENU_TYPE_HT_RESULT, "ht_result", s_HtResultEdges),
    NODE(MENU_TYPE_WARNING_MODAL, "warning_modal", s_WarningModalEdges),
};

const char *menuGraphDestKindName(MenuGraphDestKind kind)
{
    switch (kind) {
    case MENU_GRAPH_DEST_NONE: return "none";
    case MENU_GRAPH_DEST_PUSH_MENU: return "push_menu";
    case MENU_GRAPH_DEST_POP_TO_PARENT: return "pop_to_parent";
    case MENU_GRAPH_DEST_POP_TO_ROOT: return "pop_to_root";
    case MENU_GRAPH_DEST_SWITCH_SIBLING: return "switch_sibling";
    case MENU_GRAPH_DEST_SCENE_EVENT: return "scene_event";
    case MENU_GRAPH_DEST_STAGE_CHANGE: return "stage_change";
    case MENU_GRAPH_DEST_NETWORK_OP: return "network_op";
    case MENU_GRAPH_DEST_PROCESS_EXIT: return "process_exit";
    case MENU_GRAPH_DEST_LOCAL_OP: return "local_op";
    default: return "unknown";
    }
}

s32 menuGraphNodeCount(void)
{
    return (s32)(sizeof(s_Nodes) / sizeof(s_Nodes[0]));
}

const MenuGraphNode *menuGraphNodeAt(s32 index)
{
    if (index < 0 || index >= menuGraphNodeCount()) {
        return NULL;
    }
    return &s_Nodes[index];
}

const MenuGraphNode *menuGraphNode(menu_type_t type)
{
    for (s32 i = 0; i < menuGraphNodeCount(); i++) {
        if (s_Nodes[i].type == type) {
            return &s_Nodes[i];
        }
    }
    return NULL;
}

const MenuGraphEdge *menuGraphEdge(menu_type_t source, const char *edge_id)
{
    if (!edge_id) {
        return NULL;
    }

    const MenuGraphNode *node = menuGraphNode(source);
    if (!node) {
        return NULL;
    }

    for (s32 i = 0; i < node->edge_count; i++) {
        if (node->edges[i].id && strcmp(node->edges[i].id, edge_id) == 0) {
            return &node->edges[i];
        }
    }

    return NULL;
}

s32 menuGraphFirePushDialog(menu_type_t source,
                            const char *edge_id,
                            struct menudialogdef *dialogdef)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !dialogdef) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_PUSH_MENU) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    menu_type_t actual = menupoolTypeForDialogdef(dialogdef);
    if (edge->payload.push_target != MENU_TYPE_NONE &&
            actual != edge->payload.push_target) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s target=%s actual=%s ok=0",
            menupoolTypeName(source), edge_id,
            menupoolTypeName(edge->payload.push_target),
            menupoolTypeName(actual));
        return -3;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s target=%s ok=1",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        edge->payload.push_target == MENU_TYPE_NONE
            ? menupoolTypeName(actual)
            : menupoolTypeName(edge->payload.push_target));

    menuPushDialog(dialogdef);
    return 0;
}

s32 menuGraphFireReplaceDialog(menu_type_t source,
                               const char *edge_id,
                               struct menudialogdef *dialogdef)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !dialogdef) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_PUSH_MENU) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    menu_type_t actual = menupoolTypeForDialogdef(dialogdef);
    if (edge->payload.push_target != MENU_TYPE_NONE &&
            actual != edge->payload.push_target) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s target=%s actual=%s ok=0",
            menupoolTypeName(source), edge_id,
            menupoolTypeName(edge->payload.push_target),
            menupoolTypeName(actual));
        return -3;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s target=%s replace=1 ok=1",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        edge->payload.push_target == MENU_TYPE_NONE
            ? menupoolTypeName(actual)
            : menupoolTypeName(edge->payload.push_target));

    menuPopDialog();
    menuPushDialog(dialogdef);
    return 0;
}

s32 menuGraphFirePushOp(menu_type_t source,
                        const char *edge_id,
                        MenuGraphPushOpFn op,
                        void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_PUSH_MENU) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s target=%s",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        menupoolTypeName(edge->payload.push_target));

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    return rc;
}

s32 menuGraphFireSwitchSibling(menu_type_t source,
                               const char *edge_id,
                               struct menudialogdef *dialogdef)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !dialogdef) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_SWITCH_SIBLING) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    menu_type_t actual = menupoolTypeForDialogdef(dialogdef);
    if (actual != edge->payload.sibling_target) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s target=%s actual=%s ok=0",
            menupoolTypeName(source), edge_id,
            menupoolTypeName(edge->payload.sibling_target),
            menupoolTypeName(actual));
        return -3;
    }

    if (!menuSwitchToDialog(dialogdef)) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s target=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id,
            menupoolTypeName(edge->payload.sibling_target),
            menuGraphDestKindName(edge->kind));
        return -4;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s target=%s ok=1",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        menupoolTypeName(edge->payload.sibling_target));

    return 0;
}

s32 menuGraphFirePop(menu_type_t source, const char *edge_id)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind == MENU_GRAPH_DEST_POP_TO_PARENT) {
        sysLogPrintf(LOG_NOTE,
            "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s ok=1",
            menupoolTypeName(source), edge_id, (int)edge->trigger,
            menuGraphDestKindName(edge->kind));
        menuPopDialog();
        return 0;
    }

    if (edge->kind == MENU_GRAPH_DEST_POP_TO_ROOT) {
        sysLogPrintf(LOG_NOTE,
            "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s ok=1",
            menupoolTypeName(source), edge_id, (int)edge->trigger,
            menuGraphDestKindName(edge->kind));
        /* Root close has two owners: the pool/input context and the
         * legacy root dialog that keeps queuing the ImGui renderer. */
        menupoolReleaseAll();
        menuClose();
        return 0;
    }

    sysLogPrintf(LOG_WARNING,
        "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
        menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
    return -2;
}

s32 menuGraphFirePopOp(menu_type_t source,
                       const char *edge_id,
                       MenuGraphPopOpFn op,
                       void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_POP_TO_PARENT &&
            edge->kind != MENU_GRAPH_DEST_POP_TO_ROOT) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind));

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    if (rc == 0) {
        if (edge->kind == MENU_GRAPH_DEST_POP_TO_PARENT) {
            menuPopDialog();
        } else if (edge->kind == MENU_GRAPH_DEST_POP_TO_ROOT) {
            /* Root close has two owners: the pool/input context and the
             * legacy root dialog that keeps queuing the ImGui renderer. */
            menupoolReleaseAll();
            menuClose();
        }
    }

    return rc;
}

s32 menuGraphFireSceneOp(menu_type_t source,
                         const char *edge_id,
                         MenuGraphSceneOpFn op,
                         void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_SCENE_EVENT) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s event=%d",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind), (int)edge->payload.scene_event);

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    return rc;
}

s32 menuGraphFireNetworkOp(menu_type_t source,
                           const char *edge_id,
                           MenuGraphNetworkOpFn op,
                           void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_NETWORK_OP) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s op=%s",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        edge->payload.network_op ? edge->payload.network_op : "(null)");

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    return rc;
}

s32 menuGraphFireProcessOp(menu_type_t source,
                           const char *edge_id,
                           MenuGraphProcessOpFn op,
                           void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_PROCESS_EXIT) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind));

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    return rc;
}

s32 menuGraphFireLocalOp(menu_type_t source,
                         const char *edge_id,
                         MenuGraphLocalOpFn op,
                         void *userdata)
{
    const MenuGraphEdge *edge = menuGraphEdge(source, edge_id);
    if (!edge || !op) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=missing ok=0",
            menupoolTypeName(source), edge_id ? edge_id : "(null)");
        return -1;
    }

    if (edge->kind != MENU_GRAPH_DEST_LOCAL_OP) {
        sysLogPrintf(LOG_WARNING,
            "MENU.GRAPH.FIRE source=%s edge=%s dest=%s ok=0",
            menupoolTypeName(source), edge_id, menuGraphDestKindName(edge->kind));
        return -2;
    }

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.FIRE source=%s edge=%s trigger=%d dest=%s op=%s",
        menupoolTypeName(source), edge_id, (int)edge->trigger,
        menuGraphDestKindName(edge->kind),
        edge->payload.local_op ? edge->payload.local_op : "(null)");

    s32 rc = op(userdata);

    sysLogPrintf(LOG_NOTE,
        "MENU.GRAPH.RESULT source=%s edge=%s rc=%d ok=%d",
        menupoolTypeName(source), edge_id, rc, rc == 0 ? 1 : 0);

    return rc;
}
