/**
 * pdgui_menu_leveltool.cpp -- the Modding Hub's Levels tool.
 *
 * Browser: the catalog's levels, the game's own and the level mods, with what
 * the selected level holds (read from its own files by the level document
 * Forge edits). Open in Forge starts a Forge session on the selected level.
 * New Level makes a copy of it under a name of its own through the mod author
 * (modAuthorRuntimeSave), which writes the mod, enables it and puts it in the
 * catalog; Forge then opens that level and its Save updates it in place.
 *
 * What a level's placements add up to and which names a new level may take is
 * decided by port/src/level_tool.c (level_tool.h), which pd-tests covers; this
 * file draws and calls. Every step logs a LEVELTOOL.* line, and
 * --debug-level-tool-new runs the same function the Create button does.
 *
 * First slice (context/designs/modding/forge-mode-scope.md, step F4): choose a
 * template, name the new level, open it in Forge, and see the levels made so
 * far. A check of a level beyond "its files are read" is the game's own when
 * it loads one, and is not repeated here yet.
 *
 * IMPORTANT: C++ file, must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <vector>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_widgets.h"
#include "pdgui_leveltool.h"
#include "system.h"
#include "assetcatalog.h"
#include "forge_level_doc.h"
#include "forge_level_io.h"
#include "grid_archive_policy.h"
#include "forge_room_builder.h"
#include "fs.h"
#include "game/forgemode.h"
#include "identity.h"
#include "level_tool.h"
#include "mod_author.h"
#include "mod_author_runtime.h"

extern "C" {
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);
void pdguiModManagerRefreshSnapshot(void);
void pdguiModdingHubHide(void);
const char *modmgrGetModsDir(void);
extern s32 g_NetMode; /* 0 (NETMODE_NONE) when no network game is running */
}

/* A level's name for a list: a mod level's own, else the game's localised one
 * (pdgui_menu_room.cpp). */
const char *arenaEntryName(const asset_entry_t *e);

/* ========================================================================
 * State
 * ======================================================================== */

/* The browser: which levels are listed, and the selected level of each list. */
enum {
    LEVEL_LIST_MOD = 0, /* level mods: made here, installed, and templates */
    LEVEL_LIST_BASE,    /* the game's own levels */
    LEVEL_LIST_COUNT
};
static int  s_List = LEVEL_LIST_MOD;
static char s_SelectedId[LEVEL_LIST_COUNT][CATALOG_ID_LEN];

/* What was read for the selected level (read again when the selection or the
 * catalog changes). */
struct LevelToolDetail {
    char id[CATALOG_ID_LEN];
    bool opened;                         /* its files were read */
    char openError[320];                 /* why not, if so */
    char archive[MOD_AUTHOR_PATH_MAX];   /* the .pdarena it was loaded from */
    level_tool_summary_t summary;
    char holds[256];                     /* the summary in one line */
};
static LevelToolDetail s_Detail;

/* The new level: its name, and the mod author's answer for that name on the
 * selected template (asked again only when either changes: the answer looks
 * at the mods folder). */
static char s_NewName[MOD_AUTHOR_NAME_MAX] = "";
static char s_OriginalName[MOD_AUTHOR_NAME_MAX] = "";
static bool s_ShowOriginal = false;
static constexpr const char *kOriginalRoomSeed = "$E/grid/templates/grid_two_rooms.pdarena";
static constexpr const char *kDebugOriginal = "original";
struct LevelToolPlan {
    bool valid;
    char templateId[CATALOG_ID_LEN];
    char archive[MOD_AUTHOR_PATH_MAX];
    char name[MOD_AUTHOR_NAME_MAX];
    s32 answer;                          /* LEVEL_TOOL_NAME_* */
    char why[320];
    char catalogId[CATALOG_ID_LEN];      /* the id the name gives, when the author accepts it */
};
static LevelToolPlan s_Plan;

static char s_Status[400] = "";
static bool s_StatusOk = true;

/* ========================================================================
 * Small helpers
 * ======================================================================== */

