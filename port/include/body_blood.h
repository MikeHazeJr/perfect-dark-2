/**
 * body_blood.h -- the blood a mod body bleeds.
 *
 * chrGetBloodColour (src/game/chr.c) keeps the original's per-body list: the
 * Maian bodies (Elvis, The King, Elvis in a waistcoat) bleed green, the robots
 * oil, Mr Blonde and the Skedar their own; every other body bleeds red. A mod
 * body is never in that list, so it bleeds by its declared type instead: a
 * HEADBODYTYPE_MAIAN mod body bleeds the Maian green (C3 of
 * context/designs/modding/maian-vehicle-and-veteran.md). Base bodies keep the
 * original's colours, the base Maian soldier's red included.
 */
#ifndef _IN_BODY_BLOOD_H
#define _IN_BODY_BLOOD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BODY_BLOOD_ORIGINAL 0 /* the original's colour for this body number */
#define BODY_BLOOD_MAIAN    1 /* the Maian bodies' green */

/* The blood of a body whose catalog id and HEADBODYTYPE_* type are given:
 * BODY_BLOOD_MAIAN for a Maian-type body outside the base namespace,
 * BODY_BLOOD_ORIGINAL for a base body, an unknown one (NULL or empty id) and
 * any other type. */
s32 bodyBloodForCatalogBody(const char *catalog_id, s32 type);

#ifdef __cplusplus
}
#endif

#endif
