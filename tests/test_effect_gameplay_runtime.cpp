#include "catch.hpp"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "assetprovider.h"
#include "asset_runtime.h"
#include "constants.h"
#include "effect_gameplay_runtime.h"
#include "effect_presentation_runtime.h"

void sparksResetCustomTypes(void);
s32 sparksCustomTypeCount(void);

void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
void testStubEffectAudio(const char *id, s32 category, s32 sound_id);
void testStubEffectGameplayReset(void);
void testStubEffectGameplayCapacity(s32 explosions, s32 smokes,
	s32 props, s32 sounds);
s32 testStubEffectGameplayCount(s32 kind);
s32 testStubEffectGameplayLast(s32 kind);
}

namespace {

class GameplayMaterial {
public:
	GameplayMaterial(const char *id) {
		path = std::filesystem::temp_directory_path() /
			("pd2_effect_gameplay_" + std::to_string(++serial) + ".json");
		std::ofstream out(path, std::ios::binary);
		out << "{\"schema\":\"pd2.material.v1\",\"catalog_id\":\"" << id
			<< "\",\"shading_model\":\"classic_emissive\","
			"\"base_color\":[0.2,0.4,0.8,0.5],\"emissive\":true,"
			"\"roughness\":0.2,\"metallic\":0.1}";
	}
	~GameplayMaterial() {
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
	std::string fullPath() const { return path.string(); }
	std::string member() const { return path.filename().string(); }
private:
	std::filesystem::path path;
	static unsigned serial;
};
unsigned GameplayMaterial::serial = 0;

void activateGameplayMaterial(const char *id, GameplayMaterial &source) {
	asset_entry_t entry{};
	entry.type = ASSET_MATERIAL;
	entry.enabled = 1;
	std::snprintf(entry.id, sizeof(entry.id), "%s", id);
	std::snprintf(entry.ext.material.material_file,
		sizeof(entry.ext.material.material_file), "%s", source.member().c_str());
	REQUIRE(assetRuntimeActivateCatalogEntry(&entry, source.fullPath().c_str()) == 1);
	REQUIRE(assetRuntimeHydrateCatalogEntry(&entry) == 1);
}

void setString(weapon_graph_ir_param_t &param, const char *key,
	const char *value) {
	std::memset(&param, 0, sizeof(param));
	std::snprintf(param.key, sizeof(param.key), "%s", key);
	param.type = WEAPON_GRAPH_PARAM_STRING;
	std::snprintf(param.value, sizeof(param.value), "%s", value);
}

void setNumber(weapon_graph_ir_param_t &param, const char *key, float value) {
	std::memset(&param, 0, sizeof(param));
	std::snprintf(param.key, sizeof(param.key), "%s", key);
	param.type = WEAPON_GRAPH_PARAM_FLOAT;
	param.f_value = value;
}

void setArray(weapon_graph_ir_param_t &param, const char *key,
	const char *value) {
	std::memset(&param, 0, sizeof(param));
	std::snprintf(param.key, sizeof(param.key), "%s", key);
	param.type = WEAPON_GRAPH_PARAM_ARRAY;
	std::snprintf(param.value, sizeof(param.value), "%s", value);
}

struct Fixture {
	effect_graph_runtime_t runtime{};
	weapon_graph_ir_param_t params[12]{};
	weapon_graph_ir_node_t node{};
	effect_instance_frame_t frame{};
	effect_instance_consumer_t consumer{};
	asset_entry_t texture{};
	char error[256]{};

	Fixture() {
		runtime.valid = 1;
		runtime.program.params = params;
		runtime.program.param_count = 12;
		frame.instance_id = 42;
		frame.runtime = &runtime;
		frame.node = &node;
		frame.position = {10, 20, 30};
		frame.primary_direction = {1, 0, 0};
		frame.secondary_direction = {0, 1, 0};
		frame.rooms[0] = 7;
		frame.room_count = 1;
		frame.lifetime = 2.0f;
		std::snprintf(texture.id, sizeof(texture.id), "%s", "modx:particle_tex");
		texture.type = ASSET_TEXTURE;
		texture.enabled = 1;
		texture.ext.texture.texture_id = -1;
		texture.source_texnum = TEXTURE_CUSTOM_START + 9;
		texture.source.primary.provider = reinterpret_cast<const asset_provider_t *>(1);
		testStubAssetCatalogResolveWith(&texture);
		testStubEffectGameplayReset();
		testStubEffectAudio("modx:blast", AUDIO_CAT_SFX, 777);
		REQUIRE(effectGameplayRuntimeBuildConsumer(&consumer,
			error, sizeof(error)) == 0);
		REQUIRE(consumer.begin(&frame, error, sizeof(error)) == 0);
	}

