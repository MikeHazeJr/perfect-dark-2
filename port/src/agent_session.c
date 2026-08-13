/**
 * agent_session.c -- one production lifecycle for unified Agent Profiles.
 */

#include <PR/ultratypes.h>
#include <string.h>

#include "agent_session.h"
#include "prefs_agent.h"
#include "savefile.h"
#include "system.h"

s32 agentSessionListProfiles(struct agentprofilesummary *profiles, s32 maxcount)
{
	struct saveagentsummary rows[SAVE_MAX_AGENTS];
	s32 count;

	if (!profiles || maxcount <= 0) return 0;
	if (maxcount > AGENT_PROFILE_CAPACITY) maxcount = AGENT_PROFILE_CAPACITY;
	count = saveListAgentProfiles(rows, maxcount);
	for (s32 i = 0; i < count; i++) {
		memset(&profiles[i], 0, sizeof(profiles[i]));
		strncpy(profiles[i].name, rows[i].name,
			sizeof(profiles[i].name) - 1);
		profiles[i].totaltime = rows[i].totaltime;
		profiles[i].autodifficulty = rows[i].autodifficulty;
		profiles[i].autostageindex = rows[i].autostageindex;
		profiles[i].thumbnail = rows[i].thumbnail;
	}
	return count;
}

u32 agentSessionProfileRevision(void)
{
	return saveGetAgentProfileRevision();
}

s32 agentSessionActivate(const char *name)
{
	if (saveLoadAgent(name) != 0) {
		sysLogPrintf(LOG_ERROR,
			"AGENT.SESSION: activation rejected name='%s'; prior session preserved",
			name ? name : "(null)");
		return -1;
	}

	/* Publish identity and presence only after the complete profile has
	 * migrated if needed and committed successfully. */
	prefsAgentPublishActive(name);
	sysLogPrintf(LOG_NOTE,
		"AGENT.SESSION: activation committed name='%s' source=json",
		name);
	return 0;
}

s32 agentSessionCreate(const char *name)
{
	s32 result = saveCreateAgent(name);
	sysLogPrintf(result == 0 ? LOG_NOTE : LOG_WARNING,
		"AGENT.SESSION: create name='%s' result=%s",
		name ? name : "(null)", result == 0 ? "OK" : "FAILED");
	return result;
}

s32 agentSessionCopy(const char *source_name, const char *destination_name)
{
	s32 result = saveCopyAgent(source_name, destination_name);
	sysLogPrintf(result == 0 ? LOG_NOTE : LOG_WARNING,
		"AGENT.SESSION: copy source='%s' destination='%s' result=%s",
		source_name ? source_name : "(null)",
		destination_name ? destination_name : "(null)",
		result == 0 ? "OK" : "FAILED");
	return result;
}

s32 agentSessionDelete(const char *name)
{
	s32 result = saveDeleteAgent(name);
	sysLogPrintf(result == 0 ? LOG_NOTE : LOG_WARNING,
		"AGENT.SESSION: delete name='%s' result=%s",
		name ? name : "(null)", result == 0 ? "OK" : "FAILED");
	return result;
}
