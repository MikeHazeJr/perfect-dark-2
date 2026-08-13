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
#include <stdarg.h>
#include <PR/ultratypes.h>
#include "types.h"
#include "platform.h"
#include "system.h"
#include "config.h"
#include "fs.h"
#include "modmgr.h"
#include "modarchive.h"
#include "modvfs.h"
#include "modmigrate.h"
#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_load.h"
#include "data.h"
#include "game/stagetable.h"
#include "game/menu.h"
#include "video.h"
#include "pdgui.h"
#include "pdgui_theme_loader.h"  /* Issue 2/8: theme rescan after mod apply */
#include "pdgui_theme.h"         /* S-8: chrome style rescan after mod apply */
#include "prefs_agent.h"

/* Forward declaration — defined in src/lib/main.c */
extern void mainChangeToStage(s32 stagenum);
#define MODMGR_STAGE_TITLE 0x5a  /* STAGE_TITLE */

/* Static forward declarations */
static void modmgrParseBotNames(modinfo_t *mod);
static void modmgrParseBotNamesBuf(modinfo_t *mod, const char *src, u32 size);
static void modmgrClearBotNames(void);
static bool modmgrParseModJsonBuf(modinfo_t *mod, const char *src, u32 size,
                                   const char *manifest_label);
static void modmgrRegisterModJsonContentBuf(modinfo_t *mod, const char *src, u32 size);
static s32  modmgrTryRegisterArchive(const char *archivePath, const char *display_id,
                                      const char *source_tag, bool dedupe_by_id);
static int  modmgrIsReservedTopLevel(const char *name);
static int  modmgrHasArchiveExtension(const char *name);
static int  modmgrHasPdmodExtension(const char *name);
static s32  modmgrCopyFileAtomic(const char *src, const char *dst);

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

modinfo_t g_ModRegistry[MODMGR_MAX_MODS];
s32       g_ModRegistryCount = 0;
bool      g_ModManagerInitialized = false;

// Config-persisted string: comma-separated enabled mod IDs
static char g_ModEnabledList[2048] = "";

// Size threshold in MB — mods exceeding this trigger a confirmation prompt
static s32 g_ModSizeThresholdMB = 50;

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
static bool modmgrParseAudioIni(modinfo_t *mod);
static void modmgrRegisterModJsonContent(modinfo_t *mod);
static bool modmgrLoadMod(modinfo_t *mod);
static void modmgrUnloadAllMods(void);
static void modmgrRebuildCatalogFromCurrentSelection(void);
static s32  modmgrParseAudioCategoryValue(const char *value, s32 defaultCategory);
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

/**
 * Parse a mod.json document already loaded into memory.
 *
 * `src`             : pointer to JSON bytes; must be NUL-terminated AT or AFTER
 *                     position `size`. The buffer is read-only.
 * `size`            : byte count of JSON content (excluding any terminator).
 * `manifest_label`  : human-readable source identifier used for log lines
 *                     ("<archivepath>:mod.json", "<dirpath>/mod.json", etc.).
 *
 * Side effects: populates fields on `mod`. Sets `mod->has_modjson` on success.
 * The function does NOT take ownership of `src`; the caller frees it.
 *
 * Returns true on parse success, false if the document does not start with
 * an object or is otherwise unrecoverable.
 */
static bool modmgrParseModJsonBuf(modinfo_t *mod, const char *src, u32 size,
                                   const char *manifest_label)
{
	(void)size;  /* JSON parser is NUL-terminated-driven */

	jparse_t j;
	j.src = src;
	j.pos = src;

	jtok_t tok = json_next(&j);
	if (tok.type != JTOK_LBRACE) {
		sysLogPrintf(LOG_WARNING, "modmgr: %s: expected object",
			manifest_label ? manifest_label : mod->id);
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
		} else if (json_key_eq(&key, "base_fallback")) {
			tok = json_next(&j);
			json_tok_string(&tok, mod->base_fallback, MODMGR_FALLBACK_LEN);
		} else if (json_key_eq(&key, "dependencies")) {
			tok = json_next(&j);
			if (tok.type == JTOK_LBRACKET) {
				mod->num_dependencies = 0;
				while (1) {
					tok = json_next(&j);
					if (tok.type == JTOK_RBRACKET || tok.type == JTOK_EOF) break;
					if (tok.type == JTOK_COMMA) continue;
					if (tok.type == JTOK_STRING && mod->num_dependencies < MODMGR_MAX_DEPS) {
						json_tok_string(&tok, mod->dependencies[mod->num_dependencies], MODMGR_DEP_ID_LEN);
						mod->num_dependencies++;
					}
				}
			}
		} else if (json_key_eq(&key, "template")) {
			// S196: base-game template flag — hard gate for the save path
			tok = json_next(&j);
			if (tok.type == JTOK_TRUE) {
				mod->is_template = 1;
			} else if (tok.type == JTOK_FALSE || tok.type == JTOK_NULL) {
				mod->is_template = 0;
			} else {
				// Numeric or other — treat non-zero as true
				mod->is_template = (tok.type == JTOK_NUMBER) ? 1 : 0;
			}
		} else if (json_key_eq(&key, "tags")) {
			// S196: free-form UI grouping labels
			tok = json_next(&j);
			if (tok.type == JTOK_LBRACKET) {
				mod->num_tags = 0;
				while (1) {
					tok = json_next(&j);
					if (tok.type == JTOK_RBRACKET || tok.type == JTOK_EOF) break;
					if (tok.type == JTOK_COMMA) continue;
					if (tok.type == JTOK_STRING && mod->num_tags < MODMGR_MAX_TAGS) {
						json_tok_string(&tok, mod->tags[mod->num_tags], MODMGR_TAG_LEN);
						mod->num_tags++;
					}
				}
			}
		} else if (json_key_eq(&key, "requires_restart")) {
			/* Priority M: opt-out of hot-reload. mod authors set true when
			 * the mod's setup is not idempotent. Loader surfaces "Restart
			 * required for [name] to take effect." instead of hot-mounting. */
			tok = json_next(&j);
			if (tok.type == JTOK_TRUE) {
				mod->requires_restart = 1;
			} else if (tok.type == JTOK_FALSE || tok.type == JTOK_NULL) {
				mod->requires_restart = 0;
			} else {
				mod->requires_restart = (tok.type == JTOK_NUMBER) ? (json_tok_int(&tok) != 0) : 0;
			}
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
			/* Unknown key -- skip value. NOTE (B-913): `contents` (plural, a
			 * top-level tag array like ["palette","menubg","shimmer",...]) and
			 * `assets` are recognised FORWARD-LOOKING schema affordances, not
			 * yet consumed -- the future mod-manager sub-type green-dot
			 * indicators will read `contents`. Skipping them here is intentional
			 * and correct, NOT a parser bug: current mods register via loose-file
			 * auto-discovery plus the singular `content` counts above. Wire real
			 * consumers here when that UI lands. */
			json_skip_value(&j);
		}
	}

	mod->has_modjson = true;

	// Compatibility validation / defaults:
	// - id: if missing, derive from a leaf-name slug. For folder mods this
	//       is the directory basename; for archive mods the archive
	//       filename without its .pdmod / .zip extension.
	// - name: if missing, use id
	// - base_fallback: if missing, default to "base-game"
	mod->valid = true;
	mod->validation_error[0] = '\0';
	{
		/* Pick the fallback source: archive filename for archive-backed
		 * mods, directory leaf otherwise. Either way, scan from the right
		 * for the rightmost path separator. */
		const char *slugSrc = (mod->is_archive && mod->archive_path[0])
		                        ? mod->archive_path : mod->dirpath;
		const char *slash = strrchr(slugSrc, '/');
		const char *bslash = strrchr(slugSrc, '\\');
		const char *base = slash;
		if (!base || (bslash && bslash > base)) base = bslash;
		base = base ? base + 1 : slugSrc;

		/* Strip a trailing .pdmod / .zip from the slug so the fallback id
		 * does not include the extension. */
		char slug[MODMGR_ID_LEN];
		strncpy(slug, base ? base : "", sizeof(slug) - 1);
		slug[sizeof(slug) - 1] = '\0';
		if (mod->is_archive) {
			s32 sl = (s32)strlen(slug);
			if (sl > 6 && strcmp(slug + sl - 6, MODMGR_PDMOD_EXT) == 0) {
				slug[sl - 6] = '\0';
			} else if (sl > 4 && strcmp(slug + sl - 4, MODMGR_ZIP_EXT) == 0) {
				slug[sl - 4] = '\0';
			}
		}

		if (mod->id[0] == '\0' && slug[0] != '\0') {
			strncpy(mod->id, slug, MODMGR_ID_LEN - 1);
			mod->id[MODMGR_ID_LEN - 1] = '\0';
			sysLogPrintf(LOG_WARNING,
				"modmgr: mod '%s' missing id in %s — using filename fallback",
				mod->id, manifest_label ? manifest_label : "mod.json");
		}
		if (mod->name[0] == '\0' && mod->id[0] != '\0') {
			strncpy(mod->name, mod->id, MODMGR_NAME_LEN - 1);
			mod->name[MODMGR_NAME_LEN - 1] = '\0';
		}
		if (mod->base_fallback[0] == '\0') {
			strncpy(mod->base_fallback, "base-game", MODMGR_FALLBACK_LEN - 1);
			mod->base_fallback[MODMGR_FALLBACK_LEN - 1] = '\0';
		}
		if (mod->id[0] == '\0') {
			mod->valid = false;
			snprintf(mod->validation_error, MODMGR_ERROR_LEN, "Missing required field: id");
		}
	}

	if (mod->valid) {
		sysLogPrintf(LOG_NOTE, "modmgr: parsed mod.json for '%s' (%s v%s by %s) — %d bodies, %d heads, %d arenas, base_mod=%s, template=%s, tags=%d, archive=%s, requires_restart=%s",
			mod->id, mod->name, mod->version, mod->author,
			mod->num_bodies, mod->num_heads, mod->num_arenas, mod->base_fallback,
			mod->is_template ? "yes" : "no", mod->num_tags,
			mod->is_archive ? "yes" : "no",
			mod->requires_restart ? "yes" : "no");
	} else {
		sysLogPrintf(LOG_WARNING, "modmgr: mod.json validation failed for '%s': %s",
			mod->id[0] ? mod->id : "(unknown)", mod->validation_error);
	}

	return true;
}

/* Folder-mod entry point: load mod.json from disk and call the buffer body. */
static bool modmgrParseModJson(modinfo_t *mod)
{
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/mod.json", mod->dirpath);

	/* B-220: avoid fsFileLoad ERROR spam when the file vanished after scan */
	if (fsFileSize(path) <= 0) {
		return false;
	}

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) {
		return false;
	}

	/* Null-terminate the buffer for the JSON parser. */
	char *buf = (char *)malloc(filesize + 1);
	if (!buf) {
		free(data);
		return false;
	}
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	bool ok = modmgrParseModJsonBuf(mod, buf, filesize, path);
	free(buf);
	return ok;
}

// ---------------------------------------------------------------------------
// audio.ini parser — ini-based audio mod discovery
// ---------------------------------------------------------------------------
// Parses audio.ini from a mod directory. Format:
//   [audio]
//   type = audio
//   name = My Track
//   category = 1
//   duration_ms = 0
//   file_path = mytrack.mp3
//
// Populates modinfo_t fields so the mod appears in the Mod Manager and catalog.

static s32 modmgrParseAudioCategoryValue(const char *value, s32 defaultCategory)
{
	if (!value || !value[0]) {
		return defaultCategory;
	}

	while (*value == ' ' || *value == '\t') {
		value++;
	}
	if (!*value) {
		return defaultCategory;
	}

	/* Numeric form: category=0/1/2 */
	if ((*value >= '0' && *value <= '9') || *value == '-' || *value == '+') {
		s32 n = (s32)strtol(value, NULL, 10);
		if (n >= AUDIO_CAT_SFX && n <= AUDIO_CAT_VOICE) {
			return n;
		}
		return defaultCategory;
	}

	/* Text form: category=music/sfx/voice */
	char lower[32];
	s32 i = 0;
	while (value[i] && i < (s32)sizeof(lower) - 1) {
		lower[i] = (char)tolower((unsigned char)value[i]);
		i++;
	}
	lower[i] = '\0';

	if (strcmp(lower, "music") == 0 || strcmp(lower, "track") == 0) {
		return AUDIO_CAT_MUSIC;
	}
	if (strcmp(lower, "sfx") == 0 || strcmp(lower, "sound") == 0 || strcmp(lower, "soundfx") == 0) {
		return AUDIO_CAT_SFX;
	}
	if (strcmp(lower, "voice") == 0 || strcmp(lower, "dialog") == 0 || strcmp(lower, "dialogue") == 0) {
		return AUDIO_CAT_VOICE;
	}

	return defaultCategory;
}

