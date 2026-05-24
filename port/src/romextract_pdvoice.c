/**
 * romextract_pdvoice.c -- Catalog universality pivot Step 3 audio
 * half (2026-05-03).
 *
 * Voice-line emitter. Calls the shared SFX-bank walker in
 * romextract_pdsfx.c with PDAUDIO_WALK_VOICE, which restricts the
 * walk to leaf SFX indices that map (via g_AudioRussMappings) to a
 * voice audioconfig slot per the Slice 10 retag predicate.
 *
 * The emitted .pdvoice ZIP shares the clean .pdsfx container layout
 * (_meta/manifest.json + sample.wav + _meta/sample.wav.sha256). The manifest
 * carries pd_kind="voice" plus actor/transcript/language/context
 * placeholder fields per universality-pivot-schemas.md Section 2.8;
 * a curation pass at Step 5 (or in a follow-up worktree) fills the
 * placeholder fields per voice slot.
 *
 * Per Mike's Q-2 (audio type-tolerance): consumers reading audio
 * refs (e.g. weapon shootsound) accept any audio kind -- .pdvoice
 * can be used in place of .pdsfx without engine changes. Voice
 * misclassification at extract time stays recoverable for that
 * reason; the per-line actor curation lands without breaking refs.
 *
 * Server build: thin wrapper, returns 0; the underlying walker is
 * also a no-op on the server (snd.c not linked).
 */

#include <PR/ultratypes.h>

#include "romextract_pd.h"
#include "romextract_pdaudio_internal.h"

s32 romExtractAllPdvoice(s32 force_rewrite)
{
	return romextract_pdaudio_walkBank(PDAUDIO_WALK_VOICE, force_rewrite);
}
