#ifndef _IN_INPUTMODES_H
#define _IN_INPUTMODES_H

#include <PR/ultratypes.h>

/* Input mode constants */
#define INPUTMODE_SINGLE    0  /* Normal single press */
#define INPUTMODE_DOUBLETAP 1  /* Press twice within timing window */
#define INPUTMODE_HOLD      2  /* Hold for timing duration */

/* Get/set the input mode for a CK action */
s32 inputModeGet(u32 ck);
void inputModeSet(u32 ck, s32 mode);

/* Get/set the timing window (seconds) for double-tap/hold */
f32 inputModeGetTiming(u32 ck);
void inputModeSetTiming(u32 ck, f32 seconds);

#endif