static bool modmgrParseAudioIni(modinfo_t *mod)
{
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/audio.ini", mod->dirpath);

	FILE *f = fopen(path, "r");
	if (!f) return false;

	char name[MODMGR_NAME_LEN] = "";
	char file_path[FS_MAXPATH] = "";
	s32  category = 1; /* default: music */
	s32  duration_ms = 0;
	bool in_audio_section = false;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		/* Strip trailing whitespace/newline */
		s32 len = (s32)strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
		       line[len-1] == ' ' || line[len-1] == '\t')) {
			line[--len] = '\0';
		}

		/* Skip empty lines and comments */
		if (len == 0 || line[0] == '#' || line[0] == ';') continue;

		/* Section header */
		if (line[0] == '[') {
			in_audio_section = (strncmp(line, "[audio]", 7) == 0);
			continue;
		}

		if (!in_audio_section) continue;

		/* Parse key = value */
		char *eq = strchr(line, '=');
		if (!eq) continue;

		/* Trim key */
		char *kstart = line;
		while (*kstart == ' ' || *kstart == '\t') kstart++;
		char *kend = eq - 1;
		while (kend > kstart && (*kend == ' ' || *kend == '\t')) kend--;
		kend[1] = '\0';

		/* Trim value */
		char *vstart = eq + 1;
		while (*vstart == ' ' || *vstart == '\t') vstart++;

		if (strcmp(kstart, "name") == 0) {
			strncpy(name, vstart, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
		} else if (strcmp(kstart, "category") == 0) {
			category = modmgrParseAudioCategoryValue(vstart, category);
		} else if (strcmp(kstart, "duration_ms") == 0) {
			duration_ms = atoi(vstart);
		} else if (strcmp(kstart, "file_path") == 0) {
			strncpy(file_path, vstart, sizeof(file_path) - 1);
			file_path[sizeof(file_path) - 1] = '\0';
		}
		/* "type" key is noted but not stored — the presence of audio.ini
		 * itself is the type discriminator */
	}

	fclose(f);

	if (name[0] == '\0') {
		sysLogPrintf(LOG_WARNING, "modmgr: audio.ini in '%s' missing name field",
			mod->dirpath);
		return false;
	}

	/* Derive mod ID from directory name (matches how importAudioFile creates the slug) */
	const char *dirname = mod->dirpath;
	for (const char *p = mod->dirpath; *p; p++) {
		if (*p == '/' || *p == '\\') dirname = p + 1;
	}
	snprintf(mod->id, MODMGR_ID_LEN, "%s", dirname);
	strncpy(mod->name, name, MODMGR_NAME_LEN - 1);
	mod->name[MODMGR_NAME_LEN - 1] = '\0';
	strncpy(mod->version, "1.0", MODMGR_VERSION_LEN - 1);
	strncpy(mod->author, "User", MODMGR_AUTHOR_LEN - 1);
	snprintf(mod->description, MODMGR_DESC_LEN, "Audio mod: %s", name);
	/* Audio mods have no base_fallback requirement — they add content, not replace */
	mod->base_fallback[0] = '\0';

	mod->has_modjson = false;
	mod->has_audioini = true;
	mod->valid = true;
	mod->validation_error[0] = '\0';

	(void)duration_ms;
	(void)file_path;
	(void)category;

	sysLogPrintf(LOG_NOTE, "modmgr: parsed audio.ini for '%s' (%s)",
		mod->id, mod->name);

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

/* Body that walks an in-memory mod.json buffer. The buffer must be NUL
 * terminated. Caller owns the buffer; this function does NOT free it. */
