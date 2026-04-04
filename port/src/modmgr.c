/**
 * modmgr.c — Dynamic mod manager for Perfect Dark Mike
 *
 * Scans mods/ directory, parses mod.json manifests (with modconfig.txt fallback),
 * manages enable/disable state, integrates with filesystem and config system.
 *
 * Phase D3a: Foundation — scanning, parsing, config persistence, fs integration.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "platform.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "modmgr.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_load.h"
#include "data.h"
#include "game/stagetable.h"
#include "video.h"

/* Forward declaration — defined in src/lib/main.c */
extern void mainChangeToStage(s32 stagenum);
#define MODMGR_STAGE_TITLE 0x5a  /* STAGE_TITLE */

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

modinfo_t g_ModRegistry[MODMGR_MAX_MODS];
s32       g_ModRegistryCount = 0;
bool      g_ModManagerInitialized = false;

// Config-persisted string: comma-separated enabled mod IDs
static char g_ModEnabledList[2048] = "";

// Dirty flag: set when user toggles mods in menu, cleared on reload
static bool g_ModDirty = false;

// Path buffer for file resolution
static char g_ModPathBuf[FS_MAXPATH + 1];

// Resolved mods directory path (set by modmgrScanDirectory)
static char g_ModsDirPath[FS_MAXPATH + 1] = "";

// ---------------------------------------------------------------------------
// Catalog-backed accessor caches (D3R-5 rewire)
// ---------------------------------------------------------------------------
// The Asset Catalog is the single source of truth for arena, body, and head
// data. These caches convert catalog entries back into game structs for ABI
// compatibility with 62+ existing callsites. Rebuild lazily on dirty flag.

#define MODMGR_MAX_CATALOG_ARENAS 256
#define MODMGR_MAX_CATALOG_BODIES 256
#define MODMGR_MAX_CATALOG_HEADS  256

static struct mparena s_CatalogArenas[MODMGR_MAX_CATALOG_ARENAS];
static s32            s_CatalogArenaCount = 0;

static struct mpbody  s_CatalogBodies[MODMGR_MAX_CATALOG_BODIES];
static s32            s_CatalogBodyCount = 0;

static struct mphead  s_CatalogHeads[MODMGR_MAX_CATALOG_HEADS];
static s32            s_CatalogHeadCount = 0;

static s32            s_CatalogCacheDirty = 1; // dirty at startup

static void modmgrRebuildArenaCache(void);
static void modmgrRebuildBodyCache(void);
static void modmgrRebuildHeadCache(void);
static void modmgrRebuildAllCaches(void);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void modmgrScanDirectory(void);
static bool modmgrParseModJson(modinfo_t *mod);
static void modmgrRegisterModJsonContent(modinfo_t *mod);
static void modmgrLoadMod(modinfo_t *mod);
static void modmgrUnloadAllMods(void);
static u32  modmgrHashString(const char *str);
static void modmgrParseEnabledList(void);
static void modmgrBuildEnabledList(void);

// ---------------------------------------------------------------------------
// Minimal JSON parser (read-only, for mod.json)
// ---------------------------------------------------------------------------

// Token types for our simple JSON needs
typedef enum {
	JTOK_NONE = 0,
	JTOK_LBRACE,     // {
	JTOK_RBRACE,     // }
	JTOK_LBRACKET,   // [
	JTOK_RBRACKET,   // ]
	JTOK_COLON,      // :
	JTOK_COMMA,      // ,
	JTOK_STRING,     // "..."
	JTOK_NUMBER,     // 123 or 0x1a
	JTOK_TRUE,       // true
	JTOK_FALSE,      // false
	JTOK_NULL,       // null
	JTOK_EOF,
	JTOK_ERROR,
} jtok_type_t;

typedef struct {
	const char *start;  // pointer into source
	s32 len;            // length of token value (for strings: excluding quotes)
	jtok_type_t type;
} jtok_t;

typedef struct {
	const char *src;    // full source
	const char *pos;    // current parse position
	jtok_t cur;         // current token
	char strbuf[512];   // buffer for extracted string values
} jparse_t;

static void json_skip_ws(jparse_t *j)
{
	while (*j->pos && (*j->pos == ' ' || *j->pos == '\t' || *j->pos == '\n' || *j->pos == '\r')) {
		j->pos++;
	}
}

static jtok_t json_next(jparse_t *j)
{
	jtok_t tok = { NULL, 0, JTOK_NONE };
	json_skip_ws(j);

	if (!*j->pos) {
		tok.type = JTOK_EOF;
		return tok;
	}

	tok.start = j->pos;
	char c = *j->pos;

	switch (c) {
	case '{': tok.type = JTOK_LBRACE;   tok.len = 1; j->pos++; break;
	case '}': tok.type = JTOK_RBRACE;   tok.len = 1; j->pos++; break;
	case '[': tok.type = JTOK_LBRACKET;  tok.len = 1; j->pos++; break;
	case ']': tok.type = JTOK_RBRACKET;  tok.len = 1; j->pos++; break;
	case ':': tok.type = JTOK_COLON;    tok.len = 1; j->pos++; break;
	case ',': tok.type = JTOK_COMMA;    tok.len = 1; j->pos++; break;
	case '"': {
		j->pos++; // skip opening quote
		tok.start = j->pos;
		while (*j->pos && *j->pos != '"') {
			if (*j->pos == '\\') j->pos++; // skip escaped char
			if (*j->pos) j->pos++;
		}
		tok.len = (s32)(j->pos - tok.start);
		tok.type = JTOK_STRING;
		if (*j->pos == '"') j->pos++; // skip closing quote
		break;
	}
	default:
		if (c == '-' || (c >= '0' && c <= '9')) {
			// Number (decimal or hex)
			if (c == '0' && (j->pos[1] == 'x' || j->pos[1] == 'X')) {
				j->pos += 2;
				while (isxdigit((unsigned char)*j->pos)) j->pos++;
			} else {
				if (c == '-') j->pos++;
				while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
				if (*j->pos == '.') {
					j->pos++;
					while (*j->pos >= '0' && *j->pos <= '9') j->pos++;
				}
			}
			tok.len = (s32)(j->pos - tok.start);
			tok.type = JTOK_NUMBER;
		} else if (strncmp(j->pos, "true", 4) == 0) {
			tok.type = JTOK_TRUE; tok.len = 4; j->pos += 4;
		} else if (strncmp(j->pos, "false", 5) == 0) {
			tok.type = JTOK_FALSE; tok.len = 5; j->pos += 5;
		} else if (strncmp(j->pos, "null", 4) == 0) {
			tok.type = JTOK_NULL; tok.len = 4; j->pos += 4;
		} else {
			tok.type = JTOK_ERROR;
			j->pos++;
		}
		break;
	}

	j->cur = tok;
	return tok;
}

