#include "catch.hpp"

#include "net/grid_edit_room.h"
#include "net/grid_edit_room_wire.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

/* The production C netbuf header includes the project's C bool macro. */
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

constexpr grid_edit_wire_identity_t kIdentity = {3, 0x0123456789abcdefull};
constexpr u64 kHost = 21;
constexpr u64 kGuest = 43;
constexpr u64 kGuestLink = 142;
constexpr u64 kHostLink = 163;
constexpr const char *kArenaId = "example:tri_arena";
using Room = std::unique_ptr<grid_edit_room_t, decltype(&gridEditRoomFree)>;

struct ArenaFixture {
	const std::string path;
	const char *arena_id;
	char digest[SHA256_HEX_SIZE] = {};

	ArenaFixture(const char *arena_path = "examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena",
		const char *id = kArenaId)
		: path((std::filesystem::path(PD_SOURCE_DIR) / arena_path).string()), arena_id(id)
	{
		u8 bytes[SHA256_DIGEST_SIZE] = {};
		REQUIRE(sha256HashFile(path.c_str(), bytes) == 0);
		sha256ToHex(bytes, digest);
	}

	Room openHost() const
	{
		char err[256] = {};
		Room room(gridEditRoomHostOpen(&kIdentity, kHost, arena_id, path.c_str(), digest, err, sizeof(err)),
			gridEditRoomFree);
		INFO(err);
		REQUIRE(room != nullptr);
		return room;
	}

	Room openGuest(const grid_edit_room_invite_t &invite, u64 host_link = kHostLink) const
	{
		char err[256] = {};
		Room room(gridEditRoomGuestOpen(&invite, host_link, path.c_str(), err, sizeof(err)), gridEditRoomFree);
		INFO(err);
		REQUIRE(room != nullptr);
		return room;
	}
};

grid_edit_room_state_t stateOf(const Room &room)
{
	grid_edit_room_state_t state = {};
	REQUIRE(gridEditRoomGetState(room.get(), &state));
	return state;
}

grid_edit_command_t ambient(u64 sequence, u64 revision, s32 enabled, s32 brightness)
{
	grid_edit_command_t command = {};
	command.sequence = sequence;
	command.expected_revision = revision;
	command.operation = GRID_EDIT_SET_AMBIENT_LIGHT;
	command.data.ambient_light = {enabled, brightness};
	return command;
}

void checkRejectedControl(netbuf &wire)
{
	const netbuf before = wire;
	grid_edit_room_control_t control = {};
	control.kind = GRID_EDIT_ROOM_CONTROL_REFUSAL;
	control.invite.identity = {7, 71};
	control.granted_actor = 37;
	control.requested_sequence = 37;
	control.expected_revision = 41;
	control.result = GRID_EDIT_RESOURCE_LEASED;
	control.review.offer_serial = 57;
	control.review.arena_sha256[31] = 0xdd;
	control.review_reply = GRID_EDIT_VARIANT_REVIEW_DECLINED;
	control.review_arena_size = 241;
	control.apply_previous_layout_generation = 43;
	control.apply_layout_generation = 45;
	control.apply_manifest_sha256[0] = 0xb4;
	control.apply_result = GRID_EDIT_LAYOUT_APPLY_READY;

	CHECK_FALSE(gridEditRoomControlRead(&wire, &control));
	CHECK(wire.rp == before.rp);
	CHECK(wire.wp == before.wp);
	CHECK(wire.error == before.error);
	CHECK(control.kind == GRID_EDIT_ROOM_CONTROL_REFUSAL);
	CHECK(control.invite.identity.room_id == 7);
	CHECK(control.invite.identity.generation == 71);
	CHECK(control.granted_actor == 37);
	CHECK(control.requested_sequence == 37);
	CHECK(control.expected_revision == 41);
	CHECK(control.result == GRID_EDIT_RESOURCE_LEASED);
	CHECK(control.review.offer_serial == 57);
	CHECK(control.review.arena_sha256[31] == 0xdd);
	CHECK(control.review_reply == GRID_EDIT_VARIANT_REVIEW_DECLINED);
	CHECK(control.review_arena_size == 241);
	CHECK(control.apply_previous_layout_generation == 43);
	CHECK(control.apply_layout_generation == 45);
	CHECK(control.apply_manifest_sha256[0] == 0xb4);
	CHECK(control.apply_result == GRID_EDIT_LAYOUT_APPLY_READY);
}

grid_edit_room_control_t variantReview()
{
	grid_edit_room_control_t control = {};
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER;
	control.invite.identity = kIdentity;
	auto &review = control.review;
	review.offer_serial = 0x8765432100abcdefull;
	review.draft_revision = 0x0102030405060708ull;
	review.stage_epoch = 0xfedcba98;
	review.edited_room = 2;
	review.extension = 300;
	std::strcpy(review.arena_id, "mod:arena_review");
	review.arena_sha256[0] = 0x12;
	review.arena_sha256[31] = 0x34;
	review.parent_sha256[0] = 0x56;
	review.parent_sha256[31] = 0x78;
	return control;
}

} // namespace

TEST_CASE("Shared Grid requires the exact local native arena and logical identity", "[grid-edit-room]")
{
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	CHECK(invite.baseline_revision == 1);
	CHECK(std::strcmp(invite.arena_id, kArenaId) == 0);
	CHECK(std::strcmp(invite.arena_sha256, fixture.digest) == 0);

	char err[256] = {};
	auto mismatch = invite;
	mismatch.arena_sha256[0] = mismatch.arena_sha256[0] == '0' ? '1' : '0';
	const Room invalid_host(gridEditRoomHostOpen(&kIdentity, kHost, kArenaId,
		fixture.path.c_str(), mismatch.arena_sha256, err, sizeof(err)), gridEditRoomFree);
	CHECK(invalid_host == nullptr);
	CHECK(std::strstr(err, "different saved revision") != nullptr);

	const Room wrong_revision(gridEditRoomGuestOpen(&mismatch, kHostLink,
		fixture.path.c_str(), err, sizeof(err)), gridEditRoomFree);
	CHECK(wrong_revision == nullptr);
	CHECK(std::strstr(err, "different saved revision") != nullptr);
	mismatch = invite;
	std::strcpy(mismatch.arena_id, "example:different_arena");
	const Room wrong_id(gridEditRoomGuestOpen(&mismatch, kHostLink,
		fixture.path.c_str(), err, sizeof(err)), gridEditRoomFree);
	CHECK(wrong_id == nullptr);
	CHECK(std::strstr(err, "arena ID") != nullptr);

	const Room missing(gridEditRoomGuestOpen(&invite, kHostLink, nullptr, err, sizeof(err)), gridEditRoomFree);
	CHECK(missing == nullptr);
	CHECK(std::strstr(err, "unavailable locally") != nullptr);

	const auto guest = fixture.openGuest(invite);
	const auto state = stateOf(guest);
	CHECK(state.pending_invite == 1);
	CHECK(state.ready == 0);
	CHECK(state.local_actor == 0);
	CHECK(state.enabled == 0);
	CHECK(state.brightness == 0);
	auto command = ambient(42, 42, 1, 123);
	CHECK_FALSE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 123, &command));
	CHECK(command.sequence == 42);
}

TEST_CASE("Shared Grid grants bind the exact invitation and authenticated connections", "[grid-edit-room]")
{
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	const auto command = ambient(1, 1, 1, 123);
	grid_edit_receipt_t receipt = {99};
	grid_edit_delta_t delta = {99, 99, command};
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(receipt.revision == 0);
	CHECK(delta.revision == 99);

	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	CHECK_FALSE(gridEditRoomHostCancelInvite(host.get(), kGuest, kGuestLink + 1));
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest + 1, kGuestLink, &invite));
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink + 1, &invite));
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	REQUIRE(gridEditRoomHostCancelInvite(host.get(), kGuest, kGuestLink));
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));

	constexpr u64 reconnected = kGuestLink + 1;
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, reconnected, &invite));
	auto different = invite;
	different.identity.generation++;
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest, reconnected, &different));
	different = invite;
	different.arena_sha256[0] = different.arena_sha256[0] == '0' ? '1' : '0';
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest, reconnected, &different));
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));
	CHECK(stateOf(host).pending_invite == 1);

	const auto guest = fixture.openGuest(invite);
	CHECK_FALSE(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink + 1, kGuest));
	CHECK_FALSE(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink, kHost));
	CHECK_FALSE(gridEditRoomGuestGrant(guest.get(), &different, kHostLink, kGuest));
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, reconnected, &invite));
	REQUIRE(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink, kGuest));
	CHECK(stateOf(host).ready == 1);
	CHECK(stateOf(host).pending_invite == 0);
	CHECK(stateOf(guest).ready == 1);
	CHECK(stateOf(guest).local_actor == kGuest);

	const grid_edit_wire_identity_t wrong_room = {kIdentity.room_id + 1, kIdentity.generation};
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest + 1, reconnected, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kHost, reconnected, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, 0, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(gridEditRoomHostApply(host.get(), &wrong_room, kGuest, reconnected, &command, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK(stateOf(host).revision == 1);
	CHECK(delta.revision == 99);

	auto unsupported = command;
	unsupported.operation = GRID_EDIT_MOVE_SPAWN;
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, reconnected, &unsupported, &receipt, &delta, nullptr, 0)
		== GRID_EDIT_INVALID);
	CHECK(stateOf(host).revision == 1);
}

