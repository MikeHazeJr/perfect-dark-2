/*
 * test_player_identity.cpp -- B-1064 exact typed player identity planner.
 *
 * These tests pin the prepare boundary that prevents unresolved typed
 * body/head IDs from becoming slot-zero or cached numeric identities.  The
 * planner must also reset its output before every attempt so a failed retry
 * cannot publish stale state from an earlier successful prepare.
 */

#include "catch.hpp"

#include <cstring>

extern "C" {
#include "player_identity.h"
void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
void testStubAssetCatalogResolvePair(const asset_entry_t *first,
		const asset_entry_t *second);
void testStubCatalogCompleteBodyNum(s32 bodynum);
}

static void initEntry(asset_entry_t &entry, asset_type_e type, const char *id,
		s32 runtime_index, s32 bound_index, s16 mp_index)
{
	std::memset(&entry, 0, sizeof(entry));
	entry.occupied = 1;
	entry.enabled = 1;
	entry.type = type;
	entry.runtime_index = runtime_index;
	entry.mp_index = mp_index;
	std::strncpy(entry.id, id, sizeof(entry.id) - 1);

	if (type == ASSET_BODY) {
		entry.ext.body.bodynum = (s16)bound_index;
		std::strncpy(entry.ext.body.rig_class, "test_rig",
			sizeof(entry.ext.body.rig_class) - 1);
	} else if (type == ASSET_HEAD) {
		entry.ext.head.headnum = (s16)bound_index;
		std::strncpy(entry.ext.head.rig_class, "test_rig",
			sizeof(entry.ext.head.rig_class) - 1);
	}
}

static void requireResetState(const player_identity_plan_t &plan,
		player_identity_status_e status)
{
	REQUIRE(plan.body_id[0] == '\0');
	REQUIRE(plan.head_id[0] == '\0');
	REQUIRE(plan.runtime_bodynum == -1);
	REQUIRE(plan.runtime_headnum == -1);
	REQUIRE(plan.mp_body_index == -1);
	REQUIRE(plan.mp_head_index == -1);
	REQUIRE(plan.status == status);
}

TEST_CASE("player identity status strings are stable", "[player][identity]")
{
	const player_identity_status_e statuses[] = {
		PLAYER_IDENTITY_OK,
		PLAYER_IDENTITY_INVALID_ARGUMENT,
		PLAYER_IDENTITY_EMPTY_BODY_ID,
		PLAYER_IDENTITY_EMPTY_HEAD_ID,
		PLAYER_IDENTITY_UNTERMINATED_BODY_ID,
		PLAYER_IDENTITY_UNTERMINATED_HEAD_ID,
		PLAYER_IDENTITY_MISSING_BODY_ENTRY,
		PLAYER_IDENTITY_MISSING_HEAD_ENTRY,
		PLAYER_IDENTITY_DISABLED_BODY_ENTRY,
		PLAYER_IDENTITY_DISABLED_HEAD_ENTRY,
		PLAYER_IDENTITY_WRONG_BODY_TYPE,
		PLAYER_IDENTITY_WRONG_HEAD_TYPE,
		PLAYER_IDENTITY_EMPTY_BODY_RIG_CLASS,
		PLAYER_IDENTITY_EMPTY_HEAD_RIG_CLASS,
		PLAYER_IDENTITY_INCOMPATIBLE_RIG_CLASS,
		PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX,
		PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX,
		PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING,
		PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING,
		PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH,
		PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH,
	};

	for (player_identity_status_e status : statuses) {
		REQUIRE(std::strlen(playerIdentityStatusString(status)) > 0);
	}
	REQUIRE(std::strcmp(playerIdentityStatusString((player_identity_status_e)99),
			"unknown") == 0);
}