	void select(effect_graph_dispatch_slot_t slot, const char *kind,
		s32 start, s32 count) {
		std::memset(&node, 0, sizeof(node));
		std::snprintf(node.id, sizeof(node.id), "node_%d", slot);
		std::snprintf(node.kind, sizeof(node.kind), "%s", kind);
		node.param_start = start;
		node.param_count = count;
		frame.node = &node;
		frame.execution_index = slot;
	}
};

} // namespace

TEST_CASE("v1 gameplay consumer stages all emitters and exact catalog audio before commit",
	"[modding][pdxxx][effect_gameplay][t-assets-034]") {
	Fixture f;
	setString(f.params[0], "explosion_class", "small");
	setString(f.params[1], "audio_catalog_id", "modx:blast");
	f.select(EFFECT_GRAPH_DISPATCH_EXPLOSION, "effect.explosion", 0, 2);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION](
		&f.frame, f.error, sizeof(f.error)) == 0);

	f.select(EFFECT_GRAPH_DISPATCH_SPARK, "effect.spark", 2, 0);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_SPARK](
		&f.frame, f.error, sizeof(f.error)) == 0);

	setString(f.params[2], "smoke_class", "small");
	f.select(EFFECT_GRAPH_DISPATCH_SMOKE, "effect.smoke", 2, 1);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_SMOKE](
		&f.frame, f.error, sizeof(f.error)) == 0);

	setString(f.params[3], "texture_ref", "modx:particle_tex");
	setNumber(f.params[4], "size", 24.0f);
	setNumber(f.params[5], "intensity", 0.75f);
	setArray(f.params[6], "tint", "[0.25,0.5,0.75,1]");
	setNumber(f.params[7], "glow", 0.4f);
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 3, 5);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);

	REQUIRE(testStubEffectGameplayCount(1) == 0);
	REQUIRE(testStubEffectGameplayCount(2) == 0);
	REQUIRE(testStubEffectGameplayCount(3) == 0);
	REQUIRE(testStubEffectGameplayCount(4) == 0);
	testStubEffectGameplayCapacity(1, 1, 2, 1);
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) == 0);
	f.consumer.commit(&f.frame);
	REQUIRE(testStubEffectGameplayCount(1) == 1);
	REQUIRE(testStubEffectGameplayCount(2) == 1);
	REQUIRE(testStubEffectGameplayCount(3) == 1);
	REQUIRE(testStubEffectGameplayCount(4) == 1);
	REQUIRE(testStubEffectGameplayLast(1) == EXPLOSIONTYPE_EYESPY);
	REQUIRE(testStubEffectGameplayLast(3) == SMOKETYPE_SMALL);
	REQUIRE(testStubEffectGameplayLast(4) == 777);

	REQUIRE(effectGameplayRuntimeParticleCount() == 1);
	effect_gameplay_particle_snapshot_t particle{};
	REQUIRE(effectGameplayRuntimeParticleSnapshot(0, &particle) == 1);
	REQUIRE(particle.instance_id == 42);
	REQUIRE(particle.texture_num == TEXTURE_CUSTOM_START + 9);
	REQUIRE(particle.texture_num != f.texture.ext.texture.texture_id);
	REQUIRE(particle.size == Approx(24.0f));
	REQUIRE(particle.intensity == Approx(0.75f));
	REQUIRE(particle.tint[0] == Approx(0.25f));
	REQUIRE(particle.glow == Approx(0.4f));
	f.consumer.cleanup(42);
	REQUIRE(effectGameplayRuntimeParticleCount() == 0);
}

