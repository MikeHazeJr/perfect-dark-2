#ifndef PD_HEAD_BODY_RIG_H
#define PD_HEAD_BODY_RIG_H
#include "constants.h"

/* Shared semantic default for an omitted public rig_class. Explicit source
 * values, including the empty incompatible value, always override this. */
static inline const char *catalogRigClassForHeadBodyType(unsigned type)
{
    switch (type) {
    case HEADBODYTYPE_DEFAULT: return "human_male_neck_standard";
    case HEADBODYTYPE_FEMALE:
    case HEADBODYTYPE_FEMALEGUARD: return "human_female_neck_standard";
    case HEADBODYTYPE_MAIAN: return "maian_tall_neck";
    case HEADBODYTYPE_CASS: return "cass_neck";
    case HEADBODYTYPE_MRBLONDE: return "mrblonde_neck";
    default: return "";
    }
}
#endif
