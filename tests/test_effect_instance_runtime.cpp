#include "catch.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "effect_instance_runtime.h"
#include "weapon_graph_runtime.h"
}

namespace {

struct LaneState {
	std::vector<std::string> nodes;
	std::vector<float> samples;
	s32 begins = 0;
	s32 updates = 0;
	s32 prepares = 0;
	s32 commits = 0;
	s32 rollbacks = 0;
	s32 cleanups = 0;
	s32 fail_node = 0;
	s32 fail_begin = 0;
	s32 fail_update = 0;
	s32 fail_prepare = 0;
	u64 last_id = 0;
	effect_instance_vec3_t last_position = {};
	std::string last_target;
	std::string last_attachment;
	size_t last_context_count = 0;
};

LaneState g_lanes[2];

s32 laneBegin(s32 lane, const effect_instance_frame_t *frame) {
	g_lanes[lane].begins++;
	g_lanes[lane].last_id = frame->instance_id;
	return g_lanes[lane].fail_begin ? -1 : 0;
}
s32 lane0Begin(const effect_instance_frame_t *f, char *, size_t) { return laneBegin(0, f); }
s32 lane1Begin(const effect_instance_frame_t *f, char *, size_t) { return laneBegin(1, f); }

s32 laneUpdate(s32 lane, const effect_instance_frame_t *frame) {
	g_lanes[lane].updates++;
	g_lanes[lane].last_id = frame->instance_id;
	return g_lanes[lane].fail_update ? -1 : 0;
}
s32 lane0Update(const effect_instance_frame_t *f, char *, size_t) { return laneUpdate(0, f); }
s32 lane1Update(const effect_instance_frame_t *f, char *, size_t) { return laneUpdate(1, f); }

s32 lanePrepare(s32 lane, const effect_instance_frame_t *) {
	g_lanes[lane].prepares++;
	return g_lanes[lane].fail_prepare ? -1 : 0;
}
s32 lane0Prepare(const effect_instance_frame_t *f, char *, size_t) { return lanePrepare(0, f); }
s32 lane1Prepare(const effect_instance_frame_t *f, char *, size_t) { return lanePrepare(1, f); }

void laneCommit(s32 lane, const effect_instance_frame_t *) { g_lanes[lane].commits++; }
void lane0Commit(const effect_instance_frame_t *f) { laneCommit(0, f); }
void lane1Commit(const effect_instance_frame_t *f) { laneCommit(1, f); }

void laneRollback(s32 lane, const effect_instance_frame_t *, s32) {
	g_lanes[lane].rollbacks++;
}
void lane0Rollback(const effect_instance_frame_t *f, s32 n) { laneRollback(0, f, n); }
void lane1Rollback(const effect_instance_frame_t *f, s32 n) { laneRollback(1, f, n); }

void laneCleanup(s32 lane, u64 id) {
	g_lanes[lane].cleanups++;
	g_lanes[lane].last_id = id;
}
void lane0Cleanup(u64 id) { laneCleanup(0, id); }
void lane1Cleanup(u64 id) { laneCleanup(1, id); }

s32 laneNode(s32 lane, const effect_instance_frame_t *frame) {
	REQUIRE(frame->node != nullptr);
	g_lanes[lane].nodes.emplace_back(frame->node->id);
	g_lanes[lane].last_position = frame->position;
	g_lanes[lane].last_target = frame->target_name ? frame->target_name : "";
	g_lanes[lane].last_attachment = frame->attachment ? frame->attachment : "";
	g_lanes[lane].last_context_count = frame->context_count;
	float sampled = -1.0f;
	if (effectInstanceTimelineSample(frame, "intensity", &sampled)) {
		g_lanes[lane].samples.push_back(sampled);
	}
	return g_lanes[lane].fail_node ? -1 : 0;
}
s32 lane0Node(const effect_instance_frame_t *f, char *, size_t) { return laneNode(0, f); }
s32 lane1Node(const effect_instance_frame_t *f, char *, size_t) { return laneNode(1, f); }

void installConsumers(bool omitSmoke = false, bool observeExplosion = false) {
	g_lanes[0] = LaneState{};
	g_lanes[1] = LaneState{};
	effect_instance_consumer_t consumers[2] = {};
	consumers[0].name = "presentation-test";
	consumers[0].begin = lane0Begin;
	consumers[0].update = lane0Update;
	consumers[0].prepare = lane0Prepare;
	consumers[0].commit = lane0Commit;
	consumers[0].rollback = lane0Rollback;
	consumers[0].cleanup = lane0Cleanup;
	for (s32 slot = EFFECT_GRAPH_DISPATCH_TINT;
			slot <= EFFECT_GRAPH_DISPATCH_SCREEN; slot++) {
		consumers[0].nodes[slot] = lane0Node;
	}
	if (observeExplosion) {
		consumers[0].nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION] = lane0Node;
	}
	consumers[1].name = "gameplay-test";
	consumers[1].begin = lane1Begin;
	consumers[1].update = lane1Update;
	consumers[1].prepare = lane1Prepare;
	consumers[1].commit = lane1Commit;
	consumers[1].rollback = lane1Rollback;
	consumers[1].cleanup = lane1Cleanup;
	for (s32 slot = EFFECT_GRAPH_DISPATCH_PARTICLE;
			slot < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; slot++) {
		consumers[1].nodes[slot] = lane1Node;
	}
	if (omitSmoke) consumers[1].nodes[EFFECT_GRAPH_DISPATCH_SMOKE] = nullptr;
	effectInstanceRuntimeSetTestConsumers(consumers, 2);
}

