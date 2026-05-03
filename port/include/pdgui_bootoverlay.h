#ifndef PDGUI_BOOTOVERLAY_H
#define PDGUI_BOOTOVERLAY_H

/* Boot Overlay (Engine Phase 2)
 *
 * Plain bottom progress bar plus phase / status label, PD-themed via
 * the active pdgui_theme palette (Mike Q2, 2026-05-03).  Renders during
 * the worker-driven catalog work so the user sees a responsive window
 * instead of a "Not Responding" freeze.
 *
 * The overlay reads boot_progress.h state each frame.  No per-frame
 * memory allocation; uses ImGui DrawList primitives only.  No textures.
 *
 * Lifecycle: pdguiBootOverlayInit() registers Boot.OverlayEnabled and
 * sets up internal state.  pdguiBootOverlayPump() drives ONE complete
 * frame (event pump + GL clear + ImGui frame + swap) and is called from
 * the main thread in a tight loop while bootProgressIsComplete() == 0.
 * pdguiBootOverlayShutdown() tears down internal state once boot
 * completes.
 *
 * Intentionally independent of pdguiNewFrame / pdguiRender, which gate
 * on game state we don't have yet at boot time.
 */

#ifdef __cplusplus
extern "C" {
#endif

void pdguiBootOverlayInit(void);
void pdguiBootOverlayShutdown(void);

/* Single-frame pump: handle SDL events, draw the overlay, swap buffers.
 * Skips render entirely for dedicated server (g_NetDedicated == 1).
 * Returns immediately if pdguiInit hasn't run yet. */
void pdguiBootOverlayPump(void);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_BOOTOVERLAY_H */
