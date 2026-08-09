#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define pdca_mkdir(path) _mkdir(path)
#define pdca_rmdir(path) _rmdir(path)
#define pdca_sync_file(fp) _commit(_fileno(fp))
#else
#include <unistd.h>
#define pdca_mkdir(path) mkdir(path, 0777)
#define pdca_rmdir(path) rmdir(path)
#define pdca_sync_file(fp) fsync(fileno(fp))
#endif

#include "asset_path_contract.h"
#include "fs.h"
#include "pdca_extract_transaction.h"

static u16 readLe16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 readLe32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16)
		| ((u32)p[3] << 24);
}

static s32 pathIsDirectory(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static s32 createDirOne(const char *path)
{
	if (pdca_mkdir(path) == 0) return 1;
	return errno == EEXIST && pathIsDirectory(path);
}

static s32 createDirRecursive(const char *path)
{
	char current[FS_MAXPATH];
	if (!assetPathCopyChecked(current, sizeof(current), path)) return 0;
	size_t len = strlen(current);
	for (size_t i = 1; i < len; i++) {
		if (current[i] != '/' && current[i] != '\\') continue;
		if (i == 2 && current[1] == ':') continue;
		char saved = current[i];
		current[i] = '\0';
		if (current[0] && !createDirOne(current)) return 0;
		current[i] = saved;
	}
	return createDirOne(current);
}

static s32 removeTree(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) return errno == ENOENT;
	if (!S_ISDIR(st.st_mode)) return remove(path) == 0;
	DIR *dir = opendir(path);
	if (!dir) return 0;
	s32 ok = 1;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		char child[FS_MAXPATH];
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
		if (!assetPathJoinChecked(child, sizeof(child), path, "/",
				ent->d_name) || !removeTree(child)) ok = 0;
	}
	closedir(dir);
	if (ok && pdca_rmdir(path) != 0) ok = 0;
	return ok;
}

static u32 pathHash(const char *path)
{
	u32 hash = 2166136261u;
	for (; path && *path; path++) {
		hash ^= (u8)*path;
		hash *= 16777619u;
	}
	return hash;
}

static s32 siblingPath(char *out, size_t out_cap, const char *parent,
	const char *kind, u32 hash, u32 attempt)
{
	char leaf[48];
	int wrote = snprintf(leaf, sizeof(leaf), ".pd2-%s-%08x-%02x",
		kind, (unsigned)hash, (unsigned)attempt);
	return wrote >= 0 && (size_t)wrote < sizeof(leaf)
		&& assetPathJoinChecked(out, out_cap, parent, "/", leaf);
}

static s32 uniqueSibling(char *out, size_t out_cap, const char *parent,
	const char *kind, u32 hash)
{
	for (u32 attempt = 0; attempt < 256; attempt++) {
		if (!siblingPath(out, out_cap, parent, kind, hash, attempt)) return 0;
		struct stat st;
		if (stat(out, &st) != 0 && errno == ENOENT) return 1;
	}
	if (out && out_cap) out[0] = '\0';
	return 0;
}

/* Product target is Windows. Normalize separators and case for collision
 * checks, and reject names that the Windows filesystem aliases or interprets
 * as ADS/wildcards. This prevents two envelope rows from reaching one file. */
static s32 componentIsReservedDevice(const char *key, size_t start, size_t len)
{
	size_t stem_len = 0;
	while (stem_len < len && key[start + stem_len] != '.') stem_len++;
	if ((stem_len == 3 && (!strncmp(key + start, "con", 3)
				|| !strncmp(key + start, "prn", 3)
				|| !strncmp(key + start, "aux", 3)
				|| !strncmp(key + start, "nul", 3)))) return 1;
	return stem_len == 4
		&& ((!strncmp(key + start, "com", 3)
				|| !strncmp(key + start, "lpt", 3))
			&& key[start + 3] >= '1' && key[start + 3] <= '9');
}

