/*
 * tests/test_effect_graph_runtime.cpp -- c3849 Wave 6b Unit 8 (.pdeffect).
 *
 * Behavioral coverage for the effect graph compiler/runtime
 * (port/src/effect_graph_runtime.c), the custom spark-row registry
 * (src/game/sparks_custom.c), the nested .pdeffect ingestion path through
 * the weapon archive walk, and the four OG-executor bridges. Static pins
 * lock the ingestion arm, the canonical base-emitter schema, the three
 * sparkTypeFor read sites, and the catalog activation/clear hooks.
 *
 * NOTE: this file deliberately does NOT include types.h / game/sparks.h
 * (the `#define bool s32` macro collides with C++); the spark registry is
 * exercised through its layout-free s32/u32 accessors.
 */

#include "catch.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "constants.h"
#include "effect_graph_runtime.h"
#include "modarchive.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"

/* Spark registry accessors (game/sparks.h is C-only; see file note). */
s32 sparksRegisterCustomTintedType(u32 color1, u32 color2);
void sparksResetCustomTypes(void);
s32 sparksCustomTypeCount(void);
s32 sparksTypeColors(s32 typenum, u32 *color1, u32 *color2);
s32 sparksTypeClonesRowExceptColors(s32 typenum, s32 base_typenum);
void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
}

/* Mirror of SPARKTYPE_BASE_COUNT (game/sparks.h); the static pin below
 * keeps the literal honest. */
static const s32 kSparkBaseCount = 27;

/* Complete the source-builder's forward declarations without including the
 * C-only game headers. Layout matches the public native table definitions. */
struct explosiontype {
	float rangeh, rangev, changerateh, changeratev, innersize, blastradius,
		damageradius;
	int16_t duration, propagationrate;
	float flarespeed;
	uint8_t smoketype;
	uint16_t sound;
	float damage;
};

struct smoketype {
	int16_t duration, fadespeed, spreadspeed, size;
	float bgrotatespeed;
	uint8_t r, g, b;
	float fgrotatespeed;
	int16_t numclouds;
	float unk18, unk1c, unk20;
};

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

struct TempArchive {
	std::filesystem::path path;

	~TempArchive() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	}
};

TempArchive writeTypedArchiveEntries(const std::string &stem,
                                     const std::string &extension,
                                     const std::vector<std::pair<std::string, std::string>> &entries) {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-" + stem + "-" + std::to_string(stamp) + extension)
	};

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	for (const auto &entry : entries) {
		INFO("packing " << entry.first);
		if (modArchiveAddFileMem(writer, entry.first.c_str(),
				entry.second.data(), static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileMem failed for " << entry.first);
		}
	}
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

/* Base-emitter-shaped effect graph: the exact JSON shape s_emitEffect
 * (romextract_pdmeta.c) writes -- catalog_id spelling, no graph_id, one
 * effect.<type_key> node with shader/intensity params, empty edges. */
std::string baseEffectGraph(const std::string &schema, const std::string &id,
                            const std::string &typeKey) {
	return
		"{\n"
		"  \"schema\": \"" + schema + "\",\n"
		"  \"catalog_id\": \"" + id + "\",\n"
		"  \"effect\": \"" + typeKey + "\",\n"
		"  \"target\": \"scene\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"apply\", \"kind\": \"effect." + typeKey + "\", \"params\": { \"shader\": \"classic_" + typeKey + "\", \"intensity\": 0.500 } }\n"
		"  ],\n"
		"  \"edges\": []\n"
		"}\n";
}

/* Needler-shaped pink fixture: effect.explosion (class small + tint) ->
 * effect.spark (tint), with the tint array spread across lines the way
 * build_needler_mod.py's json.dumps(indent=2) emits it. */
std::string pinkEffectGraph(const std::string &id) {
	return
		"{\n"
		"  \"schema\": \"pd.effect_graph.v1\",\n"
		"  \"asset_id\": \"" + id + "\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"burst\",\n"
		"      \"kind\": \"effect.explosion\",\n"
		"      \"params\": {\n"
		"        \"explosion_class\": \"small\",\n"
		"        \"tint\": [\n"
		"          1.0,\n"
		"          0.4,\n"
		"          0.8,\n"
		"          1.0\n"
		"        ]\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"spark\",\n"
		"      \"kind\": \"effect.spark\",\n"
		"      \"params\": { \"tint\": [1.0, 0.5, 0.85, 1.0] }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [ { \"from\": \"burst\", \"to\": \"spark\" } ]\n"
		"}\n";
}

void effectRuntimeTestReset() {
	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
	effectGraphRuntimeClearAll();  /* also resets the spark registry */
}

s32 dispatchBegin(const effect_graph_dispatch_context_t *) { return 0; }
s32 dispatchCommit(const effect_graph_dispatch_context_t *) { return 0; }
void dispatchRollback(const effect_graph_dispatch_context_t *, s32) {}

s32 collectProgramNode(const effect_graph_dispatch_context_t *context,
	const weapon_graph_ir_node_t *node, s32) {
	auto *ids = static_cast<std::vector<std::string> *>(context->user);
	ids->emplace_back(node->id);
	return 0;
}

}  // namespace