TEST_CASE("player identity fails closed on missing or incompatible rig classes",
		"[player][identity][rig][B-183]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:dark_combat", 86, 86, 0);
	initEntry(head, ASSET_HEAD, "base:head_dark_combat", 4, 4, 0);
	std::strcpy(body.ext.body.rig_class, "human_female_neck_standard");
	std::strcpy(head.ext.head.rig_class, "human_female_neck_standard");
	REQUIRE(playerIdentityPrepareResolved("base:dark_combat",
		"base:head_dark_combat", &body, &head, &plan) ==
		PLAYER_IDENTITY_OK);

	body.ext.body.rig_class[0] = '\0';
	REQUIRE(playerIdentityPrepareResolved("base:dark_combat",
		"base:head_dark_combat", &body, &head, &plan) ==
		PLAYER_IDENTITY_EMPTY_BODY_RIG_CLASS);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_BODY_RIG_CLASS);

	std::strcpy(body.ext.body.rig_class, "human_female_neck_standard");
	head.ext.head.rig_class[0] = '\0';
	REQUIRE(playerIdentityPrepareResolved("base:dark_combat",
		"base:head_dark_combat", &body, &head, &plan) ==
		PLAYER_IDENTITY_EMPTY_HEAD_RIG_CLASS);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_HEAD_RIG_CLASS);

	std::strcpy(head.ext.head.rig_class, "human_male_neck_standard");
	REQUIRE(playerIdentityPrepareResolved("base:dark_combat",
		"base:head_dark_combat", &body, &head, &plan) ==
		PLAYER_IDENTITY_INCOMPATIBLE_RIG_CLASS);
	requireResetState(plan, PLAYER_IDENTITY_INCOMPATIBLE_RIG_CLASS);
}

TEST_CASE("player identity plans base and custom exact bindings",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:joanna_dark", 12, 12, 3);
	initEntry(head, ASSET_HEAD, "base:head_joanna", 27, 27, 4);

	REQUIRE(playerIdentityPrepareResolved("base:joanna_dark", "base:head_joanna",
			&body, &head, &plan) == PLAYER_IDENTITY_OK);
	REQUIRE(plan.status == PLAYER_IDENTITY_OK);
	REQUIRE(std::strcmp(plan.body_id, "base:joanna_dark") == 0);
	REQUIRE(std::strcmp(plan.head_id, "base:head_joanna") == 0);
	REQUIRE(plan.runtime_bodynum == 12);
	REQUIRE(plan.runtime_headnum == 27);
	REQUIRE(plan.mp_body_index == 3);
	REQUIRE(plan.mp_head_index == 4);

	initEntry(body, ASSET_BODY, "mod:burst_body", 193, 193, -1);
	initEntry(head, ASSET_HEAD, "mod:burst_head", 221, 221, 300);
	REQUIRE(playerIdentityPrepareResolved("mod:burst_body", "mod:burst_head",
			&body, &head, &plan) == PLAYER_IDENTITY_OK);
	REQUIRE(plan.runtime_bodynum == 193);
	REQUIRE(plan.runtime_headnum == 221);
	REQUIRE(plan.mp_body_index == -1);
	REQUIRE(plan.mp_head_index == -1);
}

TEST_CASE("player identity rejects invalid arguments and empty IDs",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 1, 1, 0);
	initEntry(head, ASSET_HEAD, "base:head", 2, 2, 0);

	REQUIRE(playerIdentityPrepareResolved(NULL, "base:head", &body, &head,
			&plan) == PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareResolved("base:body", NULL, &body, &head,
			&plan) == PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareResolved("", "base:head", &body, &head,
			&plan) == PLAYER_IDENTITY_EMPTY_BODY_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_BODY_ID);
	REQUIRE(playerIdentityPrepareResolved("base:body", "", &body, &head,
			&plan) == PLAYER_IDENTITY_EMPTY_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_HEAD_ID);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body, &head,
			NULL) == PLAYER_IDENTITY_INVALID_ARGUMENT);
}