TEST_CASE("Shared Grid mirrors ordered host and guest edits and retries an exact stale refusal", "[grid-edit-room]")
{
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	const auto guest = fixture.openGuest(invite);
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));
	REQUIRE(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink, kGuest));

	grid_edit_command_t request = {};
	REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 170, &request));
	CHECK(request.sequence == 1);
	CHECK(request.expected_revision == 1);
	CHECK_FALSE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 171, &request));

	const auto local = ambient(1, 1, 1, 90);
	grid_edit_delta_t commit = {};
	grid_edit_receipt_t receipt = {};
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &local, &receipt, &commit, nullptr, 0)
		== GRID_EDIT_APPLIED);
	CHECK(commit.revision == 2);
	CHECK(commit.author_id == kHost);
	CHECK(receipt.revision == 2);
	CHECK(stateOf(host).can_undo_ambient);

	auto altered = commit;
	altered.author_id++;
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &altered, nullptr, 0));
	altered = commit;
	altered.revision++;
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &altered, nullptr, 0));
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink + 1, &commit, nullptr, 0));
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &commit, nullptr, 0));
	CHECK(stateOf(guest).revision == 2);
	CHECK(stateOf(guest).brightness == 90);
	CHECK_FALSE(stateOf(guest).can_undo_ambient);
	CHECK(stateOf(guest).pending_command == 1);

	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request, &receipt, &commit, nullptr, 0)
		== GRID_EDIT_STALE);
	CHECK(receipt.revision == 2);
	CHECK(commit.author_id == kHost);
	CHECK_FALSE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink + 1, 1, 1, GRID_EDIT_STALE));
	CHECK_FALSE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink, 1, 2, GRID_EDIT_STALE));
	CHECK(stateOf(guest).pending_command == 1);
	REQUIRE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink, 1, 1, GRID_EDIT_STALE));
	CHECK(stateOf(guest).pending_command == 0);
	CHECK(stateOf(guest).next_sequence == 1);

	REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 170, &request));
	CHECK(request.sequence == 1);
	CHECK(request.expected_revision == 2);
	CHECK_FALSE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink, 1, 1, GRID_EDIT_STALE));
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request, &receipt, &commit, nullptr, 0)
		== GRID_EDIT_APPLIED);
	CHECK(commit.revision == 3);
	CHECK(commit.author_id == kGuest);
	REQUIRE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink, 1, 2, GRID_EDIT_APPLIED));
	CHECK(stateOf(guest).pending_command == 1);
	altered = commit;
	altered.command.data.ambient_light.brightness = 171;
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &altered, nullptr, 0));
	CHECK(stateOf(guest).revision == 2);
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &commit, nullptr, 0));

	const auto host_state = stateOf(host);
	const auto guest_state = stateOf(guest);
	CHECK(host_state.revision == 3);
	CHECK(guest_state.revision == 3);
	CHECK(host_state.enabled == 1);
	CHECK(host_state.brightness == 170);
	CHECK(guest_state.brightness == host_state.brightness);
	CHECK(guest_state.pending_command == 0);
	CHECK(guest_state.next_sequence == 2);
	CHECK(host_state.next_sequence == 2);
	CHECK(guest_state.can_undo_ambient);
	CHECK(guest_state.previous_ambient.enabled == 1);
	CHECK(guest_state.previous_ambient.brightness == 90);
	CHECK_FALSE(host_state.can_undo_ambient);

	grid_edit_delta_t duplicate = {};
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request, &receipt, &duplicate, nullptr, 0)
		== GRID_EDIT_DUPLICATE);
	CHECK(receipt.revision == 3);
	CHECK(duplicate.revision == 0);

	REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), 0, 35, &request));
	REQUIRE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink,
		request.sequence, request.expected_revision, GRID_EDIT_RESOURCE_EXHAUSTED));
	CHECK(stateOf(guest).pending_command == 0);
	CHECK(stateOf(guest).next_sequence == 2);
	CHECK(stateOf(guest).revision == 3);

	REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), guest_state.previous_ambient.enabled,
		guest_state.previous_ambient.brightness, &request));
	CHECK(request.sequence == 2);
	CHECK(request.expected_revision == 3);
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request, &receipt, &commit, nullptr, 0)
		== GRID_EDIT_APPLIED);
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &commit, nullptr, 0));
	CHECK(stateOf(host).revision == 4);
	CHECK(stateOf(guest).revision == 4);
	CHECK(stateOf(host).brightness == 90);
	CHECK(stateOf(guest).brightness == 90);

	CHECK_FALSE(gridEditRoomHostDetachPeer(host.get(), kGuest, kGuestLink + 1));
	REQUIRE(gridEditRoomHostDetachPeer(host.get(), kGuest, kGuestLink));
	const auto preserved = stateOf(host);
	CHECK(preserved.creator_detached);
	CHECK_FALSE(preserved.ready);
	CHECK(preserved.peer_actor == 0);
	CHECK(preserved.revision == 4);
	CHECK(preserved.brightness == 90);
	u64 saved_revision = 0;
	forge_level_doc_t *saved = gridEditRoomHostCloneDocument(host.get(), &saved_revision);
	REQUIRE(saved);
	CHECK(saved_revision == preserved.revision);
	forgeLevelDocFree(saved);
	const auto after_leave = ambient(3, 4, 1, 105);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &after_leave, &receipt, &commit, nullptr, 0)
		== GRID_EDIT_DENIED);
	CHECK_FALSE(gridEditRoomHostInvite(host.get(), kGuest + 1, kGuestLink + 1, &invite));
	CHECK(sha256VerifyFile(fixture.path.c_str(), fixture.digest) == 1);
}

TEST_CASE("Shared Grid resumes a detached draft with fresh authority and a verified snapshot", "[grid-edit-room]")
{
	using SnapshotBytes = std::unique_ptr<void, decltype(&std::free)>;
	using Document = std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>;
	const ArenaFixture fixture("dev-mods/grid_two_rooms/grid_two_rooms.pdarena", "mod_grid_two_rooms:arena_grid_two_rooms");
	const auto host = fixture.openHost();
	grid_edit_room_invite_t original = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &original));
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &original));

	const Document pinned(gridEditRoomHostCloneDocument(host.get(), nullptr), forgeLevelDocFree);
	REQUIRE(pinned);

	grid_edit_delta_t delta = {};
	const auto first = ambient(1, 1, 1, 147);
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &first, nullptr, &delta, nullptr, 0)
		== GRID_EDIT_APPLIED);
	CHECK(delta.revision == 2);
	REQUIRE(gridEditRoomHostDetachPeer(host.get(), kGuest, kGuestLink));
	CHECK(stateOf(host).creator_detached);
	CHECK(stateOf(host).revision == 2);

	u64 guest_link = kGuestLink;
	u64 host_link = kHostLink;
	SECTION("voluntary departure reuses the same live connection") {}
	SECTION("a reconnect binds a new connection") {
		guest_link++;
		host_link++;
	}

	constexpr u64 fresh_actor = kGuest + 1;
	auto fresh_identity = kIdentity;
	fresh_identity.generation++;
	grid_edit_room_invite_t resumed = {};
	grid_edit_snapshot_offer_t offer = {};
	char err[256] = {};
	const SnapshotBytes stale_generation(gridEditRoomHostPrepareResume(host.get(), &kIdentity, fresh_actor,
		guest_link, &resumed, &offer, err, sizeof(err)), std::free);
	CHECK(stale_generation == nullptr);
	const SnapshotBytes stale_actor(gridEditRoomHostPrepareResume(host.get(), &fresh_identity, kGuest,
		guest_link, &resumed, &offer, err, sizeof(err)), std::free);
	CHECK(stale_actor == nullptr);
	CHECK_FALSE(stateOf(host).pending_invite);

	const SnapshotBytes bytes(gridEditRoomHostPrepareResume(host.get(), &fresh_identity, fresh_actor,
		guest_link, &resumed, &offer, err, sizeof(err)), std::free);
	INFO(err);
	REQUIRE(bytes != nullptr);
	CHECK(offer.revision == 2);
	CHECK(offer.host_sequence == 1);
	CHECK(stateOf(host).pending_resume);
	CHECK_FALSE(stateOf(host).ready);

	const auto next = ambient(2, 2, 1, 179);
	const auto early_guest_edit = ambient(1, 2, 1, 179);
	CHECK(gridEditRoomHostApply(host.get(), &fresh_identity, fresh_actor, guest_link,
		&early_guest_edit, nullptr, nullptr, nullptr, 0) == GRID_EDIT_DENIED);
	grid_edit_room_resume_tail_t tail = {};
	CHECK_FALSE(gridEditRoomHostConfirmResume(host.get(), kGuest, guest_link, &resumed));
	CHECK_FALSE(gridEditRoomHostConfirmResume(host.get(), fresh_actor, guest_link, &original));
	CHECK_FALSE(gridEditRoomHostGrant(host.get(), fresh_actor, guest_link, &resumed));
	CHECK_FALSE(gridEditRoomHostGrantResume(host.get(), fresh_actor, guest_link, &resumed, &offer, &tail));
	REQUIRE(gridEditRoomHostConfirmResume(host.get(), fresh_actor, guest_link, &resumed));

	const auto guest = fixture.openGuest(resumed, host_link);
	REQUIRE(gridEditRoomGuestRequireResume(guest.get(), &fresh_identity, host_link));
	CHECK_FALSE(gridEditRoomGuestGrant(guest.get(), &resumed, host_link, fresh_actor));
	auto wrong_offer = offer;
	wrong_offer.sha256[0] ^= 1;
	CHECK_FALSE(gridEditRoomGuestInstallResume(guest.get(), host_link, &wrong_offer, bytes.get(), offer.size,
		err, sizeof(err)));
	CHECK(stateOf(guest).revision == 1);
	CHECK_FALSE(stateOf(guest).snapshot_ready);
	CHECK_FALSE(gridEditRoomHostGrantResume(host.get(), fresh_actor, guest_link, &resumed, &wrong_offer, &tail));
	REQUIRE(gridEditRoomGuestInstallResume(guest.get(), host_link, &offer, bytes.get(), offer.size,
		err, sizeof(err)));
	CHECK(stateOf(guest).revision == 2);
	CHECK(stateOf(guest).snapshot_ready);
	CHECK_FALSE(stateOf(guest).ready);
	REQUIRE(gridEditRoomHostGrantResume(host.get(), fresh_actor, guest_link, &resumed, &offer, &tail));
	CHECK(tail.count == 0);
	REQUIRE(gridEditRoomGuestGrant(guest.get(), &resumed, host_link, fresh_actor));
	CHECK(stateOf(guest).brightness == 147);

	u64 restored_revision = 0;
	const Document restored(gridEditRoomHostCloneDocument(host.get(), &restored_revision), forgeLevelDocFree);
	REQUIRE(restored);
	CHECK(restored_revision == offer.revision);
	for (s32 member : {FORGE_LEVEL_MEMBER_COVERS, FORGE_LEVEL_MEMBER_PATHS, FORGE_LEVEL_MEMBER_ROOMS}) {
		CAPTURE(forgeLevelDocMemberName(member));
		size_t pinned_size = 0, restored_size = 0;
		const char *pinned_text = forgeLevelDocMemberText(pinned.get(), member, &pinned_size);
		const char *restored_text = forgeLevelDocMemberText(restored.get(), member, &restored_size);
		REQUIRE(pinned_text);
		REQUIRE(pinned_size > 0);
		REQUIRE(restored_text);
		CHECK(std::string(restored_text, restored_size) == std::string(pinned_text, pinned_size));
	}

	const auto stale_edit = ambient(1, 2, 1, 77);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, fresh_actor, guest_link, &stale_edit, nullptr, nullptr,
		nullptr, 0) == GRID_EDIT_DENIED);
	CHECK(gridEditRoomHostApply(host.get(), &fresh_identity, kGuest, guest_link, &stale_edit, nullptr, nullptr,
		nullptr, 0) == GRID_EDIT_DENIED);
	REQUIRE(gridEditRoomHostApply(host.get(), &fresh_identity, kHost, 0, &next, nullptr, &delta, nullptr, 0)
		== GRID_EDIT_APPLIED);
	CHECK(delta.revision == 3);
	CHECK(delta.command.sequence == 2);
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &fresh_identity, host_link, &delta, err, sizeof(err)));
	CHECK(stateOf(guest).revision == 3);
	CHECK(stateOf(guest).brightness == 179);
	grid_edit_command_t guest_edit = {};
	REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 183, &guest_edit));
	CHECK(guest_edit.sequence == 1);
	CHECK(guest_edit.expected_revision == 3);
}

