#ifndef _IN_PREFS_AGENT_H
#define _IN_PREFS_AGENT_H

/**
 * prefs_agent.h -- Per-agent preference sidecar (S309 + S313 batch)
 *
 * Tracks visual + audio + mod-enablement preferences per Agent profile
 * so the user's preferences (theme, menu style, title bar, font,
 * scanlines, audio volume layers, enabled mods) follow the active
 * agent.  Sidecar file at
 *
 *     saves/prefs_<agent_name>.ini
 *
 * with the same INI format as pd.ini.  Fields are loaded via the
 * matching setter on each subsystem, not via the global config registry,
 * so loading a profile doesn't smear its values onto pd.ini.
 *
 * All visual fields (theme, chrome, font, scanlines) swap live via
 * pdguiRequestFontAtlasRebuild() — no restart required.
 *
 * Global pd.ini remains the source of per-machine / hardware-level
 * defaults (resolution, fullscreen, video mode, gameplay bindings,
 * server port, network tuning).  The global file is still read at
 * boot; agent prefs overlay on top for visuals + audio + mods.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the subsystem.  Must be called AFTER saveInit() so
 *  saveGetDir() returns a valid path. */
void prefsAgentInit(void);

/** Remember who the currently-loaded agent is.  Any subsequent
 *  prefsAgentSave() call writes to this agent's sidecar. */
void prefsAgentSetActive(const char *agent_name);

/** Load (or apply) the active agent's prefs.  Looks for
 *  saves/prefs_<active>.ini and applies each recognised key via the
 *  owning subsystem's setter.  Missing file is a no-op. */
void prefsAgentLoad(const char *agent_name);

/** Reset all visual per-agent prefs (theme, chrome, font, scanlines) to
 *  built-in defaults.  Call when Agent Select opens so the screen always
 *  shows the unmodified base appearance before any agent is signed in. */
void prefsAgentResetVisuals(void);

/** B-172: re-capture the *current* visual state (theme, chrome, font,
 *  title bar style, scanlines) as the pd.ini baseline.  Call from the
 *  theme / chrome / font picker after a pre-sign-in change saves to
 *  pd.ini so a subsequent prefsAgentResetVisuals() reverts to the newly
 *  saved preference instead of the boot-time snapshot. */
void prefsAgentRefreshVisualsBaseline(void);

/** Write the current in-memory values for every per-agent key to the
 *  active agent's sidecar.  No-op if no active agent is set. */
void prefsAgentSave(void);

/** Convenience: call prefsAgentSave() only when the caller has already
 *  performed a prefsAgentSetActive(). */
const char *prefsAgentGetActive(void);

/** One-shot migration: if the display-name sidecar doesn't exist, look for
 *  a legacy sidecar built from the raw N64 save bytes (pre-S313 naming).
 *  If found, rename it to the new path and log the migration.
 *  raw_name: file->name[] (16 raw save bytes, the old prefsAgentLoad arg).
 *  display_name: human-readable agent name decoded by gamefileGetOverview. */
void prefsAgentMigrateLegacySidecar(const char *raw_name, const char *display_name);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PREFS_AGENT_H */
