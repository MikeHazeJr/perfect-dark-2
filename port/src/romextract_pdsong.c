/**
 * romextract_pdsong.c -- Catalog universality pivot Step 3 audio
 * half (2026-05-03).
 *
 * Music-track emitter. Walks the byte-swapped struct seqtable at
 * the head of the disk-migrated "sequences" segment and emits one
 * .pdsong ZIP compound per entry at data/<romid>/audio/music/.
 *
 * Per-asset ZIP layout per universality-pivot-schemas.md Section 2.9:
 *   _meta/manifest.json envelope + sequence metadata + provenance
 *   _meta/*.json       shared inventory/provenance/validation/source handles
 *   sequence.mid      standard MIDI conversion of the compressed N64 sequence
 *   sequence.json     semantic event source for round-trip authoring
 *   _meta/*.sha256   public-file SHA-256 sidecars
 *
 * Catalog ID convention: base:song_sequence_<readable ordinal>. The 43 catalog-registered
 * music tracks (s_BaseMusicTracks[] in assetcatalog_base_extended.c)
 * use slug names like "track_dark_combat" but those map to MUSIC_*
 * enum values, NOT to seqtable slot indices directly. The MUSIC_* ->
 * seqtable mapping lives in the music subsystem and is curated at
 * Step 5 cleanup (or in a follow-up worktree). Raw seqtable slots stay
 * descriptor/provenance metadata, not catalog identity.
 *
 * Per-entry layout in the sequences segment:
 *   offset 0x00     u16 count                  (byte-swapped)
 *   offset 0x02     padding to 8-byte stride   (preprocessSequences leaves
 *                                               the original 4-byte alignment;
 *                                               the entries[] array starts
 *                                               at offset 4 because the table
 *                                               is dma'd as a 16-byte header
 *                                               then a count-driven payload)
 *   offset 0x04     struct seqtableentry[count]
 *
 * Each seqtableentry is { u32 romaddr; u16 binlen; u16 ziplen }.
 * romaddr at extract time is segment-relative (the live runtime adds
 * _sequencesSegmentRomStart); binlen is the uncompressed sequence
 * stream length, ziplen the on-disk compressed length. We slice
 * `binlen` bytes from offset `romaddr` as the native sequence stream. The
 * runtime decoder feeds the same slice. The extractor inflates + header-swaps that
 * slice, then converts the N64 compressed-MIDI event stream into files that
 * normal tools can open.
 *
 * Server build: returns 0 immediately (sequences segment not
 * populated; nothing to extract).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <SDL.h>
#include <PR/libaudio.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "catalog_readable_ids.h"
#include "data.h"
#include "types.h"
#include "constants.h"
#include "fs.h"
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "preprocess.h"
#include "romdata.h"
#include "romextract.h"
#include "romextract_pd.h"
#include "system.h"
#include "lib/rzip.h"

#define PDSONG_OUT_DIR "audio/music"

/* Locate a named segment buffer + size. Returns 1 on success, 0 on
 * miss. Mirrors the segment-lookup helpers in romextract_pdsfx.c
 * and romextract_pdanim_chr.c. */
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

/* Catalog ID for a raw sequence slot. Numeric slots remain metadata. */
static void s_buildSongCatalogId(s32 slot_idx, char *out, size_t out_n)
{
	catalogReadableSongId(slot_idx, out, out_n);
}

/* Filename slug: catalog ID with ':' -> '_'. */
static void s_filenameSlug(const char *catalog_id, char *out, size_t out_n)
{
	size_t i, j = 0;
	for (i = 0; catalog_id[i] && j + 1 < out_n; i++) {
		out[j++] = (catalog_id[i] == ':') ? '_' : catalog_id[i];
	}
	out[j] = '\0';
}

static s32 s_existingArchiveHasSongPayloads(const char *relpath)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;
	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;
	s32 ok = modArchiveFindEntry(arc, "music.ini") >= 0
	      && modArchiveFindEntry(arc, "_meta/manifest.json") >= 0
	      && modArchiveFindEntry(arc, "sequence.mid") >= 0
	      && modArchiveFindEntry(arc, "sequence.json") >= 0;
	modArchiveClose(arc);
	return ok;
}

typedef struct {
	u8  *data;
	u32  len;
	u32  cap;
} pdsong_bytebuf_t;

static void s_bytebufFree(pdsong_bytebuf_t *b)
{
	if (b->data) free(b->data);
	memset(b, 0, sizeof(*b));
}

