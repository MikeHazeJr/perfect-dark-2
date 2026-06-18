#ifndef _IN_SCENARIO_SOURCE_RUNTIME_H
#define _IN_SCENARIO_SOURCE_RUNTIME_H

#include <PR/ultratypes.h>

#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

struct objective;
struct chrdata;
struct chopperobj;
struct gset;
struct bgportal;

typedef struct scenario_source_objective_operand {
	u8 type;
	u32 stage_flag_mask;
	s32 tag_id;
	s32 pad;
	s32 match_value;
	s32 initial_status;
	s32 runtime_status;
	s32 runtime_status_valid;
	s32 runtime_object_state_valid;
	s32 runtime_object_present;
	s32 runtime_object_healthy;
	s32 runtime_object_held_by_player;
} scenario_source_objective_operand_t;

const asset_entry_t *scenarioSourceFindEntryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp);
void scenarioSourceFatalRuntimeFallbackForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp,
	const char *payload, s32 legacy_id, const char *reason);
s32 scenarioSourceValidateBackgroundGeometryForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp);
s32 scenarioSourceActivateGraphsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp);
s32 scenarioSourceLevelGraphRecordTick(const char *reason);
s32 scenarioSourceLevelGraphCheckPadRoom(s32 pad, s32 room,
	const char *reason, s32 *out_matches);
s32 scenarioSourceSetupGraphRecordBehaviorLink(u8 type, s32 record_index,
	s32 target0, s32 target1, s32 target2, s32 aux0, s32 aux1);
s32 scenarioSourceObjectiveGraphIsActive(void);
s32 scenarioSourceObjectiveGraphRecordInsert(const struct objective *objective);
s32 scenarioSourceObjectiveGraphRecordCriterionStatus(const void *criteria,
	s32 status);
s32 scenarioSourceObjectiveGraphRecordObjectState(s32 tag_id,
	s32 present, s32 healthy, s32 held_by_player);
s32 scenarioSourceObjectiveGraphRecordStageFlags(u32 flags);
s32 scenarioSourceObjectiveGraphHasStageFlag(u32 flag, s32 *out_has_flag);
s32 scenarioSourceMissionGraphRecordPhase(const char *phase,
	const char *reason);
s32 scenarioSourceAiGraphExecuteStop(struct chrdata *chr,
	struct chopperobj *hovercar);
