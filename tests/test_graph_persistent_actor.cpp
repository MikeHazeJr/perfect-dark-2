#include "catch.hpp"
#include "graph_persistent_actor.h"
#include <array>
#include <limits>
#include <map>
#include <tuple>

/* This bounded fake host proves component behavior, not native reservation,
 * ammo, damage, networking or real-time release guarantees. */
namespace {
using namespace graph_actor;
using Identity = std::tuple<uint64_t, uint64_t, uint64_t>;
struct FakeHost {
    int reservations = 0, creations = 0, closes = 0;
    bool capacity = true, contract_lost = false, close_fails = false;
    bool fail_after_create = false, zero_receipt = false, interrupted = false;
    Boundary pause = Boundary::BeforeRelease;
    size_t pause_index = 0;
    std::map<Identity, std::pair<uint64_t, uint64_t>> materialized;
    std::map<uint64_t, bool> leases;

    Host callbacks() {
        Host h{}; h.context = this;
        h.reserve = [](void *v, const Descriptor &, uint64_t *token) {
            auto &f = *static_cast<FakeHost *>(v);
            if (!f.capacity) return Result::Waiting;
            *token = uint64_t(++f.reservations);
            f.leases[*token] = true;
            return Result::Ready;
        };
        h.lookup = [](void *v, const Descriptor &d, uint64_t token,
            const Fragment &part, uint64_t *receipt) {
            auto &f = *static_cast<FakeHost *>(v);
            if (f.contract_lost || !f.leases[token]) return Result::HostFailure;
            const auto it = f.materialized.find({d.key.epoch, d.key.job, part.id});
            if (it == f.materialized.end()) return Result::Waiting;
            if (it->second.first != part.payload) return Result::HostFailure;
            *receipt = f.zero_receipt ? 0 : it->second.second;
            return Result::Ready;
        };
        h.materialize = [](void *v, const Descriptor &d, uint64_t token,
            const Fragment &part, uint64_t *receipt) {
            auto &f = *static_cast<FakeHost *>(v);
            if (f.contract_lost || !f.leases[token]) return Result::HostFailure;
            const Identity id{d.key.epoch, d.key.job, part.id};
            auto it = f.materialized.find(id);
            if (it == f.materialized.end()) {
                const auto proof = uint64_t(++f.creations);
                it = f.materialized.emplace(id, std::make_pair(part.payload, proof)).first;
            }
            if (f.fail_after_create) { f.fail_after_create = false; return Result::HostFailure; }
            *receipt = f.zero_receipt ? 0 : it->second.second;
            return Result::Ready;
        };
        h.close = [](void *v, const Descriptor &d, uint64_t token, bool committed) {
            auto &f = *static_cast<FakeHost *>(v);
            if (f.close_fails) return false;
            if (committed) {
                for (size_t i = 0; i < d.fragment_count; ++i)
                    if (!f.materialized.count({d.key.epoch, d.key.job, d.fragments[i].id})) return false;
            }
            if (f.leases[token]) { ++f.closes; f.leases[token] = false; }
            return true;
        };
        h.checkpoint = [](void *v, Boundary b, Key, size_t index) {
            auto &f = *static_cast<FakeHost *>(v);
            if (f.interrupted && b == f.pause && index == f.pause_index) {
                f.interrupted = false; return false;
            }
            return true;
        };
        return h;
    }
};
struct Fixture {
    FakeHost host;
    JobStorage jobs[3]{};
    Fragment descriptors[9]{};
    FragmentRecord receipts[9]{};
    Fragment input[3]{{101, 901}, {102, 902}, {103, 903}};
    Descriptor desc;
    Runtime runtime;
    Fixture() : runtime(jobs, 3, descriptors, receipts, 9, 3, host.callbacks()) {
        desc.key = {11, 22}; desc.clock = 7; desc.deadline = 50;
        desc.owner_generation = 12; desc.source_generation = 13;
        desc.source_closure[0] = 41; desc.origin.direction[2] = 1;
        desc.ammo_receipt = 23; desc.credit_receipt = 24;
        desc.fragments = input; desc.fragment_count = 3;
    }
};
}

