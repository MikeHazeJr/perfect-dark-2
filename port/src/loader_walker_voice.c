/**
 * loader_walker_voice.c -- Step 4 (2026-05-03).
 *
 * Walks data/<romid>/audio/voice/*.pdvoice and registers each as
 * ASSET_AUDIO with category=AUDIO_CAT_VOICE. Per Q-2 type-tolerance the
 * playback layer accepts a .pdvoice catalog ID anywhere a .pdsfx ID is
 * expected; the discriminator is the registered ext.audio.category.
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
            || !loaderWalkerArchiveTextMember(file_path, "voice.ini", &text, &length)) return -1;
    if (length <= 0x7fffffff && !memchr(text, '\0', length)
            && iniParseBuffer("voice.ini", text, (u32)length, &ini)
            && (!strcmp(ini.type, "voice"))) {
        result = assetCatalogRegisterBaseAudioSource(id, file_path, &ini,
            AUDIO_CAT_VOICE, soundnum, filenum);
    }
    sysMemFree(text);
    return result;
}

void loaderWalkerScanVoices(const char *tier_dir,
                             loader_walker_kind_result_t *out)
{
    static const loader_walker_kind_desc_t desc = {
        .kind_str = "voice",
        .subdir = "audio/voice",
        .extension = ".pdvoice",
        .always_invoke = 1,
    };
    loaderWalkerScanKind(tier_dir, &desc, s_register, out);
}
