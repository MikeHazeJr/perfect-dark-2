#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include "fs.h"
#include "config.h"
#include "save_atomic.h"
#include "system.h"
#include "utils.h"

#define CONFIG_MAX_SECNAME 128
#define CONFIG_MAX_KEYNAME 256
#define CONFIG_MAX_SETTINGS 2048
#define CONFIG_PENDING_VAL_MAX 256

typedef enum {
	CFG_NONE,
	CFG_S32,
	CFG_F32,
	CFG_U32,
	CFG_STR
} configtype;

struct configentry {
	char key[CONFIG_MAX_KEYNAME + 1];
	s32 seclen;
	configtype type;
	void *ptr;
	union {
		struct { f32 min_f32, max_f32; };
		struct { s32 min_s32, max_s32; };
		struct { u32 min_u32, max_u32; };
		u32 max_str;
	};
	/* S305: raw value stashed by configLoad when the key is seen before the
	 * owning subsystem has had a chance to call configRegister*.  When the
	 * registration eventually arrives, we replay the pending value into the
	 * now-typed ptr.  Fixes theme/chrome/title-bar persistence bug where
	 * pd.ini is read (in configInit) before pdguiThemeInit registers its
	 * Video.* keys — the saved value was silently dropped and the next
	 * restart reverted to defaults. */
	char pending[CONFIG_PENDING_VAL_MAX];
	u8 has_pending;
} settings[CONFIG_MAX_SETTINGS];

static s32 numSettings = 0;
static u8 configMaxWarningLogged = 0;

static inline s32 configClampInt(s32 val, s32 min, s32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline u32 configClampUInt(u32 val, u32 min, u32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline f32 configClampFloat(f32 val, f32 min, f32 max)
{
	return (val < min) ? min : ((val > max) ? max : val);
}

static inline struct configentry *configFindEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return NULL;
}

static inline struct configentry *configAddEntry(const char *key)
{
	if (numSettings < CONFIG_MAX_SETTINGS) {
		struct configentry *cfg = &settings[numSettings++];
		snprintf(cfg->key, CONFIG_MAX_KEYNAME, "%s", key);
		const char *delim = strrchr(cfg->key, '.');
		cfg->seclen = delim ? (delim - cfg->key) : 0;
		return cfg;
	}
	if (!configMaxWarningLogged) {
		sysLogPrintf(LOG_WARNING, "Maximum number of configuration entries exceeded: %d", CONFIG_MAX_SETTINGS);
		configMaxWarningLogged = 1;
	}
	return NULL;
}

static inline struct configentry *configFindOrAddEntry(const char *key)
{
	for (s32 i = 0; i < numSettings; ++i) {
		if (!strncasecmp(settings[i].key, key, CONFIG_MAX_KEYNAME)) {
			return &settings[i];
		}
	}
	return configAddEntry(key);
}

static inline const char *configGetSection(char *sec, const struct configentry *cfg)
{
	if (!cfg->seclen || cfg->seclen > CONFIG_MAX_SECNAME) {
		strncpy(sec, cfg->key, CONFIG_MAX_SECNAME);
		sec[CONFIG_MAX_SECNAME] = '\0';
		return sec;
	}

	memcpy(sec, cfg->key, cfg->seclen);
	sec[cfg->seclen] = '\0';

	return sec;
}

/* Forward declarations for S305 pending-value replay plumbing. */
static void configApplyEntry(struct configentry *cfg, const char *val);
static void configStashPending(struct configentry *cfg, const char *val);

/* S305: replay a pending raw value (stashed by configLoad before this
 * entry was registered).  Called at the tail of every configRegister*. */
static void configReplayPending(struct configentry *cfg)
{
	if (cfg && cfg->has_pending) {
		configApplyEntry(cfg, cfg->pending);
		cfg->has_pending = 0;
		cfg->pending[0] = '\0';
	}
}

void configRegisterInt(const char *key, s32 *var, s32 min, s32 max)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_S32;
		cfg->ptr = var;
		cfg->min_s32 = min;
		cfg->max_s32 = max;
		configReplayPending(cfg);
	}
}

void configRegisterUInt(const char* key, u32* var, u32 min, u32 max)
{
	struct configentry* cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_U32;
		cfg->ptr = var;
		cfg->min_u32 = min;
		cfg->max_u32 = max;
		configReplayPending(cfg);
	}
}

void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_F32;
		cfg->ptr = var;
		cfg->min_f32 = min;
		cfg->max_f32 = max;
		configReplayPending(cfg);
	}
}