TEST_CASE("v1 gameplay prepare exhaustion rolls back with zero native emission",
	"[modding][pdxxx][effect_gameplay][t-assets-034]") {
	struct CapacityCase { s32 explosions, smokes, props, sounds; };
	const CapacityCase cases[] = {
		{0, 1, 2, 1}, {1, 0, 2, 1}, {1, 1, 1, 1}, {1, 1, 2, 0}
	};
	for (const auto &capacity : cases) {
		Fixture f;
		setString(f.params[0], "explosion_class", "small");
		setString(f.params[1], "audio_catalog_id", "modx:blast");
		f.select(EFFECT_GRAPH_DISPATCH_EXPLOSION, "effect.explosion", 0, 2);
		REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION](
			&f.frame, f.error, sizeof(f.error)) == 0);
		setString(f.params[2], "smoke_class", "small");
		f.select(EFFECT_GRAPH_DISPATCH_SMOKE, "effect.smoke", 2, 1);
		REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_SMOKE](
			&f.frame, f.error, sizeof(f.error)) == 0);
		testStubEffectGameplayCapacity(capacity.explosions, capacity.smokes,
			capacity.props, capacity.sounds);
		REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) != 0);
		REQUIRE(std::string(f.error).find("resources are exhausted") !=
			std::string::npos);
		/* Even a faulty caller cannot publish an unprepared transaction. */
		f.consumer.commit(&f.frame);
		REQUIRE(testStubEffectGameplayCount(1) == 0);
		REQUIRE(testStubEffectGameplayCount(3) == 0);
		REQUIRE(testStubEffectGameplayCount(4) == 0);
		f.consumer.rollback(&f.frame, 2);
	}
}

TEST_CASE("prepared custom spark rows roll back when a later lane fails",
	"[modding][pdxxx][effect_gameplay][rollback][t-assets-034]") {
	sparksResetCustomTypes();
	Fixture f;
	setArray(f.params[0], "tint", "[1,0.25,0.5,1]");
	f.select(EFFECT_GRAPH_DISPATCH_SPARK, "effect.spark", 0, 1);
	const s32 before = sparksCustomTypeCount();
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_SPARK](
		&f.frame, f.error, sizeof(f.error)) == 0);
	REQUIRE(sparksCustomTypeCount() == before);
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) == 0);
	REQUIRE(sparksCustomTypeCount() == before + 1);
	/* Mirrors the facade rollback when the following presentation lane's
	 * prepare fails: no spark/output consumed the reserved row. */
	f.consumer.rollback(&f.frame, 1);
	REQUIRE(sparksCustomTypeCount() == before);
	REQUIRE(testStubEffectGameplayCount(2) == 0);
	sparksResetCustomTypes();
}

TEST_CASE("production instance facade dispatches a v1 gameplay graph without native fallback",
	"[modding][pdxxx][effect_gameplay][integration][t-assets-034]") {
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:gameplay_spawn\","
		"\"nodes\":[{\"id\":\"boom\",\"kind\":\"effect.explosion\","
		"\"params\":{\"explosion_class\":\"small\","
		"\"audio_catalog_id\":\"modx:blast\",\"tint\":[0.2,0.4,0.8,1],"
		"\"intensity\":0.75,\"lifetime\":1}}],\"edges\":[]}";
	char error[256] = {};
	effect_instance_request_t request{};
	u64 instance = 0;

	effectInstanceRuntimeClearAll();
	effectGraphRuntimeClearAll();
	effectPresentationRuntimeClearAll();
	effectInstanceRuntimeSetTestConsumers(nullptr, 0);
	testStubEffectGameplayReset();
	testStubEffectGameplayCapacity(1, 0, 1, 1);
	testStubEffectAudio("modx:blast", AUDIO_CAT_SFX, 777);
	INFO(error);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:gameplay_spawn",
		graph.data(), static_cast<u32>(graph.size()), error, sizeof(error)) == 0);
	request.asset_id = "modx:gameplay_spawn";
	request.position = {10, 20, 30};
	request.source_position = request.position;
	request.target_position = request.position;
	request.rooms[0] = 7;
	request.room_count = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&request, &instance,
		error, sizeof(error)) == 0);
	REQUIRE(instance != 0);
	REQUIRE(testStubEffectGameplayCount(1) == 1);
	REQUIRE(testStubEffectGameplayCount(4) == 1);
	REQUIRE(testStubEffectGameplayLast(1) == EXPLOSIONTYPE_EYESPY);
	REQUIRE(testStubEffectGameplayLast(4) == 777);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 1);
	effect_presentation_command_t visual{};
	REQUIRE(effectPresentationRuntimeSnapshot(0, &visual) == 1);
	REQUIRE(visual.rgba[0] == Approx(0.2f));
	REQUIRE(visual.rgba[2] == Approx(0.8f));
	REQUIRE(visual.intensity == Approx(0.75f));
	effectInstanceRuntimeClearAll();
	effectGraphRuntimeClearAll();
	effectPresentationRuntimeClearAll();
}

