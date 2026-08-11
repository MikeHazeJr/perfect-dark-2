/**
 * Field-level and transaction tests for the public .pdeffect presentation
 * consumer. The renderer bridge itself is pinned below against production
 * pdgui source; state/rollback behavior is exercised through the real hooks.
 */
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "catch.hpp"

extern "C" {
#include "assetprovider.h"
#include "asset_runtime.h"
#include "constants.h"
#include "effect_gameplay_runtime.h"
#include "effect_presentation_runtime.h"

void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
void testStubEffectGameplayReset(void);
size_t effectPresentationRenderableParticleCount(void);
}

namespace {
class TempMaterial {
public:
	TempMaterial(const char *id, const char *rgba)
	{
		path = std::filesystem::temp_directory_path() /
			("pd2_effect_presentation_" + std::to_string(++serial) + ".json");
		std::ofstream out(path, std::ios::binary);
		out << "{\"schema\":\"pd2.material.v1\",\"catalog_id\":\"" << id
			<< "\",\"shading_model\":\"classic_emissive\",\"base_color\":"
			<< rgba << ",\"emissive\":true,\"roughness\":0.2,\"metallic\":0.1}";
	}
	~TempMaterial()
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	std::string string() const { return path.string(); }
	std::string member() const { return path.filename().string(); }
private:
	std::filesystem::path path;
	static unsigned serial;
};
unsigned TempMaterial::serial = 0;

static void activateMaterial(const char *id, const char *rgba,
	TempMaterial *&owned)
{
	asset_entry_t entry = {};
	entry.type = ASSET_MATERIAL;
	entry.enabled = 1;
	std::strncpy(entry.id, id, sizeof(entry.id) - 1);
	owned = new TempMaterial(id, rgba);
	std::strncpy(entry.ext.material.material_file, owned->member().c_str(),
		sizeof(entry.ext.material.material_file) - 1);
	REQUIRE(assetRuntimeActivateCatalogEntry(&entry, owned->string().c_str()) == 1);
	REQUIRE(assetRuntimeHydrateCatalogEntry(&entry) == 1);
}

static void setArray(weapon_graph_ir_param_t &param, const char *key,
	const char *value)
{
	std::strncpy(param.key, key, sizeof(param.key) - 1);
	param.type = WEAPON_GRAPH_PARAM_ARRAY;
	std::strncpy(param.value, value, sizeof(param.value) - 1);
}

static void setString(weapon_graph_ir_param_t &param, const char *key,
	const char *value)
{
	std::strncpy(param.key, key, sizeof(param.key) - 1);
	param.type = WEAPON_GRAPH_PARAM_STRING;
	std::strncpy(param.value, value, sizeof(param.value) - 1);
}

static void setFloat(weapon_graph_ir_param_t &param, const char *key, float value)
{
	std::strncpy(param.key, key, sizeof(param.key) - 1);
	param.type = WEAPON_GRAPH_PARAM_FLOAT;
	param.f_value = value;
}

static void setInt(weapon_graph_ir_param_t &param, const char *key, int value)
{
	std::strncpy(param.key, key, sizeof(param.key) - 1);
	param.type = WEAPON_GRAPH_PARAM_INT;
	param.i_value = value;
}

static effect_instance_frame_t makeFrame(effect_graph_runtime_t &runtime,
	u64 id)
{
	effect_instance_frame_t frame = {};
	frame.instance_id = id;
	frame.runtime = &runtime;
	return frame;
}

static void runNode(effect_instance_consumer_t &consumer,
	effect_instance_frame_t &frame, int nodeIndex,
	effect_graph_dispatch_slot_t slot)
{
	char err[256] = {};
	frame.node = &frame.runtime->program.nodes[nodeIndex];
	frame.node_index = nodeIndex;
	frame.execution_index = nodeIndex;
	const s32 result = consumer.nodes[slot](&frame, err, sizeof(err));
	INFO(err);
	REQUIRE(result == 0);
}
} // namespace

