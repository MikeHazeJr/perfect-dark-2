#include "catch.hpp"
#include "net/group_session_policy.h"
#include <cstring>
#include <utility>
#include <vector>

namespace {
struct Retired {
    std::vector<std::pair<u32, u32>> peers;
    int routes = 0;
};
void retirePeer(void *ctx, u32 handle, u32 pair) {
    static_cast<Retired *>(ctx)->peers.emplace_back(handle, pair);
}
void retireRoute(void *ctx) { ++static_cast<Retired *>(ctx)->routes; }
group_session_t group() {
    group_session_t s{};
    s.local_handle = 10; s.in_session = 1;
    s.authority_handle = 20; s.latched_authority_handle = 20;
    s.transport_attempted = 1; s.initiator_handle = 10;
    s.peers[0].handle = 20; s.peers[0].pair_id = 123;
    s.peers[0].state = GROUP_PEER_CONNECTED;
    s.peers[1].handle = 30; s.peers[1].state = GROUP_PEER_INVITED;
    return s;
}
}

TEST_CASE("Group owner keeps same-Agent state and never repeats cleanup",
          "[net][group][owner]") {
    auto s = group(), before = s;
    Retired retired; group_session_retire_ops ops{retirePeer, retireRoute, &retired};
    REQUIRE(groupSessionRefreshOwner(&s, 10, 1, &ops) == 1);
    REQUIRE(std::memcmp(&s, &before, sizeof(s)) == 0);
    REQUIRE(retired.peers.empty()); REQUIRE(retired.routes == 0);
}

TEST_CASE("Group owner change cannot steal a live ENet transport",
          "[net][group][owner]") {
    auto s = group(), before = s;
    Retired retired; group_session_retire_ops ops{retirePeer, retireRoute, &retired};
    REQUIRE(groupSessionRefreshOwner(&s, 99, 1, &ops) == 0);
    REQUIRE(std::memcmp(&s, &before, sizeof(s)) == 0);
    REQUIRE(retired.peers.empty()); REQUIRE(retired.routes == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 99, 20) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 1);
}

TEST_CASE("Group owner change retires exact peers and routes after transport ends",
          "[net][group][owner]") {
    auto s = group();
    Retired retired; group_session_retire_ops ops{retirePeer, retireRoute, &retired};
    REQUIRE(groupSessionRefreshOwner(&s, 99, 0, &ops) == 2);
    const std::vector<std::pair<u32,u32>> expectedPeers{{20,123},{30,0}};
    REQUIRE(retired.peers == expectedPeers);
    REQUIRE(retired.routes == 1);
    group_session_t expected{}; expected.local_handle = 99;
    REQUIRE(std::memcmp(&s, &expected, sizeof(s)) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 99, 20) == 0);
    REQUIRE(groupSessionRefreshOwner(&s, 99, 0, &ops) == 1);
    REQUIRE(retired.peers.size() == 2); REQUIRE(retired.routes == 1);
}

TEST_CASE("Group owner rejects missing identity or cleanup without mutating state",
          "[net][group][owner]") {
    auto s = group(), before = s;
    Retired retired; group_session_retire_ops ops{retirePeer, retireRoute, &retired};
    REQUIRE(groupSessionRefreshOwner(&s, 0, 0, &ops) == 0);
    REQUIRE(groupSessionRefreshOwner(nullptr, 99, 0, &ops) == 0);
    REQUIRE(groupSessionRefreshOwner(&s, 99, 0, nullptr) == 0);
    ops.peer = nullptr;
    REQUIRE(groupSessionRefreshOwner(&s, 99, 0, &ops) == 0);
    REQUIRE(std::memcmp(&s, &before, sizeof(s)) == 0);
    REQUIRE(retired.peers.empty()); REQUIRE(retired.routes == 0);
}

TEST_CASE("Voice audience requires an accepted peer in the current Agent group",
          "[net][group][audience]") {
    auto s = group();
    REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 1);
    REQUIRE(groupSessionPeerAccepted(&s, 10, 30) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 10, 40) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 99, 20) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 10, 10) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 0, 20) == 0);
    REQUIRE(groupSessionPeerAccepted(&s, 10, 0) == 0);
    REQUIRE(groupSessionPeerAccepted(nullptr, 10, 20) == 0);
    s.in_session = 0;
    REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 0);
}

TEST_CASE("Voice admission follows invite acceptance decline failure and removal",
          "[net][group][audience]") {
    auto s = group();
    for (auto state : {GROUP_PEER_UNKNOWN, GROUP_PEER_INVITED, GROUP_PEER_FAILED}) {
        s.peers[0].state = state;
        REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 0);
    }
    for (auto state : {GROUP_PEER_RESOLVING, GROUP_PEER_CONNECTED}) {
        s.peers[0].state = state;
        REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 1);
    }
    s.peers[0].handle = 0;
    REQUIRE(groupSessionPeerAccepted(&s, 10, 20) == 0);
}
