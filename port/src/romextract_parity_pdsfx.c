/**
 * romextract_parity_pdsfx.c -- Catalog universality pivot Step 3
 * audio half parity check (2026-05-03). Per Mike's Q-5 ruling parity
 * stays active through Steps 1-4 and retires at Step 5 with the
 * .pdbase deletion.
 *
 * Strategy: re-walk the leaf SFX bank in mode-filter form (matching
 * the emitter), re-open each emitted ZIP, parse manifest.json, and
 * verify envelope + key scalar fields round-trip the source ALSound.
 * Both .pdsfx and .pdvoice share this walker; the mode flag selects
 * which output directory + which manifest fields to assert.
 *
 * Failures emit LOADER.UNIVERSAL.PARITY_FAIL with sfx_idx, field,
 * and observed-vs-expected values. Returns the count of failed
 * sounds (0 = pass).
 *
 * Full field-by-field round-trip parity is the responsibility of
 * Step 4 with the universal directory walker; this is the structural
 * counterpart to the other Step 1/2/3a parity checks.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include <PR/libaudio.h>

#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "loader_pdbase_enums.h"
#include "modarchive.h"
#include "romdata.h"
#include "romextract_pd.h"
#include "romextract_pdaudio_internal.h"
#include "system.h"

#ifndef AUDIOCONFIG_01
#define AUDIOCONFIG_01 1
#define AUDIOCONFIG_02 2
#define AUDIOCONFIG_03 3
#define AUDIOCONFIG_47 47
#define AUDIOCONFIG_48 48
#define AUDIOCONFIG_60 60
#define AUDIOCONFIG_62 62
#endif

#if !defined(PD_SERVER)

/* Mirrors of the emitter helpers; kept private here because the
 * parity check has its own iteration shape. The emitter file
 * (romextract_pdsfx.c) holds the canonical versions. */

static s32 s_audioConfigIsVoice(s32 idx)
{
	switch (idx) {
	case AUDIOCONFIG_01:
	case AUDIOCONFIG_02:
	case AUDIOCONFIG_03:
	case AUDIOCONFIG_47:
	case AUDIOCONFIG_48:
	case AUDIOCONFIG_60:
#if VERSION >= VERSION_NTSC_1_0
	case AUDIOCONFIG_62:
#endif
		return 1;
	default:
		return 0;
	}
}

static s32 s_findSegment(const char *name, const u8 **outData, u32 *outSize)
{
	*outData = NULL;
	*outSize = 0;
	if (!name) return 0;

	s32 nseg = romdataSegmentCount();
	for (s32 i = 0; i < nseg; i++) {
		const char *seg_name = romdataSegmentGetName(i);
		if (seg_name && strcmp(seg_name, name) == 0) {
			const u8 *data = romdataSegmentGetData(i);
			u32 size = romdataSegmentGetSize(i);
			if (!data || size == 0) return 0;
			*outData = data;
			*outSize = size;
			return 1;
		}
	}
	return 0;
}

static u32 s_offsetFromPointer(void *p)
{
	return (u32)(uintptr_t)p;
}

static void s_lowerSfxSymbol(const char *src, char *out, size_t out_n)
{
	if (out_n == 0) return;
	out[0] = '\0';
	if (!src) return;

	const char *p = src;
	if (strncmp(p, "SFX_", 4) == 0 || strncmp(p, "sfx_", 4) == 0) {
		p += 4;
	}

	size_t i;
	for (i = 0; p[i] && i + 5 < out_n; i++) {
		out[i] = (char)tolower((unsigned char)p[i]);
	}
	out[i] = '\0';
}

static void s_buildSfxCatalogId(s32 sfx_idx, pdaudio_walk_mode_t mode,
                                 char *out, size_t out_n)
{
	const char *sym = loaderPdbaseNameForSfxEnum(sfx_idx);
	if (mode == PDAUDIO_WALK_SFX) {
		if (sym && sym[0]) {
			char lowered[96];
			s_lowerSfxSymbol(sym, lowered, sizeof(lowered));
			if (lowered[0]) {
				snprintf(out, out_n, "base:sfx_%s", lowered);
				return;
			}
		}
		snprintf(out, out_n, "base:sfx_%04x", (unsigned)sfx_idx);
		return;
	}
	snprintf(out, out_n, "base:voice_%04x", (unsigned)sfx_idx);
}

/* Tiny "find string then read scalar" parser for the manifest.json
 * envelope. Mirrors the helper in romextract_parity_pdanim_chr.c. */