TEST_CASE("A returning Grid creator verifies the snapshot and catches up before editing", "[grid-edit-room]")
{
	using SnapshotBytes = std::unique_ptr<void, decltype(&std::free)>;
	const ArenaFixture fixture("dev-mods/grid_two_rooms/grid_two_rooms.pdarena",
		"mod_grid_two_rooms:arena_grid_two_rooms");
	const auto host = fixture.openHost();
	grid_edit_room_invite_t invited = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invited));
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invited));
	REQUIRE(gridEditRoomHostDetachPeer(host.get(), kGuest, kGuestLink));

	const grid_edit_wire_identity_t fresh = {kIdentity.room_id, kIdentity.generation + 1};
	constexpr u64 returning_actor = kGuest + 1;
	constexpr u64 returning_link = kGuestLink + 1;
	constexpr u64 host_link = kHostLink + 1;
	grid_edit_room_invite_t resume = {};
	grid_edit_snapshot_offer_t offer = {};
	char err[256] = {};
	const SnapshotBytes bytes(gridEditRoomHostPrepareResume(host.get(), &fresh, returning_actor,
		returning_link, &resume, &offer, err, sizeof(err)), std::free);
	INFO(err);
	REQUIRE(bytes);
	CHECK(offer.revision == 1);
	REQUIRE(gridEditRoomHostConfirmResume(host.get(), returning_actor, returning_link, &resume));
	CHECK(stateOf(host).can_edit);
	CHECK_FALSE(stateOf(host).ready);

	const auto guest = fixture.openGuest(resume, host_link);
	REQUIRE(gridEditRoomGuestRequireResume(guest.get(), &fresh, host_link));
	const auto stale = ambient(1, 2, 1, 90);
	CHECK(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &stale, nullptr, nullptr, err, sizeof(err))
		== GRID_EDIT_STALE);
	CHECK(stateOf(host).revision == offer.revision);
	const auto light = ambient(1, 1, 1, 169);
	CHECK(gridEditRoomHostApply(host.get(), &fresh, returning_actor, returning_link, &light, nullptr,
		nullptr, err, sizeof(err)) == GRID_EDIT_DENIED);
	REQUIRE(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &light, nullptr, nullptr, err, sizeof(err))
		== GRID_EDIT_APPLIED);

	grid_edit_command_t move = {};
	move.sequence = 2;
	move.expected_revision = 2;
	move.operation = GRID_EDIT_MOVE_SPAWN;
	REQUIRE(gridEditRoomGetSpawnPose(host.get(), 0, &move.data.move_spawn));
	move.data.move_spawn.position[0] += 25.0f;
	REQUIRE(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &move, nullptr, nullptr, err, sizeof(err))
		== GRID_EDIT_APPLIED);
	CHECK(stateOf(host).revision == 3);

	SECTION("An interrupted invitation preserves the host's edits") {
		REQUIRE(gridEditRoomHostCancelInvite(host.get(), returning_actor, returning_link));
		CHECK(stateOf(host).creator_detached);
		CHECK(stateOf(host).revision == 3);
		CHECK(stateOf(host).brightness == 169);
		CHECK(stateOf(host).next_sequence == 3);
		grid_edit_room_invite_t retry = {};
		grid_edit_snapshot_offer_t retry_offer = {};
		const grid_edit_wire_identity_t retry_identity = {fresh.room_id, fresh.generation + 1};
		const SnapshotBytes retry_bytes(gridEditRoomHostPrepareResume(host.get(), &retry_identity, returning_actor + 1,
			returning_link + 1, &retry, &retry_offer, err, sizeof(err)), std::free);
		REQUIRE(retry_bytes);
		CHECK(retry_offer.revision == 3);
		CHECK(retry_offer.host_sequence == 2);
	}
	SECTION("The host cannot overrun the retained catch-up history") {
		for (u64 sequence = 3; sequence <= GRID_EDIT_DELTA_CAPACITY; ++sequence) {
			const auto change = ambient(sequence, sequence, 1, 100 + (s32)sequence);
			REQUIRE(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &change, nullptr, nullptr, err, sizeof(err))
				== GRID_EDIT_APPLIED);
		}
		const u64 full_revision = offer.revision + GRID_EDIT_DELTA_CAPACITY;
		const auto overflow = ambient(full_revision, full_revision, 1, 25);
		CHECK(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &overflow, nullptr, nullptr, err, sizeof(err))
			== GRID_EDIT_RESOURCE_EXHAUSTED);
		CHECK(stateOf(host).revision == full_revision);
		grid_edit_room_resume_tail_t tail = {};
		REQUIRE(gridEditRoomHostGrantResume(host.get(), returning_actor, returning_link, &resume, &offer, &tail));
		REQUIRE(tail.count == GRID_EDIT_DELTA_CAPACITY);
		CHECK(tail.deltas[0].revision == offer.revision + 1);
		CHECK(tail.deltas[tail.count - 1].revision == full_revision);
		REQUIRE(gridEditRoomHostApply(host.get(), &fresh, kHost, 0, &overflow, nullptr, nullptr, err, sizeof(err))
			== GRID_EDIT_APPLIED);
	}
	SECTION("An ordered authenticated tail advances the replica before the grant") {
		grid_edit_room_resume_tail_t tail = {};
		tail.count = 7;
		auto wrong_offer = offer;
		wrong_offer.sha256[0] ^= 1;
		CHECK_FALSE(gridEditRoomHostGrantResume(host.get(), returning_actor, returning_link, &resume, &wrong_offer, &tail));
		CHECK(tail.count == 7);
		REQUIRE(gridEditRoomHostGrantResume(host.get(), returning_actor, returning_link, &resume, &offer, &tail));
		REQUIRE(tail.count == 2);
		CHECK(tail.deltas[0].revision == 2);
		CHECK(tail.deltas[1].revision == 3);
		CHECK(tail.deltas[0].author_id == kHost);
		CHECK(gridEditCommandMatches(&tail.deltas[1].command, &move));

		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &tail.deltas[0], err, sizeof(err)));
		REQUIRE(gridEditRoomGuestInstallResume(guest.get(), host_link, &offer, bytes.get(), offer.size, err, sizeof(err)));
		auto forged = tail.deltas[0];
		forged.author_id = returning_actor;
		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &forged, err, sizeof(err)));
		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, host_link, &tail.deltas[0], err, sizeof(err)));
		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link - 1, &tail.deltas[0], err, sizeof(err)));
		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &tail.deltas[1], err, sizeof(err)));
		CHECK(stateOf(guest).revision == offer.revision);
		REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &tail.deltas[0], err, sizeof(err)));
		CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &tail.deltas[0], err, sizeof(err)));
		REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &fresh, host_link, &tail.deltas[1], err, sizeof(err)));
		CHECK_FALSE(stateOf(guest).ready);
		CHECK_FALSE(stateOf(guest).can_edit);
		CHECK(stateOf(guest).revision == 3);
		grid_edit_command_t reply = {};
		CHECK_FALSE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 185, &reply));
		REQUIRE(gridEditRoomGuestGrant(guest.get(), &resume, host_link, returning_actor));
		CHECK(stateOf(guest).can_edit);
		CHECK(stateOf(guest).brightness == 169);
		grid_edit_move_spawn_t host_pose = {}, guest_pose = {};
		REQUIRE(gridEditRoomGetSpawnPose(host.get(), 0, &host_pose));
		REQUIRE(gridEditRoomGetSpawnPose(guest.get(), 0, &guest_pose));
		CHECK(guest_pose.position[0] == host_pose.position[0]);
		CHECK(guest_pose.position[0] == move.data.move_spawn.position[0]);
		REQUIRE(gridEditRoomGuestBuildAmbient(guest.get(), 1, 185, &reply));
		CHECK(reply.expected_revision == 3);
		CHECK(reply.sequence == 1);
	}
}

