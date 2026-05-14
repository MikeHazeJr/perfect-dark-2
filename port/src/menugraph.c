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
    EDGE_PUSH("solo_play", ACTION_MENU_ACCEPT, "Solo Play", MENU_TYPE_MAIN_SOLO_VIEW),
    EDGE_PUSH("online_play", ACTION_MENU_ACCEPT, "Online Play", MENU_TYPE_MAIN_ONLINE_VIEW),
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

static const MenuGraphEdge s_MainModdingEdges[] = {
    EDGE_PUSH("open_hub", ACTION_MENU_ACCEPT, "Open Modding Hub", MENU_TYPE_MODDING_HUB),
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MainOnlineEdges[] = {
    EDGE_NETWORK("connect", ACTION_MENU_ACCEPT, "Connect", "client"),
    EDGE_NETWORK("recent_server", ACTION_MENU_ACCEPT, "Recent Server", "client"),
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
};

static const MenuGraphEdge s_MpPauseEdges[] = {
    EDGE_POP("resume", ACTION_MENU_CANCEL, "Resume"),
    EDGE_PUSH("end_game", ACTION_MENU_ACCEPT, "End Game", MENU_TYPE_WARNING_MODAL),
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
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpAdvancedEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_MpBotSetupEdges[] = {
    EDGE_POP("back", ACTION_MENU_CANCEL, "Back"),
};

static const MenuGraphEdge s_FrDifficultyEdges[] = {
    EDGE_PUSH("start", ACTION_MENU_ACCEPT, "Start Firing Range", MENU_TYPE_FR_INFO),
    EDGE_POP("cancel", ACTION_MENU_CANCEL, "Cancel"),
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
    NODE(MENU_TYPE_MAIN_MODDING_VIEW, "main_modding_view", s_MainModdingEdges),
    NODE(MENU_TYPE_MAIN_ONLINE_VIEW, "main_online_view", s_MainOnlineEdges),
    NODE(MENU_TYPE_MAIN_STATS_VIEW, "main_stats_view", s_MainStatsEdges),
    NODE(MENU_TYPE_GRID_SUBMENU, "grid_submenu", s_GridEdges),
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
    NODE(MENU_TYPE_CHALLENGES, "challenges", s_ChallengesEdges),
    NODE(MENU_TYPE_MP_TEAM_SETUP, "mp_team_setup", s_MpTeamSetupEdges),
    NODE(MENU_TYPE_MP_PLAYER_CONFIG, "mp_player_config", s_MpPlayerConfigEdges),
    NODE(MENU_TYPE_MP_SETUP, "mp_setup", s_MpSetupEdges),
    NODE(MENU_TYPE_MP_ADVANCED, "mp_advanced", s_MpAdvancedEdges),
    NODE(MENU_TYPE_MP_BOT_SETUP, "mp_bot_setup", s_MpBotSetupEdges),
    NODE(MENU_TYPE_FR_DIFFICULTY, "fr_difficulty", s_FrDifficultyEdges),
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
    if (actual != edge->payload.push_target) {
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
        menupoolTypeName(edge->payload.push_target));

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
        menupoolReleaseAll();
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
