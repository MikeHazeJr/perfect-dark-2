/**
 * loader_walker_sfx.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/sfx/*.pdsfx and registers each as ASSET_AUDIO
 * with category=AUDIO_CAT_SFX. Public sound.ini selects the standard source
 * and playback controls. Private manifest fields carry only native identity.
 */

#include <stddef.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker.h"
#include "loader_walker_common.h"
#include "assetcatalog_scanner.h"
#include "catalog_audio_public_source.h"
#include "system.h"

static s32 s_register(const char *manifest, size_t manifest_len,
                      const char *pd_kind, const char *id,
                      const char *file_path)
{
    char *text = NULL;
    size_t length = 0;
    ini_section_t ini;
    s32 soundnum = -1, filenum = -1;
    s32 result = -1;
    (void)pd_kind;
    if (!catalogAudioNativeIdentityParse(manifest, manifest_len, &soundnum, &filenum)
            || !loaderWalkerArchiveTextMember(file_path, "sound.ini", &text, &length)) return -1;
    if (length <= 0x7fffffff && !memchr(text, '\0', length)
            && iniParseBuffer("sound.ini", text, (u32)length, &ini)
            && (!strcmp(ini.type, "sound") || !strcmp(ini.type, "sfx"))) {
        result = assetCatalogRegisterBaseAudioSource(id, file_path, &ini,
            AUDIO_CAT_SFX, soundnum, filenum);
    }
    sysMemFree(text);
    return result;
}

void loaderWalkerScanSfx(const char *tier_dir,
                          loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "sfx",
        .subdir = "audio/sfx",
        .extension = ".pdsfx",
        .always_invoke = 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
