/**
 * modpack_pdmod.c -- Priority M / B-238 shared `.pdmod` save helper.
 *
 * Implements port/include/modpack_pdmod.h. Builds on top of modarchive's
 * writer; layered above modmgr's manifest fields so the comment mirror
 * has access to the same names / authors / versions the loader will see
 * later.
 *
 * Single source of truth for the comment mirror: the manifest JSON
 * embedded inside the archive. The mirror is a projection of the headline
 * fields (name, creator/author, version, tags) computed at write time;
 * never written separately, never editable independently.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "system.h"
#include "modarchive.h"
#include "modpack_pdmod.h"

/* ------------------------------------------------------------ JSON helper */

/* Locate the start of a quoted string value associated with the given
 * top-level key. Returns a pointer to the first character of the value
 * (inside the opening quote) and writes the length to *outLen. Returns
 * NULL when the key is missing or its value is not a string. */
static const char *findJsonStringValue(const char *json, const char *key, u32 *outLen)
{
	if (!json || !key || !outLen) return NULL;
	*outLen = 0;

	size_t klen = strlen(key);
	const char *p = json;
	while (p && *p) {
		const char *q = strchr(p, '"');
		if (!q) return NULL;
		const char *qend = strchr(q + 1, '"');
		if (!qend) return NULL;
		/* q+1 .. qend = key candidate */
		if ((size_t)(qend - q - 1) == klen && memcmp(q + 1, key, klen) == 0) {
			/* Skip whitespace + colon. */
			const char *r = qend + 1;
			while (*r && (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')) r++;
			if (*r != ':') {
				p = qend + 1;
				continue;
			}
			r++;
			while (*r && (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')) r++;
			if (*r != '"') return NULL;
			r++;
			const char *vEnd = r;
			while (*vEnd && *vEnd != '"') {
				if (*vEnd == '\\' && vEnd[1]) vEnd += 2;
				else vEnd++;
			}
			if (*vEnd != '"') return NULL;
			*outLen = (u32)(vEnd - r);
			return r;
		}
		p = qend + 1;
	}
	return NULL;
}

/* Build the comment-mirror JSON blob. Output into `out` (cap `cap`).
 * Returns the number of bytes written (excluding the trailing NUL). The
 * blob is bounded so it always fits inside the zip's 64 KiB comment field;
 * we cap each string at a few hundred chars. */
static u32 buildCommentMirror(const char *manifest, char *out, u32 cap)
{
	if (!manifest || !out || cap < 4) return 0;

	u32 nameLen = 0, creatorLen = 0, versionLen = 0;
	const char *namePtr = findJsonStringValue(manifest, "name", &nameLen);
	const char *creatorPtr = findJsonStringValue(manifest, "creator", &creatorLen);
	if (!creatorPtr) {
		creatorPtr = findJsonStringValue(manifest, "author", &creatorLen);
	}
	const char *versionPtr = findJsonStringValue(manifest, "version", &versionLen);

	/* Clamp each field to a sane upper bound; the mirror is for tools
	 * peeking at the file, not a full manifest replacement. */
	if (nameLen > 200)    nameLen = 200;
	if (creatorLen > 100) creatorLen = 100;
	if (versionLen > 64)  versionLen = 64;

	/* Manual JSON-encode each value so embedded quotes / backslashes do
	 * not corrupt the output. */
	#define EMIT_FIELD(label, src, srcLen)                                   \
		do {                                                                  \
			pos += (u32)snprintf(out + pos, cap - pos, "\"%s\":\"", label);   \
			if (pos >= cap) goto done;                                        \
			for (u32 _i = 0; _i < (srcLen) && pos + 2 < cap; _i++) {          \
				char _c = (src)[_i];                                          \
				if (_c == '"' || _c == '\\') out[pos++] = '\\';               \
				out[pos++] = _c;                                              \
			}                                                                  \
			if (pos < cap) out[pos++] = '"';                                  \
		} while (0)

	u32 pos = 0;
	if (cap == 0) return 0;
	out[pos++] = '{';
	bool needsComma = false;
	if (namePtr) {
		EMIT_FIELD("name", namePtr, nameLen);
		needsComma = true;
	}
	if (creatorPtr) {
		if (needsComma && pos < cap) out[pos++] = ',';
		EMIT_FIELD("creator", creatorPtr, creatorLen);
		needsComma = true;
	}
	if (versionPtr) {
		if (needsComma && pos < cap) out[pos++] = ',';
		EMIT_FIELD("version", versionPtr, versionLen);
	}
done:
	if (pos < cap) out[pos++] = '}';
	if (pos < cap) out[pos] = '\0';
	else           out[cap - 1] = '\0';
	#undef EMIT_FIELD
	return pos;
}

/* ----------------------------------------------------------- Public: bulk */

s32 modpackPdmodWriteSingle(const char *out_path,
                             const char *manifest_json, u32 manifest_len,
                             const modpack_entry_t *entries, s32 entry_count)
{
	if (!out_path || !out_path[0] || !manifest_json) return MODPACK_PDMOD_ERR_OPEN;

	mod_archive_writer_t *w = modArchiveBegin(out_path);
	if (!w) return MODPACK_PDMOD_ERR_OPEN;

	if (modArchiveAddFileMem(w, "mod.json", manifest_json, manifest_len) != MODARCHIVE_OK) {
		modArchiveAbort(w);
		return MODPACK_PDMOD_ERR_IO;
	}

	for (s32 i = 0; i < entry_count; i++) {
		const modpack_entry_t *e = &entries[i];
		if (!e->entry_name || !e->entry_name[0]) continue;
		/* The reader sanitises entry names; the writer does the same so
		 * we surface "this name is not safe" before producing an archive
		 * that the reader would reject. */
		s32 r = modArchiveAddFileMem(w, e->entry_name, e->data, e->len);
		if (r != MODARCHIVE_OK) {
			modArchiveAbort(w);
			return MODPACK_PDMOD_ERR_IO;
		}
	}

	/* Comment mirror -- 1024 byte cap covers headline fields even with
	 * heavy escaping. */
	char mirror[1024];
	if (buildCommentMirror(manifest_json, mirror, sizeof(mirror)) > 0) {
		modArchiveSetComment(w, mirror);
	}

	if (modArchiveFinish(w) != MODARCHIVE_OK) {
		return MODPACK_PDMOD_ERR_IO;
	}
	return MODPACK_PDMOD_OK;
}

/* ----------------------------------------------------------- Public: folder */

/* Skip-file rules for the folder packer:
 *   - dotfiles (.modstate, .DS_Store, .git*, ...)
 *   - thumbnail/cache junk that some tools leave behind
 *   - the destination archive itself (in case the caller is re-packing
 *     into the same directory) */
static int folderShouldSkip(const char *leaf, const char *fullPath, const char *destPath)
{
	if (!leaf || leaf[0] == '.') return 1;
	if (destPath && fullPath && strcmp(fullPath, destPath) == 0) return 1;
	if (!strcmp(leaf, "Thumbs.db") || !strcmp(leaf, "desktop.ini")) return 1;
	return 0;
}

/* Recursive walker. `relRoot` is the relative path inside the archive
 * (e.g. "" at the top level, "assets/textures" deeper in). */
static s32 packFolderRecurse(mod_archive_writer_t *w,
                              const char *fsRoot, const char *relRoot,
                              const char *destPath, u32 *bytesAccum)
{
	char absDir[FS_MAXPATH + 1];
	if (relRoot && relRoot[0]) {
		snprintf(absDir, sizeof(absDir), "%s/%s", fsRoot, relRoot);
	} else {
		strncpy(absDir, fsRoot, FS_MAXPATH);
		absDir[FS_MAXPATH] = '\0';
	}

	DIR *d = opendir(absDir);
	if (!d) return MODPACK_PDMOD_ERR_IO;

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		char childAbs[FS_MAXPATH + 1];
		snprintf(childAbs, sizeof(childAbs), "%s/%s", absDir, ent->d_name);
		if (folderShouldSkip(ent->d_name, childAbs, destPath)) continue;

		struct stat st;
		if (stat(childAbs, &st) != 0) continue;

		char childRel[FS_MAXPATH + 1];
		if (relRoot && relRoot[0]) {
			snprintf(childRel, sizeof(childRel), "%s/%s", relRoot, ent->d_name);
		} else {
			strncpy(childRel, ent->d_name, FS_MAXPATH);
			childRel[FS_MAXPATH] = '\0';
		}

		if (S_ISDIR(st.st_mode)) {
			s32 r = packFolderRecurse(w, fsRoot, childRel, destPath, bytesAccum);
			if (r != MODPACK_PDMOD_OK) {
				closedir(d);
				return r;
			}
		} else if (S_ISREG(st.st_mode)) {
			if ((u64)*bytesAccum + (u64)st.st_size > 0xFFFFFFFFull) {
				closedir(d);
				return MODPACK_PDMOD_ERR_TOO_BIG;
			}
			*bytesAccum += (u32)st.st_size;
			if (modArchiveAddFileDisk(w, childRel, childAbs) != MODARCHIVE_OK) {
				closedir(d);
				return MODPACK_PDMOD_ERR_IO;
			}
		}
	}

	closedir(d);
	return MODPACK_PDMOD_OK;
}

s32 modpackPdmodFromFolder(const char *src_folder, const char *out_path)
{
	if (!src_folder || !src_folder[0] || !out_path || !out_path[0]) {
		return MODPACK_PDMOD_ERR_OPEN;
	}

	/* Read mod.json so the comment mirror has source data. The folder
	 * packer also embeds it into the archive via the regular recursion,
	 * but we read it once up front to compute the mirror string. */
	char mfstPath[FS_MAXPATH + 1];
	snprintf(mfstPath, sizeof(mfstPath), "%s/mod.json", src_folder);
	FILE *mf = fopen(mfstPath, "rb");
	if (!mf) return MODPACK_PDMOD_ERR_NO_MFST;
	fseek(mf, 0, SEEK_END);
	long mfsize = ftell(mf);
	if (mfsize <= 0 || mfsize > (long)(8 * 1024 * 1024)) {
		fclose(mf);
		return MODPACK_PDMOD_ERR_BAD_MFST;
	}
	fseek(mf, 0, SEEK_SET);
	char *mfBuf = (char *)malloc(mfsize + 1);
	if (!mfBuf) {
		fclose(mf);
		return MODPACK_PDMOD_ERR_IO;
	}
	if (fread(mfBuf, 1, mfsize, mf) != (size_t)mfsize) {
		free(mfBuf);
		fclose(mf);
		return MODPACK_PDMOD_ERR_IO;
	}
	fclose(mf);
	mfBuf[mfsize] = '\0';

	mod_archive_writer_t *w = modArchiveBegin(out_path);
	if (!w) {
		free(mfBuf);
		return MODPACK_PDMOD_ERR_OPEN;
	}

	u32 bytesAccum = 0;
	s32 r = packFolderRecurse(w, src_folder, "", out_path, &bytesAccum);
	if (r != MODPACK_PDMOD_OK) {
		modArchiveAbort(w);
		free(mfBuf);
		return r;
	}

	char mirror[1024];
	if (buildCommentMirror(mfBuf, mirror, sizeof(mirror)) > 0) {
		modArchiveSetComment(w, mirror);
	}

	if (modArchiveFinish(w) != MODARCHIVE_OK) {
		free(mfBuf);
		return MODPACK_PDMOD_ERR_IO;
	}
	free(mfBuf);
	return MODPACK_PDMOD_OK;
}
