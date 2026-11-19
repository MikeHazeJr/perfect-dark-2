/**
 * pdgui_menu_charactertool.cpp -- the Modding Hub's Characters tool.
 *
 * Browser: the catalog's characters, bodies and heads, one kind at a time,
 * with what the selected entry's descriptor says (gender, build, height).
 * Creator: "Use as Template" opens the selected entry in a form; Save hands
 * the template's archive, the new name and the changed descriptor values to
 * the mod author (modAuthorRuntimeSave), which writes the mod, enables it and
 * puts it in the catalog.
 *
 * What the form may hold and which values a save sends is decided by
 * port/src/character_tool.c (character_tool.h), which pd-tests covers; this
 * file draws and calls. Every step logs a CHARMOD.* line, and
 * --debug-character-tool-create runs the same functions the buttons do.
 *
 * First slice (Mike, 2026-11-16): a name for all three kinds, and gender,
 * build and height for a body or a head. A character keeps its template's
 * body and head; its head policy, and a body's or head's own model, come
 * later (context/designs/modding/maian-vehicle-and-veteran.md, C1 and C2).
 *
 * IMPORTANT: C++ file, must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <memory>
#include <vector>

#include "imgui/imgui.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_widgets.h"
#include "pdgui_charactertool.h"
#include "system.h"
#include "assetcatalog.h"
#include "body_head_source.h"
#include "character_tool.h"
#include "fs.h"
#include "head_body_rig.h"
#include "identity.h"
#include "mod_author.h"
#include "mod_author_runtime.h"

extern "C" {
s32 viGetWidth(void);
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);
void pdguiModManagerRefreshSnapshot(void);
}

/* ========================================================================
 * State
 * ======================================================================== */

/* The browser: which kind is listed, and the selected entry of each kind. */
static int  s_KindFilter = CHARACTER_TOOL_KIND_CHARACTER;
static char s_SelectedId[CHARACTER_TOOL_KIND_HEAD + 1][CATALOG_ID_LEN];

/* What was read for the selected entry (read again when the selection or the
 * catalog changes). */
struct CharacterToolDetail {
    char id[CATALOG_ID_LEN];
    char archive[FS_MAXPATH];      /* the typed archive it was loaded from */
    char archiveError[192];        /* why it has none on disk, if so */
    char descriptorError[192];     /* why its descriptor could not be read, if so */
    char rigClass[32];
    bool hasTemplate;              /* tmpl below is filled */
    character_tool_template_t tmpl;
};
static CharacterToolDetail s_Detail;

/* The creator: the template that is open and the form the player edits. */
static bool s_CreatorOpen = false;
static bool s_EditActive = false;
static bool s_EditBundled = false;
static char s_EditTemplateId[CATALOG_ID_LEN] = "";
static char s_EditTemplateArchive[FS_MAXPATH] = "";
static char s_EditDisplayName[MOD_AUTHOR_NAME_MAX] = "";
static character_tool_template_t s_EditTemplate;
static character_tool_form_t s_EditForm;
static char s_CreatorTitle[48] = "CREATE CHARACTER MOD";
static char s_CreatorPath[64] = "Characters > Create Character Mod";

static char s_Status[256] = "";
static bool s_StatusOk = true;

/* The save window. */
static bool s_SaveModalOpen = false;
static bool s_SaveComplete = false;
static bool s_SaveStatusOk = true;
static char s_SaveCreator[64] = "";
static char s_SaveDisplayName[MOD_AUTHOR_NAME_MAX] = "";
static char s_SaveStatus[256] = "";
static char s_SavedCatalogId[CATALOG_ID_LEN] = "";

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

/* The status line under the browser and the creator; the save window shows
 * it too while it is open. */
static void toolSetStatus(bool ok, const char *msg)
{
    s_StatusOk = ok;
    snprintf(s_Status, sizeof(s_Status), "%s", msg ? msg : "");
    if (s_SaveModalOpen) {
        s_SaveStatusOk = ok;
        snprintf(s_SaveStatus, sizeof(s_SaveStatus), "%s", msg ? msg : "");
    }
}

static const char *toolActiveAgentName(void)
{
    identity_profile_t *profile = identityGetActiveProfile();
    return (profile && profile->name[0]) ? profile->name : "Agent";
}

