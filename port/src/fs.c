#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <PR/ultratypes.h>
#include "constants.h"
#include "config.h"
#include "system.h"
#include "platform.h"
#include "utils.h"
#include "fs.h"
#include "modmgr.h"
#include "versioninfo.h"  /* VERSION_ROMID for fsDataDir / fsDataPathFor */
#include "modarchive.h"
#ifndef PD_SERVER
#include "modvfs.h"  /* Priority M / B-238: VFS-backed .pdmod mounts */
#endif
#include "assetcatalog_resolve.h"
#ifdef PLATFORM_WIN32
#include <direct.h>
#endif

/* B-321 (2026-05-03): base dir is the install root, not a nested data/ subdir.
 *
 * Pre-fix: base dir defaulted to "$E/data" so the runtime resolved everything
 * relative to <install_root>/data/. The pd.ini, ROM, and put_your_rom_here.txt
 * lived inside that base dir, and fsDataDir() returned "data/<romid>", which
 * meant extracted assets ended up at <install_root>/data/data/<romid>/...
 * (one level too deep). Mike's directive: "the mods and data folders may be
 * generated in the wrong location, they should be in the install root beside
 * the executable."
 *
 * Post-fix: DEFAULT_BASEDIR_NAME="." so base dir = $E (the EXE directory aka
 * install root). Now pd.ini and the ROM live next to PerfectDark.exe, and
 * fsDataDir() resolves to <install_root>/data/<romid>/ which is the natural
 * location for extracted assets. mods/ likewise lives at install root. */
#define DEFAULT_BASEDIR_NAME "."

static char baseDir[FS_MAXPATH + 1]; // replaces $B
static char modDir[FS_MAXPATH + 1];  // replaces $M
static char saveDir[FS_MAXPATH + 1]; // replaces $S
static char homeDir[FS_MAXPATH + 1]; // replaces $H
static char exeDir[FS_MAXPATH + 1];  // replaces $E

// Per-mod directories removed; modmgr now handles dynamic mod resolution
// via modmgrResolvePath() which iterates enabled mods in load order.


static s32 fsPathIsWritable(const char *path)
{
#ifdef PLATFORM_WIN32
	// on windows access() on directories will only check if the directory exists, so
	char tmp[FS_MAXPATH + 1] = { 0 };
	snprintf(tmp, sizeof(tmp), "%s/.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (f) {
		fclose(f);
		remove(tmp);
		return 1;
	}
	return 0;
#else
	return (access(path, W_OK) == 0);
#endif
}

s32 fsPathIsAbsolute(const char *path)
{
 return (path[0] == '/' || (isalpha(path[0]) && path[1] == ':'));
}

s32 fsPathIsCwdRelative(const char *path)
{
	// ., .., ./, ../
	return (path[0] == '.' && (path[1] == '.' || path[1] == '/' || path[1] == '\\' || path[1] == '\0'));
}

static const char *fsNestedArchiveSeparator(const char *name)
{
	return name ? strstr(name, "::") : NULL;
}

static void fsCopyArchiveEntryName(const char *entry, char *out, size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = '\0';
	if (!entry) {
		return;
	}
	while (*entry == '/' || *entry == '\\') {
		entry++;
	}
	size_t i = 0;
	while (entry[i] && i + 1 < outSize) {
		out[i] = (entry[i] == '\\') ? '/' : entry[i];
		i++;
	}
	out[i] = '\0';
}

static void *fsTerminateArchiveBytes(void *raw, u32 entrySize, u32 *outSize)
{
	void *out;

	if (!raw) {
		return NULL;
	}

	out = realloc(raw, (size_t)entrySize + 1);
	if (!out) {
		free(raw);
		return NULL;
	}

	((u8 *)out)[entrySize] = 0;
	if (outSize) {
		*outSize = entrySize;
	}
	return out;
}

