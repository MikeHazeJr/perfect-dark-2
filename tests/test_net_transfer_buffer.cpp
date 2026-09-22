#include "catch.hpp"
#include "net/transfer_buffer.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace {
unsigned allocationCalls;
void *failAllocation(void *, size_t) { ++allocationCalls; return nullptr; }
struct Buffer {
    uint8_t *data = static_cast<uint8_t *>(std::malloc(4));
    uint32_t capacity = 4, length = 4;
    Buffer() { REQUIRE(data); std::memcpy(data, "abcd", 4); }
    ~Buffer() { std::free(data); }
};
}

TEST_CASE("Transfer allocation failure preserves capacity pointer length and bytes",
          "[net][distribution][allocation][T-NETWORKING-011]")
{
    Buffer buffer;
    auto *original = buffer.data;
    const uint8_t incoming[] = {'e', 'f'};
    allocationCalls = 0;
    REQUIRE_FALSE(netTransferBufferAppend(&buffer.data, &buffer.capacity, &buffer.length,
        incoming, sizeof(incoming), 64, failAllocation));
    REQUIRE(allocationCalls == 1);
    REQUIRE(buffer.data == original);
    REQUIRE(buffer.capacity == 4);
    REQUIRE(buffer.length == 4);
    REQUIRE(std::memcmp(buffer.data, "abcd", 4) == 0);
    REQUIRE(netTransferBufferAppend(&buffer.data, &buffer.capacity, &buffer.length,
        incoming, sizeof(incoming), 64, nullptr));
    REQUIRE(buffer.length == 6);
    REQUIRE(std::memcmp(buffer.data, "abcdef", 6) == 0);
}

TEST_CASE("Transfer append rejects excessive lengths and malformed state before allocation",
          "[net][distribution][allocation][T-NETWORKING-011]")
{
    Buffer buffer;
    const uint8_t byte = 'x';
    allocationCalls = 0;
    REQUIRE_FALSE(netTransferBufferAppend(&buffer.data, &buffer.capacity, &buffer.length,
        &byte, UINT32_MAX, 64, failAllocation));
    REQUIRE_FALSE(netTransferBufferAppend(&buffer.data, &buffer.capacity, &buffer.length,
        &byte, 1, 4, failAllocation));
    REQUIRE(allocationCalls == 0);
    REQUIRE(buffer.length == 4);
    REQUIRE(buffer.capacity == 4);
    REQUIRE(std::memcmp(buffer.data, "abcd", 4) == 0);
}

TEST_CASE("Transfer growth respects a non power of two hard limit",
          "[net][distribution][allocation][T-NETWORKING-011]")
{
    Buffer buffer;
    const uint8_t incoming[] = {'e', 'f', 'g'};
    REQUIRE(netTransferBufferAppend(&buffer.data, &buffer.capacity, &buffer.length,
        incoming, sizeof(incoming), 7, nullptr));
    REQUIRE(buffer.capacity == 7);
    REQUIRE(buffer.length == 7);
    REQUIRE(std::memcmp(buffer.data, "abcdefg", 7) == 0);
}

TEST_CASE("Transfer expanded archive reservations count concurrent and completed offers",
          "[net][distribution][budget][T-NETWORKING-011]")
{
    constexpr uint32_t item = 50u * 1024u * 1024u;
    constexpr uint32_t session = 200u * 1024u * 1024u;
    uint32_t reserved = 0;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(netTransferBudgetReserve(&reserved, item, item, session));
        REQUIRE(reserved == static_cast<uint32_t>(i + 1) * item);
    }
    REQUIRE_FALSE(netTransferBudgetReserve(&reserved, 1, item, session));
    REQUIRE(reserved == session);
    REQUIRE_FALSE(netTransferBudgetReserve(&reserved, item + 1, item, session));
    REQUIRE_FALSE(netTransferBudgetReserve(&reserved, 0, item, session));
    REQUIRE_FALSE(netTransferBudgetReserve(nullptr, 1, item, session));
    REQUIRE(reserved == session);

    reserved = UINT32_MAX;
    REQUIRE_FALSE(netTransferBudgetReserve(&reserved, 1, item, session));
    REQUIRE(reserved == UINT32_MAX);
}