static s32 normalizeMemberPath(char *out, size_t out_cap, char *key,
	size_t key_cap, const char *path)
{
	if (!out || out_cap == 0 || !key || key_cap == 0 || !path || !path[0]
			|| assetPathIsAbsolute(path) || assetPathHasParentTraversal(path)
			|| strstr(path, "::")) return 0;
	size_t used = 0;
	size_t component_start = 0;
	for (const unsigned char *p = (const unsigned char *)path; ; p++) {
		unsigned char ch = *p;
		if (ch == '\\') ch = '/';
		if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|'
				|| ch == '?' || ch == '*' || (ch && ch < 0x20)) return 0;
		if (ch == '/' || ch == '\0') {
			size_t component_len = used - component_start;
			if (component_len == 0
					|| (component_len == 1 && out[component_start] == '.')
					|| (component_len == 2 && out[component_start] == '.'
						&& out[component_start + 1] == '.')
					|| out[used - 1] == '.' || out[used - 1] == ' '
					|| componentIsReservedDevice(key, component_start,
						component_len)) return 0;
			if (ch == '\0') break;
			if (used + 1 >= out_cap) return 0;
			out[used] = '/';
			key[used++] = '/';
			component_start = used;
			continue;
		}
		if (used + 1 >= out_cap || used + 1 >= key_cap) return 0;
		out[used] = (char)ch;
		key[used++] = (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
	}
	out[used] = '\0';
	key[used] = '\0';
	return 1;
}

static s32 cleanupStaleStages(const char *parent, u32 hash)
{
	for (u32 attempt = 0; attempt < 256; attempt++) {
		char stage[FS_MAXPATH];
		struct stat st;
		if (!siblingPath(stage, sizeof(stage), parent, "recv", hash, attempt))
			return 0;
		if (stat(stage, &st) == 0 && !removeTree(stage)) return 0;
	}
	return 1;
}

static s32 recoverPriorBackup(const char *parent, const char *dest, u32 hash)
{
	for (u32 attempt = 0; attempt < 256; attempt++) {
		char backup[FS_MAXPATH];
		struct stat st;
		if (!siblingPath(backup, sizeof(backup), parent, "backup", hash,
				attempt)) return PDCA_EXTRACT_STAGE_FAILED;
		if (stat(backup, &st) != 0) continue;
		/* A destination beside a backup means publication may have completed.
		 * Preserve both and require explicit recovery instead of guessing which
		 * user-owned tree to delete. */
		if (pathIsDirectory(dest)) return PDCA_EXTRACT_RECOVERY_REQUIRED;
		if (rename(backup, dest) != 0) return PDCA_EXTRACT_RECOVERY_REQUIRED;
	}
	return PDCA_EXTRACT_OK;
}

static s32 archiveMemberAt(const u8 *data, const u8 *end, u16 wanted,
	const char **relpath_out, const u8 **bytes_out, u32 *bytes_len_out)
{
	const u8 *p = data + 6;
	for (u16 i = 0; i <= wanted; i++) {
		if (p + 2 > end) return 0;
		u16 path_len = readLe16(p);
		p += 2;
		if (path_len < 2 || p + path_len > end) return 0;
		const char *relpath = (const char *)p;
		p += path_len;
		if (relpath[path_len - 1] != '\0'
				|| strlen(relpath) + 1 != path_len || p + 4 > end) return 0;
		u32 bytes_len = readLe32(p);
		p += 4;
		if (p + bytes_len > end) return 0;
		if (i == wanted) {
			*relpath_out = relpath;
			*bytes_out = p;
			*bytes_len_out = bytes_len;
			return 1;
		}
		p += bytes_len;
	}
	return 0;
}

