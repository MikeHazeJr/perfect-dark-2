#ifndef _IN_AUTOCAMPAIGN_H
#define _IN_AUTOCAMPAIGN_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Strict release campaign runner.
 *
 * The runner accelerates objective completion through the same scoped helper
 * used by the F6 developer action, then observes the ordinary authoritative
 * endscreen, progression save, unlock, next-mission bridge, and Credits path.
 * It validates one exact catalog-ID plan and fails on skips, substitutions,
 * missing saves, stale identity, bad social state, or numeric fallback.
 *
 * CLI:
 *   --auto-campaign [solo-index]
 *   --auto-campaign-fast
 *   --auto-campaign-difficulty [0..2]
 *   --auto-campaign-through-load [solo-index]
 *   --auto-campaign-verify [solo-index]
 *   --auto-campaign-soak [1..900 game seconds]
 *
 * Before every forced end the runner proves, through the production
 * objective check, that each active objective made only of stage-flag
 * criteria answers to its own flags (objective flag probe).
 *
 * The optional soak is run-only. It holds each mission in live play for the
 * given number of game seconds before the forced end, with the idle player
 * kept invincible, and fails the run if the mission ends by itself, the
 * player dies anyway, or game time stops advancing.
 *
 * The optional through-load terminal is run-only. It builds one exact,
 * inclusive campaign slice and exits successfully after the final planned
 * mission becomes live. This supports bounded transition regressions without
 * manufacturing unlock state for missions outside the slice.
 *
 * A loaded agent is mandatory. Smoke runs should pair either campaign mode
 * with --launch-load-agent <name>. Live and restart-verification evidence is
 * atomically written under the configured save directory.
 */

typedef enum autocampaign_flag {
	AUTOCAMPAIGN_FLAG_NONE = 0,
	AUTOCAMPAIGN_FLAG_FAST = 1u << 0,
	AUTOCAMPAIGN_FLAG_VERIFY_ONLY = 1u << 1
} autocampaign_flag_t;

void autocampaignArm(int start_solo_index, unsigned int flags);
void autocampaignArmAtDifficulty(int start_solo_index, int difficulty,
	unsigned int flags);
void autocampaignArmThroughLoadAtDifficulty(int start_solo_index,
	int final_load_solo_index, int difficulty, unsigned int flags);
void autocampaignDisarm(void);
void autocampaignTick(void);
int  autocampaignIsActive(void);
void autocampaignInitFromCli(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_AUTOCAMPAIGN_H */
