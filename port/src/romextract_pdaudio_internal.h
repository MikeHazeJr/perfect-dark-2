/**
 * romextract_pdaudio_internal.h -- Shared walker glue between
 * romextract_pdsfx.c and romextract_pdvoice.c (Step 3 audio half,
 * 2026-05-03).
 *
 * The two emitters iterate the same sfxctl ALBankFile (the leaf SFX
 * bank) and share the same per-sound byte slicer over sfxtbl. They
 * differ only in the per-sound filter:
 *   .pdsfx    emits the entry if the leaf is NOT classified as voice
 *             (i.e. no russ-mapping points it at a voice audioconfig
 *             slot).
 *   .pdvoice  emits the entry if it IS classified as voice.
 *
 * One callable, one filter knob -- no diff between the two files in
 * how they walk the bank or slice the bytes. This keeps the byte
 * layout interpretation in a single place; format mistakes can only
 * be made once instead of twice.
 *
 * Not exposed in the public romextract_pd.h surface; this header
 * lives under port/src/ alongside its callers because no out-of-tree
 * consumer should touch it.
 */
#ifndef _IN_ROMEXTRACT_PDAUDIO_INTERNAL_H
#define _IN_ROMEXTRACT_PDAUDIO_INTERNAL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Walk modes for romextract_pdaudio_walkBank. */
typedef enum {
	PDAUDIO_WALK_SFX = 0,    /* emit non-voice leaves to .pdsfx */
	PDAUDIO_WALK_VOICE = 1,  /* emit voice leaves to .pdvoice */
} pdaudio_walk_mode_t;

/**
 * Walk the leaf SFX bank (post-preprocess sfxctl + sfxtbl segments)
 * and emit one ZIP compound per sound matching the mode filter.
 *
 * mode          PDAUDIO_WALK_SFX or PDAUDIO_WALK_VOICE.
 * force_rewrite Non-zero forces re-emit of files that already exist.
 *
 * Returns count of files newly written, or -1 on infrastructure
 * failure (segment lookup, data dir creation, etc.). Per-sound
 * failures emit LOUDFAIL.EXTRACT.PDSFX or LOUDFAIL.EXTRACT.PDVOICE
 * (per the mode) but do not abort the walk.
 *
 * Server build: returns 0 immediately; the russ-mapping table linkage
 * lives in snd.c which is excluded from pd-server.
 */
s32 romextract_pdaudio_walkBank(pdaudio_walk_mode_t mode, s32 force_rewrite);

/**
 * Re-walk the leaf SFX bank in mode-filter form and verify each
 * emitted ZIP's manifest envelope + key scalar fields round-trip the
 * source bank entry. Same Q-5 structural-integrity contract as the
 * other parity checks. Returns the count of mismatched files.
 */
s32 romextract_pdaudio_parityCheck(pdaudio_walk_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ROMEXTRACT_PDAUDIO_INTERNAL_H */
