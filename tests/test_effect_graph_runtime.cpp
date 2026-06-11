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
}

/* Mirror of SPARKTYPE_BASE_COUNT (game/sparks.h); the static pin below
 * keeps the literal honest. */
static const s32 kSparkBaseCount = 27;

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
		"    { \"id\": \"apply\", \"kind\": \"effect." + typeKey + "\", \"params\": { \"shader\": \"shader_" + typeKey + "\", \"intensity\": 0.500 } }\n"
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
		{ "huge", SMOKETYPE_LARGE },     /* nearest: no bigger OG row */
		{ "massive", SMOKETYPE_LARGE },  /* nearest: no bigger OG row */
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

	/* Unknown class words stay unresolved (-1, bridge falls back). */
	const std::string unknown =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:smoke_bogus\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"volcanic\"}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:smoke_bogus",
		unknown.data(), static_cast<u32>(unknown.size()), err, sizeof(err)) == 0);
	const effect_graph_runtime_t *record =
		effectGraphRuntimeGet("modx:smoke_bogus");
	REQUIRE(record != nullptr);
	REQUIRE(record->has_smoke == 1);
	REQUIRE(record->smoke_type == -1);

	effectRuntimeTestReset();
}

TEST_CASE("effect bridges gate on the runtime toggle and resolve registered records",
          "[modding][pdxxx][effect_graph][c3849]") {
	effectRuntimeTestReset();
	char err[256] = {};

	const std::string pink = pinkEffectGraph("modx:pink_fx");
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:pink_fx",
		pink.data(), static_cast<u32>(pink.size()), err, sizeof(err)) == 0);
	const std::string sound =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:boom_sound\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.smoke\","
		"\"params\":{\"smoke_class\":\"large\",\"sound\":345}}],\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:boom_sound",
		sound.data(), static_cast<u32>(sound.size()), err, sizeof(err)) == 0);

	const effect_graph_runtime_t *pinkRecord =
		effectGraphRuntimeGet("modx:pink_fx");
	REQUIRE(pinkRecord != nullptr);
	const s32 pinkSparkRow = pinkRecord->spark_type;
	REQUIRE(pinkSparkRow >= kSparkBaseCount);

	/* Toggle OFF: GetForGameplay is NULL and all four bridges return the
	 * fallback verbatim, registered records or not. */
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(effectGraphRuntimeGetForGameplay("modx:pink_fx") == nullptr);
	REQUIRE(effectGraphResolveExplosionType("modx:pink_fx", 13) == 13);
	REQUIRE(effectGraphResolveSparkType("modx:pink_fx", SPARKTYPE_PROJECTILE)
		== SPARKTYPE_PROJECTILE);
	REQUIRE(effectGraphResolveSmokeType("modx:boom_sound", 8) == 8);
	REQUIRE(effectGraphResolveSound("modx:boom_sound", 123) == 123);

	/* Toggle ON: registered refs resolve to the record values. */
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphRuntimeGetForGameplay("modx:pink_fx") == pinkRecord);
	REQUIRE(effectGraphResolveExplosionType("modx:pink_fx", 13)
		== EXPLOSIONTYPE_EYESPY);
	REQUIRE(effectGraphResolveSparkType("modx:pink_fx", SPARKTYPE_PROJECTILE)
		== pinkSparkRow);
	REQUIRE(effectGraphResolveSmokeType("modx:boom_sound", 8) == SMOKETYPE_LARGE);
	REQUIRE(effectGraphResolveSound("modx:boom_sound", 123) == 345);

	/* Toggle ON, unresolved/empty refs: fallback verbatim. The base: token
	 * path through weaponGraphResolveExplosionRef stays intact. */
	REQUIRE(effectGraphResolveExplosionType("modx:never_registered", 13) == 13);
	REQUIRE(effectGraphResolveExplosionType("base:explosion_rocket", 5)
		== EXPLOSIONTYPE_ROCKET);
	REQUIRE(effectGraphResolveSparkType("modx:never_registered", 4) == 4);
	REQUIRE(effectGraphResolveSparkType("", 4) == 4);
	REQUIRE(effectGraphResolveSparkType(nullptr, 4) == 4);
	REQUIRE(effectGraphResolveSmokeType("modx:never_registered", 6) == 6);
	REQUIRE(effectGraphResolveSound("modx:pink_fx", 123) == 123);  /* no sound param */

	/* A record whose channels are unresolved keeps the fallback even when
	 * the record itself resolves (untinted spark, unknown smoke class). */
	const std::string inert =
		"{\"schema\":\"pd.effect_graph.v1\",\"asset_id\":\"modx:inert_spark\","
		"\"nodes\":[{\"id\":\"s\",\"kind\":\"effect.spark\",\"params\":{}}],"
		"\"edges\":[]}";
	REQUIRE(effectGraphRuntimeRegisterGraphJson("modx:inert_spark",
		inert.data(), static_cast<u32>(inert.size()), err, sizeof(err)) == 0);
	REQUIRE(effectGraphResolveSparkType("modx:inert_spark", 4) == 4);

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

TEST_CASE("spark custom row registry is bounded, deduplicated and resettable",
          "[modding][pdxxx][effect_graph][c3849]") {
	sparksResetCustomTypes();
	REQUIRE(sparksCustomTypeCount() == 0);

	const s32 first = sparksRegisterCustomTintedType(0xff80d9ffu, 0xffffffffu);
	REQUIRE(first == kSparkBaseCount);
	REQUIRE(sparksRegisterCustomTintedType(0xff80d9ffu, 0xffffffffu) == first);
	REQUIRE(sparksCustomTypeCount() == 1);

	/* Fill the registry (cap 16) and confirm loud-fail exhaustion. */
	for (u32 i = 1; i < 16; i++) {
		REQUIRE(sparksRegisterCustomTintedType(0x10000000u + i, 0xffffffffu)
			== kSparkBaseCount + static_cast<s32>(i));
	}
	REQUIRE(sparksCustomTypeCount() == 16);
	REQUIRE(sparksRegisterCustomTintedType(0x20000000u, 0xffffffffu) == -1);

	/* Base rows stay addressable; out-of-range typenums report dead. */
	u32 color1 = 0;
	u32 color2 = 0;
	REQUIRE(sparksTypeColors(SPARKTYPE_PROJECTILE, &color1, &color2) == 1);
	REQUIRE(color1 == 0xffff80ffu);  /* OG NTSC PROJECTILE row */
	REQUIRE(sparksTypeColors(kSparkBaseCount + 16, &color1, &color2) == 0);

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
	REQUIRE(meta.find("\\\"schema\\\": \\\"pd.effect_graph.v1\\\"") != std::string::npos);
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
	REQUIRE(lifecycle.find("effectGraphRuntimeClearAsset") != std::string::npos);

	/* The effect runtime's registry reset rides the record reset path. */
	const std::string effectRuntime = readFile("port/src/effect_graph_runtime.c");
	REQUIRE(effectRuntime.find("sparksResetCustomTypes()") != std::string::npos);
	REQUIRE(effectRuntime.find("weaponGraphRuntimeEnabled()") != std::string::npos);

	/* The needler proving asset authors the canonical schema and the node
	 * params this compiler consumes. */
	const std::string builder = readFile("tools/build_needler_mod.py");
	REQUIRE(builder.find("\"schema\": \"pd.effect_graph.v1\"") != std::string::npos);
	REQUIRE(builder.find("\"explosion_class\": \"small\"") != std::string::npos);
	REQUIRE(builder.find("effect.spark") != std::string::npos);
}