void resetRuntime() {
	effectInstanceRuntimeClearAll();
	effectGraphRuntimeClearAll();
	effectInstanceRuntimeSetTestConsumers(nullptr, 0);
	weaponGraphRuntimeSetEnabled(1);
}

const std::string kGraph =
	"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:instance_fx\","
	"\"shared_context\":[{\"name\":\"impact\",\"scope\":\"call\","
	"\"source\":\"target\",\"type\":\"vec3\",\"lifetime\":\"effect\"}],"
	"\"nodes\":["
	"{\"id\":\"glow\",\"kind\":\"effect.glow\",\"subgraph\":\"main\","
	"\"params\":{\"target\":\"impact\",\"attachment\":\"point\","
	"\"lifetime\":1.0,\"priority\":7}},"
	"{\"id\":\"spark\",\"kind\":\"effect.spark\",\"subgraph\":\"main\",\"params\":{}},"
	"{\"id\":\"smoke\",\"kind\":\"effect.smoke\",\"subgraph\":\"tail\","
	"\"params\":{\"smoke_class\":\"small\"}}],"
	"\"subgraphs\":[{\"id\":\"main\",\"entry\":\"glow\"},{\"id\":\"tail\",\"entry\":\"smoke\"}],"
	"\"edges\":[{\"from\":\"glow\",\"to\":\"spark\"},{\"from\":\"spark\",\"to\":\"smoke\"}],"
	"\"exports\":[{\"name\":\"primary\",\"node\":\"glow\"}]}";

void registerGraph() {
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:instance_fx", kGraph.data(),
		static_cast<u32>(kGraph.size()), err, sizeof(err)) == 0);
}

effect_instance_request_t request() {
	effect_instance_request_t result = {};
	result.asset_id = "modx:instance_fx";
	result.position = { 1.0f, 2.0f, 3.0f };
	result.source_position = { 4.0f, 5.0f, 6.0f };
	result.target_position = { 7.0f, 8.0f, 9.0f };
	result.primary_direction = { 0.0f, 1.0f, 0.0f };
	result.secondary_direction = { 1.0f, 0.0f, 0.0f };
	result.rooms[0] = 12;
	result.room_count = 1;
	result.playernum = 2;
	return result;
}