TEST_CASE("effect graph compiler accepts all six base emitter shapes in both schemas",
          "[modding][pdxxx][effect_graph][c3849]") {
	static const char *kTypeKeys[6] = {
		"tint", "glow", "shimmer", "darken", "screen", "particle",
	};
	static const char *kSchemas[2] = {
		"pd.effect_graph.v1",   /* canonical (B6.4) */
		"pd2.effect.graph.v1",  /* legacy: stale archives; one-time warning */
	};

	for (const char *schema : kSchemas) {
		for (const char *typeKey : kTypeKeys) {
			const std::string id = std::string("base:fx_") + typeKey;
			const std::string graph = baseEffectGraph(schema, id, typeKey);

			weapon_graph_ir_t ir;
			char err[256] = {};
			INFO("schema " << schema << " type " << typeKey);
			REQUIRE(weaponGraphCompileJson(ASSET_EFFECT, graph.data(),
				static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
			/* catalog_id alias + defaulted graph_id. */
			REQUIRE(std::string(ir.asset_id) == id);
			REQUIRE(std::string(ir.graph_id) == "effect_graph");
			REQUIRE(ir.node_count == 1);
		}
	}

	/* The base shapes register as records carrying intensity, with every
	 * gameplay channel unresolved (presentation kinds are gameplay-inert). */
	effectRuntimeTestReset();
	char err[256] = {};
	const std::string graph =
		baseEffectGraph("pd.effect_graph.v1", "base:fx_glow", "glow");
	REQUIRE(effectGraphRuntimeRegisterGraphJson("base:fx_glow",
		graph.data(), static_cast<u32>(graph.size()), err, sizeof(err)) == 0);

	const effect_graph_runtime_t *record = effectGraphRuntimeGet("base:fx_glow");
	REQUIRE(record != nullptr);
	REQUIRE(record->has_intensity == 1);
	REQUIRE(record->intensity == Approx(0.5f));
	REQUIRE(record->has_explosion == 0);
	REQUIRE(record->has_spark == 0);
	REQUIRE(record->has_smoke == 0);
	REQUIRE(sparksCustomTypeCount() == 0);

	effectRuntimeTestReset();
}

TEST_CASE("effect graph compiler is deterministic and rejects bad input",
          "[modding][pdxxx][effect_graph][c3849]") {
	char err[256] = {};
	weapon_graph_ir_t first;
	weapon_graph_ir_t second;

	const std::string pink = pinkEffectGraph("modx:pink_fx");
	REQUIRE(weaponGraphCompileJson(ASSET_EFFECT, pink.data(),
		static_cast<u32>(pink.size()), &first, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_EFFECT, pink.data(),
		static_cast<u32>(pink.size()), &second, err, sizeof(err)) == 0);
	REQUIRE(std::string(first.ir_sha256) == std::string(second.ir_sha256));
	REQUIRE(std::string(first.source_sha256) == std::string(second.source_sha256));
	REQUIRE(first.ir_sha256[0] != '\0');

	/* Unknown / script-like node kinds are rejected. */
	const std::string unknownKind =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:bad\","
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.script\",\"params\":{}}],"
		"\"edges\":[]}";
	weapon_graph_ir_t ir;
	REQUIRE(weaponGraphCompileJson(ASSET_EFFECT, unknownKind.data(),
		static_cast<u32>(unknownKind.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsupported") != std::string::npos);

	/* Wrong-type schema strings are rejected (only the legacy effect
	 * spelling aliases). */
	const std::string wrongSchema =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"modx:bad\","
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.tint\",\"params\":{}}],"
		"\"edges\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_EFFECT, wrongSchema.data(),
		static_cast<u32>(wrongSchema.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("schema") != std::string::npos);

	/* Effect kinds stay out of the weapon module table. */
	const std::string effectKindInWeapon =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:bad\","
		"\"graph_id\":\"bad\",\"nodes\":[{\"id\":\"n\",\"kind\":\"effect.tint\","
		"\"params\":{}}],\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, effectKindInWeapon.data(),
		static_cast<u32>(effectKindInWeapon.size()), &ir, err, sizeof(err)) != 0);

	/* Registration-time asset id mismatch is loud. */
	effectRuntimeTestReset();
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:other_id",
		pink.data(), static_cast<u32>(pink.size()), err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("does not match") != std::string::npos);
	effectRuntimeTestReset();
}

TEST_CASE("pink effect fixture compiles to OG explosion class and a tinted spark row",
          "[modding][pdxxx][effect_graph][c3849]") {
	effectRuntimeTestReset();
	char err[256] = {};

	const std::string pink = pinkEffectGraph("modx:pink_fx");
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:pink_fx",
		pink.data(), static_cast<u32>(pink.size()), err, sizeof(err)) == 0);

	const effect_graph_runtime_t *record = effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(record != nullptr);

	/* explosion_class small -> EXPLOSIONTYPE_EYESPY (2); B2 class words are
	 * graph-body-only vocabulary. */
	REQUIRE(record->has_explosion == 1);
	REQUIRE(std::string(record->explosion_class) == "small");
	REQUIRE(record->explosion_type == EXPLOSIONTYPE_EYESPY);
	REQUIRE(record->explosion_type == 2);
	REQUIRE(record->explosion_tint[0] == Approx(1.0f));
	REQUIRE(record->explosion_tint[1] == Approx(0.4f));
	REQUIRE(record->explosion_tint[2] == Approx(0.8f));
	REQUIRE(record->explosion_tint[3] == Approx(1.0f));

	/* effect.spark tint [1.0, 0.5, 0.85, 1.0] -> 0xff80d9ff in the
	 * sparks.c 0xRRGGBBAA colour-word layout; secondary defaults to the OG
	 * fade-to-white. The registered row index is >= the base table size. */
	REQUIRE(record->has_spark == 1);
	REQUIRE(record->spark_color1 == 0xff80d9ffu);
	REQUIRE(record->spark_color2 == 0xffffffffu);
	REQUIRE(record->spark_type >= kSparkBaseCount);
	REQUIRE(sparksCustomTypeCount() == 1);

	u32 color1 = 0;
	u32 color2 = 0;
	REQUIRE(sparksTypeColors(record->spark_type, &color1, &color2) == 1);
	REQUIRE(color1 == 0xff80d9ffu);
	REQUIRE(color2 == 0xffffffffu);
	/* The row is a sensible clone: every non-colour field matches the OG
	 * SPARKTYPE_PROJECTILE row it was based on. */
	REQUIRE(sparksTypeClonesRowExceptColors(record->spark_type,
		SPARKTYPE_PROJECTILE) == 1);

	/* Re-registration deduplicates onto the same registry row (mod reload
	 * cycles must not leak slots). */
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:pink_fx",
		pink.data(), static_cast<u32>(pink.size()), err, sizeof(err)) == 0);
	const effect_graph_runtime_t *again = effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(again != nullptr);
	REQUIRE(again->spark_type == record->spark_type);
	REQUIRE(sparksCustomTypeCount() == 1);

	/* ClearAll releases both the records and the spark registry. */
	effectGraphRuntimeClearAll();
	REQUIRE(effectGraphRuntimeGet("modx:pink_fx") == nullptr);
	REQUIRE(sparksCustomTypeCount() == 0);
	effectRuntimeTestReset();
}

TEST_CASE("effect smoke nodes map to the nearest existing SMOKETYPE row",
          "[modding][pdxxx][effect_graph][c3849]") {
	effectRuntimeTestReset();
	char err[256] = {};

	struct SmokeCase {
		const char *cls;
		s32 expected;
	};
	static const SmokeCase kCases[] = {
		{ "tiny", SMOKETYPE_MINI },      /* 2: smallest OG row */
		{ "small", SMOKETYPE_SMALL },    /* 4 */
		{ "medium", SMOKETYPE_MEDIUM },  /* 5 */
		{ "large", SMOKETYPE_LARGE },    /* 6 */
		{ "rocket_tail", SMOKETYPE_ROCKETTAIL },
	};

	for (const SmokeCase &c : kCases) {
		const std::string id = std::string("modx:smoke_") + c.cls;
		const std::string graph =
			"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"" + id + "\","
			"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
			"\"params\":{\"smoke_class\":\"" + c.cls + "\"}}],\"edges\":[]}";
		INFO("smoke class " << c.cls);
		REQUIRE(effectGraphRuntimeRegisterGraphJson(id.c_str(),
			graph.data(), static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
		const effect_graph_runtime_t *record = effectGraphRuntimeGet(id.c_str());
		REQUIRE(record != nullptr);
		REQUIRE(record->has_smoke == 1);
		REQUIRE(record->smoke_type == c.expected);
	}

	/* Unsupported and lossy class words fail activation; they do not enter a
	 * catalog-visible record that a later callsite could silently coerce. */
	const std::string unknown =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:smoke_bogus\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"volcanic\"}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:smoke_bogus",
		unknown.data(), static_cast<u32>(unknown.size()), err, sizeof(err)) != 0);
	REQUIRE(effectGraphRuntimeGet("modx:smoke_bogus") == nullptr);
	for (const char *lossy : { "huge", "massive" }) {
		const std::string id = std::string("modx:smoke_") + lossy;
		const std::string graph =
			"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"" + id + "\","
			"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
			"\"params\":{\"smoke_class\":\"" + lossy + "\"}}],\"edges\":[]}";
		std::memset(err, 0, sizeof(err));
		REQUIRE(effectGraphRuntimeRegisterGraphJson(id.c_str(), graph.data(),
			static_cast<u32>(graph.size()), err, sizeof(err)) != 0);
	}

	effectRuntimeTestReset();
}