TEST_CASE("Shared Grid commits a native spawn pose and its inverse across snapshot resume", "[grid-edit-room]")
{
	using Doc = std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>;
	using SnapshotBytes = std::unique_ptr<void, decltype(&std::free)>;
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	const auto guest = fixture.openGuest(invite);
	grid_edit_move_spawn_t initial = {};
	CHECK(gridEditRoomSpawnCount(guest.get()) == 0);
	CHECK_FALSE(gridEditRoomGetSpawnPose(guest.get(), 0, &initial));
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));
	REQUIRE(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink, kGuest));
	REQUIRE(gridEditRoomSpawnCount(guest.get()) > 0);
	CHECK(gridEditRoomSpawnCount(guest.get()) == gridEditRoomSpawnCount(host.get()));
	REQUIRE(gridEditRoomGetSpawnPose(guest.get(), 0, &initial));
	const Doc baseline(gridEditRoomHostCloneDocument(host.get(), nullptr), forgeLevelDocFree);
	REQUIRE(baseline != nullptr);
	const auto original_pads = forgeLevelDocPadCount(baseline.get());

	auto move = initial;
	move.position[0] += 20.0f;
	move.look[0] = 0.0f;
	move.look[1] = 0.0f;
	move.look[2] = -1.0f;
	grid_edit_command_t request = {};
	auto invalid = move;
	invalid.position[0] = std::numeric_limits<f32>::infinity();
	CHECK_FALSE(gridEditRoomGuestBuildMoveSpawn(guest.get(), &invalid, &request));
	std::strcpy(invalid.spawn_id, "spawn_9999");
	invalid.position[0] = move.position[0];
	CHECK_FALSE(gridEditRoomGuestBuildMoveSpawn(guest.get(), &invalid, &request));
	REQUIRE(gridEditRoomGuestBuildMoveSpawn(guest.get(), &move, &request));
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink + 1, &request,
		nullptr, nullptr, nullptr, 0) == GRID_EDIT_DENIED);
	grid_edit_delta_t delta = {};
	const auto lighting = ambient(1, 1, 1, 168);
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &lighting,
		nullptr, &delta, nullptr, 0) == GRID_EDIT_APPLIED);
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &delta, nullptr, 0));
	CHECK(stateOf(host).can_undo_ambient);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request,
		nullptr, nullptr, nullptr, 0) == GRID_EDIT_STALE);
	REQUIRE(gridEditRoomGuestReceiveReceipt(guest.get(), &kIdentity, kHostLink,
		request.sequence, request.expected_revision, GRID_EDIT_STALE));
	REQUIRE(gridEditRoomGuestBuildMoveSpawn(guest.get(), &move, &request));
	REQUIRE(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &request,
		nullptr, &delta, nullptr, 0) == GRID_EDIT_APPLIED);
	CHECK(delta.revision == 3);
	auto mismatched = delta;
	mismatched.command = ambient(request.sequence, request.expected_revision, 1, 169);
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &mismatched, nullptr, 0));
	mismatched = delta;
	mismatched.command.data.move_spawn.position[0] += 1.0f;
	CHECK_FALSE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &mismatched, nullptr, 0));
	CHECK(stateOf(guest).pending_command);
	REQUIRE(gridEditRoomGuestApplyDelta(guest.get(), &kIdentity, kHostLink, &delta, nullptr, 0));
	CHECK_FALSE(stateOf(host).can_undo_ambient);
	CHECK_FALSE(stateOf(host).can_undo_spawn);
	const auto guest_commit = stateOf(guest);
	CHECK(guest_commit.can_undo_spawn);
	CHECK(guest_commit.previous_spawn.position[0] == initial.position[0]);
	CHECK(guest_commit.brightness == 168);

	REQUIRE(gridEditRoomHostDetachPeer(host.get(), kGuest, kGuestLink));
	CHECK(gridEditRoomSpawnCount(host.get()) == 0);
	auto fresh_identity = kIdentity;
	fresh_identity.generation++;
	constexpr u64 fresh_actor = kGuest + 1;
	grid_edit_snapshot_offer_t offer = {};
	const SnapshotBytes snapshot(gridEditRoomHostPrepareResume(host.get(), &fresh_identity, fresh_actor,
		kGuestLink, &invite, &offer, nullptr, 0), std::free);
	REQUIRE(snapshot != nullptr);
	REQUIRE(gridEditRoomHostConfirmResume(host.get(), fresh_actor, kGuestLink, &invite));
	const auto resumed = fixture.openGuest(invite);
	REQUIRE(gridEditRoomGuestRequireResume(resumed.get(), &fresh_identity, kHostLink));
	REQUIRE(gridEditRoomGuestInstallResume(resumed.get(), kHostLink, &offer, snapshot.get(), offer.size, nullptr, 0));
	CHECK(gridEditRoomSpawnCount(resumed.get()) == 0);
	grid_edit_room_resume_tail_t tail = {};
	REQUIRE(gridEditRoomHostGrantResume(host.get(), fresh_actor, kGuestLink, &invite, &offer, &tail));
	CHECK(tail.count == 0);
	REQUIRE(gridEditRoomGuestGrant(resumed.get(), &invite, kHostLink, fresh_actor));
	grid_edit_move_spawn_t restored = {};
	REQUIRE(gridEditRoomGetSpawnPose(resumed.get(), 0, &restored));
	CHECK(restored.position[0] == move.position[0]);
	CHECK(restored.look[2] == -1.0f);

	move.position[0] += 35.0f;
	REQUIRE(gridEditRoomGuestBuildMoveSpawn(resumed.get(), &move, &request));
	REQUIRE(gridEditRoomHostApply(host.get(), &fresh_identity, fresh_actor, kGuestLink, &request,
		nullptr, &delta, nullptr, 0) == GRID_EDIT_APPLIED);
	REQUIRE(gridEditRoomGuestApplyDelta(resumed.get(), &fresh_identity, kHostLink, &delta, nullptr, 0));
	const auto edited = stateOf(resumed);
	REQUIRE(edited.can_undo_spawn);
	CHECK(edited.previous_spawn.position[0] == restored.position[0]);
	REQUIRE(gridEditRoomGuestBuildMoveSpawn(resumed.get(), &edited.previous_spawn, &request));
	REQUIRE(gridEditRoomHostApply(host.get(), &fresh_identity, fresh_actor, kGuestLink, &request,
		nullptr, &delta, nullptr, 0) == GRID_EDIT_APPLIED);
	REQUIRE(gridEditRoomGuestApplyDelta(resumed.get(), &fresh_identity, kHostLink, &delta, nullptr, 0));
	CHECK(delta.revision == 5);
	CHECK(delta.command.sequence == 2);
	u64 revision = 0;
	const Doc committed(gridEditRoomHostCloneDocument(host.get(), &revision), forgeLevelDocFree);
	REQUIRE(committed != nullptr);
	CHECK(revision == 5);
	/* The first post-snapshot move allocates a fresh pad on both peers. Its
	 * inverse moves that pad in place, preserving the pre-snapshot pad. */
	CHECK(forgeLevelDocPadCount(committed.get()) == original_pads + 2);
	grid_edit_move_spawn_t actual = {};
	for (const auto &room : {host.get(), resumed.get()}) {
		REQUIRE(gridEditRoomGetSpawnPose(room, 0, &actual));
		CHECK(std::strcmp(actual.spawn_id, restored.spawn_id) == 0);
		CHECK(std::equal(std::begin(actual.position), std::end(actual.position), std::begin(restored.position)));
		CHECK(std::equal(std::begin(actual.look), std::end(actual.look), std::begin(restored.look)));
	}
	CHECK(stateOf(resumed).brightness == 168);
	CHECK(sha256VerifyFile(fixture.path.c_str(), fixture.digest) == 1);
}

