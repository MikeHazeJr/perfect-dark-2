#ifndef _IN_FS_H
#define _IN_FS_H

#include <stdio.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MAXPATH 1024

s32 fsInit(void);

const char *fsFullPath(const char *relPath);

s32 fsPathIsAbsolute(const char *path);
s32 fsPathIsCwdRelative(const char *path);

void *fsFileLoad(const char *name, u32 *outSize);
s32 fsFileLoadTo(const char *name, void *dst, u32 dstSize);
s32 fsFileSize(const char *name);

FILE *fsFileOpenWrite(const char *name);
FILE *fsFileOpenRead(const char *name);
void fsFileFree(FILE *f);

const char *fsGetBaseDir(void);
const char *fsGetModDir(void);
s32 fsCreateDir(const char *path);

/* Phase 3 Pass A.1 (2026-05-02): data/<romid>/ tier accessors.
 *
 * `data/` holds BYOR ROM-extracted runtime content authored on the
 * user's machine at first launch.  `data/<romid>/...` further scopes
 * by ROM region (ntsc-final / pal-final / jpn-final) so multiple
 * regions can coexist on the same install.  `base/` (project-authored
 * shippable content) is a sibling tier; mods/ overlays both.  See
 * context/audits/rom-extraction-audit-2026-04-30.md Section 2 +
 * context/designs/catalog/catalog-rom-once-phase3-plan-2026-05-02.md
 * for the full architecture.
 *
 * fsDataPathFor(rel) returns "data/<romid>/<rel>" suitable for passing
 * to catalogSetPrimaryFile / fsFileLoad / fileProviderHandle.  Output
 * lives in a static buffer that the next call may overwrite -- callers
 * must consume the pointer before calling again.  NOT thread-safe.
 *
 * fsDataDir() returns "data/<romid>" suitable for passing to
 * fsCreateDir or as a directory walk root.  Same static-buffer
 * caveat. */
const char *fsDataDir(void);
const char *fsDataPathFor(const char *rel);

/* Ensure the data/<romid>/ directory exists. Idempotent. Returns 1 on
 * success (directory exists or was created), 0 on failure. */
s32 fsDataDirEnsure(void);

#ifdef __cplusplus
}
#endif

#endif