// Extract string value from a STRING token into dest buffer
static void json_tok_string(const jtok_t *tok, char *dest, s32 maxlen)
{
	if (tok->type != JTOK_STRING || !tok->start) {
		dest[0] = '\0';
		return;
	}
	s32 copylen = tok->len < (maxlen - 1) ? tok->len : (maxlen - 1);
	memcpy(dest, tok->start, copylen);
	dest[copylen] = '\0';
}

// Extract integer value from a NUMBER token
static s32 json_tok_int(const jtok_t *tok)
{
	if (tok->type != JTOK_NUMBER || !tok->start) return 0;
	return (s32)strtol(tok->start, NULL, 0);
}

// Extract boolean from TRUE/FALSE token
static bool json_tok_bool(const jtok_t *tok)
{
	return tok->type == JTOK_TRUE;
}

// Skip a JSON value (object, array, or primitive) — used to skip unrecognized keys
static void json_skip_value(jparse_t *j)
{
	jtok_t tok = json_next(j);
	if (tok.type == JTOK_LBRACE) {
		// skip object
		s32 depth = 1;
		while (depth > 0) {
			tok = json_next(j);
			if (tok.type == JTOK_LBRACE) depth++;
			else if (tok.type == JTOK_RBRACE) depth--;
			else if (tok.type == JTOK_EOF || tok.type == JTOK_ERROR) return;
		}
	} else if (tok.type == JTOK_LBRACKET) {
		// skip array
		s32 depth = 1;
		while (depth > 0) {
			tok = json_next(j);
			if (tok.type == JTOK_LBRACKET) depth++;
			else if (tok.type == JTOK_RBRACKET) depth--;
			else if (tok.type == JTOK_EOF || tok.type == JTOK_ERROR) return;
		}
	}
	// primitives (string, number, bool, null) are already consumed by json_next
}

// Check if current string token matches a key name
static bool json_key_eq(const jtok_t *tok, const char *key)
{
	if (tok->type != JTOK_STRING) return false;
	s32 keylen = (s32)strlen(key);
	return tok->len == keylen && memcmp(tok->start, key, keylen) == 0;
}

// ---------------------------------------------------------------------------
// mod.json parser
// ---------------------------------------------------------------------------

static bool modmgrParseModJson(modinfo_t *mod)
{
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/mod.json", mod->dirpath);

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) {
		return false;
	}

	// Null-terminate
	char *buf = (char *)malloc(filesize + 1);
	if (!buf) {
		free(data);
		return false;
	}
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	jparse_t j;
	j.src = buf;
	j.pos = buf;

	jtok_t tok = json_next(&j);
	if (tok.type != JTOK_LBRACE) {
		sysLogPrintf(LOG_WARNING, "modmgr: %s/mod.json: expected object", mod->id);
		free(buf);
		return false;
	}

	// Parse top-level keys
	while (1) {
		tok = json_next(&j);
		if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
		if (tok.type == JTOK_COMMA) continue;

		if (tok.type != JTOK_STRING) {
			json_skip_value(&j);
			continue;
		}

		jtok_t key = tok;

		// Expect colon
		tok = json_next(&j);
		if (tok.type != JTOK_COLON) break;

		if (json_key_eq(&key, "id")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->id, MODMGR_ID_LEN);
		} else if (json_key_eq(&key, "name")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->name, MODMGR_NAME_LEN);
		} else if (json_key_eq(&key, "version")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->version, MODMGR_VERSION_LEN);
		} else if (json_key_eq(&key, "author")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->author, MODMGR_AUTHOR_LEN);
		} else if (json_key_eq(&key, "description")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->description, MODMGR_DESC_LEN);
		} else if (json_key_eq(&key, "content")) {
			// Parse content object for asset counts
			tok = json_next(&j);
			if (tok.type == JTOK_LBRACE) {
				while (1) {
					tok = json_next(&j);
					if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
					if (tok.type == JTOK_COMMA) continue;
					if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

					jtok_t ckey = tok;
					tok = json_next(&j); // colon
					if (tok.type != JTOK_COLON) break;

					// Count array items
					tok = json_next(&j);
					if (tok.type == JTOK_LBRACKET) {
						s32 count = 0;
						s32 depth = 1;
						while (depth > 0) {
							tok = json_next(&j);
							if (tok.type == JTOK_LBRACKET) depth++;
							else if (tok.type == JTOK_RBRACKET) depth--;
							else if (tok.type == JTOK_LBRACE && depth == 1) count++;
							else if (tok.type == JTOK_EOF) break;
						}
						if (json_key_eq(&ckey, "bodies")) mod->num_bodies = count;
						else if (json_key_eq(&ckey, "heads")) mod->num_heads = count;
						else if (json_key_eq(&ckey, "arenas")) mod->num_arenas = count;
					} else {
						// Not an array, skip
					}
				}
			}
		} else {
			// Unknown key — skip value
			json_skip_value(&j);
		}
	}

	free(buf);
	mod->has_modjson = true;

	sysLogPrintf(LOG_NOTE, "modmgr: parsed mod.json for '%s' (%s v%s by %s) — %d bodies, %d heads, %d arenas",
		mod->id, mod->name, mod->version, mod->author,
		mod->num_bodies, mod->num_heads, mod->num_arenas);

	return true;
}