/* A button with the hub's select sound and edge glow. */
static bool toolButton(const char *label, const ImVec2 &size = ImVec2(0, 0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y, rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

static void toolSetStatus(bool ok, const char *msg)
{
    s_StatusOk = ok;
    snprintf(s_Status, sizeof(s_Status), "%s", msg ? msg : "");
}

static const char *toolActiveAgentName(void)
{
    identity_profile_t *profile = identityGetActiveProfile();
    return (profile && profile->name[0]) ? profile->name : "Agent";
}

static int toolListOf(const asset_entry_t *e)
{
    return e->bundled ? LEVEL_LIST_BASE : LEVEL_LIST_MOD;
}

/* The name the list and the header show for a level. */
static const char *toolLevelName(const asset_entry_t *e)
{
    const char *name = e ? arenaEntryName(e) : NULL;
    return (name && name[0]) ? name : (e ? e->id : "");
}

static const asset_entry_t *toolSelectedLevel(void)
{
    const char *id = s_SelectedId[s_List];
    const asset_entry_t *e = id[0] ? assetCatalogResolve(id) : NULL;
    return (e && e->type == ASSET_ARENA && toolListOf(e) == s_List) ? e : NULL;
}

/* One row of the list. The entry is the catalog's and is good for the frame
 * it was collected in. */
struct LevelRow {
    const asset_entry_t *entry;
    const char *name;
};

/* The levels of one list, by name. `counts`, when given, receives how many
 * levels each list has. */
static void toolCollect(int list, std::vector<LevelRow> &rows, int *counts)
{
    rows.clear();
    if (counts) {
        for (int i = 0; i < LEVEL_LIST_COUNT; i++) counts[i] = 0;
    }
    const s32 poolSize = assetCatalogGetPoolSize();
    for (s32 i = 0; i < poolSize; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || e->type != ASSET_ARENA) continue;
        if (counts) counts[toolListOf(e)]++;
        if (toolListOf(e) == list) rows.push_back(LevelRow{ e, toolLevelName(e) });
    }
    qsort(rows.data(), rows.size(), sizeof(LevelRow), [](const void *a, const void *b) {
        const LevelRow *ra = (const LevelRow *)a;
        const LevelRow *rb = (const LevelRow *)b;
        const int cmp = strcasecmp(ra->name, rb->name);
        return cmp != 0 ? cmp : strcasecmp(ra->entry->id, rb->entry->id);
    });
}

/* ========================================================================
 * The selected level
 * ======================================================================== */

/* Reads what the browser shows for a level: the archive it came from and
 * what its own files hold, through the document Forge edits. A level Forge
 * could not open says why here, before the player tries. */
static void toolLoadDetail(const asset_entry_t *e)
{
    static forge_level_source_t source; /* about 2 KB: kept off the stack */

    memset(&s_Detail, 0, sizeof(s_Detail));
    if (!e) return;
    snprintf(s_Detail.id, sizeof(s_Detail.id), "%s", e->id);

    char err[320] = "";
    forge_level_doc_t *doc = forgeLevelOpen(e->id, &source, err, sizeof(err));
    if (!doc) {
        snprintf(s_Detail.openError, sizeof(s_Detail.openError), "%s",
                 err[0] ? err : "its files could not be read");
        /* The archive is still worth showing when it is known. */
        if (!modAuthorRuntimeTemplatePath(e->id, s_Detail.archive, sizeof(s_Detail.archive), NULL, 0)) {
            s_Detail.archive[0] = '\0';
        }
        return;
    }
    levelToolSummarise(doc, &s_Detail.summary);
    levelToolDescribe(&s_Detail.summary, s_Detail.holds, sizeof(s_Detail.holds));
    forgeLevelDocFree(doc);
    snprintf(s_Detail.archive, sizeof(s_Detail.archive), "%s", source.archive_path);
    s_Detail.opened = true;
}

/* ========================================================================
 * A new level
 * ======================================================================== */

/* The mod author's answer for this name on this template, and what the tool
 * makes of it (levelToolNewLevelName). Kept until the name or the template
 * changes, or a level is created. */
static const LevelToolPlan &toolPlan(const char *templateId, const char *archive, const char *name)
{
    static mod_author_result_t plan; /* large: paths and the id map */

    if (!templateId) templateId = "";
    if (!archive) archive = "";
    if (!name) name = "";
    if (s_Plan.valid && strcmp(s_Plan.templateId, templateId) == 0
            && strcmp(s_Plan.archive, archive) == 0 && strcmp(s_Plan.name, name) == 0) {
        return s_Plan;
    }
    memset(&s_Plan, 0, sizeof(s_Plan));
    snprintf(s_Plan.templateId, sizeof(s_Plan.templateId), "%s", templateId);
    snprintf(s_Plan.archive, sizeof(s_Plan.archive), "%s", archive);
    snprintf(s_Plan.name, sizeof(s_Plan.name), "%s", name);
    s_Plan.valid = true;
    if (!name[0]) {
        s_Plan.answer = LEVEL_TOOL_NAME_EMPTY;
        return s_Plan;
    }

    const char *modsRoot = modmgrGetModsDir();
    char reason[256] = "";
    s32 code = MOD_AUTHOR_ERR_ARGS;
    memset(&plan, 0, sizeof(plan));
    if (!archive[0]) {
        snprintf(reason, sizeof(reason), "the template's archive is not known");
    } else if (!modsRoot || !modsRoot[0]) {
        snprintf(reason, sizeof(reason), "the game has no mods folder to save into");
    } else {
        mod_author_request_t request;
        memset(&request, 0, sizeof(request));
        request.template_path = archive;
        request.mods_root = modsRoot;
        request.display_name = name;
        code = modAuthorPlan(&request, &plan, reason, sizeof(reason));
    }
    s_Plan.answer = levelToolNewLevelName(name, templateId, code, &plan, reason, s_Plan.why, sizeof(s_Plan.why));
    if (code == MOD_AUTHOR_OK) {
        snprintf(s_Plan.catalogId, sizeof(s_Plan.catalogId), "%s", plan.catalog_id);
    }
    return s_Plan;
}

static bool toolCreateFail(const char *stage, const char *templateId, const char *name, const char *msg)
{
    sysLogPrintf(LOG_WARNING, "LEVELTOOL.NEW.%s template=%s display='%s' message='%s'",
                 stage ? stage : "FAIL", templateId ? templateId : "(none)", (name && name[0]) ? name : "(none)",
                 msg ? msg : "");
    toolSetStatus(false, msg);
    return false;
}

/* Save a typed arena under its own name and select the registered result. */
static bool toolCreateFromSource(const char *templateId, const char *archive, const char *displayName,
    char *outCatalogId, size_t outCap, const ForgeRoomSize *roomSize, bool original)
{
    static mod_author_result_t result; /* large: paths and the id map */
    if (outCatalogId && outCap) outCatalogId[0] = '\0';

    const LevelToolPlan &plan = toolPlan(templateId, archive, displayName);
    if (plan.answer == LEVEL_TOOL_NAME_EMPTY) {
        return toolCreateFail("NO_NAME", templateId, displayName, "Type a name for the new level");
    }
    if (plan.answer != LEVEL_TOOL_NAME_OK) {
        return toolCreateFail(plan.answer == LEVEL_TOOL_NAME_TAKEN ? "NAME_TAKEN"
                              : plan.answer == LEVEL_TOOL_NAME_TEMPLATE ? "NAME_IS_TEMPLATE"
                              : "BAD_NAME", templateId, displayName, plan.why);
    }

    const char *creator = toolActiveAgentName();
    char description[160];
    if (original) snprintf(description, sizeof(description), "Original arena made with the Levels room builder.");
    else snprintf(description, sizeof(description), "Level made with the Levels tool from %s.", templateId);
    sysLogPrintf(LOG_NOTE,
                 "LEVELTOOL.NEW.BEGIN template=%s display='%s' catalog=%s creator='%s' template_archive='%s'",
                 templateId, displayName, plan.catalogId, creator, archive);

    mod_author_request_t request;
    memset(&request, 0, sizeof(request));
    request.template_path = archive;
    request.display_name = displayName;
    request.author = creator;
    request.description = description;
    request.tool = "levels-tool";
    mod_author_descriptor_value_t provenance = {
        GRID_ARENA_PROVENANCE_KEY, roomSize ? GRID_ARENA_PROVENANCE_ORIGINAL
            : forgeLevelCloneProvenance(templateId, archive)
    };
    request.descriptor_values = &provenance;
    request.descriptor_value_count = 1;

    memset(&result, 0, sizeof(result));
    s32 restartCount = 0;
    char err[512] = "";
    ForgeRoomOverrides roomOverrides;
    if (roomSize) {
        if (!forgeRoomBuildOverrides(archive, *roomSize, roomOverrides, err, sizeof(err))) {
            return toolCreateFail("ROOM_BUILD_FAIL", templateId, displayName, err);
        }
        request.overrides = roomOverrides.entries.data();
        request.override_count = static_cast<u32>(roomOverrides.entries.size());
        if (original) {
            sysLogPrintf(LOG_NOTE, "LEVELTOOL.ORIGINAL.SEED: template=%s seed_catalogued=%d path='%s'",
                         templateId, assetCatalogResolve(templateId) ? 1 : 0, archive);
        }
        sysLogPrintf(LOG_NOTE,
                     "LEVELTOOL.ROOMS: length=%d width=%d height=%d members=%u second_length=%d doorway_width=%.2f doorway_height=%.2f",
                     roomSize->firstLength, roomSize->width, roomSize->height, request.override_count,
                     roomSize->secondLength, roomSize->effectiveDoorwayWidth(), roomSize->effectiveDoorwayHeight());
    }
    const s32 code = modAuthorRuntimeSave(&request, &result, &restartCount, err, sizeof(err));
    /* Whatever happened, the answer for this name is no longer the one kept. */
    s_Plan.valid = false;
    if (code != MOD_AUTHOR_OK) {
        const char *stage = "AUTHOR_FAIL";
        if (code == MOD_AUTHOR_ERR_EXISTS) stage = "NAME_TAKEN";
        else if (code == MOD_AUTHOR_ERR_TEMPLATE) stage = "TEMPLATE_FAIL";
        else if (code == MOD_AUTHOR_ERR_ARCHIVE) stage = "ARCHIVE_FAIL";
        else if (code == MOD_AUTHOR_ERR_WRITE && result.archive_path[0]) stage = "ENABLE_FAIL";
        else if (code == MOD_AUTHOR_ERR_WRITE) stage = "WRITE_FAIL";
        char msg[600];
        snprintf(msg, sizeof(msg), "%s%s",
                 result.archive_path[0] ? "Saved, but not active yet: " : "Not created: ",
                 err[0] ? err : "the mod author gave no reason");
        return toolCreateFail(stage, nullptr, displayName, msg);
    }
    const asset_entry_t *made = assetCatalogResolve(result.catalog_id);
    if (!made || made->type != ASSET_ARENA) {
        char msg[400];
        snprintf(msg, sizeof(msg), "Saved as %s, but the catalog does not list it as a level%s",
                 result.catalog_id, restartCount ? " until the game is restarted" : "");
        return toolCreateFail("NOT_IN_CATALOG", nullptr, displayName, msg);
    }

    /* Show the new level in the browser. */
    pdguiModManagerRefreshSnapshot();
    s_List = LEVEL_LIST_MOD;
    snprintf(s_SelectedId[LEVEL_LIST_MOD], CATALOG_ID_LEN, "%s", result.catalog_id);
    s_Detail.id[0] = '\0';
    if (outCatalogId && outCap) snprintf(outCatalogId, outCap, "%s", result.catalog_id);

    char status[400];
    snprintf(status, sizeof(status), "Created %s (%s). Open it in %s to add placements.",
             displayName, result.catalog_id, forgeDisplayName());
    toolSetStatus(true, status);
    sysLogPrintf(LOG_NOTE, "LEVELTOOL.NEW.OK template=%s catalog=%s mod_id=%s archive='%s' restart_count=%d",
                 templateId, result.catalog_id, result.mod_id, result.archive_path, (int)restartCount);
    return true;
}

static bool toolCreate(const asset_entry_t *tmpl, const char *displayName, char *outCatalogId, size_t outCap,
    const ForgeRoomSize *roomSize = nullptr)
{
    if (!tmpl || tmpl->type != ASSET_ARENA) {
        return toolCreateFail("NO_TEMPLATE", nullptr, displayName, "Select a level to start from");
    }
    /* A save rescans the catalog. Keep the source identity and path by value. */
    char templateId[CATALOG_ID_LEN];
    snprintf(templateId, sizeof(templateId), "%s", tmpl->id);
    if (strcmp(s_Detail.id, templateId) != 0) toolLoadDetail(tmpl);
    if (!s_Detail.opened) {
        char msg[400];
        snprintf(msg, sizeof(msg), "This level cannot be a template: %s", s_Detail.openError);
        return toolCreateFail("TEMPLATE_FAIL", templateId, displayName, msg);
    }
    char archive[MOD_AUTHOR_PATH_MAX];
    snprintf(archive, sizeof(archive), "%s", s_Detail.archive);
    return toolCreateFromSource(templateId, archive, displayName, outCatalogId, outCap, roomSize, false);
}

static bool toolOriginalRoomPath(char (&path)[MOD_AUTHOR_PATH_MAX])
{
    fsFullPath(kOriginalRoomSeed, path, sizeof(path));
    return fsPathIsAbsolute(path) && strlen(path) < sizeof(path) - 1;
}

static bool toolCreateOriginal(const ForgeRoomSize &size, const char *displayName,
    char *outCatalogId, size_t outCap)
{
    if (g_NetMode != 0) {
        return toolCreateFail("ORIGINAL_OFFLINE", FORGE_ROOM_TEMPLATE_ID, displayName,
                              "Leave the network game before creating an original arena.");
    }
    char archive[MOD_AUTHOR_PATH_MAX];
    if (!toolOriginalRoomPath(archive)) {
        return toolCreateFail("TEMPLATE_FAIL", FORGE_ROOM_TEMPLATE_ID, displayName,
                              "The original arena template is unavailable.");
    }
    return toolCreateFromSource(FORGE_ROOM_TEMPLATE_ID, archive, displayName,
        outCatalogId, outCap, &size, true);
}

/* "Open in Forge": a Forge session on this level, the way The Grid starts
 * one. The hub closes; the stage change takes the menus with it. */
static bool toolOpenInForge(const char *arenaId)
{
    char id[CATALOG_ID_LEN];
    snprintf(id, sizeof(id), "%s", arenaId ? arenaId : "");
    if (g_NetMode != 0) {
        sysLogPrintf(LOG_WARNING, "LEVELTOOL.FORGE arena=%s result=FAIL reason='a network game is running'", id);
        toolSetStatus(false, "Forge edits a level on this PC only. Leave the network game first.");
        return false;
    }
    const s32 started = forgeLevelStartForge(id);
    sysLogPrintf(started ? LOG_NOTE : LOG_WARNING, "LEVELTOOL.FORGE arena=%s result=%s", id,
                 started ? "OK" : "FAIL");
    if (!started) {
        toolSetStatus(false, "Forge could not be started on this level. The log says why.");
        return false;
    }
    toolSetStatus(true, "");
    pdguiModdingHubHide();
    return true;
}

static const LevelToolPlan &toolRenderNewName(const char *templateId, const char *archive,
    char (&name)[MOD_AUTHOR_NAME_MAX])
{
    pdguiInputText("Name", name, sizeof(name));
    const LevelToolPlan &plan = toolPlan(templateId, archive, name);
    if (plan.answer == LEVEL_TOOL_NAME_OK) {
        ImGui::Text("Saves as %s, a new level.", plan.catalogId);
    } else if (plan.answer == LEVEL_TOOL_NAME_EMPTY) {
        ImGui::TextDisabled("Type a name. It is what players see in the level lists.");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(240));
        ImGui::TextWrapped("%s", plan.why);
        ImGui::PopStyleColor();
    }
    return plan;
}

