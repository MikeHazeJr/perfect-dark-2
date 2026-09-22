#ifndef PD_MODEL_SOURCE_PATH_H
#define PD_MODEL_SOURCE_PATH_H
#include <stddef.h>
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Resolve an editable OBJ/glTF/GLB file or the public selection in .pdmesh.
 * Does not bind catalog rows, read private runtime metadata, or probe siblings.
 * Failure clears out. Callers must reject failed FileProvider model sources. */
s32 modelSourceResolvePath(const char *source, char *out, size_t capacity,
    char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