// ---------------------------------------------------------------------------
// Mod content registration (D3b)
// ---------------------------------------------------------------------------
// Parses the mod.json "content" section and registers bodies, heads, and arenas
// into the Asset Catalog. Called from modmgrLoadMod() for each enabled mod.
//
// Expected mod.json schema for each content type:
//   "bodies":  [ { "id": "...", "bodynum": N, "name_langid": N, "headnum": N, "requirefeature": N }, ... ]
//   "heads":   [ { "id": "...", "headnum": N, "requirefeature": N }, ... ]
//   "arenas":  [ { "id": "...", "stagenum": N, "name_langid": N, "requirefeature": N }, ... ]
//
// Catalog IDs are built as "{modid}:{item_id}".
// runtime_index is assigned sequentially starting after all currently registered
// entries of that type (base game + any prior mod entries).
// ---------------------------------------------------------------------------

static void modmgrRegisterModJsonContent(modinfo_t *mod)
{
	if (!mod->has_modjson) return;
	if (mod->num_bodies == 0 && mod->num_heads == 0 && mod->num_arenas == 0) return;

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/mod.json", mod->dirpath);

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) return;

	char *buf = (char *)malloc(filesize + 1);
	if (!buf) { free(data); return; }
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	jparse_t j;
	j.src = buf;
	j.pos = buf;

	// Determine starting runtime_index offsets — new entries slot in after
	// all existing entries of each type (base game + prior mod registrations).
	s32 body_start  = assetCatalogGetCountByType(ASSET_BODY);
	s32 head_start  = assetCatalogGetCountByType(ASSET_HEAD);
	s32 arena_start = assetCatalogGetCountByType(ASSET_ARENA);
	s32 body_reg = 0, head_reg = 0, arena_reg = 0;

	jtok_t tok = json_next(&j);
	if (tok.type != JTOK_LBRACE) goto done;

	// Scan top-level keys for "content"
	while (1) {
		tok = json_next(&j);
		if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
		if (tok.type == JTOK_COMMA) continue;
		if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

		jtok_t topkey = tok;
		tok = json_next(&j); // colon
		if (tok.type != JTOK_COLON) break;

		if (!json_key_eq(&topkey, "content")) {
			json_skip_value(&j);
			continue;
		}

		// Parse "content" object
		tok = json_next(&j);
		if (tok.type != JTOK_LBRACE) break;

		while (1) {
			tok = json_next(&j);
			if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
			if (tok.type == JTOK_COMMA) continue;
			if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

			jtok_t ckey = tok;
			tok = json_next(&j); // colon
			if (tok.type != JTOK_COLON) break;

			s32 is_bodies = json_key_eq(&ckey, "bodies");
			s32 is_heads  = json_key_eq(&ckey, "heads");
			s32 is_arenas = json_key_eq(&ckey, "arenas");

			if (!is_bodies && !is_heads && !is_arenas) {
				json_skip_value(&j);
				continue;
			}

			// Expect array "["; if not, skip whatever value this is
			tok = json_next(&j);
			if (tok.type != JTOK_LBRACKET) {
				// First token of value already consumed — skip compound values
				if (tok.type == JTOK_LBRACE) {
					s32 d = 1;
					while (d > 0) {
						tok = json_next(&j);
						if (tok.type == JTOK_LBRACE)   d++;
						else if (tok.type == JTOK_RBRACE) d--;
						else if (tok.type == JTOK_EOF)    break;
					}
				}
				// Primitives already consumed — nothing more to do
				continue;
			}

			// Iterate array elements
			while (1) {
				tok = json_next(&j);
				if (tok.type == JTOK_RBRACKET || tok.type == JTOK_EOF) break;
				if (tok.type == JTOK_COMMA) continue;
				if (tok.type != JTOK_LBRACE) continue; // unexpected; skip primitive

				// Parse item object fields (common superset for all three types)
				char id[CATALOG_ID_LEN] = "";
				s32  bodynum_field      = -1; // "bodynum"  — body index
				s32  headnum_field      = -1; // "headnum"  — head index (also body's default head)
				s32  stagenum_field     = -1; // "stagenum" — arena stage ID
				s32  name_langid        = 0;  // "name_langid"
				s32  requirefeature     = 0;  // "requirefeature"

				while (1) {
					tok = json_next(&j);
					if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
					if (tok.type == JTOK_COMMA) continue;
					if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

					jtok_t fkey = tok;
					tok = json_next(&j); // colon
					if (tok.type != JTOK_COLON) break;

					if (json_key_eq(&fkey, "id")) {
						tok = json_next(&j);
						json_tok_string(&tok, id, sizeof(id));
					} else if (json_key_eq(&fkey, "bodynum")) {
						tok = json_next(&j);
						bodynum_field = json_tok_int(&tok);
					} else if (json_key_eq(&fkey, "headnum")) {
						tok = json_next(&j);
						headnum_field = json_tok_int(&tok);
					} else if (json_key_eq(&fkey, "stagenum")) {
						tok = json_next(&j);
						stagenum_field = json_tok_int(&tok);
					} else if (json_key_eq(&fkey, "name_langid")) {
						tok = json_next(&j);
						name_langid = json_tok_int(&tok);
					} else if (json_key_eq(&fkey, "requirefeature")) {
						tok = json_next(&j);
						requirefeature = json_tok_int(&tok);
					} else {
						json_skip_value(&j); // unknown field
					}
				}

				if (id[0] == '\0') continue; // no id — skip entry

				char catid[CATALOG_ID_LEN];
				snprintf(catid, sizeof(catid), "%s:%s", mod->id, id);

				asset_entry_t *e = NULL;

				if (is_bodies && bodynum_field >= 0) {
					s16 defhead = (headnum_field >= 0) ? (s16)headnum_field : 0;
					e = assetCatalogRegisterBody(catid, (s16)bodynum_field,
					        (s16)name_langid, defhead, (u8)requirefeature);
					if (e) { e->runtime_index = body_start + body_reg++; }
				} else if (is_heads && headnum_field >= 0) {
					e = assetCatalogRegisterHead(catid, (s16)headnum_field,
					        (u8)requirefeature);
					if (e) { e->runtime_index = head_start + head_reg++; }
				} else if (is_arenas && stagenum_field >= 0) {
					e = assetCatalogRegisterArena(catid, stagenum_field,
					        (u8)requirefeature, name_langid);
					if (e) { e->runtime_index = arena_start + arena_reg++; }
				}

				if (e) {
					strncpy(e->category, mod->id, CATALOG_CATEGORY_LEN - 1);
					e->bundled  = 0;
					e->enabled  = 1;
				}
			}
		}
		break; // "content" processed; stop scanning top-level keys
	}

