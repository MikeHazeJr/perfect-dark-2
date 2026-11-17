/**
 * mods_install_roots.c -- the install's own mods folders (B-1169).
 * See mods_install_roots.h for the rule.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "mods_install_roots.h"

#define MODS_INSTALL_PATH_MAX 1100

static s32 s_isSeparator(char c)
{
	return c == '/' || c == '\\';
}

/* Length of `path` without trailing separators (a bare root keeps its own). */
static size_t s_trimmedLength(const char *path)
{
	size_t n = strlen(path);
	while (n > 1 && s_isSeparator(path[n - 1])) n--;
	return n;
}

/* `dir` + "/" + `leaf` with forward slashes. 1 when it fits. */
static s32 s_join(char *out, size_t cap, const char *dir, size_t dir_len, const char *leaf)
{
	if (!out || cap == 0) return 0;
	out[0] = '\0';
	int written = snprintf(out, cap, "%.*s/%s", (int)dir_len, dir, leaf);
	if (written <= 0 || (size_t)written >= cap) {
		out[0] = '\0';
		return 0;
	}
	for (char *p = out; *p; ++p) {
		if (*p == '\\') *p = '/';
	}
	return 1;
}

/* Length of exe_dir's parent folder, or 0 when it has none. */
static size_t s_parentLength(const char *exe_dir)
{
	size_t n = s_trimmedLength(exe_dir);
	while (n > 0 && !s_isSeparator(exe_dir[n - 1])) n--;
	while (n > 1 && s_isSeparator(exe_dir[n - 1])) n--;
	return n;
}

static s32 s_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

/* Same folder, ignoring separators, trailing slashes and (Windows) case. */
static s32 s_sameFolder(const char *a, const char *b)
{
	size_t na = s_trimmedLength(a), nb = s_trimmedLength(b);
	if (na != nb) return 0;
	for (size_t i = 0; i < na; i++) {
		char ca = s_isSeparator(a[i]) ? '/' : a[i];
		char cb = s_isSeparator(b[i]) ? '/' : b[i];
#if defined(_WIN32)
		ca = (char)tolower((unsigned char)ca);
		cb = (char)tolower((unsigned char)cb);
#endif
		if (ca != cb) return 0;
	}
	return 1;
}

s32 modsInstallIsSourceCheckout(const char *exe_dir)
{
	char probe[MODS_INSTALL_PATH_MAX];
	size_t parent;

	if (!exe_dir || !exe_dir[0]) return 0;
	parent = s_parentLength(exe_dir);
	if (parent == 0) return 0;
	return s_join(probe, sizeof(probe), exe_dir, parent, "CMakeLists.txt") && s_exists(probe)
		&& s_join(probe, sizeof(probe), exe_dir, parent, ".git") && s_exists(probe);
}

s32 modsInstallRoots(const char *exe_dir, const char *base_dir,
		char *const *out, size_t cap, s32 out_count)
{
	char base_mods[MODS_INSTALL_PATH_MAX];
	s32 count = 0;

	if (!base_dir || !base_dir[0] || !out || out_count <= 0) return 0;
	if (!s_join(base_mods, sizeof(base_mods), base_dir, s_trimmedLength(base_dir), "mods")) return 0;

	if (out_count >= 2 && modsInstallIsSourceCheckout(exe_dir)) {
		char repo_mods[MODS_INSTALL_PATH_MAX];
		if (s_join(repo_mods, sizeof(repo_mods), exe_dir, s_parentLength(exe_dir), "mods")
				&& !s_sameFolder(repo_mods, base_mods)) {
			if (strlen(repo_mods) >= cap) return 0;
			strcpy(out[count++], repo_mods);
		}
	}
	if (strlen(base_mods) >= cap) return 0;
	strcpy(out[count++], base_mods);
	return count;
}