static s32 s_bytebufReserve(pdsong_bytebuf_t *b, u32 extra)
{
	if (extra > 0xffffffffu - b->len) return -1;
	u32 need = b->len + extra;
	if (need <= b->cap) return 0;
	u32 cap = b->cap ? b->cap : 1024;
	while (cap < need) {
		if (cap > 0x80000000u) return -1;
		cap *= 2;
	}
	u8 *p = (u8 *)realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

static s32 s_bytebufAppend(pdsong_bytebuf_t *b, const void *src, u32 n)
{
	if (s_bytebufReserve(b, n) != 0) return -1;
	memcpy(b->data + b->len, src, n);
	b->len += n;
	return 0;
}

static s32 s_bytebufPutU8(pdsong_bytebuf_t *b, u8 v)
{
	return s_bytebufAppend(b, &v, 1);
}

static s32 s_bytebufPutBe16(pdsong_bytebuf_t *b, u16 v)
{
	u8 bytes[2] = { (u8)(v >> 8), (u8)v };
	return s_bytebufAppend(b, bytes, sizeof(bytes));
}

static s32 s_bytebufPutBe32(pdsong_bytebuf_t *b, u32 v)
{
	u8 bytes[4] = {
		(u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v
	};
	return s_bytebufAppend(b, bytes, sizeof(bytes));
}

static s32 s_bytebufPutVarLen(pdsong_bytebuf_t *b, u32 v)
{
	u8 tmp[5];
	s32 n = 0;
	tmp[n++] = (u8)(v & 0x7f);
	while ((v >>= 7) != 0) tmp[n++] = (u8)((v & 0x7f) | 0x80);
	while (n-- > 0) {
		if (s_bytebufPutU8(b, tmp[n]) != 0) return -1;
	}
	return 0;
}

static s32 s_bytebufPrintf(pdsong_bytebuf_t *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) {
		va_end(ap2);
		return -1;
	}
	if (s_bytebufReserve(b, (u32)need + 1) != 0) {
		va_end(ap2);
		return -1;
	}
	int wrote = vsnprintf((char *)b->data + b->len, (size_t)need + 1, fmt, ap2);
	va_end(ap2);
	if (wrote != need) return -1;
	b->len += (u32)need;
	return 0;
}

static s32 s_bytebufAppendJsonString(pdsong_bytebuf_t *b, const char *s)
{
	if (s_bytebufPutU8(b, '"') != 0) return -1;
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			if (s_bytebufPutU8(b, '\\') != 0 ||
			    s_bytebufPutU8(b, c) != 0) return -1;
		} else if (c == '\n') {
			if (s_bytebufAppend(b, "\\n", 2) != 0) return -1;
		} else if (c == '\r') {
			if (s_bytebufAppend(b, "\\r", 2) != 0) return -1;
		} else if (c == '\t') {
			if (s_bytebufAppend(b, "\\t", 2) != 0) return -1;
		} else if (c < 0x20) {
			if (s_bytebufPrintf(b, "\\u%04x", (unsigned)c) != 0) return -1;
		} else {
			if (s_bytebufPutU8(b, c) != 0) return -1;
		}
	}
	return s_bytebufPutU8(b, '"');
}

typedef struct {
	u32 tick;
	u32 order;
	u8  nbytes;
	u8  bytes[6];
} pdsong_midi_evt_t;

typedef struct {
	pdsong_midi_evt_t *items;
	u32                count;
	u32                cap;
} pdsong_midi_vec_t;

static void s_midiVecFree(pdsong_midi_vec_t *v)
{
	if (v->items) free(v->items);
	memset(v, 0, sizeof(*v));
}

static s32 s_midiVecPush(pdsong_midi_vec_t *v, u32 tick, u32 order,
                         const u8 *bytes, u8 nbytes)
{
	if (nbytes > sizeof(v->items[0].bytes)) return -1;
	if (v->count == v->cap) {
		u32 cap = v->cap ? v->cap * 2 : 1024;
		pdsong_midi_evt_t *p = (pdsong_midi_evt_t *)realloc(
			v->items, sizeof(*v->items) * cap);
		if (!p) return -1;
		v->items = p;
		v->cap = cap;
	}
	pdsong_midi_evt_t *e = &v->items[v->count++];
	e->tick = tick;
	e->order = order;
	e->nbytes = nbytes;
	memcpy(e->bytes, bytes, nbytes);
	return 0;
}

static int s_midiEvtCmp(const void *a, const void *b)
{
	const pdsong_midi_evt_t *ea = (const pdsong_midi_evt_t *)a;
	const pdsong_midi_evt_t *eb = (const pdsong_midi_evt_t *)b;
	if (ea->tick < eb->tick) return -1;
	if (ea->tick > eb->tick) return 1;
	if (ea->order < eb->order) return -1;
	if (ea->order > eb->order) return 1;
	return 0;
}

typedef struct {
	const u8 *base;
	const u8 *end;
	const u8 *cur[16];
	const u8 *bu_ptr[16];
	u32       bu_len[16];
	u32       delta[16];
	u8        last_status[16];
	u32       valid_tracks;
	u32       last_ticks;
} pdsong_seq_parser_t;