static void *fsExtractNestedArchiveChain(void *archiveBytes, u32 archiveSize,
		const char *entryChain, u32 *outSize)
{
	char entryName[FS_MAXPATH + 1];
	char nestedName[FS_MAXPATH + 1];
	const char *sep;
	u32 entrySize = 0;
	void *raw;

	if (!archiveBytes || archiveSize == 0 || !entryChain || !entryChain[0]) {
		return NULL;
	}

	sep = fsNestedArchiveSeparator(entryChain);
	if (sep) {
		size_t entryLen = (size_t)(sep - entryChain);
		if (entryLen == 0 || entryLen >= sizeof(entryName)) {
			return NULL;
		}
		memcpy(entryName, entryChain, entryLen);
		entryName[entryLen] = '\0';
		fsCopyArchiveEntryName(entryName, nestedName, sizeof(nestedName));
		raw = modArchiveExtractMemAlloc(archiveBytes, archiveSize,
			nestedName, &entrySize);
		if (!raw) {
			return NULL;
		}
		void *out = fsExtractNestedArchiveChain(raw, entrySize, sep + 2, outSize);
		free(raw);
		return out;
	}

	fsCopyArchiveEntryName(entryChain, entryName, sizeof(entryName));
	if (!entryName[0]) {
		return NULL;
	}

	raw = modArchiveExtractMemAlloc(archiveBytes, archiveSize, entryName, &entrySize);
	return fsTerminateArchiveBytes(raw, entrySize, outSize);
}

static void *fsLoadNestedArchiveEntry(const char *name, u32 *outSize)
{
	const char *sep = fsNestedArchiveSeparator(name);
	if (!sep || sep == name) {
		return NULL;
	}

	char archiveName[FS_MAXPATH + 1];
	size_t archiveLen = (size_t)(sep - name);
	if (archiveLen == 0 || archiveLen >= sizeof(archiveName)) {
		return NULL;
	}
	memcpy(archiveName, name, archiveLen);
	archiveName[archiveLen] = '\0';

	char entryName[FS_MAXPATH + 1];
	fsCopyArchiveEntryName(sep + 2, entryName, sizeof(entryName));
	if (!entryName[0]) {
		return NULL;
	}

#ifndef PD_SERVER
	{
		u32 archiveSize = 0;
		void *archiveBytes = modVfsResolveAnyAlloc(archiveName, &archiveSize, NULL, 0);
		if (archiveBytes) {
			void *raw = fsExtractNestedArchiveChain(archiveBytes, archiveSize,
				entryName, outSize);
			free(archiveBytes);
			return raw;
		}
	}
#endif

	char archiveFullBuf[FS_MAXPATH + 1];
	const char *archiveFull = fsFullPath(archiveName, archiveFullBuf,
		sizeof(archiveFullBuf));
	mod_archive_t *arc = modArchiveOpen(archiveFull);
	if (!arc) {
		return NULL;
	}

	char topEntry[FS_MAXPATH + 1];
	const char *nestedSep = fsNestedArchiveSeparator(entryName);
	if (nestedSep) {
		size_t topLen = (size_t)(nestedSep - entryName);
		if (topLen == 0 || topLen >= sizeof(topEntry)) {
			modArchiveClose(arc);
			return NULL;
		}
		memcpy(topEntry, entryName, topLen);
		topEntry[topLen] = '\0';
	} else {
		snprintf(topEntry, sizeof(topEntry), "%s", entryName);
	}

	s32 idx = modArchiveFindEntry(arc, topEntry);
	if (idx < 0) {
		modArchiveClose(arc);
		return NULL;
	}

	u32 entrySize = 0;
	void *raw = modArchiveExtractAlloc(arc, idx, &entrySize);
	modArchiveClose(arc);
	if (raw && nestedSep) {
		void *out = fsExtractNestedArchiveChain(raw, entrySize,
			nestedSep + 2, outSize);
		free(raw);
		return out;
	}
	return fsTerminateArchiveBytes(raw, entrySize, outSize);
}