static s32 validateEnvelope(const u8 *data, u32 data_len, const char *stage,
	const char *dest, u16 *file_count_out)
{
	if (!data || data_len < 6 || readLe32(data) != PDCA_ARCHIVE_MAGIC)
		return PDCA_EXTRACT_INVALID;
	u16 file_count = readLe16(data + 4);
	if (file_count == 0) return PDCA_EXTRACT_INVALID;
	const u8 *end = data + data_len;
	const u8 *after_last = data + 6;
	for (u16 i = 0; i < file_count; i++) {
		const char *relpath;
		char safe_path[FS_MAXPATH];
		char collision_key[FS_MAXPATH];
		const u8 *bytes;
		u32 bytes_len;
		char final_path[FS_MAXPATH];
		char stage_path[FS_MAXPATH];
		if (!archiveMemberAt(data, end, i, &relpath, &bytes, &bytes_len))
			return PDCA_EXTRACT_INVALID;
		if (!normalizeMemberPath(safe_path, sizeof(safe_path), collision_key,
				sizeof(collision_key), relpath)
				|| !assetPathJoinChecked(final_path, sizeof(final_path), dest, "/",
					safe_path)
				|| !assetPathJoinChecked(stage_path, sizeof(stage_path), stage, "/",
					safe_path)) return PDCA_EXTRACT_PATH_REJECTED;
		for (u16 prior = 0; prior < i; prior++) {
			const char *prior_path;
			char prior_safe[FS_MAXPATH];
			char prior_key[FS_MAXPATH];
			const u8 *prior_bytes;
			u32 prior_bytes_len;
			if (!archiveMemberAt(data, end, prior, &prior_path, &prior_bytes,
					&prior_bytes_len)) return PDCA_EXTRACT_INVALID;
			if (!normalizeMemberPath(prior_safe, sizeof(prior_safe), prior_key,
					sizeof(prior_key), prior_path)) return PDCA_EXTRACT_PATH_REJECTED;
			if (!strcmp(prior_key, collision_key)) return PDCA_EXTRACT_INVALID;
		}
		after_last = bytes + bytes_len;
	}
	if (after_last != end) return PDCA_EXTRACT_INVALID;
	*file_count_out = file_count;
	return PDCA_EXTRACT_OK;
}

