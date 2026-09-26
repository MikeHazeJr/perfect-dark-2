#include "catch.hpp"
#include "net/distrib_queue.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
struct Queue {
    distrib_queue value{};
    ~Queue() { distribQueueClear(&value); }
};
distrib_request request(void *client, void *peer, uint32_t connection, const char *id)
{
    distrib_request r{};
    r.client = client;
    r.peer = peer;
    r.connection = connection;
    std::snprintf(r.id, sizeof(r.id), "%s", id);
    return r;
}
}

TEST_CASE("Distribution admission coalesces retransmission without changing consent",
          "[net][distribution][queue]")
{
    Queue q;
    int client, peer;
    auto r = request(&client, &peer, 7, "weapon:test");
    r.temporary = 1;
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    r.temporary = 0;
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_DUPLICATE);
    REQUIRE(q.value.count == 1);
    REQUIRE(distribQueuePop(&q.value, &r));
    REQUIRE(r.temporary == 1);
    REQUIRE_FALSE(distribQueuePop(&q.value, &r));
}

TEST_CASE("Distribution pending requests bind both peer pointer and connection generation",
          "[net][distribution][queue]")
{
    Queue q;
    int client, peer, other_peer, other_client;
    auto r = request(&client, &peer, 7, "weapon:test");
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    REQUIRE(distribRequestSameConnection(&r, &client, &peer, 7));
    REQUIRE_FALSE(distribRequestSameConnection(&r, &client, &peer, 8));
    REQUIRE_FALSE(distribRequestSameConnection(&r, &client, &other_peer, 7));
    REQUIRE_FALSE(distribRequestSameConnection(&r, &other_client, &peer, 7));
    r.connection = 8;
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    distribQueueRemoveClient(&q.value, &client);
    REQUIRE(q.value.count == 0);
}

TEST_CASE("Distribution cancellation preserves other clients and FIFO admission",
          "[net][distribution][queue]")
{
    Queue q;
    int a, b, peer;
    for (int i = 0; i < 6; ++i) {
        auto r = request(i % 2 ? &a : &b, &peer, 1, "");
        std::snprintf(r.id, sizeof(r.id), "asset:%d", i);
        REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    }
    distribQueueRemoveClient(&q.value, &a);
    for (int i = 0; i < 6; i += 2) {
        distrib_request r{};
        REQUIRE(distribQueuePop(&q.value, &r));
        REQUIRE(r.client == &b);
        REQUIRE(r.id[6] == '0' + i);
    }
    REQUIRE(q.value.count == 0);
}

TEST_CASE("Distribution queue rejects invalid identity without consuming capacity",
          "[net][distribution][queue]")
{
    Queue q;
    int a, peer;
    auto r = request(&a, &peer, 1, "asset:test");
    r.peer = nullptr;
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_REJECTED);
    r.peer = &peer;
    std::memset(r.id, 'a', sizeof(r.id));
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_REJECTED);
    REQUIRE(q.value.count == 0);
    REQUIRE(q.value.capacity == 0);
}

TEST_CASE("Distribution per-connection budget preserves admitted work and other peers",
          "[net][distribution][queue]")
{
    Queue q;
    int a, b, peer;
    auto r = request(&a, &peer, 1, "asset:test");
    for (int i = 0; i < DISTRIB_QUEUE_MAX_PER_CONNECTION; ++i) {
        std::snprintf(r.id, sizeof(r.id), "asset:%d", i);
        REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    }
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_DUPLICATE);
    std::snprintf(r.id, sizeof(r.id), "asset:overflow");
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_REJECTED);
    REQUIRE(q.value.count == DISTRIB_QUEUE_MAX_PER_CONNECTION);
    r.client = &b;
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
    distribQueueRemoveClient(&q.value, &a);
    REQUIRE(q.value.count == 1);
    REQUIRE(distribQueuePop(&q.value, &r));
    REQUIRE(r.client == &b);
}

TEST_CASE("Distribution global budget rejects overflow without disturbing FIFO",
          "[net][distribution][queue]")
{
    Queue q;
    int clients[5], peer;
    q.value.entries = static_cast<distrib_request *>(
        std::calloc(DISTRIB_QUEUE_MAX_PENDING, sizeof(distrib_request)));
    REQUIRE(q.value.entries);
    q.value.capacity = q.value.count = DISTRIB_QUEUE_MAX_PENDING;
    for (size_t i = 0; i < q.value.count; ++i) {
        auto r = request(&clients[i / DISTRIB_QUEUE_MAX_PER_CONNECTION], &peer, 1, "");
        std::snprintf(r.id, sizeof(r.id), "asset:%zu", i);
        q.value.entries[i] = r;
    }
    auto r = request(&clients[4], &peer, 1, "asset:overflow");
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_REJECTED);
    REQUIRE(q.value.count == DISTRIB_QUEUE_MAX_PENDING);
    REQUIRE(distribQueuePop(&q.value, &r));
    REQUIRE(std::strcmp(r.id, "asset:0") == 0);
    distribQueueClear(&q.value);
    REQUIRE(q.value.entries == nullptr);
    REQUIRE(q.value.capacity == 0);
    r = request(&clients[4], &peer, 2, "asset:after-reset");
    REQUIRE(distribQueuePush(&q.value, &r) == DISTRIB_QUEUE_ADDED);
}