const char *fsFullPath(const char *relPath, char *out, size_t outSize)
{
	if (out == NULL || outSize == 0) {
		/* Caller bug. Return the input pointer to keep things sane in
		 * the rare case a logger calls us with a tiny / NULL buffer. */
		return relPath ? relPath : "";
	}
	out[0] = '\0';

	if (relPath == NULL) {
		return out;
	}

	if (relPath[0] == '$') {
		// expandable placeholder $X; will be replaced with the corresponding path, if any
		const char *expStr = NULL;
		switch (relPath[1]) {
			case 'E': expStr = exeDir; break;
			case 'H': expStr = homeDir; break;
			case 'M': expStr = modDir; break;
			case 'B': expStr = baseDir; break;
			case 'S': expStr = saveDir; break;
			default: break;
		}
		if (expStr && expStr[0]) {
			snprintf(out, outSize, "%s%s", expStr, relPath + 2);
			return out;
		}
		// couldn't expand anything, copy as-is
		snprintf(out, outSize, "%s", relPath);
		return out;
	} else if (!baseDir[0] || fsPathIsAbsolute(relPath) || fsPathIsCwdRelative(relPath)) {
		// user explicitly wants working directory or this is an absolute path or we have no baseDir set up yet
		snprintf(out, outSize, "%s", relPath);
		return out;
	}

	// path relative to mod or base dir; this will be a read request, so check where the file actually is

	// D3R-5: Check catalog component first (standalone, priority over legacy)
	const char *catResolved = assetCatalogResolvePath(relPath);
	if (catResolved) {
		snprintf(out, outSize, "%s", catResolved);
		if (strstr(relPath, "bgdata/")) {
			sysLogPrintf(LOG_VERBOSE, "FSPATH: \"%s\" -> CATALOG -> \"%s\"", relPath, out);
		}
		return out;
	}

	// Try modmgr registry (iterates all enabled mods in load order)
	const char *modResolved = modmgrResolvePath(relPath);
	if (modResolved) {
		snprintf(out, outSize, "%s", modResolved);
		if (strstr(relPath, "bgdata/")) {
			sysLogPrintf(LOG_VERBOSE, "FSPATH: \"%s\" -> MODMGR -> \"%s\"", relPath, out);
		}
		return out;
	}

	// Fall back to legacy modDir (--moddir flag)
	if (modDir[0]) {
		snprintf(out, outSize, "%s/%s", modDir, relPath);
		if (fsFileSize(out) >= 0) {
			if (strstr(relPath, "bgdata/")) {
				sysLogPrintf(LOG_VERBOSE, "FSPATH: \"%s\" -> MODDIR -> \"%s\"", relPath, out);
			}
			return out;
		}
	}

	// fall back to basedir
	snprintf(out, outSize, "%s/%s", baseDir, relPath);
	if (strstr(relPath, "bgdata/")) {
		sysLogPrintf(LOG_VERBOSE, "FSPATH: \"%s\" -> BASEDIR -> \"%s\"", relPath, out);
	}
	return out;
}