static bool toolReadWholeFile(const char *path, std::vector<char> &out)
{
    out.clear();
    FILE *f = path && path[0] ? fopen(path, "rb") : NULL;
    if (!f) return false;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long size = ftell(f);
        if (size > 0 && fseek(f, 0, SEEK_SET) == 0) {
            out.resize((size_t)size);
            ok = fread(out.data(), 1, (size_t)size, f) == (size_t)size;
        }
    }
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

/* The name the list and the headers show for an entry: its own display name,
 * for a character without one its body's, else its catalog name. */
static const char *toolEntryName(const asset_entry_t *e)
{
    if (!e) return "";
    if (e->type == ASSET_CHARACTER) {
        if (e->ext.character.display_name[0]) return e->ext.character.display_name;
        const asset_entry_t *body = e->ext.character.body_id[0]
            ? assetCatalogResolve(e->ext.character.body_id) : NULL;
        if (body && body->type == ASSET_BODY && body->ext.body.display_name[0]) {
            return body->ext.body.display_name;
        }
    } else if (e->type == ASSET_BODY && e->ext.body.display_name[0]) {
        return e->ext.body.display_name;
    }
    return e->id;
}

static const asset_entry_t *toolSelectedEntry(void)
{
    const char *id = s_SelectedId[s_KindFilter];
    const asset_entry_t *e = id[0] ? assetCatalogResolve(id) : NULL;
    return (e && characterToolKindForAssetType(e->type) == s_KindFilter) ? e : NULL;
}

/* "Male" or "Female" with the build and the descriptor's type, for readouts. */
static void toolDescribeBuild(const character_tool_template_t *tmpl, char *out, size_t outSize)
{
    character_tool_form_t form;
    characterToolFormFromTemplate(tmpl, &form);
    const char *typeName = characterToolTypeName(tmpl->type);
    snprintf(out, outSize, "%s, %s (%s)", form.ismale ? "Male" : "Female",
             characterToolBuildLabel(form.build), typeName ? typeName : "unknown type");
}

/* ========================================================================
 * The selected entry
 * ======================================================================== */

/* Reads what the browser shows for an entry and what a template needs: the
 * archive it came from and, for a body or a head, its descriptor's gender,
 * type and height (through the game's own strict reader). */
static void toolLoadDetail(const asset_entry_t *e)
{
    memset(&s_Detail, 0, sizeof(s_Detail));
    if (!e) return;
    snprintf(s_Detail.id, sizeof(s_Detail.id), "%s", e->id);
    const s32 kind = characterToolKindForAssetType(e->type);
    s_Detail.tmpl.kind = kind;

    if (!modAuthorRuntimeTemplatePath(e->id, s_Detail.archive, sizeof(s_Detail.archive),
            s_Detail.archiveError, sizeof(s_Detail.archiveError))) {
        s_Detail.archive[0] = '\0';
        if (!s_Detail.archiveError[0]) {
            snprintf(s_Detail.archiveError, sizeof(s_Detail.archiveError),
                     "its archive is not a file on disk");
        }
        return;
    }
    if (kind == CHARACTER_TOOL_KIND_CHARACTER) {
        s_Detail.hasTemplate = true; /* nothing of its descriptor is edited yet */
        return;
    }

    std::vector<char> bytes;
    if (!toolReadWholeFile(s_Detail.archive, bytes)) {
        snprintf(s_Detail.descriptorError, sizeof(s_Detail.descriptorError),
                 "the archive could not be read");
        return;
    }
    auto source = std::make_unique<body_head_source_t>();
    char err[160] = "";
    if (!bodyHeadSourceParseArchiveBytes(bytes.data(), (u32)bytes.size(), e->id,
            kind == CHARACTER_TOOL_KIND_HEAD ? 1 : 0, source.get(), err, sizeof(err))) {
        snprintf(s_Detail.descriptorError, sizeof(s_Detail.descriptorError), "%s",
                 err[0] ? err : "its descriptor could not be read");
        return;
    }
    s_Detail.tmpl.ismale = source->ismale ? 1 : 0;
    s_Detail.tmpl.type = source->type;
    s_Detail.tmpl.height = source->height;
    snprintf(s_Detail.rigClass, sizeof(s_Detail.rigClass), "%s", source->rig_class);
    s_Detail.hasTemplate = true;
}

