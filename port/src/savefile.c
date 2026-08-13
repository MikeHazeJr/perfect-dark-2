/**
 * savefile.c -- PC-native save system implementation.
 *
 * Uses JSON files for human-readable, extensible save data.
 * Replaces the N64 EEPROM/savebuffer bit-packing approach.
 *
 * Each save type is a separate JSON file in the save directory:
 *   agent_<name>.json, player_<name>.json, mpsetup_<name>.json, system.json
 *
 * Writing: direct fprintf (simple, no library dependency)
 * Reading: minimal JSON tokenizer (same approach as modmgr.c)
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>

#include "versions.h"
#include "types.h"
#include "constants.h"
#include "data.h"
#include "bss.h"
#include "system.h"
#include "savefile.h"
#include "agent_profile_codec.h"
#include "assetcatalog.h"
#include "net/matchsetup.h"
#include "fs.h"
#include "save_atomic.h"
#include "prefs_agent.h"
#include "game/challenge.h"
#include "game/cheats.h"
#include "game/gamefile.h"
#include "game/mplayer/mplayer.h"
#include "game/options.h"
#include "game/training.h"
#include "lib/snd.h"

static struct saveagentwritereceipt s_LastAgentWriteReceipt;
static u32 s_AgentProfileRevision;

static void savePublishAgentWriteReceipt(const char *name, s32 result)
{
	s_LastAgentWriteReceipt.serial++;
	if (s_LastAgentWriteReceipt.serial == 0) {
		s_LastAgentWriteReceipt.serial = 1;
	}
	s_LastAgentWriteReceipt.result = result;
	strncpy(s_LastAgentWriteReceipt.name, name ? name : "",
		SAVE_NAME_MAX - 1);
	s_LastAgentWriteReceipt.name[SAVE_NAME_MAX - 1] = '\0';
}

void saveGetLastAgentWriteReceipt(struct saveagentwritereceipt *out)
{
	if (out) {
		*out = s_LastAgentWriteReceipt;
	}
}

u32 saveGetAgentProfileRevision(void)
{
	return s_AgentProfileRevision;
}

static void saveAdvanceAgentProfileRevision(void)
{
	s_AgentProfileRevision++;
	if (s_AgentProfileRevision == 0) {
		s_AgentProfileRevision = 1;
	}
}

/* ========================================================================
 * Mini JSON tokenizer (shared approach with modmgr.c)
 * ======================================================================== */

typedef enum {
	STOK_NONE = 0, STOK_LBRACE, STOK_RBRACE, STOK_LBRACKET, STOK_RBRACKET,
	STOK_COLON, STOK_COMMA, STOK_STRING, STOK_NUMBER, STOK_TRUE, STOK_FALSE,
	STOK_NULL, STOK_EOF, STOK_ERROR,
} stok_type_t;

typedef struct {
	const char *start;
	s32 len;
	stok_type_t type;
} stok_t;

typedef struct {
	const char *pos;
	stok_t cur;
} sparse_t;

static void s_skipws(sparse_t *p)
{
	while (*p->pos && (*p->pos == ' ' || *p->pos == '\t' || *p->pos == '\n' || *p->pos == '\r')) {
		p->pos++;
	}
}

static stok_t s_next(sparse_t *p)
{
	stok_t tok = { NULL, 0, STOK_NONE };
	s_skipws(p);
	if (!*p->pos) { tok.type = STOK_EOF; return tok; }

	tok.start = p->pos;
	char c = *p->pos;

	switch (c) {
	case '{': tok.type = STOK_LBRACE;   tok.len = 1; p->pos++; break;
	case '}': tok.type = STOK_RBRACE;   tok.len = 1; p->pos++; break;
	case '[': tok.type = STOK_LBRACKET;  tok.len = 1; p->pos++; break;
	case ']': tok.type = STOK_RBRACKET;  tok.len = 1; p->pos++; break;
	case ':': tok.type = STOK_COLON;    tok.len = 1; p->pos++; break;
	case ',': tok.type = STOK_COMMA;    tok.len = 1; p->pos++; break;
	case '"': {
		p->pos++;
		tok.start = p->pos;
		while (*p->pos && *p->pos != '"') {
			if (*p->pos == '\\') p->pos++;
			if (*p->pos) p->pos++;
		}
		tok.len = (s32)(p->pos - tok.start);
		if (*p->pos == '"') {
			tok.type = STOK_STRING;
			p->pos++;
		} else {
			tok.type = STOK_ERROR;
		}
		break;
	}
	default:
		if (c == '-' || (c >= '0' && c <= '9')) {
			if (c == '-') p->pos++;
			while (*p->pos >= '0' && *p->pos <= '9') p->pos++;
			if (*p->pos == '.') { p->pos++; while (*p->pos >= '0' && *p->pos <= '9') p->pos++; }
			tok.len = (s32)(p->pos - tok.start);
			tok.type = STOK_NUMBER;
		} else if (strncmp(p->pos, "true", 4) == 0) {
			tok.type = STOK_TRUE; tok.len = 4; p->pos += 4;
		} else if (strncmp(p->pos, "false", 5) == 0) {
			tok.type = STOK_FALSE; tok.len = 5; p->pos += 5;
		} else if (strncmp(p->pos, "null", 4) == 0) {
			tok.type = STOK_NULL; tok.len = 4; p->pos += 4;
		} else {
			tok.type = STOK_ERROR; p->pos++;
		}
		break;
	}
	p->cur = tok;
	return tok;
}

#define S_MAX_DEPTH 64          /* max JSON nesting depth — crafted saves can't stack-overflow */
#define SAVE_MAX_FILE_BYTES (256 * 1024) /* 256 KB hard cap — rejects giant crafted saves before malloc */

static void s_skip_value(sparse_t *p, s32 depth)
{
	if (depth > S_MAX_DEPTH) {
		/* Skip to end-of-input to abort the parse cleanly. */
		while (*p->pos) p->pos++;
		return;
	}
	stok_t tok = s_next(p);
	if (tok.type == STOK_LBRACE) {
		while ((tok = s_next(p)).type != STOK_RBRACE && tok.type != STOK_EOF) {
			if (tok.type == STOK_STRING) { s_next(p); s_skip_value(p, depth + 1); }
		}
	} else if (tok.type == STOK_LBRACKET) {
		while ((tok = s_next(p)).type != STOK_RBRACKET && tok.type != STOK_EOF) {
			if (tok.type == STOK_COMMA) continue;
			/* value already consumed by s_next for primitives, recurse for nested */
			if (tok.type == STOK_LBRACE || tok.type == STOK_LBRACKET) {
				/* back up and recurse */
				p->pos = tok.start;
				s_skip_value(p, depth + 1);
			}
		}
	}
	/* primitives are already consumed by s_next */
}

static s32 s_validate_json_value(sparse_t *p, s32 depth)
{
	stok_t tok;

	if (!p || depth > S_MAX_DEPTH) {
		return -1;
	}
	tok = s_next(p);
	if (tok.type == STOK_STRING || tok.type == STOK_NUMBER
			|| tok.type == STOK_TRUE || tok.type == STOK_FALSE
			|| tok.type == STOK_NULL) {
		return 0;
	}
	if (tok.type == STOK_LBRACE) {
		tok = s_next(p);
		if (tok.type == STOK_RBRACE) {
			return 0;
		}
		for (;;) {
			if (tok.type != STOK_STRING || s_next(p).type != STOK_COLON
					|| s_validate_json_value(p, depth + 1) != 0) {
				return -1;
			}
			tok = s_next(p);
			if (tok.type == STOK_RBRACE) {
				return 0;
			}
			if (tok.type != STOK_COMMA) {
				return -1;
			}
			tok = s_next(p);
		}
	}
	if (tok.type == STOK_LBRACKET) {
		const char *value_start = p->pos;
		tok = s_next(p);
		if (tok.type == STOK_RBRACKET) {
			return 0;
		}
		p->pos = value_start;
		for (;;) {
			if (s_validate_json_value(p, depth + 1) != 0) {
				return -1;
			}
			tok = s_next(p);
			if (tok.type == STOK_RBRACKET) {
				return 0;
			}
			if (tok.type != STOK_COMMA) {
				return -1;
			}
		}
	}
	return -1;
}