done:
	if (body_reg || head_reg || arena_reg) {
		sysLogPrintf(LOG_NOTE,
		    "modmgr: '%s' content registered: %d bodies, %d heads, %d arenas",
		    mod->id, body_reg, head_reg, arena_reg);
	}
	free(buf);
}

// ---------------------------------------------------------------------------
// Simple CRC32 for string hashing
// ---------------------------------------------------------------------------

/* Recursively sum the sizes of all files under dirpath.
 * Returns total bytes (capped at UINT32_MAX). */
static u32 modmgrComputeDirSize(const char *dirpath)
{
	DIR *dir = opendir(dirpath);
	if (!dir) return 0;

	u32 total = 0;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;

		char fullpath[FS_MAXPATH + 1];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);

		struct stat st;
		if (stat(fullpath, &st) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			u32 sub = modmgrComputeDirSize(fullpath);
			total = (total + sub < total) ? 0xFFFFFFFF : total + sub; /* overflow guard */
		} else {
			u32 fsz = (u32)st.st_size;
			total = (total + fsz < total) ? 0xFFFFFFFF : total + fsz;
		}
	}
	closedir(dir);
	return total;
}

static u32 modmgrHashString(const char *str)
{
	u32 hash = 0xFFFFFFFF;
	while (*str) {
		hash ^= (u8)*str++;
		for (s32 i = 0; i < 8; i++) {
			hash = (hash >> 1) ^ (0xEDB88320 & (-(hash & 1)));
		}
	}
	return ~hash;
}

// ---------------------------------------------------------------------------
// Directory scanning
// ---------------------------------------------------------------------------

static void modmgrScanDirectory(void)
{
	g_ModRegistryCount = 0;

	// PC: fsFullPath("mods") resolves relative to baseDir (./data/mods), but
	// mods live at ./mods/ relative to the working directory. Try CWD first,
	// then exe dir, then the base dir fallback.
	const char *modsdir = NULL;
	DIR *dir = NULL;
	const char *candidates[] = {
		"./" MODMGR_MODS_DIR,
		fsFullPath("$E/" MODMGR_MODS_DIR),
		fsFullPath(MODMGR_MODS_DIR),
	};

	for (s32 i = 0; i < 3; i++) {
		dir = opendir(candidates[i]);
		if (dir) {
			modsdir = candidates[i];
			break;
		}
	}

	if (!dir) {
		sysLogPrintf(LOG_WARNING, "modmgr: could not open mods directory (tried ./%s, $E/%s, base/%s)",
			MODMGR_MODS_DIR, MODMGR_MODS_DIR, MODMGR_MODS_DIR);
		return;
	}

	// Store resolved path for assetCatalogScanComponents() to use later
	strncpy(g_ModsDirPath, modsdir, sizeof(g_ModsDirPath) - 1);
	g_ModsDirPath[sizeof(g_ModsDirPath) - 1] = '\0';

	sysLogPrintf(LOG_NOTE, "modmgr: scanning '%s' for mods...", modsdir);

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL && g_ModRegistryCount < MODMGR_MAX_MODS) {
		// Skip . and ..
		if (ent->d_name[0] == '.') continue;

		// Build full path
		char fullpath[FS_MAXPATH + 1];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", modsdir, ent->d_name);

		// Check if it's a directory
		struct stat st;
		if (stat(fullpath, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		// Check if it has mod.json
		char checkpath[FS_MAXPATH + 1];
		bool has_modjson = false;

		snprintf(checkpath, sizeof(checkpath), "%s/mod.json", fullpath);
		if (fsFileSize(checkpath) > 0) {
			has_modjson = true;
		}

		if (!has_modjson) {
			sysLogPrintf(LOG_NOTE, "modmgr: skipping '%s' (no mod.json)", ent->d_name);
			continue;
		}

		// Initialize mod entry
		modinfo_t *mod = &g_ModRegistry[g_ModRegistryCount];
		memset(mod, 0, sizeof(modinfo_t));
		strncpy(mod->dirpath, fullpath, FS_MAXPATH - 1);
		mod->dirpath[FS_MAXPATH - 1] = '\0';

		if (!modmgrParseModJson(mod)) {
			sysLogPrintf(LOG_WARNING, "modmgr: skipping '%s' (mod.json parse failed)", ent->d_name);
			continue;
		}

		// mod->bundled is always 0 — no hardcoded bundled mods exist
		mod->bundled = 0;

		// Compute content hash from ID + version
		char hashsrc[256];
		snprintf(hashsrc, sizeof(hashsrc), "%s:%s", mod->id, mod->version);
		mod->contenthash = modmgrHashString(hashsrc);

		// Compute SHA-256 for authoritative network verification.
		// Primary: hash mod.json content (stable, covers declared assets).
		// Fallback: hash the "id:version" string so sha256 is never all-zeroes.
		{
			char modjsonpath[FS_MAXPATH + 1];
			snprintf(modjsonpath, sizeof(modjsonpath), "%s/mod.json", fullpath);
			if (sha256HashFile(modjsonpath, mod->sha256) != 0) {
				sha256Hash(hashsrc, strlen(hashsrc), mod->sha256);
			}
		}

		// Compute directory size for download estimation
		mod->size_bytes = modmgrComputeDirSize(fullpath);

		// Default: disabled until user explicitly enables
		mod->enabled = 0;

		g_ModRegistryCount++;
		sysLogPrintf(LOG_NOTE, "modmgr: discovered mod [%d] '%s' (%s) %s",
			g_ModRegistryCount - 1, mod->id, mod->name,
			mod->has_modjson ? "[mod.json]" : "[legacy]");
	}

	closedir(dir);
	sysLogPrintf(LOG_NOTE, "modmgr: scan complete — %d mods found", g_ModRegistryCount);
}

// ---------------------------------------------------------------------------
// Config persistence
// ---------------------------------------------------------------------------

// Parse comma-separated enabled list and set mod->enabled flags
static void modmgrParseEnabledList(void)
{
	if (g_ModEnabledList[0] == '\0') {
		// Empty config = use defaults (all mods disabled)
		return;
	}

	// First, disable all mods
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		g_ModRegistry[i].enabled = false;
	}

	// Parse comma-separated list
	char buf[2048];
	strncpy(buf, g_ModEnabledList, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	char *token = strtok(buf, ",");
	while (token) {
		// Trim whitespace
		while (*token == ' ') token++;
		char *end = token + strlen(token) - 1;
		while (end > token && *end == ' ') *end-- = '\0';

		if (*token) {
			modinfo_t *mod = modmgrFindMod(token);
			if (mod) {
				mod->enabled = true;
			} else {
				sysLogPrintf(LOG_WARNING, "modmgr: config references unknown mod '%s'", token);
			}
		}
		token = strtok(NULL, ",");
	}
}

// Build comma-separated list from current enabled flags
static void modmgrBuildEnabledList(void)
{
	g_ModEnabledList[0] = '\0';
	s32 pos = 0;
	bool first = true;

	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) {
			s32 idlen = (s32)strlen(g_ModRegistry[i].id);
			s32 needed = idlen + (first ? 0 : 1); // comma + id
			if (pos + needed >= (s32)sizeof(g_ModEnabledList) - 1) break;

			if (!first) {
				g_ModEnabledList[pos++] = ',';
			}
			memcpy(&g_ModEnabledList[pos], g_ModRegistry[i].id, idlen);
			pos += idlen;
			first = false;
		}
	}
	g_ModEnabledList[pos] = '\0';
}

