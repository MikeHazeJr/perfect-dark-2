#include "catch.hpp"
#include "net_move_fixture.h"
#include <array>
#include <cfloat>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>

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
using IngressFixture = std::unique_ptr<void, decltype(&moveIngressDestroy)>;
constexpr uint32_t force = 0xe0000000u;
constexpr uint32_t select = 1u << 7, dual = 1u << 8, fire = 1u;
std::vector<unsigned char> ingressPacket(bool snapshot, uint32_t tick,
        uint32_t ack, uint32_t command = 0, int weapon = 1, unsigned char id = 1) {
    std::vector<unsigned char> packet;
    if (snapshot) packet.push_back(id);
    word(packet, ack);
    std::array<unsigned char, 57> move{};
    const auto size = moveFixtureWrite(move.data(), move.size(), tick,
        command, fields.data(), weapon);
    packet.insert(packet.end(), move.begin(), move.begin() + size);
    if (snapshot && (command & force)) {
        packet.insert(packet.end(), {4, 0, 0xff, 0xff}); // room 4, terminator
    }
    return packet;
}
int readIngress(void *f, bool snapshot, const std::vector<unsigned char> &packet,
        move_ingress_result &r) {
    return moveIngressRead(f, snapshot, packet.data(), packet.size(), &r);
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

TEST_CASE("Production movement readers reject stale records before ACK force or witness effects",
          "[net][move][ingress]") {
    for (bool snapshot : {false, true}) {
        IngressFixture f(moveIngressCreate(1), moveIngressDestroy);
        REQUIRE(f);
        move_ingress_result r{};
        REQUIRE(readIngress(f.get(), snapshot, ingressPacket(snapshot, 100, 50), r) == 0);
        REQUIRE(readIngress(f.get(), snapshot, ingressPacket(snapshot, 102, 55), r) == 0);
        moveIngressForce(f.get(), 60, force);
        moveIngressWitness(f.get());
        for (uint32_t tick : {100u, 101u, 102u, 102u + 0x80000000u}) {
            CAPTURE(snapshot, tick);
            REQUIRE(readIngress(f.get(), snapshot,
                ingressPacket(snapshot, tick, 60, force | fire), r) == 0);
            REQUIRE(r.unchanged);
            REQUIRE(r.tick == 102);
            REQUIRE(r.previousTick == 100);
            REQUIRE(r.ack == 55);
            REQUIRE(r.lerp == 17);
            REQUIRE(r.forceTick == 60);
            REQUIRE(r.playerCommand == force);
            REQUIRE(r.witnessPending);
            REQUIRE(r.teleports == 0);
            REQUIRE(r.logs == 0);
        }
    }
}

TEST_CASE("Production movement readers reject every truncation and nonfinite float without effects",
          "[net][move][ingress]") {
    const size_t offsets[] = {8,12,16,20,24,28,32,36,41,45,49,53};
    for (bool snapshot : {false, true}) {
        IngressFixture f(moveIngressCreate(1), moveIngressDestroy);
        REQUIRE(f);
        move_ingress_result r{};
        REQUIRE(readIngress(f.get(), snapshot, ingressPacket(snapshot, 100, 50), r) == 0);
        moveIngressForce(f.get(), 60, force);
        auto packet = ingressPacket(snapshot, 101, 60, force | aim);
        for (size_t size = 0; size < packet.size(); ++size) {
            CAPTURE(snapshot, size);
            REQUIRE(moveIngressRead(f.get(), snapshot, packet.data(), size, &r) != 0);
            REQUIRE(r.unchanged);
            REQUIRE(r.teleports == 0);
            REQUIRE(r.logs == 0);
            REQUIRE(r.singleChecks == 0);
        }
        for (size_t offset : offsets) for (uint32_t bits : {0x7fc00001u, 0x7f800000u, 0xff800000u}) {
            CAPTURE(snapshot, offset, bits);
            auto invalid = packet;
            const size_t prefix = snapshot ? 5 : 4;
            for (int i = 0; i < 4; ++i) invalid[prefix + offset + i] = (unsigned char)(bits >> (8 * i));
            REQUIRE(readIngress(f.get(), snapshot, invalid, r) != 0);
            REQUIRE(r.unchanged);
            REQUIRE(r.teleports == 0);
            REQUIRE(r.logs == 0);
        }
    }
}

TEST_CASE("CLC movement ACK clears pending force only on a fresh accepted record",
          "[net][move][ingress]") {
    IngressFixture f(moveIngressCreate(1), moveIngressDestroy);
    REQUIRE(f);
    move_ingress_result r{};
    moveIngressForce(f.get(), 60, force | fire);
    REQUIRE(readIngress(f.get(), false, ingressPacket(false, 100, 55), r) == 0);
    REQUIRE(r.position == 700.f);
    REQUIRE(r.angle == 90.f);
    REQUIRE(r.forceTick == 60);
    REQUIRE(r.forceLogs == 0);
    moveIngressWitness(f.get());
    REQUIRE(readIngress(f.get(), false, ingressPacket(false, 101, 60, fire), r) == 0);
    REQUIRE(r.forceTick == 0);
    REQUIRE(r.playerCommand == fire);
    REQUIRE(r.position == fields[8]);
    REQUIRE(r.forceLogs == 1);
    REQUIRE(r.reconnectLogs == 1);
    REQUIRE_FALSE(r.witnessPending);
    REQUIRE(r.teleports == 0);
    REQUIRE(readIngress(f.get(), false, ingressPacket(false, 101, 61, fire), r) == 0);
    REQUIRE(r.unchanged);
    REQUIRE(r.logs == 0);
}

TEST_CASE("SVC force snapshot teleports the local player exactly once and never a remote player",
          "[net][move][ingress]") {
    for (int local : {0, 1}) {
        IngressFixture f(moveIngressCreate(local), moveIngressDestroy);
        REQUIRE(f);
        move_ingress_result r{};
        auto packet = ingressPacket(true, 0, 50, force);
        REQUIRE(readIngress(f.get(), true, packet, r) == 0);
        REQUIRE(r.teleports == local);
        if (local) {
            REQUIRE(r.teleportPosition == fields[8]);
            REQUIRE(r.teleportAngle == fields[4]);
            REQUIRE(r.ground);
            REQUIRE(r.room == 4);
        }
        REQUIRE(readIngress(f.get(), true, packet, r) == 0);
        REQUIRE(r.unchanged);
        REQUIRE(r.teleports == 0);
    }
}

TEST_CASE("CLC weapon policy uses the sending player inventory and sanitizes only selection",
          "[net][move][ingress]") {
    for (int single : {0, 1}) for (int doubleWeapon : {0, 1}) for (bool dualWield : {false, true}) {
        CAPTURE(single, doubleWeapon, dualWield);
        IngressFixture f(moveIngressCreate(1), moveIngressDestroy);
        REQUIRE(f);
        moveIngressInventory(f.get(), single, doubleWeapon);
        move_ingress_result r{};
        const uint32_t command = fire | select | (dualWield ? dual : 0);
        REQUIRE(readIngress(f.get(), false, ingressPacket(false, 0, 0, command, 2), r) == 0);
        const bool allowed = single && (!dualWield || doubleWeapon);
        REQUIRE(r.command == (allowed ? command : fire));
        REQUIRE(r.weapon == (allowed ? 2 : -1));
        REQUIRE(r.singleChecks == 1);
        REQUIRE(r.doubleChecks == (dualWield ? 1 : 0));
        REQUIRE(r.playerChanges == 2);
        REQUIRE(r.inventoryPlayer == 1);
        REQUIRE(r.currentPlayer == 3);
        REQUIRE(r.warnings == (allowed ? 0 : 1));
        REQUIRE(r.teleports == 0);
    }
}

TEST_CASE("Production movement state ID and vehicle intent gates leave client state intact",
          "[net][move][ingress]") {
    for (bool snapshot : {false, true}) {
        IngressFixture f(moveIngressCreate(1), moveIngressDestroy);
        REQUIRE(f);
        move_ingress_result r{};
        moveIngressReady(f.get(), 0, 0);
        REQUIRE(readIngress(f.get(), snapshot, ingressPacket(snapshot, 1, 60, force), r) == 0);
        REQUIRE(r.unchanged);
        REQUIRE(r.logs == 0);
        REQUIRE(r.teleports == 0);
        moveIngressReady(f.get(), 1, 1);
        if (snapshot) {
            REQUIRE(readIngress(f.get(), true, ingressPacket(true, 1, 60, force, 1, 0xff), r) != 0);
            REQUIRE(r.warnings == 1);
            moveIngressReady(f.get(), 1, 0);
            REQUIRE(readIngress(f.get(), true, ingressPacket(true, 1, 60, force), r) == 0);
        } else {
            REQUIRE(readIngress(f.get(), false, ingressPacket(false, 1, 60, 1u << 12), r) != 0);
            REQUIRE(r.warnings == 1);
        }
        REQUIRE(r.unchanged);
        REQUIRE(r.teleports == 0);
    }
}

TEST_CASE("Tick zero consumes fresh actions once while retaining held input and the complete record",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    const uint32_t held = fire | aim | (1u << 9);
    const uint32_t edges = (1u << 1) | (1u << 2) | select | dual | (1u << 11);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.pendingBefore);
    REQUIRE_FALSE(c.consumed);
    REQUIRE_FALSE(c.appliedValid);
    REQUIRE(c.commandAfter == 0);
    moveFixtureCommit(f.get(), 0, 0, 10.f, held | edges, 0, &r);
    REQUIRE(r.accepted);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.pendingBefore);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedValid);
    REQUIRE(c.appliedTick == 0);
    REQUIRE(c.receivedValid);
    REQUIRE_FALSE(c.previousValid);
    REQUIRE(c.interval == 0);
    REQUIRE(c.commandBefore == (held | (1u << 2) | select | dual));
    REQUIRE(c.commandAfter == held);
    REQUIRE(c.recordUnchanged);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.pendingBefore);
    REQUIRE_FALSE(c.pendingAfter);
    REQUIRE_FALSE(c.consumed);
    REQUIRE(c.commandBefore == held);
    REQUIRE(c.commandAfter == held);
    REQUIRE(c.recordUnchanged);
}

