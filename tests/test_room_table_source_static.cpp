// B-1113: the authored room boxes must reach public scenario source and be
// used when a stage loads.
//
// The game puts a pad into a room by testing it against the room boxes, and a
// prop is only created when its pad has a room. The background file carries one
// authored box per room. The scenario-source path used to bound each room by
// its collision triangles instead: never larger than the authored box, and
// empty for a room with no collision. Pads outside those boxes got no room and
// the props on them were silently not created (44 in one Agent campaign run,
// among them the RC-P120 a Defense objective asks for).
#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

static std::string readSource(const char *path)
{
	std::ifstream input(path, std::ios::in | std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream out;
	out << input.rdbuf();
	return out.str();
}

}

TEST_CASE("the extractor writes the authored room boxes into every base scenario",
		"[extraction][scenario][b-1113]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");

	// One box per room, six s16 relative to the room's position, for rooms
	// 1 .. room_count-1: the same walk the game's own loader does.
	REQUIRE(extractor.find("static s32 s_buildBgRoomTableJson(") != std::string::npos);
	REQUIRE(extractor.find("pd2.scenario.rooms.v1") != std::string::npos);
	REQUIRE(extractor.find("need = room_count > 1 ? (room_count - 1u) * 6u * (u32)sizeof(s16) : 0u;") !=
		std::string::npos);
	REQUIRE(extractor.find("const u8 *row = section3 + (r - 1u) * 6u * sizeof(s16);") !=
		std::string::npos);
	REQUIRE(extractor.find("bounds[a] = (f32)raw + origin;") != std::string::npos);

	// The inflate target is sized from the stream's own stated length.
	REQUIRE(extractor.find("if (stream_len < need || stream_len > 0x8000u) goto fail;") !=
		std::string::npos);

	// The member is written, named in the manifest, and a failed conversion
	// fails the scenario instead of shipping it without boxes.
	REQUIRE(extractor.find("assetArchiveWriterAddPublicMem(&asset_writer, \"rooms.json\"") !=
		std::string::npos);
	REQUIRE(extractor.find("\\\"room_table\\\": \\\"rooms.json\\\"") != std::string::npos);
	REQUIRE(extractor.find("room table conversion failed for") != std::string::npos);

	// An archive without the member is not clean, so installs extracted
	// before the correction are re-extracted.
	REQUIRE(extractor.find("s_existingArchiveHasEntry(check, \"rooms.json\")") !=
		std::string::npos);
	REQUIRE(extractor.find("_objoperands_b1111_roomtable_b1113_b1114_b1115") != std::string::npos);
}

TEST_CASE("stage load takes room boxes from the authored member before pads are placed",
		"[scenario][bg][b-1113]")
{
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string runtime_h = readSource("port/include/scenario_source_runtime.h");
	const std::string bg = readSource("src/game/bg.c");

	REQUIRE(runtime.find("s32 scenarioSourceApplyRoomTableForStage(") != std::string::npos);
	REQUIRE(runtime_h.find("s32 scenarioSourceApplyRoomTableForStage(") != std::string::npos);

	// A row outside the room domain, or with an inverted box, is refused.
	REQUIRE(runtime.find("|| roomnum <= 0 || roomnum >= room_count") != std::string::npos);
	REQUIRE(runtime.find("|| !(bmin[0] <= bmax[0]) || !(bmin[1] <= bmax[1])") !=
		std::string::npos);

	// The authored boxes replace the collision-derived ones after the triangle
	// walk and before the centre and radius are computed from the box.
	const size_t walk = bg.find("if (verts[j]->z > g_Rooms[room].bbmax[2]) g_Rooms[room].bbmax[2] = verts[j]->z;");
	const size_t apply = bg.find("s32 authored = scenarioSourceApplyRoomTableForStage(&stage,");
	const size_t centre = bg.find("bgMeasureRoomBounds(g_Rooms[i].bbmin, g_Rooms[i].bbmax,");
	REQUIRE(walk != std::string::npos);
	REQUIRE(apply != std::string::npos);
	REQUIRE(centre != std::string::npos);
	REQUIRE(walk < apply);
	REQUIRE(apply < centre);

	// A base scenario without usable boxes says so; it does not load quietly
	// with the tighter collision boxes.
	REQUIRE(bg.find("B-1113 no usable rooms.json for") != std::string::npos);
}