TEST_CASE("B-1014 presentation retains exact authored fields and overlapping owners",
	"[modding][pdxxx][effect][presentation][b1014][t-assets-035]")
{
	assetRuntimeReset();
	effectPresentationRuntimeClearAll();
	TempMaterial *material = nullptr;
	activateMaterial("modx:glow_material", "[0.2,0.4,0.8,0.5]", material);
	asset_entry_t texture = {};
	texture.type = ASSET_TEXTURE;
	texture.enabled = 1;
	std::strncpy(texture.id, "modx:screen_texture", sizeof(texture.id) - 1);
	texture.source_texnum = TEXTURE_CUSTOM_START + 17;
	texture.source.primary.provider = reinterpret_cast<const asset_provider_t *>(1);
	testStubAssetCatalogResolveWith(&texture);

	weapon_graph_ir_node_t nodes[5] = {};
	weapon_graph_ir_param_t params[24] = {};
	const weapon_graph_opcode_e opcodes[5] = {
		WEAPON_GRAPH_OP_EFFECT_TINT, WEAPON_GRAPH_OP_EFFECT_GLOW,
		WEAPON_GRAPH_OP_EFFECT_SHIMMER, WEAPON_GRAPH_OP_EFFECT_DARKEN,
		WEAPON_GRAPH_OP_EFFECT_SCREEN
	};
	const char *kinds[5] = {
		"effect.tint", "effect.glow", "effect.shimmer", "effect.darken",
		"effect.screen"
	};
	int p = 0;
	for (int i = 0; i < 5; i++) {
		nodes[i].opcode = opcodes[i];
		std::strncpy(nodes[i].kind, kinds[i], sizeof(nodes[i].kind) - 1);
	}
	nodes[0].param_start = p;
	setArray(params[p++], "tint", "[0.25,0.5,0.75,0.8]");
	setFloat(params[p++], "intensity", 0.5f);
	setString(params[p++], "shader", "classic_tint");
	nodes[0].param_count = p - nodes[0].param_start;
	nodes[1].param_start = p;
	setString(params[p++], "material_ref", "modx:glow_material");
	setArray(params[p++], "tint", "[0.5,0.25,1.0,0.4]");
	setFloat(params[p++], "intensity", 1.25f);
	setString(params[p++], "shader", "classic_glow");
	nodes[1].param_count = p - nodes[1].param_start;
	nodes[2].param_start = p;
	setArray(params[p++], "tint", "[0.9,0.8,0.7]");
	setFloat(params[p++], "intensity", 0.6f);
	setFloat(params[p++], "speed", -2.5f);
	setFloat(params[p++], "width", 0.3f);
	setString(params[p++], "shader", "classic_shimmer");
	nodes[2].param_count = p - nodes[2].param_start;
	nodes[3].param_start = p;
	setFloat(params[p++], "intensity", 0.7f);
	setString(params[p++], "shader", "classic_darken");
	nodes[3].param_count = p - nodes[3].param_start;
	nodes[4].param_start = p;
	setArray(params[p++], "tint", "[0.1,0.2,0.3,0.4]");
	setInt(params[p++], "intensity", 2);
	setString(params[p++], "texture_ref", "modx:screen_texture");
	setString(params[p++], "shader", "classic_screen");
	setFloat(params[p++], "size", 0.8f);
	nodes[4].param_count = p - nodes[4].param_start;

	effect_graph_runtime_t runtime = {};
	runtime.program.nodes = nodes;
	runtime.program.node_count = 5;
	runtime.program.params = params;
	runtime.program.param_count = p;
	effect_instance_consumer_t consumer = {};
	char err[256] = {};
	REQUIRE(effectPresentationRuntimeBuildConsumer(&consumer, err, sizeof(err)) == 0);
	for (int slot = 0; slot < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; slot++)
		REQUIRE(consumer.nodes[slot] != nullptr);

	auto frame = makeFrame(runtime, 1001);
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	for (int i = 0; i < 5; i++) {
		frame.priority = 20 + i;
		frame.attachment = "";
		if (i == 0) {
			frame.target_name = "world";
			frame.attachment = "surface";
			frame.position = { 1, 2, 3 };
			frame.primary_direction = { 1, 0, 0 };
			frame.secondary_direction = { 0, 1, 0 };
		} else if (i == 1) {
			frame.target_name = "prop";
			frame.attachment = "point";
			frame.position = { 4, 5, 6 };
			frame.rooms[0] = 7;
			frame.room_count = 1;
		} else if (i == 2) {
			frame.target_name = "target";
			frame.attachment = "beam";
			frame.source_position = { 10, 11, 12 };
			frame.target_position = { 20, 21, 22 };
		} else if (i == 3) {
			frame.target_name = "scene";
		} else {
			frame.target_name = "player";
		}
		runNode(consumer, frame, i, (effect_graph_dispatch_slot_t)i);
	}
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 0);
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) == 0);
	consumer.commit(&frame);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 5);

	effect_presentation_command_t command = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &command) == 1);
	REQUIRE(command.instance_id == 1001);
	REQUIRE(command.kind == EFFECT_PRESENTATION_TINT);
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_DECAL);
	REQUIRE(std::string(command.target_name) == "world");
	REQUIRE(std::string(command.attachment) == "surface");
	REQUIRE(command.position.x == Approx(1.0f));
	REQUIRE(command.primary_direction.x == Approx(1.0f));
	REQUIRE(std::string(command.shader_id) == "classic_tint");
	REQUIRE(command.rgba[0] == Approx(0.25f));
	REQUIRE(command.rgba[3] == Approx(0.8f));
	REQUIRE(command.intensity == Approx(0.5f));
	REQUIRE(effectPresentationRuntimeSnapshot(1, &command) == 1);
	REQUIRE(std::string(command.material_ref) == "modx:glow_material");
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_LIGHT);
	REQUIRE(std::string(command.target_name) == "prop");
	REQUIRE(command.rooms[0] == 7);
	REQUIRE(std::string(command.material_shading_model) == "classic_emissive");
	REQUIRE(command.material_roughness == Approx(0.2f));
	REQUIRE(command.material_metallic == Approx(0.1f));
	REQUIRE(command.material_emissive == 1);
	REQUIRE(std::string(command.shader_id) == "classic_glow");
	REQUIRE(command.rgba[0] == Approx(0.1f));
	REQUIRE(command.rgba[1] == Approx(0.1f));
	REQUIRE(command.rgba[2] == Approx(0.8f));
	REQUIRE(command.rgba[3] == Approx(0.2f));
	REQUIRE(command.intensity == Approx(1.25f));
	REQUIRE(effectPresentationRuntimeSnapshot(2, &command) == 1);
	REQUIRE(command.kind == EFFECT_PRESENTATION_SHIMMER);
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_BEAM);
	REQUIRE(std::string(command.attachment) == "beam");
	REQUIRE(command.source_position.x == Approx(10.0f));
	REQUIRE(command.target_position.x == Approx(20.0f));
	REQUIRE(command.rgba[3] == Approx(1.0f));
	REQUIRE(command.speed == Approx(-2.5f));
	REQUIRE(command.width == Approx(0.3f));
	REQUIRE(std::string(command.shader_id) == "classic_shimmer");
	REQUIRE(effectPresentationRuntimeSnapshot(3, &command) == 1);
	REQUIRE(command.kind == EFFECT_PRESENTATION_DARKEN);
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN);
	REQUIRE(command.rgba[0] == Approx(0.0f));
	REQUIRE(command.intensity == Approx(0.7f));
	REQUIRE(std::string(command.shader_id) == "classic_darken");
	REQUIRE(effectPresentationRuntimeSnapshot(4, &command) == 1);
	REQUIRE(command.kind == EFFECT_PRESENTATION_SCREEN);
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN);
	REQUIRE(std::string(command.target_name) == "player");
	REQUIRE(std::string(command.texture_ref) == "modx:screen_texture");
	REQUIRE(command.texture_num == TEXTURE_CUSTOM_START + 17);
	REQUIRE(std::string(command.shader_id) == "classic_screen");
	REQUIRE(command.size == Approx(0.8f));
	REQUIRE(command.intensity == Approx(2.0f));

	/* A second instance commits independently and sorts by authored priority. */
	weapon_graph_ir_node_t secondNode = {};
	weapon_graph_ir_param_t secondParams[2] = {};
	secondNode.opcode = WEAPON_GRAPH_OP_EFFECT_TINT;
	secondNode.param_count = 2;
	setArray(secondParams[0], "tint", "[0.8,0.1,0.2,1.0]");
	setFloat(secondParams[1], "intensity", 0.25f);
	effect_graph_runtime_t secondRuntime = {};
	secondRuntime.program.nodes = &secondNode;
	secondRuntime.program.node_count = 1;
	secondRuntime.program.params = secondParams;
	secondRuntime.program.param_count = 2;
	auto secondFrame = makeFrame(secondRuntime, 2002);
	secondFrame.priority = -4;
	secondFrame.target_name = "scene";
	REQUIRE(consumer.begin(&secondFrame, err, sizeof(err)) == 0);
	runNode(consumer, secondFrame, 0, EFFECT_GRAPH_DISPATCH_TINT);
	REQUIRE(consumer.prepare(&secondFrame, err, sizeof(err)) == 0);
	consumer.commit(&secondFrame);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 6);
	REQUIRE(effectPresentationRuntimeSnapshot(0, &command) == 1);
	REQUIRE(command.instance_id == 2002);
	consumer.cleanup(1001);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 1);
	REQUIRE(effectPresentationRuntimeSnapshot(0, &command) == 1);
	REQUIRE(command.instance_id == 2002);

	consumer.cleanup(2002);
	effectPresentationRuntimeClearAll();
	assetRuntimeReset();
	testStubAssetCatalogResolveWith(nullptr);
	delete material;
}

