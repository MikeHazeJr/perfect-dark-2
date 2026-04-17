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
 * Font changes still take effect on next restart (ImGui atlas is built
 * once per session — see pdgui_font_mod.h).  All other fields swap live.
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

/** Write the current in-memory values for every per-agent key to the
 *  active agent's sidecar.  No-op if no active agent is set. */
void prefsAgentSave(void);

/** Convenience: call prefsAgentSave() only when the caller has already
 *  performed a prefsAgentSetActive(). */
const char *prefsAgentGetActive(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PREFS_AGENT_H */
