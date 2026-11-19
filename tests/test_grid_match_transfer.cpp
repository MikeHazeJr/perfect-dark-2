#include "catch.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "net/grid_match_transfer.h"

// As in test_netbuf.cpp, mirror netbuf to avoid types.h's C bool macro.
struct netbuf {
	u8 *data;
	u32 size;
	u32 rp;
	u32 wp;
	u32 error;
};

namespace {

constexpr u32 kChunkSize = 16u * 1024u;
constexpr u32 kPackageLimit = 50u * 1024u * 1024u;
constexpr u16 kPackageIndex = 17;
const grid_match_offer_t kOffer = {
	3, 0x123456789abcdef0ull, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
};

struct Transfer {
	grid_match_transfer_t *handle = nullptr;
	~Transfer() { gridMatchTransferReset(&handle); }
};

}

TEST_CASE("Grid package chunk borrows a bounded payload after an unaligned message prefix",
	"[net][grid][distribution]")
{
	std::array<u8, kChunkSize> bytes{};
	for (size_t i = 0; i < bytes.size(); i++) bytes[i] = static_cast<u8>(i);
	std::array<u8, 2 + GRID_MATCH_CHUNK_WIRE_HEADER_SIZE + kChunkSize> packet{};
	packet.front() = 0xa5;
	packet.back() = 0x5a;
	const grid_match_chunk_t sent = {kOffer, kPackageIndex, kPackageLimit - kChunkSize,
		kChunkSize, bytes.data()};
	struct netbuf wire = {packet.data(), static_cast<u32>(packet.size()), 1, 1, 0};
	REQUIRE(gridMatchChunkWrite(&wire, &sent));
	REQUIRE(wire.wp == packet.size() - 1);
	REQUIRE(packet.front() == 0xa5);
	REQUIRE(packet.back() == 0x5a);
	wire.wp++;

	grid_match_chunk_t received{};
	REQUIRE(gridMatchChunkRead(&wire, &received));
	REQUIRE(wire.rp == packet.size() - 1);
	REQUIRE(received.offer.room_id == sent.offer.room_id);
	REQUIRE(received.offer.generation == sent.offer.generation);
	REQUIRE(std::strcmp(received.offer.manifest_digest, sent.offer.manifest_digest) == 0);
	REQUIRE(received.package_index == sent.package_index);
	REQUIRE(received.offset == sent.offset);
	REQUIRE(received.size == sent.size);
	REQUIRE(received.data == packet.data() + 1 + GRID_MATCH_CHUNK_WIRE_HEADER_SIZE);
	REQUIRE(std::memcmp(received.data, bytes.data(), bytes.size()) == 0);
}

TEST_CASE("Grid package chunk write and read refusals preserve the packet and caller output",
	"[net][grid][distribution]")
{
	constexpr std::array<u8, 4> bytes = {0x50, 0x4b, 0x03, 0x04};
	std::array<u8, GRID_MATCH_CHUNK_WIRE_HEADER_SIZE + bytes.size()> packet{};
	packet.fill(0xa5);
	const auto empty = packet;
	const grid_match_chunk_t sent = {kOffer, kPackageIndex, 0, bytes.size(), bytes.data()};
	struct netbuf wire = {packet.data(), static_cast<u32>(packet.size() - 1), 0, 0, 0};
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &sent));
	REQUIRE(wire.wp == 0);
	REQUIRE(wire.error == 0);
	REQUIRE(packet == empty);

	wire.size = packet.size();
	grid_match_chunk_t invalid = sent;
	invalid.size = 0;
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &invalid));
	invalid.size = kChunkSize + 1;
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &invalid));
	invalid = sent;
	invalid.offset = UINT32_MAX;
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &invalid));
	invalid.offset = kPackageLimit - 1;
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &invalid));
	invalid = sent;
	invalid.offer.manifest_digest[0] = 'G';
	REQUIRE_FALSE(gridMatchChunkWrite(&wire, &invalid));
	REQUIRE(wire.wp == 0);
	REQUIRE(wire.error == 0);
	REQUIRE(packet == empty);

	// Encode from a borrowed payload where the header will be written.
	std::memcpy(packet.data(), bytes.data(), bytes.size());
	grid_match_chunk_t borrowed = sent;
	borrowed.data = packet.data();
	REQUIRE(gridMatchChunkWrite(&wire, &borrowed));
	REQUIRE(std::memcmp(packet.data() + GRID_MATCH_CHUNK_WIRE_HEADER_SIZE,
		bytes.data(), bytes.size()) == 0);

	const grid_match_chunk_t previous = {kOffer, 42, 5, 1, bytes.data() + 1};
	for (u32 length = 0; length < wire.wp; length++) {
		struct netbuf truncated = {packet.data(), static_cast<u32>(packet.size()), 0, length, 0};
		grid_match_chunk_t output = previous;
		REQUIRE_FALSE(gridMatchChunkRead(&truncated, &output));
		REQUIRE(truncated.rp == 0);
		REQUIRE(truncated.error == 0);
		REQUIRE(output.package_index == previous.package_index);
		REQUIRE(output.data == previous.data);
	}
	packet[GRID_MATCH_CHUNK_WIRE_HEADER_SIZE - 1] = 'G';
	grid_match_chunk_t output = previous;
	REQUIRE_FALSE(gridMatchChunkRead(&wire, &output));
	REQUIRE(wire.rp == 0);
	REQUIRE(wire.error == 0);
	REQUIRE(output.offset == previous.offset);
	REQUIRE(output.data == previous.data);
}

