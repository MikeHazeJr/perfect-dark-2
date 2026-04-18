#ifndef _IN_PDMAIN_H
#define _IN_PDMAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns g_Vars.lvframe60 — the in-stage tick counter (0 on the first frame).
 * Exposed as a C function so C++ port code can read it without including
 * types.h (which #defines bool as s32 and breaks C++ bool).
 */
s32 pdmainGetLvFrame60(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDMAIN_H */