TEST_CASE("Shared Grid focus leases bind to authenticated creators and expire on the host clock", "[grid-edit-room]")
{
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	grid_edit_resource_t spawn = {};
	spawn.kind = GRID_EDIT_RESOURCE_SPAWN;
	grid_edit_resource_t light = {};
	light.kind = GRID_EDIT_RESOURCE_AMBIENT_LIGHT;

	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	CHECK(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink, &light, 100) == GRID_EDIT_LEASE_DENIED);
	REQUIRE(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));
	grid_edit_move_spawn_t pose = {};
	REQUIRE(gridEditRoomGetSpawnPose(host.get(), 0, &pose));
	std::strcpy(spawn.spawn_id, pose.spawn_id);
	pose.position[0] += 20.0f;

	CHECK(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink + 1, &spawn, 1000) == GRID_EDIT_LEASE_DENIED);
	CHECK(gridEditRoomHostFocusResource(host.get(), kHost, kGuestLink, &spawn, 1000) == GRID_EDIT_LEASE_DENIED);
	REQUIRE(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink, &spawn, 100) == GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditRoomHostFocusResource(host.get(), kHost, 0, &spawn, 100) == GRID_EDIT_LEASE_HELD);

	grid_edit_command_t move = {};
	move.sequence = 1;
	move.expected_revision = 1;
	move.operation = GRID_EDIT_MOVE_SPAWN;
	move.data.move_spawn = pose;
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &move, nullptr, nullptr, nullptr, 0)
		== GRID_EDIT_RESOURCE_LEASED);
	REQUIRE(gridEditRoomHostFocusResource(host.get(), kHost, 0, &light, 101) == GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink, &light, 102) == GRID_EDIT_LEASE_HELD);
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kHost, 0, &move, nullptr, nullptr, nullptr, 0)
		== GRID_EDIT_APPLIED);

	constexpr u64 renewed_at = 200;
	REQUIRE(gridEditRoomHostFocusResource(host.get(), kHost, 0, &light, renewed_at) == GRID_EDIT_LEASE_GRANTED);
	CHECK_FALSE(gridEditRoomHostAdvanceClock(host.get(), renewed_at - 1));
	const auto light_edit = ambient(1, 2, 1, 125);
	REQUIRE(gridEditRoomHostAdvanceClock(host.get(), renewed_at + GRID_EDIT_LEASE_MS - 1));
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &light_edit, nullptr, nullptr, nullptr, 0)
		== GRID_EDIT_RESOURCE_LEASED);
	REQUIRE(gridEditRoomHostAdvanceClock(host.get(), renewed_at + GRID_EDIT_LEASE_MS));
	CHECK(gridEditRoomHostApply(host.get(), &kIdentity, kGuest, kGuestLink, &light_edit, nullptr, nullptr, nullptr, 0)
		== GRID_EDIT_APPLIED);
	CHECK(stateOf(host).revision == 3);
	REQUIRE(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink, &spawn, renewed_at + GRID_EDIT_LEASE_MS)
		== GRID_EDIT_LEASE_GRANTED);
	REQUIRE(gridEditRoomHostFocusResource(host.get(), kGuest, kGuestLink, nullptr, renewed_at + GRID_EDIT_LEASE_MS)
		== GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditRoomHostFocusResource(host.get(), kHost, 0, &spawn, renewed_at + GRID_EDIT_LEASE_MS)
		== GRID_EDIT_LEASE_GRANTED);

	grid_edit_resource_t decoration = {};
	decoration.kind = GRID_EDIT_RESOURCE_DECORATION;
	std::strcpy(decoration.record_id, "setup_0017");
	std::array<u8, GRID_EDIT_FOCUS_WIRE_MAX + 2> bytes;
	bytes.fill(0xA5);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 1, 1, 0};
	REQUIRE(gridEditFocusResourceWrite(&wire, &kIdentity, &decoration));
	CHECK(wire.wp == 1 + GRID_EDIT_FOCUS_WIRE_MAX);
	CHECK(bytes.front() == 0xA5);
	CHECK(bytes.back() == 0xA5);
	grid_edit_wire_identity_t received = {1, 99};
	grid_edit_resource_t resource = light;
	s32 has_resource = 77;
	--wire.wp;
	CHECK_FALSE(gridEditFocusResourceRead(&wire, &received, &resource, &has_resource));
	CHECK(wire.rp == 1);
	CHECK(received.generation == 99);
	CHECK(resource.kind == GRID_EDIT_RESOURCE_AMBIENT_LIGHT);
	CHECK(has_resource == 77);
	++wire.wp;
	bytes[wire.wp - 1] = 'x';
	CHECK_FALSE(gridEditFocusResourceRead(&wire, &received, &resource, &has_resource));
	CHECK(wire.rp == 1);
	CHECK(received.generation == 99);
	CHECK(resource.kind == GRID_EDIT_RESOURCE_AMBIENT_LIGHT);
	CHECK(has_resource == 77);
	bytes[wire.wp - 1] = '7';
	REQUIRE(gridEditFocusResourceRead(&wire, &received, &resource, &has_resource));
	CHECK(wire.rp == wire.wp);
	CHECK(received.room_id == kIdentity.room_id);
	CHECK(received.generation == kIdentity.generation);
	CHECK(has_resource == 1);
	CHECK(resource.kind == GRID_EDIT_RESOURCE_DECORATION);
	CHECK(std::strcmp(resource.record_id, decoration.record_id) == 0);
	wire.rp = wire.wp = 0;
	REQUIRE(gridEditFocusResourceWrite(&wire, &kIdentity, nullptr));
	CHECK(wire.wp == 10);
	REQUIRE(gridEditFocusResourceRead(&wire, &received, &resource, &has_resource));
	CHECK(has_resource == 0);

	using Doc = std::unique_ptr<forge_level_doc_t, decltype(&forgeLevelDocFree)>;
	using Session = std::unique_ptr<grid_edit_session_t, decltype(&gridEditSessionFree)>;
	const Doc document(gridEditRoomHostCloneDocument(host.get(), nullptr), forgeLevelDocFree);
	REQUIRE(document != nullptr);
	const f32 position[3] = {40.0f, 72.0f, 48.0f};
	const f32 facing[3] = {1.0f, 0.0f, 0.0f};
	REQUIRE(forgeLevelDocAddDecoration(document.get(), "example:tri_mesh", position, facing,
		decoration.record_id, sizeof(decoration.record_id), nullptr, 0));
	const Session session(gridEditSessionCreate(kHost, document.get()), gridEditSessionFree);
	REQUIRE(session != nullptr);
	REQUIRE(gridEditSessionSetMember(session.get(), kHost, kGuest, GRID_EDIT_ROLE_CREATOR));
	REQUIRE(gridEditSessionAdvanceClock(session.get(), kHost, 100));
	REQUIRE(gridEditSessionFocusResource(session.get(), kHost, &decoration) == GRID_EDIT_LEASE_GRANTED);
	grid_edit_command_t move_decoration = {};
	move_decoration.sequence = move_decoration.expected_revision = 1;
	move_decoration.operation = GRID_EDIT_MOVE_DECORATION;
	REQUIRE(gridEditSessionFindDecorationPose(session.get(), kGuest, decoration.record_id,
		&move_decoration.data.move_decoration));
	move_decoration.data.move_decoration.x += 20.0f;
	CHECK(gridEditSessionApply(session.get(), kGuest, &move_decoration, nullptr, nullptr, 0)
		== GRID_EDIT_RESOURCE_LEASED);
	REQUIRE(gridEditSessionFocusResource(session.get(), kHost, nullptr) == GRID_EDIT_LEASE_GRANTED);
	CHECK(gridEditSessionApply(session.get(), kGuest, &move_decoration, nullptr, nullptr, 0) == GRID_EDIT_APPLIED);
}

TEST_CASE("Shared Grid grant wire rejects malformed or truncated creator privilege", "[grid-edit-room]")
{
	grid_edit_room_control_t grant = {};
	grant.kind = GRID_EDIT_ROOM_CONTROL_GRANT;
	grant.invite.identity = kIdentity;
	grant.invite.host_actor = kHost;
	grant.invite.baseline_revision = 1;
	grant.granted_actor = kGuest;
	std::strcpy(grant.invite.arena_id, kArenaId);
	std::strcpy(grant.invite.arena_sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

	constexpr u32 prefix = 3;
	std::array<u8, prefix + GRID_EDIT_ROOM_CONTROL_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto empty = bytes;
	netbuf write = {bytes.data(), static_cast<u32>(bytes.size()), prefix, prefix, 0};
	auto invalid = grant;
	invalid.granted_actor = kHost;
	CHECK_FALSE(gridEditRoomControlWrite(&write, &invalid));
	CHECK(write.wp == prefix);
	CHECK(write.error == 0);
	CHECK(bytes == empty);
	REQUIRE(gridEditRoomControlWrite(&write, &grant));
	const u32 length = write.wp - prefix;
	CHECK(bytes[prefix - 1] == 0xa5);
	CHECK(bytes[write.wp] == 0xa5);

	for (u32 i = 0; i < length; i++) {
		CAPTURE(i);
		netbuf truncated = write;
		truncated.wp = prefix + i;
		checkRejectedControl(truncated);
	}
	netbuf trailing = write;
	trailing.wp++;
	checkRejectedControl(trailing);

	grid_edit_room_control_t decoded = {};
	netbuf verified = write;
	REQUIRE(gridEditRoomControlRead(&verified, &decoded));
	CHECK(verified.rp == write.wp);
	CHECK(decoded.kind == GRID_EDIT_ROOM_CONTROL_GRANT);
	CHECK(decoded.granted_actor == kGuest);
	CHECK(decoded.invite.host_actor == kHost);
	CHECK(decoded.invite.identity.generation == kIdentity.generation);
	CHECK(decoded.invite.baseline_revision == 1);
	CHECK(std::strcmp(decoded.invite.arena_id, kArenaId) == 0);
	CHECK(std::strcmp(decoded.invite.arena_sha256, grant.invite.arena_sha256) == 0);

	constexpr u32 id_length_offset = 10;
	constexpr u32 id_offset = id_length_offset + 1;
	const u32 sha_offset = id_offset + bytes[prefix + id_length_offset];
	SECTION("invitation and confirmation carry no granted actor") {
		auto invite = grant;
		for (auto kind : {GRID_EDIT_ROOM_CONTROL_INVITE, GRID_EDIT_ROOM_CONTROL_CONFIRM}) {
			invite.kind = kind;
			auto packet = write;
			packet.wp = prefix;
			REQUIRE(gridEditRoomControlWrite(&packet, &invite));
			CHECK(packet.wp + 8 == write.wp);
			REQUIRE(gridEditRoomControlRead(&packet, &decoded));
			CHECK(decoded.kind == kind);
			CHECK(decoded.granted_actor == 0);
			CHECK(decoded.invite.host_actor == kHost);
		}
		/* Treating the shorter confirmation as a grant cannot mint an actor. */
		bytes[prefix] = GRID_EDIT_ROOM_CONTROL_GRANT;
		auto missing_actor = write;
		missing_actor.wp -= 8;
		checkRejectedControl(missing_actor);
		return;
	}
	SECTION("unsupported room generation") {
		std::fill_n(bytes.begin() + prefix + 2, 8, 0);
	}
	SECTION("embedded terminator in arena ID") {
		bytes[prefix + id_offset + 2] = 0;
	}
	SECTION("all-zero checksum") {
		std::fill_n(bytes.begin() + prefix + sha_offset, SHA256_HEX_SIZE - 1, '0');
	}
	SECTION("noncanonical checksum") {
		bytes[prefix + sha_offset + 1] = 'A';
	}
	SECTION("forged baseline") {
		bytes[prefix + sha_offset + SHA256_HEX_SIZE - 1] = 2;
	}
	SECTION("empty granted actor") {
		std::fill_n(bytes.begin() + write.wp - 8, 8, 0);
	}
	SECTION("granted actor is the host") {
		std::copy_n(bytes.begin() + write.wp - 16, 8, bytes.begin() + write.wp - 8);
	}
	checkRejectedControl(write);
}

TEST_CASE("Shared Grid refusal wire carries an exact rejection without granting access", "[grid-edit-room]")
{
	grid_edit_room_control_t refusal = {};
	refusal.kind = GRID_EDIT_ROOM_CONTROL_REFUSAL;
	refusal.invite.identity = kIdentity;
	refusal.requested_sequence = 21;
	refusal.expected_revision = 51;
	refusal.result = GRID_EDIT_STALE;
	std::array<u8, GRID_EDIT_ROOM_CONTROL_WIRE_MAX> bytes;
	bytes.fill(0xa5);
	const auto empty = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};

	auto invalid = refusal;
	invalid.result = GRID_EDIT_APPLIED;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	invalid = refusal;
	invalid.requested_sequence = 0;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	CHECK(wire.wp == 0);
	CHECK(bytes == empty);
	REQUIRE(gridEditRoomControlWrite(&wire, &refusal));
	REQUIRE(wire.wp == 27);
	CHECK(bytes[26] == 3);

	for (u32 length = 0; length < wire.wp; length++) {
		CAPTURE(length);
		netbuf truncated = wire;
		truncated.wp = length;
		checkRejectedControl(truncated);
	}

	grid_edit_room_control_t decoded = {};
	netbuf verified = wire;
	REQUIRE(gridEditRoomControlRead(&verified, &decoded));
	CHECK(decoded.kind == GRID_EDIT_ROOM_CONTROL_REFUSAL);
	CHECK(decoded.invite.identity.room_id == kIdentity.room_id);
	CHECK(decoded.invite.identity.generation == kIdentity.generation);
	CHECK(decoded.invite.arena_id[0] == '\0');
	CHECK(decoded.invite.host_actor == 0);
	CHECK(decoded.granted_actor == 0);
	CHECK(decoded.requested_sequence == 21);
	CHECK(decoded.expected_revision == 51);
	CHECK(decoded.result == GRID_EDIT_STALE);

	bytes[26] = 0xff;
	checkRejectedControl(wire);
}

