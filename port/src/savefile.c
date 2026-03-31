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
#include "assetcatalog.h"
#include "fs.h"

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
		tok.type = STOK_STRING;
		if (*p->pos == '"') p->pos++;
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

static void s_skip_value(sparse_t *p)
{
	stok_t tok = s_next(p);
	if (tok.type == STOK_LBRACE) {
		while ((tok = s_next(p)).type != STOK_RBRACE && tok.type != STOK_EOF) {
			if (tok.type == STOK_STRING) { s_next(p); s_skip_value(p); }
		}
	} else if (tok.type == STOK_LBRACKET) {
		while ((tok = s_next(p)).type != STOK_RBRACKET && tok.type != STOK_EOF) {
			if (tok.type == STOK_COMMA) continue;
			/* value already consumed by s_next for primitives, recurse for nested */
			if (tok.type == STOK_LBRACE || tok.type == STOK_LBRACKET) {
				/* back up and recurse */
				p->pos = tok.start;
				s_skip_value(p);
			}
		}
	}
	/* primitives are already consumed by s_next */
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

	char *buf = (char *)malloc(len + 1);
	if (!buf) { fclose(fp); return NULL; }

	fread(buf, 1, len, fp);
	buf[len] = '\0';
	fclose(fp);

	if (outLen) *outLen = (s32)len;
	return buf;
}

static void writeJsonString(FILE *fp, const char *key, const char *value)
{
	/* Escape special chars in value */
	fprintf(fp, "  \"%s\": \"", key);
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
	fprintf(fp, "\"");
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
	const char *saveDir = fsFullPath("$S");
	if (saveDir) {
		strncpy(s_SaveDir, saveDir, sizeof(s_SaveDir) - 1);
	} else {
		strncpy(s_SaveDir, ".", sizeof(s_SaveDir) - 1);
	}
	s_SaveDir[sizeof(s_SaveDir) - 1] = '\0';

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

s32 saveSaveAgent(const char *name)
{
	char path[512];
	buildSavePath(path, sizeof(path), "agent", name, "json");

	FILE *fp = fopen(path, "w");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "SAVE: failed to write agent '%s' to %s", name, path);
		return -1;
	}

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	writeJsonString(fp, "name", name);
	fprintf(fp, ",\n");

	/* Campaign data from g_GameFile */
	fprintf(fp, "  \"totaltime\": %u,\n", g_GameFile.totaltime);
	fprintf(fp, "  \"autodifficulty\": %u,\n", g_GameFile.autodifficulty);
	fprintf(fp, "  \"autostageindex\": %u,\n", g_GameFile.autostageindex);
	fprintf(fp, "  \"thumbnail\": %u,\n", g_GameFile.thumbnail);

	/* Best times array */
	fprintf(fp, "  \"besttimes\": [\n");
	for (s32 s = 0; s < NUM_SOLOSTAGES; s++) {
		fprintf(fp, "    [%u, %u, %u]%s\n",
		        g_GameFile.besttimes[s][0],
		        g_GameFile.besttimes[s][1],
		        g_GameFile.besttimes[s][2],
		        s < NUM_SOLOSTAGES - 1 ? "," : "");
	}
	fprintf(fp, "  ],\n");

	/* Co-op completions */
	fprintf(fp, "  \"coopcompletions\": [%d, %d, %d],\n",
	        g_GameFile.coopcompletions[0], g_GameFile.coopcompletions[1], g_GameFile.coopcompletions[2]);

	/* Firing range scores */
	fprintf(fp, "  \"firingrangescores\": [");
	for (s32 i = 0; i < 9; i++) {
		fprintf(fp, "%u%s", g_GameFile.firingrangescores[i], i < 8 ? ", " : "");
	}
	fprintf(fp, "],\n");

	/* Weapons found */
	fprintf(fp, "  \"weaponsfound\": [");
	for (s32 i = 0; i < 6; i++) {
		fprintf(fp, "%u%s", g_GameFile.weaponsfound[i], i < 5 ? ", " : "");
	}
	fprintf(fp, "]\n");

	fprintf(fp, "}\n");
	fclose(fp);

	sysLogPrintf(LOG_NOTE, "SAVE: agent '%s' saved to %s", name, path);
	return 0;
}

