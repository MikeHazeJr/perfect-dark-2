/**
 * screenmfst.h -- Phase 6: Menu/UI screen asset mini-manifests.
 *
 * Each ImGui menu screen may declare a "mini-manifest" — a list of catalog
 * asset IDs it needs loaded.  As screens enter and leave, catalogLoadAsset /
 * catalogUnloadAsset are called so assets stay resident while a screen is
 * visible and are released when it is dismissed.
 *
 * Phase 5 ref counting handles shared assets correctly: an asset needed by
 * two overlapping screens is only freed after both screens have left.
 *
 * Integration:
 *   1. Call screenManifestRegister() during screen init, alongside
 *      pdguiHotswapRegister(), passing the same menudialogdef pointer.
 *   2. screenManifestTick() is called automatically from
 *      pdguiHotswapRenderQueued() — no per-screen boilerplate needed.
 *   3. Screens with no mini-manifest work unchanged.
 *
 * Asset types: use MANIFEST_TYPE_* constants from netmanifest.h.
 *
 * Note on base-game assets: all bundled base-game bodies, heads, lang banks,
 * etc. carry bundled=1, so catalogLoadAsset / catalogUnloadAsset are no-ops
 * for them.  Mini-manifests for base-game content are still useful as
 * documentation and as a compatibility layer for mod overrides of those assets
 * (mod entries are non-bundled and go through the full ref-counted lifecycle).
 */

#ifndef _IN_SCREENMFST_H
#define _IN_SCREENMFST_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum catalog IDs a single screen manifest may declare. */
#define SMFST_MAX_IDS_PER_SCREEN  32

/* Maximum number of registered screens. */
#define SMFST_MAX_SCREENS         64

/* Maximum simultaneous active screens tracked per frame. */
#define SMFST_MAX_ACTIVE          16

/**
 * Register a mini-manifest for a menu screen.
 *
 * dialogdef:   Pointer to the menudialogdef that identifies this screen.
 *              Passed as void* to avoid pulling game headers into port code.
 * catalog_ids: NULL-terminated array of catalog string IDs this screen needs.
 *              (e.g. "base:dark_combat", "base:lang_mpmenu")
 * types:       Array of MANIFEST_TYPE_* constants, one per catalog_id.
 * count:       Number of entries.  Clamped to SMFST_MAX_IDS_PER_SCREEN.
 *
 * Calling again for the same dialogdef replaces the previous registration.
 * Safe to call from C++ menu registration functions via extern "C".
 */
void screenManifestRegister(void *dialogdef,
                             const char **catalog_ids,
                             const u8 *types,
                             s32 count);

/**
 * Per-frame tick: detect enter/leave events and drive asset lifecycle.
 *
 * active_defs: Array of menudialogdef* pointers active this frame.
 * count:       Number of entries in active_defs.
 *
 * Called from pdguiHotswapRenderQueued() after processing the render queue.
 * On enter (newly visible screen): calls catalogLoadAsset() for each ID.
 * On leave (screen no longer visible): calls catalogUnloadAsset() for each ID.
 *
 * Internal — not intended for direct call outside of pdgui_hotswap.cpp.
 */
void screenManifestTick(void **active_defs, s32 count);

/**
 * Shut down the screen manifest system.
 *
 * Unloads assets loaded on behalf of any currently-active screens, then
 * clears all registry state.  Called from pdguiHotswapShutdown().
 */
void screenManifestShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SCREENMFST_H */
