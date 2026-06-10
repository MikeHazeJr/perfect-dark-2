#include <stdio.h>
#include <string.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "asset_fallback_telemetry.h"
#include "system.h"

/*
 * c3849 Wave 1: normal-play asset fallback telemetry.
 * See port/include/asset_fallback_telemetry.h. Game-thread only (boot-phase
 * fan-out workers never touch the instrumented paths); counters are plain s32.
 * Dependencies kept to string.h/stdio.h/PR types/assetcatalog.h/system.h so
 * pd-tests links it (sysLogPrintf is stubbed there).
 */

#define FALLBACK_WHAT_LEN 64

static s32 s_Counts[ASSET_TYPE_COUNT];
static s32 s_FirstNum[ASSET_TYPE_COUNT];
static char s_FirstWhat[ASSET_TYPE_COUNT][FALLBACK_WHAT_LEN];
static s32 s_Total;

/* Local family-name table: assetSourceDebugTypeLabel is not pd-tests-linkable
 * (project precedent: local typeName tables in main.c / pdgui_menu_modmgr). */
static const char *s_familyName(asset_type_e family)
{
    switch (family) {
    case ASSET_SFX:       return "sfx";
    case ASSET_MUSIC:     return "music";
    case ASSET_ANIMATION: return "animation";
    case ASSET_TEXTURE:   return "texture";
    case ASSET_MODEL:     return "model";
    case ASSET_SCENARIO:  return "scenario";
    case ASSET_WEAPON:    return "weapon";
    case ASSET_BODY:      return "body";
    case ASSET_HEAD:      return "head";
    case ASSET_FONT:      return "font";
    case ASSET_LANG:      return "lang";
    case ASSET_NONE:      return "file";
    default:              return "other";
    }
}

void assetFallbackRecord(asset_type_e family, s32 num, const char *what)
{
    s32 f = (s32)family;

    if (f < 0 || f >= ASSET_TYPE_COUNT) {
        f = (s32)ASSET_NONE;
    }

    if (s_Counts[f] == 0) {
        s_FirstNum[f] = num;
        snprintf(s_FirstWhat[f], sizeof(s_FirstWhat[f]), "%s",
            what ? what : "");
    }

    s_Counts[f]++;
    s_Total++;
}

s32 assetFallbackReportAndReset(const char *checkpoint)
{
    s32 total = s_Total;
    s32 f;

    if (total == 0) {
        return 0;
    }

    sysLogPrintf(LOG_WARNING,
        "ASSET.FALLBACK: %d fallback(s) since last checkpoint, at %s:",
        total, checkpoint ? checkpoint : "(unnamed)");

    for (f = 0; f < ASSET_TYPE_COUNT; f++) {
        if (s_Counts[f] == 0) {
            continue;
        }
        sysLogPrintf(LOG_WARNING,
            "ASSET.FALLBACK:   %s count=%d first(num=%d, %s)",
            s_familyName((asset_type_e)f), s_Counts[f], s_FirstNum[f],
            s_FirstWhat[f]);
    }

    memset(s_Counts, 0, sizeof(s_Counts));
    memset(s_FirstNum, 0, sizeof(s_FirstNum));
    memset(s_FirstWhat, 0, sizeof(s_FirstWhat));
    s_Total = 0;

    return total;
}

s32 assetFallbackPendingTotal(void)
{
    return s_Total;
}

s32 assetFallbackCountFor(asset_type_e family)
{
    s32 f = (s32)family;

    if (f < 0 || f >= ASSET_TYPE_COUNT) {
        f = (s32)ASSET_NONE;
    }

    return s_Counts[f];
}
