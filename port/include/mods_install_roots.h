/**
 * mods_install_roots.h -- the install's own mods folders (B-1169).
 *
 * Mike, 2026-11-15 (option A): a player's install reads only the mods folder
 * next to the game, <base>/mods (the base folder is the game's own folder
 * unless --basedir names another). An executable inside a source checkout
 * (its parent folder holds CMakeLists.txt and .git, as Build/ does) also
 * reads the repository's own mods folder, first, so a developer keeps the
 * repository-root folder. Nothing else is read: not a folder beside the
 * install, not one under the working folder.
 *
 * These are the install's folders only. The user mods folder in LocalAppData
 * (mods_roots.h on week-pd/mods) is read before them; --moddir and the
 * existing-install reuse policy keep their single folder and do not ask here.
 */
#ifndef _IN_MODS_INSTALL_ROOTS_H
#define _IN_MODS_INSTALL_ROOTS_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* At most: the repository-root folder, then <base>/mods. */
#define MODS_INSTALL_ROOTS_MAX 2

/**
 * 1 when exe_dir's parent folder holds both CMakeLists.txt and .git (a
 * folder in a clone, a file in a git worktree).
 */
s32 modsInstallIsSourceCheckout(const char *exe_dir);

/**
 * The install's mods folders, in read order, into out[0 .. out_count - 1]
 * (each buffer `cap` bytes; forward slashes, no trailing slash). Returns how
 * many were written: 0 when base_dir is empty or a path does not fit. With
 * room for only one folder it is <base>/mods. The repository folder is left
 * out when it names the same folder as <base>/mods.
 */
s32 modsInstallRoots(const char *exe_dir, const char *base_dir,
		char *const *out, size_t cap, s32 out_count);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODS_INSTALL_ROOTS_H */
