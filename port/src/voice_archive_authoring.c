/**
 * voice_archive_authoring.c -- atomic creator-facing .pdvoice emission.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "asset_archive_policy.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "voice_archive_authoring.h"

#define VOICE_AUTHOR_TEXT_CAP 4096
#define VOICE_AUTHOR_PATH_CAP 2048

static void s_error(char *error, size_t error_cap, const char *fmt, ...)
{
	va_list args;
	if (!error || error_cap == 0) return;
	va_start(args, fmt);
	vsnprintf(error, error_cap, fmt, args);
	va_end(args);
	error[error_cap - 1] = '\0';
}

static s32 s_supportedLocale(const char *locale)
{
	static const char *const tags[] = { "en", "fr", "de", "it", "es", "ja" };
	if (!locale) return 0;
	for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
		if (strcmp(locale, tags[i]) == 0) return 1;
	}
	return 0;
}

static s32 s_iniSafe(const char *text)
{
	if (!text || !text[0]) return 0;
	for (; *text; text++) {
		if (*text == '\r' || *text == '\n') return 0;
	}
	return 1;
}

static s32 s_catalogIdSafe(const char *id)
{
	s32 colons = 0;
	if (!id || !id[0]) return 0;
	for (; *id; id++) {
		if (*id == ':') {
			colons++;
			continue;
		}
		if (!(isalnum((unsigned char)*id) || *id == '_' || *id == '-'
				|| *id == '.')) return 0;
	}
	return colons == 1;
}

static s32 s_validUtf8(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	if (!p) return 0;
	while (*p) {
		u32 cp;
		s32 count;
		if (*p <= 0x7f) {
			p++;
			continue;
		}
		if ((*p & 0xe0) == 0xc0) { cp = *p & 0x1f; count = 1; if (cp < 2) return 0; }
		else if ((*p & 0xf0) == 0xe0) { cp = *p & 0x0f; count = 2; }
		else if ((*p & 0xf8) == 0xf0) { cp = *p & 0x07; count = 3; }
		else return 0;
		p++;
		for (s32 i = 0; i < count; i++, p++) {
			if ((*p & 0xc0) != 0x80) return 0;
			cp = (cp << 6) | (*p & 0x3f);
		}
		if ((count == 2 && cp < 0x800) || (count == 3 && cp < 0x10000)
				|| cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
	}
	return 1;
}

static const char *s_extension(const char *path)
{
	const char *dot = path ? strrchr(path, '.') : NULL;
	return dot ? dot : "";
}

static s32 s_extensionIs(const char *extension, const char *expected)
{
	while (*extension && *expected) {
		if (tolower((unsigned char)*extension++) !=
				tolower((unsigned char)*expected++)) return 0;
	}
	return *extension == '\0' && *expected == '\0';
}

static u32 s_readLe32(const unsigned char *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 s_readLe16(const unsigned char *p)
{
	return (u32)p[0] | ((u32)p[1] << 8);
}

static s32 s_wavSourceValid(FILE *file, long file_size)
{
	unsigned char riff[12];
	s32 format_seen = 0;
	s32 data_seen = 0;
	u32 block_align = 0;
	if (fseek(file, 0, SEEK_SET) != 0
			|| fread(riff, 1, sizeof(riff), file) != sizeof(riff)
			|| memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
		return 0;
	}
	while (ftell(file) >= 0 && ftell(file) + 8 <= file_size) {
		unsigned char chunk[8];
		if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) return 0;
		u32 chunk_size = s_readLe32(chunk + 4);
		long payload = ftell(file);
		if (payload < 0 || (unsigned long long)payload + chunk_size >
				(unsigned long long)file_size) return 0;
		if (memcmp(chunk, "fmt ", 4) == 0) {
			unsigned char fmt[16];
			if (chunk_size < sizeof(fmt)
					|| fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) return 0;
			u32 audio_format = s_readLe16(fmt);
			u32 channels = s_readLe16(fmt + 2);
			u32 sample_rate = s_readLe32(fmt + 4);
			u32 byte_rate = s_readLe32(fmt + 8);
			block_align = s_readLe16(fmt + 12);
			u32 bits = s_readLe16(fmt + 14);
			if (audio_format != 1 || channels != 1 || sample_rate == 0
					|| bits != 16 || block_align != 2
					|| byte_rate != sample_rate * block_align) return 0;
			format_seen = 1;
		} else if (memcmp(chunk, "data", 4) == 0) {
			if (chunk_size == 0) return 0;
			data_seen = 1;
			if (block_align && chunk_size % block_align != 0) return 0;
		}
		long next = payload + (long)chunk_size + (chunk_size & 1u);
		if (next > file_size || fseek(file, next, SEEK_SET) != 0) return 0;
	}
	return format_seen && data_seen;
}

static s32 s_sourceLooksValid(const char *path, const char *extension,
	u32 *out_size)
{
	unsigned char header[12];
	FILE *file = fopen(path, "rb");
	long size;
	if (!file) return 0;
	size_t got = fread(header, 1, sizeof(header), file);
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0) {
		fclose(file);
		return 0;
	}
	if ((unsigned long)size > 0xffffffffUL) { fclose(file); return 0; }
	if (out_size) *out_size = (u32)size;
	if (s_extensionIs(extension, ".wav")) {
		s32 valid = s_wavSourceValid(file, size);
		fclose(file);
		return valid;
	}
	if (s_extensionIs(extension, ".mp3")) {
		s32 valid = got >= 3 && (memcmp(header, "ID3", 3) == 0
			|| (header[0] == 0xff && (header[1] & 0xe0) == 0xe0));
		fclose(file);
		return valid;
	}
	fclose(file);
	return 0;
}

static s32 s_jsonEscape(const char *input, char *output, size_t output_cap)
{
	size_t used = 0;
	if (!input || !output || output_cap == 0 || !s_validUtf8(input)) return 0;
	for (const unsigned char *p = (const unsigned char *)input; *p; p++) {
		const char *escape = NULL;
		char unicode[7];
		size_t add;
		switch (*p) {
		case '"': escape = "\\\""; break;
		case '\\': escape = "\\\\"; break;
		case '\b': escape = "\\b"; break;
		case '\f': escape = "\\f"; break;
		case '\n': escape = "\\n"; break;
		case '\r': escape = "\\r"; break;
		case '\t': escape = "\\t"; break;
		default:
			if (*p < 0x20) {
				snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned)*p);
				escape = unicode;
			}
			break;
		}
		if (escape) {
			add = strlen(escape);
			if (used + add >= output_cap) return 0;
			memcpy(output + used, escape, add);
			used += add;
		} else {
			if (used + 1 >= output_cap) return 0;
			output[used++] = (char)*p;
		}
	}
	output[used] = '\0';
	return 1;
}

static s32 s_iniFlatten(const char *input, char *output, size_t output_cap)
{
	size_t used = 0;
	if (!input || !output || output_cap == 0) return 0;
	for (; *input; input++) {
		char c = *input;
		if (used + 1 >= output_cap) return 0;
		if (c == '\r' || c == '\n' || (unsigned char)c < 0x20) c = ' ';
		output[used++] = c;
	}
	output[used] = '\0';
	return 1;
}

static s32 s_replaceFile(const char *source, const char *destination)
{
#ifdef _WIN32
	return MoveFileExA(source, destination,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(source, destination) == 0;
#endif
}

s32 voiceArchiveAuthor(const voice_archive_author_request_t *request,
	char *error, size_t error_cap)
{
	char escaped_subtitle[VOICE_AUTHOR_TEXT_CAP];
	char transcript[VOICE_AUTHOR_TEXT_CAP];
	char descriptor[VOICE_AUTHOR_TEXT_CAP];
	char subtitle_json[VOICE_AUTHOR_TEXT_CAP];
	char candidate[VOICE_AUTHOR_PATH_CAP];
	char sample_member[32];
	char locale_member[64];
	char policy_error[256];
	const char *extension;
	u32 source_size = 0;
	mod_archive_writer_t *archive = NULL;
	asset_archive_writer_t writer;
	s32 descriptor_len;
	s32 subtitle_len;
	s32 result = 0;

	if (error && error_cap > 0) error[0] = '\0';
	if (!request || !request->archive_path || !request->archive_path[0]
			|| !s_catalogIdSafe(request->catalog_id)
			|| !s_iniSafe(request->display_name) || !s_iniSafe(request->actor)
			|| !s_iniSafe(request->context) || !request->source_audio_path
			|| !request->source_audio_path[0] || !request->subtitle
			|| !request->subtitle[0]) {
		s_error(error, error_cap,
			"archive path, catalog ID, name, actor, context, and subtitle are required");
		return 0;
	}
	if (!s_supportedLocale(request->locale)
			|| !s_supportedLocale(request->fallback_locale)) {
		s_error(error, error_cap, "locale and fallback locale must be en/fr/de/it/es/ja");
		return 0;
	}
	if (!s_validUtf8(request->display_name) || !s_validUtf8(request->actor)
			|| !s_validUtf8(request->context) || !s_validUtf8(request->subtitle)) {
		s_error(error, error_cap, "voice text must be valid UTF-8");
		return 0;
	}

	extension = s_extension(request->source_audio_path);
	if (!s_sourceLooksValid(request->source_audio_path, extension, &source_size)) {
		s_error(error, error_cap, "voice source must be a readable WAV or MP3 file");
		return 0;
	}
	const char *leaf_ext = s_extensionIs(extension, ".wav") ? "wav" : "mp3";
	snprintf(sample_member, sizeof(sample_member), "sample.%s", leaf_ext);
	snprintf(locale_member, sizeof(locale_member), "locales/%s.%s",
		request->locale, leaf_ext);

	if (!s_jsonEscape(request->subtitle, escaped_subtitle,
			sizeof(escaped_subtitle))
			|| !s_iniFlatten(request->subtitle, transcript, sizeof(transcript))) {
		s_error(error, error_cap, "subtitle is too large or invalid UTF-8");
		return 0;
	}

	descriptor_len = snprintf(descriptor, sizeof(descriptor),
		"; Creator-authored self-contained localized voice asset\n"
		"[voice]\n"
		"catalog_id = %s\n"
		"name = %s\n"
		"audio_category = voice\n"
		"duration_ms = %u\n"
		"format = %s\n"
		"source_format = %s\n"
		"file_path = %s\n"
		"data_size = %u\n"
		"actor = %s\n"
		"transcript = %s\n"
		"language = %s\n"
		"context = %s\n"
		"subtitle_file = subtitle.json\n"
		"fallback_locale = %s\n"
		"locale_%s_file = %s\n",
		request->catalog_id, request->display_name,
		(unsigned)request->duration_ms,
		s_extensionIs(extension, ".wav") ? "WAV" : "MP3",
		s_extensionIs(extension, ".wav") ? "WAV" : "MP3",
		sample_member, (unsigned)source_size, request->actor,
		transcript, request->locale, request->context,
		request->fallback_locale, request->locale, locale_member);
	if (descriptor_len <= 0 || (size_t)descriptor_len >= sizeof(descriptor)) {
		s_error(error, error_cap, "voice descriptor is too large");
		return 0;
	}

	subtitle_len = snprintf(subtitle_json, sizeof(subtitle_json),
		"{\n  \"schema\": \"pd.voice_subtitle.v1\",\n"
		"  \"default\": \"%s\",\n  \"%s\": \"%s\"\n}\n",
		escaped_subtitle, request->locale, escaped_subtitle);
	if (subtitle_len <= 0 || (size_t)subtitle_len >= sizeof(subtitle_json)) {
		s_error(error, error_cap, "subtitle JSON is too large");
		return 0;
	}

	if (snprintf(candidate, sizeof(candidate), "%s.candidate.pdvoice",
			request->archive_path) <= 0
			|| strlen(candidate) + 1 >= sizeof(candidate)) {
		s_error(error, error_cap, "voice archive path is too long");
		return 0;
	}
	remove(candidate);
	archive = modArchiveBegin(candidate);
	if (!archive) {
		s_error(error, error_cap, "could not create staged .pdvoice archive");
		return 0;
	}
	if (assetArchiveWriterInit(&writer, archive, "voice",
			request->catalog_id) != MODARCHIVE_OK) goto write_failed;
	assetArchiveWriterSetProvenance(&writer, "Audio Mods Voice Creator",
		request->source_audio_path, -1, "creator-authored");
	if (assetArchiveWriterAddDescriptor(&writer, "voice.ini", descriptor,
			(u32)descriptor_len) != MODARCHIVE_OK
			|| assetArchiveWriterAddPublicDisk(&writer, sample_member,
				request->source_audio_path, "sample") != MODARCHIVE_OK
			|| assetArchiveWriterAddPublicDisk(&writer, locale_member,
				request->source_audio_path, "localized-sample") != MODARCHIVE_OK
			|| assetArchiveWriterAddPublicMem(&writer, "subtitle.json",
				subtitle_json, (u32)subtitle_len, "subtitle") != MODARCHIVE_OK
			|| assetArchiveWriterFinishMetadata(&writer) != MODARCHIVE_OK) {
		goto write_failed;
	}
	if (modArchiveFinish(archive) != MODARCHIVE_OK) {
		archive = NULL;
		s_error(error, error_cap, "could not finish staged .pdvoice archive");
		goto cleanup;
	}
	archive = NULL;
	if (assetArchiveValidateFile(candidate, ASSET_ARCHIVE_VALIDATE_RELEASE,
			policy_error, sizeof(policy_error)) != 0) {
		s_error(error, error_cap, "staged .pdvoice validation failed: %s",
			policy_error[0] ? policy_error : "unknown archive error");
		goto cleanup;
	}
	if (!s_replaceFile(candidate, request->archive_path)) {
		s_error(error, error_cap, "could not atomically replace the .pdvoice archive");
		goto cleanup;
	}
	result = 1;
	goto cleanup;

write_failed:
	s_error(error, error_cap, "could not write the complete .pdvoice source archive");
	modArchiveAbort(archive);
	archive = NULL;
cleanup:
	if (archive) modArchiveAbort(archive);
	if (!result) remove(candidate);
	return result;
}