s32 saveLoadAgent(const char *name)
{
	char path[512];
	buildSavePath(path, sizeof(path), "agent", name, "json");

	s32 len = 0;
	char *data = readFileContents(path, &len);
	if (!data) {
		sysLogPrintf(LOG_WARNING, "SAVE: failed to load agent '%s' from %s", name, path);
		return -1;
	}

	/* Clear current game file */
	memset(&g_GameFile, 0, sizeof(g_GameFile));

	/* Parse JSON */
	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p); /* opening { */

	if (tok.type != STOK_LBRACE) {
		free(data);
		return -1;
	}

	char key[64];
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p); /* colon */

		if (strcmp(key, "name") == 0) {
			tok = s_next(&p);
			s_tok_str(&tok, g_GameFile.name, 11); /* engine limit */
		} else if (strcmp(key, "totaltime") == 0) {
			tok = s_next(&p);
			g_GameFile.totaltime = s_tok_uint(&tok);
		} else if (strcmp(key, "autodifficulty") == 0) {
			tok = s_next(&p);
			g_GameFile.autodifficulty = s_tok_int(&tok);
		} else if (strcmp(key, "autostageindex") == 0) {
			tok = s_next(&p);
			g_GameFile.autostageindex = s_tok_int(&tok);
		} else if (strcmp(key, "thumbnail") == 0) {
			tok = s_next(&p);
			g_GameFile.thumbnail = s_tok_int(&tok);
		} else if (strcmp(key, "besttimes") == 0) {
			tok = s_next(&p); /* [ */
			if (tok.type == STOK_LBRACKET) {
				for (s32 s = 0; s < NUM_SOLOSTAGES; s++) {
					tok = s_next(&p); /* [ */
					if (tok.type == STOK_LBRACKET) {
						for (s32 d = 0; d < 3; d++) {
							tok = s_next(&p);
							g_GameFile.besttimes[s][d] = s_tok_int(&tok);
							s_next(&p); /* comma or ] */
						}
					}
					tok = s_next(&p); /* comma or ] */
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else if (strcmp(key, "coopcompletions") == 0) {
			tok = s_next(&p); /* [ */
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < 3; i++) {
					tok = s_next(&p);
					g_GameFile.coopcompletions[i] = s_tok_int(&tok);
					s_next(&p); /* comma or ] */
				}
			}
		} else if (strcmp(key, "firingrangescores") == 0) {
			tok = s_next(&p); /* [ */
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < 9; i++) {
					tok = s_next(&p);
					g_GameFile.firingrangescores[i] = s_tok_int(&tok);
					tok = s_next(&p); /* comma or ] */
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else if (strcmp(key, "weaponsfound") == 0) {
			tok = s_next(&p); /* [ */
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < 6; i++) {
					tok = s_next(&p);
					g_GameFile.weaponsfound[i] = s_tok_int(&tok);
					tok = s_next(&p); /* comma or ] */
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else {
			/* Unknown key — skip value for forward compatibility */
			s_skip_value(&p);
		}

		/* Consume comma between fields */
		tok = s_next(&p);
		if (tok.type == STOK_RBRACE) break;
		/* if comma, continue loop */
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "SAVE: agent '%s' loaded from %s", name, path);
	return 0;
}

s32 saveCreateAgent(const char *name)
{
	/* Check if already exists */
	char path[512];
	buildSavePath(path, sizeof(path), "agent", name, "json");

	struct stat st;
	if (stat(path, &st) == 0) {
		sysLogPrintf(LOG_WARNING, "SAVE: agent '%s' already exists", name);
		return -1;
	}

	/* Initialize a fresh game file */
	memset(&g_GameFile, 0, sizeof(g_GameFile));
	strncpy(g_GameFile.name, name, 10);
	g_GameFile.name[10] = '\0';

	return saveSaveAgent(name);
}

s32 saveDeleteAgent(const char *name)
{
	char path[512];
	buildSavePath(path, sizeof(path), "agent", name, "json");

	if (remove(path) == 0) {
		sysLogPrintf(LOG_NOTE, "SAVE: agent '%s' deleted", name);
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
	snprintf(path, sizeof(path), "%s/system.json", s_SaveDir);

	FILE *fp = fopen(path, "w");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "SAVE: failed to write system settings to %s", path);
		return -1;
	}

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
	fclose(fp);

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

		if (strcmp(key, "language") == 0) {
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
			s_skip_value(&p);
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
	buildSavePath(path, sizeof(path), "player", name, "json");

	FILE *fp = fopen(path, "w");
	if (!fp) return -1;

	struct mpplayerconfig *pc = &g_PlayerConfigsArray[playernum];

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	writeJsonString(fp, "name", name);
	fprintf(fp, ",\n");

	/* Appearance — SA-4: write catalog string IDs */
	{
		const char *head_id = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)pc->base.mpheadnum);
		const char *body_id = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)pc->base.mpbodynum);
		writeJsonString(fp, "head_id", head_id ? head_id : "");
		fprintf(fp, ",\n");
		writeJsonString(fp, "body_id", body_id ? body_id : "");
		fprintf(fp, ",\n");
	}
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
	fclose(fp);

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

	struct mpplayerconfig *pc = &g_PlayerConfigsArray[playernum];

	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p);
	if (tok.type != STOK_LBRACE) { free(data); return -1; }

	char key[64];
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p); /* colon */

		if (strcmp(key, "name") == 0) {
			tok = s_next(&p);
			s_tok_str(&tok, pc->base.name, 15);
		} else if (strcmp(key, "head_id") == 0) {
			/* SA-4: catalog string ID for head */
			char id_buf[CATALOG_ID_LEN];
			const asset_entry_t *e;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			e = assetCatalogResolve(id_buf);
			if (e && e->type == ASSET_HEAD)
				pc->base.mpheadnum = (u8)e->runtime_index;
		} else if (strcmp(key, "body_id") == 0) {
			/* SA-4: catalog string ID for body */
			char id_buf[CATALOG_ID_LEN];
			const asset_entry_t *e;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			e = assetCatalogResolve(id_buf);
			if (e && e->type == ASSET_BODY)
				pc->base.mpbodynum = (u8)e->runtime_index;
		} else if (strcmp(key, "mpheadnum") == 0) {
			/* SA-4 v1 fallback: legacy integer field */
			tok = s_next(&p); pc->base.mpheadnum = s_tok_int(&tok);
		} else if (strcmp(key, "mpbodynum") == 0) {
			/* SA-4 v1 fallback: legacy integer field */
			tok = s_next(&p); pc->base.mpbodynum = s_tok_int(&tok);
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
			s_skip_value(&p);
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

s32 saveSaveMpSetup(const char *name)
{
	char path[512];
	buildSavePath(path, sizeof(path), "mpsetup", name, "json");

	FILE *fp = fopen(path, "w");
	if (!fp) return -1;

	fprintf(fp, "{\n");
	fprintf(fp, "  \"version\": %d,\n", SAVE_VERSION);
	writeJsonString(fp, "name", name);
	fprintf(fp, ",\n");

	fprintf(fp, "  \"scenario\": %u,\n", g_MpSetup.scenario);
	/* SA-4: write stage as catalog string ID */
	{
		const char *stage_id = catalogResolveByRuntimeIndex(ASSET_MAP, (s32)g_MpSetup.stagenum);
		writeJsonString(fp, "stage_id", stage_id ? stage_id : "");
		fprintf(fp, ",\n");
	}
	fprintf(fp, "  \"timelimit\": %u,\n", g_MpSetup.timelimit);
	fprintf(fp, "  \"scorelimit\": %u,\n", g_MpSetup.scorelimit);
	fprintf(fp, "  \"teamscorelimit\": %u,\n", g_MpSetup.teamscorelimit);
	fprintf(fp, "  \"options\": %u,\n", g_MpSetup.options);

	/* Weapons */
	fprintf(fp, "  \"weapons\": [");
	for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
		fprintf(fp, "%u%s", g_MpSetup.weapons[i], i < NUM_MPWEAPONSLOTS - 1 ? ", " : "");
	}
	fprintf(fp, "]\n");

	fprintf(fp, "}\n");
	fclose(fp);

	sysLogPrintf(LOG_NOTE, "SAVE: MP setup '%s' saved", name);
	return 0;
}

