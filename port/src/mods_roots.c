/**
 * mods_roots.c -- where the game keeps mods. See mods_roots.h.
 */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#include <strings.h>
#endif
#include "mods_roots.h"

#define MODS_ROOTS_PATH_CAP 1024

/* Copy with forward slashes and without trailing slashes ("C:/" keeps its). */
static s32 rootsCopyClean(const char *in, char *out, size_t cap)
{
	size_t n = 0;

	if (!in || !out || cap == 0) return 0;
	for (size_t i = 0; in[i]; i++) {
		if (n + 1 >= cap) {
			out[0] = '\0';
			return 0;
		}
		out[n++] = in[i] == '\\' ? '/' : in[i];
	}
	out[n] = '\0';
	while (n > 1 && out[n - 1] == '/' && !(n == 3 && out[1] == ':')) {
		out[--n] = '\0';
	}
	return n > 0;
}

s32 modsRootsUserDir(s32 portable, const char *savedir_arg, const char *save_dir,
		const char *local_appdata, const char *home, char *out, size_t out_cap)
{
	char base[MODS_ROOTS_PATH_CAP];
	size_t len;

	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	base[0] = '\0';

	if (portable || (savedir_arg && savedir_arg[0])) {
		/* Isolated with the saves. */
		if (!save_dir || !rootsCopyClean(save_dir, base, sizeof(base))) return 0;
	} else if (local_appdata && local_appdata[0]) {
		char appdata[MODS_ROOTS_PATH_CAP];
		if (!rootsCopyClean(local_appdata, appdata, sizeof(appdata))) return 0;
		len = strlen(appdata);
		if (len + 1 + strlen(MODS_ROOTS_APP_FOLDER) + 1 > sizeof(base)) return 0;
		memcpy(base, appdata, len);
		if (base[len - 1] != '/') base[len++] = '/';
		memcpy(base + len, MODS_ROOTS_APP_FOLDER, strlen(MODS_ROOTS_APP_FOLDER) + 1);
	} else if (home && home[0]) {
		if (!rootsCopyClean(home, base, sizeof(base))) return 0;
	} else {
		return 0;
	}

	len = strlen(base);
	if (len + (base[len - 1] == '/' ? 0 : 1) + strlen("mods") + 1 > out_cap) return 0;
	memcpy(out, base, len);
	if (out[len - 1] != '/') out[len++] = '/';
	memcpy(out + len, "mods", 5);
	return 1;
}

static s32 rootsIsDir(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static s32 rootsMkdir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path) == 0 || errno == EEXIST;
#else
	return mkdir(path, 0755) == 0 || errno == EEXIST;
#endif
}

s32 modsRootsEnsureDir(const char *path)
{
	char buf[MODS_ROOTS_PATH_CAP];
	size_t start = 0;

	if (!path || !path[0] || !rootsCopyClean(path, buf, sizeof(buf))) return 0;
	if (rootsIsDir(buf)) return 1;

	/* Skip what cannot be created: a drive ("C:/"), a share ("//server/share/")
	 * or the root ("/"). */
	if (buf[0] && buf[1] == ':') {
		start = buf[2] == '/' ? 3 : 2;
	} else if (buf[0] == '/' && buf[1] == '/') {
		const char *share = strchr(buf + 2, '/');
		share = share ? strchr(share + 1, '/') : NULL;
		start = share ? (size_t)(share - buf) + 1 : strlen(buf);
	} else if (buf[0] == '/') {
		start = 1;
	}

	for (size_t i = start; ; i++) {
		if (buf[i] == '/' || buf[i] == '\0') {
			const char saved = buf[i];
			buf[i] = '\0';
			if (i > start && !rootsIsDir(buf) && !rootsMkdir(buf)) return 0;
			buf[i] = saved;
			if (saved == '\0') break;
		}
	}
	return rootsIsDir(buf);
}

/* Absolute, forward slashes, no "." segments, no trailing slash. */
static s32 rootsCanonical(const char *in, char *out, size_t cap)
{
	char abs[MODS_ROOTS_PATH_CAP];
	char clean[MODS_ROOTS_PATH_CAP];
	size_t n = 0;

	if (!in || !in[0]) return 0;
#ifdef _WIN32
	if (!_fullpath(abs, in, sizeof(abs))) return 0;
#else
	if (in[0] == '/') {
		if (strlen(in) + 1 > sizeof(abs)) return 0;
		memcpy(abs, in, strlen(in) + 1);
	} else {
		char *resolved = realpath(in, NULL);
		if (!resolved) return 0;
		if (strlen(resolved) + 1 > sizeof(abs)) {
			free(resolved);
			return 0;
		}
		memcpy(abs, resolved, strlen(resolved) + 1);
		free(resolved);
	}
#endif
	if (!rootsCopyClean(abs, clean, sizeof(clean))) return 0;

	/* Drop "/./" segments (_fullpath already folds "..", but not always "."). */
	for (size_t i = 0; clean[i]; i++) {
		if (clean[i] == '/' && clean[i + 1] == '.' &&
				(clean[i + 2] == '/' || clean[i + 2] == '\0')) {
			i++;
			continue;
		}
		if (n + 1 >= cap) return 0;
		out[n++] = clean[i];
	}
	out[n] = '\0';
	while (n > 1 && out[n - 1] == '/' && !(n == 3 && out[1] == ':')) out[--n] = '\0';
	return n > 0;
}

s32 modsRootsSameDir(const char *a, const char *b)
{
	char ca[MODS_ROOTS_PATH_CAP];
	char cb[MODS_ROOTS_PATH_CAP];

	if (!rootsCanonical(a, ca, sizeof(ca)) || !rootsCanonical(b, cb, sizeof(cb))) return 0;
#ifdef _WIN32
	return strcasecmp(ca, cb) == 0;
#else
	return strcmp(ca, cb) == 0;
#endif
}