TEST_CASE("Grid package receiver binds consent and admits only contiguous bytes or identical repeats",
	"[net][grid][distribution]")
{
	const std::array<u8, 8> bytes = {0x50, 0x4b, 0x03, 0x04, 10, 20, 30, 40};
	const grid_match_chunk_t first = {kOffer, kPackageIndex, 0, 4, bytes.data()};
	const grid_match_chunk_t last = {kOffer, kPackageIndex, 4, 4, bytes.data() + 4};
	Transfer transfer;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &first));
	REQUIRE_FALSE(gridMatchTransferBegin(&transfer.handle, &kOffer, kPackageIndex, 0));
	REQUIRE_FALSE(gridMatchTransferBegin(&transfer.handle, &kOffer, kPackageIndex, kPackageLimit + 1));
	REQUIRE_FALSE(gridMatchTransferBegin(&transfer.handle, &kOffer, UINT16_MAX, bytes.size()));
	REQUIRE(transfer.handle == nullptr);
	REQUIRE(gridMatchTransferBegin(&transfer.handle, &kOffer, kPackageIndex, bytes.size()));
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &last));
	REQUIRE(gridMatchTransferAppend(transfer.handle, &first));
	REQUIRE(gridMatchTransferAppend(transfer.handle, &first));
	REQUIRE_FALSE(gridMatchTransferBegin(&transfer.handle, &kOffer, kPackageIndex, bytes.size()));

	u32 size = 99;
	REQUIRE(gridMatchTransferComplete(transfer.handle, &kOffer, kPackageIndex, &size) == nullptr);
	REQUIRE(size == 0);
	grid_match_chunk_t wrong = first;
	wrong.data = last.data;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.offset--;
	wrong.data--;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.offset++;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.size++;
	wrong.data = bytes.data();
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.package_index++;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.offer.room_id++;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.offer.generation--;
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));
	wrong = last;
	wrong.offer.manifest_digest[0] = 'f';
	REQUIRE_FALSE(gridMatchTransferAppend(transfer.handle, &wrong));

	REQUIRE(gridMatchTransferAppend(transfer.handle, &last));
	REQUIRE(gridMatchTransferAppend(transfer.handle, &last));
	REQUIRE(gridMatchTransferComplete(transfer.handle, &wrong.offer, kPackageIndex, &size) == nullptr);
	REQUIRE(gridMatchTransferComplete(transfer.handle, &kOffer, kPackageIndex + 1, &size) == nullptr);
	const u8 *complete = gridMatchTransferComplete(transfer.handle, &kOffer, kPackageIndex, &size);
	REQUIRE(complete);
	REQUIRE(complete != bytes.data());
	REQUIRE(size == bytes.size());
	REQUIRE(std::memcmp(complete, bytes.data(), bytes.size()) == 0);

	gridMatchTransferReset(&transfer.handle);
	REQUIRE(transfer.handle == nullptr);
	REQUIRE(gridMatchTransferComplete(transfer.handle, &kOffer, kPackageIndex, &size) == nullptr);
	REQUIRE(size == 0);
	REQUIRE(gridMatchTransferBegin(&transfer.handle, &kOffer, kPackageIndex, kPackageLimit));
	REQUIRE(gridMatchTransferComplete(transfer.handle, &kOffer, kPackageIndex, &size) == nullptr);
}
