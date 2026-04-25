/**
 * pdgui_widgets.h -- Priority L (2026-04-25) shared label-left widget helpers.
 *
 * Mike's flat-menu spec rule 5 requires labels ABOVE or to the LEFT of
 * controls, never on the right.  ImGui's default `Checkbox(label, ...)` /
 * `Combo(label, ...)` / `SliderInt(label, ...)` / `SliderFloat(label, ...)`
 * paints the label on the right.  These helpers route every call site
 * through a single point so the layout is consistent across the menu
 * surface:
 *
 *   1. Render the label as `Text` first (left-aligned, frame-padded).
 *   2. SameLine to a fixed label-column width so the widgets align.
 *   3. Render the widget with `##` ID so ImGui suppresses the right-side
 *      label and the widget extends to fill the remaining width.
 *
 * Sound feedback is included in the helpers so per-menu wrappers are
 * unnecessary -- TOGGLEON/OFF for checkbox edges, SUBFOCUS for combo
 * commits.  Sliders are continuous so they don't fire sound (caller
 * may layer one on the per-frame `Edit` return).
 *
 * C++-only header (declared inside the C++ port surface).  Auto-discovered
 * by GLOB_RECURSE in CMakeLists.txt.  Auto-namespaced by file inclusion;
 * no extern "C" needed because every caller is C++.
 */

#ifndef _PDGUI_WIDGETS_H_
#define _PDGUI_WIDGETS_H_

#include <stddef.h>

/* Settings-style label column width in scaled pixels.  Tuned so the
 * longest existing label ("Mouse Sensitivity (X)" et al.) fits with a
 * single space of padding.  Per-menu overrides can call
 * `pdguiSettingsBeginRowAt` directly to set a custom width. */
float pdguiSettingsLabelColWidth(void);

/* Begin a label-left row: render the label as `Text` then `SameLine`
 * to the column width, then `SetNextItemWidth` to fill the remainder.
 * Does NOT call any widget itself -- the caller follows up with the
 * widget call (e.g. `ImGui::Checkbox("##id", ...)`).
 *
 * Use this directly when none of the typed helpers below cover the
 * widget you need (e.g. ColorEdit3, custom widget). */
void pdguiSettingsBeginRow(const char *label);

/* Same as above but with an explicit column width override. */
void pdguiSettingsBeginRowAt(const char *label, float labelColW);

/* Build a `##label`-style ID into the caller's buffer so the widget call
 * suppresses ImGui's default right-side label.  Returns the buffer.
 *
 * Caller-allocated to avoid a static lifetime contract. */
const char *pdguiSettingsHashId(const char *label, char *buf, size_t cap);

/* ---- Typed widget helpers -- single-call replacements for the bare
 * ImGui::Widget(label, ...) form.  Each places the label on the LEFT,
 * fills the row width with the widget, and plays the matching sound on
 * an edit-frame return. ---- */

/* Checkbox.  Returns true on the toggle frame. */
bool pdguiCheckbox(const char *label, bool *v);

/* Combo (string-array form).  `current_item` is a 0-based index into
 * `items[items_count]`.  Returns true on selection change. */
bool pdguiCombo(const char *label, int *current_item,
                const char *const items[], int items_count);

/* SliderInt with format string (default "%d"). */
bool pdguiSliderInt(const char *label, int *v, int v_min, int v_max,
                    const char *format = "%d");

/* SliderFloat with format string (default "%.3f"). */
bool pdguiSliderFloat(const char *label, float *v, float v_min, float v_max,
                      const char *format = "%.3f");

/* InputText.  Buffer-size variant; returns true when the user finishes
 * editing (Enter or focus-loss with EnterReturnsTrue style).  Use the
 * standard ImGui flag set; default is no flags.  Label placement is
 * ABOVE for InputText (free-form entry conventionally reads better with
 * the label on its own line followed by the full-width input). */
bool pdguiInputText(const char *label, char *buf, size_t buf_size,
                    int imgui_flags = 0);

#endif /* _PDGUI_WIDGETS_H_ */