TEST_CASE("effect bridges gate on the runtime toggle and resolve registered records",
          "[modding][pdxxx][effect_graph][c3849]") {
	effectRuntimeTestReset();
	char err[256] = {};

	const std::string pink = pinkEffectGraph("modx:pink_fx");
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:pink_fx",
		pink.data(), static_cast<u32>(pink.size()), err, sizeof(err)) == 0);
	const std::string forbiddenSound =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:boom_sound\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"large\",\"sound\":345}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:boom_sound",
		forbiddenSound.data(), static_cast<u32>(forbiddenSound.size()),
		err, sizeof(err)) != 0);
	const std::string smoke =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:boom_smoke\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"large\"}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:boom_smoke",
		smoke.data(), static_cast<u32>(smoke.size()), err, sizeof(err)) == 0);

	const effect_graph_runtime_t *pinkRecord =
		effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(pinkRecord != nullptr);
	const s32 pinkSparkRow = pinkRecord->spark_type;
	REQUIRE(pinkSparkRow >= kSparkBaseCount);

	/* Toggle OFF: an authored selection is unavailable, so all four bridges
	 * fail closed. Empty refs alone retain explicit callsite defaults. */
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(effectGraphRuntimeGetForGameplay("modx:pink_fx") == nullptr);
	REQUIRE(effectGraphResolveExplosionType("modx:pink_fx", 13) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSparkType("modx:pink_fx", SPARKTYPE_PROJECTILE)
		== EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSmokeType("modx:boom_smoke", 8) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSound("modx:boom_smoke", 123) ==
		EFFECT_GRAPH_RESOLVE_FAILED);

	/* Toggle ON: registered refs resolve to the record values. */
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphRuntimeGetForGameplay("modx:pink_fx") == pinkRecord);
	REQUIRE(effectGraphResolveExplosionType("modx:pink_fx", 13)
		== EXPLOSIONTYPE_EYESPY);
	REQUIRE(effectGraphResolveSparkType("modx:pink_fx", SPARKTYPE_PROJECTILE)
		== pinkSparkRow);
	REQUIRE(effectGraphResolveSmokeType("modx:boom_smoke", 8) == SMOKETYPE_LARGE);
	REQUIRE(effectGraphResolveSound("modx:boom_smoke", 123) ==
		EFFECT_GRAPH_RESOLVE_FAILED);

	/* Toggle ON: unresolved selected refs and records missing the selected
	 * channel fail closed. Empty refs remain unauthored defaults. Base profile
	 * rows are resolved only by the installed public profile executor. */
	REQUIRE(effectGraphResolveExplosionType("modx:never_registered", 13) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSparkType("modx:never_registered", 4) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSparkType("", 4) == 4);
	REQUIRE(effectGraphResolveSparkType(nullptr, 4) == 4);
	REQUIRE(effectGraphResolveSmokeType("modx:never_registered", 6) ==
		EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSound("modx:pink_fx", 123) ==
		EFFECT_GRAPH_RESOLVE_FAILED);  /* selected record has no sound */

	/* A record whose channels are unresolved keeps the fallback even when
	 * the record itself resolves (untinted spark, unknown smoke class). */
	const std::string inert =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:inert_spark\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.spark\",\"params\":{}}],"
		"\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:inert_spark",
		inert.data(), static_cast<u32>(inert.size()), err, sizeof(err)) == 0);
	REQUIRE(effectGraphResolveSparkType("modx:inert_spark", 4) ==
		EFFECT_GRAPH_RESOLVE_FAILED);

	effectRuntimeTestReset();
}

TEST_CASE("effect archives register through descriptor and nested weapon walks",
          "[modding][pdxxx][effect_graph][c3849]") {
	effectRuntimeTestReset();
	char err[256] = {};

	const std::string pinkGraph = pinkEffectGraph("modx:pink_fx");
	const std::string effectIni =
		"[effect]\n"
		"catalog_id = modx:pink_fx\n"
		"name = Pink Burst\n"
		"effect_key = explosion\n"
		"target_key = world\n"
		"effect_file = effect.graph.json\n"
		"intensity = 1.0\n";

	auto effectArchive = writeTypedArchiveEntries("effect-pink", ".pdeffect", {
		{ "effect.ini", effectIni },
		{ "effect.graph.json", pinkGraph },
	});

	/* Loose archive path (the catalog activation shape). */
	REQUIRE(effectGraphRuntimeRegisterArchive(
		effectArchive.path.string().c_str(), err, sizeof(err)) == 0);
	const effect_graph_runtime_t *record = effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(record != nullptr);
	REQUIRE(std::string(record->effect_key) == "explosion");
	REQUIRE(std::string(record->target_key) == "world");
	REQUIRE(record->descriptor_intensity == Approx(1.0f));
	REQUIRE(record->explosion_type == EXPLOSIONTYPE_EYESPY);
	REQUIRE(record->spark_type >= kSparkBaseCount);

	effectRuntimeTestReset();

	/* Bytes path (the nested ingestion shape). */
	const std::string effectBytes = readFile(effectArchive.path.string().c_str());
	REQUIRE(effectGraphRuntimeRegisterArchiveBytes(effectBytes.data(),
		static_cast<u32>(effectBytes.size()), err, sizeof(err)) == 0);
	REQUIRE(effectGraphRuntimeGet("modx:pink_fx") != nullptr);

	effectRuntimeTestReset();

	/* Full needler-shaped chain: weapon -> nested projectile -> embedded
	 * effect, registered via the production weapon-archive walk. */
	const std::string projectileGraph =
		"{\"schema\":\"pd.projectile_graph.v1\",\"asset_id\":\"modx:needle\","
		"\"graph_id\":\"projectile\",\"nodes\":["
		"{\"id\":\"m\",\"kind\":\"projectile.motion\","
		"\"params\":{\"motion_kind\":\"powered\",\"speed\":22.0}},"
		"{\"id\":\"i\",\"kind\":\"projectile.impact\","
		"\"params\":{\"explosion_ref\":\"modx:pink_fx\","
		"\"spark_ref\":\"modx:pink_fx\",\"consume_on_hit\":true}}],"
		"\"edges\":[{\"from\":\"m\",\"to\":\"i\"}],\"exports\":[]}";
	auto projectileArchive = writeTypedArchiveEntries("proj-pink", ".pdprojectile", {
		{ "projectile.ini",
		  "[projectile]\n"
		  "catalog_id = modx:needle\n"
		  "behavior_graph = behavior.graph.json\n" },
		{ "behavior.graph.json", projectileGraph },
		{ "dependencies/assets/effects/pink.pdeffect", effectBytes },
	});
	const std::string projectileBytes =
		readFile(projectileArchive.path.string().c_str());

	const std::string weaponGraph =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"modx:pinkgun\","
		"\"graph_id\":\"held\",\"nodes\":["
		"{\"id\":\"p\",\"kind\":\"fire.hitscan\",\"params\":{\"mode\":\"primary\"}}],"
		"\"edges\":[],\"exports\":[{\"name\":\"primary\",\"node\":\"p\"}]}";
	auto weaponArchive = writeTypedArchiveEntries("weapon-pink", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = modx:pinkgun\n"
		  "behavior_graph = behavior.graph.json\n" },
		{ "behavior.graph.json", weaponGraph },
		{ "dependencies/assets/projectiles/needle.pdprojectile", projectileBytes },
	});

	weaponGraphRuntimeClearAll();
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		weaponArchive.path.string().c_str(), err, sizeof(err)) == 0);

	/* The projectile registered AND the effect embedded one level down
	 * registered through the s_registerEmbeddedEffectDeps mirror -- the gap
	 * that previously dropped the needler's pink effect. */
	REQUIRE(weaponGraphRuntimeGetProjectile("modx:needle") != nullptr);
	const effect_graph_runtime_t *nested = effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(nested != nullptr);
	REQUIRE(nested->explosion_type == EXPLOSIONTYPE_EYESPY);
	REQUIRE(nested->spark_type >= kSparkBaseCount);

	/* Bridges deliver the pink payload with the toggle on. */
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphResolveExplosionType("modx:pink_fx", EXPLOSIONTYPE_ROCKET)
		== EXPLOSIONTYPE_EYESPY);
	REQUIRE(effectGraphResolveSparkType("modx:pink_fx", SPARKTYPE_PROJECTILE)
		== nested->spark_type);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
	effectRuntimeTestReset();
}

