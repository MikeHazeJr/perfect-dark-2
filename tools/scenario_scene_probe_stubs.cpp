#include "fs.h"
#include "modarchive.h"
#include "system.h"

#include <PR/ultratypes.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_argc = 0;
static const char **g_argv = nullptr;

static const char *findNestedSep(const char *path)
{
	return path ? std::strstr(path, "::") : nullptr;
}

static char *copySegment(const char *begin, const char *end)
{
	if (!begin || !end || end < begin) {
		return nullptr;
	}
	size_t len = (size_t)(end - begin);
	char *out = (char *)std::malloc(len + 1u);
	if (!out) {
		return nullptr;
	}
	for (size_t i = 0; i < len; i++) {
		out[i] = (begin[i] == '\\') ? '/' : begin[i];
	}
	out[len] = '\0';
	return out;
}

static char *copyString(const char *value)
{
	return value ? copySegment(value, value + std::strlen(value)) : nullptr;
}

static void *readWholeFile(const char *path, u32 *outSize)
{
	if (outSize) {
		*outSize = 0;
	}
	FILE *f = std::fopen(path, "rb");
	if (!f) {
		return nullptr;
	}
	if (std::fseek(f, 0, SEEK_END) != 0) {
		std::fclose(f);
		return nullptr;
	}
	long len = std::ftell(f);
	if (len < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
		std::fclose(f);
		return nullptr;
	}
	void *bytes = std::malloc((size_t)len + 1u);
	if (!bytes) {
		std::fclose(f);
		return nullptr;
	}
	if (len > 0 && std::fread(bytes, 1, (size_t)len, f) != (size_t)len) {
		std::free(bytes);
		std::fclose(f);
		return nullptr;
	}
	std::fclose(f);
	((u8 *)bytes)[len] = 0;
	if (outSize) {
		*outSize = (u32)len;
	}
	return bytes;
}

static void *extractMemChain(void *archiveBytes, u32 archiveSize,
	const char *entryChain, u32 *outSize)
{
	const char *sep = findNestedSep(entryChain);
	char *entry = sep ? copySegment(entryChain, sep) : copyString(entryChain);
	if (!entry || !entry[0]) {
		std::free(entry);
		return nullptr;
	}

	u32 entrySize = 0;
	void *raw = modArchiveExtractMemAlloc(archiveBytes, archiveSize, entry,
		&entrySize);
	std::free(entry);
	if (!raw) {
		return nullptr;
	}

	if (sep) {
		void *nested = extractMemChain(raw, entrySize, sep + 2, outSize);
		std::free(raw);
		return nested;
	}

	if (outSize) {
		*outSize = entrySize;
	}
	return raw;
}

static void *extractDiskArchiveChain(const char *archivePath,
	const char *entryChain, u32 *outSize)
{
	const char *sep = findNestedSep(entryChain);
	char *entry = sep ? copySegment(entryChain, sep) : copyString(entryChain);
	if (!entry || !entry[0]) {
		std::free(entry);
		return nullptr;
	}

	mod_archive_t *archive = modArchiveOpen(archivePath);
	if (!archive) {
		std::free(entry);
		return nullptr;
	}

	s32 idx = modArchiveFindEntry(archive, entry);
	std::free(entry);
	if (idx < 0) {
		modArchiveClose(archive);
		return nullptr;
	}

	u32 entrySize = 0;
	void *raw = modArchiveExtractAlloc(archive, idx, &entrySize);
	modArchiveClose(archive);
	if (!raw) {
		return nullptr;
	}

	if (sep) {
		void *nested = extractMemChain(raw, entrySize, sep + 2, outSize);
		std::free(raw);
		return nested;
	}

	if (outSize) {
		*outSize = entrySize;
	}
	return raw;
}

extern "C" const char *fsFullPath(const char *relPath, char *out,
	size_t outSize)
{
	if (!out || outSize == 0) {
		return relPath ? relPath : "";
	}
	std::snprintf(out, outSize, "%s", relPath ? relPath : "");
	return out;
}

extern "C" void *fsFileLoad(const char *name, u32 *outSize)
{
	if (outSize) {
		*outSize = 0;
	}
	if (!name || !name[0]) {
		return nullptr;
	}

	const char *sep = findNestedSep(name);
	if (!sep) {
		return readWholeFile(name, outSize);
	}

	char *archivePath = copySegment(name, sep);
	if (!archivePath || !archivePath[0]) {
		std::free(archivePath);
		return nullptr;
	}
	void *bytes = extractDiskArchiveChain(archivePath, sep + 2, outSize);
	std::free(archivePath);
	return bytes;
}

extern "C" void sysInitArgs(s32 argc, const char **argv)
{
	g_argc = argc;
	g_argv = argv;
}

extern "C" s32 sysArgCheck(const char *arg)
{
	if (!arg || !g_argv) {
		return 0;
	}
	for (int i = 1; i < g_argc; i++) {
		if (g_argv[i] && std::strcmp(g_argv[i], arg) == 0) {
			return 1;
		}
	}
	return 0;
}

extern "C" void sysLogPrintf(s32 level, const char *fmt, ...)
{
	(void)level;
	va_list args;
	va_start(args, fmt);
	std::vfprintf(stderr, fmt, args);
	std::fputc('\n', stderr);
	va_end(args);
}

extern "C" void sysFatalError(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	std::vfprintf(stderr, fmt, args);
	std::fputc('\n', stderr);
	va_end(args);
	std::exit(1);
}
