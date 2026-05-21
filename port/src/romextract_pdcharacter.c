/**
 * romextract_pdcharacter.c -- typed character asset archive emitter.
 *
 * Emits canonical .pdcharacter archives for base MP characters without
 * touching the separate weapon graph lane. A .pdcharacter is the
 * full character-facing content unit; the existing .pdhead/.pdbody
 * archives remain lower-level dependencies while the runtime still uses
 * the legacy head/body split.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <PR/ultratypes.h>

#include "boot_pool.h"
#include "boot_progress.h"
#include "bodydata_authored.h"
#include "data.h"
#include "fs.h"
#include "headdata_authored.h"
#include "modarchive.h"
#include "romextract_pd.h"
#include "system.h"
#include "types.h"

#define PDCHARACTER_MP_BODY_COUNT 63

static void s_idToFilename(const char *id, char *out, size_t n)
{
	if (!id || !out || n == 0) {
		if (out && n) out[0] = '\0';
		return;
	}

	size_t i;
	for (i = 0; i + 1 < n && id[i]; i++) {
		out[i] = (id[i] == ':') ? '_' : id[i];
	}
	out[i] = '\0';
}

static void s_characterIdForBody(const char *body_id, char *out, size_t n)
{
	if (!out || n == 0) return;
	out[0] = '\0';
	if (!body_id || !body_id[0]) return;

	const char *slug = strchr(body_id, ':');
	slug = slug ? slug + 1 : body_id;
	snprintf(out, n, "base:character_%s", slug);
}

static s32 s_existingArchiveHasEntry(const char *relpath, const char *entry)
{
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;

	s32 has_entry = modArchiveFindEntry(arc, entry) >= 0;
	modArchiveClose(arc);
	return has_entry;
}

static s32 s_existingArchiveEntryContains(const char *relpath, const char *entry,
	const char *needle)
{
	if (!needle || !needle[0]) return 1;
	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) return 0;

	mod_archive_t *arc = modArchiveOpen(full);
	if (!arc) return 0;

	s32 idx = modArchiveFindEntry(arc, entry);
	if (idx < 0) {
		modArchiveClose(arc);
		return 0;
	}

	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(arc, idx, &size);
	s32 found = 0;
	if (bytes) {
		const char *hay = (const char *)bytes;
		size_t nlen = strlen(needle);
		for (u32 i = 0; i + nlen <= size; i++) {
			if (memcmp(hay + i, needle, nlen) == 0) {
				found = 1;
				break;
			}
		}
		free(bytes);
	}
	modArchiveClose(arc);
	return found;
}

static s32 s_addArchiveFile(mod_archive_writer_t *aw, const char *inner,
	const char *relpath, const char *dst_full)
{
	if (!relpath || !relpath[0] || fsFileSize(relpath) <= 0) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"required dependency %s missing for \"%s\" (rel=\"%s\")",
			inner ? inner : "(null)", dst_full ? dst_full : "(null)",
			relpath ? relpath : "(null)");
		return -1;
	}

	char full_buf[FS_MAXPATH + 1];
	const char *full = fsFullPath(relpath, full_buf, sizeof(full_buf));
	if (!full || !full[0]) {
		return -1;
	}

	if (modArchiveAddFileDisk(aw, inner, full) != 0) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"AddFileDisk %s failed for \"%s\"", inner,
			dst_full);
		return -1;
	}

	return 1;
}

static s32 s_emitOneCharacter(s32 mpbody_idx, const char *out_dir,
	s32 force_rewrite)
{
	if (mpbody_idx < 0 || mpbody_idx >= PDCHARACTER_MP_BODY_COUNT) return 0;

	const struct mpbody *mpbody = &g_MpBodies[mpbody_idx];
	const body_authored_record_t *body =
		bodyDataLookupByBodynum((s32)mpbody->bodynum);
	if (!body || !body->catalog_id || !body->catalog_id[0]) return 0;

	const head_authored_record_t *head =
		headDataLookupByHeadnum((s32)mpbody->headnum);

	char character_id[128];
	s_characterIdForBody(body->catalog_id, character_id, sizeof(character_id));
	if (!character_id[0]) return 0;

	char character_file[128];
	s_idToFilename(character_id, character_file, sizeof(character_file));

	char dst_rel[FS_MAXPATH];
	snprintf(dst_rel, sizeof(dst_rel), "%s/%s.pdcharacter",
		out_dir, character_file);

	char body_file[128];
	s_idToFilename(body->catalog_id, body_file, sizeof(body_file));

	char data_dir[FS_MAXPATH + 1];
	const char *data_root = fsDataDir(data_dir, sizeof(data_dir));

	char body_rel[FS_MAXPATH];
	snprintf(body_rel, sizeof(body_rel), "%s/bodies/%s.pdbody",
		data_root, body_file);

	char head_rel[FS_MAXPATH];
	head_rel[0] = '\0';
	if (head && head->catalog_id && head->catalog_id[0]) {
		char head_file[128];
		s_idToFilename(head->catalog_id, head_file, sizeof(head_file));
		snprintf(head_rel, sizeof(head_rel), "%s/heads/%s.pdhead",
			data_root, head_file);
	}

	if (!force_rewrite && fsFileSize(dst_rel) > 0 &&
	    s_existingArchiveHasEntry(dst_rel, "character.ini") &&
	    s_existingArchiveEntryContains(dst_rel, "character.ini",
		    "dependency_closure = embedded.v2") &&
	    s_existingArchiveHasEntry(dst_rel, "body.pdbody") &&
	    (!head || s_existingArchiveHasEntry(dst_rel, "head.pdhead"))) {
		return 0;
	}

	char manifest_buf[1024];
	int manifest_len = snprintf(manifest_buf, sizeof(manifest_buf),
		"{\n"
		"  \"pd_kind\": \"character\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"%s\",\n"
		"  \"dependency_closure\": \"embedded.v2\",\n"
		"  \"body\": \"%s\",\n"
		"  \"head\": %s%s%s,\n"
		"  \"mp_body_index\": %d,\n"
		"  \"body_archive\": \"body.pdbody\",\n"
		"  \"head_archive\": %s%s%s\n"
		"}\n",
		character_id,
		body->catalog_id,
		head ? "\"" : "", head ? head->catalog_id : "null", head ? "\"" : "",
		mpbody_idx,
		head ? "\"" : "", head ? "head.pdhead" : "null", head ? "\"" : "");
	if (manifest_len <= 0 || (size_t)manifest_len >= sizeof(manifest_buf)) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"manifest.json snprintf truncated for \"%s\"", character_id);
		return -1;
	}

	char ini_buf[1024];
	int ini_len = snprintf(ini_buf, sizeof(ini_buf),
		"[character]\n"
		"catalog_id = %s\n"
		"schema = character.v1\n"
		"dependency_closure = embedded.v2\n"
		"body_asset = %s\n"
		"head_asset = %s\n"
		"bodyfile = body.pdbody\n"
		"headfile = %s\n"
		"mp_body_index = %d\n"
		"mp_bodynum = %d\n"
		"mp_headnum = %d\n"
		"requirefeature = %u\n",
		character_id,
		body->catalog_id,
		head && head->catalog_id ? head->catalog_id : "",
		head ? "head.pdhead" : "",
		mpbody_idx,
		(s32)mpbody->bodynum,
		(s32)mpbody->headnum,
		(unsigned)mpbody->requirefeature);
	if (ini_len <= 0 || (size_t)ini_len >= sizeof(ini_buf)) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"character.ini snprintf truncated for \"%s\"", character_id);
		return -1;
	}

	char dst_full_buf[FS_MAXPATH + 1];
	const char *dst_full = fsFullPath(dst_rel, dst_full_buf, sizeof(dst_full_buf));
	if (!dst_full || !dst_full[0]) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"fsFullPath failed for \"%s\"", dst_rel);
		return -1;
	}

	mod_archive_writer_t *aw = modArchiveBegin(dst_full);
	if (!aw) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"modArchiveBegin failed for \"%s\"", dst_full);
		return -1;
	}

	if (modArchiveAddFileMem(aw, "character.ini", ini_buf, (u32)ini_len) != 0) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"AddFileMem character.ini failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	if (modArchiveAddFileMem(aw, "manifest.json",
		manifest_buf, (u32)manifest_len) != 0) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"AddFileMem manifest.json failed for \"%s\"", dst_full);
		modArchiveAbort(aw);
		return -1;
	}
	if (s_addArchiveFile(aw, "body.pdbody", body_rel, dst_full) < 0) {
		modArchiveAbort(aw);
		return -1;
	}
	if (head && s_addArchiveFile(aw, "head.pdhead", head_rel, dst_full) < 0) {
		modArchiveAbort(aw);
		return -1;
	}

	if (modArchiveFinish(aw) != 0) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"modArchiveFinish failed for \"%s\"", dst_full);
		return -1;
	}

	return 1;
}

typedef struct {
	const char  *characters_dir;
	s32          force_rewrite;
	SDL_atomic_t written;
	SDL_atomic_t skipped;
	SDL_atomic_t failed;
	SDL_atomic_t processed;
} pdcharacter_fanout_ctx_t;

static void s_pdcharacterWork(int i, void *user)
{
	pdcharacter_fanout_ctx_t *c = (pdcharacter_fanout_ctx_t *)user;
	if (i < 0 || i >= PDCHARACTER_MP_BODY_COUNT) return;

	s32 r = s_emitOneCharacter(i, c->characters_dir, c->force_rewrite);
	if (r > 0)       SDL_AtomicAdd(&c->written, 1);
	else if (r == 0) SDL_AtomicAdd(&c->skipped, 1);
	else             SDL_AtomicAdd(&c->failed, 1);

	int done = SDL_AtomicAdd(&c->processed, 1) + 1;
	if ((done & 0x07) == 0 || done == PDCHARACTER_MP_BODY_COUNT) {
		bootProgressUpdate(done, PDCHARACTER_MP_BODY_COUNT);
	}
}

s32 romExtractAllPdcharacter(s32 force_rewrite)
{
	if (!fsDataDirEnsure()) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"fsDataDirEnsure failed; cannot create output dir");
		return -1;
	}

	char data_dir[FS_MAXPATH + 1];
	char characters_dir[FS_MAXPATH];
	snprintf(characters_dir, sizeof(characters_dir), "%s/characters",
		fsDataDir(data_dir, sizeof(data_dir)));
	if (!fsCreateDir(characters_dir)) {
		sysLoudFailf("EXTRACT.PDCHARACTER",
			"fsCreateDir(\"%s\") failed", characters_dir);
		return -1;
	}

	pdcharacter_fanout_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.characters_dir = characters_dir;
	ctx.force_rewrite = force_rewrite;
	SDL_AtomicSet(&ctx.written, 0);
	SDL_AtomicSet(&ctx.skipped, 0);
	SDL_AtomicSet(&ctx.failed, 0);
	SDL_AtomicSet(&ctx.processed, 0);

	bootProgressUpdate(0, PDCHARACTER_MP_BODY_COUNT);
	bootPoolForRangeBlocking(0, PDCHARACTER_MP_BODY_COUNT,
		s_pdcharacterWork, &ctx);
	bootProgressUpdate(PDCHARACTER_MP_BODY_COUNT, PDCHARACTER_MP_BODY_COUNT);

	s32 written = SDL_AtomicGet(&ctx.written);
	s32 skipped = SDL_AtomicGet(&ctx.skipped);
	s32 failed = SDL_AtomicGet(&ctx.failed);

	sysLogPrintf(LOG_NOTE,
		"romextract pdcharacter: written=%d skipped=%d failed=%d total=%d",
		written, skipped, failed, PDCHARACTER_MP_BODY_COUNT);

	return written;
}
