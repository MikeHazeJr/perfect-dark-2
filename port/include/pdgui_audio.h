#ifndef _IN_PDGUI_AUDIO_H
#define _IN_PDGUI_AUDIO_H

/**
 * pdgui_audio.h -- Menu sound effects for ImGui menus.
 *
 * Wraps the original PD menuPlaySound() function so ImGui menu code
 * can trigger authentic menu sounds on navigation, selection, etc.
 *
 * Sound IDs match constants.h MENUSOUND_* defines.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Sound effect IDs -- must match src/include/constants.h */
#define PDGUI_SND_SWIPE          0x00  /* Navigate to left/right dialog */
#define PDGUI_SND_OPENDIALOG     0x01  /* Opening a new dialog */
#define PDGUI_SND_FOCUS          0x02  /* Focusing a different item */
#define PDGUI_SND_SELECT         0x03  /* Selecting / activating an item */
#define PDGUI_SND_ERROR          0x04  /* Error beep */
#define PDGUI_SND_EXPLOSION      0x05  /* Explosion effect */
#define PDGUI_SND_TOGGLEON       0x08  /* Checking a checkbox */
#define PDGUI_SND_TOGGLEOFF      0x09  /* Unchecking, opening dropdown */
#define PDGUI_SND_SUBFOCUS       0x0a  /* Focus within a list / dropdown */
#define PDGUI_SND_KBFOCUS        0x0c  /* Keyboard focus change */
#define PDGUI_SND_KBCANCEL       0x0d  /* Keyboard cancel */
#define PDGUI_SND_SUCCESS        0x0e  /* Success chime */

typedef struct pdgui_theme_sound_role {
    int sound_id;
    char catalog_id[64];
} pdgui_theme_sound_role_t;

/* Play a menu sound effect. Calls menuPlaySound() in the game engine. */
void pdguiPlaySound(int soundId);

/* Replace all active theme-owned menu sound roles atomically. An empty list
 * restores the native menu sounds. Declared roles fail closed at playback;
 * they never fall through to an unrelated native sound. */
void pdguiAudioReplaceThemeRoles(const pdgui_theme_sound_role_t *roles, int count);
int pdguiAudioValidateThemeRole(int soundId, const char *catalogId,
                                char *error, unsigned long errorCap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_AUDIO_H */
