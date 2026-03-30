/**
 * scenario_save.c -- Combat Simulator scenario save/load system.
 *
 * Saves and loads complete Combat Simulator match configurations (arena,
 * scenario, limits, options, weapon set, bot roster) as JSON files in
 * the user's save directory ($S/scenarios/).
 *
 * File format (version 1):
 *   {
 *     "version": 1,
 *     "name": "My Setup",
 *     "arena": 31,
 *     "scenario": 0,
 *     "timelimit": 60,
 *     "scorelimit": 9,
 *     "teamscorelimit": 400,
 *     "options": 0,
 *     "weaponset": 0,
 *     "bots": [
 *       {"name": "NormalSim", "difficulty": 2, "body": 0, "head": 0},
 *       ...
 *     ]
 *   }
 *
 * Dynamic player count on load:
 *   Human players always fill slots first (slot 0 = local player from
 *   matchConfigInit).  Bots from the file fill remaining slots in order,
 *   up to (MATCH_MAX_SLOTS - humanCount).  Excess bots are silently dropped.
 *
 * Auto-discovered by CMake GLOB_RECURSE for port/*.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

/* Game engine headers — must come before scenario_save.h so that
 * MATCH_MAX_SLOTS is already defined as PARTICIPANT_DEFAULT_CAPACITY
 * before the #ifndef guard in scenario_save.h fires. */
#include "game/mplayer/participant.h"   /* PARTICIPANT_DEFAULT_CAPACITY */

#include "scenario_save.h"
#include "fs.h"
#include "system.h"

/* g_MatchConfig is defined in matchsetup.c (the match configuration module).
 * scenario_save.c uses it via the extern in scenario_save.h. */

/* mpSetWeaponSet: applies weapon set index to g_MpSetup.weapons[]. */
#include "game/mplayer/mplayer.h"

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

/**
 * getSaveDir -- copy the expanded save directory path into out[size].
 *
 * fsFullPath("$S") expands $S → the configured save directory.
 * The result is copied immediately because fsFullPath uses a static buffer.
 */
static void getSaveDir(char *out, s32 size)
{
    const char *sd = fsFullPath("$S");
    strncpy(out, sd, (size_t)(size - 1));
    out[size - 1] = '\0';
}

/**
 * getScenarioDir -- build the scenarios subdirectory path.
 */
static void getScenarioDir(char *out, s32 size)
{
    char savedir[SCENARIO_PATH_MAX];
    getSaveDir(savedir, sizeof(savedir));
    snprintf(out, (size_t)size, "%s/scenarios", savedir);
}

/**
 * sanitizeName -- produce a safe filename component from user input.
 *
 * Allows: alphanumerics, spaces, hyphens, underscores, periods.
 * Everything else becomes '_'.  Leading/trailing spaces are stripped.
 * Empty result falls back to "scenario".
 */
static void sanitizeName(const char *in, char *out, s32 maxlen)
{
    s32 j = 0;
    for (s32 i = 0; in[i] && j < maxlen - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == ' ' || c == '-' || c == '_' || c == '.') {
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';

    /* Strip leading spaces */
    s32 start = 0;
    while (out[start] == ' ') start++;
    if (start > 0) memmove(out, out + start, (size_t)(j - start + 1));

    /* Strip trailing spaces */
    s32 len = (s32)strlen(out);
    while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';

    if (out[0] == '\0') {
        strncpy(out, "scenario", (size_t)(maxlen - 1));
        out[maxlen - 1] = '\0';
    }
}

/**
 * jsonEscapeStr -- write a JSON-escaped string (without surrounding quotes)
 * to fp.  Escapes " and \ only; we do not expect control characters in names.
 */
static void jsonEscapeStr(FILE *fp, const char *s)
{
    for (; *s; s++) {
        if (*s == '"')       fputs("\\\"", fp);
        else if (*s == '\\') fputs("\\\\", fp);
        else                 fputc(*s, fp);
    }
}

/* ========================================================================
 * Minimal JSON parser (read-only, for scenario files)
 *
 * The parser only handles the simple flat format we write ourselves.
 * It is NOT a general-purpose JSON parser.
 * ======================================================================== */

/**
 * jsonFindString -- find "key": "value" in json, copy value into out[maxlen].
 * Returns 1 on success, 0 if key not found or value is not a string.
 */
static s32 jsonFindString(const char *json, const char *key,
                           char *out, s32 maxlen)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);

    /* Skip optional whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p != '"') return 0;
    p++;  /* skip opening quote */

    s32 i = 0;
    while (*p && *p != '"' && i < maxlen - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;  /* skip backslash */
            switch (*p) {
                case '"':  out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                case '/':  out[i++] = '/';  break;
                case 'n':  out[i++] = '\n'; break;
                case 'r':  out[i++] = '\r'; break;
                case 't':  out[i++] = '\t'; break;
                default:   out[i++] = *p;   break;
            }
            p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 1;
}