s32 scenarioSourceAiGraphExecuteKneel(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteSurrender(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteFadeOut(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteRemoveChr(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteTrySidestep(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryJumpOut(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryRunSideways(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackWalk(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackRun(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackRoll(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackStand(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackKneel(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackLie(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteIfAttackLocked(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfAttacking(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteTryModifyAttack(struct chrdata *chr,
	struct chopperobj *hovercar, u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteFaceEntity(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteApplyGsetDamage(struct chrdata *basechr,
	s32 chrnum, s32 hitpart, const struct gset *gset);
s32 scenarioSourceAiGraphExecuteChrDamageChr(struct chrdata *basechr,
	s32 attacker_chrnum, s32 victim_chrnum, s32 hitpart);
s32 scenarioSourceAiGraphExecuteConsiderGrenadeThrow(struct chrdata *chr,
	u32 thingtype, u32 thingid, s32 label);
s32 scenarioSourceAiGraphExecuteDropItem(struct chrdata *chr,
	u32 modelnum, u32 weaponnum, s32 label);
s32 scenarioSourceAiGraphExecuteChrDoAnimation(struct chrdata *basechr,
	u16 anim_id, s32 startframe, s32 endframe, u8 chranimflags, s32 merge,
	s32 target_chr, s32 speed_divisor);
s32 scenarioSourceAiGraphExecuteBeSurprisedOneHand(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteBeSurprisedLookAround(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteBeSurprisedSurrender(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteRandom(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteIfRandomLessThan(struct chrdata *chr,
	struct chopperobj *hovercar, s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfRandomGreaterThan(struct chrdata *chr,
	struct chopperobj *hovercar, s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecutePrint(void);
s32 scenarioSourceAiGraphExecuteNoOp(const char *opcode_name, s32 len);
s32 scenarioSourceAiGraphExecuteSetList(s32 target_preset, u16 list_id);
s32 scenarioSourceAiGraphExecuteSetReturnList(s32 target_preset, u16 list_id);
s32 scenarioSourceAiGraphExecuteSetShotList(u16 list_id);
s32 scenarioSourceAiGraphExecuteReturnList(void);
s32 scenarioSourceAiGraphExecuteSetPunchDodgeList(u16 list_id);
s32 scenarioSourceAiGraphExecuteSetShootingAtMeList(u16 list_id);
s32 scenarioSourceAiGraphExecuteSetDarkRoomList(u16 list_id);
s32 scenarioSourceAiGraphExecuteSetPlayerDeadList(u16 list_id);
s32 scenarioSourceAiGraphExecuteTryStartAlarm(struct chrdata *chr,
	s32 pad_id, s32 label);
s32 scenarioSourceAiGraphExecuteActivateAlarm(void);
s32 scenarioSourceAiGraphExecuteDeactivateAlarm(void);
s32 scenarioSourceAiGraphExecuteTryRunFromTarget(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryJogToTargetProp(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryWalkToTargetProp(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryRunToTargetProp(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryGoToCoverProp(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryJogToChr(struct chrdata *chr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteTryWalkToChr(struct chrdata *chr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteTryRunToChr(struct chrdata *chr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfCanHearAlarm(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfPatrolling(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfAlarmActive(s32 label);
s32 scenarioSourceAiGraphExecuteIfGasActive(s32 label);
s32 scenarioSourceAiGraphExecuteIfHearsTarget(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfSawInjury(struct chrdata *chr,
	s32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfSawDeath(struct chrdata *chr,
	s32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfLosToTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 label);
s32 scenarioSourceAiGraphExecuteIfLosToAttackTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetNearlyInSight(struct chrdata *chr,
	u32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfNearlyInTargetsSight(struct chrdata *chr,
	u32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget(
	struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfSawTargetRecently(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfHeardTargetRecently(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfLosToChr(struct chrdata *chr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfNeverBeenOnScreen(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfOnScreen(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrInOnScreenRoom(struct chrdata *chr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfRoomIsOnScreen(struct chrdata *chr,
	s32 pad, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetAimingAtMe(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfNearMiss(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfSeesSuspiciousItem(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfCheckFovWithTarget(struct chrdata *chr,
	s32 angle, s32 use_x, s32 invert_yvisang, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetInFovLeft(struct chrdata *chr,
	s32 angle, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft(struct chrdata *chr,
	s32 angle, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetInFov(struct chrdata *chr,
	s32 angle, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetOutOfFov(struct chrdata *chr,
	s32 angle, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan(
	struct chrdata *chr, f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan(
	struct chrdata *chr, f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceToChrLessThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan(
	struct chrdata *chr, s32 chrnum, f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfAnyChrNearSelf(struct chrdata *chr,
	f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan(
	struct chrdata *chr, f32 distance, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan(
	struct chrdata *chr, f32 distance, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrInRoom(struct chrdata *chr,
	s32 chrnum, s32 room_type, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetInRoom(struct chrdata *chr,
	s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrHasObject(struct chrdata *chr,
	s32 chrnum, s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteIfWeaponThrown(s32 weaponnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfWeaponThrownOnObject(s32 weaponnum,
	s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped(struct chrdata *chr,
	s32 chrnum, s32 weaponnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfGunUnclaimed(struct chrdata *chr,
	s32 tag_id, s32 mode, s32 label);
s32 scenarioSourceAiGraphExecuteIfObjectHealthy(s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrActivatedObject(s32 chrnum,
	s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteObjInteract(s32 tag_id);
s32 scenarioSourceAiGraphExecuteDestroyObject(s32 tag_id);
s32 scenarioSourceAiGraphExecuteDropObjectFromChr(s32 tag_id);
s32 scenarioSourceAiGraphExecuteChrDropItems(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteChrDropWeapon(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteGiveObjectToChr(struct chrdata *basechr,
	s32 tag_id, s32 chrnum);
s32 scenarioSourceAiGraphExecuteObjectMoveToPad(s32 tag_id, s32 padnum);
s32 scenarioSourceAiGraphExecuteJogToPad(struct chrdata *chr, s32 pad);
s32 scenarioSourceAiGraphExecuteGoToPadPreset(struct chrdata *chr,
	s32 speed_code);
s32 scenarioSourceAiGraphExecuteWalkToPad(struct chrdata *chr, s32 pad);
s32 scenarioSourceAiGraphExecuteRunToPad(struct chrdata *chr, s32 pad);
s32 scenarioSourceAiGraphExecuteSetPath(struct chrdata *chr, s32 path_id);
s32 scenarioSourceAiGraphExecuteStartPatrol(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteSetPadPreset(struct chrdata *chr, s32 pad);
s32 scenarioSourceAiGraphExecuteChrSetPadPreset(struct chrdata *basechr,
	s32 chrnum, s32 pad);
s32 scenarioSourceAiGraphExecuteChrCopyPadPreset(struct chrdata *basechr,
	s32 src_chrnum, s32 dst_chrnum);
s32 scenarioSourceAiGraphExecuteSetChrPreset(struct chrdata *chr,
	s32 chrpreset);
s32 scenarioSourceAiGraphExecuteSetChrTarget(struct chrdata *basechr,
	s32 chrnum, s32 chrpreset);
s32 scenarioSourceAiGraphExecuteSetAction(struct chrdata *chr,
	s32 action, s32 clear_orders);
s32 scenarioSourceAiGraphExecuteSetTeamOrders(struct chrdata *chr,
	s32 *out_follow_label);
s32 scenarioSourceAiGraphExecuteRetreat(struct chrdata *chr,
	s32 speed, s32 operation);
s32 scenarioSourceAiGraphExecuteFindCover(struct chrdata *chr,
	u16 criteria, s32 *out_assigned);
s32 scenarioSourceAiGraphExecuteFindCoverWithinDist(struct chrdata *chr,
	u16 criteria, s32 refdist, s32 *out_assigned);
s32 scenarioSourceAiGraphExecuteFindCoverOutsideDist(struct chrdata *chr,
	u16 criteria, s32 refdist, s32 *out_assigned);
s32 scenarioSourceAiGraphExecuteGoToCover(struct chrdata *chr, s32 speed);
s32 scenarioSourceAiGraphExecuteCheckCoverOutOfSight(struct chrdata *chr,
	s32 *out_out_of_sight);
s32 scenarioSourceAiGraphExecuteOrbitTarget(struct chrdata *chr,
	u32 angle, s32 try_alternate, s32 speed);
s32 scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate(
	struct chrdata *chr, s32 distance_units, s32 require_far,
	s32 *out_follow_label);
s32 scenarioSourceAiGraphExecuteSetSquadron(struct chrdata *chr,
	s32 squadron);
s32 scenarioSourceAiGraphExecuteFaceCover(struct chrdata *chr,
	s32 *out_faced);
s32 scenarioSourceAiGraphExecuteDangerCover(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteReleaseCover(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteRebuildTeams(void);
s32 scenarioSourceAiGraphExecuteRebuildSquadrons(void);
s32 scenarioSourceAiGraphExecuteChrSetListening(struct chrdata *basechr,
	s32 chrnum, s32 listening);
s32 scenarioSourceAiGraphExecuteIfChrNotTalking(s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfOrders(struct chrdata *chr, s32 order,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfHasOrders(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction(
	struct chrdata *chr, s32 action, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrListening(struct chrdata *basechr,
	s32 chrnum, s32 listening, s32 check_convtalk, s32 label);
s32 scenarioSourceAiGraphExecuteIfNotListening(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfChrInjuredTarget(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfAction(struct chrdata *chr, s32 action,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan(
	struct chrdata *basechr, s32 chrnum, s32 ammo_type, s32 quantity,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfChrTarget(struct chrdata *basechr,
	s32 chrnum, s32 target_chrnum, s32 require_any_target, s32 label);
s32 scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam(
	struct chrdata *chr, s32 comparison, s32 label);
s32 scenarioSourceAiGraphExecuteIfHuman(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfSkedar(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget(
	struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan(
	struct chrdata *chr, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteSetTarget(struct chrdata *chr,
	struct chopperobj *hovercar, s32 chrnum, s32 mode, s32 flags);
s32 scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget(
	struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf(
	struct chrdata *chr, s32 preset, f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad(
	struct chrdata *chr, s32 preset, f32 distance, s32 padnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfDangerousObjectNearby(struct chrdata *chr,
	s32 flags, s32 label);
s32 scenarioSourceAiGraphExecuteIfHeliWeaponsArmed(
	struct chopperobj *hovercar, s32 label);
s32 scenarioSourceAiGraphExecuteIfHoverbotNextStep(
	struct chopperobj *hovercar, s32 comparison, s32 value, s32 label);
s32 scenarioSourceAiGraphExecuteShuffleInvestigationTerminals(s32 goodtag_id,
	s32 badtag_id, s32 pc1_id, s32 pc2_id, s32 pc3_id, s32 pc4_id,
	s32 enabled);
s32 scenarioSourceAiGraphExecuteSetPadPresetToInvestigationTerminal(
	struct chrdata *chr, s32 tag_id, const u16 *pad_map, s32 pad_map_count);
s32 scenarioSourceAiGraphExecuteHeliSetWeaponsArmed(
	struct chopperobj *hovercar, s32 armed);
s32 scenarioSourceAiGraphExecuteIfSafety2LessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34(s32 label);
s32 scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteDetectEnemy(struct chrdata *chr,
	s32 maxdist_raw, s32 label);
s32 scenarioSourceAiGraphExecuteIfSafetyLessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetMovingSlowly(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetMovingCloser(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetMovingAway(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfSquadronIsDead(s32 squadron, s32 label);
s32 scenarioSourceAiGraphExecuteIfTrue(s32 label);
s32 scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan(s32 threshold,
	s32 squadron, s32 label);
s32 scenarioSourceAiGraphExecuteIfNaturalAnim(struct chrdata *chr,
	s32 anim, s32 label);
s32 scenarioSourceAiGraphExecuteIfY(struct chrdata *basechr, s32 chrnum,
	s32 cutoff_y, s32 comparison, s32 label);
s32 scenarioSourceAiGraphExecuteIfSoundTimer(struct chrdata *chr,
	s32 ticks_value, s32 comparison, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteTryAttackAmount(struct chrdata *chr,
	s32 arg0, s32 arg1);
s32 scenarioSourceAiGraphExecuteSetMorale(struct chrdata *chr, s32 morale);
s32 scenarioSourceAiGraphExecuteAddMorale(struct chrdata *chr, s32 amount);
s32 scenarioSourceAiGraphExecuteChrAddMorale(struct chrdata *basechr,
	s32 amount, s32 chrnum);
s32 scenarioSourceAiGraphExecuteSubtractMorale(struct chrdata *chr,
	s32 amount);
s32 scenarioSourceAiGraphExecuteSetAlertness(struct chrdata *chr,
	s32 alertness);
s32 scenarioSourceAiGraphExecuteAddAlertness(struct chrdata *chr,
	s32 amount);
s32 scenarioSourceAiGraphExecuteChrAddAlertness(struct chrdata *basechr,
	s32 amount, s32 chrnum);
s32 scenarioSourceAiGraphExecuteSubtractAlertness(struct chrdata *chr,
	s32 amount);
s32 scenarioSourceAiGraphExecuteIncreaseSquadronAlertness(
	struct chrdata *chr, s32 amount);
s32 scenarioSourceAiGraphExecuteSetHearDistance(struct chrdata *chr,
	f32 distance);
s32 scenarioSourceAiGraphExecuteSetViewDistance(struct chrdata *chr,
	s32 distance);
s32 scenarioSourceAiGraphExecuteSetGrenadeProbability(struct chrdata *chr,
	s32 probability);
s32 scenarioSourceAiGraphExecuteSetChrNum(struct chrdata *chr, s32 chrnum);
s32 scenarioSourceAiGraphExecuteSetMaxDamage(struct chrdata *basechr,
	struct chopperobj *hovercar, s32 chrnum, f32 maxdamage);
s32 scenarioSourceAiGraphExecuteAddHealth(struct chrdata *chr, f32 amount);
s32 scenarioSourceAiGraphExecuteSetShield(struct chrdata *chr, f32 amount);
s32 scenarioSourceAiGraphExecuteSetReactionSpeed(struct chrdata *chr,
	s32 speed);
s32 scenarioSourceAiGraphExecuteSetRecoverySpeed(struct chrdata *chr,
	s32 speed);
s32 scenarioSourceAiGraphExecuteSetAccuracy(struct chrdata *chr,
	s32 accuracy);
s32 scenarioSourceAiGraphExecuteSetDodgeRating(struct chrdata *chr,
	s32 mode, s32 rating);
s32 scenarioSourceAiGraphExecuteSetUnarmedDodgeRating(struct chrdata *chr,
	s32 rating);
s32 scenarioSourceAiGraphExecuteSetFlag(struct chrdata *chr,
	u32 flags, u8 bank);
s32 scenarioSourceAiGraphExecuteUnsetFlag(struct chrdata *chr,
	u32 flags, u8 bank);
s32 scenarioSourceAiGraphExecuteIfHasFlag(struct chrdata *chr,
	u32 flags, u8 bank, s32 invert, s32 label);
s32 scenarioSourceAiGraphExecuteChrSetFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank);
s32 scenarioSourceAiGraphExecuteChrUnsetFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank);
s32 scenarioSourceAiGraphExecuteIfChrHasFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, u8 bank, s32 label);
s32 scenarioSourceAiGraphExecuteSetStageFlag(u32 flags);
s32 scenarioSourceAiGraphExecuteUnsetStageFlag(u32 flags);
s32 scenarioSourceAiGraphExecuteIfStageFlagEq(u32 flags, s32 expected,
	s32 label);
s32 scenarioSourceAiGraphExecuteSetChrflag(struct chrdata *chr, u32 flags);
s32 scenarioSourceAiGraphExecuteUnsetChrflag(struct chrdata *chr, u32 flags);
s32 scenarioSourceAiGraphExecuteIfHasChrflag(struct chrdata *chr,
	u32 flags, s32 label);
s32 scenarioSourceAiGraphExecuteChrSetChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags);
s32 scenarioSourceAiGraphExecuteChrUnsetChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags);
s32 scenarioSourceAiGraphExecuteIfChrHasChrflag(struct chrdata *basechr,
	s32 chrnum, u32 flags, s32 label);
s32 scenarioSourceAiGraphExecuteChrSetHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags);
s32 scenarioSourceAiGraphExecuteChrUnsetHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags);
s32 scenarioSourceAiGraphExecuteIfChrHasHiddenFlag(struct chrdata *basechr,
	s32 chrnum, u32 flags, s32 label);
s32 scenarioSourceAiGraphExecuteSetObjFlag(s32 tag_id, u32 flags, s32 bank);
s32 scenarioSourceAiGraphExecuteUnsetObjFlag(s32 tag_id, u32 flags, s32 bank);
s32 scenarioSourceAiGraphExecuteIfObjHasFlag(s32 tag_id, u32 flags, s32 bank,
	s32 label);
s32 scenarioSourceAiGraphExecuteOpenDoor(s32 tag_id);
s32 scenarioSourceAiGraphExecuteCloseDoor(s32 tag_id);
s32 scenarioSourceAiGraphExecuteIfDoorState(s32 tag_id, u32 states,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfObjectIsDoor(s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteLockDoor(s32 tag_id, u32 bits);
s32 scenarioSourceAiGraphExecuteUnlockDoor(s32 tag_id, u32 bits);
s32 scenarioSourceAiGraphExecuteIfDoorLocked(s32 tag_id, u32 bits,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfLiftStationary(s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteLiftGoToStop(s32 tag_id, s32 stopnum);
s32 scenarioSourceAiGraphExecuteIfLiftAtStop(s32 tag_id, s32 stopnum,
	s32 label);
s32 scenarioSourceAiGraphExecuteActivateLift(s32 liftnum, s32 tag_id);
s32 scenarioSourceAiGraphExecuteIfUsingLift(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteConfigureRain(u32 intensity);
s32 scenarioSourceAiGraphExecuteConfigureSnow(u32 intensity);
s32 scenarioSourceAiGraphExecuteSwitchToAltSky(void);
s32 scenarioSourceAiGraphExecuteSetWindSpeed(s32 speed);
s32 scenarioSourceAiGraphExecuteSetLights(struct chrdata *chr, s32 padnum,
	s32 operation, s32 arg1, s32 arg2, s32 duration);
s32 scenarioSourceAiGraphExecuteSetRoomFlag(s32 roomnum, s32 flag);
s32 scenarioSourceAiGraphExecuteShowCutsceneChrs(s32 show);
s32 scenarioSourceAiGraphExecuteConfigureEnvironment(s32 roomnum,
	s32 command, s32 value);
s32 scenarioSourceAiGraphExecuteIfDistanceToTarget2(struct chrdata *chr,
	f32 distance, s32 greater_than, s32 label);
s32 scenarioSourceAiGraphExecuteSpeak(struct chrdata *basechr, s32 chrnum,
	s16 text_id, s16 audio_id, s8 channel, s32 subtitle_timer);
s32 scenarioSourceAiGraphExecutePlaySound(s8 channel, s16 audio_id);
s32 scenarioSourceAiGraphExecuteAssignSound(s8 channel, s16 audio_id);
s32 scenarioSourceAiGraphExecuteAudioMuteChannel(s8 channel);
s32 scenarioSourceAiGraphExecuteIfChannelFree(s8 channel, s32 label);
s32 scenarioSourceAiGraphExecuteSetObjectSoundVolume(s8 channel, s16 volume,
	u16 volchangetimer60);
s32 scenarioSourceAiGraphExecuteSetObjectSoundVolumeByDistance(s8 channel,
	f32 playerdist, u16 volchangetimer60);
s32 scenarioSourceAiGraphExecuteSetObjectSoundPlaying(s8 channel, s32 tag_id,
	u16 volchangetimer60);
s32 scenarioSourceAiGraphExecutePlayRepeatingSoundFromObject(s8 channel,
	s32 tag_id, u16 volchangetimer60, u16 dist2, u16 dist3);
s32 scenarioSourceAiGraphExecutePlaySoundFromEntity(s8 channel, s32 entity_id,
	u16 volchangetimer60, u16 dist2, u16 dist3, s32 entity_is_chr);
s32 scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad(s16 padnum,
	s16 sound);
s32 scenarioSourceAiGraphExecuteIfObjectSoundVolumeLessThan(s8 channel,
	s16 value, s32 label);
s32 scenarioSourceAiGraphExecutePlaySoundFromProp(s32 channel, s16 audio_id,
	s32 volume, s32 tag_id, s16 type, u16 flags);
s32 scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack(s32 tracknum);
s32 scenarioSourceAiGraphExecutePlayXTrack(s32 reason, s32 minsecs,
	s32 maxsecs);
s32 scenarioSourceAiGraphExecuteStopXTrack(s32 reason);
s32 scenarioSourceAiGraphExecutePlayTrackIsolated(s32 tracknum);
s32 scenarioSourceAiGraphExecutePlayDefaultTracks(void);
s32 scenarioSourceAiGraphExecutePlayCutsceneTrack(s32 tracknum);
s32 scenarioSourceAiGraphExecuteStopCutsceneTrack(void);
s32 scenarioSourceAiGraphExecutePlayTemporaryTrack(s32 tracknum);
s32 scenarioSourceAiGraphExecuteStopAmbientTrack(void);
s32 scenarioSourceAiGraphExecuteChrDrawWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum);
s32 scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene(
	struct chrdata *basechr, s32 chrnum, s32 weaponnum);
s32 scenarioSourceAiGraphExecuteSetPlayerForceSpeed(struct chrdata *basechr,
	s32 chrnum, s32 speed_x, s32 speed_z);
s32 scenarioSourceAiGraphExecuteChrSetInvincible(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteIfPlayerIsInvincible(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrHasNoGun(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteChrDeleteWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum);
s32 scenarioSourceAiGraphExecuteIfTriggerShotList(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteEndLevel(void);
s32 scenarioSourceAiGraphExecuteEndCutscene(void);
s32 scenarioSourceAiGraphExecuteWarpJoToPad(s32 pad_id);
s32 scenarioSourceAiGraphExecuteSetCameraAnimation(s32 anim_id);
s32 scenarioSourceAiGraphExecuteIfInCutscene(s32 label);
s32 scenarioSourceAiGraphExecuteIfCutsceneButtonPressed(s32 label);
s32 scenarioSourceAiGraphExecuteReorientForCutsceneStop(s32 mode);
s32 scenarioSourceAiGraphExecuteWarpJoToTag(s32 tag_id, s32 arg0, s32 arg1);
s32 scenarioSourceAiGraphExecuteRevokeControl(struct chrdata *basechr,
	s32 chrnum, s32 flags);
s32 scenarioSourceAiGraphExecuteGrantControl(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecutePlayerFadeIn(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecutePlayersFadeOut(void);
s32 scenarioSourceAiGraphExecuteIfColourFadeComplete(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecutePrepareWarpOrbit(s32 range, s32 height1,
	s32 rotangle, s32 padnum, s32 height2, s32 posangle);
s32 scenarioSourceAiGraphExecuteBeginWarpLatch(void);
s32 scenarioSourceAiGraphExecuteIfWarpLatchComplete(s32 label);
s32 scenarioSourceAiGraphExecuteSpawnChrAtPad(struct chrdata *basechr,
	s32 body, s32 head, s32 pad, u16 ailistid, u32 spawnflags,
	s32 label);
s32 scenarioSourceAiGraphExecuteSpawnChrAtChr(struct chrdata *basechr,
	s32 body, s32 head, s32 chrnum, u16 ailistid, u32 spawnflags,
	s32 label);
s32 scenarioSourceAiGraphExecuteTryEquipWeapon(u32 model, s32 weaponnum,
	u32 flags, s32 label);
s32 scenarioSourceAiGraphExecuteTryEquipHat(u32 modelnum, u32 flags,
	s32 label);
s32 scenarioSourceAiGraphExecuteDuplicateChr(struct chrdata *basechr,
	s32 chrnum, u16 ailistid, u32 spawnflags, s32 label);
s32 scenarioSourceAiGraphExecuteEnableChr(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteDisableChr(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteEnableObj(s32 tag_id);
s32 scenarioSourceAiGraphExecuteDisableObj(s32 tag_id);
s32 scenarioSourceAiGraphExecuteChrMoveToPad(struct chrdata *basechr,
	s32 chrnum, s32 pad_or_chr, s32 mode, s32 label);
s32 scenarioSourceAiGraphExecuteChrSetTeam(struct chrdata *basechr,
	s32 chrnum, s32 team);
s32 scenarioSourceAiGraphExecuteDamageChrByAmount(struct chrdata *basechr,
	s32 chrnum, s32 amount, s32 mode);
s32 scenarioSourceAiGraphExecuteDoPresetAnimation(struct chrdata *chr,
	s32 preset);
s32 scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan(
	struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrRepositionValid(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteSetObjImage(s32 tag_id, s32 slot,
	s32 image);
s32 scenarioSourceAiGraphExecuteObjectDoAnimation(s32 anim_id, s32 tag_id,
	s32 speed_divisor, s32 startframe);
s32 scenarioSourceAiGraphExecuteSetDoorOpen(s32 tag_id);
s32 scenarioSourceAiGraphExecuteDoGunCommand(struct chrdata *chr,
	s32 mode, s32 label);
s32 scenarioSourceAiGraphExecuteIfDistanceToGunLessThan(struct chrdata *chr,
	f32 distance, s32 label);
s32 scenarioSourceAiGraphExecuteRecoverGun(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteChrCopyProperties(struct chrdata *basechr,
	s32 src_chrnum, s32 label);
s32 scenarioSourceAiGraphExecutePlayerAutoWalk(struct chrdata *basechr,
	s32 chrnum, s32 pad_id, s32 walkspeed, s32 turnspeed, s32 lookup,
	s32 dist);
s32 scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfObjInRoom(struct chrdata *basechr,
	s32 tag_id, s32 room_id, s32 label);
s32 scenarioSourceAiGraphExecuteIfPlayerLookingAtObject(struct chrdata *basechr,
	s32 chrnum, s32 tag_id, s32 label);
s32 scenarioSourceAiGraphExecuteIfTargetIsPlayer(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteChrKill(struct chrdata *basechr, s32 chrnum);
s32 scenarioSourceAiGraphExecuteRemoveWeaponFromInventory(s32 weaponnum);
s32 scenarioSourceAiGraphExecuteClearInventory(void);
s32 scenarioSourceAiGraphExecuteReleaseObject(void);
s32 scenarioSourceAiGraphExecuteChrGrabObject(struct chrdata *basechr,
	s32 chrnum, s32 tag_id);
s32 scenarioSourceAiGraphExecuteToggleP1P2(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteChrSetP1P2(struct chrdata *basechr,
	s32 chrnum, s32 target_chrnum);
s32 scenarioSourceAiGraphExecuteChrSetCloaked(struct chrdata *basechr,
	s32 chrnum, s32 cloaked, s32 timer);
s32 scenarioSourceAiGraphExecuteSetAutogunTargetTeam(s32 tag_id, s32 team);
s32 scenarioSourceAiGraphExecuteIfIdle(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfStopped(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrDead(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrDeathAnimationFinished(
	struct chrdata *basechr, s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrKnockedOut(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfCanSeeTarget(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfNumArghsLessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfNumArghsGreaterThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfNumCloseArghsLessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfNumCloseArghsGreaterThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrHealthGreaterThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrHealthLessThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrShieldLessThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrShieldGreaterThan(struct chrdata *basechr,
	s32 chrnum, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfInjured(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfShieldDamaged(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfMoraleLessThan(struct chrdata *chr,
	s32 threshold, s32 label);
s32 scenarioSourceAiGraphExecuteIfMoraleLessThanRandom(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfAlertness(struct chrdata *chr,
	s32 threshold, s32 comparison_mode, s32 label);
s32 scenarioSourceAiGraphExecuteIfChrAlertnessLessThan(
	struct chrdata *basechr, s32 threshold, s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteIfAlertnessLessThanRandom(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfObjectiveComplete(s32 objective_index,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfObjectiveFailed(s32 objective_index,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfAllObjectivesComplete(s32 label);
s32 scenarioSourceAiGraphExecuteIfDifficultyLessThan(s32 difficulty,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfDifficultyGreaterThan(s32 difficulty,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfStageTimerLessThan(f32 seconds, s32 label);
s32 scenarioSourceAiGraphExecuteIfStageTimerGreaterThan(f32 seconds,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfStageIdLessThan(s32 stagenum, s32 label);
s32 scenarioSourceAiGraphExecuteIfStageIdGreaterThan(s32 stagenum, s32 label);
s32 scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant(struct chrdata *chr,
	s32 quadrant, s32 label);
s32 scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant(
	struct chrdata *chr, s32 quadrant, s32 label);
s32 scenarioSourceAiGraphExecuteIfNumPlayersLessThan(s32 player_count,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfKillCountGreaterThan(s32 kill_count,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfNumKnockedOutChrs(s32 count,
	s32 comparison_mode, s32 label);
s32 scenarioSourceAiGraphExecuteKillBond(void);
s32 scenarioSourceAiGraphExecuteIfPouncebitsEq(struct chrdata *chr,
	s32 pouncebits, s32 label);
s32 scenarioSourceAiGraphExecuteIfTrainingPcHolographed(s32 label);
s32 scenarioSourceAiGraphExecuteIfPlayerUsingDevice(struct chrdata *basechr,
	s32 chrnum, s32 devicenum, s32 label);
s32 scenarioSourceAiGraphExecuteChrBeginOrEndTeleport(struct chrdata *basechr,
	s32 chrnum, s32 pad_id);
s32 scenarioSourceAiGraphExecuteIfChrTeleportFullWhite(struct chrdata *basechr,
	s32 chrnum, s32 label);
s32 scenarioSourceAiGraphExecuteChrSetCutsceneWeapon(struct chrdata *basechr,
	s32 chrnum, s32 weaponnum, s32 fallback_weaponnum);
s32 scenarioSourceAiGraphExecuteFadeScreen(u32 color, s32 num_frames);
s32 scenarioSourceAiGraphExecuteIfFadeComplete(s32 label);
s32 scenarioSourceAiGraphExecuteSetChrHudpieceVisible(struct chrdata *basechr,
	s32 chrnum, s32 visible);
s32 scenarioSourceAiGraphExecuteSetPassiveMode(s32 enable);
s32 scenarioSourceAiGraphExecuteChrSetFiringInCutscene(struct chrdata *basechr,
	s32 chrnum, s32 firing);
s32 scenarioSourceAiGraphExecuteSetPortalFlag(s32 portalnum, s32 flags);
s32 scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty(s32 label);
s32 scenarioSourceAiGraphExecuteIfCoopMode(s32 label);
s32 scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan(
	struct chrdata *basechr, s32 chrnum, f32 distance, s32 padnum,
	s32 label);
s32 scenarioSourceAiGraphExecuteRemoveReferencesToChr(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteChrToggleModelPart(struct chrdata *basechr,
	s32 chrnum, s32 partnum);
s32 scenarioSourceAiGraphExecuteObjSetModelPartVisible(s32 tag_id,
	s32 partnum, s32 visible);
s32 scenarioSourceAiGraphExecuteIfObjHealthLessThan(s32 tag_id,
	s32 damage, s32 label);
s32 scenarioSourceAiGraphExecuteSetObjHealth(s32 tag_id, s32 damage);
s32 scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation(
	struct chrdata *basechr, s32 chrnum, s32 animation);
s32 scenarioSourceAiGraphExecuteSetRoomToSearch(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteRestartTimer(struct chrdata *chr,
	struct chopperobj *hovercar);
s32 scenarioSourceAiGraphExecuteResetTimer(struct chrdata *chr);
s32 scenarioSourceAiGraphExecutePauseTimer(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteResumeTimer(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteIfTimerStopped(struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteIfTimerGreaterThanRandom(struct chrdata *chr,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfTimerLessThan(struct chrdata *chr,
	struct chopperobj *hovercar, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteIfTimerGreaterThan(struct chrdata *chr,
	struct chopperobj *hovercar, f32 value, s32 label);
s32 scenarioSourceAiGraphExecuteShowCountdownTimer(void);
s32 scenarioSourceAiGraphExecuteHideCountdownTimer(void);
s32 scenarioSourceAiGraphExecuteSetCountdownTimerValue(f32 seconds);
s32 scenarioSourceAiGraphExecuteStopCountdownTimer(void);
s32 scenarioSourceAiGraphExecuteStartCountdownTimer(void);
s32 scenarioSourceAiGraphExecuteIfCountdownTimerStopped(s32 label);
s32 scenarioSourceAiGraphExecuteIfCountdownTimerLessThan(f32 seconds,
	s32 label);
s32 scenarioSourceAiGraphExecuteIfCountdownTimerGreaterThan(f32 seconds,
	s32 label);
s32 scenarioSourceAiGraphExecuteSetSavefileFlag(u32 flag);
s32 scenarioSourceAiGraphExecuteUnsetSavefileFlag(u32 flag);
s32 scenarioSourceAiGraphExecuteIfSavefileFlagIsSet(u32 flag, s32 label);
s32 scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset(u32 flag, s32 label);
s32 scenarioSourceAiGraphExecuteShowHudmsg(struct chrdata *basechr,
	s32 chrnum, u16 text_id);
s32 scenarioSourceAiGraphExecuteShowHudmsgMiddle(s32 mode, s32 colour,
	u16 text_id);
s32 scenarioSourceAiGraphExecuteShowHudmsgTopMiddle(struct chrdata *basechr,
	s32 chrnum, u16 text_id, s32 colour);
s32 scenarioSourceAiGraphExecuteHovercarBeginPath(s32 path_id);
s32 scenarioSourceAiGraphExecuteSetVehicleSpeed(f32 speedaim, f32 speedtime);
s32 scenarioSourceAiGraphExecuteSetRotorSpeed(f32 speedaim, f32 speedtime);
s32 scenarioSourceAiGraphExecuteChrExplosions(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteSetTintedGlassEnabled(s32 enabled);
s32 scenarioSourceAiGraphExecuteHovercopterFireRocket(s32 side);
s32 scenarioSourceAiGraphExecuteChrAdjustMotionBlur(struct chrdata *basechr,
	s32 chrnum, s32 amount, s32 mode);
s32 scenarioSourceAiGraphExecutePunchOrKick(struct chrdata *chr,
	s32 reverse, s32 label);
s32 scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight(
	struct chrdata *chr, s32 label);
s32 scenarioSourceAiGraphExecuteMiniSkedarTryPounce(struct chrdata *chr,
	s32 arg0, s32 arg1, s32 arg2, s32 label);
s32 scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan(
	struct chrdata *chr, s32 tag_id, f32 distance, s32 pad_id, s32 label);
s32 scenarioSourceAiGraphExecuteAvoid(struct chrdata *chr);
s32 scenarioSourceAiGraphExecuteTitleInitMode(s32 mode);
s32 scenarioSourceAiGraphExecuteTryExitTitle(s32 label);
s32 scenarioSourceAiGraphExecuteChrEmitSparks(struct chrdata *basechr,
	s32 chrnum);
s32 scenarioSourceAiGraphExecuteSetDrCarollImages(struct chrdata *basechr,
	s32 chrnum, s32 right_image, s32 left_image);
s32 scenarioSourceAiGraphExecuteSayQuip(struct chrdata *basechr, s32 chr_id,
	s32 row, s32 probability, s32 sound_gap, s32 nearby_mode,
	s32 quip_flags, s32 text_index, s32 colour);
s32 scenarioSourceAiGraphExecuteSayCiStaffQuip(struct chrdata *chr,
	s32 quip_type, s32 channel);
s32 scenarioSourceAiGraphExecuteShuffleRuinsPillars(const u8 *cmd);
s32 scenarioSourceAiGraphExecuteShufflePelagicSwitches(void);
s32 scenarioSourceObjectiveGraphGetCriterionCount(s32 index);
s32 scenarioSourceObjectiveGraphGetCriterionType(s32 index,
	s32 criterion_index, u8 *out_type);
s32 scenarioSourceObjectiveGraphGetCriterionOperand(s32 index,
	s32 criterion_index, scenario_source_objective_operand_t *out_operand);
s32 scenarioSourceObjectiveGraphReportRuntimeMismatch(s32 index,
	const char *action, const char *reason);
s32 scenarioSourceObjectiveGraphRecordEvaluate(s32 index, s32 criteria_count);
s32 scenarioSourceObjectiveGraphRecordCheck(s32 index, s32 status);

u8 *scenarioSourceLoadPadsForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size);
u32 *scenarioSourcePadsGetWideOffsets(const u8 *padfiledata, s32 *out_count);
u8 *scenarioSourceLoadSetupForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size);
u8 *scenarioSourceLoadTilesForStage(const catalog_stage_result_t *stage,
	s32 prefer_mp, s32 *out_size, s32 *out_rooms, s32 *out_tiles);
struct bgportal *scenarioSourceLoadPortalsForStage(
	const catalog_stage_result_t *stage, s32 prefer_mp, s32 *out_count);

#ifdef __cplusplus
}
#endif

#endif
