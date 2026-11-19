// weaponGraphCompileWeaponSourceJsonWithVariables: the member-text twin of
// the archive path's $variables scope (MODS ask, week note 2026-11-14), so a
// validator that only holds a weapon's source texts compiles a weapon whose
// graph uses $variables the way the game does.
#include "catch.hpp"

#include <cstring>
#include <string>

extern "C" {
#include "constants.h"
#include "weapon_graph_runtime.h"
}

namespace {

const std::string kPrimary =
	"{\"nodes\":[{\"id\":\"p\",\"kind\":\"fire.hitscan\","
	"\"params\":{\"mode\":\"primary\",\"spread\":\"$spread\"}}],"
	"\"edges\":[],\"exports\":[{\"name\":\"primary\",\"node\":\"p\"}]}";
const std::string kSecondary =
	"{\"nodes\":[{\"id\":\"s\",\"kind\":\"fire.hitscan\","
	"\"params\":{\"mode\":\"secondary\"}}],"
	"\"edges\":[],\"exports\":[{\"name\":\"secondary\",\"node\":\"s\"}]}";
const std::string kVariables =
	"{\"schema\":\"pd.weapon_variables.v1\",\"asset_id\":\"modx:vartest\","
	"\"variables\":[{\"name\":\"spread\",\"value\":2.5}]}";
const std::string kEmptyVariables =
	"{\"schema\":\"pd.weapon_variables.v1\",\"asset_id\":\"modx:vartest\","
	"\"variables\":[]}";

const weapon_graph_ir_param_t *findParam(const weapon_graph_ir_t &ir,
                                         const char *node, const char *key)
{
	for (s32 n = 0; n < ir.node_count; ++n) {
		if (std::strcmp(ir.nodes[n].id, node) != 0) {
			continue;
		}
		for (s32 i = 0; i < ir.nodes[n].param_count; ++i) {
			const weapon_graph_ir_param_t &p = ir.params[ir.nodes[n].param_start + i];
			if (std::strcmp(p.key, key) == 0) {
				return &p;
			}
		}
	}
	return nullptr;
}

s32 compileWith(const std::string *variables, weapon_graph_ir_t *ir, char *err, size_t cap)
{
	return weaponGraphCompileWeaponSourceJsonWithVariables("modx:vartest",
		kPrimary.data(), static_cast<u32>(kPrimary.size()),
		kSecondary.data(), static_cast<u32>(kSecondary.size()),
		nullptr, 0,
		variables ? variables->data() : nullptr,
		variables ? static_cast<u32>(variables->size()) : 0u,
		ir, err, cap);
}

s32 compileBare(weapon_graph_ir_t *ir, char *err, size_t cap)
{
	return weaponGraphCompileWeaponSourceJson("modx:vartest",
		kPrimary.data(), static_cast<u32>(kPrimary.size()),
		kSecondary.data(), static_cast<u32>(kSecondary.size()),
		nullptr, 0, ir, err, cap);
}

} // namespace

TEST_CASE("weapon source compile with variables substitutes $name like the archive path",
          "[weapon][graph][variables]")
{
	char err[256] = {};

	SECTION("a variables text resolves $spread to its typed value") {
		weapon_graph_ir_t ir;
		REQUIRE(compileWith(&kVariables, &ir, err, sizeof(err)) == 0);
		const weapon_graph_ir_param_t *spread = findParam(ir, "p", "spread");
		REQUIRE(spread != nullptr);
		REQUIRE(spread->type == WEAPON_GRAPH_PARAM_FLOAT);
		REQUIRE(spread->f_value == Approx(2.5f));
		weaponGraphIrFree(&ir);
	}

	SECTION("a present but empty table is an active scope: an unresolved name fails") {
		weapon_graph_ir_t ir;
		REQUIRE(compileWith(&kEmptyVariables, &ir, err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("unresolved weapon variable '$spread'") != std::string::npos);
	}

	SECTION("no variables text means no scope: $spread stays a literal string") {
		weapon_graph_ir_t ir;
		REQUIRE(compileWith(nullptr, &ir, err, sizeof(err)) == 0);
		const weapon_graph_ir_param_t *spread = findParam(ir, "p", "spread");
		REQUIRE(spread != nullptr);
		REQUIRE(spread->type == WEAPON_GRAPH_PARAM_STRING);
		REQUIRE(std::string(spread->value) == "$spread");
		weaponGraphIrFree(&ir);
	}

	SECTION("a bad variables text fails and leaves no scope behind") {
		const std::string bad = "{\"schema\":\"pd.weapon_settings.v1\",\"variables\":[]}";
		weapon_graph_ir_t ir;
		REQUIRE(compileWith(&bad, &ir, err, sizeof(err)) != 0);
		REQUIRE(std::string(err).find("variables schema must be pd.weapon_variables.v1") != std::string::npos);
		// The bare entry right after still sees no scope.
		weapon_graph_ir_t after;
		REQUIRE(compileBare(&after, err, sizeof(err)) == 0);
		const weapon_graph_ir_param_t *spread = findParam(after, "p", "spread");
		REQUIRE(spread != nullptr);
		REQUIRE(spread->type == WEAPON_GRAPH_PARAM_STRING);
		weaponGraphIrFree(&after);
	}

	SECTION("the scope ends with the call") {
		weapon_graph_ir_t ir;
		REQUIRE(compileWith(&kVariables, &ir, err, sizeof(err)) == 0);
		weaponGraphIrFree(&ir);
		weapon_graph_ir_t after;
		REQUIRE(compileBare(&after, err, sizeof(err)) == 0);
		const weapon_graph_ir_param_t *spread = findParam(after, "p", "spread");
		REQUIRE(spread != nullptr);
		REQUIRE(spread->type == WEAPON_GRAPH_PARAM_STRING);
		weaponGraphIrFree(&after);
	}
}
