/**
 * romextract_pdui.c -- Catalog universality pivot Step 3b part 2
 * (2026-05-03).
 *
 * Per-asset UI texture emitter. Thin C wrapper around the C++ emitter
 * that lives in port/fast3d/pdgui_theme.cpp. The actual decode + ZIP
 * write lives there because it shares the UI theme source table and texture
 * decoders; this C wrapper exists so the boot sequence in main.c
 * can call it from C through the same romextract_pd.h surface that
 * exposes the other Step 1 / 2 / 3 / 3a / 3b emitters.
 *
 * Output paths under data/<romid>/ui/:
 *   <slug>.pdui    ZIP compound (manifest + texture.tga + shared _meta)
 *
 * Per universality-pivot-schemas.md Section 2.11.
 *
 * Boot order: this wrapper must be called from main.c at the Step 3b
 * part 2 block. On first launch g_TexGeneralConfigs is not yet
 * populated (texInit runs from pdmain.c::mainInit later in the boot
 * sequence). The boot extractor emits `.pdtexture` first, then this emitter
 * clones rows from g_TcGeneralConfigs and decodes them through a temporary
 * texture pool in extractor-only/headless boots. That lets the texture loader
 * consume public `.pdtexture` source instead of falling through to legacy
 * texture DMA, so fresh extraction still emits the full .pdui set without
 * waiting for texReset() or a render loop. The post-texReset hook remains a
 * safety repair for missing/stale files. Subsequent boots find the .pdui files
 * already on disk and the call is an idempotent skip.
 *
 * Server build: returns 0 immediately (no GL context, no texture
 * system, no UI rendering on the dedicated server side).
 */

#include <PR/ultratypes.h>

#include "romextract_pd.h"
#include "system.h"

/* extern "C" declaration of the C++ emitter that lives in
 * port/fast3d/pdgui_theme.cpp. The C++ side wraps the implementation
 * in extern "C" so this prototype matches by linkage. */
extern int pdguiThemeEmitPduiZips(int force);

s32 romExtractAllPdui(s32 force_rewrite)
{
#if defined(PD_SERVER)
	(void)force_rewrite;
	return 0;
#else
	return (s32)pdguiThemeEmitPduiZips((int)force_rewrite);
#endif
}