TEST_CASE("nested weapon reload uses the active catalog-selected effect source",
		"[modding][pdxxx][effect_graph][b1027]") {
	effectRuntimeTestReset();
	char err[256] = {};

	const std::string effectIni =
		"[effect]\n"
		"catalog_id = modx:replace_fx\n"
		"name = Replace Effect\n"
		"effect_key = explosion\n"
		"target_key = world\n"
		"effect_file = effect.graph.json\n"
		"intensity = 1.0\n";
	const std::string baselineGraph = pinkEffectGraph("modx:replace_fx");
	std::string selectedGraph = baselineGraph;
	const std::string pink = "1.0, 0.5, 0.85, 1.0";
	const size_t tint = selectedGraph.find(pink);
	REQUIRE(tint != std::string::npos);
	selectedGraph.replace(tint, pink.size(), "0.0, 1.0, 1.0, 1.0");

	auto baselineEffect = writeTypedArchiveEntries("effect-replace-baseline",
		".pdeffect", {
			{ "effect.ini", effectIni },
			{ "effect.graph.json", baselineGraph },
		});
	auto selectedEffect = writeTypedArchiveEntries("effect-replace-selected",
		".pdeffect", {
			{ "effect.ini", effectIni },
			{ "effect.graph.json", selectedGraph },
		});
	const std::string baselineBytes = readFile(baselineEffect.path.string().c_str());
	const std::string selectedBytes = readFile(selectedEffect.path.string().c_str());

	asset_entry_t catalogEntry = {};
	std::strcpy(catalogEntry.id, "modx:replace_fx");
	catalogEntry.type = ASSET_EFFECT;
	catalogEntry.enabled = 1;
	catalogEntry.load_state = ASSET_STATE_ACTIVE;
	testStubAssetCatalogResolveWith(&catalogEntry);
	REQUIRE(effectGraphRuntimeRegisterArchiveBytes(selectedBytes.data(),
		static_cast<u32>(selectedBytes.size()), err, sizeof(err)) == 0);
	const std::string selectedDigest =
		effectGraphRuntimeGet("modx:replace_fx")->source_sha256;

	const std::string projectileGraph =
		"{\"schema\":\"pd.projectile_graph.v1\","
		"\"asset_id\":\"modx:replace_projectile\",\"graph_id\":\"projectile\","
		"\"nodes\":[{\"id\":\"m\",\"kind\":\"projectile.motion\","
		"\"params\":{\"motion_kind\":\"powered\",\"speed\":22.0}},"
		"{\"id\":\"i\",\"kind\":\"projectile.impact\",\"params\":{"
		"\"explosion_ref\":\"modx:replace_fx\",\"consume_on_hit\":true}}],"
		"\"edges\":[{\"from\":\"m\",\"to\":\"i\"}],\"exports\":[]}";
	auto projectile = writeTypedArchiveEntries("projectile-replace", ".pdprojectile", {
		{ "projectile.ini",
		  "[projectile]\n"
		  "catalog_id = modx:replace_projectile\n"
		  "behavior_graph = behavior.graph.json\n" },
		{ "behavior.graph.json", projectileGraph },
		{ "dependencies/assets/effects/replace.pdeffect", baselineBytes },
	});
	const std::string projectileBytes = readFile(projectile.path.string().c_str());
	const std::string weaponGraph =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"modx:replace_weapon\","
		"\"graph_id\":\"held\",\"nodes\":[{\"id\":\"p\","
		"\"kind\":\"fire.hitscan\",\"params\":{\"mode\":\"primary\"}}],"
		"\"edges\":[],\"exports\":[{\"name\":\"primary\",\"node\":\"p\"}]}";
	auto weapon = writeTypedArchiveEntries("weapon-replace", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = modx:replace_weapon\n"
		  "behavior_graph = behavior.graph.json\n" },
		{ "behavior.graph.json", weaponGraph },
		{ "dependencies/assets/projectiles/replace.pdprojectile", projectileBytes },
	});

	INFO(err);
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		weapon.path.string().c_str(), err, sizeof(err)) == 0);
	const effect_graph_runtime_t *selected =
		effectGraphRuntimeGet("modx:replace_fx");
	REQUIRE(selected != nullptr);
	REQUIRE(std::string(selected->source_sha256) == selectedDigest);
	REQUIRE(selected->owner_count == 1);
	REQUIRE(std::string(selected->owners[0]) == "modx:replace_fx");

	/* A catalog row never authorizes an unrelated owner to masquerade as the
	 * selected source. This remains a loud parent-admission failure. */
	effectRuntimeTestReset();
	catalogEntry.load_state = ASSET_STATE_ACTIVE;
	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(selectedBytes.data(),
		static_cast<u32>(selectedBytes.size()), "modx:foreign_owner", err,
		sizeof(err)) == 0);
	err[0] = '\0';
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		weapon.path.string().c_str(), err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("not owned by its selected catalog row") !=
		std::string::npos);

	effectRuntimeTestReset();
	testStubAssetCatalogResolveWith(nullptr);
}

TEST_CASE("effect_key is wired as catalog classification on local and network ingestion",
	"[modding][pdxxx][effect_graph][t-assets-036][descriptor]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string runtime = readFile("port/src/asset_runtime.c");
	const std::string network = readFile("port/src/net/netdistrib.c");
	REQUIRE(scanner.find("iniGet(ini, \"effect_key\"") != std::string::npos);
	REQUIRE(scanner.find("e->ext.effect.effect_type = parseEffectTypeKeyValue") !=
		std::string::npos);
	REQUIRE(runtime.find("binding->kind = entry->ext.effect.effect_type") !=
		std::string::npos);
	REQUIRE(network.find("e->ext.effect.effect_type =") != std::string::npos);
	REQUIRE(network.find("iniGet(ini, \"effect_key\"") != std::string::npos);
}

