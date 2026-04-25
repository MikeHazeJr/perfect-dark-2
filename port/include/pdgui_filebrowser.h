/**
 * pdgui_filebrowser.h -- In-engine ImGui file browser dialog.
 *
 * Shared component for audio import and skin image import.
 * Controller-navigable (D-pad scroll, A select/enter, B back/cancel).
 * No native OS dialogs — pure ImGui for consistent controller flow.
 *
 * Usage:
 *   pdguiFileBrowserOpen("Import Audio", "mods", ".mp3;.wav;.ogg");
 *   // Each frame:
 *   if (pdguiFileBrowserRender()) {
 *       // User confirmed — path is in pdguiFileBrowserGetPath()
 *       doSomethingWith(pdguiFileBrowserGetPath());
 *       pdguiFileBrowserClose();
 *   }
 *   if (!pdguiFileBrowserIsOpen()) { // cancelled }
 *
 * Auto-discovered by CMake GLOB_RECURSE.
 */

#ifndef _IN_PDGUI_FILEBROWSER_H
#define _IN_PDGUI_FILEBROWSER_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open the file browser popup.
 *
 * @param title     Window title (e.g. "Import Audio")
 * @param startDir  Initial directory to browse (relative or absolute).
 *                  If NULL/empty, defaults to "mods".
 * @param filters   Semicolon-separated extension list including dots,
 *                  e.g. ".mp3;.wav;.ogg" or ".png;.jpg;.bmp;.tga".
 *                  If NULL/empty, shows all files.
 */
void pdguiFileBrowserOpen(const char *title, const char *startDir, const char *filters);

/**
 * Close the file browser and reset state.
 */
void pdguiFileBrowserClose(void);

/**
 * Returns non-zero if the file browser is currently open.
 */
s32 pdguiFileBrowserIsOpen(void);

/**
 * Render the file browser popup. Call each frame.
 *
 * Returns non-zero if the user confirmed a file selection.
 * After confirmation, call pdguiFileBrowserGetPath() to get the result.
 */
s32 pdguiFileBrowserRender(void);

/**
 * Get the selected file's full path after confirmation.
 * Valid until the next pdguiFileBrowserOpen() or pdguiFileBrowserClose().
 */
const char *pdguiFileBrowserGetPath(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FILEBROWSER_H */