TEST_CASE("B-1014 failed material prepare preserves committed renderer state",
	"[modding][pdxxx][effect][presentation][rollback][b1014][t-assets-035]")
{
	assetRuntimeReset();
	effectPresentationRuntimeClearAll();
	TempMaterial *material = nullptr;
	activateMaterial("modx:volatile_material", "[0.3,0.6,0.9,0.7]", material);
	weapon_graph_ir_node_t node = {};
	weapon_graph_ir_param_t params[2] = {};
	node.opcode = WEAPON_GRAPH_OP_EFFECT_GLOW;
	node.param_count = 2;
	setString(params[0], "material_ref", "modx:volatile_material");
	setFloat(params[1], "intensity", 0.5f);
	effect_graph_runtime_t runtime = {};
	runtime.program.nodes = &node;
	runtime.program.node_count = 1;
	runtime.program.params = params;
	runtime.program.param_count = 2;
	effect_instance_consumer_t consumer = {};
	char err[256] = {};
	REQUIRE(effectPresentationRuntimeBuildConsumer(&consumer, err, sizeof(err)) == 0);
	auto frame = makeFrame(runtime, 3003);
	frame.target_name = "prop";
	frame.attachment = "point";
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	runNode(consumer, frame, 0, EFFECT_GRAPH_DISPATCH_GLOW);
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) == 0);
	consumer.commit(&frame);
	effect_presentation_command_t before = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &before) == 1);

	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	runNode(consumer, frame, 0, EFFECT_GRAPH_DISPATCH_GLOW);
	assetRuntimeReleaseCatalogEntry("modx:volatile_material");
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no active hydrated public source") != std::string::npos);
	consumer.rollback(&frame, 1);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 1);
	effect_presentation_command_t after = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &after) == 1);
	REQUIRE(std::memcmp(&before, &after, sizeof(before)) == 0);

	consumer.cleanup(3003);
	effectPresentationRuntimeClearAll();
	assetRuntimeReset();
	delete material;
}