static s32 s_validate_json_document(const char *data)
{
	sparse_t parser;
	stok_t tail;

	if (!data || !data[0]) {
		return -1;
	}
	memset(&parser, 0, sizeof(parser));
	parser.pos = data;
	if (s_validate_json_value(&parser, 0) != 0) {
		return -1;
	}
	tail = s_next(&parser);
	return tail.type == STOK_EOF ? 0 : -1;
}

static void s_tok_str(const stok_t *tok, char *dest, s32 maxlen)
{
	if (tok->type != STOK_STRING || !tok->start) { dest[0] = '\0'; return; }
	s32 n = tok->len < (maxlen - 1) ? tok->len : (maxlen - 1);
	memcpy(dest, tok->start, n);
	dest[n] = '\0';
}

static s32 s_tok_int(const stok_t *tok)
{
	if (tok->type != STOK_NUMBER || !tok->start) return 0;
	return (s32)strtol(tok->start, NULL, 0);
}

static u32 s_tok_uint(const stok_t *tok)
{
	if (tok->type != STOK_NUMBER || !tok->start) return 0;
	return (u32)strtoul(tok->start, NULL, 0);
}

static u8 s_tok_bool(const stok_t *tok)
{
	return tok->type == STOK_TRUE ? 1 : 0;
}

/* ========================================================================
 * File I/O helpers
 * ======================================================================== */

static char s_SaveDir[512];
static s32 s_Initialized = 0;

static char *readFileContents(const char *path, s32 *outLen)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;

	fseek(fp, 0, SEEK_END);
	long len = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (len <= 0 || len > SAVE_MAX_FILE_BYTES) {
		fclose(fp);
		if (outLen) *outLen = 0;
		return NULL;
	}

	char *buf = (char *)malloc((size_t)len + 1);
	if (!buf) { fclose(fp); return NULL; }

	if (fread(buf, 1, len, fp) != (size_t)len) {
		free(buf);
		fclose(fp);
		if (outLen) *outLen = 0;
		return NULL;
	}
	buf[len] = '\0';
	fclose(fp);

	if (outLen) *outLen = (s32)len;
	return buf;
}

static void writeJsonStringValue(FILE *fp, const char *value)
{
	fputc('"', fp);
	for (const char *c = value; *c; c++) {
		switch (*c) {
		case '"':  fprintf(fp, "\\\""); break;
		case '\\': fprintf(fp, "\\\\"); break;
		case '\n': fprintf(fp, "\\n");  break;
		case '\r': fprintf(fp, "\\r");  break;
		case '\t': fprintf(fp, "\\t");  break;
		default:   fputc(*c, fp);       break;
		}
	}
	fputc('"', fp);
}

static void writeJsonString(FILE *fp, const char *key, const char *value)
{
	fprintf(fp, "  \"%s\": ", key);
	writeJsonStringValue(fp, value);
}

/* LAYOUT-4: dispatch on the save's version field.
 * Returns 0 to continue loading, -1 to reject.
 *   - version > SAVE_VERSION → reject (refuse partial load of newer format)
 *   - version < SAVE_VERSION → log; loader's unknown-key skip is the
 *     forward-compat path for older saves. saveMigrateFile is the hook
 *     for future schema-bump migrations (savemigrate.c).
 *   - version == SAVE_VERSION → normal load. */
static s32 saveCheckFileVersion(s32 fileVersion, const char *kind, const char *path)
{
	if (fileVersion <= 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: refusing to load %s '%s' with invalid version %d",
			kind, path, fileVersion);
		return -1;
	}
	if (fileVersion > SAVE_VERSION) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: refusing to load %s '%s' — file version %d is NEWER than current SAVE_VERSION %d",
			kind, path, fileVersion, SAVE_VERSION);
		return -1;
	}
	if (fileVersion > 0 && fileVersion < SAVE_VERSION) {
		/* Older-format save. The v1->v2 bump (SA-4, savefile.h) replaced raw
		 * integer body/head/scenario IDs with string IDs, so the current
		 * loader's unknown-key skip would silently DROP a v1 file's identity
		 * fields and substitute defaults -- silent data loss (character /
		 * scenario identity). No saveMigrateFile() data migration is
		 * registered for this step yet (savemigrate.c registers none), and
		 * migration cannot run at this mid-parse point, so refuse the load
		 * LOUDLY and leave the on-disk file untouched. It is preserved so a
		 * future v1->v2 migration can still upgrade it, instead of being
		 * consumed lossily now. When that migration is wired at load entry
		 * (before parse), relax this branch to migrate-then-load. */
		sysLogPrintf(LOG_WARNING,
			"SAVE: refusing to load %s '%s' — file is v%d but current SAVE_VERSION is %d and no "
			"migration is registered; loading would silently lose identity data. File preserved.",
			kind, path, fileVersion, SAVE_VERSION);
		return -1;
	}
	return 0;
}

static s32 savePreflightJson(const char *data, const char *kind,
		const char *path)
{
	sparse_t parser;
	stok_t tok;
	s32 saw_version = 0;

	if (s_validate_json_document(data) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: refusing malformed %s JSON '%s'; live state preserved",
			kind, path);
		return -1;
	}
	memset(&parser, 0, sizeof(parser));
	parser.pos = data;
	if (s_next(&parser).type != STOK_LBRACE) {
		return -1;
	}
	while ((tok = s_next(&parser)).type == STOK_STRING) {
		char key[64];
		s_tok_str(&tok, key, sizeof(key));
		if (s_next(&parser).type != STOK_COLON) {
			return -1;
		}
		if (strcmp(key, "version") == 0) {
			tok = s_next(&parser);
			if (tok.type != STOK_NUMBER
					|| saveCheckFileVersion(s_tok_int(&tok), kind, path) != 0) {
				return -1;
			}
			saw_version = 1;
		} else {
			s_skip_value(&parser, 0);
		}
		tok = s_next(&parser);
		if (tok.type == STOK_RBRACE) {
			break;
		}
		if (tok.type != STOK_COMMA) {
			return -1;
		}
	}
	if (!saw_version) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: refusing %s JSON '%s' without a version; live state preserved",
			kind, path);
		return -1;
	}
	return 0;
}

static void buildSavePath(char *out, s32 maxlen, const char *prefix, const char *name, const char *ext)
{
	/* Sanitize name: replace non-alphanumeric chars with underscore */
	char safeName[SAVE_NAME_MAX];
	s32 j = 0;
	for (s32 i = 0; name[i] && j < SAVE_NAME_MAX - 1; i++) {
		char c = name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
			safeName[j++] = c;
		} else if (c == ' ') {
			safeName[j++] = '_';
		}
		/* skip other characters */
	}
	safeName[j] = '\0';

	snprintf(out, maxlen, "%s/%s_%s.%s", s_SaveDir, prefix, safeName, ext);
}

/* ========================================================================
 * Public: saveInit
 * ======================================================================== */

void saveInit(void)
{
	if (s_Initialized) return;

	/* Use the same save directory as the existing system ($S expands to save dir) */
	fsFullPath("$S", s_SaveDir, sizeof(s_SaveDir));
	if (!s_SaveDir[0]) {
		strncpy(s_SaveDir, ".", sizeof(s_SaveDir) - 1);
		s_SaveDir[sizeof(s_SaveDir) - 1] = '\0';
	}

	sysLogPrintf(LOG_NOTE, "SAVE: initialized — save dir: %s", s_SaveDir);
	s_Initialized = 1;

	/* Check for old eeprom.bin — offer migration */
	char eepromPath[512];
	snprintf(eepromPath, sizeof(eepromPath), "%s/eeprom.bin", s_SaveDir);

	struct stat st;
	if (stat(eepromPath, &st) == 0) {
		/* Check if we've already migrated */
		char systemPath[512];
		snprintf(systemPath, sizeof(systemPath), "%s/system.json", s_SaveDir);
		if (stat(systemPath, &st) != 0) {
			sysLogPrintf(LOG_NOTE, "SAVE: found eeprom.bin without system.json — migration available");
			/* Migration will be triggered by the game flow, not automatically,
			 * to allow the engine to finish initializing first. */
		}
	}
}

const char *saveGetDir(void)
{
	return s_SaveDir;
}

/* ========================================================================
 * Agent profile save/load
 * ======================================================================== */

_Static_assert(SAVE_AGENT_STAGE_COUNT == NUM_SOLOSTAGES,
	"agent JSON stage domain must match struct gamefile");
_Static_assert(SAVE_AGENT_CHALLENGE_COUNT == ARRAYCOUNT(g_MpChallenges),
	"agent JSON challenge domain must match gameplay");
