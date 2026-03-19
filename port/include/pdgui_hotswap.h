#ifndef _IN_PDGUI_HOTSWAP_H
#define _IN_PDGUI_HOTSWAP_H

/**
 * pdgui_hotswap.h -- In-game OLD/NEW menu hot-swap system.
 *
 * F8 toggles between the original PD menu renderer and ImGui replacements
 * for any menu that has a registered builder.  Unlike the F11 storyboard
 * (which is a sandboxed dev preview), hot-swap operates on LIVE menus:
 * the ImGui version receives real game state and its actions feed back
 * into the game.
 *
 * Architecture:
 *   1. Each ImGui menu registers itself via pdguiHotswapRegister() at init,
 *      providing the address of the original menudialogdef it replaces and
 *      a render callback.
 *   2. In menu.c, menuRenderDialog() calls pdguiHotswapCheck() before
 *      rendering.  If hot-swap is active AND the dialog has a registered
 *      replacement, the PD native render is skipped and ImGui draws instead.
 *   3. The ImGui render callback receives the live dialog struct so it can
 *      read/write game state (selected item, scroll position, etc.).
 *   4. F8 toggles the master switch.  Individual menus can also be toggled
 *      via pdguiHotswapSetOverride().
 *
 * Part of Phase D4 -- Menu Migration.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid pulling game headers into C++ */
struct menudialogdef;
struct menudialog;
struct menu;

/* ---- Lifecycle ---- */

void pdguiHotswapInit(void);
void pdguiHotswapShutdown(void);

/* ---- Registration ---- */

/**
 * Callback type for ImGui menu replacements.
 *
 * dialog:  The live PD dialog struct (read game state, selected item, etc.)
 * menu:    The parent menu struct (player context, depth, etc.)
 * winW/H:  SDL window dimensions for layout.
 *
 * Return 1 if the ImGui menu handled rendering (skip PD native).
 * Return 0 to fall through to PD native rendering.
 */
typedef s32 (*PdguiMenuRenderFn)(struct menudialog *dialog,
                                  struct menu *menu,
                                  s32 winW, s32 winH);

/**
 * Register an ImGui replacement for a PD dialog.
 *
 * dialogdef:  Pointer to the original menudialogdef struct (e.g.,
 *             &g_FilemgrFileSelectMenuDialog).
 * renderFn:   The ImGui render callback.
 * name:       Human-readable name for debug display (e.g., "Agent Select").
 *
 * Returns the slot index, or -1 on failure.
 */
s32 pdguiHotswapRegister(struct menudialogdef *dialogdef,
                          PdguiMenuRenderFn renderFn,
                          const char *name);

/**
 * Register a type-based fallback ImGui renderer.
 * When pdguiHotswapCheck finds no definition-specific match, it falls back
 * to a type renderer (if one is registered for the dialog's type).
 *
 * dialogType: MENUDIALOGTYPE_* constant (1=DEFAULT, 2=DANGER, 3=SUCCESS, etc.)
 * renderFn:   The ImGui render callback.
 * name:       Human-readable name for debug display (e.g., "Warning Dialog").
 *
 * Returns 0 on success, -1 on failure.
 */
s32 pdguiHotswapRegisterType(u8 dialogType,
                              PdguiMenuRenderFn renderFn,
                              const char *name);

/* ---- Per-frame Hook ---- */

/**
 * Called from menuRenderDialog() BEFORE the PD native render.
 *
 * If hot-swap is active AND this dialog has a registered ImGui replacement:
 *   - Calls the ImGui render function
 *   - Returns 1 (caller should skip PD native render for this dialog)
 *
 * If hot-swap is off OR no replacement exists:
 *   - Returns 0 (caller proceeds with PD native render)
 *
 * dialogdef:  The dialog definition being rendered.
 * dialog:     The live dialog instance.
 * menu:       The parent menu struct.
 */
s32 pdguiHotswapCheck(struct menudialogdef *dialogdef,
                       struct menudialog *dialog,
                       struct menu *menu);

/* ---- Toggle / Query ---- */

/**
 * Toggle hot-swap on/off (bound to F8).
 * When turning on: ImGui menus replace PD native for registered dialogs.
 * When turning off: all menus revert to PD native rendering.
 */
void pdguiHotswapToggle(void);

/** Is hot-swap currently active? */
s32 pdguiHotswapIsActive(void);

/** Are there queued dialogs awaiting ImGui render this frame? */
s32 pdguiHotswapHasQueued(void);

/** Were hot-swap menus rendered in the most recent frame?
 * Persists through the next frame's input polling phase, unlike
 * pdguiHotswapHasQueued() which is only valid during GBI/render. */
s32 pdguiHotswapWasActive(void);

/**
 * Render all queued hot-swap dialogs via ImGui.
 * Called from pdguiRender() during the overlay phase, AFTER pdguiNewFrame().
 * This is the second half of the two-phase approach: pdguiHotswapCheck()
 * queues during the GBI phase, this renders during the ImGui phase.
 */
void pdguiHotswapRenderQueued(s32 winW, s32 winH);

/**
 * Check if a specific dialog definition is currently being hot-swapped
 * (i.e., has a registered ImGui replacement AND hot-swap is active or
 * forced NEW). Used by menuProcessInput() to suppress PD input processing
 * for dialogs that ImGui is handling.
 *
 * Returns 1 if ImGui is handling this dialog, 0 otherwise.
 */
s32 pdguiHotswapIsDialogSwapped(struct menudialogdef *dialogdef);

/* ---- Per-menu Override ---- */

/**
 * Force a specific dialog to always use OLD or NEW regardless of F8 state.
 * override: 0 = follow F8 global toggle (default)
 *           1 = force NEW (always ImGui)
 *          -1 = force OLD (always PD native)
 */
void pdguiHotswapSetOverride(struct menudialogdef *dialogdef, s32 override);

/* ---- Status Display ---- */

/**
 * Render a small overlay badge showing current hot-swap state.
 * Called from pdguiRender() when any menu is active.
 * Shows "[NEW]" or "[OLD]" in the corner with the current menu name.
 */
void pdguiHotswapRenderBadge(s32 winW, s32 winH);

/* ---- Stats ---- */

/** How many menus have ImGui replacements registered? */
s32 pdguiHotswapRegisteredCount(void);

/** How many total menus exist in the game? (for progress display) */
s32 pdguiHotswapTotalMenuCount(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_HOTSWAP_H */