typedef struct {
	s32 type;
	u32 tick;
	u32 track;
	u8  status;
	u8  byte1;
	u8  byte2;
	u8  tempo[3];
	u32 duration;
	u32 loop_count;
} pdsong_seq_evt_t;

static s32 s_seqGetByte(pdsong_seq_parser_t *p, u32 track, u8 *out)
{
	u8 b;
	if (track >= 16) return -1;
	if (p->bu_len[track]) {
		if (!p->bu_ptr[track] || p->bu_ptr[track] >= p->end) return -1;
		b = *p->bu_ptr[track]++;
		p->bu_len[track]--;
		*out = b;
		return 0;
	}
	if (!p->cur[track] || p->cur[track] >= p->end) return -1;
	b = *p->cur[track]++;
	if (b == AL_CMIDI_BLOCK_CODE) {
		u8 next_byte;
		if (p->cur[track] >= p->end) return -1;
		next_byte = *p->cur[track]++;
		if (next_byte != AL_CMIDI_BLOCK_CODE) {
			u8 lo_backup;
			u8 the_len;
			u32 backup;
			if (p->cur[track] + 2 > p->end) return -1;
			lo_backup = *p->cur[track]++;
			the_len = *p->cur[track]++;
			backup = ((u32)next_byte << 8) | (u32)lo_backup;
			if (p->cur[track] < p->base + backup + 4) return -1;
			const u8 *bu = p->cur[track] - (backup + 4);
			if (bu < p->base || bu + the_len > p->end) return -1;
			p->bu_ptr[track] = bu;
			p->bu_len[track] = the_len;
			if (p->bu_len[track] == 0) return -1;
			b = *p->bu_ptr[track]++;
			p->bu_len[track]--;
		}
	}
	*out = b;
	return 0;
}

static s32 s_seqReadVarLen(pdsong_seq_parser_t *p, u32 track, u32 *out)
{
	u8 c;
	u32 value;
	if (s_seqGetByte(p, track, &c) != 0) return -1;
	value = c;
	if (value & 0x80) {
		value &= 0x7f;
		do {
			if (s_seqGetByte(p, track, &c) != 0) return -1;
			value = (value << 7) + (c & 0x7f);
		} while (c & 0x80);
	}
	*out = value;
	return 0;
}

static s32 s_seqParserInit(pdsong_seq_parser_t *p, const u8 *seq, u32 seq_len,
                           u32 *out_division)
{
	if (seq_len < sizeof(ALCMidiHdr)) return -1;
	memset(p, 0, sizeof(*p));
	p->base = seq;
	p->end = seq + seq_len;
	const ALCMidiHdr *hdr = (const ALCMidiHdr *)seq;
	if (hdr->division == 0 || hdr->division > 0x7fff) return -1;
	*out_division = hdr->division;
	for (u32 i = 0; i < 16; i++) {
		u32 off = hdr->trackOffset[i];
		if (off) {
			if (off >= seq_len) return -1;
			p->cur[i] = seq + off;
			p->valid_tracks |= (1u << i);
			if (s_seqReadVarLen(p, i, &p->delta[i]) != 0) return -1;
		}
	}
	return 0;
}