void attachTimeline() {
	auto *runtime = const_cast<effect_graph_runtime_t *>(
		effectGraphRuntimeGet("modx:instance_fx"));
	REQUIRE(runtime != nullptr);
	runtime->program.timeline.keys = static_cast<pd_effect_timeline_key_t *>(
		std::calloc(2, sizeof(pd_effect_timeline_key_t)));
	REQUIRE(runtime->program.timeline.keys != nullptr);
	runtime->program.timeline.count = 2;
	runtime->program.timeline.keys[0].time = 0.0f;
	std::strcpy(runtime->program.timeline.keys[0].property, "intensity");
	runtime->program.timeline.keys[0].value = 0.0f;
	runtime->program.timeline.keys[1].time = 1.0f;
	std::strcpy(runtime->program.timeline.keys[1].property, "intensity");
	runtime->program.timeline.keys[1].value = 1.0f;
	runtime->program.kind = EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE;
}

} // namespace

TEST_CASE("effect instances resolve contexts and replay selected topology transactionally",
	"[modding][pdxxx][effect_instance][t-assets-036]") {
	resetRuntime();
	registerGraph();
	installConsumers();
	auto spawn = request();
	u64 id = 0;
	char err[256] = {};
	INFO(err);
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, &id, err, sizeof(err)) == 0);
	REQUIRE(id != 0);
	REQUIRE(effectInstanceRuntimeCount() == 1);
	REQUIRE(g_lanes[0].nodes == std::vector<std::string>{ "glow" });
	REQUIRE(g_lanes[1].nodes == std::vector<std::string>{ "spark", "smoke" });
	REQUIRE(g_lanes[0].last_target == "impact");
	REQUIRE(g_lanes[0].last_attachment == "point");
	REQUIRE(g_lanes[0].last_position.x == Approx(7.0f));
	REQUIRE(g_lanes[0].begins == 1);
	REQUIRE(g_lanes[1].prepares == 1);
	REQUIRE(g_lanes[0].commits == 1);

	effectInstanceRuntimeTick(0.25f);
	REQUIRE(g_lanes[0].nodes == std::vector<std::string>{ "glow", "glow" });
	REQUIRE(g_lanes[1].nodes ==
		std::vector<std::string>{ "spark", "smoke", "spark", "smoke" });
	REQUIRE(g_lanes[0].updates == 1);
	REQUIRE(g_lanes[1].updates == 1);
	REQUIRE(g_lanes[0].commits == 2);
	resetRuntime();
}

TEST_CASE("effect instance export and subgraph roots select only reachable authored nodes",
	"[modding][pdxxx][effect_instance][t-assets-036]") {
	resetRuntime();
	registerGraph();
	installConsumers();
	char err[256] = {};
	auto spawn = request();
	spawn.subgraph_id = "main";
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(g_lanes[0].nodes == std::vector<std::string>{ "glow" });
	REQUIRE(g_lanes[1].nodes == std::vector<std::string>{ "spark" });
	effectInstanceRuntimeClearAll();
	installConsumers();
	spawn.subgraph_id = nullptr;
	spawn.export_name = "missing";
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("does not exist") != std::string::npos);
	REQUIRE(g_lanes[0].begins == 0);
	resetRuntime();
}

TEST_CASE("effect instance preflight and lane failures publish no partial transaction",
	"[modding][pdxxx][effect_instance][t-assets-036][rollback][fail-closed]") {
	resetRuntime();
	registerGraph();
	installConsumers(true);
	auto spawn = request();
	char err[256] = {};
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(g_lanes[0].begins == 0);
	REQUIRE(g_lanes[1].commits == 0);

	installConsumers();
	g_lanes[1].fail_node = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(g_lanes[0].rollbacks == 1);
	REQUIRE(g_lanes[1].rollbacks == 1);
	REQUIRE(g_lanes[0].commits == 0);
	REQUIRE(g_lanes[1].commits == 0);

	installConsumers();
	g_lanes[1].fail_prepare = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(g_lanes[0].rollbacks == 1);
	REQUIRE(g_lanes[1].rollbacks == 1);
	REQUIRE(g_lanes[0].commits == 0);

	installConsumers();
	g_lanes[0].fail_begin = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(g_lanes[0].rollbacks == 1);
	REQUIRE(g_lanes[1].begins == 0);
	REQUIRE(g_lanes[0].commits == 0);

	installConsumers();
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(effectInstanceRuntimeCount() == 1);
	g_lanes[1].fail_update = 1;
	effectInstanceRuntimeTick(0.1f);
	REQUIRE(effectInstanceRuntimeCount() == 0);
	REQUIRE(g_lanes[0].commits == 1);
	REQUIRE(g_lanes[1].commits == 1);
	REQUIRE(g_lanes[0].rollbacks == 1);
	REQUIRE(g_lanes[1].rollbacks == 1);
	resetRuntime();
}

