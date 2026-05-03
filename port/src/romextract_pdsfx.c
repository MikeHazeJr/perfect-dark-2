/**
 * romextract_pdsfx.c -- Catalog universality pivot Step 3 audio half
 * (2026-05-03). Owns the shared SFX-bank walker used by both .pdsfx
 * and .pdvoice emitters.
 *
 * Walks the leaf SFX bank exposed via the disk-migrated sfxctl and
 * sfxtbl segments and emits one .pdsfx ZIP compound per leaf sound
 * NOT classified as voice (.pdvoice -- see romextract_pdvoice.c --
 * uses the same walker with PDAUDIO_WALK_VOICE).
 *
 * Per-asset ZIP layout per universality-pivot-schemas.md Section 2.7:
 *   manifest.json     envelope + audio metadata + provenance
 *   sample.bin        raw sample bytes sliced from sfxtbl
 *   sample.bin.sha256 outer-file SHA-256 sidecar
 *
 * Catalog ID convention (Q-4 buckets):
 *   .pdsfx    base:<lowered_sym>   when loaderPdbaseNameForSfxEnum
 *                                  returns a symbolic name, e.g.
 *                                  base:sfx_launch_rocket
 *             base:sfx_<NNNN>      4-digit hex fallback, e.g.
 *                                  base:sfx_0042
 *
 * Voice classification (Q-2 type-tolerance + Slice 10 predicate):
 *   A leaf SFX index `i` is classified as voice if some russ entry
 *   in g_AudioRussMappings[] has soundnum==i AND audioconfig_index
 *   in {1, 2, 3, 47, 48, 60, 62}. The same predicate gates the
 *   .pdvoice walk in romextract_pdvoice.c -- the two filters are
 *   complementary so every leaf goes to exactly one emitter.
 *
 * Server build (PD_SERVER): no audio segments populated, no russ
 * table linkage (snd.c is not in the server build), so the walker
 * short-circuits to 0 on entry.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  plan
 *   context/audits/catalog-phase3-slice10-voice-retag-2026-05-02.md
 *                                              voice-classification basis
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "romextract.h"
#include "romextract_pd.h"
#include "romextract_pdaudio_internal.h"
#include "sha256.h"
#include "system.h"

/* The bank file format is described in include/PR/libaudio.h plus
 * the post-preprocess form in port/src/preprocess/segaudio.c. Stored
 * pointer fields are (uintptr_t)-cast offsets into the same sfxctl
 * buffer, which is what makes "treat the buffer as raw bytes and add
 * offset" the right read pattern below. */
#define PDSFX_OUT_DIR_SFX   "audio/sfx"
#define PDSFX_OUT_DIR_VOICE "audio/voice"

/* Audioconfig slot numbers that the Slice 10 retag classifies as
 * voice. Mirrors s_audioConfigIsVoice() in
 * port/src/assetcatalog_base_extended.c -- kept in lockstep with that
 * predicate so audio classification is consistent between catalog
 * registration and per-asset extraction. */
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

/* Locate a named segment buffer + size. Returns 1 on success, 0 on
 * miss. Mirrors the s_findAnimSegment helper in
 * romextract_pdanim_chr.c. */
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

/* Lowercase + strip the leading "SFX_" so an enum like
 * "SFX_LAUNCH_ROCKET" becomes "sfx_launch_rocket" -- the catalog ID
 * format from feedback_human_readable_ids. Output is written into
 * out_buf, which must hold at least out_n bytes. */
