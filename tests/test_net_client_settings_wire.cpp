/*
 * test_net_client_settings_wire.cpp -- B-1071 v55 settings transaction.
 */

#include "catch.hpp"

#include <cmath>
#include <cstring>

extern "C" {
#include <PR/ultratypes.h>
#include "net/net_client_settings_wire.h"
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};
void netbufStartReadData(struct netbuf *buf, const void *data, u32 size);
s32 netbufReadLeft(const struct netbuf *buf);
u8 netbufReadU8(struct netbuf *buf);
void netbufStartWrite(struct netbuf *buf);
u32 netbufWriteU8(struct netbuf *buf, u8 value);
u32 netbufWriteU16(struct netbuf *buf, u16 value);
u32 netbufWriteF32(struct netbuf *buf, f32 value);
u32 netbufWriteStr(struct netbuf *buf, const char *value);
void testStubAssetCatalogResolvePair(const asset_entry_t *first,
	const asset_entry_t *second);
void testStubAssetCatalogResolveWith(const asset_entry_t *entry);
}

static void initSettingsAsset(asset_entry_t &entry, asset_type_e type,
		const char *id, s32 runtime_index, s16 mp_index)
{
	std::memset(&entry, 0, sizeof(entry));
	entry.occupied = 1;
	entry.enabled = 1;
	entry.type = type;
	entry.runtime_index = runtime_index;
	entry.mp_index = mp_index;
	std::strncpy(entry.id, id, sizeof(entry.id) - 1);
	if (type == ASSET_BODY) {
		entry.ext.body.bodynum = (s16)runtime_index;
	} else if (type == ASSET_HEAD) {
		entry.ext.head.headnum = (s16)runtime_index;
	}
}

static net_client_settings_input_t distinctSettingsInput()
{
	net_client_settings_input_t input = {};
	input.options = 0x5a3c;
	input.body_id = "mod:body_distinct";
	input.head_id = "mod:head_distinct";
	input.team = 3;
	input.handicap = 0xb7;
	input.fovy = 73.5f;
	input.fovzoommult = 1.225f;
	input.name = "RemoteAgent";
	return input;
}

static void writeUncheckedPayload(struct netbuf &buf,
		const net_client_settings_input_t &input)
{
	netbufWriteU16(&buf, input.options);
	netbufWriteStr(&buf, input.body_id);
	netbufWriteStr(&buf, input.head_id);
	netbufWriteU8(&buf, input.team);
	netbufWriteU8(&buf, input.handicap);
	netbufWriteF32(&buf, input.fovy);
	netbufWriteF32(&buf, input.fovzoommult);
	netbufWriteStr(&buf, input.name);
}