/* ========================================================================
 * The creator
 * ======================================================================== */

/* What a save under this display name would be called and where it would go
 * (the mod author's own answer, so the preview cannot drift from the save). */
static bool toolNamesFor(const char *displayName, mod_author_result_t *names)
{
    mod_author_request_t request;
    memset(&request, 0, sizeof(request));
    request.template_path = s_EditTemplateArchive;
    request.mods_root = "mods";
    request.display_name = displayName;
    return modAuthorNames(&request, names, NULL, 0) == MOD_AUTHOR_OK;
}

/* "Use as Template": open the selected entry in the creator. */
static bool toolStartTemplate(const asset_entry_t *e)
{
    s_EditActive = false;
    if (!e || characterToolKindForAssetType(e->type) == CHARACTER_TOOL_KIND_NONE) {
        toolSetStatus(false, "Select a character, a body or a head");
        return false;
    }
    if (strcmp(s_Detail.id, e->id) != 0) toolLoadDetail(e);
    if (!s_Detail.archive[0]) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot be a template: %s", s_Detail.archiveError);
        toolSetStatus(false, msg);
        return false;
    }
    if (!s_Detail.hasTemplate) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot be a template: %s", s_Detail.descriptorError);
        toolSetStatus(false, msg);
        return false;
    }

    snprintf(s_EditTemplateId, sizeof(s_EditTemplateId), "%s", e->id);
    snprintf(s_EditTemplateArchive, sizeof(s_EditTemplateArchive), "%s", s_Detail.archive);
    s_EditTemplate = s_Detail.tmpl;
    s_EditBundled = e->bundled != 0;
    characterToolFormFromTemplate(&s_EditTemplate, &s_EditForm);
    snprintf(s_EditDisplayName, sizeof(s_EditDisplayName), "Custom %s",
             characterToolKindLabel(s_EditTemplate.kind));
    snprintf(s_CreatorTitle, sizeof(s_CreatorTitle), "CREATE %s MOD",
             s_EditTemplate.kind == CHARACTER_TOOL_KIND_BODY ? "BODY"
             : s_EditTemplate.kind == CHARACTER_TOOL_KIND_HEAD ? "HEAD" : "CHARACTER");
    snprintf(s_CreatorPath, sizeof(s_CreatorPath), "Characters > Create %s Mod",
             characterToolKindLabel(s_EditTemplate.kind));
    s_EditActive = true;
    s_CreatorOpen = true;
    s_SavedCatalogId[0] = '\0';

    const char *typeName = characterToolTypeName(s_EditTemplate.type);
    sysLogPrintf(LOG_NOTE,
                 "CHARMOD.TEMPLATE.START template=%s kind=%s archive='%s' ismale=%d type=%s height=%d",
                 s_EditTemplateId, characterToolKindName(s_EditTemplate.kind),
                 s_EditTemplateArchive, (int)s_EditTemplate.ismale,
                 characterToolKindHasBuild(s_EditTemplate.kind)
                     ? (typeName ? typeName : "unknown") : "n/a",
                 (int)s_EditTemplate.height);
    toolSetStatus(true, "Template opened");
    return true;
}

static bool toolSaveFail(const char *stage, const char *msg)
{
    sysLogPrintf(LOG_WARNING,
                 "CHARMOD.SAVE.%s kind=%s template=%s display='%s' message='%s'",
                 stage ? stage : "FAIL", characterToolKindName(s_EditTemplate.kind),
                 s_EditTemplateId[0] ? s_EditTemplateId : "(none)",
                 s_EditDisplayName[0] ? s_EditDisplayName : "(none)", msg ? msg : "");
    toolSetStatus(false, msg);
    return false;
}

