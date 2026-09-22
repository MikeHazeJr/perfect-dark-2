#pragma once

#include <string>

// Caller supplies the actual registry. No copied/capped item array is used.
struct PdguiSettingsRegistry {
    int count;
    const char *(*id)(int index, void *userdata);
    const char *(*name)(int index, void *userdata);
    void *userdata;
};

enum PdguiSettingsThemeAction {
    PDGUI_SETTINGS_THEME_NONE,
    PDGUI_SETTINGS_THEME_ENABLE,
    PDGUI_SETTINGS_THEME_DISABLE,
    PDGUI_SETTINGS_THEME_DELETE
};

// Inside a caller's stable theme PushID scope. The caller may also call
// OpenPopupOnItemClick("##theme_actions") after the theme preview button.
// Glyph strings are obtained live by the caller from pdguiGlyphGetActionLabel.
PdguiSettingsThemeAction pdguiSettingsThemeActions(bool enabled,
    const char *acceptGlyph, float width, float height);

std::string pdguiSettingsStylePreview(const PdguiSettingsRegistry &registry,
    bool staticEnabled, const char *activeId);
// A changed selection returns its registry index; -1 means procedural.
bool pdguiSettingsStyleCombo(const char *label,
    const PdguiSettingsRegistry &registry, bool staticEnabled,
    const char *activeId, int *selectedIndex);

// General catalog-backed choice (for optional theme dependencies). Empty IDs
// select the explicit empty item; unknown nonempty IDs retain their identity.
std::string pdguiSettingsRegistryPreview(const PdguiSettingsRegistry &registry,
    const char *activeId, const char *emptyLabel);
bool pdguiSettingsRegistryCombo(const char *label,
    const PdguiSettingsRegistry &registry, const char *activeId,
    const char *emptyLabel, int *selectedIndex);

// Labels stack above their controls and never impose a minimum outside the
// actual available region. The next widget must use a hidden native label.
void pdguiSettingsStackedRow(const char *label);
float pdguiSettingsFitWidth(float preferred);

// Native button plus the same owner-filtered mapped Back used by the caller.
// The native ownership check is retained here to protect nested/finishing
// editors even if a caller supplies an unfiltered action edge.
bool pdguiSettingsEditorClose(const char *cancelGlyph, bool cancelPressed,
    float height);