static void modmgrRegisterModJsonContentBuf(modinfo_t *mod, const char *src, u32 size)
{
	if (!mod->has_modjson) return;
	if (mod->num_bodies == 0 && mod->num_heads == 0 && mod->num_arenas == 0) return;
	if (!src || size == 0) return;

	jparse_t j;
	j.src = src;
	j.pos = src;

	// Body/head runtime slots are owned by the catalog registration helpers.
	// Custom entries without legacy numbers get private slots there.
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

				if (is_bodies) {
					s16 defhead = (headnum_field >= 0) ? (s16)headnum_field : -1;
					e = assetCatalogRegisterBody(catid, (s16)bodynum_field,
					        (s16)name_langid, defhead, (u8)requirefeature);
					if (e) { body_reg++; }
				} else if (is_heads) {
					e = assetCatalogRegisterHead(catid, (s16)headnum_field,
					        (u8)requirefeature);
					if (e) { head_reg++; }
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
}

/* Folder-mod entry point: load mod.json from disk and call the buffer body. */
static void modmgrRegisterModJsonContent(modinfo_t *mod)
{
	if (!mod->has_modjson) return;
	if (mod->num_bodies == 0 && mod->num_heads == 0 && mod->num_arenas == 0) return;

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/mod.json", mod->dirpath);

	if (fsFileSize(path) <= 0) {
		return;
	}

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) return;

	char *buf = (char *)malloc(filesize + 1);
	if (!buf) { free(data); return; }
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	modmgrRegisterModJsonContentBuf(mod, buf, filesize);
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

static bool modmgrDirHasDirectManifest(const char *fullpath)
{
	if (!fullpath || !fullpath[0]) {
		return false;
	}

	char checkpath[FS_MAXPATH + 1];
	struct stat st;

	snprintf(checkpath, sizeof(checkpath), "%s/mod.json", fullpath);
	if (stat(checkpath, &st) == 0 && S_ISREG(st.st_mode)) {
		return true;
	}

	snprintf(checkpath, sizeof(checkpath), "%s/audio.ini", fullpath);
	return stat(checkpath, &st) == 0 && S_ISREG(st.st_mode);
}

/* Try to register a single mod entry rooted at `fullpath`. `display_id` is
 * the directory-leaf name used for fallback id/name when mod.json parse
 * fails. `source_tag` labels the originating root for log messages (or NULL
 * for the primary root). When `dedupe_by_id` is true, entries whose id
 * already exists in the registry are skipped (used by the alt-root pass and
 * the category-folder recursion).
 *
 * Returns 1 if a slot was consumed, 0 otherwise. Does not recurse. */
static s32 modmgrTryRegisterModEntry(const char *fullpath, const char *display_id,
                                      const char *source_tag, bool dedupe_by_id)
{
	if (g_ModRegistryCount >= MODMGR_MAX_MODS) return 0;

	/* Priority M / B-238 / M-4.1: skip folders ending in `.legacy_backup`.
	 * These are post-migration safety copies of folder mods that have been
	 * packaged into a sibling `.pdmod`; loading both would produce a
	 * duplicate registry entry. The dedupe_by_id check would catch this
	 * for known ids, but the suffix check is the precise rule. */
	if (display_id) {
		size_t leafLen = strlen(display_id);
		const char *suffix = ".legacy_backup";
		size_t suffixLen = strlen(suffix);
		if (leafLen > suffixLen &&
		    strcmp(display_id + leafLen - suffixLen, suffix) == 0) {
			return 0;
		}
	}

	/* Check for manifest: mod.json (full mod) or audio.ini (audio mod) */
	char checkpath[FS_MAXPATH + 1];
	bool has_modjson = false;
	bool has_audioini = false;

	snprintf(checkpath, sizeof(checkpath), "%s/mod.json", fullpath);
	if (fsFileSize(checkpath) > 0) {
		has_modjson = true;
	} else {
		snprintf(checkpath, sizeof(checkpath), "%s/audio.ini", fullpath);
		if (fsFileSize(checkpath) > 0) {
			has_audioini = true;
		}
	}

	if (!has_modjson && !has_audioini) {
		return 0;
	}

	/* Initialize mod entry */
	modinfo_t *mod = &g_ModRegistry[g_ModRegistryCount];
	memset(mod, 0, sizeof(modinfo_t));
	strncpy(mod->dirpath, fullpath, FS_MAXPATH - 1);
	mod->dirpath[FS_MAXPATH - 1] = '\0';

	if (has_modjson) {
		if (!modmgrParseModJson(mod)) {
			strncpy(mod->id, display_id, MODMGR_ID_LEN - 1);
			mod->id[MODMGR_ID_LEN - 1] = '\0';
			strncpy(mod->name, display_id, MODMGR_NAME_LEN - 1);
			mod->name[MODMGR_NAME_LEN - 1] = '\0';
			mod->valid = false;
			snprintf(mod->validation_error, MODMGR_ERROR_LEN,
				"Malformed mod.json — failed to parse");
			mod->has_modjson = false;
			sysLogPrintf(LOG_WARNING, "modmgr: '%s' has invalid mod.json (kept for error display)",
				display_id);
		}
	} else {
		if (!modmgrParseAudioIni(mod)) {
			strncpy(mod->id, display_id, MODMGR_ID_LEN - 1);
			mod->id[MODMGR_ID_LEN - 1] = '\0';
			strncpy(mod->name, display_id, MODMGR_NAME_LEN - 1);
			mod->name[MODMGR_NAME_LEN - 1] = '\0';
			mod->valid = false;
			snprintf(mod->validation_error, MODMGR_ERROR_LEN,
				"Malformed audio.ini — failed to parse");
			mod->has_audioini = false;
			sysLogPrintf(LOG_WARNING, "modmgr: '%s' has invalid audio.ini (kept for error display)",
				display_id);
		}
	}

	if (dedupe_by_id) {
		for (s32 k = 0; k < g_ModRegistryCount; k++) {
			if (strcmp(g_ModRegistry[k].id, mod->id) == 0) {
				/* Duplicate — rewind slot and skip. memset above means we
				 * haven't leaked anything. */
				memset(mod, 0, sizeof(*mod));
				return 0;
			}
		}
	}

	mod->bundled = 0;

	/* Compute content hash from ID + version */
	char hashsrc[256];
	snprintf(hashsrc, sizeof(hashsrc), "%s:%s", mod->id, mod->version);
	mod->contenthash = modmgrHashString(hashsrc);

	/* SHA-256 over manifest content for network verification. */
	{
		char manifestpath[FS_MAXPATH + 1];
		if (has_modjson) {
			snprintf(manifestpath, sizeof(manifestpath), "%s/mod.json", fullpath);
		} else {
			snprintf(manifestpath, sizeof(manifestpath), "%s/audio.ini", fullpath);
		}
		if (sha256HashFile(manifestpath, mod->sha256) != 0) {
			sha256Hash((const u8 *)hashsrc, strlen(hashsrc), mod->sha256);
		}
	}

	mod->size_bytes = modmgrComputeDirSize(fullpath);
	mod->enabled = 0;

	g_ModRegistryCount++;
	if (source_tag) {
		sysLogPrintf(LOG_NOTE, "modmgr: discovered mod [%d] '%s' (%s) %s from '%s'",
			g_ModRegistryCount - 1, mod->id, mod->name,
			mod->has_modjson ? "[mod.json]" : mod->has_audioini ? "[audio.ini]" : "[legacy]",
			source_tag);
	} else {
		sysLogPrintf(LOG_NOTE, "modmgr: discovered mod [%d] '%s' (%s) %s",
			g_ModRegistryCount - 1, mod->id, mod->name,
			mod->has_modjson ? "[mod.json]" : mod->has_audioini ? "[audio.ini]" : "[legacy]");
	}
	return 1;
}

/* ------------------------------------------------------------------
 * Priority M / B-238: archive-based mod discovery
 * ------------------------------------------------------------------
 *
 * `.pdmod` (and plain `.zip` with a root mod.json) entries are first-class
 * mods. The loader opens the archive briefly during scan to read mod.json
 * in memory; the archive handle is reopened in modmgrLoadMod when the mod
 * is enabled. No on-disk extraction at any point. */

/* Returns 1 if the directory leaf name should be skipped during enumeration
 * because it is a reserved trust-gate location (per design Section 8). */
static int modmgrIsReservedTopLevel(const char *name)
{
	static const char *const reserved[MODMGR_RESERVED_NAMES_COUNT] = MODMGR_RESERVED_NAMES_LIST;
	if (!name) return 0;
	for (s32 i = 0; i < MODMGR_RESERVED_NAMES_COUNT; i++) {
		if (strcmp(name, reserved[i]) == 0) return 1;
	}
	return 0;
}

/* Defense-in-depth trust check: for an archive at `archivePath`, verify
 * its parent directory (the directory immediately containing the archive
 * file) is NOT a reserved trust-gate name AT TOP LEVEL of the mods root.
 * The scan walker already skips reserved top-levels; this fires as a
 * second-line assertion against any future code path that might bypass
 * the walker. Returns 1 if trusted, 0 if reserved.
 *
 * Note: only top-level reserved subdirectories trigger refusal. A user
 * who organises a category folder named e.g. mods/UI Chrome/shared/ is
 * still trusted -- only mods/shared/ at the very top level is the inbox. */
static int modmgrArchivePathIsTrusted(const char *archivePath, const char *modsRoot)
{
	if (!archivePath || !modsRoot || !modsRoot[0]) {
		return 1;  /* No root to compare against -- skip the check. */
	}
	size_t rootLen = strlen(modsRoot);
	if (strncmp(archivePath, modsRoot, rootLen) != 0) {
		return 1;  /* Not under the scanned root; outside scope. */
	}
	const char *rel = archivePath + rootLen;
	while (*rel == '/' || *rel == '\\') rel++;

	/* Pull the first path segment. */
	char first[MODMGR_ID_LEN];
	s32 n = 0;
	while (rel[n] && rel[n] != '/' && rel[n] != '\\' && n < (s32)sizeof(first) - 1) {
		first[n] = rel[n];
		n++;
	}
	first[n] = '\0';
	/* If the next character is NUL, this is the archive file itself at the
	 * top level (e.g. mods/foo.pdmod). Trust it. The reserved check applies
	 * only to subdirectories. */
	if (rel[n] == '\0') return 1;

	return !modmgrIsReservedTopLevel(first);
}

/* Hex-encode the first `nBytes` of a sha256 digest into a static buffer.
 * For sysLogPrintf interpolation only -- not thread-safe. */
static const char *modmgrShortSha256(const u8 digest[SHA256_DIGEST_SIZE], s32 nBytes)
{
	static char hex[SHA256_HEX_SIZE];
	if (nBytes < 1) nBytes = 1;
	if (nBytes > SHA256_DIGEST_SIZE) nBytes = SHA256_DIGEST_SIZE;
	for (s32 i = 0; i < nBytes; i++) {
		static const char *const lut = "0123456789abcdef";
		hex[i * 2 + 0] = lut[(digest[i] >> 4) & 0xF];
		hex[i * 2 + 1] = lut[digest[i] & 0xF];
	}
	hex[nBytes * 2] = '\0';
	return hex;
}

/* Informational one-shot: enumerate `mods/shared/` if it exists and log
 * the per-friend file count. The inbox is read-only-for-browsing and the
 * loader never auto-mounts from it; this is a defensive surface so the
 * operator can confirm the inbox is bounded and visible. */
static void modmgrLogSharedInbox(const char *modsRoot)
{
	if (!modsRoot || !modsRoot[0]) return;
	char inboxPath[FS_MAXPATH + 1];
	snprintf(inboxPath, sizeof(inboxPath), "%s/shared", modsRoot);
	DIR *d = opendir(inboxPath);
	if (!d) return;

	s32 friendCount = 0;
	s32 totalFiles  = 0;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		char friendPath[FS_MAXPATH + 1];
		snprintf(friendPath, sizeof(friendPath), "%s/%s", inboxPath, ent->d_name);
		struct stat st;
		if (stat(friendPath, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		friendCount++;
		DIR *fd = opendir(friendPath);
		if (!fd) continue;
		struct dirent *fe;
		while ((fe = readdir(fd)) != NULL) {
			if (fe->d_name[0] == '.') continue;
			if (modmgrHasArchiveExtension(fe->d_name)) totalFiles++;
		}
		closedir(fd);
	}
	closedir(d);

	if (friendCount > 0 || totalFiles > 0) {
		sysLogPrintf(LOG_NOTE,
			"modmgr: shared inbox present -- %d friend folder(s), %d archive(s) (browse-only, never auto-mounted)",
			friendCount, totalFiles);
	}
}

/* Returns 1 if `name` ends in .pdmod or .zip (case-insensitive). */
static int modmgrHasArchiveExtension(const char *name)
{
	if (!name) return 0;
	s32 n = (s32)strlen(name);
	if (n >= 6) {
		const char *tail = name + n - 6;
		if ((tail[0] == '.') &&
		    (tail[1] == 'p' || tail[1] == 'P') &&
		    (tail[2] == 'd' || tail[2] == 'D') &&
		    (tail[3] == 'm' || tail[3] == 'M') &&
		    (tail[4] == 'o' || tail[4] == 'O') &&
		    (tail[5] == 'd' || tail[5] == 'D')) {
			return 1;
		}
	}
	if (n >= 4) {
		const char *tail = name + n - 4;
		if ((tail[0] == '.') &&
		    (tail[1] == 'z' || tail[1] == 'Z') &&
		    (tail[2] == 'i' || tail[2] == 'I') &&
		    (tail[3] == 'p' || tail[3] == 'P')) {
			return 1;
		}
	}
	return 0;
}

static int modmgrHasPdmodExtension(const char *name)
{
	if (!name) return 0;
	s32 n = (s32)strlen(name);
	if (n < 6) return 0;
	const char *tail = name + n - 6;
	return (tail[0] == '.') &&
	       (tail[1] == 'p' || tail[1] == 'P') &&
	       (tail[2] == 'd' || tail[2] == 'D') &&
	       (tail[3] == 'm' || tail[3] == 'M') &&
	       (tail[4] == 'o' || tail[4] == 'O') &&
	       (tail[5] == 'd' || tail[5] == 'D');
}

static int modmgrArchiveEntryHasForbiddenBinPayload(const char *name)
{
	return assetArchiveEntryIsForbiddenBinPayload(name) != 0;
}

static int modmgrArchiveEntryHasForbiddenPublicTsvPayload(const char *name)
{
	return assetArchiveEntryIsForbiddenTsvPayload(name) != 0;
}

static int modmgrArchiveEntryIsTypedPdAssetArchive(const char *name)
{
	if (!name) return 0;
	static const char *suffixes[] = {
		".pdweapon", ".pdprojectile", ".pdentity", ".pdcharacter",
		".pdmaterial", ".pdtexture", ".pdhead", ".pdbody", ".pdarena", ".pdmesh", ".pdanim",
		".pdsfx", ".pdvoice", ".pdsong", ".pdui", ".pdfont", ".pdlang",
		".pdscenario", ".pdskin", ".pdeffect", ".pdprop", ".pdvehicle",
		".pdmission", ".pdgamemode", ".pdbotprofile", ".pdhud", ".pdtheme",
		NULL
	};
	size_t n = strlen(name);
	for (s32 i = 0; suffixes[i]; i++) {
		size_t m = strlen(suffixes[i]);
		if (m <= n) {
			const char *tail = name + n - m;
			s32 match = 1;
			for (size_t j = 0; j < m; j++) {
				if (tolower((u8)tail[j]) != tolower((u8)suffixes[i][j])) {
					match = 0;
					break;
				}
			}
			if (match) return 1;
		}
	}
	return 0;
}

static int modmgrArchiveFindForbiddenPublicPayload(mod_archive_t *arc,
                                                   char *out_name,
                                                   s32 out_name_cap,
                                                   char *out_kind,
                                                   s32 out_kind_cap)
{
	if (!arc) return 0;

	s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *name = modArchiveGetEntryName(arc, i);
		if (modmgrArchiveEntryHasForbiddenBinPayload(name)) {
			if (out_name && out_name_cap > 0) {
				strncpy(out_name, name, out_name_cap - 1);
				out_name[out_name_cap - 1] = '\0';
			}
			if (out_kind && out_kind_cap > 0) {
				strncpy(out_kind, "authored .bin", out_kind_cap - 1);
				out_kind[out_kind_cap - 1] = '\0';
			}
			return 1;
		}
		if (modmgrArchiveEntryHasForbiddenPublicTsvPayload(name)) {
			if (out_name && out_name_cap > 0) {
				strncpy(out_name, name, out_name_cap - 1);
				out_name[out_name_cap - 1] = '\0';
			}
			if (out_kind && out_kind_cap > 0) {
				strncpy(out_kind, "public .tsv", out_kind_cap - 1);
				out_kind[out_kind_cap - 1] = '\0';
			}
			return 1;
		}

		if (modmgrArchiveEntryIsTypedPdAssetArchive(name)) {
			u32 nested_size = 0;
			void *nested = modArchiveExtractAlloc(arc, i, &nested_size);
			if (!nested) {
				continue;
			}
			char policyErr[256];
			if (assetArchiveValidateBytes(nested, nested_size, name,
					ASSET_ARCHIVE_VALIDATE_RELEASE,
					policyErr, sizeof(policyErr)) != 0) {
				if (out_name && out_name_cap > 0) {
					snprintf(out_name, out_name_cap, "%s: %s",
						name, policyErr[0] ? policyErr : "typed asset archive validation failed");
					out_name[out_name_cap - 1] = '\0';
				}
				if (out_kind && out_kind_cap > 0) {
					strncpy(out_kind, "invalid typed archive", out_kind_cap - 1);
					out_kind[out_kind_cap - 1] = '\0';
				}
				free(nested);
				return 1;
			}
			char nested_bad[FS_MAXPATH + 1];
			s32 has_nested_bin = modArchiveMemFindForbiddenBinPayload(
				nested, nested_size, nested_bad, sizeof(nested_bad));
			free(nested);
			if (has_nested_bin) {
				if (out_name && out_name_cap > 0) {
					snprintf(out_name, out_name_cap, "%s::%s",
						name, nested_bad);
					out_name[out_name_cap - 1] = '\0';
				}
				if (out_kind && out_kind_cap > 0) {
					strncpy(out_kind, "authored .bin", out_kind_cap - 1);
					out_kind[out_kind_cap - 1] = '\0';
				}
				return 1;
			}
		}
	}

	return 0;
}

static void modmgrSetError(char *out_error, s32 error_len, const char *fmt, ...)
{
	if (!out_error || error_len <= 0) return;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(out_error, (size_t)error_len, fmt, ap);
	va_end(ap);
	out_error[error_len - 1] = '\0';
}

static const char *modmgrPathLeaf(const char *path)
{
	const char *leaf = path ? path : "";
	const char *slash = strrchr(leaf, '/');
	if (slash && slash + 1 > leaf) leaf = slash + 1;
	slash = strrchr(leaf, '\\');
	if (slash && slash + 1 > leaf) leaf = slash + 1;
	return leaf;
}

static void modmgrSanitizeInstallFilename(const char *src, char *out, u32 outsize)
{
	if (!out || outsize == 0) return;
	const char *base = modmgrPathLeaf(src);
	if (!base || !base[0]) base = "imported.pdmod";

	u32 i = 0;
	while (base[i] && i + 1 < outsize) {
		char c = base[i];
		if (c == '/' || c == '\\' || c == ':' || c == '?' ||
		    c == '*' || c == '<' || c == '>' || c == '|' || c == '"') {
			out[i] = '_';
		} else {
			out[i] = c;
		}
		i++;
	}
	out[i] = '\0';
	if (i == 0) snprintf(out, outsize, "imported.pdmod");
	if (!modmgrHasArchiveExtension(out)) {
		strncat(out, MODMGR_PDMOD_EXT, outsize - strlen(out) - 1);
	}
}

static s32 modmgrCharEqPathNoCase(char a, char b)
{
	if (a == '\\') a = '/';
	if (b == '\\') b = '/';
	return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static s32 modmgrPathEqualsNoCase(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		if (!modmgrCharEqPathNoCase(*a, *b)) return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static s32 modmgrCopyFileAtomic(const char *src, const char *dst)
{
	char tmp[FS_MAXPATH + 1];
	snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
	remove(tmp);

	FILE *in = fopen(src, "rb");
	if (!in) return 0;
	FILE *out = fopen(tmp, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}

	u8 buf[8192];
	s32 ok = 1;
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			ok = 0;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) ok = 0;
			break;
		}
	}

	if (fclose(out) != 0) ok = 0;
	fclose(in);

	if (!ok) {
		remove(tmp);
		return 0;
	}

	remove(dst);
	if (rename(tmp, dst) != 0) {
		remove(tmp);
		return 0;
	}
	return 1;
}