TEST_CASE("later presentation prepare failure rolls back gameplay and audio",
	"[modding][pdxxx][effect_gameplay][integration][rollback][t-assets-034]") {
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:rollback_fx\","
		"\"nodes\":[{\"id\":\"boom\",\"kind\":\"effect.explosion\","
		"\"params\":{\"explosion_class\":\"small\","
		"\"audio_catalog_id\":\"modx:blast\",\"tint\":[1,0,0,1],"
		"\"material_ref\":\"modx:disabled_material\",\"lifetime\":1}}],"
		"\"edges\":[]}";
	char error[256] = {};
	effect_instance_request_t request{};

	effectInstanceRuntimeClearAll();
	effectGraphRuntimeClearAll();
	effectPresentationRuntimeClearAll();
	effectInstanceRuntimeSetTestConsumers(nullptr, 0);
	testStubEffectGameplayReset();
	testStubEffectGameplayCapacity(1, 0, 1, 1);
	testStubEffectAudio("modx:blast", AUDIO_CAT_SFX, 777);
	INFO(error);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:rollback_fx", graph.data(),
		static_cast<u32>(graph.size()), error, sizeof(error)) == 0);
	request.asset_id = "modx:rollback_fx";
	request.position = {10, 20, 30};
	request.source_position = request.position;
	request.target_position = request.position;
	request.rooms[0] = 7;
	request.room_count = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&request, nullptr,
		error, sizeof(error)) != 0);
	REQUIRE(testStubEffectGameplayCount(1) == 0);
	REQUIRE(testStubEffectGameplayCount(4) == 0);
	REQUIRE(effectPresentationRuntimeSnapshotCount() == 0);
	effectInstanceRuntimeClearAll();
	effectGraphRuntimeClearAll();
	effectPresentationRuntimeClearAll();
}

TEST_CASE("v1 particle revalidates exact public texture during prepare",
	"[modding][pdxxx][effect_gameplay][t-assets-034]") {
	Fixture f;
	setString(f.params[0], "texture_ref", "modx:particle_tex");
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 0, 1);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);
	f.texture.enabled = 0;
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) != 0);
	f.consumer.commit(&f.frame);
	REQUIRE(effectGameplayRuntimeParticleCount() == 0);
	f.consumer.rollback(&f.frame, 1);
}

TEST_CASE("v1 gameplay retains every particle node in one instance",
	"[modding][pdxxx][effect_gameplay][t-assets-034]") {
	Fixture f;
	setString(f.params[0], "texture_ref", "modx:particle_tex");
	setNumber(f.params[1], "size", 10.0f);
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 0, 2);
	f.frame.node_index = 3;
	f.frame.execution_index = 7;
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);
	setNumber(f.params[1], "size", 20.0f);
	f.frame.node_index = 4;
	f.frame.execution_index = 8;
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) == 0);
	f.consumer.commit(&f.frame);
	REQUIRE(effectGameplayRuntimeParticleCount() == 2);
	effect_gameplay_particle_snapshot_t first{};
	effect_gameplay_particle_snapshot_t second{};
	REQUIRE(effectGameplayRuntimeParticleSnapshot(0, &first) == 1);
	REQUIRE(effectGameplayRuntimeParticleSnapshot(1, &second) == 1);
	REQUIRE(first.instance_id == second.instance_id);
	REQUIRE(first.node_index == 3);
	REQUIRE(second.node_index == 4);
	REQUIRE(first.execution_index == 7);
	REQUIRE(second.execution_index == 8);
	REQUIRE(first.size == Approx(10.0f));
	REQUIRE(second.size == Approx(20.0f));
	f.consumer.cleanup(42);
	REQUIRE(effectGameplayRuntimeParticleCount() == 0);
}

TEST_CASE("v1 particle retains every hydrated public material field",
	"[modding][pdxxx][effect_gameplay][material][t-assets-034]") {
	assetRuntimeReset();
	GameplayMaterial material("modx:particle_material");
	activateGameplayMaterial("modx:particle_material", material);
	Fixture f;
	setString(f.params[0], "texture_ref", "modx:particle_tex");
	setString(f.params[1], "material_ref", "modx:particle_material");
	setArray(f.params[2], "tint", "[0.5,0.5,0.5,1]");
	setString(f.params[3], "shader", "classic_particle");
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 0, 4);
	f.frame.node_index = 5;
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) == 0);
	f.consumer.commit(&f.frame);
	effect_gameplay_particle_snapshot_t particle{};
	REQUIRE(effectGameplayRuntimeParticleSnapshot(0, &particle) == 1);
	REQUIRE(std::string(particle.material_shading_model) == "classic_emissive");
	REQUIRE(std::string(particle.shader_id) == "classic_particle");
	REQUIRE(particle.material_roughness == Approx(0.2f));
	REQUIRE(particle.material_metallic == Approx(0.1f));
	REQUIRE(particle.material_emissive == 1);
	REQUIRE(particle.tint[0] == Approx(0.1f));
	REQUIRE(particle.tint[2] == Approx(0.4f));
	f.consumer.cleanup(42);
	assetRuntimeReset();
}