s32 fsInit(void)
{
	sysGetExecutablePath(exeDir, FS_MAXPATH);

	// if this is set, default to exe path for everything
	const s32 portable = sysArgCheck("--portable");
	if (portable) {
		strncpy(homeDir, exeDir, FS_MAXPATH);
		homeDir[FS_MAXPATH] = '\0';
	} else {
		sysGetHomePath(homeDir, FS_MAXPATH);
	}

	// get path to base dir and expand it if needed
	// Priority: --basedir arg > exe directory > working directory > home directory
	const char *path = sysArgGetString("--basedir");
	if (!path) {
		path = "$E/" DEFAULT_BASEDIR_NAME;
		if (!portable) {
			if (fsFileSize("$E/" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "$E/" DEFAULT_BASEDIR_NAME;
			} else if (fsFileSize("./" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "./" DEFAULT_BASEDIR_NAME;
			} else if (fsFileSize("$H/" DEFAULT_BASEDIR_NAME) >= 0) {
				path = "$H/" DEFAULT_BASEDIR_NAME;
			}
		}
	}
	fsFullPath(path, baseDir, sizeof(baseDir));

	/* B-321 (2026-05-03): with DEFAULT_BASEDIR_NAME = ".", the resolved
	 * path ends in "/." or "\." which is functionally fine but cosmetically
	 * confusing in log lines and downstream paths.  Strip the trailing
	 * dot-slash so logs read "base dir: C:/install" instead of
	 * "base dir: C:/install/." */
	{
		size_t bdlen = strlen(baseDir);
		while (bdlen >= 2
		    && baseDir[bdlen - 1] == '.'
		    && (baseDir[bdlen - 2] == '/' || baseDir[bdlen - 2] == '\\')) {
			baseDir[bdlen - 2] = '\0';
			bdlen -= 2;
		}
	}

	// get path to mod dir and expand it if needed
	// mod directory is overlaid on top of base directory (legacy --moddir only)
	// Mod discovery is now handled by modmgrInit() scanning mods/
	path = sysArgGetString("--moddir");
	if (path) {
		if (fsPathIsAbsolute(path) || fsPathIsCwdRelative(path) || path[0] == '$') {
			// path is explicit; check as-is
			if (fsFileSize(path) >= 0) {
				fsFullPath(path, modDir, sizeof(modDir));
			}
		} else {
			// path is relative to workdir; try to find it
			const char *priority[] = { ".", "$E", "$H" };
			for (s32 i = 0; i < 2 + (portable != 0); ++i) {
				char *tmp = strFmt("%s/%s", priority[i], path);
				if (fsFileSize(tmp) >= 0) {
					fsFullPath(tmp, modDir, sizeof(modDir));
					break;
				}
			}
		}
		if (!modDir[0]) {
			sysLogPrintf(LOG_WARNING, "could not find specified moddir `%s`", path);
		}
	}

	// Mod directories are discovered dynamically by modmgrInit() scanning mods/.

	// get path to save dir and expand it if needed
	path = sysArgGetString("--savedir");
	if (!path) {
		if (portable) {
			path = "$E";
		} else {
#if defined(PLATFORM_LINUX) || defined(PLATFORM_OSX)
			// check if there's a config in the working directory, otherwise default to homeDir
			if (fsFileSize("./" CONFIG_FNAME) >= 0) {
				path = ".";
			} else {
				path = "$H";
			}
#else
			/*
			 * PC port: Always use AppData for saves so they persist
			 * across game updates and reinstalls. SDL_GetPrefPath
			 * gives us AppData/Roaming/perfectdark on Windows.
			 * Use --savedir or --portable to override.
			 */
			path = "$H";
#endif
		}
	}

	fsFullPath(path, saveDir, sizeof(saveDir));

#ifdef PLATFORM_WIN32
	/*
	 * Migration: if saves exist in the old location (exe dir or working dir)
	 * but not in the new AppData location, copy them over automatically.
	 * This ensures existing players don't lose their saves after updating.
	 */
	if (!portable) {
		static const char *migrateFiles[] = { "eeprom.bin", CONFIG_FNAME, NULL };
		const char *oldDirs[] = { exeDir, "." };

		for (s32 f = 0; migrateFiles[f]; f++) {
			char newPath[FS_MAXPATH + 1];
			snprintf(newPath, FS_MAXPATH, "%s/%s", saveDir, migrateFiles[f]);

			if (fsFileSize(newPath) >= 0) {
				continue; /* already exists in AppData, don't overwrite */
			}

			for (s32 d = 0; d < 2; d++) {
				char oldPath[FS_MAXPATH + 1];
				snprintf(oldPath, FS_MAXPATH, "%s/%s", oldDirs[d], migrateFiles[f]);

				s32 oldSize = fsFileSize(oldPath);
				if (oldSize > 0) {
					FILE *src = fopen(oldPath, "rb");
					if (src) {
						fsCreateDir(saveDir);
						FILE *dst = fopen(newPath, "wb");
						if (dst) {
							u8 buf[4096];
							size_t n;
							while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
								if (fwrite(buf, 1, n, dst) != n) {
									sysLogPrintf(LOG_WARNING, "fs: short write during migration copy");
									break;
								}
							}
							fclose(dst);
							sysLogPrintf(LOG_NOTE, "migrated %s -> %s", oldPath, newPath);
						}
						fclose(src);
					}
					break; /* found in this old dir, don't check the other */
				}
			}
		}
	}
#endif

	if (modDir[0]) {
		sysLogPrintf(LOG_NOTE, " mod dir: %s", modDir);
	}
	// Per-mod directories now logged by modmgrInit()
	sysLogPrintf(LOG_NOTE, "base dir: %s", baseDir);
	sysLogPrintf(LOG_NOTE, "save dir: %s", saveDir);

	return 0;
}

const char *fsGetBaseDir(void)
{
	return baseDir[0] ? baseDir : NULL;
}

const char *fsGetModDir(void)
{
	// Check modmgr first; return first enabled mod's directory
	for (s32 i = 0; i < modmgrGetCount(); i++) {
		modinfo_t *mod = modmgrGetMod(i);
		if (mod && mod->enabled && mod->dirpath[0]) {
			return mod->dirpath;
		}
	}

	// Fall back to legacy modDir
	return modDir[0] ? modDir : NULL;
}

s32 fsFileLoadTo(const char *name, void *dst, u32 dstSize)
{
	{
		u32 nestedSize = 0;
		void *nested = fsLoadNestedArchiveEntry(name, &nestedSize);
		if (nested) {
			if (nestedSize > dstSize) {
				sysLogPrintf(LOG_ERROR, "fsFileLoadTo: archive entry too big for buffer (%u > %u): %s",
					nestedSize, dstSize, name);
				free(nested);
				return -1;
			}
			memcpy(dst, nested, nestedSize);
			free(nested);
			return (s32)nestedSize;
		}
	}

#ifndef PD_SERVER
	/* Priority M / B-238: archive-backed mount lookup. The VFS owns its
	 * own decompression pass so we can copy out without going through the
	 * fsFullPath -> fopen -> fread pipeline. */
	{
		u32 vfsSize = 0;
		void *vbuf = modVfsResolveAnyAlloc(name, &vfsSize, NULL, 0);
		if (vbuf) {
			if (vfsSize > dstSize) {
				sysLogPrintf(LOG_ERROR, "fsFileLoadTo: vfs entry too big for buffer (%u > %u): %s",
					vfsSize, dstSize, name);
				free(vbuf);
				return -1;
			}
			memcpy(dst, vbuf, vfsSize);
			free(vbuf);
			return (s32)vfsSize;
		}
	}
#endif
	char fullBuf[FS_MAXPATH + 1];
	const char *fullName = fsFullPath(name, fullBuf, sizeof(fullBuf));

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		return -1;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return -1;
	}

	if ((u32)size > dstSize) {
		sysLogPrintf(LOG_ERROR, "fsFileLoadTo: file too big for buffer (%u > %u): %s", size, dstSize, fullName);
		fclose(f);
		return -1;
	}

	if (fread(dst, 1, size, f) != (size_t)size) {
		sysLogPrintf(LOG_WARNING, "fsFileLoadTo: short read: %s", fullName);
		fclose(f);
		return -1;
	}
	fclose(f);

	return size;
}