s32 modmgrValidateArchiveFile(const char *archive_path,
                              char *out_error,
                              s32 error_len)
{
	if (out_error && error_len > 0) out_error[0] = '\0';
	if (!archive_path || !archive_path[0]) {
		modmgrSetError(out_error, error_len, "No .pdmod archive path was provided");
		return 0;
	}
	if (!modmgrHasPdmodExtension(archive_path)) {
		modmgrSetError(out_error, error_len, "Expected a .pdmod archive");
		return 0;
	}

	mod_archive_t *arc = modArchiveOpen(archive_path);
	if (!arc) {
		modmgrSetError(out_error, error_len,
			"Archive could not be opened (err=%d)", modArchiveLastError());
		return 0;
	}

	u32 mfst_size = 0;
	char *mfst = modArchiveReadManifest(arc, &mfst_size);
	if (!mfst) {
		modmgrSetError(out_error, error_len, "Archive is missing root mod.json");
		modArchiveClose(arc);
		return 0;
	}
	free(mfst);
	(void)mfst_size;

	char badEntry[FS_MAXPATH + 1];
	char badKind[64];
	if (modmgrArchiveFindForbiddenPublicPayload(arc, badEntry,
			sizeof(badEntry), badKind, sizeof(badKind))) {
		modmgrSetError(out_error, error_len,
			"External-format archives cannot contain %s payloads: %s",
			badKind, badEntry);
		modArchiveClose(arc);
		return 0;
	}

	modArchiveClose(arc);
	return 1;
}

/* Try to register a single archive at `archivePath`. The archive is opened,
 * its root mod.json is decompressed and parsed, then closed (the mounted
 * handle is reopened during modmgrLoadMod when the mod is enabled).
 *
 * Plain .zip is accepted only when its root mod.json is present.
 *
 * Returns 1 if a slot was consumed, 0 otherwise. */
static s32 modmgrTryRegisterArchive(const char *archivePath, const char *display_id,
                                     const char *source_tag, bool dedupe_by_id)
{
	if (g_ModRegistryCount >= MODMGR_MAX_MODS) return 0;
	if (!archivePath || !archivePath[0]) return 0;

	/* Defense-in-depth trust gate (M-1.6): refuse to register an archive
	 * whose path lives under a reserved top-level subdirectory of the
	 * scanned mods root (e.g. mods/shared/, mods/inbox/). The scan walker
	 * already skips these names; this is the second line so any future
	 * code path that calls this function directly cannot bypass the rule. */
	if (g_ModsDirPath[0] && !modmgrArchivePathIsTrusted(archivePath, g_ModsDirPath)) {
		sysLogPrintf(LOG_ERROR,
			"modmgr: REFUSING to register archive '%s' -- under reserved trust-gate directory; manual install required",
			archivePath);
		return 0;
	}

	mod_archive_t *arc = modArchiveOpen(archivePath);
	if (!arc) {
		sysLogPrintf(LOG_WARNING,
			"modmgr: archive '%s' could not be opened (err=%d) -- skipping",
			archivePath, modArchiveLastError());
		return 0;
	}

	u32 mfstSize = 0;
	char *mfstBuf = modArchiveReadManifest(arc, &mfstSize);
	if (!mfstBuf) {
		/* Plain .zip with no root mod.json -- not a mod, drop quietly. */
		sysLogPrintf(LOG_NOTE,
			"modmgr: archive '%s' has no root mod.json -- not a mod",
			archivePath);
		modArchiveClose(arc);
		return 0;
	}
	u8 manifestSha[SHA256_DIGEST_SIZE];
	sha256Hash((const u8 *)mfstBuf, mfstSize, manifestSha);

	/* Initialise mod entry. dirpath is not used for archives; archive_path
	 * is the canonical reference. */
	modinfo_t *mod = &g_ModRegistry[g_ModRegistryCount];
	memset(mod, 0, sizeof(modinfo_t));
	mod->is_archive = 1;
	strncpy(mod->archive_path, archivePath, FS_MAXPATH);
	mod->archive_path[FS_MAXPATH] = '\0';

	char manifestLabel[FS_MAXPATH + 16];
	snprintf(manifestLabel, sizeof(manifestLabel), "%s:mod.json", archivePath);

	bool ok = modmgrParseModJsonBuf(mod, mfstBuf, mfstSize, manifestLabel);
	free(mfstBuf);

	if (!ok) {
		/* Keep the entry visible in the registry so the UI can show the
		 * validation error -- mirrors the folder-mod behaviour. */
		strncpy(mod->id, display_id ? display_id : "", MODMGR_ID_LEN - 1);
		mod->id[MODMGR_ID_LEN - 1] = '\0';
		strncpy(mod->name, mod->id, MODMGR_NAME_LEN - 1);
		mod->name[MODMGR_NAME_LEN - 1] = '\0';
		mod->valid = false;
		snprintf(mod->validation_error, MODMGR_ERROR_LEN,
			"Malformed mod.json inside archive -- failed to parse");
		mod->has_modjson = false;
	}

	if (ok) {
		char badEntry[FS_MAXPATH + 1];
		char badKind[64];
		if (modmgrArchiveFindForbiddenPublicPayload(arc, badEntry,
				sizeof(badEntry), badKind, sizeof(badKind))) {
			mod->valid = false;
			snprintf(mod->validation_error, MODMGR_ERROR_LEN,
				"External-format archives cannot contain %s payloads: %s",
				badKind, badEntry);
			sysLogPrintf(LOG_ERROR,
				"modmgr: archive '%s' rejected for %s payload '%s'",
				archivePath, badKind, badEntry);
		}
	}

	modArchiveClose(arc);

	if (dedupe_by_id) {
		for (s32 k = 0; k < g_ModRegistryCount; k++) {
			if (strcmp(g_ModRegistry[k].id, mod->id) == 0) {
				memset(mod, 0, sizeof(*mod));
				return 0;
			}
		}
	}

	mod->bundled = 0;

	/* Content hash (CRC32 of id:version) for the network manifest. */
	char hashsrc[256];
	snprintf(hashsrc, sizeof(hashsrc), "%s:%s", mod->id, mod->version);
	mod->contenthash = modmgrHashString(hashsrc);

	/* SHA-256 over root mod.json bytes. Folder mods use the same boundary,
	 * so an authored folder and the .pdmod transport generated from it
	 * compare equal in manifest checks. File-transfer integrity remains
	 * owned by the file-transfer and distribution packet digests. */
	memcpy(mod->sha256, manifestSha, sizeof(mod->sha256));

	/* Size for download estimation: archive file size, not uncompressed bytes. */
	struct stat st;
	if (stat(archivePath, &st) == 0) {
		/* Windows MinGW may define off_t as signed 32-bit. Casting UINT32_MAX
		 * to off_t then comparing makes the limit -1, so every non-empty
		 * archive is falsely reported as 4 GiB. Widen the observed size before
		 * applying the public u32 storage boundary. */
		mod->size_bytes = (u32)((u64)st.st_size > 0xFFFFFFFFull
			? 0xFFFFFFFFu : (u64)st.st_size);
	}

	mod->enabled = 0;

	g_ModRegistryCount++;

	/* sha256 prefix is the integrity fingerprint of the bytes we just
	 * scanned. Echoing it makes mount events traceable from the log alone
	 * (defense-in-depth for the M-1.6 trust model). */
	const char *sha8 = modmgrShortSha256(mod->sha256, 8);

	if (source_tag) {
		sysLogPrintf(LOG_NOTE,
			"modmgr: discovered mod [%d] '%s' (%s) [.pdmod] from '%s' file=%s sha256=%s..",
			g_ModRegistryCount - 1, mod->id, mod->name, source_tag, archivePath, sha8);
	} else {
		sysLogPrintf(LOG_NOTE,
			"modmgr: discovered mod [%d] '%s' (%s) [.pdmod] file=%s sha256=%s..",
			g_ModRegistryCount - 1, mod->id, mod->name, archivePath, sha8);
	}
	return 1;
}

/* Iterate children of `catPath` (a category folder such as
 * `mods/UI Chrome/`). Each child entry is tried as a folder mod or as a
 * `.pdmod` / `.zip` archive. Non-matching entries are skipped. This
 * function does NOT recurse further -- depth is capped at one level under
 * root to prevent runaway directory walks on arbitrary user layouts. */
static void modmgrScanCategoryFolder(const char *catPath, const char *catName,
                                      bool dedupe_by_id)
{
	DIR *d = opendir(catPath);
	if (!d) return;

	sysLogPrintf(LOG_NOTE, "modmgr: entering category folder '%s'", catName);

	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && g_ModRegistryCount < MODMGR_MAX_MODS) {
		if (ent->d_name[0] == '.') continue;

		char subpath[FS_MAXPATH + 1];
		snprintf(subpath, sizeof(subpath), "%s/%s", catPath, ent->d_name);

		struct stat st;
		if (stat(subpath, &st) != 0) continue;

		if (S_ISDIR(st.st_mode)) {
			modmgrTryRegisterModEntry(subpath, ent->d_name, catName, dedupe_by_id);
		} else if (S_ISREG(st.st_mode) && modmgrHasArchiveExtension(ent->d_name)) {
			modmgrTryRegisterArchive(subpath, ent->d_name, catName, dedupe_by_id);
		}
	}

	closedir(d);
}

