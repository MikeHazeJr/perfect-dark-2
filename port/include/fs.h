#ifndef _IN_FS_H
#define _IN_FS_H

#include <stdio.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MAXPATH 1024

s32 fsInit(void);

/* fsFullPath: resolves relPath against base/mod/save/exe/home dirs and
 * the active asset catalog + modmgr registry. Writes the resolved path
 * into out (capped at outSize, including NUL). Always null-terminates
 * when outSize > 0. Returns out for chaining; never returns NULL.
 *
 * Contract:
 *   - Caller owns the buffer. A stack buffer of FS_MAXPATH + 1 bytes
 *     is sufficient for any input.
 *   - Thread-safe: no static state. Multiple threads may resolve in
 *     parallel as long as each passes its own out buffer.
 *   - Common pattern:
 *       char buf[FS_MAXPATH + 1];
 *       FILE *f = fopen(fsFullPath(rel, buf, sizeof(buf)), "rb");
 *
 * Phase 1 of context/designs/engine/startup-acceleration.md
 * (2026-05-03) replaced the prior static-buffer signature so the boot
 * pipeline can fan out file I/O across worker threads. */
const char *fsFullPath(const char *relPath, char *out, size_t outSize);

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
 * Phase 1 of startup-acceleration (2026-05-03) replaced the prior
 * static-buffer signatures so these are thread-safe. Caller-owned
 * buffer; out is always null-terminated when outSize > 0. Returns
 * out for chaining; never NULL.
 *
 * fsDataPathFor(rel, out, outSize) writes "data/<romid>/<rel>"
 * suitable for passing to catalogSetPrimaryFile / fsFileLoad /
 * fileProviderHandle. If rel is NULL or empty, equivalent to
 * fsDataDir(out, outSize). Strips a leading slash from rel.
 *
 * fsDataDir(out, outSize) writes "data/<romid>" suitable for passing
 * to fsCreateDir or as a directory walk root. */
const char *fsDataDir(char *out, size_t outSize);
const char *fsDataPathFor(const char *rel, char *out, size_t outSize);

/* Ensure the data/<romid>/ directory exists. Idempotent. Returns 1 on
 * success (directory exists or was created), 0 on failure. */
s32 fsDataDirEnsure(void);

#ifdef __cplusplus
}
#endif

#endif