void *fsFileLoad(const char *name, u32 *outSize)
{
	{
		void *nested = fsLoadNestedArchiveEntry(name, outSize);
		if (nested) {
			return nested;
		}
	}

#ifndef PD_SERVER
	/* Priority M / B-238: prefer the in-memory VFS for any path satisfied
	 * by a mounted .pdmod / .zip archive. The buffer is sysMemZeroAlloc-
	 * compatible (heap-malloc with a trailing NUL), matching the existing
	 * caller contract: "free() me when you are done." */
	{
		u32 vfsSize = 0;
		void *vbuf = modVfsResolveAnyAlloc(name, &vfsSize, NULL, 0);
		if (vbuf) {
			if (outSize) *outSize = vfsSize;
			return vbuf;
		}
	}
#endif
	char fullBuf[FS_MAXPATH + 1];
	const char *fullName = fsFullPath(name, fullBuf, sizeof(fullBuf));

	FILE *f = fopen(fullName, "rb");
	if (!f) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: could not find file: %s", fullName);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	const s32 size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		sysLogPrintf(LOG_ERROR, "fsFileLoad: empty file or invalid size (%d): %s", size, fullName);
		fclose(f);
		return NULL;
	}

	void *buf = NULL;
	if (size) {
		buf = sysMemZeroAlloc(size + 1); // sick hack for a free null terminator
		if (!buf) {
			sysLogPrintf(LOG_ERROR, "fsFileLoad: could not alloc %d bytes for file: %s", size, fullName);
			fclose(f);
			return NULL;
		}
		if (fread(buf, 1, size, f) != (size_t)size) {
			sysLogPrintf(LOG_WARNING, "fsFileLoad: short read: %s", fullName);
			free(buf);
			fclose(f);
			if (outSize) *outSize = 0;
			return NULL;
		}
	}

	fclose(f);

	if (outSize) {
		*outSize = size;
	}

	return buf;
}

s32 fsFileSize(const char *name)
{
	{
		u32 nestedSize = 0;
		void *nested = fsLoadNestedArchiveEntry(name, &nestedSize);
		if (nested) {
			free(nested);
			return (s32)nestedSize;
		}
	}

#ifndef PD_SERVER
	/* Priority M / B-238: VFS short-circuit. modVfsGetSize is cheap --
	 * a central-directory lookup, no decompression. */
	s32 vfsSize = modVfsGetSize(name);
	if (vfsSize >= 0) {
		return vfsSize;
	}
#endif
	char fullBuf[FS_MAXPATH + 1];
	const char *fullName = fsFullPath(name, fullBuf, sizeof(fullBuf));
	struct stat st;
	if (stat(fullName, &st) < 0) {
		return -1;
	} else {
		return st.st_size;
	}
}