static const char *s_findKey(const char *src, const char *key)
{
	char pattern[128];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	const char *p = strstr(src, pattern);
	if (!p) return NULL;
	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	if (*p != ':') return NULL;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

static s32 s_parseInt(const char *src, const char *key, long long *out_val)
{
	const char *p = s_findKey(src, key);
	if (!p) return 0;
	*out_val = strtoll(p, NULL, 10);
	return 1;
}

static s32 s_parseStr(const char *src, const char *key, char *out, size_t n)
{
	const char *p = s_findKey(src, key);
	if (!p || *p != '"') return 0;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
	out[i] = '\0';
	return 1;
}

static void s_buildVoiceCache(u8 *cache, u32 cache_n)
{
	memset(cache, 0, cache_n);
	const s32 russ_n = g_NumAudioRussMappings;
	for (s32 i = 0; i < russ_n; i++) {
		s32 cfg = (s32)g_AudioRussMappings[i].audioconfig_index;
		if (!s_audioConfigIsVoice(cfg)) continue;
		s16 sfx_idx = g_AudioRussMappings[i].soundnum;
		if (sfx_idx >= 0 && (u32)sfx_idx < cache_n) {
			cache[sfx_idx] = 1;
		}
	}
}

s32 romextract_pdaudio_parityCheck(pdaudio_walk_mode_t mode)
{
	const char *kind_label = (mode == PDAUDIO_WALK_VOICE) ? "pdvoice" : "pdsfx";
	const char *out_subdir = (mode == PDAUDIO_WALK_VOICE)
		? "audio/voice" : "audio/sfx";
	const char *expected_kind = (mode == PDAUDIO_WALK_VOICE) ? "voice" : "sfx";

	const u8 *ctl_data = NULL;
	u32 ctl_size = 0;
	const u8 *tbl_data = NULL;
	u32 tbl_size = 0;

	if (!s_findSegment("sfxctl", &ctl_data, &ctl_size)) return 0;
	if (!s_findSegment("sfxtbl", &tbl_data, &tbl_size)) return 0;
	if (ctl_size < sizeof(ALBankFile)) return 0;

	const ALBankFile *bf = (const ALBankFile *)ctl_data;
	if (bf->bankCount < 1) return 0;

	u32 bank_off = s_offsetFromPointer(bf->bankArray[0]);
	if (bank_off + sizeof(ALBank) > ctl_size) return 0;
	const ALBank *bank = (const ALBank *)(ctl_data + bank_off);

	if (bank->instCount < 1) return 0;
	u32 inst_off = s_offsetFromPointer(bank->instArray[0]);
	if (inst_off + sizeof(ALInstrument) > ctl_size) return 0;
	const ALInstrument *inst = (const ALInstrument *)(ctl_data + inst_off);

	s32 sound_count = inst->soundCount;
	if (sound_count <= 0) return 0;

	u8 *voice_cache = sysMemZeroAlloc((u32)sound_count);
	if (!voice_cache) return 0;
	s_buildVoiceCache(voice_cache, (u32)sound_count);

	s32 failures = 0;
	s32 checked = 0;
	s32 skipped = 0;

	for (s32 i = 0; i < sound_count; i++) {
		s32 is_voice = voice_cache[i] ? 1 : 0;
		s32 want_voice = (mode == PDAUDIO_WALK_VOICE) ? 1 : 0;
		if (is_voice != want_voice) { skipped++; continue; }

		u32 snd_off = s_offsetFromPointer(inst->soundArray[i]);
		if (snd_off + sizeof(ALSound) > ctl_size) { skipped++; continue; }
		const ALSound *snd = (const ALSound *)(ctl_data + snd_off);
		if (!snd->wavetable) { skipped++; continue; }

		u32 wt_off = s_offsetFromPointer(snd->wavetable);
		if (wt_off + sizeof(ALWaveTable) > ctl_size) { skipped++; continue; }
		const ALWaveTable *wt = (const ALWaveTable *)(ctl_data + wt_off);
		if (wt->len == 0) { skipped++; continue; }

		char catalog_id[128];
		s_buildSfxCatalogId(i, mode, catalog_id, sizeof(catalog_id));

		char filename_slug[128];
		size_t k, j = 0;
		for (k = 0; catalog_id[k] && j + 1 < sizeof(filename_slug); k++) {
			filename_slug[j++] = (catalog_id[k] == ':') ? '_' : catalog_id[k];
		}
		filename_slug[j] = '\0';

		const char *ext = (mode == PDAUDIO_WALK_VOICE) ? "pdvoice" : "pdsfx";
		char relpath[FS_MAXPATH];
		snprintf(relpath, sizeof(relpath), "%s/%s/%s.%s",
			fsDataDir(), out_subdir, filename_slug, ext);

		const char *full = fsFullPath(relpath);
		if (!full || !full[0]) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": fsFullPath empty for \"%s\"",
				kind_label, i, catalog_id, relpath);
			failures++;
			continue;
		}

		mod_archive_t *arc = modArchiveOpen(full);
		if (!arc) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": modArchiveOpen failed (\"%s\")",
				kind_label, i, catalog_id, relpath);
			failures++;
			continue;
		}

		s32 manifest_idx = modArchiveFindEntry(arc, "manifest.json");
		if (manifest_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": missing manifest.json", kind_label, i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		u32 manifest_size = 0;
		char *manifest = (char *)modArchiveExtractAlloc(arc, manifest_idx, &manifest_size);
		if (!manifest) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": ExtractAlloc(manifest.json) failed",
				kind_label, i, catalog_id);
			failures++;
			modArchiveClose(arc);
			continue;
		}

		s32 row_failures = 0;
		char buf[128];

		if (!s_parseStr(manifest, "pd_kind", buf, sizeof(buf))
		 || strcmp(buf, expected_kind) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": pd_kind != \"%s\" (got \"%s\")",
				kind_label, i, catalog_id, expected_kind, buf);
			row_failures++;
		}

		if (!s_parseStr(manifest, "id", buf, sizeof(buf))
		 || strcmp(buf, catalog_id) != 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"id mismatch: expected=\"%s\" got=\"%s\"",
				kind_label, i, catalog_id, buf);
			row_failures++;
		}

		long long iv = -1;
		if (!s_parseInt(manifest, "source_index", &iv) || iv != i) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s \"%s\": "
				"source_index mismatch: expected=%d got=%lld",
				kind_label, catalog_id, i, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "data_size", &iv) || (u32)iv != (u32)wt->len) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": data_size mismatch: expected=%u got=%lld",
				kind_label, i, catalog_id, (unsigned)wt->len, iv);
			row_failures++;
		}

		if (!s_parseInt(manifest, "sample_rate_hz", &iv)
		 || iv != bank->sampleRate) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": sample_rate_hz mismatch: expected=%d got=%lld",
				kind_label, i, catalog_id,
				(int)bank->sampleRate, iv);
			row_failures++;
		}

		free(manifest);

		s32 sample_idx = modArchiveFindEntry(arc, "sample.bin");
		if (sample_idx < 0) {
			sysLogPrintf(LOG_WARNING,
				"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
				"\"%s\": missing sample.bin",
				kind_label, i, catalog_id);
			row_failures++;
		} else {
			u32 sample_size = modArchiveGetEntrySize(arc, sample_idx);
			if (sample_size != (u32)wt->len) {
				sysLogPrintf(LOG_WARNING,
					"LOADER.UNIVERSAL.PARITY_FAIL: %s sfx_idx=%d "
					"\"%s\": sample.bin size mismatch: expected=%u got=%u",
					kind_label, i, catalog_id,
					(unsigned)wt->len, (unsigned)sample_size);
				row_failures++;
			}
		}

		modArchiveClose(arc);

		if (row_failures > 0) failures += row_failures;
		checked++;
		(void)tbl_data; (void)tbl_size;
	}

	sysMemFree(voice_cache);

	if (failures == 0) {
		sysLogPrintf(LOG_NOTE,
			"LOADER.UNIVERSAL.PARITY: %s PASS (checked=%d skipped=%d "
			"sound_count=%d)",
			kind_label, checked, skipped, sound_count);
	} else {
		sysLogPrintf(LOG_WARNING,
			"LOADER.UNIVERSAL.PARITY: %s %d failures across %d checked "
			"(skipped=%d sound_count=%d)",
			kind_label, failures, checked, skipped, sound_count);
	}

	return failures;
}

s32 romExtractParityCheckPdsfx(void)
{
	return romextract_pdaudio_parityCheck(PDAUDIO_WALK_SFX);
}

#else /* PD_SERVER */

s32 romextract_pdaudio_parityCheck(pdaudio_walk_mode_t mode)
{
	(void)mode;
	return 0;
}

s32 romExtractParityCheckPdsfx(void)
{
	return 0;
}

#endif /* PD_SERVER */
