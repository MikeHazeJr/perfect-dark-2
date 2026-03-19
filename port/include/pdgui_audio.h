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

/* Play a menu sound effect. Calls menuPlaySound() in the game engine. */
void pdguiPlaySound(int soundId);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_AUDIO_H */