static void toolRenderRoomDimensions(ForgeRoomSize &roomSize)
{
    using Room = ForgeRoomSize;
    constexpr auto metres = [](double units) { return static_cast<float>(units / 100.0); };
    constexpr auto units = [](float distance) { return static_cast<s32>(distance * 100.0f + 0.5f); };
    constexpr auto clampInput = ImGuiSliderFlags_AlwaysClamp;
    float firstLength = metres(roomSize.firstLength);
    float secondLength = metres(roomSize.secondLength);
    float width = metres(roomSize.width);
    float height = metres(roomSize.height);
    float doorwayWidth = metres(roomSize.effectiveDoorwayWidth());
    float doorwayHeight = metres(roomSize.effectiveDoorwayHeight());
    ImGui::SeparatorText("Room Dimensions");
    ImGui::SliderFloat("Room 1 length", &firstLength,
        metres(Room::MinLength), metres(Room::MaxLength), "%.1f m", clampInput);
    ImGui::SliderFloat("Room 2 length", &secondLength,
        metres(Room::MinLength), metres(Room::MaxLength), "%.1f m", clampInput);
    ImGui::SliderFloat("Width of both rooms", &width,
        metres(Room::MinWidth), metres(Room::MaxWidth), "%.1f m", clampInput);
    ImGui::SliderFloat("Height of both rooms", &height,
        metres(Room::MinHeight), metres(Room::MaxHeight), "%.1f m", clampInput);
    roomSize.firstLength = units(firstLength);
    roomSize.secondLength = units(secondLength);
    roomSize.width = units(width);
    roomSize.height = units(height);
    const float maxDoorwayWidth = metres(roomSize.width - 2 * Room::MinSideWallWidth);
    const float maxDoorwayHeight = metres(roomSize.height - Room::MinLintelHeight);
    doorwayWidth = std::clamp(doorwayWidth, metres(Room::MinDoorwayWidth), maxDoorwayWidth);
    doorwayHeight = std::clamp(doorwayHeight, metres(Room::MinDoorwayHeight), maxDoorwayHeight);
    ImGui::SliderFloat("Doorway width", &doorwayWidth,
        metres(Room::MinDoorwayWidth), maxDoorwayWidth, "%.1f m", clampInput);
    ImGui::SliderFloat("Doorway height", &doorwayHeight,
        metres(Room::MinDoorwayHeight), maxDoorwayHeight, "%.1f m", clampInput);
    roomSize.doorwayWidth = units(doorwayWidth);
    roomSize.doorwayHeight = units(doorwayHeight);
}

