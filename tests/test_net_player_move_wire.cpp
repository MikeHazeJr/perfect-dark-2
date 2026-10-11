#include "catch.hpp"
#include "net_move_fixture.h"
#include <array>
#include <cfloat>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {
using AdmissionFixture = std::unique_ptr<void, decltype(&moveFixtureDestroy)>;
constexpr uint32_t aim = 1u << 3;
const std::array<float, 12> fields = {-.25f, -45.f, 2.f, -3.f, 180.f,
    22.f, 160.f, 120.f, 1024.f, -100.f, 2048.f, 30.f};
void word(std::vector<unsigned char> &wire, uint32_t v) {
    for (int i = 0; i < 4; ++i) wire.push_back((unsigned char)(v >> (8 * i)));
}
void real(std::vector<unsigned char> &wire, float f) {
    uint32_t v; std::memcpy(&v, &f, sizeof(v)); word(wire, v);
}
std::vector<unsigned char> golden(bool aiming) {
    std::vector<unsigned char> wire;
    word(wire, 123); word(wire, aiming ? aim : 0);
    for (int i = 0; i < 8; ++i) real(wire, fields[i]);
    wire.push_back(1); // Existing unarmed runtime weapon value.
    for (int i = 8; i < 11; ++i) real(wire, fields[i]);
    if (aiming) real(wire, fields[11]);
    return wire;
}
}