static void modmgrScanDirectory(void)
{
	g_ModRegistryCount = 0;

	// Ensure mods directory exists on fresh install
	fsCreateDir("./" MODMGR_MODS_DIR);

	// PC: multiple roots are valid depending on launch dir:
	// - $E/../mods (repo root when exe is in Build/)
	// - ./mods (current working directory)
	// - $E/mods (mods alongside exe)
	// - base-dir fallback (often data/mods)
	const char *modsdir = NULL;
	DIR *dir = NULL;
	char candidateBufs[4][512];
	const char *candidates[4];

	const char *explicitModsDir = fsGetModDir();

	/* B-1031: --moddir is an explicit isolation/override contract, not one
	 * more discovery candidate. fsInit has already resolved and validated it.
	 * The prior dynamic scanner ignored fsGetModDir() and still walked every
	 * executable/CWD/base root, which made two canonical-executable peers see
	 * each other's mods even with distinct base/save directories. */
	if (explicitModsDir && explicitModsDir[0]) {
		strncpy(candidateBufs[0], explicitModsDir, sizeof(candidateBufs[0]) - 1);
		candidateBufs[0][sizeof(candidateBufs[0]) - 1] = '\0';
		candidateBufs[1][0] = '\0';
		candidateBufs[2][0] = '\0';
		candidateBufs[3][0] = '\0';
	} else {
		fsFullPath("$E/../" MODMGR_MODS_DIR, candidateBufs[0], sizeof(candidateBufs[0]));
		strncpy(candidateBufs[1], "./" MODMGR_MODS_DIR, sizeof(candidateBufs[1]));
		candidateBufs[1][sizeof(candidateBufs[1]) - 1] = '\0';
		fsFullPath("$E/" MODMGR_MODS_DIR, candidateBufs[2], sizeof(candidateBufs[2]));
		fsFullPath(MODMGR_MODS_DIR,       candidateBufs[3], sizeof(candidateBufs[3]));
	}
	candidates[0] = candidateBufs[0];
	candidates[1] = candidateBufs[1];
	candidates[2] = candidateBufs[2];
	candidates[3] = candidateBufs[3];

	for (s32 i = 0; i < 4; i++) {
		if (!candidates[i][0]) continue;
		dir = opendir(candidates[i]);
		if (dir) {
			modsdir = candidates[i];
			break;
		}
	}

	if (!dir) {
		if (explicitModsDir && explicitModsDir[0]) {
			sysLogPrintf(LOG_WARNING,
				"modmgr: could not open explicit mod directory '%s'", explicitModsDir);
		} else {
			sysLogPrintf(LOG_WARNING, "modmgr: could not open mods directory (tried $E/../%s, ./%s, $E/%s, base/%s)",
				MODMGR_MODS_DIR, MODMGR_MODS_DIR, MODMGR_MODS_DIR, MODMGR_MODS_DIR);
		}
		return;
	}

	// Store resolved path for assetCatalogScanComponents() to use later
	strncpy(g_ModsDirPath, modsdir, sizeof(g_ModsDirPath) - 1);
	g_ModsDirPath[sizeof(g_ModsDirPath) - 1] = '\0';

	/* Priority M / B-238 / M-4.1: one-shot folder->.pdmod migration. Runs
	 * before the iteration loop below so the scan picks up freshly-created
	 * .pdmod files in the same pass. Sentinel-guarded; subsequent launches
	 * skip without rescan. modMigrateRun is idempotent and safe to call
	 * unconditionally. */
	{
		mod_migrate_summary_t mig = { 0 };
		modMigrateRun(modsdir, &mig);
	}

	sysLogPrintf(LOG_NOTE, "modmgr: scanning '%s' for mods...", modsdir);

	/* Root pass: try each top-level entry as a mod. Order:
	 *   1. Skip dotfiles and reserved trust-gate names (`shared`, `inbox`,
	 *      `untrusted`) per design Section 8 -- these are read-only-for-
	 *      browsing and must NEVER auto-mount.
	 *   2. Files: candidate `.pdmod` / `.zip` archives.
	 *   3. Directories with a manifest: folder mod (legacy path).
	 *   4. Directories without a manifest: treat as category folder, scan
	 *      one level deeper for archives + folder mods.
	 * Depth is capped at one level under root to prevent runaway walks. */
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL && g_ModRegistryCount < MODMGR_MAX_MODS) {
		if (ent->d_name[0] == '.') continue;
		if (modmgrIsReservedTopLevel(ent->d_name)) {
			sysLogPrintf(LOG_NOTE,
				"modmgr: skipping reserved top-level '%s' (read-only inbox)",
				ent->d_name);
			continue;
		}

		/* M-4.1 follow-up: silently skip .legacy_backup folders at the top
		 * level. modmgrTryRegisterModEntry already refuses these as mods,
		 * but without this skip the scanner falls through to
		 * modmgrScanCategoryFolder which walks INTO the backup folder
		 * looking for child mods, producing noisy log lines for what is
		 * by design an inert safety copy. */
		{
			size_t leafLen = strlen(ent->d_name);
			const char *suffix = ".legacy_backup";
			size_t suffixLen = strlen(suffix);
			if (leafLen > suffixLen &&
			    strcmp(ent->d_name + leafLen - suffixLen, suffix) == 0) {
				continue;
			}
		}

		char fullpath[FS_MAXPATH + 1];
		snprintf(fullpath, sizeof(fullpath), "%s/%s", modsdir, ent->d_name);

		struct stat st;
		if (stat(fullpath, &st) != 0) continue;

		if (S_ISREG(st.st_mode)) {
			if (modmgrHasArchiveExtension(ent->d_name)) {
				modmgrTryRegisterArchive(fullpath, ent->d_name, NULL, false);
			}
			continue;
		}
		if (!S_ISDIR(st.st_mode)) continue;

		if (modmgrTryRegisterModEntry(fullpath, ent->d_name, NULL, false)) {
			continue;
		}
		if (modmgrDirHasDirectManifest(fullpath)) {
			continue;
		}

		/* Entry is a directory with no manifest -- treat as category. */
		modmgrScanCategoryFolder(fullpath, ent->d_name, false);
	}

	closedir(dir);

	/* Scan remaining candidate directories for mods not in the primary dir.
	 * This catches mods placed in data/mods/ when ./mods/ was the primary,
	 * or vice versa. Duplicate mod IDs are skipped. */
	for (s32 ci = 0; ci < 4 && g_ModRegistryCount < MODMGR_MAX_MODS; ci++) {
		if (!candidates[ci][0]) continue;
		if (strcmp(candidates[ci], modsdir) == 0) continue; /* skip primary */

		DIR *altdir = opendir(candidates[ci]);
		if (!altdir) continue;

		sysLogPrintf(LOG_NOTE, "modmgr: also scanning '%s'", candidates[ci]);

		struct dirent *altent;
		while ((altent = readdir(altdir)) != NULL && g_ModRegistryCount < MODMGR_MAX_MODS) {
			if (altent->d_name[0] == '.') continue;
			if (modmgrIsReservedTopLevel(altent->d_name)) continue;

			/* M-4.1 follow-up: same .legacy_backup skip as the primary
			 * scan so the alt candidates do not produce noisy walk
			 * logs for migration safety folders either. */
			{
				size_t altLen = strlen(altent->d_name);
				const char *suffix = ".legacy_backup";
				size_t suffixLen = strlen(suffix);
				if (altLen > suffixLen &&
				    strcmp(altent->d_name + altLen - suffixLen, suffix) == 0) {
					continue;
				}
			}

			char altpath[FS_MAXPATH + 1];
			snprintf(altpath, sizeof(altpath), "%s/%s", candidates[ci], altent->d_name);

			struct stat altst;
			if (stat(altpath, &altst) != 0) continue;

			if (S_ISREG(altst.st_mode)) {
				if (modmgrHasArchiveExtension(altent->d_name)) {
					modmgrTryRegisterArchive(altpath, altent->d_name,
					                          candidates[ci], true);
				}
				continue;
			}
			if (!S_ISDIR(altst.st_mode)) continue;

			/* Try as mod first; if that fails (no manifest), try as category. */
			if (modmgrTryRegisterModEntry(altpath, altent->d_name,
			                              candidates[ci], true)) {
				continue;
			}
			if (modmgrDirHasDirectManifest(altpath)) {
				continue;
			}
			modmgrScanCategoryFolder(altpath, altent->d_name, true);
		}

		closedir(altdir);
	}

	sysLogPrintf(LOG_NOTE, "modmgr: scan complete -- %d mods found", g_ModRegistryCount);

	/* M-1.6: surface any browsed-but-not-mounted shared inbox so the
	 * operator sees the trust gate is doing its job. Cheap one-shot. */
	modmgrLogSharedInbox(g_ModsDirPath);
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
		if (!g_ModRegistry[i].session_only) {
			g_ModRegistry[i].enabled = false;
		}
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
		if (g_ModRegistry[i].enabled && !g_ModRegistry[i].session_only) {
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

// ---------------------------------------------------------------------------
// mods-enabled.json persistence (ordered JSON array)
// ---------------------------------------------------------------------------
// File format: ["mod_id_1", "mod_id_2", ...]
// Load order = array order. Falls back to comma-separated config string.

#define MODS_ENABLED_JSON_PATH "$S/mods-enabled.json"

static void modmgrSaveModsEnabledJson(void)
{
	char pathBuf[FS_MAXPATH + 1];
	const char *path = fsFullPath(MODS_ENABLED_JSON_PATH, pathBuf, sizeof(pathBuf));
	if (!path) return;

	FILE *f = fopen(path, "w");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "modmgr: could not write %s", path);
		return;
	}

	fprintf(f, "[\n");
	bool first = true;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled && !g_ModRegistry[i].session_only) {
			if (!first) fprintf(f, ",\n");
			fprintf(f, "  \"%s\"", g_ModRegistry[i].id);
			first = false;
		}
	}
	if (!first) fprintf(f, "\n");
	fprintf(f, "]\n");
	fclose(f);

	sysLogPrintf(LOG_NOTE, "modmgr: saved mods-enabled.json");
}

static bool modmgrLoadModsEnabledJson(void)
{
	char pathBuf[FS_MAXPATH + 1];
	const char *path = fsFullPath(MODS_ENABLED_JSON_PATH, pathBuf, sizeof(pathBuf));
	if (!path) return false;

	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
		return false;
	}

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) return false;

	char *buf = (char *)malloc(filesize + 1);
	if (!buf) { free(data); return false; }
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	// Disable all mods first
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (!g_ModRegistry[i].session_only) {
			g_ModRegistry[i].enabled = false;
		}
	}

	// Parse the JSON array
	jparse_t j;
	j.src = buf;
	j.pos = buf;

	jtok_t tok = json_next(&j);
	if (tok.type != JTOK_LBRACKET) {
		free(buf);
		return false;
	}

	s32 order = 0;
	while (1) {
		tok = json_next(&j);
		if (tok.type == JTOK_RBRACKET || tok.type == JTOK_EOF) break;
		if (tok.type == JTOK_COMMA) continue;
		if (tok.type == JTOK_STRING) {
			char modid[MODMGR_ID_LEN];
			json_tok_string(&tok, modid, MODMGR_ID_LEN);
			modinfo_t *mod = modmgrFindMod(modid);
			if (mod) {
				mod->enabled = true;
			} else {
				sysLogPrintf(LOG_WARNING, "modmgr: mods-enabled.json references unknown mod '%s'", modid);
			}
		}
	}

	free(buf);
	sysLogPrintf(LOG_NOTE, "modmgr: loaded mods-enabled.json");
	return true;
}

void modmgrSaveConfig(void)
{
	// Save to both formats: JSON (primary) and config string (fallback)
	modmgrSaveModsEnabledJson();
	modmgrBuildEnabledList();
	configSave(CONFIG_PATH);
	if (prefsAgentGetActive()[0] && prefsAgentSave() != 0) {
		sysLogPrintf(LOG_WARNING,
			"modmgr: active Agent Profile did not persist the enabled-mod change");
	}
	sysLogPrintf(LOG_NOTE, "modmgr: saved config — enabled mods: %s",
		g_ModEnabledList[0] ? g_ModEnabledList : "(none)");
}

void modmgrLoadConfig(void)
{
	// Try mods-enabled.json first (primary, ordered)
	if (modmgrLoadModsEnabledJson()) {
		return;
	}

	// Fallback: comma-separated config string from pd.ini
	modmgrParseEnabledList();
}

// ---------------------------------------------------------------------------
// Mod loading (register assets from a single mod)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Audio ini mod loading — re-parse audio.ini and register catalog entry
// ---------------------------------------------------------------------------

static void modmgrLoadAudioIni(modinfo_t *mod)
{
	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/audio.ini", mod->dirpath);

	FILE *f = fopen(path, "r");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "modmgr: could not open %s for loading", path);
		return;
	}

	char name[MODMGR_NAME_LEN] = "";
	char file_path[FS_MAXPATH] = "";
	s32  category = 1;
	s32  duration_ms = 0;
	bool in_audio_section = false;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		s32 len = (s32)strlen(line);
		while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' ||
		       line[len-1] == ' ' || line[len-1] == '\t')) {
			line[--len] = '\0';
		}
		if (len == 0 || line[0] == '#' || line[0] == ';') continue;

		if (line[0] == '[') {
			in_audio_section = (strncmp(line, "[audio]", 7) == 0);
			continue;
		}
		if (!in_audio_section) continue;

		char *eq = strchr(line, '=');
		if (!eq) continue;

		char *kstart = line;
		while (*kstart == ' ' || *kstart == '\t') kstart++;
		char *kend = eq - 1;
		while (kend > kstart && (*kend == ' ' || *kend == '\t')) kend--;
		kend[1] = '\0';

		char *vstart = eq + 1;
		while (*vstart == ' ' || *vstart == '\t') vstart++;

		if (strcmp(kstart, "name") == 0) {
			strncpy(name, vstart, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
		} else if (strcmp(kstart, "category") == 0) {
			category = modmgrParseAudioCategoryValue(vstart, category);
		} else if (strcmp(kstart, "duration_ms") == 0) {
			duration_ms = atoi(vstart);
		} else if (strcmp(kstart, "file_path") == 0) {
			strncpy(file_path, vstart, sizeof(file_path) - 1);
			file_path[sizeof(file_path) - 1] = '\0';
		}
	}
	fclose(f);

	if (name[0] == '\0' || file_path[0] == '\0') {
		sysLogPrintf(LOG_WARNING, "modmgr: audio.ini in '%s' missing name or file_path", mod->dirpath);
		return;
	}

	/* Build catalog ID: <mod_id>:audio */
	char catalogId[MODMGR_ID_LEN];
	snprintf(catalogId, sizeof(catalogId), "%s:audio", mod->id);

	/* Build file path relative to mods dir */
	char relPath[FS_MAXPATH];
	snprintf(relPath, sizeof(relPath), "%s/%s", mod->dirpath, file_path);

	/* Register in asset catalog */
	asset_entry_t *e = assetCatalogRegisterAudio(
		catalogId, 0, name, category, duration_ms, relPath);
	if (e) {
		e->bundled = 0;
		e->enabled = 1;
		strncpy(e->dirpath, mod->dirpath, FS_MAXPATH - 1);
		e->dirpath[FS_MAXPATH - 1] = '\0';
	}

	sysLogPrintf(LOG_NOTE, "modmgr: registered audio mod '%s' -> %s (%s) category=%d",
		name, catalogId, relPath, category);
}