TEST_CASE("player identity rejects unterminated or overlong IDs",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;
	char overlong_body[CATALOG_ID_LEN];
	char overlong_head[CATALOG_ID_LEN];

	initEntry(body, ASSET_BODY, "base:body", 1, 1, 0);
	initEntry(head, ASSET_HEAD, "base:head", 2, 2, 0);
	std::memset(overlong_body, 'b', sizeof(overlong_body));
	std::memset(overlong_head, 'h', sizeof(overlong_head));

	REQUIRE(playerIdentityPrepareResolved(overlong_body, "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	requireResetState(plan, PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	REQUIRE(playerIdentityPrepareResolved("base:body", overlong_head, &body,
			&head, &plan) == PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
}

TEST_CASE("player identity reports missing entries and exact ID mismatch",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	asset_entry_t unoccupied;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 1, 1, 0);
	initEntry(head, ASSET_HEAD, "base:head", 2, 2, 0);
	std::memset(&unoccupied, 0, sizeof(unoccupied));

	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", NULL,
			&head, &plan) == PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			NULL, &plan) == PLAYER_IDENTITY_MISSING_HEAD_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_HEAD_ENTRY);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &unoccupied,
			&head, &plan) == PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_BODY_ENTRY);

	std::strcpy(body.id, "base:other_body");
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH);
	requireResetState(plan, PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH);

	std::strcpy(body.id, "base:body");
	std::strcpy(head.id, "base:other_head");
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH);
	requireResetState(plan, PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH);
}

TEST_CASE("player identity rejects disabled resolved entries",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 1, 1, 0);
	initEntry(head, ASSET_HEAD, "base:head", 2, 2, 0);
	body.enabled = 0;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_DISABLED_BODY_ENTRY);

	body.enabled = 1;
	head.enabled = 0;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_DISABLED_HEAD_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_DISABLED_HEAD_ENTRY);
}

TEST_CASE("player identity enforces body and head type and bindings",
		"[player][identity]")
{
	asset_entry_t body;
	asset_entry_t head;
	asset_entry_t wrong;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 1, 1, 0);
	initEntry(head, ASSET_HEAD, "base:head", 2, 2, 0);
	initEntry(wrong, ASSET_WEAPON, "base:body", 1, 1, 0);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &wrong,
			&head, &plan) == PLAYER_IDENTITY_WRONG_BODY_TYPE);
	requireResetState(plan, PLAYER_IDENTITY_WRONG_BODY_TYPE);

	initEntry(wrong, ASSET_BODY, "base:head", 2, 2, 0);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&wrong, &plan) == PLAYER_IDENTITY_WRONG_HEAD_TYPE);
	requireResetState(plan, PLAYER_IDENTITY_WRONG_HEAD_TYPE);

	body.runtime_index = -1;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);
	body.runtime_index = 1;
	body.ext.body.bodynum = -1;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX);

	body.ext.body.bodynum = 9;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
	requireResetState(plan, PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
	body.ext.body.bodynum = 1;

	head.runtime_index = -1;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	head.runtime_index = 2;
	head.ext.head.headnum = -1;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);

	head.ext.head.headnum = 8;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING);
	requireResetState(plan, PLAYER_IDENTITY_INCONSISTENT_HEAD_BINDING);
}

TEST_CASE("player identity resets a successful plan before a failed retry",
		"[player][identity][rollback]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 4, 4, 0);
	initEntry(head, ASSET_HEAD, "base:head", 5, 5, 0);
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_OK);
	REQUIRE(plan.runtime_bodynum == 4);

	head.type = ASSET_BODY;
	REQUIRE(playerIdentityPrepareResolved("base:body", "base:head", &body,
			&head, &plan) == PLAYER_IDENTITY_WRONG_HEAD_TYPE);
	requireResetState(plan, PLAYER_IDENTITY_WRONG_HEAD_TYPE);
}

