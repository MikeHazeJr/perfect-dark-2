/**
 * pdgui_audio.cpp -- Menu sound effects bridge for ImGui menus.
 *
 * Forwards sound requests to PD's native menuPlaySound() function.
 * This allows ImGui menus to play the exact same navigation, selection,
 * and toggle sounds as the original PD menus.
 *
 * UI sounds are scaled by the UI volume layer (Audio.UIVolume * Audio.MasterVolume)
 * via a temporary SFX volume adjustment around the sound call.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include "pdgui_audio.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "system.h"

extern "C" {
#include "audio.h"
}

#include <stdio.h>
#include <string.h>

#define PDGUI_THEME_SOUND_ROLE_MAX 11

static pdgui_theme_sound_role_t s_ThemeSoundRoles[PDGUI_THEME_SOUND_ROLE_MAX];
static int s_ThemeSoundRoleCount = 0;

static int pdguiSoundRoleSupported(int soundId)
{
    static const int ids[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x08,
                               0x09, 0x0a, 0x0c, 0x0d, 0x0e };
    for (unsigned int i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        if (ids[i] == soundId) return 1;
    }
    return 0;
}

static const char *pdguiThemeSoundCatalogId(int soundId)
{
    for (int i = 0; i < s_ThemeSoundRoleCount; i++) {
        if (s_ThemeSoundRoles[i].sound_id == soundId) {
            return s_ThemeSoundRoles[i].catalog_id;
        }
    }
    return NULL;
}

extern "C" {

/* PD's native menu sound player -- defined in src/game/menu.c */
void menuPlaySound(int menusound);

/* Direct access to SFX volume for temporary override */
extern unsigned short g_SfxVolume;
void sndSetSfxVolume(unsigned short volume);

void pdguiPlaySound(int soundId)
{
    const char *themeId = pdguiThemeSoundCatalogId(soundId);
    if (themeId) {
        catalog_audio_result_t audio;
        if (!catalogResolveAudio(themeId, &audio) ||
                audio.category != AUDIO_CAT_SFX || !audio.file_path ||
                !audio.file_path[0] ||
                !audioPlayFileSound(audio.file_path,
                    audioGetUiVolumeScaled(), 64, 1.0f)) {
            sysLogPrintf(LOG_WARNING,
                "PDGUI theme audio: declared role %d source '%s' failed; refusing native fallback",
                soundId, themeId);
        }
        return;
    }

    /* Set the SFX volume to the UI-scaled value before starting the sound.
     * menuPlaySound() -> sndStart() uses g_SfxVolume as the initial volume
     * for the new sound instance on whichever channel it lands on.
     *
     * IMPORTANT: We restore g_SfxVolume directly (not via sndSetSfxVolume)
     * because sndSetSfxVolume() iterates ALL 9 sound channels and resets
     * their volume — which would immediately override the just-started
     * sound's channel volume back to the gameplay level. By writing the
     * global directly, the started sound keeps its UI-scaled volume on
     * its channel while future sounds will use the restored gameplay volume. */
    unsigned short savedVol = g_SfxVolume;
    unsigned short uiVol = audioGetUiVolumeScaled();

    sndSetSfxVolume(uiVol);
    menuPlaySound(soundId);
    g_SfxVolume = savedVol;    /* restore global only — don't touch channels */
}

int pdguiAudioValidateThemeRole(int soundId, const char *catalogId,
                                char *error, unsigned long errorCap)
{
    catalog_audio_result_t audio;
    if (error && errorCap) error[0] = '\0';
    if (!pdguiSoundRoleSupported(soundId)) {
        if (error && errorCap) snprintf(error, errorCap, "unsupported menu sound role");
        return 0;
    }
    if (!catalogId || !catalogId[0] || !catalogResolveAudio(catalogId, &audio) ||
            audio.category != AUDIO_CAT_SFX || !audio.file_path ||
            !audio.file_path[0]) {
        if (error && errorCap) snprintf(error, errorCap,
            "menu sound role source is missing, wrong-type, or has no public file");
        return 0;
    }
    return catalogLoadTypedAsset(ASSET_AUDIO, catalogId) != 0;
}

void pdguiAudioReplaceThemeRoles(const pdgui_theme_sound_role_t *roles, int count)
{
    if (count < 0 || count > PDGUI_THEME_SOUND_ROLE_MAX || (count && !roles)) return;
    memset(s_ThemeSoundRoles, 0, sizeof(s_ThemeSoundRoles));
    if (count) memcpy(s_ThemeSoundRoles, roles,
        (size_t)count * sizeof(s_ThemeSoundRoles[0]));
    s_ThemeSoundRoleCount = count;
}

} /* extern "C" */
