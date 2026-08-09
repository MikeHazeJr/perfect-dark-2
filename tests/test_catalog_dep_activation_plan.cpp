#include "catch.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "assetcatalog_deps.h"
#include "catalog_dep_activation_plan.h"
}

namespace {
struct FakeCatalog {
	std::map<std::string, asset_type_e> types;
	std::vector<std::string> released;
};

s32 resolveFake(const char *id, asset_type_e *actual, void *userdata)
{
	auto &types = static_cast<FakeCatalog *>(userdata)->types;
	auto it = types.find(id ? id : "");
	if (it == types.end()) return 0;
	*actual = it->second;
	return 1;
}

void collectReleased(const char *id, asset_type_e, void *userdata)
{
	static_cast<FakeCatalog *>(userdata)->released.emplace_back(id);
}
}

TEST_CASE("composite activation grows past 64 and rolls back exact committed prefix",
	"[modding][pdxxx][effect][t-assets-020][lifecycle]")
{
	catalogDepClear();
	FakeCatalog catalog;
	catalog.types["modx:effect_root"] = ASSET_EFFECT;
	for (int i = 0; i < 96; i++) {
		char id[CATALOG_ID_LEN];
		std::snprintf(id, sizeof(id), "modx:texture_%03d", i);
		catalog.types[id] = ASSET_TEXTURE;
		REQUIRE(catalogDepRegisterTyped("modx:effect_root", id,
			ASSET_TEXTURE, 0) == 1);
	}
	catalog_dep_activation_plan_t plan = {};
	char error[256] = {};
	REQUIRE(catalogDepActivationPlanBuild(&plan, "modx:effect_root",
		ASSET_EFFECT, resolveFake, &catalog, error, sizeof(error)) == 1);
	REQUIRE(plan.count == 97);
	REQUIRE(std::string(plan.nodes[95].id) == "modx:texture_095");
	REQUIRE(std::string(plan.nodes[96].id) == "modx:effect_root");

	/* Model failure while committing row 73: only rows 0..72 changed truth,
	 * so rollback releases exactly that prefix in reverse order. */
	catalogDepActivationPlanRollback(&plan, 73, collectReleased, &catalog);
	REQUIRE(catalog.released.size() == 73);
	REQUIRE(catalog.released.front() == "modx:texture_072");
	REQUIRE(catalog.released.back() == "modx:texture_000");
	catalogDepActivationPlanFree(&plan);
	catalogDepClear();
}

TEST_CASE("activation plan preflights transitive diamond type and cycle truth",
	"[modding][pdxxx][effect][t-assets-020][lifecycle][fail-closed]")
{
	catalogDepClear();
	FakeCatalog catalog;
	catalog.types = {
		{ "modx:effect", ASSET_EFFECT },
		{ "modx:material_a", ASSET_MATERIAL },
		{ "modx:material_b", ASSET_MATERIAL },
		{ "modx:texture", ASSET_TEXTURE },
	};
	REQUIRE(catalogDepRegisterTyped("modx:effect", "modx:material_a",
		ASSET_MATERIAL, 0) == 1);
	REQUIRE(catalogDepRegisterTyped("modx:effect", "modx:material_b",
		ASSET_MATERIAL, 0) == 1);
	REQUIRE(catalogDepRegisterTyped("modx:material_a", "modx:texture",
		ASSET_TEXTURE, 0) == 1);
	REQUIRE(catalogDepRegisterTyped("modx:material_b", "modx:texture",
		ASSET_TEXTURE, 0) == 1);

	catalog_dep_activation_plan_t plan = {};
	char error[256] = {};
	REQUIRE(catalogDepActivationPlanBuild(&plan, "modx:effect", ASSET_EFFECT,
		resolveFake, &catalog, error, sizeof(error)) == 1);
	/* Diamond closure is deduplicated per parent reference. */
	REQUIRE(plan.count == 4);
	REQUIRE(std::string(plan.nodes[0].id) == "modx:texture");
	REQUIRE(std::string(plan.nodes[3].id) == "modx:effect");
	catalogDepActivationPlanFree(&plan);

	/* Wrong expected type rejects before any load/retain mutation. */
	REQUIRE(catalogDepRegisterTyped("modx:texture", "modx:effect",
		ASSET_AUDIO, 0) == 1);
	REQUIRE(catalogDepActivationPlanBuild(&plan, "modx:effect", ASSET_EFFECT,
		resolveFake, &catalog, error, sizeof(error)) == 0);
	REQUIRE(std::string(error).find("wrong type") != std::string::npos);
	REQUIRE(plan.count == 0);
	catalogDepUnregister("modx:texture", "modx:effect");

	/* A fully type-correct effect -> material -> texture -> effect loop is
	 * rejected iteratively, without attacker-controlled C recursion depth. */
	REQUIRE(catalogDepRegisterTyped("modx:texture", "modx:effect",
		ASSET_EFFECT, 0) == 1);
	REQUIRE(catalogDepActivationPlanBuild(&plan, "modx:effect", ASSET_EFFECT,
		resolveFake, &catalog, error, sizeof(error)) == 0);
	REQUIRE(std::string(error).find("cycle") != std::string::npos);
	REQUIRE(plan.count == 0);
	catalogDepActivationPlanFree(&plan);
	catalogDepClear();
}

