/**
 * pdgui_audio.cpp -- Menu sound effects bridge for ImGui menus.
 *
 * Forwards sound requests to PD's native menuPlaySound() function.
 * This allows ImGui menus to play the exact same navigation, selection,
 * and toggle sounds as the original PD menus.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include "pdgui_audio.h"

extern "C" {

/* PD's native menu sound player -- defined in src/game/menu.c */
void menuPlaySound(int menusound);

void pdguiPlaySound(int soundId)
{
    menuPlaySound(soundId);
}

} /* extern "C" */