_Static_assert(SAVE_AGENT_PLAYER_COUNTS == MAX_LOCAL_PLAYERS,
	"agent JSON player-count domain must match gameplay");
_Static_assert(SAVE_AGENT_STAGE_COUNT == AGENT_PROFILE_STAGE_COUNT,
	"public save and Agent Profile stage domains must match");
_Static_assert(SAVE_AGENT_CHALLENGE_COUNT == AGENT_PROFILE_CHALLENGE_COUNT,
	"public save and Agent Profile challenge domains must match");
_Static_assert(SAVE_AGENT_PLAYER_COUNTS == AGENT_PROFILE_PLAYER_COUNTS,
	"public save and Agent Profile player-count domains must match");

static s32 saveAgentNameIsValid(const char *name)
{
	s32 length;
	if (!name) return 0;
	length = (s32)strlen(name);
	if (length < 1 || length > SAVE_AGENT_NAME_LENGTH_MAX
			|| name[0] == ' ' || name[length - 1] == ' ') return 0;
	for (s32 i = 0; i < length; i++) {
		unsigned char c = (unsigned char)name[i];
		if (!isalnum(c) && c != ' ' && c != '_' && c != '-') return 0;
	}
	return 1;
}

static void saveAgentDocumentSetGamefile(
		struct agent_profile_document *document,
		const struct gamefile *gamefile)
{
	strncpy(document->name, gamefile->name, sizeof(document->name) - 1);
	document->name[sizeof(document->name) - 1] = '\0';
	document->totaltime = gamefile->totaltime;
	document->autodifficulty = gamefile->autodifficulty;
	document->autostageindex = gamefile->autostageindex;
	document->thumbnail = gamefile->thumbnail;
	memcpy(document->besttimes, gamefile->besttimes, sizeof(document->besttimes));
	memcpy(document->coopcompletions, gamefile->coopcompletions,
		sizeof(document->coopcompletions));
	memcpy(document->firingrangescores, gamefile->firingrangescores,
		sizeof(document->firingrangescores));
	memcpy(document->weaponsfound, gamefile->weaponsfound,
		sizeof(document->weaponsfound));
	memcpy(document->flags, gamefile->flags, sizeof(document->flags));
	document->unk1e = gamefile->unk1e;
}

static void saveAgentDocumentGetGamefile(
		const struct agent_profile_document *document,
		struct gamefile *gamefile)
{
	memset(gamefile, 0, sizeof(*gamefile));
	strncpy(gamefile->name, document->name, sizeof(gamefile->name) - 1);
	gamefile->name[sizeof(gamefile->name) - 1] = '\0';
	gamefile->totaltime = document->totaltime;
	gamefile->autodifficulty = document->autodifficulty;
	gamefile->autostageindex = document->autostageindex;
	gamefile->thumbnail = document->thumbnail;
	memcpy(gamefile->besttimes, document->besttimes, sizeof(gamefile->besttimes));
	memcpy(gamefile->coopcompletions, document->coopcompletions,
		sizeof(gamefile->coopcompletions));
	memcpy(gamefile->firingrangescores, document->firingrangescores,
		sizeof(gamefile->firingrangescores));
	memcpy(gamefile->weaponsfound, document->weaponsfound,
		sizeof(gamefile->weaponsfound));
	memcpy(gamefile->flags, document->flags, sizeof(gamefile->flags));
	gamefile->unk1e = document->unk1e;
}

static void saveAgentDocumentDefaults(struct agent_profile_document *document)
{
	struct gamefile gamefile;
	memset(document, 0, sizeof(*document));
	gamefileSetDefaultState(&gamefile);
	saveAgentDocumentSetGamefile(document, &gamefile);
#if VERSION >= VERSION_NTSC_1_0
	document->sfxvolume = 0x5000;
	document->musicvolume = 0x5000;
#else
	document->sfxvolume = 0x7f80;
	document->musicvolume = 0x7f80;
#endif
	document->soundmode = SOUNDMODE_STEREO;
	document->controlmode[0] = CONTROLMODE_11;
	document->controlmode[1] = CONTROLMODE_11;
	prefsAgentGetBaselineSnapshot(&document->preferences);
}

static s32 saveParseAgentDocument(const char *data, const char *path,
		const char *expected_name, struct agent_profile_document *document,
		enum agent_profile_source_shape *shape)
{
	struct agent_profile_document defaults;
	char error[256];
	if (!data || !path || !document) return -1;
	saveAgentDocumentDefaults(&defaults);
	if (agentProfileParseJson(data, expected_name, &defaults, document,
			shape, error, sizeof(error)) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: Agent Profile JSON '%s' rejected: %s; live state preserved",
			path, error[0] ? error : "schema validation failed");
		return -1;
	}
	return 0;
}

static s32 saveReadAgentDocument(const char *name,
		struct agent_profile_document *document, char *path, s32 path_size,
		enum agent_profile_source_shape *shape)
{
	s32 length = 0;
	char *data;
	enum agent_profile_source_shape source;
	char error[256];

	if (!saveAgentNameIsValid(name) || !document || !path || path_size <= 0) {
		return -1;
	}
	buildSavePath(path, path_size, "agent", name, "json");
	data = readFileContents(path, &length);
	if (!data) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: failed to read agent '%s' from %s", name, path);
		return -1;
	}
	if (saveParseAgentDocument(data, path, name, document, &source) != 0) {
		free(data);
		return -1;
	}
	free(data);
	if (source != AGENT_PROFILE_SOURCE_CURRENT) {
		struct agent_profile_preferences merged;
		s32 found = 0;
		if (prefsAgentReadLegacySidecar(name, &document->preferences,
				&merged, &found, error, sizeof(error)) != 0) {
			sysLogPrintf(LOG_ERROR,
				"SAVE: legacy Agent Profile '%s' sidecar rejected: %s; files preserved",
				name, error[0] ? error : "invalid sidecar");
			return -1;
		}
		document->preferences = merged;
		sysLogPrintf(LOG_NOTE,
			"SAVE: prepared exact legacy Agent Profile name='%s' shape=%d sidecar=%s",
			name, (s32)source, found ? "present" : "absent");
	}
	if (shape) *shape = source;
	return 0;
}