static void s_lowerSfxSymbol(const char *src, char *out, size_t out_n)
{
	if (out_n == 0) return;
	out[0] = '\0';
	if (!src) return;

	/* Drop a leading "SFX_" if present so the prefix doesn't double. */
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

/* Build the catalog ID for a given SFX index in either sfx or voice
 * mode. Returns 1 if a symbolic name was used, 0 if the hex fallback
 * was generated. */
static s32 s_buildSfxCatalogId(s32 sfx_idx, pdaudio_walk_mode_t mode,
                                char *out, size_t out_n)
{
	const char *sym = loaderPdbaseNameForSfxEnum(sfx_idx);
	if (mode == PDAUDIO_WALK_SFX) {
		if (sym && sym[0]) {
			char lowered[96];
			s_lowerSfxSymbol(sym, lowered, sizeof(lowered));
			if (lowered[0]) {
				snprintf(out, out_n, "base:sfx_%s", lowered);
				return 1;
			}
		}
		snprintf(out, out_n, "base:sfx_%04x", (unsigned)sfx_idx);
		return 0;
	}

	/* Voice: keep the index in the ID. Slice 10 voice classification
	 * lives at the russ-mapping level so per-line actor curation lands
	 * later; the ID stays stable across that follow-up so refs survive. */
	snprintf(out, out_n, "base:voice_%04x", (unsigned)sfx_idx);
	return sym && sym[0] ? 1 : 0;
}

/* Filename slug: catalog ID with ':' replaced by '_'. */
static void s_filenameSlug(const char *catalog_id, char *out, size_t out_n)
{
	size_t i, j = 0;
	for (i = 0; catalog_id[i] && j + 1 < out_n; i++) {
		out[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
	}
	out[j] = '\0';
}

/* Read offset out of a stored "pointer" field in the post-preprocess
 * bank buffer. The preprocess stage casts u32 offsets to void* via
 * uintptr_t; we round-trip the same conversion here. */
static u32 s_offsetFromPointer(void *p)
{
	return (u32)(uintptr_t)p;
}

/* Pre-build a bitset cache that flags which leaf SFX indices map to
 * a voice audioconfig slot. The russ table has 444 entries (less
 * than the 1545 leaf SFX); flag once at start of walk so the
 * per-sound check is O(1). */
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

/* Wave format string per ALWaveTable.type. The ROM's two formats are
 * AL_ADPCM_WAVE (compressed; SFX bank's normal format) and
 * AL_RAW16_WAVE (uncompressed PCM16). The schema's "OGG_REPLACEMENT"
 * is reserved for mod-supplied sample replacement; base extraction
 * never produces it. */
static const char *s_waveFormatString(u8 type)
{
	switch (type) {
	case AL_ADPCM_WAVE: return "ALADPCM";
	case AL_RAW16_WAVE: return "PCM16";
	default:            return "UNKNOWN";
	}
}

/* Emit one .pdsfx (or .pdvoice) ZIP for a given ALSound entry. Returns
 * 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneSound(s32 sfx_idx,
                          const u8 *ctl_data, u32 ctl_size,
                          const u8 *tbl_data, u32 tbl_size,
                          const ALSound *snd, s32 sample_rate,
                          pdaudio_walk_mode_t mode,
                          const char *out_dir, s32 force_rewrite,
                          const char *channel)
{
	if (!snd || !snd->wavetable) {
		/* Empty slot: no wavetable means no audio data. Skip silently.
		 * The catalog row still exists for reference parity. */
		return 0;
	}

	/* Resolve wavetable. wavetable is stored as offset within ctl. */
	u32 wt_off = s_offsetFromPointer(snd->wavetable);
	if (wt_off + sizeof(ALWaveTable) > ctl_size) {
		sysLoudFailf(channel,
			"sfx_idx=%d wavetable offset 0x%x + sizeof past ctl size 0x%x",
			sfx_idx, (unsigned)wt_off, (unsigned)ctl_size);
		return -1;
	}
	const ALWaveTable *wt = (const ALWaveTable *)(ctl_data + wt_off);

	/* Resolve sample byte range in the sfxtbl segment. */
	u32 sample_off = s_offsetFromPointer(wt->base);
	u32 sample_len = (u32)wt->len;
	if (sample_len == 0) {
		/* Wavetable with zero byte length = empty placeholder; skip. */
		return 0;
	}
	if (sample_off + sample_len > tbl_size) {
		sysLoudFailf(channel,
			"sfx_idx=%d sample range 0x%x..0x%x past tbl size 0x%x",
			sfx_idx, (unsigned)sample_off,
			(unsigned)(sample_off + sample_len), (unsigned)tbl_size);
		return -1;
	}

	/* Loop info -- ADPCM and RAW16 differ in struct, but the
	 * (start, end, count) sample-frame triple is shared in the JSON
	 * envelope. */
	s32 has_loop = 0;
	u32 loop_start = 0, loop_end = 0, loop_count = 0;
	if (wt->type == AL_ADPCM_WAVE) {
		u32 loop_off = s_offsetFromPointer(wt->waveInfo.adpcmWave.loop);
		if (loop_off != 0 && loop_off + sizeof(ALADPCMloop) <= ctl_size) {
			const ALADPCMloop *lp =
				(const ALADPCMloop *)(ctl_data + loop_off);
			loop_start = lp->start;
			loop_end   = lp->end;
			loop_count = lp->count;
			has_loop = 1;
		}
	} else if (wt->type == AL_RAW16_WAVE) {
		u32 loop_off = s_offsetFromPointer(wt->waveInfo.rawWave.loop);
		if (loop_off != 0 && loop_off + sizeof(ALRawLoop) <= ctl_size) {
			const ALRawLoop *lp =
				(const ALRawLoop *)(ctl_data + loop_off);
			loop_start = lp->start;
			loop_end   = lp->end;
			loop_count = lp->count;
			has_loop = 1;
		}
	}

	/* Catalog ID + filename slug. */
	char catalog_id[128];
	(void)s_buildSfxCatalogId(sfx_idx, mode, catalog_id, sizeof(catalog_id));

	char filename_slug[128];
	s_filenameSlug(catalog_id, filename_slug, sizeof(filename_slug));

	const char *kind_ext = (mode == PDAUDIO_WALK_VOICE) ? "pdvoice" : "pdsfx";
	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel),
		"%s/%s.%s", out_dir, filename_slug, kind_ext);

	if (!force_rewrite && fsFileSize(dst_rel) > 0) return 0;

	/* Build manifest.json. Voice variant adds actor + transcript +
	 * language + context placeholder fields per Section 2.8 (curation
	 * fills them in a follow-up; "unknown" is the schema-stable
	 * sentinel). */
	const char *pd_kind = (mode == PDAUDIO_WALK_VOICE) ? "voice" : "sfx";
	const char *fmt_str = s_waveFormatString(wt->type);
	const char *sym = loaderPdbaseNameForSfxEnum(sfx_idx);

	char manifest_buf[1536];
	int manifest_len;
	if (mode == PDAUDIO_WALK_VOICE) {
		manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
			"{\n"
			"  \"pd_kind\": \"%s\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"id\": \"%s\",\n"
			"  \"format\": \"%s\",\n"
			"  \"sample_rate_hz\": %d,\n"
			"  \"data\": \"sample.bin\",\n"
			"  \"data_size\": %u,\n"
			"  \"actor\": \"unknown\",\n"
			"  \"transcript\": \"\",\n"
			"  \"language\": \"\",\n"
			"  \"context\": \"\",\n"
			"  \"loop_start_samples\": %u,\n"
			"  \"loop_end_samples\": %u,\n"
			"  \"loop_count\": %u,\n"
			"  \"has_loop\": %s,\n"
			"  \"sample_pan\": %u,\n"
			"  \"sample_volume\": %u,\n"
			"  \"sound_flags\": %u,\n"
			"  \"source_index\": %d,\n"
			"  \"source_offset\": %u,\n"
			"  \"source_symbol\": \"%s\"\n"
			"}\n",
			pd_kind, catalog_id, fmt_str,
			sample_rate,
			(unsigned)sample_len,
			(unsigned)loop_start, (unsigned)loop_end, (unsigned)loop_count,
			has_loop ? "true" : "false",
			(unsigned)snd->samplePan,
			(unsigned)snd->sampleVolume,
			(unsigned)snd->flags,
			sfx_idx,
			(unsigned)sample_off,
			sym ? sym : "");
	} else {
		manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
			"{\n"
			"  \"pd_kind\": \"%s\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"id\": \"%s\",\n"
			"  \"format\": \"%s\",\n"
			"  \"sample_rate_hz\": %d,\n"
			"  \"data\": \"sample.bin\",\n"
			"  \"data_size\": %u,\n"
			"  \"loop_start_samples\": %u,\n"
			"  \"loop_end_samples\": %u,\n"
			"  \"loop_count\": %u,\n"
			"  \"has_loop\": %s,\n"
			"  \"sample_pan\": %u,\n"
			"  \"sample_volume\": %u,\n"
			"  \"sound_flags\": %u,\n"
			"  \"source_index\": %d,\n"
			"  \"source_offset\": %u,\n"
			"  \"source_symbol\": \"%s\"\n"
			"}\n",
			pd_kind, catalog_id, fmt_str,
			sample_rate,
			(unsigned)sample_len,
			(unsigned)loop_start, (unsigned)loop_end, (unsigned)loop_count,
			has_loop ? "true" : "false",
			(unsigned)snd->samplePan,
			(unsigned)snd->sampleVolume,
			(unsigned)snd->flags,
			sfx_idx,
			(unsigned)sample_off,
			sym ? sym : "");
	}
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf(channel,
			"manifest.json snprintf truncated for sfx_idx=%d", sfx_idx);
		return -1;
	}

	const char *dst_full = fsFullPath(dst_rel);
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf(channel, "fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf(channel, "modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "manifest.json",
	                          manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf(channel,
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "sample.bin",
	                          (const char *)(tbl_data + sample_off),
	                          sample_len) != 0) {
		sysLoudFailf(channel,
			"AddFileMem sample.bin failed for \"%s\" (sfx_idx=%d len=%u)",
			dst_full, sfx_idx, (unsigned)sample_len);
		modArchiveAbort(aw);
		return -1;
	}

	/* SHA-256 sidecar (matches .pdmesh / .pdanim_chr pattern). */
	{
		u8 digest[SHA256_DIGEST_SIZE];
		sha256Hash(tbl_data + sample_off, (size_t)sample_len, digest);
		char hex[SHA256_HEX_SIZE + 1];
		sha256ToHex(digest, hex);
		hex[SHA256_HEX_SIZE] = '\0';
		char sidecar[SHA256_HEX_SIZE + 2];
		snprintf(sidecar, sizeof(sidecar), "%s\n", hex);
		if (modArchiveAddFileMem(aw, "sample.bin.sha256",
		                          sidecar, (u32)strlen(sidecar)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"romextract %s: sidecar write failed for \"%s\" "
				"(sfx_idx=%d)",
				kind_ext, dst_full, sfx_idx);
		}
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf(channel, "modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

s32 romextract_pdaudio_walkBank(pdaudio_walk_mode_t mode, s32 force_rewrite)
{
	const char *channel = (mode == PDAUDIO_WALK_VOICE)
		? "EXTRACT.PDVOICE" : "EXTRACT.PDSFX";
	const char *out_subdir = (mode == PDAUDIO_WALK_VOICE)
		? PDSFX_OUT_DIR_VOICE : PDSFX_OUT_DIR_SFX;
	const char *kind_label = (mode == PDAUDIO_WALK_VOICE) ? "pdvoice" : "pdsfx";

	const u8 *ctl_data = NULL;
	u32 ctl_size = 0;
	const u8 *tbl_data = NULL;
	u32 tbl_size = 0;

	if (!s_findSegment("sfxctl", &ctl_data, &ctl_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract %s: \"sfxctl\" segment not loaded "
			"(server build or pre-romdata-init); skipping", kind_label);
		return 0;
	}
	if (!s_findSegment("sfxtbl", &tbl_data, &tbl_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract %s: \"sfxtbl\" segment not loaded; skipping",
			kind_label);
		return 0;
	}

	if (ctl_size < sizeof(ALBankFile)) {
		sysLoudFailf(channel,
			"sfxctl size %u smaller than ALBankFile header",
			(unsigned)ctl_size);
		return -1;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf(channel, "fsDataDirEnsure failed");
		return -1;
	}

	char out_dir[FS_MAXPATH];
	snprintf(out_dir, sizeof(out_dir), "%s/%s", fsDataDir(), out_subdir);
	if (!fsCreateDir(out_dir)) {
		sysLoudFailf(channel, "fsCreateDir(\"%s\") failed", out_dir);
		return -1;
	}

	/* Walk into bank[0] -> instArray[0]. The SFX bank stores all
	 * leaf sounds in a single instrument; PD's sndLoadSfxCtl confirms
	 * "the first (and only) bank" and "the first (and only) instrument"
	 * (src/lib/snd.c around line 970). */
	const ALBankFile *bf = (const ALBankFile *)ctl_data;
	if (bf->bankCount < 1) {
		sysLoudFailf(channel, "ALBankFile bankCount=%d (<1)",
			(int)bf->bankCount);
		return -1;
	}

	u32 bank_off = s_offsetFromPointer(bf->bankArray[0]);
	if (bank_off + sizeof(ALBank) > ctl_size) {
		sysLoudFailf(channel,
			"bank[0] offset 0x%x past ctl size 0x%x",
			(unsigned)bank_off, (unsigned)ctl_size);
		return -1;
	}
	const ALBank *bank = (const ALBank *)(ctl_data + bank_off);
	s32 sample_rate = bank->sampleRate > 0 ? bank->sampleRate : 22050;

	if (bank->instCount < 1) {
		sysLoudFailf(channel, "ALBank instCount=%d (<1)",
			(int)bank->instCount);
		return -1;
	}

	u32 inst_off = s_offsetFromPointer(bank->instArray[0]);
	if (inst_off + sizeof(ALInstrument) > ctl_size) {
		sysLoudFailf(channel,
			"inst[0] offset 0x%x past ctl size 0x%x",
			(unsigned)inst_off, (unsigned)ctl_size);
		return -1;
	}
	const ALInstrument *inst = (const ALInstrument *)(ctl_data + inst_off);

	s32 sound_count = inst->soundCount;
	if (sound_count <= 0) {
		sysLoudFailf(channel, "ALInstrument soundCount=%d (<=0)",
			sound_count);
		return -1;
	}

	/* Bound check: the soundArray follows ALInstrument inline, so
	 * verify enough room for sound_count uintptr_t entries beyond
	 * the base struct. ALInstrument's struct already declares
	 * soundArray[1] so we subtract one to avoid double-counting. */
	if (inst_off + sizeof(ALInstrument)
	    + (size_t)(sound_count - 1) * sizeof(uintptr_t)
	    > ctl_size) {
		sysLoudFailf(channel,
			"inst soundArray spans past ctl size (count=%d off=0x%x)",
			sound_count, (unsigned)inst_off);
		return -1;
	}

	/* Voice bitset cache: 1 byte per leaf SFX index. We size on
	 * sound_count which the SFX bank reports; the russ table can never
	 * point past it. */
	u8 *voice_cache = sysMemZeroAlloc((u32)sound_count);
	if (!voice_cache) {
		sysLoudFailf(channel, "sysMemZeroAlloc(%d) failed for voice cache",
			sound_count);
		return -1;
	}
	s_buildVoiceCache(voice_cache, (u32)sound_count);

	s32 written = 0;
	s32 skipped = 0;
	s32 failed  = 0;

	for (s32 i = 0; i < sound_count; i++) {
		s32 is_voice = voice_cache[i] ? 1 : 0;
		s32 want_voice = (mode == PDAUDIO_WALK_VOICE) ? 1 : 0;

		if (is_voice != want_voice) {
			skipped++;
			continue;
		}

		u32 snd_off = s_offsetFromPointer(inst->soundArray[i]);
		if (snd_off + sizeof(ALSound) > ctl_size) {
			sysLoudFailf(channel,
				"sound[%d] offset 0x%x past ctl size 0x%x",
				i, (unsigned)snd_off, (unsigned)ctl_size);
			failed++;
			continue;
		}
		const ALSound *snd = (const ALSound *)(ctl_data + snd_off);

		s32 r = s_emitOneSound(i, ctl_data, ctl_size,
		                        tbl_data, tbl_size,
		                        snd, sample_rate, mode,
		                        out_dir, force_rewrite, channel);
		if (r > 0)        written++;
		else if (r == 0)  skipped++;
		else              failed++;
	}

	sysMemFree(voice_cache);

	sysLogPrintf(LOG_NOTE,
		"romextract %s: written=%d skipped=%d failed=%d "
		"sound_count=%d sample_rate=%d (out=%s)",
		kind_label, written, skipped, failed, sound_count,
		sample_rate, out_dir);

	return written;
}

s32 romExtractAllPdsfx(s32 force_rewrite)
{
	return romextract_pdaudio_walkBank(PDAUDIO_WALK_SFX, force_rewrite);
}

#else /* PD_SERVER */

s32 romextract_pdaudio_walkBank(pdaudio_walk_mode_t mode, s32 force_rewrite)
{
	(void)mode; (void)force_rewrite;
	return 0;
}

s32 romExtractAllPdsfx(s32 force_rewrite)
{
	(void)force_rewrite;
	return 0;
}

#endif /* PD_SERVER */
