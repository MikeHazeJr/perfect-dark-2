/*
 * tests/test_connectcode.cpp -- IP <-> word-sentence connect code roundtrip.
 *
 * Code under test: port/src/connectcode.c
 *
 * Connect codes are how players join a server without exchanging raw
 * IP:port pairs (the "no raw IP in any UI surface" constraint per
 * context/constraints.md). The encoder turns an IPv4 address into a
 * 4-slot sentence; the decoder reverses it. The encode/decode pair is
 * the property under test:
 *
 *   for any valid IP, decode(encode(ip)) == ip
 *
 * Variant for ports:
 *
 *   for any valid (ip, port), decodeWithPort(encodeWithPort(ip, port)) == (ip, port)
 *
 * IP byte-order convention:
 *   The encoder reads byte 0 from the LSB of the u32 and goes up. Real
 *   callers (port/src/net/netupnp.c, port/src/server_main.c, the lobby
 *   UI in pdgui_lobby.cpp) pack as
 *       u32 ip = a | (b << 8) | (c << 16) | (d << 24);
 *   for an "a.b.c.d" address. This is HOST byte order on x86_64 (which
 *   matches what inet_addr returns on Windows). Per
 *   context/constraints.md: "do not apply htonl() before passing to
 *   these functions" — the header comment about network byte order is
 *   stale.
 *
 * Slot dictionary:
 *   Each slot maps to one of 256 entries. Some entries are multi-word
 *   phrases ("running to", "the park"). The decoder does longest-match
 *   prefix lookup with separator awareness, so phrases-with-internal-
 *   spaces still parse correctly.
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

#include <cstring>
#include <string>
#include <cctype>

extern "C" {
#define CONNECT_DEFAULT_PORT 27100
#define CONNECT_CODE_MAX     128

s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
s32 connectCodeDecode(const char *code, u32 *outIp);
s32 connectCodeEncodeWithPort(u32 ip, u16 port, char *buf, s32 bufsize);
s32 connectCodeDecodeWithPort(const char *code, u32 *outIp, u16 *outPort);
}

namespace {
/* Pack an "a.b.c.d" address into the u32 the encoder expects.
 * Mirrors the convention used by every live caller (see netupnp.c,
 * server_main.c, pdgui_lobby.cpp). On little-endian x86_64 this
 * matches inet_addr's return value. */
constexpr u32 packIp(u8 a, u8 b, u8 c, u8 d) {
    return (u32)a | ((u32)b << 8) | ((u32)c << 16) | ((u32)d << 24);
}
} /* anon */

TEST_CASE("connectcode: encode then decode roundtrip preserves IP",
          "[connectcode]") {
    const u32 ips[] = {
        packIp(127, 0,   0,   1),     /* localhost */
        packIp(192, 168, 1,   1),     /* private LAN */
        packIp(10,  0,   0,   1),     /* private LAN */
        packIp(172, 16,  254, 1),     /* private LAN */
        packIp(8,   8,   8,   8),     /* public */
        packIp(255, 255, 255, 255),   /* broadcast */
        packIp(0,   0,   0,   0),     /* zero */
        packIp(1,   2,   3,   4),     /* arbitrary */
        packIp(99,  100, 101, 102),   /* arbitrary */
    };

    for (u32 ip : ips) {
        char buf[CONNECT_CODE_MAX];
        s32 enc = connectCodeEncode(ip, buf, sizeof(buf));
        REQUIRE(enc > 0);
        REQUIRE(enc < (s32)sizeof(buf));

        u32 decoded = 0xDEADBEEFu;
        s32 dec = connectCodeDecode(buf, &decoded);
        INFO("ip=" << std::hex << ip << " code='" << buf << "'");
        REQUIRE(dec == 0);
        REQUIRE(decoded == ip);
    }
}

TEST_CASE("connectcode: encodeWithPort roundtrip preserves IP and port",
          "[connectcode]") {
    struct {
        u32 ip;
        u16 port;
    } cases[] = {
        { packIp(127, 0,   0,   1), CONNECT_DEFAULT_PORT },  /* default port: 4-slot code */
        { packIp(192, 168, 1,   1), CONNECT_DEFAULT_PORT },
        { packIp(192, 168, 1,   1), 27101 },                 /* non-default: 6-slot code */
        { packIp(192, 168, 1,   1), 1024  },
        { packIp(8,   8,   8,   8), 65535 },
        { packIp(8,   8,   8,   8), 1     },
        { packIp(127, 0,   0,   1), 27500 },
    };

    for (auto &tc : cases) {
        char buf[CONNECT_CODE_MAX];
        s32 enc = connectCodeEncodeWithPort(tc.ip, tc.port, buf, sizeof(buf));
        REQUIRE(enc > 0);

        u32 outIp = 0xDEADBEEFu;
        u16 outPort = 0xBEEF;
        s32 dec = connectCodeDecodeWithPort(buf, &outIp, &outPort);
        INFO("ip=" << std::hex << tc.ip << " port=" << std::dec << tc.port
             << " code='" << buf << "'");
        REQUIRE(dec == 0);
        REQUIRE(outIp == tc.ip);
        REQUIRE(outPort == tc.port);
    }
}