TEST_CASE("effect activation owners are transactional and shared by source identity",
		  "[modding][pdxxx][effect_graph][t-assets-020][fail-closed]") {
	effectRuntimeTestReset();
	char err[256] = {};
	const std::string effectIni =
		"[effect]\n"
		"catalog_id = modx:shared_fx\n"
		"name = Shared FX\n"
		"schema = pd.effect_graph.v1\n"
		"effect_file = effect.graph.json\n";
	const std::string graph = pinkEffectGraph("modx:shared_fx");
	auto archive = writeTypedArchiveEntries("effect-shared-owner", ".pdeffect", {
		{ "effect.ini", effectIni }, { "effect.graph.json", graph },
	});
	const std::string bytes = readFile(archive.path.string().c_str());

	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(bytes.data(),
		static_cast<u32>(bytes.size()), "modx:weapon_a", err, sizeof(err)) == 0);
	/* Re-registering one activation owner is idempotent, not an aggregate
	 * catalog reference. Catalog retain/release counts live above this edge. */
	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(bytes.data(),
		static_cast<u32>(bytes.size()), "modx:weapon_a", err, sizeof(err)) == 0);
	const effect_graph_runtime_t *record = effectGraphRuntimeGet("modx:shared_fx");
	REQUIRE(record != nullptr);
	REQUIRE(record->owner_count == 1);

	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(bytes.data(),
		static_cast<u32>(bytes.size()), "modx:weapon_b", err, sizeof(err)) == 0);
	record = effectGraphRuntimeGet("modx:shared_fx");
	REQUIRE(record != nullptr);
	REQUIRE(record->owner_count == 2);

	std::string conflictingGraph = graph;
	const size_t color = conflictingGraph.find("1.0, 0.5, 0.85, 1.0");
	REQUIRE(color != std::string::npos);
	conflictingGraph.replace(color, std::strlen("1.0, 0.5, 0.85, 1.0"),
		"0.1,0.2,0.3,1.0");
	auto conflicting = writeTypedArchiveEntries("effect-shared-conflict", ".pdeffect", {
		{ "effect.ini", effectIni }, { "effect.graph.json", conflictingGraph },
	});
	const std::string conflictingBytes = readFile(conflicting.path.string().c_str());
	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(conflictingBytes.data(),
		static_cast<u32>(conflictingBytes.size()), "modx:weapon_c", err,
		sizeof(err)) != 0);
	REQUIRE(std::string(err).find("conflicts with another active owner") !=
		std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:shared_fx")->owner_count == 2);

	effectGraphRuntimeReleaseOwner("modx:weapon_a");
	REQUIRE(effectGraphRuntimeGet("modx:shared_fx") != nullptr);
	REQUIRE(effectGraphRuntimeGet("modx:shared_fx")->owner_count == 1);
	effectGraphRuntimeReleaseOwner("modx:weapon_b");
	REQUIRE(effectGraphRuntimeGet("modx:shared_fx") == nullptr);

	/* The activation identity includes the timeline and normalized descriptor,
	 * not only the compiled graph digest. A second parent cannot silently
	 * inherit the first parent's timeline. */
	const std::string timelineIni =
		"[effect]\ncatalog_id = modx:shared_fx\nname = Shared Timeline FX\n"
		"schema = pd.effect_graph.v1\neffect_file = effect.graph.json\n"
		"timeline_file = timeline.json\n";
	const std::string timelineA =
		"{\"schema\":\"pd2.effect.timeline.v1\",\"tracks\":["
		"{\"time\":0,\"property\":\"intensity\",\"value\":0.25}]}";
	const std::string timelineB =
		"{\"schema\":\"pd2.effect.timeline.v1\",\"tracks\":["
		"{\"time\":0,\"property\":\"intensity\",\"value\":0.75}]}";
	auto timedA = writeTypedArchiveEntries("effect-owner-timeline-a", ".pdeffect", {
		{ "effect.ini", timelineIni }, { "effect.graph.json", graph },
		{ "timeline.json", timelineA },
	});
	auto timedB = writeTypedArchiveEntries("effect-owner-timeline-b", ".pdeffect", {
		{ "effect.ini", timelineIni }, { "effect.graph.json", graph },
		{ "timeline.json", timelineB },
	});
	const std::string timedABytes = readFile(timedA.path.string().c_str());
	const std::string timedBBytes = readFile(timedB.path.string().c_str());
	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(timedABytes.data(),
		static_cast<u32>(timedABytes.size()), "modx:weapon_a", err,
		sizeof(err)) == 0);
	std::memset(err, 0, sizeof(err));
	REQUIRE(effectGraphRuntimeRegisterArchiveBytesOwned(timedBBytes.data(),
		static_cast<u32>(timedBBytes.size()), "modx:weapon_b", err,
		sizeof(err)) != 0);
	REQUIRE(std::string(err).find("conflicts with another active owner") !=
		std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:shared_fx")->owner_count == 1);
	effectGraphRuntimeReleaseOwner("modx:weapon_a");
	effectRuntimeTestReset();
}

TEST_CASE("nested selected effect failures reject and roll back the parent archive",
		  "[modding][pdxxx][effect_graph][t-assets-020][fail-closed]") {
	effectRuntimeTestReset();
	const std::string projectileGraph =
		"{\"schema\":\"pd.projectile_graph.v1\","
		"\"asset_id\":\"modx:strict_projectile\",\"graph_id\":\"projectile\","
		"\"nodes\":[{\"id\":\"motion\",\"kind\":\"projectile.motion\","
		"\"params\":{\"motion_kind\":\"ballistic\"}},"
		"{\"id\":\"impact\",\"kind\":\"projectile.impact\","
		"\"params\":{\"explosion_ref\":\"modx:absent_fx\","
		"\"spark_ref\":\"modx:absent_fx\",\"consume_on_hit\":true}}],"
		"\"edges\":[]}";
	auto makeWeapon = [&](const std::string &suffix,
		const std::string &projectileBytes) {
		return writeTypedArchiveEntries("strict-effect-parent-" + suffix,
			".pdweapon", {
				{ "weapon.ini", "[weapon]\ncatalog_id = modx:strict_weapon\n"
					"behavior_graph = behavior.graph.json\n" },
				{ "behavior.graph.json",
					"{\"schema\":\"pd.weapon_graph.v1\","
					"\"asset_id\":\"modx:strict_weapon\",\"graph_id\":\"held\","
					"\"nodes\":[{\"id\":\"p\",\"kind\":\"fire.hitscan\","
					"\"params\":{\"mode\":\"primary\"}}],\"edges\":[],"
					"\"exports\":[{\"name\":\"primary\",\"node\":\"p\"}]}" },
				{ "dependencies/assets/projectiles/strict.pdprojectile",
					projectileBytes },
			});
	};

	/* A syntactically valid selected ref with no active public effect source
	 * rejects the parent after the complete nested inventory is admitted. */
	auto missingProjectile = writeTypedArchiveEntries("strict-effect-missing",
		".pdprojectile", {
			{ "projectile.ini", "[projectile]\n"
				"catalog_id = modx:strict_projectile\n"
				"behavior_graph = behavior.graph.json\n" },
			{ "behavior.graph.json", projectileGraph },
		});
	auto missingWeapon = makeWeapon("missing",
		readFile(missingProjectile.path.string().c_str()));
	char err[256] = {};
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		missingWeapon.path.string().c_str(), err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("has no active effect source") !=
		std::string::npos);
	REQUIRE(weaponGraphRuntimeGetProjectile("modx:strict_projectile") == nullptr);
	REQUIRE(effectGraphRuntimeCount() == 0);

	/* A nested member carrying the selected .pdeffect role but corrupt bytes
	 * is fatal too; the prior missing-source transaction cannot mask it. */
	auto corruptProjectile = writeTypedArchiveEntries("strict-effect-corrupt",
		".pdprojectile", {
			{ "projectile.ini", "[projectile]\n"
				"catalog_id = modx:strict_projectile\n"
				"behavior_graph = behavior.graph.json\n" },
			{ "behavior.graph.json", projectileGraph },
			{ "dependencies/assets/effects/absent.pdeffect",
				"not a typed archive" },
		});
	auto corruptWeapon = makeWeapon("corrupt",
		readFile(corruptProjectile.path.string().c_str()));
	std::memset(err, 0, sizeof(err));
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		corruptWeapon.path.string().c_str(), err, sizeof(err)) != 0);
	REQUIRE(weaponGraphRuntimeGetProjectile("modx:strict_projectile") == nullptr);
	REQUIRE(effectGraphRuntimeCount() == 0);
	effectRuntimeTestReset();
}