void configRegisterString(const char *key, char *var, u32 maxstr)
{
	struct configentry *cfg = configFindOrAddEntry(key);
	if (cfg) {
		cfg->type = CFG_STR;
		cfg->ptr = var;
		cfg->max_str = maxstr;
		configReplayPending(cfg);
	}
}

static void configApplyEntry(struct configentry *cfg, const char *val)
{
	s32 tmp_s32;
	f32 tmp_f32;
	u32 tmp_u32;
	switch (cfg->type) {
		case CFG_S32:
			tmp_s32 = strtol(val, NULL, 0);
			if (cfg->min_s32 < cfg->max_s32) {
				tmp_s32 = configClampInt(tmp_s32, cfg->min_s32, cfg->max_s32);
			}
			*(s32 *)cfg->ptr = tmp_s32;
			break;
		case CFG_F32:
			tmp_f32 = strtof(val, NULL);
			if (cfg->min_f32 < cfg->max_f32) {
				tmp_f32 = configClampFloat(tmp_f32, cfg->min_f32, cfg->max_f32);
			}
			*(f32 *)cfg->ptr = tmp_f32;
			break;
		case CFG_U32:
			tmp_u32 = strtoul(val, NULL, 0);
			if (cfg->min_u32 < cfg->max_u32) {
				tmp_u32 = configClampUInt(tmp_u32, cfg->min_u32, cfg->max_u32);
			}
			*(u32*)cfg->ptr = tmp_u32;
			break;
		case CFG_STR:
			{
				const size_t maxlen = cfg->max_str ? cfg->max_str - 1 : 4095;
				strncpy(cfg->ptr, val, maxlen);
				((char *)cfg->ptr)[maxlen] = '\0';
			}
			break;
		default:
			break;
	}
}

static void configStashPending(struct configentry *cfg, const char *val)
{
	if (!cfg || !val) return;
	const size_t maxlen = CONFIG_PENDING_VAL_MAX - 1;
	strncpy(cfg->pending, val, maxlen);
	cfg->pending[maxlen] = '\0';
	cfg->has_pending = 1;
}

static void configSetFromString(const char *key, const char *val)
{
	/* S305: use FindOrAdd so keys loaded from pd.ini BEFORE the owning
	 * subsystem has registered their ptr still retain the raw value.
	 * configRegister* will replay pending values when the entry type
	 * gets set. */
	struct configentry *cfg = configFindOrAddEntry(key);
	if (!cfg) return;

	if (cfg->type == CFG_NONE) {
		/* Not yet registered — stash raw value for later replay. */
		configStashPending(cfg, val);
		return;
	}

	configApplyEntry(cfg, val);
}

/* Capture normalized numeric values without changing live settings before the
 * disk commit. The registry and its pointers remain owned by the synchronous
 * config caller, just as for configLoad/configRegister. */
union configsavevalue {
    s32 int_value;
    u32 uint_value;
    f32 float_value;
};

static void configCaptureSaveValues(union configsavevalue *values, s32 count)
{
    for (s32 i = 0; i < count; ++i) {
        const struct configentry *cfg = &settings[i];
        switch (cfg->type) {
        case CFG_S32:
            values[i].int_value = *(const s32 *)cfg->ptr;
            if (cfg->min_s32 < cfg->max_s32)
                values[i].int_value = configClampInt(values[i].int_value, cfg->min_s32, cfg->max_s32);
            break;
        case CFG_U32:
            values[i].uint_value = *(const u32 *)cfg->ptr;
            if (cfg->min_u32 < cfg->max_u32)
                values[i].uint_value = configClampUInt(values[i].uint_value, cfg->min_u32, cfg->max_u32);
            break;
        case CFG_F32:
            values[i].float_value = *(const f32 *)cfg->ptr;
            if (cfg->min_f32 < cfg->max_f32)
                values[i].float_value = configClampFloat(values[i].float_value, cfg->min_f32, cfg->max_f32);
            break;
        default: break;
        }
    }
}

static s32 configSaveEntry(const struct configentry *cfg, FILE *f,
                          const union configsavevalue *value)
{
    const char *key = cfg->key + cfg->seclen + 1;
    switch (cfg->type) {
    case CFG_S32: return fprintf(f, "%s=%d\n", key, value->int_value) >= 0;
    case CFG_F32: return fprintf(f, "%s=%f\n", key, value->float_value) >= 0;
    case CFG_U32: return fprintf(f, "%s=%u\n", key, value->uint_value) >= 0;
    case CFG_STR: return fprintf(f, "%s=%s\n", key, (const char *)cfg->ptr) >= 0;
    default:
        /* Keep late-registered subsystem values and their existing format. */
        return !cfg->has_pending || fprintf(f, "%s=%s\n", key, cfg->pending) >= 0;
    }
}