void modmgrSaveConfig(void)
{
	modmgrBuildEnabledList();
	configSave(CONFIG_PATH);
	sysLogPrintf(LOG_NOTE, "modmgr: saved config — enabled mods: %s",
		g_ModEnabledList[0] ? g_ModEnabledList : "(none)");
}

void modmgrLoadConfig(void)
{
	// Config system already loaded g_ModEnabledList from pd.ini
	modmgrParseEnabledList();
}

// ---------------------------------------------------------------------------
// Mod loading (register assets from a single mod)
// ---------------------------------------------------------------------------

static void modmgrLoadMod(modinfo_t *mod)
{
	if (mod->loaded) return;

	sysLogPrintf(LOG_NOTE, "modmgr: loading mod '%s' from %s", mod->id, mod->dirpath);

	// D3b: Register mod.json content sections (bodies, heads, arenas) into catalog.
	// Component-based content (maps, characters) is handled by assetCatalogScanComponents().
	modmgrRegisterModJsonContent(mod);

	mod->loaded = true;
}

static void modmgrUnloadAllMods(void)
{
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		g_ModRegistry[i].loaded = false;
	}

	stageTableReset();
}

// ---------------------------------------------------------------------------
// Sort: bundled first, then alphabetical by name
// ---------------------------------------------------------------------------

static int modmgrCompare(const void *a, const void *b)
{
	const modinfo_t *ma = (const modinfo_t *)a;
	const modinfo_t *mb = (const modinfo_t *)b;

	// Alphabetical by name
	return strcmp(ma->name, mb->name);
}

// ---------------------------------------------------------------------------
// Public API: Lifecycle
// ---------------------------------------------------------------------------

PD_CONSTRUCTOR static void modmgrConfigInit(void)
{
	configRegisterString("Mods.EnabledMods", g_ModEnabledList, sizeof(g_ModEnabledList));
}

void modmgrInit(void)
{
	if (g_ModManagerInitialized) return;

	sysLogPrintf(LOG_NOTE, "modmgr: initializing...");

	// Scan for mods
	modmgrScanDirectory();

	// Sort registry
	if (g_ModRegistryCount > 1) {
		qsort(g_ModRegistry, g_ModRegistryCount, sizeof(modinfo_t), modmgrCompare);
	}

	// Apply config (enable/disable based on saved preferences)
	modmgrLoadConfig();

	// Load all enabled mods
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) {
			modmgrLoadMod(&g_ModRegistry[i]);
		}
	}

	g_ModDirty = false;
	g_ModManagerInitialized = true;

	s32 enabledCount = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) enabledCount++;
	}
	sysLogPrintf(LOG_NOTE, "modmgr: initialized — %d mods, %d enabled",
		g_ModRegistryCount, enabledCount);
}

void modmgrShutdown(void)
{
	modmgrUnloadAllMods();
	g_ModManagerInitialized = false;
	g_ModRegistryCount = 0;
	sysLogPrintf(LOG_NOTE, "modmgr: shutdown");
}

void modmgrReload(void)
{
	sysLogPrintf(LOG_NOTE, "modmgr: reloading — rebuilding asset tables...");

	// Unload everything
	modmgrUnloadAllMods();

	// C-8: Rebuild catalog with the new enabled mod set.
	// assetCatalogClearMods() removes all non-bundled (mod) entries from the
	// catalog, then re-scanning repopulates them for the currently enabled mods.
	// catalogLoadInit() then rebuilds the four reverse-index arrays so the
	// C-4/C-5/C-6/C-7 intercepts reflect the updated mod state immediately.
	sysLogPrintf(LOG_NOTE, "MOD: catalog rebuild — clearing mod entries");
	assetCatalogClearMods();
	{
		const char *modsdir = modmgrGetModsDir();
		if (modsdir) {
			s32 ncomp = assetCatalogScanComponents(modsdir);
			assetCatalogScanBotVariants(modsdir);
			sysLogPrintf(LOG_NOTE, "MOD: catalog rebuild — %d component(s) re-registered", ncomp);
		}
	}
	// Restore per-component enable state from .modstate (written by
	// modmgrSaveComponentState during the preceding apply step).
	modmgrLoadComponentState();
	// Rebuild reverse-index arrays — skips disabled entries, so components
	// toggled off via .modstate will not appear in override lookups.
	catalogLoadInit();
	sysLogPrintf(LOG_NOTE, "MOD: catalog rebuild complete — %d total entries",
	             assetCatalogGetCount());

	// Re-load enabled mods
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) {
			modmgrLoadMod(&g_ModRegistry[i]);
		}
	}

	g_ModDirty = false;

	sysLogPrintf(LOG_NOTE, "modmgr: reload complete");

	// Invalidate catalog-backed caches so accessors pick up new state
	modmgrCatalogChanged();

	videoResetTextureCache();
	mainChangeToStage(MODMGR_STAGE_TITLE);
}

