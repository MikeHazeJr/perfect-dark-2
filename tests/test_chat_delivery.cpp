#include "catch.hpp"
#include "chat.h"
#include <cstring>
#include <string>
extern "C" {
int chatFixtureReset(void);
void chatFixtureAdvance(u32 milliseconds);
void chatFixtureSetBlocked(int blocked);
void chatFixtureSetLoaded(int loaded);
void chatFixtureSetLocalHandle(u32 handle);
void chatFixtureSetSendFailure(int fail);
int chatFixturePacketCount(void);
const u8 *chatFixturePacket(int index);
int chatFixturePeerFrame(u8 *, u8, u64, u16, u16, const char *, u16);
int chatFixtureSignPeer(u8 *);
int chatFixtureParseId(const char *, u64 *);
int chatFixtureRoundtripText(const char *, char *, u32);
}

TEST_CASE("Chat retries a lost datagram then accepts only an authenticated matching acknowledgment", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    REQUIRE(chatSendText(2002, "Hello") == 0);
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_PENDING);
    REQUIRE(chatFixturePacketCount() == 1);
    chatFixtureAdvance(499);
    REQUIRE(chatFixturePacketCount() == 1);
    chatFixtureAdvance(1);
    REQUIRE(chatFixturePacketCount() == 2);
    REQUIRE(std::memcmp(chatFixturePacket(0), chatFixturePacket(1), 320) == 0);
    u8 ack[320];
    const auto id = chatHistoryAt(2002, 0)->msg_id;
    REQUIRE(chatFixturePeerFrame(ack, 1, id + 1, 0, 0, nullptr, 0));
    chatReceiveFrame(ack, sizeof(ack));
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_PENDING);
    REQUIRE(chatFixturePeerFrame(ack, 1, id, 0, 0, nullptr, 0));
    ack[319] ^= 1;
    chatReceiveFrame(ack, sizeof(ack));
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_PENDING);
    REQUIRE(chatFixtureSignPeer(ack));
    chatReceiveFrame(ack, sizeof(ack));
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_DELIVERED);
    chatFixtureAdvance(16000);
    REQUIRE(chatFixturePacketCount() == 2);
}

TEST_CASE("Chat reorders fragments and reacknowledges retransmission without duplicate history", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    const std::string text(300, 'j');
    u8 first[320], last[320];
    REQUIRE(chatFixturePeerFrame(first, 0, 42, 0, 2, text.data(), 192));
    REQUIRE(chatFixturePeerFrame(last, 0, 42, 1, 2, text.data() + 192, 108));
    chatReceiveFrame(last, sizeof(last));
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePacketCount() == 0);
    chatReceiveFrame(first, sizeof(first));
    REQUIRE(chatHistoryCount(2002) == 1);
    REQUIRE(chatHistoryAt(2002, 0)->text == text);
    REQUIRE(chatFixturePacketCount() == 1);
    REQUIRE(chatFixturePacket(0)[6] == 1);
    chatReceiveFrame(first, sizeof(first));
    chatReceiveFrame(last, sizeof(last));
    REQUIRE(chatHistoryCount(2002) == 1);
    REQUIRE(chatFixturePacketCount() == 2);
}

TEST_CASE("Chat send failure is bounded and visible while oversized or excessive requests are rejected", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    chatFixtureSetSendFailure(1);
    REQUIRE(chatSendText(2002, "No route") == 0);
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_PENDING);
    chatFixtureAdvance(15000);
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_FAILED);
    REQUIRE(chatFixturePacketCount() == 0);
    REQUIRE(chatSendText(2002, std::string(CHAT_TEXT_MAX, 'x').c_str()) == -1);
    for (int i = 0; i < 4; ++i) REQUIRE(chatSendText(2002, "Queued") == 0);
    REQUIRE(chatSendText(2002, "Full") == -1);
    REQUIRE(std::strstr(chatLastSendError(), "pending") != nullptr);
    chatFixtureSetBlocked(1);
    chatFixtureAdvance(1);
    REQUIRE(chatHistoryAt(2002, 4)->delivery == CHAT_DELIVERY_FAILED);
}