TEST_CASE("Applied and previous record validity survive the full serial wrap sequence",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    bool first = true;
    for (uint32_t tick : {0xfffffffeu, 0xffffffffu, 0u, 1u}) {
        CAPTURE(tick);
        moveFixtureCommit(f.get(), tick, tick, 2.f, fire | (1u << 2), 0, &r);
        REQUIRE(r.accepted);
        moveFixtureConsume(f.get(), 6, &c);
        REQUIRE(c.pendingBefore);
        REQUIRE(c.consumed);
        REQUIRE(c.appliedValid);
        REQUIRE(c.appliedTick == tick);
        REQUIRE(c.previousValid == !first);
        REQUIRE(c.interval == (first ? 0 : 1));
        REQUIRE(c.commandBefore == (fire | (1u << 2)));
        REQUIRE(c.commandAfter == fire);
        REQUIRE(c.recordUnchanged);
        moveFixtureConsume(f.get(), 6, &c);
        REQUIRE_FALSE(c.consumed);
        first = false;
    }
    REQUIRE(r.previousTick == 0);
    for (uint32_t tick : {0xffffffffu, 0u, 1u, 0x80000001u}) {
        moveFixtureCommit(f.get(), tick, 99, -10.f, select, 2, &r);
        REQUIRE_FALSE(r.accepted);
        REQUIRE(r.unchanged);
        moveFixtureConsume(f.get(), 6, &c);
        REQUIRE_FALSE(c.pendingBefore);
        REQUIRE_FALSE(c.consumed);
        REQUIRE(c.appliedTick == 1);
        REQUIRE(c.commandAfter == fire);
    }
}