static bool modmgrLoadMod(modinfo_t *mod)
{
	if (mod->loaded) return true;
	if (!mod->valid) return false; // Don't load invalid mods

	if (mod->is_archive) {
		/* Priority M / B-238: archive load path. Reopen the archive (it was
		 * closed after the scan-time manifest read), keep the handle on
		 * mod->archive_handle for the duration of the load, then read mod.json
		 * once into a buffer that drives both content registration and bot
		 * name parsing. The handle stays open so the M-1.3 VFS layer can
		 * resolve asset path requests against this mount. */
		sysLogPrintf(LOG_NOTE, "modmgr: loading mod '%s' from archive '%s' sha256=%s..",
			mod->id, mod->archive_path,
			modmgrShortSha256(mod->sha256, 8));

		if (mod->archive_handle) {
			modArchiveClose(mod->archive_handle);
			mod->archive_handle = NULL;
		}
		mod->archive_handle = modArchiveOpen(mod->archive_path);
		if (!mod->archive_handle) {
			sysLogPrintf(LOG_WARNING,
				"modmgr: archive '%s' would not reopen during load (err=%d) -- mod skipped",
				mod->archive_path, modArchiveLastError());
			return false;
		}

		u32 mfstSize = 0;
		char *mfstBuf = modArchiveReadManifest(mod->archive_handle, &mfstSize);
		if (mfstBuf) {
			modmgrRegisterModJsonContentBuf(mod, mfstBuf, mfstSize);
			modmgrParseBotNamesBuf(mod, mfstBuf, mfstSize);
			free(mfstBuf);
		}

		/* Priority M / B-238 (M-1.3): register the open archive with the
		 * VFS layer so fsFileLoad / fsFileSize can resolve asset paths
		 * inside it. The handle stays owned by modmgr; unload path will
		 * call modVfsUnmount + modArchiveClose in modmgrUnloadAllMods. */
		modVfsMount(mod->id, mod->archive_handle);

		/* External-format .pdmod pipeline: archive component INIs are the
		 * same authoring surface as loose folder _components, but paths stay
		 * archive-relative so fsFileLoad resolves them through the VFS mount. */
		assetCatalogScanComponentsFromArchive(mod->id, mod->archive_handle);

		mod->loaded = true;
		return true;
	}

	sysLogPrintf(LOG_NOTE, "modmgr: loading mod '%s' from %s", mod->id, mod->dirpath);

	if (mod->has_audioini) {
		// Audio ini mod: register audio entry in catalog
		modmgrLoadAudioIni(mod);
	} else {
		s32 scan_result = assetCatalogScanExternalLayoutFolder(mod->id, mod->dirpath);
		if (scan_result < 0) {
			sysLogPrintf(LOG_WARNING,
				"modmgr: folder package '%s' failed transactional catalog admission (%d)",
				mod->id, scan_result);
			return false;
		}
		// D3b: Register mod.json content sections (bodies, heads, arenas) into catalog.
		// Legacy _components content is handled by assetCatalogScanComponents().
		modmgrRegisterModJsonContent(mod);
	}

	// P2: Parse bot name overrides if this mod has them
	modmgrParseBotNames(mod);

	mod->loaded = true;
	return true;
}

static void modmgrUnloadAllMods(void)
{
	/* Priority M / B-238: drop every VFS mount in one shot before closing
	 * archive handles. modVfsUnmountAll iterates internally and frees per-
	 * mount cache entries; doing this BEFORE modArchiveClose avoids any
	 * brief window where a cached entry's mount has a dangling archive. */
	modVfsUnmountAll();

	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		modinfo_t *mod = &g_ModRegistry[i];
		mod->loaded = false;
		if (mod->archive_handle) {
			modArchiveClose(mod->archive_handle);
			mod->archive_handle = NULL;
		}
	}

	modmgrClearBotNames();
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

/* Priority M / B-238: cache cap for the in-memory VFS asset cache, in MiB.
 * Zero is allowed (effectively disables the cache, every read decompresses
 * fresh). 256 MiB is the design 4.4 default. */
static s32 g_ModAssetCacheMB = 256;

PD_CONSTRUCTOR static void modmgrConfigInit(void)
{
	configRegisterString("Mods.EnabledMods", g_ModEnabledList, sizeof(g_ModEnabledList));
	configRegisterInt("Mods.SizeThresholdMB", &g_ModSizeThresholdMB, 0, 10000);
	configRegisterInt("Mods.AssetCacheMB", &g_ModAssetCacheMB, 0, 16384);
}

void modmgrInit(void)
{
	if (g_ModManagerInitialized) return;

	sysLogPrintf(LOG_NOTE, "modmgr: initializing...");

	/* Priority M / B-238: bring up the VFS layer + apply the configured
	 * asset cache cap. Idempotent so repeated init calls (e.g. during
	 * tests) are safe. */
	modVfsInit();
	modVfsSetCacheCapMB(g_ModAssetCacheMB);

	// Scan for mods
	modmgrScanDirectory();

	// Sort registry
	if (g_ModRegistryCount > 1) {
		qsort(g_ModRegistry, g_ModRegistryCount, sizeof(modinfo_t), modmgrCompare);
	}

	// Apply config (enable/disable based on saved preferences)
	modmgrLoadConfig();

	// Load all enabled mods + all audio-only mods.
	// Audio mods (audio.ini) are non-gameplay-affecting; their tracks must be
	// browseable in the music menu regardless of the "enabled" flag, which
	// gates gameplay mods (skins, arenas, mod.json packs) only.
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled || g_ModRegistry[i].has_audioini) {
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
	modmgrRebuildCatalogFromCurrentSelection();

	g_ModDirty = false;

	sysLogPrintf(LOG_NOTE, "modmgr: reload complete");

	// Invalidate catalog-backed caches so accessors pick up new state
	modmgrCatalogChanged();

	videoResetTextureCache();
	mainChangeToStage(MODMGR_STAGE_TITLE);
}

void modmgrRescanDirectory(void)
{
	// Snapshot {id, enabled, loaded} of currently-known mods so we can
	// restore per-entry state after the rescan wipes g_ModRegistry. This
	// preserves in-memory edits the user hasn't applied yet (pending
	// enable/disable toggles) and avoids re-loading catalog content that
	// already lives there.
	struct {
		char id[MODMGR_ID_LEN];
		s32  enabled;
		s32  loaded;
	} saved[MODMGR_MAX_MODS];
	modinfo_t sessionSaved[MODMGR_MAX_MODS];
	s32 numSessionSaved = 0;
	s32 numSaved = g_ModRegistryCount;
	if (numSaved > MODMGR_MAX_MODS) numSaved = MODMGR_MAX_MODS;
	for (s32 i = 0; i < numSaved; i++) {
		strncpy(saved[i].id, g_ModRegistry[i].id, MODMGR_ID_LEN - 1);
		saved[i].id[MODMGR_ID_LEN - 1] = '\0';
		saved[i].enabled = g_ModRegistry[i].enabled;
		saved[i].loaded  = g_ModRegistry[i].loaded;
		if (g_ModRegistry[i].session_only &&
				numSessionSaved < MODMGR_MAX_MODS) {
			sessionSaved[numSessionSaved++] = g_ModRegistry[i];
		}
	}

	sysLogPrintf(LOG_NOTE, "modmgr: rescanning mods/ (was %d mods)", numSaved);

	// Re-scan from disk — resets g_ModRegistryCount to 0 and re-parses every
	// mod.json / audio.ini under the mods roots.
	modmgrScanDirectory();

	/* The normal scan intentionally ignores mods/.temp. Re-append validated
	 * ready-gate packages so an unrelated disk rescan cannot erase the active
	 * session's manifest authority. A real installed package with the same ID
	 * wins; the later hash check will still reject a mismatched manifest. */
	for (s32 i = 0; i < numSessionSaved &&
			g_ModRegistryCount < MODMGR_MAX_MODS; i++) {
		s32 duplicate = 0;
		for (s32 j = 0; j < g_ModRegistryCount; j++) {
			if (strcmp(g_ModRegistry[j].id, sessionSaved[i].id) == 0) {
				duplicate = 1;
				break;
			}
		}
		if (!duplicate) {
			g_ModRegistry[g_ModRegistryCount++] = sessionSaved[i];
		}
	}

	// Keep list ordering stable after rescan.
	if (g_ModRegistryCount > 1) {
		qsort(g_ModRegistry, g_ModRegistryCount, sizeof(modinfo_t), modmgrCompare);
	}

	// Restore enabled/loaded for entries that existed before the rescan.
	// Newly-discovered entries keep scanDirectory's defaults (enabled=0,
	// loaded=0); caller can flip them via modmgrSetEnabled + modmgrSaveConfig.
	//
	// Priority M / B-238: archive mods carry an open FILE handle on
	// mod->archive_handle when loaded=1. The pre-rescan handle was closed
	// at the top of modmgrScanDirectory() (memset of every entry); a stale
	// loaded=1 with a NULL handle would mis-state the unload path. So
	// archive mods always come back as loaded=0 after rescan and are
	// brought back live by the next modmgrLoadMod() pass.
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		for (s32 j = 0; j < numSaved; j++) {
			if (strcmp(g_ModRegistry[i].id, saved[j].id) == 0) {
				g_ModRegistry[i].enabled = saved[j].enabled;
				g_ModRegistry[i].loaded  =
					(g_ModRegistry[i].is_archive ? 0 : saved[j].loaded);
				break;
			}
		}
	}

	sysLogPrintf(LOG_NOTE, "modmgr: rescan complete (now %d mods)", g_ModRegistryCount);
}

static void modmgrRebuildCatalogFromCurrentSelection(void)
{
	s32 enabledCount = 0;

	// C-8: Rebuild catalog with the new enabled mod set.
	// assetCatalogClearMods() removes all non-bundled (mod) entries from the
	// catalog, then re-scanning repopulates them for the currently enabled mods.
	// catalogLoadInit() then rebuilds reverse-index arrays so C-4/C-5/C-6/C-7
	// intercepts reflect the updated mod state immediately.
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
	// modmgrSaveComponentState during apply).
	modmgrLoadComponentState();

	// assetCatalogClearMods() removes catalog entries, but not mod->loaded flags.
	// Reset them so enabled audio/mod.json packages are re-registered in this
	// rebuild pass (without requiring a full modmgrUnloadAllMods()).
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		g_ModRegistry[i].loaded = false;
	}

	// Re-register enabled manifest mods + all audio-only mods so theme/audio
	// content is present in the catalog for the next reverse-index build.
	// Audio mods always re-register (see modmgrInit rationale).
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].enabled || g_ModRegistry[i].has_audioini) {
			modmgrLoadMod(&g_ModRegistry[i]);
			if (g_ModRegistry[i].enabled) enabledCount++;
		}
	}

	// Rebuild reverse-index arrays after all mod entries are present.
	catalogLoadInit();
	/* B-1009: these caches store pointers into catalog row IDs. ClearMods
	 * invalidates those addresses, so rebuild every runtime-to-ID cache before
	 * any consumer can resolve a removed or reused mod row. */
	catalogBuildRuntimeCaches();
	sysLogPrintf(LOG_NOTE, "MOD: catalog rebuild complete — %d total entries (%d enabled mod package(s))",
	             assetCatalogGetCount(), enabledCount);
}