// ---------------------------------------------------------------------------
// Public API: Registry queries
// ---------------------------------------------------------------------------

s32 modmgrGetCount(void)
{
	return g_ModRegistryCount;
}

modinfo_t *modmgrGetMod(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return NULL;
	return &g_ModRegistry[index];
}

modinfo_t *modmgrFindMod(const char *id)
{
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (strcmp(g_ModRegistry[i].id, id) == 0) {
			return &g_ModRegistry[i];
		}
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// Public API: Enable/Disable
// ---------------------------------------------------------------------------

void modmgrSetEnabled(s32 index, s32 enabled)
{
	if (index < 0 || index >= g_ModRegistryCount) return;
	if (g_ModRegistry[index].enabled != enabled) {
		g_ModRegistry[index].enabled = enabled;
		g_ModDirty = true;
	}
}

s32 modmgrIsDirty(void)
{
	return g_ModDirty;
}

// ---------------------------------------------------------------------------
// Component-level enable state (D3R-6)
// ---------------------------------------------------------------------------
// State file: mods/.modstate — one disabled component ID per line.
// Lines beginning with '#' are comments.  Blank lines are ignored.
// Only non-bundled (mod) entries are ever written here; base game entries
// are always enabled and are never listed.
// ---------------------------------------------------------------------------

// Iteration callback: writes disabled non-bundled entry IDs to a FILE*.
typedef struct { FILE *f; s32 *count; } SaveStateCtx;

static void saveStateCallback(const asset_entry_t *entry, void *userdata)
{
	SaveStateCtx *ctx = (SaveStateCtx *)userdata;
	if (!entry->enabled && !entry->bundled) {
		fprintf(ctx->f, "%s\n", entry->id);
		(*ctx->count)++;
	}
}

void modmgrSaveComponentState(void)
{
	const char *modsdir = modmgrGetModsDir();
	if (!modsdir) {
		sysLogPrintf(LOG_NOTE, "modmgr: no mods dir, skipping component state save");
		return;
	}

	char statepath[FS_MAXPATH + 1];
	snprintf(statepath, sizeof(statepath), "%s/.modstate", modsdir);

	FILE *f = fopen(statepath, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "modmgr: could not write component state to %s", statepath);
		return;
	}

	fprintf(f, "# mods/.modstate -- disabled component IDs\n");
	fprintf(f, "# Written by Mod Manager. One ID per line. # = comment.\n");

	s32 count = 0;
	SaveStateCtx ctx = { f, &count };

	// Iterate all user-manageable asset types (non-bundled entries only matter)
	static const asset_type_e types[] = {
		ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
		ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
		ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL
	};
	for (s32 i = 0; i < (s32)(sizeof(types) / sizeof(types[0])); i++) {
		assetCatalogIterateByType(types[i], saveStateCallback, &ctx);
	}

	fclose(f);
	sysLogPrintf(LOG_NOTE, "modmgr: saved component state (%d disabled)", count);
}

void modmgrLoadComponentState(void)
{
	const char *modsdir = modmgrGetModsDir();
	if (!modsdir) {
		return;
	}

	char statepath[FS_MAXPATH + 1];
	snprintf(statepath, sizeof(statepath), "%s/.modstate", modsdir);

	FILE *f = fopen(statepath, "r");
	if (!f) {
		return;  /* no .modstate file = everything enabled, that's fine */
	}

	char line[CATALOG_ID_LEN + 4];
	s32 count = 0;
	while (fgets(line, sizeof(line), f)) {
		/* Strip trailing newline/carriage-return */
		s32 len = (s32)strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}

		/* Skip comments and blank lines */
		if (line[0] == '#' || line[0] == '\0') {
			continue;
		}

		assetCatalogSetEnabled(line, 0);
		count++;
	}

	fclose(f);
	if (count > 0) {
		sysLogPrintf(LOG_NOTE, "modmgr: loaded component state (%d disabled from .modstate)", count);
	}
}

void modmgrApplyChanges(void)
{
	sysLogPrintf(LOG_NOTE, "modmgr: applying changes...");

	/* Persist catalog enable state to .modstate (read at next scan) */
	modmgrSaveComponentState();

	/* Persist legacy modinfo enables */
	modmgrSaveConfig();

	/* C-8: Component enable/disable flags changed — rebuild the reverse-index
	 * arrays so C-4/C-5/C-6/C-7 intercepts reflect the new state before the
	 * title screen reloads.  catalogLoadInit() skips disabled entries, so
	 * components the user toggled off will produce no overrides. */
	sysLogPrintf(LOG_NOTE, "MOD: reverse-index rebuild after component toggle");
	catalogLoadInit();

	/* Invalidate catalog-backed caches so accessors pick up new state */
	modmgrCatalogChanged();

	/* Return to title screen — clean slate for the new mod configuration */
	mainChangeToStage(MODMGR_STAGE_TITLE);

	sysLogPrintf(LOG_NOTE, "modmgr: apply complete — returning to title");
}

// ---------------------------------------------------------------------------
// Public API: Network
// ---------------------------------------------------------------------------

u32 modmgrGetManifestHash(void)
{
	// Build a combined hash of all enabled mod IDs and versions
	u32 hash = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) {
			hash ^= g_ModRegistry[i].contenthash;
			// Rotate to avoid order-independence
			hash = (hash << 7) | (hash >> 25);
		}
	}
	return hash;
}

