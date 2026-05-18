#ifndef _IN_AUTOCAMPAIGN_H
#define _IN_AUTOCAMPAIGN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Campaign auto-runner (c126, 2026-05-18).
 *
 * Drives the solo campaign mission by mission: for each stage, dwells
 * while the intro cutscene plays, force-completes the objectives via
 * the existing debug flags, triggers the endscreen, then chains into
 * the next mission via endscreenContinue + menuhandlerAcceptMission.
 *
 * The auto-runner does NOT play the gameplay -- it sits as a passive
 * tick driver that nudges the existing campaign flow forward at the
 * right state boundaries. Intro cutscenes play naturally; the dwell
 * window holds until playerAnyInCutscene() reports false. The
 * endscreen and briefing dialogs are visible to the user but advance
 * automatically after a short dwell.
 *
 * Lifecycle:
 *   - CLI: --auto-campaign            arms at solo stage 0 (Defection)
 *   - CLI: --auto-campaign N          arms at solo stage index N
 *   - CLI: --auto-campaign-fast       short dwells, same flow
 *
 * Hierarchical log channel: CAMPAIGN.AUTO.* . State transitions log
 * to LOG_NOTE; load/wait timeouts log to LOG_WARNING.
 *
 * Client-only module. Server target's hand-curated SRC_SERVER list
 * does not include autocampaign.c; server_stubs.c provides inert
 * stubs so the pd-server link stays clean.
 */

typedef enum autocampaign_flag {
	AUTOCAMPAIGN_FLAG_NONE = 0,
	AUTOCAMPAIGN_FLAG_FAST = 1u << 0
} autocampaign_flag_t;

void autocampaignArm(int start_solo_index, unsigned int flags);
void autocampaignDisarm(void);
void autocampaignTick(void);
int  autocampaignIsActive(void);
void autocampaignInitFromCli(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_AUTOCAMPAIGN_H */