// B-1114: room lights could not be shot out in extracted stages, for two
// reasons on top of each other.
//
// The extractor writes a light's quad as four nested [x, y, z] corners, and the
// loader read it with a flat-array reader that stops at the first inner '['. So
// no quad was ever read: every light kept an all-zero quad.
//
// And the quads of the background file are relative to their room's origin,
// while the scenario-source path keeps every room origin at zero (its geometry
// is in world coordinates). Measured over the extracted stages: every quad lies
// inside its room's authored box once the origin is added, and almost none do
// as written.
TEST_CASE("room lights are placed in world coordinates from their room's origin",
		"[scenario][bg][lights][b-1114]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string bg = readSource("src/game/bg.c");
	const std::string lights = readSource("src/game/dlights.c");

	// The quad is written nested, so it is read with the nested reader, and a
	// light whose quad cannot be read is reported instead of passing as zero.
	REQUIRE(extractor.find("if (s_textbufAppendf(out, \"[%d, %d, %d]\",") != std::string::npos);
	REQUIRE(runtime.find("static s32 s_jsonReadNestedIntArrayInRange(") != std::string::npos);
	REQUIRE(runtime.find("if (!s_jsonReadNestedIntArrayInRange(obj_open, obj_close, \"bbox\", bbox,") !=
		std::string::npos);
	REQUIRE(runtime.find("s_jsonReadIntArrayInRange(obj_open, obj_close, \"bbox\"") ==
		std::string::npos);
	REQUIRE(runtime.find("have no readable quad (bbox); they cannot be shot out") !=
		std::string::npos);

	// The extractor publishes each room's origin beside its box, and an
	// archive without it is not clean.
	REQUIRE(extractor.find("\"\\\"origin\\\": [%.6f, %.6f, %.6f], \"") != std::string::npos);
	REQUIRE(extractor.find("rooms[r].pos.x, rooms[r].pos.y, rooms[r].pos.z,") != std::string::npos);
	REQUIRE(extractor.find("\"\\\"origin\\\": [\") &&") != std::string::npos);

	// The light loader reads the origins before it parses the quads, adds the
	// origin to all four corners, and drops a light it cannot store.
	const size_t load = runtime.find("origins = s_loadRoomOrigins(scenario, &origin_count);");
	const size_t add = runtime.find("bbox[j] += origin[j % 3];");
	const size_t range = runtime.find("if (bbox[j] < -32768 || bbox[j] > 32767) {");
	const size_t store = runtime.find("l->bbox[j].x = (s16)bbox[j * 3 + 0];");
	REQUIRE(runtime.find("static struct coord *s_loadRoomOrigins(") != std::string::npos);
	REQUIRE(load != std::string::npos);
	REQUIRE(add != std::string::npos);
	REQUIRE(range != std::string::npos);
	REQUIRE(store != std::string::npos);
	REQUIRE(load < add);
	REQUIRE(add < range);
	REQUIRE(range < store);
	REQUIRE(runtime.find("does not fit 16-bit world coordinates; light dropped") !=
		std::string::npos);

	// That placement is only right while this path keeps room origins at zero:
	// the game subtracts the room origin from a shot before testing the quad.
	REQUIRE(bg.find("memset(g_BgRooms, 0, (g_Vars.roomcount + 1) * sizeof(struct bgroom));") !=
		std::string::npos);
	REQUIRE(lights.find("spa4.x = gunpos->x - g_BgRooms[roomnum].pos.x;") != std::string::npos);

	// Stage load checks every quad against its room's box and says so when
	// the two tables disagree.
	REQUIRE(bg.find("outside_room=%d source=room_lights.json") != std::string::npos);
	REQUIRE(bg.find("lie outside their room's box: light positions and room boxes disagree") !=
		std::string::npos);
}