TEST_CASE("v1 particle consumes descriptor and timeline intensity defaults",
	"[modding][pdxxx][effect_gameplay][timeline][t-assets-034]") {
	Fixture f;
	pd_effect_timeline_key_t keys[2]{};
	f.runtime.descriptor_intensity = 0.25f;
	std::snprintf(keys[0].property, sizeof(keys[0].property), "%s", "intensity");
	keys[0].time = 0.0f;
	keys[0].value = 0.5f;
	std::snprintf(keys[1].property, sizeof(keys[1].property), "%s", "intensity");
	keys[1].time = 1.0f;
	keys[1].value = 1.0f;
	f.runtime.program.timeline.keys = keys;
	f.runtime.program.timeline.count = 2;
	f.frame.time = 0.5f;
	setString(f.params[0], "texture_ref", "modx:particle_tex");
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 0, 1);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) == 0);
	REQUIRE(f.consumer.prepare(&f.frame, f.error, sizeof(f.error)) == 0);
	f.consumer.commit(&f.frame);
	effect_gameplay_particle_snapshot_t particle{};
	REQUIRE(effectGameplayRuntimeParticleSnapshot(0, &particle) == 1);
	REQUIRE(particle.intensity == Approx(0.75f));
	f.consumer.cleanup(42);
}

TEST_CASE("v1 particle tint rejects rather than truncating a fifth component",
	"[modding][pdxxx][effect_gameplay][t-assets-034]") {
	Fixture f;
	setString(f.params[0], "texture_ref", "modx:particle_tex");
	setArray(f.params[1], "tint", "[1,1,1,1,0]");
	f.select(EFFECT_GRAPH_DISPATCH_PARTICLE, "effect.particle", 0, 2);
	REQUIRE(f.consumer.nodes[EFFECT_GRAPH_DISPATCH_PARTICLE](
		&f.frame, f.error, sizeof(f.error)) != 0);
	REQUIRE(std::string(f.error).find("invalid particle tint") != std::string::npos);
	REQUIRE(effectGameplayRuntimeParticleCount() == 0);
	f.consumer.rollback(&f.frame, 1);
}

TEST_CASE("production effect callsites use the single instance facade and cancel prop owners",
	"[modding][pdxxx][effect_gameplay][static][t-assets-034]") {
	const auto root = std::filesystem::current_path();
	auto read = [](const std::filesystem::path &path) {
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), {});
	};
	const std::string propobj = read(root / "src/game/propobj.c");
	const std::string prop = read(root / "src/game/prop.c");
	const std::string explosions = read(root / "src/game/explosions.c");
	const std::string smoke = read(root / "src/game/smoke.c");
	const std::string smokeReset = read(root / "src/game/smokereset.c");
	const std::string propsnd = read(root / "src/game/propsnd.c");
	REQUIRE(propobj.find("effectGameplayRuntimeTriggerPropExplosion(") != std::string::npos);
	REQUIRE(propobj.find("effectGameplayRuntimeTriggerSpark(") != std::string::npos);
	REQUIRE(propobj.find("effectGraphResolveExplosionType(") == std::string::npos);
	REQUIRE(propobj.find("effectGraphResolveSparkType(") == std::string::npos);
	REQUIRE(propobj.find("bool propExplodeWithSound(") != std::string::npos);
	REQUIRE(propobj.find("explosionCreateWithSound(NULL, &position") !=
		std::string::npos);
	const auto freePos = prop.find("void propFree(struct prop *prop)");
	REQUIRE(freePos != std::string::npos);
	REQUIRE(prop.find("effectInstanceRuntimeCancelEntity(prop);", freePos) <
		prop.find("meshDetachFromProp(prop);", freePos));
	REQUIRE(explosions.find("explosionsReserveCreateCount") != std::string::npos);
	REQUIRE(explosions.find("exp->effect_soundnum") != std::string::npos);
	REQUIRE(smoke.find("arenaPoolSetup(&s_SmokePool") != std::string::npos);
	REQUIRE(smoke.find("smokesReserveCreateCount") != std::string::npos);
	REQUIRE(smokeReset.find("smokesSetupPool(g_MaxSmokes)") != std::string::npos);
	REQUIRE(propsnd.find("psReserveCreateCount") != std::string::npos);
}