TEST_CASE("Multiple received records before application consume only the newest record across wrap",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    moveFixtureCommit(f.get(), 0xfffffffeu, 0, 1.f, fire, 0, &r);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    moveFixtureCommit(f.get(), 0xffffffffu, 0, 2.f, select, 0, &r);
    REQUIRE(r.accepted);
    moveFixtureCommit(f.get(), 0, 0, 3.f, aim | (1u << 2), 0, &r);
    REQUIRE(r.accepted);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 0);
    REQUIRE(c.interval == 1);
    REQUIRE(c.commandBefore == (aim | (1u << 2)));
    REQUIRE(c.commandAfter == aim);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.consumed);
}

TEST_CASE("Interpolation interval clamps serial gaps and treats previous tick zero as history",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    moveFixtureCommit(f.get(), 0, 0, 1.f, fire, 0, &r);
    moveFixtureCommit(f.get(), 1, 0, 2.f, fire, 0, &r);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.previousValid);
    REQUIRE(c.interval == 1);
    moveFixtureCommit(f.get(), 0x7fffffffu, 0, 3.f, fire, 0, &r);
    REQUIRE(r.accepted);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.interval == 6);
    for (int cap : {0, -1}) {
        moveFixtureConsume(f.get(), cap, &c);
        REQUIRE(c.interval == 0);
        REQUIRE(c.commandAfter == fire);
    }
}