pdca_extract_result_t pdcaExtractArchiveBegin(const u8 *data, u32 data_len,
	const char *destdir, const pdca_extract_faults_t *faults,
	pdca_extract_transaction_t *transaction)
{
	char stage[FS_MAXPATH] = "";
	char backup[FS_MAXPATH] = "";
	char dest_parent[FS_MAXPATH] = "";
	u16 file_count = 0;
	s32 result = PDCA_EXTRACT_STAGE_FAILED;
	s32 moved_destination = 0;
	u32 hash;

	if (transaction) memset(transaction, 0, sizeof(*transaction));
	if (!transaction) return PDCA_EXTRACT_STAGE_FAILED;

	if (!destdir || !destdir[0]
			|| !assetPathCopyChecked(dest_parent, sizeof(dest_parent), destdir))
		return PDCA_EXTRACT_STAGE_FAILED;
	char *slash = strrchr(dest_parent, '/');
	char *backslash = strrchr(dest_parent, '\\');
	if (!slash || (backslash && backslash > slash)) slash = backslash;
	if (!slash) {
		if (!assetPathCopyChecked(dest_parent, sizeof(dest_parent), "."))
			return PDCA_EXTRACT_STAGE_FAILED;
	} else {
		*slash = '\0';
	}
	if (!createDirRecursive(dest_parent)) return PDCA_EXTRACT_STAGE_FAILED;
	hash = pathHash(destdir);
	if (!cleanupStaleStages(dest_parent, hash)) return PDCA_EXTRACT_RECOVERY_REQUIRED;
	result = recoverPriorBackup(dest_parent, destdir, hash);
	if (result != PDCA_EXTRACT_OK) return (pdca_extract_result_t)result;
	if (!uniqueSibling(stage, sizeof(stage), dest_parent, "recv", hash)
			|| !createDirRecursive(stage)) return PDCA_EXTRACT_STAGE_FAILED;

	result = validateEnvelope(data, data_len, stage, destdir, &file_count);
	if (result != PDCA_EXTRACT_OK) goto rollback;

	for (u16 i = 0; i < file_count; i++) {
		const char *relpath;
		char safe_path[FS_MAXPATH];
		char collision_key[FS_MAXPATH];
		const u8 *bytes;
		u32 bytes_len;
		char output[FS_MAXPATH];
		char parent[FS_MAXPATH];
		FILE *fp;
		if (!archiveMemberAt(data, data + data_len, i, &relpath, &bytes,
				&bytes_len)
				|| !normalizeMemberPath(safe_path, sizeof(safe_path), collision_key,
					sizeof(collision_key), relpath)
				|| !assetPathJoinChecked(output, sizeof(output), stage, "/",
					safe_path)
				|| !assetPathCopyChecked(parent, sizeof(parent), output)) {
			result = PDCA_EXTRACT_INVALID;
			goto rollback;
		}
		char *slash = strrchr(parent, '/');
		char *backslash = strrchr(parent, '\\');
		if (!slash || (backslash && backslash > slash)) slash = backslash;
		if (slash) {
			*slash = '\0';
			if (!createDirRecursive(parent)) {
				result = PDCA_EXTRACT_STAGE_FAILED;
				goto rollback;
			}
		}
		if (faults && faults->fail_open_index == (s32)i) {
			result = PDCA_EXTRACT_OPEN_FAILED;
			goto rollback;
		}
		fp = fopen(output, "wb");
		if (!fp) {
			result = PDCA_EXTRACT_OPEN_FAILED;
			goto rollback;
		}
		if (faults && faults->fail_write_index == (s32)i) {
			fclose(fp);
			result = PDCA_EXTRACT_WRITE_FAILED;
			goto rollback;
		}
		if (fwrite(bytes, 1, bytes_len, fp) != bytes_len || fflush(fp) != 0
				|| pdca_sync_file(fp) != 0) {
			fclose(fp);
			result = PDCA_EXTRACT_WRITE_FAILED;
			goto rollback;
		}
		if (fclose(fp) != 0) {
			result = PDCA_EXTRACT_WRITE_FAILED;
			goto rollback;
		}
	}

	if (pathIsDirectory(destdir)) {
		if (!uniqueSibling(backup, sizeof(backup), dest_parent, "backup", hash)
				|| rename(destdir, backup) != 0) {
			result = PDCA_EXTRACT_PUBLISH_FAILED;
			goto rollback;
		}
		moved_destination = 1;
	}
	if ((faults && faults->fail_publish) || rename(stage, destdir) != 0) {
		result = PDCA_EXTRACT_PUBLISH_FAILED;
		goto rollback;
	}
	stage[0] = '\0';
	if (!assetPathCopyChecked(transaction->destdir,
			sizeof(transaction->destdir), destdir)
			|| (moved_destination && !assetPathCopyChecked(transaction->backup,
				sizeof(transaction->backup), backup))) {
		result = PDCA_EXTRACT_STAGE_FAILED;
		goto rollback;
	}
	transaction->active = 1;
	return PDCA_EXTRACT_OK;

rollback:
	if (moved_destination && !pathIsDirectory(destdir)
			&& rename(backup, destdir) != 0) {
		if (stage[0]) removeTree(stage);
		return PDCA_EXTRACT_RECOVERY_REQUIRED;
	}
	if (stage[0]) removeTree(stage);
	return (pdca_extract_result_t)result;
}

pdca_extract_result_t pdcaExtractTransactionCommit(
	pdca_extract_transaction_t *transaction)
{
	if (!transaction || !transaction->active) return PDCA_EXTRACT_INVALID;
	transaction->active = 0;
	if (transaction->backup[0] && !removeTree(transaction->backup)) {
		return PDCA_EXTRACT_OK_BACKUP_RETAINED;
	}
	transaction->backup[0] = '\0';
	return PDCA_EXTRACT_OK;
}

s32 pdcaExtractTransactionRollback(pdca_extract_transaction_t *transaction)
{
	if (!transaction || !transaction->active) return 0;
	if (!removeTree(transaction->destdir)) return 0;
	if (transaction->backup[0]
			&& rename(transaction->backup, transaction->destdir) != 0) return 0;
	transaction->active = 0;
	transaction->backup[0] = '\0';
	return 1;
}

pdca_extract_result_t pdcaExtractArchiveTransactional(const u8 *data,
	u32 data_len, const char *destdir, const pdca_extract_faults_t *faults)
{
	pdca_extract_transaction_t transaction;
	pdca_extract_result_t result = pdcaExtractArchiveBegin(data, data_len,
		destdir, faults, &transaction);
	if (result != PDCA_EXTRACT_OK) return result;
	return pdcaExtractTransactionCommit(&transaction);
}
