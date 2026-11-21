/*
 * Static pins for c3842 and c3844, split out of
 * tests/test_asset_native_source_contract.cpp for build time (T-TOOLING-018):
 * Settings Debug family forcing, runtime load surfaces and the universal archive walkers.
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

}

TEST_CASE("Settings Debug can force one asset family to public file source",
          "[modding][pdxxx][c3842][debug][source_gate][static]") {
	const std::string header = readTextFile("port/include/asset_source_debug.h");
	const std::string debug = readTextFile("port/src/asset_source_debug.c");
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string load_h = readTextFile("port/include/assetcatalog_load.h");
	const std::string mainmenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const std::string romdata = readTextFile("port/src/romdata.c");
	const std::string mod = readTextFile("port/src/mod.c");
	const std::string snd = readTextFile("src/lib/snd.c");
	const std::string anim_c = readTextFile("src/lib/anim.c");
	const std::string propsnd = readTextFile("src/game/propsnd.c");
	const std::string main_c = readTextFile("port/src/main.c");
	const std::string pdmain_runtime = readTextFile("port/src/pdmain.c");
	const std::string texreset_c = readTextFile("src/game/texreset.c");
	const std::string pdgui_theme = readTextFile("port/fast3d/pdgui_theme.cpp");
	const std::string loader_ui = readTextFile("port/src/loader_walker_ui.c");
	const std::string smoke_run = readTextFile("tools/smoke-verify/run.ps1");
	const std::string all_family_smoke =
		readTextFile("tools/smoke-verify/tests/all_family_source_gate_smoke.json");
	const std::string audio_live_smoke =
		readTextFile("tools/smoke-verify/tests/audio_live_playback_source_smoke.json");
	const std::string all_family_matrix =
		readTextFile("tools/smoke-verify/run-all-family-source-matrix.ps1");

	REQUIRE(header.find("assetSourceDebugOnlyType") != std::string::npos);
	REQUIRE(header.find("assetSourceDebugEntryRequiresPublicFileSource") !=
	        std::string::npos);
	REQUIRE(debug.find("Debug.AssetSourceOnlyType") != std::string::npos);
	REQUIRE(debug.find("configRegisterInt(\"Debug.AssetSourceOnlyType\"") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(entry->source.primary)") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(entry->source.override)") !=
	        std::string::npos);

	REQUIRE(load_h.find("source_only_blocked") != std::string::npos);
	REQUIRE(load_h.find("catalogResolveMusicSequence") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(e)") !=
	        std::string::npos);
	REQUIRE(load.find("r->source_only_blocked = 1") != std::string::npos);
	REQUIRE(load.find("assetSourceDebugEntryRequiresPublicFileSource(entry)") !=
	        std::string::npos);
	REQUIRE(load.find("CatalogResolveResult catalogResolveMusicSequence") !=
	        std::string::npos);
	REQUIRE(load.find("modSequenceVirtualTrackId(tracknum)") !=
	        std::string::npos);
	REQUIRE(load.find("strcmp(e->id, virtual_id)") != std::string::npos);
	REQUIRE(load.find("e->ext.audio.category != AUDIO_CAT_MUSIC") !=
	        std::string::npos);
	REQUIRE(load.find("e->ext.audio.sound_id != tracknum") !=
	        std::string::npos);
	REQUIRE(load.find("r.path = entryGetFilePath(entry)") != std::string::npos);
	REQUIRE(load.find("r.is_mod_override = 1") != std::string::npos);
	REQUIRE(load.find("s_catalogApplySourceOnlyDebug(&r, entry)") !=
	        std::string::npos);
	REQUIRE(load.find("refusing fallback") != std::string::npos);

	REQUIRE(romdata.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(romdata.find("ASSET.SOURCE_ONLY: file") != std::string::npos);
	REQUIRE(romdata.find("ROM/static fallback.") != std::string::npos);
	const std::string mod_sequence_load = functionBlock(mod, "void *modSequenceLoad");
	const std::string mplayer = readTextFile("src/game/mplayer/mplayer.c");
	const std::string audio = readTextFile("port/src/audio.c");
	REQUIRE(mod.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(mod.find("catalogResolveMusicSequence((s32)num)") !=
	        std::string::npos);
	{
		const std::string anim_load_data =
			functionBlock(mod, "void *modAnimationLoadData");
		const std::string anim_try_override =
			functionBlock(mod, "void *modAnimationTryCatalogOverride");
		const std::string anim_load_frame =
			functionBlock(anim_c, "u8 animLoadFrame");
		const std::string anim_load_header =
			functionBlock(anim_c, "void animLoadHeader");
		REQUIRE(mod.find("modAnimationFatalPublicSourceFailure") !=
		        std::string::npos);
		REQUIRE(mod.find("ASSET.SOURCE_ONLY: animation %d maps to public animation source") !=
		        std::string::npos);
		REQUIRE(mod.find("refusing ROM/static fallback") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("catalogResolveAnim((s32)num)") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("r.source_only_blocked") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
		        std::string::npos);
		REQUIRE(anim_load_data.find("runtime clip compilation failed") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("the selected public source is not editable GLTF/GLB animation source") !=
		        std::string::npos);
		REQUIRE(anim_load_data.find("fsFileLoad(r.path") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("catalogResolveAnim((s32)num)") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("catalogGetAnimOverride") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("r.source_only_blocked") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("assetSourceDebugIsEnabledFor(ASSET_ANIMATION)") ==
		        std::string::npos);
		REQUIRE(anim_try_override.find("runtime clip compilation failed") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("the selected public source is not editable GLTF/GLB animation source") !=
		        std::string::npos);
		REQUIRE(anim_try_override.find("fsFileLoad(path") ==
		        std::string::npos);
		REQUIRE(anim_load_frame.find("modAnimationTryCatalogOverride(animnum)") <
		        anim_load_frame.find("animDma(&g_AnimFrameByteSlots"));
		REQUIRE(anim_load_header.find("modAnimationTryCatalogOverride(animnum)") <
		        anim_load_header.find("animDma(&g_AnimHeaderByteSlots"));
	}
	REQUIRE(mod.find("#define MOD_SEQUENCE_VIRTUAL_BASE 0x4000") !=
	        std::string::npos);
	REQUIRE(mod.find("s_ModSequenceVirtualIds[MOD_SEQUENCE_VIRTUAL_SLOTS]") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceVirtualTrackForCatalogId") !=
	        std::string::npos);
	REQUIRE(mod.find("entry->ext.audio.category != AUDIO_CAT_MUSIC") !=
	        std::string::npos);
	REQUIRE(mod.find("entry->ext.audio.file_path[0]") !=
	        std::string::npos);
	REQUIRE(mod.find("private sequence slot") != std::string::npos);
	REQUIRE(mplayer.find("modSequenceVirtualTrackForCatalogId(modId)") !=
	        std::string::npos);
	REQUIRE(mplayer.find("return virtual_track") != std::string::npos);
	REQUIRE(audio.find("modSequenceVirtualTrackForCatalogId(track_id)") !=
	        std::string::npos);
	REQUIRE(audio.find("musicStartTemporaryPrimary(virtual_track)") !=
	        std::string::npos);
	REQUIRE(audio.find("sequence-backed sync is a start-only handoff") !=
	        std::string::npos);
	REQUIRE(audio.find("sequence track-change") != std::string::npos);
	REQUIRE(mod.find("modSequencePlayAudioSource(u16 num)") !=
	        std::string::npos);
	REQUIRE(main_c.find("track = modSequenceVirtualTrackForCatalogId(asset_id)") !=
	        std::string::npos);
	{
		const std::string song_probe = functionBlock(main_c,
			"static s32 bootDebugPlayCatalogSong");
		REQUIRE(song_probe.find("n_alCSPGetState(seq->seqp) != AL_STOPPED") != std::string::npos);
		REQUIRE(song_probe.find("seqPlay(seq, track)") < song_probe.find("playback=native_sequence"));
		REQUIRE(song_probe.find("modSequenceLoad(") == std::string::npos);
		REQUIRE(song_probe.find("state=registered") == std::string::npos);
	}
	REQUIRE(mod.find("modSequencePathHasAudioExtension(r.path)") !=
	        std::string::npos);
	REQUIRE(mod.find("modMusicPlay(r.path)") != std::string::npos);
	REQUIRE(mod.find("modMusicIsPlaying()") != std::string::npos);
	REQUIRE(mod.find("modSequenceCompilePublicSource") != std::string::npos);
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.mid\"") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.json\"") !=
	        std::string::npos);
	REQUIRE(mod.find("fsFileSize(mid_path) <= 0") != std::string::npos);
	REQUIRE(mod.find("modSequenceLoadEventsJson") != std::string::npos);
	REQUIRE(mod.find("modSequenceBuildAlcBuffer") != std::string::npos);
	REQUIRE(mod.find("modSequencePutBe32(data + 64, division)") !=
	        std::string::npos);
	REQUIRE(mod.find("AL_CMIDI_LOOPSTART_CODE") != std::string::npos);
	REQUIRE(mod.find("AL_CMIDI_LOOPEND_CODE") != std::string::npos);
	REQUIRE(mod.find("modSequenceCloseOpenLoopsAtTrackEnd") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceEmitLoopEndBody(track, 0xff)") !=
	        std::string::npos);
	REQUIRE(mod.find("modSequenceCloseOpenLoopsAtTrackEnd(track)") <
	        mod.find("AL_MIDI_META_EOT"));
	REQUIRE(mod.find("streaming playback failed") != std::string::npos);
	REQUIRE(mod.find("ASSET.SOURCE_ONLY: music sequence") !=
	        std::string::npos);
	REQUIRE(mod.find("but has no public FileProvider source") !=
	        std::string::npos);
	REQUIRE(mod.find("sequencer-native ") != std::string::npos);
	REQUIRE(mod.find("public source compile failed") != std::string::npos);
	REQUIRE(mod.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(mod_sequence_load.find("catalogResolveMusicSequence((s32)num)") <
	        mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)"));
	REQUIRE(mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        mod_sequence_load.find("r.source_only_blocked"));
	REQUIRE(mod_sequence_load.find("modSequenceCompilePublicSource(&r, num, outSize)") <
	        mod_sequence_load.find("fsFileSize(MOD_SEQUENCES_DIR \"/\")"));
	REQUIRE(mod.find("modSequenceSiblingPath(r->path, \"sequence.mid\"") <
	        mod.find("fsFileSize(mid_path) <= 0"));
	REQUIRE(mod.find("fsFileSize(mid_path) <= 0") <
	        mod.find("modSequenceLoadEventsJson(json_path, tracks, &event_count)"));
	REQUIRE(snd.find("modSequencePlayAudioSource(seq->tracknum)") !=
	        std::string::npos);
	REQUIRE(snd.find("modSequencePlayAudioSource(seq->tracknum)") <
	        snd.find("modSequenceLoad(seq->tracknum, &extlen)"));
	REQUIRE(snd.find("modSequenceLoad(seq->tracknum, &extlen)") <
	        snd.find("g_SeqRomAddrs[seq->tracknum] < 0x10000"));
	REQUIRE(snd.find("seq->tracknum >= g_SeqTable->count") !=
	        std::string::npos);
	REQUIRE(snd.find("modSequenceIsVirtualTrack(seq->tracknum)") !=
	        std::string::npos);
	REQUIRE(snd.find("var8005ecf8[seq->tracknum] >= 0") !=
	        std::string::npos);
	REQUIRE(snd.find("r.source_only_blocked") != std::string::npos);
	REQUIRE(snd.find("audioStartFileSound(r.path, volume, pan") !=
	        std::string::npos);
	REQUIRE(snd.find("f32 filebasepitch = 1.0f") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_sample_pan = AL_PAN_CENTER") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_sample_volume = 127") !=
	        std::string::npos);
	REQUIRE(snd.find("u8 file_key_volume_index = 0") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_pan = entry->ext.audio.sample_pan") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_volume = entry->ext.audio.sample_volume") !=
	        std::string::npos);
	REQUIRE(snd.find("file_key_volume_index = (u8)(entry->ext.audio.key_min & 0x1f)") !=
	        std::string::npos);
	REQUIRE(snd.find("filebasepitch = alCents2Ratio(cents)") !=
	        std::string::npos);
	REQUIRE(snd.find("pitch,\n\t\t\t\t\tfilebasepitch") !=
	        std::string::npos);
	REQUIRE(snd.find("file_sample_pan,\n\t\t\t\t\tfile_sample_volume") !=
	        std::string::npos);
	REQUIRE(snd.find("file_key_volume_index") != std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.has_loop : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_start_samples : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_end_samples : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.loop_count : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.has_envelope : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.attack_time_us : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("entry ? entry->ext.audio.release_time_us : 0") !=
	        std::string::npos);
	REQUIRE(snd.find("file_fxmix_key_offset = (u8)((entry->ext.audio.key_max & 0x0f) * 8)") !=
	        std::string::npos);
	REQUIRE(snd.find("fxmix,\n\t\t\t\t\tfxbus,\n\t\t\t\t\tfile_fxmix_key_offset") !=
	        std::string::npos);
	REQUIRE(snd.find("ASSET.CHAIN: sound %d maps to public file source") !=
	        std::string::npos);
	REQUIRE(snd.find("but file playback failed; refusing native bank fallback") !=
	        std::string::npos);
	REQUIRE(snd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") ==
	        std::string::npos);
	REQUIRE(snd.find("falling back to ROM") == std::string::npos);
	const std::string snd_start_mp3 = functionBlock(snd, "void sndStartMp3(s16");
	const std::string snd_mp3_resolve = functionBlock(snd, "static s32 sndMp3ResolvePublicSource");
	const std::string snd_mp3_release = functionBlock(snd, "static void sndMp3FreeSourceBuffer(void)\n{");
	for (const char *token : {"catalogResolveFile(filenum)", "modMusicLoadAudioPcm22050(source.path, &samples, NULL)",
	        "samples >= 2 && samples % 2u == 0", "sndMp3FreeSourceBuffer()", "*out_pcm = pcm", "*out_frames = samples / 2u"})
		REQUIRE(snd_mp3_resolve.find(token) != std::string::npos);
	REQUIRE(snd_mp3_resolve.find("catalogResolveFile(filenum)") < snd_mp3_resolve.find("modMusicLoadAudioPcm22050"));
	REQUIRE(snd_mp3_resolve.find("modMusicLoadAudioPcm22050") < snd_mp3_resolve.find("sndMp3FreeSourceBuffer()"));
	REQUIRE(snd_mp3_resolve.find("sndMp3FreeSourceBuffer()") < snd_mp3_resolve.find("*out_pcm = pcm"));
	REQUIRE(snd_mp3_release.find("mp3ReleasePcm()") != std::string::npos);
	REQUIRE(snd_mp3_release.find("SDL_free(g_SndCurMp3.source_pcm)") != std::string::npos);
	REQUIRE(snd_mp3_release.find("mp3ReleasePcm()") < snd_mp3_release.find("SDL_free"));
	REQUIRE(snd_start_mp3.find("mp3PlayPcmStereo22050(g_SndCurMp3.source_pcm, g_SndCurMp3.source_frames)") != std::string::npos);
	REQUIRE(snd_start_mp3.find("sndMp3ResolvePublicSource((s32)sp20.id") < snd_start_mp3.find("mp3PlayPcmStereo22050"));
	REQUIRE(snd_start_mp3.find("mp3PlayFile(") == std::string::npos);
	REQUIRE(snd_start_mp3.find("g_AudioRussMappings[sp24.confignum].audioconfig_index") != std::string::npos);
	REQUIRE(snd_start_mp3.find("g_AudioConfigs[sp24.confignum]") == std::string::npos);
	REQUIRE(snd_start_mp3.find("config->volpercentage") < snd_start_mp3.find("sndMp3ResolvePublicSource((s32)sp20.id"));
	REQUIRE(snd_start_mp3.find("config && (config->flags & AUDIOCONFIGFLAG_RESPONDHELLO)") != std::string::npos);
	for (const char *token : {"romExtractRelPathForFilenum", "fileGetRomAddress(filenum)", "fileGetRomSize(filenum)",
	        "sndMp3LoadPublicSourceFile", "g_SndMp3SourceBytes", "assetSourceDebugIsEnabledFor(ASSET_AUDIO)"})
		REQUIRE(snd.find(token) == std::string::npos);
	REQUIRE(snd.find("ASSET.CHAIN: MP3 file") != std::string::npos);
	REQUIRE(snd.find("refusing loose extracted file or ROM playback fallback") != std::string::npos);
	const std::string mp3_duration = functionBlock(propsnd, "static s32 psMp3DurationGetPublicSource60");
	REQUIRE(mp3_duration.find("catalogResolveFile(filenum)") != std::string::npos);
	REQUIRE(mp3_duration.find("modMusicAudioSourceDuration60(source.path)") != std::string::npos);
	REQUIRE(mp3_duration.find("catalogResolveFile(filenum)") < mp3_duration.find("modMusicAudioSourceDuration60"));
	REQUIRE(propsnd.find("psMp3DurationGetPublicSource60((s32)soundnum.id)") != std::string::npos);
	REQUIRE(propsnd.find("fsFileSize(source.path)") == std::string::npos);
	REQUIRE(propsnd.find("fileGetRomSize(") == std::string::npos);
	REQUIRE(propsnd.find("romExtractRelPathForFilenum") == std::string::npos);
	REQUIRE(propsnd.find("assetSourceDebugIsEnabledFor(ASSET_AUDIO)") == std::string::npos);
	REQUIRE(propsnd.find("ASSET.CHAIN: MP3 file") != std::string::npos);
	REQUIRE(propsnd.find("ROM/static file-size fallback.") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"voice\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"song\") == 0") != std::string::npos);
	REQUIRE(main_c.find("strcmp(s, \"music\") == 0") != std::string::npos);
	REQUIRE(main_c.find("bootEnsureUiArchivesReadyAfterTextureInit") !=
	        std::string::npos);
	REQUIRE(main_c.find("written = romExtractAllPdui(0)") != std::string::npos);
	REQUIRE(main_c.find("texture_result = romExtractAllPdtexture(0)") <
	        main_c.find("written = romExtractAllPdui(0)"));
	{
		const std::string ui_ready = functionBlock(main_c,
			"s32 bootEnsureUiArchivesReadyAfterTextureInit");
		REQUIRE(!ui_ready.empty());
		requireTokenOrder(ui_ready,
			"texture_result = romExtractAllPdtexture(0)",
			"written = romExtractAllPdui(0)");
	}
	REQUIRE(pdgui_theme.find("g_TcGeneralConfigs") != std::string::npos);
	REQUIRE(pdgui_theme.find("cfg_copy = g_TcGeneralConfigs[e->tex_index]") !=
		std::string::npos);
	REQUIRE(pdgui_theme.find("s_decodeUiConfigWithTempPool") !=
		std::string::npos);
	REQUIRE(pdgui_theme.find("texInitPool(&pool") != std::string::npos);
	REQUIRE(pdgui_theme.find("extractor-only") != std::string::npos);
	REQUIRE(pdgui_theme.find("deferred to render-loop trigger") ==
		std::string::npos);
	{
		const std::string check_extract = functionBlock(pdgui_theme,
			"void pdguiThemeCheckExtract");
		REQUIRE(!check_extract.empty());
		REQUIRE(check_extract.find("bootProgressIsActive()") <
		        check_extract.find("s_checked = true;"));
		REQUIRE(check_extract.find("bootProgressIsComplete()") <
		        check_extract.find("s_checked = true;"));
		REQUIRE(check_extract.find("pdguiThemeEmitPduiZips(0)") >
		        check_extract.find("s_checked = true;"));
	}
	REQUIRE(main_c.find("loaderWalkerScanUi(data_root, &kr)") !=
	        std::string::npos);
	REQUIRE(main_c.find("pdguiThemeReloadPduiSourceTextures()") !=
	        std::string::npos);
	REQUIRE(main_c.find("BOOT: UI source archives ready after texReset") !=
	        std::string::npos);
	{
		const std::string tex_reset = functionBlock(texreset_c, "void texReset");
		REQUIRE(tex_reset.find("g_TexWords[i] = NULL;") <
		        tex_reset.find("bootEnsureUiArchivesReadyAfterTextureInit();"));
		REQUIRE(tex_reset.find("bootEnsureUiArchivesReadyAfterTextureInit();") <
		        tex_reset.find("texLoadFromDisplayList(g_TexGdl1"));
	}
	{
		const std::string reload = functionBlock(pdgui_theme,
			"void pdguiThemeReloadPduiSourceTextures");
		REQUIRE(reload.find("s_ThemeLateInitDone = false") <
		        reload.find("pdguiThemeLateInit();"));
	}
	{
		const std::string apply_ui = functionBlock(pdgui_theme,
			"static void s_applyCatalogUiAsset");
		REQUIRE(apply_ui.find("s_loadThemeTextureFromPath(entry->id, path)") !=
		        std::string::npos);
		REQUIRE(apply_ui.find("s_registerModTexture(entry->id, path)") ==
		        std::string::npos);
	}
	REQUIRE(loader_ui.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(loader_ui.find("if (!e)") < loader_ui.find("assetCatalogRegister(id, ASSET_UI)"));
	REQUIRE(loader_ui.find("catalogSetPrimaryFile(e, source_path)") !=
	        std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.texture_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.layout_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_file") != std::string::npos);
	REQUIRE(loader_ui.find("e->ext.ui.nineslice_left") != std::string::npos);
	REQUIRE(loader_ui.find("\"edgeMode\"") != std::string::npos);
	REQUIRE(main_c.find("bootArmDebugLoadCatalogAssets") != std::string::npos);
	REQUIRE(main_c.find("--debug-play-catalog-audio") != std::string::npos);
	REQUIRE(main_c.find("--debug-play-catalog-audio-source-only") !=
	        std::string::npos);
	REQUIRE(main_c.find("catalogResolveAudio(asset_id, &audio)") !=
	        std::string::npos);
	REQUIRE(main_c.find("assetSourceDebugSetOnlyType(ASSET_AUDIO)") !=
	        std::string::npos);
	REQUIRE(main_c.find("extern void sndSetSfxVolume(s32 vol)") ==
	        std::string::npos);
	REQUIRE(main_c.find("catalogLoadTypedAsset(ASSET_AUDIO, asset_id)") !=
	        std::string::npos);
	REQUIRE(main_c.find("sndStart(0, (s16)audio->sound_id") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootDebugPlayCatalogSong(kind, asset_id, &audio)") !=
	        std::string::npos);
	REQUIRE(main_c.find("sndIsPlayingPublicMp3Sound((s16)audio->sound_id)") != std::string::npos);
	REQUIRE(main_c.find("audioFileSoundOwnsHandle(state)") != std::string::npos);
	REQUIRE(main_c.find("state=registered") == std::string::npos);
	REQUIRE(main_c.find("state=unassigned") != std::string::npos);
	REQUIRE(main_c.find("static void bootExitAfterCatalogProbesIfRequested(void)") !=
	        std::string::npos);
	REQUIRE(functionBlock(main_c, "void bootCatalogProbeExitTick(void)").find(
		"bootExitAfterCatalogProbesIfRequested();") != std::string::npos);
	const std::string regular_tick = functionBlock(readTextFile("port/src/pdmain.c"),
		"void mainTick(void)");
	requireTokenOrder(regular_tick, "smokeHarnessTick();", "bootCatalogProbeExitTick();");
	REQUIRE(main_c.find("--debug-load-catalog-assets-file") !=
	        std::string::npos);
	REQUIRE(main_c.find("bootApplyDebugLoadCatalogAssetsFile") !=
	        std::string::npos);
	REQUIRE(main_c.find("g_BootDebugLoadCatalogAssetsPending = 1") !=
	        std::string::npos);
	REQUIRE(main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)") !=
	        std::string::npos);
	{
		const std::string boot_catalog = functionBlock(main_c,
			"static void bootRunCatalogWork");
		REQUIRE(boot_catalog.find("bootArmDebugLoadCatalogAssets();") !=
		        std::string::npos);
		requireTokenOrder(boot_catalog,
			"Asset Catalog: %d entries registered",
			"bootArmDebugLoadCatalogAssets();");
		REQUIRE(boot_catalog.find("bootArmDebugLoadCatalogAssets();") <
		        boot_catalog.find("romExtractAllPdmesh(0)"));
		REQUIRE(boot_catalog.find("romExtractAllPdtexture(0)") <
		        boot_catalog.find("romExtractAllPdui(0)"));
	}
	{
		const std::string cli_fast_paths = functionBlock(main_c,
			"static void bootApplyCliFastPaths");
		REQUIRE(main_c.find("static void bootApplyLaunchModdingHub(void)") !=
		        std::string::npos);
		REQUIRE(main_c.find("s32 bootLaunchModdingHubTick(void)") !=
		        std::string::npos);
		REQUIRE(main_c.find("pdguiModdingHubShowTool(0);") !=
		        std::string::npos);
		REQUIRE(main_c.find("g_BootLaunchModdingHubPending = 1;") !=
		        std::string::npos);
		REQUIRE(main_c.find("!pdguiHotswapWasActive()") !=
		        std::string::npos);
		REQUIRE(pdmain_runtime.find("bootLaunchModdingHubTick();") !=
		        std::string::npos);
		requireTokenOrder(pdmain_runtime,
			"smokeHarnessTick();",
			"bootLaunchModdingHubTick();");
		REQUIRE(main_c.find("--launch-modding-hub consumed -> production Mod Manager tool 0") !=
		        std::string::npos);
		REQUIRE(cli_fast_paths.find("bootApplyLaunchModdingHub();") !=
		        std::string::npos);
		REQUIRE(cli_fast_paths.find("bootArmDebugLoadCatalogAssets();") !=
		        std::string::npos);
		REQUIRE(cli_fast_paths.find("bootApplyDeferredDebugLoadCatalogAssets();") !=
		        std::string::npos);
		requireTokenOrder(cli_fast_paths,
			"bootArmDebugLoadCatalogAssets();",
			"bootApplyDeferredDebugLoadCatalogAssets();");
		REQUIRE(cli_fast_paths.find("bootApplyDebugLoadCatalogAssets(") ==
		        std::string::npos);
	}
	{
		const std::string anim_probe = functionBlock(main_c,
			"static void bootApplyDebugProbeAnimationSource");
		REQUIRE(anim_probe.find("catalogLoadTypedAsset(ASSET_ANIMATION, asset_id)") !=
		        std::string::npos);
		REQUIRE(anim_probe.find("result=INVALID_ANIM") != std::string::npos);
		const std::size_t anim_source_only =
			anim_probe.find("assetSourceDebugSetOnlyType(ASSET_ANIMATION)");
		const std::size_t anim_load =
			anim_probe.find("catalogLoadTypedAsset(ASSET_ANIMATION, asset_id)");
		const std::size_t anim_invalid =
			anim_probe.find("result=INVALID_ANIM");
		const std::size_t anim_exit =
			anim_probe.find("bootExitAfterCatalogProbesIfRequested()");
		REQUIRE(anim_source_only != std::string::npos);
		REQUIRE(anim_load != std::string::npos);
		REQUIRE(anim_invalid != std::string::npos);
		REQUIRE(anim_exit != std::string::npos);
		REQUIRE(anim_source_only < anim_load);
		REQUIRE(anim_load < anim_invalid);
	}
	{
		const std::string audio_probe = functionBlock(main_c,
			"static void bootApplyDebugPlayCatalogAudio(const char *arg");
		REQUIRE(audio_probe.find("bootApplyDebugPlayCatalogAudioToken") !=
		        std::string::npos);
		REQUIRE(audio_probe.find("bootExitAfterCatalogProbesIfRequested()") !=
		        std::string::npos);
		REQUIRE(audio_probe.find("bootApplyDebugPlayCatalogAudioToken") <
		        audio_probe.find("bootExitAfterCatalogProbesIfRequested()"));
	}
	{
		std::size_t deferred_impl =
			main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)\n{");
		if (deferred_impl == std::string::npos) {
			deferred_impl =
				main_c.find("s32 bootApplyDeferredDebugLoadCatalogAssets(void)\r\n{");
		}
		REQUIRE(deferred_impl != std::string::npos);
		const std::size_t defer_gate =
			main_c.find("bootDebugCatalogProbesNeedCompletedBaseEmit()",
				deferred_impl);
		const std::size_t defer_complete =
			main_c.find("!bootProgressIsComplete()", deferred_impl);
		const std::size_t defer_anim =
			main_c.find("bootDebugCatalogProbesNeedAnimationTable()",
				deferred_impl);
		const std::size_t defer_wait =
			main_c.find("return 0;", defer_gate);
		const std::size_t defer_anim_wait =
			main_c.find("return 0;", defer_anim);
		const std::size_t defer_clear =
			main_c.find("g_BootDebugLoadCatalogAssetsPending = 0;",
				deferred_impl);
		REQUIRE(defer_gate != std::string::npos);
		REQUIRE(defer_complete != std::string::npos);
		REQUIRE(defer_anim != std::string::npos);
		REQUIRE(defer_wait != std::string::npos);
		REQUIRE(defer_anim_wait != std::string::npos);
		REQUIRE(defer_clear != std::string::npos);
		REQUIRE(defer_gate < defer_wait);
		REQUIRE(defer_complete < defer_wait);
		REQUIRE(defer_anim < defer_anim_wait);
		REQUIRE(defer_anim_wait < defer_clear);
		REQUIRE(defer_wait < defer_clear);
		REQUIRE(main_c.find("bootExitAfterCatalogProbesIfRequested();",
			        defer_clear) != std::string::npos);
		const std::size_t deferred_start =
			main_c.find("\n\tg_BootDebugLoadCatalogAssetsPending = 0;");
		REQUIRE(deferred_start != std::string::npos);
		const std::size_t ensure_ui =
			main_c.find("bootEnsureUiArchivesReadyForCliSourceLoads();",
				deferred_start);
		const std::size_t load_arg =
			main_c.find("bootApplyDebugLoadCatalogAssets(",
				deferred_start);
		const std::size_t load_file =
			main_c.find("bootApplyDebugLoadCatalogAssetsFile(",
				deferred_start);
		REQUIRE(ensure_ui != std::string::npos);
		REQUIRE(load_arg != std::string::npos);
		REQUIRE(load_file != std::string::npos);
		REQUIRE(ensure_ui < load_arg);
		REQUIRE(load_arg < load_file);
	}
	{
		const std::string theme_check = functionBlock(pdgui_theme,
			"void pdguiThemeCheckExtract");
		REQUIRE(theme_check.find("pdguiThemeEmitPduiZips(0)") <
		        theme_check.find("bootApplyDeferredDebugLoadCatalogAssets()"));
	}
	{
		const std::string loaded_ui = functionBlock(pdgui_theme,
			"static void s_registerLoadedThemeTexture");
		REQUIRE(loaded_ui.find("s_findPduiEntryByCatalogId(catalog_id)") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("e->load_state = ASSET_STATE_ACTIVE") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("e->payload_kind = ASSET_PAYLOAD_RUNTIME_ACTIVE") !=
		        std::string::npos);
		REQUIRE(loaded_ui.find("\"%s::texture.tga\"") != std::string::npos);
		REQUIRE(loaded_ui.find("catalogSetPrimaryFile(e, member_path)") !=
		        std::string::npos);
	}
	REQUIRE(all_family_smoke.find("audio=base:sfx_alarm_2") != std::string::npos);
	REQUIRE(all_family_smoke.find("voice=base:voice_cover_me_aiw") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("song=base:song_sequence_a") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("id='base:voice_cover_me_aiw' result=OK") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("id='base:song_sequence_a' result=OK") !=
	        std::string::npos);
	REQUIRE(all_family_smoke.find("\"min\": 24, \"max\": 24") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-play-catalog-audio-source-only") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-exit-after-catalog-probes") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("--debug-play-catalog-audio") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("    \"--no-sound\",") ==
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("sfx=base:sfx_alarm_2,voice=base:voice_cover_me_aiw,song=base:song_sequence_a") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio request kind=sfx id='base:sfx_alarm_2' source_only=1") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=sfx id='base:sfx_alarm_2' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=voice id='base:voice_cover_me_aiw' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("CATALOG: music sequence .* -> public sequence source") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("BOOT: --debug-play-catalog-audio result kind=song id='base:song_sequence_a' result=OK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("\"BOOT: --debug-play-catalog-audio result kind=.* result=OK\", \"min\": 3, \"max\": 3") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("LOUDFAIL\\\\.FALLBACK") !=
	        std::string::npos);
	REQUIRE(audio_live_smoke.find("ASSET\\\\.FALLBACK") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("Runs exhaustive non-Scenario typed-archive source-only catalog smokes") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find(".pdscenario") == std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdsfx\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdvoice\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("\".pdsong\"") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"sfx\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"voice\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Request = \"song\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("Result = \"audio\";") != std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets-source-only") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets-file") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find(".assets.txt") != std::string::npos);
	REQUIRE(all_family_matrix.find("BugId = \"B-509\"") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("all-family-source-matrix-manifest") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("asset_list_file") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("debug_load_argument") ==
	        std::string::npos);
	REQUIRE(all_family_matrix.find("Missing debug-load request/result blocks are B-509") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("BOOT: --debug-load-catalog-assets request type=.* source_only=1") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("BOOT: --debug-load-catalog-assets result type=") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets invalid token") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("--debug-load-catalog-assets token '.*' missing type=id form") !=
	        std::string::npos);
	REQUIRE(all_family_matrix.find("RomProvider:filenum") != std::string::npos);
	REQUIRE(all_family_matrix.find("refusing fallback") != std::string::npos);
	REQUIRE(smoke_run.find("$resultLabel = if ($r.Passed)") !=
	        std::string::npos);
	REQUIRE(smoke_run.find("$tag = if ($r.Passed)") == std::string::npos);

	REQUIRE(mainmenu.find("Asset Source Gate") != std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugOnlyType()") != std::string::npos);
	REQUIRE(mainmenu.find("ImGui::Checkbox(family.label, &checked)") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("assetSourceDebugSetOnlyType(checked ? family.type : ASSET_NONE)") !=
	        std::string::npos);

	const std::string family_marker =
		"static const DebugAssetSourceFamily s_DebugAssetSourceFamilies[]";
	const std::string family_end_marker = "static void renderSettingsDebug";
	const size_t family_start = mainmenu.find(family_marker);
	REQUIRE(family_start != std::string::npos);
	const size_t family_end = mainmenu.find(family_end_marker, family_start);
	REQUIRE(family_end != std::string::npos);
	const std::string families =
		mainmenu.substr(family_start, family_end - family_start);

	size_t listed_families = 0;
	size_t listed_pos = 0;
	while ((listed_pos = families.find("{ ASSET_", listed_pos)) != std::string::npos) {
		listed_families++;
		listed_pos += 8;
	}
	REQUIRE(listed_families == 26);

	auto requireFamily = [&](const char *type, const char *label) {
		REQUIRE(families.find(type) != std::string::npos);
		REQUIRE(families.find(label) != std::string::npos);
	};

	requireFamily("ASSET_WEAPON", "Weapon (.pdweapon)");
	requireFamily("ASSET_PROJECTILE", "Projectile (.pdprojectile)");
	requireFamily("ASSET_ENTITY", "Entity (.pdentity)");
	requireFamily("ASSET_CHARACTER", "Character (.pdcharacter)");
	requireFamily("ASSET_HEAD", "Head (.pdhead)");
	requireFamily("ASSET_BODY", "Body (.pdbody)");
	requireFamily("ASSET_ARENA", "Arena (.pdarena)");
	requireFamily("ASSET_SCENARIO", "Scenario (.pdscenario)");
	requireFamily("ASSET_MISSION", "Mission (.pdmission)");
	requireFamily("ASSET_MODEL", "Mesh (.pdmesh)");
	requireFamily("ASSET_ANIMATION", "Animation (.pdanim)");
	requireFamily("ASSET_TEXTURE", "Texture (.pdtexture)");
	requireFamily("ASSET_MATERIAL", "Material (.pdmaterial)");
	requireFamily("ASSET_SKIN", "Skin (.pdskin)");
	requireFamily("ASSET_EFFECT", "Effect (.pdeffect)");
	requireFamily("ASSET_PROP", "Prop (.pdprop)");
	requireFamily("ASSET_VEHICLE", "Vehicle (.pdvehicle)");
	requireFamily("ASSET_AUDIO", "Audio (.pdsfx/.pdvoice/.pdsong)");
	requireFamily("ASSET_UI", "UI (.pdui)");
	requireFamily("ASSET_FONT", "Font (.pdfont)");
	requireFamily("ASSET_LANG", "Language (.pdlang)");
	requireFamily("ASSET_GAMEMODE", "Gamemode (.pdgamemode)");
	requireFamily("ASSET_BOT_PROFILE", "Bot Profile (.pdbotprofile)");
	requireFamily("ASSET_HUD", "HUD (.pdhud)");
	requireFamily("ASSET_THEME", "Theme (.pdtheme)");
	requireFamily("ASSET_AI_LISTS", "AI Lists (.pdai)");

	REQUIRE(families.find("ASSET_TOOL") == std::string::npos);
	REQUIRE(families.find("ASSET_MAP") == std::string::npos);
	REQUIRE(families.find("ASSET_TEXTURES") == std::string::npos);
	REQUIRE(families.find("ASSET_SFX") == std::string::npos);
	REQUIRE(families.find("ASSET_MUSIC") == std::string::npos);
	REQUIRE(families.find("ASSET_BOT_VARIANT") == std::string::npos);
}

TEST_CASE("public source families have explicit runtime load surfaces",
          "[modding][pdxxx][runtime][c3838][adapters][static]") {
	const std::string load = readTextFile("port/src/assetcatalog_load.c");
	const std::string runtime = readTextFile("port/src/asset_runtime.c");
	const std::string supports =
		functionBlock(runtime, "assetRuntimeSupportsType");
	const std::string activate =
		functionBlock(runtime, "assetRuntimeActivateCatalogEntry");

	const char *adapter_families[] = {
		"ASSET_WEAPON",
		"ASSET_PROJECTILE",
		"ASSET_ENTITY",
		"ASSET_CHARACTER",
		"ASSET_HEAD",
		"ASSET_BODY",
		"ASSET_ARENA",
		"ASSET_SCENARIO",
		"ASSET_MISSION",
		"ASSET_MATERIAL",
		"ASSET_SKIN",
		"ASSET_EFFECT",
		"ASSET_PROP",
		"ASSET_VEHICLE",
		"ASSET_UI",
		"ASSET_FONT",
		"ASSET_LANG",
		"ASSET_GAMEMODE",
		"ASSET_BOT_PROFILE",
		"ASSET_HUD",
		"ASSET_THEME",
		"ASSET_AI_LISTS",
	};

	REQUIRE(!supports.empty());
	REQUIRE(!activate.empty());
	for (const char *family : adapter_families) {
		const std::string case_label =
			std::string("case ") + family + ":";
		REQUIRE(supports.find(case_label) != std::string::npos);
		REQUIRE(activate.find(case_label) != std::string::npos);
	}

	REQUIRE(load.find("s_catalogTypeUsesModelPayload(entry->type)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryAnimationPayload(entry)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogTypeUsesAudioRuntimePayload(entry->type)") !=
	        std::string::npos);
	REQUIRE(load.find("entry->type == ASSET_TEXTURE") != std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryTexturePayload(entry, handle)") !=
	        std::string::npos);
	{
		const std::string texture_source = readTextFile("port/src/mod_texture_source.c");
		const std::string mod = readTextFile("port/src/mod.c");
		REQUIRE(texture_source.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") ==
		        std::string::npos);
        const std::string texture_runtime = readTextFile("port/src/texture_source_runtime.cpp");
        REQUIRE(texture_source.find("the selected provider is not an editable public image source") != std::string::npos);
        REQUIRE(texture_source.find("textureSourceRuntimeCatalogIndex((s32)num)") != std::string::npos);
        REQUIRE(texture_runtime.find("assetCatalogGetPoolSize()") != std::string::npos);
        REQUIRE(texture_runtime.find("entry->occupied") != std::string::npos);
        REQUIRE(texture_source.find("assetCatalogGetCount()") == std::string::npos);
        REQUIRE(texture_runtime.find("assetCatalogGetCount()") == std::string::npos);
        REQUIRE(texture_source.find("catalogEffectiveHandle(entry)") != std::string::npos);
        REQUIRE(texture_source.find("handle.provider == fileProvider() ? fileProviderPath(handle) : NULL") != std::string::npos);
		REQUIRE(mod.find("assetSourceDebugIsEnabledFor(ASSET_TEXTURE)") ==
		        std::string::npos);
		REQUIRE(mod.find("refusing legacy compressed texture fallback") !=
		        std::string::npos);
	}
}

TEST_CASE("universal extracted archive walkers bind public source members",
          "[modding][pdxxx][c3844][source][static]") {
	// This contract inspects C/C++ source, whose line endings vary by checkout.
	// Normalize CRLF only; preserve all source tokens and standalone CR bytes.
	const auto readSourceText = [](const char *path) {
		const std::string raw = readTextFile(path);
		std::string text;
		text.reserve(raw.size());
		for (size_t i = 0; i < raw.size(); ++i) {
			if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;
			text += raw[i];
		}
		return text;
	};
	const std::string common_h = readSourceText("port/include/loader_walker_common.h");
	const std::string common = readSourceText("port/src/loader_walker_common.c");
	const std::string fs = readSourceText("port/src/fs.c");
	const std::string load = readSourceText("port/src/assetcatalog_load.c");
	const std::string meta_walker = readSourceText("port/src/loader_walker_meta.c");
	const std::string provider = readSourceText("port/src/assetprovider_file.c");
	const std::string player = readSourceText("src/game/player.c");
	const std::string menu = readSourceText("src/game/menu.c");
	const std::string mainmenu = readSourceText("src/game/mainmenu.c");
	const std::string mplayer_setup = readSourceText("src/game/mplayer/setup.c");
	const std::string title = readSourceText("src/game/title.c");
	const std::string modelcatalog = readSourceText("port/src/modelcatalog.c");
	const std::string forge_runtime = readSourceText("port/src/forge/forge_runtime.c");
	const std::string setuputils = readSourceText("src/game/setuputils.c");
	const std::string body_runtime = readSourceText("src/game/body.c");
	const std::string chraction = readSourceText("src/game/chraction.c");
	const std::string modeldef = readSourceText("src/game/modeldef.c");
	const std::string setup = readSourceText("src/game/setup.c");
	const std::string guard = readSourceText("tools/asset_native_source_guard.py");
	const std::string conformance = readSourceText("tools/asset_archive_conformance.py");
	const std::string body_mgr = readSourceText("port/src/catalog_mgr_bodies.c");
	const std::string head_mgr = readSourceText("port/src/catalog_mgr_heads.c");
	const std::string scanner = readSourceText("port/src/assetcatalog_scanner.c");
	const std::string netdistrib = readSourceText("port/src/net/netdistrib.c");
	const std::string runtime = readSourceText("port/src/asset_runtime.c");

	REQUIRE(common_h.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(common_h.find("loaderWalkerMarkBaseArchiveEntry") != std::string::npos);
	REQUIRE(common.find("assetPathJoinChecked(out, out_n, archive_path, \"::\", member)") !=
	        std::string::npos);
	REQUIRE(common.find("register_mutex") != std::string::npos);
	REQUIRE(common.find("SDL_LockMutex(ctx->register_mutex)") != std::string::npos);
	REQUIRE(common.find("entry->ref_count = ASSET_REF_BUNDLED") !=
	        std::string::npos);
	REQUIRE(provider.find("s_PathMutex") != std::string::npos);
	REQUIRE(provider.find("SDL_LockMutex(s_PathMutex)") != std::string::npos);
	REQUIRE(fs.find("fsExtractNestedArchiveChain") != std::string::npos);
	REQUIRE(fs.find("sep + 2") != std::string::npos);
	REQUIRE(load.find("s_catalogTypePreloadsBundledMetadataPayload(e->type)") !=
	        std::string::npos);
	REQUIRE(load.find("type == ASSET_ANIMATION || type == ASSET_SCENARIO") !=
	        std::string::npos);
	REQUIRE(load.find("static s32 s_catalogPreloadBundledMetadataPayload(asset_entry_t *entry)") !=
	        std::string::npos);
	REQUIRE(load.find("s_catalogPreloadBundledMetadataPayload(mutable_entry)") !=
	        std::string::npos);
	REQUIRE(load.find("bundled metadata preload") <
	        load.find("return s_catalogLoadEntryMetadataPayload(entry)"));
	REQUIRE(load.find("s_catalogLoadEntryModelPayload(entry, handle)") !=
	        std::string::npos);
	const std::string modelPayloadBlock =
		functionBlock(load, "static s32 s_catalogTypeUsesModelPayload");
	const std::string metadataPayloadBlock =
		functionBlock(load, "static s32 s_catalogTypeUsesMetadataRuntimePayload");
	REQUIRE(modelPayloadBlock.find("case ASSET_PROP:") == std::string::npos);
	REQUIRE(load.find("|| type == ASSET_PROP") != std::string::npos);
	REQUIRE(load.find("s_catalogLoadEntryAudioPayload(entry, handle)") <
	        load.find("CATALOG: retain bundled"));
	REQUIRE(load.find("s_catalogLoadEntryTexturePayload(entry, handle)") <
	        load.find("CATALOG: retain bundled"));
	REQUIRE(load.find("&& e->source.primary.provider == fileProvider()") !=
	        std::string::npos);
	const std::string compiler = readSourceText("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("skipped_degenerate") != std::string::npos);
	REQUIRE(compiler.find("len2 <= 0.000001f") != std::string::npos);
	REQUIRE(compiler.find("continue;") != std::string::npos);

	const char *walkers[] = {
		"port/src/loader_walker_anim.c",
		"port/src/loader_walker_arena.c",
		"port/src/loader_walker_body.c",
		"port/src/loader_walker_font.c",
		"port/src/loader_walker_head.c",
		"port/src/loader_walker_lang.c",
		"port/src/loader_walker_mesh.c",
		"port/src/loader_walker_scenario.c",
		"port/src/loader_walker_sfx.c",
		"port/src/loader_walker_song.c",
		"port/src/loader_walker_ui.c",
		"port/src/loader_walker_voice.c",
	};
	for (const char *path : walkers) {
		const std::string walker = readSourceText(path);
		INFO(path);
		const std::string registration = functionBlock(walker, "static s32 s_register");
		REQUIRE(registration.find("(void)file_path") == std::string::npos);
		if (std::string(path) == "port/src/loader_walker_anim.c") {
			// Registration selects the public member; the second pass must compile
			// the effective catalog selection after all registrations have finished.
			REQUIRE(registration.find("loaderWalkerArchiveMemberPath(file_path, source_member,") != std::string::npos);
			REQUIRE(registration.find("catalogSetPrimaryFile(e, source_path)") != std::string::npos);
			const std::string compile = functionBlock(walker, "static s32 s_compile");
			REQUIRE(compile.find("assetCatalogIterateByType(ASSET_ANIMATION, s_copyCommandSelection, &selected)") != std::string::npos);
			requireTokenOrder(compile, "assetCatalogIterateByType", "fsFileLoad(selected.path, &command_size)");
			REQUIRE(compile.find("loaderPoolParseAnimationCatalogSourceJson(command_json, command_size,") != std::string::npos);
			REQUIRE(compile.find("selected.path, id)") != std::string::npos);
			const std::string scan = functionBlock(walker, "void loaderWalkerScanAnimations");
			requireTokenOrder(scan, "loaderWalkerScanKind(tier_dir, &desc, s_register, &registered)",
				"loaderWalkerScanKind(tier_dir, &commands, s_compile, &compiled)");
			REQUIRE(scan.find("if (!registered.envelope_failures && !registered.register_failures)") != std::string::npos);
		}
		if (std::string(path) == "port/src/loader_walker_mesh.c") {
			REQUIRE(walker.find("meshPublicSourceFields") != std::string::npos);
			REQUIRE(walker.find("loaderWalkerBindMeshSource") !=
				std::string::npos);
			REQUIRE(walker.find("catalogSetPrimaryFile") == std::string::npos);
		} else if (std::string(path) == "port/src/loader_walker_body.c" ||
		        std::string(path) == "port/src/loader_walker_head.c") {
			REQUIRE(walker.find("assetCatalogRegisterBodyHeadArchive") != std::string::npos);
			REQUIRE(scanner.find("loaderWalkerMarkBaseArchiveEntry(entry)") != std::string::npos);
			continue;
		} else if (std::string(path) == "port/src/loader_walker_sfx.c" ||
		        std::string(path) == "port/src/loader_walker_voice.c") {
			REQUIRE(walker.find("catalogAudioNativeIdentityParse") != std::string::npos);
			REQUIRE(walker.find("loaderWalkerArchiveTextMember") != std::string::npos);
			REQUIRE(walker.find("assetCatalogRegisterBaseAudioSource") != std::string::npos);
			continue;
		} else {
			REQUIRE(walker.find("loaderWalkerArchiveMemberPath") !=
				std::string::npos);
			REQUIRE(walker.find("catalogSetPrimaryFile") != std::string::npos);
		}
		REQUIRE(walker.find("loaderWalkerMarkBaseArchiveEntry") !=
		        std::string::npos);
	}

	const char *metadataExts[] = {
		".pdarena", ".pdcharacter", ".pdskin", ".pdprop", ".pdvehicle", ".pdmission",
		".pdgamemode", ".pdbotprofile", ".pdhud", ".pdeffect", ".pdmaterial",
		".pdtheme", ".pdai",
	};
	for (const char *ext : metadataExts) {
		INFO(ext);
		REQUIRE(meta_walker.find(ext) != std::string::npos);
	}
	/* Specialized public admission is the sole body/head registration owner.
	 * The later generic scan must not replace it from private metadata. */
	REQUIRE(meta_walker.find(".pdbody") == std::string::npos);
	REQUIRE(meta_walker.find(".pdhead") == std::string::npos);
	REQUIRE(meta_walker.find("desc.always_invoke = 1") != std::string::npos);
	REQUIRE(meta_walker.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(meta_walker.find("entry->type != type") != std::string::npos);
	REQUIRE(meta_walker.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(meta_walker.find("catalogSetPrimaryFile(entry, source_path)") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("loaderWalkerMarkBaseArchiveEntry(entry)") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("s_copyManifestMemberPath") != std::string::npos);
	REQUIRE(meta_walker.find("\"body_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"head_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"scenario_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"skin_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"swatches_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"material_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"texture_archive\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"prop_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"physics_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"behavior_graph\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"timeline_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("\"mission_graph_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("s_applyMissionPublicDescriptor") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("loaderWalkerArchiveTextMember(archive_path, \"mission.ini\"") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("briefing records and their language tokens are already") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("LOADER.UNIVERSAL.META: invalid public mission source") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"prop_file\", \"behavior_graph\", NULL } }") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_PROP,        \"prop\",       \"props\",       \".pdprop\",       \"prop.ini\",      { \"model_file\", \"behavior_graph\", \"prop_file\", NULL } }") ==
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"model_file\", \"behavior_graph\", \"physics_file\", NULL } }") !=
	        std::string::npos);
	REQUIRE(meta_walker.find("{ ASSET_VEHICLE,     \"vehicle\",    \"vehicles\",    \".pdvehicle\",    \"vehicle.ini\",   { \"physics_file\", \"behavior_graph\", \"model_file\", NULL } }") ==
	        std::string::npos);
	REQUIRE(meta_walker.find("\"theme_file\"") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.prop.behavior_graph") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.vehicle.behavior_graph") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.mission.scenario_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.hud.texture_file") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.material.texture_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.ui_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.audio_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.music_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.theme.effect_archive") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.mode_id") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.gamemode.requirefeature") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.difficulty") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.bot_profile.requirefeature") != std::string::npos);
	REQUIRE(meta_walker.find("s_manifestBotType") != std::string::npos);
	REQUIRE(meta_walker.find("s_manifestBotDifficulty") != std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.body.mesh_archive") == std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.body.hand_archive") == std::string::npos);
	REQUIRE(meta_walker.find("entry->ext.head.mesh_archive") == std::string::npos);
	/* Optional native metadata supplies only a checked private slot hint.
	 * Missing/out-of-range hints must never silently select native slot zero. */
	{
		const std::string body_hint = functionBlock(readSourceText("port/src/loader_walker_body.c"), "static s32 s_register");
		const std::string head_hint = functionBlock(readSourceText("port/src/loader_walker_head.c"), "static s32 s_register");
		for (const std::string *walker : {&body_hint, &head_hint}) {
			REQUIRE(walker->find("s64 hint = -1;") != std::string::npos);
			REQUIRE(walker->find("s64 hint = 0;") == std::string::npos);
			requireTokenOrder(*walker, "loaderWalkerEnvelopeInt", "assetCatalogRegisterBodyHeadArchive");
		}
		REQUIRE(body_hint.find("\"bodynum\", &hint") != std::string::npos);
		REQUIRE(head_hint.find("\"headnum\", &hint") != std::string::npos);
		REQUIRE(body_hint.find("hint < 0 || hint >= CATALOG_MGR_BODY_COUNT) hint = -1") != std::string::npos);
		REQUIRE(head_hint.find("hint < 0 || hint >= CATALOG_MGR_HEAD_COUNT) hint = -1") != std::string::npos);
		REQUIRE(body_hint.find("assetCatalogRegisterBodyHeadArchive(file_path, id, 0, (s32)hint") != std::string::npos);
		REQUIRE(head_hint.find("assetCatalogRegisterBodyHeadArchive(file_path, id, 1, (s32)hint") != std::string::npos);
	}
	REQUIRE(conformance.find("\".pdmaterial\": schema(") != std::string::npos);
	REQUIRE(conformance.find("required=[\"material.ini\", \"material.json\"]") !=
	        std::string::npos);
	REQUIRE(scanner.find("\"mesh_archive\"") != std::string::npos);
	REQUIRE(scanner.find("const char *pf = iniGet(ini, \"material_file\",") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"texture_archive\",\n\t\t\t\tiniGet(ini, \"texture_file\"") ==
	        std::string::npos);
	REQUIRE(scanner.find("e->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(scanner.find("entry->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(scanner.find("entry->ext.body.hand_archive") != std::string::npos);
	REQUIRE(scanner.find("entry->ext.head.mesh_archive") != std::string::npos);
	{
		const size_t arena_start = scanner.find("case ASSET_ARENA:");
		const size_t body_start = scanner.find("case ASSET_MODEL:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			scanner.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("catalogSetPrimaryFile(e, e->ext.arena.scenario_archive)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("geometry_file") == std::string::npos);
		REQUIRE(arena_case.find("iniGet(ini, \"geometry\"") == std::string::npos);
	}
	{
		const std::string shared = functionBlock(scanner, "static s32 registerBodyHeadPublicIni");
		REQUIRE(shared.find("bodyHeadSourcePrepareMesh(source.mesh_archive") != std::string::npos);
		REQUIRE(shared.find("bodyHeadSourcePrepareMesh(source.hand_archive") != std::string::npos);
		REQUIRE(shared.find("loaderPoolInstallPublicBody(&body)") != std::string::npos);
		REQUIRE(shared.find("loaderPoolInstallPublicHead(&head)") != std::string::npos);
		REQUIRE(shared.find("catalogSetPrimary(entry, primary)") != std::string::npos);
		REQUIRE(shared.find("parseBodyManifestForPrivateSlot") == std::string::npos);
		REQUIRE(shared.find("parseHeadManifestForPrivateSlot") == std::string::npos);
	}
	REQUIRE(scanner.find("e->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(scanner.find("const char *pf = e->ext.theme.theme_file") != std::string::npos);
	REQUIRE(scanner.find("e->ext.theme.ui_archive[0] ?") == std::string::npos);
	REQUIRE(netdistrib.find("e->ext.arena.scenario_archive") != std::string::npos);
	{
		const size_t arena_start = netdistrib.find("case ASSET_ARENA:");
		const size_t body_start = netdistrib.find("case ASSET_BODY:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			netdistrib.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("distribSetPrimaryFromFile(e, dirpath,\n                                      e->ext.arena.scenario_archive)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("geometry_file") == std::string::npos);
		REQUIRE(arena_case.find("iniGet(ini, \"geometry\"") == std::string::npos);
	}
	{
		const std::string received = functionBlock(netdistrib, "static s32 distribPreparePublicBodyHead");
		REQUIRE(received.find("assetCatalogPrepareBodyHeadDescriptor") != std::string::npos);
		REQUIRE(netdistrib.find("distribParseBodyManifestForPrivateSlot") == std::string::npos);
		REQUIRE(netdistrib.find("distribParseHeadManifestForPrivateSlot") == std::string::npos);
		REQUIRE(netdistrib.find("assetCatalogFinishBodyHeadAdmission") != std::string::npos);
	}
	REQUIRE(netdistrib.find("e->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(netdistrib.find("const char *pf = iniGet(ini, \"material_file\",") != std::string::npos);
	REQUIRE(netdistrib.find("iniGet(ini, \"texture_archive\",\n                iniGet(ini, \"texture_file\"") ==
	        std::string::npos);
	REQUIRE(netdistrib.find("const char *pf = e->ext.theme.theme_file") != std::string::npos);
	REQUIRE(netdistrib.find("e->ext.theme.ui_archive[0] ?") == std::string::npos);
	REQUIRE(runtime.find("case ASSET_ARENA:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.arena.scenario_archive") != std::string::npos);
	REQUIRE(runtime.find("binding->name_langid = entry->ext.arena.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.arena.requirefeature") != std::string::npos);
	const size_t runtime_binding_switch = runtime.find("switch (entry->type)");
	REQUIRE(runtime_binding_switch != std::string::npos);
	{
		const size_t arena_start =
			runtime.find("case ASSET_ARENA:", runtime_binding_switch);
		const size_t body_start = runtime.find("case ASSET_BODY:", arena_start);
		REQUIRE(arena_start != std::string::npos);
		REQUIRE(body_start != std::string::npos);
		const std::string arena_case =
			runtime.substr(arena_start, body_start - arena_start);
		REQUIRE(arena_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
		REQUIRE(arena_case.find("binding->primary_path") == std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_BODY:") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_HEAD:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.mesh_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.hand_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.body.display_name") != std::string::npos);
	REQUIRE(runtime.find("binding->name_langid = entry->ext.body.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.body.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.head.mesh_archive") != std::string::npos);
	REQUIRE(runtime.find("binding->requirefeature = entry->ext.head.requirefeature") != std::string::npos);
	{
		const size_t character_start =
			runtime.find("case ASSET_CHARACTER:", runtime_binding_switch);
		const size_t skin_start = runtime.find("case ASSET_SKIN:",
			character_start);
		REQUIRE(character_start != std::string::npos);
		REQUIRE(skin_start != std::string::npos);
		const std::string character_case =
			runtime.substr(character_start, skin_start - character_start);
		REQUIRE(character_case.find("entry->ext.character.bodyfile") !=
		        std::string::npos);
		REQUIRE(character_case.find("entry->ext.character.portrait_file") !=
		        std::string::npos);
		REQUIRE(character_case.find("binding->primary_path") ==
		        std::string::npos);
		/* Shared policy validation requires body source for every template,
		 * fixed head source only for fixed templates, and precedes publication. */
		REQUIRE(character_case.find("binding->character_head_policy = character_policy") != std::string::npos);
		REQUIRE(character_case.find("binding->character_head_policy != CHARACTER_HEAD_POLICY_INVALID") != std::string::npos);
		const std::string activation = functionBlock(runtime, "s32 assetRuntimeActivateCatalogEntry");
		requireTokenOrder(activation, "character_policy = characterSourcePolicy(entry)",
			"if (character_policy == CHARACTER_HEAD_POLICY_INVALID) return 0");
		requireTokenOrder(activation, "if (character_policy == CHARACTER_HEAD_POLICY_INVALID) return 0",
			"s_allocBinding(entry->id)");
		const std::string policy = functionBlock(readSourceText("port/src/character_head_policy.c"),
			"character_head_policy_e characterHeadPolicyResolve");
		REQUIRE(policy.find("!catalogIdValid(body_id) || !bodyfile || !bodyfile[0]") != std::string::npos);
		REQUIRE(policy.find("if (resolved == CHARACTER_HEAD_POLICY_FIXED)") != std::string::npos);
		REQUIRE(policy.find("!catalogIdValid(head_id) || !headfile || !headfile[0]") != std::string::npos);
		REQUIRE(policy.find("else if ((head_id && head_id[0]) || (headfile && headfile[0]))") != std::string::npos);
	}
	{
		const size_t body_start =
			runtime.find("case ASSET_BODY:", runtime_binding_switch);
		const size_t head_start = runtime.find("case ASSET_HEAD:",
			body_start);
		REQUIRE(body_start != std::string::npos);
		REQUIRE(head_start != std::string::npos);
		const std::string body_case =
			runtime.substr(body_start, head_start - body_start);
		REQUIRE(body_case.find("entry->ext.body.mesh_archive") !=
		        std::string::npos);
		REQUIRE(body_case.find("entry->ext.body.hand_archive") !=
		        std::string::npos);
		REQUIRE(body_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(body_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	{
		const size_t head_start =
			runtime.find("case ASSET_HEAD:", runtime_binding_switch);
		const size_t character_start = runtime.find("case ASSET_CHARACTER:",
			head_start);
		REQUIRE(head_start != std::string::npos);
		REQUIRE(character_start != std::string::npos);
		const std::string head_case =
			runtime.substr(head_start, character_start - head_start);
		REQUIRE(head_case.find("entry->ext.head.mesh_archive") !=
		        std::string::npos);
		REQUIRE(head_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(head_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_GAMEMODE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.name") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.description") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.gamemode.min_players") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_max_players = entry->ext.gamemode.max_players") != std::string::npos);
	REQUIRE(runtime.find("binding->gamemode_requirefeature = entry->ext.gamemode.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_BOT_PROFILE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.profile_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_name_langid = entry->ext.bot_profile.name_langid") != std::string::npos);
	REQUIRE(runtime.find("binding->bot_profile_requirefeature = entry->ext.bot_profile.requirefeature") != std::string::npos);
	{
		const size_t gamemode_start =
			runtime.find("case ASSET_GAMEMODE:", runtime_binding_switch);
		const size_t bot_start = runtime.find("case ASSET_BOT_PROFILE:",
			gamemode_start);
		REQUIRE(gamemode_start != std::string::npos);
		REQUIRE(bot_start != std::string::npos);
		const std::string gamemode_case =
			runtime.substr(gamemode_start, bot_start - gamemode_start);
		REQUIRE(gamemode_case.find("entry->ext.gamemode.rules_file") !=
		        std::string::npos);
		REQUIRE(gamemode_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t bot_start =
			runtime.find("case ASSET_BOT_PROFILE:", runtime_binding_switch);
		const size_t hud_start = runtime.find("case ASSET_HUD:", bot_start);
		REQUIRE(bot_start != std::string::npos);
		REQUIRE(hud_start != std::string::npos);
		const std::string bot_case =
			runtime.substr(bot_start, hud_start - bot_start);
		REQUIRE(bot_case.find("entry->ext.bot_profile.profile_file") !=
		        std::string::npos);
		REQUIRE(bot_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.effect.effect_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.effect.timeline_file") != std::string::npos);
	REQUIRE(runtime.find("binding->dependency_a, binding->shader_id") ==
	        std::string::npos);
	{
		const size_t effect_start =
			runtime.find("case ASSET_EFFECT:", runtime_binding_switch);
		const size_t vehicle_start = runtime.find("case ASSET_VEHICLE:",
			effect_start);
		REQUIRE(effect_start != std::string::npos);
		REQUIRE(vehicle_start != std::string::npos);
		const std::string effect_case =
			runtime.substr(effect_start, vehicle_start - effect_start);
		REQUIRE(effect_case.find("entry->ext.effect.effect_file") !=
		        std::string::npos);
		REQUIRE(effect_case.find("entry->ext.effect.timeline_file") !=
		        std::string::npos);
		REQUIRE(effect_case.find("binding->primary_path") ==
		        std::string::npos);
	}
	{
		const size_t vehicle_start =
			runtime.find("case ASSET_VEHICLE:", runtime_binding_switch);
		const size_t prop_start = runtime.find("case ASSET_PROP:",
			vehicle_start);
		REQUIRE(vehicle_start != std::string::npos);
		REQUIRE(prop_start != std::string::npos);
		const std::string vehicle_case =
			runtime.substr(vehicle_start, prop_start - vehicle_start);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.model_file") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.physics_file") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("entry->ext.vehicle.behavior_graph") !=
		        std::string::npos);
		REQUIRE(vehicle_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(vehicle_case.find("s_hasText(binding->dependency_a)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_SKIN:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.skin_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.skin.material_archive") != std::string::npos);
	REQUIRE(runtime.find("s_hasAnyFile(binding->dependency_c, binding->dependency_d") ==
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.material_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.texture_archive") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.material.effect_archive") != std::string::npos);
	REQUIRE(runtime.find("case ASSET_THEME:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.theme.theme_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.theme.ui_archive") != std::string::npos);
	REQUIRE(runtime.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
	        std::string::npos);
	REQUIRE(runtime.find("case ASSET_WEAPON:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.primary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.secondary_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.shared_context") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.settings_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.variables_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.weapon_id") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.requirefeature") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.dual_wieldable") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.weapon.name") != std::string::npos);
	{
		const size_t weapon_start =
			runtime.find("case ASSET_WEAPON:", runtime_binding_switch);
		const size_t projectile_start = runtime.find("case ASSET_PROJECTILE:",
			weapon_start);
		REQUIRE(weapon_start != std::string::npos);
		REQUIRE(projectile_start != std::string::npos);
		const std::string weapon_case =
			runtime.substr(weapon_start, projectile_start - weapon_start);
		REQUIRE(weapon_case.find("entry->ext.weapon.primary_graph") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.secondary_graph") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.shared_context") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.settings_file") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.variables_file") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("entry->ext.weapon.behavior_graph") ==
		        std::string::npos);
		REQUIRE(weapon_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(weapon_case.find("s_hasText(binding->authored_file)") !=
		        std::string::npos);
		REQUIRE(weapon_case.find("s_hasText(binding->dependency_d)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_PROJECTILE:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.behavior_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.entity_ref") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.projectile.name") != std::string::npos);
	{
		const size_t projectile_start =
			runtime.find("case ASSET_PROJECTILE:", runtime_binding_switch);
		const size_t entity_start = runtime.find("case ASSET_ENTITY:",
			projectile_start);
		REQUIRE(projectile_start != std::string::npos);
		REQUIRE(entity_start != std::string::npos);
		const std::string projectile_case =
			runtime.substr(projectile_start, entity_start - projectile_start);
		REQUIRE(projectile_case.find(": entry->ext.projectile.model_file") ==
		        std::string::npos);
		REQUIRE(projectile_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(projectile_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_ENTITY:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.behavior_graph") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.model_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.archetype") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.entity.name") != std::string::npos);
	{
		const size_t entity_start =
			runtime.find("case ASSET_ENTITY:", runtime_binding_switch);
		const size_t mission_start = runtime.find("case ASSET_MISSION:",
			entity_start);
		REQUIRE(entity_start != std::string::npos);
		REQUIRE(mission_start != std::string::npos);
		const std::string entity_case =
			runtime.substr(entity_start, mission_start - entity_start);
		REQUIRE(entity_case.find(": entry->ext.entity.model_file") ==
		        std::string::npos);
		REQUIRE(entity_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(entity_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("entry->ext.prop.name") != std::string::npos);
	{
		const size_t prop_start =
			runtime.find("case ASSET_PROP:", runtime_binding_switch);
		const size_t weapon_start = runtime.find("case ASSET_WEAPON:",
			prop_start);
		REQUIRE(prop_start != std::string::npos);
		REQUIRE(weapon_start != std::string::npos);
		const std::string prop_case =
			runtime.substr(prop_start, weapon_start - prop_start);
		REQUIRE(prop_case.find("entry->ext.prop.model_file") !=
		        std::string::npos);
		REQUIRE(prop_case.find("entry->ext.prop.prop_file") !=
		        std::string::npos);
		REQUIRE(prop_case.find("entry->ext.prop.behavior_graph") !=
		        std::string::npos);
		REQUIRE(prop_case.find("binding->primary_path") ==
		        std::string::npos);
		REQUIRE(prop_case.find("s_hasAnyFile(binding->authored_file, NULL, NULL, NULL)") !=
		        std::string::npos);
	}
	REQUIRE(runtime.find("case ASSET_LANG:") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.strings_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.locale") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.lang_category") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.lang.string_count") != std::string::npos);

	const std::string scenario = readSourceText("port/src/loader_walker_scenario.c");
	REQUIRE(scenario.find("\"runtime_source\"") != std::string::npos);
	REQUIRE(scenario.find("\"scene\"") != std::string::npos);
	REQUIRE(scenario.find("e->ext.scenario.scene_file") != std::string::npos);
	REQUIRE(scenario.find("e->ext.scenario.level_graph_file") !=
	        std::string::npos);

	const std::string mesh_source =
		readSourceText("port/src/loader_walker_mesh_source_plan.c");
	const std::string mesh_bind =
		readSourceText("port/src/loader_walker_mesh_source_bind.c");
	REQUIRE(mesh_source.find("\"geometry\"") != std::string::npos);
	REQUIRE(mesh_source.find("\"model.obj\"") != std::string::npos);
	REQUIRE(mesh_source.find("\"source_filenum_symbol\"") != std::string::npos);
	REQUIRE(mesh_source.find("loaderEnumResolveFileEnum(fields.source_symbol, -1)") !=
	        std::string::npos);
	REQUIRE(mesh_source.find("meshManifestParse") != std::string::npos);
	REQUIRE(mesh_source.find("loaderWalkerMeshSourceChangeAllowed") !=
		std::string::npos);
	REQUIRE(mesh_bind.find("entry->source_filenum = plan->source_filenum") !=
	        std::string::npos);
	REQUIRE(mesh_bind.find("loaderWalkerMeshSourceMatchesEntry") !=
		std::string::npos);
	const std::string pdmesh_extract = readSourceText("port/src/romextract_pdmesh.c");
	REQUIRE(pdmesh_extract.find("skeleton_symbol") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"source_filenum_symbol\\\"") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("source_filenum_symbol = %s") ==
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("modAssetCompilerSkeletonSymbolForPointer") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("g_CartFileNums") != std::string::npos);
	REQUIRE(pdmesh_extract.find("base:model_cartridge_rifle") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("FILE_GHUDPIECE") !=
	        std::string::npos);

	const std::string body = readSourceText("port/src/loader_walker_body.c");
	const std::string head = readSourceText("port/src/loader_walker_head.c");
	REQUIRE(body.find("assetCatalogRegisterBodyHeadArchive") != std::string::npos);
	REQUIRE(head.find("assetCatalogRegisterBodyHeadArchive") != std::string::npos);
	REQUIRE(body.find("s_bindBodyHandModelSource") == std::string::npos);
	const std::string body_head_registration = functionBlock(scanner, "static s32 registerBodyHeadPublicIni");
	const std::string body_head_binding = readSourceText("port/src/body_head_source_bind.c");
	const std::string prepare_mesh = functionBlock(body_head_binding, "s32 bodyHeadSourcePrepareMesh");
	const std::string bind_mesh = functionBlock(body_head_binding, "s32 bodyHeadSourceBindMesh");
	requireTokenOrder(body_head_registration, "bodyHeadSourcePrepareMesh(source.mesh_archive", "externalScanTransactionBegin");
	requireTokenOrder(body_head_registration, "bodyHeadSourcePrepareMesh(source.hand_archive", "externalScanTransactionBegin");
	REQUIRE(prepare_mesh.find("loaderWalkerMeshSourcePlanManifest") != std::string::npos);
	REQUIRE(prepare_mesh.find("fsFileSize(candidate.plan.source_path) <= 0") != std::string::npos);
	REQUIRE(prepare_mesh.find("existing ? existing->source_filenum : -1") != std::string::npos);
	REQUIRE(bind_mesh.find("assetCatalogResolveModelPrivateSlot(binding->id)") != std::string::npos);
	REQUIRE(bind_mesh.find("assetCatalogModelPrivateSourceFilenum(runtime_slot)") != std::string::npos);
	REQUIRE(bind_mesh.find("catalogIdBySourceFilenum(ASSET_MODEL, binding->plan.source_filenum)") != std::string::npos);
	REQUIRE(bind_mesh.find("loaderWalkerBindMeshSource(entry, &binding->plan, 1") != std::string::npos);
	REQUIRE(bind_mesh.find("reverse ownership mismatch") != std::string::npos);
	REQUIRE(body_head_registration.find("hand.present && !bodyHeadSourceBindMesh(&hand") != std::string::npos);
	REQUIRE(body_head_registration.find("bodyHeadSourceBuildBody(&source, slot, mesh_file, hand_file, &body)") != std::string::npos);
	REQUIRE(body_head_registration.find("catalogDepRegisterTyped(id, hand.id, ASSET_MODEL, bundled)") != std::string::npos);

	REQUIRE(body_mgr.find("catalogLoadStageAsset(ASSET_BODY, id)") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("s_Bodies[bodynum].modeldef = catalogGetLoadedModeldef(id)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogLoadStageAsset(ASSET_HEAD, id)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("s_Heads[headnum].modeldef = catalogGetLoadedModeldef(id)") !=
	        std::string::npos);
	REQUIRE(load.find("modelSourceResolvePath") != std::string::npos);
	REQUIRE(load.find("public model source is invalid or missing") != std::string::npos);
	REQUIRE(load.find("modelSourceResolvePath(source_path") <
	        load.find("modeldefLoadToNewFromHandle(handle"));
	const std::string modeldef_loader = readSourceText("src/game/modeldef.c");
	REQUIRE(modeldef_loader.find("modelSourceResolvePath") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.weapon.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.prop.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("entry->ext.vehicle.model_file") != std::string::npos);
	REQUIRE(modeldef_loader.find("modAssetCompilerBuildModeldef(entry, resolved_source_path") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("body manager modeldef fallback") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("assetSourceDebugFatalHandleFallback(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_BODY, handle)") !=
	        std::string::npos);
	REQUIRE(body_mgr.find("catalogManagerBodyModeldefPassesSourceOnlyCheck(bodynum, id") <
	        body_mgr.find("modeldefLoadToNewFromHandle(handle"));
	REQUIRE(head_mgr.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("head manager modeldef fallback") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("assetSourceDebugFatalHandleFallback(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_HEAD, handle)") !=
	        std::string::npos);
	REQUIRE(head_mgr.find("catalogManagerHeadModeldefPassesSourceOnlyCheck(headnum, id") <
	        head_mgr.find("modeldefLoadToNewFromHandle(handle"));
	REQUIRE(player.find("bodyresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("headresult.handle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(player.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(player.find("playerFatalSourceOnlyCharacterAssetFailure(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(player.find("playerFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(player.find("refusing player fallback/substitution") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetBodyModeldef(bodynum)") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetHeadModeldef(headnum)") !=
	        std::string::npos);
	REQUIRE(player.find("static bool playerBodyModeldefIsUsable") !=
	        std::string::npos);
	REQUIRE(player.find("bool public_source_generated =") !=
	        std::string::npos);
	REQUIRE(player.find("catalogGetLoadedModeldef(body_id) == modeldef") !=
	        std::string::npos);
	REQUIRE(player.find("playerBodyModeldefIsUsable(bodymodeldef, multi_body_id)") !=
	        std::string::npos);
	REQUIRE(player.find("bodymodeldef->numparts == 0") ==
	        std::string::npos);
	REQUIRE(player.find("weapon_model_file_source_1p") != std::string::npos);
	REQUIRE(player.find("player chrbody weapon modeldef") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(player.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, weapon_handle)") !=
	        std::string::npos);
	REQUIRE(player.find("catalogLoadStageAsset(ASSET_MODEL, weapon_model_id_1p)") !=
	        std::string::npos);
	REQUIRE(menu.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(menu.find("menu body preview modeldef") !=
	        std::string::npos);
	REQUIRE(menu.find("menu head preview modeldef") !=
	        std::string::npos);
	REQUIRE(menu.find("menu raw model preview") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(menu.find("menuModelHandlePassesSourceOnlyCheck(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(mainmenu.find("MENUMODELPARAMS_SET_FILENUM(weaponGetFileNum(weaponnum))") ==
	        std::string::npos);
	REQUIRE(mainmenu.find("weapon menu preview weaponnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(mplayer_setup.find("MENUMODELPARAMS_SET_FILENUM(catalogGetHeadFilenumByIndex(headnum))") ==
	        std::string::npos);
	REQUIRE(mplayer_setup.find("MP head preview headnum=%d has no provider-backed catalog entry") !=
	        std::string::npos);
	REQUIRE(title.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(title.find("title model size") !=
	        std::string::npos);
	REQUIRE(title.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(title.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, modelresult.handle)") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidatePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog body validation modeldef") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("modelcatalog head validation modeldef") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("assetSourceDebugFatalHandleFallback(type, context, id, handle)") !=
	        std::string::npos);
	REQUIRE(modelcatalog.find("catalogValidateSourceMissing(handle, filenum)") >
	        modelcatalog.find("catalogValidatePassesSourceOnlyCheck(index, ce->category, filenum, handle)"));
	REQUIRE(forge_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("s_forgeModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge door modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge weapon pad modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("forge prop modeldef") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("assetSourceDebugFatalHandleFallback(type, context, catalog_id, handle)") !=
	        std::string::npos);
	REQUIRE(forge_runtime.find("|| pr.filenum <= 0") == std::string::npos);
	REQUIRE(forge_runtime.find("|| wr.filenum <= 0") == std::string::npos);
	REQUIRE(forge_runtime.find("assetHandleIsNull(pr.handle)") != std::string::npos);
	REQUIRE(forge_runtime.find("assetHandleIsNull(wr.handle)") != std::string::npos);
	REQUIRE(setuputils.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setup modeldef") !=
	        std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(setuputils.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL, handle)") !=
	        std::string::npos);
	REQUIRE(setuputils.find("setupModelHandlePassesSourceOnlyCheck(modelnum, model_id, model_handle)") <
	        setuputils.find("modeldefLoadToNewFromHandle(model_handle, source_filenum)"));
	const std::string bondgun = readSourceText("src/game/bondgun.c");
	REQUIRE(bondgun.find("#include \"assetcatalog.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("#include \"modasset_compiler.h\"") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderPath(player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogHandleByModelSourceFilenum(ASSET_NONE, filenum)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("fileProviderHandle") == std::string::npos);
	REQUIRE(bondgun.find("bgunQueuedLoadPassesSourceOnlyCheck") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(bondgun.find("assetSourceDebugHandleRequiresPublicFileSource(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model inflated-size") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model loaded-size") !=
	        std::string::npos);
	REQUIRE(bondgun.find("weapon model load-to-addr") !=
	        std::string::npos);
	REQUIRE(bondgun.find("modelSourceResolvePath") !=
	        std::string::npos);
	REQUIRE(bondgun.find("player->gunctrl.loadhandle.provider == fileProvider()") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogIdBySourceHandle(ASSET_MODEL, player->gunctrl.loadhandle)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogLoadStageAsset(ASSET_MODEL, model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("catalogGetLoadedModeldef(model_id)") !=
	        std::string::npos);
	REQUIRE(bondgun.find("BONDGUN.SOURCE: loaded catalog model source") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_bondgun_model_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("BONDGUN_MODEL_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_model_handle_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("MODEL_HANDLE_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("menu raw model preview") !=
	        std::string::npos);
	REQUIRE(guard.find("title model size") !=
	        std::string::npos);
	REQUIRE(guard.find("player chrbody weapon modeldef") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_modelcatalog_source_only_guards") !=
	        std::string::npos);
	REQUIRE(guard.find("MODELCATALOG_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("modelcatalog body validation modeldef") !=
	        std::string::npos);
	REQUIRE(guard.find("source-only check must precede catalogValidateSourceMissing") !=
	        std::string::npos);
	REQUIRE(guard.find("CATALOG_METADATA_PRELOAD_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_catalog_metadata_preload_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("catalog bundled metadata preloads must enforce source-only public sources") !=
	        std::string::npos);
	REQUIRE(guard.find("FORGE_RUNTIME_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_forge_runtime_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("Forge runtime model spawns must enforce source-only public family handles") !=
	        std::string::npos);
	REQUIRE(guard.find("SETUP_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_setup_modeldef_source_only_guard(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("setup modeldef loads must enforce source-only public model handles") !=
	        std::string::npos);
	REQUIRE(guard.find("CHARACTER_MANAGER_MODELDEF_SOURCE_ONLY_ERROR") !=
	        std::string::npos);
	REQUIRE(guard.find("scan_character_manager_modeldef_source_only_guards(root)") !=
	        std::string::npos);
	REQUIRE(guard.find("body/head manager modeldef fallbacks must enforce source-only public") !=
	        std::string::npos);
	REQUIRE(guard.find("weapon model load-to-addr") !=
	        std::string::npos);
	REQUIRE(guard.find("assetSourceDebugFatalHandleFallback(ASSET_MODEL") !=
	        std::string::npos);
	REQUIRE(modeldef.find("#include \"assetcatalog.h\"") !=
	        std::string::npos);
	REQUIRE(modeldef.find("#include \"modasset_compiler.h\"") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefLoadExternalCatalogSource") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefExternalCatalogSourceHandle") !=
	        std::string::npos);
	REQUIRE(modeldef.find("catalogHandleByModelSourceFilenum(ASSET_NONE, source_filenum)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("handle = modeldefCatalogModelSourceHandle((s32)fileid)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("handle = external_handle") !=
	        std::string::npos);
	REQUIRE(modeldef.find("fileProviderHandle") == std::string::npos);
	REQUIRE(modeldef.find("modeldefRefuseRomSource") !=
	        std::string::npos);
	REQUIRE(modeldef.find("ROM fallback refused") !=
	        std::string::npos);
	REQUIRE(modeldef.find("assetLoadRomToNew") == std::string::npos);
	REQUIRE(modeldef.find("assetLoadRomToAddr") == std::string::npos);
	const std::string selected_model_load = functionBlock(modeldef, "struct modeldef *modeldefLoadFromHandle");
	REQUIRE(selected_model_load.find("if (handle.provider == fileProvider())") != std::string::npos);
	requireTokenOrder(selected_model_load, "modeldefLoadExternalCatalogSource(handle, source_filenum)",
		"assetLoadToAddr(handle");
	REQUIRE(selected_model_load.find("modAssetCompilerIsExternalSource(modeldefCatalogSourcePath(handle))") ==
	        std::string::npos);
	REQUIRE(modeldef.find("catalogIdBySourceHandle(model_payload_types[i], handle)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modeldefTypeUsesLoadedModelPayload(model_type)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("catalogLoadStageAsset(model_type, entry->id)") !=
	        std::string::npos);
	REQUIRE(modeldef.find("modAssetCompilerBuildModeldef(entry, resolved_source_path") !=
	        std::string::npos);
	REQUIRE(modeldef.find("MODELDEF.SOURCE: loaded catalog model source") !=
	        std::string::npos);
	REQUIRE(modeldef.find("i < ARRAYCOUNT(g_Skeletons)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_static_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("public_source_generated_modeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodyFatalSourceOnlyCharacterAssetFailure(ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("bodyFatalSourceOnlyCharacterAssetFailure(ASSET_HEAD") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("refusing body fallback/substitution") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("catalogGetLoadedModeldef(body_source_id) == bodymodeldef") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithoutAnim(bodymodeldef)") !=
	        std::string::npos);
	REQUIRE(body_runtime.find("modelmgrInstantiateModelWithAnim(bodymodeldef)") !=
	        std::string::npos);
	const std::string modasset_compiler = readSourceText("port/src/modasset_compiler.c");
	const std::string modasset_compiler_h = readSourceText("port/include/modasset_compiler.h");
	REQUIRE(modasset_compiler_h.find("#define MODASSET_COMPILER_VERSION 10") !=
	        std::string::npos);
	REQUIRE(modasset_compiler_h.find(
	        "#define MODASSET_COMPILER_ANIMATION_VERSION 11") !=
	        std::string::npos);
	REQUIRE(modasset_compiler_h.find("#define MODASSET_COMPILER_MODELDEF_VERSION 15") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonForSymbol") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonSymbolForPointer") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("{ \"SKEL_HEAD\", NULL, SKEL_HEAD }") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("token < 0x10000") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("modAssetCompilerSkeletonSymbolForId((s16)token)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefSkeletonFromMetadata") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadHierarchy") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("buildGeneratedModeldefFromMeshHierarchy") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.nodes.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.parts.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.faces.json\"") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.nodes.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.parts.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("\"model.faces.tsv\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefReadFaces") !=
	        std::string::npos);
	REQUIRE(pdmesh_extract.find("distance_near") != std::string::npos);
	REQUIRE(pdmesh_extract.find("reorder_target_a") != std::string::npos);
	REQUIRE(modasset_compiler.find("MODELNODETYPE_DISTANCE") != std::string::npos);
	REQUIRE(modasset_compiler.find("rodata->distance.near = row->distance_near") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->dynamic_rodatas[i].reorder.unk18") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->dynamic_rodatas[i].reorder.unk1c") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("if (parts_rc <= 0)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->matrix_index") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("tri->matrix_index >= 0 ? tri->matrix_index") !=
	        std::string::npos);
	// Private extraction provenance cannot override edited public mesh metadata.
    REQUIRE(modasset_compiler.find("\"_meta/manifest.json\"") ==
            std::string::npos);
    REQUIRE(modasset_compiler.find("modAssetCompilerBuildModeldefWithInputs") != std::string::npos);
    REQUIRE(modasset_compiler.find("modelInputRead(inputs, metadata_path") != std::string::npos);
	REQUIRE(modasset_compiler.find("\"mesh.ini\"") != std::string::npos);
	REQUIRE(pdmesh_extract.find("model_scale = %.9g") != std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"model_scale\\\": %.9g") != std::string::npos);
	REQUIRE(pdmesh_extract.find("faces_file = model.faces.json") != std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"faces\\\": \\\"model.faces.json\\\"") != std::string::npos);
	REQUIRE(pdmesh_extract.find("faces_file = model.faces.tsv") == std::string::npos);
	REQUIRE(pdmesh_extract.find("\\\"faces\\\": \\\"model.faces.tsv\\\"") == std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefScaleFromMetadata") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.scale = generatedModeldefScaleFromMetadata(inputs, source_path)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("strstr(sep + 2, \"::\")") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_rodata.position.part = skeleton ? 0 : 0xffff") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel = skeleton") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefNeedsChrRoot") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("entry->type == ASSET_BODY") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("skeleton = chr_root ? &g_SkelChr") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("if (chr_root && i == 0)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("node_type = MODELNODETYPE_CHRINFO") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("strcmp(group_name, \"-\") == 0") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureSourceBounds") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_node.type == MODELNODETYPE_CHRINFO") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->bbox_node.type = MODELNODETYPE_BBOX") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefMeshBounds(mesh, &owner->bbox_rodata.bbox)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->root_node.child = &owner->bbox_node") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureCctvParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelCctv") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_CASING") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_LENS") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_CCTV_0003") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->cctv_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.nummatrices = 2") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureAutogunParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelAutogun") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_AUTOGUN_0001") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_AUTOGUN_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->toggle_node.type = MODELNODETYPE_POSITION") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("autogun_part_table") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("struct modelnode *nodes[2]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("s16 partnums[2]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->autogun_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.nummatrices = 3") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureWindowedDoorParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelWindowedDoor") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0000") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0001") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_WINDOWEDDOOR_0002") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->toggle_rodata.toggle.target = NULL") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->windowed_door_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.numparts = 3") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("generatedModeldefConfigureLogoParts") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelLogo") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.skel != &g_SkelPdLogo") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_LOGO_FRONTSIDE") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("MODELPART_LOGO_0003") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("struct modelnode *nodes[4]") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.parts = owner->logo_part_table.nodes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->def.numparts = 4") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("vertex_colour_bytes") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("ALIGN8(vertex_bytes)") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("owner->colours = (Col *)((u8 *)owner->vertices + ALIGN8(vertex_bytes))") !=
	        std::string::npos);
	REQUIRE(modasset_compiler.find("free(owner->colours)") ==
	        std::string::npos);
	REQUIRE(chraction.find("chr->model == NULL || chr->model->anim == NULL") !=
	        std::string::npos);
	REQUIRE(chraction.find("static model has no animation controller") !=
	        std::string::npos);
	const std::string chr_runtime = readSourceText("src/game/chr.c");
	REQUIRE(chr_runtime.find("bool model_has_anim") != std::string::npos);
	REQUIRE(chr_runtime.find("if (!model_has_anim)") != std::string::npos);
	REQUIRE(chr_runtime.find("!model_has_anim || chr->actiontype != ACT_STAND") !=
	        std::string::npos);
	REQUIRE(setup.find("setupResolvePointerInLoadedSetup") !=
	        std::string::npos);
	REQUIRE(setup.find("invalid path pads pointer") != std::string::npos);

	const std::string sfx = readSourceText("port/src/loader_walker_sfx.c");
	const std::string voice = readSourceText("port/src/loader_walker_voice.c");
	const std::string song = readSourceText("port/src/loader_walker_song.c");
	const std::string font = readSourceText("port/src/loader_walker_font.c");
	const std::string audio_source = readSourceText("port/src/catalog_audio_public_source.cpp");
	const std::string audio_registration = functionBlock(scanner, "s32 assetCatalogRegisterBaseAudioSource");
	for (const std::string *walker : {&sfx, &voice}) {
		REQUIRE(walker->find("always_invoke = 1") != std::string::npos);
		REQUIRE(walker->find("catalogAudioNativeIdentityParse") != std::string::npos);
		REQUIRE(walker->find("loaderWalkerArchiveTextMember") != std::string::npos);
		REQUIRE(walker->find("iniParseBuffer") != std::string::npos);
		REQUIRE(walker->find("assetCatalogRegisterBaseAudioSource") != std::string::npos);
		REQUIRE(walker->find("loaderWalkerEnvelopeInt") == std::string::npos);
		REQUIRE(walker->find("decoded_sample_count") == std::string::npos);
		REQUIRE(walker->find("sample_rate_hz") == std::string::npos);
	}
	REQUIRE(sfx.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(voice.find("\"voice.ini\"") != std::string::npos);
	for (const char *token : {"catalogAudioPublicSourceParse(source, 1, &parsed)", "catalogAudioSourceIdentityPrepare",
	        "catalogHandleForSourceFile(parsed.file_path)", "assetCatalogRegisterAudio", "catalogAudioSourceIdentityApply",
	        "catalogAudioPublicSourceApply(entry, &parsed)", "catalogSetPrimary(entry, prepared_handle)",
	        "loaderWalkerMarkBaseArchiveEntry(entry)", "externalScanTransactionRollback", "*restored = *passive_backup"})
		REQUIRE(audio_registration.find(token) != std::string::npos);
	requireTokenOrder(audio_registration, "catalogAudioPublicSourceParse(source, 1, &parsed)", "assetCatalogRegisterAudio");
	requireTokenOrder(audio_registration, "catalogAudioSourceIdentityPrepare", "assetCatalogRegisterAudio");
	requireTokenOrder(audio_registration, "catalogHandleForSourceFile(parsed.file_path)", "assetCatalogRegisterAudio");
	REQUIRE(audio_registration.find("if (!prior)") < audio_registration.find("identity.source_filenum = source_filenum"));
	REQUIRE(audio_source.find("modAssetJsonReadValue") != std::string::npos);
	REQUIRE(audio_source.find("source_index") != std::string::npos);
	REQUIRE(song.find("\"sequence.mid\"") != std::string::npos);
	REQUIRE(song.find("always_invoke = 1") != std::string::npos);
	REQUIRE(song.find("assetCatalogResolve(id)") != std::string::npos);
	REQUIRE(song.find("duration_ms = existing->ext.audio.duration_ms") !=
	        std::string::npos);
	REQUIRE(font.find("e->ext.font.font_file") != std::string::npos);
	REQUIRE(font.find("e->ext.font.metrics_file") != std::string::npos);

	const std::string lang = readSourceText("port/src/loader_walker_lang.c");
	const std::string lang_source = readSourceText("port/src/lang_source.cpp");
	REQUIRE(lang.find("langSourceParseManifestFields") != std::string::npos);
	REQUIRE(lang_source.find("\"source_bank\"") != std::string::npos);
	REQUIRE(lang_source.find("\"string_count\"") != std::string::npos);
	REQUIRE(lang.find("\"locale\"") != std::string::npos);
	REQUIRE(lang.find("\"category\"") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.bank_id") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.locale") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.lang_category") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.string_count") != std::string::npos);
	REQUIRE(lang.find("e->ext.lang.strings_file") != std::string::npos);
}
