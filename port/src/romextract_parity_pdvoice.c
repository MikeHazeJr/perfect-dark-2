/**
 * romextract_parity_pdvoice.c -- Catalog universality pivot Step 3
 * audio half parity check for .pdvoice (2026-05-03).
 *
 * Thin wrapper that calls the shared parity walker in
 * romextract_parity_pdsfx.c with PDAUDIO_WALK_VOICE. The walker
 * handles the mode-filter to only re-check leaf SFX entries that
 * map to a voice audioconfig slot.
 *
 * Server build: returns 0 (no audio segments populated).
 */

#include <PR/ultratypes.h>

#include "romextract_pd.h"
#include "romextract_pdaudio_internal.h"

s32 romExtractParityCheckPdvoice(void)
{
	return romextract_pdaudio_parityCheck(PDAUDIO_WALK_VOICE);
}