TEST_CASE("Unconsumed receipt stays fresh after cumulative admitted advances exceed half-range",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    moveFixtureCommit(f.get(), 0, 0, 1.f, fire, 0, &r);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 0);
    const uint32_t held = fire | aim;
    const uint32_t forwardedEdges = (1u << 2) | select | dual;
    for (uint32_t tick : {0x7fffffffu, 0xfffffffeu}) {
        moveFixtureCommit(f.get(), tick, 0, 2.f, held | forwardedEdges, 0, &r);
        REQUIRE(r.accepted);
    }
    /* Rejecting a duplicate must also preserve an outstanding fresh receipt. */
    moveFixtureCommit(f.get(), 0xfffffffeu, 10, -9.f, 0, 0, &r);
    REQUIRE_FALSE(r.accepted);
    REQUIRE(r.unchanged);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.pendingBefore);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 0xfffffffeu);
    REQUIRE(c.receiptPendingBefore);
    REQUIRE_FALSE(c.receiptPendingAfter);
    REQUIRE(c.interval == 6);
    REQUIRE(c.commandBefore == (held | forwardedEdges));
    REQUIRE(c.commandAfter == held);
    REQUIRE(c.recordUnchanged);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.pendingBefore);
    REQUIRE_FALSE(c.consumed);
    REQUIRE(c.commandAfter == held);
}

TEST_CASE("Unconsumed receipt stays fresh after a full wrap back to the last applied tick",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    moveFixtureCommit(f.get(), 0, 0, 1.f, fire, 0, &r);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 0);
    const uint32_t held = fire | aim | (1u << 9);
    const uint32_t forwardedEdges = (1u << 2) | select | dual;
    for (uint32_t tick : {0x7fffffffu, 0xfffffffeu, 0u}) {
        moveFixtureCommit(f.get(), tick, 0, 2.f, held | forwardedEdges, 0, &r);
        REQUIRE(r.accepted);
    }
    REQUIRE(r.tick == 0); // Numerically equal to the already applied old receipt.
    REQUIRE(r.previousTick == 0xfffffffeu);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.pendingBefore);
    REQUIRE(c.consumed);
    REQUIRE(c.receiptPendingBefore);
    REQUIRE_FALSE(c.receiptPendingAfter);
    REQUIRE(c.appliedValid);
    REQUIRE(c.appliedTick == 0);
    REQUIRE(c.interval == 2);
    REQUIRE(c.commandBefore == (held | forwardedEdges));
    REQUIRE(c.commandAfter == held);
    REQUIRE(c.recordUnchanged);
    moveFixtureCommit(f.get(), 0, 10, -9.f, 0, 0, &r);
    REQUIRE_FALSE(r.accepted);
    REQUIRE(r.unchanged);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.pendingBefore);
    REQUIRE_FALSE(c.consumed);
    REQUIRE(c.commandAfter == held);
    moveFixtureCommit(f.get(), 1, 0, 3.f, held | forwardedEdges, 0, &r);
    REQUIRE(r.accepted);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 1);
    REQUIRE(c.interval == 1);
}