FILE *fsFileOpenWrite(const char *name)
{
	char fullBuf[FS_MAXPATH + 1];
	return fopen(fsFullPath(name, fullBuf, sizeof(fullBuf)), "wb");
}

FILE *fsFileOpenRead(const char *name)
{
	char fullBuf[FS_MAXPATH + 1];
	return fopen(fsFullPath(name, fullBuf, sizeof(fullBuf)), "rb");
}

void fsFileFree(FILE *f)
{
	fclose(f);
}

s32 fsCreateDir(const char *path)
{
	/* B-319 (2026-05-03): consistent success semantics. Returns 1 when the
	 * directory exists at function return -- whether newly created here, or
	 * already present (EEXIST is a successful idempotent path) -- and 0 only
	 * when the directory could not be created and does not already exist.
	 *
	 * Pre-fix: returned the raw mkdir/_mkdir int (0=success, -1=failure),
	 * so callers using `if (!fsCreateDir(x))` treated SUCCESS as failure
	 * and emitted spurious LOUDFAIL warnings on every directory the game
	 * legitimately created.
	 *
	 * Phase 1 (2026-05-03): fsFullPath migrated to caller-owned buffer
	 * contract. Use a stack buffer here so concurrent callers do not
	 * trash each other's path. */
	char fullBuf[FS_MAXPATH + 1];
	const char *full = fsFullPath(path, fullBuf, sizeof(fullBuf));
#ifdef PLATFORM_WIN32
	if (_mkdir(full) == 0) {
		return 1;
	}
#else
	if (mkdir(full, 0777) == 0) {
		return 1;
	}
#endif
	if (errno == EEXIST) {
		/* Already exists -- success-in-spirit per the docblock. Confirm
		 * it is a directory (not a regular file with the same name). */
		struct stat st;
		if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
			return 1;
		}
	}
	return 0;
}

/* Phase 3 Pass A.1 (2026-05-02): data/<romid>/ tier accessors.
 * Phase 1 of startup-acceleration (2026-05-03): caller-owned buffers. */

const char *fsDataDir(char *out, size_t outSize)
{
	if (out == NULL || outSize == 0) {
		return "";
	}
	snprintf(out, outSize, "data/%s", VERSION_ROMID);
	return out;
}

const char *fsDataPathFor(const char *rel, char *out, size_t outSize)
{
	if (out == NULL || outSize == 0) {
		return "";
	}
	if (rel == NULL || rel[0] == '\0') {
		return fsDataDir(out, outSize);
	}
	/* Strip leading slash so callers can pass either "files/foo" or
	 * "/files/foo" and get a consistent result. */
	while (rel[0] == '/' || rel[0] == '\\') {
		rel++;
	}
	snprintf(out, outSize, "data/%s/%s", VERSION_ROMID, rel);
	return out;
}

s32 fsDataDirEnsure(void)
{
	/* Two-step create: parent "data" first, then "data/<romid>" so the
	 * second mkdir succeeds even on platforms where mkdir does not
	 * create intermediate directories.  Both steps are idempotent --
	 * an existing directory returns success in spirit (errno EEXIST is
	 * acceptable; we treat the final-path existence as the outcome). */
	char romidBuf[FS_MAXPATH + 1];
	const char *parentRel = "data";
	const char *romidRel = fsDataDir(romidBuf, sizeof(romidBuf));
	s32 r1 = fsCreateDir(parentRel);
	s32 r2 = fsCreateDir(romidRel);
	(void)r1;
	(void)r2;
	/* Confirm via stat on the resolved path. */
	char fullBuf[FS_MAXPATH + 1];
	const char *full = fsFullPath(romidRel, fullBuf, sizeof(fullBuf));
	struct stat st;
	if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
		return 1;
	}
	sysLoudFailf("DATA",
		"failed to create or stat data dir \"%s\" (errno will appear in next syscall)",
		full ? full : romidRel);
	return 0;
}
