/*
 * Static pins for c3842 and c3844, split out of
 * tests/test_asset_native_source_contract.cpp for build time (T-TOOLING-018):
 * part 2 of 3 of "scenario stage payloads reject ROM fallback in source-only mode".
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

TEST_CASE("scenario stage payloads reject ROM fallback in source-only mode, part 2 of 3",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string scenario_runtime =
		readTextFile("port/src/scenario_source_runtime.c");
	const std::string speak_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteSpeak");
	/* Part 2 of 3 of one contract, split for build time (T-TOOLING-018). The locals above are re-declared from part 1. */
	requireTokenOrder(speak_block,
		"s_aiGraphResolveSpeechAudioSourceId(\"speak\"",
		"s_aiGraphResolveLangTextCatalogId(\"speak\"");
	REQUIRE(speak_block.find(
		        "s_aiGraphResolveRuntimeCharacterRefOrSelector(\"speak\"") !=
	        std::string::npos);
	REQUIRE(speak_block.find("s_aiGraphRequireRuntimePlayerSlot(\"speak\"") !=
	        std::string::npos);
	REQUIRE(speak_block.find("chrFindById(basechr, chrnum)") ==
	        std::string::npos);
	REQUIRE(speak_block.find(
		        "chr_rows=%d target_chr=%d target_selector=%d player_checked=%d text_id=%s audio_id=%s") !=
	        std::string::npos);
	REQUIRE(speak_block.find("player_checked=%d") != std::string::npos);
	REQUIRE(speak_block.find("text_id=%s audio_id=%s") !=
	        std::string::npos);
	REQUIRE(speak_block.find("text=%d audio_id=%s") ==
	        std::string::npos);
	requireTokenOrder(speak_block,
		"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"speak\"",
		"s_aiGraphRequireRuntimePlayerSlot(\"speak\"");
	requireTokenOrder(speak_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"speak\"",
		"setCurrentPlayerNum(playernum)");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteShowHudmsg",
		     "scenarioSourceAiGraphExecuteShowHudmsgMiddle",
		     "scenarioSourceAiGraphExecuteShowHudmsgTopMiddle",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveLangTextCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("text_id=%s") != std::string::npos);
		REQUIRE(block.find("text=%u") == std::string::npos);
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphHudPlayerForChr");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimeCharacterRefFromBase") !=
		        std::string::npos);
		REQUIRE(helper.find("playermgrGetPlayerNumByProp") !=
		        std::string::npos);
		REQUIRE(helper.find("s32 playernum = g_Vars.currentplayernum") ==
		        std::string::npos);
		REQUIRE(helper.find("playernum = g_Vars.currentplayernum;") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimePlayerSlot(action") !=
		        std::string::npos);
		requireTokenOrder(helper,
			"s_aiGraphRequireRuntimeCharacterRefFromBase",
			"playernum = g_Vars.currentplayernum;");
		const size_t fallback_player_pos =
			helper.find("playernum = g_Vars.currentplayernum;");
		REQUIRE(fallback_player_pos != std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimePlayerSlot(action",
			fallback_player_pos) != std::string::npos);
		for (const char *fn : {
			     "scenarioSourceAiGraphExecuteShowHudmsg",
			     "scenarioSourceAiGraphExecuteShowHudmsgTopMiddle",
		     }) {
			const std::string block = functionBlock(scenario_runtime, fn);
			REQUIRE(!block.empty());
			REQUIRE(block.find("s32 playernum = g_Vars.currentplayernum") ==
			        std::string::npos);
			REQUIRE(block.find("chr_rows=%d target_chr=%d player_checked=%d") !=
			        std::string::npos);
			REQUIRE(block.find("player_checked=%d") != std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphHudPlayerForChr(",
				"setCurrentPlayerNum(playernum)");
		}
	}
	const std::string say_quip_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayQuip");
	REQUIRE(say_quip_block.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("s_aiGraphResolveLangTextCatalogId") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_GuardQuipBank") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_QuipTexts") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("chr_rows=%d target_chr=%d source_chr=%d checked=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("text=%d") == std::string::npos);
	REQUIRE(say_quip_block.find("g_Vars.players[playernum]->isdead") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find("struct chrdata *active_chr = g_Vars.chrdata") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find("chrFindById(basechr") ==
	        std::string::npos);
	REQUIRE(say_quip_block.find(
		        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"") !=
	        std::string::npos);
	REQUIRE(say_quip_block.find(
		        "s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"") !=
	        std::string::npos);
	REQUIRE(countOccurrences(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(") >= 2);
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"",
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"CHRRACE(active_chr)");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"g_GuardQuipBank");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(\"say_quip\"",
		"active_chr->soundtimer");
	requireTokenOrder(say_quip_block,
		"\"say_quip\", loopchr",
		"loopchr && loopchr->model");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(\"say_quip\"",
		"playermgrGetPlayerNumByProp(chr->prop)");
	requireTokenOrder(say_quip_block,
		"playermgrGetPlayerNumByProp(chr->prop)",
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"",
		"player->isdead");
	requireTokenOrder(say_quip_block,
		"s_aiGraphRequireRuntimePlayerSlot(\"say_quip\"",
		"g_Vars.coopplayernum >= 0");
	requireTokenOrder(say_quip_block,
		"g_Vars.coopplayernum >= 0 && player->isdead",
		"alternate_playernum = playernum == g_Vars.bondplayernum");
	requireTokenOrder(say_quip_block,
		"alternate_playernum = playernum == g_Vars.bondplayernum",
		"playernum = (u32)alternate_playernum");
	const std::string ci_quip_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayCiStaffQuip");
	REQUIRE(ci_quip_block.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("g_CiGreetingQuips") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("chr_rows=%d target_chr=%d type=%d channel=%d audio_id=%s") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
	        std::string::npos);
	REQUIRE(ci_quip_block.find("quip=%d") == std::string::npos);
	requireTokenOrder(ci_quip_block,
		"s_aiGraphRequireRuntimeCharacterPointer(",
		"g_CiGreetingQuips[chr->morale]");
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrNotTalking");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_chr_not_talking chr=%d chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->propsoundcount");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrAddMorale",
		     "scenarioSourceAiGraphExecuteChrAddAlertness",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"incrementByte(&chr->");
	}
	const std::string max_damage_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetMaxDamage");
	REQUIRE(!max_damage_block.empty());
	REQUIRE(max_damage_block.find("chrFindById(basechr, chrnum)") ==
	        std::string::npos);
	REQUIRE(max_damage_block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(max_damage_block.find("chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d value=%.3f") !=
	        std::string::npos);
	requireTokenOrder(max_damage_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"chrSetMaxDamage(chr, maxdamage)");
	REQUIRE(scenario_runtime.find("s_aiGraphFindChrById(") ==
	        std::string::npos);
	for (const char *fn : {
		     "s_aiGraphRequireRuntimeCharacterRefFromBase",
		     "s_aiGraphResolveRuntimeCharacterRefOrSelector",
		     "s_aiGraphRuntimeCharacterRefLooksSourceBacked",
		     "s_aiGraphRequireRuntimeCharacterPointer",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRuntimeChrIsSourceSpawned(") !=
		        std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrflag",
		     "scenarioSourceAiGraphExecuteUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfHasChrflag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterStatePointer(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteUnsetChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHasChrflag");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"result = ((chr->chrflags & flags) == flags)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrSetChrflag",
		     "scenarioSourceAiGraphExecuteChrUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfChrHasChrflag",
		     "scenarioSourceAiGraphExecuteChrSetHiddenFlag",
		     "scenarioSourceAiGraphExecuteChrUnsetHiddenFlag",
		     "scenarioSourceAiGraphExecuteIfChrHasHiddenFlag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d selector=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		REQUIRE(block.find("if (selector)") != std::string::npos);
		REQUIRE(block.find("\"missing runtime character id %d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (selector)",
			"\"missing runtime character id %d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		REQUIRE(block.find("if (!chr || !chr->prop)") !=
		        std::string::npos);
		REQUIRE(block.find("return 1;") != std::string::npos);
		REQUIRE(block.find("runtime character id %d does not point at a source character row") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (!chr || !chr->prop)",
			"return 1;");
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
			"runtime character id %d does not point at a source character row");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->chrflags |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrUnsetChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->chrflags &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasChrflag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"result = chr ? ((chr->chrflags & flags) == flags) : 0");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->hidden |= flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrUnsetHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\tchr->hidden &= ~flags");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasHiddenFlag");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"result = chr ? ((chr->hidden & flags) == flags) : 0");
	}
	const std::string music_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveMusicCatalogId");
	const std::string music_match_helper =
		functionBlock(scenario_runtime, "s_aiGraphMatchMusicCatalogId");
	REQUIRE(music_id_helper.find("assetCatalogIterateByType") !=
	        std::string::npos);
	REQUIRE(music_id_helper.find("ASSET_AUDIO") != std::string::npos);
	REQUIRE(music_match_helper.find("AUDIO_CAT_MUSIC") != std::string::npos);
	REQUIRE(music_match_helper.find("ext.audio.sound_id") !=
	        std::string::npos);
	REQUIRE(music_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack",
		     "scenarioSourceAiGraphExecutePlayTrackIsolated",
		     "scenarioSourceAiGraphExecutePlayCutsceneTrack",
		     "scenarioSourceAiGraphExecutePlayTemporaryTrack",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveMusicCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("track_id=%s") != std::string::npos);
		REQUIRE(block.find("track=%d") == std::string::npos);
	}
	const std::string play_x_track_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecutePlayXTrack");
	REQUIRE(play_x_track_block.find("minsecs=%d maxsecs=%d") !=
	        std::string::npos);
	REQUIRE(play_x_track_block.find("track=%d") == std::string::npos);
	const std::string anim_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveAnimationCatalogId");
	const std::string catalog_register_anim =
		functionBlock(readTextFile("port/src/assetcatalog.c"),
			"assetCatalogRegisterAnimation");
	const std::string walker_anim =
		readTextFile("port/src/loader_walker_anim.c");
	REQUIRE(catalog_register_anim.find("entry->source_animnum = anim_id") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("id, /* anim_id: */ -1") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("e->source_animnum = (s32)source_index") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("fsFileLoad(selected.path, &command_size)") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("loaderPoolParseAnimationCatalogSourceJson(command_json, command_size") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("loaderPoolParseAnimationJson(manifest, manifest_len)") ==
	        std::string::npos);
	REQUIRE(walker_anim.find("LOADER.POOL.ANIMATION.SOURCE: id=%s source=%s bytes=%u") !=
	        std::string::npos);
	REQUIRE(walker_anim.find("LOADER.POOL.ANIMATION.SOURCE_FAIL") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogResolveAnim") != std::string::npos);
	REQUIRE(anim_id_helper.find("assetCatalogGetByIndex") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("has no public .pdanim source") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogLoadStageAsset(ASSET_ANIMATION") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("catalogGetLoadedAnimationClip") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("has no compiled public clip") !=
	        std::string::npos);
	REQUIRE(anim_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string special_death_anim_helper =
		functionBlock(scenario_runtime,
			"s_aiGraphResolveSpecialDeathAnimationCatalogIds");
	REQUIRE(special_death_anim_helper.find("SPECIALDIE_NONE") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("SPECIALDIE_ONCHAIR") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("g_SpecialDieAnims") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("s_aiGraphResolveAnimationCatalogId") !=
	        std::string::npos);
	REQUIRE(special_death_anim_helper.find("invalid special death animation mode %d") !=
	        std::string::npos);
	requireTokenOrder(special_death_anim_helper,
		"specialdie < SPECIALDIE_FALLBACK || specialdie > SPECIALDIE_ONCHAIR",
		"g_SpecialDieAnims[specialdie - 1].animnum");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDoAnimation",
		     "scenarioSourceAiGraphExecuteSetCameraAnimation",
		     "scenarioSourceAiGraphExecuteObjectDoAnimation",
		     "scenarioSourceAiGraphExecuteDoPresetAnimation",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveAnimationCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("anim_id=%s") != std::string::npos);
		REQUIRE(block.find("anim_source=%s") != std::string::npos);
		REQUIRE(block.find("clip_bytes=%u") != std::string::npos);
		REQUIRE(block.find("anim=%d") == std::string::npos);
		REQUIRE(block.find("anim=%u") == std::string::npos);
	}
	const std::string preset_anim_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDoPresetAnimation");
	REQUIRE(preset_anim_block.find("selected_anim") != std::string::npos);
	requireTokenOrder(preset_anim_block,
		"s_aiGraphResolveAnimationCatalogId",
		"chrTryStartAnim");
	REQUIRE(preset_anim_block.find(
			"preset=%d anim_id=%s anim_source=%s clip_bytes=%u applied=%d") != std::string::npos);
	REQUIRE(preset_anim_block.find(
			"preset=%d applied=%d source=%s") == std::string::npos);
	const std::string natural_anim_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteIfNaturalAnim");
	// B-1161: the comparison takes an empty base slot as it is.
	REQUIRE(natural_anim_block.find("s_aiGraphResolveComparableAnimationCatalogId") !=
	        std::string::npos);
	REQUIRE(natural_anim_block.find("anim_id=%s anim_source=%s clip_bytes=%u current_anim_id=%s current_anim_source=%s current_clip_bytes=%u") !=
	        std::string::npos);
	REQUIRE(natural_anim_block.find("anim=%d current=%d") ==
	        std::string::npos);
	const std::string special_death_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation");
	REQUIRE(special_death_block.find(
			"s_aiGraphResolveSpecialDeathAnimationCatalogIds") !=
	        std::string::npos);
	REQUIRE(special_death_block.find(
			"chr_rows=%d target_chr=%d specialdie=%d anim_id=%s alt_anim_id=%s chair_fallback_anim_id=%s") !=
	        std::string::npos);
	REQUIRE(special_death_block.find("chrFindById(basechr, chrnum);") ==
	        std::string::npos);
	REQUIRE(special_death_block.find("animation=%d") == std::string::npos);
	requireTokenOrder(special_death_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"s_aiGraphResolveSpecialDeathAnimationCatalogIds");
	requireTokenOrder(special_death_block,
		"s_aiGraphResolveSpecialDeathAnimationCatalogIds",
		"chr->specialdie = animation");
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_players_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_kill_count_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_knocked_out_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.kill_bond") !=
	        std::string::npos);
	{
		const std::string kill_count = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfKillCountGreaterThan");
		REQUIRE(!kill_count.empty());
		REQUIRE(kill_count.find(
			        "AI condition if_kill_count_greater_than kill_count=%d current=%d") !=
		        std::string::npos);
		requireTokenOrder(kill_count,
			"s_aiGraphRequireMissionGlobalConditionNode(",
			"branch_taken = g_Vars.killcount > kill_count");
		requireTokenOrder(kill_count,
			"branch_taken = g_Vars.killcount > kill_count",
			"AI condition if_kill_count_greater_than kill_count=%d current=%d");
	}
	{
		const std::string kill_bond = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteKillBond");
		REQUIRE(!kill_bond.empty());
		REQUIRE(kill_bond.find("g_Vars.bond->isdead = true") ==
		        std::string::npos);
		REQUIRE(kill_bond.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		requireTokenOrder(kill_bond,
			"s_aiGraphRequireRuntimePlayerPointer(\"kill_bond\"",
			"bond->isdead = true");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteKillBond") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.mission_global+ai/ailists.json+mission.graph.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.mission_global+ai/ailists.json+mission.graph.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_arghs_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_close_arghs_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_num_close_arghs_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_health_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_shield_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_shield_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_injured") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_shield_damaged") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_morale_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_morale_less_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_alertness_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_alertness_less_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.character_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_idle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_dead") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_death_animation_finished") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_knocked_out") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_can_see_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.lifecycle+ai/ailists.json") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfChrDead",
		     "scenarioSourceAiGraphExecuteIfChrKnockedOut",
		     "scenarioSourceAiGraphExecuteIfChrShieldLessThan",
		     "scenarioSourceAiGraphExecuteIfChrShieldGreaterThan",
		     "scenarioSourceAiGraphExecuteIfInjured",
		     "scenarioSourceAiGraphExecuteIfShieldDamaged",
		     "scenarioSourceAiGraphExecuteIfChrAlertnessLessThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDeathAnimationFinished");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("AI condition if_chr_death_animation_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp(chr->prop)",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->isdead");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphChrHealthComparison");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->bondhealth") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->bondhealth");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->maxdamage - chr->damage");
	}
	REQUIRE(scenario_runtime.find("AI condition if_chr_dead chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_death_animation_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_knocked_out chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_health_greater_than chr=%d chr_rows=%d target_chr=%d player_checked=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_health_less_than chr=%d chr_rows=%d target_chr=%d player_checked=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_shield_less_than chr=%d chr_rows=%d target_chr=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_shield_greater_than chr=%d chr_rows=%d target_chr=%d value=%.3f current=%.3f label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_injured chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_shield_damaged chr=%d chr_rows=%d target_chr=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_alertness_less_than chr=%d chr_rows=%d target_chr=%d threshold=%d current=%d label=%d branch=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_pouncebits_eq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_training_pc_holographed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_using_device") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.state_device+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_player_using_device chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d device=%d label=%d branch=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerUsingDevice");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_using_device\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_using_device\"",
			"currentPlayerGetDeviceState(devicenum)");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_begin_or_end_teleport") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_teleport_full_white") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_cutscene_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.fade_screen") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_fade_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_hudpiece_visible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_passive_mode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_firing_in_cutscene") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrHudpieceVisible",
		     "scenarioSourceAiGraphExecuteChrSetFiringInCutscene",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_portal_flag") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimePortal");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("missing portals.json source") !=
		        std::string::npos);
		REQUIRE(helper.find("missing source-built portal table") !=
		        std::string::npos);
		REQUIRE(helper.find("if (!g_BgPortals)") !=
		        std::string::npos);
		REQUIRE(helper.find("g_BgNumPortalCameraCacheItems > 0") !=
		        std::string::npos);
		REQUIRE(helper.find("portalnum >= 0") != std::string::npos);
		REQUIRE(helper.find("portalnum < g_BgNumPortalCameraCacheItems") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPortalFlag");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_portal_flag portal=%d found=%d portal_rows=%d") !=
		        std::string::npos);
		REQUIRE(block.find("portals=%s scene=scene.glb backend=graph.ai.action.cutscene_presentation+ai/ailists.json+portals.json+scene.glb") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimePortal(\"set_portal_flag\"",
			"if (portal_available)");
		requireTokenOrder(block,
			"if (portal_available)",
			"g_BgPortals[portalnum].flags |= flags");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_music_event_queue_is_empty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_coop_mode") !=
	        std::string::npos);
	{
		const std::string queue_block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty");
		REQUIRE(!queue_block.empty());
		REQUIRE(queue_block.find("AI condition if_music_event_queue_is_empty queue=%d waited=%d branch=%d label=%d") !=
		        std::string::npos);
		requireTokenOrder(queue_block,
			"missing ai/ailists.json source",
			"g_MusicEventQueueLength && !waited");
		requireTokenOrder(queue_block,
			"missing ai/ailists.json source",
			"g_MusicEventQueueLength, waited");
	}
	{
		const std::string coop_block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCoopMode");
		REQUIRE(!coop_block.empty());
		REQUIRE(coop_block.find("AI condition if_coop_mode coop=%d normmp=%d branch=%d label=%d") !=
		        std::string::npos);
		requireTokenOrder(coop_block,
			"missing ai/ailists.json source",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(coop_block,
			"missing ai/ailists.json source",
			"g_MissionConfig.iscoop");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_same_floor_distance_to_pad_less_than chr=%d chr_rows=%d target_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_references_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_toggle_model_part") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.obj_set_model_part_visible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_obj_health_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_obj_health") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_special_death_animation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_room_to_search") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowCutsceneChrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_visibility+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayXTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStopAmbientTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.music_track+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDrawWeapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPlayerForceSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetInvincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerIsInvincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrHasNoGun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDoGunCommand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDistanceToGunLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRecoverGun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireCurrentGunProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("current gun prop has invalid runtime weapon %d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("current gun prop is type %d without weapon payload") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_no_gun chr=%d chr_rows=%d target_chr=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_draw_weapon chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_draw_weapon_in_cutscene chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_player_force_speed chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_player_is_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action do_gun_command chr_rows=%d source_chr=%d mode=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_distance_to_gun_less_than chr_rows=%d source_chr=%d distance=%.2f gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action recover_gun chr_rows=%d source_chr=%d gun_model_id=%s weapon_id=%s") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphResolvePlayerChr");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphResolveRuntimeCharacterRefOrSelector") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRequireRuntimeCharacterRefFromBase") ==
		        std::string::npos);
		REQUIRE(helper.find("PROPTYPE_PLAYER") != std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteSetPlayerForceSpeed",
		     "scenarioSourceAiGraphExecuteChrSetInvincible",
		     "scenarioSourceAiGraphExecuteIfPlayerIsInvincible",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteSetPlayerForceSpeed",
		     "scenarioSourceAiGraphExecuteChrSetInvincible",
		     "scenarioSourceAiGraphExecuteIfPlayerIsInvincible",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolvePlayerChr(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string chr_set_invincible = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetInvincible");
		REQUIRE(!chr_set_invincible.empty());
		REQUIRE(chr_set_invincible.find(
			        "AI action chr_set_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(chr_set_invincible,
			"missing scenario.ai.action.chr_set_invincible node",
			"g_PlayerInvincible = true");
		requireTokenOrder(chr_set_invincible,
			"s_aiGraphResolvePlayerChr(\"chr_set_invincible\"",
			"g_PlayerInvincible = true");
		requireTokenOrder(chr_set_invincible,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_invincible\"",
			"g_PlayerInvincible = true");
	}
	{
		const std::string if_player_is_invincible = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerIsInvincible");
		REQUIRE(!if_player_is_invincible.empty());
		REQUIRE(if_player_is_invincible.find(
			        "AI condition if_player_is_invincible chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(if_player_is_invincible,
			"missing scenario.ai.condition.if_player_is_invincible node",
			"pass = g_PlayerInvincible ? 1 : 0");
		requireTokenOrder(if_player_is_invincible,
			"s_aiGraphResolvePlayerChr(\"if_player_is_invincible\"",
			"pass = g_PlayerInvincible ? 1 : 0");
		requireTokenOrder(if_player_is_invincible,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_player_is_invincible\"",
			"pass = g_PlayerInvincible ? 1 : 0");
	}
	{
		const std::string set_player_force_speed = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPlayerForceSpeed");
		REQUIRE(set_player_force_speed.find(
			        "g_Vars.currentplayer->bondforcespeed") ==
		        std::string::npos);
		requireTokenOrder(set_player_force_speed,
			"s_aiGraphRequireRuntimePlayerSlot(\"set_player_force_speed\"",
			"player->bondforcespeed.x");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfChrHasNoGun",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteIfTriggerShotList",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string if_gun_unclaimed =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfGunUnclaimed");
		REQUIRE(!if_gun_unclaimed.empty());
		requireTokenOrder(if_gun_unclaimed,
			"s_aiGraphRequireCurrentGunProp(\"if_gun_unclaimed\"",
			"weapon->base.flags |= OBJFLAG_FORCENOBOUNCE");
	}
	{
		const std::string if_chr_has_no_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfChrHasNoGun");
		REQUIRE(!if_chr_has_no_gun.empty());
		requireTokenOrder(if_chr_has_no_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_chr_has_no_gun\"",
			"pass = chr && chr->model && chr->gunprop == NULL");
		requireTokenOrder(if_chr_has_no_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_chr_has_no_gun\"",
			"s_ActiveScenarioGraphs.ai_action_if_chr_has_no_gun_logged");
	}
	{
		const std::string do_gun_command =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDoGunCommand");
		REQUIRE(!do_gun_command.empty());
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireRuntimeCharacterPointer(\"do_gun_command\"",
			"s_aiGraphRequireCurrentGunProp(\"do_gun_command\"");
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireRuntimeCharacterPointer(\"do_gun_command\"",
			"chrGoToProp(chr, chr->gunprop, GOPOSFLAG_JOG)");
		requireTokenOrder(do_gun_command,
			"s_aiGraphRequireCurrentGunProp(\"do_gun_command\"",
			"chrGoToProp(chr, chr->gunprop, GOPOSFLAG_JOG)");
	}
	{
		const std::string distance_to_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfDistanceToGunLessThan");
		REQUIRE(!distance_to_gun.empty());
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireCurrentGunProp(\"if_distance_to_gun_less_than\"");
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"xdiff = chr->prop->pos.x - chr->gunprop->pos.x");
		requireTokenOrder(distance_to_gun,
			"s_aiGraphRequireCurrentGunProp(\"if_distance_to_gun_less_than\"",
			"xdiff = chr->prop->pos.x - chr->gunprop->pos.x");
	}
	{
		const std::string recover_gun =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteRecoverGun");
		REQUIRE(!recover_gun.empty());
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireRuntimeCharacterPointer(\"recover_gun\"",
			"prop = chr->gunprop");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireRuntimeCharacterPointer(\"recover_gun\"",
			"chr->gunprop = NULL");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireCurrentGunProp(\"recover_gun\"",
			"chr->gunprop = NULL");
		requireTokenOrder(recover_gun,
			"s_aiGraphRequireCurrentGunProp(\"recover_gun\"",
			"chrEquipWeapon(prop->weapon, chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrCopyProperties") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action duplicate_chr chr=%d chr_rows=%d source_chr=%d body_id=%s weapon0_model_id=%s weapon0_id=%s weapon1_model_id=%s weapon1_id=%s hat_model_id=%s ailist=%u pad=%d found=%d pad_rows=%d pass=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_copy_properties src_chr=%d chr_rows=%d source_chr=%d label=%d pad=%d found=%d pad_rows=%d copied=%d") !=
	        std::string::npos);
	const std::string enable_chr_block = functionBlock(scenario_runtime,
		"scenarioSourceAiGraphExecuteEnableChr");
	REQUIRE(!enable_chr_block.empty());
	REQUIRE(enable_chr_block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(enable_chr_block.find("chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	const std::string disable_chr_block = functionBlock(scenario_runtime,
		"scenarioSourceAiGraphExecuteDisableChr");
	REQUIRE(!disable_chr_block.empty());
	REQUIRE(disable_chr_block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
	        std::string::npos);
	REQUIRE(disable_chr_block.find("chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	{
		const std::string enable_chr = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEnableChr");
		REQUIRE(enable_chr.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"enable_chr\"") !=
		        std::string::npos);
		requireTokenOrder(enable_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"propActivate(chr->prop)");
	}
	{
		const std::string disable_chr = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDisableChr");
		REQUIRE(disable_chr.find(
			        "s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"disable_chr\"") !=
		        std::string::npos);
		requireTokenOrder(disable_chr,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"propDeregisterRooms(chr->prop)");
	}
	{
		const std::string duplicate_chr =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDuplicateChr");
		REQUIRE(!duplicate_chr.empty());
		REQUIRE(duplicate_chr.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		// B-1165: the clone is registered as source-spawned, like the
		// spawn_chr_at_* characters, before the branch is taken.
		requireTokenOrder(duplicate_chr,
			"cloneprop = chrSpawnAtChr(basechr, chr->bodynum, -1,",
			"s_aiGraphRegisterSourceSpawnedChr(\"duplicate_chr\",\n\t\t\tcloneprop)");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRegisterSourceSpawnedChr(\"duplicate_chr\",",
			"g_Vars.aioffset = chraiGoToLabel(g_Vars.ailist,");
		// B-1166: a number set_chr_num gives a source-derived chr stays
		// source-derived, alive (the chrnum check) or gone (both resolvers).
		const std::string set_chr_num = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrNum");
		REQUIRE(!set_chr_num.empty());
		requireTokenOrder(set_chr_num,
			"const s32 was_source = s_aiGraphRuntimeChrIsSourceSpawned(chr) ||",
			"chrSetChrnum(chr, chrnum);");
		requireTokenOrder(set_chr_num,
			"chrSetChrnum(chr, chrnum);",
			"s_aiGraphRecordSourceAssignedChrnum(\"set_chr_num\", chrnum)");
		const std::string source_chrnum = functionBlock(scenario_runtime,
			"s_aiGraphSetupSourceContainsChrnum");
		REQUIRE(source_chrnum.find("s_aiGraphChrnumIsSourceAssigned(chrnum)") !=
		        std::string::npos);
		REQUIRE(countOccurrences(scenario_runtime,
			"if (s_sourceSetupContainsChrnum(chr_id) ||\n\t\t\t\t\ts_aiGraphChrnumIsSourceAssigned(chr_id)) {") == 2);
		REQUIRE(duplicate_chr.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphRequireRuntimePad(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphRequireRuntimePadTable(\n\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveModelCatalogId(\n\t\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("s_aiGraphResolveWeaponCatalogId(\n\t\t\t\t\t\"duplicate_chr\"") !=
		        std::string::npos);
		REQUIRE(duplicate_chr.find("body_id=%s weapon0_model_id=%s weapon0_id=%s") !=
		        std::string::npos);
		requireTokenOrder(duplicate_chr,
			"s_aiEntityLifecycleGraphReady(\"duplicate_chr\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(duplicate_chr,
			"ailistFindById(ailistid)",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = chr->padpreset1");
		requireTokenOrder(duplicate_chr,
			"copied_pad = chr->padpreset1",
			"s_aiGraphRequireRuntimePad(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimePad(\"duplicate_chr\"",
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveModelCatalogId(",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveWeaponCatalogId(",
			"cloneprop = chrSpawnAtChr");
		requireTokenOrder(duplicate_chr,
			"cloneprop = chrSpawnAtChr",
			"clone->padpreset1 = copied_pad");
		requireTokenOrder(duplicate_chr,
			"missing scenario.ai.action.duplicate_chr node",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"missing pads.json source",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_MissionConfig.iscoop");
		requireTokenOrder(duplicate_chr,
			"missing scenario.ai.action.duplicate_chr node",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"missing pads.json source",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"duplicate_chr\"",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_Vars.normmplayerisrunning");
		requireTokenOrder(duplicate_chr,
			"s_aiGraphResolveBodyCatalogId(\"duplicate_chr\"",
			"g_Vars.numaibuddies");
		REQUIRE(duplicate_chr.find(
			        "AI action duplicate_chr chr=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
	}
	{
		const std::string copy_properties =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrCopyProperties");
		REQUIRE(!copy_properties.empty());
		REQUIRE(copy_properties.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		REQUIRE(copy_properties.find("s_aiGraphRequireRuntimePadTable(\n\t\t\t\t\"chr_copy_properties\"") !=
		        std::string::npos);
		requireTokenOrder(copy_properties,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = src->padpreset1");
		requireTokenOrder(copy_properties,
			"copied_pad = src->padpreset1",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(copy_properties,
			"s_aiGraphRequireRuntimePad(",
			"basechr->hearingscale = src->hearingscale");
		requireTokenOrder(copy_properties,
			"basechr->hearingscale = src->hearingscale",
			"basechr->padpreset1 = copied_pad");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayerAutoWalk") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"player_auto_walk\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action player_auto_walk chr=%d chr_rows=%d target_chr=%d player_checked=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayerAutoWalk");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(\"player_auto_walk\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"player_auto_walk\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_auto_walk\"",
			"playerAutoWalk(");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"chr_move_to_pad\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_move_to_pad chr=%d chr_rows=%d target_chr=%d operand=%d mode88_chr=%d mode88_chr_rows=%d resolved_pad=%d mode=%d pass=%d pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrMoveToPad");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr, pad_or_chr") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"chr_move_to_pad\"") !=
		        std::string::npos);
		REQUIRE(block.find("s32 pad_count = s_countRuntimePads();") ==
		        std::string::npos);
		REQUIRE(block.find("mode88_target_chr = target_operand") !=
		        std::string::npos);
		REQUIRE(block.find("&mode88_chr_count, &chr2") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(\"chr_move_to_pad\"",
			"chrResolvePadId");
		requireTokenOrder(block,
			"&mode88_chr_count, &chr2",
			"chrGetInverseTheta");
	}
	REQUIRE(scenario_runtime.find("AI action warp_jo_to_pad pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action spawn_chr_at_pad body_id=%s head_id=%s pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteRevokeControl",
		     "scenarioSourceAiGraphExecuteGrantControl",
		     "scenarioSourceAiGraphExecutePlayerFadeIn",
		     "scenarioSourceAiGraphExecuteIfColourFadeComplete",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_player_auto_walk_finished chr=%d chr_rows=%d target_chr=%d player_checked=%d label=%d walking=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerAutoWalkFinished");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
		requireTokenOrder(block,
			"setCurrentPlayerNum(playernum)",
			"g_Vars.tickmode == TICKMODE_AUTOWALK");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr") == std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteApplyGsetDamage",
		     "scenarioSourceAiGraphExecuteChrDamageChr",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(basechr") == std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI action remove_chr chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action apply_gset_damage chr=%d chr_rows=%d target_chr=%d hitpart=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_damage_chr attacker=%d attacker_chr_rows=%d source_attacker_chr=%d victim=%d victim_chr_rows=%d source_victim_chr=%d hitpart=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireOptionalRuntimeCharacterPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s chr=%d chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_attack_locked chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_attacking chr_rows=%d source_chr=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireCharacterConditionActor") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_idle chr_rows=%d source_chr=%d actiontype=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_num_arghs_less_than chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_morale_less_than chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_alertness_less_than_random chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action random chr_rows=%d source_chr=%d value=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_pouncebits_eq chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"static s32 s_aiGraphRequireCharacterConditionActor(const char *condition");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequireCharacterConditionNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
	}
	struct ActiveStateGuardCase {
		const char *fn;
		const char *live_read;
	};
	const ActiveStateGuardCase active_state_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfIdle", "chr->actiontype == ACT_ANIM" },
		{ "scenarioSourceAiGraphExecuteIfStopped", "chrIsStopped(chr)" },
		{ "scenarioSourceAiGraphExecuteIfCanSeeTarget", "chrCheckCanSeeTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumArghsLessThan", "chrGetNumArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumArghsGreaterThan", "chrGetNumArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumCloseArghsLessThan", "chrGetNumCloseArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNumCloseArghsGreaterThan", "chrGetNumCloseArghs(chr)" },
		{ "scenarioSourceAiGraphExecuteIfMoraleLessThan", "chr && chr->prop ? chr->morale : 0" },
		{ "scenarioSourceAiGraphExecuteIfMoraleLessThanRandom", "chr && chr->prop ? chr->morale : 0" },
		{ "scenarioSourceAiGraphExecuteIfAlertness", "chr && chr->prop ? chr->alertness : 0" },
		{ "scenarioSourceAiGraphExecuteIfAlertnessLessThanRandom", "chr && chr->prop ? chr->alertness : 0" },
		{ "scenarioSourceAiGraphExecuteIfPouncebitsEq", "chr->pouncebits == pouncebits" },
	};
	for (const ActiveStateGuardCase &entry : active_state_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireCharacterConditionActor(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(",
			entry.live_read);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRandom");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"random\"",
			"chr->random = rngRandom() & 0xff");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRandomLessThan");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"branch_taken = (chr && chr->random < threshold)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRandomGreaterThan");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"branch_taken = (chr && chr->random > threshold)");
	}
	REQUIRE(scenario_runtime.find("AI action try_modify_attack chr=%d chr_rows=%d source_chr=%d hovercar=%d vehicle_rows=%d source_vehicle_type=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action face_entity chr=%d chr_rows=%d source_chr=%d type=%u id=%u label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition consider_grenade_throw chr=%d chr_rows=%d source_chr=%d type=%u id=%u label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action drop_item chr=%d chr_rows=%d source_chr=%d model_id=%s weapon_id=%s label=%d result=%d") !=
	        std::string::npos);
	struct CombatGuardCase {
		const char *fn;
		const char *live_call;
	};
	const CombatGuardCase combat_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteTrySidestep", "chrTrySidestep(chr)" },
		{ "scenarioSourceAiGraphExecuteTryJumpOut", "chrTryJumpOut(chr)" },
		{ "scenarioSourceAiGraphExecuteTryRunSideways", "chrTryRunSideways(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackWalk", "chrTryAttackWalk(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackRun", "chrTryAttackRun(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackRoll", "chrTryAttackRoll(chr)" },
		{ "scenarioSourceAiGraphExecuteTryAttackStand", "chrTryAttackStand(chr" },
		{ "scenarioSourceAiGraphExecuteTryAttackKneel", "chrTryAttackKneel(chr" },
		{ "scenarioSourceAiGraphExecuteTryAttackLie", "chrTryAttackLie(chr" },
		{ "scenarioSourceAiGraphExecuteTryModifyAttack", "chrTryModifyAttack(chr" },
		{ "scenarioSourceAiGraphExecuteFaceEntity", "chrFaceEntity(chr" },
		{ "scenarioSourceAiGraphExecuteConsiderGrenadeThrow", "chrConsiderGrenadeThrow(chr" },
		{ "scenarioSourceAiGraphExecuteDropItem", "chrDropItem(chr" },
	};
	for (const CombatGuardCase &entry : combat_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			entry.live_call);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryModifyAttack");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperAttack(hovercar)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfAttackLocked",
		     "scenarioSourceAiGraphExecuteIfAttacking",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chr->actiontype");
	}
	REQUIRE(scenario_runtime.find("AI action %s chr=%d chr_rows=%d source_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryRunFromTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(\"try_run_from_target\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrTryRunFromTarget(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphExecuteTryToTargetProp");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(action",
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
			"chrGoToTarget(chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryGoToCoverProp");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(\"try_go_to_cover_prop\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrGoToCoverProp(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphExecuteTryToChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireTargetMovementNode(action",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(action");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(action",
			"chrGoToChr(chr");
	}
	for (const char *helper_name : {
		     "s_aiGraphRequireRuntimeCharacterRefFromBase",
		     "s_aiGraphResolveRuntimeCharacterRefOrSelector",
		     "s_aiGraphRuntimeCharacterRefLooksSourceBacked",
	     }) {
		const std::string helper = functionBlock(scenario_runtime,
			helper_name);
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chr = chrFindById(basechr, chr_id);") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphRuntimeChrIsSourceSpawned(chr)") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphSetupSourceContainsChrnum(chr->chrnum)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI %s %s chr_rows=%d source_chr=%d value=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_pad_on_route_to_target chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s chr_rows=%d source_chr=%d value=%d label=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_room_is_on_screen chr_rows=%d source_chr=%d pad=%d") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequirePerceptionAlarmActor");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePerceptionAlarmNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireSpatialPerceptionActor");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePerceptionAlarmNode(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
	}
	struct PerceptionAlarmGuardCase {
		const char *fn;
		const char *live_read;
	};
	const PerceptionAlarmGuardCase perception_alarm_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfCanHearAlarm", "chrCanHearAlarm(chr)" },
		{ "scenarioSourceAiGraphExecuteIfPatrolling", "chr->actiontype" },
		{ "scenarioSourceAiGraphExecuteIfHearsTarget", "chrIsHearingTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfSawInjury", "chrSawInjury(chr" },
		{ "scenarioSourceAiGraphExecuteIfSawDeath", "chrSawDeath(chr" },
		{ "scenarioSourceAiGraphExecuteIfLosToTarget", "chrHasLosToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfLosToAttackTarget", "chrHasLosToAttackTarget(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetNearlyInSight", "chrIsTargetNearlyInSight(chr" },
		{ "scenarioSourceAiGraphExecuteIfNearlyInTargetsSight", "chrIsNearlyInTargetsSight(chr" },
		{ "scenarioSourceAiGraphExecuteIfSawTargetRecently", "chrSawTargetRecently(chr)" },
		{ "scenarioSourceAiGraphExecuteIfHeardTargetRecently", "chrHeardTargetRecently(chr)" },
	};
	for (const PerceptionAlarmGuardCase &entry : perception_alarm_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequirePerceptionAlarmActor(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePerceptionAlarmActor(",
			entry.live_read);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfLosToTarget",
		     "scenarioSourceAiGraphExecuteIfLosToAttackTarget",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperCheckTargetInFov(hovercar, 64)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperCheckTargetInSight(hovercar)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequirePerceptionAlarmNode(\"action\"",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrGetTargetProp(chr)");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrSetPadPresetToPadOnRouteToTarget(chr)");
	}
	struct SpatialPerceptionGuardCase {
		const char *fn;
		const char *live_read;
	};
	const SpatialPerceptionGuardCase spatial_perception_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfLosToChr", "chrHasLosToPos(basechr" },
		{ "scenarioSourceAiGraphExecuteIfNeverBeenOnScreen", "chr->chrflags" },
		{ "scenarioSourceAiGraphExecuteIfOnScreen", "chr->prop->flags" },
		{ "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom", "s_aiGraphChrInOnScreenRoom(chr)" },
		{ "scenarioSourceAiGraphExecuteIfRoomIsOnScreen", "s_aiGraphResolveRoomOrPad(\"if_room_is_on_screen\", chr, pad," },
		{ "scenarioSourceAiGraphExecuteIfTargetAimingAtMe", "chrIsTargetAimingAtMe(chr)" },
		{ "scenarioSourceAiGraphExecuteIfNearMiss", "chrResetNearMiss(chr)" },
		{ "scenarioSourceAiGraphExecuteIfSeesSuspiciousItem", "s_aiGraphChrSeesSuspiciousItem(chr)" },
		{ "scenarioSourceAiGraphExecuteIfCheckFovWithTarget", "chrIsInTargetsFovX(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetInFovLeft", "chrGetAngleToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfTargetOutOfFovLeft", "chrGetAngleToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfTargetInFov(", "chrIsTargetInFov(chr" },
		{ "scenarioSourceAiGraphExecuteIfTargetOutOfFov(", "chrIsTargetInFov(chr" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToTargetLessThan", "chrGetDistanceToTarget(chr)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToTargetGreaterThan", "chrGetDistanceToTarget(chr)" },
	};
	for (const SpatialPerceptionGuardCase &entry :
			spatial_perception_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		if (std::string(entry.fn) ==
				"scenarioSourceAiGraphExecuteIfRoomIsOnScreen") {
			REQUIRE(block.find(
				        "s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
				entry.live_read);
		} else {
			REQUIRE(block.find("s_aiGraphRequireSpatialPerceptionActor(") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireSpatialPerceptionActor(",
				entry.live_read);
		}
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphChrSeesSuspiciousItem");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("roomGetProps(chr->prop->rooms") !=
		        std::string::npos);
		REQUIRE(helper.find("struct prop *prop = &g_Vars.props[*ptr];") !=
		        std::string::npos);
		REQUIRE(helper.find("chrHasLosToProp(chr, prop)") !=
		        std::string::npos);
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSeesSuspiciousItem");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr, 0, 1, &chr_count, &source_chr") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireSpatialPerceptionActor(",
			"s_aiGraphChrSeesSuspiciousItem(chr)");
		requireTokenOrder(block,
			"chr, 0, 1, &chr_count, &source_chr",
			"s_aiGraphChrSeesSuspiciousItem(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfLosToChr");
		requireTokenOrder(block,
			"s_aiGraphRequireSpatialPerceptionActor(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrHasLosToPos(basechr");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphPreparePadDistancePerceptionBranch");
		REQUIRE(!helper.empty());
		requireTokenOrder(helper,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(");
		requireTokenOrder(helper,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrResolvePadId(chr, padnum)");
	}
	struct DistancePerceptionGuardCase {
		const char *fn;
		const char *guard;
		const char *live_read;
	};
	const DistancePerceptionGuardCase distance_perception_guard_cases[] = {
		{ "scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan",
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"chrGetDistanceToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"chrGetDistanceToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrGetDistanceToChr(chr, chrnum)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrGetDistanceToChr(chr, chrnum)" },
		{ "scenarioSourceAiGraphExecuteIfAnyChrNearSelf",
			"s_aiGraphRequireDistancePerceptionActor(",
			"chrSetChrPresetToAnyChrNearSelf(chr, distance)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadLessThan",
			"s_aiGraphPreparePadDistancePerceptionBranch(",
			"chrGetDistanceFromTargetToPad(chr, resolved_pad)" },
		{ "scenarioSourceAiGraphExecuteIfDistanceFromTargetToPadGreaterThan",
			"s_aiGraphPreparePadDistancePerceptionBranch(",
			"chrGetDistanceFromTargetToPad(chr, resolved_pad)" },
	};
	for (const DistancePerceptionGuardCase &entry :
			distance_perception_guard_cases) {
		const std::string block = functionBlock(scenario_runtime, entry.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(entry.guard) != std::string::npos);
		requireTokenOrder(block, entry.guard, entry.live_read);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDistanceToPadLessThan");
		requireTokenOrder(block,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"s_aiGraphPreparePadDistancePerceptionBranch(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan");
		requireTokenOrder(block,
			"s_aiGraphRequirePadDistancePerceptionSource(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphPreparePadDistancePerceptionBranch(");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfDistanceToChrLessThan",
		     "scenarioSourceAiGraphExecuteIfDistanceToChrGreaterThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		requireTokenOrder(block,
			"s_aiGraphRequireDistancePerceptionActor(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetDistanceToChr(chr, chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveChr");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"chr->hidden |= 0x20");
		REQUIRE(block.find("applied = chr && chr->prop") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteApplyGsetDamage");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByImpact(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDamageChr");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetHeldUsableProp(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByImpact(");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfLosToChr",
		     "scenarioSourceAiGraphExecuteIfChrInOnScreenRoom",
		     "scenarioSourceAiGraphExecuteIfChrDistanceToPadGreaterThan",
		     "scenarioSourceAiGraphExecuteIfChrInRoom",
		     "scenarioSourceAiGraphExecuteChrDropItems",
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeObjectTag(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"objFindByTagId(tag_id)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("objFindByTagId(tag_id)") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrActivatedObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRef(") !=
		        std::string::npos);
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("chrnum == CHR_ANY") != std::string::npos);
		REQUIRE(block.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		REQUIRE(block.find("struct player *coop = g_Vars.coop;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.bond->prop") == std::string::npos);
		REQUIRE(block.find("g_Vars.coop->prop") == std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.coopplayernum >= 0",
			"\"if_chr_activated_object\", coop");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"chr->prop == bond->prop");
		requireTokenOrder(block,
			"\"if_chr_activated_object\", coop",
			"chr->prop == coop->prop");
	}
	REQUIRE(scenario_runtime.find("AI condition if_chr_activated_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_has_weapon_equipped chr=%d chr_rows=%d target_chr=%d player_checked=%d weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action drop_object_from_chr tag=%d object_rows=%d chr_rows=%d source_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_drop_items chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_drop_weapon chr=%d chr_rows=%d target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action give_object_to_chr tag=%d chr=%d chr_rows=%d target_chr=%d player_checked=%d object_rows=%d applied=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"objFindByTagId(tag_id)");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"invHasProp(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrHasWeaponEquipped");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveWeaponCatalogId(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"bgunGetWeaponNum(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDropObjectFromChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphRequireRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"objSetDropped(obj->prop, DROPTYPE_SURRENDER)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->hidden |= CHRHFLAG_DROPPINGITEM");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeObjectTag(",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"if (obj && obj->prop && chr && chr->prop)");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrSetTeam",
		     "scenarioSourceAiGraphExecuteDamageChrByAmount",
		     "scenarioSourceAiGraphExecuteChrKill",
		     "scenarioSourceAiGraphExecuteChrGrabObject",
		     "scenarioSourceAiGraphExecuteToggleP1P2",
		     "scenarioSourceAiGraphExecuteChrSetP1P2",
		     "scenarioSourceAiGraphExecuteChrSetCloaked",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string chr_source_guard = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeSetupCharacterSource");
		REQUIRE(!chr_source_guard.empty());
		REQUIRE(chr_source_guard.find(
			        "missing setup.fields.json or spawns.json source") !=
		        std::string::npos);

		const std::string chr_count_guard = functionBlock(scenario_runtime,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource");
		REQUIRE(!chr_count_guard.empty());
		requireTokenOrder(chr_count_guard,
			"s_aiGraphRequireRuntimeSetupCharacterSource(action)",
			"s_aiGraphTryCountRuntimeSetupChrRowsFromSource(out_chr_count)");

		const std::string chr_try_count_guard = functionBlock(scenario_runtime,
			"s_aiGraphTryCountRuntimeSetupChrRowsFromSource");
		REQUIRE(!chr_try_count_guard.empty());
		requireTokenOrder(chr_try_count_guard,
			"s_aiGraphRuntimeSetupCharacterSourceIsActive()",
			"s_ActiveScenarioGraphs.source_setup_chr_count");

		const std::string selector_block = functionBlock(scenario_runtime,
			"s_aiGraphChrIdIsRuntimeSelector");
		REQUIRE(!selector_block.empty());
		REQUIRE(selector_block.find("case CHR_SELF:") != std::string::npos);
		REQUIRE(selector_block.find("case CHR_TARGET:") != std::string::npos);
		REQUIRE(selector_block.find("case CHR_P1P2:") != std::string::npos);

		const std::string guard_block = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeCharacterRefFromBase");
		REQUIRE(!guard_block.empty());
		REQUIRE(guard_block.find("s_aiGraphChrIdIsRuntimeSelector(chr_id)") !=
		        std::string::npos);
		requireTokenOrder(guard_block,
			"s_aiGraphChrIdIsRuntimeSelector(chr_id)",
			"return 1;");
		requireTokenOrder(guard_block,
			"s_aiGraphChrIdIsRuntimeSelector(chr_id)",
			"s_aiGraphSetupSourceContainsChrnum(chr->chrnum)");
		requireTokenOrder(guard_block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(action",
			"s_aiGraphSetupSourceContainsChrnum(chr->chrnum)");
	}
	REQUIRE(scenario_runtime.find("AI action chr_set_team chr=%d chr_rows=%d target_chr=%d checked_players=%d team=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action damage_chr_by_amount chr=%d chr_rows=%d target_chr=%d amount=%d mode=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_kill chr=%d chr_rows=%d target_chr=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_grab_object chr=%d chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d grabbed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action toggle_p1p2 chr=%d chr_rows=%d target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_p1p2 chr=%d chr_rows=%d source_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_cloaked chr=%d chr_rows=%d target_chr=%d cloaked=%d timer=%d applied=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteToggleP1P2");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.coopplayernum >= 0",
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->p1p2 == g_Vars.bondplayernum");
		requireTokenOrder(block,
			"\"toggle_p1p2\", coop",
			"chr->p1p2 = g_Vars.coopplayernum");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetP1P2");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"missing ai/ailists.json source",
			"g_Vars.coopplayernum >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"(s32)playernum == g_Vars.coopplayernum");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"chr1->p1p2 = g_Vars.bondplayernum");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetTeam");
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("chrnum == CHR_ANTI") != std::string::npos);
		REQUIRE(block.find("g_Vars.antiplayernum >= 0") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]") ==
		        std::string::npos);
		REQUIRE(block.find("checked_players=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiEntityLifecycleGraphReady(\"chr_set_team\"",
			"g_Vars.antiplayernum >= 0");
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(",
			"g_Vars.antiplayernum >= 0");
		requireTokenOrder(block,
			"g_Vars.antiplayernum >= 0",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"",
			"PLAYER_IS_ANTI(player)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_team\"",
			"player->prop->chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerChrPortalDistanceLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.currentplayer->prop") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer &&") ==
		        std::string::npos);
		REQUIRE(block.find("AI condition if_player_chr_portal_distance_less_than chr_rows=%d source_chr=%d player_checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"func0f0056f4(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"player->prop->rooms[0]");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDamageChrByAmount");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDamageByMisc(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrKill");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->actiontype = ACT_DEAD");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrGrabObject");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->bondmovemode") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->crouchoffset") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"player->bondmovemode");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"player->crouchoffset");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_grab_object\"",
			"bmoveGrabProp(obj->prop)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteToggleP1P2");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("struct player *bond = g_Vars.bond;") !=
		        std::string::npos);
		REQUIRE(block.find("struct player *coop = g_Vars.coop;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.coop->isdead") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.bond->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"coop->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"bond->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->p1p2 =");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerPointer(\"toggle_p1p2\"",
			"chr->p1p2 =");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetP1P2");
		REQUIRE(block.find("chrFindById(basechr") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]->isdead") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"player->isdead");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr1->p1p2 =");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_set_p1p2\"",
			"chr1->p1p2 =");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetCloaked");
		REQUIRE(block.find("chr = chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrCloak(chr, timer)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjInRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPlayerLookingAtObject") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPlayerLookingAtObject");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum)") ==
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolvePlayerChr(") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d player_checked=%d tag=%d object_rows=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.currentplayer->lookingatprop.prop") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeObjectTag(",
			"s_aiGraphResolvePlayerChr(");
		requireTokenOrder(block,
			"s_aiGraphResolvePlayerChr(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->lookingatprop.prop");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDropWeapon");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_drop_weapon\"",
			"bgunGetWeaponNum(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_drop_weapon\"",
			"invRemoveItemByNum(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGiveObjectToChr");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"give_object_to_chr\"",
			"propPickupByPlayer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"give_object_to_chr\"",
			"propExecuteTickOperation(");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfTargetIsPlayer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.gun_interaction+ai/ailists.json+objects.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_property+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_navigation+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.object_room+ai/ailists.json+objects.json+pads.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_obj_in_room tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfObjInRoom");
		REQUIRE(!block.empty());
		// B-1163: room-or-pad value, decoded and checked as native does.
		REQUIRE(block.find("s_aiGraphResolveRoomOrPad(\"if_obj_in_room\", basechr, room_id,") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePad(\"if_obj_in_room\"") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"if_obj_in_room\"",
			"resolved_room == obj->prop->rooms[0]");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception+ai/ailists.json+objects.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureEnvironment") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.environment+ai/ailists.json+scenario.ini+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDistanceToTarget2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.target_distance+ai/ailists.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfDistanceToTarget2");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition %s chr_rows=%d source_chr=%d actual=%.2f") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"chrGetDistanceToTarget2(chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSpeak") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySound") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAssignSound") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAudioMuteChannel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChannelFree") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundVolume") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundVolumeByDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjectSoundPlaying") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayRepeatingSoundFromObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySoundFromEntity") !=
	        std::string::npos);
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeCharacterRefFromBase");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("chrFindById(basechr, chr_id)") !=
		        std::string::npos);
		REQUIRE(helper.find("chr->prop->type == PROPTYPE_PLAYER") !=
		        std::string::npos);
		REQUIRE(helper.find("s_aiGraphSetupSourceContainsChrnum(chr->chrnum)") !=
		        std::string::npos);
		REQUIRE(scenario_runtime.find(
			        "return s_aiGraphRequireRuntimeCharacterRefFromBase(action,\n\t\tg_Vars.chrdata") !=
		        std::string::npos);
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlaySoundFromEntity");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(g_Vars.chrdata, entity_id)") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRef(\"play_sound_from_entity\"",
			"psModify(channel, -1, -1, prop");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjectSoundVolumeLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlaySoundFromProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePlayTemporaryPrimaryTrack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.audio+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrKill") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveWeaponFromInventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteClearInventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteReleaseObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrGrabObject") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteToggleP1P2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetP1P2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetCloaked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAutogunTargetTeam") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrBeginOrEndTeleport") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrTeleportFullWhite") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetCutsceneWeapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFadeScreen") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfFadeComplete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrHudpieceVisible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPassiveMode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetFiringInCutscene") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPortalFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_inventory+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_state+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.cutscene_presentation+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfMusicEventQueueIsEmpty") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfCoopMode") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.music_mode+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveReferencesToChr") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrSameFloorDistanceToPadLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetSameFloorDistanceToPad");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveReferencesToChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"prop_index = (s32)(chr->prop - g_Vars.props);");
		requireTokenOrder(block,
			"prop_index = (s32)(chr->prop - g_Vars.props);",
			"chrClearReferences(prop_index)");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphPreparePadDistancePerceptionBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("padnum == PAD_PRESET") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chrResolvePadId(chr, padnum)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing chr for PAD_PRESET resolution") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphLogPadDistancePerceptionBranch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition %s value=%d pad=%d found=1 pad_rows=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.pad_reference+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteOpenDoor") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfDoorState") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.door+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action open_door tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_door_locked tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteLiftGoToStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeLiftNumber") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireLiftMotionPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action activate_lift liftnum=%d pad=%d found=1 pad_rows=%d tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action lift_go_to_stop tag=%d object_rows=%d stop=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lift+objects.json+pads.json") !=
	        std::string::npos);
	{
		const std::string lift_number = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeLiftNumber");
		REQUIRE(!lift_number.empty());
		REQUIRE(lift_number.find("ARRAYCOUNT(g_Lifts)") !=
		        std::string::npos);
		requireTokenOrder(lift_number,
			"s_aiGraphRequireRuntimePadTable(action, &pad_count)",
			"ARRAYCOUNT(g_Lifts)");
		requireTokenOrder(lift_number,
			"ARRAYCOUNT(g_Lifts)",
			"padUnpack(i, PADFIELD_LIFT, &pad)");
		requireTokenOrder(lift_number,
			"padUnpack(i, PADFIELD_LIFT, &pad)",
			"pad.liftnum == liftnum");
	}
	{
		const std::string activate_lift = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteActivateLift");
		REQUIRE(!activate_lift.empty());
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireLiftNode(\"activate_lift\"",
			"s_aiGraphRequireRuntimeLiftNumber(\"activate_lift\"");
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireRuntimeLiftNumber(\"activate_lift\"",
			"s_aiGraphRequireRuntimeObjectTagType(\"activate_lift\"");
		requireTokenOrder(activate_lift,
			"s_aiGraphRequireRuntimeObjectTagType(\"activate_lift\"",
			"liftActivate(obj->prop, (u8)liftnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfUsingLift");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action if_using_lift pad_rows=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"if_using_lift\"",
			"chr && chr->prop && chrIsUsingLift(chr)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConfigureRain") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.weather+ai/ailists.json+scenario.ini") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSwitchToAltSky") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetWindSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.sky+ai/ailists.json") !=
	        std::string::npos);
	{
		const std::string sky_helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireSkyNode");
		REQUIRE(!sky_helper.empty());
		REQUIRE(sky_helper.find("missing ai/ailists.json source") !=
		        std::string::npos);
		const std::string set_wind_speed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetWindSpeed");
		REQUIRE(!set_wind_speed.empty());
		REQUIRE(set_wind_speed.find(
			        "AI action set_wind_speed speed=%d value=%.3f") !=
		        std::string::npos);
		requireTokenOrder(set_wind_speed,
			"s_aiGraphRequireSkyNode(\"set_wind_speed\"",
			"g_SkyWindSpeed = 0.1f * speed");
		requireTokenOrder(set_wind_speed,
			"g_SkyWindSpeed = 0.1f * speed",
			"AI action set_wind_speed speed=%d value=%.3f");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetLights") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphResolveRoomOrPad(\"set_lights\", chr, padnum, &roomnum,") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_lights pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.lighting+ai/ailists.json+pads.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetLights");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_lights pad=%d found=1 pad_rows=%d chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		// B-1162: background scripts (a blank chr with no prop) change
		// lights too: the room no longer requires chr->prop.
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"set_lights\"",
			"s_aiGraphResolveRoomOrPad(\"set_lights\"");
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"set_lights\"",
			"roomSetLightsOn(roomnum, false)");
		REQUIRE(block.find("chr->prop ? chrGetPadRoom") == std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePad(\"set_lights\"") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer");
		REQUIRE(!block.empty());
		REQUIRE(block.find("*out_chrnum = chr && chr->prop ? chr->chrnum : -1") !=
		        std::string::npos);
		requireTokenOrder(block,
			"if (!chr || !chr->prop)",
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_flags+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeSceneRooms");
		REQUIRE(!block.empty());
		REQUIRE(block.find("if (!g_Rooms || g_Vars.roomcount <= 1)") !=
		        std::string::npos);
		REQUIRE(block.find("*out_room_count = g_Vars.roomcount;") !=
		        std::string::npos);
		REQUIRE(block.find("missing source-built scene room table") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetRoomFlag");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_room_flag room=%d room_rows=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"set_room_flag\"",
			"if (room_available)");
		requireTokenOrder(block,
			"if (room_available)",
			"g_Rooms[roomnum].flags |= flag");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"s_aiGraphResolveRuntimeSceneRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find("if (roomnum > 0 && roomnum < g_Vars.roomcount)") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeFailure(action") ==
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteConfigureEnvironment");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action configure_environment room=%d room_rows=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"if (room_available)");
		requireTokenOrder(block,
			"if (room_available)",
			"g_Rooms[roomnum].flags &= ~ROOMFLAG_PLAYAMBIENTTRACK");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].flags &= ~ROOMFLAG_OUTDOORS");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].unk4e_04 = value");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"g_Rooms[roomnum].unk4d = value");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeSceneRooms(\"configure_environment\"",
			"for (i = 1; i < g_Vars.roomcount; i++)");
		requireTokenOrder(block,
			"s_aiGraphResolveRuntimeSceneRoom(\"configure_environment\"",
			"roomSetLightsFaulty(roomnum, value)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteShowCutsceneChrs");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action show_cutscene_chrs show=%d chr_rows=%d checked_chrs=%d applied=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].hidden2 &=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].hidden2 |=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].chrflags &=") ==
		        std::string::npos);
		REQUIRE(block.find("g_ChrSlots[i].chrflags |=") ==
		        std::string::npos);
		REQUIRE(block.find("struct chrdata *checked_chr_slots[slot_count];") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphCountRuntimeSetupChrRowsFromSource(",
			"slot_count = chrsGetNumSlots();");
		requireTokenOrder(block,
			"slot_count = chrsGetNumSlots();",
			"chr = &g_ChrSlots[i];");
		requireTokenOrder(block,
			"\"show_cutscene_chrs\", chr",
			"checked_chr_slots[checked_chrs++] = chr");
		requireTokenOrder(block,
			"chr = checked_chr_slots[i];",
			"chr->hidden2 &=");
		requireTokenOrder(block,
			"chr = checked_chr_slots[i];",
			"chr->hidden2 |= CHRH2FLAG_HIDDENFORCUTSCENE");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrToggleModelPart") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteObjSetModelPartVisible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.model_part+ai/ailists.json+objects.json") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrToggleModelPart");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrToggleModelPart");
	}
	REQUIRE(scenario_runtime.find("AI action obj_set_model_part_visible tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfObjHealthLessThan") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_health+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_obj_health tag=%d object_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrSpecialDeathAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.special_death+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRoomToSearch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.room_search+ai/ailists.json+scene.glb") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_room_to_search chr_rows=%d chr=%d target_chr=%d target_selector=%d room=%d") !=
	        std::string::npos);
	{
		const std::string set_room_to_search =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetRoomToSearch");
		REQUIRE(!set_room_to_search.empty());
		REQUIRE(set_room_to_search.find(
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_room_to_search\"") !=
		        std::string::npos);
		REQUIRE(set_room_to_search.find(
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"") !=
		        std::string::npos);
		requireTokenOrder(set_room_to_search,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_room_to_search\"",
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"");
		requireTokenOrder(set_room_to_search,
			"s_aiGraphResolveRuntimeCharacterRefOrSelector(\"set_room_to_search\"",
			"chr->roomtosearch = room");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_savefile_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_savefile_flag_set") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_savefile_flag_unset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.restart_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.reset_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.pause_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.resume_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_greater_than_random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.hide_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.start_countdown_timer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_stopped") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_countdown_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRestartTimer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetCountdownTimerValue") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCharacterStatePointer") !=
	        std::string::npos);
	struct TimerCharacterPointerPin {
		const char *fn;
		const char *guard;
		const char *use;
	};
	const TimerCharacterPointerPin timer_character_pointer_pins[] = {
		     {
			     "scenarioSourceAiGraphExecuteRestartTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"restart_timer\"",
			     "chrRestartTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteResetTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"reset_timer\"",
			     "chr->timer60 = 0",
		     },
		     {
			     "scenarioSourceAiGraphExecutePauseTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"pause_timer\"",
			     "chr->hidden &= ~CHRHFLAG_TIMER_RUNNING",
		     },
		     {
			     "scenarioSourceAiGraphExecuteResumeTimer",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"resume_timer\"",
			     "chr->hidden |= CHRHFLAG_TIMER_RUNNING",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerStopped",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(\"if_timer_stopped\"",
			     "result = (chr->hidden & CHRHFLAG_TIMER_RUNNING) == 0",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThanRandom",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "timer = chrGetTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerLessThan",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "chr_timer = chrGetTimer(chr)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThan(",
			     "s_aiGraphRequireRuntimeCharacterStatePointer(",
			     "chr_timer = chrGetTimer(chr)",
		     },
	     };
	for (const TimerCharacterPointerPin &pin :
			timer_character_pointer_pins) {
		const std::string block = functionBlock(scenario_runtime, pin.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(pin.guard) != std::string::npos);
		requireTokenOrder(block, pin.guard, pin.use);
	}
	struct TimerVehiclePointerPin {
		const char *fn;
		const char *guard_arg;
		const char *use;
	};
	const TimerVehiclePointerPin timer_vehicle_pointer_pins[] = {
		     {
			     "scenarioSourceAiGraphExecuteRestartTimer",
			     "\"restart_timer\", &hovercar->base",
			     "chopperRestartTimer(hovercar)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerLessThan",
			     "\"if_timer_less_than\", &hovercar->base",
			     "chopperGetTimer(hovercar)",
		     },
		     {
			     "scenarioSourceAiGraphExecuteIfTimerGreaterThan(",
			     "\"if_timer_greater_than\", &hovercar->base",
			     "chopperGetTimer(hovercar)",
		     },
	     };
	for (const TimerVehiclePointerPin &pin : timer_vehicle_pointer_pins) {
		const std::string block = functionBlock(scenario_runtime, pin.fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find(pin.guard_arg) != std::string::npos);
		REQUIRE(block.find("OBJTYPE_CHOPPER, OBJTYPE_HOVERCAR") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(", pin.use);
	}
	REQUIRE(scenario_runtime.find("AI action restart_timer target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action reset_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action pause_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action resume_timer chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_stopped chr_rows=%d target_chr=%d result=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_greater_than_random chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_less_than value=%f result=%d target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action if_timer_greater_than value=%f result=%d target=%s chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg_middle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_hudmsg_top_middle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.hovercar_begin_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_vehicle_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_rotor_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_explosions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_object_distance_to_pad_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.misc_effect+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quip_shuffle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteHovercarBeginPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphFindRuntimePath(\"hovercar_begin_path\"") !=
	        std::string::npos);
	{
		const std::string find_runtime_path =
			functionBlock(scenario_runtime, "s_aiGraphFindRuntimePath");
		REQUIRE(!find_runtime_path.empty());
		requireTokenOrder(find_runtime_path,
			"missing navigation/paths.json source",
			"s_countRuntimePaths()");
		requireTokenOrder(find_runtime_path,
			"missing navigation/paths.json source", "pathFindById");
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePathPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePathPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime path pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation path id %d has no source pad sequence") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing pads.json source for navigation path pads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action hovercar_begin_path path=%d found=1 vehicle_rows=%d truck_type=%d hovercar_type=%d path_rows=%d source=%s") !=
	        std::string::npos);
	{
		const std::string hovercar_begin_path = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteHovercarBeginPath");
		REQUIRE(!hovercar_begin_path.empty());
		REQUIRE(hovercar_begin_path.find("g_Vars.truck->") ==
		        std::string::npos);
		REQUIRE(hovercar_begin_path.find("g_Vars.hovercar->") ==
		        std::string::npos);
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphFindRuntimePath(\"hovercar_begin_path\"",
			"s_aiGraphRequireRuntimePathPads(\"hovercar_begin_path\"");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimePathPads(\"hovercar_begin_path\"",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"truck->path = path");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperFromHovercar(hovercar)");
		requireTokenOrder(hovercar_begin_path,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"hovercar->path = path");
	}
	REQUIRE(scenario_runtime.find("AI condition if_hoverbot_next_step vehicle_rows=%d source_vehicle_type=%d comparison=%d value=%d path=%d path_rows=%d path_pads=%d pad_rows=%d nextstep=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.vehicle_investigation+ai/ailists.json+objects.json+navigation/paths.json+pads.json") !=
	        std::string::npos);
	{
		const std::string hoverbot_next_step = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteIfHoverbotNextStep");
		REQUIRE(!hoverbot_next_step.empty());
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"s_aiGraphRequireRuntimePathPointer(\"if_hoverbot_next_step\"");
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimePathPointer(\"if_hoverbot_next_step\"",
			"s_aiGraphRequireRuntimePathPads(");
		requireTokenOrder(hoverbot_next_step,
			"s_aiGraphRequireRuntimePathPads(",
			"hovercar->nextstep < 0");
		requireTokenOrder(hoverbot_next_step,
			"hovercar->nextstep < 0",
			"branch_taken = hovercar &&");
	}
	{
		const std::string path_pointer =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimePathPointer");
		REQUIRE(!path_pointer.empty());
		requireTokenOrder(path_pointer,
			"missing navigation/paths.json source",
			"s_countRuntimePaths()");
		REQUIRE(path_pointer.find("if (&g_StageSetup.paths[i] == path)") !=
		        std::string::npos);
	}
	{
		const std::string heli_armed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHeliWeaponsArmed");
		REQUIRE(!heli_armed.empty());
		requireTokenOrder(heli_armed,
			"s_aiGraphRequireRuntimeChopperPointer(\"if_heli_weapons_armed\"",
			"branch_taken = hovercar && hovercar->weaponsarmed");
	}
	{
		const std::string heli_set_armed = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteHeliSetWeaponsArmed");
		REQUIRE(!heli_set_armed.empty());
		requireTokenOrder(heli_set_armed,
			"s_aiGraphRequireRuntimeChopperPointer(name, hovercar",
			"chopperSetArmed(hovercar, armed)");
	}
	{
		const std::string chopper_pointer = functionBlock(scenario_runtime,
			"s_aiGraphRequireRuntimeChopperPointer");
		REQUIRE(!chopper_pointer.empty());
		requireTokenOrder(chopper_pointer,
			"missing objects.json source for vehicle weapon state",
			"s_countRuntimeSetupObjectRows()");

		const size_t vehicle_pointer_pos = scenario_runtime.rfind(
			"static s32 s_aiGraphRequireRuntimeVehicleObjectPointer");
		REQUIRE(vehicle_pointer_pos != std::string::npos);
		const std::string vehicle_pointer = functionBlock(
			scenario_runtime.substr(vehicle_pointer_pos),
			"s_aiGraphRequireRuntimeVehicleObjectPointer");
		REQUIRE(!vehicle_pointer.empty());
		requireTokenOrder(vehicle_pointer,
			"missing objects.json source for vehicle state",
			"s_countRuntimeSetupObjectRows()");
	}
	REQUIRE(scenario_runtime.find("AI condition if_object_distance_to_pad_less_than tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\"set_pad_preset_to_investigation_terminal\",\n\t\t\t\t\t\tselected_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_investigation_terminal tag=%d object_rows=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetVehicleSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRotorSpeed") !=
	        std::string::npos);
	{
		const std::string set_vehicle_speed = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteSetVehicleSpeed");
		REQUIRE(!set_vehicle_speed.empty());
		REQUIRE(set_vehicle_speed.find("g_Vars.truck->") ==
		        std::string::npos);
		REQUIRE(set_vehicle_speed.find("g_Vars.hovercar->") ==
		        std::string::npos);
		requireTokenOrder(set_vehicle_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"truck->speedaim = speedaim");
		requireTokenOrder(set_vehicle_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"hovercar->speedaim = speedaim");
	}
	{
		const std::string set_rotor_speed = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecuteSetRotorSpeed");
		REQUIRE(!set_rotor_speed.empty());
		REQUIRE(set_rotor_speed.find("g_Vars.heli->") ==
		        std::string::npos);
		requireTokenOrder(set_rotor_speed,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"heli->rotoryspeedaim = speedaim");
	}
	REQUIRE(scenario_runtime.find("chopperFromHovercar(hovercar)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("PATHFLAG_INUSE") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("OBJFLAG_CHOPPER_INIT") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsg") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsgMiddle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteShowHudmsgTopMiddle") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_action") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_team_orders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.retreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover_within_dist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.find_cover_outside_dist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.go_to_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.check_cover_out_of_sight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.orbit_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_squadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.face_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.danger_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.release_cover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.rebuild_teams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.rebuild_squadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_listening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_dangerous_object_nearby") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.shuffle_investigation_terminals") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteHeliSetWeaponsArmed") !=
	        std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_safety2_less_than",
		     "scenario.ai.condition.if_player_using_cmp_or_ar34",
		     "scenario.ai.condition.detect_enemy_on_same_floor",
		     "scenario.ai.condition.detect_enemy",
		     "scenario.ai.condition.if_safety_less_than",
		     "scenario.ai.condition.if_target_moving_slowly",
		     "scenario.ai.condition.if_target_moving_closer",
		     "scenario.ai.condition.if_target_moving_away",
		     "scenario.ai.condition.if_squadron_is_dead",
		     "scenario.ai.condition.if_true",
		     "scenario.ai.condition.if_num_chrs_in_squadron_greater_than",
		     "scenario.ai.condition.if_natural_anim",
		     "scenario.ai.condition.if_y",
		     "scenario.ai.condition.if_sound_timer",
		     "scenario.ai.condition.if_target_y_difference_less_than",
	     }) {
		REQUIRE(scenario_runtime.find(kind) != std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNaturalAnim");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_natural_anim chr_rows=%d source_chr=%d anim_id=%s anim_source=%s clip_bytes=%u") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_natural_anim\"",
			"chr && chr->naturalanim >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_natural_anim\"",
			"chr && chr->naturalanim == anim");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSoundTimer");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_sound_timer chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"if_sound_timer\"",
			"chr->soundtimer > ticks_value");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetYDifferenceLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_target_y_difference_less_than chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"if_target_y_difference_less_than\"",
			"prop = chr ? chrGetTargetProp(chr) : NULL");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"if_target_y_difference_less_than\"",
			"prop->pos.y - chr->prop->pos.y");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteHovercopterFireRocket");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action hovercopter_fire_rocket vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		REQUIRE(block.find("chopperFireRocket(g_Vars.hovercar, side)") ==
		        std::string::npos);
		REQUIRE(block.find("struct chopperobj *hovercar = g_Vars.hovercar;") !=
		        std::string::npos);
		requireTokenOrder(block,
			"struct chopperobj *hovercar = g_Vars.hovercar;",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperFireRocket(hovercar, side)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePunchOrKick");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action punch_or_kick chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"punch_or_kick\"",
			"chrTryPunch(chr, reverse)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTargetToEyespyIfInSight");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action set_target_to_eyespy_if_in_sight chr_rows=%d source_chr=%d player_checked=%d target_chr_rows=%d source_target_chr=%d") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[chr->p1p2]->eyespy") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"set_target_to_eyespy_if_in_sight\"",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"eyespy = player->eyespy");
		requireTokenOrder(block,
			"targetchr, &target_chr_count",
			"propGetIndexByChrId(chr, targetchr->chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteMiniSkedarTryPounce");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action mini_skedar_try_pounce chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\n\t\t\t\"mini_skedar_try_pounce\"",
			"chrTrySkJump(chr, chr->pouncebits");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteAvoid");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action avoid chr_rows=%d source_chr=%d applied=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(\"avoid\"",
			"chrAvoid(chr)");
	}
	{
		const std::string helper = functionBlock(scenario_runtime,
			"s_aiGraphSafety2Score");
		REQUIRE(!helper.empty());
		REQUIRE(helper.find("AI condition if_safety2_less_than chr_rows=%d source_chr=%d checked=%d") ==
		        std::string::npos);
		REQUIRE(countOccurrences(helper,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(helper,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_safety2_less_than\"",
			"chrGetNumArghs(chr)");
		requireTokenOrder(helper,
			"\"if_safety2_less_than\", nearby",
			"nearby && nearby->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSafety2LessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_safety2_less_than chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition detect_enemy_on_same_floor chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeCharacterPointerLooksSourceBacked(") !=
		        std::string::npos);
		REQUIRE(block.find("stageGetIndex(g_Vars.stagenum) ==") !=
		        std::string::npos);
		REQUIRE(block.find("STAGEINDEX_MAIANSOS") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"chr->teamscandist");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"stageGetIndex(g_Vars.stagenum) ==");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
			"candidate_source_backed && candidate->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDetectEnemy(struct chrdata");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition detect_enemy chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRuntimeCharacterPointerLooksSourceBacked(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(chr",
			"chrnums = teamGetChrIds(1)");
		requireTokenOrder(block,
			"s_aiGraphRuntimeCharacterPointerLooksSourceBacked(",
			"candidate_source_backed && candidate->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSafetyLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_safety_less_than chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_safety_less_than\"",
			"chrGetNumArghs(chr)");
		requireTokenOrder(block,
			"\"if_safety_less_than\", nearby",
			"nearby && nearby->model");
	}
	{
		const std::string guard =
			readTextFile("tools/asset_native_source_guard.py");
		const char *literal_scan_functions[] = {
			"scenarioSourceAiGraphExecuteSetTeamOrders",
			"scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate",
			"scenarioSourceAiGraphExecuteIfChrNotTalking",
			"scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction",
			"s_aiGraphSafety2Score",
			"scenarioSourceAiGraphExecuteDetectEnemyOnSameFloor",
			"scenarioSourceAiGraphExecuteDetectEnemy",
			"scenarioSourceAiGraphExecuteIfSafetyLessThan",
			"scenarioSourceAiGraphExecuteIfSquadronIsDead",
			"scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan",
			"scenarioSourceAiGraphExecuteSayQuip",
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness",
		};
		for (const char *fn : literal_scan_functions) {
			INFO(fn);
			REQUIRE(guard.find(fn) != std::string::npos);
		}
		REQUIRE(scenario_runtime.find("chrFindByLiteralId(") !=
		        std::string::npos);
	}
	{
		struct LiteralScanGuardCase {
			const char *fn;
			const char *log_token;
		};
		const LiteralScanGuardCase cases[] = {
			{
				"scenarioSourceAiGraphExecuteSetTeamOrders",
				"AI action set_team_orders chr_rows=%d source_chr=%d action=%d chrs=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteIfChrNotTalking",
				"AI condition if_chr_not_talking chr=%d chr_rows=%d target_chr=%d",
			},
			{
				"s_aiGraphSafety2Score",
				"out_checked_chrs",
			},
			{
				"scenarioSourceAiGraphExecuteIfSquadronIsDead",
				"AI condition if_squadron_is_dead chr_rows=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteSayQuip",
				"AI action say_quip chr=%d chr_rows=%d target_chr=%d source_chr=%d checked=%d",
			},
			{
				"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness",
				"AI action increase_squadron_alertness chr_rows=%d source_chr=%d checked=%d",
			},
		};
		for (const LiteralScanGuardCase &entry : cases) {
			const std::string block = functionBlock(scenario_runtime,
				entry.fn);
			INFO(entry.fn);
			REQUIRE(!block.empty());
			REQUIRE(block.find(entry.log_token) != std::string::npos);
			REQUIRE(countOccurrences(block, "chrFindByLiteralId(") >= 1);
			REQUIRE(countOccurrences(block,
				        "s_aiGraphRequireRuntimeCharacterPointer(") +
				        countOccurrences(block,
					        "s_aiGraphRequireRuntimeCharacterStatePointer(") >=
			        countOccurrences(block, "chrFindByLiteralId("));
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrNotTalking");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"chr = chrFindByLiteralId(chrnum);",
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_chr_not_talking\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"if_chr_not_talking\"",
			"branch_taken = chr && chr->propsoundcount == 0;");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSayQuip");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"loopchr = chrFindByLiteralId(*chrnums);",
			"\"say_quip\", loopchr");
		requireTokenOrder(block,
			"\"say_quip\", loopchr",
			"loopchr && loopchr->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"struct chrdata *target = chrFindByLiteralId(*chrnums);",
			"target, &chr_count, NULL");
		requireTokenOrder(block,
			"target, &chr_count, NULL",
			"target &&\n\t\t\t\t\ttarget->model");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_attack_amount") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStartPatrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryStartAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteActivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDeactivateAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrCopyPadPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrPreset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrAddMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSubtractMorale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrAddAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSubtractAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIncreaseSquadronAlertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetHearDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetViewDistance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetGrenadeProbability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrNum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetMaxDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteAddHealth") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetShield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetReactionSpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetRecoverySpeed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAccuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetUnarmedDodgeRating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetAction") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetTeamOrders") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRetreat") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCoverWithinDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFindCoverOutsideDist") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteGoToCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteCheckCoverOutOfSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCoverTable") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphValidateRuntimeCoverIndex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime cover table") !=
	        std::string::npos);
	{
		const std::string cover_table =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimeCoverTable");
		REQUIRE(!cover_table.empty());
		requireTokenOrder(cover_table,
			"missing navigation/covers.json source",
			"s_countRuntimeCovers()");
	}
	REQUIRE(scenario_runtime.find("missing runtime cover id %d (cover rows=%d)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("retreat chr_rows=%d source_chr=%d speed=%d operation=%d assigned=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_rows=%d source_chr=%d criteria=0x%04x refdist=%d assigned=%d cover_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("go_to_cover chr_rows=%d source_chr=%d speed=%d cover=%d moved_cover=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("check_cover_out_of_sight chr_rows=%d source_chr=%d cover=%d out_of_sight=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("orbit_target chr_rows=%d source_chr=%d angle=%u alternate=%d speed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("set_squadron chr_rows=%d source_chr=%d squadron=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteOrbitTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetSquadron") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFaceCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDangerCover") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("face_cover chr_rows=%d source_chr=%d cover=%d faced=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("danger_cover chr_rows=%d source_chr=%d assigned=%d moved=%d cover_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("release_cover chr_rows=%d source_chr=%d cover=%d released=%d cover_rows=%d") !=
	        std::string::npos);
	{
		const std::string retreat =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteRetreat");
		REQUIRE(!retreat.empty());
		REQUIRE(retreat.find("assigned_cover = chrAssignCoverByCriteria") !=
		        std::string::npos);
		REQUIRE(retreat.find("s_aiGraphValidateRuntimeCoverIndex(\"retreat\"") !=
		        std::string::npos);
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"chrRunFromPos(chr, speed");
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"chrGetTargetProp(chr)");
		requireTokenOrder(retreat,
			"s_aiGraphRequireRuntimeCharacterPointer(\"retreat\"",
			"assigned_cover = chrAssignCoverByCriteria");
		requireTokenOrder(retreat,
			"assigned_cover = chrAssignCoverByCriteria",
			"s_aiGraphValidateRuntimeCoverIndex(\"retreat\"");
		requireTokenOrder(retreat,
			"s_aiGraphValidateRuntimeCoverIndex(\"retreat\"",
			"chrGoToCover(chr, speed)");
	}
	{
		const std::string find_cover =
			functionBlock(scenario_runtime,
				"s_aiGraphExecuteFindCoverByCriteria");
		REQUIRE(!find_cover.empty());
		REQUIRE(find_cover.find("assigned = chrAssignCoverByCriteria") !=
		        std::string::npos);
		REQUIRE(find_cover.find("s_aiGraphValidateRuntimeCoverIndex(action, assigned") !=
		        std::string::npos);
		requireTokenOrder(find_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"assigned = chrAssignCoverByCriteria");
		requireTokenOrder(find_cover,
			"assigned = chrAssignCoverByCriteria",
			"s_aiGraphValidateRuntimeCoverIndex(action, assigned");
		requireTokenOrder(find_cover,
			"s_aiGraphValidateRuntimeCoverIndex(action, assigned",
			"if (out_assigned)");
	}
	{
		const std::string go_to_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteGoToCover");
		REQUIRE(!go_to_cover.empty());
		requireTokenOrder(go_to_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"go_to_cover\"");
		requireTokenOrder(go_to_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_cover\"",
			"moved_cover = chrGoToCover(chr, speed)");
	}
	{
		const std::string check_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteCheckCoverOutOfSight");
		REQUIRE(!check_cover.empty());
		requireTokenOrder(check_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"check_cover_out_of_sight\"",
			"s_aiGraphValidateRuntimeCoverIndex(");
		requireTokenOrder(check_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"check_cover_out_of_sight\"",
			"out_of_sight = chrCheckCoverOutOfSight");
	}
	{
		const std::string orbit =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteOrbitTarget");
		REQUIRE(!orbit.empty());
		requireTokenOrder(orbit,
			"s_aiGraphRequireRuntimeCharacterPointer(\"orbit_target\"",
			"target = chrGetTargetProp(chr)");
		requireTokenOrder(orbit,
			"s_aiGraphRequireRuntimeCharacterPointer(\"orbit_target\"",
			"chr0f04c874(chr, angle");
	}
	{
		const std::string set_squadron =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetSquadron");
		REQUIRE(!set_squadron.empty());
		requireTokenOrder(set_squadron,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_squadron\"",
			"chr->squadron = (u8)squadron");
	}
	{
		const std::string face_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteFaceCover");
		REQUIRE(!face_cover.empty());
		requireTokenOrder(face_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"face_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"face_cover\"");
		requireTokenOrder(face_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"face_cover\"",
			"faced = chrFaceCover(chr)");
	}
	{
		const std::string danger =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDangerCover");
		REQUIRE(!danger.empty());
		REQUIRE(danger.find("assigned = chrAssignCoverAwayFromDanger") !=
		        std::string::npos);
		REQUIRE(danger.find("s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"") !=
		        std::string::npos);
		requireTokenOrder(danger,
			"s_aiGraphRequireRuntimeCharacterPointer(\"danger_cover\"",
			"func0f03aca0(chr");
		requireTokenOrder(danger,
			"s_aiGraphRequireRuntimeCharacterPointer(\"danger_cover\"",
			"assigned = chrAssignCoverAwayFromDanger");
		requireTokenOrder(danger,
			"assigned = chrAssignCoverAwayFromDanger",
			"s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"");
		requireTokenOrder(danger,
			"s_aiGraphValidateRuntimeCoverIndex(\"danger_cover\"",
			"chrGoToCover(chr, GOPOSFLAG_RUN)");
	}
	{
		const std::string release_cover =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteReleaseCover");
		REQUIRE(!release_cover.empty());
		requireTokenOrder(release_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"release_cover\"",
			"s_aiGraphValidateRuntimeCoverIndex(\"release_cover\"");
		requireTokenOrder(release_cover,
			"s_aiGraphRequireRuntimeCharacterPointer(\"release_cover\"",
			"if (chr->cover >= 0)");
	}
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRebuildTeams") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRebuildSquadrons") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetListening") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteUnsetSavefileFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsSet") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSavefileFlagIsUnset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetChrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrSetHiddenFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetObjFlag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_path+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.start_patrol+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphResolveOptionalRuntimePath(\"set_path\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphFindRuntimePath(\"start_patrol\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime path table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing navigation path id %d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime pad table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing runtime pad id %d (pad rows=%d)") !=
	        std::string::npos);
	REQUIRE(countOccurrences(scenario_runtime,
		        "if (!s_ActiveScenarioGraphs.pads_path[0]) {\n"
		        "\t\ts_aiGraphRuntimeFailure(action, \"missing pads.json source\");\n"
		        "\t\treturn 0;\n"
		        "\t}\n\n"
		        "\tpad_count = s_countRuntimePads();") >= 2);
	REQUIRE(scenario_runtime.find("AI action set_path chr_rows=%d source_chr=%d path=%d found=1 path_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action start_patrol chr_rows=%d source_chr=%d path=%u found=1 path_rows=%d source=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_pads=%u pad_rows=%d backend=graph.ai.action.set_path+navigation/paths.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_pads=%u pad_rows=%d backend=graph.ai.action.start_patrol+navigation/paths.json+pads.json") !=
	        std::string::npos);
	{
		const std::string set_path = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPath");
		const std::string start_patrol = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteStartPatrol");
		REQUIRE(!set_path.empty());
		REQUIRE(!start_patrol.empty());
		requireTokenOrder(set_path,
			"s_aiGraphResolveOptionalRuntimePath(\"set_path\"",
			"s_aiGraphRequireRuntimePathPads(\"set_path\"");
		requireTokenOrder(set_path,
			"s_aiGraphRequireRuntimePathPads(\"set_path\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_path\"");
		requireTokenOrder(set_path,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_path\"",
			"chrSetPath(chr, (u32)path_id)");
		requireTokenOrder(start_patrol,
			"s_aiGraphRequireRuntimeCharacterPointer(\"start_patrol\"",
			"if (chr->path < 0)");
		requireTokenOrder(start_patrol,
			"if (chr->path < 0)",
			"s_aiGraphFindRuntimePath(\"start_patrol\"");
		requireTokenOrder(start_patrol,
			"s_aiGraphFindRuntimePath(\"start_patrol\"",
			"s_aiGraphRequireRuntimePathPads(\"start_patrol\"");
		requireTokenOrder(start_patrol,
			"s_aiGraphRequireRuntimePathPads(\"start_patrol\"",
			"chrTryStartPatrol(chr)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireResolvedRuntimePad(\"try_start_alarm\", chr, pad_id,") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_set_pad_preset chr=%d chr_rows=%d target_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action try_start_alarm chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_copy_pad_preset src_chr=%d dst_chr=%d chr_rows=%d source_chr=%d target_chr=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_copy_pad_preset+chrstate+pads.json") !=
	        std::string::npos);
	{
		const std::string try_alarm = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryStartAlarm");
		const std::string go_to_pad = functionBlock(scenario_runtime,
			"s_aiGraphExecuteGoToPad");
		const std::string go_to_pad_preset = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGoToPadPreset");
		const std::string set_pad_preset = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetPadPreset(struct chrdata *chr");
		const std::string chr_set_pad_preset =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrSetPadPreset");
		const std::string copy_preset =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteChrCopyPadPreset");
		REQUIRE(!try_alarm.empty());
		REQUIRE(!go_to_pad.empty());
		REQUIRE(!go_to_pad_preset.empty());
		REQUIRE(!set_pad_preset.empty());
		REQUIRE(!chr_set_pad_preset.empty());
		REQUIRE(!copy_preset.empty());
		// B-1164: these natives resolve 9000 themselves, so the graph
		// checks the pad they will use (s_aiGraphRequireResolvedRuntimePad)
		// and still hands them the raw pad.
		requireTokenOrder(try_alarm,
			"s_aiGraphRequireResolvedRuntimePad(\"try_start_alarm\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_start_alarm\"");
		requireTokenOrder(try_alarm,
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_start_alarm\"",
			"chrTryStartAlarm(chr, pad_id)");
		// B-1119: the pad checked is the resolved one (pad 9000 is the
		// character's preset pad), still before the character pointer.
		requireTokenOrder(go_to_pad,
			"resolved_pad = pad == PAD_PRESET ? chrResolvePadId(chr, pad) : pad;",
			"s_aiGraphRequireRuntimePad(action, resolved_pad");
		requireTokenOrder(go_to_pad,
			"s_aiGraphRequireRuntimePad(action, resolved_pad",
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr");
		requireTokenOrder(go_to_pad,
			"s_aiGraphRequireRuntimeCharacterPointer(action, chr",
			"chrGoToPad(chr, pad, goposflags)");
		requireTokenOrder(go_to_pad_preset,
			"s_aiGraphRequireRuntimeCharacterPointer(\"go_to_pad_preset\"",
			"chr->padpreset1");
		requireTokenOrder(set_pad_preset,
			"s_aiGraphRequireResolvedRuntimePad(\"set_pad_preset\"",
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_pad_preset\"");
		requireTokenOrder(set_pad_preset,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_pad_preset\"",
			"chrSetPadPreset(chr, pad)");
		REQUIRE(chr_set_pad_preset.find("chrSetPadPresetByChrnum(") ==
		        std::string::npos);
		requireTokenOrder(chr_set_pad_preset,
			"s_aiGraphRequireResolvedRuntimePad(\"chr_set_pad_preset\", basechr,",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(chr_set_pad_preset,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->padpreset1 = chrResolvePadId(basechr, pad)");
		REQUIRE(copy_preset.find("missing pads.json source") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\n\t\t\t\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("chr_rows=%d source_chr=%d target_chr=%d") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		REQUIRE(copy_preset.find("s_aiGraphRequireRuntimePadTable(\"chr_copy_pad_preset\"") !=
		        std::string::npos);
		requireTokenOrder(copy_preset,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"copied_pad = chrsrc->padpreset1");
		requireTokenOrder(copy_preset,
			"copied_pad = chrsrc->padpreset1",
			"s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"");
		requireTokenOrder(copy_preset,
			"s_aiGraphRequireRuntimePad(\"chr_copy_pad_preset\"",
			"chrdst->padpreset1 = copied_pad");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_target+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset chr_rows=%d target_chr=%d chrpreset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_target chr=%d chr_rows=%d target_chr=%d chrpreset=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPreset");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"chrSetChrPreset(chr, chrpreset)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrTarget");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("chrSetChrPresetByChrnum(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrSetChrPreset(chr, chrpreset)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_morale+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_add_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.subtract_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.increase_squadron_alertness+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_hear_distance+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_view_distance+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_grenade_probability+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_num+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_max_damage+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.add_health+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_shield+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_reaction_speed+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_recovery_speed+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_accuracy+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_dodge_rating+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_unarmed_dodge_rating+ai/ailists.json") !=
	        std::string::npos);
	for (const char *proof : {
		     "AI action set_morale chr_rows=%d source_chr=%d value=%d",
		     "AI action add_morale chr_rows=%d source_chr=%d amount=%d",
		     "AI action subtract_morale chr_rows=%d source_chr=%d amount=%d",
		     "AI action set_alertness chr_rows=%d source_chr=%d value=%d",
		     "AI action add_alertness chr_rows=%d source_chr=%d amount=%d",
		     "AI action subtract_alertness chr_rows=%d source_chr=%d amount=%d",
		     "AI action set_hear_distance chr_rows=%d source_chr=%d value=%.3f",
		     "AI action set_view_distance chr_rows=%d source_chr=%d value=%d applied=%d",
		     "AI action set_grenade_probability chr_rows=%d source_chr=%d value=%d",
		     "AI action set_chr_num chr_rows=%d source_chr=%d value=%d",
		     "AI action set_max_damage chr=%d chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d",
		     "AI action add_health chr_rows=%d source_chr=%d amount=%.3f",
		     "AI action set_shield chr_rows=%d source_chr=%d value=%.3f",
		     "AI action set_reaction_speed chr_rows=%d source_chr=%d value=%d",
		     "AI action set_recovery_speed chr_rows=%d source_chr=%d value=%d",
		     "AI action set_accuracy chr_rows=%d source_chr=%d value=%d",
		     "AI action set_dodge_rating chr_rows=%d source_chr=%d mode=%d value=%d",
		     "AI action set_unarmed_dodge_rating chr_rows=%d source_chr=%d value=%d",
		     "AI action set_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action unset_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action if_has_flag chr_rows=%d source_chr=%d flags=0x%08x bank=%u",
		     "AI action chr_set_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
		     "AI action chr_unset_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
		     "AI action if_chr_has_flag chr=%d chr_rows=%d target_chr=%d flags=0x%08x bank=%u",
	     }) {
		REQUIRE(scenario_runtime.find(proof) != std::string::npos);
	}
	{
		const char *mutations[][2] = {
			{"scenarioSourceAiGraphExecuteSetMorale",
				"chr->morale = (u8)morale"},
			{"scenarioSourceAiGraphExecuteAddMorale",
				"incrementByte(&chr->morale"},
			{"scenarioSourceAiGraphExecuteSubtractMorale",
				"decrementByte(&chr->morale"},
			{"scenarioSourceAiGraphExecuteSetAlertness",
				"chr->alertness = (u8)alertness"},
			{"scenarioSourceAiGraphExecuteAddAlertness",
				"incrementByte(&chr->alertness"},
			{"scenarioSourceAiGraphExecuteSubtractAlertness",
				"decrementByte(&chr->alertness"},
			{"scenarioSourceAiGraphExecuteSetHearDistance",
				"chr->hearingscale = distance"},
			{"scenarioSourceAiGraphExecuteSetViewDistance",
				"chr->visionrange = (u8)distance"},
			{"scenarioSourceAiGraphExecuteSetGrenadeProbability",
				"chr->grenadeprob = (u8)probability"},
			{"scenarioSourceAiGraphExecuteSetChrNum",
				"chrSetChrnum(chr, chrnum)"},
			{"scenarioSourceAiGraphExecuteAddHealth",
				"chrAddHealth(chr, amount)"},
			{"scenarioSourceAiGraphExecuteSetShield",
				"chrSetShield(chr, amount)"},
			{"scenarioSourceAiGraphExecuteSetReactionSpeed",
				"chr->speedrating = (s8)speed"},
			{"scenarioSourceAiGraphExecuteSetRecoverySpeed",
				"chr->arghrating = (s8)speed"},
			{"scenarioSourceAiGraphExecuteSetAccuracy",
				"chr->accuracyrating = (s8)accuracy"},
			{"scenarioSourceAiGraphExecuteSetDodgeRating",
				"chr->dodgerating = (s8)rating"},
			{"scenarioSourceAiGraphExecuteSetUnarmedDodgeRating",
				"chr->unarmeddodgerating = (s8)rating"},
			{"scenarioSourceAiGraphExecuteSetFlag",
				"chrSetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteUnsetFlag",
				"chrUnsetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteIfHasFlag",
				"chrHasFlag(chr, flags, bank)"},
		};

		for (const auto &mutation : mutations) {
			const std::string block = functionBlock(scenario_runtime,
				mutation[0]);
			REQUIRE(!block.empty());
			requireTokenOrder(block,
				"s_aiGraphRequireActiveCharacterAction(",
				mutation[1]);
		}
	}
	{
		const char *mutations[][3] = {
			{"scenarioSourceAiGraphExecuteChrSetFlag",
				"chrSetFlagsById(", "chrSetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteChrUnsetFlag",
				"chrUnsetFlagsById(", "chrUnsetFlags(chr, flags, bank)"},
			{"scenarioSourceAiGraphExecuteIfChrHasFlag",
				"chrHasFlagById(", "chrHasFlag(chr, flags, bank)"},
		};

		for (const auto &mutation : mutations) {
			const std::string block = functionBlock(scenario_runtime,
				mutation[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find(mutation[1]) == std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireRuntimeCharacterRefFromBase(",
				mutation[2]);
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetMaxDamage");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperSetMaxDamage(hovercar, maxdamage)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_has_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_unset_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_chr_has_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_stage_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_stage_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_stage_flag_eq+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chrflag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_set_hidden_flag+ai/ailists.json") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetChrflag",
		     "scenarioSourceAiGraphExecuteUnsetChrflag",
		     "scenarioSourceAiGraphExecuteIfHasChrflag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterStatePointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->chrflags");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_obj_flag+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_obj_flag tag=%d object_rows=%d") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetObjFlag",
		     "scenarioSourceAiGraphExecuteUnsetObjFlag",
		     "scenarioSourceAiGraphExecuteIfObjHasFlag",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeObjectTag(") !=
		        std::string::npos);
		REQUIRE(block.find("objFindByTagId(tag_id)") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeObjectTag(",
			"s_aiGraphObjectFlagBank(obj, bank)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.chr_object_flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_savefile_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.unset_savefile_flag+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_set+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.if_savefile_flag_unset+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_action+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_action chr_rows=%d target_chr=%d action=%d clear_orders=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetAction");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(",
			"chr->myaction = (u8)action");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_team_orders+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_team_orders chr_rows=%d source_chr=%d action=%d chrs=%d checked=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTeamOrders");
		REQUIRE(!block.empty());
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 3);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"set_team_orders\", chr,",
			"chrnums = squadronGetChrIds(chr->squadron)");
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"target && target->model");
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"target->orders = MA_SHOOTING");
		REQUIRE(block.find("chrHasFlagById(") == std::string::npos);
		requireTokenOrder(block,
			"\"set_team_orders\", target, &chr_count, NULL",
			"chrHasFlag(target, CHRFLAG0_CAN_BACKOFF");
	}
	REQUIRE(scenario_runtime.find("AI action try_attack_amount chr_rows=%d source_chr=%d arg0=%d arg1=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryAttackAmount");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(\"try_attack_amount\"",
			"chrTryAttackAmount(chr, 512, 0, arg0, arg1)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.retreat+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_within_dist+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.find_cover_outside_dist+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.check_cover_out_of_sight+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orbit_target+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_chr_preset_to_unalerted_teammate+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_unalerted_teammate chr_rows=%d source_chr=%d checked=%d candidate=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToUnalertedTeammate");
		REQUIRE(!block.empty());
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = teamGetChrIds(chr->team)");
		requireTokenOrder(block,
			"\"set_chr_preset_to_unalerted_teammate\",\n\t\t\t\t\t\ttarget",
			"!target || !target->model");
		requireTokenOrder(block,
			"\"set_chr_preset_to_unalerted_teammate\",\n\t\t\t\t\t\ttarget",
			"chrSetChrPreset(chr, candidate_chrnum)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfOrders");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_orders chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_orders\"",
			"chr && chr->prop && chr->orders == order");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_orders\"",
			"chr && chr->prop ? chr->orders : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHasOrders");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_has_orders chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_has_orders\"",
			"chr && chr->prop && chr->orders");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_has_orders\"",
			"chr && chr->prop ? chr->orders : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNotListening");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_not_listening chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_not_listening\"",
			"chr && chr->prop && chr->listening == 0");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_not_listening\"",
			"chr && chr->prop ? chr->listening : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfAction");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_action chr_rows=%d source_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_action\"",
			"chr && chr->prop && chr->myaction == action");
		requireTokenOrder(block,
			"s_aiGraphRequireCharacterConditionActor(\"if_action\"",
			"chr && chr->prop ? chr->myaction : -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_chr_in_squadron_doing_action chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = squadronGetChrIds(chr->squadron)");
		requireTokenOrder(block,
			"\"if_chr_in_squadron_doing_action\",\n\t\t\t\t\t\t\tother",
			"other && other->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSquadronIsDead");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_squadron_is_dead chr_rows=%d checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr && chr->model");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfNumChrsInSquadronGreaterThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI condition if_num_chrs_in_squadron_greater_than chr_rows=%d checked=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr && chr->prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIncreaseSquadronAlertness");
		REQUIRE(!block.empty());
		REQUIRE(block.find("AI action increase_squadron_alertness chr_rows=%d source_chr=%d checked=%d") !=
		        std::string::npos);
		REQUIRE(countOccurrences(block,
			"s_aiGraphRequireRuntimeCharacterPointer(") >= 2);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrnums = teamGetChrIds(chr->team)");
		requireTokenOrder(block,
			"\"increase_squadron_alertness\",\n\t\t\t\t\t\ttarget",
			"target->model");
		requireTokenOrder(block,
			"\"increase_squadron_alertness\",\n\t\t\t\t\t\ttarget",
			"incrementByte(&target->alertness");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.set_squadron+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.face_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.danger_cover+navigation/covers.json") !=
	        std::string::npos);
}
