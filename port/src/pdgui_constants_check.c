/*
 * pdgui_constants_check.c -- Compile-time verification that the C++-safe
 * capacity mirror in port/include/pdgui_constants.h matches the canonical
 * values in src/include/constants.h.
 *
 * This file is C (not C++) precisely so it can include both constants.h and
 * pdgui_constants.h without the `bool`/`true`/`false` macro collisions that
 * keep C++ files from including constants.h. If any value drifts, the build
 * fails here rather than silently corrupting rankings, HUD, pause menus, etc.
 */

#include "constants.h"

/* Shadow the canonical macros so we can re-#include pdgui_constants.h without
 * a redefinition warning; we only care about the _numeric value_ matching. */
#define PDGUI_CHECK_MAX_PLAYERS         MAX_PLAYERS
#define PDGUI_CHECK_MAX_LOCAL_PLAYERS   MAX_LOCAL_PLAYERS
#define PDGUI_CHECK_MAX_BOTS            MAX_BOTS
#define PDGUI_CHECK_MAX_MPCHRS          MAX_MPCHRS
#define PDGUI_CHECK_MAX_TEAMS           MAX_TEAMS

#undef MAX_PLAYERS
#undef MAX_LOCAL_PLAYERS
#undef MAX_BOTS
#undef MAX_MPCHRS
#undef MAX_TEAMS

#include "pdgui_constants.h"

_Static_assert(MAX_PLAYERS       == PDGUI_CHECK_MAX_PLAYERS,
    "pdgui_constants.h MAX_PLAYERS drifted from constants.h");
_Static_assert(MAX_LOCAL_PLAYERS == PDGUI_CHECK_MAX_LOCAL_PLAYERS,
    "pdgui_constants.h MAX_LOCAL_PLAYERS drifted from constants.h");
_Static_assert(MAX_BOTS          == PDGUI_CHECK_MAX_BOTS,
    "pdgui_constants.h MAX_BOTS drifted from constants.h");
_Static_assert(MAX_MPCHRS        == PDGUI_CHECK_MAX_MPCHRS,
    "pdgui_constants.h MAX_MPCHRS drifted from constants.h");
_Static_assert(MAX_TEAMS         == PDGUI_CHECK_MAX_TEAMS,
    "pdgui_constants.h MAX_TEAMS drifted from constants.h");
