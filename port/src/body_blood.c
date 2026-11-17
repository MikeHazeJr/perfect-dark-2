/**
 * body_blood.c -- the blood a mod body bleeds: see body_blood.h.
 */
#include <string.h>

#include <PR/ultratypes.h>

#include "constants.h"
#include "body_blood.h"

s32 bodyBloodForCatalogBody(const char *catalog_id, s32 type)
{
	if (!catalog_id || !catalog_id[0] || strncmp(catalog_id, "base:", 5) == 0) {
		return BODY_BLOOD_ORIGINAL;
	}
	return type == HEADBODYTYPE_MAIAN ? BODY_BLOOD_MAIAN : BODY_BLOOD_ORIGINAL;
}
