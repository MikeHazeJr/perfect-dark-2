/*
 * Static pins for c3842 and c3844, split out of
 * tests/test_asset_native_source_contract.cpp for build time (T-TOOLING-018):
 * part 1 of 3 of "scenario stage payloads reject ROM fallback in source-only mode".
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

TEST_CASE("scenario stage payloads reject ROM fallback in source-only mode, part 1 of 3",
          "[modding][pdxxx][c3844][scenario][source_gate][static]") {
	const std::string header = readTextFile("port/include/asset_source_debug.h");
	const std::string debug = readTextFile("port/src/asset_source_debug.c");
	const std::string setup = readTextFile("src/game/setup.c");
	const std::string propobj_runtime = readTextFile("src/game/propobj.c");
	const std::string early_objectives_runtime =
		readTextFile("src/game/objectives.c");
	const std::string constants = readTextFile("src/include/constants.h");
	const std::string bg_runtime = readTextFile("src/game/bg.c");
	const std::string dlights_runtime = readTextFile("src/game/dlights.c");
	const std::string scene_renderer =
		readTextFile("port/fast3d/scenario_scene_renderer.cpp");
	const std::string scene_renderer_h =
		readTextFile("port/include/scenario_scene_renderer.h");
	const std::string gfx_pc = readTextFile("port/fast3d/gfx_pc.cpp");
	const std::string prop_runtime = readTextFile("src/game/prop.c");
	const std::string tiles = readTextFile("src/game/tilesreset.c");
	const std::string lv = readTextFile("src/game/lv.c");
	const std::string pad_runtime = readTextFile("src/game/pad.c");
	const std::string pad_runtime_h = readTextFile("src/include/game/pad.h");
	const std::string setup_pads = readTextFile("src/game/setuppads.c");
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

	REQUIRE(header.find("assetSourceDebugHandleUsesPublicFileSource") !=
	        std::string::npos);
	REQUIRE(header.find("assetSourceDebugHandleRequiresPublicFileSource") !=
	        std::string::npos);
	REQUIRE(header.find("assetSourceDebugFatalHandleFallback") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugIsEnabledFor(type)") !=
	        std::string::npos);
	REQUIRE(debug.find("assetSourceDebugHandleUsesPublicFileSource(handle)") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathIsRawExtractedRomPayload") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathHasSegment(path, \"files\")") !=
	        std::string::npos);
	REQUIRE(debug.find("s_pathHasSegment(path, \"segments\")") !=
	        std::string::npos);
	REQUIRE(debug.find("s_endsWithNoCase(path, \".bin\")") !=
	        std::string::npos);
	REQUIRE(debug.find("clean public typed-archive source") !=
	        std::string::npos);
	REQUIRE(debug.find("refusing ROM/static fallback") !=
	        std::string::npos);

	REQUIRE(setup.find("assetLoadRomToAddr(setupfilenum") ==
	        std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"briefing setup\"") !=
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToAddr(setup_handle") !=
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToNew(setup_handle") ==
	        std::string::npos);
	REQUIRE(setup.find("assetLoadToNew(stage.pads_handle") ==
	        std::string::npos);
	REQUIRE(setup.find("setup source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(setup.find("pads source -> ROM handle") ==
	        std::string::npos);
	REQUIRE(setup.find("\"mp setup\"") != std::string::npos);
	REQUIRE(setup.find("\"setup\"") != std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadSetupForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceActivateGraphsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("level graph activation failed") !=
	        std::string::npos);
	REQUIRE(setup.find("setup source compile failed") !=
	        std::string::npos);
	REQUIRE(setup.find("pads source compile failed") !=
	        std::string::npos);
	REQUIRE(setup.find("setupIntroCommandsAreValid") !=
	        std::string::npos);
	REQUIRE(setup.find("if (!g_GeCreditsData)") != std::string::npos);
	REQUIRE(setup.find("setupRequireScenarioSourceHandle(\"pads\"") !=
	        std::string::npos);
	REQUIRE(setup.find("scenarioSourceLoadPadsForStage(&stage") !=
	        std::string::npos);
	REQUIRE(setup.find("prop = obj->prop;\n\n\t\t\t\t\t\tif (prop == NULL)") !=
	        std::string::npos);
	REQUIRE(constants.find("#define MODELNODETYPE_TYPE19       0x19") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("(node->type & 0xff) == MODELNODETYPE_TYPE19") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata(modeldef, MODELPART_BASIC_0065)") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("modelGetType19PartRodata(modeldef, MODELPART_BASIC_0066)") !=
	        std::string::npos);
	REQUIRE(propobj_runtime.find("union modelrodata *floorPart = modelGetPartRodata(modeldef, MODELPART_BASIC_0065)") ==
	        std::string::npos);
	REQUIRE(early_objectives_runtime.find("g_Vars.currentplayer && g_Vars.currentplayer->prop &&") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"asset_source_debug.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"scenario_scene_renderer.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("#include \"scenario_source_runtime.h\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceValidateBackgroundGeometryForStage(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgTryActivateScenarioSourceBackground(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgBuildScenarioSourceTables(stagenum)") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSceneRendererIsActive()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_BgUsingScenarioSource") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("assetSourceDebugFatalHandleFallback(ASSET_SCENARIO, \"background geometry\"") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bg source -> ROM bg cache") ==
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceFatalRuntimeFallbackForStage(&stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("background source renderer activation failed") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("stage.bg_handle") != std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererActivate") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenario_scene_renderer_probe_t") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererProbeSource") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererSetCameraFrame") !=
	        std::string::npos);
	REQUIRE(scene_renderer_h.find("scenarioSceneRendererRender") !=
	        std::string::npos);
	const std::string cmake = readTextFile("CMakeLists.txt");
	const std::string probe_main =
		readTextFile("tools/scenario_scene_probe_main.cpp");
	const std::string probe_stubs =
		readTextFile("tools/scenario_scene_probe_stubs.cpp");
	REQUIRE(cmake.find("add_executable(scenario-scene-probe") !=
	        std::string::npos);
	REQUIRE(cmake.find("CXX_SCAN_FOR_MODULES OFF") != std::string::npos);
	REQUIRE(cmake.find("port/fast3d/scenario_scene_renderer.cpp") !=
	        std::string::npos);
	REQUIRE(probe_main.find("scenarioSceneRendererProbeSource") !=
	        std::string::npos);
	REQUIRE(probe_main.find("--debug-scenario-render-probe") !=
	        std::string::npos);
	REQUIRE(probe_main.find("SCENARIO.RENDER.CPU_PROBE") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("extern \"C\" void *fsFileLoad") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("extern \"C\" s32 sysArgCheck") !=
	        std::string::npos);
	REQUIRE(probe_stubs.find("modArchiveExtractMemAlloc") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSceneRendererSetCameraFrame(") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_pos.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_look.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("g_Vars.currentplayer->cam_up.f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = viGetFovY()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = viGetAspect()") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = g_Vars.currentplayer->zoominfovy") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("fovy = 60.0f") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = g_Vars.currentplayer->aspect") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("aspect = 4.0f / 3.0f") !=
	        std::string::npos);
	// B-1172: the scene draws at its display-list command, after the sky, the
	// stars and the sun, into the current view with its player's camera; it
	// no longer draws before the display list.
	REQUIRE(gfx_pc.find("case G_SCENE_RENDER_EXT:") != std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererRenderPasses(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width,") !=
	        std::string::npos);
	// B-1172 step 2: a world draw uses the game's BG matrix and keeps its
	// depth; only a backdrop draw clears depth after it.
	REQUIRE(gfx_pc.find("scenarioSceneMatrixMultiply(out, camera.world, rsp.P_matrix);") !=
	        std::string::npos);
	REQUIRE(gfx_pc.find("if (scene_passes & SCENARIO_SCENE_PASS_BACKDROP) {\n                        gfx_rapi->clear_framebuffer(false, true);") !=
	        std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererRenderSlot(") == std::string::npos);
	// B-1172: sky rooms draw relative to the camera's room, as
	// roomPopulateMtx places them.
	REQUIRE(scene_renderer.find("vec4 clip = u_VP * vec4(a_Pos + a_Sky * u_SkyOffset, 1.0);") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("out.room = readRoom(idx);") != std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"_PD_ROOM\")") != std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSceneRendererSetSkyRooms(skyrooms, skycount);") !=
	        std::string::npos);
	requireTokenOrder(bg_runtime, "scenarioSourceApplyRoomTableForStage(&stage,",
		"scenarioSceneRendererSetSkyRooms(skyrooms, skycount);");
	// B-1172: the scene applies roomHighlight's lighting per view.
	REQUIRE(bg_runtime.find("bgGetScaleBg2Gfx(), g_Vars.currentplayer->globaldrawworldoffset.f);\n\t\t\tbgSetScenarioSceneRoomLights();") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("light->brightness = roomGetSettledRegionalBrightnessForPlayer(i);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioHighlightRoom(i, &r, &g, &b);") != std::string::npos);
	REQUIRE(scene_renderer.find("\"  v_Color = pdRoomLit(a_Color);\\n\"") != std::string::npos);
	REQUIRE(scene_renderer.find("float extra = l1.w > 0.5 ? 0.0 : min(l0.y, 285.0 - m);") !=
	        std::string::npos);
	{
		// B-1157: a custom model slot takes its catalog model scale when its
		// model loads, and the stage reset clears it with the modeldef.
		const std::string setuputils = readTextFile("src/game/setuputils.c");
		const std::string setup = readTextFile("src/game/setup.c");
		REQUIRE(setuputils.find("if (modelnum >= MODEL_CUSTOM_START && modelnum < MODEL_CUSTOM_END) {\n\t\t\tconst f32 declared = model_result.entry && model_result.entry->model_scale > 0.0f") !=
		        std::string::npos);
		requireTokenOrder(setuputils, "modelAllocateRwData(g_ModelStates[modelnum].modeldef);",
			"g_ModelStates[modelnum].scale = units < 1 ? 1");
		REQUIRE(setup.find("for (i = MODEL_CUSTOM_START; i < MODEL_CUSTOM_END; i++) {\n\t\tg_ModelStates[i].scale = 0;") !=
		        std::string::npos);
	}
	{
		// The multiplayer crate slots of base content are empty (model 0).
		const std::string runtime = readTextFile("port/src/scenario_source_runtime.c");
		REQUIRE(runtime.find("if (!catalog_id || !catalog_id[0]) {\n\t\t\t\tivalue = 0;\n\t\t\t} else if (!s_setupResolveAssetOperand(table, catalog_id, ASSET_MODEL, s_setupResolveModelnum, &ivalue)) {") !=
		        std::string::npos);
		REQUIRE(gfx_pc.find("no on-screen scene vertex this frame (frames without: %d)") != std::string::npos);
	}
	{
		// B-1153: optional model metadata members are looked up quietly.
		const std::string compiler = readTextFile("port/src/modasset_compiler.c");
		REQUIRE(compiler.find("if (!(inputs && inputs->read) && fsFileSize(path) < 0) {") != std::string::npos);
		REQUIRE(compiler.find("text = (char *)modelInputReadOptional(inputs, mtl_path, &size);") != std::string::npos);
		REQUIRE(compiler.find("text = (char *)modelInputReadOptional(inputs, hierarchy_path, &size);") != std::string::npos);
		REQUIRE(compiler.find("text = (char *)modelInputReadOptional(inputs, render_path, &size);") != std::string::npos);
	}
	// B-1172: the stage fog, started for each scene pass as for the BG rooms
	// and applied with gfx_sp_vertex's formula.
	REQUIRE(bg_runtime.find("gdl = envStartFog(gdl, false);\n\t\tgSPSceneRenderEXT(gdl++, scenarioSceneRendererCurrentCameraSlot(), SCENARIO_SCENE_PASS_OPAQUE);\n\t\tgdl = envStopFog(gdl);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("gdl = envStartFog(gdl, true);\n\t\tgSPSceneRenderEXT(gdl++, scenarioSceneRendererCurrentCameraSlot(), SCENARIO_SCENE_PASS_BLEND);\n\t\tgdl = envStopFog(gdl);") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("v_Fog = clamp(clip.z * winv * u_Fog.y + u_Fog.z, 0.0, 255.0) / 255.0;") !=
	        std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererSetFog((rsp.geometry_mode & G_FOG) != 0") != std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererEndFrame();") != std::string::npos);
	REQUIRE(gfx_pc.find("scenarioSceneRendererRender(gfx_current_dimensions.width") ==
	        std::string::npos);
	REQUIRE(bg_runtime.find("gSPSceneRenderEXT(gdl++, scenarioSceneRendererCurrentCameraSlot(), SCENARIO_SCENE_PASS_OPAQUE);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("gSPSceneRenderEXT(gdl++, scenarioSceneRendererCurrentCameraSlot(), SCENARIO_SCENE_PASS_BLEND);") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgGetScaleBg2Gfx(), g_Vars.currentplayer->globaldrawworldoffset.f);") !=
	        std::string::npos);
	requireTokenOrder(bg_runtime, "SCENARIO_SCENE_PASS_OPAQUE);", "RENDERPASS_OPA_PREBG");
	requireTokenOrder(bg_runtime, "RENDERPASS_OPA_POSTBG", "SCENARIO_SCENE_PASS_BLEND);");
	requireTokenOrder(bg_runtime, "SCENARIO_SCENE_PASS_BLEND);",
		"\n\t\t\t\tgdl = propsRender(gdl, 0, RENDERPASS_XLU, roomnumsbyprop);");
	{
		const std::string lv_runtime = readTextFile("src/game/lv.c");
		REQUIRE(lv_runtime.find("if (!scenarioSceneRendererIsActive()) {\n\t\t\t\t\t\tgdl = skyRender(gdl);") ==
		        std::string::npos);
		REQUIRE(lv_runtime.find("no longer covers it and renders on every stage again. */\n\t\t\t\tgdl = skyRender(gdl);") !=
		        std::string::npos);
		REQUIRE(lv_runtime.find("SCENARIO_SCENE_PASS_OPAQUE | SCENARIO_SCENE_PASS_BLEND | SCENARIO_SCENE_PASS_BACKDROP);") !=
		        std::string::npos);
	}
	REQUIRE(scene_renderer.find("fsFileLoad(path") != std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"TEXCOORD_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("findAttr(\"COLOR_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("int uv0_i = findAttr(\"TEXCOORD_0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("int uv1_i = findAttr(\"TEXCOORD_1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("out.u0 = out.u1") != std::string::npos);
	REQUIRE(scene_renderer.find("out.u1 = out.u0") != std::string::npos);
	REQUIRE(scene_renderer.find("mat.primary_texcoord = base ? intField(*base, \"texCoord\", 0) : 0") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("mat.primary_texcoord = 1") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("vec4 sampled = texture(u_Tex, pdTexcoord(u_PrimaryTexcoord))") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform sampler2D u_Tex2") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform int u_PrimaryTexcoord") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("uniform int u_SecondaryTexcoord") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glBindAttribLocation(program, 1, \"a_Uv0\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glBindAttribLocation(program, 2, \"a_Uv1\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("readTextureBinding(textures, samplers") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("mat.has_secondary") != std::string::npos);
	REQUIRE(scene_renderer.find("secondary_texcoord = mat.secondary_texcoord == 0 ? 0 : 1") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glUniform1i(secondary_texcoord_loc, secondary_texcoord)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("sampled = mix(sampled, sampled2, 0.5)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("if (outColor.a < u_AlphaCutoff) discard") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("imageHasNonOpaqueAlpha") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("markMaterialAlpha(out)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("fillProbeResult") != std::string::npos);
	REQUIRE(scene_renderer.find("probeLoggingEnabled") != std::string::npos);
	REQUIRE(scene_renderer.find("objectField(const crude_json::object &obj") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("intField(const crude_json::object &obj") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("const crude_json::object *mat_obj =") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("m.get_ptr<crude_json::object>()") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("objectField(*mat_obj, \"pbrMetallicRoughness\")") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("intField(*secondary, \"texCoord\", 1)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("decode image index=%zu") != std::string::npos);
	const std::string probe_block =
		functionBlock(scene_renderer, "scenarioSceneRendererProbeSource");
	REQUIRE(probe_block.find("Scene probe") != std::string::npos);
	REQUIRE(probe_block.find("buildCpuScene(scenario_id, scene_path, probe)") !=
	        std::string::npos);
	REQUIRE(probe_block.find("fillProbeResult(probe, out_probe)") !=
	        std::string::npos);
	REQUIRE(probe_block.find("freeScene(probe)") != std::string::npos);
	REQUIRE(probe_block.find("g_scene") == std::string::npos);
	// B-1172: the scene camera is built in the game's draw space
	// (scenario_scene_camera.c, unit-tested in test_scenario_scene_camera.cpp).
	REQUIRE(scene_renderer.find("scenarioSceneCameraBuild(&camera, position, look, up, fovy_degrees,") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("memcpy(g_scene.view_projection, camera.view_projection") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("camera_ever_valid") != std::string::npos);
	REQUIRE(scene_renderer.find("if (!g_scene.camera_ever_valid)") !=
	        std::string::npos);
	requireTokenOrder(scene_renderer,
		"if (!g_scene.camera_ever_valid)",
		"SCENARIO.RENDER: skipping source scene");
	REQUIRE(scene_renderer.find("g_scene.view_projection") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: activated source scene") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: accepted camera frame") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: rendered native source scene") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetShaderiv(shader, GL_COMPILE_STATUS") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: shader compile failed") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetProgramiv(program, GL_LINK_STATUS") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("SCENARIO.RENDER: shader link failed") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("if (!ensureShader(scene))") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glGetIntegerv(GL_VIEWPORT, prev_viewport)") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("glViewport(prev_viewport[0], prev_viewport[1]") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("alpha_textures=%zu alpha_materials=%zu secondary_materials=%zu") !=
	        std::string::npos);
	REQUIRE(scene_renderer.find("dualtex=extras") != std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceValidateBackgroundGeometryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceValidateBackgroundGeometryForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogLoadStageAsset(ASSET_SCENARIO, scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("catalogGetLoadedColmesh(scenario->id)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: validated background scene source") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: background renderer using native scene source") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: built native background room tables") !=
	        std::string::npos);
	const std::string scenario_source_smoke =
		readTextFile("tools/smoke-verify/tests/scenario_pads_source_gate_smoke.json");
	REQUIRE(scenario_source_smoke.find("SCENARIO\\\\.RENDER: rendered native source scene") !=
	        std::string::npos);
	REQUIRE(scenario_source_smoke.find("SCENARIO\\\\.RENDER: skipping source scene .* without camera matrices") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("native BG renderer still pending") ==
	        std::string::npos);

	REQUIRE(scenario_walker.find("e->ext.scenario.pads_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.portals_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"portals\", \"portals.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"pads\", \"pads.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.objects_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"objects\", \"objects.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("e->ext.scenario.setup_fields_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"setup_fields\", \"setup.fields.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_waypoints_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"waypoints\", \"navigation/waypoints.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_waygroups_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"waygroups\", \"navigation/waygroups.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_covers_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"covers\", \"navigation/covers.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("navigation_paths_file") !=
	        std::string::npos);
	REQUIRE(scenario_walker.find("\"paths\", \"navigation/paths.json\"") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.ai.lists.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.pads.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("portals.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.portals.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildBgPortalsJson") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"portal_count\\\": %u") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("scenario.navigation.paths.source") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildPathsJson") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_pathRef((s32)paths[i].id") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildAiListsTable") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("s_buildIntroSpawnsTable") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("INTROCMD_SPAWN") !=
	        std::string::npos);
	REQUIRE(scenario_extractor.find("\\\"spawn_count\\\": %u") !=
	        std::string::npos);

	REQUIRE(scenario_runtime.find("fsFileLoad(pads_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePadsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parseWaypointsJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generateNavigationTablesFromPads") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("const scenario_source_path_table_t *path_table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_table && path_table->count > 0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("u32 *pad_segments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_parsePathPadJsonArray(cursor, object_end + 1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("&row.pad_segments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pad_order[ordered_count++] = padnum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generatedNavigationAddEdge(nav, a, b,") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_segmentListAppendOrMerge") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_reverseSegmentFlags") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_FLAG_OUTWARD") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_FLAG_INWARD") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO_SEGMENT_ID_MASK)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pads[j - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pad_segments[j - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->flags & PATHFLAG_CIRCULAR") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pads[path->pad_count - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path->pad_segments[path->pad_count - 1]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("branch waypoints backend=source.navigation.generated") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationDirectionalSegments") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationWaypointNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countNavigationWaygroupNeighbourRefs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated directional path segments from public navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_generateNavigationWaygroupsFromEdges(nav)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("group_ids[neighbour] = group_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav->waypoints[i].padnum = padnum") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav->covers[i].pos[0] = pads[padnum].pos[0]") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/waypoints.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/waypoints.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/waygroups.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing public navigation table 'navigation/covers.json'") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled navigation tables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("waypoint_neighbour_refs=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("waygroup_neighbour_refs=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.tables+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: generated deterministic navigation tables from public pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.generated+pads.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadPathSourceRows(text, &path_table)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("row.id != table->count") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_CIRCULAR)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countPathRowsWithFlag(&path_table, PATHFLAG_FLYING)") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("path_flags=circular:%d,flying:%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=source.navigation.generated+pads.json+navigation/paths.json+navigation/waypoints.json+navigation/waygroups.json+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_bindNavigationGenerateSource") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countAndHashSourceJsonRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_hashSourceTextHex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_hashSourceFileHex") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav_source_counts") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("nav_source_hashes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("portals_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("\\\"portals.json\\\": \\\"%s\\\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_navigation_generate_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_walk = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_jump = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_drop = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_wall = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("supports_ceiling = true") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("source_counts=pads:%d,volumes:%d,waypoints:%d,waygroups:%d,covers:%d,paths:%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("source_hashes=sha256") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("capabilities=walk,jump,drop,wall,ceiling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.navigation.generate+navigation.ini+generated-navmesh.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("navigation.ini missing deterministic nav generation contract") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata missing deterministic cache contract") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata source counts mismatch public navigation tables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("generated navmesh metadata source hashes mismatch public navigation inputs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadPathSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("setup->paths = path_count ?") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct padsfileheader") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("struct waypoint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("PADFLAG_HASBBOXDATA") !=
	        std::string::npos);
	// B-1204: a pad keeps its own box bit; forcing it on every pad made the
	// -100..100 default a real box and dropped objects 100 under their pad.
	REQUIRE(scenario_runtime.find("flags |= PADFLAG_HASBBOXDATA;") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("boxed += (rows[i].flags & PADFLAG_HASBBOXDATA) != 0;") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourcePadsGetWideOffsets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_clearSourceWidePadOffsets()") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("sizeof(u32)") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: pads.json runtime uses 32-bit source pad offsets") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("exceeding u16 pad offsets") ==
	        std::string::npos);
	{
		const std::string clear_wide_offsets =
			functionBlock(scenario_runtime, "s_clearSourceWidePadOffsets");
		const std::string get_wide_offsets =
			functionBlock(scenario_runtime, "scenarioSourcePadsGetWideOffsets");
		const std::string build_padfile =
			functionBlock(scenario_runtime, "static u8 *s_buildPadfile(");
		REQUIRE(!clear_wide_offsets.empty());
		REQUIRE(!get_wide_offsets.empty());
		REQUIRE(!build_padfile.empty());
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadFile = NULL;") !=
		        std::string::npos);
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadOffsets = NULL;") !=
		        std::string::npos);
		REQUIRE(clear_wide_offsets.find("g_SourceWidePadOffsetCount = 0;") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("padfiledata == g_SourceWidePadFile") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find(
			        "g_SourceWidePadOffsets && g_SourceWidePadOffsetCount >= 0") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("*out_count = g_SourceWidePadOffsetCount;") !=
		        std::string::npos);
		REQUIRE(get_wide_offsets.find("return g_SourceWidePadOffsets;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("if (wide_offsets)") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadFile = buf;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadOffsets = wide_offsets;") !=
		        std::string::npos);
		REQUIRE(build_padfile.find("g_SourceWidePadOffsetCount = row_count;") !=
		        std::string::npos);
	}
	REQUIRE(pad_runtime_h.find("padGetPackedOffset") != std::string::npos);
	REQUIRE(pad_runtime.find("g_PadOffsets32") != std::string::npos);
	REQUIRE(pad_runtime.find("return g_PadOffsets ? g_PadOffsets[padnum] : 0") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("scenarioSourcePadsGetWideOffsets") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("padSetWideOffsets(wide_offsets)") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("offset = padGetPackedOffset(padnum)") !=
	        std::string::npos);
	REQUIRE(setup_pads.find("offset = g_PadOffsets[padnum]") ==
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadPortalsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioPortalSourceRead(text, text_size, &rows, &row_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.SOURCE: compiled portals.json") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("bgBuildScenarioSourcePortalTables") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("scenarioSourceLoadPortalsForStage(stage") !=
	        std::string::npos);
	REQUIRE(bg_runtime.find("SCENARIO.SOURCE: built native portal tables") !=
	        std::string::npos);
	{
		const std::string bg_source_tables =
			functionBlock(bg_runtime, "bgBuildScenarioSourceTables");
		REQUIRE(bg_source_tables.find("if (g_BgLightsFileData)") !=
		        std::string::npos);
		REQUIRE(bg_source_tables.find("func0f001c0c();") !=
		        std::string::npos);
		REQUIRE(bg_source_tables.find("skipped legacy dynamic-light precompute") !=
		        std::string::npos);
	}
	{
		const std::string portal_distance =
			functionBlock(dlights_runtime, "func0f000920");
		const std::string light_reset =
			functionBlock(dlights_runtime, "func0f002a98");
		const std::string sound_distance =
			functionBlock(dlights_runtime, "func0f0056f4");
		const std::string portal_range =
			functionBlock(dlights_runtime, "func0f0059fc");
		REQUIRE(portal_distance.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("upper >= g_NumPortals") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("var80061430[upper] == NULL") !=
		        std::string::npos);
		REQUIRE(portal_distance.find("return 0xffffffff") !=
		        std::string::npos);
		REQUIRE(light_reset.find("var80061430 = NULL") !=
		        std::string::npos);
		REQUIRE(light_reset.find("g_NumPortals = 0") !=
		        std::string::npos);
		REQUIRE(sound_distance.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(sound_distance.find("g_NumPortals <= 0") !=
		        std::string::npos);
		REQUIRE(portal_range.find("var80061430 == NULL") !=
		        std::string::npos);
		REQUIRE(portal_range.find("g_NumPortals <= 0") !=
		        std::string::npos);
		REQUIRE(portal_range.find("coordsGetDistance(pos1, pos2)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime_h.find("scenarioSourceLoadSetupForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceActivateGraphsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceSetupGraphRecordBehaviorLink") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLoadSetupForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceActivateGraphsForStage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pd2.level.graph.v1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("pd2.mission.graph.v1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadGraphText") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("fsFileLoad(path") != std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: activated level graph") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: table refs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: volume source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+volumes.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLevelGraphCheckPadRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLevelGraphCheckPadRoom") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: trigger volume evaluated from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+objective.status") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_volume_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.trigger.volume.source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: trigger volume nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.trigger.volumes+level.graph.nodes+volumes.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: global settings source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.global.settings+level.graph.nodes") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_global_settings_node_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceLevelGraphRecordTick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime_h.find("scenarioSourceLevelGraphRecordTick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: level tick from graph source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.global.settings+level.tick") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("global settings source scenario mismatch") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadAiListSourceJson") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_loadSpawnSourceRows") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiFindOrAddList(table, ailist_ref") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.spawns_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_ActiveScenarioGraphs.ai_lists_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI list source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.lists+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI list-control actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.list_control+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI alarm actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.alarm+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI savefile flag actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.savefile_flags+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI timer/countdown actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.timer+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: AI HUD message actions") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.hud+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_return_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shot_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.return_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.kneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.surrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.fade_out") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_sidestep") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_attack_stand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_attacking") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_modify_attack") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.face_entity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.apply_gset_damage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_damage_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.consider_grenade_throw") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.drop_item") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_from_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_jog_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_walk_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_to_target_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_go_to_cover_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_jog_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_walk_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_run_to_chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_do_animation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_one_hand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_look_around") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.be_surprised_surrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.random") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_random_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_random_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.control.random+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.print") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.noop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecutePrint") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteNoOp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.debug_noop+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_punch_dodge_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shooting_at_me_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_dark_room_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_player_dead_list") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetShotList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteReturnList") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteStop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteKneel") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFadeOut") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRemoveChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.character_lifecycle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.basic_motion+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action stop chr=%d chr_rows=%d source_chr=%d hovercar=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action kneel chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action surrender chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action fade_out chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteStop");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(",
			"chopperStop(hovercar)");
	}
	{
		const char *actions[][2] = {
			{"scenarioSourceAiGraphExecuteStop", "chrTryStop("},
			{"scenarioSourceAiGraphExecuteKneel", "chrTryKneel("},
			{"scenarioSourceAiGraphExecuteSurrender", "chrTrySurrender("},
			{"scenarioSourceAiGraphExecuteFadeOut", "chrFadeOut("},
		};

		for (const auto &action : actions) {
			const std::string block = functionBlock(scenario_runtime,
				action[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(",
				action[1]);
		}
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.combat+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteFaceEntity") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteApplyGsetDamage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDamageChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteConsiderGrenadeThrow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteDropItem") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunFromTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryJogToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryWalkToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunToTargetProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryGoToCoverProp") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryJogToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryWalkToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteTryRunToChr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.target_movement+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfCanHearAlarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfPatrolling") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfAlarmActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfGasActive") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfHearsTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawInjury") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawDeath") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfLosToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfLosToAttackTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfTargetNearlyInSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfNearlyInTargetsSight") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteSetPadPresetToPadOnRouteToTarget") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeNavigationTables(\n\t\t\t\"set_pad_preset_to_pad_on_route_to_target\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"set_pad_preset_to_pad_on_route_to_target\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_pad_on_route_to_target chr_rows=%d source_chr=%d label=%d pad=%d found=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfSawTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteIfHeardTargetRecently") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.perception_alarm+ai/ailists.json") !=
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
		     "scenarioSourceAiGraphExecuteIfChrActivatedObject",
		     "scenarioSourceAiGraphExecuteObjInteract",
		     "scenarioSourceAiGraphExecuteDestroyObject",
		     "scenarioSourceAiGraphExecuteDropObjectFromChr",
		     "scenarioSourceAiGraphExecuteChrDropItems",
		     "scenarioSourceAiGraphExecuteChrDropWeapon",
		     "scenarioSourceAiGraphExecuteGiveObjectToChr",
		     "scenarioSourceAiGraphExecuteObjectMoveToPad",
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
		     "scenarioSourceAiGraphExecuteSpawnChrAtPad",
		     "scenarioSourceAiGraphExecuteSpawnChrAtChr",
		     "scenarioSourceAiGraphExecuteTryEquipWeapon",
		     "scenarioSourceAiGraphExecuteTryEquipHat",
		     "scenarioSourceAiGraphExecuteSetObjImage",
		     "scenarioSourceAiGraphExecuteObjectDoAnimation",
		     "scenarioSourceAiGraphExecuteSetDoorOpen",
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
		     "scenarioSourceAiGraphExecuteIfChrNotTalking",
		     "scenarioSourceAiGraphExecuteIfOrders",
		     "scenarioSourceAiGraphExecuteIfHasOrders",
		     "scenarioSourceAiGraphExecuteIfChrInSquadronDoingAction",
		     "scenarioSourceAiGraphExecuteIfChrListening",
		     "scenarioSourceAiGraphExecuteIfNotListening",
		     "scenarioSourceAiGraphExecuteIfChrInjuredTarget",
		     "scenarioSourceAiGraphExecuteIfAction",
		     "scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan",
		     "scenarioSourceAiGraphExecuteIfChrTarget",
		     "scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam",
		     "scenarioSourceAiGraphExecuteIfHuman",
		     "scenarioSourceAiGraphExecuteIfSkedar",
		     "scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget",
		     "scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset",
		     "scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan",
		     "scenarioSourceAiGraphExecuteSetTarget",
		     "scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget",
		     "scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf",
		     "scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad",
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
		REQUIRE(scenario_runtime.find(symbol) != std::string::npos);
	}
	{
		const std::string destroy_object =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteDestroyObject");
		REQUIRE(destroy_object.find("OBJFLAG_01000000") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("OBJHFLAG_DELETING") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("OBJH2FLAG_DESTROYED") !=
		        std::string::npos);
		REQUIRE(destroy_object.find("objectiveRecordObjectState(obj)") !=
		        std::string::npos);
	}
	{
		const std::string set_door_open = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDoorOpen");
		REQUIRE(!set_door_open.empty());
		requireTokenOrder(set_door_open,
			"s_aiSetupSpawnGraphReady(\"set_door_open\"",
			"s_aiGraphRequireRuntimeObjectTagType(\"set_door_open\"");
		requireTokenOrder(set_door_open,
			"s_aiGraphRequireRuntimeObjectTagType(\"set_door_open\"",
			"obj = objFindByTagId(tag_id)");
		requireTokenOrder(set_door_open,
			"obj = objFindByTagId(tag_id)",
			"door->lastopen60 = g_Vars.lvframe60");
		requireTokenOrder(set_door_open,
			"door->lastopen60 = g_Vars.lvframe60",
			"AI action set_door_open tag=%d object_rows=%d applied=%d");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.spatial_perception+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.distance_perception+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.room_object_weapon+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.condition.spatial_perception+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.condition.room_object_weapon+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_room_is_on_screen chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_chr_in_room chr=%d room_type=%d pad=%d found=%d pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_target_in_room pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfRoomIsOnScreen");
		REQUIRE(!block.empty());
		// B-1163: the room argument is chrGetPadRoom's room-or-pad value,
		// decoded and checked as native indexes it, not checked as a pad.
		REQUIRE(block.find(
			        "s_aiGraphResolveRoomOrPad(\"if_room_is_on_screen\", chr, pad,") !=
		        std::string::npos);
		REQUIRE(block.find(
			        "s_aiGraphRequireRuntimePad(\"if_room_is_on_screen\"") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"if_room_is_on_screen\"",
			"bgRoomIsOnscreen(room_id)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInRoom");
		REQUIRE(!block.empty());
		// B-1163 (type 0 room-or-pad) and B-1164 (type 2: each eyespy
		// resolves the preset pad itself, as chrGetDistanceToPad does).
		REQUIRE(block.find("s_aiGraphResolveRoomOrPad(\"if_chr_in_room\", basechr,") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePad(\"if_chr_in_room\"") ==
		        std::string::npos);
		REQUIRE(block.find("g_Vars.players[i]->eyespy") ==
		        std::string::npos);
		REQUIRE(block.find(
			        "AI condition if_chr_in_room chr=%d room_type=%d pad=%d found=%d pad_rows=%d checked_players=%d") !=
		        std::string::npos);
		REQUIRE(block.find("stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"if_chr_in_room\"",
			"chr->prop->rooms[0] == room");
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"if_chr_in_room\"",
			"stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING");
		requireTokenOrder(block,
			"s_aiGraphRequireResolvedRuntimePad(\"if_chr_in_room\"",
			"chrGetDistanceToPad(eyespychr, padnum)");
		requireTokenOrder(block,
			"stageGetIndex(g_Vars.stagenum) == STAGEINDEX_G5BUILDING",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"",
			"player->eyespy");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_in_room\"",
			"chrGetDistanceToPad(");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetInRoom");
		REQUIRE(!block.empty());
		REQUIRE(block.find(
			        "s_aiGraphResolveRoomOrPad(\"if_target_in_room\", chr, padnum,") !=
		        std::string::npos);
		REQUIRE(block.find(
			        "s_aiGraphRequireRuntimePad(\"if_target_in_room\"") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveRoomOrPad(\"if_target_in_room\"",
			"room == prop->rooms[0]");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.object_interaction+ai/ailists.json+objects.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeObjectTag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeSetupObjectRows") !=
	        std::string::npos);
	REQUIRE(countOccurrences(scenario_runtime,
		        "if (!s_ActiveScenarioGraphs.objects_path[0]) {\n"
		        "\t\ts_aiGraphRuntimeFailure(action, \"missing objects.json source\");\n"
		        "\t\treturn 0;\n"
		        "\t}\n\n"
		        "\tobject_count = s_countRuntimeSetupObjectRows();") >= 1);
	REQUIRE(scenario_runtime.find("missing source-derived runtime setup object table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime setup object tag %d does not point at a source row") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimePad(\"object_move_to_pad\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action object_move_to_pad tag=%d object_rows=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.quadrant_preset+ai/ailists.json+pads.json+navigation.generate") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeNavigationTables") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeWaypoints") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countRuntimeWaygroups") !=
	        std::string::npos);
	{
		const std::string count_paths =
			functionBlock(scenario_runtime, "s_countRuntimePaths");
		const std::string count_pads =
			functionBlock(scenario_runtime, "s_countRuntimePads");
		const std::string count_covers =
			functionBlock(scenario_runtime, "s_countRuntimeCovers");
		const std::string count_objects =
			functionBlock(scenario_runtime, "s_countRuntimeSetupObjectRows");
		const std::string count_chrs =
			functionBlock(scenario_runtime, "s_countRuntimeSetupChrRows");
		const std::string source_chr_refs =
			functionBlock(scenario_runtime, "s_activeGraphSetSourceSetupChrRefs");
		const std::string contains_source_record =
			functionBlock(scenario_runtime, "s_sourceSetupContainsRecordOrder");
		const std::string contains_object =
			functionBlock(scenario_runtime, "s_runtimeSetupContainsObjectRow");
		const std::string contains_chr =
			functionBlock(scenario_runtime, "s_runtimeSetupContainsChrnum");
		const std::string contains_source_chr =
			functionBlock(scenario_runtime, "s_aiGraphSetupSourceContainsChrnum");
		const std::string count_waypoints =
			functionBlock(scenario_runtime, "s_countRuntimeWaypoints");
		const std::string count_waygroups =
			functionBlock(scenario_runtime, "s_countRuntimeWaygroups");
		REQUIRE(!count_paths.empty());
		REQUIRE(!count_pads.empty());
		REQUIRE(!count_covers.empty());
		REQUIRE(!count_objects.empty());
		REQUIRE(!count_chrs.empty());
		REQUIRE(!source_chr_refs.empty());
		REQUIRE(!contains_source_record.empty());
		REQUIRE(!contains_object.empty());
		REQUIRE(!contains_chr.empty());
		REQUIRE(!contains_source_chr.empty());
		REQUIRE(!count_waypoints.empty());
		REQUIRE(!count_waygroups.empty());
		REQUIRE(count_paths.find("if (!g_StageSetup.paths)") !=
		        std::string::npos);
		REQUIRE(count_paths.find("g_StageSetup.paths[count].pads") !=
		        std::string::npos);
		REQUIRE(count_pads.find("!g_StageSetup.padfiledata") !=
		        std::string::npos);
		REQUIRE(count_pads.find("g_PadsFile->numpads <= 0") !=
		        std::string::npos);
		REQUIRE(count_pads.find("return g_PadsFile->numpads;") !=
		        std::string::npos);
		REQUIRE(count_covers.find("!g_StageSetup.padfiledata") !=
		        std::string::npos);
		REQUIRE(count_covers.find("!g_StageSetup.cover") !=
		        std::string::npos);
		REQUIRE(count_covers.find("g_PadsFile->numcovers <= 0") !=
		        std::string::npos);
		REQUIRE(count_covers.find("return g_PadsFile->numcovers;") !=
		        std::string::npos);
		REQUIRE(count_objects.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(count_objects.find("obj->type != OBJTYPE_END") !=
		        std::string::npos);
		REQUIRE(count_chrs.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(count_chrs.find("obj->type == OBJTYPE_CHR") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_chr_count++") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_chr_refs") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_record_count++") !=
		        std::string::npos);
		REQUIRE(source_chr_refs.find("source_setup_record_refs") !=
		        std::string::npos);
		REQUIRE(contains_source_record.find("source_setup_record_refs") !=
		        std::string::npos);
		REQUIRE(contains_object.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(contains_object.find("if (obj == needle)") !=
		        std::string::npos);
		REQUIRE(contains_chr.find("g_StageSetup.props") !=
		        std::string::npos);
		REQUIRE(contains_chr.find("packed->chrnum == chrnum") !=
		        std::string::npos);
		REQUIRE(contains_source_chr.find("s_sourceSetupContainsChrnum(chrnum)") !=
		        std::string::npos);
		REQUIRE(contains_source_chr.find("s_runtimeSetupContainsChrnum(chrnum)") !=
		        std::string::npos);
		REQUIRE(count_waypoints.find("if (!g_StageSetup.waypoints)") !=
		        std::string::npos);
		REQUIRE(count_waypoints.find(
			        "g_StageSetup.waypoints[count].padnum >= 0") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("if (!g_StageSetup.waygroups)") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("g_StageSetup.waygroups[count].waypoints") !=
		        std::string::npos);
		REQUIRE(count_waygroups.find("g_StageSetup.waygroups[count].neighbours") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("missing source-derived runtime waypoint table") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing source-derived runtime waygroup table") !=
	        std::string::npos);
	{
		const std::string nav_tables =
			functionBlock(scenario_runtime,
				"s_aiGraphRequireRuntimeNavigationTables");
		REQUIRE(!nav_tables.empty());
		requireTokenOrder(nav_tables,
			"missing pads.json source", "s_countRuntimePads()");
		requireTokenOrder(nav_tables,
			"missing navigation/waypoints.json source",
			"s_countRuntimeWaypoints()");
		requireTokenOrder(nav_tables,
			"missing navigation/waygroups.json source",
			"s_countRuntimeWaygroups()");
	}
	REQUIRE(scenario_runtime.find("pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_waypoint_within_quadrant quadrant=%d label=%d pad=%d found=%d result=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	{
		const std::string quadrant_condition =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteIfWaypointWithinQuadrant");
		REQUIRE(!quadrant_condition.empty());
		REQUIRE(quadrant_condition.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"if_waypoint_within_quadrant\"") !=
		        std::string::npos);
		requireTokenOrder(quadrant_condition,
			"func0f04a4ec(chr, (u8)quadrant)",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(quadrant_condition,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphApplyBranch(branch_taken, label, 4)");
	}
	REQUIRE(scenario_runtime.find("AI action set_pad_preset_to_target_quadrant quadrant=%d label=%d pad=%d found=%d result=%d pad_rows=%d waypoint_rows=%d waygroup_rows=%d") !=
	        std::string::npos);
	{
		const std::string quadrant_block =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSetPadPresetToTargetQuadrant");
		REQUIRE(!quadrant_block.empty());
		REQUIRE(quadrant_block.find("s_aiGraphRequireRuntimePad(\n\t\t\t\t\"set_pad_preset_to_target_quadrant\"") !=
		        std::string::npos);
		requireTokenOrder(quadrant_block,
			"chrSetPadPresetToWaypointWithinTargetQuadrant",
			"s_aiGraphRequireRuntimePad(");
		requireTokenOrder(quadrant_block,
			"s_aiGraphRequireRuntimePad(",
			"s_aiGraphApplyBranch(branch_taken, label, 4)");
	}
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_weapon_state+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.player_cutscene+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.setup_spawn+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.entity_lifecycle+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.release_cover+navigation/covers.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.orders+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.condition.intent_status+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteChrDoAnimation") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedOneHand") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedLookAround") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteBeSurprisedSurrender") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.animation+ai/ailists.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_do_animation chr=%d chr_rows=%d target_chr=%d player_checked=%d anim_id=%s anim_source=%s clip_bytes=%u target=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_one_hand chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_look_around chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action be_surprised_surrender chr=%d chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_list target=%d chr_rows=%d target_chr=%d list=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_return_list target=%d chr_rows=%d source_chr=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d list=%u") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("s_countSourceBackedMpParticipantChrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("level_global_settings_kind,\n\t\t\t\t\"mp\"") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("g_Vars.normmplayerisrunning && g_MpNumChrs > 0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("g_MpAllChrPtrs[i] == chr") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_count = s_ActiveScenarioGraphs.source_setup_chr_count") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("chr_count +=\n\t\ts_countSourceBackedMpParticipantChrs()") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_shot_list chr_rows=%d source_chr=%d list=%u applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action return_list chr_rows=%d source_chr=%d vehicle_rows=%d source_vehicle_type=%d list=%u") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrDoAnimation");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		REQUIRE(block.find("g_Vars.players[playernum]") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveAnimationCatalogId(\"chr_do_animation\"",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"playerPresentationCutsceneInProgress()",
			"g_CutsceneFrameOverrun240 * speed * 0.25f");
		requireTokenOrder(block,
			"g_CutsceneFrameOverrun240 * speed * 0.25f",
			"AI action chr_do_animation chr=%d chr_rows=%d target_chr=%d player_checked=%d anim_id=%s anim_source=%s clip_bytes=%u");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"chrTryStartAnim(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->vv_ground");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"player->vv_manground");
	}
	{
		const char *actions[][2] = {
			{"scenarioSourceAiGraphExecuteBeSurprisedOneHand",
				"chrTrySurprisedOneHand("},
			{"scenarioSourceAiGraphExecuteBeSurprisedLookAround",
				"chrTrySurprisedLookAround("},
			{"scenarioSourceAiGraphExecuteBeSurprisedSurrender",
				"chrTrySurprisedSurrender("},
		};

		for (const auto &action : actions) {
			const std::string block = functionBlock(scenario_runtime,
				action[0]);
			REQUIRE(!block.empty());
			REQUIRE(block.find("chr_rows=%d source_chr=%d") !=
			        std::string::npos);
			requireTokenOrder(block,
				"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
				action[1]);
		}
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("ailistFindById(list_id)") !=
		        std::string::npos);
		REQUIRE(block.find("target_preset & 0xff") != std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireListControlNode(\"set_list\"",
			"ailistFindById(list_id)");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"chr->ailist = ailist");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetReturnList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("target_preset == CHR_SELF") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireOptionalRuntimeCharacterPointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("g_Vars.truck->") == std::string::npos);
		REQUIRE(block.find("g_Vars.heli->") == std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterPointer(",
			"active_chr->aireturnlist = list_id");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (chr) {\n\t\t\t\tchr->aireturnlist = list_id");
		REQUIRE(block.find("if (chr && chr->prop)") ==
		        std::string::npos);
		REQUIRE(block.find(
			"if (chr) {\n\t\t\t\tchr->aireturnlist = list_id;\n\t\t\t\tapplied = 1;") !=
		        std::string::npos);
		requireTokenOrder(block,
			"OBJTYPE_TRUCK, -1, &vehicle_count",
			"truck->aireturnlist = list_id");
		requireTokenOrder(block,
			"OBJTYPE_HELI, -1, &vehicle_count",
			"heli->aireturnlist = list_id");
		requireTokenOrder(block,
			"OBJTYPE_HOVERCAR, OBJTYPE_CHOPPER, &vehicle_count",
			"hovercar->aireturnlist = list_id");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteReturnList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("ailistFindById(list_id)") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("g_Vars.truck->") == std::string::npos);
		REQUIRE(block.find("g_Vars.heli->") == std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireListControlNode(\"return_list\"",
			"ailistFindById(list_id)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"return_list\"",
			"list_id = chr->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_TRUCK,",
			"list_id = (u16)truck->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_HELI,",
			"list_id = (u16)heli->aireturnlist");
		requireTokenOrder(block,
			"OBJTYPE_HOVERCAR, OBJTYPE_CHOPPER",
			"list_id = (u16)hovercar->aireturnlist");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetShotList");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.chrdata->") == std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_shot_list\"",
			"chr->aishotlist = list_id");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"static s32 s_aiGraphExecuteSetChrListField");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s16 *field, u16 list_id") ==
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterStatePointer(action, chr",
			"field = (s16 *)((u8 *)chr + field_offset)");
		requireTokenOrder(block,
			"field = (s16 *)((u8 *)chr + field_offset)",
			"*field = (s16)list_id");
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSetPunchDodgeList",
		     "scenarioSourceAiGraphExecuteSetShootingAtMeList",
		     "scenarioSourceAiGraphExecuteSetDarkRoomList",
		     "scenarioSourceAiGraphExecuteSetPlayerDeadList",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(!block.empty());
		REQUIRE(block.find("&g_Vars.chrdata->") == std::string::npos);
		REQUIRE(block.find("offsetof(struct chrdata") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("AI action chr_set_listening chr=%d chr_rows=%d target_chr=%d value=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_listening chr=%d chr_rows=%d target_chr=%d listening=%d check_convtalk=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_injured_target chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_ammo_quantity_less_than chr=%d chr_rows=%d target_chr=%d player_checked=%d ammo_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_chr_target chr=%d chr_rows=%d resolved_chr=%d target_chr=%d target_chr_rows=%d resolved_target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_compare_chr_presets_team chr_rows=%d preset_chr=%d comparison=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_human chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(",
			"bgunGetAmmoCount(");
	}
	REQUIRE(scenario_runtime.find("AI condition if_skedar chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_prop_preset_blocking_sight_to_target chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action remove_object_at_prop_preset chr_rows=%d source_chr=%d cleared=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_prop_preset_height_less_than chr_rows=%d source_chr=%d value=%.3f height=%.3f") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_target chr_rows=%d source_chr=%d target_chr_rows=%d target_chr=%d resolved_target_chr=%d vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_presets_target_is_not_my_target chr_rows=%d source_chr=%d preset_target=%d target=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_chr_near_self chr_rows=%d source_chr=%d preset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_chr_preset_to_chr_near_pad chr_rows=%d source_chr=%d preset=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_dangerous_object_nearby chr_rows=%d source_chr=%d flags=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_heli_weapons_armed vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s vehicle_rows=%d source_vehicle_type=%d armed=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action hovercar_begin_path path=%d found=1 vehicle_rows=%d truck_type=%d hovercar_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_vehicle_speed vehicle_rows=%d truck_type=%d hovercar_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_rotor_speed vehicle_rows=%d source_vehicle_type=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing objects.json source for vehicle weapon state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime chopper pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source type %u for chopper weapon state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("missing objects.json source for vehicle state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("runtime vehicle pointer is not source-derived") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("expected source vehicle type") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteChrSetListening",
		     "scenarioSourceAiGraphExecuteIfChrListening",
		     "scenarioSourceAiGraphExecuteIfChrInjuredTarget",
		     "scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan",
		     "scenarioSourceAiGraphExecuteIfChrTarget",
		     "scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam",
		     "scenarioSourceAiGraphExecuteIfHuman",
		     "scenarioSourceAiGraphExecuteIfSkedar",
	     }) {
		const std::string block = functionBlock(scenario_runtime, symbol);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPropPresetIsBlockingSightToTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrIsPropPresetBlockingSightToTarget(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRemoveObjectAtPropPreset");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=graph.ai.action.prop_target+ai/ailists.json+objects.json") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePropPresetTargetNode(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 = -1");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPropPresetHeightLessThan");
		REQUIRE(!block.empty());
		REQUIRE(block.find("objects=%s backend=graph.ai.condition.prop_target+ai/ailists.json+objects.json") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequirePropPresetTargetNode(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->proppreset1 >= 0");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"struct prop *prop = &g_Vars.props[chr->proppreset1];");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"propGetBbox(prop");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"if (newtarget != chr->target)");
		requireTokenOrder(block,
			"s_aiGraphRequireOptionalRuntimeCharacterStatePointer(\"set_target\"",
			"goto log_result;");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\n\t\t\t\t\"set_target\", &hovercar->base",
			"chopperSetTarget(hovercar, chrnum)");
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") !=
		        std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfPresetsTargetIsNotMyTarget");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chr->chrpreset1 != -1");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"branch_taken = chr && chr->target");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearSelf");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrSetChrPresetToChrNearSelf");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad");
		REQUIRE(!block.empty());
		// B-1164: the pad is checked as the native helper resolves it.
		requireTokenOrder(block,
			"s_aiGraphRequireResolvedRuntimePad(",
			"s_aiGraphRequireRuntimeCharacterPointer(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrSetChrPresetToChrNearPad");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfDangerousObjectNearby");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterPointer(",
			"chrDetectDangerousObject(chr");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrSetListening");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->listening = (u8)listening");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrListening");
		REQUIRE(block.find("if (!check_convtalk)") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->listening == listening");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrInjuredTarget");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->chrflags &= ~CHRCFLAG_INJUREDTARGET");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrAmmoQuantityLessThan");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"playermgrGetPlayerNumByProp");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrTarget");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetTargetProp(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCompareChrPresetsTeam");
		REQUIRE(block.find("chrSetChrPreset(chr, CHR_BOND)") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrCompareTeams(preset_chr, chr, comparison)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfHuman");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"CHRRACE(chr) == RACE_HUMAN");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfSkedar");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"CHRRACE(chr) == RACE_SKEDAR");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTarget");
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(",
			"if (!target_chr)");
		requireTokenOrder(block,
			"if (!target_chr)",
			"propGetIndexByChrId(chr, chrnum)");
		REQUIRE(block.find("newtarget = target_chr ? target_chr->target : -1") !=
		        std::string::npos);
		REQUIRE(block.find("chopperSetTarget(hovercar, chrnum)") !=
		        std::string::npos);
	}
	REQUIRE(scenario_runtime.find("s_aiGraphRequireRuntimeCharacterPointer") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_target_moving_slowly chr=%d chr_rows=%d target_chr=%d delta=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI condition if_y chr=%d chr_rows=%d target_chr=%d vehicle_rows=%d source_vehicle_type=%d y=%.3f") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_explosions chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_adjust_motion_blur chr=%d chr_rows=%d target_chr=%d amount=%d mode=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action chr_emit_sparks chr=%d chr_rows=%d target_chr=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action set_dr_caroll_images chr=%d chr_rows=%d target_chr=%d right=%d left=%d") !=
	        std::string::npos);
	for (const char *symbol : {
		     "scenarioSourceAiGraphExecuteIfTargetMovingSlowly",
		     "scenarioSourceAiGraphExecuteIfY",
		     "scenarioSourceAiGraphExecuteChrExplosions",
		     "scenarioSourceAiGraphExecuteChrAdjustMotionBlur",
		     "scenarioSourceAiGraphExecuteChrEmitSparks",
		     "scenarioSourceAiGraphExecuteSetDrCarollImages",
	     }) {
		const std::string block = functionBlock(scenario_runtime, symbol);
		REQUIRE(!block.empty());
		REQUIRE(block.find("chrFindById(") == std::string::npos);
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDrCarollImages");
		REQUIRE(!block.empty());
		requireTokenOrder(block,
			"s_aiGraphRequireMiscEffectNode(\"set_dr_caroll_images\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"g_Vars.lvframenum % 4");
		requireTokenOrder(block,
			"g_Vars.lvframenum % 4",
			"AI action set_dr_caroll_images chr=%d chr_rows=%d target_chr=%d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfY");
		REQUIRE(!block.empty());
		REQUIRE(block.find("struct chopperobj *hovercar = g_Vars.hovercar;") !=
		        std::string::npos);
		REQUIRE(block.find("g_Vars.hovercar->") == std::string::npos);
		REQUIRE(block.find("chopperFromHovercar(g_Vars.hovercar)") ==
		        std::string::npos);
		REQUIRE(block.find("vehicle_rows=%d source_vehicle_type=%d") !=
		        std::string::npos);
		REQUIRE(block.find("objects=%s backend=%s") != std::string::npos);
		requireTokenOrder(block,
			"struct chopperobj *hovercar = g_Vars.hovercar;",
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\"if_y\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeVehicleObjectPointer(\"if_y\"",
			"chopperFromHovercar(hovercar)");
		requireTokenOrder(block,
			"chopperFromHovercar(hovercar)",
			"chopperGetTargetProp(chopper)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfTargetMovingSlowly");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterPointer(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrGetDistanceLostToTargetInLastSecond(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfY");
		REQUIRE(block.find("s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"if_y\"") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveOptionalRuntimeCharacterRefOrSelector(\"if_y\"",
			"chr->prop->pos.y");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrExplosions");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"") !=
		        std::string::npos);
		REQUIRE(block.find("player_checked=%d") != std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"playermgrGetPlayerNumByProp");
		requireTokenOrder(block,
			"playermgrGetPlayerNumByProp",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_explosions\"",
			"playerSurroundWithExplosions");
	}
	{
		const std::string misc_effect_helper = functionBlock(scenario_runtime,
			"s_aiGraphRequireMiscEffectNode");
		REQUIRE(!misc_effect_helper.empty());
		REQUIRE(misc_effect_helper.find("missing ai/ailists.json source") !=
		        std::string::npos);
		const std::string set_tinted_glass = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetTintedGlassEnabled");
		REQUIRE(!set_tinted_glass.empty());
		REQUIRE(set_tinted_glass.find(
			        "AI action set_tinted_glass_enabled enabled=%d") !=
		        std::string::npos);
		requireTokenOrder(set_tinted_glass,
			"s_aiGraphRequireMiscEffectNode(\"set_tinted_glass_enabled\"",
			"g_TintedGlassEnabled = enabled");
		requireTokenOrder(set_tinted_glass,
			"g_TintedGlassEnabled = enabled",
			"AI action set_tinted_glass_enabled enabled=%d");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrAdjustMotionBlur");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chr->blurdrugamount");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteChrEmitSparks");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrDrCarollEmitSparks(chr)");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetDrCarollImages");
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"drcaroll->drcarollimage_left");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"drcaroll->drcarollimage_right");
	}
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: pad source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.pads+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.jog_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.go_to_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.walk_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.run_to_pad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteWalkToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenarioSourceAiGraphExecuteRunToPad") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.jog_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.go_to_pad_preset+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.walk_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.ai.action.run_to_pad+pads.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("AI action %s chr_rows=%d source_chr=%d pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	// B-1119: walk, jog and run to pad resolve pad 9000 (PAD_PRESET) to the
	// character's preset pad before checking it, as chrGoToPad does; a refused
	// pad also skips the native handler, so the character stood still.
	REQUIRE(scenario_runtime.find("resolved_pad = pad == PAD_PRESET ? chrResolvePadId(chr, pad) : pad;") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("if (!s_aiGraphRequireRuntimePad(action, resolved_pad, &pad_count)) {") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("if (!s_aiGraphRequireRuntimePad(action, pad, &pad_count)) {") ==
	        std::string::npos);
	// B-1161: if_natural_anim only compares animation numbers, so an empty
	// base slot (ANIM_IDLE, where chr->naturalanim starts) is valid there; the
	// strict resolver still refuses it for actions that play an animation.
	{
		// The slot test lives in the animation library, so the runtime reads
		// no animation table global itself (source-boundary guard).
		const std::string anim = readTextFile("src/lib/anim.c");
		const size_t empty = anim.find("bool animIsEmptyBaseSlot(s32 animnum)");
		REQUIRE(empty != std::string::npos);
		REQUIRE(anim.find("&& g_Anims[animnum].numframes == 0\n\t\t&& g_Anims[animnum].headerlen == 0;", empty) !=
		        std::string::npos);
		REQUIRE(readTextFile("src/include/lib/anim.h").find("bool animIsEmptyBaseSlot(s32 animnum);") !=
		        std::string::npos);
		REQUIRE(scenario_runtime.find("if (animIsEmptyBaseSlot(anim_id)) {") != std::string::npos);
		REQUIRE(scenario_runtime.find("g_Anims") == std::string::npos);
		const size_t natural = scenario_runtime.find("s32 scenarioSourceAiGraphExecuteIfNaturalAnim(struct chrdata *chr,");
		REQUIRE(natural != std::string::npos);
		const size_t naturalEnd = scenario_runtime.find("s32 scenarioSourceAiGraphExecuteIfY(", natural);
		REQUIRE(naturalEnd != std::string::npos);
		const std::string body = scenario_runtime.substr(natural, naturalEnd - natural);
		size_t comparable = 0;
		for (size_t at = body.find("s_aiGraphResolveComparableAnimationCatalogId("); at != std::string::npos;
		     at = body.find("s_aiGraphResolveComparableAnimationCatalogId(", at + 1)) {
			++comparable;
		}
		REQUIRE(comparable == 2);
		REQUIRE(body.find("s_aiGraphResolveAnimationCatalogId(") == std::string::npos);
		const size_t play = scenario_runtime.find("anim_catalog_id = s_aiGraphResolveAnimationCatalogId(\"chr_do_animation\",");
		REQUIRE(play != std::string::npos);
	}
	REQUIRE(scenario_runtime.find("SCENARIO.GRAPH: path source") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("backend=graph.navigation.paths+navigation/paths.json") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_path") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.start_patrol") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.try_start_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.activate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.deactivate_alarm") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_copy_pad_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_preset") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_target") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_add_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.subtract_morale") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_add_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.subtract_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.increase_squadron_alertness") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_hear_distance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_view_distance") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_grenade_probability") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chr_num") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_max_damage") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.add_health") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_shield") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_reaction_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_recovery_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_accuracy") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_unarmed_dodge_rating") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_stage_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_stage_flag_eq") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_chrflag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_unset_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_chr_has_hidden_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unset_obj_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_obj_has_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.open_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.close_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_door_state") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_object_is_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.lock_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.unlock_door") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_door_locked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_lift_stationary") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.lift_go_to_stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_lift_at_stop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.activate_lift") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.if_using_lift") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_rain") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_snow") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.switch_to_alt_sky") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_wind_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_lights") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_room_flag") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.show_cutscene_chrs") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.configure_environment") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_target2_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_target2_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_sound_from_prop") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_temporary_primary_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.play_x_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.stop_ambient_track") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_draw_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_draw_weapon_in_cutscene") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_player_force_speed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_is_invincible") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_chr_has_no_gun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_delete_weapon") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_trigger_shot_list") !=
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
		     "scenario.ai.action.duplicate_chr",
		     "scenario.ai.action.enable_chr",
		     "scenario.ai.action.disable_chr",
		     "scenario.ai.action.enable_obj",
		     "scenario.ai.action.disable_obj",
		     "scenario.ai.action.chr_move_to_pad",
		     "scenario.ai.action.chr_set_team",
		     "scenario.ai.action.damage_chr_by_amount",
		     "scenario.ai.action.do_preset_animation",
		     "scenario.ai.condition.if_player_chr_portal_distance_less_than",
		     "scenario.ai.condition.if_chr_reposition_valid",
	}) {
		REQUIRE(scenario_runtime.find(node) != std::string::npos);
	}
	REQUIRE(scenario_runtime.find(
		        "AI action prepare_warp_orbit pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action set_camera_animation anim_id=%s anim_source=%s clip_bytes=%u player_checked=%d chr_rows=%d source_chr=%d yielded=1") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action set_camera_animation anim_id=%s anim_source=%s clip_bytes=%u player_checked=%d chr_rows=%d source_chr=%d yielded=0") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action players_fade_out checked_players=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action player_fade_in chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action revoke_control chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action grant_control chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_colour_fade_complete chr=%d chr_rows=%d target_chr=%d player_checked=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action enable_obj tag=%d object_rows=%d player_checked=%d applied=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action clear_inventory checked_players=%d players=%d") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI action kill_bond player_checked=1 applied=1") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteIfCutsceneButtonPressed");
		REQUIRE(!block.empty());
		REQUIRE(block.find("g_Vars.stagenum == STAGE_CITRAINING") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiPlayerCutsceneGraphReady(\"if_cutscene_button_pressed\"",
			"g_Vars.stagenum == STAGE_CITRAINING");
		requireTokenOrder(block,
			"playerAnyCutsceneSkipRequested()",
			"g_Vars.stagenum == STAGE_CITRAINING");
	}
	{
		const std::string end_level = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEndLevel");
		REQUIRE(!end_level.empty());
		requireTokenOrder(end_level,
			"s_aiPlayerCutsceneGraphReady(\"end_level\"",
			"if (g_Vars.autocutplaying)");
		requireTokenOrder(end_level,
			"if (g_Vars.autocutplaying)",
			"g_Vars.autocutfinished = true");
		requireTokenOrder(end_level,
			"g_Vars.autocutfinished = true",
			"AI action end_level autocut=%d");
	}
	{
		const std::string disable_obj = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteDisableObj");
		REQUIRE(!disable_obj.empty());
		requireTokenOrder(disable_obj,
			"s_aiEntityLifecycleGraphReady(\"disable_obj\"",
			"s_aiGraphRequireRuntimeObjectTag(\"disable_obj\"");
		requireTokenOrder(disable_obj,
			"s_aiGraphRequireRuntimeObjectTag(\"disable_obj\"",
			"obj = objFindByTagId(tag_id)");
		requireTokenOrder(disable_obj,
			"obj = objFindByTagId(tag_id)",
			"if (g_Vars.autocutplaying &&");
		requireTokenOrder(disable_obj,
			"if (g_Vars.autocutplaying &&",
			"AI action disable_obj tag=%d object_rows=%d applied=%d");
	}
	{
		const std::string player_pointer = functionBlock(
			scenario_runtime,
			"s_aiGraphRequireRuntimePlayerPointer");
		REQUIRE(!player_pointer.empty());
		REQUIRE(player_pointer.find("missing %s player state") !=
		        std::string::npos);
		REQUIRE(player_pointer.find("PROPTYPE_PLAYER") !=
		        std::string::npos);
	}
	{
		const std::string set_camera_animation = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteSetCameraAnimation");
		REQUIRE(!set_camera_animation.empty());
		REQUIRE(set_camera_animation.find("g_Vars.currentplayer->haschrbody") ==
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "s_aiGraphRequireRuntimePlayerSlot(\"set_camera_animation\"") !=
		        std::string::npos);
		REQUIRE(set_camera_animation.find("g_Vars.players[") ==
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"") !=
		        std::string::npos);
		REQUIRE(set_camera_animation.find("g_Vars.chrdata->sleep") ==
		        std::string::npos);
		REQUIRE(set_camera_animation.find(
			        "struct chrdata *active_chr = g_Vars.chrdata;") !=
		        std::string::npos);
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimePlayerSlot(\"set_camera_animation\"",
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"");
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"",
			"playerStartCutsceneForPresentation(");
		requireTokenOrder(set_camera_animation,
			"s_aiGraphRequireRuntimeCharacterStatePointer(\"set_camera_animation\"",
			"active_chr->sleep = -1");
	}
	{
		const std::string players_fade_out = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecutePlayersFadeOut");
		REQUIRE(!players_fade_out.empty());
		requireTokenOrder(players_fade_out,
			"s_aiGraphRequireRuntimePlayerSlot(\"players_fade_out\"",
			"playerSetFadeColour(");
		requireTokenOrder(players_fade_out,
			"s_aiGraphRequireRuntimePlayerSlot(\"players_fade_out\"",
			"playerSetFadeFrac(");
	}
	{
		const std::string player_fade_in = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayerFadeIn");
		REQUIRE(!player_fade_in.empty());
		requireTokenOrder(player_fade_in,
			"s_aiGraphResolvePlayerChr(\"player_fade_in\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"playerSetFadeColour(");
		requireTokenOrder(player_fade_in,
			"s_aiGraphRequireRuntimePlayerSlot(\"player_fade_in\"",
			"playerSetFadeFrac(");
	}
	{
		const std::string revoke_control = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteRevokeControl");
		REQUIRE(!revoke_control.empty());
		REQUIRE(revoke_control.find("g_PlayersWithControl[g_Vars.currentplayernum]") ==
		        std::string::npos);
		requireTokenOrder(revoke_control,
			"s_aiGraphResolvePlayerChr(\"revoke_control\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"");
		requireTokenOrder(revoke_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(revoke_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"revoke_control\"",
			"g_PlayersWithControl[playernum] = false");
	}
	{
		const std::string grant_control = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteGrantControl");
		REQUIRE(!grant_control.empty());
		REQUIRE(grant_control.find("g_PlayersWithControl[g_Vars.currentplayernum]") ==
		        std::string::npos);
		requireTokenOrder(grant_control,
			"s_aiGraphResolvePlayerChr(\"grant_control\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"");
		requireTokenOrder(grant_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"",
			"setCurrentPlayerNum(playernum)");
		requireTokenOrder(grant_control,
			"s_aiGraphRequireRuntimePlayerSlot(\"grant_control\"",
			"g_PlayersWithControl[playernum] = true");
	}
	{
		const std::string if_colour_fade_complete = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfColourFadeComplete");
		REQUIRE(!if_colour_fade_complete.empty());
		REQUIRE(if_colour_fade_complete.find(
			        "g_Vars.players[playernum]->colourfadetimemax60") ==
		        std::string::npos);
		requireTokenOrder(if_colour_fade_complete,
			"s_aiGraphResolvePlayerChr(\"if_colour_fade_complete\"",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_colour_fade_complete\"");
		requireTokenOrder(if_colour_fade_complete,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_colour_fade_complete\"",
			"player->colourfadetimemax60");
	}
	{
		const std::string enable_obj = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteEnableObj");
		REQUIRE(!enable_obj.empty());
		REQUIRE(enable_obj.find("g_Vars.currentplayer->eyespy") ==
		        std::string::npos);
		REQUIRE(enable_obj.find(
			        "struct player *current_player = g_Vars.currentplayer;") !=
		        std::string::npos);
		REQUIRE(enable_obj.find("\n\t\t\t\tplayer = g_Vars.currentplayer;") ==
		        std::string::npos);
		requireTokenOrder(enable_obj,
			"s_aiGraphRequireRuntimeObjectTag(\"enable_obj\"",
			"s_aiGraphRequireRuntimePlayerPointer(");
		requireTokenOrder(enable_obj,
			"s_aiGraphRequireRuntimePlayerPointer(",
			"player = current_player");
		requireTokenOrder(enable_obj,
			"player = current_player",
			"propActivate(");
		requireTokenOrder(enable_obj,
			"player = current_player",
			"player->eyespy");
		requireTokenOrder(enable_obj,
			"player->eyespy",
			"playerInitEyespy(");
	}
	{
		const std::string prepare_warp_orbit = functionBlock(
			scenario_runtime, "scenarioSourceAiGraphExecutePrepareWarpOrbit");
		REQUIRE(!prepare_warp_orbit.empty());
		requireTokenOrder(prepare_warp_orbit,
			"s_aiGraphRequireRuntimePad(\"prepare_warp_orbit\"",
			"playerPrepareWarpType3");
	}
	REQUIRE(scenario_runtime.find(
		        "AI action chr_begin_or_end_teleport chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d pad=%d found=%d pad_rows=%d sound_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json+pads.json+audio") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "AI condition if_chr_teleport_full_white chr=%d chr_rows=%d target_chr=%d player=%d player_checked=%d label=%d branch=%d sound_id=%s") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find(
		        "backend=graph.ai.action.teleport_cutscene_weapon+ai/ailists.json+audio") !=
	        std::string::npos);
	{
		const std::string teleport_sound = functionBlock(
			scenario_runtime, "s_aiGraphPlayTeleportSound");
		REQUIRE(!teleport_sound.empty());
		requireTokenOrder(teleport_sound,
			"osGetThreadPri(0)",
			"osGetThreadPri(&g_AudioManager.thread)");
		requireTokenOrder(teleport_sound,
			"osGetThreadPri(&g_AudioManager.thread)",
			"osSetThreadPri(0, audiopri + 1)");
		requireTokenOrder(teleport_sound,
			"osSetThreadPri(0, audiopri + 1)",
			"sndStart(var80095200, soundnum");
		requireTokenOrder(teleport_sound,
			"sndStart(var80095200, soundnum",
			"osSetThreadPri(0, mainpri)");
	}
	{
		const std::string chr_begin_or_end_teleport = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteChrBeginOrEndTeleport");
		REQUIRE(!chr_begin_or_end_teleport.empty());
		REQUIRE(chr_begin_or_end_teleport.find(
			        "chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
		        std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "g_Vars.currentplayer->teleport") == std::string::npos);
		REQUIRE(chr_begin_or_end_teleport.find(
			        "pad_count = s_countRuntimePads();") ==
		        std::string::npos);
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimePad(\"chr_begin_or_end_teleport\"",
			"s_aiGraphResolveAudioCatalogId(");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_begin_or_end_teleport\"");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphRequireRuntimePlayerSlot(\"chr_begin_or_end_teleport\"",
			"player->teleportpad = pad_id");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphResolveAudioCatalogId(",
			"s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT)");
		requireTokenOrder(chr_begin_or_end_teleport,
			"s_aiGraphPlayTeleportSound(SFX_RELOAD_FARSIGHT)",
			"sound_id=%s");
	}
	{
		const std::string if_chr_teleport_full_white = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteIfChrTeleportFullWhite");
		REQUIRE(!if_chr_teleport_full_white.empty());
		REQUIRE(if_chr_teleport_full_white.find(
			        "chrFindById(basechr, chrnum);") ==
		        std::string::npos);
		REQUIRE(if_chr_teleport_full_white.find(
			        "g_Vars.currentplayer->teleport") == std::string::npos);
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_teleport_full_white\"");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphRequireRuntimePlayerSlot(\"if_chr_teleport_full_white\"",
			"player->teleportstate");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphResolveAudioCatalogId(",
			"s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN)");
		requireTokenOrder(if_chr_teleport_full_white,
			"s_aiGraphPlayTeleportSound(SFX_FIRE_SHOTGUN)",
			"sound_id=%s");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.do_gun_command") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_distance_to_gun_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.recover_gun") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_obj_in_room") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_player_looking_at_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_target_is_player") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_kill") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.remove_weapon_from_inventory") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.clear_inventory") !=
	        std::string::npos);
	{
		const std::string clear_inventory = functionBlock(
			scenario_runtime,
			"scenarioSourceAiGraphExecuteClearInventory");
		REQUIRE(!clear_inventory.empty());
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"invClear(");
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"player->devicesactive = 0");
		REQUIRE(clear_inventory.find(
				"g_Vars.currentplayer->devicesactive = 0") ==
			std::string::npos);
		REQUIRE(clear_inventory.find("player == g_Vars.bond") ==
		        std::string::npos);
		REQUIRE(clear_inventory.find("player == g_Vars.coop") ==
		        std::string::npos);
		REQUIRE(clear_inventory.find("playernum == g_Vars.bondplayernum") !=
		        std::string::npos);
		REQUIRE(clear_inventory.find("playernum == g_Vars.coopplayernum") !=
		        std::string::npos);
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"playernum == g_Vars.bondplayernum");
		requireTokenOrder(clear_inventory,
			"setCurrentPlayerNum(playernum)",
			"playernum == g_Vars.bondplayernum");
		requireTokenOrder(clear_inventory,
			"s_aiGraphRequireRuntimePlayerSlot(\"clear_inventory\"",
			"bgunEquipWeapon(WEAPON_UNARMED)");
	}
	REQUIRE(scenario_runtime.find("scenario.ai.action.release_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_grab_object") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.toggle_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_p1p2") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.chr_set_cloaked") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.action.set_autogun_target_team") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_objective_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_objective_failed") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_all_objectives_complete") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_difficulty_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_difficulty_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_timer_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_timer_greater_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_id_less_than") !=
	        std::string::npos);
	REQUIRE(scenario_runtime.find("scenario.ai.condition.if_stage_id_greater_than") !=
	        std::string::npos);
	const std::string stage_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveStageCatalogId");
	REQUIRE(stage_id_helper.find("catalogStageIdByStagenum") !=
	        std::string::npos);
	REQUIRE(stage_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteIfStageIdLessThan",
		     "scenarioSourceAiGraphExecuteIfStageIdGreaterThan",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveStageCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("stage_id=%s current_stage_id=%s") !=
		        std::string::npos);
		REQUIRE(block.find("stagenum=%d current=%d") ==
		        std::string::npos);
	}
	const std::string weapon_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveWeaponCatalogId");
	REQUIRE(weapon_id_helper.find("catalogWeaponIdByRuntimeWeaponNum") !=
	        std::string::npos);
	REQUIRE(weapon_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	REQUIRE(weapon_id_helper.find("WEAPON_NONE") != std::string::npos);
	const std::string body_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveBodyCatalogId");
	REQUIRE(body_id_helper.find("catalogBodyIdByBodynum") !=
	        std::string::npos);
	REQUIRE(body_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string head_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveHeadCatalogId");
	REQUIRE(head_id_helper.find("catalogHeadIdByHeadnum") !=
	        std::string::npos);
	REQUIRE(head_id_helper.find("HEAD_RANDOM") != std::string::npos);
	REQUIRE(head_id_helper.find("HEAD_RANDOM_GENDER") !=
	        std::string::npos);
	REQUIRE(head_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string model_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveModelCatalogId");
	REQUIRE(model_id_helper.find("catalogModelIdByModelnum") !=
	        std::string::npos);
	REQUIRE(model_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string weapon_modelnum_helper =
		functionBlock(scenario_runtime, "s_aiGraphWeaponModelNumOrNone");
	REQUIRE(weapon_modelnum_helper.find("s_aiGraphWeaponHasNoModelSentinel") !=
	        std::string::npos);
	REQUIRE(weapon_modelnum_helper.find("playermgrGetModelOfWeapon") !=
	        std::string::npos);
	requireTokenOrder(weapon_modelnum_helper,
		"s_aiGraphWeaponHasNoModelSentinel",
		"playermgrGetModelOfWeapon");
	const std::string weapon_model_sentinel_helper =
		functionBlock(scenario_runtime, "s_aiGraphWeaponHasNoModelSentinel");
	REQUIRE(weapon_model_sentinel_helper.find("weaponnum == 0xff") !=
	        std::string::npos);
	REQUIRE(weapon_model_sentinel_helper.find("WEAPON_NONE") !=
	        std::string::npos);
	REQUIRE(weapon_model_sentinel_helper.find("WEAPON_UNARMED") !=
	        std::string::npos);
	const std::string weapon_model_id_helper =
		functionBlock(scenario_runtime, "s_aiGraphResolveWeaponModelCatalogId");
	REQUIRE(weapon_model_id_helper.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("s_aiGraphWeaponHasNoModelSentinel") !=
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("weaponnum == 0xff") ==
	        std::string::npos);
	REQUIRE(weapon_model_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	const std::string cutscene_weapon_block = functionBlock(
		scenario_runtime, "scenarioSourceAiGraphExecuteChrSetCutsceneWeapon");
	REQUIRE(cutscene_weapon_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("s_aiGraphResolveWeaponModelCatalogId") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("s_aiGraphWeaponModelNumOrNone") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("playermgrGetModelOfWeapon") ==
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("chrFindById(basechr, chrnum);") ==
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find(
		        "s_aiGraphRequireRuntimeCharacterRefFromBase(") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find(
			"chr_rows=%d target_chr=%d weapon_id=%s model_id=%s fallback_weapon_id=%s fallback_model_id=%s") !=
	        std::string::npos);
	REQUIRE(cutscene_weapon_block.find("weapon=%d fallback_weapon=%d") ==
	        std::string::npos);
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphRequireRuntimeCharacterRefFromBase(",
		"s_aiGraphResolveWeaponCatalogId");
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphResolveWeaponCatalogId",
		"s_aiGraphResolveWeaponModelCatalogId");
	requireTokenOrder(cutscene_weapon_block,
		"s_aiGraphResolveWeaponModelCatalogId",
		"weaponCreateForChr");
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteChrDrawWeapon",
		     "scenarioSourceAiGraphExecuteChrDrawWeaponInCutscene",
		     "scenarioSourceAiGraphExecuteChrDeleteWeapon",
		     "scenarioSourceAiGraphExecuteRemoveWeaponFromInventory",
		     "scenarioSourceAiGraphExecuteIfPlayerUsingCmpOrAr34",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveWeaponCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("weapon_id=%s") != std::string::npos);
		REQUIRE(block.find("weapon=%d") == std::string::npos);
	}
	const std::string drop_item_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteDropItem");
	REQUIRE(drop_item_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(drop_item_block.find("model=%u weapon=%u") ==
	        std::string::npos);
	for (const char *fn : {
		     "scenarioSourceAiGraphExecuteSpawnChrAtPad",
		     "scenarioSourceAiGraphExecuteSpawnChrAtChr",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveBodyCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphResolveHeadCatalogId") !=
		        std::string::npos);
		REQUIRE(block.find("s_aiGraphRegisterSourceSpawnedChr(") !=
		        std::string::npos);
		REQUIRE(block.find("body_id=%s head_id=%s") !=
		        std::string::npos);
		REQUIRE(block.find("body=%d head=%d") == std::string::npos);
	}
	{
		const std::string spawn_at_pad =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSpawnChrAtPad");
		REQUIRE(!spawn_at_pad.empty());
		REQUIRE(spawn_at_pad.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		requireTokenOrder(spawn_at_pad,
			"s_aiSetupSpawnGraphReady(\"spawn_chr_at_pad\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(spawn_at_pad,
			"ailistFindById(ailistid)",
			"chrSpawnAtPad(");
		requireTokenOrder(spawn_at_pad,
			"chrSpawnAtPad(",
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_pad\"");
		requireTokenOrder(spawn_at_pad,
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_pad\"",
			"g_Vars.aioffset = chraiGoToLabel");
	}
	{
		const std::string spawn_at_chr =
			functionBlock(scenario_runtime,
				"scenarioSourceAiGraphExecuteSpawnChrAtChr");
		REQUIRE(!spawn_at_chr.empty());
		REQUIRE(spawn_at_chr.find("ailistFindById(ailistid)") !=
		        std::string::npos);
		requireTokenOrder(spawn_at_chr,
			"s_aiSetupSpawnGraphReady(\"spawn_chr_at_chr\"",
			"ailistFindById(ailistid)");
		requireTokenOrder(spawn_at_chr,
			"ailistFindById(ailistid)",
			"chrSpawnAtChr(");
		requireTokenOrder(spawn_at_chr,
			"chrSpawnAtChr(",
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_chr\"");
		requireTokenOrder(spawn_at_chr,
			"s_aiGraphRegisterSourceSpawnedChr(\"spawn_chr_at_chr\"",
			"g_Vars.aioffset = chraiGoToLabel");
	}
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSpawnChrAtChr");
		REQUIRE(!block.empty());
		REQUIRE(block.find("s_aiGraphRequireRuntimeCharacterRefFromBase(\"spawn_chr_at_chr\"") !=
		        std::string::npos);
		REQUIRE(block.find("chr_rows=%d target_chr=%d") !=
		        std::string::npos);
		requireTokenOrder(block,
			"s_aiGraphResolveHeadCatalogId(\"spawn_chr_at_chr\"",
			"s_aiGraphRequireRuntimeCharacterRefFromBase(");
		requireTokenOrder(block,
			"s_aiGraphRequireRuntimeCharacterRefFromBase(",
			"chrSpawnAtChr");
	}
	const std::string try_equip_weapon_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryEquipWeapon");
	REQUIRE(try_equip_weapon_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("s_aiGraphResolveWeaponCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("model_id=%s weapon_id=%s") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find(
		        "s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("chrGiveWeapon(active_chr") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("g_Vars.stagenum") !=
	        std::string::npos);
	REQUIRE(try_equip_weapon_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphResolveWeaponCatalogId",
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"");
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"",
		"chrGiveWeapon(active_chr");
	requireTokenOrder(try_equip_weapon_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_weapon\"",
		"g_Vars.stagenum");
	requireTokenOrder(try_equip_weapon_block,
		"g_Vars.stagenum",
		"chrGiveWeapon(active_chr");
	REQUIRE(try_equip_weapon_block.find("model=%u weapon=%d") ==
	        std::string::npos);
	const std::string try_equip_hat_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteTryEquipHat");
	REQUIRE(try_equip_hat_block.find("s_aiGraphResolveModelCatalogId") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("model_id=%s") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find(
		        "s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("hatCreateForChr(active_chr") !=
	        std::string::npos);
	REQUIRE(try_equip_hat_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(try_equip_hat_block,
		"s_aiGraphResolveModelCatalogId",
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"");
	requireTokenOrder(try_equip_hat_block,
		"s_aiGraphRequireOptionalLiveRuntimeCharacterPointer(\"try_equip_hat\"",
		"hatCreateForChr(active_chr");
	REQUIRE(try_equip_hat_block.find("model=%u") == std::string::npos);
	const std::string set_obj_image_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetObjImage");
	REQUIRE(set_obj_image_block.find("image_index=%d") !=
	        std::string::npos);
	REQUIRE(set_obj_image_block.find("image=%d") == std::string::npos);
	const std::string object_do_animation_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteObjectDoAnimation");
	REQUIRE(!object_do_animation_block.empty());
	REQUIRE(object_do_animation_block.find("g_Vars.chrdata->") ==
	        std::string::npos);
	REQUIRE(object_do_animation_block.find(
		        "struct chrdata *active_chr = g_Vars.chrdata;") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find(
		        "s_aiGraphRequireRuntimeCharacterStatePointer(\n\t\t\t\t\t\"object_do_animation\"") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find("resolved_tag_id = active_chr->myspecial;") !=
	        std::string::npos);
	REQUIRE(object_do_animation_block.find("chr_rows=%d source_chr=%d") !=
	        std::string::npos);
	requireTokenOrder(object_do_animation_block,
		"tag_id == 255",
		"s_aiGraphRequireRuntimeCharacterStatePointer(");
	requireTokenOrder(object_do_animation_block,
		"s_aiGraphRequireRuntimeCharacterStatePointer(",
		"resolved_tag_id = active_chr->myspecial");
	requireTokenOrder(object_do_animation_block,
		"\"object_do_animation\", anim_id, \"object\"",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"s_aiGraphRequireRuntimeObjectTag(\"object_do_animation\"",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"playerPresentationCutsceneInProgress()",
		"g_CutsceneFrameOverrun240 * speed *");
	requireTokenOrder(object_do_animation_block,
		"g_CutsceneFrameOverrun240 * speed *",
		"AI action object_do_animation anim_id=%s anim_source=%s clip_bytes=%u tag=%d resolved_tag=%d object_rows=%d chr_rows=%d");
	const std::string audio_id_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveAudioCatalogId");
	REQUIRE(audio_id_helper.find("s_aiGraphNormalizeAudioRuntimeId") !=
	        std::string::npos);
	REQUIRE(audio_id_helper.find("catalogResolveSound") != std::string::npos);
	REQUIRE(audio_id_helper.find("assetCatalogGetByIndex") !=
	        std::string::npos);
	REQUIRE(audio_id_helper.find("s_aiGraphRuntimeFailure") !=
	        std::string::npos);
	requireTokenOrder(audio_id_helper,
		"s_aiGraphNormalizeAudioRuntimeId(audio_id)",
		"catalogResolveSound(leaf_id)");
	const std::string audio_normalize_helper =
		functionBlock(scenario_runtime,
			"static s32 s_aiGraphNormalizeAudioRuntimeId");
	REQUIRE(audio_normalize_helper.find("union soundnumhack") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find("g_AudioRussMappings") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find("g_NumAudioRussMappings") !=
	        std::string::npos);
	REQUIRE(audio_normalize_helper.find(
		        "ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings") !=
	        std::string::npos);
	requireTokenOrder(audio_normalize_helper,
		"ref.hasconfig && ref.confignum < (u32)g_NumAudioRussMappings",
		"g_AudioRussMappings[ref.confignum].soundnum");
	const std::string mp3_file_helper =
		functionBlock(scenario_runtime,
			"static s32 s_aiGraphResolveMp3FileNum");
	REQUIRE(mp3_file_helper.find("ref.mp3priority != 0") !=
	        std::string::npos);
	REQUIRE(mp3_file_helper.find("mapped.mp3priority != 0") !=
	        std::string::npos);
	REQUIRE(mp3_file_helper.find(
		        "g_AudioRussMappings[ref.confignum].soundnum") !=
	        std::string::npos);
	const std::string speech_audio_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveSpeechAudioSourceId");
	REQUIRE(speech_audio_helper.find("s_aiGraphResolveMp3FileNum") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("s_aiGraphResolveAudioCatalogId") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("romExtractRelPathForFilenum") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("fsFileSize(rel_path) <= 0") !=
	        std::string::npos);
	REQUIRE(speech_audio_helper.find("romdataFileGetName(file_num)") !=
	        std::string::npos);
	requireTokenOrder(speech_audio_helper,
		"romExtractRelPathForFilenum(file_num",
		"fsFileSize(rel_path) <= 0");
	requireTokenOrder(speech_audio_helper,
		"fsFileSize(rel_path) <= 0",
		"romdataFileGetName(file_num)");
	const std::string audio_source_guard =
		readTextFile("tools/asset_native_source_guard.py");
	REQUIRE(audio_source_guard.find("SCENARIO_AUDIO_SOURCE_METADATA_ERROR") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "scan_scenario_audio_source_metadata_guards") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "romExtractRelPathForFilenum|romdataFileGetName") !=
	        std::string::npos);
	REQUIRE(audio_source_guard.find(
		        "scenario ROM audio metadata helpers must stay behind extracted speech source proof") !=
	        std::string::npos);
	const std::string lang_text_helper =
		functionBlock(scenario_runtime,
			"static const char *s_aiGraphResolveLangTextCatalogId");
	const std::string lang_catalog_helper =
		functionBlock(scenario_runtime,
			"static const asset_entry_t *s_aiGraphFindLangCatalogEntryForBank");
	REQUIRE(lang_catalog_helper.find("ASSET_LANG") != std::string::npos);
	REQUIRE(lang_catalog_helper.find("LANG_MANIFEST_MAX_BANKS") !=
	        std::string::npos);
	REQUIRE(lang_catalog_helper.find("ext.lang.bank_id") != std::string::npos);
	REQUIRE(lang_catalog_helper.find("ext.lang.strings_file") !=
	        std::string::npos);
	REQUIRE(lang_catalog_helper.find("entry->id[0]") != std::string::npos);
	REQUIRE(lang_text_helper.find("s_aiGraphFindLangCatalogEntryForBank") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("LANG_MANIFEST_MAX_BANKS") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("langManifestLoadBankFromCatalog") !=
	        std::string::npos);
	REQUIRE(lang_text_helper.find("public .pdlang source") !=
	        std::string::npos);
	{
		const std::string block = functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSpeak");
		REQUIRE(block.find("s_aiGraphResolveSpeechAudioSourceId") !=
		        std::string::npos);
		REQUIRE(block.find("audio_id=%s") != std::string::npos);
		REQUIRE(block.find("audio=%d") == std::string::npos);
		REQUIRE(block.find("sound=%d") == std::string::npos);
	}
	for (const char *fn : {
		     "scenarioSourceAiGraphExecutePlaySound",
		     "scenarioSourceAiGraphExecuteAssignSound",
		     "scenarioSourceAiGraphExecutePlaySoundFromProp",
	     }) {
		const std::string block = functionBlock(scenario_runtime, fn);
		REQUIRE(block.find("s_aiGraphResolveSpeechAudioSourceId") !=
		        std::string::npos);
		REQUIRE(block.find("audio_id=%s") != std::string::npos);
		REQUIRE(block.find("audio=%d") == std::string::npos);
		REQUIRE(block.find("sound=%d") == std::string::npos);
	}
	const std::string repeat_pad_sound_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecutePlayRepeatingSoundFromPad");
	REQUIRE(repeat_pad_sound_block.find("s_aiGraphResolveSpeechAudioSourceId") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find(
		        "s_aiGraphRequireRuntimePad(\"play_repeating_sound_from_pad\"") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("audio_id=%s") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("pad=%d found=1 pad_rows=%d") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find(
		        "backend=graph.ai.action.audio+ai/ailists.json+pads.json") !=
	        std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("audio=%d") == std::string::npos);
	REQUIRE(repeat_pad_sound_block.find("sound=%d") == std::string::npos);
	requireTokenOrder(repeat_pad_sound_block,
		"s_aiGraphRequireRuntimePad(\"play_repeating_sound_from_pad\"",
		"psCreate(0, NULL, sound, padnum");
	const std::string near_pad_target_block =
		functionBlock(scenario_runtime,
			"scenarioSourceAiGraphExecuteSetChrPresetToChrNearPad");
	// B-1164: the pad checked is the one the native helper resolves (9000).
	REQUIRE(near_pad_target_block.find(
		        "s_aiGraphRequireResolvedRuntimePad(\n\t\t\t\"set_chr_preset_to_chr_near_pad\", chr, padnum, NULL,") !=
	        std::string::npos);
	REQUIRE(near_pad_target_block.find(
		        "pad=%d found=1 pad_rows=%d") != std::string::npos);
	REQUIRE(near_pad_target_block.find(
		        "backend=graph.ai.action.prop_target+ai/ailists.json+pads.json") !=
	        std::string::npos);
	requireTokenOrder(near_pad_target_block,
		"\"set_chr_preset_to_chr_near_pad\", chr, padnum, NULL,",
		"chrSetChrPresetToChrNearPad(preset, chr, distance, padnum)");
	const std::string speak_block =
		functionBlock(scenario_runtime, "scenarioSourceAiGraphExecuteSpeak");
	REQUIRE(speak_block.find("s_aiGraphResolveSpeechAudioSourceId") !=
	        std::string::npos);
	REQUIRE(speak_block.find("s_aiGraphResolveLangTextCatalogId") !=
	        std::string::npos);
}