TEST_CASE("player identity catalog wrapper resolves through exact IDs",
		"[player][identity][catalog]")
{
	asset_entry_t body;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 7, 7, 0);
	testStubAssetCatalogResolveWith(&body);

	REQUIRE(playerIdentityPrepare("base:body", "base:head", &plan)
			== PLAYER_IDENTITY_MISSING_HEAD_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_HEAD_ENTRY);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("player identity catalog wrapper rejects malformed input before lookup",
		"[player][identity][catalog]")
{
	player_identity_plan_t plan;
	char unterminated[CATALOG_ID_LEN];
	std::memset(unterminated, 'x', sizeof(unterminated));

	REQUIRE(playerIdentityPrepare(NULL, "base:head", &plan)
			== PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepare("", "base:head", &plan)
			== PLAYER_IDENTITY_EMPTY_BODY_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_BODY_ID);
	REQUIRE(playerIdentityPrepare("base:body", unterminated, &plan)
			== PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
}

TEST_CASE("player identity runtime wrapper round-trips exact typed bindings",
		"[player][identity][runtime]")
{
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;

	initEntry(body, ASSET_BODY, "base:body", 17, 17, 3);
	initEntry(head, ASSET_HEAD, "base:head", 29, 29, 4);
	testStubAssetCatalogResolvePair(&body, &head);

	REQUIRE(playerIdentityPrepareRuntime(17, 29, &plan) ==
		PLAYER_IDENTITY_OK);
	REQUIRE(std::strcmp(plan.body_id, "base:body") == 0);
	REQUIRE(std::strcmp(plan.head_id, "base:head") == 0);
	REQUIRE(plan.runtime_bodynum == 17);
	REQUIRE(plan.runtime_headnum == 29);

	testStubCatalogCompleteBodyNum(17);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, &plan) ==
		PLAYER_IDENTITY_OK);
	REQUIRE(std::strcmp(plan.body_id, "base:body") == 0);
	REQUIRE(plan.head_id[0] == '\0');
	REQUIRE(plan.runtime_bodynum == 17);
	REQUIRE(plan.runtime_headnum == -1);
	REQUIRE(plan.mp_body_index == 3);
	REQUIRE(plan.mp_head_index == -1);

	testStubCatalogCompleteBodyNum(-1);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, &plan) ==
		PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);

	REQUIRE(playerIdentityPrepareRuntime(18, 29, &plan) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	testStubAssetCatalogResolveWith(NULL);
}

namespace {
struct CompleteBodyStubScope {
	CompleteBodyStubScope() { testStubAssetCatalogResolveWith(NULL); }
	~CompleteBodyStubScope() { testStubAssetCatalogResolveWith(NULL); }
};

void requireBodyOnlyState(const player_identity_plan_t &plan, const char *id,
		s32 bodynum, s32 mp_index)
{
	REQUIRE(plan.status == PLAYER_IDENTITY_OK);
	REQUIRE(std::strcmp(plan.body_id, id) == 0);
	REQUIRE(plan.runtime_bodynum == bodynum);
	REQUIRE(plan.runtime_headnum == -1);
	REQUIRE(plan.mp_body_index == mp_index);
	REQUIRE(plan.mp_head_index == -1);
	for (char byte : plan.head_id) {
		REQUIRE(byte == '\0');
	}
}
}

TEST_CASE("complete bodies accept empty public head IDs without catalog mutation",
		"[player][identity][catalog][integrated][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	player_identity_plan_t plan;
	initEntry(body, ASSET_BODY, "mod:integrated", 193, 193, 300);
	/* No external attachment requires a head rig: preserve RuntimeBodyOnly. */
	body.ext.body.rig_class[0] = '\0';
	const asset_entry_t original = body;
	testStubCatalogCompleteBodyNum(193);
	std::memset(&plan, 0x5a, sizeof(plan));

	/* No entry is in the resolver: Resolved must consume its supplied entry. */
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", "", &body, NULL,
		&plan) == PLAYER_IDENTITY_OK);
	requireBodyOnlyState(plan, "mod:integrated", 193, -1);
	REQUIRE(std::memcmp(&body, &original, sizeof(body)) == 0);

	testStubAssetCatalogResolveWith(&body);
	testStubCatalogCompleteBodyNum(193);
	REQUIRE(playerIdentityPrepare("mod:integrated", "", &plan) ==
		PLAYER_IDENTITY_OK);
	requireBodyOnlyState(plan, "mod:integrated", 193, -1);
	REQUIRE(playerIdentityPrepareRuntime(193, -1, &plan) == PLAYER_IDENTITY_OK);
	requireBodyOnlyState(plan, "mod:integrated", 193, -1);
	REQUIRE(std::memcmp(&body, &original, sizeof(body)) == 0);
}