TEST_CASE("effect timeline interpolates on ticks and expires with deterministic cleanup",
	"[modding][pdxxx][effect_instance][t-assets-036][timeline][lifetime]") {
	resetRuntime();
	registerGraph();
	attachTimeline();
	installConsumers();
	auto spawn = request();
	char err[256] = {};
	u64 id = 0;
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, &id, err, sizeof(err)) == 0);
	REQUIRE(g_lanes[0].samples.back() == Approx(0.0f));
	effectInstanceRuntimeTick(0.5f);
	REQUIRE(g_lanes[0].samples.back() == Approx(0.5f));
	REQUIRE(effectInstanceRuntimeCount() == 1);
	effectInstanceRuntimeTick(0.5f);
	REQUIRE(g_lanes[0].samples.back() == Approx(1.0f));
	REQUIRE(effectInstanceRuntimeCount() == 0);
	REQUIRE(g_lanes[0].cleanups == 1);
	REQUIRE(g_lanes[1].cleanups == 1);
	REQUIRE(g_lanes[0].last_id == id);
	resetRuntime();
}

TEST_CASE("effect source retirement cancels live instances before program release",
	"[modding][pdxxx][effect_instance][t-assets-036][cleanup]") {
	resetRuntime();
	registerGraph();
	installConsumers();
	auto spawn = request();
	char err[256] = {};
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(effectInstanceRuntimeCount() == 1);
	effectGraphRuntimeReleaseOwner("modx:instance_fx");
	REQUIRE(effectInstanceRuntimeCount() == 0);
	REQUIRE(g_lanes[0].cleanups == 1);
	REQUIRE(effectGraphRuntimeGet("modx:instance_fx") == nullptr);
	resetRuntime();
}

TEST_CASE("effect activation rejects unresolved targets and invalid lifetime policy",
	"[modding][pdxxx][effect_instance][t-assets-036][activation][fail-closed]") {
	resetRuntime();
	char err[256] = {};
	const std::string unresolved =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:bad_target\","
		"\"nodes\":[{\"id\":\"g\",\"kind\":\"effect.glow\","
		"\"params\":{\"target\":\"missing_context\"}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:bad_target", unresolved.data(),
		static_cast<u32>(unresolved.size()), err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unresolved") != std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:bad_target") == nullptr);

	const std::string invalidLifetime =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:bad_lifetime\","
		"\"nodes\":[{\"id\":\"g\",\"kind\":\"effect.glow\","
		"\"params\":{\"lifetime\":\"forever\"}}],\"edges\":[]}";
	std::memset(err, 0, sizeof(err));
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:bad_lifetime",
		invalidLifetime.data(), static_cast<u32>(invalidLifetime.size()),
		err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("lifetime") != std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:bad_lifetime") == nullptr);

	const std::string collapsedSmoke =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:huge_smoke\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"huge\"}}],\"edges\":[]}";
	std::memset(err, 0, sizeof(err));
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:huge_smoke",
		collapsedSmoke.data(), static_cast<u32>(collapsedSmoke.size()),
		err, sizeof(err)) != 0);
	REQUIRE(err[0] != '\0');
	REQUIRE(effectGraphRuntimeGet("modx:huge_smoke") == nullptr);

	const std::string unsupportedVisual =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:bad_screen\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.screen\","
		"\"params\":{\"target\":\"world\"}}],\"edges\":[]}";
	std::memset(err, 0, sizeof(err));
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:bad_screen",
		unsupportedVisual.data(), static_cast<u32>(unsupportedVisual.size()),
		err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("requires a screen target") != std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:bad_screen") == nullptr);

	const std::vector<std::pair<std::string, std::string>> semanticFailures = {
		{ "bad_shader",
			"{\"id\":\"g\",\"kind\":\"effect.glow\",\"params\":{"
			"\"shader\":\"classic_screen\"}}" },
		{ "particle_attachment",
			"{\"id\":\"p\",\"kind\":\"effect.particle\",\"params\":{"
			"\"texture_ref\":\"modx:tex\",\"attachment\":\"point\"}}" },
		{ "duplicate_smoke_selector",
			"{\"id\":\"m\",\"kind\":\"effect.smoke\",\"params\":{"
			"\"smoke_class\":\"small\",\"smoke_type\":4}}" },
		{ "duplicate_spark_color",
			"{\"id\":\"s\",\"kind\":\"effect.spark\",\"params\":{"
			"\"tint2\":[1,1,1],\"tint_secondary\":[1,1,1]}}" },
	};
	for (const auto &failure : semanticFailures) {
		const std::string id = "modx:" + failure.first;
		const std::string graph =
			"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"" + id + "\","
			"\"nodes\":[" + failure.second + "],\"edges\":[]}";
		std::memset(err, 0, sizeof(err));
		INFO(failure.first << ": " << err);
		REQUIRE(effectGraphRuntimeRegisterGraphJson(id.c_str(), graph.data(),
			static_cast<u32>(graph.size()), err, sizeof(err)) != 0);
		REQUIRE(effectGraphRuntimeGet(id.c_str()) == nullptr);
	}
	resetRuntime();
}