TEST_CASE("Shared Grid review controls round trip within the existing v65 limit", "[grid-edit-room]")
{
	auto control = variantReview();
	std::fill_n(control.review.arena_id + 4, CATALOG_ID_LEN - 5, 'a');
	std::array<u8, GRID_EDIT_ROOM_CONTROL_WIRE_MAX> bytes = {};
	grid_edit_room_control_t decoded = {};

	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED;
	netbuf supported = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
	REQUIRE(gridEditRoomControlWrite(&supported, &control));
	REQUIRE(supported.wp == 10);
	REQUIRE(gridEditRoomControlRead(&supported, &decoded));
	CHECK(decoded.kind == GRID_EDIT_ROOM_CONTROL_REVIEW_SUPPORTED);
	CHECK(decoded.invite.identity.room_id == kIdentity.room_id);
	CHECK(decoded.invite.identity.generation == kIdentity.generation);
	CHECK(decoded.review.offer_serial == 0);

	const struct {
		grid_edit_room_control_kind_e kind;
		grid_edit_variant_review_reply_e reply;
	} cases[] = {
		{GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER, {}},
		{GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY, GRID_EDIT_VARIANT_REVIEW_DECLINED},
		{GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY, GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED},
		{GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY, GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED},
		{GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL, {}},
	};
	for (const auto &message : cases) {
		control.kind = message.kind;
		control.review_reply = message.reply;
		const bool is_reply = message.kind == GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY;
		netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
		REQUIRE(gridEditRoomControlWrite(&wire, &control));
		CHECK(wire.wp == GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX - (is_reply ? 0 : 1));
		CHECK(bytes[26] == 0x98);
		CHECK(bytes[31] == 0x2c);
		CHECK(bytes[32] == 0x01);
		REQUIRE(gridEditRoomControlRead(&wire, &decoded));
		CHECK(wire.rp == wire.wp);
		CHECK(decoded.kind == message.kind);
		CHECK(decoded.invite.identity.room_id == kIdentity.room_id);
		CHECK(decoded.invite.identity.generation == kIdentity.generation);
		CHECK(decoded.invite.arena_id[0] == '\0');
		CHECK(decoded.granted_actor == 0);
		CHECK(decoded.review_reply == message.reply);
		REQUIRE(gridEditVariantReviewMatches(&decoded.review, &control.review));
	}
	auto mismatch = decoded.review;
	mismatch.stage_epoch++;
	CHECK_FALSE(gridEditVariantReviewMatches(&mismatch, &control.review));
	mismatch = decoded.review;
	mismatch.draft_revision++;
	CHECK_FALSE(gridEditVariantReviewMatches(&mismatch, &control.review));
	mismatch = decoded.review;
	mismatch.offer_serial++;
	CHECK_FALSE(gridEditVariantReviewMatches(&mismatch, &control.review));
	mismatch = decoded.review;
	mismatch.arena_sha256[31]++;
	CHECK_FALSE(gridEditVariantReviewMatches(&mismatch, &control.review));
	mismatch = decoded.review;
	mismatch.parent_sha256[31]++;
	CHECK_FALSE(gridEditVariantReviewMatches(&mismatch, &control.review));
}

TEST_CASE("Shared Grid review wire rejects malformed exact tuples transactionally", "[grid-edit-room]")
{
	auto control = variantReview();
	std::array<u8, GRID_EDIT_ROOM_CONTROL_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto empty = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};

	for (auto kind : {GRID_EDIT_ROOM_CONTROL_REVIEW_OFFER, GRID_EDIT_ROOM_CONTROL_REVIEW_CANCEL}) {
		control.kind = kind;
		control.review_reply = GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	}
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY;
	control.review_reply = {};
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	CHECK(wire.wp == 0);
	CHECK(wire.error == 0);
	CHECK(bytes == empty);
	control.review_reply = GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
	REQUIRE(gridEditRoomControlWrite(&wire, &control));

	for (u32 size = 0; size < wire.wp; size++) {
		CAPTURE(size);
		auto truncated = wire;
		truncated.wp = size;
		checkRejectedControl(truncated);
	}
	auto trailing = wire;
	trailing.wp++;
	checkRejectedControl(trailing);

	constexpr u32 id_length = 33;
	const u32 digest = id_length + 1 + bytes[id_length];
	SECTION("zero offer serial") { std::fill_n(bytes.begin() + 10, 8, 0); }
	SECTION("zero draft revision") { std::fill_n(bytes.begin() + 18, 8, 0); }
	SECTION("zero stage epoch") { std::fill_n(bytes.begin() + 26, 4, 0); }
	SECTION("room outside the builder domain") { bytes[30] = 3; }
	SECTION("zero extension") { bytes[31] = bytes[32] = 0; }
	SECTION("noncanonical arena ID") { bytes[id_length + 1] = 'M'; }
	SECTION("embedded terminator") { bytes[id_length + 2] = 0; }
	SECTION("empty staged digest") { std::fill_n(bytes.begin() + digest, SHA256_DIGEST_SIZE, 0); }
	SECTION("empty parent digest") { std::fill_n(bytes.begin() + digest + SHA256_DIGEST_SIZE, SHA256_DIGEST_SIZE, 0); }
	SECTION("unknown reply") { bytes[wire.wp - 1] = 4; }
	checkRejectedControl(wire);
}

TEST_CASE("Shared Grid opens an authorized native match baseline from borrowed bytes", "[grid-edit-room]")
{
	const ArenaFixture fixture;
	const auto host = fixture.openHost();
	grid_edit_room_invite_t invite = {};
	REQUIRE(gridEditRoomHostInvite(host.get(), kGuest, kGuestLink, &invite));
	std::ifstream input(fixture.path, std::ios::binary);
	REQUIRE(input.is_open());
	std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	REQUIRE_FALSE(bytes.empty());

	char error[256] = {};
	const Room guest(gridEditRoomGuestOpenBytes(&invite, kHostLink, bytes.data(),
		static_cast<u32>(bytes.size()), error, sizeof(error)), gridEditRoomFree);
	INFO(error);
	REQUIRE(guest);
	CHECK(stateOf(guest).revision == invite.baseline_revision);
	CHECK_FALSE(stateOf(guest).ready);

	bytes[0] ^= 1;
	const Room changed(gridEditRoomGuestOpenBytes(&invite, kHostLink, bytes.data(),
		static_cast<u32>(bytes.size()), error, sizeof(error)), gridEditRoomFree);
	CHECK_FALSE(changed);
	CHECK(gridEditRoomHostGrant(host.get(), kGuest, kGuestLink, &invite));
	CHECK(gridEditRoomGuestGrant(guest.get(), &invite, kHostLink, kGuest));
	CHECK(stateOf(guest).ready);
}


