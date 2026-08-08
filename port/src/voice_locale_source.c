/**
 * voice_locale_source.c -- active-locale .pdvoice source selection.
 *
 * subtitle.json is deliberately small and creator-facing:
 * {
 *   "schema": "pd.voice_subtitle.v1",
 *   "default": "Optional fallback text",
 *   "en": "English text",
 *   "fr": "Texte francais",
 *   "de": "Deutscher Text",
 *   "it": "Testo italiano",
 *   "es": "Texto espanol",
 *   "ja": "Japanese UTF-8 text"
 * }
 *
 * Unknown fields are allowed for forward-compatible authoring metadata, but
 * the schema and every recognized subtitle value must be JSON strings.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "constants.h"
#include "data.h"
#include "assetcatalog.h"
#include "fs.h"
#include "system.h"
#include "voice_locale_source.h"

static const char *const s_LocaleTags[VOICE_LOCALE_COUNT] = {
	"en", "fr", "de", "it", "es", "ja"
};

s32 voiceLocaleIndex(const char *locale)
{
	char normalized[16];
	size_t i = 0;

	if (!locale || !locale[0]) {
		return -1;
	}
	while (locale[i] && i + 1 < sizeof(normalized)) {
		char c = (char)tolower((u8)locale[i]);
		if (c == '_') c = '-';
		normalized[i] = c;
		i++;
	}
	normalized[i] = '\0';

	if (strcmp(normalized, "jp") == 0) return VOICE_LOCALE_JA;
	for (i = 0; i < VOICE_LOCALE_COUNT; i++) {
		size_t tag_len = strlen(s_LocaleTags[i]);
		if (strcmp(normalized, s_LocaleTags[i]) == 0
				|| (strncmp(normalized, s_LocaleTags[i], tag_len) == 0
					&& normalized[tag_len] == '-')) {
			return (s32)i;
		}
	}
	return -1;
}

const char *voiceLocaleTag(s32 index)
{
	return index >= 0 && index < VOICE_LOCALE_COUNT
		? s_LocaleTags[index] : NULL;
}

static const char *s_skipWs(const char *p, const char *end)
{
	while (p < end && isspace((u8)*p)) p++;
	return p;
}

static s32 s_hexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static s32 s_appendUtf8(char *out, size_t out_size, size_t *used, u32 cp)
{
	u8 bytes[4];
	size_t count;

	if (cp <= 0x7f) {
		bytes[0] = (u8)cp; count = 1;
	} else if (cp <= 0x7ff) {
		bytes[0] = (u8)(0xc0 | (cp >> 6));
		bytes[1] = (u8)(0x80 | (cp & 0x3f)); count = 2;
	} else if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
		bytes[0] = (u8)(0xe0 | (cp >> 12));
		bytes[1] = (u8)(0x80 | ((cp >> 6) & 0x3f));
		bytes[2] = (u8)(0x80 | (cp & 0x3f)); count = 3;
	} else if (cp <= 0x10ffff) {
		bytes[0] = (u8)(0xf0 | (cp >> 18));
		bytes[1] = (u8)(0x80 | ((cp >> 12) & 0x3f));
		bytes[2] = (u8)(0x80 | ((cp >> 6) & 0x3f));
		bytes[3] = (u8)(0x80 | (cp & 0x3f)); count = 4;
	} else {
		return 0;
	}
	if (*used + count >= out_size) return 0;
	memcpy(out + *used, bytes, count);
	*used += count;
	out[*used] = '\0';
	return 1;
}

static s32 s_readString(const char **cursor, const char *end,
	char *out, size_t out_size)
{
	const char *p = *cursor;
	size_t used = 0;

	if (!out || out_size == 0 || p >= end || *p != '"') return 0;
	out[0] = '\0';
	p++;
	while (p < end) {
		u8 c = (u8)*p++;
		if (c == '"') {
			*cursor = p;
			return 1;
		}
		if (c < 0x20) return 0;
		if (c != '\\') {
			if (used + 1 >= out_size) return 0;
			out[used++] = (char)c;
			out[used] = '\0';
			continue;
		}
		if (p >= end) return 0;
		c = (u8)*p++;
		switch (c) {
		case '"': case '\\': case '/':
			if (used + 1 >= out_size) return 0;
			out[used++] = (char)c; out[used] = '\0'; break;
		case 'b': c = '\b'; goto append_control;
		case 'f': c = '\f'; goto append_control;
		case 'n': c = '\n'; goto append_control;
		case 'r': c = '\r'; goto append_control;
		case 't': c = '\t';
		append_control:
			if (used + 1 >= out_size) return 0;
			out[used++] = (char)c; out[used] = '\0'; break;
		case 'u': {
			u32 cp = 0;
			for (s32 i = 0; i < 4; i++) {
				s32 h;
				if (p >= end || (h = s_hexValue(*p++)) < 0) return 0;
				cp = (cp << 4) | (u32)h;
			}
			if (cp >= 0xd800 && cp <= 0xdbff) {
				u32 low = 0;
				if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return 0;
				p += 2;
				for (s32 i = 0; i < 4; i++) {
					s32 h = s_hexValue(*p++);
					if (h < 0) return 0;
					low = (low << 4) | (u32)h;
				}
				if (low < 0xdc00 || low > 0xdfff) return 0;
				cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
			} else if (cp >= 0xdc00 && cp <= 0xdfff) {
				return 0;
			}
			if (!s_appendUtf8(out, out_size, &used, cp)) return 0;
			break;
		}
		default: return 0;
		}
	}
	return 0;
}

static const char *s_skipString(const char *p, const char *end)
{
	if (p >= end || *p++ != '"') return NULL;
	while (p < end) {
		if ((u8)*p < 0x20) return NULL;
		if (*p == '"') return p + 1;
		if (*p++ == '\\') {
			if (p >= end) return NULL;
			if (*p == 'u') {
				u32 cp = 0;
				p++;
				for (s32 i = 0; i < 4; i++) {
					s32 h;
					if (p >= end || (h = s_hexValue(*p++)) < 0) return NULL;
					cp = (cp << 4) | (u32)h;
				}
				if (cp >= 0xd800 && cp <= 0xdbff) {
					u32 low = 0;
					if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return NULL;
					p += 2;
					for (s32 i = 0; i < 4; i++) {
						s32 h = s_hexValue(*p++);
						if (h < 0) return NULL;
						low = (low << 4) | (u32)h;
					}
					if (low < 0xdc00 || low > 0xdfff) return NULL;
				} else if (cp >= 0xdc00 && cp <= 0xdfff) {
					return NULL;
				}
			} else {
				if (!strchr("\"\\/bfnrt", *p)) return NULL;
				p++;
			}
		}
	}
	return NULL;
}

static const char *s_skipValue(const char *p, const char *end)
{
	char stack[32];
	s32 depth = 0;

	p = s_skipWs(p, end);
	if (p >= end) return NULL;
	if (*p == '"') return s_skipString(p, end);
	if (*p == '{' || *p == '[') {
		stack[depth++] = *p++ == '{' ? '}' : ']';
		while (p < end && depth > 0) {
			if (*p == '"') {
				p = s_skipString(p, end);
				if (!p) return NULL;
				continue;
			}
			if (*p == '{' || *p == '[') {
				if (depth >= (s32)(sizeof(stack) / sizeof(stack[0]))) return NULL;
				stack[depth++] = *p++ == '{' ? '}' : ']';
				continue;
			}
			if (*p == '}' || *p == ']') {
				if (*p != stack[depth - 1]) return NULL;
				p++;
				depth--;
				continue;
			}
			p++;
		}
		return depth == 0 ? p : NULL;
	}
	if ((end - p >= 4 && memcmp(p, "true", 4) == 0)) return p + 4;
	if ((end - p >= 5 && memcmp(p, "false", 5) == 0)
			|| (end - p >= 4 && memcmp(p, "null", 4) == 0)) {
		return p + (*p == 'f' ? 5 : 4);
	}
	{
		const char *number = p;
		if (p < end && *p == '-') p++;
		if (p >= end) return NULL;
		if (*p == '0') {
			p++;
		} else if (*p >= '1' && *p <= '9') {
			while (p < end && isdigit((u8)*p)) p++;
		} else {
			return NULL;
		}
		if (p < end && *p == '.') {
			p++;
			if (p >= end || !isdigit((u8)*p)) return NULL;
			while (p < end && isdigit((u8)*p)) p++;
		}
		if (p < end && (*p == 'e' || *p == 'E')) {
			p++;
			if (p < end && (*p == '+' || *p == '-')) p++;
			if (p >= end || !isdigit((u8)*p)) return NULL;
			while (p < end && isdigit((u8)*p)) p++;
		}
		return p > number ? p : NULL;
	}
}

s32 voiceSubtitleJsonSelect(const char *json, size_t json_len,
	const char *requested_locale, const char *fallback_locale,
	char *out_text, size_t out_text_size)
{
	char values[VOICE_LOCALE_COUNT][512];
	char default_text[512] = "";
	char schema[64] = "";
	const char *p;
	const char *end;
	s32 requested;
	s32 fallback;

	if (!json || !out_text || out_text_size == 0) return -1;
	memset(values, 0, sizeof(values));
	out_text[0] = '\0';
	p = s_skipWs(json, json + json_len);
	end = json + json_len;
	if (p >= end || *p++ != '{') return -1;

	for (;;) {
		char key[64];
		char value[512];
		s32 locale_index;
		p = s_skipWs(p, end);
		if (p < end && *p == '}') { p++; break; }
		if (!s_readString(&p, end, key, sizeof(key))) return -1;
		p = s_skipWs(p, end);
		if (p >= end || *p++ != ':') return -1;
		p = s_skipWs(p, end);
		locale_index = voiceLocaleIndex(key);
		if (strcmp(key, "schema") == 0 || strcmp(key, "default") == 0
				|| locale_index >= 0) {
			if (!s_readString(&p, end, value, sizeof(value))) return -1;
			if (strcmp(key, "schema") == 0) {
				strncpy(schema, value, sizeof(schema) - 1);
			} else if (strcmp(key, "default") == 0) {
				strncpy(default_text, value, sizeof(default_text) - 1);
			} else {
				strncpy(values[locale_index], value,
					sizeof(values[locale_index]) - 1);
			}
		} else {
			p = s_skipValue(p, end);
			if (!p) return -1;
		}
		p = s_skipWs(p, end);
		if (p < end && *p == ',') { p++; continue; }
		if (p < end && *p == '}') { p++; break; }
		return -1;
	}
	p = s_skipWs(p, end);
	if (p != end || strcmp(schema, "pd.voice_subtitle.v1") != 0) return -1;

	requested = voiceLocaleIndex(requested_locale);
	fallback = voiceLocaleIndex(fallback_locale);
	if (requested >= 0 && values[requested][0]) {
		strncpy(out_text, values[requested], out_text_size - 1);
	} else if (fallback >= 0 && values[fallback][0]) {
		strncpy(out_text, values[fallback], out_text_size - 1);
	} else if (default_text[0]) {
		strncpy(out_text, default_text, out_text_size - 1);
	} else {
		return 0;
	}
	out_text[out_text_size - 1] = '\0';
	return 1;
}

#ifndef VOICE_LOCALE_PURE_ONLY
const char *voiceLocaleActiveTag(void)
{
#if VERSION == VERSION_JPN_FINAL
	return "ja";
#elif VERSION >= VERSION_PAL_BETA
	if (g_LanguageId == LANGUAGE_PAL_FR) return "fr";
	if (g_LanguageId == LANGUAGE_PAL_DE) return "de";
	if (g_LanguageId == LANGUAGE_PAL_IT) return "it";
	if (g_LanguageId == LANGUAGE_PAL_ES) return "es";
	return "en";
#else
	return "en";
#endif
}

const char *voiceLocaleSelectAudioPath(const struct asset_entry *entry)
{
	s32 active;
	s32 fallback;

	if (!entry || entry->type != ASSET_AUDIO
			|| entry->ext.audio.category != AUDIO_CAT_VOICE) {
		return NULL;
	}
	active = voiceLocaleIndex(voiceLocaleActiveTag());
	if (active >= 0 && entry->ext.audio.locale_audio_files[active][0]) {
		return entry->ext.audio.locale_audio_files[active];
	}
	fallback = voiceLocaleIndex(entry->ext.audio.fallback_locale);
	if (fallback >= 0 && fallback != active
			&& entry->ext.audio.locale_audio_files[fallback][0]) {
		return entry->ext.audio.locale_audio_files[fallback];
	}
	return NULL;
}

s32 voiceLocaleLoadSubtitle(const struct asset_entry *entry,
	char *out_text, size_t out_text_size)
{
	void *json;
	u32 json_size = 0;
	s32 result;

	if (!entry || !out_text || out_text_size == 0) return -1;
	out_text[0] = '\0';
	if (!entry->ext.audio.subtitle_file[0]) return 0;
	json = fsFileLoad(entry->ext.audio.subtitle_file, &json_size);
	if (!json) return -1;
	result = voiceSubtitleJsonSelect((const char *)json, json_size,
		voiceLocaleActiveTag(), entry->ext.audio.fallback_locale,
		out_text, out_text_size);
	free(json);
	return result;
}
#endif