TEST_CASE("B-1014 screen texture fails closed on missing disabled wrong or replaced source",
	"[modding][pdxxx][effect][presentation][texture][rollback][b1014][t-assets-035]")
{
	effectPresentationRuntimeClearAll();
	asset_entry_t texture = {};
	texture.type = ASSET_TEXTURE;
	texture.enabled = 1;
	std::strncpy(texture.id, "modx:screen_texture", sizeof(texture.id) - 1);
	texture.source_texnum = TEXTURE_CUSTOM_START + 23;
	texture.source.primary.provider = reinterpret_cast<const asset_provider_t *>(1);
	weapon_graph_ir_node_t node = {};
	weapon_graph_ir_param_t params[2] = {};
	node.opcode = WEAPON_GRAPH_OP_EFFECT_SCREEN;
	node.param_count = 2;
	setString(params[0], "texture_ref", "modx:screen_texture");
	setArray(params[1], "tint", "[0.2,0.4,0.8,0.6]");
	effect_graph_runtime_t runtime = {};
	runtime.program.nodes = &node;
	runtime.program.node_count = 1;
	runtime.program.params = params;
	runtime.program.param_count = 2;
	effect_instance_consumer_t consumer = {};
	char err[256] = {};
	REQUIRE(effectPresentationRuntimeBuildConsumer(&consumer, err, sizeof(err)) == 0);
	auto frame = makeFrame(runtime, 4004);
	frame.target_name = "player";

	testStubAssetCatalogResolveWith(nullptr);
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	REQUIRE(consumer.nodes[EFFECT_GRAPH_DISPATCH_SCREEN](&frame, err,
		sizeof(err)) != 0);
	consumer.rollback(&frame, 1);

	testStubAssetCatalogResolveWith(&texture);
	texture.enabled = 0;
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	REQUIRE(consumer.nodes[EFFECT_GRAPH_DISPATCH_SCREEN](&frame, err,
		sizeof(err)) != 0);
	consumer.rollback(&frame, 1);
	texture.enabled = 1;
	texture.type = ASSET_AUDIO;
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	REQUIRE(consumer.nodes[EFFECT_GRAPH_DISPATCH_SCREEN](&frame, err,
		sizeof(err)) != 0);
	consumer.rollback(&frame, 1);
	texture.type = ASSET_TEXTURE;

	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	runNode(consumer, frame, 0, EFFECT_GRAPH_DISPATCH_SCREEN);
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) == 0);
	consumer.commit(&frame);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 1);
	effect_presentation_command_t before = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &before) == 1);

	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	runNode(consumer, frame, 0, EFFECT_GRAPH_DISPATCH_SCREEN);
	texture.source_texnum++;
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("changed before prepare") != std::string::npos);
	consumer.rollback(&frame, 1);
	effect_presentation_command_t after = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &after) == 1);
	REQUIRE(std::memcmp(&before, &after, sizeof(before)) == 0);

	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	texture.source_texnum = before.texture_num;
	runNode(consumer, frame, 0, EFFECT_GRAPH_DISPATCH_SCREEN);
	texture.enabled = 0;
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) != 0);
	consumer.rollback(&frame, 1);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 1);

	consumer.cleanup(4004);
	testStubAssetCatalogResolveWith(nullptr);
	effectPresentationRuntimeClearAll();
}

