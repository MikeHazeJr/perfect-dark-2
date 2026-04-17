#ifndef _IN_PREFS_AGENT_H
#define _IN_PREFS_AGENT_H

/**
 * prefs_agent.h -- Per-agent preference sidecar (S309)
 *
 * Tracks visual + mod-enablement preferences per Agent profile so the
 * user's look (theme, menu style, title bar, font, scanlines, enabled
 * mods) follows the active agent.  Sidecar file at
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
 * Global pd.ini remains the source of per-machine defaults (resolution,
 * audio volume, gameplay bindings).  The global file is still read at
 * boot; agent prefs overlay on top.
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