s32 modmgrWriteManifest(u8 *buf, s32 maxlen)
{
	s32 pos = 0;

	// Count enabled mods
	u16 count = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled) count++;
	}

	if (pos + 2 > maxlen) return -1;
	buf[pos++] = (count >> 8) & 0xFF;
	buf[pos++] = count & 0xFF;

	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (!g_ModRegistry[i].enabled) continue;

		s32 idlen = (s32)strlen(g_ModRegistry[i].id);
		s32 verlen = (s32)strlen(g_ModRegistry[i].version);

		if (pos + 1 + idlen + 1 + verlen + 8 > maxlen) return -1;

		buf[pos++] = (u8)idlen;
		memcpy(&buf[pos], g_ModRegistry[i].id, idlen);
		pos += idlen;

		buf[pos++] = (u8)verlen;
		memcpy(&buf[pos], g_ModRegistry[i].version, verlen);
		pos += verlen;

		// Content hash (4 bytes)
		buf[pos++] = (g_ModRegistry[i].contenthash >> 24) & 0xFF;
		buf[pos++] = (g_ModRegistry[i].contenthash >> 16) & 0xFF;
		buf[pos++] = (g_ModRegistry[i].contenthash >> 8) & 0xFF;
		buf[pos++] = g_ModRegistry[i].contenthash & 0xFF;

		// size_bytes (4 bytes) — total mod directory size for download estimation
		buf[pos++] = (g_ModRegistry[i].size_bytes >> 24) & 0xFF;
		buf[pos++] = (g_ModRegistry[i].size_bytes >> 16) & 0xFF;
		buf[pos++] = (g_ModRegistry[i].size_bytes >>  8) & 0xFF;
		buf[pos++] = g_ModRegistry[i].size_bytes & 0xFF;
	}

	return pos;
}

s32 modmgrReadManifest(const u8 *buf, s32 len, char *missing, s32 misslen)
{
	s32 pos = 0;
	s32 missingCount = 0;
	s32 misspos = 0;

	if (pos + 2 > len) return -1;
	u16 count = ((u16)buf[pos] << 8) | buf[pos + 1];
	pos += 2;

	for (u16 i = 0; i < count; i++) {
		if (pos + 1 > len) return -1;
		s32 idlen = buf[pos++];
		if (pos + idlen > len) return -1;

		char modid[MODMGR_ID_LEN];
		s32 cplen = idlen < (s32)(MODMGR_ID_LEN - 1) ? idlen : (s32)(MODMGR_ID_LEN - 1);
		memcpy(modid, &buf[pos], cplen);
		modid[cplen] = '\0';
		pos += idlen;

		if (pos + 1 > len) return -1;
		s32 verlen = buf[pos++];
		if (pos + verlen > len) return -1;

		char modver[MODMGR_VERSION_LEN];
		s32 vlen = verlen < (s32)(MODMGR_VERSION_LEN - 1) ? verlen : (s32)(MODMGR_VERSION_LEN - 1);
		memcpy(modver, &buf[pos], vlen);
		modver[vlen] = '\0';
		pos += verlen;

		if (pos + 8 > len) return -1;
		u32 remotehash = ((u32)buf[pos] << 24) | ((u32)buf[pos+1] << 16) |
		                 ((u32)buf[pos+2] << 8) | buf[pos+3];
		pos += 4;
		/* size_bytes — decoded but not used in compatibility check */
		/* u32 remote_size = ((u32)buf[pos]<<24)|((u32)buf[pos+1]<<16)|((u32)buf[pos+2]<<8)|buf[pos+3]; */
		pos += 4;

		// Check if we have this mod
		modinfo_t *local = modmgrFindMod(modid);
		if (!local || !local->enabled || local->contenthash != remotehash) {
			missingCount++;
			if (missing && misspos < misslen - 1) {
				s32 wrote = snprintf(&missing[misspos], misslen - misspos,
					"%s%s v%s", misspos > 0 ? ", " : "", modid, modver);
				if (wrote > 0) misspos += wrote;
			}
		}
	}

	return missingCount;
}

// ---------------------------------------------------------------------------
// Public API: Filesystem integration
// ---------------------------------------------------------------------------

const char *modmgrResolvePath(const char *relPath)
{
	// Iterate enabled mods in registry order (load order)
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (!g_ModRegistry[i].enabled || !g_ModRegistry[i].loaded) continue;

		snprintf(g_ModPathBuf, sizeof(g_ModPathBuf), "%s/%s",
			g_ModRegistry[i].dirpath, relPath);

		if (fsFileSize(g_ModPathBuf) >= 0) {
			return g_ModPathBuf;
		}
	}

	return NULL; // not found in any mod
}

const char *modmgrGetModDir(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return NULL;
	return g_ModRegistry[index].dirpath;
}

// ---------------------------------------------------------------------------
// Public API: Dynamic asset table accessors (catalog-backed, D3R-5)
// ---------------------------------------------------------------------------
// The Asset Catalog is the single source of truth. These accessors read from
// cache arrays populated by catalog iteration, falling back to legacy static
// arrays only during early startup before the catalog is initialized.
// All caches rebuild lazily when s_CatalogCacheDirty is set.

static void modmgrEnsureCaches(void)
{
	if (!s_CatalogCacheDirty) return;
	modmgrRebuildAllCaches();
}

// ---- Body collect callback + rebuild ----

static void modmgrBodyCollectCb(const asset_entry_t *entry, void *userdata)
{
	/*
	 * FIX: Index by mpbodynum (sequential position = 0, 1, 2, ...) NOT by
	 * entry->runtime_index (g_HeadsAndBodies bodynum, e.g. 86 for BODY_DARK_COMBAT).
	 * modmgrGetBody(mpbodynum) accesses s_CatalogBodies[mpbodynum], so the cache
	 * must be in mpbodynum order.  Bodies are registered in g_MpBodies[] order
	 * (s_BaseBodies indices 0..62), so the iteration order matches mpbodynum order.
	 */
	s32 *idx_ptr = (s32 *)userdata;
	s32 idx = *idx_ptr;

	if (idx < 0 || idx >= MODMGR_MAX_CATALOG_BODIES) {
		sysLogPrintf(LOG_WARNING, "modmgr: body \"%s\" position %d out of cache range",
			entry->id, idx);
		return;
	}

	struct mpbody *b = &s_CatalogBodies[idx];
	b->bodynum = entry->ext.body.bodynum;
	b->name = entry->ext.body.name_langid;
	b->headnum = entry->ext.body.headnum;
	b->requirefeature = entry->ext.body.requirefeature;

	(*idx_ptr)++;
	if (*idx_ptr > s_CatalogBodyCount) {
		s_CatalogBodyCount = *idx_ptr;
	}
}