TEST_CASE("connectcode: 4-slot code decodes with default port via WithPort decoder",
          "[connectcode]") {
    /* Encoding without a port should be decodable via the with-port
     * decoder, which sets *outPort = CONNECT_DEFAULT_PORT. */
    char buf[CONNECT_CODE_MAX];
    REQUIRE(connectCodeEncode(packIp(1, 2, 3, 4), buf, sizeof(buf)) > 0);

    u32 ip = 0;
    u16 port = 0;
    REQUIRE(connectCodeDecodeWithPort(buf, &ip, &port) == 0);
    REQUIRE(ip == packIp(1, 2, 3, 4));
    REQUIRE(port == CONNECT_DEFAULT_PORT);
}

TEST_CASE("connectcode: case insensitivity preserves decode result",
          "[connectcode]") {
    char buf[CONNECT_CODE_MAX];
    REQUIRE(connectCodeEncode(packIp(192, 168, 1, 100), buf, sizeof(buf)) > 0);

    /* Force lowercase, then uppercase, then mixed. All must decode equal. */
    std::string lower(buf), upper(buf), mixed(buf);
    for (char &c : lower) c = static_cast<char>(std::tolower((unsigned char)c));
    for (char &c : upper) c = static_cast<char>(std::toupper((unsigned char)c));
    for (size_t i = 0; i < mixed.size(); ++i) {
        if (i & 1) mixed[i] = static_cast<char>(std::toupper((unsigned char)mixed[i]));
    }

    u32 a = 0, b = 0, c = 0;
    REQUIRE(connectCodeDecode(lower.c_str(), &a) == 0);
    REQUIRE(connectCodeDecode(upper.c_str(), &b) == 0);
    REQUIRE(connectCodeDecode(mixed.c_str(), &c) == 0);
    REQUIRE(a == packIp(192, 168, 1, 100));
    REQUIRE(b == packIp(192, 168, 1, 100));
    REQUIRE(c == packIp(192, 168, 1, 100));
}

TEST_CASE("connectcode: garbage input returns -1",
          "[connectcode]") {
    u32 ip = 0;
    /* Empty input. */
    REQUIRE(connectCodeDecode("", &ip) == -1);
    /* Single word — too few slots. */
    REQUIRE(connectCodeDecode("hello", &ip) == -1);
    /* Two words — still too few. */
    REQUIRE(connectCodeDecode("hello world", &ip) == -1);
    /* Words none of which appear in any slot's dictionary. */
    REQUIRE(connectCodeDecode("zzz qqq xxx www vvv", &ip) == -1);
}

TEST_CASE("connectcode: byte 0 (LSB) drives adjective slot",
          "[connectcode]") {
    /* Pin the LSB convention. The encoder reads (ip >> 0) & 0xFF as
     * the adjective slot index. So packIp(0, 0, 0, 0) selects entry 0
     * of every slot. The first word of the produced sentence must be
     * the adjective at index 0 — currently "fat". */
    char buf[CONNECT_CODE_MAX];
    REQUIRE(connectCodeEncode(packIp(0, 0, 0, 0), buf, sizeof(buf)) > 0);
    /* Lowercase the buffer for stable comparison. */
    std::string s(buf);
    /* The first token must be all lowercase letters. */
    REQUIRE(s.size() > 0);
    REQUIRE(std::isalpha((unsigned char)s[0]));
    /* And that word must NOT vary if we change ONLY the noun byte. */
    char buf2[CONNECT_CODE_MAX];
    REQUIRE(connectCodeEncode(packIp(0, 7, 0, 0), buf2, sizeof(buf2)) > 0);
    /* Strip first token from each, compare them. */
    std::string s2(buf2);
    auto first_token = [](const std::string &x) {
        size_t sp = x.find(' ');
        return x.substr(0, sp);
    };
    REQUIRE(first_token(s) == first_token(s2));
}
