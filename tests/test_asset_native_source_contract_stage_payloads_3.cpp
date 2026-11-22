/*
 * Static pins for c3842 and c3844, split out of
 * tests/test_asset_native_source_contract.cpp for build time (T-TOOLING-018):
 * part 3 of 3 of "scenario stage payloads reject ROM fallback in source-only mode".
 */

#include "catch.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	/* Checkout line endings are not a source contract. Preserve every other
	 * byte, including lone carriage returns, for the existing content pins. */
	std::size_t write = 0;
	for (std::size_t read = 0; read < text.size(); ++read) {
		if (text[read] == '\r' && read + 1 < text.size() && text[read + 1] == '\n') continue;
		text[write++] = text[read];
	}
	text.resize(write);
	return text;
}

std::string functionBlock(const std::string &text, const std::string &name)
{
	const size_t start = text.find(name);
	if (start == std::string::npos) {
		return "";
	}
	const size_t brace = text.find('{', start);
	if (brace == std::string::npos) {
		return "";
	}
	int depth = 0;
	for (size_t i = brace; i < text.size(); i++) {
		if (text[i] == '{') {
			depth++;
		} else if (text[i] == '}') {
			depth--;
			if (depth == 0) {
				return text.substr(start, i - start + 1);
			}
		}
	}
	return "";
}

void requireTokenOrder(const std::string &text, const char *first,
	const char *second)
{
	const size_t first_pos = text.find(first);
	const size_t second_pos = text.find(second);
	INFO("Expected '" << first << "' before '" << second << "'");
	REQUIRE(first_pos != std::string::npos);
	REQUIRE(second_pos != std::string::npos);
	REQUIRE(first_pos < second_pos);
}

size_t countOccurrences(const std::string &text, const char *needle)
{
	size_t count = 0;
	size_t pos = 0;

	while ((pos = text.find(needle, pos)) != std::string::npos) {
		count++;
		pos += strlen(needle);
	}

	return count;
}

}