static void modmgrRebuildBodyCache(void)
{
	s32 idx = 0;
	memset(s_CatalogBodies, 0, sizeof(s_CatalogBodies));
	s_CatalogBodyCount = 0;

	assetCatalogIterateByType(ASSET_BODY, modmgrBodyCollectCb, &idx);
}

// ---- Head collect callback + rebuild ----

static void modmgrHeadCollectCb(const asset_entry_t *entry, void *userdata)
{
	/*
	 * FIX: Index by mpheadnum (sequential position = 0, 1, 2, ...) NOT by
	 * entry->runtime_index (g_HeadsAndBodies headnum, e.g. HEAD_BEAU1=0x18).
	 * modmgrGetHead(mpheadnum) accesses s_CatalogHeads[mpheadnum], so the cache
	 * must be in mpheadnum order.  Heads are registered in g_MpHeads[] order
	 * (loop mpidx 0..75), so the iteration order matches mpheadnum order.
	 */
	s32 *idx_ptr = (s32 *)userdata;
	s32 idx = *idx_ptr;

	if (idx < 0 || idx >= MODMGR_MAX_CATALOG_HEADS) {
		sysLogPrintf(LOG_WARNING, "modmgr: head \"%s\" position %d out of cache range",
			entry->id, idx);
		return;
	}

	struct mphead *h = &s_CatalogHeads[idx];
	h->headnum = entry->ext.head.headnum;
	h->requirefeature = entry->ext.head.requirefeature;

	(*idx_ptr)++;
	if (*idx_ptr > s_CatalogHeadCount) {
		s_CatalogHeadCount = *idx_ptr;
	}
}

static void modmgrRebuildHeadCache(void)
{
	s32 idx = 0;
	memset(s_CatalogHeads, 0, sizeof(s_CatalogHeads));
	s_CatalogHeadCount = 0;

	assetCatalogIterateByType(ASSET_HEAD, modmgrHeadCollectCb, &idx);
}

// ---- Arena collect callback + rebuild ----

static void modmgrArenaCollectCb(const asset_entry_t *entry, void *userdata)
{
	(void)userdata;
	s32 idx = entry->runtime_index;

	if (idx < 0 || idx >= MODMGR_MAX_CATALOG_ARENAS) {
		sysLogPrintf(LOG_WARNING, "modmgr: arena \"%s\" runtime_index %d out of cache range",
			entry->id, idx);
		return;
	}

	struct mparena *a = &s_CatalogArenas[idx];
	a->stagenum = (s16)entry->ext.arena.stagenum;
	a->requirefeature = (u8)entry->ext.arena.requirefeature;
	a->name = (u16)entry->ext.arena.name_langid;

	if (idx + 1 > s_CatalogArenaCount) {
		s_CatalogArenaCount = idx + 1;
	}
}

static void modmgrRebuildArenaCache(void)
{
	memset(s_CatalogArenas, 0, sizeof(s_CatalogArenas));
	s_CatalogArenaCount = 0;

	assetCatalogIterateByType(ASSET_ARENA, modmgrArenaCollectCb, NULL);
}

// ---- Unified rebuild ----

static void modmgrRebuildAllCaches(void)
{
	modmgrRebuildBodyCache();
	modmgrRebuildHeadCache();
	modmgrRebuildArenaCache();

	s_CatalogCacheDirty = 0;

	sysLogPrintf(LOG_NOTE, "modmgr: rebuilt catalog caches — bodies=%d heads=%d arenas=%d",
		s_CatalogBodyCount, s_CatalogHeadCount, s_CatalogArenaCount);
}

// ---- Bodies (catalog-backed) ----

s32 modmgrGetTotalBodies(void)
{
	modmgrEnsureCaches();
	return s_CatalogBodyCount > 0 ? s_CatalogBodyCount : MODMGR_BASE_BODIES;
}

struct mpbody *modmgrGetBody(s32 index)
{
	modmgrEnsureCaches();
	if (index < 0) return &s_CatalogBodies[0];
	if (index < s_CatalogBodyCount) return &s_CatalogBodies[index];
	return &s_CatalogBodies[0];
}

// ---- Heads (catalog-backed) ----

s32 modmgrGetTotalHeads(void)
{
	modmgrEnsureCaches();
	return s_CatalogHeadCount > 0 ? s_CatalogHeadCount : MODMGR_BASE_HEADS;
}

struct mphead *modmgrGetHead(s32 index)
{
	modmgrEnsureCaches();
	if (index < 0) return &s_CatalogHeads[0];
	if (index < s_CatalogHeadCount) return &s_CatalogHeads[index];
	return &s_CatalogHeads[0];
}

// ---- Arenas (catalog-backed) ----

s32 modmgrGetTotalArenas(void)
{
	modmgrEnsureCaches();
	return s_CatalogArenaCount > 0 ? s_CatalogArenaCount : MODMGR_BASE_ARENAS;
}

struct mparena *modmgrGetArena(s32 index)
{
	modmgrEnsureCaches();
	if (index < 0) return &s_CatalogArenas[0];
	if (index < s_CatalogArenaCount) return &s_CatalogArenas[index];
	return &s_CatalogArenas[0];
}

// ---- Catalog change signal ----

void modmgrCatalogChanged(void)
{
	s_CatalogCacheDirty = 1;
}

const char *modmgrGetModsDir(void)
{
	return g_ModsDirPath[0] ? g_ModsDirPath : NULL;
}
