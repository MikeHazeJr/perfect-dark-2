/**
 * modpack_archive.c -- .pdpack component archives: see modpack_archive.h.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <PR/ultratypes.h>

#include "asset_path_contract.h"
#include "fs.h"
#include "modpack_archive.h"
#include "pdca_extract_transaction.h"

typedef struct modpack_path_list {
	char **paths;
	u32 count;
	u32 cap;
} modpack_path_list_t;

static void archiveErr(char *err, size_t err_cap, const char *what, const char *path)
{
	if (!err || !err_cap) {
		return;
	}
	if (path && path[0]) {
		snprintf(err, err_cap, "%s: %s", what, path);
	} else {
		snprintf(err, err_cap, "%s", what);
	}
}

static void putLe16(u8 *p, u16 value)
{
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
}

static void putLe32(u8 *p, u32 value)
{
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
	p[2] = (u8)(value >> 16);
	p[3] = (u8)(value >> 24);
}

static void pathListFree(modpack_path_list_t *list)
{
	for (u32 i = 0; i < list->count; i++) {
		free(list->paths[i]);
	}
	free(list->paths);
	list->paths = NULL;
	list->count = 0;
	list->cap = 0;
}

static s32 pathListAdd(modpack_path_list_t *list, const char *path)
{
	size_t len;
	char *copy;

	if (list->count >= MODPACK_ARCHIVE_MAX_FILES) {
		return 0;
	}
	if (list->count == list->cap) {
		const u32 cap = list->cap ? list->cap * 2u : 32u;
		char **grown = (char **)realloc(list->paths, (size_t)cap * sizeof(*grown));
		if (!grown) {
			return 0;
		}
		list->paths = grown;
		list->cap = cap;
	}
	len = strlen(path) + 1;
	copy = (char *)malloc(len);
	if (!copy) {
		return 0;
	}
	memcpy(copy, path, len);
	list->paths[list->count++] = copy;
	return 1;
}

static int comparePathStrings(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Every regular file under root/relprefix, as a path relative to root. */
static s32 collectFiles(modpack_path_list_t *list, const char *root,
	const char *relprefix, char *err, size_t err_cap)
{
	char folder[FS_MAXPATH];
	DIR *dir;
	struct dirent *ent;
	s32 ok = 1;

	if (relprefix[0]
			? !assetPathJoinChecked(folder, sizeof(folder), root, "/", relprefix)
			: !assetPathCopyChecked(folder, sizeof(folder), root)) {
		archiveErr(err, err_cap, "path too long", relprefix);
		return 0;
	}
	dir = opendir(folder);
	if (!dir) {
		archiveErr(err, err_cap, "cannot open folder", folder);
		return 0;
	}
	while (ok && (ent = readdir(dir)) != NULL) {
		char child_rel[FS_MAXPATH];
		char child_abs[FS_MAXPATH];
		struct stat st;

		if (ent->d_name[0] == '.') {
			continue; /* ".", ".." and hidden files are never packed */
		}
		if (relprefix[0]
				? !assetPathJoinChecked(child_rel, sizeof(child_rel), relprefix, "/", ent->d_name)
				: !assetPathCopyChecked(child_rel, sizeof(child_rel), ent->d_name)) {
			archiveErr(err, err_cap, "path too long", ent->d_name);
			ok = 0;
			break;
		}
		if (!assetPathJoinChecked(child_abs, sizeof(child_abs), root, "/", child_rel)) {
			archiveErr(err, err_cap, "path too long", child_rel);
			ok = 0;
			break;
		}
		if (stat(child_abs, &st) != 0) {
			archiveErr(err, err_cap, "cannot read", child_rel);
			ok = 0;
			break;
		}
		if (S_ISDIR(st.st_mode)) {
			ok = collectFiles(list, root, child_rel, err, err_cap);
		} else if (S_ISREG(st.st_mode) && !pathListAdd(list, child_rel)) {
			archiveErr(err, err_cap, "too many files, or out of memory", child_rel);
			ok = 0;
		}
	}
	closedir(dir);
	return ok;
}

/* Each member must pass the extractor's own rule, and no two members may be
 * one file to Windows (paths equal ignoring case). */
static s32 checkMemberPaths(const modpack_path_list_t *list, char *err, size_t err_cap)
{
	char **keys = (char **)calloc(list->count, sizeof(*keys));
	s32 ok = keys != NULL;

	if (!keys) {
		archiveErr(err, err_cap, "out of memory", NULL);
	}
	for (u32 i = 0; ok && i < list->count; i++) {
		char normalized[FS_MAXPATH];
		char key[FS_MAXPATH];
		size_t len;

		if (!pdcaNormalizeMemberPath(normalized, sizeof(normalized), key, sizeof(key),
					list->paths[i])
				|| strcmp(normalized, list->paths[i]) != 0) {
			archiveErr(err, err_cap, "a file name the importer would refuse", list->paths[i]);
			ok = 0;
			break;
		}
		len = strlen(key) + 1;
		keys[i] = (char *)malloc(len);
		if (!keys[i]) {
			archiveErr(err, err_cap, "out of memory", NULL);
			ok = 0;
			break;
		}
		memcpy(keys[i], key, len);
	}
	if (ok) {
		qsort(keys, list->count, sizeof(*keys), comparePathStrings);
		for (u32 i = 1; i < list->count; i++) {
			if (strcmp(keys[i - 1], keys[i]) == 0) {
				archiveErr(err, err_cap, "two files whose names differ only in case", keys[i]);
				ok = 0;
				break;
			}
		}
	}
	if (keys) {
		for (u32 i = 0; i < list->count; i++) {
			free(keys[i]);
		}
		free(keys);
	}
	return ok;
}