TEST_CASE("scenario stage payloads reject ROM fallback in source-only mode, part 3 of 3",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string bg_runtime = readTextFile("src/game/bg.c");
	const std::string prop_runtime = readTextFile("src/game/prop.c");
	const std::string tiles = readTextFile("src/game/tilesreset.c");
	const std::string lv = readTextFile("src/game/lv.c");
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string scenario_runtime_h =
		readTextFile("port/include/scenario_source_runtime.h");
	const std::string chraicommands =
		readTextFile("src/game/chraicommands.c");
	const std::string scenario_walker =
		readTextFile("port/src/loader_walker_scenario.c");
	const std::string scenario_extractor =
		readTextFile("port/src/romextract_pdarena.c");
	const std::string mesh_extractor =
		readTextFile("port/src/romextract_pdmesh.c");
	const std::string modasset_compiler =
		readTextFile("port/src/modasset_compiler.c");
	const std::string catalog_api =
		readTextFile("port/src/assetcatalog_api.c");
	const std::string meta_extractor =
		readTextFile("port/src/romextract_pdmeta.c");
	const std::string conformance =
		readTextFile("tools/asset_archive_conformance.py");
	/* Part 3 of 3 of one contract, split for build time (T-TOOLING-018). The locals above are re-declared from part 1. */
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_teams+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.rebuild_squadrons+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_listening+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShotList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteKneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFadeOut") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRemoveChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFaceEntity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteApplyGsetDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrDamageChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDropItem") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunFromTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryJogToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryGoToCoverProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryJogToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryWalkToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryRunToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfCanHearAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfPatrolling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfAlarmActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfGasActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHearsTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawInjury") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawDeath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfLosToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSawTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteIfLosToChr",
		     "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen",
		     "scenarioSourceAiGraphExecuteIfOnScreen",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom",
		     "scenarioSourceAiGraphExecuteIfRoomIsOnScreen",
		     "scenarioSourceAiGraphExecuteIfTargetAimingAtMe",
		     "scenarioSourceAiGraphExecuteIfNearMiss",
		     "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem",
		     "scenarioSourceAiGraphExecuteIfCheckFovWithTarget",
		     "scenarioSourceAiGraphExecuteIfTargetInFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft",
		     "scenarioSourceAiGraphExecuteIfTargetInFov",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFov",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
		     "scenarioSourceAiGraphExecuteIfAnyChrNearSelf",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrInRoom",
		     "scenarioSourceAiGraphExecuteIfTargetInRoom",
		     "scenarioSourceAiGraphExecuteIfChrHasObject",
		     "scenarioSourceAiGraphExecuteIfWeaponThrown",
		     "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject",
		     "scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped",
		     "scenarioSourceAiGraphExecuteIfGunUnclaimed",
		     "scenarioSourceAiGraphExecuteIfObjectHealthy",
		     "scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant",
		     "scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList",
		     "scenarioSourceAiGraphExecuteEndLevel",
		     "scenarioSourceAiGraphExecuteEndCutscene",
		     "scenarioSourceAiGraphExecuteWarpJoToPad",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation",
		     "scenarioSourceAiGraphExecuteIfInCutscene",
		     "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed",
		     "scenarioSourceAiGraphExecuteReorientForCutsceneStop",
		     "scenarioSourceAiGraphExecuteWarpJoToTag",
		     "scenarioSourceAiGraphExecuteRevokeControl",
		     "scenarioSourceAiGraphExecuteGrantControl",
		     "scenarioSourceAiGraphExecutePlayerFadeIn",
		     "scenarioSourceAiGraphExecutePlayersFadeOut",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete",
		     "scenarioSourceAiGraphExecutePrepareWarpOrbit",
		     "scenarioSourceAiGraphExecuteBeginWarpLatch",
		     "scenarioSourceAiGraphExecuteIfWarpLatchComplete",
		     "scenarioSourceAiGraphExecuteDuplicateChr",
		     "scenarioSourceAiGraphExecuteEnableChr",
		     "scenarioSourceAiGraphExecuteDisableChr",
		     "scenarioSourceAiGraphExecuteEnableObj",
		     "scenarioSourceAiGraphExecuteDisableObj",
		     "scenarioSourceAiGraphExecuteChrMoveToPad",
		     "scenarioSourceAiGraphExecuteChrSetTeam",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation",
		     "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan",
		     "scenarioSourceAiGraphExecuteIfChrRepositionValid",
		     "scenarioSourceAiGraphExecuteReleaseCover",
		     "scenarioSourceAiGraphExecuteIfSafety2LessThan",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34",
		     "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor",
		     "scenarioSourceAiGraphExecuteDetectEnemy",
		     "scenarioSourceAiGraphExecuteIfSafetyLessThan",
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly",
		     "scenarioSourceAiGraphExecuteIfTargetMovingCloser",
		     "scenarioSourceAiGraphExecuteIfTargetMovingAway",
		     "scenarioSourceAiGraphExecuteIfSquadronIsDead",
		     "scenarioSourceAiGraphExecuteIfTrue",
		     "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan",
		     "scenarioSourceAiGraphExecuteIfNaturalAnim",
		     "scenarioSourceAiGraphExecuteIfY",
		     "scenarioSourceAiGraphExecuteIfSoundTimer",
		     "scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan",
		     "scenarioSourceAiGraphExecuteChrExplosions",
		     "scenarioSourceAiGraphExecuteSetTintedGlassEnabled",
		     "scenarioSourceAiGraphExecuteHovercopterFireRocket",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur",
		     "scenarioSourceAiGraphExecutePunchOrKick",
		     "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight",
		     "scenarioSourceAiGraphExecuteMiniSkedarTryPounce",
		     "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan",
		     "scenarioSourceAiGraphExecuteAvoid",
		     "scenarioSourceAiGraphExecuteTitleInitMode",
		     "scenarioSourceAiGraphExecuteTryExitTitle",
		     "scenarioSourceAiGraphExecuteChrEmitSparks",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages",
		     "scenarioSourceAiGraphExecuteSayQuip",
		     "scenarioSourceAiGraphExecuteSayCiStaffQuip",
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches",
		     "scenarioSourceAiGraphExecuteTryAttackAmount",
	     }) {
		REQUIRE(scenario_runtime_h.find(symbol) != std::string::npos);
	}
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPunchDodgeList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShootingAtMeList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetDarkRoomList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPlayerDeadList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteStartPatrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteTryStartAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteActivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDeactivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrCopyPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSubtractMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSubtractAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetHearDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetViewDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetGrenadeProbability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrNum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetMaxDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteAddHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetShield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetReactionSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetRecoverySpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAccuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfHasFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrUnsetFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfChrHasFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfStageFlagEq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetAction") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetTeamOrders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRetreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCoverWithinDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteGoToCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteOrbitTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteSetSquadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteFaceCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteDangerCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRebuildTeams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRebuildSquadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteChrSetListening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteWalkToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteRunToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteJogToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceAiGraphExecuteGoToPadPreset") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetList(cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetReturnList(cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShotList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteReturnList()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteStop(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteKneel(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSurrender(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFadeOut(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRemoveChr(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteKillBond()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTrySidestep(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryAttackStand(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfAttacking(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryModifyAttack(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFaceEntity(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteApplyGsetDamage(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrDamageChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDropItem(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunFromTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryJogToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunToTargetProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryGoToCoverProp(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryJogToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryWalkToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryRunToChr(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfCanHearAlarm(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfPatrolling(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfAlarmActive(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfGasActive(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHearsTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawInjury(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawDeath(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfLosToTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSawTargetRecently(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently(g_Vars.chrdata") !=
	        std::string::npos);
	for (const char *call : {
		     "scenarioSourceAiGraphExecuteIfLosToChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfRoomIsOnScreen(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetAimingAtMe(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfNearMiss(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfCheckFovWithTarget(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInFovLeft(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInFov(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetOutOfFov(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan(",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfAnyChrNearSelf(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan(",
		     "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfChrInRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetInRoom(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfChrHasObject(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfWeaponThrown(cmd[2]",
		     "scenarioSourceAiGraphExecuteIfWeaponThrownOnObject(cmd->b2",
		     "scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped(",
		     "scenarioSourceAiGraphExecuteIfGunUnclaimed(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfObjectHealthy(cmd[2]",
		     "scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant(",
		     "scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant(",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEndLevel()",
		     "scenarioSourceAiGraphExecuteEndCutscene()",
		     "scenarioSourceAiGraphExecuteWarpJoToPad(pad_id)",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation(anim_id)",
		     "scenarioSourceAiGraphExecuteIfInCutscene(cmd[2])",
		     "scenarioSourceAiGraphExecuteIfCutsceneButtonPressed(cmd[2])",
		     "scenarioSourceAiGraphExecuteReorientForCutsceneStop(cmd[2])",
		     "scenarioSourceAiGraphExecuteWarpJoToTag(cmd[2]",
		     "scenarioSourceAiGraphExecuteRevokeControl(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteGrantControl(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePlayerFadeIn(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePlayersFadeOut()",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePrepareWarpOrbit(range",
		     "scenarioSourceAiGraphExecuteBeginWarpLatch()",
		     "scenarioSourceAiGraphExecuteIfWarpLatchComplete(cmd[2])",
		     "scenarioSourceAiGraphExecuteDuplicateChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEnableChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDisableChr(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteEnableObj(cmd[2])",
		     "scenarioSourceAiGraphExecuteDisableObj(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrMoveToPad(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteChrSetTeam(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan(",
		     "scenarioSourceAiGraphExecuteIfChrRepositionValid(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteReleaseCover(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteTryAttackAmount(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSafety2LessThan(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34(cmd[2])",
		     "scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteDetectEnemy(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSafetyLessThan(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingCloser(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetMovingAway(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSquadronIsDead(cmd[2], cmd[3])",
		     "scenarioSourceAiGraphExecuteIfTrue(cmd[5])",
		     "scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan(",
		     "scenarioSourceAiGraphExecuteIfNaturalAnim(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfY(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfSoundTimer(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan(",
		     "scenarioSourceAiGraphExecuteChrExplosions(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetTintedGlassEnabled(cmd[2])",
		     "scenarioSourceAiGraphExecuteHovercopterFireRocket(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecutePunchOrKick(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight(",
		     "scenarioSourceAiGraphExecuteMiniSkedarTryPounce(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteIfObjectDistanceToPadLessThan(",
		     "scenarioSourceAiGraphExecuteAvoid(g_Vars.chrdata)",
		     "scenarioSourceAiGraphExecuteTitleInitMode(cmd[2])",
		     "scenarioSourceAiGraphExecuteTryExitTitle(cmd[2])",
		     "scenarioSourceAiGraphExecuteChrEmitSparks(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSayQuip(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteSayCiStaffQuip(g_Vars.chrdata",
		     "scenarioSourceAiGraphExecuteShuffleRuinsPillars(cmd)",
		     "scenarioSourceAiGraphExecuteShufflePelagicSwitches()",
	     }) {
		REQUIRE(chraicommands.find(call) != std::string::npos);
	}
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPunchDodgeList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShootingAtMeList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetDarkRoomList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPlayerDeadList(ailistid)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteJogToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteGoToPadPreset(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteWalkToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRunToPad(g_Vars.chrdata, pad)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPath(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteStartPatrol(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteHovercarBeginPath(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetVehicleSpeed(speedaim, speedtime)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetRotorSpeed(speedaim, speedtime)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteTryStartAlarm(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteActivateAlarm()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDeactivateAlarm()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrCopyPadPreset(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrPreset(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrTarget(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrAddMorale(g_Vars.chrdata, cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSubtractMorale(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrAddAlertness(g_Vars.chrdata, cmd[2]") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSubtractAlertness(g_Vars.chrdata, cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetHearDistance(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetViewDistance(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetGrenadeProbability(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrNum(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetMaxDamage(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteAddHealth(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetShield(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetReactionSpeed(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetRecoverySpeed(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAccuracy(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetDodgeRating(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfHasFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrUnsetFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfChrHasFlag(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetStageFlag(flags)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetStageFlag(flags)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfStageFlagEq(flags") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetSavefileFlag(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag(cmd[2])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet(cmd[2], cmd[3])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset(cmd[2], cmd[3])") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetAction(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetTeamOrders(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRetreat(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCoverWithinDist(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteGoToCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteOrbitTarget(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate(") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteSetSquadron(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteFaceCover(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteDangerCover(g_Vars.chrdata)") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRebuildTeams()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteRebuildSquadrons()") !=
	        std::string::npos);
	REQUIRE(chraicommands.find("scenarioSourceAiGraphExecuteChrSetListening(g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.trigger.volume.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.global.settings.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.pads.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.lists.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_return_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shot_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.return_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.kneel") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.surrender") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.fade_out") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_sidestep") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_attack_stand") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_attacking") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_modify_attack") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.face_entity") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.apply_gset_damage") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_damage_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.consider_grenade_throw") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.drop_item") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_from_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_jog_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_walk_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_go_to_cover_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_jog_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_walk_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_run_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_do_animation") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_one_hand") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_look_around") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.be_surprised_surrender") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.random") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_random_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_random_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.print") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.noop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_punch_dodge_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shooting_at_me_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_dark_room_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_player_dead_list") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.jog_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.go_to_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.walk_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.run_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_path") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.start_patrol") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_start_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.activate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.deactivate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.subtract_morale") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.subtract_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.increase_squadron_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_hear_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_view_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_grenade_probability") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_num") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_max_damage") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.add_health") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_shield") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_reaction_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_recovery_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_accuracy") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_unarmed_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_stage_flag_eq") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_unset_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_chr_has_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_obj_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.open_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.close_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_door_state") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_object_is_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.lock_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unlock_door") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_door_locked") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_lift_stationary") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.lift_go_to_stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_lift_at_stop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.activate_lift") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_using_lift") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_rain") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_snow") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.switch_to_alt_sky") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_wind_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_lights") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_room_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_cutscene_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.configure_environment") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_target2_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_target2_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.speak") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.assign_sound") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.audio_mute_channel") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_channel_free") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_volume") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_volume_by_distance") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_object_sound_playing") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_repeating_sound_from_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound_from_entity") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_repeating_sound_from_pad") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_object_sound_volume_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_sound_from_prop") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_temporary_primary_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.play_x_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.stop_ambient_track") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_draw_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_draw_weapon_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_player_force_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_is_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_has_no_gun") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_delete_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_trigger_shot_list") !=
	        std::string::npos);
	for (const char *node : {
		     "scenario.ai.action.end_level",
		     "scenario.ai.action.end_cutscene",
		     "scenario.ai.action.warp_jo_to_pad",
		     "scenario.ai.action.warp_jo_to_tag",
		     "scenario.ai.action.revoke_control",
		     "scenario.ai.action.grant_control",
		     "scenario.ai.action.player_fade_in",
		     "scenario.ai.action.players_fade_out",
		     "scenario.ai.condition.if_colour_fade_complete",
		     "scenario.ai.action.prepare_warp_orbit",
		     "scenario.ai.action.begin_warp_latch",
		     "scenario.ai.condition.if_warp_latch_complete",
		     "scenario.ai.action.set_camera_animation",
		     "scenario.ai.condition.if_in_cutscene",
		     "scenario.ai.condition.if_cutscene_button_pressed",
		     "scenario.ai.action.reorient_for_cutscene_stop",
	     }) {
		REQUIRE(scenario_extractor.find(node) != std::string::npos);
	}
	REQUIRE(scenario_extractor.find("scenario.ai.action.do_gun_command") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_distance_to_gun_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.recover_gun") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_copy_properties") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.player_auto_walk") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_auto_walk_finished") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_obj_in_room") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"objects\\\": \\\"objects.json\\\", \\\"pads\\\": \\\"pads.json\\\", \\\"scene\\\": \\\"scene.glb\\\", \\\"target\\\": \\\"object.room\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"from\\\": \\\"scenario.pads\\\", \\\"to\\\": \\\"scenario.ai.condition.if_obj_in_room\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_looking_at_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_target_is_player") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_kill") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_weapon_from_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.clear_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.release_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_grab_object") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.toggle_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_cloaked") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_autogun_target_team") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_objective_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_objective_failed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_all_objectives_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_difficulty_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_difficulty_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_id_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_stage_id_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_players_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_kill_count_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_knocked_out_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.kill_bond") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_num_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_health_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_shield_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_injured") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_morale_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_idle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_dead") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_can_see_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_pouncebits_eq") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_training_pc_holographed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_player_using_device") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_begin_or_end_teleport") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_teleport_full_white") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_cutscene_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.fade_screen") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_fade_complete") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_hudpiece_visible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_passive_mode") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_firing_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_portal_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_music_event_queue_is_empty") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_coop_mode") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.remove_references_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_toggle_model_part") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.obj_set_model_part_visible") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_obj_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_obj_health") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_special_death_animation") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_room_to_search") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.unset_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_savefile_flag_set") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_savefile_flag_unset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.restart_timer") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_timer_greater_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.if_countdown_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg_middle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.show_hudmsg_top_middle") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.hovercar_begin_path") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_vehicle_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_rotor_speed") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_explosions") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_object_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.say_quip") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.say_ci_staff_quip") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_ruins_pillars") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_pelagic_switches") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_action") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_team_orders") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.retreat") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover_within_dist") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.find_cover_outside_dist") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.go_to_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.check_cover_out_of_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.orbit_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_squadron") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.face_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.danger_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.release_cover") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.rebuild_teams") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.rebuild_squadrons") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_listening") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_dangerous_object_nearby") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.shuffle_investigation_terminals") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.heli_arm_weapons") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.try_attack_amount") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_can_hear_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_patrolling") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_alarm_active") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_gas_active") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_hears_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_injury") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_death") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_los_to_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_los_to_attack_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_target_nearly_in_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_nearly_in_targets_sight") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_saw_target_recently") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.condition.if_heard_target_recently") !=
	        std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_los_to_chr",
		     "scenario.ai.condition.if_never_been_on_screen",
		     "scenario.ai.condition.if_on_screen",
		     "scenario.ai.condition.if_chr_in_on_screen_room",
		     "scenario.ai.condition.if_room_is_on_screen",
		     "scenario.ai.condition.if_target_aiming_at_me",
		     "scenario.ai.condition.if_near_miss",
		     "scenario.ai.condition.if_sees_suspicious_item",
		     "scenario.ai.condition.if_target_in_fov_left",
		     "scenario.ai.condition.if_check_fov_with_target",
		     "scenario.ai.condition.if_target_out_of_fov_left",
		     "scenario.ai.condition.if_target_in_fov",
		     "scenario.ai.condition.if_target_out_of_fov",
		     "scenario.ai.condition.if_distance_to_target_less_than",
		     "scenario.ai.condition.if_distance_to_target_greater_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_less_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_greater_than",
		     "scenario.ai.condition.if_distance_to_chr_less_than",
		     "scenario.ai.condition.if_distance_to_chr_greater_than",
		     "scenario.ai.condition.if_any_chr_near_self",
		     "scenario.ai.condition.if_distance_from_target_to_pad_less_than",
		     "scenario.ai.condition.if_distance_from_target_to_pad_greater_than",
		     "scenario.ai.condition.if_chr_in_room",
		     "scenario.ai.condition.if_target_in_room",
		     "scenario.ai.condition.if_chr_has_object",
		     "scenario.ai.condition.if_weapon_thrown",
		     "scenario.ai.condition.if_weapon_thrown_on_object",
		     "scenario.ai.condition.if_chr_has_weapon_equipped",
		     "scenario.ai.condition.if_gun_unclaimed",
		     "scenario.ai.condition.if_object_healthy",
		     "scenario.ai.condition.if_chr_activated_object",
		     "scenario.ai.action.obj_interact",
		     "scenario.ai.action.destroy_object",
		     "scenario.ai.action.drop_object_from_chr",
		     "scenario.ai.action.chr_drop_items",
		     "scenario.ai.action.chr_drop_weapon",
		     "scenario.ai.action.give_object_to_chr",
		     "scenario.ai.action.object_move_to_pad",
		     "scenario.ai.condition.if_chr_not_talking",
		     "scenario.ai.condition.if_orders",
		     "scenario.ai.condition.if_has_orders",
		     "scenario.ai.condition.if_chr_in_squadron_doing_action",
		     "scenario.ai.condition.if_chr_listening",
		     "scenario.ai.condition.if_not_listening",
		     "scenario.ai.condition.if_chr_injured_target",
		     "scenario.ai.condition.if_action",
		     "scenario.ai.condition.if_chr_ammo_quantity_less_than",
		     "scenario.ai.condition.if_chr_target",
		     "scenario.ai.condition.if_compare_chr_presets_team",
		     "scenario.ai.condition.if_human",
		     "scenario.ai.condition.if_skedar",
		     "scenario.ai.condition.if_prop_preset_blocking_sight_to_target",
		     "scenario.ai.action.remove_object_at_prop_preset",
		     "scenario.ai.condition.if_prop_preset_height_less_than",
		     "scenario.ai.action.set_target",
		     "scenario.ai.condition.if_presets_target_is_not_my_target",
		     "scenario.ai.action.set_chr_preset_to_chr_near_self",
		     "scenario.ai.action.set_chr_preset_to_chr_near_pad",
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(scenario_extractor.find(kind) != std::string::npos);
	}
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.chr_copy_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_preset") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.action.set_chr_target") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.global.settings") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("trigger.volume.%04u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: setup behavior link source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: setup behavior link registered from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("refusing legacy-only setup behavior") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup_link_logged_mask") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupBehaviorLinkLogBit") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupGraphValidateBlockedPathWaypoints") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupFindRecordByOrder") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupValidateBehaviorLinkSourceTargets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupValidateBehaviorLinkTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("is not a public setup.fields.json row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source type %u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"linked_guns.weapon_1\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"linked_guns.weapon_2\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"lift_door_link.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"lift_door_link.lift\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"safe_item.safe\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"safe_item.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"padlocked_door.door\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.trigger\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.unexploded\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"conditional_scenery.exploded\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"blocked_path.blocker\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("blocked_path waypoint_1 %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("blocked_path waypoint_2 %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.setup.links+setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_bindLevelGraphTablePath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadLevelVolumeSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_activeGraphPathForScenario") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing level graph table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.volumes_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.setup_fields_path") !=
	        std::string::npos);
	{
		const std::string behavior_link =
			functionBlock(scenario_runtime,
				"scenarioSourceSetupGraphRecordBehaviorLink");
		REQUIRE(!behavior_link.empty());
		REQUIRE(behavior_link.find(
				"s_setupGraphValidateBlockedPathWaypoints") !=
		        std::string::npos);
		requireTokenOrder(behavior_link,
			"s_setupGraphValidateBlockedPathWaypoints",
			"s_setupBehaviorLinkTargetsMatch");
	}
	{
		const std::string collect_links =
			functionBlock(scenario_runtime,
				"s_setupCollectBehaviorLinkSource");
		REQUIRE(!collect_links.empty());
		requireTokenOrder(collect_links,
			"s_setupFillBehaviorLink",
			"s_setupValidateBehaviorLinkSourceTargets");
		requireTokenOrder(collect_links,
			"s_setupValidateBehaviorLinkSourceTargets",
			"s_ActiveScenarioGraphs.setup_link_count = count");
	}
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: activated mission graph") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordCheck") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordInsert") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordCriterionStatus") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordObjectState") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordStageFlags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphHasStageFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceMissionGraphRecordPhase") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphGetCriterionType") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceObjectiveGraphRecordEvaluate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective runtime source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective insert matched graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective criteria evaluated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective check routed through graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective state updated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: objective object state updated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: mission flags updated in graph runtime") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: phase source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("MISSION.GRAPH: phase transition from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.mission.phase+mission.graph.nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.mission.phase") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("mission_phase_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("mission_stage_flags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+graph.objective.state+graph.objective.object_state+graph.mission.flags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+graph.objective.state\"") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.objective.operands+og.objective.state") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing executable mission objective source nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing executable mission objective source rows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("assetSourceDebugIsEnabledFor(ASSET_MISSION)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("empty nodes") != std::string::npos);
	REQUIRE(scenario_runtime.find("missing mission objective nodes") !=
	        std::string::npos);
	const std::string objectives_runtime = readTextFile("src/game/objectives.c");
	const std::string inv_runtime = readTextFile("src/game/inv.c");
	const std::string objectives_reset_runtime =
		readTextFile("src/game/objectivesreset.c");
	const std::string stage_reset_runtime =
		readTextFile("src/game/game_00b820.c");
	const std::string chraction_runtime = readTextFile("src/game/chraction.c");
	const std::string netmsg_runtime = readTextFile("port/src/net/netmsg.c");
	const std::string pdmain_runtime = readTextFile("port/src/pdmain.c");
	const std::string lv_runtime = readTextFile("src/game/lv.c");
	REQUIRE(objectives_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordCheck(index, objstatus)") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveCheckGraphSource") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphGetCriterionType(index, i") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphGetCriterionOperand(index, i") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveEvaluateGraphRequirement(&operand)") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveGraphRequirementUsesMissionFlags") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("objectiveGraphRequirementUsesObjectState") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphHasStageFlag") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordCriterionStatus") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceMissionGraphRecordPhase(") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objectives.check\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objectiveIsAllComplete\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceLevelGraphCheckPadRoom(criteria->pad") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objective.enter_room\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("\"objective.throw_in_room\"") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("scenarioSourceObjectiveGraphRecordObjectState") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing criteria graph runtime status") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing graph mission flag state") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("missing graph object state") !=
	        std::string::npos);
	REQUIRE(objectives_runtime.find("chrHasStageFlag(NULL, operand->stage_flag_mask)") ==
	        std::string::npos);
	const size_t graph_eval_start = objectives_runtime.find(
		"static s32 objectiveEvaluateGraphRequirement");
	const size_t graph_eval_end = objectives_runtime.find(
		"static bool objectiveCheckGraphSource");
	REQUIRE(graph_eval_start != std::string::npos);
	REQUIRE(graph_eval_end != std::string::npos);
	REQUIRE(graph_eval_end > graph_eval_start);
	const std::string graph_eval = objectives_runtime.substr(
		graph_eval_start, graph_eval_end - graph_eval_start);
	REQUIRE(graph_eval.find("objFindByTagId") == std::string::npos);
	REQUIRE(graph_eval.find("invHasProp") == std::string::npos);
	REQUIRE(inv_runtime.find("objectiveRecordPropState(insertedprop)") !=
	        std::string::npos);
	REQUIRE(inv_runtime.find("objectiveRecordPropState(removedprop)") !=
	        std::string::npos);
	REQUIRE(objectives_reset_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(objectives_reset_runtime.find("scenarioSourceObjectiveGraphRecordInsert(objective)") !=
	        std::string::npos);
	REQUIRE(stage_reset_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("chrSetStageFlag(NULL, STAGEFLAG_EYESPY_DESTROYED)") !=
	        std::string::npos);
	REQUIRE(chraction_runtime.find("g_StageFlags |= STAGEFLAG_EYESPY_DESTROYED") ==
	        std::string::npos);
	REQUIRE(netmsg_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(netmsg_runtime.find("scenarioSourceObjectiveGraphRecordStageFlags(g_StageFlags)") !=
	        std::string::npos);
	REQUIRE(pdmain_runtime.find("scenario_source_runtime.h") !=
	        std::string::npos);
	REQUIRE(pdmain_runtime.find("scenarioSourceMissionGraphRecordPhase(\"end\", \"mainEndStage\")") !=
	        std::string::npos);
	REQUIRE(lv_runtime.find("scenarioSourceMissionGraphRecordPhase(\"active\", \"lvTick.start\")") !=
	        std::string::npos);
	REQUIRE(lv_runtime.find("scenarioSourceLevelGraphRecordTick(\"lvTick.start\")") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("PDMETA_FAST_CACHE_KIND \"pdmeta_table_backed_v16_botprofile_manifest_objective_status_event_mission_endings_vehiclegraph_modegraph_outside_bots\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("PDMETA_SCENARIO_DEP_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pdscenario_scene_glb_clean_public_v99_standalone_backfill_collision_obj_collision_flags_json_room_lights_json_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_color0_alphamask_quip_shuffle_graph_portals_json_objects_json_setup_fields_json_ai_lists_json_ai_command_graph_navhashes_objectives_spawns_volumes_pads_paths_json_navtables_json") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pdscenario_scene_glb_clean_public_v96_") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("scenario_graph_cache = \" PDMETA_SCENARIO_DEP_CACHE_KIND") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("s_existingArchiveEntryContains") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objectives.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objective.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.objective.criteria.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.phase.source") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.phase.active") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("s_loadScenarioObjectiveRows") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("mission.behavior.parity_backend") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("dependencies/assets/scenarios/%s.pdscenario::objectives.json#%s") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("pd2.mission.objectives.v1") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("objectives_file = objectives.json") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"objectives_file\\\": \\\"objectives.json\\\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("assetArchiveWriterAddPublicMem(&writer, \"briefing.json\"") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"min_players\\\": %d") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("\\\"type_key\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(meta_extractor.find("objectives_file = objectives.tsv") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("\"operand_kind\"") != std::string::npos);
	REQUIRE(meta_extractor.find("Original mission objectives") ==
	        std::string::npos);
	REQUIRE(meta_extractor.find("primary\\tOriginal mission objectives\\toriginal_perfect_dark_setup") ==
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must contain executable mission/objective nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must include mission objective graph nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must include mission phase graph nodes") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.graph.json must bind mission.objectives.source to objectives.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("retired duplicate briefing.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("_meta/manifest.json must declare {field} = {expected}") !=
	        std::string::npos);
	REQUIRE(conformance.find("briefing_file is retired") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must include exactly one {description} graph node") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare ai_lists_file = ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("scenario.ini must declare paths_file = navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("path_ref id must fit native u8") !=
	        std::string::npos);
	REQUIRE(conformance.find("path_ref must preserve path order") ==
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.lists.source\": \"AI list source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.pads.source\": \"pad source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.navigation.paths.source\": \"navigation path source\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_list\": \"AI set_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_return_list\": \"AI set_return_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shot_list\": \"AI set_shot_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.return_list\": \"AI return_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.stop\": \"AI stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.kneel\": \"AI kneel action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.surrender\": \"AI surrender action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.fade_out\": \"AI fade_out action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_chr\": \"AI remove_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_sidestep\": \"AI try_sidestep action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_attack_stand\": \"AI try_attack_stand action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_attacking\": \"AI if_attacking condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_modify_attack\": \"AI try_modify_attack action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.face_entity\": \"AI face_entity action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.apply_gset_damage\": \"AI apply_gset_damage action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_damage_chr\": \"AI chr_damage_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.consider_grenade_throw\": \"AI consider_grenade_throw condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.drop_item\": \"AI drop_item action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_from_target\": \"AI try_run_from_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_jog_to_target_prop\": \"AI try_jog_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_walk_to_target_prop\": \"AI try_walk_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_to_target_prop\": \"AI try_run_to_target_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_go_to_cover_prop\": \"AI try_go_to_cover_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_jog_to_chr\": \"AI try_jog_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_walk_to_chr\": \"AI try_walk_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_run_to_chr\": \"AI try_run_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_do_animation\": \"AI chr_do_animation action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_one_hand\": \"AI be_surprised_one_hand action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_look_around\": \"AI be_surprised_look_around action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.be_surprised_surrender\": \"AI be_surprised_surrender action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.random\": \"AI random action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_random_less_than\": \"AI if_random_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_random_greater_than\": \"AI if_random_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.print\": \"AI print action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.noop\": \"AI no-op action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_punch_dodge_list\": \"AI set_punch_dodge_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shooting_at_me_list\": \"AI set_shooting_at_me_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_dark_room_list\": \"AI set_dark_room_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_player_dead_list\": \"AI set_player_dead_list action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.jog_to_pad\": \"AI jog_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.go_to_pad_preset\": \"AI go_to_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.walk_to_pad\": \"AI walk_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.run_to_pad\": \"AI run_to_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_path\": \"AI set_path action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.start_patrol\": \"AI start_patrol action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_start_alarm\": \"AI try_start_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.activate_alarm\": \"AI activate_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.deactivate_alarm\": \"AI deactivate_alarm action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_morale\": \"AI set_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_morale\": \"AI add_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_add_morale\": \"AI chr_add_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.subtract_morale\": \"AI subtract_morale action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_alertness\": \"AI set_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_alertness\": \"AI add_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_add_alertness\": \"AI chr_add_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.subtract_alertness\": \"AI subtract_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.increase_squadron_alertness\": \"AI increase_squadron_alertness action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_hear_distance\": \"AI set_hear_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_view_distance\": \"AI set_view_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_grenade_probability\": \"AI set_grenade_probability action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_num\": \"AI set_chr_num action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_max_damage\": \"AI set_max_damage action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.add_health\": \"AI add_health action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_shield\": \"AI set_shield action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_reaction_speed\": \"AI set_reaction_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_recovery_speed\": \"AI set_recovery_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_accuracy\": \"AI set_accuracy action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_dodge_rating\": \"AI set_dodge_rating action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_unarmed_dodge_rating\": \"AI set_unarmed_dodge_rating action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_flag\": \"AI set_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_flag\": \"AI unset_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_has_flag\": \"AI if_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_flag\": \"AI chr_set_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_flag\": \"AI chr_unset_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_flag\": \"AI if_chr_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_stage_flag\": \"AI set_stage_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_stage_flag\": \"AI unset_stage_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_stage_flag_eq\": \"AI if_stage_flag_eq action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chrflag\": \"AI set_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_chrflag\": \"AI unset_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_has_chrflag\": \"AI if_has_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_chrflag\": \"AI chr_set_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_chrflag\": \"AI chr_unset_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_chrflag\": \"AI if_chr_has_chrflag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_hidden_flag\": \"AI chr_set_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_unset_hidden_flag\": \"AI chr_unset_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_chr_has_hidden_flag\": \"AI if_chr_has_hidden_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_obj_flag\": \"AI set_obj_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_obj_flag\": \"AI unset_obj_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_obj_has_flag\": \"AI if_obj_has_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.open_door\": \"AI open_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.close_door\": \"AI close_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_door_state\": \"AI if_door_state action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_object_is_door\": \"AI if_object_is_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.lock_door\": \"AI lock_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unlock_door\": \"AI unlock_door action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_door_locked\": \"AI if_door_locked action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_lift_stationary\": \"AI if_lift_stationary action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.lift_go_to_stop\": \"AI lift_go_to_stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_lift_at_stop\": \"AI if_lift_at_stop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.activate_lift\": \"AI activate_lift action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_using_lift\": \"AI if_using_lift action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_rain\": \"AI configure_rain action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_snow\": \"AI configure_snow action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.switch_to_alt_sky\": \"AI switch_to_alt_sky action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_wind_speed\": \"AI set_wind_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_lights\": \"AI set_lights action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_room_flag\": \"AI set_room_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_cutscene_chrs\": \"AI show_cutscene_chrs action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.configure_environment\": \"AI configure_environment action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_target2_less_than\": \"AI if_distance_to_target2_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_target2_greater_than\": \"AI if_distance_to_target2_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.speak\": \"AI speak action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound\": \"AI play_sound action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.assign_sound\": \"AI assign_sound action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.audio_mute_channel\": \"AI audio_mute_channel action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_channel_free\": \"AI if_channel_free condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_volume\": \"AI set_object_sound_volume action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_volume_by_distance\": \"AI set_object_sound_volume_by_distance action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_object_sound_playing\": \"AI set_object_sound_playing action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_repeating_sound_from_object\": \"AI play_repeating_sound_from_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound_from_entity\": \"AI play_sound_from_entity action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_repeating_sound_from_pad\": \"AI play_repeating_sound_from_pad action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_object_sound_volume_less_than\": \"AI if_object_sound_volume_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_sound_from_prop\": \"AI play_sound_from_prop action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_temporary_primary_track\": \"AI play_temporary_primary_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.play_x_track\": \"AI play_x_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.stop_ambient_track\": \"AI stop_ambient_track action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_draw_weapon\": \"AI chr_draw_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_draw_weapon_in_cutscene\": \"AI chr_draw_weapon_in_cutscene action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_player_force_speed\": \"AI set_player_force_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_invincible\": \"AI chr_set_invincible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_is_invincible\": \"AI if_player_is_invincible condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_has_no_gun\": \"AI if_chr_has_no_gun condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_delete_weapon\": \"AI chr_delete_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_trigger_shot_list\": \"AI if_trigger_shot_list condition\"") !=
	        std::string::npos);
	for (const char *node : {
		     "\"scenario.ai.action.end_level\": \"AI end_level action\"",
		     "\"scenario.ai.action.end_cutscene\": \"AI end_cutscene action\"",
		     "\"scenario.ai.action.warp_jo_to_pad\": \"AI warp_jo_to_pad action\"",
		     "\"scenario.ai.action.warp_jo_to_tag\": \"AI warp_jo_to_tag action\"",
		     "\"scenario.ai.action.revoke_control\": \"AI revoke_control action\"",
		     "\"scenario.ai.action.grant_control\": \"AI grant_control action\"",
		     "\"scenario.ai.action.player_fade_in\": \"AI player_fade_in action\"",
		     "\"scenario.ai.action.players_fade_out\": \"AI players_fade_out action\"",
		     "\"scenario.ai.condition.if_colour_fade_complete\": \"AI if_colour_fade_complete condition\"",
		     "\"scenario.ai.action.prepare_warp_orbit\": \"AI prepare_warp_orbit action\"",
		     "\"scenario.ai.action.begin_warp_latch\": \"AI begin_warp_latch action\"",
		     "\"scenario.ai.condition.if_warp_latch_complete\": \"AI if_warp_latch_complete condition\"",
		     "\"scenario.ai.action.set_camera_animation\": \"AI set_camera_animation action\"",
		     "\"scenario.ai.condition.if_in_cutscene\": \"AI if_in_cutscene condition\"",
		     "\"scenario.ai.condition.if_cutscene_button_pressed\": \"AI if_cutscene_button_pressed condition\"",
		     "\"scenario.ai.action.reorient_for_cutscene_stop\": \"AI reorient_for_cutscene_stop action\"",
		     "\"scenario.ai.action.duplicate_chr\": \"AI duplicate_chr action\"",
		     "\"scenario.ai.action.enable_chr\": \"AI enable_chr action\"",
		     "\"scenario.ai.action.disable_chr\": \"AI disable_chr action\"",
		     "\"scenario.ai.action.enable_obj\": \"AI enable_obj action\"",
		     "\"scenario.ai.action.disable_obj\": \"AI disable_obj action\"",
		     "\"scenario.ai.action.chr_move_to_pad\": \"AI chr_move_to_pad action\"",
		     "\"scenario.ai.action.chr_set_team\": \"AI chr_set_team action\"",
		     "\"scenario.ai.action.damage_chr_by_amount\": \"AI damage_chr_by_amount action\"",
		     "\"scenario.ai.action.do_preset_animation\": \"AI do_preset_animation action\"",
		     "\"scenario.ai.condition.if_player_chr_portal_distance_less_than\": \"AI if_player_chr_portal_distance_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_reposition_valid\": \"AI if_chr_reposition_valid condition\"",
	     }) {
		REQUIRE(conformance.find(node) != std::string::npos);
	}
	REQUIRE(conformance.find("\"scenario.ai.action.do_gun_command\": \"AI do_gun_command action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_distance_to_gun_less_than\": \"AI if_distance_to_gun_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.recover_gun\": \"AI recover_gun action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_copy_properties\": \"AI chr_copy_properties action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.player_auto_walk\": \"AI player_auto_walk action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_auto_walk_finished\": \"AI if_player_auto_walk_finished condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_obj_in_room\": \"AI if_obj_in_room condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_looking_at_object\": \"AI if_player_looking_at_object condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_target_is_player\": \"AI if_target_is_player condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_kill\": \"AI chr_kill action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_weapon_from_inventory\": \"AI remove_weapon_from_inventory action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.clear_inventory\": \"AI clear_inventory action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.release_object\": \"AI release_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_grab_object\": \"AI chr_grab_object action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.toggle_p1p2\": \"AI toggle_p1p2 action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_p1p2\": \"AI chr_set_p1p2 action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_cloaked\": \"AI chr_set_cloaked action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_autogun_target_team\": \"AI set_autogun_target_team action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_objective_complete\": \"AI if_objective_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_objective_failed\": \"AI if_objective_failed condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_all_objectives_complete\": \"AI if_all_objectives_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_difficulty_less_than\": \"AI if_difficulty_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_difficulty_greater_than\": \"AI if_difficulty_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_timer_less_than\": \"AI if_stage_timer_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_timer_greater_than\": \"AI if_stage_timer_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_id_less_than\": \"AI if_stage_id_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_stage_id_greater_than\": \"AI if_stage_id_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_players_less_than\": \"AI if_num_players_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_kill_count_greater_than\": \"AI if_kill_count_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_knocked_out_chrs\": \"AI if_num_knocked_out_chrs condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.kill_bond\": \"AI kill_bond action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_num_arghs_less_than\": \"AI if_num_arghs_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_health_greater_than\": \"AI if_chr_health_greater_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_shield_less_than\": \"AI if_chr_shield_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_injured\": \"AI if_injured condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_morale_less_than\": \"AI if_morale_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_alertness\": \"AI if_alertness condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_idle\": \"AI if_idle condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_dead\": \"AI if_chr_dead condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_can_see_target\": \"AI if_can_see_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_pouncebits_eq\": \"AI if_pouncebits_eq condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_training_pc_holographed\": \"AI if_training_pc_holographed condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_player_using_device\": \"AI if_player_using_device condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_begin_or_end_teleport\": \"AI chr_begin_or_end_teleport action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_teleport_full_white\": \"AI if_chr_teleport_full_white condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_cutscene_weapon\": \"AI chr_set_cutscene_weapon action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.fade_screen\": \"AI fade_screen action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_fade_complete\": \"AI if_fade_complete condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_hudpiece_visible\": \"AI set_chr_hudpiece_visible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_passive_mode\": \"AI set_passive_mode action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_firing_in_cutscene\": \"AI chr_set_firing_in_cutscene action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_portal_flag\": \"AI set_portal_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_music_event_queue_is_empty\": \"AI if_music_event_queue_is_empty condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_coop_mode\": \"AI if_coop_mode condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than\": \"AI if_chr_same_floor_distance_to_pad_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.remove_references_to_chr\": \"AI remove_references_to_chr action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_toggle_model_part\": \"AI chr_toggle_model_part action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.obj_set_model_part_visible\": \"AI obj_set_model_part_visible action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_obj_health_less_than\": \"AI if_obj_health_less_than action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_obj_health\": \"AI set_obj_health action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_special_death_animation\": \"AI set_chr_special_death_animation action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_room_to_search\": \"AI set_room_to_search action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_savefile_flag\": \"AI set_savefile_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.unset_savefile_flag\": \"AI unset_savefile_flag action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_savefile_flag_set\": \"AI if_savefile_flag_set action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_savefile_flag_unset\": \"AI if_savefile_flag_unset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.restart_timer\": \"AI restart_timer action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_timer_greater_than_random\": \"AI if_timer_greater_than_random action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_countdown_timer\": \"AI set_countdown_timer action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.if_countdown_timer_greater_than\": \"AI if_countdown_timer_greater_than action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg\": \"AI show_hudmsg action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg_middle\": \"AI show_hudmsg_middle action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.show_hudmsg_top_middle\": \"AI show_hudmsg_top_middle action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.hovercar_begin_path\": \"AI hovercar_begin_path action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_vehicle_speed\": \"AI set_vehicle_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_rotor_speed\": \"AI set_rotor_speed action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_explosions\": \"AI chr_explosions action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_object_distance_to_pad_less_than\": \"AI if_object_distance_to_pad_less_than condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_action\": \"AI set_action action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_team_orders\": \"AI set_team_orders action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.retreat\": \"AI retreat action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover\": \"AI find_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover_within_dist\": \"AI find_cover_within_dist action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.find_cover_outside_dist\": \"AI find_cover_outside_dist action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.go_to_cover\": \"AI go_to_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.check_cover_out_of_sight\": \"AI check_cover_out_of_sight action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.orbit_target\": \"AI orbit_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_preset_to_unalerted_teammate\": \"AI set_chr_preset_to_unalerted_teammate action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_squadron\": \"AI set_squadron action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.face_cover\": \"AI face_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.danger_cover\": \"AI danger_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.release_cover\": \"AI release_cover action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.rebuild_teams\": \"AI rebuild_teams action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.rebuild_squadrons\": \"AI rebuild_squadrons action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_listening\": \"AI chr_set_listening action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.try_attack_amount\": \"AI try_attack_amount action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_pad_preset\": \"AI set_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_set_pad_preset\": \"AI chr_set_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.chr_copy_pad_preset\": \"AI chr_copy_pad_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_can_hear_alarm\": \"AI if_can_hear_alarm condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_patrolling\": \"AI if_patrolling condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_alarm_active\": \"AI if_alarm_active condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_gas_active\": \"AI if_gas_active condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_hears_target\": \"AI if_hears_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_injury\": \"AI if_saw_injury condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_death\": \"AI if_saw_death condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_los_to_target\": \"AI if_los_to_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_los_to_attack_target\": \"AI if_los_to_attack_target condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_target_nearly_in_sight\": \"AI if_target_nearly_in_sight condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_nearly_in_targets_sight\": \"AI if_nearly_in_targets_sight condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_pad_preset_to_pad_on_route_to_target\": \"AI set_pad_preset_to_pad_on_route_to_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_saw_target_recently\": \"AI if_saw_target_recently condition\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.condition.if_heard_target_recently\": \"AI if_heard_target_recently condition\"") !=
	        std::string::npos);
	for (const char *entry : {
		     "\"scenario.ai.condition.if_los_to_chr\": \"AI if_los_to_chr condition\"",
		     "\"scenario.ai.condition.if_never_been_on_screen\": \"AI if_never_been_on_screen condition\"",
		     "\"scenario.ai.condition.if_on_screen\": \"AI if_on_screen condition\"",
		     "\"scenario.ai.condition.if_chr_in_on_screen_room\": \"AI if_chr_in_on_screen_room condition\"",
		     "\"scenario.ai.condition.if_room_is_on_screen\": \"AI if_room_is_on_screen condition\"",
		     "\"scenario.ai.condition.if_target_aiming_at_me\": \"AI if_target_aiming_at_me condition\"",
		     "\"scenario.ai.condition.if_near_miss\": \"AI if_near_miss condition\"",
		     "\"scenario.ai.condition.if_sees_suspicious_item\": \"AI if_sees_suspicious_item condition\"",
		     "\"scenario.ai.condition.if_target_in_fov_left\": \"AI if_target_in_fov_left condition\"",
		     "\"scenario.ai.condition.if_check_fov_with_target\": \"AI if_check_fov_with_target condition\"",
		     "\"scenario.ai.condition.if_target_out_of_fov_left\": \"AI if_target_out_of_fov_left condition\"",
		     "\"scenario.ai.condition.if_target_in_fov\": \"AI if_target_in_fov condition\"",
		     "\"scenario.ai.condition.if_target_out_of_fov\": \"AI if_target_out_of_fov condition\"",
		     "\"scenario.ai.condition.if_distance_to_target_less_than\": \"AI if_distance_to_target_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_target_greater_than\": \"AI if_distance_to_target_greater_than condition\"",
		     "\"scenario.ai.condition.if_chr_distance_to_pad_less_than\": \"AI if_chr_distance_to_pad_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_distance_to_pad_greater_than\": \"AI if_chr_distance_to_pad_greater_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_chr_less_than\": \"AI if_distance_to_chr_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_to_chr_greater_than\": \"AI if_distance_to_chr_greater_than condition\"",
		     "\"scenario.ai.condition.if_any_chr_near_self\": \"AI if_any_chr_near_self condition\"",
		     "\"scenario.ai.condition.if_distance_from_target_to_pad_less_than\": \"AI if_distance_from_target_to_pad_less_than condition\"",
		     "\"scenario.ai.condition.if_distance_from_target_to_pad_greater_than\": \"AI if_distance_from_target_to_pad_greater_than condition\"",
		     "\"scenario.ai.condition.if_chr_in_room\": \"AI if_chr_in_room condition\"",
		     "\"scenario.ai.condition.if_target_in_room\": \"AI if_target_in_room condition\"",
		     "\"scenario.ai.condition.if_chr_has_object\": \"AI if_chr_has_object condition\"",
		     "\"scenario.ai.condition.if_weapon_thrown\": \"AI if_weapon_thrown condition\"",
		     "\"scenario.ai.condition.if_weapon_thrown_on_object\": \"AI if_weapon_thrown_on_object condition\"",
		     "\"scenario.ai.condition.if_chr_has_weapon_equipped\": \"AI if_chr_has_weapon_equipped condition\"",
		     "\"scenario.ai.condition.if_gun_unclaimed\": \"AI if_gun_unclaimed condition\"",
		     "\"scenario.ai.condition.if_object_healthy\": \"AI if_object_healthy condition\"",
		     "\"scenario.ai.condition.if_chr_activated_object\": \"AI if_chr_activated_object condition\"",
		     "\"scenario.ai.action.obj_interact\": \"AI obj_interact action\"",
		     "\"scenario.ai.action.destroy_object\": \"AI destroy_object action\"",
		     "\"scenario.ai.action.drop_object_from_chr\": \"AI drop_object_from_chr action\"",
		     "\"scenario.ai.action.chr_drop_items\": \"AI chr_drop_items action\"",
		     "\"scenario.ai.action.chr_drop_weapon\": \"AI chr_drop_weapon action\"",
		     "\"scenario.ai.action.give_object_to_chr\": \"AI give_object_to_chr action\"",
		     "\"scenario.ai.action.object_move_to_pad\": \"AI object_move_to_pad action\"",
		     "\"scenario.ai.condition.if_chr_not_talking\": \"AI if_chr_not_talking condition\"",
		     "\"scenario.ai.condition.if_orders\": \"AI if_orders condition\"",
		     "\"scenario.ai.condition.if_has_orders\": \"AI if_has_orders condition\"",
		     "\"scenario.ai.condition.if_chr_in_squadron_doing_action\": \"AI if_chr_in_squadron_doing_action condition\"",
		     "\"scenario.ai.condition.if_chr_listening\": \"AI if_chr_listening condition\"",
		     "\"scenario.ai.condition.if_not_listening\": \"AI if_not_listening condition\"",
		     "\"scenario.ai.condition.if_chr_injured_target\": \"AI if_chr_injured_target condition\"",
		     "\"scenario.ai.condition.if_action\": \"AI if_action condition\"",
		     "\"scenario.ai.condition.if_chr_ammo_quantity_less_than\": \"AI if_chr_ammo_quantity_less_than condition\"",
		     "\"scenario.ai.condition.if_chr_target\": \"AI if_chr_target condition\"",
		     "\"scenario.ai.condition.if_compare_chr_presets_team\": \"AI if_compare_chr_presets_team condition\"",
		     "\"scenario.ai.condition.if_human\": \"AI if_human condition\"",
		     "\"scenario.ai.condition.if_skedar\": \"AI if_skedar condition\"",
		     "\"scenario.ai.condition.if_prop_preset_blocking_sight_to_target\": \"AI if_prop_preset_blocking_sight_to_target condition\"",
		     "\"scenario.ai.action.remove_object_at_prop_preset\": \"AI remove_object_at_prop_preset action\"",
		     "\"scenario.ai.condition.if_prop_preset_height_less_than\": \"AI if_prop_preset_height_less_than condition\"",
		     "\"scenario.ai.action.set_target\": \"AI set_target action\"",
		     "\"scenario.ai.condition.if_presets_target_is_not_my_target\": \"AI if_presets_target_is_not_my_target condition\"",
		     "\"scenario.ai.action.set_chr_preset_to_chr_near_self\": \"AI set_chr_preset_to_chr_near_self action\"",
		     "\"scenario.ai.action.set_chr_preset_to_chr_near_pad\": \"AI set_chr_preset_to_chr_near_pad action\"",
		     "\"scenario.ai.condition.if_dangerous_object_nearby\": \"AI if_dangerous_object_nearby condition\"",
		     "\"scenario.ai.condition.if_heli_weapons_armed\": \"AI if_heli_weapons_armed condition\"",
		     "\"scenario.ai.condition.if_hoverbot_next_step\": \"AI if_hoverbot_next_step condition\"",
		     "\"scenario.ai.action.shuffle_investigation_terminals\": \"AI shuffle_investigation_terminals action\"",
		     "\"scenario.ai.action.set_pad_preset_to_investigation_terminal\": \"AI set_pad_preset_to_investigation_terminal action\"",
		     "\"scenario.ai.action.heli_arm_weapons\": \"AI heli_arm_weapons action\"",
		     "\"scenario.ai.action.heli_unarm_weapons\": \"AI heli_unarm_weapons action\"",
		     "\"scenario.ai.condition.if_squadron_is_dead\": \"AI if_squadron_is_dead condition\"",
		     "\"scenario.ai.condition.if_true\": \"AI if_true condition\"",
		     "\"scenario.ai.condition.if_num_chrs_in_squadron_greater_than\": \"AI if_num_chrs_in_squadron_greater_than condition\"",
		     "\"scenario.ai.condition.if_natural_anim\": \"AI if_natural_anim condition\"",
		     "\"scenario.ai.condition.if_y\": \"AI if_y condition\"",
		     "\"scenario.ai.condition.if_sound_timer\": \"AI if_sound_timer condition\"",
		     "\"scenario.ai.condition.if_target_y_difference_less_than\": \"AI if_target_y_difference_less_than condition\"",
		     "\"scenario.ai.action.set_tinted_glass_enabled\": \"AI set_tinted_glass_enabled action\"",
		     "\"scenario.ai.action.hovercopter_fire_rocket\": \"AI hovercopter_fire_rocket action\"",
		     "\"scenario.ai.action.chr_adjust_motion_blur\": \"AI chr_adjust_motion_blur action\"",
		     "\"scenario.ai.action.punch_or_kick\": \"AI punch_or_kick action\"",
		     "\"scenario.ai.action.set_target_to_eyespy_if_in_sight\": \"AI set_target_to_eyespy_if_in_sight action\"",
		     "\"scenario.ai.action.mini_skedar_try_pounce\": \"AI mini_skedar_try_pounce action\"",
		     "\"scenario.ai.action.avoid\": \"AI avoid action\"",
		     "\"scenario.ai.action.title_init_mode\": \"AI title_init_mode action\"",
		     "\"scenario.ai.action.try_exit_title\": \"AI try_exit_title action\"",
		     "\"scenario.ai.action.chr_emit_sparks\": \"AI chr_emit_sparks action\"",
		     "\"scenario.ai.action.set_dr_caroll_images\": \"AI set_dr_caroll_images action\"",
		     "\"scenario.ai.action.say_quip\": \"AI say_quip action\"",
		     "\"scenario.ai.action.say_ci_staff_quip\": \"AI say_ci_staff_quip action\"",
		     "\"scenario.ai.action.shuffle_ruins_pillars\": \"AI shuffle_ruins_pillars action\"",
		     "\"scenario.ai.action.shuffle_pelagic_switches\": \"AI shuffle_pelagic_switches action\"",
		     "\"scenario.ai.condition.if_waypoint_within_quadrant\": \"AI if_waypoint_within_quadrant condition\"",
		     "\"scenario.ai.action.set_pad_preset_to_target_quadrant\": \"AI set_pad_preset_to_target_quadrant action\"",
	     }) {
		REQUIRE(conformance.find(entry) != std::string::npos);
	}
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_preset\": \"AI set_chr_preset action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"scenario.ai.action.set_chr_target\": \"AI set_chr_target action\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must link {pair[0]} to {pair[1]}") !=
	        std::string::npos);
	REQUIRE(conformance.find("level.graph.json must bind paths table to navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.scenario.ai.lists.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("must declare schema pd2.scenario.paths.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("pd2.scenario.spawns.v1") !=
	        std::string::npos);
	REQUIRE(conformance.find("mission.objective.criteria.source") !=
	        std::string::npos);
	REQUIRE(conformance.find("objectives.json still points at original_perfect_dark_setup") !=
	        std::string::npos);
	REQUIRE(conformance.find("SCENARIO_OBJECTIVES_HEADER") !=
	        std::string::npos);
	REQUIRE(conformance.find("\"target_record_ref\"") !=
	        std::string::npos);
	REQUIRE(conformance.find("objective_step.argument") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupParseFieldsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario_source_setup_link_t") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupCollectBehaviorLinkSource") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("objects.json") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled setup.fields.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.y_zero\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.y_max_left\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"autogun.max_speed\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("spawns.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("cmd[0] = INTROCMD_SPAWN") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->ailists = (struct ailist *)(uintptr_t)ailists_offset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->paths = path_count ?") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("count < (s32)ARRAYCOUNT(prop->rooms)") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("PROP.ROOMS: register room list missing terminator") !=
	        std::string::npos);
	REQUIRE(prop_runtime.find("PROP.ROOMS: deregister invalid room") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("BG.ROOMS: skipping invalid entered room") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("BG.ROOMS: skipping invalid portal") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("for (len = 0; len <= maxlen && rooms[len] != -1; len++);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("if (len > maxlen)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("unsupported setup source field") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogResolveBody(catalog_id") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogResolveHead(catalog_id") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("head_selector") != std::string::npos);
	REQUIRE(scenario_runtime.find("\"embedded\"") != std::string::npos);
	REQUIRE(scenario_runtime.find("weapon_selector") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordRefOffset") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordOrderFromId(record_id, default_order)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_setupRecordOrderFromId(record_id, order)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("offsetof(struct tag, cmdoffset)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("offsetof(struct textoverride, objoffset)") !=
	        std::string::npos);
	const std::string setup_create_props = functionBlock(setup, "setupCreateProps");
	REQUIRE(!setup_create_props.empty());
	REQUIRE(setup_create_props.find("scenarioSourceSetupGraphRecordBehaviorLink(") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_LINKGUNS, index") != std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_LINKLIFTDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_SAFEITEM, index") != std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_PADLOCKEDDOOR, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_CONDITIONALSCENERY, index") !=
	        std::string::npos);
	REQUIRE(setup_create_props.find("OBJTYPE_BLOCKEDPATH, index") !=
	        std::string::npos);
	REQUIRE(countOccurrences(setup_create_props,
			"if (!scenarioSourceSetupGraphRecordBehaviorLink(") >= 6);
	requireTokenOrder(setup_create_props, "OBJTYPE_LINKGUNS, index",
		"propweaponSetDual");
	requireTokenOrder(setup_create_props, "OBJTYPE_LINKLIFTDOOR, index",
		"setupCreateLiftDoor(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_SAFEITEM, index",
		"setupCreateSafeItem(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_PADLOCKEDDOOR, index",
		"setupCreatePadlockedDoor(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_CONDITIONALSCENERY, index",
		"setupCreateConditionalScenery(link)");
	requireTokenOrder(setup_create_props, "OBJTYPE_BLOCKEDPATH, index",
		"setupCreateBlockedPath(blockedpath)");
	REQUIRE(catalog_api.find("Extractors run before catalogBuildRuntimeCaches()") !=
	        std::string::npos);
	REQUIRE(catalog_api.find("assetCatalogGetByIndex(i)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("for (s32 i = 0; i < NUM_MODELS; i++)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogModelIdByModelnum(modelnum)") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogReadableModelIdForModelnum(modelnum") !=
	        std::string::npos);
	const std::string mesh_walker =
		readTextFile("port/src/loader_walker_mesh.c");
	REQUIRE(mesh_walker.find("\"mesh\", \"meshes\", \".pdmesh\", /* always_invoke: */ 1") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("assetCatalogGetMutable(id)") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("loaderWalkerMeshSourcePlanManifest") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("loaderWalkerBindMeshSource") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find(
	        "loaderWalkerArchiveTextMember(archive_path, \"mesh.ini\"") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("file_path, public_geometry") !=
	        std::string::npos);
	REQUIRE(mesh_walker.find("catalogSetPrimaryFile(e, source_path)") ==
	        std::string::npos);
	REQUIRE(mesh_extractor.find("pdmesh_fastcache_v29_allmodels_menuhud_") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, \"menu\", \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh_extractor.find("(u16)FILE_GHUDPIECE, \"menu\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_setupFieldHeadRef") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_setupFieldWeaponRef") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\"rename_object.weapon\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("g_WeaponDataCatalogIds[weaponnum]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadTilesForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadTilesForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled scene tiles") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.obj\"") !=
	        std::string::npos);
	// B-943: the per-triangle GEOFLAG/floortype/floorcol sidecar is emitted
	// alongside collision.obj, schema-versioned, and required for an archive to
	// be considered clean (forces re-extraction of pre-B-943 archives).
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.flags.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("pd2.scenario.collision.flags.v1") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(check, \"collision.flags.json\")") !=
	        std::string::npos);
	// Runtime must consume the sidecar as the authoritative flag source and
	// carry floortype/floorcol into the tile cache (not hardcode 0).
	REQUIRE(scenario_runtime.find("tile->floortype = src->floortype;") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("tile->floorcol = src->floorcol;") !=
	        std::string::npos);
	// B-943 (gap #1 part-2): the BG per-room LIGHT table is emitted as a
	// schema-versioned room_lights.json sidecar, required for a clean archive
	// (forces re-extraction of pre-lights archives), and rebuilt at runtime into
	// g_BgLightsFileData so the shoot-out-lights / dimming mechanic survives the
	// scenario-source path.
	REQUIRE(scenario_extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"room_lights.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("pd2.scenario.room.lights.v1") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(check, \"room_lights.json\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildBgRoomLightsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceLoadRoomLightsForStage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_BgLightsFileData = lightdata;") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("collision_source = collision.obj") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"collision_source\\\": \\\"collision.obj\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"collision\\\": \\\"collision.obj\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_existingArchiveHasEntry(check, \"collision.obj\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_collectStandaloneStageScenario") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("assetCatalogIterateByType(ASSET_MAP, s_collectStandaloneStageScenario") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("base:scenario_%s") != std::string::npos);
	REQUIRE(scenario_extractor.find("strcmp(slug, \"citraining\")") ==
	        std::string::npos);
	REQUIRE(scenario_extractor.find(":citraining") != std::string::npos);
	REQUIRE(scenario_extractor.find("return \"firingrange\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("fast-cache blocked by missing/stale standalone stage archive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceFindEntryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("category_exact") != std::string::npos);
	REQUIRE(scenario_runtime.find("s_findScenarioByDerivedStageId") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("assetCatalogResolve(scenario_id)") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("strcmp(kind, \"firingrange\")") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("_PD_ROOM") != std::string::npos);
	REQUIRE(scenario_extractor.find("roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("gltfApplyRoomTags") != std::string::npos);
	REQUIRE(modasset_compiler.find("_PD_ROOM") != std::string::npos);
	REQUIRE(modasset_compiler.find("parseObjRoomName") != std::string::npos);
	REQUIRE(modasset_compiler.find("current_room = parseObjRoomName(p)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("objMeshAddTriangleWithTexcoords(mesh,") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("indices[0], indices[i - 1], indices[i],") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->roomnum") != std::string::npos);
	REQUIRE(modasset_compiler.find("added->roomnum = (RoomNum)tri->roomnum") !=
	        std::string::npos);

	REQUIRE(tiles.find("asset_source_debug.h") != std::string::npos);
	REQUIRE(tiles.find("scenarioSourceLoadTilesForStage(&stage") !=
	        std::string::npos);
	REQUIRE(tiles.find("TILES: using scenario source scene-derived tile cache") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, \"tiles\"") !=
	        std::string::npos);
	REQUIRE(tiles.find("assetLoadToNew(stage.tile_handle") ==
	        std::string::npos);
	REQUIRE(tiles.find("tiles source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(tiles.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(tiles.find("tile source compile failed") !=
	        std::string::npos);

	REQUIRE(lv.find("scenario_source_runtime.h") != std::string::npos);
	REQUIRE(lv.find("lvAddScenarioSourceColmesh") != std::string::npos);
	REQUIRE(lv.find("scenarioSourceFindEntryForStage(&stage") !=
	        std::string::npos);
	REQUIRE(lv.find("catalogLoadStageAsset(ASSET_SCENARIO") !=
	        std::string::npos);
	REQUIRE(lv.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(lv.find("SCENARIO.SOURCE: added scene colmesh") !=
	        std::string::npos);
	REQUIRE(lv.find("MESHCOL: ENABLED -- source=") != std::string::npos);
}