static s32 saveWriteAgentDocument(const char *name,
		const struct agent_profile_document *document)
{
	char path[512];
	save_atomic_file_t transaction;
	FILE *stream;

	if (!saveAgentNameIsValid(name) || !document
			|| strcmp(name, document->name) != 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: refusing invalid or mismatched Agent Profile name '%s'",
			name ? name : "(null)");
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	buildSavePath(path, sizeof(path), "agent", name, "json");
	if (saveAtomicBegin(&transaction, path) != 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: failed to start Agent Profile write name='%s' path='%s'",
			name, path);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	stream = saveAtomicStream(&transaction);
	if (agentProfileWriteJson(stream, document) != 0) {
		saveAtomicAbort(&transaction);
		sysLogPrintf(LOG_ERROR,
			"SAVE: Agent Profile serialization failed name='%s'; destination preserved",
			name);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	if (saveAtomicCommit(&transaction) != 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: failed to commit Agent Profile '%s' to %s", name, path);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	saveAdvanceAgentProfileRevision();
	savePublishAgentWriteReceipt(name, 0);
	sysLogPrintf(LOG_NOTE,
		"SAVE: Agent Profile v%d committed name='%s' path='%s'",
		SAVE_AGENT_VERSION, name, path);
	return 0;
}

static void saveCaptureAgentDocument(const char *name,
		struct agent_profile_document *document)
{
	struct gamefile captured = g_GameFile;
	s32 player1 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 0 : 4;
	s32 player2 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 1 : 5;

	memset(document, 0, sizeof(*document));
	strncpy(captured.name, name, sizeof(captured.name) - 1);
	captured.name[sizeof(captured.name) - 1] = '\0';
	gamefileCaptureOptions(&captured);
	saveAgentDocumentSetGamefile(document, &captured);
	document->sfxvolume = g_SfxVolume;
	document->musicvolume = optionsGetMusicVolume();
	document->soundmode = g_SoundMode;
	document->controlmode[0] = (u8)optionsGetControlMode(player1);
	document->controlmode[1] = (u8)optionsGetControlMode(player2);
	for (s32 challenge = 0; challenge < SAVE_AGENT_CHALLENGE_COUNT; challenge++) {
		for (s32 players = 0; players < SAVE_AGENT_PLAYER_COUNTS; players++) {
			document->challengecompleted[challenge][players] =
				challengeIsCompletedByAnyPlayerWithNumPlayers(
					challenge, players + 1) ? 1 : 0;
		}
	}
	prefsAgentCaptureSnapshot(&document->preferences);
}

static void saveCommitAgentDocument(
		const struct agent_profile_document *document)
{
	struct gamefile committed;
	s32 player1 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 0 : 4;
	s32 player2 = (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) ? 1 : 5;

	prefsAgentCommitSnapshot(&document->preferences);
	saveAgentDocumentGetGamefile(document, &committed);
	g_GameFile = committed;
	cheatsInit();
	sndSetSfxVolume(document->sfxvolume);
	optionsSetMusicVolume(document->musicvolume);
	sndSetSoundMode(document->soundmode);
	optionsSetControlMode(player1, g_PlayerExtCfg[0].extcontrols
		? CONTROLMODE_PC : document->controlmode[0]);
	optionsSetControlMode(player2, g_PlayerExtCfg[1].extcontrols
		? CONTROLMODE_PC : document->controlmode[1]);
	for (s32 challenge = 0; challenge < SAVE_AGENT_CHALLENGE_COUNT; challenge++) {
		for (s32 players = 0; players < SAVE_AGENT_PLAYER_COUNTS; players++) {
			challengeSetCompletedByAnyPlayerWithNumPlayers(challenge,
				players + 1,
				document->challengecompleted[challenge][players] != 0);
		}
	}
	challengeDetermineUnlockedFeatures();
	gamefileApplyOptions(&g_GameFile);
}

s32 saveSaveAgent(const char *name)
{
	struct agent_profile_document document;
	char error[256];

	if (!saveAgentNameIsValid(name)) {
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	saveCaptureAgentDocument(name, &document);
	if (prefsAgentPrepareSnapshot(&document.preferences,
			error, sizeof(error)) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: captured Agent Profile '%s' rejected: %s", name, error);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	return saveWriteAgentDocument(name, &document);
}

s32 saveLoadAgent(const char *name)
{
	char path[512];
	char error[256];
	struct agent_profile_document candidate;
	enum agent_profile_source_shape source;

	if (saveReadAgentDocument(name, &candidate, path, sizeof(path),
			&source) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: agent '%s' rejected; prior live profile preserved",
			name ? name : "(null)");
		return -1;
	}
	if (prefsAgentPrepareSnapshot(&candidate.preferences,
			error, sizeof(error)) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: Agent Profile '%s' preference candidate rejected: %s",
			name, error);
		return -1;
	}
	if (source != AGENT_PROFILE_SOURCE_CURRENT
			&& saveWriteAgentDocument(name, &candidate) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: Agent Profile '%s' migration commit failed; legacy files and live state preserved",
			name);
		return -1;
	}
	saveCommitAgentDocument(&candidate);
	prefsAgentRetireLegacySidecar(name);
	sysLogPrintf(LOG_NOTE,
		"SAVE: Agent Profile activation committed name='%s' path='%s' source=%s",
		name, path, source == AGENT_PROFILE_SOURCE_CURRENT ? "v3" : "migrated-v2");
	return 0;
}

s32 saveCreateAgent(const char *name)
{
	char path[512];
	struct stat st;
	struct agent_profile_document document;
	struct saveagentsummary existing[SAVE_MAX_AGENTS];
	s32 existing_count;

	if (!saveAgentNameIsValid(name)) {
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	buildSavePath(path, sizeof(path), "agent", name, "json");
	if (stat(path, &st) == 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: agent '%s' aliases an existing profile path", name);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	existing_count = saveListAgentProfiles(existing, SAVE_MAX_AGENTS);
	for (s32 i = 0; i < existing_count; i++) {
		if (strcasecmp(existing[i].name, name) == 0) {
			sysLogPrintf(LOG_WARNING,
				"SAVE: agent identity '%s' conflicts with existing profile '%s'",
				name, existing[i].name);
			savePublishAgentWriteReceipt(name, -1);
			return -1;
		}
	}
	if (existing_count >= SAVE_MAX_AGENTS) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: agent profile capacity %d is full", SAVE_MAX_AGENTS);
		savePublishAgentWriteReceipt(name, -1);
		return -1;
	}
	saveAgentDocumentDefaults(&document);
	strncpy(document.name, name, sizeof(document.name) - 1);
	document.name[sizeof(document.name) - 1] = '\0';
	return saveWriteAgentDocument(name, &document);
}

s32 saveCopyAgent(const char *source_name, const char *destination_name)
{
	char source_path[512];
	char destination_path[512];
	struct stat st;
	struct agent_profile_document document;
	struct saveagentsummary existing[SAVE_MAX_AGENTS];
	enum agent_profile_source_shape source_shape;
	s32 existing_count;

	if (!saveAgentNameIsValid(destination_name)
			|| saveReadAgentDocument(source_name, &document,
				source_path, sizeof(source_path), &source_shape) != 0) {
		savePublishAgentWriteReceipt(destination_name, -1);
		return -1;
	}
	buildSavePath(destination_path, sizeof(destination_path),
		"agent", destination_name, "json");
	if (stat(destination_path, &st) == 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: copy destination '%s' already exists or aliases another profile",
			destination_name);
		savePublishAgentWriteReceipt(destination_name, -1);
		return -1;
	}
	existing_count = saveListAgentProfiles(existing, SAVE_MAX_AGENTS);
	for (s32 i = 0; i < existing_count; i++) {
		if (strcasecmp(existing[i].name, destination_name) == 0) {
			sysLogPrintf(LOG_WARNING,
				"SAVE: copy destination identity '%s' conflicts with existing profile '%s'",
				destination_name, existing[i].name);
			savePublishAgentWriteReceipt(destination_name, -1);
			return -1;
		}
	}
	if (existing_count >= SAVE_MAX_AGENTS) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: agent profile capacity %d is full", SAVE_MAX_AGENTS);
		savePublishAgentWriteReceipt(destination_name, -1);
		return -1;
	}
	strncpy(document.name, destination_name, sizeof(document.name) - 1);
	document.name[sizeof(document.name) - 1] = '\0';
	return saveWriteAgentDocument(destination_name, &document);
}

s32 saveDeleteAgent(const char *name)
{
	char path[512];
	struct agent_profile_document document;
	enum agent_profile_source_shape source;

	if (name && prefsAgentGetActive()[0]
			&& strcasecmp(name, prefsAgentGetActive()) == 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: refusing to delete active Agent Profile '%s'", name);
		return -1;
	}
	if (saveReadAgentDocument(name, &document, path, sizeof(path),
			&source) != 0) {
		return -1;
	}
	if (remove(path) == 0) {
		prefsAgentRetireLegacySidecar(name);
		saveAdvanceAgentProfileRevision();
		sysLogPrintf(LOG_NOTE,
			"SAVE: Agent Profile '%s' deleted source=%s",
			name, source == AGENT_PROFILE_SOURCE_CURRENT ? "v3" : "legacy-v2");
		return 0;
	}
	sysLogPrintf(LOG_WARNING, "SAVE: failed to delete agent '%s'", name);
	return -1;
}


/* ========================================================================
 * System settings save/load
 * ======================================================================== */

s32 saveSaveSystem(void)
{
	char path[512];
	save_atomic_file_t transaction;
	snprintf(path, sizeof(path), "%s/system.json", s_SaveDir);

	if (saveAtomicBegin(&transaction, path) != 0) {
		sysLogPrintf(LOG_WARNING, "SAVE: failed to write system settings to %s", path);
		return -1;
	}
	FILE *fp = saveAtomicStream(&transaction);

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	/* g_LanguageId only exists in PAL builds; NTSC is always English */
#if VERSION >= VERSION_PAL_BETA
	fprintf(fp, "  \"language\": %d,\n", g_LanguageId);
#else
	fprintf(fp, "  \"language\": 0,\n");
#endif

	/* Team names */
	fprintf(fp, "  \"teamnames\": [\n");
	for (s32 i = 0; i < MAX_TEAMS && i < 8; i++) {
		fprintf(fp, "    \"%s\"%s\n",
		        g_BossFile.teamnames[i],
		        i < 7 ? "," : "");
	}
	fprintf(fp, "  ],\n");

	/* Soundtrack settings */
	fprintf(fp, "  \"tracknum\": %d,\n", g_BossFile.tracknum);
	fprintf(fp, "  \"usingmultipletunes\": %s,\n",
	        g_BossFile.usingmultipletunes ? "true" : "false");
	fprintf(fp, "  \"alttitleunlocked\": %s,\n",
	        g_AltTitleUnlocked ? "true" : "false");
	fprintf(fp, "  \"alttitleenabled\": %s\n",
	        g_AltTitleEnabled ? "true" : "false");

	fprintf(fp, "}\n");
	if (saveAtomicCommit(&transaction) != 0) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: failed to commit system settings to %s", path);
		return -1;
	}

	sysLogPrintf(LOG_NOTE, "SAVE: system settings saved");
	return 0;
}