/* "Create + Enable": save the open template as a mod with the form's values. */
static bool toolSave(void)
{
    if (!s_EditActive) return toolSaveFail("NO_TEMPLATE", "Open a template before saving");
    if (s_SaveDisplayName[0]) {
        snprintf(s_EditDisplayName, sizeof(s_EditDisplayName), "%s", s_SaveDisplayName);
    }
    const char *creator = s_SaveCreator[0] ? s_SaveCreator : toolActiveAgentName();

    mod_author_result_t names;
    if (!toolNamesFor(s_EditDisplayName, &names)) {
        return toolSaveFail("BAD_DISPLAY_NAME", "Use a valid display name for the catalog name");
    }
    char why[96];
    if (!characterToolFormValid(&s_EditForm, &s_EditTemplate, why, sizeof(why))) {
        char msg[160];
        snprintf(msg, sizeof(msg), "Not saved: %s", why);
        return toolSaveFail("BAD_FORM", msg);
    }
    character_tool_values_t values;
    if (!characterToolDescriptorValues(&s_EditForm, &s_EditTemplate, &values)) {
        return toolSaveFail("BAD_FORM", "Not saved: the form could not be read");
    }

    char changed[160] = "";
    size_t pos = 0;
    for (u32 i = 0; i < values.count && pos + 1 < sizeof(changed); i++) {
        const int n = snprintf(changed + pos, sizeof(changed) - pos, "%s%s=%s", i ? " " : "",
                               values.values[i].key,
                               values.values[i].value ? values.values[i].value : "(removed)");
        if (n < 0) break;
        pos += (size_t)n;
    }
    sysLogPrintf(LOG_NOTE,
                 "CHARMOD.SAVE.BEGIN kind=%s catalog=%s template=%s display='%s' creator='%s' values=%u [%s] template_archive='%s'",
                 characterToolKindName(s_EditTemplate.kind), names.catalog_id, s_EditTemplateId,
                 s_EditDisplayName, creator, (unsigned)values.count, changed,
                 s_EditTemplateArchive);

    char description[96];
    snprintf(description, sizeof(description), "%s mod created with the Characters tool.",
             characterToolKindLabel(s_EditTemplate.kind));
    mod_author_request_t request;
    memset(&request, 0, sizeof(request));
    request.template_path = s_EditTemplateArchive;
    request.display_name = s_EditDisplayName;
    request.author = creator;
    request.description = description;
    request.tool = "characters-tool";
    request.descriptor_values = values.count ? values.values : NULL;
    request.descriptor_value_count = values.count;

    static mod_author_result_t result;
    memset(&result, 0, sizeof(result));
    s32 restartCount = 0;
    char err[512] = "";
    const s32 code = modAuthorRuntimeSave(&request, &result, &restartCount, err, sizeof(err));
    if (code != MOD_AUTHOR_OK) {
        const char *stage = "AUTHOR_FAIL";
        if (code == MOD_AUTHOR_ERR_EXISTS) stage = "NAME_TAKEN";
        else if (code == MOD_AUTHOR_ERR_TEMPLATE) stage = "TEMPLATE_FAIL";
        else if (code == MOD_AUTHOR_ERR_ARCHIVE) stage = "ARCHIVE_FAIL";
        else if (code == MOD_AUTHOR_ERR_WRITE && result.archive_path[0]) stage = "ENABLE_FAIL";
        else if (code == MOD_AUTHOR_ERR_WRITE) stage = "WRITE_FAIL";
        char msg[600];
        snprintf(msg, sizeof(msg), "%s%s",
                 result.archive_path[0] ? "Saved, but not active yet: " : "Not saved: ",
                 err[0] ? err : "the mod author gave no reason");
        return toolSaveFail(stage, msg);
    }

    /* Show the new mod in the browser. */
    pdguiModManagerRefreshSnapshot();
    snprintf(s_SavedCatalogId, sizeof(s_SavedCatalogId), "%s", result.catalog_id);
    s_KindFilter = s_EditTemplate.kind;
    snprintf(s_SelectedId[s_KindFilter], CATALOG_ID_LEN, "%s", result.catalog_id);
    s_Detail.id[0] = '\0';

    char status[256];
    if (restartCount) {
        snprintf(status, sizeof(status),
                 "Saved. Restart required to finish applying %d package change(s).",
                 (int)restartCount);
    } else {
        snprintf(status, sizeof(status), "Saved, enabled, and catalog updated: %s",
                 result.catalog_id);
    }
    toolSetStatus(true, status);
    s_SaveComplete = true;
    sysLogPrintf(LOG_NOTE, "CHARMOD.SAVE.OK kind=%s catalog=%s mod_id=%s archive='%s' updated=%d",
                 result.kind, result.catalog_id, result.mod_id, result.archive_path,
                 (int)result.updated);
    return true;
}