TEST_CASE("effect program retains topology policy parameters and interpolated timeline",
		"[modding][pdxxx][effect_graph][t-assets-018]") {
	effectRuntimeTestReset();
	const std::string graph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:complete_fx\","
		"\"shared_context\":[{\"name\":\"impact\",\"scope\":\"call\","
		"\"source\":\"target\",\"type\":\"vec3\",\"lifetime\":\"effect\"}],"
		"\"nodes\":["
		"{\"id\":\"attach\",\"kind\":\"effect.glow\",\"subgraph\":\"main\","
		"\"params\":{\"target\":\"impact\",\"attachment\":\"point\","
		"\"lifetime\":1.5,\"priority\":7,"
		"\"intensity\":0.5}},"
		"{\"id\":\"burst\",\"kind\":\"effect.explosion\",\"subgraph\":\"main\","
		"\"params\":{\"explosion_class\":\"small\"}},"
		"{\"id\":\"smoke\",\"kind\":\"effect.smoke\",\"subgraph\":\"tail\","
		"\"params\":{\"smoke_class\":\"large\"}}],"
		"\"subgraphs\":[{\"id\":\"main\",\"entry\":\"attach\"},"
		"{\"id\":\"tail\",\"entry\":\"smoke\"}],"
		"\"edges\":[{\"from\":\"attach\",\"to\":\"burst\"},"
		"{\"from\":\"burst\",\"to\":\"smoke\"}],"
		"\"exports\":[{\"name\":\"primary\",\"node\":\"attach\"}]}";
	const std::string timeline =
		"{\"schema\":\"pd2.effect.timeline.v1\",\"tracks\":["
		"{\"time\":0.0,\"property\":\"intensity\",\"value\":0.0},"
		"{\"time\":0.25,\"property\":\"intensity\",\"value\":0.75}]}";
	const std::string descriptor =
		"[effect]\n"
		"catalog_id = modx:complete_fx\n"
		"name = Complete FX\n"
		"schema = pd.effect_graph.v1\n"
		"effect_file = effect.graph.json\n"
		"timeline_file = timeline.json\n";
	auto archive = writeTypedArchiveEntries("effect-complete", ".pdeffect", {
		{ "effect.ini", descriptor }, { "effect.graph.json", graph },
		{ "timeline.json", timeline },
	});
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterArchive(archive.path.string().c_str(),
		err, sizeof(err)) == 0);
	const effect_graph_runtime_t *record = effectGraphRuntimeGet("modx:complete_fx");
	REQUIRE(record != nullptr);
	const effect_graph_program_t &program = record->program;
	REQUIRE(program.kind == EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE);
	REQUIRE(program.context_count == 1);
	REQUIRE(std::string(program.contexts[0].lifetime) == "effect");
	REQUIRE(program.node_count == 3);
	REQUIRE(program.edge_count == 2);
	REQUIRE(program.subgraph_count == 2);
	REQUIRE(program.export_count == 1);
	REQUIRE(program.param_count >= 7);
	REQUIRE(program.timeline.count == 2);
	REQUIRE(program.timeline.keys[1].authored_order == 1);
	float sampled = -1.0f;
	REQUIRE(effectGraphProgramSample(&program, "intensity", 0.125f, &sampled) == 1);
	REQUIRE(sampled == Approx(0.375f));
	std::vector<std::string> execution;
	effect_graph_dispatch_table_t dispatch = {};
	dispatch.begin = dispatchBegin;
	dispatch.commit = dispatchCommit;
	dispatch.rollback = dispatchRollback;
	for (s32 i = 0; i < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; i++) {
		dispatch.nodes[i] = collectProgramNode;
	}
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphRuntimeDispatch("modx:complete_fx", 0.125f, &dispatch,
		&execution, err, sizeof(err)) == 3);
	REQUIRE(execution == std::vector<std::string>{ "attach", "burst", "smoke" });

	bool sawTarget = false, sawAttachment = false, sawLifetime = false;
	bool sawPriority = false;
	for (s32 i = 0; i < program.param_count; i++) {
		const std::string key = program.params[i].key;
		sawTarget |= key == "target";
		sawAttachment |= key == "attachment";
		sawLifetime |= key == "lifetime";
		sawPriority |= key == "priority";
	}
	REQUIRE(sawTarget);
	REQUIRE(sawAttachment);
	REQUIRE(sawLifetime);
	REQUIRE(sawPriority);

	const std::string badTrackGraph =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:bad_track\","
		"\"nodes\":[{\"id\":\"g\",\"kind\":\"effect.glow\",\"params\":{}}],"
		"\"edges\":[]}";
	const std::string badTrack =
		"{\"schema\":\"pd2.effect.timeline.v1\",\"tracks\":["
		"{\"time\":0,\"property\":\"light.radius\",\"value\":2}]}";
	auto badTrackArchive = writeTypedArchiveEntries("effect-bad-track", ".pdeffect", {
		{ "effect.ini", "[effect]\ncatalog_id = modx:bad_track\nname = Bad Track\n"
			"schema = pd.effect_graph.v1\neffect_file = effect.graph.json\n"
			"timeline_file = timeline.json\n" },
		{ "effect.graph.json", badTrackGraph }, { "timeline.json", badTrack },
	});
	std::memset(err, 0, sizeof(err));
	REQUIRE(effectGraphRuntimeRegisterArchive(badTrackArchive.path.string().c_str(),
		err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no production consumer") != std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:bad_track") == nullptr);
	effectRuntimeTestReset();
}