TEST_CASE("effect public topology and contexts grow beyond retired IR ceilings",
	"[modding][pdxxx][effect_instance][t-assets-036][capacity]") {
	resetRuntime();
	std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:wide_fx\","
		"\"shared_context\":[";
	for (int i = 0; i < 24; i++) {
		if (i) graph += ',';
		graph += "{\"name\":\"ctx" + std::to_string(i) +
			"\",\"scope\":\"call\",\"source\":\"position\","
			"\"type\":\"vec3\",\"lifetime\":\"effect\"}";
	}
	graph += "],\"nodes\":[{\"id\":\"glow\",\"kind\":\"effect.glow\","
		"\"subgraph\":\"sg0\",\"params\":{}}],\"subgraphs\":[";
	for (int i = 0; i < 24; i++) {
		if (i) graph += ',';
		graph += "{\"id\":\"sg" + std::to_string(i) +
			"\",\"entry\":\"glow\"}";
	}
	graph += "],\"edges\":[],\"exports\":[";
	for (int i = 0; i < 24; i++) {
		if (i) graph += ',';
		graph += "{\"name\":\"export" + std::to_string(i) +
			"\",\"node\":\"glow\"}";
	}
	graph += "]}";
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:wide_fx", graph.data(),
		static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
	const auto *runtime = effectGraphRuntimeGet("modx:wide_fx");
	REQUIRE(runtime != nullptr);
	REQUIRE(runtime->program.context_count == 24);
	REQUIRE(runtime->program.subgraph_count == 24);
	REQUIRE(runtime->program.export_count == 24);
	installConsumers();
	effect_instance_request_t spawn = {};
	spawn.asset_id = "modx:wide_fx";
	spawn.export_name = "export23";
	spawn.position = { 3.0f, 4.0f, 5.0f };
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(g_lanes[0].last_context_count == 24);
	resetRuntime();
}

TEST_CASE("effect target teardown synchronously cancels retained state before reuse",
	"[modding][pdxxx][effect_instance][t-assets-036][target-lifetime]") {
	resetRuntime();
	registerGraph();
	installConsumers();
	int target = 7;
	auto spawn = request();
	spawn.target_prop = &target;
	char err[256] = {};
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(effectInstanceRuntimeCount() == 1);
	effectInstanceRuntimeCancelEntity(&target);
	REQUIRE(effectInstanceRuntimeCount() == 0);
	REQUIRE(g_lanes[0].cleanups == 1);
	REQUIRE(g_lanes[1].cleanups == 1);
	resetRuntime();
}