static void toolOpenSaveModal(void)
{
    if (!s_EditActive) {
        toolSetStatus(false, "Open a template before saving");
        return;
    }
    snprintf(s_SaveCreator, sizeof(s_SaveCreator), "%s", toolActiveAgentName());
    snprintf(s_SaveDisplayName, sizeof(s_SaveDisplayName), "%s", s_EditDisplayName);
    s_SaveStatus[0] = '\0';
    s_SaveStatusOk = true;
    s_SaveComplete = false;
    s_SaveModalOpen = true;
}

/* The save window: creator and display name, what the mod will be called,
 * Create + Enable. */
static void toolRenderSaveModal(float scale)
{
    const char *const popupId = "Save Character Mod##character_save_options";
    if (s_SaveModalOpen) ImGui::OpenPopup(popupId);
    if (s_SaveModalOpen || ImGui::IsPopupOpen(popupId)) pdguiPopupDarkenBehind(0.55f);

    const float viewportW = (float)viGetWidth();
    float popupW = 520.0f * scale;
    if (popupW > viewportW - 80.0f * scale) popupW = viewportW - 80.0f * scale;
    if (popupW < 420.0f * scale) popupW = 420.0f * scale;
    ImGui::SetNextWindowSize(ImVec2(popupW, 0.0f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.045f, 0.065f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.045f, 0.065f, 0.98f));
    if (ImGui::BeginPopupModal(popupId, &s_SaveModalOpen,
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        if (s_SaveComplete) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Creator", s_SaveCreator, sizeof(s_SaveCreator));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Display Name", s_SaveDisplayName, sizeof(s_SaveDisplayName));
        if (s_SaveComplete) ImGui::EndDisabled();

        mod_author_result_t names;
        const bool namesOk = toolNamesFor(s_SaveDisplayName, &names);
        ImGui::Separator();
        if (namesOk) {
            ImGui::Text("Catalog Name: %s", names.catalog_id);
            ImGui::TextWrapped("Output: %s", names.archive_path);
        } else {
            ImGui::TextDisabled("Catalog Name: (enter a valid display name)");
        }
        ImGui::TextDisabled("Saving enables the mod and rebuilds the catalog immediately.");

        if (s_SaveStatus[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, s_SaveStatusOk
                                      ? ImVec4(0.55f, 0.90f, 0.68f, 0.92f)
                                      : pdguiVec4TextWarning(240));
            ImGui::TextWrapped("%s", s_SaveStatus);
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        if (s_SaveComplete) {
            if (toolButton("Done", ImVec2(92.0f * scale, 28.0f * scale))) {
                s_SaveModalOpen = false;
                s_CreatorOpen = false; /* back to the browser, on the new mod */
                ImGui::CloseCurrentPopup();
            }
        } else {
            const bool canCreate = namesOk && s_SaveCreator[0] && s_EditTemplateArchive[0]
                && characterToolFormValid(&s_EditForm, &s_EditTemplate, NULL, 0);
            if (!canCreate) ImGui::BeginDisabled();
            if (toolButton("Create + Enable", ImVec2(152.0f * scale, 28.0f * scale))) toolSave();
            if (!canCreate) ImGui::EndDisabled();
            ImGui::SameLine();
            if (toolButton("Cancel", ImVec2(92.0f * scale, 28.0f * scale))) {
                s_SaveModalOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
}

/* One line of what a save would change, e.g. "gender, build (type and neck),
 * height", or that it is a plain copy. */
static void toolDescribeChanges(char *out, size_t outSize)
{
    character_tool_values_t values;
    out[0] = '\0';
    if (!characterToolDescriptorValues(&s_EditForm, &s_EditTemplate, &values)) return;
    if (values.count == 0) {
        snprintf(out, outSize, "No changes to the descriptor: a copy of the template under its new name.");
        return;
    }
    bool gender = false, type = false, height = false;
    for (u32 i = 0; i < values.count; i++) {
        if (strcmp(values.values[i].key, "ismale") == 0) gender = true;
        else if (strcmp(values.values[i].key, "type") == 0) type = true;
        else if (strcmp(values.values[i].key, "height") == 0) height = true;
    }
    snprintf(out, outSize, "Changes:%s%s%s%s%s",
             gender ? " gender" : "", gender && (type || height) ? "," : "",
             type ? " build (type and neck)" : "", type && height ? "," : "",
             height ? " height" : "");
}

/* ========================================================================
 * Public API
 * ======================================================================== */

extern "C" {

void pdguiCharacterToolRefresh(void)
{
    s_Detail.id[0] = '\0'; /* read the selected entry again */
}

s32 pdguiCharacterToolCreatorOpen(void)
{
    return s_CreatorOpen ? 1 : 0;
}

void pdguiCharacterToolCloseCreator(void)
{
    s_CreatorOpen = false;
    s_SaveModalOpen = false;
}

const char *pdguiCharacterToolCreatorTitle(void)
{
    return s_CreatorTitle;
}

const char *pdguiCharacterToolCreatorPath(void)
{
    return s_CreatorPath;
}

void pdguiCharacterToolRender(float contentW, float contentH, float scale)
{
    static const int kKinds[] = {
        CHARACTER_TOOL_KIND_CHARACTER, CHARACTER_TOOL_KIND_BODY, CHARACTER_TOOL_KIND_HEAD
    };
    static const char *const kKindTabs[] = { "Characters", "Bodies", "Heads" };

    const float listW = contentW * 0.30f;
    const float detailW = contentW - listW - ImGui::GetStyle().ItemSpacing.x;

    /* ---- The list: one kind at a time, by name ---- */
    ImGui::BeginChild("##character_list", ImVec2(listW, contentH), true);
    for (int i = 0; i < 3; i++) {
        if (i > 0) ImGui::SameLine();
        const bool active = s_KindFilter == kKinds[i];
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (toolButton(kKindTabs[i])) s_KindFilter = kKinds[i];
        if (active) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    struct Row {
        const asset_entry_t *entry;
        const char *name;
    };
    std::vector<Row> rows;
    const s32 poolSize = assetCatalogGetPoolSize();
    for (s32 i = 0; i < poolSize; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || characterToolKindForAssetType(e->type) != s_KindFilter) continue;
        rows.push_back(Row{ e, toolEntryName(e) });
    }
    qsort(rows.data(), rows.size(), sizeof(Row), [](const void *a, const void *b) {
        const Row *ra = (const Row *)a;
        const Row *rb = (const Row *)b;
        const int cmp = strcasecmp(ra->name, rb->name);
        return cmp != 0 ? cmp : strcasecmp(ra->entry->id, rb->entry->id);
    });
    char *selectedId = s_SelectedId[s_KindFilter];
    if (!rows.empty() && !toolSelectedEntry()) {
        snprintf(selectedId, CATALOG_ID_LEN, "%s", rows[0].entry->id);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        const asset_entry_t *e = rows[i].entry;
        char label[CATALOG_ID_LEN + 96];
        snprintf(label, sizeof(label), "%s%s##character_%d", rows[i].name,
                 e->bundled ? "" : "  (mod)", (int)i);
        if (ImGui::Selectable(label, strcmp(selectedId, e->id) == 0)) {
            snprintf(selectedId, CATALOG_ID_LEN, "%s", e->id);
        }
    }
    if (rows.empty()) ImGui::TextDisabled("None registered.");
    ImGui::EndChild();

    ImGui::SameLine();

    /* ---- The selected entry ---- */
    ImGui::BeginChild("##character_detail", ImVec2(detailW, contentH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    const asset_entry_t *e = toolSelectedEntry();
    if (!e) {
        ImGui::TextDisabled("Select a character, a body or a head.");
        ImGui::EndChild();
        return;
    }
    if (strcmp(s_Detail.id, e->id) != 0) toolLoadDetail(e);
    const s32 kind = characterToolKindForAssetType(e->type);

    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TitleGlow());
    ImGui::Text("%s", toolEntryName(e));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s", e->bundled ? "Base" : "Mod", characterToolKindLabel(kind));
    ImGui::Separator();

    ImGui::Text("Catalog Name: %s", e->id);
    if (kind == CHARACTER_TOOL_KIND_CHARACTER) {
        ImGui::Text("Body: %s", e->ext.character.body_id[0] ? e->ext.character.body_id : "(none)");
        ImGui::Text("Head: %s", e->ext.character.head_id[0] ? e->ext.character.head_id
                                                             : "(chosen by its head policy)");
    } else if (s_Detail.hasTemplate) {
        char build[96];
        toolDescribeBuild(&s_Detail.tmpl, build, sizeof(build));
        ImGui::Text("Gender and build: %s", build);
        ImGui::Text("Height: %d", (int)s_Detail.tmpl.height);
        ImGui::Text("Neck: %s", s_Detail.rigClass[0] ? s_Detail.rigClass : "(none)");
    } else if (s_Detail.descriptorError[0]) {
        ImGui::TextDisabled("Descriptor: %s", s_Detail.descriptorError);
    }
    ImGui::TextWrapped("Archive: %s", s_Detail.archive[0] ? s_Detail.archive : s_Detail.archiveError);

    const bool canTemplate = s_Detail.archive[0] && s_Detail.hasTemplate;
    if (!canTemplate) ImGui::BeginDisabled();
    if (toolButton("Use as Template", ImVec2(204.0f * scale, 28.0f * scale))) toolStartTemplate(e);
    if (!canTemplate) ImGui::EndDisabled();
    if (s_EditActive) {
        ImGui::SameLine();
        if (toolButton("Open Creator", ImVec2(132.0f * scale, 28.0f * scale))) s_CreatorOpen = true;
    }
    ImGui::TextDisabled("%s", e->bundled
        ? "A base entry is read-only; a template saves as a new mod."
        : "A mod entry can be saved again under a new name.");

    ImGui::PushStyleColor(ImGuiCol_Text, s_StatusOk ? ImVec4(0.55f, 0.90f, 0.68f, 0.86f)
                                                    : pdguiVec4TextWarning(220));
    ImGui::TextWrapped("%s", s_Status[0] ? s_Status : "Ready");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void pdguiCharacterToolRenderCreator(float scale)
{
    static const char *const kGenders[] = { "Female", "Male" };
    const s32 kind = s_EditTemplate.kind;

    if (!s_EditActive) {
        ImGui::TextDisabled("No template is open.");
        return;
    }

    ImGui::Text("Template: %s", s_EditTemplateId);
    ImGui::TextDisabled("%s", characterToolKindLabel(kind));
    ImGui::Separator();

    pdguiInputText("Display Name", s_EditDisplayName, sizeof(s_EditDisplayName));

    if (characterToolKindHasBuild(kind)) {
        int gender = s_EditForm.ismale ? 1 : 0;
        if (pdguiCombo("Gender", &gender, kGenders, 2)) s_EditForm.ismale = gender;

        /* The builds, the last one named after the template's own type. */
        char own[64];
        const char *ownType = characterToolTypeName(s_EditTemplate.type);
        snprintf(own, sizeof(own), "%s (%s)", characterToolBuildLabel(CHARACTER_TOOL_BUILD_TEMPLATE),
                 ownType ? ownType : "unknown type");
        const char *builds[CHARACTER_TOOL_BUILD_COUNT] = {
            characterToolBuildLabel(CHARACTER_TOOL_BUILD_HUMAN),
            characterToolBuildLabel(CHARACTER_TOOL_BUILD_GUARD),
            characterToolBuildLabel(CHARACTER_TOOL_BUILD_MAIAN),
            own,
        };
        int build = s_EditForm.build;
        if (pdguiCombo("Build", &build, builds, CHARACTER_TOOL_BUILD_COUNT)) s_EditForm.build = build;

        pdguiSettingsBeginRow("Height");
        int height = s_EditForm.height;
        if (ImGui::InputInt("##character_height", &height, 1, 10)) s_EditForm.height = height;

        /* What the choice means in the descriptor. */
        const s32 type = characterToolTypeForForm(&s_EditForm, &s_EditTemplate);
        const char *typeName = characterToolTypeName(type);
        ImGui::TextDisabled("Type: %s    Neck: %s", typeName ? typeName : "unknown",
                            typeName ? catalogRigClassForHeadBodyType((unsigned)type) : "unknown");
        ImGui::TextDisabled("A head fits a body with the same neck. The template's height is %d.",
                            (int)s_EditTemplate.height);
    } else {
        ImGui::TextWrapped("A character keeps its template's body and head. Gender, build and "
                           "height belong to a body or a head: make those first, from the Bodies "
                           "and Heads lists.");
    }

    ImGui::Separator();
    mod_author_result_t names;
    if (toolNamesFor(s_EditDisplayName, &names)) {
        ImGui::Text("Catalog Name: %s", names.catalog_id);
    } else {
        ImGui::TextDisabled("Catalog Name: (enter a valid display name)");
    }
    char changes[160];
    toolDescribeChanges(changes, sizeof(changes));
    if (changes[0]) ImGui::TextDisabled("%s", changes);

    char why[96];
    const bool valid = characterToolFormValid(&s_EditForm, &s_EditTemplate, why, sizeof(why)) != 0;
    if (!valid) {
        ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TextWarning(240));
        ImGui::TextWrapped("Cannot save: %s.", why);
        ImGui::PopStyleColor();
    }
    if (s_EditBundled) {
        ImGui::TextWrapped("This template comes from your own game files, and the mod will hold "
                           "its data. Keep it on this computer: it must not be shared.");
    }

    if (!valid) ImGui::BeginDisabled();
    if (toolButton("Save...", ImVec2(132.0f * scale, 28.0f * scale))) toolOpenSaveModal();
    if (!valid) ImGui::EndDisabled();

    ImGui::PushStyleColor(ImGuiCol_Text, s_StatusOk ? ImVec4(0.55f, 0.90f, 0.68f, 0.86f)
                                                    : pdguiVec4TextWarning(220));
    ImGui::TextWrapped("%s", s_Status[0] ? s_Status : "Ready");
    ImGui::PopStyleColor();

    toolRenderSaveModal(scale);
}

s32 pdguiCharacterToolDebugCreate(const char *templateId, const char *displayName,
	const char *gender, const char *build, s32 height)
{
    sysLogPrintf(LOG_NOTE,
                 "CHARMOD.DEBUG.CREATE.BEGIN template=%s display='%s' gender=%s build=%s height=%d",
                 templateId ? templateId : "(none)", displayName ? displayName : "",
                 gender ? gender : "(keep)", build ? build : "(keep)", (int)height);
    const asset_entry_t *e = templateId ? assetCatalogResolve(templateId) : NULL;
    const s32 kind = e ? characterToolKindForAssetType(e->type) : CHARACTER_TOOL_KIND_NONE;
    if (kind == CHARACTER_TOOL_KIND_NONE) {
        sysLogPrintf(LOG_WARNING,
                     "CHARMOD.DEBUG.CREATE.RESULT result=FAIL reason='not a character, body or head in the catalog'");
        return 0;
    }
    s_KindFilter = kind;
    snprintf(s_SelectedId[kind], CATALOG_ID_LEN, "%s", e->id);
    toolLoadDetail(e);
    if (!toolStartTemplate(e)) {
        sysLogPrintf(LOG_WARNING, "CHARMOD.DEBUG.CREATE.RESULT result=FAIL reason='template: %s'",
                     s_Status);
        return 0;
    }
    /* The same fields the creator's widgets set. A word the flags do not
     * know fails the run instead of being skipped. */
    s32 ismale = s_EditForm.ismale;
    s32 formBuild = s_EditForm.build;
    if ((gender && !characterToolParseGender(gender, &ismale))
            || (build && !characterToolParseBuild(build, &formBuild))) {
        sysLogPrintf(LOG_WARNING,
                     "CHARMOD.DEBUG.CREATE.RESULT result=FAIL reason='gender is male or female; build is human, guard, maian or template'");
        return 0;
    }
    s_EditForm.ismale = ismale;
    s_EditForm.build = formBuild;
    if (height != 0) s_EditForm.height = height;
    if (displayName && displayName[0]) {
        snprintf(s_EditDisplayName, sizeof(s_EditDisplayName), "%s", displayName);
    }
    s_SaveDisplayName[0] = '\0';
    s_SaveCreator[0] = '\0';
    const bool saved = toolSave();
    s_CreatorOpen = false;
    sysLogPrintf(saved ? LOG_NOTE : LOG_WARNING,
                 "CHARMOD.DEBUG.CREATE.RESULT result=%s catalog=%s status='%s'",
                 saved ? "OK" : "FAIL", saved ? s_SavedCatalogId : "(none)", s_Status);
    return saved ? 1 : 0;
}

} /* extern "C" */