static s32 s_seqNextEvent(pdsong_seq_parser_t *p, pdsong_seq_evt_t *out)
{
	if (!p->valid_tracks) return 0;

	u32 first_track = 0;
	u32 first_time = 0xffffffffu;
	for (u32 i = 0; i < 16; i++) {
		if ((p->valid_tracks >> i) & 1u) {
			if (p->delta[i] < first_time) {
				first_time = p->delta[i];
				first_track = i;
			}
		}
	}
	for (u32 i = 0; i < 16; i++) {
		if ((p->valid_tracks >> i) & 1u) p->delta[i] -= first_time;
	}
	p->last_ticks += first_time;

	memset(out, 0, sizeof(*out));
	out->tick = p->last_ticks;
	out->track = first_track;

	u8 status;
	if (s_seqGetByte(p, first_track, &status) != 0) return -1;
	if (status == AL_MIDI_Meta) {
		u8 type;
		if (s_seqGetByte(p, first_track, &type) != 0) return -1;
		p->last_status[first_track] = 0;
		if (type == AL_MIDI_META_TEMPO) {
			out->type = AL_TEMPO_EVT;
			out->status = status;
			for (s32 i = 0; i < 3; i++) {
				if (s_seqGetByte(p, first_track, &out->tempo[i]) != 0) return -1;
			}
		} else if (type == AL_MIDI_META_EOT) {
			p->valid_tracks &= ~(1u << first_track);
			out->type = p->valid_tracks ? AL_TRACK_END : AL_SEQ_END_EVT;
		} else if (type == AL_CMIDI_LOOPSTART_CODE) {
			u8 hi, lo;
			if (s_seqGetByte(p, first_track, &hi) != 0) return -1;
			if (s_seqGetByte(p, first_track, &lo) != 0) return -1;
			out->type = AL_CSP_LOOPSTART;
			out->loop_count = ((u32)hi << 8) | lo;
		} else if (type == AL_CMIDI_LOOPEND_CODE) {
			u8 tmp;
			u32 count = 0;
			if (s_seqGetByte(p, first_track, &tmp) != 0) return -1;
			count = tmp;
			for (s32 i = 0; i < 5; i++) {
				if (s_seqGetByte(p, first_track, &tmp) != 0) return -1;
			}
			out->type = AL_CSP_LOOPEND;
			out->loop_count = count;
		} else {
			return -1;
		}
	} else {
		out->type = AL_SEQ_MIDI_EVT;
		if (status & 0x80) {
			out->status = (status & 0xf0) | (u8)first_track;
			if (s_seqGetByte(p, first_track, &out->byte1) != 0) return -1;
			p->last_status[first_track] = out->status;
		} else {
			if (!p->last_status[first_track]) return -1;
			out->status = p->last_status[first_track];
			out->byte1 = status;
		}
		if ((out->status & 0xf0) != AL_MIDI_ProgramChange
				&& (out->status & 0xf0) != AL_MIDI_ChannelPressure) {
			if (s_seqGetByte(p, first_track, &out->byte2) != 0) return -1;
			if ((out->status & 0xf0) == AL_MIDI_NoteOn) {
				if (s_seqReadVarLen(p, first_track, &out->duration) != 0) return -1;
			}
		}
	}

	if (out->type != AL_TRACK_END && out->type != AL_SEQ_END_EVT) {
		u32 next_delta;
		if (s_seqReadVarLen(p, first_track, &next_delta) != 0) return -1;
		p->delta[first_track] += next_delta;
	}
	return 1;
}

static s32 s_buildMidiFile(const pdsong_midi_vec_t *events, u32 division,
                           pdsong_bytebuf_t *mid)
{
	pdsong_bytebuf_t track;
	memset(&track, 0, sizeof(track));

	u32 last_tick = 0;
	for (u32 i = 0; i < events->count; i++) {
		const pdsong_midi_evt_t *e = &events->items[i];
		if (s_bytebufPutVarLen(&track, e->tick - last_tick) != 0 ||
		    s_bytebufAppend(&track, e->bytes, e->nbytes) != 0) {
			s_bytebufFree(&track);
			return -1;
		}
		last_tick = e->tick;
	}
	const u8 end_track[3] = { 0xff, 0x2f, 0x00 };
	if (s_bytebufPutVarLen(&track, 0) != 0 ||
	    s_bytebufAppend(&track, end_track, sizeof(end_track)) != 0) {
		s_bytebufFree(&track);
		return -1;
	}

	if (s_bytebufAppend(mid, "MThd", 4) != 0 ||
	    s_bytebufPutBe32(mid, 6) != 0 ||
	    s_bytebufPutBe16(mid, 0) != 0 ||
	    s_bytebufPutBe16(mid, 1) != 0 ||
	    s_bytebufPutBe16(mid, (u16)division) != 0 ||
	    s_bytebufAppend(mid, "MTrk", 4) != 0 ||
	    s_bytebufPutBe32(mid, track.len) != 0 ||
	    s_bytebufAppend(mid, track.data, track.len) != 0) {
		s_bytebufFree(&track);
		return -1;
	}

	s_bytebufFree(&track);
	return 0;
}

static const char *s_seqEventName(s32 type)
{
	switch (type) {
	case AL_SEQ_MIDI_EVT: return "midi";
	case AL_TEMPO_EVT: return "tempo";
	case AL_TRACK_END: return "track_end";
	case AL_SEQ_END_EVT: return "sequence_end";
	case AL_CSP_LOOPSTART: return "loop_start";
	case AL_CSP_LOOPEND: return "loop_end";
	default: return "unknown";
	}
}

static const char *s_midiStatusName(u8 status)
{
	switch (status & 0xf0) {
	case AL_MIDI_NoteOff: return "note_off";
	case AL_MIDI_NoteOn: return "note_on";
	case AL_MIDI_ControlChange: return "control_change";
	case AL_MIDI_ProgramChange: return "program_change";
	case AL_MIDI_ChannelPressure: return "channel_pressure";
	case AL_MIDI_PitchBendChange: return "pitch_bend";
	default: return status == AL_MIDI_Meta ? "meta" : "none";
	}
}