TEST_CASE("deduplicated closure balances repeated and shared external owners",
	"[modding][pdxxx][effect][t-assets-020][lifecycle]")
{
	catalogDepClear();
	FakeCatalog catalog;
	catalog.types = {
		{ "modx:effect_a", ASSET_EFFECT },
		{ "modx:effect_b", ASSET_EFFECT },
		{ "modx:material", ASSET_MATERIAL },
		{ "modx:texture", ASSET_TEXTURE },
	};
	catalogDepRegisterTyped("modx:effect_a", "modx:material", ASSET_MATERIAL, 0);
	catalogDepRegisterTyped("modx:material", "modx:texture", ASSET_TEXTURE, 0);
	catalogDepRegisterTyped("modx:effect_b", "modx:texture", ASSET_TEXTURE, 0);
	catalog_dep_activation_plan_t a = {}, b = {};
	char error[256] = {};
	REQUIRE(catalogDepActivationPlanBuild(&a, "modx:effect_a", ASSET_EFFECT,
		resolveFake, &catalog, error, sizeof(error)) == 1);
	REQUIRE(catalogDepActivationPlanBuild(&b, "modx:effect_b", ASSET_EFFECT,
		resolveFake, &catalog, error, sizeof(error)) == 1);
	std::map<std::string, int> refs;
	auto retain = [&](const catalog_dep_activation_plan_t &p) {
		for (size_t i = 0; i < p.count; i++) refs[p.nodes[i].id]++;
	};
	auto release = [&](const catalog_dep_activation_plan_t &p) {
		for (size_t i = p.count; i-- > 0;) refs[p.nodes[i].id]--;
	};
	retain(a);
	retain(a);
	retain(b);
	REQUIRE(refs["modx:texture"] == 3);
	release(a);
	REQUIRE(refs["modx:effect_a"] == 1);
	REQUIRE(refs["modx:texture"] == 2);
	release(a);
	REQUIRE(refs["modx:effect_a"] == 0);
	REQUIRE(refs["modx:texture"] == 1);
	release(b);
	REQUIRE(refs["modx:texture"] == 0);
	catalogDepActivationPlanFree(&a);
	catalogDepActivationPlanFree(&b);
	catalogDepClear();
}

TEST_CASE("bundled effect theme and weapon edges remain lifecycle-visible",
	"[modding][pdxxx][t-assets-020][lifecycle][bundled]")
{
	catalogDepClear();
	FakeCatalog catalog;
	catalog.types = {
		{ "base:weapon", ASSET_WEAPON },
		{ "base:effect", ASSET_EFFECT },
		{ "base:smoke_library", ASSET_EFFECT },
		{ "base:theme", ASSET_THEME },
		{ "base:ui", ASSET_UI },
	};
	REQUIRE(catalogDepRegisterTyped("base:weapon", "base:effect",
		ASSET_EFFECT, 1) == 1);
	REQUIRE(catalogDepRegisterTyped("base:effect", "base:smoke_library",
		ASSET_EFFECT, 1) == 1);
	REQUIRE(catalogDepRegisterTyped("base:theme", "base:ui",
		ASSET_UI, 1) == 1);
	catalog_dep_activation_plan_t weapon = {}, theme = {};
	char error[256] = {};
	REQUIRE(catalogDepActivationPlanBuild(&weapon, "base:weapon", ASSET_WEAPON,
		resolveFake, &catalog, error, sizeof(error)) == 1);
	REQUIRE(weapon.count == 3);
	REQUIRE(std::string(weapon.nodes[0].id) == "base:smoke_library");
	REQUIRE(std::string(weapon.nodes[2].id) == "base:weapon");
	REQUIRE(catalogDepActivationPlanBuild(&theme, "base:theme", ASSET_THEME,
		resolveFake, &catalog, error, sizeof(error)) == 1);
	REQUIRE(theme.count == 2);
	REQUIRE(std::string(theme.nodes[0].id) == "base:ui");
	catalogDepActivationPlanFree(&weapon);
	catalogDepActivationPlanFree(&theme);
	catalogDepClear();
}