TEST_CASE("Persistent actor commits one immutable release independently of caller lifetime",
    "[graph-persistent-actor]") {
    Fixture f;
    REQUIRE(f.runtime.valid());
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    REQUIRE(f.host.reservations == 1);
    REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Waiting);
    REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Waiting);
    REQUIRE(f.runtime.cancel_prepared(f.desc.key) == Result::Invalid);
    REQUIRE(f.runtime.advance(f.desc.key, 49) == Result::Waiting);
    REQUIRE(f.host.creations == 0);
    // Retire/mutate the original caller state. The match-owned snapshot survives.
    f.desc.owner_generation = 0; f.desc.source_generation = 0;
    f.desc.origin.position[0] = 999; f.input[0].payload = 999;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::Complete);
    REQUIRE(f.host.creations == 3);
    REQUIRE(f.host.closes == 1);
    REQUIRE(f.runtime.find(f.desc.key)->descriptor.owner_generation == 12);
    REQUIRE(f.runtime.find(f.desc.key)->descriptor.origin.position[0] == 0);
    REQUIRE(f.host.materialized.at({11, 22, 101}).first == 901);
    for (size_t i = 0; i < 3; ++i) REQUIRE(f.runtime.receipt(f.desc.key, i) != 0);
    REQUIRE(f.runtime.advance(f.desc.key, 100) == Result::Complete);
    REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Complete);
    REQUIRE(f.host.creations == 3);
    REQUIRE(f.host.closes == 1);
    Runtime destructive_reset(f.jobs, 3, f.descriptors, f.receipts, 9, 3, f.host.callbacks());
    REQUIRE_FALSE(destructive_reset.valid());
}

TEST_CASE("Persistent actor reconciles every interrupted release boundary",
    "[graph-persistent-actor]") {
    const std::array<Boundary, 7> points{{Boundary::BeforeRelease, Boundary::AfterReleaseClaim,
        Boundary::BeforeMaterialize, Boundary::AfterMaterialize, Boundary::BeforeReceipt,
        Boundary::AfterReceipt, Boundary::BeforeComplete}};
    for (Boundary point : points) for (size_t index = 0; index < 3; ++index) {
        if ((point == Boundary::BeforeRelease || point == Boundary::AfterReleaseClaim) && index) continue;
        if (point == Boundary::BeforeComplete && index) continue;
        Fixture f;
        f.host.pause = point; f.host.pause_index = point == Boundary::BeforeComplete ? 3 : index;
        f.host.interrupted = true;
        REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
        REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Waiting);
        REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::Interrupted);
        REQUIRE(f.runtime.find(f.desc.key)->state != State::Released);
        REQUIRE(f.host.closes == 0);
        REQUIRE(f.host.leases.at(1));
        REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::Complete);
        REQUIRE(f.host.creations == 3);
        REQUIRE(f.host.closes == 1);
        REQUIRE(f.runtime.advance(f.desc.key, 51) == Result::Complete);
        REQUIRE(f.host.creations == 3);
    }
}

TEST_CASE("Persistent actor rejects invalid admission without host side effects",
    "[graph-persistent-actor]") {
    Fixture f;
    const Descriptor good = f.desc;
    auto rejected = [&](Descriptor bad) {
        REQUIRE(f.runtime.prepare(bad, 10) == Result::Invalid);
        REQUIRE(f.host.reservations == 0);
        REQUIRE(f.runtime.find(good.key) == nullptr);
    };
    f.desc.key.epoch = 0; rejected(f.desc); f.desc = good;
    f.desc.key.job = 0; rejected(f.desc); f.desc = good;
    f.desc.clock = 0; rejected(f.desc); f.desc = good;
    f.desc.deadline = 9; rejected(f.desc); f.desc = good;
    f.desc.owner_generation = 0; rejected(f.desc); f.desc = good;
    f.desc.source_generation = 0; rejected(f.desc); f.desc = good;
    f.desc.source_closure[0] = 0; rejected(f.desc); f.desc = good;
    f.desc.ammo_receipt = 0; rejected(f.desc); f.desc = good;
    f.desc.credit_receipt = 0; rejected(f.desc); f.desc = good;
    f.desc.origin.direction[2] = 0; rejected(f.desc); f.desc = good;
    f.desc.origin.position[1] = std::numeric_limits<float>::infinity(); rejected(f.desc); f.desc = good;
    f.desc.fragments = nullptr; rejected(f.desc); f.desc = good;
    f.input[1].id = f.input[0].id; rejected(f.desc); f.input[1].id = 102;
    f.input[0].id = 0; rejected(f.desc); f.input[0].id = 101;
    f.input[0].payload = 0; rejected(f.desc); f.input[0].payload = 901;
    // An oversized count must fail before dereferencing even an invalid pointer.
    f.desc.fragment_count = std::numeric_limits<size_t>::max(); f.desc.fragments = nullptr;
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Capacity);
    REQUIRE(f.host.reservations == 0);
}