static s32 s_exportSequence(const u8 *seq_src, u32 seq_len,
                            pdsong_bytebuf_t *mid, pdsong_bytebuf_t *json,
                            u32 *out_division, u32 *out_event_count,
                            s32 *out_loops)
{
	u8 *seq = (u8 *)malloc(seq_len);
	if (!seq) return -1;
	memcpy(seq, seq_src, seq_len);
	preprocessALCMidiHdr(seq, seq_len, NULL);

	pdsong_seq_parser_t parser;
	u32 division = 0;
	if (s_seqParserInit(&parser, seq, seq_len, &division) != 0) {
		free(seq);
		return -1;
	}

	pdsong_midi_vec_t midi_events;
	memset(&midi_events, 0, sizeof(midi_events));

	if (s_bytebufPrintf(json,
			"{\n"
			"  \"pd_kind\": \"song_sequence\",\n"
			"  \"pd_schema_version\": 1,\n"
			"  \"division\": %u,\n"
			"  \"events\": [\n",
			(unsigned)division) != 0) {
		free(seq);
		return -1;
	}

	u32 order = 0;
	u32 parsed_count = 0;
	s32 saw_loops = 0;
	for (;;) {
		pdsong_seq_evt_t evt;
		s32 r = s_seqNextEvent(&parser, &evt);
		if (r < 0) {
			s_midiVecFree(&midi_events);
			free(seq);
			return -1;
		}
		if (r == 0) break;
		parsed_count++;
		if (parsed_count > 250000) {
			s_midiVecFree(&midi_events);
			free(seq);
			return -1;
		}

		u32 tempo_us = 0;
		if (evt.type == AL_TEMPO_EVT) {
			tempo_us = ((u32)evt.tempo[0] << 16) |
			           ((u32)evt.tempo[1] << 8) |
			           (u32)evt.tempo[2];
			u8 bytes[6] = { 0xff, 0x51, 0x03, evt.tempo[0], evt.tempo[1], evt.tempo[2] };
			if (s_midiVecPush(&midi_events, evt.tick, order++, bytes, sizeof(bytes)) != 0) {
				s_midiVecFree(&midi_events);
				free(seq);
				return -1;
			}
		} else if (evt.type == AL_SEQ_MIDI_EVT) {
			u8 bytes[3] = { evt.status, evt.byte1, evt.byte2 };
			u8 nbytes = ((evt.status & 0xf0) == AL_MIDI_ProgramChange ||
			             (evt.status & 0xf0) == AL_MIDI_ChannelPressure) ? 2 : 3;
			if (s_midiVecPush(&midi_events, evt.tick, order++, bytes, nbytes) != 0) {
				s_midiVecFree(&midi_events);
				free(seq);
				return -1;
			}
			if ((evt.status & 0xf0) == AL_MIDI_NoteOn) {
				u8 off[3] = { (u8)(AL_MIDI_NoteOff | (evt.status & 0x0f)), evt.byte1, 0 };
				if (s_midiVecPush(&midi_events, evt.tick + evt.duration, order++, off, sizeof(off)) != 0) {
					s_midiVecFree(&midi_events);
					free(seq);
					return -1;
				}
			}
		} else if (evt.type == AL_CSP_LOOPSTART || evt.type == AL_CSP_LOOPEND) {
			saw_loops = 1;
		}

		if (s_bytebufPrintf(json,
				"%s    { \"tick\": %u, \"track\": %u, \"type\": ",
				parsed_count > 1 ? ",\n" : "",
				(unsigned)evt.tick,
				(unsigned)evt.track) != 0 ||
				s_bytebufAppendJsonString(json, s_seqEventName(evt.type)) != 0 ||
				s_bytebufPrintf(json,
				", \"status\": %u, \"status_name\": ",
				(unsigned)evt.status) != 0 ||
				s_bytebufAppendJsonString(json, s_midiStatusName(evt.status)) != 0 ||
				s_bytebufPrintf(json,
				", \"channel\": %u, \"byte1\": %u, \"byte2\": %u, "
				"\"duration\": %u, \"tempo_us\": %u, \"loop_count\": %u }",
				(unsigned)(evt.status & 0x0f),
				(unsigned)evt.byte1,
				(unsigned)evt.byte2,
				(unsigned)evt.duration,
				(unsigned)tempo_us,
				(unsigned)evt.loop_count) != 0) {
			s_midiVecFree(&midi_events);
			free(seq);
			return -1;
		}

		if (evt.type == AL_SEQ_END_EVT) break;
	}

	if (s_bytebufPrintf(json, "\n  ]\n}\n") != 0) {
		s_midiVecFree(&midi_events);
		free(seq);
		return -1;
	}

	qsort(midi_events.items, midi_events.count, sizeof(midi_events.items[0]), s_midiEvtCmp);
	if (s_buildMidiFile(&midi_events, division, mid) != 0) {
		s_midiVecFree(&midi_events);
		free(seq);
		return -1;
	}

	*out_division = division;
	*out_event_count = parsed_count;
	*out_loops = saw_loops;
	s_midiVecFree(&midi_events);
	free(seq);
	return 0;
}