// B-1115: each room of the background file has an authored brightness range.
// roomInitLights takes the level a room keeps when its lights are out from the
// minimum. The scenario-source path gave every room 255/255, so a room whose
// lights were shot out or switched off by a script stayed about twice as bright
// as authored (63 instead of 32 for the usual 128/255 room).
TEST_CASE("rooms take their authored brightness range from the room table",
		"[scenario][bg][lights][b-1115]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const std::string runtime = readSource("port/src/scenario_source_runtime.c");
	const std::string bg = readSource("src/game/bg.c");
	const std::string lights = readSource("src/game/dlights.c");

	// The row ends with the pair; the optional room rules follow it and then
	// the row is closed (see test_stage_light_rules_static.cpp).
	REQUIRE(extractor.find("\"\\\"brightness\\\": [%u, %u]\",") != std::string::npos);
	REQUIRE(extractor.find("(unsigned)rooms[r].br_light_min,") != std::string::npos);
	REQUIRE(extractor.find("(unsigned)rooms[r].br_light_max) != 0 ||") != std::string::npos);

	// A pair outside 0..255 or with the minimum above the maximum is ignored.
	REQUIRE(runtime.find("&& brightness[0] >= 0 && brightness[0] <= 255") != std::string::npos);
	REQUIRE(runtime.find("&& brightness[1] >= brightness[0] && brightness[1] <= 255) {") !=
		std::string::npos);
	REQUIRE(runtime.find("bgrooms[roomnum].br_light_min = (u8)brightness[0];") != std::string::npos);
	REQUIRE(runtime.find("bgrooms[roomnum].br_light_max = (u8)brightness[1];") != std::string::npos);

	// The table is applied to the same array the light setup reads, and before
	// that setup runs.
	const size_t apply = bg.find("g_Vars.normmplayerisrunning, g_Rooms, g_BgRooms, g_Vars.roomcount,");
	const size_t setup = bg.find("\tfunc0f002a98();\n\tif (g_BgLightsFileData) {");
	REQUIRE(apply != std::string::npos);
	REQUIRE(setup != std::string::npos);
	REQUIRE(apply < setup);
	REQUIRE(bg.find("*min = g_BgRooms[roomnum].br_light_min;") != std::string::npos);
	REQUIRE(lights.find("bgGetRoomBrightnessRange(roomnum, &room->br_light_min, &room->br_light_max);") !=
		std::string::npos);
}

TEST_CASE("a pad without a room and a prop that is not created are both reported",
		"[setup][b-1113]")
{
	const std::string pads = readSource("src/game/setuppads.c");
	const std::string setup = readSource("src/game/setup.c");

	REQUIRE(pads.find("\"SETUP.PADS: stage 0x%02x pads=%d without_room=%d\"") !=
		std::string::npos);
	REQUIRE(setup.find("has no room (room=%d); prop not created") != std::string::npos);
}

TEST_CASE("the archive checker knows the room table member",
		"[extraction][conformance][b-1113]")
{
	const std::string conformance = readSource("tools/asset_archive_conformance.py");

	REQUIRE(conformance.find("\"rooms.json\",      # authored BG room table sidecar (B-1113)") !=
		std::string::npos);
	REQUIRE(conformance.find("\"rooms.json\": slot(") != std::string::npos);
	REQUIRE(conformance.find("_objoperands_b1111_roomtable_b1113_b1114_b1115_lightrules_sky_stagejson_player_ai_intro_mpsetup_ending_hub_stars_dyntex_ammo_objrules_sparks_gas_mplifts_aiedges_simtwins_typedargs_catalogargs_assetargs_ruleevents_liftglass\"") != std::string::npos);
}

