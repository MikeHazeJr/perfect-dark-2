#include "catalog_readable_ids.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "loader_enum_reverse.h"

static s32 s_startsWith(const char *s, const char *prefix)
{
	return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

static s32 s_isHexOnly(const char *s)
{
	s32 saw = 0;
	for (s32 i = 0; s && s[i]; i++) {
		if (s[i] == '_') {
			continue;
		}
		if (!isxdigit((unsigned char)s[i])) {
			return 0;
		}
		saw = 1;
	}
	return saw;
}

static void s_slugifyBody(const char *src, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';
	if (!src) return;

	size_t w = 0;
	s32 last_was_sep = 1;
	for (size_t r = 0; src[r] && w + 1 < out_n; r++) {
		const unsigned char c = (unsigned char)src[r];
		if (isalnum(c)) {
			out[w++] = (char)tolower(c);
			last_was_sep = 0;
		} else if (!last_was_sep) {
			out[w++] = '_';
			last_was_sep = 1;
		}
	}
	if (w > 0 && out[w - 1] == '_') {
		w--;
	}
	out[w] = '\0';
}

static void s_dropLeadingHexToken(char *slug)
{
	if (!slug || !slug[0]) return;

	size_t i = 0;
	while (slug[i] && slug[i] != '_') {
		if (!isxdigit((unsigned char)slug[i])) {
			return;
		}
		i++;
	}

	if (slug[i] != '_' || !slug[i + 1]) {
		return;
	}

	memmove(slug, slug + i + 1, strlen(slug + i + 1) + 1);
}

void catalogReadableAlphaOrdinal(s32 value, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';

	if (value < 0) {
		snprintf(out, out_n, "unknown");
		return;
	}

	char tmp[24];
	size_t n = 0;
	u32 v = (u32)value;
	for (;;) {
		tmp[n++] = (char)('a' + (v % 26));
		if (v < 26 || n >= sizeof(tmp)) {
			break;
		}
		v = (v / 26) - 1;
	}

	size_t w = 0;
	while (n > 0 && w + 1 < out_n) {
		out[w++] = tmp[--n];
	}
	out[w] = '\0';
}

static void s_slugFromSymbol(const char *symbol, const char *fallback_role,
	s32 ordinal, char *out, size_t out_n)
{
	if (!out || out_n == 0) return;
	out[0] = '\0';

	const char *body = symbol;
	if (body) {
		if (s_startsWith(body, "SFX_")) body += 4;
		else if (s_startsWith(body, "ANIM_")) body += 5;
		else if (s_startsWith(body, "FILE_G")) body += 6;
		else if (s_startsWith(body, "FILE_P")) body += 6;
		else if (s_startsWith(body, "FILE_")) body += 5;
	}

	char slug[96];
	s_slugifyBody(body, slug, sizeof(slug));

	s32 had_leading_hex = 0;
	if (slug[0]) {
		size_t first_sep = 0;
		while (slug[first_sep] && slug[first_sep] != '_') first_sep++;
		if (first_sep > 0 && slug[first_sep] == '_') {
			had_leading_hex = 1;
			for (size_t i = 0; i < first_sep; i++) {
				if (!isxdigit((unsigned char)slug[i])) {
					had_leading_hex = 0;
					break;
				}
			}
		}
	}

	if (had_leading_hex) {
		s_dropLeadingHexToken(slug);
	}

	if (!slug[0] || s_isHexOnly(slug)) {
		char alpha[24];
		catalogReadableAlphaOrdinal(ordinal, alpha, sizeof(alpha));
		snprintf(out, out_n, "%s_%s",
			(fallback_role && fallback_role[0]) ? fallback_role : "unlabeled",
			alpha);
		return;
	}

	if (had_leading_hex) {
		char alpha[24];
		catalogReadableAlphaOrdinal(ordinal, alpha, sizeof(alpha));
		snprintf(out, out_n, "%s_%s", slug, alpha);
		return;
	}

	snprintf(out, out_n, "%s", slug);
}

void catalogReadableAnimationId(s32 anim_idx, const char *fallback_role,
	char *out, size_t out_n)
{
	char slug[128];
	s_slugFromSymbol(loaderEnumNameForAnimEnum(anim_idx),
		(fallback_role && fallback_role[0]) ? fallback_role : "animation",
		anim_idx, slug, sizeof(slug));
	snprintf(out, out_n, "base:animation_%s", slug);
}

void catalogReadableTextureId(s32 texture_idx, char *out, size_t out_n)
{
	char alpha[24];
	catalogReadableAlphaOrdinal(texture_idx, alpha, sizeof(alpha));
	snprintf(out, out_n, "base:texture_unlabeled_%s", alpha);
}

void catalogReadableSfxId(s32 sfx_idx, char *out, size_t out_n)
{
	char slug[128];
	s_slugFromSymbol(loaderEnumNameForSfxEnum(sfx_idx), "unlabeled",
		sfx_idx, slug, sizeof(slug));
	snprintf(out, out_n, "base:sfx_%s", slug);
}

void catalogReadableVoiceId(s32 sfx_idx, char *out, size_t out_n)
{
	char slug[128];
	s_slugFromSymbol(loaderEnumNameForSfxEnum(sfx_idx), "line",
		sfx_idx, slug, sizeof(slug));
	snprintf(out, out_n, "base:voice_%s", slug);
}

void catalogReadableSongId(s32 slot_idx, char *out, size_t out_n)
{
	char alpha[24];
	catalogReadableAlphaOrdinal(slot_idx, alpha, sizeof(alpha));
	snprintf(out, out_n, "base:song_sequence_%s", alpha);
}

void catalogReadableModelIdForFile(s32 filenum, const char *hint_suffix,
	const char *fallback_role, char *out, size_t out_n)
{
	char slug[128];
	s_slugFromSymbol(loaderEnumNameForFileEnum(filenum),
		(fallback_role && fallback_role[0]) ? fallback_role : "unlabeled",
		filenum, slug, sizeof(slug));

	if (hint_suffix && hint_suffix[0]) {
		char hint[48];
		s_slugifyBody(hint_suffix, hint, sizeof(hint));
		if (hint[0]) {
			snprintf(out, out_n, "base:model_%s_%s", slug, hint);
			return;
		}
	}

	snprintf(out, out_n, "base:model_%s", slug);
}

void catalogReadableModelIdForModelnum(s32 modelnum, s32 filenum,
	char *out, size_t out_n)
{
	(void)modelnum;
	catalogReadableModelIdForFile(filenum, NULL, "unlabeled", out, out_n);
}

void catalogReadableStageSceneId(const char *stage_id, const char *slot,
	s32 filenum, char *out, size_t out_n)
{
	char stage[96];
	const char *body = stage_id;
	if (body) {
		const char *colon = strchr(body, ':');
		if (colon) body = colon + 1;
		if (s_startsWith(body, "stage_")) body += 6;
		else if (s_startsWith(body, "arena_")) body += 6;
	}
	s_slugFromSymbol(body, "stage", filenum, stage, sizeof(stage));

	char slot_slug[48];
	s_slugifyBody(slot, slot_slug, sizeof(slot_slug));
	if (!slot_slug[0]) {
		snprintf(slot_slug, sizeof(slot_slug), "scene");
	}

	snprintf(out, out_n, "base:model_%s_%s", stage, slot_slug);
}