TEST_CASE("effect archive accepts executable graph programs and rejects inert timeline-only source",
		"[modding][pdxxx][effect_graph][t-assets-018][t-assets-036][fail-closed]") {
	effectRuntimeTestReset();
	char err[256] = {};
	const std::string graph = baseEffectGraph("pd.effect_graph.v1",
		"modx:graph_only", "glow");
	auto graphOnly = writeTypedArchiveEntries("effect-graph-only", ".pdeffect", {
		{ "effect.ini", "[effect]\ncatalog_id = modx:graph_only\nname = Graph Only\n"
			"schema = pd.effect_graph.v1\neffect_file = graph.json\n" },
		{ "graph.json", graph },
	});
	REQUIRE(effectGraphRuntimeRegisterArchive(graphOnly.path.string().c_str(),
		err, sizeof(err)) == 0);
	REQUIRE(effectGraphRuntimeGet("modx:graph_only")->program.kind ==
		EFFECT_GRAPH_PROGRAM_GRAPH);

	const std::string timeline =
		"{\"schema\":\"pd2.effect.timeline.v1\",\"tracks\":["
		"{\"time\":0,\"property\":\"alpha\",\"value\":0.25},"
		"{\"time\":1,\"property\":\"alpha\",\"value\":1}]}";
	auto timelineOnly = writeTypedArchiveEntries("effect-timeline-only", ".pdeffect", {
		{ "effect.ini", "[effect]\ncatalog_id = modx:timeline_only\nname = Timeline Only\n"
			"schema = pd.effect_graph.v1\ntimeline_file = timeline.json\n" },
		{ "timeline.json", timeline },
	});
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterArchive(timelineOnly.path.string().c_str(),
		err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("requires an effect graph target") !=
		std::string::npos);
	REQUIRE(effectGraphRuntimeGet("modx:timeline_only") == nullptr);
	effectRuntimeTestReset();
}

TEST_CASE("effect runtime and spark registries grow beyond legacy caps",
		"[modding][pdxxx][effect_graph][t-assets-018]") {
	effectRuntimeTestReset();
	char err[256] = {};
	for (int i = 0; i < 96; i++) {
		const std::string id = "modx:effect_" + std::to_string(i);
		const std::string graph = baseEffectGraph("pd.effect_graph.v1", id, "glow");
		INFO("effect index " << i << ": " << err);
		REQUIRE(effectGraphRuntimeRegisterGraphJson(id.c_str(), graph.data(),
			static_cast<u32>(graph.size()), err, sizeof(err)) == 0);
	}
	REQUIRE(effectGraphRuntimeCount() == 96);
	REQUIRE(effectGraphRuntimeGet("modx:effect_95") != nullptr);

	for (u32 i = 0; i < 48; i++) {
		REQUIRE(sparksRegisterCustomTintedType(0x10000000u + i, 0xff000000u + i)
			== kSparkBaseCount + static_cast<s32>(i));
	}
	REQUIRE(sparksCustomTypeCount() == 48);
	effectRuntimeTestReset();
}

TEST_CASE("nested effect discovery registers every member beyond legacy eight",
		"[modding][pdxxx][effect_graph][t-assets-018]") {
	effectRuntimeTestReset();
	std::vector<std::pair<std::string, std::string>> projectileEntries = {
		{ "projectile.ini", "[projectile]\ncatalog_id = modx:carrier\n"
			"behavior_graph = behavior.graph.json\n" },
		{ "behavior.graph.json",
			"{\"schema\":\"pd.projectile_graph.v1\",\"asset_id\":\"modx:carrier\","
			"\"graph_id\":\"projectile\",\"nodes\":[{\"id\":\"m\","
			"\"kind\":\"projectile.motion\",\"params\":{\"motion_kind\":"
			"\"powered\",\"speed\":1}}],\"edges\":[],\"exports\":[]}" },
	};
	for (int i = 0; i < 12; i++) {
		const std::string id = "modx:nested_fx_" + std::to_string(i);
		const std::string descriptor =
			"[effect]\ncatalog_id = " + id + "\nname = Nested FX\n"
			"schema = pd.effect_graph.v1\neffect_file = effect.graph.json\n";
		auto effectArchive = writeTypedArchiveEntries(
			"nested-effect-" + std::to_string(i), ".pdeffect", {
				{ "effect.ini", descriptor },
				{ "effect.graph.json",
					baseEffectGraph("pd.effect_graph.v1", id, "glow") },
			});
		projectileEntries.emplace_back(
			"dependencies/assets/effects/fx" + std::to_string(i) + ".pdeffect",
			readFile(effectArchive.path.string().c_str()));
	}
	auto projectileArchive = writeTypedArchiveEntries("effect-carrier",
		".pdprojectile", projectileEntries);
	const std::string projectileBytes = readFile(
		projectileArchive.path.string().c_str());
	auto weaponArchive = writeTypedArchiveEntries("effect-carrier-weapon",
		".pdweapon", {
			{ "weapon.ini", "[weapon]\ncatalog_id = modx:carrier_weapon\n"
				"behavior_graph = behavior.graph.json\n" },
			{ "behavior.graph.json",
				"{\"schema\":\"pd.weapon_graph.v1\","
				"\"asset_id\":\"modx:carrier_weapon\",\"graph_id\":\"held\","
				"\"nodes\":[{\"id\":\"p\",\"kind\":\"fire.hitscan\","
				"\"params\":{\"mode\":\"primary\"}}],\"edges\":[],"
				"\"exports\":[{\"name\":\"primary\",\"node\":\"p\"}]}" },
			{ "dependencies/assets/projectiles/carrier.pdprojectile",
				projectileBytes },
		});
	char err[256] = {};
	INFO(err);
	REQUIRE(weaponGraphRuntimeRegisterWeaponArchive(80,
		weaponArchive.path.string().c_str(), err, sizeof(err)) == 0);
	for (int i = 0; i < 12; i++) {
		INFO("nested effect " << i);
		REQUIRE(effectGraphRuntimeGet(
			("modx:nested_fx_" + std::to_string(i)).c_str()) != nullptr);
	}
	effectRuntimeTestReset();
}

TEST_CASE("validated v2 profile library becomes retained executable source rows",
		"[modding][pdxxx][effect_graph][t-assets-018]") {
	effectRuntimeTestReset();
	smoketype smokeRows[23] = {};
	char *smokeSource = nullptr;
	size_t smokeSourceLen = 0;
	REQUIRE(pdEffectSourceBuildSmokeGraph("base:effect_smoke_profiles",
		smokeRows, 23, &smokeSource, &smokeSourceLen) == 0);
	const std::string smokeGraph(smokeSource, smokeSourceLen);
	std::free(smokeSource);
	const std::string smokeDescriptor =
		"[effect]\ncatalog_id = base:effect_smoke_profiles\n"
		"name = Smoke Profiles\nschema = pd.effect_graph.v2\n"
		"profile_kind = smoke\ntarget_policy = callsite\n"
		"attachment_policy = callsite\npriority_policy = callsite\n"
		"scorch_policy = callsite\neffect_file = effect.graph.json\n";
	auto smokeArchive = writeTypedArchiveEntries("effect-smoke-profiles",
		".pdeffect", {
			{ "effect.ini", smokeDescriptor },
			{ "effect.graph.json", smokeGraph },
		});
	char err[256] = {};
	INFO(err);
	REQUIRE(effectGraphRuntimeRegisterArchive(smokeArchive.path.string().c_str(),
		err, sizeof(err)) == 0);
	REQUIRE(effectGraphRuntimeGet("base:effect_smoke_profiles") != nullptr);

	explosiontype rows[26] = {};
	rows[2] = { 20, 21, 2, 3, 30, 50, 60, 40, 2, 3, 2, 0, 0.125f };
	char *source = nullptr;
	size_t sourceLen = 0;
	auto soundId = [](s32 sound, char *out, size_t cap) {
		std::snprintf(out, cap, "base:sfx_%d", sound);
	};
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, 26, soundId, &source, &sourceLen) == 0);
	const std::string graph(source, sourceLen);
	std::free(source);
	const std::string descriptor =
		"[effect]\ncatalog_id = base:effect_explosion_profiles\n"
		"name = Explosion Profiles\nschema = pd.effect_graph.v2\n"
		"profile_kind = explosion\ntarget_policy = callsite\n"
		"attachment_policy = callsite\npriority_policy = callsite\n"
		"scorch_policy = callsite\neffect_file = effect.graph.json\n";
	auto archive = writeTypedArchiveEntries("effect-profiles", ".pdeffect", {
		{ "effect.ini", descriptor }, { "effect.graph.json", graph },
	});
	REQUIRE(effectGraphRuntimeRegisterArchive(archive.path.string().c_str(),
		err, sizeof(err)) == 0);
	const auto *record = effectGraphRuntimeGet("base:effect_explosion_profiles");
	REQUIRE(record != nullptr);
	REQUIRE(record->program.kind == EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY);
	REQUIRE(record->program.profiles.kind == PD_EFFECT_PROFILE_EXPLOSION);
	REQUIRE(record->program.profiles.count == 26);
	const auto &eyespy = record->program.profiles.explosions[2];
	REQUIRE(std::string(eyespy.id) == "eyespy");
	REQUIRE(eyespy.range_h == Approx(20.0f));
	REQUIRE(eyespy.duration_ticks == 40);
	REQUIRE(std::string(eyespy.smoke_profile) == "mini");
	REQUIRE(eyespy.has_audio == 0);
	effectRuntimeTestReset();
}