static s32 configWriteRegistry(FILE *f, const union configsavevalue *values, s32 count)
{
    char tmpSec[CONFIG_MAX_SECNAME + 1] = { 0 };
    char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };
    if (!f) return 0;
    configGetSection(curSec, &settings[0]);
    if (fprintf(f, "[%s]\n", curSec) < 0) return 0;

    for (s32 i = 0; i < count; ++i) {
        const struct configentry *cfg = &settings[i];
        configGetSection(tmpSec, cfg);
        if (strncmp(curSec, tmpSec, CONFIG_MAX_SECNAME) != 0) {
            if (fprintf(f, "\n[%s]\n", tmpSec) < 0) return 0;
            strncpy(curSec, tmpSec, CONFIG_MAX_SECNAME - 1);
            curSec[CONFIG_MAX_SECNAME - 1] = '\0';
        }
        if (!configSaveEntry(cfg, f, &values[i])) return 0;
    }
    return !ferror(f);
}

s32 configWriteSnapshot(FILE *stream)
{
    union configsavevalue values[CONFIG_MAX_SETTINGS];
    const s32 count = numSettings;
    configCaptureSaveValues(values, count);
    return configWriteRegistry(stream, values, count);
}

s32 configSave(const char *fname)
{
    save_atomic_file_t transaction;
    char destination[FS_MAXPATH + 1];
    union configsavevalue values[CONFIG_MAX_SETTINGS];
    const s32 count = numSettings;
    if (!fname || !fname[0]) return 0;
    fsFullPath(fname, destination, sizeof(destination));
    configCaptureSaveValues(values, count);
    if (saveAtomicBegin(&transaction, destination) != 0) return 0;
    if (!configWriteRegistry(saveAtomicStream(&transaction), values, count)) {
        saveAtomicAbort(&transaction);
        return 0;
    }
    if (saveAtomicCommit(&transaction) != 0) return 0;

    /* Preserve successful-save normalization, using exactly the values written.
     * Failure never modifies registered runtime values or the prior file. */
    for (s32 i = 0; i < count; ++i) {
        switch (settings[i].type) {
        case CFG_S32: *(s32 *)settings[i].ptr = values[i].int_value; break;
        case CFG_U32: *(u32 *)settings[i].ptr = values[i].uint_value; break;
        case CFG_F32: *(f32 *)settings[i].ptr = values[i].float_value; break;
        default: break;
        }
    }
    return 1;
}

s32 configLoad(const char *fname)
{
	FILE *f = fsFileOpenRead(fname);
	if (!f) {
		return 0;
	}

	char curSec[CONFIG_MAX_SECNAME + 1] = { 0 };
	char keyBuf[CONFIG_MAX_SECNAME * 2 + 2] = { 0 }; // SECTION + . + KEY + \0
	char token[UTIL_MAX_TOKEN + 1] = { 0 };
	char lineBuf[2048] = { 0 };
	char *line = lineBuf;
	s32 lineLen = 0;

	while (fgets(lineBuf, sizeof(lineBuf), f)) {
		line = lineBuf;

		line = strParseToken(line, token, NULL);

		if (token[0] == '[' && token[1] == '\0') {
			// section; get name
			line = strParseToken(line, token, NULL);
			if (!token[0]) {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
				continue;
			}
			strncpy(curSec, token, CONFIG_MAX_SECNAME - 1);
			curSec[CONFIG_MAX_SECNAME - 1] = '\0';
			// eat ]
			line = strParseToken(line, token, NULL);
			if (token[0] != ']' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed section line: %s", lineBuf);
			}
		} else if (token[0]) {
			// probably a key=value pair; append key name to section name
			snprintf(keyBuf, sizeof(keyBuf) - 1, "%s.%s", curSec, token);
			// eat =
			line = strParseToken(line, token, NULL);
			if (token[0] != '=' || token[1] != '\0') {
				sysLogPrintf(LOG_ERROR, "configLoad: malformed keyvalue line: %s", lineBuf);
				continue;
			}
			// the rest of the line is the value
			line = strTrim(line);
			if (line[0] == '"') {
				line = strUnquote(line);
			}
			configSetFromString(keyBuf, line);
		}
	}

	fsFileFree(f);

	return 1;
}

void configInit(void)
{
	if (fsFileSize(CONFIG_PATH) > 0) {
		configLoad(CONFIG_PATH);
	}
}