/**
 * jsonFindInt -- find "key": <integer> in json, store in *out.
 * Returns 1 on success, 0 if not found.
 */
static s32 jsonFindInt(const char *json, const char *key, s32 *out)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    /* Accept optional negative sign */
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        *out = (s32)strtol(p, NULL, 10);
        return 1;
    }
    return 0;
}

/**
 * jsonFindUInt -- find "key": <unsigned integer> in json, store in *out.
 * Returns 1 on success, 0 if not found.
 */
static s32 jsonFindUInt(const char *json, const char *key, u32 *out)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (*p >= '0' && *p <= '9') {
        *out = (u32)strtoul(p, NULL, 10);
        return 1;
    }
    return 0;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

s32 scenarioSave(const char *name)
{
    if (!name || !name[0]) {
        sysLogPrintf(LOG_WARNING, "SCENARIO: scenarioSave called with empty name");
        return -1;
    }

    /* Ensure $S/scenarios/ exists */
    char scenDir[SCENARIO_PATH_MAX];
    getScenarioDir(scenDir, sizeof(scenDir));

    /* fsCreateDir is idempotent — ignore error if dir already exists */
    fsCreateDir(scenDir);

    /* Build safe filename */
    char safeName[128];
    sanitizeName(name, safeName, sizeof(safeName));

    char filepath[SCENARIO_PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", scenDir, safeName);

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        sysLogPrintf(LOG_WARNING, "SCENARIO: failed to open '%s' for writing", filepath);
        return -1;
    }

    /* --- Write JSON --- */
    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": 1,\n");
    fprintf(fp, "  \"name\": \"");
    jsonEscapeStr(fp, name);
    fprintf(fp, "\",\n");
    fprintf(fp, "  \"arena\": %u,\n",        (unsigned)g_MatchConfig.stagenum);
    fprintf(fp, "  \"scenario\": %u,\n",     (unsigned)g_MatchConfig.scenario);
    fprintf(fp, "  \"timelimit\": %u,\n",    (unsigned)g_MatchConfig.timelimit);
    fprintf(fp, "  \"scorelimit\": %u,\n",   (unsigned)g_MatchConfig.scorelimit);
    fprintf(fp, "  \"teamscorelimit\": %u,\n",(unsigned)g_MatchConfig.teamscorelimit);
    fprintf(fp, "  \"options\": %u,\n",      (unsigned)g_MatchConfig.options);
    fprintf(fp, "  \"weaponset\": %d,\n",    (int)g_MatchConfig.weaponSetIndex);

    /* Bot roster — only SLOT_BOT entries, skip slot 0 (local player) */
    fprintf(fp, "  \"bots\": [\n");
    s32 first = 1;
    for (s32 i = 1; i < g_MatchConfig.numSlots && i < MATCH_MAX_SLOTS; i++) {
        struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) continue;

        if (!first) fprintf(fp, ",\n");
        first = 0;

        fprintf(fp, "    {\"name\": \"");
        jsonEscapeStr(fp, sl->name);
        fprintf(fp, "\", \"difficulty\": %u, \"body\": %u, \"head\": %u}",
                (unsigned)sl->botDifficulty,
                (unsigned)sl->bodynum,
                (unsigned)sl->headnum);
    }
    if (!first) fprintf(fp, "\n");
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");

    fclose(fp);

    sysLogPrintf(LOG_NOTE, "SCENARIO: saved '%s' → %s", name, filepath);
    return 0;
}