TEST_CASE("Chat rejects wrong recipients tampering and pre-agent activity", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    u8 packet[320];
    REQUIRE(chatFixturePeerFrame(packet, 0, 77, 0, 1, "hello", 5));
    packet[12] ^= 1;
    REQUIRE(chatFixtureSignPeer(packet));
    chatReceiveFrame(packet, sizeof(packet));
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePeerFrame(packet, 0, 77, 0, 1, "hello", 5));
    packet[32] ^= 1;
    chatReceiveFrame(packet, sizeof(packet));
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixtureSignPeer(packet));
    chatFixtureSetLoaded(0);
    chatReceiveFrame(packet, sizeof(packet));
    REQUIRE(chatSendText(2002, "Not loaded") == -1);
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePacketCount() == 0);
}

TEST_CASE("Chat message IDs preserve unsigned persistence boundaries", "[net][chat][delivery][T-NETWORKING-011]") {
    u64 id = 0;
    REQUIRE(chatFixtureParseId("18446744073709551615", &id));
    REQUIRE(id == ~(u64)0);
    REQUIRE_FALSE(chatFixtureParseId("18446744073709551616", &id));
    REQUIRE_FALSE(chatFixtureParseId("-1", &id));
}

TEST_CASE("Chat history preserves escaped text and rejects truncated decode", "[net][chat][delivery][T-NETWORKING-011]") {
    const std::string text = "line one\nline two\t\"quoted\" \\ path\r\b\f";
    char decoded[CHAT_TEXT_MAX];
    REQUIRE(chatFixtureRoundtripText(text.c_str(), decoded, sizeof(decoded)));
    REQUIRE(decoded == text);
    REQUIRE_FALSE(chatFixtureRoundtripText(text.c_str(), decoded, 5));
}

TEST_CASE("Chat never retries an old agents message under a newly selected identity", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    REQUIRE(chatSendText(2002, "Original agent") == 0);
    REQUIRE(chatFixturePacketCount() == 1);
    chatFixtureSetLocalHandle(3003);
    chatFixtureAdvance(1000);
    REQUIRE(chatHistoryAt(2002, 0)->delivery == CHAT_DELIVERY_FAILED);
    REQUIRE(chatFixturePacketCount() == 1);
}

TEST_CASE("Chat delivery receipts survive visible history churn", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    u8 packet[320];
    REQUIRE(chatFixturePeerFrame(packet, 0, 99, 0, 1, "Once", 4));
    chatReceiveFrame(packet, sizeof(packet));
    REQUIRE(chatHistoryCount(2002) == 1);
    for (int i = 0; i < CHAT_HISTORY_MAX + 1; ++i)
        REQUIRE(chatHistoryAppendSystem(2002, "A newer event") == 0);
    REQUIRE(chatHistoryAt(2002, 0)->direction == CHAT_DIR_SYS);
    chatReceiveFrame(packet, sizeof(packet));
    REQUIRE(chatHistoryCount(2002) == CHAT_HISTORY_MAX);
    REQUIRE(chatHistoryAt(2002, CHAT_HISTORY_MAX - 1)->direction == CHAT_DIR_SYS);
    REQUIRE(chatFixturePacketCount() == 2);
}

TEST_CASE("Chat rejects conflicting and oversized fragments and expires incomplete messages", "[net][chat][delivery][T-NETWORKING-011]") {
    REQUIRE(chatFixtureReset());
    const std::string part(192, 'a');
    u8 first[320], conflict[320], last[320];
    REQUIRE(chatFixturePeerFrame(first, 0, 123, 0, 2, part.data(), 192));
    REQUIRE(chatFixturePeerFrame(conflict, 0, 123, 0, 2, std::string(192, 'b').data(), 192));
    REQUIRE(chatFixturePeerFrame(last, 0, 123, 1, 2, "end", 3));
    chatReceiveFrame(first, sizeof(first));
    chatReceiveFrame(conflict, sizeof(conflict));
    chatReceiveFrame(last, sizeof(last));
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePacketCount() == 0);
    chatFixtureAdvance(15000);
    chatReceiveFrame(first, sizeof(first));
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePeerFrame(last, 0, 124, 2, 3, part.data(), 128));
    chatReceiveFrame(last, sizeof(last)); // Would produce 512 bytes; no terminator space.
    REQUIRE(chatHistoryCount(2002) == 0);
    REQUIRE(chatFixturePacketCount() == 0);
}