/* Appends one member, read whole from root/rel, growing the buffer. */
static s32 appendFile(u8 **buf, u64 *len, u64 *cap, const char *root, const char *rel,
	char *err, size_t err_cap)
{
	char path[FS_MAXPATH];
	FILE *fp;
	long size = -1;
	s32 ok;

	if (!assetPathJoinChecked(path, sizeof(path), root, "/", rel)) {
		archiveErr(err, err_cap, "path too long", rel);
		return 0;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		archiveErr(err, err_cap, "cannot open", rel);
		return 0;
	}
	ok = fseek(fp, 0, SEEK_END) == 0;
	if (ok) {
		size = ftell(fp);
		ok = size >= 0 && fseek(fp, 0, SEEK_SET) == 0;
	}
	if (!ok) {
		archiveErr(err, err_cap, "cannot read", rel);
	}
	if (ok) {
		const u16 path_len = (u16)(strlen(rel) + 1);
		const u64 need = *len + 2u + path_len + 4u + (u64)size;

		if (need > 0xFFFFFFFFull) {
			archiveErr(err, err_cap, "the archive would pass 4 GiB at", rel);
			ok = 0;
		}
		if (ok && need > *cap) {
			u64 grown_cap = *cap ? *cap : 65536u;
			u8 *grown;

			while (grown_cap < need) {
				grown_cap *= 2u;
			}
			if (grown_cap > 0xFFFFFFFFull) {
				grown_cap = need;
			}
			grown = (u8 *)realloc(*buf, (size_t)grown_cap);
			if (!grown) {
				archiveErr(err, err_cap, "out of memory at", rel);
				ok = 0;
			} else {
				*buf = grown;
				*cap = grown_cap;
			}
		}
		if (ok) {
			u8 *p = *buf + *len;

			putLe16(p, path_len);
			p += 2;
			memcpy(p, rel, path_len);
			p += path_len;
			putLe32(p, (u32)size);
			p += 4;
			/* A file that changes while it is read fails the build rather
			 * than packing a mix of old and new bytes. */
			if (fread(p, 1, (size_t)size, fp) != (size_t)size || fgetc(fp) != EOF
					|| ferror(fp)) {
				archiveErr(err, err_cap, "could not read the whole file", rel);
				ok = 0;
			} else {
				*len = need;
			}
		}
	}
	fclose(fp);
	return ok;
}

u8 *modpackArchiveBuild(const char *dirpath, u32 *out_len, char *err, size_t err_cap)
{
	modpack_path_list_t list = { NULL, 0, 0 };
	u8 *buf = NULL;
	u64 len = 6;
	u64 cap = 65536u;
	s32 ok;

	if (err && err_cap) {
		err[0] = '\0';
	}
	if (out_len) {
		*out_len = 0;
	}
	if (!dirpath || !dirpath[0] || !out_len) {
		archiveErr(err, err_cap, "no folder given", NULL);
		return NULL;
	}
	ok = collectFiles(&list, dirpath, "", err, err_cap);
	if (ok && list.count == 0) {
		archiveErr(err, err_cap, "the folder has no files", dirpath);
		ok = 0;
	}
	if (ok) {
		/* The order a directory lists its entries in is not the archive's
		 * order: the same files must always give the same bytes. */
		qsort(list.paths, list.count, sizeof(*list.paths), comparePathStrings);
		ok = checkMemberPaths(&list, err, err_cap);
	}
	if (ok) {
		buf = (u8 *)malloc((size_t)cap);
		if (!buf) {
			archiveErr(err, err_cap, "out of memory", NULL);
			ok = 0;
		}
	}
	if (ok) {
		putLe32(buf, PDCA_ARCHIVE_MAGIC);
		putLe16(buf + 4, (u16)list.count);
		for (u32 i = 0; ok && i < list.count; i++) {
			ok = appendFile(&buf, &len, &cap, dirpath, list.paths[i], err, err_cap);
		}
	}
	pathListFree(&list);
	if (!ok) {
		free(buf);
		return NULL;
	}
	*out_len = (u32)len;
	return buf;
}

s32 modpackArchiveNameIsSafe(const char *name)
{
	char normalized[FS_MAXPATH];
	char key[FS_MAXPATH];

	if (!name || !name[0] || name[0] == '.' || strchr(name, '/') || strchr(name, '\\')) {
		return 0;
	}
	return pdcaNormalizeMemberPath(normalized, sizeof(normalized), key, sizeof(key), name)
		&& strcmp(normalized, name) == 0;
}