s32 scenarioLoad(const char *filepath, s32 humanCount)
{
    if (!filepath || !filepath[0]) return -1;

    /* Read the entire file into memory */
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        sysLogPrintf(LOG_WARNING, "SCENARIO: failed to open '%s' for reading", filepath);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (filesize <= 0 || filesize > 65536) {
        fclose(fp);
        sysLogPrintf(LOG_WARNING, "SCENARIO: file '%s' is empty or too large (%ld bytes)",
                     filepath, filesize);
        return -1;
    }

    char *buf = (char *)malloc((size_t)(filesize + 1));
    if (!buf) {
        fclose(fp);
        sysLogPrintf(LOG_WARNING, "SCENARIO: out of memory reading '%s'", filepath);
        return -1;
    }

    size_t nread = fread(buf, 1, (size_t)filesize, fp);
    fclose(fp);
    buf[nread] = '\0';

    /* --- Parse version check --- */
    s32 version = 0;
    jsonFindInt(buf, "version", &version);
    if (version != 1) {
        sysLogPrintf(LOG_WARNING, "SCENARIO: unsupported version %d in '%s'",
                     version, filepath);
        free(buf);
        return -1;
    }

    /* --- Extract match settings --- */
    char scenarioName[64];
    scenarioName[0] = '\0';
    jsonFindString(buf, "name", scenarioName, sizeof(scenarioName));

    s32 arena          = -1;
    s32 scenario       = 0;
    s32 timelimit      = 60;
    s32 scorelimit     = 9;
    s32 weaponset      = 0;
    u32 teamscorelimit = 400;
    u32 options        = 0;

    jsonFindInt (buf, "arena",          &arena);
    jsonFindInt (buf, "scenario",       &scenario);
    jsonFindInt (buf, "timelimit",      &timelimit);
    jsonFindInt (buf, "scorelimit",     &scorelimit);
    jsonFindInt (buf, "weaponset",      &weaponset);
    jsonFindUInt(buf, "teamscorelimit", &teamscorelimit);
    jsonFindUInt(buf, "options",        &options);

    /* --- Reset match config and apply loaded settings --- */
    matchConfigInit();

    if (arena >= 0)
        g_MatchConfig.stagenum     = (u8)arena;
    if (scenario >= 0 && scenario < 16)
        g_MatchConfig.scenario     = (u8)scenario;
    if (timelimit >= 0)
        g_MatchConfig.timelimit    = (u8)timelimit;
    if (scorelimit >= 0)
        g_MatchConfig.scorelimit   = (u8)scorelimit;
    g_MatchConfig.teamscorelimit   = (u16)teamscorelimit;
    g_MatchConfig.options          = options;
    g_MatchConfig.weaponSetIndex   = (s8)weaponset;
    mpSetWeaponSet(g_MatchConfig.weaponSetIndex);

    /* --- Parse and add bots ---
     *
     * Dynamic player count rule:
     *   maxBots = MATCH_MAX_SLOTS - humanCount
     *   Bots are added in order; excess silently dropped.
     */
    s32 maxBots = MATCH_MAX_SLOTS - (humanCount > 0 ? humanCount : 1);
    if (maxBots < 0) maxBots = 0;
    s32 botCount = 0;

    const char *botsKey = strstr(buf, "\"bots\":");
    if (botsKey) {
        const char *arrStart = strchr(botsKey, '[');
        if (arrStart) {
            const char *p = arrStart + 1;

            while (*p && botCount < maxBots) {
                /* Advance to next '{' (start of bot object) or ']' (end of array) */
                while (*p && *p != '{' && *p != ']') p++;
                if (!*p || *p == ']') break;

                /* Find matching '}' — bots objects are flat, no nested braces */
                const char *objStart = p;
                const char *objEnd   = strchr(objStart + 1, '}');
                if (!objEnd) break;

                /* Copy bot object into a null-terminated scratch buffer */
                s32 objLen = (s32)(objEnd - objStart + 1);
                if (objLen < 2 || objLen > 512) {
                    p = objEnd + 1;
                    continue;
                }

                char obj[512];
                memcpy(obj, objStart, (size_t)objLen);
                obj[objLen] = '\0';

                /* Extract fields */
                char botName[MAX_PLAYER_NAME];
                botName[0] = '\0';
                s32 difficulty = 2; /* NormalSim default */
                s32 body       = 0;
                s32 head       = 0;

                jsonFindString(obj, "name",       botName, MAX_PLAYER_NAME);
                jsonFindInt   (obj, "difficulty", &difficulty);
                jsonFindInt   (obj, "body",        &body);
                jsonFindInt   (obj, "head",        &head);

                /* Clamp values to safe ranges */
                if (difficulty < 0) difficulty = 0;
                if (difficulty > 5) difficulty = 5;
                if (body < 0)       body = 0;
                if (head < 0)       head = 0;

                matchConfigAddBot(0 /* BOTTYPE_NORMAL */,
                                  (u8)difficulty,
                                  (u8)head,
                                  (u8)body,
                                  botName[0] ? botName : NULL);
                botCount++;
                p = objEnd + 1;
            }
        }
    }

    free(buf);

    sysLogPrintf(LOG_NOTE,
        "SCENARIO: loaded '%s' — arena=0x%02x scenario=%d bots=%d/%d (humanCount=%d)",
        scenarioName[0] ? scenarioName : filepath,
        g_MatchConfig.stagenum, g_MatchConfig.scenario,
        botCount, maxBots, humanCount);
    return 0;
}

s32 scenarioListFiles(char (*outPaths)[SCENARIO_PATH_MAX], s32 maxCount)
{
    if (!outPaths || maxCount <= 0) return 0;

    char scenDir[SCENARIO_PATH_MAX];
    getScenarioDir(scenDir, sizeof(scenDir));

    DIR *d = opendir(scenDir);
    if (!d) return 0;

    s32 count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < maxCount) {
        const char *fname = ent->d_name;
        size_t flen = strlen(fname);
        /* Must end in .json and not be just ".json" */
        if (flen > 5 && strcmp(fname + flen - 5, ".json") == 0) {
            snprintf(outPaths[count], (size_t)SCENARIO_PATH_MAX,
                     "%s/%s", scenDir, fname);
            count++;
        }
    }

    closedir(d);

    sysLogPrintf(LOG_NOTE, "SCENARIO: found %d scenario file(s) in '%s'",
                 count, scenDir);
    return count;
}