TEST_CASE("B-1014 world renderer enumerates every committed particle node",
	"[modding][pdxxx][effect][presentation][particle][b1014][t-assets-035]")
{
	asset_entry_t texture = {};
	texture.type = ASSET_TEXTURE;
	texture.enabled = 1;
	std::strncpy(texture.id, "modx:particle_texture", sizeof(texture.id) - 1);
	texture.source_texnum = TEXTURE_CUSTOM_START + 29;
	texture.source.primary.provider = reinterpret_cast<const asset_provider_t *>(1);
	testStubAssetCatalogResolveWith(&texture);
	testStubEffectGameplayReset();

	weapon_graph_ir_param_t params[2] = {};
	setString(params[0], "texture_ref", "modx:particle_texture");
	setFloat(params[1], "size", 12.0f);
	weapon_graph_ir_node_t node = {};
	std::strncpy(node.id, "particle", sizeof(node.id) - 1);
	std::strncpy(node.kind, "effect.particle", sizeof(node.kind) - 1);
	node.opcode = WEAPON_GRAPH_OP_EFFECT_PARTICLE;
	node.param_count = 2;
	effect_graph_runtime_t runtime = {};
	runtime.program.nodes = &node;
	runtime.program.node_count = 1;
	runtime.program.params = params;
	runtime.program.param_count = 2;
	effect_instance_frame_t frame = makeFrame(runtime, 5005);
	frame.node = &node;
	frame.position = { 1, 2, 3 };
	frame.rooms[0] = 7;
	frame.room_count = 1;
	frame.lifetime = 1.0f;
	effect_instance_consumer_t gameplay = {};
	char err[256] = {};
	REQUIRE(effectGameplayRuntimeBuildConsumer(&gameplay, err, sizeof(err)) == 0);
	REQUIRE(gameplay.begin(&frame, err, sizeof(err)) == 0);
	frame.node_index = 3;
	frame.execution_index = 7;
	REQUIRE(gameplay.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](&frame, err,
		sizeof(err)) == 0);
	params[1].f_value = 24.0f;
	frame.node_index = 4;
	frame.execution_index = 8;
	REQUIRE(gameplay.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](&frame, err,
		sizeof(err)) == 0);
	REQUIRE(gameplay.prepare(&frame, err, sizeof(err)) == 0);
	gameplay.commit(&frame);
	REQUIRE(effectGameplayRuntimeParticleCount() == 2);
	REQUIRE(effectPresentationRenderableParticleCount() == 2);
	effect_gameplay_particle_snapshot_t first = {};
	effect_gameplay_particle_snapshot_t second = {};
	REQUIRE(effectGameplayRuntimeParticleSnapshot(0, &first) == 1);
	REQUIRE(effectGameplayRuntimeParticleSnapshot(1, &second) == 1);
	REQUIRE(first.node_index != second.node_index);
	REQUIRE(first.size == Approx(12.0f));
	REQUIRE(second.size == Approx(24.0f));
	gameplay.cleanup(5005);
	REQUIRE(effectPresentationRenderableParticleCount() == 0);
	testStubAssetCatalogResolveWith(nullptr);
}