TEST_CASE("Movement reset invalidates received previous applied and ACK state for a new first zero",
          "[net][move][consumption]") {
    AdmissionFixture f(moveFixtureCreate(), moveFixtureDestroy);
    REQUIRE(f);
    move_admission_result r{};
    move_consumption_result c{};
    moveFixtureCommit(f.get(), 100, 50, 1.f, fire, 50, &r);
    moveFixtureCommit(f.get(), 101, 51, 2.f, fire, 51, &r);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.previousValid);
    REQUIRE(c.appliedValid);
    moveFixtureReset(f.get());
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE_FALSE(c.receivedValid);
    REQUIRE_FALSE(c.previousValid);
    REQUIRE_FALSE(c.appliedValid);
    REQUIRE_FALSE(c.consumed);
    REQUIRE_FALSE(c.receiptPendingBefore);
    REQUIRE_FALSE(c.receiptPendingAfter);
    REQUIRE(c.commandAfter == 0);
    moveFixtureCommit(f.get(), 0, 0, 3.f, fire, 0, &r);
    REQUIRE(r.accepted);
    REQUIRE_FALSE(r.ackValid);
    REQUIRE_FALSE(r.acknowledged);
    moveFixtureConsume(f.get(), 6, &c);
    REQUIRE(c.consumed);
    REQUIRE(c.appliedTick == 0);
    REQUIRE_FALSE(c.previousValid);
}

TEST_CASE("Movement consumers wire serial helpers before marking edges applied and forward frame edges",
          "[net][move][consumption][static]") {
    auto read = [](const char *path) {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.good());
        std::ostringstream text; text << input.rdbuf(); return text.str();
    };
    const auto movement = read("src/game/bondmove.c");
    const auto walk = read("src/game/bondwalk.c");
    const auto net = read("port/src/net/net.c");
    const auto start = movement.find("static inline void bmoveProcessRemoteInput(");
    const auto end = movement.find("void bmoveSetControlDef(", start);
    const auto remote = movement.substr(start, end - start);
    REQUIRE(remote.find("!netPlayerMovePending(pl->client)") != std::string::npos);
    REQUIRE(remote.find("if (!pl->client->inmove_valid)") != std::string::npos);
    REQUIRE(remote.find("netPlayerMoveInterval(pl->client, g_NetInterpTicks)") != std::string::npos);
    REQUIRE(remote.find("!pl->client->inmoveprev_valid") != std::string::npos);
    REQUIRE(remote.find("else if (!(inmove->ucmd & UCMD_JUMP))") != std::string::npos);
    REQUIRE(remote.find("!handled && (inmove->ucmd & UCMD_SELECT)") != std::string::npos);
    const auto consumed = remote.find("netPlayerMoveConsume(pl->client)");
    REQUIRE(consumed != std::string::npos);
    for (const char *edge : {"UCMD_JUMP", "UCMD_SELECT", "UCMD_RELOAD", "UCMD_ACTIVATE"}) {
        const auto action = remote.find(std::string("!handled && (inmove->ucmd & ") + edge + ")");
        REQUIRE(action != std::string::npos);
        REQUIRE(action < consumed);
    }
    REQUIRE(remote.find("pl->ucmd &= ~(UCMD_RELOAD | UCMD_SELECT | UCMD_SELECT_DUAL)") != std::string::npos);
    REQUIRE(remote.find("pl->ucmd |= netPlayerMoveForwardCommands(pl->client)") < consumed);
    REQUIRE(remote.find("inmovetick >=") == std::string::npos);
    REQUIRE(remote.find("!inmove->tick") == std::string::npos);
    REQUIRE(walk.find("if (!pl->client->inmove_valid) return;") != std::string::npos);
    REQUIRE(walk.find("netPlayerMoveInterval(pl->client, g_NetInterpTicks)") != std::string::npos);
    REQUIRE(walk.find("!inmoveprev->tick") == std::string::npos);
    REQUIRE(net.find("pl->isremote && cl->inmove_valid") != std::string::npos);
    REQUIRE(net.find("move->ucmd = pl->ucmd") < net.find("move->ucmd |= netPlayerMoveForwardCommands(cl)"));
    REQUIRE((net.find("memset(cl, 0, sizeof(*cl));\n\tnetPlayerMoveReset(cl);") != std::string::npos
        || net.find("memset(cl, 0, sizeof(*cl));\r\n\tnetPlayerMoveReset(cl);") != std::string::npos));
}