TEST_CASE("effect activation schema maps every accepted field to a production owner",
	"[modding][pdxxx][effect_instance][t-assets-036][field-schema]") {
	resetRuntime();
	REQUIRE(effectInstanceParamSchemaCount() > 0);
	for (size_t i = 0; i < effectInstanceParamSchemaCount(); i++) {
		weapon_graph_opcode_e opcode = static_cast<weapon_graph_opcode_e>(0);
		const char *key = nullptr;
		effect_instance_param_owner_t owner = EFFECT_INSTANCE_PARAM_UNSUPPORTED;
		INFO("schema row " << i);
		REQUIRE(effectInstanceParamSchemaEntry(i, &opcode, &key, &owner) == 1);
		REQUIRE(key != nullptr);
		REQUIRE(key[0] != '\0');
		REQUIRE(owner != EFFECT_INSTANCE_PARAM_UNSUPPORTED);
		if (opcode != static_cast<weapon_graph_opcode_e>(0)) {
			REQUIRE(effectInstanceParamOwner(opcode, key) == owner);
		}
	}

	/* Each mutually-exclusive alias gets its own node so every accepted field
	 * is activated at least once without hiding one behind another. */
	const std::string allFields =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:all_fields\","
		"\"nodes\":["
		"{\"id\":\"t\",\"kind\":\"effect.tint\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"point\",\"priority\":3,"
		"\"lifetime\":1.0,\"tint\":[1,0.5,0.25,1],"
		"\"material_ref\":\"modx:mat\",\"intensity\":0.5,\"size\":2,"
		"\"shader\":\"classic_tint\"}},"
		"{\"id\":\"g\",\"kind\":\"effect.glow\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"point\",\"tint\":[1,1,1],"
		"\"material_ref\":\"modx:mat\",\"intensity\":1,\"size\":2,"
		"\"shader\":\"classic_glow\"}},"
		"{\"id\":\"sh\",\"kind\":\"effect.shimmer\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"beam\",\"tint\":[1,1,1],"
		"\"material_ref\":\"modx:mat\",\"intensity\":1,\"size\":2,"
		"\"speed\":-2.5,\"width\":4,\"shader\":\"classic_shimmer\"}},"
		"{\"id\":\"d\",\"kind\":\"effect.darken\",\"params\":{"
		"\"target\":\"screen\",\"tint\":[0,0,0,1],\"material_ref\":\"modx:mat\","
		"\"intensity\":1,\"size\":2,\"shader\":\"classic_darken\"}},"
		"{\"id\":\"sc\",\"kind\":\"effect.screen\",\"params\":{"
		"\"target\":\"player\",\"tint\":[1,1,1],\"material_ref\":\"modx:mat\","
		"\"texture_ref\":\"modx:screen_tex\",\"intensity\":1,\"size\":2,"
		"\"shader\":\"classic_screen\"}},"
		"{\"id\":\"p\",\"kind\":\"effect.particle\",\"params\":{"
		"\"target\":\"position\",\"texture_ref\":\"modx:particle_tex\","
		"\"material_ref\":\"modx:mat\",\"tint\":[1,0,1,1],\"size\":2,"
		"\"intensity\":1,\"glow\":0.5,\"audio_catalog_id\":\"modx:sfx\","
		"\"shader\":\"classic_particle\"}},"
		"{\"id\":\"e\",\"kind\":\"effect.explosion\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"point\","
		"\"explosion_class\":\"small\",\"audio_catalog_id\":\"modx:sfx\","
		"\"tint\":[1,0,1,1],\"material_ref\":\"modx:mat\",\"intensity\":1,"
		"\"size\":2,\"glow\":0.5,\"shader\":\"needler_pink_burst\"}},"
		"{\"id\":\"s1\",\"kind\":\"effect.spark\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"point\",\"tint\":[1,0,1,1],"
		"\"tint2\":[1,1,1,1],\"audio_catalog_id\":\"modx:sfx\","
		"\"material_ref\":\"modx:mat\",\"intensity\":1,\"size\":2,\"glow\":0.5,"
		"\"shader\":\"classic_tint\"}},"
		"{\"id\":\"s2\",\"kind\":\"effect.spark\",\"params\":{"
		"\"tint\":[1,0,1,1],\"tint_secondary\":[1,1,1,1]}},"
		"{\"id\":\"m1\",\"kind\":\"effect.smoke\",\"params\":{"
		"\"target\":\"position\",\"attachment\":\"point\",\"smoke_class\":\"small\","
		"\"audio_catalog_id\":\"modx:sfx\",\"tint\":[1,1,1,1],"
		"\"material_ref\":\"modx:mat\",\"intensity\":1,\"size\":2,\"glow\":0.5,"
		"\"shader\":\"classic_tint\"}},"
		"{\"id\":\"m2\",\"kind\":\"effect.smoke\",\"params\":{\"class\":\"small\"}},"
		"{\"id\":\"m3\",\"kind\":\"effect.smoke\",\"params\":{\"smoke_type\":4}}],"
		"\"edges\":[]}";
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:all_fields",
		allFields.data(), static_cast<u32>(allFields.size()), err, sizeof(err)) == 0);
	resetRuntime();
}