s32 saveLoadSystem(void)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/system.json", s_SaveDir);

	s32 len = 0;
	char *data = readFileContents(path, &len);
	if (!data) {
		sysLogPrintf(LOG_NOTE, "SAVE: no system.json found — using defaults");
		return -1;
	}
	if (savePreflightJson(data, "system", path) != 0) {
		free(data);
		return -1;
	}

	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p);

	if (tok.type != STOK_LBRACE) {
		free(data);
		return -1;
	}

	char key[64];
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p); /* colon */

		if (strcmp(key, "version") == 0) {
			tok = s_next(&p);
			s32 fv = s_tok_int(&tok);
			if (saveCheckFileVersion(fv, "system", path) != 0) {
				free(data);
				return -1;
			}
		} else if (strcmp(key, "language") == 0) {
			tok = s_next(&p);
#if VERSION >= VERSION_PAL_BETA
			g_LanguageId = s_tok_int(&tok);
#endif
		} else if (strcmp(key, "teamnames") == 0) {
			tok = s_next(&p); /* [ */
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < 8; i++) {
					tok = s_next(&p);
					if (tok.type == STOK_STRING) {
						s_tok_str(&tok, g_BossFile.teamnames[i], 12);
					}
					tok = s_next(&p);
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else if (strcmp(key, "tracknum") == 0) {
			tok = s_next(&p);
			g_BossFile.tracknum = s_tok_int(&tok);
		} else if (strcmp(key, "usingmultipletunes") == 0) {
			tok = s_next(&p);
			g_BossFile.usingmultipletunes = s_tok_bool(&tok);
		} else if (strcmp(key, "alttitleunlocked") == 0) {
			tok = s_next(&p);
			g_AltTitleUnlocked = s_tok_bool(&tok);
		} else if (strcmp(key, "alttitleenabled") == 0) {
			tok = s_next(&p);
			g_AltTitleEnabled = s_tok_bool(&tok);
		} else {
			s_skip_value(&p, 0);
		}

		tok = s_next(&p);
		if (tok.type == STOK_RBRACE) break;
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "SAVE: system settings loaded");
	return 0;
}

/* ========================================================================
 * MP player profile save/load
 * ======================================================================== */

s32 saveSaveMpPlayer(const char *name, s32 playernum)
{
	if (playernum < 0 || playernum >= MAX_PLAYERS) return -1;

	char path[512];
	save_atomic_file_t transaction;
	buildSavePath(path, sizeof(path), "player", name, "json");

	if (saveAtomicBegin(&transaction, path) != 0) return -1;
	FILE *fp = saveAtomicStream(&transaction);

	struct mpplayerconfig *pc = &g_PlayerConfigsArray[playernum];

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	writeJsonString(fp, "name", name);
	fprintf(fp, ",\n");

	/* Appearance — write PRIMARY catalog ID strings directly.
	 * Phase 5: head_id/body_id are the sole identity; no more resolving from deprecated mpheadnum. */
	writeJsonString(fp, "head_id", pc->base.head_id[0] ? pc->base.head_id : "");
	fprintf(fp, ",\n");
	writeJsonString(fp, "body_id", pc->base.body_id[0] ? pc->base.body_id : "");
	fprintf(fp, ",\n");
	fprintf(fp, "  \"team\": %u,\n", pc->base.team);
	fprintf(fp, "  \"displayoptions\": %u,\n", pc->base.displayoptions);

	/* Stats */
	fprintf(fp, "  \"kills\": %u,\n", pc->kills);
	fprintf(fp, "  \"deaths\": %u,\n", pc->deaths);
	fprintf(fp, "  \"gamesplayed\": %u,\n", pc->gamesplayed);
	fprintf(fp, "  \"gameswon\": %u,\n", pc->gameswon);
	fprintf(fp, "  \"gameslost\": %u,\n", pc->gameslost);
	fprintf(fp, "  \"distance\": %u,\n", pc->distance);
	fprintf(fp, "  \"accuracy\": %u,\n", pc->accuracy);
	fprintf(fp, "  \"headshots\": %u,\n", pc->headshots);
	fprintf(fp, "  \"damagedealt\": %u,\n", pc->damagedealt);
	fprintf(fp, "  \"painreceived\": %u,\n", pc->painreceived);
	fprintf(fp, "  \"ammoused\": %u,\n", pc->ammoused);
	fprintf(fp, "  \"accuracymedals\": %u,\n", pc->accuracymedals);
	fprintf(fp, "  \"headshotmedals\": %u,\n", pc->headshotmedals);
	fprintf(fp, "  \"killmastermedals\": %u,\n", pc->killmastermedals);
	fprintf(fp, "  \"survivormedals\": %u,\n", pc->survivormedals);

	/* Control */
	fprintf(fp, "  \"controlmode\": %u,\n", pc->controlmode);
	fprintf(fp, "  \"options\": %u\n", pc->options);

	fprintf(fp, "}\n");
	if (saveAtomicCommit(&transaction) != 0) {
		return -1;
	}

	sysLogPrintf(LOG_NOTE, "SAVE: MP player '%s' (slot %d) saved", name, playernum);
	return 0;
}