/* Emit one .pdsong ZIP. Returns 1 written, 0 skipped, -1 failed. */
static s32 s_emitOneSong(s32 slot_idx,
                         const struct seqtableentry *entry,
                         const u8 *seg_data, u32 seg_size,
                         const char *out_dir, s32 force_rewrite)
{
	if (entry->binlen == 0) {
		/* Empty slot: zero-length sequence. Skip silently. */
		return 0;
	}

	char catalog_id[64];
	s_buildSongCatalogId(slot_idx, catalog_id, sizeof(catalog_id));

	char filename_slug[64];
	s_filenameSlug(catalog_id, filename_slug, sizeof(filename_slug));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel),
		"%s/%s.pdsong", out_dir, filename_slug);

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasSongPayloads(dst_rel)) return 0;

	/* Detect whether the slice is zlib-compressed (ziplen > 0 means
	 * the runtime needs zip-decompression before play). The schema's
	 * format string distinguishes uncompressed (loose ALSEQ) from
	 * zlib-wrapped. Either way the byte slice carries the on-disk
	 * form; the loader decompresses if needed.
	 *
	 * NOTE: ziplen == 0 in practice means binlen == ziplen and the
	 * data is uncompressed. ziplen > 0 means zlib payload.
	 *
	 * We expose both lengths in the manifest for round-trip parity, but
	 * authored contents are the converted sequence.mid/sequence.json pair. */
	const char *fmt_str = (entry->ziplen > 0) ? "N64_CMIDI_RZIP" : "N64_CMIDI";

	/* The slice we extract is whichever length is on disk. PD writes
	 * the larger of binlen and ziplen, but both should bound the
	 * actual byte range. We use ziplen if non-zero (compressed), else
	 * binlen (uncompressed). */
	u32 slice_len = (entry->ziplen > 0) ? (u32)entry->ziplen
	                                     : (u32)entry->binlen;

	if ((u32)entry->romaddr + slice_len > seg_size) {
		sysLoudFailf("EXTRACT.PDSONG",
			"slot=%d slice len=%u past segment (romaddr=0x%x size=0x%x)",
			slot_idx, (unsigned)slice_len,
			(unsigned)entry->romaddr, (unsigned)seg_size);
		return -1;
	}

	u8 *seq_bytes = NULL;
	u32 seq_len = 0;
	if (entry->ziplen > 0) {
		u32 alloc_len = (u32)entry->binlen + 0x40;
		seq_bytes = (u8 *)malloc(alloc_len);
		if (!seq_bytes) {
			sysLoudFailf("EXTRACT.PDSONG",
				"malloc failed for slot=%d inflated len=%u",
				slot_idx, (unsigned)alloc_len);
			return -1;
		}
		s32 inflated = rzipInflate((void *)(seg_data + entry->romaddr), seq_bytes, NULL);
		if (inflated <= 0 || (u32)inflated > alloc_len) {
			free(seq_bytes);
			sysLoudFailf("EXTRACT.PDSONG",
				"rzip inflate failed for slot=%d ziplen=%u binlen=%u",
				slot_idx, (unsigned)entry->ziplen, (unsigned)entry->binlen);
			return -1;
		}
		seq_len = (u32)inflated;
	} else {
		seq_len = (u32)entry->binlen;
		seq_bytes = (u8 *)malloc(seq_len);
		if (!seq_bytes) {
			sysLoudFailf("EXTRACT.PDSONG",
				"malloc failed for slot=%d seq len=%u",
				slot_idx, (unsigned)seq_len);
			return -1;
		}
		memcpy(seq_bytes, seg_data + entry->romaddr, seq_len);
	}

	pdsong_bytebuf_t mid_buf;
	pdsong_bytebuf_t json_buf;
	memset(&mid_buf, 0, sizeof(mid_buf));
	memset(&json_buf, 0, sizeof(json_buf));
	u32 division = 0;
	u32 event_count = 0;
	s32 loops = 0;
	if (s_exportSequence(seq_bytes, seq_len, &mid_buf, &json_buf,
	                     &division, &event_count, &loops) != 0) {
		free(seq_bytes);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"sequence export failed for slot=%d seq_len=%u",
			slot_idx, (unsigned)seq_len);
		return -1;
	}
	free(seq_bytes);

	char manifest_buf[1200];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"song\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"source_format\": \"%s\",\n"
		"  \"format\": \"MIDI\",\n"
		"  \"midi\": \"sequence.mid\",\n"
		"  \"events\": \"sequence.json\",\n"
		"  \"midi_size\": %u,\n"
		"  \"events_size\": %u,\n"
		"  \"division\": %u,\n"
		"  \"event_count\": %u,\n"
		"  \"binlen\": %u,\n"
		"  \"ziplen\": %u,\n"
		"  \"loops\": %s,\n"
		"  \"source_index\": %d,\n"
		"  \"source_offset\": %u\n"
		"}\n",
		catalog_id, fmt_str,
		(unsigned)mid_buf.len,
		(unsigned)json_buf.len,
		(unsigned)division,
		(unsigned)event_count,
		(unsigned)entry->binlen,
		(unsigned)entry->ziplen,
		loops ? "true" : "false",
		slot_idx,
		(unsigned)entry->romaddr);
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"manifest.json snprintf truncated for slot=%d", slot_idx);
		return -1;
	}

	char ini_buf[768];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[music]\n"
		"catalog_id = %s\n"
		"source_format = %s\n"
		"format = MIDI\n"
		"music_file = sequence.mid\n"
		"midi_file = sequence.mid\n"
		"events_file = sequence.json\n"
		"midi_size = %u\n"
		"events_size = %u\n"
		"division = %u\n"
		"event_count = %u\n"
		"binlen = %u\n"
		"ziplen = %u\n"
		"loops = %s\n"
		"source_index = %d\n"
		"source_offset = %u\n",
		catalog_id, fmt_str,
		(unsigned)mid_buf.len,
		(unsigned)json_buf.len,
		(unsigned)division,
		(unsigned)event_count,
		(unsigned)entry->binlen,
		(unsigned)entry->ziplen,
		loops ? "true" : "false",
		slot_idx,
		(unsigned)entry->romaddr);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"music.ini snprintf truncated for slot=%d", slot_idx);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"fsFullPath empty for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	asset_archive_writer_t asset_writer;
	if (assetArchiveWriterInit(&asset_writer, aw, "song", catalog_id) !=
			MODARCHIVE_OK) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"assetArchiveWriterInit failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	assetArchiveWriterSetProvenance(&asset_writer, "romextract_pdsong",
		"sequences", slot_idx, "");

	if (assetArchiveWriterAddDescriptor(&asset_writer, "music.ini",
			ini_buf, (u32)ini_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem music.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		return -1;
	}
	if (assetArchiveWriterAddManifestJson(&asset_writer,
			manifest_buf, (u32)manifest_len) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem _meta/manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "sequence.mid",
			(const char *)mid_buf.data, mid_buf.len, "midi") !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem sequence.mid failed for \"%s\" "
			"(slot=%d len=%u)",
			dst_full, slot_idx, (unsigned)mid_buf.len);
		modArchiveAbort(aw);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		return -1;
	}

	if (assetArchiveWriterAddPublicMem(&asset_writer, "sequence.json",
			(const char *)json_buf.data, json_buf.len, "events") !=
			MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSONG",
			"AddFileMem sequence.json failed for \"%s\" "
			"(slot=%d len=%u)",
			dst_full, slot_idx, (unsigned)json_buf.len);
		modArchiveAbort(aw);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		return -1;
	}

	if (assetArchiveWriterFinishMetadata(&asset_writer) != MODARCHIVE_OK) {
		sysLoudFailf("EXTRACT.PDSONG",
			"assetArchiveWriterFinishMetadata failed for \"%s\" (slot=%d)",
			dst_full, slot_idx);
		modArchiveAbort(aw);
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		s_bytebufFree(&mid_buf);
		s_bytebufFree(&json_buf);
		sysLoudFailf("EXTRACT.PDSONG",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	s_bytebufFree(&mid_buf);
	s_bytebufFree(&json_buf);
	return 1;
}

/* Engine Phase 4: per-song fan-out context. */
typedef struct {
	const struct seqtable *table;
	const u8              *seg_data;
	u32                    seg_size;
	const char            *out_dir;
	s32                    force_rewrite;
	s32                    count;
	SDL_atomic_t           written;
	SDL_atomic_t           skipped;
	SDL_atomic_t           failed;
	SDL_atomic_t           processed;
} pdsong_fanout_ctx_t;

static void s_pdsongWork(int i, void *user)
{
	pdsong_fanout_ctx_t *c = (pdsong_fanout_ctx_t *)user;
	if (i < 0 || i >= c->count) return;

	s32 r = s_emitOneSong(i, &c->table->entries[i],
	                      c->seg_data, c->seg_size,
	                      c->out_dir, c->force_rewrite);
	if (r > 0)        SDL_AtomicAdd(&c->written, 1);
	else if (r == 0)  SDL_AtomicAdd(&c->skipped, 1);
	else              SDL_AtomicAdd(&c->failed,  1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x0f) == 0 || done == c->count) {
		bootProgressUpdate(done, c->count);
	}
}