TEST_CASE("empty public head IDs require complete bodies and never select a fallback",
		"[player][identity][catalog][integrated][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	player_identity_plan_t plan;
	initEntry(body, ASSET_BODY, "base:integrated", 17, 17, 0);
	testStubAssetCatalogResolveWith(&body);
	REQUIRE(playerIdentityPrepareResolved("base:integrated", "", &body, NULL,
		&plan) == PLAYER_IDENTITY_EMPTY_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_HEAD_ID);
	REQUIRE(playerIdentityPrepare("base:integrated", "", &plan) ==
		PLAYER_IDENTITY_EMPTY_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_HEAD_ID);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, &plan) ==
		PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);
	requireResetState(plan, PLAYER_IDENTITY_UNBOUND_HEAD_RUNTIME_INDEX);

	testStubCatalogCompleteBodyNum(17);
	REQUIRE(playerIdentityPrepare("base:integrated", "", &plan) ==
		PLAYER_IDENTITY_OK);
	requireBodyOnlyState(plan, "base:integrated", 17, 0);
	/* Neither a native-looking ID nor a selector-looking ID recovers slot zero. */
	const char *missing_ids[] = {"base:drcaroll_missing", "17", "mod:missing"};
	for (const char *missing : missing_ids) {
		REQUIRE(playerIdentityPrepare(missing, "", &plan) ==
			PLAYER_IDENTITY_MISSING_BODY_ENTRY);
		requireResetState(plan, PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	}
}

TEST_CASE("complete body-only preparation validates every supplied body binding",
		"[player][identity][integrated][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	player_identity_plan_t plan;
	player_identity_status_e expected = PLAYER_IDENTITY_INVALID_ARGUMENT;
	initEntry(body, ASSET_BODY, "mod:integrated", 17, 17, 3);
	testStubCatalogCompleteBodyNum(17);

	SECTION("unoccupied body") {
		body.occupied = 0;
		expected = PLAYER_IDENTITY_MISSING_BODY_ENTRY;
	}
	SECTION("disabled body") {
		body.enabled = 0;
		expected = PLAYER_IDENTITY_DISABLED_BODY_ENTRY;
	}
	SECTION("different exact body ID") {
		std::strcpy(body.id, "mod:other_body");
		expected = PLAYER_IDENTITY_BODY_ID_ENTRY_MISMATCH;
	}
	SECTION("wrong asset type") {
		body.type = ASSET_HEAD;
		expected = PLAYER_IDENTITY_WRONG_BODY_TYPE;
	}
	SECTION("unbound runtime body") {
		body.runtime_index = -1;
		expected = PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX;
	}
	SECTION("unbound authored body") {
		body.ext.body.bodynum = -1;
		expected = PLAYER_IDENTITY_UNBOUND_BODY_RUNTIME_INDEX;
	}
	SECTION("inconsistent body binding") {
		body.ext.body.bodynum = 18;
		expected = PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING;
	}
	const asset_entry_t original = body;
	std::memset(&plan, 0x5a, sizeof(plan));
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", "", &body, NULL,
		&plan) == expected);
	requireResetState(plan, expected);
	REQUIRE(std::memcmp(&body, &original, sizeof(body)) == 0);
}

TEST_CASE("empty-head catalog preparation rejects missing and disabled bodies on retry",
		"[player][identity][catalog][integrated][rollback][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	player_identity_plan_t plan;
	initEntry(body, ASSET_BODY, "mod:integrated", 17, 17, 3);
	testStubAssetCatalogResolveWith(&body);
	testStubCatalogCompleteBodyNum(17);
	REQUIRE(playerIdentityPrepare("mod:integrated", "", &plan) ==
		PLAYER_IDENTITY_OK);
	body.enabled = 0;
	REQUIRE(playerIdentityPrepare("mod:integrated", "", &plan) ==
		PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, &plan) ==
		PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_DISABLED_BODY_ENTRY);
	body.enabled = 1;
	REQUIRE(playerIdentityPrepare("mod:integrated", "", &plan) ==
		PLAYER_IDENTITY_OK);
	body.occupied = 0;
	REQUIRE(playerIdentityPrepare("mod:integrated", "", &plan) ==
		PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", "", NULL, NULL,
		&plan) == PLAYER_IDENTITY_MISSING_BODY_ENTRY);
	requireResetState(plan, PLAYER_IDENTITY_MISSING_BODY_ENTRY);

	body.occupied = 1;
	body.runtime_index = 18;
	/* Runtime mismatch keeps precedence over the absent complete-body flag. */
	testStubCatalogCompleteBodyNum(-1);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, &plan) ==
		PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
	requireResetState(plan, PLAYER_IDENTITY_INCONSISTENT_BODY_BINDING);
}

