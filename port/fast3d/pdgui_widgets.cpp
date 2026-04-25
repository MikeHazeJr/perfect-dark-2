/**
 * pdgui_widgets.cpp -- Priority L (2026-04-25) shared label-left widget helpers.
 *
 * See pdgui_widgets.h for the contract.  Auto-discovered by GLOB_RECURSE
 * for port-cpp in CMakeLists.txt.
 */

#include <stdio.h>
#include <string.h>
#include "imgui/imgui.h"

#include "pdgui_widgets.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "pdgui_scaling.h"

float pdguiSettingsLabelColWidth(void)
{
    /* Tuned so the longest existing label fits with a single-space pad
     * at standard pdgui scale.  Per-menu overrides should pass an
     * explicit width to `pdguiSettingsBeginRowAt` instead of patching
     * this global. */
    return pdguiScale(220.0f);
}

void pdguiSettingsBeginRowAt(const char *label, float labelColW)
{
    ImGui::AlignTextToFramePadding();
    if (label && label[0]) {
        ImGui::TextUnformatted(label);
    } else {
        ImGui::TextUnformatted(" ");
    }
    ImGui::SameLine(labelColW);

    /* Stretch the widget across the remaining row width.  Reserve a
     * minimum so a tight panel (e.g. narrow modal) doesn't collapse
     * the widget to zero. */
    float remaining = ImGui::GetContentRegionAvail().x;
    if (remaining < pdguiScale(80.0f)) {
        remaining = pdguiScale(80.0f);
    }
    ImGui::SetNextItemWidth(remaining);
}

void pdguiSettingsBeginRow(const char *label)
{
    pdguiSettingsBeginRowAt(label, pdguiSettingsLabelColWidth());
}

const char *pdguiSettingsHashId(const char *label, char *buf, size_t cap)
{
    if (!buf || cap == 0) {
        return "##";
    }
    /* ImGui treats everything before `##` as the label and everything
     * after as the widget ID.  Beginning with `##` tells ImGui the
     * label is empty; the rest is the unique ID. */
    snprintf(buf, cap, "##%s", label ? label : "");
    return buf;
}

bool pdguiCheckbox(const char *label, bool *v)
{
    pdguiSettingsBeginRow(label);
    char id[160];
    bool changed = ImGui::Checkbox(pdguiSettingsHashId(label, id, sizeof(id)), v);
    if (changed && v) {
        pdguiPlaySound(*v ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
    }
    return changed;
}

bool pdguiCombo(const char *label, int *current_item,
                const char *const items[], int items_count)
{
    pdguiSettingsBeginRow(label);
    char id[160];
    bool changed = ImGui::Combo(pdguiSettingsHashId(label, id, sizeof(id)),
                                current_item, items, items_count);
    if (changed) {
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    return changed;
}

bool pdguiSliderInt(const char *label, int *v, int v_min, int v_max,
                    const char *format)
{
    pdguiSettingsBeginRow(label);
    char id[160];
    return ImGui::SliderInt(pdguiSettingsHashId(label, id, sizeof(id)),
                            v, v_min, v_max, format);
}

bool pdguiSliderFloat(const char *label, float *v, float v_min, float v_max,
                      const char *format)
{
    pdguiSettingsBeginRow(label);
    char id[160];
    return ImGui::SliderFloat(pdguiSettingsHashId(label, id, sizeof(id)),
                              v, v_min, v_max, format);
}

bool pdguiInputText(const char *label, char *buf, size_t buf_size,
                    int imgui_flags)
{
    /* Label ABOVE for free-form text -- conventionally clearer than
     * left-aligned for InputText. */
    if (label && label[0]) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
    }
    char id[160];
    float remaining = ImGui::GetContentRegionAvail().x;
    if (remaining < pdguiScale(80.0f)) {
        remaining = pdguiScale(80.0f);
    }
    ImGui::SetNextItemWidth(remaining);
    return ImGui::InputText(pdguiSettingsHashId(label, id, sizeof(id)),
                            buf, buf_size, (ImGuiInputTextFlags)imgui_flags);
}
