/**
 * modmgr_bundled_samples.c -- the bundled sample inventory: see
 * modmgr_bundled_samples.h.
 */
#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "modmgr_bundled_samples.h"
#include "modpack_archive.h"

#define BUNDLED_DIGEST_HEX 64

static s32 bundledFail(modmgr_bundled_inventory_t *out, char *err, size_t err_cap,
	s32 line, const char *why)
{
	memset(out, 0, sizeof(*out));
	if (err && err_cap) {
		snprintf(err, err_cap, "line %d: %s", (int)line, why);
	}
	return 0;
}

static s32 bundledIsLowerHex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static char bundledFold(char c)
{
	if (c == '\\') {
		return '/';
	}
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static s32 bundledEqualNoCase(const char *a, const char *b)
{
	for (;; a++, b++) {
		const char ca = bundledFold(*a);
		const char cb = bundledFold(*b);
		if (ca != cb) {
			return 0;
		}
		if (!ca) {
			return 1;
		}
	}
}

s32 modmgrBundledParse(const char *text, size_t len, modmgr_bundled_inventory_t *out,
	char *err, size_t err_cap)
{
	const size_t ext_len = sizeof(MODMGR_BUNDLED_EXT) - 1;
	size_t at = 0;
	s32 line = 0;

	if (err && err_cap) {
		err[0] = '\0';
	}
	if (!out) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
	if (!text && len) {
		return bundledFail(out, err, err_cap, 0, "no inventory text");
	}
	if (len > MODMGR_BUNDLED_MAX_BYTES) {
		return bundledFail(out, err, err_cap, 0, "the inventory is too large");
	}
	while (at < len) {
		const char *start = text + at;
		const char *newline = (const char *)memchr(start, '\n', len - at);
		char id[MODMGR_BUNDLED_ID_LEN];
		size_t n;
		size_t name_len;
		size_t id_len;
		modmgr_bundled_entry_t *entry;

		line++;
		if (!newline) {
			return bundledFail(out, err, err_cap, line, "the last line has no newline");
		}
		if (out->count >= MODMGR_BUNDLED_MAX) {
			return bundledFail(out, err, err_cap, line, "too many packages");
		}
		n = (size_t)(newline - start);
		/* <64 lowercase hex> " *" <id> ".pdmod" */
		if (n < BUNDLED_DIGEST_HEX + 2 + 1 + ext_len) {
			return bundledFail(out, err, err_cap, line, "the line is too short");
		}
		for (s32 i = 0; i < BUNDLED_DIGEST_HEX; i++) {
			if (!bundledIsLowerHex(start[i])) {
				return bundledFail(out, err, err_cap, line,
					"the digest is not 64 lowercase hex digits");
			}
		}
		if (start[BUNDLED_DIGEST_HEX] != ' ' || start[BUNDLED_DIGEST_HEX + 1] != '*') {
			return bundledFail(out, err, err_cap, line, "expected \" *\" after the digest");
		}
		name_len = n - (BUNDLED_DIGEST_HEX + 2);
		if (memcmp(start + BUNDLED_DIGEST_HEX + 2 + name_len - ext_len,
				MODMGR_BUNDLED_EXT, ext_len) != 0) {
			return bundledFail(out, err, err_cap, line, "the file is not a .pdmod");
		}
		id_len = name_len - ext_len;
		if (id_len >= sizeof(id)) {
			return bundledFail(out, err, err_cap, line, "the id is too long");
		}
		memcpy(id, start + BUNDLED_DIGEST_HEX + 2, id_len);
		id[id_len] = '\0';
		if (strlen(id) != id_len || !modpackArchiveNameIsSafe(id)) {
			return bundledFail(out, err, err_cap, line, "the id is not a safe folder name");
		}
		for (s32 i = 0; i < out->count; i++) {
			if (bundledEqualNoCase(out->entries[i].id, id)) {
				return bundledFail(out, err, err_cap, line, "the id appears twice");
			}
		}
		entry = &out->entries[out->count++];
		memcpy(entry->id, id, id_len + 1);
		memcpy(entry->digest, start, BUNDLED_DIGEST_HEX);
		entry->digest[BUNDLED_DIGEST_HEX] = '\0';
		at += n + 1;
	}
	return 1;
}

s32 modmgrBundledIndexOf(const modmgr_bundled_inventory_t *inv, const char *id)
{
	if (!inv || !id) {
		return -1;
	}
	for (s32 i = 0; i < inv->count; i++) {
		if (strcmp(inv->entries[i].id, id) == 0) {
			return i;
		}
	}
	return -1;
}

s32 modmgrBundledFindVerified(const modmgr_bundled_inventory_t *inv, s32 index,
	const modmgr_bundled_candidate_t *rows, s32 row_count, const char *expected_path,
	modmgr_bundled_verify_fn verify, void *user)
{
	const modmgr_bundled_entry_t *entry;
	const modmgr_bundled_candidate_t *row;
	s32 found = -1;

	if (!inv || index < 0 || index >= inv->count || !rows || row_count < 0
			|| !expected_path || !expected_path[0] || !verify) {
		return -1;
	}
	entry = &inv->entries[index];
	for (s32 i = 0; i < row_count; i++) {
		if (!rows[i].id || strcmp(rows[i].id, entry->id) != 0) {
			continue;
		}
		if (found >= 0) {
			return -1; /* two mods carry the id: neither is the bundled default */
		}
		found = i;
	}
	if (found < 0) {
		return -1;
	}
	row = &rows[found];
	if (!row->valid || !row->is_archive || row->session_only || !row->archive_path
			|| !bundledEqualNoCase(row->archive_path, expected_path)) {
		return -1;
	}
	return verify(row->archive_path, entry->digest, user) == 1 ? found : -1;
}
