/* Only for the dedicated Settings save test executable. Do not link into
 * pd-tests (its other test sources already define some filesystem adapters).
 * Parsing, registry serialization and atomic disk replacement are production
 * config.c/utils.c/save_atomic.c. The filesystem boundary uses real stdio. */
#include "fs.h"
#include "system.h"

#include <stdlib.h>
#include <string.h>

const char *fsFullPath(const char *name, char *out, size_t size)
{
    if (size) snprintf(out, size, "%s", name ? name : "");
    return out;
}

FILE *fsFileOpenRead(const char *name) { return fopen(name, "rb"); }
void fsFileFree(FILE *stream) { if (stream) fclose(stream); }
s32 fsFileSize(const char *name)
{
    FILE *stream = fopen(name, "rb");
    if (!stream) return -1;
    if (fseek(stream, 0, SEEK_END) != 0) { fclose(stream); return -1; }
    const long size = ftell(stream);
    fclose(stream);
    return (s32)size;
}
void sysLogPrintf(s32 level, const char *format, ...) { (void)level; (void)format; }
void *sysMemAlloc(const u32 size) { return malloc(size); }
