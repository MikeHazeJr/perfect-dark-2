/**
 * playerstats.c -- Granular player statistics tracking
 *
 * Hash table of string-keyed counters. Persisted as JSON to disk.
 * Every gameplay event (kill, death, shot, match result) increments
 * one or more counters. Achievements are queries on top of this data.
 */

#include "playerstats.h"
#include "system.h"
#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Storage ---- */

#define STATS_MAX_ENTRIES 512
#define STATS_KEY_MAX     128
#define STATS_FILE        "$S/playerstats.json"

typedef struct {
    char key[STATS_KEY_MAX];
    u64 value;
} stat_entry_t;

static stat_entry_t s_Stats[STATS_MAX_ENTRIES];
static s32 s_NumStats = 0;
static s32 s_Dirty = 0;

/* ---- Internal ---- */

static stat_entry_t *findStat(const char *key)
{
    for (s32 i = 0; i < s_NumStats; i++) {
        if (strcmp(s_Stats[i].key, key) == 0) {
            return &s_Stats[i];
        }
    }
    return NULL;
}

static stat_entry_t *findOrCreateStat(const char *key)
{
    stat_entry_t *entry = findStat(key);
    if (entry) return entry;

    if (s_NumStats >= STATS_MAX_ENTRIES) {
        sysLogPrintf(LOG_WARNING, "STATS: table full, cannot create '%s'", key);
        return NULL;
    }

    entry = &s_Stats[s_NumStats];
    strncpy(entry->key, key, STATS_KEY_MAX - 1);
    entry->key[STATS_KEY_MAX - 1] = '\0';
    entry->value = 0;
    s_NumStats++;
    return entry;
}

/* ---- Persistence ---- */

static void statsLoad(void)
{
    const char *path = fsFullPath(STATS_FILE);
    FILE *f = fopen(path, "r");
    if (!f) {
        sysLogPrintf(LOG_NOTE, "STATS: no save file found, starting fresh");
        return;
    }

    /* Simple JSON parsing: { "key": value, ... }
     * We don't need a full JSON parser -- the format is strictly controlled. */
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[STATS_KEY_MAX];
        u64 val = 0;
        /* Match: "key": value */
        char *quote1 = strchr(line, '"');
        if (!quote1) continue;
        char *quote2 = strchr(quote1 + 1, '"');
        if (!quote2) continue;
        s32 keylen = (s32)(quote2 - quote1 - 1);
        if (keylen <= 0 || keylen >= STATS_KEY_MAX) continue;
        memcpy(key, quote1 + 1, keylen);
        key[keylen] = '\0';

        char *colon = strchr(quote2, ':');
        if (!colon) continue;
        val = (u64)strtoull(colon + 1, NULL, 10);

        stat_entry_t *entry = findOrCreateStat(key);
        if (entry) {
            entry->value = val;
        }
    }

    fclose(f);
    sysLogPrintf(LOG_NOTE, "STATS: loaded %d stats from %s", s_NumStats, path);
}

void statsSave(void)
{
    if (!s_Dirty && s_NumStats == 0) return;

    const char *path = fsFullPath(STATS_FILE);
    FILE *f = fopen(path, "w");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "STATS: failed to save to %s", path);
        return;
    }

    fprintf(f, "{\n");
    for (s32 i = 0; i < s_NumStats; i++) {
        fprintf(f, "  \"%s\": %llu%s\n",
            s_Stats[i].key,
            (unsigned long long)s_Stats[i].value,
            (i < s_NumStats - 1) ? "," : "");
    }
    fprintf(f, "}\n");
    fclose(f);

    s_Dirty = 0;
    sysLogPrintf(LOG_NOTE, "STATS: saved %d stats to %s", s_NumStats, path);
}

/* ---- API ---- */

void statsInit(void)
{
    memset(s_Stats, 0, sizeof(s_Stats));
    s_NumStats = 0;
    s_Dirty = 0;
    statsLoad();
    sysLogPrintf(LOG_NOTE, "STATS: initialized (%d entries)", s_NumStats);
}

void statsShutdown(void)
{
    statsSave();
    sysLogPrintf(LOG_NOTE, "STATS: shutdown");
}

void statIncrement(const char *key, u64 amount)
{
    if (!key || !key[0]) return;
    stat_entry_t *entry = findOrCreateStat(key);
    if (entry) {
        entry->value += amount;
        s_Dirty = 1;
    }
}

void statSetMax(const char *key, u64 newval)
{
    if (!key || !key[0]) return;
    stat_entry_t *entry = findOrCreateStat(key);
    if (entry && newval > entry->value) {
        entry->value = newval;
        s_Dirty = 1;
    }
}

u64 statGet(const char *key)
{
    stat_entry_t *entry = findStat(key);
    return entry ? entry->value : 0;
}

s32 statsGetCount(void)
{
    return s_NumStats;
}

const char *statsGetKeyByIndex(s32 idx)
{
    if (idx < 0 || idx >= s_NumStats) return NULL;
    return s_Stats[idx].key;
}

u64 statsGetValueByIndex(s32 idx)
{
    if (idx < 0 || idx >= s_NumStats) return 0;
    return s_Stats[idx].value;
}
