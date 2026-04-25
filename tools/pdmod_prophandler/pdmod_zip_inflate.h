/**
 * pdmod_zip_inflate.h -- Priority M / B-238 / M-2.1
 *
 * Minimal zip reader for the Property Handler DLL. Handles only what the
 * handler needs: open from an in-memory buffer, find a single named entry,
 * inflate via libz (statically linked), return the bytes.
 *
 * Standalone -- no dependency on the game's modarchive or fs.h. The DLL
 * is shipped as a separate artifact with its own build system, so this
 * tiny reader carries the relevant bits without forcing engine code into
 * the COM target.
 */
#ifndef _IN_PDMOD_ZIP_INFLATE_H
#define _IN_PDMOD_ZIP_INFLATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pdmod_zip pdmod_zip_t;

/* Open from an in-memory buffer (the handler reads the IStream into a
 * heap allocation, then hands ownership of the bytes to this open).
 * The returned handle owns its own copy of `bytes` so the caller may
 * free/move the source after this returns. */
pdmod_zip_t *pdmodZipOpenMemory(const void *bytes, uint32_t size);

void pdmodZipClose(pdmod_zip_t *z);

/* Extract a single entry by name into a freshly-malloc'd buffer the
 * caller must free(). Returns 1 on success, 0 on miss / failure. */
int  pdmodZipExtractToMalloc(pdmod_zip_t *z, const char *name,
                              void **outBuf, uint32_t *outSize);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDMOD_ZIP_INFLATE_H */