TEST_CASE("Shared Grid v65 dependency capability is a bounded opt-in", "[grid-edit-room][review-deps]")
{
	grid_edit_room_control_t control = {};
	control.invite.identity = kIdentity;
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_CAPABILITIES;
	std::array<u8, 12> bytes = {};
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	control.review_capabilities = 0x80;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	control.review_capabilities = GRID_EDIT_REVIEW_CAP_DEPS_V1 | GRID_EDIT_REVIEW_CAP_PREPARED;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	CHECK(wire.wp == 0);
	control.review_capabilities = GRID_EDIT_REVIEW_CAP_DEPS_V1;
	REQUIRE(gridEditRoomControlWrite(&wire, &control));
	REQUIRE(wire.wp == 11);
	CHECK(bytes[10] == GRID_EDIT_REVIEW_CAP_DEPS_V1);
	grid_edit_room_control_t decoded = {};
	auto verified = wire;
	REQUIRE(gridEditRoomControlRead(&verified, &decoded));
	CHECK(decoded.kind == control.kind);
	CHECK(decoded.invite.identity.generation == kIdentity.generation);
	CHECK(decoded.review_capabilities == control.review_capabilities);
	auto truncated = wire;
	truncated.wp--;
	checkRejectedControl(truncated);
	auto trailing = wire;
	trailing.wp++;
	checkRejectedControl(trailing);
	bytes[10] = 0x80;
	checkRejectedControl(wire);
	bytes[10] = GRID_EDIT_REVIEW_CAP_DEPS_V1 | GRID_EDIT_REVIEW_CAP_PREPARED;
	checkRejectedControl(wire);

	for (const auto capability : {GRID_EDIT_REVIEW_CAP_PREPARED, GRID_EDIT_REVIEW_CAP_TRANSFER,
			GRID_EDIT_REVIEW_CAP_APPLY, GRID_EDIT_REVIEW_CAP_APPLY_BASELINE}) {
		wire.rp = wire.wp = 0;
		control.review_capabilities = capability;
		REQUIRE(gridEditRoomControlWrite(&wire, &control));
		REQUIRE(wire.wp == 11);
		CHECK(bytes[10] == capability);
		REQUIRE(gridEditRoomControlRead(&wire, &decoded));
		CHECK(decoded.review_capabilities == capability);
	}
}

TEST_CASE("Shared Grid dependency review proof is distinct from the legacy arena receipt",
	"[grid-edit-room][review-deps]")
{
	auto control = variantReview();
	std::fill_n(control.review.arena_id + 4, CATALOG_ID_LEN - 5, 'a');
	std::fill_n(control.review_deps_sha256, SHA256_DIGEST_SIZE, 0xa7);
	std::array<u8, GRID_EDIT_ROOM_CONTROL_WIRE_MAX + 1> bytes = {};
	grid_edit_room_control_t decoded = {};
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};

	for (const auto kind : {GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER, GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER}) {
		control.kind = kind;
		wire.rp = wire.wp = 0;
		REQUIRE(gridEditRoomControlWrite(&wire, &control));
		REQUIRE(wire.wp == 193);
		auto valid = wire;
		REQUIRE(gridEditRoomControlRead(&valid, &decoded));
		CHECK(decoded.kind == kind);
		CHECK(decoded.review_reply == 0);
		CHECK(gridEditVariantReviewMatches(&control.review, &decoded.review));
		CHECK(std::memcmp(decoded.review_deps_sha256, control.review_deps_sha256, SHA256_DIGEST_SIZE) == 0);
		auto truncated = wire;
		truncated.wp--;
		checkRejectedControl(truncated);
		auto trailing = wire;
		trailing.wp++;
		checkRejectedControl(trailing);
	}

	wire.rp = wire.wp = 0;
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_REPLY;
	control.review_reply = GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED;
	const auto known = bytes;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	CHECK(bytes == known);
	CHECK(wire.wp == 0);

	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_REPLY;
	REQUIRE(gridEditRoomControlWrite(&wire, &control));
	REQUIRE(wire.wp == 194);
	CHECK(wire.wp == GRID_EDIT_ROOM_CONTROL_DEPENDENCY_WIRE_MAX);
	auto positive = wire;
	REQUIRE(gridEditRoomControlRead(&positive, &decoded));
	CHECK(decoded.kind == control.kind);
	CHECK(decoded.review_reply == GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED);
	CHECK(gridEditVariantReviewMatches(&control.review, &decoded.review));
	CHECK(std::memcmp(decoded.review_deps_sha256, control.review_deps_sha256, SHA256_DIGEST_SIZE) == 0);

	for (u32 i = 0; i < wire.wp; i++) {
		CAPTURE(i);
		auto truncated = wire;
		truncated.wp = i;
		checkRejectedControl(truncated);
	}
	auto trailing = wire;
	trailing.wp++;
	checkRejectedControl(trailing);
	std::fill_n(bytes.data() + wire.wp - SHA256_DIGEST_SIZE, SHA256_DIGEST_SIZE, 0);
	checkRejectedControl(wire);

	wire.wp = 0;
	control.review_reply = GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	std::fill_n(control.review_deps_sha256, SHA256_DIGEST_SIZE, 0);
	REQUIRE(gridEditRoomControlWrite(&wire, &control));
	REQUIRE(wire.wp == GRID_EDIT_ROOM_CONTROL_LEGACY_WIRE_MAX);
	REQUIRE(gridEditRoomControlRead(&wire, &decoded));
	CHECK(decoded.kind == control.kind);
	CHECK(decoded.review_reply == GRID_EDIT_VARIANT_REVIEW_SOURCE_VERIFIED);
	CHECK(std::memcmp(decoded.review_deps_sha256, control.review_deps_sha256, SHA256_DIGEST_SIZE) == 0);

	wire.rp = wire.wp = 0;
	control.review_reply = GRID_EDIT_VARIANT_REVIEW_DEPS_VERIFIED;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_DEPS_OFFER;
	control.review_reply = {};
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	CHECK(wire.wp == 0);
}

TEST_CASE("Shared Grid transfer offer and request carry the exact bounded arena size",
	"[grid-edit-room][review-transfer]")
{
	constexpr u32 arenaCap = 50u * 1024u * 1024u;
	auto control = variantReview();
	std::fill_n(control.review.arena_id + 4, CATALOG_ID_LEN - 5, 'a');
	std::fill_n(control.review_deps_sha256, SHA256_DIGEST_SIZE, 0xa7);
	std::array<u8, GRID_EDIT_ROOM_CONTROL_WIRE_MAX + 1> bytes = {};
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
	for (auto kind : {GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_OFFER, GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST}) {
		CAPTURE(kind);
		wire.rp = wire.wp = 0;
		control.kind = kind;
		const auto unchanged = bytes;
		control.review_arena_size = 0;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
		control.review_arena_size = arenaCap + 1;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
		control.review_arena_size = arenaCap;
		control.review_reply = GRID_EDIT_VARIANT_REVIEW_ACKNOWLEDGED;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
		CHECK(wire.wp == 0);
		CHECK(bytes == unchanged);
		control.review_reply = {};
		REQUIRE(gridEditRoomControlWrite(&wire, &control));
		REQUIRE(wire.wp == 197);
		CHECK(wire.wp == GRID_EDIT_ROOM_CONTROL_TRANSFER_WIRE_MAX);
		CHECK(bytes[wire.wp - 4] == 0);
		CHECK(bytes[wire.wp - 2] == 0x20);
		CHECK(bytes[wire.wp - 1] == 0x03);

		grid_edit_room_control_t decoded = {};
		auto valid = wire;
		REQUIRE(gridEditRoomControlRead(&valid, &decoded));
		CHECK(valid.rp == valid.wp);
		CHECK(decoded.kind == kind);
		CHECK(decoded.invite.identity.room_id == kIdentity.room_id);
		CHECK(decoded.invite.identity.generation == kIdentity.generation);
		CHECK(decoded.review_arena_size == arenaCap);
		CHECK(decoded.review_reply == 0);
		CHECK(gridEditVariantReviewMatches(&control.review, &decoded.review));
		CHECK(std::memcmp(decoded.review_deps_sha256, control.review_deps_sha256, SHA256_DIGEST_SIZE) == 0);

		auto truncated = wire;
		truncated.wp--;
		checkRejectedControl(truncated);
		auto trailing = wire;
		trailing.wp++;
		checkRejectedControl(trailing);
		bytes[wire.wp - 4] = 1;
		checkRejectedControl(wire);
		std::fill_n(bytes.data() + wire.wp - sizeof(u32), sizeof(u32), 0);
		checkRejectedControl(wire);
	}

	wire.rp = wire.wp = 0;
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER;
	control.review_arena_size = 1;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &control));
	control.kind = GRID_EDIT_ROOM_CONTROL_REVIEW_TRANSFER_REQUEST;
	REQUIRE(gridEditRoomControlWrite(&wire, &control));
	std::fill_n(bytes.data() + wire.wp - sizeof(u32) - SHA256_DIGEST_SIZE, SHA256_DIGEST_SIZE, 0);
	checkRejectedControl(wire);
}