s32 romExtractAllPdsong(s32 force_rewrite)
{
	const u8 *seg_data = NULL;
	u32 seg_size = 0;

	if (!s_findSegment("sequences", &seg_data, &seg_size)) {
		sysLogPrintf(LOG_NOTE,
			"romextract pdsong: \"sequences\" segment not loaded "
			"(server build or pre-romdata-init); skipping");
		return 0;
	}

	/* The segment starts with `struct seqtable` { u16 count; entries[1] }.
	 * preprocessSequences byte-swaps count + each entry's three fields
	 * to native at romdataInit time, so direct read is safe. The
	 * runtime DMAs the first 16 bytes then re-DMAs to get the count;
	 * we read the buffer directly here. */
	if (seg_size < sizeof(u16)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"sequences segment too small (size=%u)",
			(unsigned)seg_size);
		return -1;
	}

	const struct seqtable *table = (const struct seqtable *)seg_data;
	u16 count = table->count;

	/* Sanity: bound count by what could possibly fit in the segment.
	 * struct seqtable's entries[] starts at offset sizeof(u16) (no
	 * struct padding to round up; the runtime allocates count *
	 * sizeof(seqtableentry) + 4 per src/lib/snd.c::sndLoad). */
	const u32 hdr_bytes = (u32)((const u8 *)&table->entries[0] - seg_data);
	const u32 max_by_size = (seg_size - hdr_bytes) / sizeof(struct seqtableentry);
	if ((u32)count > max_by_size) {
		sysLoudFailf("EXTRACT.PDSONG",
			"seqtable count=%u exceeds segment-derived cap %u "
			"(seg_size=%u hdr_bytes=%u)",
			(unsigned)count, (unsigned)max_by_size,
			(unsigned)seg_size, (unsigned)hdr_bytes);
		return -1;
	}

	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDSONG", "fsDataDirEnsure failed");
		return -1;
	}

	char dataDirBuf[FS_MAXPATH + 1];
	const char *dataDir = fsDataDir(dataDirBuf, sizeof(dataDirBuf));

	/* B-320 (2026-05-03): create the parent audio/ dir before the leaf
	 * audio/music subdir. _mkdir does not create intermediate directories
	 * on Windows. */
	char audio_parent[FS_MAXPATH];
	snprintf(audio_parent, sizeof(audio_parent), "%s/audio", dataDir);
	if (!fsCreateDir(audio_parent)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"fsCreateDir(\"%s\") failed", audio_parent);
		return -1;
	}

	char out_dir[FS_MAXPATH];
	snprintf(out_dir, sizeof(out_dir), "%s/%s", dataDir, PDSONG_OUT_DIR);
	if (!fsCreateDir(out_dir)) {
		sysLoudFailf("EXTRACT.PDSONG",
			"fsCreateDir(\"%s\") failed", out_dir);
		return -1;
	}

	const char *cache_kind = "pdsong_sequence_json_v1";
	if (romExtractPdFastCacheCanSkip(cache_kind, out_dir,
			".pdsong", force_rewrite)) {
		bootProgressUpdate((s32)count, (s32)count);
		sysLogPrintf(LOG_NOTE,
			"romextract pdsong: written=0 skipped=%u failed=0 "
			"total=%u (out=%s, fast-cache)",
			(unsigned)count, (unsigned)count, out_dir);
		return 0;
	}

	/* In-place cache-kind bump (B-943): force a one-time per-file rewrite when
	 * the stored kind differs from the current one (no-op on clean install or
	 * unchanged kind). See romExtractPdFastCacheKindMismatch. */
	s32 effective_force = force_rewrite |
		romExtractPdFastCacheKindMismatch(cache_kind, out_dir);

	pdsong_fanout_ctx_t sctx;
	memset(&sctx, 0, sizeof(sctx));
	sctx.table         = table;
	sctx.seg_data      = seg_data;
	sctx.seg_size      = seg_size;
	sctx.out_dir       = out_dir;
	sctx.force_rewrite = effective_force;
	sctx.count         = (s32)count;
	SDL_AtomicSet(&sctx.written,   0);
	SDL_AtomicSet(&sctx.skipped,   0);
	SDL_AtomicSet(&sctx.failed,    0);
	SDL_AtomicSet(&sctx.processed, 0);

	bootProgressUpdate(0, (s32)count);
	bootPoolForRangeBlocking(0, (int)count, s_pdsongWork, &sctx);
	bootProgressUpdate((s32)count, (s32)count);

	s32 written = SDL_AtomicGet(&sctx.written);
	s32 skipped = SDL_AtomicGet(&sctx.skipped);
	s32 failed  = SDL_AtomicGet(&sctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdsong: written=%d skipped=%d failed=%d "
		"total=%u (out=%s)",
		written, skipped, failed, (unsigned)count, out_dir);

	if (failed == 0) {
		romExtractPdFastCacheWrite("pdsong_sequence_json_v1", out_dir, ".pdsong");
	}

	return failed ? -1 : written;
}
