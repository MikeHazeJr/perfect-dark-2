#include "catch.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "catalog_activation_ledger.h"
}

namespace {
struct LedgerHarness {
	std::map<std::string, std::set<std::string>> closures;
	std::map<std::string, int> retired;
	std::set<std::string> invalid;
	std::set<std::string> reload_fail;
};

s32 closureContains(const catalog_activation_root_t *root,
	const char *dependency, s32 *out_contains, void *userdata)
{
	auto &h = *static_cast<LedgerHarness *>(userdata);
	if (h.invalid.count(root->id)) return 0;
	*out_contains = h.closures[root->id].count(dependency) ? 1 : 0;
	return 1;
}

void retireRoot(const catalog_activation_root_t *root, void *userdata)
{
	auto &h = *static_cast<LedgerHarness *>(userdata);
	h.retired[root->id] += root->references;
}

s32 reloadRoot(const catalog_activation_root_t *root, void *userdata)
{
	auto &h = *static_cast<LedgerHarness *>(userdata);
	if (h.reload_fail.count(root->id)) return 0;
	for (int i = 0; i < root->references; i++) {
		if (!catalogActivationLedgerRecord(root->id, root->type, root->bundled))
			return 0;
	}
	catalogActivationLedgerSetStageOwned(root->id, root->stage_owned);
	return 1;
}
}

TEST_CASE("reverse invalidation retires exact overlapping roots through a diamond",
	"[catalog][effect][lifecycle][t-assets-033]")
{
	catalogActivationLedgerClear();
	LedgerHarness h;
	h.closures["mod:weapon_a"] = {"mod:effect", "mod:material_a",
		"mod:material_b", "mod:texture"};
	h.closures["mod:weapon_b"] = {"mod:effect", "mod:material_b",
		"mod:texture"};
	h.closures["mod:texture"] = {"mod:texture"};

	REQUIRE(catalogActivationLedgerRecord("mod:weapon_a", ASSET_WEAPON, 0));
	REQUIRE(catalogActivationLedgerRecord("mod:weapon_a", ASSET_WEAPON, 0));
	REQUIRE(catalogActivationLedgerRecord("mod:weapon_b", ASSET_WEAPON, 0));
	/* A direct child root is owned by ordinary child teardown, not reverse
	 * invalidation, so its independent owner survives this transaction. */
	REQUIRE(catalogActivationLedgerRecord("mod:texture", ASSET_TEXTURE, 0));
	catalogActivationLedgerSetStageOwned("mod:weapon_a", 1);

	REQUIRE(catalogActivationLedgerInvalidateDependents("mod:texture",
		closureContains, retireRoot, &h));
	REQUIRE(h.retired["mod:weapon_a"] == 2);
	REQUIRE(h.retired["mod:weapon_b"] == 1);
	REQUIRE(h.retired.count("mod:texture") == 0);
	REQUIRE(catalogActivationLedgerActiveCount() == 1);
	REQUIRE(catalogActivationLedgerPendingCount() == 2);

	REQUIRE(catalogActivationLedgerReloadPending(reloadRoot, &h));
	REQUIRE(catalogActivationLedgerActiveCount() == 3);
	REQUIRE(catalogActivationLedgerPendingCount() == 0);
	const catalog_activation_root_t *a = nullptr;
	for (size_t i = 0; i < catalogActivationLedgerActiveCount(); i++) {
		const auto *row = catalogActivationLedgerActiveAt(i);
		if (std::string(row->id) == "mod:weapon_a") a = row;
	}
	REQUIRE(a != nullptr);
	REQUIRE(a->references == 2);
	REQUIRE(a->stage_owned == 1);
	catalogActivationLedgerClear();
}

TEST_CASE("cycle or type preflight aborts reverse invalidation before retirement",
	"[catalog][effect][lifecycle][t-assets-033][fail-closed]")
{
	catalogActivationLedgerClear();
	LedgerHarness h;
	h.closures["mod:effect_a"] = {"mod:audio"};
	h.closures["mod:effect_b"] = {"mod:audio"};
	h.invalid.insert("mod:effect_b");
	REQUIRE(catalogActivationLedgerRecord("mod:effect_a", ASSET_EFFECT, 0));
	REQUIRE(catalogActivationLedgerRecord("mod:effect_b", ASSET_EFFECT, 0));
	REQUIRE_FALSE(catalogActivationLedgerInvalidateDependents("mod:audio",
		closureContains, retireRoot, &h));
	REQUIRE(h.retired.empty());
	REQUIRE(catalogActivationLedgerActiveCount() == 2);
	REQUIRE(catalogActivationLedgerPendingCount() == 0);
	catalogActivationLedgerClear();
}

TEST_CASE("failed replacement reload remains pending and resets clear exact lanes",
	"[catalog][effect][lifecycle][t-assets-033][replacement][reset]")
{
	catalogActivationLedgerClear();
	LedgerHarness h;
	h.closures["base:effect"] = {"mod:audio"};
	h.closures["mod:effect"] = {"mod:audio"};
	REQUIRE(catalogActivationLedgerRecord("base:effect", ASSET_EFFECT, 1));
	REQUIRE(catalogActivationLedgerRecord("mod:effect", ASSET_EFFECT, 0));
	REQUIRE(catalogActivationLedgerInvalidateDependents("mod:audio",
		closureContains, retireRoot, &h));
	h.reload_fail.insert("mod:effect");
	h.reload_fail.insert("base:effect");
	REQUIRE_FALSE(catalogActivationLedgerReloadPending(reloadRoot, &h));
	REQUIRE(catalogActivationLedgerActiveCount() == 0);
	REQUIRE(catalogActivationLedgerPendingCount() == 2);

	/* Mod reset discards the removed mod owner but preserves a bundled pending
	 * owner so a subsequent rescan can reload it from its retained typed edge. */
	catalogActivationLedgerClearMods();
	REQUIRE(catalogActivationLedgerActiveCount() == 0);
	REQUIRE(catalogActivationLedgerPendingCount() == 1);
	REQUIRE(std::string(catalogActivationLedgerPendingAt(0)->id) == "base:effect");
	h.reload_fail.clear();
	REQUIRE(catalogActivationLedgerReloadPending(reloadRoot, &h));
	REQUIRE(catalogActivationLedgerActiveCount() == 1);
	REQUIRE(catalogActivationLedgerPendingCount() == 0);
	catalogActivationLedgerClear();
	REQUIRE(catalogActivationLedgerActiveCount() == 0);
	REQUIRE(catalogActivationLedgerPendingCount() == 0);
}