static void toolRenderStatus(void)
{
    ImGui::PushStyleColor(ImGuiCol_Text, s_StatusOk ? ImVec4(0.55f, 0.90f, 0.68f, 0.86f)
                                                    : pdguiVec4TextWarning(220));
    ImGui::TextWrapped("%s", s_Status[0] ? s_Status : "Ready");
    ImGui::PopStyleColor();
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

void pdguiLevelToolRefresh(void)
{
    s_Detail.id[0] = '\0'; /* read the selected level again */
    s_Plan.valid = false;  /* and ask about the name again: the mods folder may have changed */

    /* A player with no level mods yet starts on the game's levels. */
    std::vector<LevelRow> rows;
    int counts[LEVEL_LIST_COUNT];
    toolCollect(s_List, rows, counts);
    if (s_List == LEVEL_LIST_MOD && counts[LEVEL_LIST_MOD] == 0 && counts[LEVEL_LIST_BASE] > 0) {
        s_List = LEVEL_LIST_BASE;
    }
}

void pdguiLevelToolRender(float contentW, float contentH, float scale)
{
    static const char *const kListTabs[LEVEL_LIST_COUNT] = { "Level Mods", "Game Levels" };

    const float listW = contentW * 0.30f;
    const float detailW = contentW - listW - ImGui::GetStyle().ItemSpacing.x;

    /* ---- The list: one kind at a time, by name ---- */
    ImGui::BeginChild("##level_list", ImVec2(listW, contentH), true);
    for (int i = 0; i < LEVEL_LIST_COUNT; i++) {
        if (i > 0) ImGui::SameLine();
        const bool active = s_List == i;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (toolButton(kListTabs[i])) {
            s_List = i;
            s_ShowOriginal = false;
        }
        if (active) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    std::vector<LevelRow> rows;
    int counts[LEVEL_LIST_COUNT];
    toolCollect(s_List, rows, counts);

    char *selectedId = s_SelectedId[s_List];
    if (!rows.empty() && !toolSelectedLevel()) {
        snprintf(selectedId, CATALOG_ID_LEN, "%s", rows[0].entry->id);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        const asset_entry_t *e = rows[i].entry;
        char label[CATALOG_ID_LEN + 96];
        snprintf(label, sizeof(label), "%s%s##level_%d", rows[i].name,
                 e->ext.arena.load_mode == ARENA_LOADMODE_CANVAS ? "  (mission)" : "", (int)i);
        if (ImGui::Selectable(label, strcmp(selectedId, e->id) == 0)) {
            snprintf(selectedId, CATALOG_ID_LEN, "%s", e->id);
            s_ShowOriginal = false;
        }
    }
    if (rows.empty()) {
        ImGui::TextDisabled("%s", s_List == LEVEL_LIST_MOD && counts[LEVEL_LIST_MOD] == 0
            ? "No level mods yet. Pick one of the game's levels and make a new level from it."
            : "None registered.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* ---- The selected level ---- */
    ImGui::BeginChild("##level_detail", ImVec2(detailW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    const bool online = g_NetMode != 0;
    if (!s_ShowOriginal && toolButton("Create Original Arena")) {
        s_ShowOriginal = true;
        toolSetStatus(true, "");
    }
    if (s_ShowOriginal) {
        static ForgeRoomSize originalSize;
        ImGui::PushID("original_arena");
        ImGui::SeparatorText("Original Arena");
        const bool back = toolButton("Back to Levels");
        char archive[MOD_AUTHOR_PATH_MAX];
        const bool available = toolOriginalRoomPath(archive);
        const LevelToolPlan &plan = toolRenderNewName(FORGE_ROOM_TEMPLATE_ID,
            available ? archive : nullptr, s_OriginalName);
        toolRenderRoomDimensions(originalSize);
        if (online) ImGui::TextDisabled("Leave the network game before creating an arena.");
        ImGui::BeginDisabled(online || !available || plan.answer != LEVEL_TOOL_NAME_OK);
        const bool create = toolButton("Create", ImVec2(132.0f * scale, 28.0f * scale));
        ImGui::SameLine();
        const bool open = toolButton("Create and Open", ImVec2(244.0f * scale, 28.0f * scale));
        ImGui::EndDisabled();
        toolRenderStatus();
        ImGui::PopID();
        ImGui::EndChild();

        if (back) s_ShowOriginal = false;
        if (create || open) {
            char made[CATALOG_ID_LEN] = "";
            if (toolCreateOriginal(originalSize, s_OriginalName, made, sizeof(made))) {
                s_OriginalName[0] = '\0';
                s_ShowOriginal = false;
                if (open) toolOpenInForge(made);
            }
        }
        return;
    }
    ImGui::Separator();
    const asset_entry_t *e = toolSelectedLevel();
    if (!e) {
        ImGui::TextDisabled("Select a level.");
        ImGui::EndChild();
        return;
    }
    if (strcmp(s_Detail.id, e->id) != 0) toolLoadDetail(e);
    /* The catalog owns the row; a button below may rescan it. */
    char levelId[CATALOG_ID_LEN];
    snprintf(levelId, sizeof(levelId), "%s", e->id);
    const bool isBase = e->bundled != 0;

    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
    ImGui::Text("%s", toolLevelName(e));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", isBase ? "Game level" : "Level mod");
    ImGui::Separator();

    ImGui::Text("Catalog Name: %s", levelId);
    if (s_Detail.opened) {
        ImGui::TextWrapped("Holds: %s", s_Detail.holds);
        ImGui::Text("Pads: %d", (int)s_Detail.summary.pads);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(240));
        ImGui::TextWrapped("Forge cannot open this level: %s", s_Detail.openError);
        ImGui::PopStyleColor();
    }
    if (s_Detail.archive[0]) ImGui::TextWrapped("Archive: %s", s_Detail.archive);

    /* Open in Forge. */
    const bool canOpen = s_Detail.opened && !online;
    if (!canOpen) ImGui::BeginDisabled();
    const bool openPressed = toolButton("Open in Forge", ImVec2(204.0f * scale, 28.0f * scale));
    if (!canOpen) ImGui::EndDisabled();
    if (online) {
        ImGui::TextDisabled("Forge edits a level on this PC only. Leave the network game first.");
    } else {
        ImGui::TextDisabled("%s", isBase
            ? "A game level is read-only: Forge saves what you change as a new level."
            : "Forge's Save under this level's own name updates it.");
    }

    /* A new level from this one. */
    ImGui::Separator();
    ImGui::Text("New level from this one");
    if (!s_Detail.opened) ImGui::BeginDisabled();
    const LevelToolPlan &plan = toolRenderNewName(levelId, s_Detail.archive, s_NewName);
    if (isBase) {
        ImGui::TextWrapped("This level comes from your own game files, and the new level will hold "
                           "its data. Keep it on this computer: it must not be shared.");
    }
    const bool roomTemplate = strcmp(levelId, FORGE_ROOM_TEMPLATE_ID) == 0;
    static ForgeRoomSize roomSize;
    if (roomTemplate) {
        toolRenderRoomDimensions(roomSize);
    }
    const bool canCreate = s_Detail.opened && plan.answer == LEVEL_TOOL_NAME_OK && (!roomTemplate || !online);
    bool createPressed = false;
    bool createOpenPressed = false;
    if (!canCreate) ImGui::BeginDisabled();
    createPressed = toolButton("Create", ImVec2(132.0f * scale, 28.0f * scale));
    if (!canCreate) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canCreate || online) ImGui::BeginDisabled();
    createOpenPressed = toolButton("Create and Open", ImVec2(244.0f * scale, 28.0f * scale));
    if (!canCreate || online) ImGui::EndDisabled();
    if (!s_Detail.opened) ImGui::EndDisabled();

    toolRenderStatus();
    ImGui::EndChild();

    /* The buttons act after the pane is drawn: a save rescans the catalog,
     * and Open in Forge closes the hub. `e` is not used past this point. */
    if (openPressed) {
        toolOpenInForge(levelId);
    } else if (createPressed || createOpenPressed) {
        char name[MOD_AUTHOR_NAME_MAX];
        char made[CATALOG_ID_LEN] = "";
        snprintf(name, sizeof(name), "%s", s_NewName);
        if (toolCreate(assetCatalogResolve(levelId), name, made, sizeof(made), roomTemplate ? &roomSize : nullptr)) {
            s_NewName[0] = '\0';
            if (createOpenPressed) toolOpenInForge(made);
        }
    }
}

static bool toolReadRoomSize(const char *text, ForgeRoomSize &size)
{
    s32 dimensions[6];
    size_t count = 0;
    do {
        char *end;
        long value = strtol(text, &end, 10);
        if (end == text || value < 0 || value > ForgeRoomSize::MaxLength) return false;
        dimensions[count++] = static_cast<s32>(value);
        if (!*end) break;
        if (*end != ',' || count == 6) return false;
        text = end + 1;
    } while (true);
    if (count == 3) {
        size = {dimensions[0], dimensions[0], dimensions[1], dimensions[2], 0, 0};
        return true;
    }
    if (count == 6 && dimensions[4] != 0 && dimensions[5] != 0) {
        size = {dimensions[0], dimensions[1], dimensions[2], dimensions[3], dimensions[4], dimensions[5]};
        return true;
    }
    return false;
}

s32 pdguiLevelToolDebugNew(const char *templateId, const char *displayName)
{
    sysLogPrintf(LOG_NOTE, "LEVELTOOL.DEBUG.NEW.BEGIN template=%s display='%s'",
                 templateId ? templateId : "(none)", displayName ? displayName : "");
    char made[CATALOG_ID_LEN] = "";
    const char *dimensions = sysArgGetString("--debug-grid-room-size");
    ForgeRoomSize roomSize;
    if (dimensions && !toolReadRoomSize(dimensions, roomSize)) {
        sysLogPrintf(LOG_WARNING, "LEVELTOOL.DEBUG.NEW.RESULT result=FAIL reason='expected length,width,height "
            "or first-length,second-length,width,height,doorway-width,doorway-height in cm'");
        return 0;
    }
    const char *name = displayName ? displayName : "";
    bool created;
    if (templateId && strcmp(templateId, kDebugOriginal) == 0) {
        created = toolCreateOriginal(roomSize, name, made, sizeof(made));
    } else {
        const asset_entry_t *e = (templateId && templateId[0]) ? assetCatalogResolve(templateId) : nullptr;
        if (!e || e->type != ASSET_ARENA) {
            sysLogPrintf(LOG_WARNING, "LEVELTOOL.DEBUG.NEW.RESULT result=FAIL reason='not a level in the catalog'");
            return 0;
        }
        s_List = toolListOf(e);
        snprintf(s_SelectedId[s_List], CATALOG_ID_LEN, "%s", e->id);
        toolLoadDetail(e);
        created = toolCreate(e, name, made, sizeof(made), dimensions ? &roomSize : nullptr);
    }
    if (created) {
        /* What the browser shows for the new level, as a line a run can read. */
        const asset_entry_t *level = assetCatalogResolve(made);
        if (level) toolLoadDetail(level);
        /* A new level Forge cannot open is not what the tool promises. */
        const bool opens = level && s_Detail.opened;
        sysLogPrintf(opens ? LOG_NOTE : LOG_WARNING,
                     "LEVELTOOL.DEBUG.NEW.RESULT result=%s catalog=%s opens=%d holds='%s'",
                     opens ? "OK" : "FAIL", made, opens ? 1 : 0,
                     opens ? s_Detail.holds : s_Detail.openError);
        return opens ? 1 : 0;
    }
    sysLogPrintf(LOG_WARNING, "LEVELTOOL.DEBUG.NEW.RESULT result=FAIL status='%s'", s_Status);
    return 0;
}

} /* extern "C" */
