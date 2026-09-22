#pragma once

#include <PR/ultratypes.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_FNAME "pd.ini"
#define CONFIG_PATH "$S/" CONFIG_FNAME

void configInit(void);

// loads config from file (path extensions such as ! apply)
s32 configLoad(const char *fname);

// Atomically saves config to file (same path resolution as configLoad).
// Returns 1 only after commit, 0 on failure; failed saves preserve prior bytes
// and registered runtime values. Successful saves retain numeric normalization.
s32 configSave(const char *fname);

// Checked serializer for a caller-owned stream, also used by diagnostic tests.
// Writes the current normalized snapshot without changing runtime values.
// Does not flush, close, or commit the stream. Returns 1 on write success, 0 on error.
s32 configWriteSnapshot(FILE *stream);

// registers a variable in the config file
// this should be done before configInit() is called, preferably in a module constructor
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max);
void configRegisterUInt(const char* key, u32* var, u32 min, u32 max);
void configRegisterFloat(const char *key, f32 *var, f32 min, f32 max);
void configRegisterString(const char *key, char *var, u32 maxstr);

#ifdef __cplusplus
}
#endif
