#include "pdgui_settings_ui.h"
#include "pdgui_nav_input.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cstring>

float pdguiSettingsFitWidth(float preferred)
{
    const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    return std::min(std::max(1.0f, preferred), available);
}

void pdguiSettingsStackedRow(const char *label)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(label ? label : "");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(pdguiSettingsFitWidth(ImGui::GetContentRegionAvail().x));
}

PdguiSettingsThemeAction pdguiSettingsThemeActions(bool enabled,
    const char *acceptGlyph, float width, float height)
{
    if (ImGui::Button("Actions##theme_actions_button",
            ImVec2(pdguiSettingsFitWidth(width), height))) {
        ImGui::OpenPopup("##theme_actions");
        pdguiNavSuppressOpeningGesture();
    }
    ImGui::TextWrapped("Select: %s", acceptGlyph && acceptGlyph[0]
        ? acceptGlyph : "Unbound");

    PdguiSettingsThemeAction action = PDGUI_SETTINGS_THEME_NONE;
    if (ImGui::BeginPopup("##theme_actions")) {
        if (ImGui::MenuItem(enabled ? "Disable Theme" : "Enable Theme"))
            action = enabled ? PDGUI_SETTINGS_THEME_DISABLE : PDGUI_SETTINGS_THEME_ENABLE;
        ImGui::SetItemDefaultFocus();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Theme...")) action = PDGUI_SETTINGS_THEME_DELETE;
        ImGui::EndPopup();
    }
    return action;
}

std::string pdguiSettingsStylePreview(const PdguiSettingsRegistry &registry,
    bool staticEnabled, const char *activeId)
{
    if (!staticEnabled) return "Procedural (built-in)";
    if (registry.id && registry.name && activeId && activeId[0]) {
        for (int i = 0; i < registry.count; ++i) {
            const char *id = registry.id(i, registry.userdata);
            if (id && std::strcmp(id, activeId) == 0) {
                const char *name = registry.name(i, registry.userdata);
                return name && name[0] ? name : id;
            }
        }
    }
    return std::string("Unavailable style: ") +
        (activeId && activeId[0] ? activeId : "none selected");
}

static bool registryCombo(const char *label,
    const PdguiSettingsRegistry &registry, bool noneSelected,
    const char *activeId, const char *emptyLabel, const std::string &preview,
    int *selectedIndex)
{
    pdguiSettingsStackedRow(label);
    ImGui::PushID(label);
    bool changed = false;
    if (ImGui::BeginCombo("##style", preview.c_str())) {
        if (ImGui::Selectable(emptyLabel, noneSelected)) {
            if (selectedIndex) *selectedIndex = -1;
            changed = true;
        }
        if (noneSelected) ImGui::SetItemDefaultFocus();
        if (registry.id && registry.name) {
            for (int i = 0; i < registry.count; ++i) {
                const char *id = registry.id(i, registry.userdata);
                if (!id || !id[0]) continue;
                const char *name = registry.name(i, registry.userdata);
                const bool selected = !noneSelected && activeId && std::strcmp(id, activeId) == 0;
                ImGui::PushID(id);
                if (ImGui::Selectable(name && name[0] ? name : id, selected)) {
                    if (selectedIndex) *selectedIndex = i;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

bool pdguiSettingsStyleCombo(const char *label,
    const PdguiSettingsRegistry &registry, bool staticEnabled,
    const char *activeId, int *selectedIndex)
{
    return registryCombo(label, registry, !staticEnabled, activeId,
        "Procedural (built-in)", pdguiSettingsStylePreview(registry, staticEnabled, activeId),
        selectedIndex);
}

std::string pdguiSettingsRegistryPreview(const PdguiSettingsRegistry &registry,
    const char *activeId, const char *emptyLabel)
{
    if (!activeId || !activeId[0]) return emptyLabel ? emptyLabel : "(none)";
    if (registry.id && registry.name) {
        for (int i = 0; i < registry.count; ++i) {
            const char *id = registry.id(i, registry.userdata);
            if (id && std::strcmp(id, activeId) == 0) {
                const char *name = registry.name(i, registry.userdata);
                return name && name[0] ? name : id;
            }
        }
    }
    return std::string("Unavailable: ") + activeId;
}

bool pdguiSettingsRegistryCombo(const char *label,
    const PdguiSettingsRegistry &registry, const char *activeId,
    const char *emptyLabel, int *selectedIndex)
{
    const char *empty = emptyLabel ? emptyLabel : "(none)";
    return registryCombo(label, registry, !activeId || !activeId[0], activeId,
        empty, pdguiSettingsRegistryPreview(registry, activeId, empty), selectedIndex);
}

bool pdguiSettingsEditorClose(const char *cancelGlyph, bool cancelPressed, float height)
{
    const bool clicked = ImGui::Button("Close##settings_editor_close",
        ImVec2(pdguiSettingsFitWidth(ImGui::GetContentRegionAvail().x), height));
    ImGui::TextWrapped("Back: %s", cancelGlyph && cancelGlyph[0] ? cancelGlyph : "Unbound");
    return clicked || (cancelPressed && pdguiNavActionAllowed(ACTION_MENU_CANCEL));
}