TEST_CASE("B-1014 shader identifiers select only real fixed renderer pipelines",
	"[modding][pdxxx][effect][presentation][shader][b1014][t-assets-035]")
{
	REQUIRE(effectPresentationShaderSupported("effect.tint", "classic_tint") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.glow", "classic_glow") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.shimmer", "classic_shimmer") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.darken", "classic_darken") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.screen", "classic_screen") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.particle", "classic_particle") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.explosion",
		"needler_pink_burst") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.spark",
		"needler_pink_burst") == 1);
	REQUIRE(effectPresentationShaderSupported("effect.tint", "classic_glow") == 0);
	REQUIRE(effectPresentationShaderSupported("effect.screen", "unknown_shader") == 0);
}

TEST_CASE("B-1014 material projection changes every production color response field",
	"[modding][pdxxx][effect][presentation][material][b1014][t-assets-035]")
{
	const f32 source[4] = { 0.1f, 0.4f, 0.8f, 0.5f };
	f32 plain[4] = {};
	f32 rough[4] = {};
	f32 metallic[4] = {};
	f32 emissive[4] = {};
	effectPresentationMaterialColor(source, "classic_lit", 0.0f, 0.0f, 0,
		plain);
	effectPresentationMaterialColor(source, "classic_lit", 0.8f, 0.0f, 0,
		rough);
	effectPresentationMaterialColor(source, "classic_lit", 0.0f, 0.8f, 0,
		metallic);
	effectPresentationMaterialColor(source, "classic_emissive", 0.0f, 0.0f,
		1, emissive);
	REQUIRE(plain[0] == Approx(source[0]));
	REQUIRE(plain[3] == Approx(source[3]));
	REQUIRE(rough[0] != Approx(plain[0]));
	REQUIRE(metallic[0] != Approx(plain[0]));
	REQUIRE(emissive[0] > plain[0]);
}

TEST_CASE("B-1014 gameplay visual observer retains glow and secondary tint",
	"[modding][pdxxx][effect][presentation][observer][b1014][t-assets-035]")
{
	effectPresentationRuntimeClearAll();
	weapon_graph_ir_param_t params[4] = {};
	setArray(params[0], "tint", "[1,0.4,0.8,1]");
	setArray(params[1], "tint2", "[0.2,0.1,1,0.5]");
	setFloat(params[2], "glow", 0.6f);
	setString(params[3], "shader", "needler_pink_burst");
	weapon_graph_ir_node_t node = {};
	std::strncpy(node.id, "burst", sizeof(node.id) - 1);
	std::strncpy(node.kind, "effect.explosion", sizeof(node.kind) - 1);
	node.opcode = WEAPON_GRAPH_OP_EFFECT_EXPLOSION;
	node.param_count = 4;
	effect_graph_runtime_t runtime = {};
	runtime.program.nodes = &node;
	runtime.program.node_count = 1;
	runtime.program.params = params;
	runtime.program.param_count = 4;
	effect_instance_frame_t frame = makeFrame(runtime, 6006);
	frame.node = &node;
	frame.target_name = "world";
	frame.position = { 6, 7, 8 };
	effect_instance_consumer_t consumer = {};
	char err[256] = {};
	REQUIRE(effectPresentationRuntimeBuildConsumer(&consumer, err, sizeof(err)) == 0);
	REQUIRE(consumer.begin(&frame, err, sizeof(err)) == 0);
	REQUIRE(consumer.nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION](&frame, err,
		sizeof(err)) == 0);
	REQUIRE(consumer.prepare(&frame, err, sizeof(err)) == 0);
	consumer.commit(&frame);
	effect_presentation_command_t command = {};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &command) == 1);
	REQUIRE(command.kind == EFFECT_PRESENTATION_GLOW);
	REQUIRE(command.channel == EFFECT_PRESENTATION_CHANNEL_LIGHT);
	REQUIRE(command.glow == Approx(0.6f));
	REQUIRE(command.rgba[1] == Approx(0.4f));
	REQUIRE(command.secondary_rgba[0] == Approx(0.2f));
	REQUIRE(command.secondary_rgba[2] == Approx(1.0f));
	REQUIRE(std::string(command.shader_id) == "needler_pink_burst");
	consumer.cleanup(6006);
	effectPresentationRuntimeClearAll();
}