// B-1266: the scene export left out every room whose display data inflated
// past 8192 bytes. Its work buffer was sized from the room's COMPRESSED
// length (span * 8 + 4096, at least 65536) and a room was skipped, without a
// word, when its inflated length exceeded an eighth of that. Measured against
// the ROM: 14 of Defection's 167 rooms with 53 percent of its triangles, and
// between 10 and 64 percent of every single-player stage. The collision, the
// room boxes and the portals come from other tables, so play went on over
// floors that were not drawn.
TEST_CASE("the scene export sizes a room from its inflated length and leaves none out",
		"[extraction][scenario][b-1266]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const size_t begin = extractor.find("static s32 s_buildBgVisualExports(");
	REQUIRE(begin != std::string::npos);
	const size_t end = extractor.find("\n}\n", begin);
	REQUIRE(end != std::string::npos);
	const std::string body = extractor.substr(begin, end - begin);

	// The inflated size is the rzip header's, and the buffer is eight times
	// that (preprocessBgRoom's own bound), never a multiple of the stored span.
	CHECK(body.find("room_infsize = ((u32)src[2] << 16) | ((u32)src[3] << 8) | (u32)src[4];") !=
		std::string::npos);
	CHECK(body.find("u32 room_cap = s_alignTo(room_infsize * 8u + 4096u, 16);") != std::string::npos);
	CHECK(body.find("room_span * 8u") == std::string::npos);
	const std::string filebg = readSource("port/src/preprocess/filebg.c");
	CHECK(filebg.find("#define PREPROCESS_BG_ROOM_MULT_LOCAL 8u") != std::string::npos);

	// A room that has data and cannot be exported is counted, logged and
	// fails the export: the one silent continue left is the room with no data.
	size_t left_out = 0;
	for (size_t at = body.find("s_bgSceneRoomLeftOut(scene, roomnum,"); at != std::string::npos;
			at = body.find("s_bgSceneRoomLeftOut(scene, roomnum,", at + 1)) {
		left_out++;
	}
	CHECK(left_out == 4);
	size_t continues = 0;
	for (size_t at = body.find("continue;"); at != std::string::npos; at = body.find("continue;", at + 1)) {
		continues++;
	}
	CHECK(continues == left_out + 1);
	CHECK(body.find("scene->rooms_with_data++;") != std::string::npos);
	const size_t fail = body.find("if (scene->rooms_left_out) {");
	REQUIRE(fail != std::string::npos);
	CHECK(body.find("return -1;", fail) < body.find("s_exportBgSection2Textures(bg, bg_size, scene)", fail));
	CHECK(body.find("\"ROMEXTRACT.SCENE: stage=0x%02x rooms=%u exported=%u left_out=%u triangles=%u") !=
		std::string::npos);

	// An install that already has scenes takes them through the per-archive
	// check again, which compares the scene export version each scene carries.
	CHECK(extractor.find("_iafix_b945_dyntex_allrooms_b1266_cull_b1267\"\n") != std::string::npos);
	CHECK(extractor.find("ROMEXTRACT_PDARENA_FAST_CACHE_KIND \"pdarena_clean_public_v10_pdscenario_v99_iafix_b945_allrooms_b1266\"") !=
		std::string::npos);
	CHECK(extractor.find("s_existingArchiveEntryContains(check, \"scene.glb\",\n\t\t\tPDSCENARIO_BG_VISUAL_EXPORT_VERSION)") !=
		std::string::npos);
	// The scenario kind is the level format authored levels declare: unchanged.
	CHECK(extractor.find("_catalogargs_assetargs_ruleevents_liftglass\"\n") != std::string::npos);
}

// B-1267: the room display lists set and clear G_CULL_BACK (on Defection
// 92.5 percent of the triangles are drawn with back faces culled, 7.5 percent
// from both sides) and the scene kept none of it: the renderer drew every
// surface from both sides.
TEST_CASE("the scene keeps each room triangle's cull mode and the renderer honours it",
		"[extraction][scenario][renderer][b-1267]")
{
	const std::string extractor = readSource("port/src/romextract_pdarena.c");
	const size_t begin = extractor.find("static s32 s_exportBgGdl(");
	REQUIRE(begin != std::string::npos);
	const size_t end = extractor.find("\n}\n", begin);
	REQUIRE(end != std::string::npos);
	const std::string walk = extractor.substr(begin, end - begin);

	// The walk follows the geometry mode, and a material is resolved when a
	// triangle is drawn: the texture selection, the block list and the cull mode.
	CHECK(walk.find("} else if (op == (u8)G_SETGEOMETRYMODE) {\n\t\t\tgeometry_mode |= (u32)cmd->words.w1;") !=
		std::string::npos);
	CHECK(walk.find("} else if (op == (u8)G_CLEARGEOMETRYMODE) {\n\t\t\tgeometry_mode &= ~(u32)cmd->words.w1;") !=
		std::string::npos);
	CHECK(walk.find("u32 geometry_mode = (u32)G_CULL_BACK;") != std::string::npos);
	CHECK(walk.find("tex_subcmd, tex_w0, tex_w1, is_xlu, cull == 1)") != std::string::npos);
	CHECK(walk.find("s_exportBgTriByIndices(") == std::string::npos);
	CHECK(extractor.find("m->double_sided == (double_sided ? 1 : 0)) {") != std::string::npos);
	// Front faces culled is the same triangle seen from behind; both culled is never drawn.
	CHECK(extractor.find("if (cull == 2) {\n\t\tconst u32 swap = b;\n\t\tb = c;\n\t\tc = swap;\n\t}") !=
		std::string::npos);
	CHECK(extractor.find("scene->tris_cull_both++;") != std::string::npos);

	// The scene says it twice: glTF's doubleSided, and pd2_material.cull,
	// which is the one the renderer reads.
	CHECK(extractor.find("m->double_sided ? \"\\\"doubleSided\\\":true,\" : \"\"") != std::string::npos);
	CHECK(extractor.find("\\\"min_lod\\\":%d,\\\"tile_flag\\\":%d,\\\"cull\\\":\\\"%s\\\"\"") !=
		std::string::npos);
	CHECK(extractor.find("m->flag ? 1 : 0, m->double_sided ? \"none\" : \"back\") != 0) goto fail_json;") !=
		std::string::npos);

	// The renderer culls only where the scene asks, so a scene written before
	// the field, and an authored level, draw as they did.
	const std::string renderer = readSource("port/fast3d/scenario_scene_renderer.cpp");
	CHECK(renderer.find("bool cull_back = false;") != std::string::npos);
	CHECK(renderer.find("mat.cull_back = stringField(*pd2, \"cull\") == \"back\";") != std::string::npos);
	CHECK(renderer.find("glFrontFace(front_counter_clockwise ? GL_CCW : GL_CW);") != std::string::npos);
	CHECK(renderer.find("if (cull_back) {\n\t\t\tglEnable(GL_CULL_FACE);\n\t\t} else {\n\t\t\tglDisable(GL_CULL_FACE);\n\t\t}\n\t\tglDrawArrays(GL_TRIANGLES,") !=
		std::string::npos);
	CHECK(renderer.find("glFrontFace((GLenum)prev_front_face);\n\tglCullFace((GLenum)prev_cull_mode);") !=
		std::string::npos);
	CHECK(renderer.find("cull = mat.cull_back ? cull_back_faces : 0;") != std::string::npos);
	const std::string bgfx = readSource("port/fast3d/scenario_scene_renderer_bgfx.cpp");
	CHECK(bgfx.find("if (cull == 1) {\n        state |= BGFX_STATE_CULL_CW;\n    } else if (cull == 2) {\n        state |= BGFX_STATE_CULL_CCW;\n    }") !=
		std::string::npos);
}
