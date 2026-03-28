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

extern "C" {

/* PD's native menu sound player -- defined in src/game/menu.c */
void menuPlaySound(int menusound);

/* Volume layer system -- defined in port/src/audio.c */
unsigned short audioGetUiVolumeScaled(void);

/* Direct access to SFX volume for temporary override */
extern unsigned short g_SfxVolume;
void sndSetSfxVolume(unsigned short volume);

void pdguiPlaySound(int soundId)
{
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

} /* extern "C" */