s32 saveLoadMpPlayer(const char *name, s32 playernum)
{
	if (playernum < 0 || playernum >= MAX_PLAYERS) return -1;

	char path[512];
	buildSavePath(path, sizeof(path), "player", name, "json");

	s32 len = 0;
	char *data = readFileContents(path, &len);
	if (!data) return -1;
	if (savePreflightJson(data, "player", path) != 0) {
		free(data);
		return -1;
	}

	struct mpplayerconfig *pc = &g_PlayerConfigsArray[playernum];

	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p);
	if (tok.type != STOK_LBRACE) { free(data); return -1; }

	char key[64];
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p); /* colon */

		if (strcmp(key, "version") == 0) {
			tok = s_next(&p);
			s32 fv = s_tok_int(&tok);
			if (saveCheckFileVersion(fv, "player", path) != 0) {
				free(data);
				return -1;
			}
		} else if (strcmp(key, "name") == 0) {
			tok = s_next(&p);
			s_tok_str(&tok, pc->base.name, 15);
		} else if (strcmp(key, "head_id") == 0) {
			/* SA-4: catalog string ID for head.
			 * Resolve via catalog; mp_index gives the mpheadnum directly. */
			char id_buf[CATALOG_ID_LEN];
			const asset_entry_t *e;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			/* Phase 2: populate PRIMARY catalog ID string field */
			strncpy(pc->base.head_id, id_buf, sizeof(pc->base.head_id) - 1);
			pc->base.head_id[sizeof(pc->base.head_id) - 1] = '\0';
			e = assetCatalogResolve(id_buf);
			if (e && e->type == ASSET_HEAD && e->mp_index >= 0) {
				pc->base.mpheadnum = (u8)e->mp_index;
			}
		} else if (strcmp(key, "body_id") == 0) {
			/* SA-4: catalog string ID for body.
			 * Resolve via catalog; mp_index gives the mpbodynum directly. */
			char id_buf[CATALOG_ID_LEN];
			const asset_entry_t *e;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			/* Phase 2: populate PRIMARY catalog ID string field */
			strncpy(pc->base.body_id, id_buf, sizeof(pc->base.body_id) - 1);
			pc->base.body_id[sizeof(pc->base.body_id) - 1] = '\0';
			e = assetCatalogResolve(id_buf);
			if (e && e->type == ASSET_BODY && e->mp_index >= 0) {
				pc->base.mpbodynum = (u8)e->mp_index;
			}
		} else if (strcmp(key, "mpheadnum") == 0) {
			/* SA-4 v1 fallback: legacy integer field */
			tok = s_next(&p);
			mpchrSetHeadByIndex(&pc->base, s_tok_int(&tok));
		} else if (strcmp(key, "mpbodynum") == 0) {
			/* SA-4 v1 fallback: legacy integer field */
			tok = s_next(&p);
			mpchrSetBodyByIndex(&pc->base, s_tok_int(&tok));
		} else if (strcmp(key, "team") == 0) {
			tok = s_next(&p); pc->base.team = s_tok_int(&tok);
		} else if (strcmp(key, "displayoptions") == 0) {
			tok = s_next(&p); pc->base.displayoptions = s_tok_uint(&tok);
		} else if (strcmp(key, "kills") == 0) {
			tok = s_next(&p); pc->kills = s_tok_uint(&tok);
		} else if (strcmp(key, "deaths") == 0) {
			tok = s_next(&p); pc->deaths = s_tok_uint(&tok);
		} else if (strcmp(key, "gamesplayed") == 0) {
			tok = s_next(&p); pc->gamesplayed = s_tok_uint(&tok);
		} else if (strcmp(key, "gameswon") == 0) {
			tok = s_next(&p); pc->gameswon = s_tok_uint(&tok);
		} else if (strcmp(key, "gameslost") == 0) {
			tok = s_next(&p); pc->gameslost = s_tok_uint(&tok);
		} else if (strcmp(key, "distance") == 0) {
			tok = s_next(&p); pc->distance = s_tok_uint(&tok);
		} else if (strcmp(key, "accuracy") == 0) {
			tok = s_next(&p); pc->accuracy = s_tok_uint(&tok);
		} else if (strcmp(key, "headshots") == 0) {
			tok = s_next(&p); pc->headshots = s_tok_uint(&tok);
		} else if (strcmp(key, "damagedealt") == 0) {
			tok = s_next(&p); pc->damagedealt = s_tok_uint(&tok);
		} else if (strcmp(key, "painreceived") == 0) {
			tok = s_next(&p); pc->painreceived = s_tok_uint(&tok);
		} else if (strcmp(key, "ammoused") == 0) {
			tok = s_next(&p); pc->ammoused = s_tok_uint(&tok);
		} else if (strcmp(key, "accuracymedals") == 0) {
			tok = s_next(&p); pc->accuracymedals = s_tok_uint(&tok);
		} else if (strcmp(key, "headshotmedals") == 0) {
			tok = s_next(&p); pc->headshotmedals = s_tok_uint(&tok);
		} else if (strcmp(key, "killmastermedals") == 0) {
			tok = s_next(&p); pc->killmastermedals = s_tok_uint(&tok);
		} else if (strcmp(key, "survivormedals") == 0) {
			tok = s_next(&p); pc->survivormedals = s_tok_uint(&tok);
		} else if (strcmp(key, "controlmode") == 0) {
			tok = s_next(&p); pc->controlmode = s_tok_int(&tok);
		} else if (strcmp(key, "options") == 0) {
			tok = s_next(&p); pc->options = s_tok_uint(&tok);
		} else {
			s_skip_value(&p, 0);
		}

		tok = s_next(&p);
		if (tok.type == STOK_RBRACE) break;
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "SAVE: MP player '%s' loaded into slot %d", name, playernum);
	return 0;
}

/* ========================================================================
 * MP setup save/load
 * ======================================================================== */

extern s32 g_MpWeaponSetNum;

struct save_mpsetup_snapshot {
	struct mpsetup setup;
	struct matchconfig match;
	struct mpplayerconfig player_configs[MAX_PLAYERS];
	s32 weapon_set;
	s32 bondplayernum;
	s32 coopplayernum;
	s32 antiplayernum;
	s32 mpquickteam;
	u8 handicaps[MAX_PLAYERS];
};

static void saveCaptureMpSetupSnapshot(struct save_mpsetup_snapshot *snapshot)
{
	if (!snapshot) return;
	snapshot->setup = g_MpSetup;
	snapshot->match = g_MatchConfig;
	memcpy(snapshot->player_configs, g_PlayerConfigsArray,
		sizeof(snapshot->player_configs));
	snapshot->weapon_set = g_MpWeaponSetNum;
	snapshot->bondplayernum = g_Vars.bondplayernum;
	snapshot->coopplayernum = g_Vars.coopplayernum;
	snapshot->antiplayernum = g_Vars.antiplayernum;
	snapshot->mpquickteam = g_Vars.mpquickteam;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		snapshot->handicaps[i] = matchGetPlayerHandicap(i);
	}
}

static void saveRestoreMpSetupSnapshot(
		const struct save_mpsetup_snapshot *snapshot)
{
	if (!snapshot) return;
	g_MpSetup = snapshot->setup;
	g_MatchConfig = snapshot->match;
	memcpy(g_PlayerConfigsArray, snapshot->player_configs,
		sizeof(snapshot->player_configs));
	g_MpWeaponSetNum = snapshot->weapon_set;
	g_Vars.bondplayernum = snapshot->bondplayernum;
	g_Vars.coopplayernum = snapshot->coopplayernum;
	g_Vars.antiplayernum = snapshot->antiplayernum;
	g_Vars.mpquickteam = snapshot->mpquickteam;
	for (s32 i = 0; i < MAX_PLAYERS; i++) {
		matchSetPlayerHandicap(i, snapshot->handicaps[i]);
	}
}

s32 saveSaveMpSetup(const char *name)
{
	char path[512];
	save_atomic_file_t transaction;
	buildSavePath(path, sizeof(path), "mpsetup", name, "json");

	if (saveAtomicBegin(&transaction, path) != 0) return -1;
	FILE *fp = saveAtomicStream(&transaction);

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	writeJsonString(fp, "name", name);
	fprintf(fp, ",\n");

	fprintf(fp, "  \"scenario\": %u,\n", g_MpSetup.scenario);
	/* M0.1d: scenario_id is PRIMARY catalog identity for game mode. */
	{
		const char *sid = g_MatchConfig.scenario_id[0]
			? g_MatchConfig.scenario_id
			: catalogGameModeIdByScenarioIndex((s32)g_MpSetup.scenario);
		writeJsonString(fp, "scenario_id", sid ? sid : "");
		fprintf(fp, ",\n");
	}
	/* Phase 5: write PRIMARY catalog ID string directly — no more resolving from deprecated stagenum. */
	writeJsonString(fp, "stage_id", g_MpSetup.stage_id[0] ? g_MpSetup.stage_id : "");
	fprintf(fp, ",\n");
	fprintf(fp, "  \"timelimit\": %u,\n", g_MpSetup.timelimit);
	fprintf(fp, "  \"scorelimit\": %u,\n", g_MpSetup.scorelimit);
	fprintf(fp, "  \"teamscorelimit\": %u,\n", g_MpSetup.teamscorelimit);
	fprintf(fp, "  \"options\": %u,\n", g_MpSetup.options);

	/* B-964: write one authoritative representation. The prior writer emitted
	 * both weapon_ids and numeric weapons, and the reader then let the later
	 * legacy array overwrite the catalog-native selection. */
	fprintf(fp, "  \"weapon_ids\": [");
	for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		const char *wid = g_MatchConfig.weapon_ids[i][0]
			? g_MatchConfig.weapon_ids[i]
			: catalogWeaponIdByMpWeaponId((s32)g_MpSetup.weapons[i]);
		writeJsonStringValue(fp, wid ? wid : "");
		if (i < NUM_MPWEAPONSLOTS - 1) {
			fprintf(fp, ", ");
		}
	}
	fprintf(fp, "],\n");

	/* Bot profiles are catalog-ID-native. type/difficulty are retained as
	 * derived compatibility fields, never as the authored identity. */
	fprintf(fp, "  \"bots\": [\n");
	{
		s32 first = 1;
		for (s32 i = 0; i < g_MatchConfig.numSlots
				&& i < MATCH_MAX_SLOTS; i++) {
			const struct matchslot *slot = &g_MatchConfig.slots[i];
			if (slot->type != SLOT_BOT) {
				continue;
			}
			if (!first) {
				fprintf(fp, ",\n");
			}
			first = 0;
			fprintf(fp, "    {\"profile_id\": ");
			writeJsonStringValue(fp, slot->profile_id);
			fprintf(fp, ", \"name\": ");
			writeJsonStringValue(fp, slot->name);
			fprintf(fp, ", \"body_id\": ");
			writeJsonStringValue(fp, slot->body_id);
			fprintf(fp, ", \"head_id\": ");
			writeJsonStringValue(fp, slot->head_id);
			fprintf(fp, ", \"team\": %u, \"type\": %u, \"difficulty\": %u}",
				(unsigned)slot->team, (unsigned)slot->botType,
				(unsigned)slot->botDifficulty);
		}
		if (!first) {
			fprintf(fp, "\n");
		}
	}
	fprintf(fp, "  ]\n");

	fprintf(fp, "}\n");
	if (saveAtomicCommit(&transaction) != 0) {
		sysLogPrintf(LOG_ERROR,
			"SAVE: MP setup '%s' candidate failed; prior file preserved", name);
		return -1;
	}

	sysLogPrintf(LOG_NOTE, "SAVE: MP setup '%s' saved", name);
	return 0;
}

