/**
 * agent_session.h -- production Agent Profile Store and activation boundary.
 *
 * Menus, launch automation, and future account-facing flows use this API so
 * profile identity, validation, preferences, social rebinding, and presence
 * cannot drift into separate load paths.
 */

#ifndef _IN_AGENT_SESSION_H
#define _IN_AGENT_SESSION_H

#include <PR/ultratypes.h>
#include "savefile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_PROFILE_NAME_LENGTH_MAX SAVE_AGENT_NAME_LENGTH_MAX
#define AGENT_PROFILE_NAME_MAX (AGENT_PROFILE_NAME_LENGTH_MAX + 1)
#define AGENT_PROFILE_CAPACITY SAVE_MAX_AGENTS

struct agentprofilesummary {
	char name[AGENT_PROFILE_NAME_MAX];
	u32 totaltime;
	u8 autodifficulty;
	u8 autostageindex;
	u8 thumbnail;
};

/** Return validated profiles in deterministic name order. */
s32 agentSessionListProfiles(struct agentprofilesummary *profiles, s32 maxcount);

/** Generation incremented after every successful store mutation. */
u32 agentSessionProfileRevision(void);

/**
 * Validate and commit one profile, then apply its preference/social identity.
 * Failure leaves the previously active game profile and identity untouched.
 */
s32 agentSessionActivate(const char *name);

/** Store mutations. They never change the active in-memory profile. */
s32 agentSessionCreate(const char *name);
s32 agentSessionCopy(const char *source_name, const char *destination_name);
s32 agentSessionDelete(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _IN_AGENT_SESSION_H */