TEST_CASE("Shared Grid Apply binds the exact reviewed layout without advancing the match epoch",
	"[grid-edit-room][grid-layout-apply]")
{
	constexpr u32 prefix = 3;
	constexpr u32 suffix = 2 * sizeof(u64) + SHA256_DIGEST_SIZE + 1;
	auto prepare = variantReview();
	prepare.kind = GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE;
	prepare.apply_layout_generation = 0x0102030405060708ull;
	std::fill_n(prepare.review.arena_id + 4, CATALOG_ID_LEN - 5, 'a');
	std::fill_n(prepare.review_deps_sha256, SHA256_DIGEST_SIZE, 0xa7);
	std::fill_n(prepare.apply_manifest_sha256, SHA256_DIGEST_SIZE, 0xbd);
	std::array<u8, prefix + GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX> bytes;
	const struct {
		grid_edit_room_control_kind_e kind;
		grid_edit_layout_apply_result_e result;
	} cases[] = {
		{GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE, {}},
		{GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE, {}},
		{GRID_EDIT_ROOM_CONTROL_APPLY_REPLY, GRID_EDIT_LAYOUT_APPLY_READY},
		{GRID_EDIT_ROOM_CONTROL_APPLY_REPLY, GRID_EDIT_LAYOUT_APPLY_REFUSED},
		{GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME, GRID_EDIT_LAYOUT_APPLY_COMMITTED},
		{GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME, GRID_EDIT_LAYOUT_APPLY_ABORTED},
		{GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME, GRID_EDIT_LAYOUT_APPLY_ROLLED_BACK},
	};
	for (const auto &message : cases) {
		CAPTURE(message.kind, message.result);
		auto control = prepare;
		control.kind = message.kind;
		control.apply_result = message.result;
		bytes.fill(0x5a);
		netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), prefix, prefix, 0};
		REQUIRE(gridEditRoomControlWrite(&wire, &control));
		REQUIRE(wire.wp - prefix == 242);
		CHECK(wire.wp - prefix == GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX);
		CHECK(bytes[prefix - 1] == 0x5a);
		CHECK(bytes[wire.wp - suffix] == 0);
		CHECK(bytes[wire.wp - suffix + sizeof(u64)] == 8);
		CHECK(bytes[wire.wp - suffix + 2 * sizeof(u64) - 1] == 1);
		CHECK(bytes[wire.wp - 1] == static_cast<u8>(message.result));
		grid_edit_room_control_t decoded = {};
		REQUIRE(gridEditRoomControlRead(&wire, &decoded));
		CHECK(wire.rp == wire.wp);
		CHECK(decoded.kind == message.kind);
		CHECK(decoded.apply_result == message.result);
		CHECK(decoded.review.stage_epoch == prepare.review.stage_epoch);
		CHECK(gridEditLayoutApplyMatches(&prepare, &decoded));
	}

	auto changed = prepare;
	changed.invite.identity.room_id++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.invite.identity.generation++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.review.offer_serial++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.review_deps_sha256[0]++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.apply_previous_layout_generation++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.apply_layout_generation++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));
	changed = prepare;
	changed.apply_manifest_sha256[31]++;
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &changed));

	/* A creator who rejoins receives the exact committed layout tuple
	 * with their freshly granted draft identity. */
	auto rebound = prepare;
	rebound.kind = GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE;
	rebound.invite.identity.generation++;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), prefix, prefix, 0};
	REQUIRE(gridEditRoomControlWrite(&wire, &rebound));
	grid_edit_room_control_t decoded = {};
	REQUIRE(gridEditRoomControlRead(&wire, &decoded));
	CHECK(gridEditLayoutApplyMatches(&rebound, &decoded));
	CHECK_FALSE(gridEditLayoutApplyMatches(&prepare, &decoded));
	CHECK(gridEditVariantReviewMatches(&prepare.review, &decoded.review));
	CHECK(decoded.review_reply == 0);
	CHECK(decoded.apply_result == 0);
}

TEST_CASE("Shared Grid Apply rejects stale generations and invalid dispositions transactionally",
	"[grid-edit-room][grid-layout-apply]")
{
	constexpr u32 suffix = 2 * sizeof(u64) + SHA256_DIGEST_SIZE + 1;
	auto control = variantReview();
	control.kind = GRID_EDIT_ROOM_CONTROL_APPLY_REPLY;
	control.apply_result = GRID_EDIT_LAYOUT_APPLY_READY;
	control.apply_previous_layout_generation = 22;
	control.apply_layout_generation = 24;
	control.review_deps_sha256[0] = 0xa7;
	control.apply_manifest_sha256[0] = 0xbd;
	std::array<u8, GRID_EDIT_ROOM_CONTROL_APPLY_WIRE_MAX + 1> bytes;
	bytes.fill(0x5a);
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), 0, 0, 0};
	const auto untouched = bytes;
	for (const u64 stale : {0u, 21u, 22u}) {
		auto invalid = control;
		invalid.apply_layout_generation = stale;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	}
	for (const auto invalid_kind : {GRID_EDIT_ROOM_CONTROL_APPLY_PREPARE,
			GRID_EDIT_ROOM_CONTROL_APPLY_OUTCOME, GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE,
			GRID_EDIT_ROOM_CONTROL_REVIEW_PREPARED_OFFER}) {
		auto invalid = control;
		invalid.kind = invalid_kind;
		CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	}
	auto invalid = control;
	invalid.apply_manifest_sha256[0] = 0;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	invalid = control;
	invalid.review_deps_sha256[0] = 0;
	CHECK_FALSE(gridEditRoomControlWrite(&wire, &invalid));
	CHECK(wire.wp == 0);
	CHECK(bytes == untouched);

	REQUIRE(gridEditRoomControlWrite(&wire, &control));
	const auto encoded = bytes;
	const u32 old_layout = wire.wp - suffix;
	const u32 new_layout = old_layout + sizeof(u64);
	auto truncated = wire;
	truncated.wp--;
	checkRejectedControl(truncated);
	auto trailing = wire;
	trailing.wp++;
	checkRejectedControl(trailing);
	bytes[new_layout] = bytes[old_layout];
	checkRejectedControl(wire);
	bytes = encoded;
	std::fill_n(bytes.data() + new_layout + sizeof(u64), SHA256_DIGEST_SIZE, 0);
	checkRejectedControl(wire);
	bytes = encoded;
	bytes[0] = GRID_EDIT_ROOM_CONTROL_APPLY_BASELINE;
	checkRejectedControl(wire); // A READY reply is never a baseline.
	bytes = encoded;
	for (const u8 invalid_result : {0, 3, 4, 5, 6, 255}) {
		bytes[wire.wp - 1] = invalid_result;
		checkRejectedControl(wire);
	}
}

TEST_CASE("Shared Grid review chunk binds draft generation and a single exact payload",
	"[grid-edit-room][review-transfer]")
{
	constexpr u32 prefix = 3;
	constexpr u32 dataSize = 16u * 1024u;
	std::array<u8, dataSize> body;
	body.fill(0x42);
	std::array<u8, prefix + GRID_EDIT_REVIEW_CHUNK_WIRE_MAX + 1> bytes;
	bytes.fill(0xa5);
	const auto unchanged = bytes;
	netbuf wire = {bytes.data(), static_cast<u32>(bytes.size()), prefix, prefix, 0};
	grid_match_chunk_t chunk = {};
	chunk.offer.room_id = kIdentity.room_id;
	chunk.offer.generation = 0x1122334455667788ull;
	std::fill_n(chunk.offer.manifest_digest, SHA256_HEX_SIZE - 1, 'b');
	chunk.offset = dataSize;
	chunk.size = dataSize;
	chunk.data = body.data();

	CHECK_FALSE(gridEditReviewChunkWrite(&wire, 0, &chunk));
	chunk.package_index = 1;
	CHECK_FALSE(gridEditReviewChunkWrite(&wire, kIdentity.generation, &chunk));
	chunk.package_index = 0;
	auto short_write = wire;
	short_write.size = prefix + GRID_EDIT_REVIEW_CHUNK_WIRE_MAX - 1;
	CHECK_FALSE(gridEditReviewChunkWrite(&short_write, kIdentity.generation, &chunk));
	CHECK(short_write.wp == prefix);
	CHECK(wire.wp == prefix);
	CHECK(bytes == unchanged);

	REQUIRE(gridEditReviewChunkWrite(&wire, kIdentity.generation, &chunk));
	REQUIRE(wire.wp == prefix + GRID_EDIT_REVIEW_CHUNK_WIRE_MAX);
	CHECK(bytes[prefix - 1] == 0xa5);
	CHECK(bytes[prefix] == 0xef);
	CHECK(bytes[prefix + 7] == 0x01);
	CHECK(bytes[wire.wp] == 0xa5);
	u64 generation = 0;
	grid_match_chunk_t decoded = {};
	auto valid = wire;
	REQUIRE(gridEditReviewChunkRead(&valid, &generation, &decoded));
	CHECK(valid.rp == valid.wp);
	CHECK(generation == kIdentity.generation);
	CHECK(decoded.offer.room_id == chunk.offer.room_id);
	CHECK(decoded.offer.generation == chunk.offer.generation);
	CHECK(std::strcmp(decoded.offer.manifest_digest, chunk.offer.manifest_digest) == 0);
	CHECK(decoded.package_index == 0);
	CHECK(decoded.offset == chunk.offset);
	REQUIRE(decoded.size == chunk.size);
	CHECK(decoded.data == bytes.data() + wire.wp - dataSize);
	CHECK(std::memcmp(decoded.data, body.data(), body.size()) == 0);

	auto reject = [&](netbuf invalid) {
		const auto before = invalid;
		u64 unchanged_generation = 123;
		grid_match_chunk_t unchanged_chunk = chunk;
		REQUIRE_FALSE(gridEditReviewChunkRead(&invalid, &unchanged_generation, &unchanged_chunk));
		CHECK(invalid.rp == before.rp);
		CHECK(invalid.wp == before.wp);
		CHECK(invalid.error == before.error);
		CHECK(unchanged_generation == 123);
		CHECK(unchanged_chunk.offer.generation == chunk.offer.generation);
		CHECK(unchanged_chunk.data == chunk.data);
	};
	auto truncated = wire;
	truncated.wp = prefix + 7;
	reject(truncated);
	truncated.wp = wire.wp - 1;
	reject(truncated);
	auto trailing = wire;
	trailing.wp++;
	reject(trailing);
	std::fill_n(bytes.data() + prefix, sizeof(u64), 0);
	reject(wire);

	wire.wp = prefix;
	REQUIRE(gridEditReviewChunkWrite(&wire, kIdentity.generation, &chunk));
	auto inner = wire;
	inner.rp = inner.wp = prefix + sizeof(u64);
	auto wrong_index = chunk;
	wrong_index.package_index = 1;
	REQUIRE(gridMatchChunkWrite(&inner, &wrong_index));
	reject(wire);

	wire.wp = prefix;
	const std::array<u8, 4> overlap = {7, 11, 13, 17};
	std::copy(overlap.begin(), overlap.end(), bytes.data() + prefix);
	chunk.data = bytes.data() + prefix;
	chunk.size = overlap.size();
	REQUIRE(gridEditReviewChunkWrite(&wire, kIdentity.generation, &chunk));
	REQUIRE(gridEditReviewChunkRead(&wire, &generation, &decoded));
	REQUIRE(decoded.size == overlap.size());
	CHECK(std::memcmp(decoded.data, overlap.data(), overlap.size()) == 0);
}
