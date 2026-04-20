#ifndef PDGUI_CONSTANTS_H
#define PDGUI_CONSTANTS_H

/*
 * C++-safe mirror of the player/bot/team capacity constants from
 * src/include/constants.h.
 *
 * Why a mirror: src/include/types.h (which transitively pulls constants.h in
 * practice) defines `bool`, `true`, and `false` as macros that collide with
 * C++ keywords, so C++ files cannot include constants.h directly. Prior to
 * this header, each C++ caller kept its own #define — values drifted silently
 * when the canonical constants changed.
 *
 * Values are verified against the canonical constants at build time by the
 * C translation unit port/src/pdgui_constants_check.c (which can safely
 * include both this header and constants.h). Any mismatch fails the build
 * via _Static_assert.
 */

#define MAX_PLAYERS         8
#define MAX_LOCAL_PLAYERS   4
#define MAX_BOTS            32                          /* = PARTICIPANT_DEFAULT_CAPACITY */
#define MAX_MPCHRS          (MAX_PLAYERS + MAX_BOTS)    /* = 40 */
#define MAX_TEAMS           8

#endif /* PDGUI_CONSTANTS_H */
