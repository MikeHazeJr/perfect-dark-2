#ifndef PDGUI_CHARACTER_PORTRAIT_H
#define PDGUI_CHARACTER_PORTRAIT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve an exact public .pdcharacter body/head binding and load its
 * portrait_file through the activated catalog/runtime source path.
 * Returns 1 with a GL texture, 0 when the character intentionally declares
 * no portrait, and -1 when a declared portrait cannot be consumed. */
s32 pdguiCharacterPortraitGet(const char *body_id, const char *head_id,
                              u32 *out_texture, u32 *out_width,
                              u32 *out_height);

/* Release the session-local decoded portrait cache. Room teardown calls this
 * so creator edits are read again on the next production roster load. */
void pdguiCharacterPortraitReset(void);

#ifdef __cplusplus
}
#endif

#endif