TEST_CASE("spark custom row registry is growable, deduplicated and resettable",
          "[modding][pdxxx][effect_graph][c3849]") {
	sparksResetCustomTypes();
	REQUIRE(sparksCustomTypeCount() == 0);

	const s32 first = sparksRegisterCustomTintedType(0xff80d9ffu, 0xffffffffu);
	REQUIRE(first == kSparkBaseCount);
	REQUIRE(sparksRegisterCustomTintedType(0xff80d9ffu, 0xffffffffu) == first);
	REQUIRE(sparksCustomTypeCount() == 1);

	/* Grow well beyond the retired 16-row table. */
	for (u32 i = 1; i < 48; i++) {
		REQUIRE(sparksRegisterCustomTintedType(0x10000000u + i, 0xffffffffu)
			== kSparkBaseCount + static_cast<s32>(i));
	}
	REQUIRE(sparksCustomTypeCount() == 48);

	/* Base rows stay addressable; out-of-range typenums report dead. */
	u32 color1 = 0;
	u32 color2 = 0;
	REQUIRE(sparksTypeColors(SPARKTYPE_PROJECTILE, &color1, &color2) == 1);
	REQUIRE(color1 == 0xffff80ffu);  /* OG NTSC PROJECTILE row */
	REQUIRE(sparksTypeColors(kSparkBaseCount + 48, &color1, &color2) == 0);

	sparksResetCustomTypes();
	REQUIRE(sparksCustomTypeCount() == 0);
	REQUIRE(sparksTypeColors(kSparkBaseCount, &color1, &color2) == 0);
}

TEST_CASE("unit 8 effect runtime static pins stay wired",
          "[modding][pdxxx][effect_graph][c3849][static]") {
	/* Nested .pdeffect ingestion arm: type mapping + both walk arms. */
	const std::string archive = readFile("port/src/weapon_graph_archive.c");
	REQUIRE(archive.find(".pdeffect\")) return ASSET_EFFECT") != std::string::npos);
	REQUIRE(archive.find("return \"effect.ini\"") != std::string::npos);
	REQUIRE(archive.find("effect_file") != std::string::npos);

	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("payload->type == ASSET_EFFECT") != std::string::npos);
	REQUIRE(runtime.find("s_registerEmbeddedEffectDeps") != std::string::npos);
	REQUIRE(runtime.find("effectGraphRuntimeRegisterArchiveBytes") != std::string::npos);
	REQUIRE(runtime.find("pd2.effect.graph.v1") != std::string::npos);  /* legacy alias */
	REQUIRE(runtime.find("case ASSET_EFFECT:     return \"pd.effect_graph.v1\";")
		!= std::string::npos);

	/* Canonical schema string in the base emitter (legacy string gone from
	 * the emitted graph body). */
	const std::string meta = readFile("port/src/romextract_pdmeta.c");
	REQUIRE(meta.find("pd.effect_graph.v2") != std::string::npos);
	REQUIRE(meta.find("\\\"schema\\\": \\\"pd2.effect.graph.v1\\\"") == std::string::npos);

	/* sparkTypeFor at all three read sites; no direct table indexing left. */
	const std::string sparks = readFile("src/game/sparks.c");
	REQUIRE(sparks.find("sparkTypeFor(typenum)") != std::string::npos);
	REQUIRE(sparks.find("sparkTypeFor(group->type)") != std::string::npos);
	REQUIRE(sparks.find("&g_SparkTypes[typenum]") == std::string::npos);
	REQUIRE(sparks.find("&g_SparkTypes[group->type]") == std::string::npos);
	REQUIRE(sparks.find("SPARKTYPE_BASE_COUNT") != std::string::npos);  /* _Static_assert */
	const std::string sparkstick = readFile("src/game/sparkstick.c");
	REQUIRE(sparkstick.find("sparkTypeFor(group->type)") != std::string::npos);
	REQUIRE(sparkstick.find("&g_SparkTypes[group->type]") == std::string::npos);
	const std::string sparksHeader = readFile("src/include/game/sparks.h");
	REQUIRE(sparksHeader.find("#define SPARKTYPE_BASE_COUNT 27") != std::string::npos);
	const std::string sparksCustom = readFile("src/game/sparks_custom.c");
	REQUIRE(sparksCustom.find("SPARK.CUSTOM_ROW_FAIL") != std::string::npos);

	/* Catalog activation + clear hooks beside the weapon-graph hooks. */
	const std::string lifecycle = readFile("port/src/assetcatalog_load.c");
	REQUIRE(lifecycle.find("s_catalogTypeUsesEffectGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("s_catalogActivateEffectGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("s_catalogClearEffectGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("effectGraphRuntimeRegisterArchive") != std::string::npos);
	REQUIRE(lifecycle.find("effectGraphRuntimeRegisterGraphJson") != std::string::npos);
	REQUIRE(lifecycle.find("effectGraphRuntimeReleaseOwner") != std::string::npos);
	REQUIRE(lifecycle.find("catalogDeactivateTypedAsset") != std::string::npos);
	REQUIRE(lifecycle.find("effectGraphRuntimeClearAsset") == std::string::npos);

	/* The effect runtime's registry reset rides the record reset path. */
	const std::string effectRuntime = readFile("port/src/effect_graph_runtime.c");
	REQUIRE(effectRuntime.find("sparksResetCustomTypes()") != std::string::npos);
	REQUIRE(effectRuntime.find("weaponGraphRuntimeEnabled()") != std::string::npos);
	REQUIRE(effectRuntime.find("effectGraphRuntimeClearAsset") == std::string::npos);

	/* The needler proving asset authors the canonical schema and the node
	 * params this compiler consumes. */
	const std::string builder = readFile("tools/build_needler_mod.py");
	REQUIRE(builder.find("\"schema\": \"pd.effect_graph.v1\"") != std::string::npos);
	REQUIRE(builder.find("\"explosion_class\": \"small\"") != std::string::npos);
	REQUIRE(builder.find("effect.spark") != std::string::npos);
}