TEST_CASE("Persistent actor preserves failed host contracts and precommit cancellation",
    "[graph-persistent-actor]") {
    Fixture f;
    f.host.capacity = false;
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Waiting);
    REQUIRE(f.runtime.find(f.desc.key) == nullptr);
    f.host.capacity = true;
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    f.host.close_fails = true;
    REQUIRE(f.runtime.cancel_prepared(f.desc.key) == Result::HostFailure);
    REQUIRE(f.runtime.find(f.desc.key)->state == State::Prepared);
    f.host.close_fails = false;
    REQUIRE(f.runtime.cancel_prepared(f.desc.key) == Result::Ready);
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Waiting);
    f.host.contract_lost = true;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::HostFailure);
    REQUIRE(f.host.creations == 0);
    REQUIRE(f.runtime.find(f.desc.key)->state == State::Releasing);
    f.host.contract_lost = false; f.host.fail_after_create = true;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::HostFailure);
    REQUIRE(f.host.creations == 1);
    f.host.zero_receipt = true;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::HostFailure);
    REQUIRE(f.runtime.receipt(f.desc.key, 0) == 0);
    f.host.zero_receipt = false; f.host.close_fails = true;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::HostFailure);
    REQUIRE(f.host.creations == 3);
    REQUIRE(f.runtime.find(f.desc.key)->state == State::Releasing);
    REQUIRE(f.host.leases.at(2));
    f.host.close_fails = false;
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::Complete);
    REQUIRE(f.host.creations == 3);
}

TEST_CASE("Persistent actor validates fresh time before irrevocable commit",
    "[graph-persistent-actor]") {
    Fixture f;
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    REQUIRE(f.runtime.prepare(f.desc, 9) == Result::Invalid);
    REQUIRE(f.runtime.commit(f.desc.key, 9) == Result::Invalid);
    REQUIRE(f.runtime.prepare(f.desc, 51) == Result::Invalid);
    REQUIRE(f.runtime.commit(f.desc.key, 51) == Result::Invalid);
    REQUIRE(f.runtime.find(f.desc.key)->state == State::Prepared);
    REQUIRE(f.host.creations == 0);
    REQUIRE(f.host.reservations == 1);
    REQUIRE(f.runtime.cancel_prepared(f.desc.key) == Result::Ready);
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    REQUIRE(f.runtime.prepare(f.desc, 49) == Result::Ready);
    REQUIRE(f.runtime.commit(f.desc.key, 48) == Result::Invalid);
    REQUIRE(f.runtime.commit(f.desc.key, 50) == Result::Waiting);
    // A timely committed job continues under its identity even after its due
    // time. This recovery is not a claim that it physically met the deadline.
    REQUIRE(f.runtime.commit(f.desc.key, 100) == Result::Waiting);
    REQUIRE(f.runtime.prepare(f.desc, 100) == Result::Waiting);
    REQUIRE(f.runtime.advance(f.desc.key, 100) == Result::Complete);
    REQUIRE(f.host.creations == 3);
}

TEST_CASE("Persistent actor bounds multiple jobs and retains replay tombstones",
    "[graph-persistent-actor]") {
    Fixture f;
    REQUIRE(f.runtime.prepare(f.desc, 10) == Result::Ready);
    Descriptor other = f.desc;
    other.credit_receipt++;
    REQUIRE(f.runtime.prepare(other, 10) == Result::Conflict);
    other = f.desc; other.key.job++;
    REQUIRE(f.runtime.prepare(other, 10) == Result::Conflict); // Fragment IDs collide in epoch.
    Fragment parts[3]{{201, 901}, {202, 902}, {203, 903}};
    other.fragments = parts; other.owner_generation++;
    REQUIRE(f.runtime.prepare(other, 10) == Result::Ready);
    REQUIRE(f.runtime.commit(other.key, 10) == Result::Waiting);
    REQUIRE(f.runtime.advance(other.key, 9) == Result::Invalid);
    REQUIRE(f.runtime.advance(other.key, 50) == Result::Complete);
    REQUIRE(f.runtime.prepare(other, 99) == Result::Complete); // Same identity remains idempotent.
    REQUIRE(f.runtime.commit(f.desc.key, 10) == Result::Waiting);
    REQUIRE(f.runtime.advance(f.desc.key, 50) == Result::Complete);
    other.key.epoch++; // Same fragment IDs in a distinct match epoch are distinct identities.
    REQUIRE(f.runtime.prepare(other, 10) == Result::Ready);
    REQUIRE(f.runtime.commit(other.key, 10) == Result::Waiting);
    REQUIRE(f.runtime.advance(other.key, 50) == Result::Complete);
    other.key.job++;
    other.key.epoch++;
    REQUIRE(f.runtime.prepare(other, 10) == Result::Capacity);
    REQUIRE(f.host.creations == 9);
    REQUIRE(f.host.reservations == 3);
    REQUIRE(f.runtime.receipt(other.key, 0) == 0);
    Runtime overflow(f.jobs, std::numeric_limits<size_t>::max(), f.descriptors,
        f.receipts, 9, 3, f.host.callbacks());
    REQUIRE_FALSE(overflow.valid());
}