s32 saveLoadMpSetup(const char *name)
{
	char path[512];
	buildSavePath(path, sizeof(path), "mpsetup", name, "json");

	s32 len = 0;
	char *data = readFileContents(path, &len);
	if (!data) return -1;

	sparse_t p = { data, { NULL, 0, STOK_NONE } };
	stok_t tok = s_next(&p);
	if (tok.type != STOK_LBRACE) { free(data); return -1; }

	char key[64];
	while ((tok = s_next(&p)).type == STOK_STRING) {
		s_tok_str(&tok, key, sizeof(key));
		s_next(&p);

		if (strcmp(key, "scenario") == 0) {
			tok = s_next(&p); g_MpSetup.scenario = s_tok_int(&tok);
		} else if (strcmp(key, "stage_id") == 0) {
			/* SA-4: catalog string ID for stage */
			char id_buf[CATALOG_ID_LEN];
			s32 idx;
			tok = s_next(&p);
			s_tok_str(&tok, id_buf, sizeof(id_buf));
			idx = assetCatalogResolveStageIndex(id_buf);
			if (idx >= 0)
				g_MpSetup.stagenum = (u8)idx;
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
		} else if (strcmp(key, "weapons") == 0) {
			tok = s_next(&p);
			if (tok.type == STOK_LBRACKET) {
				for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
					tok = s_next(&p);
					g_MpSetup.weapons[i] = s_tok_int(&tok);
					tok = s_next(&p);
					if (tok.type == STOK_RBRACKET) break;
				}
			}
		} else {
			s_skip_value(&p);
		}

		tok = s_next(&p);
		if (tok.type == STOK_RBRACE) break;
	}

	free(data);
	sysLogPrintf(LOG_NOTE, "SAVE: MP setup '%s' loaded", name);
	return 0;
}