TEST_CASE("complete bodies preserve valid supplied heads but reject empty-ID entry conflicts",
		"[player][identity][integrated][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	asset_entry_t head;
	player_identity_plan_t plan;
	initEntry(body, ASSET_BODY, "base:integrated", 17, 17, 3);
	initEntry(head, ASSET_HEAD, "base:head", 29, 29, 4);
	const asset_entry_t original_body = body;
	const asset_entry_t original_head = head;
	testStubAssetCatalogResolvePair(&body, &head);
	testStubCatalogCompleteBodyNum(17);
	REQUIRE(playerIdentityPrepareResolved("base:integrated", "base:head", &body,
		&head, &plan) == PLAYER_IDENTITY_OK);
	REQUIRE(playerIdentityPrepare("base:integrated", "base:head", &plan) ==
		PLAYER_IDENTITY_OK);
	REQUIRE(playerIdentityPrepareRuntime(17, 29, &plan) == PLAYER_IDENTITY_OK);
	REQUIRE(std::strcmp(plan.body_id, "base:integrated") == 0);
	REQUIRE(std::strcmp(plan.head_id, "base:head") == 0);
	REQUIRE(plan.runtime_bodynum == 17);
	REQUIRE(plan.runtime_headnum == 29);
	REQUIRE(plan.mp_body_index == 3);
	REQUIRE(plan.mp_head_index == 4);

	REQUIRE(playerIdentityPrepareResolved("base:integrated", "", &body, &head,
		&plan) == PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH);
	requireResetState(plan, PLAYER_IDENTITY_HEAD_ID_ENTRY_MISMATCH);
	REQUIRE(std::memcmp(&body, &original_body, sizeof(body)) == 0);
	REQUIRE(std::memcmp(&head, &original_head, sizeof(head)) == 0);
	REQUIRE(playerIdentityPrepareResolved("base:integrated", "", &body, NULL,
		&plan) == PLAYER_IDENTITY_OK);
	requireBodyOnlyState(plan, "base:integrated", 17, 3);
}

TEST_CASE("body-only preparation retains bounded pointer and output contracts",
		"[player][identity][integrated][T-ASSETS-048]")
{
	CompleteBodyStubScope scope;
	asset_entry_t body;
	player_identity_plan_t plan;
	char unterminated[CATALOG_ID_LEN];
	std::memset(unterminated, 'x', sizeof(unterminated));
	initEntry(body, ASSET_BODY, "mod:integrated", 17, 17, 3);
	testStubAssetCatalogResolveWith(&body);
	testStubCatalogCompleteBodyNum(17);
	REQUIRE(playerIdentityPrepareResolved(NULL, "", &body, NULL, &plan) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareResolved("", "", &body, NULL, &plan) ==
		PLAYER_IDENTITY_EMPTY_BODY_ID);
	requireResetState(plan, PLAYER_IDENTITY_EMPTY_BODY_ID);
	REQUIRE(playerIdentityPrepareResolved(unterminated, "", &body, NULL, &plan) ==
		PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	requireResetState(plan, PLAYER_IDENTITY_UNTERMINATED_BODY_ID);
	REQUIRE(playerIdentityPrepare("mod:integrated", NULL, &plan) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", NULL, &body, NULL,
		&plan) == PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", unterminated, &body,
		NULL, &plan) == PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	requireResetState(plan, PLAYER_IDENTITY_UNTERMINATED_HEAD_ID);
	REQUIRE(playerIdentityPrepareResolved("mod:integrated", "", &body, NULL,
		NULL) == PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepare("mod:integrated", "", NULL) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareRuntime(17, -1, NULL) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	REQUIRE(playerIdentityPrepareRuntime(17, -2, &plan) ==
		PLAYER_IDENTITY_INVALID_ARGUMENT);
	requireResetState(plan, PLAYER_IDENTITY_INVALID_ARGUMENT);
	playerIdentityPlanReset(NULL);
}
