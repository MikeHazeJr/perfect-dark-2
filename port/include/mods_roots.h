/**
 * mods_roots.h -- where the game keeps mods.
 *
 * Two kinds of mods folder (Mike, 2026-11-15):
 *
 *   - The user mods folder, %LOCALAPPDATA%\perfectdark\mods. It is the only
 *     place the game writes mods: the in-game tools, installs from file,
 *     map import, bot variants, downloads. No build, cache rebuild or install
 *     owns it, so none of them can erase what is in it. Saves stay where they
 *     are (%APPDATA%\perfectdark).
 *   - The install's own mods folder: mods that ship with the game or that a
 *     build copies in (dev-mods). It is still read, after the user folder.
 *
 * On the same mod id the user's copy wins.
 *
 * --portable and --savedir isolate the user mods folder together with the
 * saves: it is then <save folder>/mods. With --portable alone that is the
 * install's own mods folder, so a portable install has one folder, as before.
 * --moddir and the existing-install reuse policy keep their single, exclusive
 * folder; the mod manager handles those before it asks for a user folder.
 */
#ifndef _IN_MODS_ROOTS_H
#define _IN_MODS_ROOTS_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Folders one scan may read: the user folder and four install candidates. */
#define MODS_ROOTS_MAX 5

/* Folder under %LOCALAPPDATA% that holds the user mods folder. */
#define MODS_ROOTS_APP_FOLDER "perfectdark"

/**
 * The user mods folder for these launch options. Pure: no file system access.
 *
 *   portable      nonzero when --portable was given
 *   savedir_arg   the --savedir value, or NULL
 *   save_dir      the resolved save folder ($S)
 *   local_appdata %LOCALAPPDATA%, or NULL
 *   home          the fallback base when there is no LOCALAPPDATA ($H)
 *
 * Returns 1 and the path in `out` (forward slashes, no trailing slash), or 0
 * with `out` empty when no folder can be named or it does not fit.
 */
s32 modsRootsUserDir(s32 portable, const char *savedir_arg, const char *save_dir,
		const char *local_appdata, const char *home, char *out, size_t out_cap);

/** Create `path` and any missing parent folders. 1 when it exists afterwards. */
s32 modsRootsEnsureDir(const char *path);

/**
 * 1 when two paths name the same folder. Relative paths are made absolute
 * against the current folder; separators, trailing slashes and "." segments
 * do not matter, and on Windows neither does case.
 */
s32 modsRootsSameDir(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODS_ROOTS_H */