/* ========================================================================
 * Agent listing
 * ======================================================================== */

s32 saveListAgents(char names[][SAVE_NAME_MAX], s32 maxcount)
{
	if (!names || maxcount <= 0) {
		return 0;
	}

	const char *dir = saveGetDir();
	if (!dir || !dir[0]) {
		return 0;
	}

	DIR *d = opendir(dir);
	if (!d) {
		sysLogPrintf(LOG_WARNING, "SAVE: saveListAgents: failed to open '%s'", dir);
		return 0;
	}

	s32 count = 0;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL && count < maxcount) {
		const char *fname = ent->d_name;

		/* Match files with the "agent_" prefix and ".json" suffix */
		if (strncmp(fname, "agent_", 6) != 0) {
			continue;
		}
		s32 flen = (s32)strlen(fname);
		if (flen < 12 || strcmp(fname + flen - 5, ".json") != 0) {
			continue; /* minimum: "agent_x.json" = 12 chars */
		}

		/* Read the file and extract the "name" field value */
		char path[FS_MAXPATH];
		snprintf(path, sizeof(path), "%s/%s", dir, fname);

		s32 filelen = 0;
		char *buf = readFileContents(path, &filelen);
		if (!buf) {
			continue;
		}

		/* Scan for "name": "<value>" — find first occurrence of the key */
		const char *p = buf;
		const char *namestart = NULL;
		while (*p) {
			if (strncmp(p, "\"name\"", 6) == 0) {
				p += 6;
				while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; }
				if (*p == ':') { p++; }
				while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { p++; }
				if (*p == '"') {
					namestart = p + 1;
				}
				break;
			}
			p++;
		}

		if (namestart) {
			s32 j = 0;
			while (*namestart && *namestart != '"' && j < SAVE_NAME_MAX - 1) {
				names[count][j++] = *namestart++;
			}
			names[count][j] = '\0';
			if (j > 0) {
				count++;
			}
		} else {
			sysLogPrintf(LOG_WARNING, "SAVE: %s has no 'name' field, skipping", fname);
		}

		free(buf);
	}

	closedir(d);
	sysLogPrintf(LOG_NOTE, "SAVE: found %d agent profile(s)", count);
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