TEST_CASE("v55 client settings roundtrip every exact field",
		"[net][settings][wire][b1071]")
{
	asset_entry_t body;
	asset_entry_t head;
	u8 bytes[512] = {};
	struct netbuf writebuf = { bytes, (u32)sizeof(bytes), 0, 0, 0 };
	struct netbuf readbuf;
	net_client_settings_input_t input = distinctSettingsInput();
	net_client_settings_plan_t decoded = {};
	player_identity_status_e identity_status = PLAYER_IDENTITY_INVALID_ARGUMENT;

	initSettingsAsset(body, ASSET_BODY, input.body_id, 81, 7);
	initSettingsAsset(head, ASSET_HEAD, input.head_id, 93, 9);
	testStubAssetCatalogResolvePair(&body, &head);
	netbufStartWrite(&writebuf);
	REQUIRE(netClientSettingsWireWrite(&writebuf, 0x05, &input,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_OK);
	REQUIRE(identity_status == PLAYER_IDENTITY_OK);
	REQUIRE(writebuf.error == 0);

	netbufStartReadData(&readbuf, bytes, writebuf.wp);
	REQUIRE(netbufReadU8(&readbuf) == 0x05);
	REQUIRE(netClientSettingsWireRead(&readbuf, &decoded,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_OK);
	REQUIRE(identity_status == PLAYER_IDENTITY_OK);
	REQUIRE(netbufReadLeft(&readbuf) == 0);
	REQUIRE(decoded.options == input.options);
	REQUIRE(std::strcmp(decoded.identity.body_id, input.body_id) == 0);
	REQUIRE(std::strcmp(decoded.identity.head_id, input.head_id) == 0);
	REQUIRE(decoded.identity.runtime_bodynum == 81);
	REQUIRE(decoded.identity.runtime_headnum == 93);
	REQUIRE(decoded.team == input.team);
	REQUIRE(decoded.handicap == input.handicap);
	REQUIRE(decoded.fovy == Approx(input.fovy));
	REQUIRE(decoded.fovzoommult == Approx(input.fovzoommult));
	REQUIRE(std::strcmp(decoded.name, input.name) == 0);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("v55 client settings writer rejects invalid snapshots before bytes",
		"[net][settings][wire][security][b1071]")
{
	asset_entry_t body;
	asset_entry_t head;
	u8 bytes[512] = {};
	struct netbuf buf = { bytes, (u32)sizeof(bytes), 0, 0, 0 };
	net_client_settings_input_t input = distinctSettingsInput();
	player_identity_status_e identity_status = PLAYER_IDENTITY_OK;

	initSettingsAsset(body, ASSET_BODY, input.body_id, 81, 7);
	initSettingsAsset(head, ASSET_HEAD, input.head_id, 93, 9);
	testStubAssetCatalogResolvePair(&body, &head);
	netbufStartWrite(&buf);
	input.handicap = 0;
	REQUIRE(netClientSettingsWireWrite(&buf, 0x05, &input,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_INVALID_HANDICAP);
	REQUIRE(buf.wp == 0);
	REQUIRE(buf.error == 0);

	input = distinctSettingsInput();
	body.type = ASSET_WEAPON;
	REQUIRE(netClientSettingsWireWrite(&buf, 0x05, &input,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_INVALID_IDENTITY);
	REQUIRE(identity_status == PLAYER_IDENTITY_WRONG_BODY_TYPE);
	REQUIRE(buf.wp == 0);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("v55 client settings reader preserves candidate on malformed and zero handicap packets",
		"[net][settings][wire][security][rollback][b1071]")
{
	asset_entry_t body;
	asset_entry_t head;
	u8 bytes[512] = {};
	struct netbuf writebuf = { bytes, (u32)sizeof(bytes), 0, 0, 0 };
	struct netbuf readbuf;
	net_client_settings_input_t input = distinctSettingsInput();
	net_client_settings_plan_t sentinel;
	net_client_settings_plan_t before;
	player_identity_status_e identity_status = PLAYER_IDENTITY_OK;

	initSettingsAsset(body, ASSET_BODY, input.body_id, 81, 7);
	initSettingsAsset(head, ASSET_HEAD, input.head_id, 93, 9);
	testStubAssetCatalogResolvePair(&body, &head);
	std::memset(&sentinel, 0xa5, sizeof(sentinel));
	before = sentinel;

	netbufStartWrite(&writebuf);
	writeUncheckedPayload(writebuf, input);
	netbufStartReadData(&readbuf, bytes, writebuf.wp - 1);
	REQUIRE(netClientSettingsWireRead(&readbuf, &sentinel,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_MALFORMED);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);

	netbufStartWrite(&writebuf);
	input.handicap = 0;
	writeUncheckedPayload(writebuf, input);
	netbufStartReadData(&readbuf, bytes, writebuf.wp);
	REQUIRE(netClientSettingsWireRead(&readbuf, &sentinel,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_INVALID_HANDICAP);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("v55 client settings reader rejects wrong typed identity without publication",
		"[net][settings][wire][security][rollback][b1071]")
{
	asset_entry_t body;
	asset_entry_t head;
	u8 bytes[512] = {};
	struct netbuf writebuf = { bytes, (u32)sizeof(bytes), 0, 0, 0 };
	struct netbuf readbuf;
	net_client_settings_input_t input = distinctSettingsInput();
	net_client_settings_plan_t sentinel;
	net_client_settings_plan_t before;
	player_identity_status_e identity_status = PLAYER_IDENTITY_OK;

	initSettingsAsset(body, ASSET_WEAPON, input.body_id, 81, 7);
	initSettingsAsset(head, ASSET_HEAD, input.head_id, 93, 9);
	testStubAssetCatalogResolvePair(&body, &head);
	std::memset(&sentinel, 0x3c, sizeof(sentinel));
	before = sentinel;
	netbufStartWrite(&writebuf);
	writeUncheckedPayload(writebuf, input);
	netbufStartReadData(&readbuf, bytes, writebuf.wp);
	REQUIRE(netClientSettingsWireRead(&readbuf, &sentinel,
		&identity_status) == NET_CLIENT_SETTINGS_WIRE_INVALID_IDENTITY);
	REQUIRE(identity_status == PLAYER_IDENTITY_WRONG_BODY_TYPE);
	REQUIRE(std::memcmp(&sentinel, &before, sizeof(sentinel)) == 0);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("committed in-match handicap is immutable",
		"[net][settings][handicap][security][b1071]")
{
	REQUIRE(netClientSettingsEffectiveHandicap(0xc0, 0, 0x80) == 0xc0);
	REQUIRE(netClientSettingsEffectiveHandicap(0xc0, 1, 0x80) == 0x80);
	REQUIRE(netClientSettingsEffectiveHandicap(0x40, 1, 0xb7) == 0xb7);
}

TEST_CASE("authenticated settings remain client-bound when roster order swaps",
		"[net][settings][roster][wire][b1071]")
{
	asset_entry_t body;
	asset_entry_t head;
	net_client_settings_input_t clients[2] = {
		distinctSettingsInput(), distinctSettingsInput()
	};
	net_client_settings_plan_t roster[2] = {};
	const u8 order[2] = { 1, 0 };

	clients[0].name = "Authority";
	clients[0].team = 1;
	clients[0].handicap = 0x44;
	clients[1].name = "Invitee";
	clients[1].team = 6;
	clients[1].handicap = 0xd2;
	initSettingsAsset(body, ASSET_BODY, clients[0].body_id, 81, 7);
	initSettingsAsset(head, ASSET_HEAD, clients[0].head_id, 93, 9);
	testStubAssetCatalogResolvePair(&body, &head);

	for (u8 roster_slot = 0; roster_slot < 2; roster_slot++) {
		const u8 client_id = order[roster_slot];
		REQUIRE(netClientSettingsPrepare(&clients[client_id],
			&roster[roster_slot], NULL) == NET_CLIENT_SETTINGS_WIRE_OK);
	}
	REQUIRE(std::strcmp(roster[0].name, "Invitee") == 0);
	REQUIRE(roster[0].team == 6);
	REQUIRE(roster[0].handicap == 0xd2);
	REQUIRE(std::strcmp(roster[1].name, "Authority") == 0);
	REQUIRE(roster[1].team == 1);
	REQUIRE(roster[1].handicap == 0x44);
	testStubAssetCatalogResolveWith(NULL);
}

TEST_CASE("v55 client settings status text is stable",
		"[net][settings][wire][b1071]")
{
	for (int value = NET_CLIENT_SETTINGS_WIRE_OK;
			value <= NET_CLIENT_SETTINGS_WIRE_MALFORMED; value++) {
		REQUIRE(std::strlen(netClientSettingsWireStatusString(
			(net_client_settings_wire_status_e)value)) > 0);
	}
	REQUIRE(std::strcmp(netClientSettingsWireStatusString(
		(net_client_settings_wire_status_e)99), "unknown") == 0);
}