void modmgrSyncCatalogToRegistry(void)
{
	/* B-214: after disk deletes / rescans, rebuild mod catalog entries +
	 * C-4 intercepts so UI and loaders drop removed packages immediately. */
	modmgrRebuildCatalogFromCurrentSelection();
	modmgrCatalogChanged();
	videoResetTextureCache();
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

s32 modmgrRegisterSessionFolder(const char *dirpath, const char *expected_id,
		const u8 expected_sha256[SHA256_DIGEST_SIZE])
{
	static const u8 zero_sha[SHA256_DIGEST_SIZE] = {0};
	if (!dirpath || !dirpath[0] || !expected_id || !expected_id[0]
			|| !expected_sha256
			|| memcmp(expected_sha256, zero_sha, sizeof(zero_sha)) == 0) {
		return 0;
	}

	modinfo_t *existing = modmgrFindMod(expected_id);
	if (existing) {
		if (existing->enabled && existing->valid && existing->has_modjson
				&& memcmp(existing->sha256, expected_sha256,
					SHA256_DIGEST_SIZE) == 0) {
			return 1;
		}
		sysLogPrintf(LOG_WARNING,
			"modmgr: session package '%s' conflicts with an existing registry entry",
			expected_id);
		return 0;
	}

	s32 prior_count = g_ModRegistryCount;
	if (!modmgrTryRegisterModEntry(dirpath, expected_id,
			"network-session", false)
			|| g_ModRegistryCount != prior_count + 1) {
		return 0;
	}

	modinfo_t *mod = &g_ModRegistry[prior_count];
	if (!mod->valid || !mod->has_modjson
			|| strcmp(mod->id, expected_id) != 0
			|| memcmp(mod->sha256, expected_sha256,
				SHA256_DIGEST_SIZE) != 0) {
		char expected_hex[SHA256_HEX_SIZE];
		char actual_hex[SHA256_HEX_SIZE];
		sha256ToHex(expected_sha256, expected_hex);
		sha256ToHex(mod->sha256, actual_hex);
		sysLogPrintf(LOG_WARNING,
			"modmgr: rejected session package expected=%s/%s actual=%s/%s valid=%d manifest=%d",
			expected_id, expected_hex, mod->id, actual_hex,
			mod->valid, mod->has_modjson);
		memset(mod, 0, sizeof(*mod));
		g_ModRegistryCount = prior_count;
		return 0;
	}

	mod->session_only = 1;
	mod->enabled = 1;
	if (!modmgrLoadMod(mod) || !mod->loaded) {
		sysLogPrintf(LOG_WARNING,
			"modmgr: session package '%s' passed identity but failed runtime loading",
			expected_id);
		memset(mod, 0, sizeof(*mod));
		g_ModRegistryCount = prior_count;
		return 0;
	}

	sysLogPrintf(LOG_NOTE,
		"modmgr: admitted session-only package '%s' from %s",
		expected_id, dirpath);
	return 1;
}

s32 modmgrRetireSessionContent(void)
{
	s32 session_count = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].session_only) session_count++;
	}

	/* Temporary loose/typed rows are not represented in g_ModRegistry, so the
	 * catalog rebuild is required even when there is no session package. Drop
	 * all VFS mounts first, compact only the session-owned registry rows, then
	 * let the existing all-family reset transaction rebuild installed state. */
	modmgrUnloadAllMods();
	if (session_count > 0) {
		s32 write = 0;
		for (s32 read = 0; read < g_ModRegistryCount; read++) {
			if (g_ModRegistry[read].session_only) continue;
			if (write != read) g_ModRegistry[write] = g_ModRegistry[read];
			write++;
		}
		memset(&g_ModRegistry[write], 0,
			(size_t)(g_ModRegistryCount - write) * sizeof(g_ModRegistry[0]));
		g_ModRegistryCount = write;
	}

	modmgrRebuildCatalogFromCurrentSelection();
	modmgrCatalogChanged();
	videoResetTextureCache();
	sysLogPrintf(LOG_NOTE,
		"modmgr: retired %d session package(s) and rebuilt installed catalog",
		session_count);
	return 1;
}

// ---------------------------------------------------------------------------
// Public API: Enable/Disable
// ---------------------------------------------------------------------------

void modmgrSetEnabled(s32 index, s32 enabled)
{
	if (index < 0 || index >= g_ModRegistryCount) return;
	/* Session packages are owned by the active network manifest, not by the
	 * user's persisted Mod Manager selection. */
	if (g_ModRegistry[index].session_only) return;
	// Cannot enable invalid mods
	if (enabled && !g_ModRegistry[index].valid) return;
	if (g_ModRegistry[index].enabled != enabled) {
		g_ModRegistry[index].enabled = enabled;
		g_ModDirty = true;
	}
}

s32 modmgrIsDirty(void)
{
	return g_ModDirty;
}

s32 modmgrCheckDependencies(s32 index, char *missing, s32 misslen)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	modinfo_t *mod = &g_ModRegistry[index];
	if (mod->num_dependencies <= 0) return 0;

	s32 missingCount = 0;
	s32 misspos = 0;
	if (missing && misslen > 0) missing[0] = '\0';

	for (s32 d = 0; d < mod->num_dependencies; d++) {
		const char *depid = mod->dependencies[d];
		if (depid[0] == '\0') continue;

		modinfo_t *dep = modmgrFindMod(depid);
		if (!dep || !dep->enabled) {
			missingCount++;
			if (missing && misslen > 0) {
				s32 idlen = (s32)strlen(depid);
				s32 needed = idlen + (misspos > 0 ? 2 : 0); // ", " + id
				if (misspos + needed < misslen - 1) {
					if (misspos > 0) {
						missing[misspos++] = ',';
						missing[misspos++] = ' ';
					}
					memcpy(&missing[misspos], depid, idlen);
					misspos += idlen;
					missing[misspos] = '\0';
				}
			}
		}
	}

	return missingCount;
}

void modmgrSwapOrder(s32 indexA, s32 indexB)
{
	if (indexA < 0 || indexA >= g_ModRegistryCount) return;
	if (indexB < 0 || indexB >= g_ModRegistryCount) return;
	if (indexA == indexB) return;

	modinfo_t tmp;
	memcpy(&tmp, &g_ModRegistry[indexA], sizeof(modinfo_t));
	memcpy(&g_ModRegistry[indexA], &g_ModRegistry[indexB], sizeof(modinfo_t));
	memcpy(&g_ModRegistry[indexB], &tmp, sizeof(modinfo_t));
	g_ModDirty = true;
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
		ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
		ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL,
		ASSET_ANIMATION,
		ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
		ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
		ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
		ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
		ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_TOOL
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

static void modmgrApplyChangesInternal(s32 persist_machine_state)
{
	sysLogPrintf(LOG_NOTE, "modmgr: applying changes mode=%s...",
		persist_machine_state ? "persistent" : "transient");

	/* Priority M / B-238 (M-1.5): hot-reload state machine.
	 *
	 * Snapshot enabled (user intent) and loaded (current runtime state).
	 * For mods with requires_restart=true whose intent diverges from the
	 * current loaded state, defer the live transition until next launch:
	 *   - The user's intent is persisted to config (so next launch picks
	 *     it up).
	 *   - The mod->enabled field is temporarily reverted to match its
	 *     pre-apply loaded state so the rebuild below produces the same
	 *     mount set as before.
	 *   - mod->pending_restart is set so the UI can render
	 *     "Restart required for [name]" until the next launch.
	 *
	 * Mods without requires_restart go through the normal hot-reload path
	 * unchanged. */
	bool intendedEnabled[MODMGR_MAX_MODS];
	bool prevLoaded[MODMGR_MAX_MODS];
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		intendedEnabled[i] = g_ModRegistry[i].enabled;
		prevLoaded[i]      = g_ModRegistry[i].loaded;
	}

	s32 deferCount = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		modinfo_t *mod = &g_ModRegistry[i];
		if (mod->requires_restart && intendedEnabled[i] != prevLoaded[i]) {
			mod->pending_restart = 1;
			deferCount++;
			sysLogPrintf(LOG_NOTE,
				"modmgr: '%s' requires_restart=true -- %s deferred until next launch",
				mod->id, intendedEnabled[i] ? "enable" : "disable");
		} else {
			mod->pending_restart = 0;
		}
	}

	if (persist_machine_state) {
		/* User-driven global changes remain the machine default. Agent profile
		 * activation uses the transient seam so it cannot create a second
		 * per-agent source in .modstate, mods-enabled.json, or pd.ini. */
		modmgrSaveComponentState();
		modmgrSaveConfig();
	}

	/* Mask enabled for deferred mods so the rebuild keeps them in their
	 * pre-apply loaded state. After the rebuild we restore intent so the
	 * registry surface (modmgrGetModEnabled) reflects what the user chose. */
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].pending_restart) {
			g_ModRegistry[i].enabled = prevLoaded[i];
		}
	}

	/* Rebuild catalog + reverse-indexes from the (possibly masked) enabled set. */
	modmgrUnloadAllMods();
	modmgrRebuildCatalogFromCurrentSelection();

	/* Restore intent so UI / network manifest / accessors see the user's
	 * choice. The runtime mount state still reflects prevLoaded for the
	 * deferred mods, which is what pending_restart communicates. */
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].pending_restart) {
			g_ModRegistry[i].enabled = intendedEnabled[i];
		}
	}

	/* Invalidate catalog-backed caches so accessors pick up new state */
	modmgrCatalogChanged();
	videoResetTextureCache();
	g_ModDirty = false;

	/* Issue 2/8: rescan mods/ for new theme.json files so newly-installed
	 * mod themes appear in the theme selector after Apply without a restart.
	 * Audio is self-healing (renderSelectTunes scans per-frame); only themes
	 * need an explicit rescan here. */
	pdguiThemeRescanMods();
	/* S-8: chrome styles are tracked in a separate subsystem from themes —
	 * without this rescan the chrome style picker goes stale after Apply. */
	pdguiThemeRescanChromeStyles();
	/* D5 Phase 4: apply UI texture overrides from newly-enabled mods */
	pdguiThemeApplyEnabledModUiTextures();

	/* Stay in-place: no forced title restart.  Callers keep the active menu
	 * and present an in-UI apply progress/completion modal. */
	if (deferCount > 0) {
		sysLogPrintf(LOG_NOTE,
			"modmgr: apply complete -- %d mod(s) require restart to take effect",
			deferCount);
	} else {
		sysLogPrintf(LOG_NOTE, "modmgr: apply complete -- no stage restart");
	}
}

void modmgrApplyChanges(void)
{
	modmgrApplyChangesInternal(1);
}

void modmgrApplyChangesTransient(void)
{
	modmgrApplyChangesInternal(0);
}

// ---------------------------------------------------------------------------
// Public API: Network
//
// The legacy binary manifest API (modmgrGetManifestHash / modmgrWriteManifest /
// modmgrReadManifest, CRC32-based) was removed 2026-07-04 (c064): it had no live
// caller and was superseded by the match_manifest_t / manifestBuildForHost path
// (SHA-256, per-asset granularity). See port/src/net/netmanifest.c.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Public API: Filesystem integration
// ---------------------------------------------------------------------------