TEST_CASE("B-1014 presentation consumer is connected to the production renderer",
	"[modding][pdxxx][effect][presentation][static][b1014][t-assets-035]")
{
	std::ifstream in("port/fast3d/pdgui_backend.cpp", std::ios::binary);
	REQUIRE(in.good());
	std::string source((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("effectPresentationRuntimeHasActive()") != std::string::npos);
	REQUIRE(source.find("effectPresentationRuntimeRender((s32)winW, (s32)winH)") !=
		std::string::npos);
	REQUIRE(source.find("command.time * command.speed") != std::string::npos);
	REQUIRE(source.find("command.texture_ref[0]") != std::string::npos);
	REQUIRE(source.find("effectGraphResolveExplosionType") == std::string::npos);
	REQUIRE(source.find("effectGraphResolveSparkType") == std::string::npos);

	std::ifstream particleIn("src/game/effect_presentation_renderer.c",
		std::ios::binary);
	REQUIRE(particleIn.good());
	std::string particle((std::istreambuf_iterator<char>(particleIn)),
		std::istreambuf_iterator<char>());
	REQUIRE(particle.find("effectGameplayRuntimeParticleSnapshot") !=
		std::string::npos);
	REQUIRE(particle.find("for (size_t i = 0; i < count; i++)") !=
		std::string::npos);
	REQUIRE(particle.find("texture.texturenum = (texnum_t)particle.texture_num") !=
		std::string::npos);
	REQUIRE(particle.find("material_color[3] * particle->intensity") !=
		std::string::npos);
	REQUIRE(particle.find("1.0f + particle.glow") != std::string::npos);
	REQUIRE(particle.find("effectPresentationRenderScreenTexture") !=
		std::string::npos);
	REQUIRE(particle.find("roomFlashLocalLighting") != std::string::npos);
	REQUIRE(particle.find("EFFECT_PRESENTATION_CHANNEL_BEAM") !=
		std::string::npos);
	REQUIRE(particle.find("command->primary_direction") != std::string::npos);
	REQUIRE(particle.find("particle.material_roughness") != std::string::npos);
	REQUIRE(particle.find("command.shader_id, \"needler_pink_burst\"") !=
		std::string::npos);
	REQUIRE(particle.find("1.75f, 0.35f") != std::string::npos);
	REQUIRE(particle.find("g_TexSparkConfigs") == std::string::npos);
	REQUIRE(particle.find("g_TexSmokeConfigs") == std::string::npos);

	std::ifstream lvIn("src/game/lv.c", std::ios::binary);
	REQUIRE(lvIn.good());
	std::string lv((std::istreambuf_iterator<char>(lvIn)),
		std::istreambuf_iterator<char>());
	const auto sparks = lv.find("gdl = sparksRender(gdl);");
	const auto authoredParticles = lv.find(
		"gdl = effectPresentationRenderWorld(gdl);");
	REQUIRE(sparks != std::string::npos);
	REQUIRE(authoredParticles != std::string::npos);
	REQUIRE(authoredParticles > sparks);
}