TEST_CASE("effect activation rejects an unknown field on every opcode",
	"[modding][pdxxx][effect_instance][t-assets-036][field-schema][fail-closed]") {
	resetRuntime();
	struct OpcodeCase { const char *kind; const char *required; };
	static const OpcodeCase cases[] = {
		{ "tint", "" }, { "glow", "" }, { "shimmer", "" },
		{ "darken", "" }, { "screen", "" },
		{ "particle", "\"texture_ref\":\"modx:tex\"," },
		{ "explosion", "\"explosion_class\":\"small\"," },
		{ "spark", "" }, { "smoke", "\"smoke_class\":\"small\"," },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const std::string id = std::string("modx:unknown_") + cases[i].kind;
		const std::string graph =
			"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"" + id + "\","
			"\"nodes\":[{\"id\":\"n\",\"kind\":\"effect." + cases[i].kind +
			"\",\"params\":{" + cases[i].required + "\"unknown_field\":1}}],"
			"\"edges\":[]}";
		char err[256] = {};
		INFO(cases[i].kind << ": " << err);
		REQUIRE(effectGraphRuntimeRegisterGraphJson(id.c_str(), graph.data(),
			static_cast<u32>(graph.size()), err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("unsupported field unknown_field") !=
			std::string::npos);
		REQUIRE(effectGraphRuntimeGet(id.c_str()) == nullptr);
	}
	resetRuntime();
}

TEST_CASE("one effect node stages gameplay and presentation in the same transaction",
	"[modding][pdxxx][effect_instance][t-assets-036][multi-consumer][rollback]") {
	resetRuntime();
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:tinted_blast\","
		"\"nodes\":[{\"id\":\"blast\",\"kind\":\"effect.explosion\","
		"\"params\":{\"explosion_class\":\"small\","
		"\"tint\":[1.0,0.25,0.5,1.0],\"lifetime\":0.5}}],\"edges\":[]}";
	char err[256] = {};
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:tinted_blast", graph.data(),
		static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
	installConsumers(false, true);
	effect_instance_request_t spawn = {};
	spawn.asset_id = "modx:tinted_blast";
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) == 0);
	REQUIRE(g_lanes[0].nodes == std::vector<std::string>{ "blast" });
	REQUIRE(g_lanes[1].nodes == std::vector<std::string>{ "blast" });
	REQUIRE(g_lanes[0].commits == 1);
	REQUIRE(g_lanes[1].commits == 1);
	effectInstanceRuntimeClearAll();

	installConsumers(false, true);
	g_lanes[1].fail_prepare = 1;
	REQUIRE(effectInstanceRuntimeSpawn(&spawn, nullptr, err, sizeof(err)) != 0);
	REQUIRE(g_lanes[0].nodes == std::vector<std::string>{ "blast" });
	REQUIRE(g_lanes[1].nodes == std::vector<std::string>{ "blast" });
	REQUIRE(g_lanes[0].commits == 0);
	REQUIRE(g_lanes[1].commits == 0);
	REQUIRE(g_lanes[0].rollbacks == 1);
	REQUIRE(g_lanes[1].rollbacks == 1);
	resetRuntime();
}