const char *modmgrResolvePath(const char *relPath)
{
	// Iterate enabled mods in registry order (load order)
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (!g_ModRegistry[i].enabled || !g_ModRegistry[i].loaded) continue;

		/* Priority M / B-238: archive-backed mods do not have on-disk
		 * asset paths. The M-1.3 modvfs layer routes their asset reads
		 * through the in-memory archive. Skip them here so we do not
		 * spuriously hit the legacy modDir / basedir fallback for an
		 * asset that exists in an archive. */
		if (g_ModRegistry[i].is_archive) continue;

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
// Bot name mod override (P2)
// ---------------------------------------------------------------------------
// When a mod with "botnames" content is enabled, its profile name overrides
// are stored here and queried by mpGenerateBotNames() in mplayer.c.

static char s_BotNameOverrides[MODMGR_MAX_BOT_PROFILES][MODMGR_BOT_NAME_LEN];
static s32  s_BotNameOverrideActive = 0;

/* Body that walks an in-memory mod.json buffer. Caller owns the buffer.
 * NOTE: this body is shared by both folder mods (via modmgrParseBotNames)
 * and archive mods (via modmgrLoadMod when is_archive). It writes into the
 * file-scope s_BotNameOverrides[] table directly. */
static void modmgrParseBotNamesBuf(modinfo_t *mod, const char *src, u32 size)
{
	if (!mod->has_modjson) return;
	if (!src || size == 0) return;

	jparse_t j;
	j.src = src;
	j.pos = src;

	jtok_t tok = json_next(&j);
	if (tok.type != JTOK_LBRACE) return;

	// Find "content" -> "botnames" -> "profiles" array
	while (1) {
		tok = json_next(&j);
		if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
		if (tok.type == JTOK_COMMA) continue;
		if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

		jtok_t key = tok;
		tok = json_next(&j); // colon
		if (tok.type != JTOK_COLON) break;

		if (!json_key_eq(&key, "content")) {
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

			if (!json_key_eq(&ckey, "botnames")) {
				json_skip_value(&j);
				continue;
			}

			// Parse "botnames" object
			tok = json_next(&j);
			if (tok.type != JTOK_LBRACE) break;

			while (1) {
				tok = json_next(&j);
				if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
				if (tok.type == JTOK_COMMA) continue;
				if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

				jtok_t pkey = tok;
				tok = json_next(&j); // colon
				if (tok.type != JTOK_COLON) break;

				if (!json_key_eq(&pkey, "profiles")) {
					json_skip_value(&j);
					continue;
				}

				// Parse profiles array
				tok = json_next(&j);
				if (tok.type != JTOK_LBRACKET) break;

				while (1) {
					tok = json_next(&j);
					if (tok.type == JTOK_RBRACKET || tok.type == JTOK_EOF) break;
					if (tok.type == JTOK_COMMA) continue;
					if (tok.type != JTOK_LBRACE) continue;

					s32 profile = -1;
					char pname[MODMGR_BOT_NAME_LEN] = "";

					while (1) {
						tok = json_next(&j);
						if (tok.type == JTOK_RBRACE || tok.type == JTOK_EOF) break;
						if (tok.type == JTOK_COMMA) continue;
						if (tok.type != JTOK_STRING) { json_skip_value(&j); continue; }

						jtok_t fkey = tok;
						tok = json_next(&j); // colon
						if (tok.type != JTOK_COLON) break;

						if (json_key_eq(&fkey, "profile")) {
							tok = json_next(&j);
							profile = json_tok_int(&tok);
						} else if (json_key_eq(&fkey, "name")) {
							tok = json_next(&j);
							json_tok_string(&tok, pname, MODMGR_BOT_NAME_LEN);
						} else {
							json_skip_value(&j);
						}
					}

					if (profile >= 0 && profile < MODMGR_MAX_BOT_PROFILES && pname[0]) {
						strncpy(s_BotNameOverrides[profile], pname, MODMGR_BOT_NAME_LEN - 1);
						s_BotNameOverrides[profile][MODMGR_BOT_NAME_LEN - 1] = '\0';
						s_BotNameOverrideActive = 1;
					}
				}
				break; // profiles processed
			}
			break; // botnames processed
		}
		break; // content processed
	}

	if (s_BotNameOverrideActive) {
		sysLogPrintf(LOG_NOTE, "modmgr: bot name overrides loaded from mod '%s'", mod->id);
	}
}

/* Folder-mod entry point: load mod.json from disk and call the buffer body. */
static void modmgrParseBotNames(modinfo_t *mod)
{
	/* B-172: skip silently for mods without mod.json. */
	if (!mod->has_modjson) return;
	/* Archive mods route through modmgrLoadMod which calls the buf body
	 * directly with the manifest already in memory. */
	if (mod->is_archive) return;

	char path[FS_MAXPATH + 1];
	snprintf(path, sizeof(path), "%s/mod.json", mod->dirpath);

	if (fsFileSize(path) <= 0) return;

	u32 filesize = 0;
	char *data = (char *)fsFileLoad(path, &filesize);
	if (!data || filesize == 0) return;

	char *buf = (char *)malloc(filesize + 1);
	if (!buf) { free(data); return; }
	memcpy(buf, data, filesize);
	buf[filesize] = '\0';
	free(data);

	modmgrParseBotNamesBuf(mod, buf, filesize);
	free(buf);
}

static void modmgrClearBotNames(void)
{
	memset(s_BotNameOverrides, 0, sizeof(s_BotNameOverrides));
	s_BotNameOverrideActive = 0;
}

const char *modmgrGetBotProfileName(s32 profileIndex)
{
	if (!s_BotNameOverrideActive) return NULL;
	if (profileIndex < 0 || profileIndex >= MODMGR_MAX_BOT_PROFILES) return NULL;
	if (s_BotNameOverrides[profileIndex][0] == '\0') return NULL;
	return s_BotNameOverrides[profileIndex];
}

s32 modmgrHasBotNameOverride(void)
{
	return s_BotNameOverrideActive;
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
	 * Index by mpbodynum (sequential position = 0, 1, 2, ...) NOT by
	 * entry->runtime_index (HeadsAndBodies bodynum, e.g. 86 for BODY_DARK_COMBAT).
	 * modmgrGetBody(mpbodynum) accesses s_CatalogBodies[mpbodynum], so the cache
	 * must be in mpbodynum order.  Catalog iterates base bodies in registration
	 * order (indices 0..62), so iteration order matches mpbodynum order.
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
	((asset_entry_t *)entry)->mp_index = (s16)idx;

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
	 * Index by mpheadnum (sequential position = 0, 1, 2, ...) NOT by
	 * entry->runtime_index (HeadsAndBodies headnum, e.g. HEAD_BEAU1=0x18).
	 * modmgrGetHead(mpheadnum) accesses s_CatalogHeads[mpheadnum], so the cache
	 * must be in mpheadnum order.  Catalog iterates base heads in registration
	 * order (loop mpidx 0..75), so iteration order matches mpheadnum order.
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
	((asset_entry_t *)entry)->mp_index = (s16)idx;

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

	/* AUDIT-24-M7 (2026-04-25): invalidate the Grid arena picker's cached
	 * list so a mod-authored arena added/removed mid-session shows up on
	 * the next picker open without a process restart. */
	{
		extern void pdguiGridArenasInvalidate(void);
		pdguiGridArenasInvalidate();
	}
}

const char *modmgrGetModsDir(void)
{
	return g_ModsDirPath[0] ? g_ModsDirPath : NULL;
}

s32 modmgrInstallArchiveFile(const char *archive_path,
                             s32 enable_now,
                             char *out_error,
                             s32 error_len)
{
	if (out_error && error_len > 0) out_error[0] = '\0';
	if (!modmgrValidateArchiveFile(archive_path, out_error, error_len)) {
		return -1;
	}

	const char *modsdir = modmgrGetModsDir();
	char fallback_mods[FS_MAXPATH + 1];
	if (!modsdir || !modsdir[0]) {
		fsFullPath("./" MODMGR_MODS_DIR, fallback_mods, sizeof(fallback_mods));
		fsCreateDir(fallback_mods);
		modsdir = fallback_mods;
	}

	char install_dir[FS_MAXPATH + 1];
	snprintf(install_dir, sizeof(install_dir), "%s/installed", modsdir);
	fsCreateDir(install_dir);

	char safe[96];
	modmgrSanitizeInstallFilename(archive_path, safe, sizeof(safe));

	char dst[FS_MAXPATH + 1];
	snprintf(dst, sizeof(dst), "%s/%s", install_dir, safe);
	if (!modmgrCopyFileAtomic(archive_path, dst)) {
		modmgrSetError(out_error, error_len,
			"Archive could not be installed to %s", dst);
		return -1;
	}

	modmgrRescanDirectory();

	s32 found = -1;
	s32 leaf_match = -1;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		modinfo_t *mod = &g_ModRegistry[i];
		if (!mod->is_archive || !mod->archive_path[0]) continue;
		if (modmgrPathEqualsNoCase(mod->archive_path, dst)) {
			found = i;
			break;
		}
		if (modmgrPathEqualsNoCase(modmgrPathLeaf(mod->archive_path), safe)) {
			leaf_match = i;
		}
	}
	if (found < 0) found = leaf_match;
	if (found < 0) {
		modmgrSetError(out_error, error_len,
			"Installed archive was not found in the mod registry");
		return -1;
	}

	modinfo_t *mod = &g_ModRegistry[found];
	if (!mod->valid) {
		modmgrSetError(out_error, error_len, "%s",
			mod->validation_error[0] ? mod->validation_error : "Installed archive is invalid");
		return -1;
	}

	if (enable_now) {
		char missing[256];
		if (modmgrCheckDependencies(found, missing, sizeof(missing)) > 0) {
			modmgrSetError(out_error, error_len,
				"Installed archive is missing dependencies: %s", missing);
			return -1;
		}
		if (!mod->enabled) {
			modmgrSetEnabled(found, 1);
			modmgrApplyChanges();
		} else {
			modmgrSyncCatalogToRegistry();
		}
	}

	sysLogPrintf(LOG_NOTE,
		"modmgr: installed .pdmod archive -> %s%s",
		dst, enable_now ? " and applied" : "");
	return found;
}

s32 modmgrGetSizeThresholdMB(void)
{
	return g_ModSizeThresholdMB;
}

void modmgrSetSizeThresholdMB(s32 mb)
{
	if (mb < 0) mb = 0;
	g_ModSizeThresholdMB = mb;
}

// ---------------------------------------------------------------------------
// UI accessor helpers (C++ safe — no struct layout needed)
// ---------------------------------------------------------------------------

const char *modmgrGetModId(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].id;
}

const char *modmgrGetModName(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].name;
}

const char *modmgrGetModVersion(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].version;
}

const char *modmgrGetModAuthor(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].author;
}

const char *modmgrGetModDescription(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].description;
}

const char *modmgrGetModBaseFallback(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].base_fallback;
}

const char *modmgrGetModValidationError(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].validation_error;
}

s32 modmgrGetModEnabled(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].enabled;
}

s32 modmgrGetModValid(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].valid;
}

u32 modmgrGetModSizeBytes(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].size_bytes;
}

s32 modmgrGetModNumDeps(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].num_dependencies;
}

const char *modmgrGetModDep(s32 index, s32 depIndex)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	if (depIndex < 0 || depIndex >= g_ModRegistry[index].num_dependencies) return "";
	return g_ModRegistry[index].dependencies[depIndex];
}

s32 modmgrExceedsThreshold(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	if (g_ModSizeThresholdMB <= 0) return 0;
	u32 threshBytes = (u32)g_ModSizeThresholdMB * 1024u * 1024u;
	return g_ModRegistry[index].size_bytes > threshBytes;
}

// --- S196: Base-Game Template Mod accessors ---

s32 modmgrGetModIsTemplate(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].is_template;
}

s32 modmgrGetModNumTags(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].num_tags;
}

const char *modmgrGetModTag(s32 index, s32 tagIndex)
{
	if (index < 0 || index >= g_ModRegistryCount) return NULL;
	if (tagIndex < 0 || tagIndex >= g_ModRegistry[index].num_tags) return NULL;
	return g_ModRegistry[index].tags[tagIndex];
}

s32 modmgrModHasTag(s32 index, const char *tag)
{
	if (!tag || !tag[0]) return 0;
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	modinfo_t *mod = &g_ModRegistry[index];
	for (s32 i = 0; i < mod->num_tags; i++) {
		if (strcmp(mod->tags[i], tag) == 0) return 1;
	}
	return 0;
}

/* Priority M / B-238 accessors */

s32 modmgrGetModIsArchive(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].is_archive;
}

const char *modmgrGetModArchivePath(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return "";
	return g_ModRegistry[index].archive_path;
}

s32 modmgrGetModRequiresRestart(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].requires_restart;
}

s32 modmgrGetModPendingRestart(s32 index)
{
	if (index < 0 || index >= g_ModRegistryCount) return 0;
	return g_ModRegistry[index].pending_restart;
}

s32 modmgrGetPendingRestartCount(void)
{
	s32 n = 0;
	for (s32 i = 0; i < g_ModRegistryCount; i++) {
		if (g_ModRegistry[i].pending_restart) n++;
	}
	return n;
}
