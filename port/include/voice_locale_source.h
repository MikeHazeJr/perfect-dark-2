/**
 * voice_locale_source.h -- public .pdvoice locale-source helpers.
 *
 * The pure JSON selector is shared with pd-tests. Runtime helpers select the
 * active game locale, a declared locale audio member, and subtitle.json text.
 */

#ifndef _IN_VOICE_LOCALE_SOURCE_H
#define _IN_VOICE_LOCALE_SOURCE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct asset_entry;

#define VOICE_LOCALE_COUNT 6

enum voice_locale_index {
	VOICE_LOCALE_EN = 0,
	VOICE_LOCALE_FR,
	VOICE_LOCALE_DE,
	VOICE_LOCALE_IT,
	VOICE_LOCALE_ES,
	VOICE_LOCALE_JA,
};

/** Return a VOICE_LOCALE_* index for a supported BCP-47 tag, or -1. */
s32 voiceLocaleIndex(const char *locale);

/** Return the canonical short BCP-47 tag for an index, or NULL. */
const char *voiceLocaleTag(s32 index);

/**
 * Parse pd.voice_subtitle.v1 and select requested, fallback, then default.
 * Returns 1 with decoded UTF-8 text, 0 when the document is valid but has no
 * applicable text, and -1 when the source is malformed or has a wrong schema.
 */
s32 voiceSubtitleJsonSelect(const char *json, size_t json_len,
	const char *requested_locale, const char *fallback_locale,
	char *out_text, size_t out_text_size);

/** Return the canonical locale tag selected by the running game. */
const char *voiceLocaleActiveTag(void);

/** Select active locale, declared fallback locale, then default sample. */
const char *voiceLocaleSelectAudioPath(const struct asset_entry *entry);

/**
 * Load and select public subtitle.json text for an entry.
 * Returns 1 when selected, 0 when no subtitle_file or no applicable string,
 * and -1 when a declared subtitle source is missing or invalid.
 */
s32 voiceLocaleLoadSubtitle(const struct asset_entry *entry,
	char *out_text, size_t out_text_size);

#ifdef __cplusplus
}
#endif

#endif