TEST_CASE("Rejected movement cannot rewind state or acknowledge a pending teleport",
          "[net][move][ordering]") {
    AdmissionFixture fixture(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(fixture);
    move_admission_result r{};
    moveFixtureCommit(fixture.get(), 100, 50, 100.f, 0, 60, &r);
    REQUIRE(r.accepted);
    REQUIRE_FALSE(r.acknowledged);
    moveFixtureCommit(fixture.get(), 102, 55, 102.f, 0, 60, &r);
    REQUIRE(r.accepted);
    REQUIRE(r.previousTick == 100);
    for (uint32_t tick : {100u, 101u, 102u, 102u + 0x80000000u}) {
        CAPTURE(tick);
        moveFixtureCommit(fixture.get(), tick, 60, -900.f, 0xffffffffu, 60, &r);
        REQUIRE_FALSE(r.accepted);
        REQUIRE(r.unchanged);
        REQUIRE(r.tick == 102);
        REQUIRE(r.previousTick == 100);
        REQUIRE(r.position == 102.f);
        REQUIRE(r.command == 0);
        REQUIRE(r.ack == 55);
        REQUIRE(r.lerp == 17);
        REQUIRE_FALSE(r.acknowledged);
    }
    moveFixtureCommit(fixture.get(), 103, 60, 103.f, 0, 60, &r);
    REQUIRE(r.accepted);
    REQUIRE(r.previousTick == 102);
    REQUIRE(r.acknowledged);
    REQUIRE(r.lerp == 0);
}

TEST_CASE("Fresh movement preserves the newest acknowledgement",
          "[net][move][ordering]") {
    AdmissionFixture fixture(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(fixture);
    move_admission_result r{};
    moveFixtureCommit(fixture.get(), 100, 50, 1.f, 0, 50, &r);
    REQUIRE(r.accepted);
    uint32_t tick = 100;
    for (uint32_t ack : {49u, 0u, 50u, 50u + 0x80000000u}) {
        CAPTURE(ack);
        moveFixtureCommit(fixture.get(), ++tick, ack, 2.f, 0, 50, &r);
        REQUIRE(r.accepted);
        REQUIRE(r.ack == 50);
        REQUIRE(r.acknowledged);
    }
}

TEST_CASE("Movement ordering and acknowledgements cross serial wrap once",
          "[net][move][ordering]") {
    AdmissionFixture fixture(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(fixture);
    move_admission_result r{};
    for (uint32_t tick : {0xfffffffeu, 0xffffffffu, 0u, 1u}) {
        CAPTURE(tick);
        moveFixtureCommit(fixture.get(), tick, tick, 1.f, 0, 0xffffffffu, &r);
        REQUIRE(r.accepted);
        REQUIRE(r.ack == tick);
        REQUIRE(r.ackValid);
        REQUIRE(r.acknowledged == (tick != 0xfffffffeu));
    }
    for (uint32_t tick : {0xffffffffu, 0u, 1u, 0x80000001u}) {
        moveFixtureCommit(fixture.get(), tick, 10, 2.f, 0, 2, &r);
        REQUIRE_FALSE(r.accepted);
        REQUIRE(r.unchanged);
        REQUIRE_FALSE(r.acknowledged);
    }
}

TEST_CASE("First zero record has explicit validity and an unknown acknowledgement",
          "[net][move][ordering]") {
    AdmissionFixture fixture(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(fixture);
    move_admission_result r{};
    moveFixtureCommit(fixture.get(), 0, 0, 1.f, 0, 0, &r);
    REQUIRE(r.accepted);
    REQUIRE_FALSE(r.ackValid);
    REQUIRE_FALSE(r.acknowledged);
    moveFixtureCommit(fixture.get(), 0, 12, 2.f, 0, 0, &r);
    REQUIRE_FALSE(r.accepted);
    REQUIRE(r.unchanged);
    /* A late join may first acknowledge a server with a large current tick. */
    moveFixtureCommit(fixture.get(), 1, 0xf0000000u, 2.f, 0, 0xf0000000u, &r);
    REQUIRE(r.accepted);
    REQUIRE(r.ackValid);
    REQUIRE(r.acknowledged);
}

TEST_CASE("Movement decoder preserves the existing payload and initializes unwired identity",
          "[net][move][numeric]") {
    for (bool aiming : {false, true}) {
        CAPTURE(aiming);
        auto wire = golden(aiming);
        REQUIRE(wire.size() == (aiming ? 57 : 53));
        std::array<unsigned char, 57> encoded{};
        REQUIRE(moveFixtureWrite(encoded.data(), encoded.size(), 123,
            aiming ? aim : 0, fields.data(), 1) == wire.size());
        REQUIRE(std::memcmp(encoded.data(), wire.data(), wire.size()) == 0);
        move_fixture_result result{};
        REQUIRE(moveFixtureRead(wire.data(), wire.size(), &result) == 0);
        REQUIRE(result.tick == 123);
        REQUIRE(result.ucmd == (aiming ? aim : 0));
        REQUIRE(result.weaponnum == 1);
        REQUIRE(result.emptyIdentity == 1);
        for (int i = 0; i < 11; ++i) REQUIRE(result.fields[i] == fields[i]);
        REQUIRE(result.fields[11] == (aiming ? fields[11] : 0.f));
    }
}

TEST_CASE("Movement decoder rejects every truncated ordinary and aiming packet transactionally",
          "[net][move][numeric]") {
    for (bool aiming : {false, true}) {
        auto wire = golden(aiming);
        for (size_t size = 0; size < wire.size(); ++size) {
            CAPTURE(aiming, size);
            move_fixture_result result{};
            REQUIRE(moveFixtureRead(wire.data(), (uint32_t)size, &result) != 0);
            REQUIRE(result.unchanged == 1);
        }
    }
}

TEST_CASE("Movement decoder rejects NaN and both infinities in every transmitted float",
          "[net][move][numeric]") {
    const size_t offsets[] = {8,12,16,20,24,28,32,36,41,45,49,53};
    const uint32_t nonfinite[] = {0x7fc00001u, 0x7f800000u, 0xff800000u};
    for (size_t offset : offsets) for (uint32_t bits : nonfinite) {
        CAPTURE(offset, bits);
        auto wire = golden(true);
        for (int i = 0; i < 4; ++i) wire[offset + i] = (unsigned char)(bits >> (8 * i));
        move_fixture_result result{};
        REQUIRE(moveFixtureRead(wire.data(), wire.size(), &result) != 0);
        REQUIRE(result.unchanged == 1);
    }
}

TEST_CASE("Movement numeric decoding does not invent physical movement limits",
          "[net][move][numeric]") {
    auto values = fields;
    for (int i = 0; i < 12; ++i) values[i] = i % 2 ? -FLT_MAX : FLT_MAX;
    std::array<unsigned char, 57> wire{};
    REQUIRE(moveFixtureWrite(wire.data(), wire.size(), 123, aim, values.data(), 1) == 57);
    move_fixture_result result{};
    REQUIRE(moveFixtureRead(wire.data(), wire.size(), &result) == 0);
    for (int i = 0; i < 12; ++i) REQUIRE(result.fields[i] == values[i]);
}
