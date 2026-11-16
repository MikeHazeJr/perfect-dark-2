#ifndef PD_BOOL_H
#define PD_BOOL_H

/*
 * Project boolean for C files that must not depend on types.h (SP-75).
 *
 * Project `bool` is `s32` (src/include/types.h: `#define bool s32`). The
 * client target force-includes types.h first in every C file through its
 * precompiled header, but the test target builds the same files without it,
 * so a small module that needs `bool`, `true` and `false` includes this
 * header instead of <stdbool.h>. <stdbool.h> would redefine `bool` as the
 * 1-byte `_Bool` for the rest of the file and give every shared declaration
 * parsed after it a different width than in other files.
 *
 * C only: in C++ `bool` is a keyword and this header leaves it alone.
 * Declarations shared with C++ spell `s32` explicitly instead of `bool`.
 */

#include <PR/ultratypes.h>

#ifndef __cplusplus
#ifndef bool
#define bool s32
#endif
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#endif

#endif /* PD_BOOL_H */