s32 saveLoadMpSetup(const char *name)
{
	char path[512];
	struct save_mpsetup_snapshot snapshot;
	buildSavePath(path, sizeof(path), "mpsetup", name, "json");

	s32 len = 0;
	char *data = readFileContents(path, &len);
	if (!data) return -1;
	if (savePreflightJson(data, "mpsetup", path) != 0) {
		free(data);
		return -1;
	}

	saveCaptureMpSetupSnapshot(&snapshot);
	matchConfigInit();

	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p);
	if (tok.type != STOK_LBRACE) goto reject;

	char key[64];
	s32 saw_weapon_ids = 0;
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p);

		if (strcmp(key, "version") == 0) {
			tok = s_next(&p);
			s32 fv = s_tok_int(&tok);
			if (saveCheckFileVersion(fv, "mpsetup", path) != 0) {
				goto reject;
			}
		} else if (strcmp(key, "scenario_id") == 0) {
			/* M0.1d: PRIMARY catalog ID for scenario — resolve to integer. */
			char scid_buf[64];
			tok = s_next(&p);
			s_tok_str(&tok, scid_buf, sizeof(scid_buf));
			if (scid_buf[0]) {
				const asset_entry_t *gm = assetCatalogResolve(scid_buf);
				if (gm && gm->type == ASSET_GAMEMODE) {
					g_MpSetup.scenario = (u8)gm->ext.gamemode.mode_id;
				}
				strncpy(g_MatchConfig.scenario_id, scid_buf, sizeof(g_MatchConfig.scenario_id) - 1);
				g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';
			}
		} else if (strcmp(key, "scenario") == 0) {
			/* Legacy integer fallback — only used if scenario_id absent. */
			tok = s_next(&p);
			if (!g_MatchConfig.scenario_id[0]) {
				g_MpSetup.scenario = s_tok_int(&tok);
			}
		} else if (strcmp(key, "stage_id") == 0) {
			/* SA-4: catalog string ID for stage */
			char id_buf[CATALOG_ID_LEN];
			s32 idx;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			idx = assetCatalogResolveStageIndex(id_buf);
			if (idx >= 0) {
				g_MpSetup.stagenum = (u8)idx;
				strncpy(g_MpSetup.stage_id, id_buf,
					sizeof(g_MpSetup.stage_id) - 1);
				g_MpSetup.stage_id[sizeof(g_MpSetup.stage_id) - 1] = '\0';
				strncpy(g_MatchConfig.stage_id, id_buf,
					sizeof(g_MatchConfig.stage_id) - 1);
				g_MatchConfig.stage_id[
					sizeof(g_MatchConfig.stage_id) - 1] = '\0';
			}
		} else if (strcmp(key, "stagenum") == 0) {
			/* SA-4 v1 fallback: legacy integer field */
			tok = s_next(&p); g_MpSetup.stagenum = s_tok_int(&tok);
		} else if (strcmp(key, "timelimit") == 0) {
			tok = s_next(&p); g_MpSetup.timelimit = s_tok_int(&tok);
		} else if (strcmp(key, "scorelimit") == 0) {
			tok = s_next(&p); g_MpSetup.scorelimit = s_tok_int(&tok);
		} else if (strcmp(key, "teamscorelimit") == 0) {
			tok = s_next(&p); g_MpSetup.teamscorelimit = s_tok_int(&tok);
		} else if (strcmp(key, "options") == 0) {
			tok = s_next(&p); g_MpSetup.options = s_tok_uint(&tok);
		} else if (strcmp(key, "weapon_ids") == 0) {
			/* Catalog IDs are authoritative whenever this field is present. */
			tok = s_next(&p);
			if (tok.type == STOK_LBRACKET) {
				saw_weapon_ids = 1;
				for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
					char wid_buf[128];
					tok = s_next(&p);
					s_tok_str(&tok, wid_buf, sizeof(wid_buf));
					if (wid_buf[0]) {
						const asset_entry_t *we = assetCatalogResolve(wid_buf);
						if (!we || we->type != ASSET_WEAPON) {
							sysLogPrintf(LOG_ERROR,
								"SAVE: MP setup weapon slot %d catalog ID '%s' "
								"is unavailable", i, wid_buf);
							goto reject;
						}
						strncpy(g_MatchConfig.weapon_ids[i], we->id,
							sizeof(g_MatchConfig.weapon_ids[i]) - 1);
						g_MatchConfig.weapon_ids[i][
							sizeof(g_MatchConfig.weapon_ids[i]) - 1] = '\0';
						if (we->ext.weapon.weapon_id >= 0
								&& we->ext.weapon.weapon_id < NUM_MPWEAPONS) {
							g_MpSetup.weapons[i] =
								(u8)we->ext.weapon.weapon_id;
						} else {
							g_MpSetup.weapons[i] = MPWEAPON_DISABLED;
						}
					} else {
						g_MatchConfig.weapon_ids[i][0] = '\0';
					}
					tok = s_next(&p);
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else if (strcmp(key, "weapons") == 0) {
			/* Read-only migration input. Parse it in all cases so token state
			 * remains correct, but never overwrite canonical weapon_ids. */
			tok = s_next(&p);
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
					tok = s_next(&p);
					s32 legacy_weapon = s_tok_int(&tok);
					if (!saw_weapon_ids) {
						if (legacy_weapon < 0
								|| legacy_weapon >= NUM_MPWEAPONS) {
							sysLogPrintf(LOG_ERROR,
								"SAVE: legacy MP setup weapon slot %d value "
								"%d is out of range", i, legacy_weapon);
							goto reject;
						}
						const char *wid =
							catalogWeaponIdByMpWeaponId(legacy_weapon);
						if (!wid) {
							sysLogPrintf(LOG_ERROR,
								"SAVE: legacy MP setup weapon slot %d value "
								"%d has no catalog identity", i,
								legacy_weapon);
							goto reject;
						}
						g_MpSetup.weapons[i] = (u8)legacy_weapon;
						strncpy(g_MatchConfig.weapon_ids[i], wid,
							sizeof(g_MatchConfig.weapon_ids[i]) - 1);
						g_MatchConfig.weapon_ids[i][
							sizeof(g_MatchConfig.weapon_ids[i]) - 1] = '\0';
					}
					tok = s_next(&p);
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else if (strcmp(key, "bots") == 0) {
			tok = s_next(&p);
			if (tok.type == STOK_LBRACKET) {
				for (;;) {
					char profile_id[CATALOG_ID_LEN] = {0};
					char bot_name[SAVE_NAME_MAX] = {0};
					char body_id[CATALOG_ID_LEN] = {0};
					char head_id[CATALOG_ID_LEN] = {0};
					s32 team = 0;
					s32 type = BOTTYPE_GENERAL;
					s32 difficulty = BOTDIFF_NORMAL;

					tok = s_next(&p);
					if (tok.type == STOK_RBRACKET) {
						break;
					}
					if (tok.type == STOK_COMMA) {
						tok = s_next(&p);
					}
					if (tok.type != STOK_LBRACE) {
						s_skip_value(&p, 0);
						continue;
					}

					for (;;) {
						char bot_key[64];
						tok = s_next(&p);
						if (tok.type == STOK_RBRACE) {
							break;
						}
						if (tok.type == STOK_COMMA) {
							tok = s_next(&p);
						}
						if (tok.type != STOK_STRING) {
							s_skip_value(&p, 0);
							continue;
						}
						s_tok_str(&tok, bot_key, sizeof(bot_key));
						s_next(&p);
						tok = s_next(&p);

						if (strcmp(bot_key, "profile_id") == 0) {
							s_tok_str(&tok, profile_id,
								sizeof(profile_id));
						} else if (strcmp(bot_key, "name") == 0) {
							s_tok_str(&tok, bot_name,
								sizeof(bot_name));
						} else if (strcmp(bot_key, "body_id") == 0) {
							s_tok_str(&tok, body_id,
								sizeof(body_id));
						} else if (strcmp(bot_key, "head_id") == 0) {
							s_tok_str(&tok, head_id,
								sizeof(head_id));
						} else if (strcmp(bot_key, "team") == 0) {
							team = s_tok_int(&tok);
						} else if (strcmp(bot_key, "type") == 0) {
							type = s_tok_int(&tok);
						} else if (strcmp(bot_key, "difficulty") == 0) {
							difficulty = s_tok_int(&tok);
						}
					}

					s32 slot = profile_id[0]
						? matchConfigAddBotWithProfile(profile_id,
							body_id[0] ? body_id : NULL,
							head_id[0] ? head_id : NULL,
							bot_name[0] ? bot_name : NULL)
						: matchConfigAddBot((u8)type, (u8)difficulty,
							body_id[0] ? body_id : NULL,
							head_id[0] ? head_id : NULL,
							bot_name[0] ? bot_name : NULL);
					if (slot >= 0) {
						g_MatchConfig.slots[slot].team =
							(u8)(team >= 0 && team < MAX_TEAMS ? team : 0);
					} else {
						sysLogPrintf(LOG_ERROR,
							"SAVE: MP setup bot profile '%s' could not be "
							"reconstructed", profile_id[0]
								? profile_id : "(legacy traits)");
						goto reject;
					}
				}
			}
		} else {
			s_skip_value(&p, 0);
		}

		tok = s_next(&p);
		if (tok.type == STOK_RBRACE) break;
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "SAVE: MP setup '%s' loaded", name);
	return 0;

reject:
	free(data);
	saveRestoreMpSetupSnapshot(&snapshot);
	sysLogPrintf(LOG_ERROR,
		"SAVE: MP setup '%s' rejected; prior live setup preserved", name);
	return -1;
}

/* ========================================================================
 * Agent listing
 * ======================================================================== */

static int saveAgentSummaryCompare(const void *left, const void *right)
{
	const struct saveagentsummary *a = (const struct saveagentsummary *)left;
	const struct saveagentsummary *b = (const struct saveagentsummary *)right;
	s32 folded = strcasecmp(a->name, b->name);
	return folded != 0 ? folded : strcmp(a->name, b->name);
}

s32 saveListAgentProfiles(struct saveagentsummary *profiles, s32 maxcount)
{
	struct saveagentsummary *found = NULL;
	s32 found_count = 0;
	s32 found_capacity = 0;
	s32 published_count;
	DIR *d;
	struct dirent *ent;
	const char *dir;

	if (!profiles || maxcount <= 0) return 0;

	dir = saveGetDir();
	if (!dir || !dir[0]) {
		return 0;
	}

	d = opendir(dir);
	if (!d) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: saveListAgentProfiles: failed to open '%s'", dir);
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		const char *fname = ent->d_name;
		char path[FS_MAXPATH];
		char expected_path[FS_MAXPATH];
		char *data;
		s32 filelen = 0;
		s32 duplicate = 0;
		s32 sidecar_found = 0;
		char sidecar_error[256];
		struct agent_profile_document document;
		struct agent_profile_preferences merged_preferences;
		enum agent_profile_source_shape source;
		struct saveagentsummary summary;

		if (strncmp(fname, "agent_", 6) != 0) continue;
		s32 flen = (s32)strlen(fname);
		if (flen < 12 || strcmp(fname + flen - 5, ".json") != 0) {
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", dir, fname);
		data = readFileContents(path, &filelen);
		if (!data) continue;
		if (saveParseAgentDocument(data, path, NULL, &document, &source) != 0) {
			free(data);
			continue;
		}
		free(data);
		if (source != AGENT_PROFILE_SOURCE_CURRENT
				&& prefsAgentReadLegacySidecar(document.name,
					&document.preferences, &merged_preferences,
					&sidecar_found, sidecar_error,
					sizeof(sidecar_error)) != 0) {
			sysLogPrintf(LOG_ERROR,
				"SAVE: excluding legacy Agent Profile '%s' because its sidecar is invalid: %s",
				document.name, sidecar_error);
			continue;
		}

		/* The embedded stable identity must reproduce the file's canonical
		 * storage path. This rejects manually renamed aliases. */
		buildSavePath(expected_path, sizeof(expected_path), "agent",
			document.name, "json");
		if (strcasecmp(path, expected_path) != 0) {
			sysLogPrintf(LOG_ERROR,
				"SAVE: rejecting aliased agent file path='%s' identity='%s' expected='%s'",
				path, document.name, expected_path);
			continue;
		}

		for (s32 i = 0; i < found_count; i++) {
			if (strcasecmp(found[i].name, document.name) == 0) {
				duplicate = 1;
				break;
			}
		}
		if (duplicate) {
			sysLogPrintf(LOG_ERROR,
				"SAVE: rejecting duplicate agent identity '%s' from %s",
				document.name, path);
			continue;
		}

		if (found_count == found_capacity) {
			s32 next_capacity = found_capacity ? found_capacity * 2 : 16;
			struct saveagentsummary *grown = (struct saveagentsummary *)realloc(
				found, (size_t)next_capacity * sizeof(*found));
			if (!grown) {
				sysLogPrintf(LOG_ERROR,
					"SAVE: out of memory while listing agent profiles");
				break;
			}
			found = grown;
			found_capacity = next_capacity;
		}

		memset(&summary, 0, sizeof(summary));
		strncpy(summary.name, document.name, sizeof(summary.name) - 1);
		summary.totaltime = document.totaltime;
		summary.autodifficulty = document.autodifficulty;
		summary.autostageindex = document.autostageindex;
		summary.thumbnail = document.thumbnail;
		found[found_count++] = summary;
	}

	closedir(d);
	if (found_count > 1) {
		qsort(found, (size_t)found_count, sizeof(*found), saveAgentSummaryCompare);
	}
	published_count = found_count;
	if (published_count > SAVE_MAX_AGENTS) published_count = SAVE_MAX_AGENTS;
	if (published_count > maxcount) published_count = maxcount;
	for (s32 i = 0; i < published_count; i++) {
		profiles[i] = found[i];
	}
	if (found_count > SAVE_MAX_AGENTS) {
		sysLogPrintf(LOG_WARNING,
			"SAVE: %d valid agent profiles exceed the supported capacity %d; publishing first %d in name order",
			found_count, SAVE_MAX_AGENTS, published_count);
	}
	free(found);
	sysLogPrintf(LOG_NOTE,
		"SAVE: Agent Profile Store published %d validated profile(s)",
		published_count);
	return published_count;
}

s32 saveListAgents(char names[][SAVE_NAME_MAX], s32 maxcount)
{
	struct saveagentsummary profiles[SAVE_MAX_AGENTS];
	s32 count;

	if (!names || maxcount <= 0) return 0;
	if (maxcount > SAVE_MAX_AGENTS) maxcount = SAVE_MAX_AGENTS;
	count = saveListAgentProfiles(profiles, maxcount);
	for (s32 i = 0; i < count; i++) {
		strncpy(names[i], profiles[i].name, SAVE_NAME_MAX - 1);
		names[i][SAVE_NAME_MAX - 1] = '\0';
	}
	return count;
}

/* ========================================================================
 * EEPROM migration (stub — full implementation later)
 * ======================================================================== */

s32 saveMigrateFromEeprom(void)
{
	sysLogPrintf(LOG_NOTE, "SAVE: EEPROM migration — reading old save data...");

	/* The migration flow:
	 * 1. Load eeprom.bin into memory (same as osEeepromLoad)
	 * 2. Parse the pak filesystem to find all file entries
	 * 3. For each PAKFILETYPE_GAME: read via savebuffer, write agent_<name>.json
	 * 4. For each PAKFILETYPE_MPPLAYER: read via savebuffer, write player_<name>.json
	 * 5. For each PAKFILETYPE_MPSETUP: read via savebuffer, write mpsetup_<name>.json
	 * 6. For PAKFILETYPE_BOSS: read via savebuffer, write system.json
	 * 7. Rename eeprom.bin to eeprom.bin.bak
	 *
	 * This requires calling the existing pak/savebuffer functions to
	 * deserialize the old format, then our new JSON writer to serialize.
	 * Will be implemented when we wire up the full save flow. */

	sysLogPrintf(LOG_NOTE, "SAVE: migration stub — not yet implemented");
	return 0;
}
